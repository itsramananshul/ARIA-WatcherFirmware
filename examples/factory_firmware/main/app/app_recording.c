#include "app_recording.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "app_audio_recorder.h"
#include "app_audio_player.h"        // audio_wav_header_t
#include "app_device_info.h"         // get_local_service_cfg_type1, CFG_ITEM..., MAX_CALLER
#include "app_voice_interaction.h"   // app_vi_session_is_running
#include "factory_info.h"            // factory_info_eui_get
#include "event_loops.h"
#include "data_defs.h"

#define TAG "app_recording"

#define REC_IN_RATE   16000
// ~15s of 16kHz mono 16-bit PCM per chunk. Two buffers (~0.96MB total) keep us
// well under the ~1MB budget and far from the old 6MB allocation that failed.
#define REC_CHUNK_CAP (480000)

typedef struct {
    int    idx;     // buffer index, or -1 sentinel to stop the sender
    size_t len;
} chunk_msg_t;

static volatile app_recording_state_t s_state = REC_IDLE;
static volatile bool s_stop_req = false;
static volatile bool s_wifi_connected = false;
static int64_t  s_start_us = 0;
static uint32_t s_elapsed_ms_final = 0;
static char     s_status[48] = "Ready";
static char     s_cur_name[40] = {0};
static TaskHandle_t s_task = NULL;

static uint8_t *s_buf[2] = { NULL, NULL };
static SemaphoreHandle_t s_buf_free[2] = { NULL, NULL };
static QueueHandle_t s_send_q = NULL;
static SemaphoreHandle_t s_sender_done = NULL;
static volatile int s_ok_chunks = 0;
static volatile int s_fail_chunks = 0;

static void set_state(app_recording_state_t st, const char *msg)
{
    s_state = st;
    if (msg) snprintf(s_status, sizeof(s_status), "%s", msg);
}

static void fill_wav_header(audio_wav_header_t *h, uint32_t pcm_bytes)
{
    memset(h, 0, sizeof(*h));
    memcpy(h->ChunkID, "RIFF", 4);
    h->ChunkSize = (int32_t)(sizeof(audio_wav_header_t) + pcm_bytes - 8);
    memcpy(h->Format, "WAVE", 4);
    memcpy(h->Subchunk1ID, "fmt ", 4);
    h->Subchunk1Size = 16;
    h->AudioFormat = 1;
    h->NumChannels = 1;
    h->SampleRate = REC_IN_RATE;
    h->BitsPerSample = 16;
    h->ByteRate = h->SampleRate * h->BitsPerSample * h->NumChannels / 8;
    h->BlockAlign = h->BitsPerSample * h->NumChannels / 8;
    memcpy(h->Subchunk2ID, "data", 4);
    h->Subchunk2Size = (int32_t)pcm_bytes;
}

static esp_err_t get_host_token(char *host, size_t host_sz, char *token, size_t token_sz)
{
    host[0] = '\0';
    token[0] = '\0';
    local_service_cfg_type1_t cfg = { .enable = false, .url = NULL };
    esp_err_t ret = get_local_service_cfg_type1(MAX_CALLER, CFG_ITEM_TYPE1_AUDIO_TASK_COMPOSER, &cfg);
    if (ret == ESP_OK && cfg.enable && cfg.url && strlen(cfg.url) > 7) {
        int len = strlen(cfg.url);
        if (cfg.url[len - 1] == '/') cfg.url[len - 1] = '\0';
        snprintf(host, host_sz, "%s", cfg.url);
        if (cfg.token && strlen(cfg.token) > 0) snprintf(token, token_sz, "%s", cfg.token);
    }
    if (cfg.url) free(cfg.url);
    if (cfg.token) free(cfg.token);
    return host[0] ? ESP_OK : ESP_FAIL;
}

// POST one PCM chunk (wrapped as WAV via a streamed 44-byte header) to the
// backend /transcribe endpoint, which transcribes it and appends the text to
// the session's GitHub file.
static bool send_chunk(const uint8_t *pcm, size_t len)
{
    char host[176], token[176];
    if (get_host_token(host, sizeof(host), token, sizeof(token)) != ESP_OK) {
        ESP_LOGE(TAG, "no backend host configured");
        return false;
    }
    char url[224];
    snprintf(url, sizeof(url), "%s/transcribe", host);

    audio_wav_header_t h;
    fill_wav_header(&h, len);
    size_t total = sizeof(h) + len;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) return false;
    esp_http_client_set_header(cl, "Content-Type", "application/octet-stream");
    if (token[0]) esp_http_client_set_header(cl, "Authorization", token);
    const char *eui = factory_info_eui_get();
    if (eui) esp_http_client_set_header(cl, "API-OBITER-DEVICE-EUI", eui);
    esp_http_client_set_header(cl, "x-aria-filename", s_cur_name);

    bool ok = true;
    if (esp_http_client_open(cl, total) != ESP_OK) {
        esp_http_client_cleanup(cl);
        return false;
    }
    if (esp_http_client_write(cl, (const char *)&h, sizeof(h)) != (int)sizeof(h)) ok = false;
    if (ok && esp_http_client_write(cl, (const char *)pcm, len) != (int)len) ok = false;

    int status = -1;
    if (ok) {
        esp_http_client_fetch_headers(cl);
        status = esp_http_client_get_status_code(cl);
    }
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    ESP_LOGI(TAG, "chunk %u bytes -> status %d", (unsigned)len, status);
    return status == 200;
}

// Sender task: drains queued chunks (sequentially, so backend appends in order)
// while the recorder keeps filling the other buffer.
static void sender_task(void *arg)
{
    chunk_msg_t m;
    while (xQueueReceive(s_send_q, &m, portMAX_DELAY) == pdTRUE) {
        if (m.idx < 0) break;   // sentinel
        if (m.len > 0) {
            if (send_chunk(s_buf[m.idx], m.len)) s_ok_chunks++;
            else s_fail_chunks++;
        }
        xSemaphoreGive(s_buf_free[m.idx]);
    }
    xSemaphoreGive(s_sender_done);
    vTaskDelete(NULL);
}

static void recording_task(void *arg)
{
    int active = 0;
    size_t pos = 0;
    s_ok_chunks = 0;
    s_fail_chunks = 0;

    if (app_vi_session_is_running()) { set_state(REC_ERR, "Busy (voice)"); goto done; }
    if (!s_wifi_connected) { set_state(REC_ERR, "Connect WiFi"); goto done; }

    s_buf[0] = heap_caps_malloc(REC_CHUNK_CAP, MALLOC_CAP_SPIRAM);
    s_buf[1] = heap_caps_malloc(REC_CHUNK_CAP, MALLOC_CAP_SPIRAM);
    s_buf_free[0] = xSemaphoreCreateBinary();
    s_buf_free[1] = xSemaphoreCreateBinary();
    s_send_q = xQueueCreate(2, sizeof(chunk_msg_t));
    s_sender_done = xSemaphoreCreateBinary();
    if (!s_buf[0] || !s_buf[1] || !s_buf_free[0] || !s_buf_free[1] || !s_send_q || !s_sender_done) {
        set_state(REC_ERR, "Out of memory");
        goto cleanup;
    }
    xSemaphoreGive(s_buf_free[0]);
    xSemaphoreGive(s_buf_free[1]);

    if (xTaskCreate(sender_task, "rec_send", 8192, NULL, 4, NULL) != pdPASS) {
        set_state(REC_ERR, "Task failed");
        goto cleanup;
    }

    if (app_audio_recorder_stream_start() != ESP_OK) {
        set_state(REC_ERR, "Mic busy");
        // wake the sender so it can exit cleanly
        chunk_msg_t end = { .idx = -1, .len = 0 };
        xQueueSend(s_send_q, &end, portMAX_DELAY);
        xSemaphoreTake(s_sender_done, portMAX_DELAY);
        goto cleanup;
    }

    xSemaphoreTake(s_buf_free[active], portMAX_DELAY);   // claim the first buffer
    s_start_us = esp_timer_get_time();
    set_state(REC_RECORDING, "Recording");

    while (!s_stop_req) {
        size_t len = 0;
        uint8_t *p = app_audio_recorder_stream_recv(&len, pdMS_TO_TICKS(200));
        if (p == NULL || len == 0) continue;

        if (pos + len > REC_CHUNK_CAP) {
            // hand the full buffer to the sender, switch to the other one
            chunk_msg_t msg = { .idx = active, .len = pos };
            xQueueSend(s_send_q, &msg, portMAX_DELAY);
            active ^= 1;
            xSemaphoreTake(s_buf_free[active], portMAX_DELAY);  // wait until free (sender is fast)
            pos = 0;
        }
        memcpy(s_buf[active] + pos, p, len);
        pos += len;
        app_audio_recorder_stream_free(p);
    }

    app_audio_recorder_stream_stop();
    s_elapsed_ms_final = (uint32_t)((esp_timer_get_time() - s_start_us) / 1000);
    set_state(REC_UPLOADING, "Finishing");

    // flush the final partial chunk, then stop the sender and wait for drain
    if (pos > 0) {
        chunk_msg_t msg = { .idx = active, .len = pos };
        xQueueSend(s_send_q, &msg, portMAX_DELAY);
    }
    chunk_msg_t end = { .idx = -1, .len = 0 };
    xQueueSend(s_send_q, &end, portMAX_DELAY);
    xSemaphoreTake(s_sender_done, portMAX_DELAY);

    if (s_ok_chunks > 0 && s_fail_chunks == 0) {
        set_state(REC_DONE_UPLOADED, "Saved to GitHub");
    } else if (s_ok_chunks > 0) {
        set_state(REC_DONE_UPLOADED, "Saved (some lost)");
    } else {
        set_state(REC_ERR, "Upload failed");
    }

cleanup:
    if (s_buf[0]) { free(s_buf[0]); s_buf[0] = NULL; }
    if (s_buf[1]) { free(s_buf[1]); s_buf[1] = NULL; }
    if (s_buf_free[0]) { vSemaphoreDelete(s_buf_free[0]); s_buf_free[0] = NULL; }
    if (s_buf_free[1]) { vSemaphoreDelete(s_buf_free[1]); s_buf_free[1] = NULL; }
    if (s_send_q) { vQueueDelete(s_send_q); s_send_q = NULL; }
    if (s_sender_done) { vSemaphoreDelete(s_sender_done); s_sender_done = NULL; }
done:
    s_task = NULL;
    vTaskDelete(NULL);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != VIEW_EVENT_WIFI_ST || data == NULL) return;
    struct view_data_wifi_st *st = (struct view_data_wifi_st *)data;
    s_wifi_connected = st->is_connected;
}

// ── Chat session flush ────────────────────────────────────────────────────
// Tell the backend the chat conversation ended so it commits the transcript to
// GitHub now (otherwise it flushes on gap detection). Fire-and-forget task.
static void chat_end_task(void *arg)
{
    char host[176], token[176];
    if (get_host_token(host, sizeof(host), token, sizeof(token)) == ESP_OK) {
        char url[224];
        snprintf(url, sizeof(url), "%s/chat_end", host);
        esp_http_client_config_t cfg = {
            .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 15000,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        esp_http_client_handle_t cl = esp_http_client_init(&cfg);
        if (cl) {
            esp_http_client_set_header(cl, "Content-Type", "application/json");
            if (token[0]) esp_http_client_set_header(cl, "Authorization", token);
            const char *eui = factory_info_eui_get();
            if (eui) esp_http_client_set_header(cl, "API-OBITER-DEVICE-EUI", eui);
            esp_err_t e = esp_http_client_perform(cl);
            ESP_LOGI(TAG, "chat_end -> %d", (e == ESP_OK) ? esp_http_client_get_status_code(cl) : -1);
            esp_http_client_cleanup(cl);
        }
    }
    vTaskDelete(NULL);
}

void app_chat_end_flush(void)
{
    xTaskCreate(chat_end_task, "chat_end", 6144, NULL, 3, NULL);
}

esp_err_t app_recording_init(void)
{
    esp_event_handler_register_with(app_event_loop_handle, VIEW_EVENT_BASE,
                                    VIEW_EVENT_WIFI_ST, on_wifi_event, NULL);
    ESP_LOGI(TAG, "recording ready");
    return ESP_OK;
}

bool app_recording_is_active(void)
{
    return s_state == REC_RECORDING || s_state == REC_FINALIZING || s_state == REC_UPLOADING;
}

esp_err_t app_recording_start(void)
{
    if (app_recording_is_active()) return ESP_ERR_INVALID_STATE;
    if (app_vi_session_is_running()) { set_state(REC_ERR, "Busy (voice)"); return ESP_FAIL; }
    s_stop_req = false;
    s_elapsed_ms_final = 0;
    snprintf(s_cur_name, sizeof(s_cur_name), "rec_%lld.md", (long long)time(NULL));
    set_state(REC_RECORDING, "Starting");
    if (xTaskCreate(recording_task, "rec_task", 6144, NULL, 4, &s_task) != pdPASS) {
        set_state(REC_ERR, "Task failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t app_recording_stop(void)
{
    if (s_state != REC_RECORDING) return ESP_ERR_INVALID_STATE;
    s_stop_req = true;
    return ESP_OK;
}

app_recording_state_t app_recording_get_state(void) { return s_state; }

uint32_t app_recording_elapsed_ms(void)
{
    if (s_state == REC_RECORDING) return (uint32_t)((esp_timer_get_time() - s_start_us) / 1000);
    return s_elapsed_ms_final;
}

const char *app_recording_status_text(void) { return s_status; }
