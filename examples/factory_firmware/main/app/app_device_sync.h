#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Device control heartbeat. Polls the backend /device_sync every few seconds:
// reports state, fetches the desired config (set from the web app) and applies
// it (voice, engine, volume, brightness, feature toggles), and runs any queued
// commands (reboot, record). This is what lets the web app control the watch.
esp_err_t app_device_sync_init(void);

#ifdef __cplusplus
}
#endif
