#include "tf_module_sensecraft_alarm.h"
#include "tf_module_util.h"
#include <string.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"
#include "esp_check.h"
#include "app_sensecraft.h"

static const char *TAG = "tfm.sensecraft_alarm";
static void __data_lock( tf_module_sensecraft_alarm_t *p_module)
{
    xSemaphoreTake(p_module->sem_handle, portMAX_DELAY);
}
static void __data_unlock( tf_module_sensecraft_alarm_t *p_module)
{
    xSemaphoreGive(p_module->sem_handle);
}
static void __parmas_default(struct tf_module_sensecraft_alarm_params *p_params)
{
    p_params->silence_duration = TF_MODULE_SENSECRAFT_ALARM_DEFAULT_SILENCE_DURATION;
    p_params->text.p_buf = NULL;
    p_params->text.len = 0;
}
static int __params_parse(struct tf_module_sensecraft_alarm_params *p_params, cJSON *p_json)
{
    cJSON *json_silence_duration = cJSON_GetObjectItem(p_json, "silence_duration");
    if (json_silence_duration != NULL  && cJSON_IsNumber(json_silence_duration)) {
        p_params->silence_duration = json_silence_duration->valueint;
    }
    cJSON *json_text = cJSON_GetObjectItem(p_json, "text");
    if (json_text != NULL  && cJSON_IsString(json_text)) {
        p_params->text.p_buf = (uint8_t *)tf_malloc(strlen(json_text->valuestring) + 1);
        if( p_params->text.p_buf ) {
            memcpy(p_params->text.p_buf, json_text->valuestring, strlen(json_text->valuestring));
            p_params->text.len = strlen(json_text->valuestring) + 1;
            p_params->text.p_buf[p_params->text.len - 1 ] = '\0';
        }
    }
    return 0;
}


static void __event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    esp_err_t ret = ESP_OK;
    tf_module_sensecraft_alarm_t *p_module_ins = (tf_module_sensecraft_alarm_t *)handler_args;
    struct tf_module_sensecraft_alarm_params *p_params = &p_module_ins->params;
    ESP_LOGI(TAG, "Input shutter");

    uint32_t type = ((uint32_t *)p_event_data)[0];
    if( type !=  TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE_AUDIO_TEXT) {
        ESP_LOGW(TAG, "unsupport type %d", type);
        tf_data_free(p_event_data);
        return;
    }

    time_t now = 0;
    double diff = 0.0;

    time(&now);
    diff = difftime(now, p_module_ins->last_alarm_time);
    if( diff >= p_params->silence_duration  || (p_module_ins->last_alarm_time == 0) ) {
        ESP_LOGI(TAG, "Notify sensecraft alarm...");

        p_module_ins->last_alarm_time = now;
        tf_data_dualimage_with_audio_text_t *p_data = (tf_data_dualimage_with_audio_text_t*)p_event_data;
        char *p_text_buf = NULL;
        int text_len = 0;

        tf_info_t tf_info;
        tf_engine_info_get(&tf_info);

        __data_lock(p_module_ins);
        if( p_params->text.p_buf  && p_params->text.len > 0 ) {
            p_text_buf = (char *)p_params->text.p_buf;
            text_len = strlen((char *)p_params->text.p_buf);
        } else  if ( p_data->text.p_buf && p_data->text.len > 0) {
            p_text_buf = (char *)p_data->text.p_buf;
            text_len = strlen((char *)p_data->text.p_buf);
        } else {
            p_text_buf = "unknown";
            text_len = strlen("unknown");
        }

        ret = app_sensecraft_mqtt_report_warn_event(tf_info.tid, 
                                              tf_info.p_tf_name,
                                              (char *)p_data->img_small.p_buf, p_data->img_small.len,
                                              (char *)p_text_buf, text_len);
        __data_unlock(p_module_ins);

        if( ret != ESP_OK ) {
            ESP_LOGE(TAG, "Faild to report sensecraft alarm");
        }
        free(tf_info.p_tf_name);
    } else {
        ESP_LOGI(TAG, "Silence: %d, diff: %f, Skip sensecraft alarm", p_params->silence_duration, diff);
    }
    tf_data_free(p_event_data);
}
/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    tf_module_sensecraft_alarm_t *p_module_ins = (tf_module_sensecraft_alarm_t *)p_module;
    return 0;
}
static int __stop(void *p_module)
{
    tf_module_sensecraft_alarm_t *p_module_ins = (tf_module_sensecraft_alarm_t *)p_module;
    __data_lock(p_module_ins);
    tf_data_buf_free(&p_module_ins->params.text);
    __data_unlock(p_module_ins);
    esp_err_t ret = tf_event_handler_unregister(p_module_ins->input_evt_id, __event_handler);
    return ret;
}
static int __cfg(void *p_module, cJSON *p_json)
{
    tf_module_sensecraft_alarm_t *p_module_ins = (tf_module_sensecraft_alarm_t *)p_module;
    __data_lock(p_module_ins);
    __parmas_default(&p_module_ins->params);
    __params_parse(&p_module_ins->params, p_json);
    __data_unlock(p_module_ins);
    return 0;
}
static int __msgs_sub_set(void *p_module, int evt_id)
{
    tf_module_sensecraft_alarm_t *p_module_ins = (tf_module_sensecraft_alarm_t *)p_module;
    esp_err_t ret;
    p_module_ins->input_evt_id = evt_id;
    ret = tf_event_handler_register(evt_id, __event_handler, p_module_ins);
    return ret;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    tf_module_sensecraft_alarm_t *p_module_ins = (tf_module_sensecraft_alarm_t *)p_module;
    if ( num > 0) {
        ESP_LOGW(TAG, "nonsupport output");
    }
    return 0;
}

static tf_module_t * __module_instance(void)
{
    tf_module_sensecraft_alarm_t *p_module_ins = (tf_module_sensecraft_alarm_t *) tf_malloc(sizeof(tf_module_sensecraft_alarm_t));
    if (p_module_ins == NULL)
    {
        return NULL;
    }
    memset(p_module_ins, 0, sizeof(tf_module_sensecraft_alarm_t));
    return tf_module_sensecraft_alarm_init(p_module_ins);
}

static  void __module_destroy(tf_module_t *handle)
{
    if( handle ) {
        tf_module_sensecraft_alarm_t *p_module_ins = (tf_module_sensecraft_alarm_t *)handle->p_module;
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
tf_module_t * tf_module_sensecraft_alarm_init(tf_module_sensecraft_alarm_t *p_module_ins)
{
    esp_err_t ret = ESP_OK;
    if ( NULL == p_module_ins)
    {
        return NULL;
    }
#if CONFIG_ENABLE_TF_MODULE_SENSECRAFT_ALARM_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    p_module_ins->module_base.p_module = p_module_ins;
    p_module_ins->module_base.ops = &__g_module_ops;
    
    __parmas_default(&p_module_ins->params);

    p_module_ins->input_evt_id = 0;
    p_module_ins->last_alarm_time = 0;
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

esp_err_t tf_module_sensecraft_alarm_register(void)
{
    return tf_module_register(TF_MODULE_SENSECRAFT_ALARM_NAME,
                              TF_MODULE_SENSECRAFT_ALARM_DESC,
                              TF_MODULE_SENSECRAFT_ALARM_RVERSION,
                              &__g_module_mgmt);
}