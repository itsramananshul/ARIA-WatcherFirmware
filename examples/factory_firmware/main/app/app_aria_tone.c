#include "app_aria_tone.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "storage.h"
#include "factory_info.h"        // factory_info_eui_get
#include "app_device_info.h"     // get_local_service_cfg_type1, local_service_cfg_type1_t
#include "app_audio_player.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "aria_tone";

#define TONE_FILE     "/spiffs/aria_tone.wav"
#define KEY_SEL       "aria_tone"     // NVS: selected tone filename
#define KEY_CACHED    "aria_tone_cd"  // NVS: filename currently cached on disk
#define DEFAULT_TONE  "water_drops.wav"
#define MAX_TONES     24

static char s_selected[48] = DEFAULT_TONE;

// Cached live tone list (refreshed in the background by the sync task) so the
// on-device picker never blocks on the network.
static char s_list[MAX_TONES][48];
static int  s_list_count = 0;
static SemaphoreHandle_t s_list_mtx = NULL;
static volatile bool s_sel_busy = false;

static void list_lock(void)   { if (s_list_mtx) xSemaphoreTake(s_list_mtx, portMAX_DELAY); }
static void list_unlock(void) { if (s_list_mtx) xSemaphoreGive(s_list_mtx); }

static void tone_nvs_get(const char *key, char *out, size_t out_sz, const char *def)
{
    char buf[48] = {0}; size_t len = sizeof(buf);
    if (storage_read((char *)key, buf, &len) == ESP_OK && buf[0]) snprintf(out, out_sz, "%s", buf);
    else snprintf(out, out_sz, "%s", def);
}

static void tone_nvs_set(const char *key, const char *val)
{
    storage_write((char *)key, (void *)val, strlen(val) + 1);
}

void aria_tone_init(void)
{
    if (!s_list_mtx) s_list_mtx = xSemaphoreCreateMutex();
    tone_nvs_get(KEY_SEL, s_selected, sizeof(s_selected), DEFAULT_TONE);
    ESP_LOGI(TAG, "selected tone: %s", s_selected);
}

const char *aria_tone_selected(void) { return s_selected; }

// Same localservice host+token the rest of the firmware uses.
static esp_err_t get_host_token(char *host, size_t hs, char *token, size_t ts)
{
    host[0] = 0; token[0] = 0;
    local_service_cfg_type1_t cfg = { .enable = false, .url = NULL };
    esp_err_t ret = get_local_service_cfg_type1(MAX_CALLER, CFG_ITEM_TYPE1_AUDIO_TASK_COMPOSER, &cfg);
    if (ret == ESP_OK && cfg.enable && cfg.url && strlen(cfg.url) > 7) {
        int len = strlen(cfg.url);
        if (cfg.url[len - 1] == '/') cfg.url[len - 1] = 0;
        snprintf(host, hs, "%s", cfg.url);
        if (cfg.token && strlen(cfg.token) > 0) snprintf(token, ts, "%s", cfg.token);
    }
    if (cfg.url) free(cfg.url);
    if (cfg.token) free(cfg.token);
    return host[0] ? ESP_OK : ESP_FAIL;
}

static bool download_tone(const char *name)
{
    char host[176], token[176];
    if (get_host_token(host, sizeof(host), token, sizeof(token)) != ESP_OK) return false;
    char url[288];
    snprintf(url, sizeof(url), "%s/tone?name=%s", host, name);

    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_GET, .timeout_ms = 20000, .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) return false;
    if (token[0]) esp_http_client_set_header(cl, "Authorization", token);
    const char *eui = factory_info_eui_get();
    if (eui) esp_http_client_set_header(cl, "API-OBITER-DEVICE-EUI", eui);

    bool ok = false;
    if (esp_http_client_open(cl, 0) == ESP_OK) {
        int clen = esp_http_client_fetch_headers(cl);
        int status = esp_http_client_get_status_code(cl);
        if (status == 200 && clen > 44 && clen < 4 * 1024 * 1024) {
            uint8_t *buf = heap_caps_malloc(clen, MALLOC_CAP_SPIRAM);
            if (buf) {
                int total = 0, rd;
                while (total < clen && (rd = esp_http_client_read(cl, (char *)buf + total, clen - total)) > 0) total += rd;
                if (total > 44 && storage_file_write(TONE_FILE, buf, total) == ESP_OK) {
                    ok = true;
                    ESP_LOGI(TAG, "cached %s (%d bytes)", name, total);
                }
                free(buf);
            }
        } else {
            ESP_LOGW(TAG, "tone download http %d, clen %d", status, clen);
        }
    }
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    return ok;
}

void aria_tone_ensure_cached(void)
{
    char cached[48]; tone_nvs_get(KEY_CACHED, cached, sizeof(cached), "");
    size_t sz = 0;
    bool have_file = (storage_file_size_get(TONE_FILE, &sz) == ESP_OK && sz > 44);
    if (have_file && strcmp(cached, s_selected) == 0) return;   // already cached
    if (download_tone(s_selected)) tone_nvs_set(KEY_CACHED, s_selected);
}

void aria_tone_apply(const char *name)
{
    if (!name || !name[0]) return;
    if (strcmp(name, s_selected) != 0) {
        snprintf(s_selected, sizeof(s_selected), "%s", name);
        tone_nvs_set(KEY_SEL, s_selected);
        ESP_LOGI(TAG, "tone -> %s", s_selected);
    }
    aria_tone_ensure_cached();
}

// TEMPORARILY DISABLED: playing the tone (a 24 kHz file) reconfigures the shared
// ES8311 codec clock, which corrupts the continuously-running mic/wakenet feed and
// breaks the next recording's upload ("http write error" + AFE Fetch Fail). Until
// the tone is made codec-safe (match the mic rate / save+restore codec fs), keep it off.
static bool s_play_enabled = false;

void aria_tone_play(void)
{
    if (!s_play_enabled) return;
    size_t sz = 0;
    if (storage_file_size_get(TONE_FILE, &sz) != ESP_OK || sz <= 44) return; // not cached yet
    app_audio_player_file((void *)TONE_FILE);
}

void aria_tone_stop(void)
{
    app_audio_player_stop();
}

// ── On-device picker support ──────────────────────────────────────────────

// GET <host>/tones -> cache the live list of tone filenames.
void aria_tone_refresh_list(void)
{
    char host[176], token[176];
    if (get_host_token(host, sizeof(host), token, sizeof(token)) != ESP_OK) return;
    char url[224];
    snprintf(url, sizeof(url), "%s/tones", host);

    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_GET, .timeout_ms = 15000, .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) return;
    if (token[0]) esp_http_client_set_header(cl, "Authorization", token);
    const char *eui = factory_info_eui_get();
    if (eui) esp_http_client_set_header(cl, "API-OBITER-DEVICE-EUI", eui);

    char *body = NULL;
    if (esp_http_client_open(cl, 0) == ESP_OK) {
        int clen = esp_http_client_fetch_headers(cl);
        if (esp_http_client_get_status_code(cl) == 200) {
            int cap = (clen > 0 && clen < 8192) ? clen + 1 : 4096;
            body = malloc(cap);
            if (body) {
                int total = 0, rd;
                while (total < cap - 1 && (rd = esp_http_client_read(cl, body + total, cap - 1 - total)) > 0) total += rd;
                body[total] = 0;
            }
        }
    }
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    if (!body) return;

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return;
    cJSON *tones = cJSON_GetObjectItem(root, "tones");
    if (cJSON_IsArray(tones)) {
        list_lock();
        int n = 0;
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, tones) {
            if (n >= MAX_TONES) break;
            if (cJSON_IsString(it) && it->valuestring && it->valuestring[0])
                snprintf(s_list[n++], 48, "%s", it->valuestring);
        }
        s_list_count = n;
        list_unlock();
        ESP_LOGI(TAG, "tone list refreshed: %d", n);
    }
    cJSON_Delete(root);
}

int aria_tone_get_list(char (*out)[48], int max)
{
    list_lock();
    int n = (s_list_count < max) ? s_list_count : max;
    for (int i = 0; i < n; i++) snprintf(out[i], 48, "%s", s_list[i]);
    list_unlock();
    return n;
}

// Background: stop any current playback, cache the newly-selected tone, preview it.
static void tone_select_task(void *arg)
{
    (void)arg;
    aria_tone_stop();          // stop any prior preview cleanly
    aria_tone_ensure_cached(); // download the now-selected tone (uses s_selected)
    aria_tone_play();          // preview
    s_sel_busy = false;
    vTaskDelete(NULL);
}

void aria_tone_select_async(const char *name)
{
    if (!name || !name[0]) return;
    snprintf(s_selected, sizeof(s_selected), "%s", name);
    tone_nvs_set(KEY_SEL, s_selected);
    ESP_LOGI(TAG, "tone (device pick) -> %s", s_selected);
    if (s_sel_busy) return;    // one already in flight; it reads the latest s_selected
    s_sel_busy = true;
    if (xTaskCreate(tone_select_task, "tone_sel", 5120, NULL, 4, NULL) != pdPASS) s_sel_busy = false;
}
