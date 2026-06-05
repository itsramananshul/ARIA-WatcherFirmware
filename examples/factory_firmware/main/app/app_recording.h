#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// On-device voice recorder ("Recording" home menu). Captures 16kHz mono PCM,
// wraps it as WAV, and mirrors it to the user's GitHub repo via the backend
// /recording endpoint. If WiFi is offline at stop time and an SD card is
// present, the WAV is kept on the card and uploaded automatically once WiFi
// reconnects. With no SD card and no WiFi, the take can't be kept.
typedef enum {
    REC_IDLE = 0,
    REC_RECORDING,
    REC_FINALIZING,
    REC_UPLOADING,
    REC_DONE_UPLOADED,        // committed to GitHub
    REC_DONE_SAVED_SD,        // on SD card, will upload when WiFi returns
    REC_ERR_NO_SD_OFFLINE,    // no SD + no WiFi -> take lost
    REC_ERR,                  // generic failure
} app_recording_state_t;

esp_err_t app_recording_init(void);

// True while a take is recording, finalizing, or uploading.
bool app_recording_is_active(void);

esp_err_t app_recording_start(void);
esp_err_t app_recording_stop(void);

app_recording_state_t app_recording_get_state(void);
uint32_t app_recording_elapsed_ms(void);
const char *app_recording_status_text(void);

// Ask the backend to commit the current chat conversation to GitHub now
// (called when leaving the Chat window).
void app_chat_end_flush(void);

#ifdef __cplusplus
}
#endif
