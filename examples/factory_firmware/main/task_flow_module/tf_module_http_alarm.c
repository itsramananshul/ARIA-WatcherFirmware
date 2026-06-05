#include <string.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "app_device_info.h"
#include "tf_module_http_alarm.h"
#include "tf_module_util.h"
#include "util.h"
#include "uuid.h"
#include "app_sensecraft.h"
#include "app_sensor.h"
#include "factory_info.h"
#include <mbedtls/base64.h>

static const char *TAG = "tfm.http_alarm";

#define EVENT_STOP          BIT0
#define EVENT_STOP_DONE     BIT1 
#define EVENT_NEED_DELETE   BIT2
#define EVENT_TASK_DELETED  BIT3 

extern struct app_sensecraft *gp_sensecraft;
extern char * __token_gen(void);

static void __data_lock( tf_module_http_alarm_t *p_module)
{
    xSemaphoreTake(p_module->sem_handle, portMAX_DELAY);
}
static void __data_unlock( tf_module_http_alarm_t *p_module)
{
    xSemaphoreGive(p_module->sem_handle);
}

static void __parmas_default(struct tf_module_http_alarm_params *p_params)
{
    p_params->time_en = true;
    p_params->text_en = true;
    p_params->image_en = true;
    p_params->sensor_en = true;
    p_params->silence_duration = TF_MODULE_HTTP_ALARM_DEFAULT_SILENCE_DURATION;
    p_params->text.p_buf = NULL;
    p_params->text.len = 0;
}

static int __params_parse(struct tf_module_http_alarm_params *p_params, cJSON *p_json)
{
    cJSON *json_time_en = cJSON_GetObjectItem(p_json, "time_en");
    if (json_time_en != NULL  && tf_cJSON_IsGeneralBool(json_time_en)) {
        p_params->time_en = tf_cJSON_IsGeneralTrue(json_time_en);
    }

    cJSON *json_text_en = cJSON_GetObjectItem(p_json, "text_en");
    if (json_text_en != NULL  && tf_cJSON_IsGeneralBool(json_text_en)) {
        p_params->text_en = tf_cJSON_IsGeneralTrue(json_text_en);
    }

    cJSON *json_image_en = cJSON_GetObjectItem(p_json, "image_en");
    if (json_image_en != NULL  && tf_cJSON_IsGeneralBool(json_image_en)) {
        p_params->image_en = tf_cJSON_IsGeneralTrue(json_image_en);
    }

    cJSON *json_sensor_en = cJSON_GetObjectItem(p_json, "sensor_en");
    if (json_sensor_en != NULL  && tf_cJSON_IsGeneralBool(json_sensor_en)) {
        p_params->sensor_en = tf_cJSON_IsGeneralTrue(json_sensor_en);
    }

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

static char *__request( const char *url,
                        esp_http_client_method_t method, 
                        const char *token, 
                        const char *content_type,
                        const char *head, 
                        uint8_t *data, size_t len)
{
    esp_err_t  ret = ESP_OK;
    char *result = NULL;

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // set header
    esp_http_client_set_header(client, "Content-Type", content_type);

    if( token !=NULL && strlen(token) > 0 ) {
        ESP_LOGI(TAG, "token: %s", token);
        esp_http_client_set_header(client, "Authorization", token);
    }

    const char *eui = factory_info_eui_get();
    ESP_LOGI(TAG, "eui: %s", eui);
    esp_http_client_set_header(client, "API-OBITER-DEVICE-EUI", eui);

    // TODO other headers set
    // if( head != NULL && strlen(head) > 0 ) {   
    // }

    ret = esp_http_client_open(client, len);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to open client!");

    if (len > 0)
    {
        int wlen = esp_http_client_write(client, (const char *)data, len);
        ESP_GOTO_ON_FALSE(wlen >= 0, ESP_FAIL, err, TAG, "Failed to write client!");
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (esp_http_client_is_chunked_response(client))
    {
        esp_http_client_get_chunk_length(client, &content_length);
    }
    ESP_LOGI(TAG, "content_length=%d", content_length);
    ESP_GOTO_ON_FALSE(content_length >= 0, ESP_FAIL, err, TAG, "HTTP client fetch headers failed!");

    result = (char *)tf_malloc(content_length + 1);
    ESP_GOTO_ON_FALSE(NULL != result, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc:%d", content_length+1);

    int read = esp_http_client_read_response(client, result, content_length);
    if (read != content_length)
    {
        ESP_LOGE(TAG, "HTTP_ERROR: read=%d, length=%d", read, content_length);
        free(result);
        result = NULL;
    }
    else
    {
        result[content_length] = 0;
        // ESP_LOGD(TAG, "content: %s, size: %d", result, strlen(result));
    }
err:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result != NULL ? result : NULL;
}

static int __http_report_warn_event(tf_module_http_alarm_t *p_module_ins,
                                    tf_data_dualimage_with_audio_text_t *p_data)
{
    int ret = 0;
    struct tf_module_http_alarm_params *p_params = &p_module_ins->params;

    char *p_str = NULL;
    cJSON *json = NULL;
    char *json_str = NULL;
    char *p_resp = NULL;

    struct app_sensecraft *p_sensecraft = gp_sensecraft;

    tf_info_t tf_info;
    tf_engine_info_get(&tf_info);

    char uuid[37];
    UUIDGen(uuid);

    __data_lock(p_module_ins);

    json = cJSON_CreateObject();

    cJSON_AddItemToObject(json, "requestId", cJSON_CreateString(uuid));
    cJSON_AddItemToObject(json, "deviceEui", cJSON_CreateString(p_sensecraft->deviceinfo.eui));
    
    cJSON *events = NULL;
    events = cJSON_CreateObject();
    cJSON_AddItemToObject(json, "events", events);

    if (p_params->time_en) {
        time_t timestamp_ms = util_get_timestamp_ms();
        cJSON_AddItemToObject(events, "timestamp", cJSON_CreateNumber(timestamp_ms));
    }

    if (p_params->text_en) {
        p_str = "";
        if ( p_params->text.p_buf && p_params->text.len > 0 ) {
            p_str = (char *)p_params->text.p_buf;
        }
        cJSON_AddItemToObject(events, "text", cJSON_CreateString(p_str));
    }

    if (p_params->image_en) {
        p_str = "";
        if (p_data->img_small.p_buf != NULL) {
            p_str = (char *)p_data->img_small.p_buf;
        }
        cJSON_AddItemToObject(events, "img", cJSON_CreateString(p_str));
    }

    cJSON *data = NULL;
    data = cJSON_CreateObject();
    cJSON_AddItemToObject(events, "data", data);

    //inference
    if (p_data->inference.is_valid) {

        cJSON *inference = cJSON_CreateObject();
        cJSON_AddItemToObject(data, "inference", inference);

        switch (p_data->inference.type)
        {
            case INFERENCE_TYPE_BOX:
            {
                cJSON *boxes = cJSON_CreateArray();
                cJSON_AddItemToObject(inference, "boxes", boxes);
                sscma_client_box_t *p_boxs = (sscma_client_box_t *)p_data->inference.p_data;
                for (size_t i = 0; i < p_data->inference.cnt; i++)
                {
                    sscma_client_box_t *p_box =  &p_boxs[i];   
                    cJSON *box = cJSON_CreateArray();
                    cJSON_AddItemToArray(box, cJSON_CreateNumber(p_box->x));
                    cJSON_AddItemToArray(box, cJSON_CreateNumber(p_box->y));
                    cJSON_AddItemToArray(box, cJSON_CreateNumber(p_box->w));
                    cJSON_AddItemToArray(box, cJSON_CreateNumber(p_box->h));
                    cJSON_AddItemToArray(box, cJSON_CreateNumber(p_box->score));
                    cJSON_AddItemToArray(box, cJSON_CreateNumber(p_box->target));
                    cJSON_AddItemToArray(boxes, box);
                }
                break;
            }
            case INFERENCE_TYPE_CLASS:
            {
                cJSON *classes = cJSON_CreateArray();
                cJSON_AddItemToObject(inference, "classes", classes);
                sscma_client_class_t *p_classes = (sscma_client_class_t *)p_data->inference.p_data;
                for (size_t i = 0; i < p_data->inference.cnt; i++)
                {
                    sscma_client_class_t *p_class =  &p_classes[i]; 
                    cJSON *class = cJSON_CreateArray();
                    cJSON_AddItemToArray(class, cJSON_CreateNumber(p_class->score));
                    cJSON_AddItemToArray(class, cJSON_CreateNumber(p_class->target));
                    cJSON_AddItemToArray(classes, class);
                }
                break;
            }
            default:
                ESP_LOGE(TAG, "unsupport inference type: %d", p_data->inference.type);
                break;
        }
        cJSON *classes = cJSON_CreateArray();
        cJSON_AddItemToObject(inference, "classes_name", classes);
        for (size_t i = 0; p_data->inference.classes[i] != NULL; i++)
        {
            cJSON_AddItemToArray(classes, cJSON_CreateString(p_data->inference.classes[i]));
        }
    }

    if (p_params->sensor_en) {
        double temp = 0;
        uint32_t humi = 0, co2 = 0;
        uint8_t sensor_num = 0;
        app_sensor_data_t app_sensor_data[APP_SENSOR_SUPPORT_MAX] = {0};
        sensor_num = app_sensor_read_measurement(app_sensor_data, sizeof(app_sensor_data_t) * APP_SENSOR_SUPPORT_MAX);
        if( sensor_num ) {
            cJSON *sensor = NULL;
            sensor = cJSON_CreateObject();
            cJSON_AddItemToObject(data, "sensor", sensor);
            for (uint8_t i = 0; i < sensor_num; i ++) {
                if (app_sensor_data[i].state) {
                    if (app_sensor_data[i].type == SENSOR_SHT4x) {
                        temp = (app_sensor_data[i].context.sht4x.temperature + 50) / 100;
                        temp /= 10;
                        humi = app_sensor_data[i].context.sht4x.humidity / 1000;
                        cJSON_AddItemToObject(sensor, "temperature", cJSON_CreateNumber(temp));
                        cJSON_AddItemToObject(sensor, "humidity", cJSON_CreateNumber(humi));
                    } else if(app_sensor_data[i].type == SENSOR_SCD4x) {
                        temp = (app_sensor_data[i].context.scd4x.temperature + 50) / 100;
                        temp /= 10;
                        humi = app_sensor_data[i].context.scd4x.humidity / 1000;
                        co2 = app_sensor_data[i].context.scd4x.co2 / 1000;

                        cJSON *json_tmp = NULL;
                        json_tmp = cJSON_GetObjectItem(sensor, "temperature");
                        if (json_tmp == NULL || json_tmp->type == cJSON_NULL) {
                            cJSON_AddItemToObject(sensor, "temperature", cJSON_CreateNumber(temp));
                        }
                        json_tmp = cJSON_GetObjectItem(sensor, "humidity");
                        if (json_tmp == NULL || json_tmp->type == cJSON_NULL) {
                            cJSON_AddItemToObject(sensor, "humidity", cJSON_CreateNumber(humi));
                        }
                        cJSON_AddItemToObject(sensor, "CO2", cJSON_CreateNumber(co2));
                    }
                }
            }
        }
    }

    __data_unlock(p_module_ins);

    free(tf_info.p_tf_name);

    json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Post %s", p_module_ins->url);

    p_resp = __request(p_module_ins->url, 
                    HTTP_METHOD_POST, 
                    p_module_ins->token,
                    "application/json",
                    p_module_ins->head, 
                    (uint8_t *)json_str, strlen(json_str));
    free(json_str);

    if (p_resp == NULL) {
        ESP_LOGE(TAG, "request failed");
        return -1;
    }

    ESP_LOGI(TAG, "Response: %s", p_resp);

    json = cJSON_Parse(p_resp);
    if (json == NULL) {
        ESP_LOGE(TAG, "Json parse failed");
        tf_free(p_resp);
        return -1;
    }

    ret = -1;
    cJSON *code = cJSON_GetObjectItem(json, "code");
    if (code != NULL && cJSON_IsNumber(code) && code->valueint == 200) {
        // TODO
        ret = 0; //success
    } else {
        if( code != NULL ) {
            ESP_LOGE(TAG, "code: %d", code->valueint);
        }
    }

    tf_free(p_resp);
    cJSON_Delete(json);
    return ret;
}

static void __event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    esp_err_t ret = ESP_OK;
    tf_module_http_alarm_t *p_module_ins = (tf_module_http_alarm_t *)handler_args;
    struct tf_module_http_alarm_params *p_params = &p_module_ins->params;
    ESP_LOGI(TAG, "Input shutter");

    uint32_t type = ((uint32_t *)p_event_data)[0];
    if ( type !=  TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE_AUDIO_TEXT) {
        ESP_LOGW(TAG, "unsupport type %d", type);
        tf_data_free(p_event_data);
        return;
    }

    time_t now = 0;
    double diff = 0.0;

    time(&now);
    diff = difftime(now, p_module_ins->last_alarm_time);
    if ( diff >= p_params->silence_duration  || (p_module_ins->last_alarm_time == 0) ) {
        ESP_LOGI(TAG, "Notify http alarm...");
        
        p_module_ins->last_alarm_time = now;

        if( xQueueSend(p_module_ins->queue_handle, p_event_data, ( TickType_t ) 0) != pdTRUE) {
            ESP_LOGW(TAG, "xQueueSend failed");
            tf_data_free(p_event_data);
        }
    } else {
        ESP_LOGI(TAG, "Silence: %d, diff: %f, Skip http alarm", p_params->silence_duration, diff);
        tf_data_free(p_event_data);
    }
}

static void http_alarm_task(void *p_arg)
{
    int ret = 0;
    tf_module_http_alarm_t *p_module_ins = (tf_module_http_alarm_t *)p_arg;
    struct tf_module_http_alarm_params *p_params = &p_module_ins->params;
    tf_data_dualimage_with_audio_text_t data;
    EventBits_t bits;

    while(1) {
        bits = xEventGroupWaitBits(p_module_ins->event_group, \
                EVENT_NEED_DELETE | EVENT_STOP , pdTRUE, pdFALSE, (TickType_t)10);

        if (( bits & EVENT_NEED_DELETE ) != 0) {
            ESP_LOGI(TAG, "EVENT_NEED_DELETE");
            while (xQueueReceive(p_module_ins->queue_handle, &data,0) == pdPASS ) {
                tf_data_free((void *)&data); //clear queue
            }
            xEventGroupSetBits(p_module_ins->event_group, EVENT_TASK_DELETED);
            vTaskDelete(NULL);
        }

        if (( bits & EVENT_STOP ) != 0) {
            ESP_LOGI(TAG, "EVENT_STOP");
            while (xQueueReceive(p_module_ins->queue_handle, &data,0) == pdPASS ) {
                tf_data_free((void *)&data); //clear queue
            }
            xEventGroupSetBits(p_module_ins->event_group, EVENT_STOP_DONE);
        }

        if (xQueueReceive(p_module_ins->queue_handle, &data, (TickType_t)10) == pdPASS) {
            ESP_LOGI(TAG, "Start send http alarm");

            ret = __http_report_warn_event(p_module_ins, &data);
            if (ret == 0) {
                ESP_LOGI(TAG, "Success to report http alarm");
            }
            else {
                ESP_LOGE(TAG, "Faild to report http alarm");
            }

            tf_data_free((void *)&data);
        }
    }
}

static void http_alarm_task_destroy(tf_module_http_alarm_t *p_module_ins)
{
    xEventGroupSetBits(p_module_ins->event_group, EVENT_NEED_DELETE);
    xEventGroupWaitBits(p_module_ins->event_group, EVENT_TASK_DELETED, 1, 1, portMAX_DELAY);
    vTaskDelay(1000 / portTICK_PERIOD_MS);  //wait task delete done
    if( p_module_ins->p_task_stack_buf ) {
        tf_free(p_module_ins->p_task_stack_buf);
        p_module_ins->p_task_stack_buf = NULL;
    }
    if( p_module_ins->p_task_buf ) {
        free(p_module_ins->p_task_buf);
        p_module_ins->p_task_buf = NULL;
    }
    if (p_module_ins->sem_handle) {
        vSemaphoreDelete(p_module_ins->sem_handle);
        p_module_ins->sem_handle = NULL;
    }
    if (p_module_ins->event_group) {
        vEventGroupDelete(p_module_ins->event_group);
        p_module_ins->event_group = NULL;
    }
    if ( p_module_ins->queue_handle ) {
        vQueueDelete(p_module_ins->queue_handle);
        p_module_ins->queue_handle = NULL;
    }
}

/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    tf_module_http_alarm_t *p_module_ins = (tf_module_http_alarm_t *)p_module;
    return 0;
}

static int __stop(void *p_module)
{
    tf_module_http_alarm_t *p_module_ins = (tf_module_http_alarm_t *)p_module;
    __data_lock(p_module_ins);
    tf_data_buf_free(&p_module_ins->params.text);
    __data_unlock(p_module_ins);
    esp_err_t ret = tf_event_handler_unregister(p_module_ins->input_evt_id, __event_handler);
    return ret;
}

static int __cfg(void *p_module, cJSON *p_json)
{
    tf_module_http_alarm_t *p_module_ins = (tf_module_http_alarm_t *)p_module;

    char *p_token = NULL, *p_host = NULL;
    local_service_cfg_type1_t local_svc_cfg = { .enable = false, .url = NULL };
    esp_err_t ret = get_local_service_cfg_type1(MAX_CALLER, CFG_ITEM_TYPE1_NOTIFICATION_PROXY, &local_svc_cfg);
    if (ret == ESP_OK && local_svc_cfg.enable) {
        if (local_svc_cfg.url != NULL && strlen(local_svc_cfg.url) > 7) {
            ESP_LOGI(TAG, "got local service cfg, url=%s", local_svc_cfg.url);
            int len = strlen(local_svc_cfg.url);
            if (local_svc_cfg.url[len - 1] == '/') local_svc_cfg.url[len - 1] = '\0';  //remove trail '/'
            p_host = local_svc_cfg.url;
        }
        if (local_svc_cfg.token != NULL && strlen(local_svc_cfg.token) > 0) {
            ESP_LOGI(TAG, "got local service cfg, token=%s", local_svc_cfg.token);
            p_token = local_svc_cfg.token;
        }
    } else {
        ESP_LOGI(TAG, "local service disable");
        // return -1;
    }

    // host
    if (p_host == NULL) p_host = CONFIG_TF_MODULE_HTTP_ALARM_SERV_HOST;
    snprintf(p_module_ins->url, sizeof(p_module_ins->url), "%s%s", p_host, CONFIG_TF_MODULE_HTTP_ALARM_SERV_REQ_PATH);

    // token
    if (p_token == NULL) p_token = __token_gen();
    if (p_token) {
        if (local_svc_cfg.enable) {
            snprintf(p_module_ins->token, sizeof(p_module_ins->token), "%s", p_token);
        } else {
            snprintf(p_module_ins->token, sizeof(p_module_ins->token), "Device %s", p_token);
        }
    } else {
        p_module_ins->token[0] = '\0';
    }

    if (local_svc_cfg.url != NULL) {
        free(local_svc_cfg.url);
    }
    if (local_svc_cfg.token != NULL) {
        free(local_svc_cfg.token);
    }

    p_module_ins->head[0] = '\0';

    __data_lock(p_module_ins);
    __parmas_default(&p_module_ins->params);
    __params_parse(&p_module_ins->params, p_json);
    __data_unlock(p_module_ins);

    return 0;
}

static int __msgs_sub_set(void *p_module, int evt_id)
{
    tf_module_http_alarm_t *p_module_ins = (tf_module_http_alarm_t *)p_module;
    esp_err_t ret;
    p_module_ins->input_evt_id = evt_id;
    ret = tf_event_handler_register(evt_id, __event_handler, p_module_ins);
    return ret;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    tf_module_http_alarm_t *p_module_ins = (tf_module_http_alarm_t *)p_module;
    if ( num > 0) {
        ESP_LOGW(TAG, "nonsupport output");
    }
    return 0;
}

static tf_module_t * __module_instance(void)
{
    tf_module_http_alarm_t *p_module_ins = (tf_module_http_alarm_t *) tf_malloc(sizeof(tf_module_http_alarm_t));
    if (p_module_ins == NULL)
    {
        return NULL;
    }
    memset(p_module_ins, 0, sizeof(tf_module_http_alarm_t));
    return tf_module_http_alarm_init(p_module_ins);
}

static void __module_destroy(tf_module_t *handle)
{
    if( handle ) {
        http_alarm_task_destroy((tf_module_http_alarm_t *)handle->p_module);
        free(handle->p_module);
    }
}

const static struct tf_module_ops __g_module_ops = {
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
tf_module_t * tf_module_http_alarm_init(tf_module_http_alarm_t *p_module_ins)
{
    esp_err_t ret = ESP_OK;
    if ( NULL == p_module_ins)
    {
        return NULL;
    }
#if CONFIG_ENABLE_TF_MODULE_HTTP_ALARM_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    p_module_ins->module_base.p_module = p_module_ins;
    p_module_ins->module_base.ops = &__g_module_ops;
    
    __parmas_default(&p_module_ins->params);

    p_module_ins->input_evt_id = 0;
    p_module_ins->last_alarm_time = 0;

    p_module_ins->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_module_ins->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");

    p_module_ins->queue_handle = xQueueCreate(TF_MODULE_HTTP_ALARM_QUEUE_SIZE, sizeof(tf_data_dualimage_with_audio_text_t));
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->queue_handle, ESP_FAIL, err, TAG, "Failed to create queue");

    p_module_ins->p_task_stack_buf = (StackType_t *)tf_malloc(TF_MODULE_HTTP_ALARM_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    // task TCB must be allocated from internal memory 
    p_module_ins->p_task_buf = heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_module_ins->task_handle = xTaskCreateStatic(http_alarm_task,
                                                "http_alarm_task",
                                                TF_MODULE_HTTP_ALARM_TASK_STACK_SIZE,
                                                (void *)p_module_ins,
                                                TF_MODULE_HTTP_ALARM_TASK_PRIO,
                                                p_module_ins->p_task_stack_buf,
                                                p_module_ins->p_task_buf);
    ESP_GOTO_ON_FALSE(p_module_ins->task_handle, ESP_FAIL, err, TAG, "Failed to create task");

    return &p_module_ins->module_base;
err:
    if(p_module_ins->task_handle ) {
        vTaskDelete(p_module_ins->task_handle);
        p_module_ins->task_handle = NULL;
    }
    if( p_module_ins->p_task_stack_buf ) {
        tf_free(p_module_ins->p_task_stack_buf);
        p_module_ins->p_task_stack_buf = NULL;
    }
    if( p_module_ins->p_task_buf ) {
        free(p_module_ins->p_task_buf);
        p_module_ins->p_task_buf = NULL;
    }
    if (p_module_ins->sem_handle) {
        vSemaphoreDelete(p_module_ins->sem_handle);
        p_module_ins->sem_handle = NULL;
    }
    if (p_module_ins->event_group) {
        vEventGroupDelete(p_module_ins->event_group);
        p_module_ins->event_group = NULL;
    }
    if( p_module_ins->queue_handle ) {
        vQueueDelete(p_module_ins->queue_handle);
        p_module_ins->queue_handle = NULL;
    }
    return NULL;
}

esp_err_t tf_module_http_alarm_register(void)
{
    return tf_module_register(TF_MODULE_HTTP_ALARM_NAME,
                              TF_MODULE_HTTP_ALARM_DESC,
                              TF_MODULE_HTTP_ALARM_RVERSION,
                              &__g_module_mgmt);
}