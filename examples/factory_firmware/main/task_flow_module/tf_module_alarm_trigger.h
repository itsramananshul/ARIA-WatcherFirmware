
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

#define TF_MODULE_ALARM_TRIGGER_NAME     "alarm trigger"
#define TF_MODULE_ALARM_TRIGGER_RVERSION "1.0.0"
#define TF_MODULE_ALARM_TRIGGER_DESC     "alarm trigger module"

struct tf_module_alarm_trigger_params
{
    struct tf_data_buf audio;
    struct tf_data_buf text; 
};

typedef struct tf_module_alarm_trigger
{
    tf_module_t module_base;
    int input_evt_id;
    int *p_output_evt_id;
    int output_evt_num;
    struct tf_module_alarm_trigger_params params;
    SemaphoreHandle_t sem_handle;
} tf_module_alarm_trigger_t;

tf_module_t * tf_module_alarm_trigger_init(tf_module_alarm_trigger_t *p_module_ins);

esp_err_t tf_module_alarm_trigger_register(void);

#ifdef __cplusplus
}
#endif
