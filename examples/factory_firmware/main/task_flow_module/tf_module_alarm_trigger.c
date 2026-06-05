#include "tf_module_alarm_trigger.h"
#include "tf_module_util.h"
#include <string.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"
#include "esp_check.h"
#include <mbedtls/base64.h>

static const char *TAG = "tfm.alarm_trigger";

static void __data_lock( tf_module_alarm_trigger_t *p_module)
{
    xSemaphoreTake(p_module->sem_handle, portMAX_DELAY);
}
static void __data_unlock( tf_module_alarm_trigger_t *p_module)
{
    xSemaphoreGive(p_module->sem_handle);  
}
static void __parmas_default(struct tf_module_alarm_trigger_params *p_params)
{
    p_params->audio.p_buf = NULL;
    p_params->audio.len = 0;
    p_params->text.p_buf = NULL;
    p_params->text.len = 0;
}
static int __params_parse(struct tf_module_alarm_trigger_params *p_params, cJSON *p_json)
{
    cJSON *json_audio = cJSON_GetObjectItem(p_json, "audio");
    if (json_audio != NULL  && cJSON_IsString(json_audio) && strlen(json_audio->valuestring) > 0 ) {
        size_t output_len = 0;
        uint8_t *p_audio = NULL;
        int decode_ret = mbedtls_base64_decode(NULL, 0, &output_len, \
                            (uint8_t *)json_audio->valuestring, strlen(json_audio->valuestring));
        if( decode_ret != MBEDTLS_ERR_BASE64_INVALID_CHARACTER  && output_len > 0 ) {
            uint8_t *p_audio = (uint8_t *)tf_malloc( output_len);
            if( p_audio != NULL ) {
                decode_ret = mbedtls_base64_decode(p_audio, output_len, &output_len, \
                    (uint8_t *)json_audio->valuestring, strlen(json_audio->valuestring));
                if( decode_ret == 0){
                    p_params->audio.p_buf = p_audio;
                    p_params->audio.len   = output_len;
                } else {
                    tf_free(p_audio);
                    ESP_LOGE(TAG, "base64 decode failed");
                }
            }
        } else {
            ESP_LOGE(TAG, "Base64 decode failed, ret: %d, len:%d", decode_ret, output_len);
        } 
    }

    cJSON *json_text = cJSON_GetObjectItem(p_json, "text");
    if (json_text != NULL  && cJSON_IsString(json_text)) {
        p_params->text.p_buf = (uint8_t *)tf_malloc(strlen(json_text->valuestring) + 1);
        if( p_params->text.p_buf ) {
            memcpy(p_params->text.p_buf, json_text->valuestring, strlen(json_text->valuestring));
            p_params->text.len = strlen(json_text->valuestring) + 1;
            p_params->text.p_buf[p_params->text.len - 1] = '\0';
        }

    }
    return 0;
}

static void __event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    esp_err_t ret = ESP_OK;
    tf_module_alarm_trigger_t *p_module_ins = (tf_module_alarm_trigger_t *)handler_args;
    ESP_LOGI(TAG, "Input trigger");

    uint32_t type = ((uint32_t *)p_event_data)[0];
    if( type !=  TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE) {
        ESP_LOGW(TAG, "Unsupport type %d", type);
        tf_data_free(p_event_data);
        return;
    }
    struct tf_module_alarm_trigger_params *p_params = &p_module_ins->params;
    tf_data_dualimage_with_inference_t *p_data = (tf_data_dualimage_with_inference_t *)p_event_data;
    tf_data_dualimage_with_audio_text_t output_data;
    output_data.type = TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE_AUDIO_TEXT;
    __data_lock(p_module_ins);
    for (int i = 0; i < p_module_ins->output_evt_num; i++) {
        tf_data_image_copy(&output_data.img_small, &p_data->img_small);
        tf_data_image_copy(&output_data.img_large, &p_data->img_large);
        tf_data_inference_copy(&output_data.inference, &p_data->inference);
        tf_data_buf_copy(&output_data.audio, &p_params->audio);
        tf_data_buf_copy(&output_data.text, &p_params->text);
        ret = tf_event_post(p_module_ins->p_output_evt_id[i], &output_data, sizeof(output_data), pdMS_TO_TICKS(100));
        if( ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to post event %d", p_module_ins->p_output_evt_id[i]);
            tf_data_free(&output_data);
        } else {
            ESP_LOGI(TAG, "Output --> %d", p_module_ins->p_output_evt_id[i]);
        }
    }
    __data_unlock(p_module_ins);

    tf_data_free(p_event_data);
}
/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    tf_module_alarm_trigger_t *p_module_ins = (tf_module_alarm_trigger_t *)p_module;
    return 0;
}
static int __stop(void *p_module)
{
    tf_module_alarm_trigger_t *p_module_ins = (tf_module_alarm_trigger_t *)p_module;
    __data_lock(p_module_ins);
    tf_data_buf_free(&p_module_ins->params.audio);
    tf_data_buf_free(&p_module_ins->params.text);

    if( p_module_ins->p_output_evt_id ) {
        tf_free(p_module_ins->p_output_evt_id); 
    }
    p_module_ins->p_output_evt_id = NULL;
    p_module_ins->output_evt_num = 0;

    __data_unlock(p_module_ins);
    esp_err_t ret = tf_event_handler_unregister(p_module_ins->input_evt_id, __event_handler);
    return ret;
}
static int __cfg(void *p_module, cJSON *p_json)
{
    tf_module_alarm_trigger_t *p_module_ins = (tf_module_alarm_trigger_t *)p_module;
    __data_lock(p_module_ins);
    __parmas_default(&p_module_ins->params);
    __params_parse(&p_module_ins->params, p_json);
    __data_unlock(p_module_ins);
    return 0;
}
static int __msgs_sub_set(void *p_module, int evt_id)
{
    tf_module_alarm_trigger_t *p_module_ins = (tf_module_alarm_trigger_t *)p_module;
    esp_err_t ret;
    p_module_ins->input_evt_id = evt_id;
    ret = tf_event_handler_register(evt_id, __event_handler, p_module_ins);
    return ret;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    tf_module_alarm_trigger_t *p_module_ins = (tf_module_alarm_trigger_t *)p_module;
    __data_lock(p_module_ins);
    if (output_index == 0 && num > 0)
    {
        p_module_ins->p_output_evt_id = (int *)tf_malloc(sizeof(int) * num);
        if (p_module_ins->p_output_evt_id )
        {
            memcpy(p_module_ins->p_output_evt_id, p_evt_id, sizeof(int) * num);
            p_module_ins->output_evt_num = num;
        } else {
            ESP_LOGE(TAG, "Failed to malloc p_output_evt_id");
            p_module_ins->output_evt_num = 0;
        }
    }
    else
    {
        ESP_LOGW(TAG, "Only support output port 0, ignore %d", output_index);
    }
    __data_unlock(p_module_ins);
    return 0;
}

static tf_module_t * __module_instance(void)
{
    tf_module_alarm_trigger_t *p_module_ins = (tf_module_alarm_trigger_t *) tf_malloc(sizeof(tf_module_alarm_trigger_t));
    if (p_module_ins == NULL)
    {
        return NULL;
    }
    memset(p_module_ins, 0, sizeof(tf_module_alarm_trigger_t));
    return tf_module_alarm_trigger_init(p_module_ins);
}

static  void __module_destroy(tf_module_t *handle)
{
    if( handle ) {
        tf_module_alarm_trigger_t *p_module_ins = (tf_module_alarm_trigger_t *)handle->p_module;
        if (p_module_ins->sem_handle) {
            vSemaphoreDelete(p_module_ins->sem_handle);
            p_module_ins->sem_handle = NULL;
        }
        free(handle->p_module);
    }
}

const static struct tf_module_ops  __g_module_ops = {
    .start = __start,
    .stop = __stop,
    .cfg = __cfg,
    .msgs_sub_set = __msgs_sub_set,
    .msgs_pub_set = __msgs_pub_set
};

const static struct tf_module_mgmt __g_module_mgmt = {
    .tf_module_instance = __module_instance,
    .tf_module_destroy = __module_destroy,
};

/*************************************************************************
 * API
 ************************************************************************/
tf_module_t * tf_module_alarm_trigger_init(tf_module_alarm_trigger_t *p_module_ins)
{
    esp_err_t ret = ESP_OK;
    if ( NULL == p_module_ins)
    {
        return NULL;
    }
#if CONFIG_ENABLE_TF_MODULE_ALARM_TRIGGER_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    p_module_ins->module_base.p_module = p_module_ins;
    p_module_ins->module_base.ops = &__g_module_ops;
    
    __parmas_default(&p_module_ins->params);

    p_module_ins->p_output_evt_id = NULL;
    p_module_ins->output_evt_num = 0;
    p_module_ins->input_evt_id = 0;

    p_module_ins->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    return &p_module_ins->module_base;
err:
    if (p_module_ins->sem_handle) {
        vSemaphoreDelete(p_module_ins->sem_handle);
        p_module_ins->sem_handle = NULL;
    }
    return NULL;
}

esp_err_t tf_module_alarm_trigger_register(void)
{
    return tf_module_register(TF_MODULE_ALARM_TRIGGER_NAME,
                              TF_MODULE_ALARM_TRIGGER_DESC,
                              TF_MODULE_ALARM_TRIGGER_RVERSION,
                              &__g_module_mgmt);
}