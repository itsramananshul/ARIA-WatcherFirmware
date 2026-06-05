#ifndef APP_RGB_H
#define APP_RGB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RGB_BREATH_RED,
    RGB_BREATH_GREEN,
    RGB_BREATH_BLUE,
    RGB_BREATH_WHITE,

    RGB_BLINK_RED,
    RGB_BLINK_GREEN,
    RGB_BLINK_BLUE,
    RGB_BLINK_WHITE,

    RGB_FLARE_RED,
    RGB_FLARE_GREEN,
    RGB_FLARE_WHITE,
    RGB_FLARE_BLUE,
    RGB_OFF,
    RGB_ON
}rgb_service_t;



int app_rgb_init(void);
void app_rgb_set(int caller, rgb_service_t service);

void app_rgb_status_set(int r, int g, int b, int mode, int step, int delay_time);

#ifdef __cplusplus
}
#endif

#endif
