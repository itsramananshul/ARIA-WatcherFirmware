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

static const char *TAG = "aria_tone";

#define TONE_FILE     "/spiffs/aria_tone.wav"
#define KEY_SEL       "aria_tone"     // NVS: selected tone filename
#define KEY_CACHED    "aria_tone_cd"  // NVS: filename currently cached on disk
#define DEFAULT_TONE  "water_drops.wav"

static char s_selected[48] = DEFAULT_TONE;

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

void aria_tone_play(void)
{
    size_t sz = 0;
    if (storage_file_size_get(TONE_FILE, &sz) != ESP_OK || sz <= 44) return; // not cached yet
    app_audio_player_file((void *)TONE_FILE);
}

void aria_tone_stop(void)
{
    app_audio_player_stop();
}
