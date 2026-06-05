
#pragma once
#include "tf_module.h"
#include "tf_module_data_type.h"
#include "esp_err.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TF_MODULE_TIMER_NAME "timer"
#define TF_MODULE_TIMER_VERSION "1.0.0"
#define TF_MODULE_TIMER_DESC "timer module"

typedef struct tf_module_timer
{
    tf_module_t module_base;
    int *p_output_evt_id;
    int output_evt_num;
    esp_timer_handle_t timer_handle;
    int period_s;
    int id;
} tf_module_timer_t;

tf_module_t * tf_module_timer_init(tf_module_timer_t *p_module_ins);

esp_err_t tf_module_timer_register(void);

#ifdef __cplusplus
}
#endif
