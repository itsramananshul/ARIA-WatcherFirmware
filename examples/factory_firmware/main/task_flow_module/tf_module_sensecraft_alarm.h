
#pragma once
#include "tf_module.h"
#include "tf_module_data_type.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TF_MODULE_SENSECRAFT_ALARM_NAME     "sensecraft alarm"
#define TF_MODULE_SENSECRAFT_ALARM_RVERSION "1.0.0"
#define TF_MODULE_SENSECRAFT_ALARM_DESC     "sensecraft alarm module"

#define TF_MODULE_SENSECRAFT_ALARM_DEFAULT_SILENCE_DURATION  30

struct tf_module_sensecraft_alarm_params
{
    int  silence_duration; //seconds
    struct tf_data_buf text; 
};

typedef struct tf_module_sensecraft_alarm
{
    tf_module_t module_base;
    int input_evt_id; // no output
    struct tf_module_sensecraft_alarm_params params;
    time_t last_alarm_time;
    SemaphoreHandle_t sem_handle;
} tf_module_sensecraft_alarm_t;

tf_module_t * tf_module_sensecraft_alarm_init(tf_module_sensecraft_alarm_t *p_module_ins);

esp_err_t tf_module_sensecraft_alarm_register(void);

#ifdef __cplusplus
}
#endif
