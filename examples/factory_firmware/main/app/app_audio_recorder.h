#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#ifdef CONFIG_ENABLE_VI_SR
#include "esp_afe_sr_models.h"
#include "esp_mn_models.h"
#include "model_path.h"

#define FEED_TASK_STACK_SIZE   8 * 1024
#define FEED_TASK_PRIO         13
#define DETECT_TASK_STACK_SIZE   10 * 1024
#define DETECT_TASK_PRIO         13

#endif

#define AUDIO_RECORDER_TASK_STACK_SIZE  5*1024
#define AUDIO_RECORDER_TASK_PRIO        13
#define AUDIO_RECORDER_TASK_CORE        1

// sample rate: 16000, bit depth: 16, channels: 1; 32000 size per second;
// 5*32000  can cache 5s of audio. 
#define AUDIO_RECORDER_RINGBUF_SIZE         5*32000  //TODO
#define AUDIO_RECORDER_RINGBUF_CHUNK_SIZE   16000

enum app_audio_recorder_status {
    AUDIO_RECORDER_STATUS_IDLE = 0,
    AUDIO_RECORDER_STATUS_FILE,
    AUDIO_RECORDER_STATUS_STREAM,
};

struct app_audio_recorder {
    SemaphoreHandle_t sem_handle;
    RingbufHandle_t rb_handle;
    StaticRingbuffer_t rb_ins;
    uint8_t * p_rb_storage;
    EventGroupHandle_t event_group;
    enum app_audio_recorder_status status;
#ifdef CONFIG_ENABLE_VI_SR
    srmodel_list_t *models;
    esp_afe_sr_iface_t *afe_handle;
    esp_afe_sr_data_t *afe_data;
    TaskHandle_t feed_task;
    StaticTask_t *feed_task_buf;
    StackType_t *feed_task_stack_buf;
    TaskHandle_t detect_task;
    StaticTask_t *detect_task_buf;
    StackType_t *detect_task_stack_buf;
    bool manul_detect_flag;
#else
    TaskHandle_t task_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
#endif
};

esp_err_t app_audio_recorder_init(void);

int app_audio_recorder_status_get(void);

esp_err_t app_audio_recorder_stop(void);

esp_err_t app_audio_recorder_stream_start(void);

uint8_t *app_audio_recorder_stream_recv(size_t *p_recv_len,
                                        TickType_t xTicksToWait);

esp_err_t app_audio_recorder_stream_free(uint8_t *p_data);

esp_err_t app_audio_recorder_stream_stop(void);

esp_err_t app_audio_recorder_file_start(void *p_filepath);
esp_err_t app_audio_recorder_file_end(void);