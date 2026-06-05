#include "app_device_sync.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "cJSON.h"

#include "sensecap-watcher.h"        // bsp_codec_volume_set, bsp_lcd_brightness_set
#include "storage.h"                 // NVS aria_voice / aria_eng / ...
#include "app_device_info.h"         // get_local_service_cfg_type1
#include "app_voice_interaction.h"   // app_vi_session_is_running
#include "app_recording.h"           // app_recording_start/stop
#include "app_audio_recorder.h"      // ARIA: wake-word toggle setter
#include "app_aria_tone.h"           // ARIA: thinking-tone select + cache
#include "app_aria_cam.h"            // aria_cam_capture (take_photo)
#include "app_audio_player.h"        // play WAV (speak)
#include "factory_info.h"
#include "event_loops.h"
#include "data_defs.h"

#define TAG "app_device_sync"
#define SYNC_PERIOD_MS  15000
#define RESP_MAX        2048

static const char *kAriaVoices[] = { "Kore", "Sulafat", "Despina", "Leda" };

static volatile bool s_wifi_connected = false;
static int64_t s_applied_rev = -1;

// Pending command results to report on the next heartbeat.
#define MAX_RESULTS 8
static struct { long long id; const char *status; } s_results[MAX_RESULTS];
static int s_nresults = 0;

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

static void apply_config(cJSON *config)
{
    cJSON *rev = cJSON_GetObjectItem(config, "config_rev");
    int64_t cur_rev = rev && cJSON_IsNumber(rev) ? (int64_t)rev->valuedouble : -1;
    if (cur_rev == s_applied_rev) return;   // nothing changed

    ESP_LOGI(TAG, "applying config rev %lld", (long long)cur_rev);

    cJSON *voice = cJSON_GetObjectItem(config, "voice");
    if (voice && cJSON_IsString(voice)) {
        for (uint8_t i = 0; i < 4; i++) {
            if (strcmp(voice->valuestring, kAriaVoices[i]) == 0) {
                storage_write("aria_voice", &i, sizeof(i));
                ESP_LOGI(TAG, "  voice -> %s", kAriaVoices[i]);
                break;
            }
        }
    }
    cJSON *engine = cJSON_GetObjectItem(config, "tts_engine");
    if (engine && cJSON_IsString(engine)) {
        uint8_t eng = (strcmp(engine->valuestring, "live") == 0) ? 1 : 0;
        storage_write("aria_eng", &eng, sizeof(eng));
        ESP_LOGI(TAG, "  engine -> %s", eng ? "live" : "current");
    }
    cJSON *vol = cJSON_GetObjectItem(config, "volume");
    if (vol && cJSON_IsNumber(vol)) {
        int v = vol->valueint; if (v < 0) v = 0; if (v > 100) v = 100;
        bsp_codec_volume_set(v, NULL);
        ESP_LOGI(TAG, "  volume -> %d", v);
    }
    cJSON *bri = cJSON_GetObjectItem(config, "brightness");
    if (bri && cJSON_IsNumber(bri)) {
        int b = bri->valueint; if (b < 5) b = 5; if (b > 100) b = 100;
        bsp_lcd_brightness_set(b);
        ESP_LOGI(TAG, "  brightness -> %d", b);
    }
    // feature toggles: persisted for the features to read (camera/recording/chat)
    cJSON *cv = cJSON_GetObjectItem(config, "camera_vision");
    if (cv) { uint8_t on = cJSON_IsTrue(cv) ? 1 : 0; storage_write("aria_cam_en", &on, sizeof(on)); }
    cJSON *rc = cJSON_GetObjectItem(config, "recording");
    if (rc) { uint8_t on = cJSON_IsTrue(rc) ? 1 : 0; storage_write("aria_rec_en", &on, sizeof(on)); }
    cJSON *cl = cJSON_GetObjectItem(config, "chat_logging");
    if (cl) { uint8_t on = cJSON_IsTrue(cl) ? 1 : 0; storage_write("aria_chat_en", &on, sizeof(on)); }
    // ARIA: hands-free wake word ("Sophia") — apply live (also persists to NVS).
    cJSON *ww = cJSON_GetObjectItem(config, "wake_word");
    if (ww) { app_audio_recorder_set_wakeword(cJSON_IsTrue(ww)); }
    // ARIA: selected "thinking" tone — set + (re)download/cache the WAV.
    cJSON *tn = cJSON_GetObjectItem(config, "tone");
    if (tn && cJSON_IsString(tn) && tn->valuestring[0]) { aria_tone_apply(tn->valuestring); }

    s_applied_rev = cur_rev;
}

static void add_result(long long id, const char *status)
{
    if (s_nresults < MAX_RESULTS) {
        s_results[s_nresults].id = id;
        s_results[s_nresults].status = status;
        s_nresults++;
    }
}

static bool s_reboot_pending = false;

// Capture a frame and POST it to the backend photos/ gallery.
static bool cmd_take_photo(void)
{
    char *b64 = NULL;
    int b64len = 0;
    if (aria_cam_capture(&b64, &b64len, 8000) != ESP_OK || !b64 || b64len <= 0) {
        if (b64) free(b64);
        return false;
    }
    char host[176], token[176];
    if (get_host_token(host, sizeof(host), token, sizeof(token)) != ESP_OK) { free(b64); return false; }
    char url[224];
    snprintf(url, sizeof(url), "%s/photo", host);

    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 30000, .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) { free(b64); return false; }
    esp_http_client_set_header(cl, "Content-Type", "application/octet-stream");
    if (token[0]) esp_http_client_set_header(cl, "Authorization", token);
    const char *eui = factory_info_eui_get();
    if (eui) esp_http_client_set_header(cl, "API-OBITER-DEVICE-EUI", eui);
    esp_http_client_set_header(cl, "x-aria-label", "photo");

    int status = -1;
    if (esp_http_client_open(cl, b64len) == ESP_OK && esp_http_client_write(cl, b64, b64len) == b64len) {
        esp_http_client_fetch_headers(cl);
        status = esp_http_client_get_status_code(cl);
    }
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    free(b64);
    ESP_LOGI(TAG, "take_photo -> %d", status);
    return status == 200;
}

// Play a complete WAV buffer through the audio stream player.
static void play_wav(uint8_t *wav, size_t len)
{
    app_audio_player_stream_init(len);
    size_t off = 0;
    const size_t chunk = 8192;
    bool started = false;
    while (off < len) {
        size_t n = (len - off) > chunk ? chunk : (len - off);
        app_audio_player_stream_send(wav + off, n, pdMS_TO_TICKS(20000));
        off += n;
        if (!started && off >= 16000) { app_audio_player_stream_start(); started = true; }
    }
    if (!started) app_audio_player_stream_start();
    app_audio_player_stream_finish();
    while (app_audio_player_status_get() == AUDIO_PLAYER_STATUS_PLAYING_STREAM) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Fetch TTS for `text` from the backend and play it out loud.
static bool cmd_speak(const char *text)
{
    if (!text || !text[0]) return false;
    char host[176], token[176];
    if (get_host_token(host, sizeof(host), token, sizeof(token)) != ESP_OK) return false;
    char url[224];
    snprintf(url, sizeof(url), "%s/speak", host);

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "text", text);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body) return false;

    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 30000, .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) { free(body); return false; }
    esp_http_client_set_header(cl, "Content-Type", "application/json");
    if (token[0]) esp_http_client_set_header(cl, "Authorization", token);

    bool ok = false;
    if (esp_http_client_open(cl, strlen(body)) == ESP_OK && esp_http_client_write(cl, body, strlen(body)) >= 0) {
        int clen = esp_http_client_fetch_headers(cl);
        int status = esp_http_client_get_status_code(cl);
        if (status == 200 && clen > 44) {
            uint8_t *wav = heap_caps_malloc(clen, MALLOC_CAP_SPIRAM);
            if (wav) {
                int total = 0, rd;
                while (total < clen && (rd = esp_http_client_read(cl, (char *)wav + total, clen - total)) > 0) total += rd;
                if (total > 44) { play_wav(wav, total); ok = true; }
                free(wav);
            }
        }
        ESP_LOGI(TAG, "speak -> http %d, %d bytes", status, clen);
    }
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    free(body);
    return ok;
}

static void run_commands(cJSON *commands)
{
    cJSON *cmd = NULL;
    cJSON_ArrayForEach(cmd, commands) {
        cJSON *jid = cJSON_GetObjectItem(cmd, "id");
        cJSON *jname = cJSON_GetObjectItem(cmd, "command");
        if (!jid || !jname || !cJSON_IsString(jname)) continue;
        long long id = (long long)jid->valuedouble;
        const char *name = jname->valuestring;
        ESP_LOGI(TAG, "command #%lld: %s", id, name);

        if (strcmp(name, "reboot") == 0) {
            add_result(id, "done");
            s_reboot_pending = true;   // report results this round, then reboot
        } else if (strcmp(name, "record_start") == 0) {
            esp_err_t r = app_recording_start();
            add_result(id, r == ESP_OK ? "done" : "failed");
        } else if (strcmp(name, "record_stop") == 0) {
            esp_err_t r = app_recording_stop();
            add_result(id, r == ESP_OK ? "done" : "failed");
        } else if (strcmp(name, "take_photo") == 0) {
            add_result(id, cmd_take_photo() ? "done" : "failed");
        } else if (strcmp(name, "speak") == 0) {
            cJSON *args = cJSON_GetObjectItem(cmd, "args");
            cJSON *txt = args ? cJSON_GetObjectItem(args, "text") : NULL;
            add_result(id, cmd_speak((txt && cJSON_IsString(txt)) ? txt->valuestring : "") ? "done" : "failed");
        } else {
            add_result(id, "failed");   // unknown command
        }
    }
}

// Build the report body: {reported:{...}, results:[{id,status}...]}
static char *build_body(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *rep = cJSON_CreateObject();
    const esp_app_desc_t *desc = esp_app_get_description();
    cJSON_AddStringToObject(rep, "fw", desc ? desc->version : "aria");
    cJSON_AddNumberToObject(rep, "applied_rev", (double)s_applied_rev);
    cJSON_AddNumberToObject(rep, "free_heap", (double)esp_get_free_heap_size());
    cJSON_AddItemToObject(root, "reported", rep);

    if (s_nresults > 0) {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < s_nresults; i++) {
            cJSON *r = cJSON_CreateObject();
            cJSON_AddNumberToObject(r, "id", (double)s_results[i].id);
            cJSON_AddStringToObject(r, "status", s_results[i].status);
            cJSON_AddItemToArray(arr, r);
        }
        cJSON_AddItemToObject(root, "results", arr);
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static void sync_once(void)
{
    char host[176], token[176];
    if (get_host_token(host, sizeof(host), token, sizeof(token)) != ESP_OK) return;

    char url[224];
    snprintf(url, sizeof(url), "%s/device_sync", host);

    char *body = build_body();
    if (!body) return;

    char *resp = heap_caps_malloc(RESP_MAX, MALLOC_CAP_SPIRAM);
    if (!resp) { free(body); return; }

    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) { free(body); free(resp); return; }
    esp_http_client_set_header(cl, "Content-Type", "application/json");
    if (token[0]) esp_http_client_set_header(cl, "Authorization", token);
    const char *eui = factory_info_eui_get();
    if (eui) esp_http_client_set_header(cl, "API-OBITER-DEVICE-EUI", eui);

    int resp_len = 0;
    esp_err_t err = esp_http_client_open(cl, strlen(body));
    if (err == ESP_OK && esp_http_client_write(cl, body, strlen(body)) >= 0) {
        esp_http_client_fetch_headers(cl);
        int rd = esp_http_client_read_response(cl, resp, RESP_MAX - 1);
        if (rd > 0) resp_len = rd;
    }
    int status = esp_http_client_get_status_code(cl);
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    free(body);

    if (status == 200 && resp_len > 0) {
        resp[resp_len] = '\0';
        // results were accepted; clear the pending list
        s_nresults = 0;
        cJSON *root = cJSON_Parse(resp);
        if (root) {
            cJSON *config = cJSON_GetObjectItem(root, "config");
            if (config && cJSON_IsObject(config)) apply_config(config);
            cJSON *commands = cJSON_GetObjectItem(root, "commands");
            if (commands && cJSON_IsArray(commands)) run_commands(commands);
            cJSON_Delete(root);
        }
    } else {
        ESP_LOGW(TAG, "sync http %d (len %d)", status, resp_len);
    }
    free(resp);
}

static void sync_task(void *arg)
{
    // small initial delay so the network + config are up
    vTaskDelay(pdMS_TO_TICKS(8000));
    while (1) {
        if (s_wifi_connected && !app_vi_session_is_running()) {
            sync_once();
            // ARIA: make sure the selected thinking tone is cached on disk (no-op
            // once cached; downloads after boot / when the selection changes).
            aria_tone_ensure_cached();
            if (s_reboot_pending) {
                // CRITICAL: report the reboot result FIRST (a second sync POSTs
                // the pending {id,done} so the backend marks it done) — otherwise
                // it stays 'pending' and we reboot-loop after every restart.
                s_reboot_pending = false;
                sync_once();
                ESP_LOGW(TAG, "reboot command acknowledged -> restarting");
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SYNC_PERIOD_MS));
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != VIEW_EVENT_WIFI_ST || data == NULL) return;
    struct view_data_wifi_st *st = (struct view_data_wifi_st *)data;
    s_wifi_connected = st->is_connected;
}

esp_err_t app_device_sync_init(void)
{
    esp_event_handler_register_with(app_event_loop_handle, VIEW_EVENT_BASE,
                                    VIEW_EVENT_WIFI_ST, on_wifi_event, NULL);
    xTaskCreate(sync_task, "dev_sync", 8192, NULL, 3, NULL);
    ESP_LOGI(TAG, "device sync ready");
    return ESP_OK;
}
