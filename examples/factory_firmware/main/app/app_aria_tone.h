#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ARIA "thinking" tone. The library lives in the cloud (tones/ in the repo);
// the device caches only the SELECTED tone in /spiffs and plays it while ARIA
// is buffering a reply. Selection comes from the web (device_config.tone via
// device_sync) or the on-device picker.

void aria_tone_init(void);              // load selected name from NVS (call at boot)
void aria_tone_apply(const char *name); // set selected tone + (re)cache it
void aria_tone_ensure_cached(void);     // download selected if cache is stale (online only)
const char *aria_tone_selected(void);   // current selected tone filename

void aria_tone_play(void);              // play the cached tone (during thinking)
void aria_tone_stop(void);              // stop it (before the reply plays)

// ── On-device picker support ──────────────────────────────────────────────
// The live tone list is cached in the background (refreshed by the sync task)
// so the UI never blocks on the network.
void aria_tone_refresh_list(void);                 // GET /tones -> cache (call when online)
int  aria_tone_get_list(char (*out)[48], int max); // copy cached names; returns count
void aria_tone_select_async(const char *name);     // set selected + download + preview (background)

#ifdef __cplusplus
}
#endif
