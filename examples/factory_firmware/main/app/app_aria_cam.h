#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ARIA's on-demand camera capture.
 *
 * Grabs the (already-initialized) shared Himax sscma_client handle and registers
 * an event callback used to receive captured frames. Safe to call multiple times.
 * Must be called after board_init() has run sscma_client_init().
 */
esp_err_t app_aria_cam_init(void);

/**
 * @brief Capture a single JPEG frame from the Himax camera.
 *
 * Blocks until a frame arrives or the timeout elapses. On success, *out_b64
 * points to a malloc'd, NUL-terminated base64 string (the JPEG bytes, base64
 * encoded) that the CALLER MUST free(). *out_len is strlen(*out_b64).
 *
 * @param out_b64      receives the malloc'd base64 string (caller frees)
 * @param out_len      receives strlen(*out_b64)
 * @param timeout_ms   how long to wait for the frame
 */
esp_err_t aria_cam_capture(char **out_b64, int *out_len, uint32_t timeout_ms);

/**
 * @brief Temporary self-test: ~15s after boot, capture one frame and log its size.
 *        Used to prove the capture foundation; remove once the voice trigger is wired.
 */
void app_aria_cam_selftest_start(void);

#ifdef __cplusplus
}
#endif
