
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "data_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#ifdef __cplusplus
extern "C" {
#endif

#define TASKFLOW_TASK_STACK_SIZE  5*1024
#define TASKFLOW_TASK_PRIO        3

struct app_taskflow_info {
    bool is_valid;
    int  len;
    // char uuid[37];
};

struct app_taskflow
{
    SemaphoreHandle_t sem_handle;
    SemaphoreHandle_t report_sem_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
    TaskHandle_t task_handle;
    char *p_taskflow_json;
    struct view_data_taskflow_status status;
    bool status_need_report;
    bool mqtt_connect_flag;
    int report_cnt;
    bool need_pause_taskflow;
};


esp_err_t app_taskflow_init(void);

#ifdef __cplusplus
}
#endif