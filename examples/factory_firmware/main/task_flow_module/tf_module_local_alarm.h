
#pragma once
#include "tf_module.h"
#include "tf_module_data_type.h"
#include "esp_err.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TF_MODULE_LOCAL_ALARM_NAME     "local alarm"
#define TF_MODULE_LOCAL_ALARM_RVERSION "1.0.0"
#define TF_MODULE_LOCAL_ALARM_DESC     "local alarm module"

#define TF_MODULE_LOCAL_ALARM_DEFAULT_AUDIO_FILE  "/spiffs/alarm-di.wav"

#define TF_MODULE_LOCAL_ALARM_TIMER_ENABLE  0

struct tf_module_local_alarm_params
{
    bool rgb;
    bool sound;
    bool img;
    bool text;
    int  duration; //seconds
};

struct tf_module_local_alarm_info
{
    int  duration; //seconds
    struct  tf_data_image img;
    bool is_show_img;
    struct  tf_data_buf text;
    bool is_show_text;
};

typedef struct tf_module_local_alarm
{
    tf_module_t module_base;
    int input_evt_id; // no output
    struct tf_module_local_alarm_params params;
#if TF_MODULE_LOCAL_ALARM_TIMER_ENABLE
    esp_timer_handle_t timer_handle;
#endif
    struct tf_data_buf audio;
    bool is_audio_playing;
    bool is_rgb_on;
} tf_module_local_alarm_t;

tf_module_t * tf_module_local_alarm_init(tf_module_local_alarm_t *p_module_ins);

esp_err_t tf_module_local_alarm_register(void);

#ifdef __cplusplus
}
#endif
