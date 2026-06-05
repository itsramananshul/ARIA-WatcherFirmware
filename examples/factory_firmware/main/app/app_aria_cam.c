#include "app_aria_cam.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "sensecap-watcher.h"      // bsp_sscma_client_init()
#include "sscma_client_ops.h"
#include "sscma_client_types.h"

// Camera sensor opt id for 640x480 (mirrors TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_640_480 == 3)
#define ARIA_CAM_RESOLUTION_640_480   3

static const char *TAG = "aria_cam";

static sscma_client_handle_t s_client = NULL;
static SemaphoreHandle_t s_done = NULL;
static volatile bool s_capturing = false;
static char *s_b64 = NULL;
static int s_b64_len = 0;

// Event callback: the Himax delivers sampled frames here. We only act when a
// capture is in flight, pull the base64 JPEG out of the reply, and hand it off.
static void __aria_cam_on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *ctx)
{
    if (!s_capturing) {
        return;
    }
    char *img = NULL;
    int img_size = 0;
    if (sscma_utils_fetch_image_from_reply(reply, &img, &img_size) == ESP_OK && img && img_size > 0) {
        s_b64 = img;            // take ownership of the strdup'd base64 string
        s_b64_len = img_size;
        s_capturing = false;
        xSemaphoreGive(s_done);
    }
}

esp_err_t app_aria_cam_init(void)
{
    if (s_client) {
        return ESP_OK;
    }

    s_done = xSemaphoreCreateBinary();
    if (!s_done) {
        return ESP_ERR_NO_MEM;
    }

    // board_init() already ran bsp_sscma_client_init() + sscma_client_init();
    // this returns the same singleton handle. We just attach our callback.
    s_client = bsp_sscma_client_init();
    if (!s_client) {
        ESP_LOGE(TAG, "bsp_sscma_client_init() returned NULL");
        return ESP_FAIL;
    }

    const sscma_client_callback_t cb = {
        .on_connect = NULL,
        .on_disconnect = NULL,
        .on_response = NULL,
        .on_event = __aria_cam_on_event,
        .on_log = NULL,
    };
    if (sscma_client_register_callback(s_client, &cb, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "sscma_client_register_callback failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "aria_cam ready (Himax handle %p)", s_client);
    return ESP_OK;
}

esp_err_t aria_cam_capture(char **out_b64, int *out_len, uint32_t timeout_ms)
{
    if (!out_b64 || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    if (app_aria_cam_init() != ESP_OK) {
        return ESP_FAIL;
    }

    // Clear any previous result and drain a stale completion signal.
    if (s_b64) {
        free(s_b64);
        s_b64 = NULL;
    }
    s_b64_len = 0;
    xSemaphoreTake(s_done, 0);

    s_capturing = true;
    sscma_client_break(s_client);   // stop any ongoing stream
    sscma_client_set_sensor(s_client, 1, ARIA_CAM_RESOLUTION_640_480, true);
    if (sscma_client_sample(s_client, 1) != ESP_OK) {
        ESP_LOGE(TAG, "sscma_client_sample failed");
        s_capturing = false;
        return ESP_FAIL;
    }

    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "capture timeout (%ums)", (unsigned)timeout_ms);
        s_capturing = false;
        return ESP_ERR_TIMEOUT;
    }

    if (!s_b64 || s_b64_len <= 0) {
        ESP_LOGE(TAG, "no image in reply");
        return ESP_FAIL;
    }

    *out_b64 = s_b64;
    *out_len = s_b64_len;
    s_b64 = NULL;       // ownership transferred to caller
    s_b64_len = 0;
    ESP_LOGI(TAG, "captured base64 JPEG: %d chars (~%.1f KB raw)", *out_len, (*out_len * 0.75f) / 1024.0f);
    return ESP_OK;
}

// ---- temporary self-test: ~15s after boot, capture one frame and log its size ----
static void __aria_cam_selftest_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(15000));
    ESP_LOGW(TAG, "[selftest] attempting one-shot capture...");
    char *b64 = NULL;
    int len = 0;
    esp_err_t r = aria_cam_capture(&b64, &len, 8000);
    if (r == ESP_OK) {
        ESP_LOGW(TAG, "[selftest] SUCCESS: %d base64 chars; head: %.48s", len, b64);
        free(b64);
    } else {
        ESP_LOGE(TAG, "[selftest] FAILED: %s", esp_err_to_name(r));
    }
    vTaskDelete(NULL);
}

void app_aria_cam_selftest_start(void)
{
    xTaskCreate(__aria_cam_selftest_task, "aria_cam_test", 4096, NULL, 3, NULL);
}
