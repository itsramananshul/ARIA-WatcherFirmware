#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "data_defs.h"

#define VOICE_INTERACTION_TASK_STACK_SIZE  10*1024
#define VOICE_INTERACTION_TASK_PRIO        14
#define VOICE_INTERACTION_TASK_CORE        1  //must be 1


#if CONFIG_ENABLE_TEST_ENV
#define CONFIG_TALK_SERV_HOST           "https://sensecraft-aiservice-test-api.seeed.cc" 
#define CONFIG_TALK_AUDIO_STREAM_PATH   "/v2/watcher/talk/audio_stream"
#define CONFIG_TASKFLOW_DETAIL_PATH     "/v2/watcher/talk/view_task_detail" 

#else
#define CONFIG_TALK_SERV_HOST           "https://sensecraft-aiservice-api.seeed.cc" 
#define CONFIG_TALK_AUDIO_STREAM_PATH   "/v2/watcher/talk/audio_stream"
#define CONFIG_TASKFLOW_DETAIL_PATH     "/v2/watcher/talk/view_task_detail"
#endif

#define ESP_ERR_VI_NO_MEM          (ESP_ERR_NO_MEM) //0x101
#define ESP_ERR_VI_HTTP_CONNECT    (ESP_ERR_HTTP_CONNECT) //0x7002
#define ESP_ERR_VI_HTTP_WRITE      (ESP_ERR_HTTP_WRITE_DATA) //0x7003
#define ESP_ERR_VI_HTTP_RESP       (ESP_ERR_HTTP_FETCH_HEADER) ////0x7004
#define ESP_ERR_VI_NET_CONNECT     (ESP_ERR_WIFI_NOT_CONNECT) //0x300F

#define VI_MODE_CHAT      0 
#define VI_MODE_TASK      1
#define VI_MODE_TASK_AUTO 2


#define VI_WAKE_FILE_PATH      "/spiffs/pushToTalk.mp3"

enum app_voice_interaction_status {
    VI_STATUS_IDLE = 0,
    VI_STATUS_WAKE_START,
    VI_STATUS_RECORDING,
    VI_STATUS_ANALYZING,
    VI_STATUS_PLAYING,
    VI_STATUS_STOP,
    VI_STATUS_FINISH,
    VI_STATUS_PRE_EXIT,
    VI_STATUS_EXIT,
    VI_STATUS_ERROR,
    VI_STATUS_TASKFLOW_GET,
};


struct app_voice_interaction {
    SemaphoreHandle_t sem_handle;
    EventGroupHandle_t event_group;
    TaskHandle_t task_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
    esp_http_client_handle_t client;
    esp_timer_handle_t timer_handle;
    enum app_voice_interaction_status cur_status;
    enum app_voice_interaction_status next_status;
    bool net_flag;
    char session_id[37];
    char stream_url[128];
    char taskflow_url[128];
    char token[128];
    bool need_delete_client;
    int  content_length;
    int  err_code;
    bool is_wait_resp;
    bool is_http_write;
    bool is_recording;
    bool is_connecting;
    bool need_get_taskflow;
    bool taskflow_pause;
    bool new_session;
    bool is_ota;
    bool use_local_svc;
};

esp_err_t app_voice_interaction_init(void);

bool app_vi_session_is_running(void);

int app_vi_result_parse(const char *p_str, size_t len,
                        struct view_data_vi_result *p_ret);

int app_vi_result_free(struct view_data_vi_result *p_ret);




