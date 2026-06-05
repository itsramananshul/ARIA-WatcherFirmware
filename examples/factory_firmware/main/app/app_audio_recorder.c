#include "app_audio_recorder.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "sensecap-watcher.h"
#include "util.h"
#include "data_defs.h"
#include "event_loops.h"

#ifdef CONFIG_ENABLE_VI_SR
#include "esp_mn_speech_commands.h"
#include "esp_process_sdkconfig.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_mn_iface.h"
#endif

static const char *TAG = "audio_recorder";

struct app_audio_recorder *gp_audio_recorder = NULL;

#define EVENT_RECORD_STREAM_START      BIT0
#define EVENT_RECORD_STREAM_STOP       BIT1
#define EVENT_RECORD_STREAM_STOP_DONE  BIT2

static void __data_lock(struct app_audio_recorder  *p_audio_recorder)
{
    xSemaphoreTake(p_audio_recorder->sem_handle, portMAX_DELAY);
}
static void __data_unlock(struct app_audio_recorder *p_audio_recorder)
{
    xSemaphoreGive(p_audio_recorder->sem_handle);  
}



#ifdef CONFIG_ENABLE_VI_SR
static void app_audio_fead_and_record_task(void *p_arg)
{
    ESP_LOGI(TAG, "Feed Task");
    struct app_audio_recorder *p_audio_recorder = (struct app_audio_recorder *)p_arg;

    size_t bytes_read = 0;
    esp_afe_sr_iface_t *afe_handle= p_audio_recorder->afe_handle; 
    esp_afe_sr_data_t *afe_data = p_audio_recorder->afe_data;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);

    int feed_channel = bsp_get_feed_channel();
    ESP_LOGI(TAG, "audio_chunksize=%d, feed_channel=%d", audio_chunksize, feed_channel);

    /* Allocate audio buffer and check for result */
    int16_t *audio_buffer = heap_caps_malloc(audio_chunksize * sizeof(int16_t) * feed_channel, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); //TODO 
    assert(audio_buffer);

    EventBits_t bits = 0;
    bool record_start = false;
    while(1) {

        bits = xEventGroupWaitBits(p_audio_recorder->event_group, 
                EVENT_RECORD_STREAM_START | EVENT_RECORD_STREAM_STOP, pdTRUE, pdFALSE, 0);

        if(bits & EVENT_RECORD_STREAM_START) {
            ESP_LOGI(TAG, "EVENT_RECORD_STREAM_START");
            record_start = true;
        }
        if(bits & EVENT_RECORD_STREAM_STOP) {
            ESP_LOGI(TAG, "EVENT_RECORD_STREAM_STOP");
            record_start = false;
            xEventGroupSetBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_STOP_DONE);
        }

        bsp_get_feed_data(false, audio_buffer, audio_chunksize * sizeof(int16_t) * feed_channel);
        afe_handle->feed(afe_data, audio_buffer);

        if( record_start ) {
            if(xRingbufferSend(p_audio_recorder->rb_handle, audio_buffer, audio_chunksize * sizeof(int16_t) * feed_channel, 0) != pdTRUE) {
                ESP_LOGE(TAG, "xRingbufferSend failed");
            }
        }
    }
}

static void app_audio_detect_task(void *p_arg)
{
    ESP_LOGI(TAG, "Detection task");

    struct app_audio_recorder *p_audio_recorder = (struct app_audio_recorder *)p_arg;
    esp_afe_sr_iface_t *afe_handle= p_audio_recorder->afe_handle; 
    esp_afe_sr_data_t *afe_data = p_audio_recorder->afe_data;

    afe_vad_state_t local_state;
    uint8_t frame_keep = 0;
    bool detect_flag = false;

    memset(&local_state, 0, sizeof(local_state));

    while (1) {

        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            ESP_LOGW(TAG, "AFE Fetch Fail");
            continue;
        }
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, LOG_BOLD(LOG_COLOR_GREEN) "wakeword detected");
            esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, \
                        CTRL_EVENT_VI_RECORD_WAKEUP, NULL, NULL, pdMS_TO_TICKS(10000));
        } else if (res->wakeup_state == WAKENET_CHANNEL_VERIFIED || p_audio_recorder->manul_detect_flag) {
            detect_flag = true;
            if (p_audio_recorder->manul_detect_flag) {
                p_audio_recorder->manul_detect_flag = false;
                esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, \
                            CTRL_EVENT_VI_RECORD_WAKEUP, NULL, NULL, pdMS_TO_TICKS(10000));
            }
            frame_keep = 0;
            p_audio_recorder->afe_handle->disable_wakenet(afe_data);
            ESP_LOGI(TAG, LOG_BOLD(LOG_COLOR_GREEN) "AFE_FETCH_CHANNEL_VERIFIED, channel index: %d\n", res->trigger_channel_id);
        } 

        if (true == detect_flag) {
            if (local_state != res->vad_state) {
                local_state = res->vad_state;
                frame_keep = 0;
            } else {
                frame_keep++;
            }
            if ((100 == frame_keep) && (AFE_VAD_SILENCE == res->vad_state)) {
                ESP_LOGI(TAG, LOG_BOLD(LOG_COLOR_GREEN) "AFE_FETCH_SILENCE\n");
                esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, \
                            CTRL_EVENT_VI_RECORD_STOP, NULL, NULL, pdMS_TO_TICKS(10000));
                p_audio_recorder->afe_handle->enable_wakenet(afe_data);
                detect_flag = false;
                continue;
            }
        }
    }
}


#else 
static void app_audio_recorder_task(void *p_arg)
{
    ESP_LOGI(TAG, "Recorder task");
    struct app_audio_recorder *p_audio_recorder = (struct app_audio_recorder *)p_arg;
    EventBits_t bits = 0;
    while(1) {

        bits = xEventGroupWaitBits(p_audio_recorder->event_group, 
                EVENT_RECORD_STREAM_START, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
        
        if(bits & EVENT_RECORD_STREAM_START) {
            ESP_LOGI(TAG, "EVENT_RECORD_STREAM_START");
            
            int16_t *audio_buffer = psram_malloc( AUDIO_RECORDER_RINGBUF_CHUNK_SIZE );

            while(1) {
                bits = xEventGroupWaitBits(p_audio_recorder->event_group, 
                        EVENT_RECORD_STREAM_STOP, pdTRUE, pdTRUE, 0);
                if(bits & EVENT_RECORD_STREAM_STOP) {
                    ESP_LOGI(TAG, "EVENT_RECORD_STREAM_STOP");
                    break;
                }

                bsp_get_feed_data(false, audio_buffer, AUDIO_RECORDER_RINGBUF_CHUNK_SIZE);
                if(xRingbufferSend(p_audio_recorder->rb_handle, audio_buffer, AUDIO_RECORDER_RINGBUF_CHUNK_SIZE, 0) != pdTRUE) {
                    ESP_LOGE(TAG, "xRingbufferSend failed");
                }
            }

            free(audio_buffer);
            xEventGroupSetBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_STOP_DONE);
        }
    }
}
#endif
/*************************************************************************
 * API
 ************************************************************************/

esp_err_t app_audio_recorder_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    esp_err_t ret = ESP_OK;
    struct app_audio_recorder * p_audio_recorder = NULL;
    gp_audio_recorder = (struct app_audio_recorder *) psram_malloc(sizeof(struct app_audio_recorder));
    if (gp_audio_recorder == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    p_audio_recorder = gp_audio_recorder;
    memset(p_audio_recorder, 0, sizeof( struct app_audio_recorder ));
    
    p_audio_recorder->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_audio_recorder->p_rb_storage = (uint8_t *)psram_malloc(AUDIO_RECORDER_RINGBUF_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->p_rb_storage, ESP_ERR_NO_MEM, err, TAG, "Failed to create rb storage");

    p_audio_recorder->rb_handle = xRingbufferCreateStatic(AUDIO_RECORDER_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, p_audio_recorder->p_rb_storage, &p_audio_recorder->rb_ins);
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->rb_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create rb");

    p_audio_recorder->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");


#ifdef CONFIG_ENABLE_VI_SR
    esp_afe_sr_iface_t *afe_handle = NULL;

    srmodel_list_t *models = esp_srmodel_init("model");
    
    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
    afe_config_t afe_config = AFE_CONFIG_DEFAULT();

    afe_config.wakenet_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);    
    afe_config.aec_init = false;
    afe_config.pcm_config.total_ch_num = 1;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 0;
    afe_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config.wakenet_init = true;
    afe_config.voice_communication_init = false;

    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(&afe_config);
    p_audio_recorder->afe_handle = afe_handle;
    p_audio_recorder->afe_data = afe_data;
    p_audio_recorder->models = models;

    char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, "");
    ESP_LOGI(TAG, "load wakenet:%s", wn_name);
    afe_handle->set_wakenet(afe_data, wn_name);

    //feed and record task
    p_audio_recorder->feed_task_stack_buf = (StackType_t *)heap_caps_malloc( FEED_TASK_STACK_SIZE,  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(p_audio_recorder->feed_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_audio_recorder->feed_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(p_audio_recorder->feed_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_audio_recorder->feed_task = xTaskCreateStaticPinnedToCore(app_audio_fead_and_record_task,
                                                        "audio_feed_task",
                                                        FEED_TASK_STACK_SIZE,
                                                        (void *)p_audio_recorder,
                                                        FEED_TASK_PRIO,
                                                        p_audio_recorder->feed_task_stack_buf,
                                                        p_audio_recorder->feed_task_buf, 0);
    ESP_GOTO_ON_FALSE(p_audio_recorder->feed_task, ESP_FAIL, err, TAG, "Failed to create task");

    // detect task
    p_audio_recorder->detect_task_stack_buf = (StackType_t *)heap_caps_malloc(DETECT_TASK_STACK_SIZE,  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(p_audio_recorder->detect_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_audio_recorder->detect_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(p_audio_recorder->detect_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_audio_recorder->detect_task = xTaskCreateStaticPinnedToCore(app_audio_detect_task,
                                                            "audio_detect_task",
                                                            DETECT_TASK_STACK_SIZE,
                                                            (void *)p_audio_recorder,
                                                            DETECT_TASK_PRIO,
                                                            p_audio_recorder->detect_task_stack_buf,
                                                            p_audio_recorder->detect_task_buf, 0);
    ESP_GOTO_ON_FALSE(p_audio_recorder->detect_task, ESP_FAIL, err, TAG, "Failed to create task");

#else
    p_audio_recorder->p_task_stack_buf = (StackType_t *)psram_malloc(AUDIO_RECORDER_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_audio_recorder->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_audio_recorder->task_handle = xTaskCreateStaticPinnedToCore(app_audio_recorder_task,
                                                                "app_audio_recorder",
                                                                AUDIO_RECORDER_TASK_STACK_SIZE,
                                                                (void *)p_audio_recorder,
                                                                AUDIO_RECORDER_TASK_PRIO,
                                                                p_audio_recorder->p_task_stack_buf,
                                                                p_audio_recorder->p_task_buf,
                                                                AUDIO_RECORDER_TASK_CORE);
    ESP_GOTO_ON_FALSE(p_audio_recorder->task_handle, ESP_FAIL, err, TAG, "Failed to create task");
#endif

    return ESP_OK;  
err:

#ifdef CONFIG_ENABLE_VI_SR
    if (p_audio_recorder->afe_data) {
        p_audio_recorder->afe_handle->destroy(p_audio_recorder->afe_data);
    }
    if( p_audio_recorder->feed_task) {
        vTaskDelete(p_audio_recorder->feed_task);
        p_audio_recorder->feed_task = NULL;
    }
    if( p_audio_recorder->detect_task) {
        vTaskDelete(p_audio_recorder->detect_task);
        p_audio_recorder->detect_task = NULL;
    }
    if( p_audio_recorder->feed_task_stack_buf) {
        heap_caps_free(p_audio_recorder->feed_task_stack_buf);
    }
    if( p_audio_recorder->feed_task_buf) {
        heap_caps_free(p_audio_recorder->feed_task_buf);
    }
    
    if( p_audio_recorder->detect_task_stack_buf) {
        heap_caps_free(p_audio_recorder->detect_task_stack_buf);
    }
    if( p_audio_recorder->detect_task_buf) {
        heap_caps_free(p_audio_recorder->detect_task_buf);
    }

#else
    if(p_audio_recorder->task_handle ) {
        vTaskDelete(p_audio_recorder->task_handle);
        p_audio_recorder->task_handle = NULL;
    }
    if( p_audio_recorder->p_task_stack_buf ) {
        free(p_audio_recorder->p_task_stack_buf);
        p_audio_recorder->p_task_stack_buf = NULL;
    }
    if( p_audio_recorder->p_task_buf ) {   
        free(p_audio_recorder->p_task_buf);
        p_audio_recorder->p_task_buf = NULL;
    }
#endif

    if (p_audio_recorder->event_group) {
        vEventGroupDelete(p_audio_recorder->event_group);
        p_audio_recorder->event_group = NULL;
    }
    if( p_audio_recorder->rb_handle ) {
        vRingbufferDelete(p_audio_recorder->rb_handle);
        p_audio_recorder->rb_handle = NULL;
    }
    if( p_audio_recorder->p_rb_storage ) {
        free(p_audio_recorder->p_rb_storage);
        p_audio_recorder->p_rb_storage = NULL;
    }   
    if (p_audio_recorder->sem_handle) {
        vSemaphoreDelete(p_audio_recorder->sem_handle);
        p_audio_recorder->sem_handle = NULL;
    }
    if (p_audio_recorder) {
        free(p_audio_recorder);
        gp_audio_recorder = NULL;
    }
    ESP_LOGE(TAG, "app_audio_recorder_init fail %d!", ret);
    return ret;
}

int app_audio_recorder_status_get(void)
{
    struct app_audio_recorder * p_audio_recorder = gp_audio_recorder;
    if( p_audio_recorder == NULL) {
        return AUDIO_RECORDER_STATUS_IDLE;
    }
    return p_audio_recorder->status;
}

esp_err_t app_audio_recorder_stop(void)
{
    return ESP_OK;
}

esp_err_t app_audio_recorder_stream_start(void)
{
    struct app_audio_recorder * p_audio_recorder = gp_audio_recorder;
    if( p_audio_recorder == NULL) {
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    void *tmp = NULL;
    size_t len = 0;

    //clear the ringbuffer
    while ((tmp = xRingbufferReceiveUpTo(p_audio_recorder->rb_handle, &len, 0, AUDIO_RECORDER_RINGBUF_SIZE))) {
        vRingbufferReturnItem(p_audio_recorder->rb_handle, tmp);
    }

    ret = bsp_codec_set_fs( DRV_AUDIO_SAMPLE_RATE, 
                            DRV_AUDIO_SAMPLE_BITS, 
                            DRV_AUDIO_CHANNELS);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bsp_codec_mute_set(true);
    if (ret != ESP_OK) {
        return ret;
    }

    __data_lock(p_audio_recorder);
    p_audio_recorder->status = AUDIO_RECORDER_STATUS_STREAM;
    __data_unlock(p_audio_recorder);

    xEventGroupClearBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_STOP | EVENT_RECORD_STREAM_STOP_DONE);
    xEventGroupSetBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_START);
    return ret;
}

uint8_t *app_audio_recorder_stream_recv(size_t *p_recv_len,
                                        TickType_t xTicksToWait)
{
    struct app_audio_recorder * p_audio_recorder = gp_audio_recorder;
    if( p_audio_recorder == NULL) {
        return NULL;
    }
    return xRingbufferReceiveUpTo( p_audio_recorder->rb_handle, p_recv_len, xTicksToWait, AUDIO_RECORDER_RINGBUF_CHUNK_SIZE);
}

esp_err_t app_audio_recorder_stream_free(uint8_t *p_data)
{
    struct app_audio_recorder * p_audio_recorder = gp_audio_recorder;
    if( p_audio_recorder == NULL) {
        return ESP_FAIL;
    }
    vRingbufferReturnItem(p_audio_recorder->rb_handle, p_data);
    return ESP_OK;
}


esp_err_t app_audio_recorder_stream_stop(void)
{
    struct app_audio_recorder * p_audio_recorder = gp_audio_recorder;
    if( p_audio_recorder == NULL) {
        return ESP_FAIL;
    }

    __data_lock(p_audio_recorder);
    p_audio_recorder->status = AUDIO_RECORDER_STATUS_IDLE;
    __data_unlock(p_audio_recorder);

    xEventGroupSetBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_STOP);
    xEventGroupWaitBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_STOP_DONE, 1, 1, pdMS_TO_TICKS(1000));

    // don't clear ringbuffer

#ifndef CONFIG_ENABLE_VI_SR
    bsp_codec_dev_stop(); //TODO
#endif
    return ESP_OK;
}

esp_err_t app_audio_recorder_file_start(void *p_filepath)
{
    return ESP_OK;
}

esp_err_t app_audio_recorder_file_end(void)
{
    return ESP_OK;
}