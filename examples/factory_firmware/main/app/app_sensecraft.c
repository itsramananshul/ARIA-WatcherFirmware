#include "app_sensecraft.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <mbedtls/base64.h>

#include "event_loops.h"
#include "data_defs.h"
#include "app_device_info.h"
#include "factory_info.h"
#include "util.h"
#include "uuid.h"
#include "tf.h"
#include "app_ota.h"
#include "app_voice_interaction.h"


static const char *TAG = "sensecaft";

struct app_sensecraft *gp_sensecraft = NULL;

const int MQTT_PUB_QOS0 = 0;
const int MQTT_PUB_QOS1 = 1;

static void __data_lock(struct app_sensecraft  *p_sensecraft)
{
    xSemaphoreTake(p_sensecraft->sem_handle, portMAX_DELAY);
}
static void __data_unlock(struct app_sensecraft *p_sensecraft)
{
    xSemaphoreGive(p_sensecraft->sem_handle);  
}

/*************************************************************************
 * HTTPS Request
 ************************************************************************/
static char *__request( const char *base_url, 
                        const char *api_key, 
                        const char *endpoint, 
                        const char *content_type, 
                        esp_http_client_method_t method, 
                        const char *boundary, 
                        uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_OK;
    char *url = NULL;
    char *result = NULL;
    asprintf(&url, "%s%s", base_url, endpoint);
    if( url == NULL ) {
        ESP_LOGE(TAG, "Failed to allocate url");
        return NULL;
    }
    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char *headers = NULL;
    if (boundary)
    {
        asprintf(&headers, "%s; boundary=%s", content_type, boundary);
    }
    else
    {
        asprintf(&headers, "%s", content_type);
    }
    ESP_GOTO_ON_FALSE(headers != NULL, ESP_FAIL, end, TAG, "Failed to allocate headers!");

    esp_http_client_set_header(client, "Content-Type", headers);
    free(headers);

    if (api_key != NULL)
    {
        asprintf(&headers, "Device %s", api_key);
        ESP_GOTO_ON_FALSE(headers != NULL, ESP_FAIL, end, TAG, "Failed to allocate headers!");
        esp_http_client_set_header(client, "Authorization", headers);
        free(headers);
    }

    esp_err_t err = esp_http_client_open(client, len);
    ESP_GOTO_ON_FALSE(err == ESP_OK, ESP_FAIL, end, TAG, "Failed to open client!");
    if (len > 0)
    {
        int wlen = esp_http_client_write(client, (const char *)data, len);
        ESP_GOTO_ON_FALSE(wlen >= 0, ESP_FAIL, end, TAG, "Failed to open client!");
    }
    int content_length = esp_http_client_fetch_headers(client);
    if (esp_http_client_is_chunked_response(client))
    {
        esp_http_client_get_chunk_length(client, &content_length);
    }
    ESP_LOGI(TAG, "content_length=%d", content_length);
    ESP_GOTO_ON_FALSE(content_length >= 0, ESP_FAIL, end, TAG, "HTTP client fetch headers failed!");

    result = (char *)psram_malloc(content_length + 1);
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
        ESP_LOGD(TAG, "content: %s, size: %d", result, strlen(result));
    }

end:
    free(url);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result != NULL ? result : NULL;
}

static int __https_mqtt_token_get(struct sensecraft_mqtt_connect_info *p_info, 
                                  const char *p_token)
{
    char *result = __request(SENSECAP_URL, p_token, SENSECAP_PATH_TOKEN_GET, "application/json", HTTP_METHOD_GET, NULL, NULL,0);
    if (result == NULL)
    {
        ESP_LOGE(TAG, "request failed");
        return -1;
    }
    cJSON *root = cJSON_Parse(result);
    if (root == NULL) {
        free(result);
        ESP_LOGE(TAG,"Error before: [%s]\n", cJSON_GetErrorPtr());
        return -1;
    }
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (  code->valueint != 0) {
        ESP_LOGE(TAG,"Code: %d\n", code->valueint);
        free(result);
        cJSON_Delete(root);
        return -1;
    }
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (data != NULL && cJSON_IsObject(data)) {

        cJSON *serverUrl_json = cJSON_GetObjectItem(data, "serverUrl");
        if (serverUrl_json != NULL && cJSON_IsString(serverUrl_json)) {
            strcpy(p_info->serverUrl, serverUrl_json->valuestring);
        }
        cJSON *token_json = cJSON_GetObjectItem(data, "token");
        if (token_json != NULL && cJSON_IsString(token_json)) {
            strcpy(p_info->token, token_json->valuestring);
        }
        cJSON *expiresIn_json = cJSON_GetObjectItem(data, "expiresIn");
        if (expiresIn_json != NULL && cJSON_IsString(expiresIn_json)) {
            p_info->expiresIn = atoll(expiresIn_json->valuestring)/1000;
        }
        cJSON *mqttPort_json = cJSON_GetObjectItem(data, "mqttPort");
        if (mqttPort_json != NULL && cJSON_IsString(mqttPort_json)) {
            p_info->mqttPort = atoi(mqttPort_json->valuestring);
        }
        cJSON *mqttsPort_json = cJSON_GetObjectItem(data, "mqttsPort");
        if (mqttsPort_json != NULL && cJSON_IsString(mqttsPort_json)) {
            p_info->mqttsPort = atoi(mqttsPort_json->valuestring);
        }
    }
    ESP_LOGI(TAG, "Server URL: %s", p_info->serverUrl);
    ESP_LOGI(TAG, "Token: %s", p_info->token);
    ESP_LOGI(TAG, "Expires In: %d", p_info->expiresIn);
    ESP_LOGI(TAG, "MQTT Port: %d", p_info->mqttPort);
    ESP_LOGI(TAG, "MQTTS Port: %d", p_info->mqttsPort);

    cJSON_Delete(root);
    free(result);
    return 0;
}

/*************************************************************************
 * MQTT Client
 ************************************************************************/
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/**
 * This func is called by __mqtt_event_handler, i.e. under the context of MQTT task 
 * (the MQTT task is within the esp-mqtt component)
 * It's OK to process a MQTT msg with max length = 2048 (2048 is inbox buffer size of
 * MQTT compoent, configured with menuconfig)
*/
static void __parse_mqtt_tasklist(char *mqtt_msg_buff, int msg_buff_len)
{
    esp_err_t  ret = ESP_OK;
    bool need_stop = false;
    intmax_t tid = 0;
    intmax_t ctd = 0;

    ESP_LOGI(TAG, "start to parse tasklist from MQTT msg ...");
    ESP_LOGD(TAG, "MQTT msg: \r\n %.*s\r\nlen=%d", msg_buff_len, mqtt_msg_buff, msg_buff_len);
    
    cJSON *json_root = cJSON_Parse(mqtt_msg_buff);
    if (json_root == NULL) {
        ESP_LOGE(TAG, "Failed to parse cJSON object for MQTT msg:");
        ESP_LOGE(TAG, "%.*s\r\n", msg_buff_len, mqtt_msg_buff);
        return;
    }

    cJSON *requestid = cJSON_GetObjectItem(json_root, "requestId");
    if ( requestid == NULL || !cJSON_IsString(requestid)) {
        ESP_LOGE(TAG, "requestid is not a string\n");
        cJSON_Delete(json_root);
        return;
    }

    cJSON *order_arr = cJSON_GetObjectItem(json_root, "order");
    if ( order_arr == NULL || !cJSON_IsArray(order_arr)) {
        ESP_LOGE(TAG, "Order field is not an array\n");
        cJSON_Delete(json_root);
        return;
    }

    cJSON *order_arr_0 = cJSON_GetArrayItem(order_arr, 0);
    if( order_arr_0 == NULL || !cJSON_IsObject(order_arr_0)) {
        cJSON_Delete(json_root);
        return;
    }
    cJSON *value = cJSON_GetObjectItem(order_arr_0, "value");
    if( value == NULL || !cJSON_IsObject(value) ) {
        cJSON_Delete(json_root);
        return;
    }
    cJSON *tl = cJSON_GetObjectItem(value, "tl");
    if( tl == NULL || !cJSON_IsObject(tl) ) {
        cJSON_Delete(json_root);
        return;
    }
    cJSON *task_flow_json = cJSON_GetObjectItem(tl, "task_flow");
    if( task_flow_json == NULL) {
        need_stop = true;
    } else {
        need_stop = false;
    }

    cJSON *tlid_json = cJSON_GetObjectItem(tl, "tlid");
    if (tlid_json == NULL || !cJSON_IsNumber(tlid_json))
    {
        ESP_LOGE(TAG, "tlid is not number");
    } else {
        tid = (intmax_t)tlid_json->valuedouble;
    }

    cJSON *ctd_json = cJSON_GetObjectItem(tl, "ctd");
    if (ctd_json == NULL || !cJSON_IsNumber(ctd_json))
    {
        ESP_LOGE(TAG, "ctd is not number");
    } else {
        ctd = (intmax_t)ctd_json->valuedouble;
    }

    char *tl_str = cJSON_PrintUnformatted(tl);


    if( need_stop) {
        ESP_LOGI(TAG, "STOP TASK FLOW");
        ret = app_sensecraft_mqtt_taskflow_ack( requestid->valuestring, tid, ctd, TF_STATUS_STOPING);
        if( ret != ESP_OK ) {
            ESP_LOGW(TAG, "Failed to report taskflow ack to MQTT server");
        }
        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_TASK_FLOW_STOP, NULL, NULL, pdMS_TO_TICKS(10000));
        free(tl_str);
    } else {
        int engine_status = 0;
        bool is_need_run_taskflow = true;

        if( app_ota_fw_is_running()){
            is_need_run_taskflow = false;
            engine_status = TF_STATUS_ERR_DEVICE_OTA;
            ESP_LOGW(TAG, "Ota is running, can't start taskflow");
        } else if(app_vi_session_is_running()) {
            is_need_run_taskflow = false;
            engine_status = TF_STATUS_ERR_DEVICE_VI;
            ESP_LOGW(TAG, "VI is running, can't start taskflow");
        } else {
            tf_engine_status_get(&engine_status);
            if(engine_status == TF_STATUS_STARTING ) {
                engine_status = TF_STATUS_ERR_GENERAL;
                is_need_run_taskflow = false;
                ESP_LOGW(TAG, "Taskflow is starting, can't start taskflow");
            } else {
                ESP_LOGI(TAG, "start taskflow");
                engine_status = TF_STATUS_STARTING;
            }
        }

        ret = app_sensecraft_mqtt_taskflow_ack( requestid->valuestring, tid, ctd, engine_status);
        if( ret != ESP_OK ) {
            ESP_LOGW(TAG, "Failed to report taskflow ack to MQTT server");
        }
        
        if( is_need_run_taskflow ) {
            esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_TASK_FLOW_START_BY_MQTT, 
                                        &tl_str,
                                        sizeof(void *), /* ptr size */
                                        pdMS_TO_TICKS(10000));  
        } else {
            free(tl_str);
        }
    }
    cJSON_Delete(json_root);
}

static void __parse_mqtt_task_report(char *mqtt_msg_buff, int msg_buff_len)
{
    ESP_LOGI(TAG, "start to parse task-report from MQTT msg ...");
    ESP_LOGD(TAG, "MQTT msg: \r\n %.*s\r\nlen=%d", msg_buff_len, mqtt_msg_buff, msg_buff_len);

    cJSON *tmp_cjson = cJSON_Parse(mqtt_msg_buff);

    if (tmp_cjson == NULL) {
        ESP_LOGE(TAG, "failed to parse cJSON object for MQTT msg:");
        ESP_LOGE(TAG, "%.*s\r\n", msg_buff_len, mqtt_msg_buff);
        return;
    }
    cJSON_Delete(tmp_cjson);

    esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_TASK_FLOW_STATUS_REPORT,
                                    NULL,
                                    0,
                                    pdMS_TO_TICKS(10000));
}

static void __parse_mqtt_version_notify(char *mqtt_msg_buff, int msg_buff_len)
{
    ESP_LOGI(TAG, "start to parse version-notify from MQTT msg ...");
    ESP_LOGD(TAG, "MQTT msg: \r\n %.*s\r\nlen=%d", msg_buff_len, mqtt_msg_buff, msg_buff_len);
    
    cJSON *tmp_cjson = cJSON_Parse(mqtt_msg_buff);

    if (tmp_cjson == NULL) {
        ESP_LOGE(TAG, "failed to parse cJSON object for MQTT msg:");
        ESP_LOGE(TAG, "%.*s\r\n", msg_buff_len, mqtt_msg_buff);
        return;
    }
    // since there's only one consumer of version-notify msg, we just post it to event loop,
    // it's up to the consumer to free the memory of the cJSON object.
    esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_OTA_JSON,
                                    &tmp_cjson,
                                    sizeof(void *), /* ptr size */
                                    pdMS_TO_TICKS(10000));
}

static void __parse_mqtt_prewiew_exit(struct app_sensecraft *p_sensecraft, char *mqtt_msg_buff, int msg_buff_len)
{
    ESP_LOGI(TAG, "start to parse camera-preview-exit from MQTT msg ...");
    ESP_LOGD(TAG, "MQTT msg: \r\n %.*s\r\nlen=%d", msg_buff_len, mqtt_msg_buff, msg_buff_len);

    cJSON *tmp_cjson = cJSON_Parse(mqtt_msg_buff);

    if (tmp_cjson == NULL) {
        ESP_LOGE(TAG, "failed to parse cJSON object for MQTT msg:");
        ESP_LOGE(TAG, "%.*s\r\n", msg_buff_len, mqtt_msg_buff);
        return;
    }
    cJSON_Delete(tmp_cjson);
    
    if (esp_timer_is_active( p_sensecraft->timer_handle ) == true){
        esp_timer_stop(p_sensecraft->timer_handle);
        ESP_LOGI(TAG, "stop timer");
    }

    __data_lock(p_sensecraft);
    p_sensecraft->preview_flag = false;
    p_sensecraft->preview_interval = 1;
    p_sensecraft->preview_continuous = 1;
    p_sensecraft->preview_timeout_s = 0;
    p_sensecraft->preview_last_send_time = 0; 
    __data_unlock(p_sensecraft);
}

static void __parse_mqtt_prewiew_start(struct app_sensecraft *p_sensecraft, char *mqtt_msg_buff, int msg_buff_len)
{
    ESP_LOGI(TAG, "start to parse camera-preview-start from MQTT msg ...");
    ESP_LOGD(TAG, "MQTT msg: \r\n %.*s\r\nlen=%d", msg_buff_len, mqtt_msg_buff, msg_buff_len);

    cJSON *json_root = cJSON_Parse(mqtt_msg_buff);
    if (json_root == NULL) {
        ESP_LOGE(TAG, "Failed to parse cJSON object for MQTT msg:");
        ESP_LOGE(TAG, "%.*s\r\n", msg_buff_len, mqtt_msg_buff);
        return;
    }
    cJSON *order_arr = cJSON_GetObjectItem(json_root, "order");
    if ( order_arr == NULL || !cJSON_IsArray(order_arr)) {
        ESP_LOGE(TAG, "Order field is not an array\n");
        cJSON_Delete(json_root);
        return;
    }

    cJSON *order_arr_0 = cJSON_GetArrayItem(order_arr, 0);
    if( order_arr_0 == NULL || !cJSON_IsObject(order_arr_0)) {
        cJSON_Delete(json_root);
        return;
    }
    cJSON *value = cJSON_GetObjectItem(order_arr_0, "value");
    if( value == NULL || !cJSON_IsObject(value) ) {
        ESP_LOGE(TAG, "value field is not an object\n");
        cJSON_Delete(json_root);
        return;
    }

    if (esp_timer_is_active( p_sensecraft->timer_handle ) == true){
        esp_timer_stop(p_sensecraft->timer_handle);
        ESP_LOGI(TAG, "stop timer");
    }

    __data_lock(p_sensecraft);
    cJSON *interval_json = cJSON_GetObjectItem(value, "interval");
    if( interval_json == NULL || !cJSON_IsNumber(interval_json) ) {
        ESP_LOGE(TAG, "interval field is not an number\n");
        p_sensecraft->preview_interval = 1;
    } else {
        p_sensecraft->preview_interval = interval_json->valueint;
    }
    cJSON *continuous_json = cJSON_GetObjectItem(value, "continuous");
    if( continuous_json == NULL || !cJSON_IsNumber(continuous_json) ) {
        p_sensecraft->preview_continuous = 1;
    } else {
        p_sensecraft->preview_continuous = continuous_json->valueint;
    }

    cJSON *timeout_json = cJSON_GetObjectItem(value, "timeout");
    if( timeout_json == NULL || !cJSON_IsNumber(timeout_json) ) {
        p_sensecraft->preview_timeout_s = 0;
    } else {
        p_sensecraft->preview_timeout_s = timeout_json->valueint;
    }
    p_sensecraft->preview_flag = true;
    p_sensecraft->preview_last_send_time = 0;

    ESP_LOGI(TAG, "interval=%d, continuous=%d, timeout=%d", p_sensecraft->preview_interval, p_sensecraft->preview_continuous, p_sensecraft->preview_timeout_s);
    __data_unlock(p_sensecraft);

    if( p_sensecraft->preview_timeout_s > 0 ) {
        ESP_LOGI(TAG, "start timer");
        esp_timer_start_once(p_sensecraft->timer_handle, p_sensecraft->preview_timeout_s * 1000000);
    }
    cJSON_Delete(json_root);
}

static void __mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    struct app_sensecraft *p_sensecraft = (struct app_sensecraft *)handler_args;
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            p_sensecraft->mqtt_connected_flag = true;
            esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_CONNECTED, 
                                NULL, 0, pdMS_TO_TICKS(10000));

            // TODO maybe repeat subscribe ?
            msg_id = esp_mqtt_client_subscribe(client, p_sensecraft->topic_down_task_publish, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d, topic=%s", msg_id, p_sensecraft->topic_down_task_publish);

            msg_id = esp_mqtt_client_subscribe(client, p_sensecraft->topic_down_version_notify, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d, topic=%s", msg_id, p_sensecraft->topic_down_version_notify);

            msg_id = esp_mqtt_client_subscribe(client, p_sensecraft->topic_down_task_report, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d, topic=%s", msg_id, p_sensecraft->topic_down_task_report);

            msg_id = esp_mqtt_client_subscribe(client, p_sensecraft->topic_down_preview_start, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d, topic=%s", msg_id, p_sensecraft->topic_down_preview_start);

            msg_id = esp_mqtt_client_subscribe(client, p_sensecraft->topic_down_preview_exit, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d, topic=%s", msg_id, p_sensecraft->topic_down_preview_exit);

            if( p_sensecraft->p_mqtt_recv_buf ) {
                free(p_sensecraft->p_mqtt_recv_buf);
                p_sensecraft->p_mqtt_recv_buf = NULL;
            }

            break;
        case MQTT_EVENT_DISCONNECTED:
            if( p_sensecraft->p_mqtt_recv_buf ) {
                free(p_sensecraft->p_mqtt_recv_buf);
                p_sensecraft->p_mqtt_recv_buf = NULL;
            } 
            p_sensecraft->mqtt_connected_flag = false;
            esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_DISCONNECTED, 
                                NULL, 0, pdMS_TO_TICKS(10000));
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");

            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:

            if (get_cloud_service_switch(MAX_CALLER) == 0) {
                ESP_LOGW(TAG, "cloud service is off, ignore data");
                break;
            }

            if( event->total_data_len !=  event->data_len ) {
                if( event->current_data_offset == 0 ) {
                    ESP_LOGI(TAG, "START RECV:%d", event->total_data_len);
                    memset(p_sensecraft->topic_cache, 0, sizeof(p_sensecraft->topic_cache));
                    memcpy(p_sensecraft->topic_cache, event->topic, event->topic_len);
                    if( p_sensecraft->p_mqtt_recv_buf ) {
                        free(p_sensecraft->p_mqtt_recv_buf);
                        p_sensecraft->p_mqtt_recv_buf = NULL;
                    }
                    p_sensecraft->p_mqtt_recv_buf = psram_malloc(event->total_data_len);
                    if( p_sensecraft->p_mqtt_recv_buf == NULL ) {
                        ESP_LOGE(TAG, "psram_malloc %d failed", event->total_data_len);
                        break;
                    }
                }

                if( p_sensecraft->p_mqtt_recv_buf != NULL ) {
                    memcpy( p_sensecraft->p_mqtt_recv_buf + event->current_data_offset, event->data, event->data_len);  
                }

                if( (event->current_data_offset + event->data_len) != event->total_data_len ) {
                    ESP_LOGI(TAG, "RECV DATA len:%d, offset:%d", event->data_len, event->current_data_offset);
                    break;
                }
                ESP_LOGI(TAG, "RECV END: len:%d", event->total_data_len);
            } else {
                if( p_sensecraft->p_mqtt_recv_buf ) {
                    free(p_sensecraft->p_mqtt_recv_buf);
                    p_sensecraft->p_mqtt_recv_buf = NULL;
                }
            }
            
            ESP_LOGI(TAG, "MQTT_EVENT_DATA TOPIC=%.*s", event->topic_len, event->topic);
            // printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            // printf("DATA=%.*s\r\n", event->total_data_len, event->data);  
            
            char *p_data = NULL;
            char *p_topic = NULL;
            size_t len = 0;

            if( p_sensecraft->p_mqtt_recv_buf ) {
                p_data = p_sensecraft->p_mqtt_recv_buf;
                len = event->total_data_len;
            } else if( event->total_data_len ==  event->data_len){
                p_data = event->data;
                len = event->total_data_len;
                p_topic = event->topic;
                memset(p_sensecraft->topic_cache, 0, sizeof(p_sensecraft->topic_cache));
                memcpy(p_sensecraft->topic_cache, event->topic, event->topic_len);
            } else {
                ESP_LOGE(TAG, "Receive exception");
                break;
            }

            // handle data

            if (strstr(p_sensecraft->topic_cache, "task-publish")) {
                __parse_mqtt_tasklist(p_data, len);
            } else if (strstr(p_sensecraft->topic_cache, "version-notify")) {
                __parse_mqtt_version_notify(p_data, len);
            } else if (strstr(p_sensecraft->topic_cache, "task-inquiry")) {
                __parse_mqtt_task_report(p_data, len);
            } else if (strstr(p_sensecraft->topic_cache, "camera-preview-exit")) {
                __parse_mqtt_prewiew_exit(p_sensecraft, p_data, len);
            } else if (strstr(p_sensecraft->topic_cache, "camera-preview-start")) {
                __parse_mqtt_prewiew_start(p_sensecraft, p_data, len);
            }

            if( p_sensecraft->p_mqtt_recv_buf ) {
                free(p_sensecraft->p_mqtt_recv_buf);
                p_sensecraft->p_mqtt_recv_buf = NULL;
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

static void __sensecraft_task(void *p_arg)
{
    ESP_LOGI(TAG, "sensecraft start:%s", SENSECAP_URL);
    struct app_sensecraft *p_sensecraft = (struct app_sensecraft *)p_arg;
    struct sensecraft_mqtt_connect_info *p_mqtt_info = &p_sensecraft->mqtt_info;

    esp_err_t ret = 0;
    time_t now = 0;
    bool mqtt_client_inited = false;
    size_t len =0;
    bool  is_need_update_token = false;
    int http_fail_cnt = 0;

    sniprintf(p_sensecraft->topic_down_task_publish, MQTT_TOPIC_STR_LEN, 
                "sensecraft/ipnode/%s/get/order/task-publish", p_sensecraft->deviceinfo.eui);
    sniprintf(p_sensecraft->topic_down_version_notify, MQTT_TOPIC_STR_LEN, 
                "sensecraft/ipnode/%s/get/order/version-notify", p_sensecraft->deviceinfo.eui);
    sniprintf(p_sensecraft->topic_down_task_report, MQTT_TOPIC_STR_LEN, 
                "sensecraft/ipnode/%s/get/order/task-inquiry", p_sensecraft->deviceinfo.eui);
    sniprintf(p_sensecraft->topic_down_preview_start, MQTT_TOPIC_STR_LEN,
                "sensecraft/ipnode/%s/get/order/camera-preview-start", p_sensecraft->deviceinfo.eui);
    sniprintf(p_sensecraft->topic_down_preview_exit, MQTT_TOPIC_STR_LEN,
                "sensecraft/ipnode/%s/get/order/camera-preview-exit", p_sensecraft->deviceinfo.eui);

    sniprintf(p_sensecraft->topic_up_task_publish_ack, MQTT_TOPIC_STR_LEN, 
                "sensecraft/ipnode/%s/update/order/task-publish-ack", p_sensecraft->deviceinfo.eui);
    sniprintf(p_sensecraft->topic_up_taskflow_report, MQTT_TOPIC_STR_LEN, 
                "sensecraft/ipnode/%s/update/event/task-flow-report", p_sensecraft->deviceinfo.eui);
    sniprintf(p_sensecraft->topic_up_change_device_status, MQTT_TOPIC_STR_LEN, 
                "sensecraft/ipnode/%s/update/event/change-device-status", p_sensecraft->deviceinfo.eui);
    sniprintf(p_sensecraft->topic_up_warn_event_report, MQTT_TOPIC_STR_LEN, 
                "sensecraft/ipnode/%s/update/event/measure-sensor", p_sensecraft->deviceinfo.eui);
    sniprintf(p_sensecraft->topic_up_model_ota_status, MQTT_TOPIC_STR_LEN, 
                "sensecraft/ipnode/%s/update/event/model-ota-status", p_sensecraft->deviceinfo.eui);
    sniprintf(p_sensecraft->topic_up_firmware_ota_status, MQTT_TOPIC_STR_LEN,
                "sensecraft/ipnode/%s/update/event/firmware-ota-status", p_sensecraft->deviceinfo.eui);
    sniprintf(p_sensecraft->topic_up_preview_upload, MQTT_TOPIC_STR_LEN,
                "sensecraft/ipnode/%s/update/event/camera-preview-upload", p_sensecraft->deviceinfo.eui);

    ESP_LOGI(TAG, "topic_down_task_publish=%s", p_sensecraft->topic_down_task_publish);
    ESP_LOGI(TAG, "topic_down_version_notify=%s", p_sensecraft->topic_down_version_notify);
    ESP_LOGI(TAG, "topic_down_task_report=%s", p_sensecraft->topic_down_task_report);
    ESP_LOGI(TAG, "topic_down_preview_start=%s", p_sensecraft->topic_down_preview_start);
    ESP_LOGI(TAG, "topic_down_preview_exit=%s", p_sensecraft->topic_down_preview_exit);
    ESP_LOGI(TAG, "topic_up_task_publish_ack=%s", p_sensecraft->topic_up_task_publish_ack);
    ESP_LOGI(TAG, "topic_up_taskflow_report=%s", p_sensecraft->topic_up_taskflow_report);
    ESP_LOGI(TAG, "topic_up_change_device_status=%s", p_sensecraft->topic_up_change_device_status);
    ESP_LOGI(TAG, "topic_up_warn_event_report=%s", p_sensecraft->topic_up_warn_event_report);
    ESP_LOGI(TAG, "topic_up_model_ota_status=%s", p_sensecraft->topic_up_model_ota_status);
    ESP_LOGI(TAG, "topic_up_firmware_ota_status=%s", p_sensecraft->topic_up_firmware_ota_status);
    ESP_LOGI(TAG, "topic_up_preview_upload=%s", p_sensecraft->topic_up_preview_upload);

    while (1) {
        
        xSemaphoreTake(p_sensecraft->net_sem_handle, pdMS_TO_TICKS(10000));
        if (!p_sensecraft->net_flag ) {
            continue;
        }

        time(&now);  // now is seconds since unix epoch
        if( p_sensecraft->timesync_flag ) {
            if ((p_mqtt_info->expiresIn) < ((int)now + 60))  {
                is_need_update_token = true;
            } 
        } else {
            if( p_sensecraft->last_http_time == 0 ) {
                is_need_update_token = true;
            } else if( difftime(now, p_sensecraft->last_http_time) > (60*60) ) {
                is_need_update_token = true;
            }
        }

        if( is_need_update_token ) {
            is_need_update_token = false;
            vTaskDelay(1000 / portTICK_PERIOD_MS); //wait ntp sync priority
            ESP_LOGI(TAG, "mqtt token is near expiration, now: %d, expire: %d, refresh it ...", (int)now, (p_mqtt_info->expiresIn));
            
            ret = __https_mqtt_token_get(p_mqtt_info, (const char *)p_sensecraft->https_token);
            if( ret == 0 ) {
                http_fail_cnt = 0;
                p_sensecraft->last_http_time = now;
                snprintf(p_sensecraft->mqtt_broker_uri, sizeof(p_sensecraft->mqtt_broker_uri), "mqtt://%s:%d", \
                                                p_mqtt_info->serverUrl, p_mqtt_info->mqttPort);
                snprintf(p_sensecraft->mqtt_client_id,  sizeof(p_sensecraft->mqtt_client_id), "device-3000-%s", p_sensecraft->deviceinfo.eui);
                memcpy(p_sensecraft->mqtt_password, p_mqtt_info->token, sizeof(p_mqtt_info->token));

                ESP_LOGI(TAG, "mqtt connect info changed, uri: %s", p_sensecraft->mqtt_broker_uri);
                p_sensecraft->mqtt_cfg.broker.address.uri = p_sensecraft->mqtt_broker_uri;
                p_sensecraft->mqtt_cfg.credentials.username = p_sensecraft->mqtt_client_id;
                p_sensecraft->mqtt_cfg.credentials.client_id = p_sensecraft->mqtt_client_id;
                p_sensecraft->mqtt_cfg.credentials.authentication.password = p_sensecraft->mqtt_password;
                p_sensecraft->mqtt_cfg.session.disable_clean_session = true;
                p_sensecraft->mqtt_cfg.network.disable_auto_reconnect = false;
                p_sensecraft->mqtt_cfg.network.reconnect_timeout_ms = 15000; // undetermined

                if (!mqtt_client_inited) {
                    p_sensecraft->mqtt_handle = esp_mqtt_client_init(&p_sensecraft->mqtt_cfg);
                    esp_mqtt_client_register_event(p_sensecraft->mqtt_handle, ESP_EVENT_ANY_ID, __mqtt_event_handler, p_sensecraft);
                    esp_mqtt_client_start(p_sensecraft->mqtt_handle);
                    mqtt_client_inited = true;
                    ESP_LOGI(TAG, "mqtt client started!");
                } else {
                    esp_mqtt_set_config(p_sensecraft->mqtt_handle, &p_sensecraft->mqtt_cfg);
                    esp_mqtt_client_reconnect(p_sensecraft->mqtt_handle);
                    ESP_LOGI(TAG, "mqtt client start reconnecting ...");
                }

            } else {
                ESP_LOGE(TAG, "get token error, ret: %d", ret);
                ESP_LOGE(TAG, "wait %ds retry.", (http_fail_cnt + 1) * 10);
                if( http_fail_cnt ) {                    
                    vTaskDelay(10000 * http_fail_cnt / portTICK_PERIOD_MS);
                }
                http_fail_cnt++;
                if( http_fail_cnt >= 10 ) {
                    http_fail_cnt = 10;
                }
            }
        }

    } 
}

static void __event_loop_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    struct app_sensecraft *p_sensecraft = (struct app_sensecraft *)handler_args;

    if ( base == CTRL_EVENT_BASE) {
        switch (id) {
            case CTRL_EVENT_SNTP_TIME_SYNCED:
            {
                ESP_LOGI(TAG, "received event: CTRL_EVENT_SNTP_TIME_SYNCED");
                p_sensecraft->timesync_flag = true;
                break;
            }
        default:
            break;
        }
    } else if( base == VIEW_EVENT_BASE) {
        switch (id) {
            case VIEW_EVENT_WIFI_ST:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
                struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
                if (p_st->is_network) {
                    p_sensecraft->net_flag = true;
                } else {
                    p_sensecraft->net_flag = false;
                }
                xSemaphoreGive(p_sensecraft->net_sem_handle);
                break;
            }
        default:
            break;
        }
    }
}

static void __timer_callback(void* p_arg)
{
    struct app_sensecraft *p_sensecraft = (struct app_sensecraft *)p_arg;
    ESP_LOGI(TAG, "preview timeout");

    __data_lock(p_sensecraft);
    p_sensecraft->preview_flag = false;
    p_sensecraft->preview_interval = 1;
    p_sensecraft->preview_continuous = 1;
    p_sensecraft->preview_timeout_s = 0;
    p_sensecraft->preview_last_send_time = 0;
    __data_unlock(p_sensecraft);
}

/*************************************************************************
 * API
 ************************************************************************/
esp_err_t app_sensecraft_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    esp_err_t ret = ESP_OK;
    struct app_sensecraft * p_sensecraft = NULL;
    gp_sensecraft = (struct app_sensecraft *) psram_malloc(sizeof(struct app_sensecraft));
    if (gp_sensecraft == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    p_sensecraft = gp_sensecraft;
    memset(p_sensecraft, 0, sizeof( struct app_sensecraft ));
    
    p_sensecraft->preview_flag = false;
    p_sensecraft->preview_interval = 1;
    p_sensecraft->preview_continuous = 1;
    p_sensecraft->preview_timeout_s = 0;
    p_sensecraft->preview_last_send_time = 0;

    ret  = sensecraft_deviceinfo_get(&p_sensecraft->deviceinfo);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "deviceinfo read fail %d!", ret);

    ret = app_sensecraft_https_token_gen(&p_sensecraft->deviceinfo, (char *)p_sensecraft->https_token, sizeof(p_sensecraft->https_token));
    ESP_GOTO_ON_ERROR(ret, err, TAG, "sensecraft token gen fail %d!", ret);
    ESP_LOGI(TAG, "\n EUI:%s\n KEY:%s\n Token:%s", p_sensecraft->deviceinfo.eui, p_sensecraft->deviceinfo.key, p_sensecraft->https_token);

    p_sensecraft->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_sensecraft->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_sensecraft->net_sem_handle = xSemaphoreCreateBinary();
    ESP_GOTO_ON_FALSE(NULL != p_sensecraft->net_sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_sensecraft->p_task_stack_buf = (StackType_t *)psram_malloc(SENSECRAFT_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_sensecraft->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_sensecraft->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_sensecraft->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_sensecraft->task_handle = xTaskCreateStatic(__sensecraft_task,
                                                "app_sensecraft",
                                                SENSECRAFT_TASK_STACK_SIZE,
                                                (void *)p_sensecraft,
                                                SENSECRAFT_TASK_PRIO,
                                                p_sensecraft->p_task_stack_buf,
                                                p_sensecraft->p_task_buf);
    ESP_GOTO_ON_FALSE(p_sensecraft->task_handle, ESP_FAIL, err, TAG, "Failed to create task");

    const esp_timer_create_args_t timer_args = {
            .callback = &__timer_callback,
            .arg = (void*) p_sensecraft,
            .name = "sensecraft_timer"
    };
    ret = esp_timer_create(&timer_args, &p_sensecraft->timer_handle);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "esp_timer_create failed");

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_SNTP_TIME_SYNCED,
                                                            __event_loop_handler, p_sensecraft, NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle,
                                                             VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                                             __event_loop_handler, p_sensecraft, NULL));
    return ESP_OK;

err:
    if(p_sensecraft->task_handle ) {
        vTaskDelete(p_sensecraft->task_handle);
        p_sensecraft->task_handle = NULL;
    }
    if( p_sensecraft->p_task_stack_buf ) {
        free(p_sensecraft->p_task_stack_buf);
        p_sensecraft->p_task_stack_buf = NULL;
    }
    if( p_sensecraft->p_task_buf ) {
        free(p_sensecraft->p_task_buf);
        p_sensecraft->p_task_buf = NULL;
    }
    if (p_sensecraft->sem_handle) {
        vSemaphoreDelete(p_sensecraft->sem_handle);
        p_sensecraft->sem_handle = NULL;
    }
    if (p_sensecraft->net_sem_handle) {
        vSemaphoreDelete(p_sensecraft->net_sem_handle);
        p_sensecraft->net_sem_handle = NULL;
    }
    if (p_sensecraft) {
        free(p_sensecraft);
        gp_sensecraft = NULL;
    }
    ESP_LOGE(TAG, "app_sensecraft_init fail %d!", ret);
    return ret;
}

esp_err_t app_sensecraft_disconnect(void)
{
    int ret = ESP_OK;
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL) {
        return ESP_FAIL;
    }
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_handle != NULL, ESP_FAIL, TAG, "mqtt_client is not inited yet");
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_connected_flag, ESP_FAIL, TAG, "mqtt_client is not connected yet");
    esp_mqtt_client_disconnect(p_sensecraft->mqtt_handle);
    esp_mqtt_client_destroy(p_sensecraft->mqtt_handle);
    
    return ret;
}

bool app_sensecraft_is_connected(void)
{
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL) {
        return false;
    }
    return p_sensecraft->mqtt_connected_flag;
}

static esp_err_t sensecraft_deviceinfo_get(struct sensecraft_deviceinfo *p_info)
{
    esp_err_t ret = ESP_OK;
    const char *eui = NULL;
    const char *key = NULL;

    eui = factory_info_eui_get();
    key = factory_info_device_key_get();
    if (eui == NULL || key == NULL ) {
        ESP_LOGE(TAG, "Failed to get eui or key");
        return ESP_FAIL;
    }
    if( strlen(eui) != 16 || strlen(key) != 32 ) {
        ESP_LOGE(TAG, "Invalid eui or key");
        return ESP_FAIL;
    }
    memcpy(p_info->key, key, 32);
    memcpy(p_info->eui, eui, 16);
    return ESP_OK;
}

esp_err_t app_sensecraft_https_token_get(char *p_token, size_t len)
{
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL || len != HTTPS_TOKEN_LEN ) {
        return ESP_FAIL;
    }
    memcpy(p_token, p_sensecraft->https_token, len);
    return ESP_OK;
}

esp_err_t app_sensecraft_https_token_gen(struct sensecraft_deviceinfo *p_deviceinfo, char *p_token, size_t len)
{
    esp_err_t ret = ESP_OK;
    size_t str_len = 0;
    size_t token_len = 0;
    char deviceinfo_buf[70];
    memset(deviceinfo_buf, 0, sizeof(deviceinfo_buf));
    str_len = snprintf(deviceinfo_buf, sizeof(deviceinfo_buf), "%s:%s", p_deviceinfo->eui, p_deviceinfo->key);
    ret = mbedtls_base64_encode(( uint8_t *)p_token, len, &token_len, ( uint8_t *)deviceinfo_buf, str_len);
    if( ret != 0  ||  token_len < 60 ) {
        ESP_LOGE(TAG, "mbedtls_base64_encode failed:%d,", ret);
        return ret;
    }
    return ESP_OK;
}

esp_err_t app_sensecraft_mqtt_taskflow_ack(char *request_id,  
                                           intmax_t taskflow_id,
                                           intmax_t taskflow_ctd,
                                           int taskflow_status)
{
    int ret = ESP_OK;
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL) {
        return ESP_FAIL;
    }
    const char *json_fmt =  \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %jd,"
        "\"intent\": \"order\","
        "\"type\": \"response\","
        "\"deviceEui\": \"%s\","
        "\"order\":  ["
            "{"
                "\"name\": \"task-publish-ack\","
                "\"value\": {"
                    "\"tlid\": %jd,"
                    "\"ctd\": %jd,"
                    "\"status\": %d"
                "}"
            "}"
        "]"
    "}";

    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_handle != NULL, ESP_FAIL, TAG, "mqtt_client is not inited yet");
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_connected_flag, ESP_FAIL, TAG, "mqtt_client is not connected yet");

    size_t json_buf_len = 2048;
    char *json_buff = psram_malloc( json_buf_len );
    ESP_RETURN_ON_FALSE(json_buff != NULL, ESP_FAIL, TAG, "psram_malloc failed");

    time_t timestamp_ms = util_get_timestamp_ms();

    size_t json_len = sniprintf(json_buff, json_buf_len, json_fmt, request_id, timestamp_ms, \
                                    p_sensecraft->deviceinfo.eui, taskflow_id, taskflow_ctd, taskflow_status);

    ESP_LOGD(TAG, "app_sensecraft_mqtt_taskflow_ack: \r\n%s\r\nstrlen=%d", json_buff, json_len);

    int msg_id = esp_mqtt_client_enqueue(p_sensecraft->mqtt_handle, p_sensecraft->topic_up_task_publish_ack, json_buff, json_len,
                                        MQTT_PUB_QOS1, false/*retain*/, true/*store*/);

    free(json_buff);

    if (msg_id < 0) {
        ESP_LOGW(TAG, "app_sensecraft_mqtt_taskflow_ack enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    }

    return ret;
}

esp_err_t app_sensecraft_mqtt_report_taskflow_status(intmax_t taskflow_id,
                                                     intmax_t taskflow_ctd,
                                                     int taskflow_status,
                                                     char *p_module_name,
                                                     int module_status)
{
    int ret = ESP_OK;
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL) {
        return ESP_FAIL;
    }
    const char *json_fmt =  \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %jd,"
        "\"intent\": \"event\","
        "\"deviceEui\": \"%s\","
        "\"events\":  ["
            "{"
                "\"name\": \"task-flow-report\","
                "\"value\": {"
                    "\"type\": 1,"
                    "\"tlid\": %jd,"
                    "\"ctd\": %jd,"
                    "\"status\": %d,"
                    "\"module_name\": \"%s\","
                    "\"err_code\": %d"
                "}"
            "}"
        "]"
    "}";

    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_handle != NULL, ESP_FAIL, TAG, "mqtt_client is not inited yet");
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_connected_flag, ESP_FAIL, TAG, "mqtt_client is not connected yet");

    size_t json_buf_len = 2048;
    char *json_buff = psram_malloc( json_buf_len );
    ESP_RETURN_ON_FALSE(json_buff != NULL, ESP_FAIL, TAG, "psram_malloc failed");

    char uuid[37];
    time_t timestamp_ms = util_get_timestamp_ms();

    UUIDGen(uuid);
    
    char *p_err_module = NULL;
    if( p_module_name ) {
        p_err_module = p_module_name;
    } else {
        p_err_module = "unknown";
    }
    size_t json_len = sniprintf(json_buff, json_buf_len, json_fmt, uuid, timestamp_ms, \
                                    p_sensecraft->deviceinfo.eui, taskflow_id, taskflow_ctd, taskflow_status, p_err_module, module_status);

    ESP_LOGD(TAG, "app_sensecraft_mqtt_report_taskflow_status: \r\n%s\r\nstrlen=%d", json_buff, json_len);

    int msg_id = esp_mqtt_client_enqueue(p_sensecraft->mqtt_handle, p_sensecraft->topic_up_taskflow_report, json_buff, json_len,
                                        MQTT_PUB_QOS1, false/*retain*/, true/*store*/);

    free(json_buff);

    if (msg_id < 0) {
        ESP_LOGW(TAG, "app_sensecraft_mqtt_report_taskflow_status enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    }

    return ret;
}

esp_err_t app_sensecraft_mqtt_report_taskflow_info(intmax_t taskflow_id,
                                                    intmax_t taskflow_ctd,
                                                    int taskflow_status,
                                                    char *p_module_name,
                                                    int module_status,
                                                    char *p_str, size_t len)
{
    int ret = ESP_OK;
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL) {
        return ESP_FAIL;
    }
    const char *json_fmt =  \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %jd,"
        "\"intent\": \"event\","
        "\"deviceEui\": \"%s\","
        "\"events\":  ["
            "{"
                "\"name\": \"task-flow-report\","
                "\"value\": {"
                    "\"tlid\": %jd,"
                    "\"ctd\": %jd,"
                    "\"status\": %d,"
                    "\"module_name\": \"%s\","
                    "\"err_code\": %d,"
                    "\"tl\": %.*s"
                "}"
            "}"
        "]"
    "}";

    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_handle != NULL, ESP_FAIL, TAG, "mqtt_client is not inited yet");
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_connected_flag, ESP_FAIL, TAG, "mqtt_client is not connected yet");

    size_t json_buf_len = len + 512;
    char *json_buff = psram_malloc( json_buf_len );
    ESP_RETURN_ON_FALSE(json_buff != NULL, ESP_FAIL, TAG, "psram_malloc failed");

    char uuid[37];
    time_t timestamp_ms = util_get_timestamp_ms();

    UUIDGen(uuid);
    
    char *p_err_module = NULL;
    if( p_module_name ) {
        p_err_module = p_module_name;
    } else {
        p_err_module = "unknown";
    }
    size_t json_len = sniprintf(json_buff, json_buf_len, json_fmt, uuid, timestamp_ms, \
                                    p_sensecraft->deviceinfo.eui, taskflow_id, taskflow_ctd, taskflow_status, p_err_module, module_status, len, p_str);

    ESP_LOGD(TAG, "app_sensecraft_mqtt_report_taskflow_info: \r\n%s\r\nstrlen=%d", json_buff, json_len);

    int msg_id = esp_mqtt_client_enqueue(p_sensecraft->mqtt_handle, p_sensecraft->topic_up_taskflow_report, json_buff, json_len,
                                        MQTT_PUB_QOS1, false/*retain*/, true/*store*/);

    free(json_buff);

    if (msg_id < 0) {
        ESP_LOGW(TAG, "app_sensecraft_mqtt_report_taskflow_info enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    }

    return ret;
}


esp_err_t app_sensecraft_mqtt_report_taskflow_model_ota_status(intmax_t taskflow_id,
                                                                intmax_t taskflow_ctd,
                                                                int ota_status,
                                                                int ota_percent,
                                                                int err_code)
{
    int ret = ESP_OK;
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL) {
        return ESP_FAIL;
    }
    const char *json_fmt =  \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %jd,"
        "\"intent\": \"event\","
        "\"deviceEui\": \"%s\","
        "\"events\":  ["
            "{"
                "\"name\": \"model-ota-status\","
                "\"value\": {"
                    "\"tlid\": %jd,"
                    "\"ctd\": %jd,"
                    "\"status\": %d,"
                    "\"percent\": %d,"
                    "\"err_code\": %d"
                "}"
            "}"
        "]"
    "}";

    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_handle != NULL, ESP_FAIL, TAG, "mqtt_client is not inited yet");
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_connected_flag, ESP_FAIL, TAG, "mqtt_client is not connected yet");

    size_t json_buf_len = 2048;
    char *json_buff = psram_malloc( json_buf_len );
    ESP_RETURN_ON_FALSE(json_buff != NULL, ESP_FAIL, TAG, "psram_malloc failed");

    char uuid[37];
    time_t timestamp_ms = util_get_timestamp_ms();

    UUIDGen(uuid);
    size_t json_len = sniprintf(json_buff, json_buf_len, json_fmt, uuid, timestamp_ms, \
                                    p_sensecraft->deviceinfo.eui, taskflow_id, taskflow_ctd, ota_status, ota_percent, err_code);

    ESP_LOGD(TAG, "app_sensecraft_mqtt_report_taskflow_model_ota_status: \r\n%s\r\nstrlen=%d", json_buff, json_len);

    int msg_id = esp_mqtt_client_enqueue(p_sensecraft->mqtt_handle, p_sensecraft->topic_up_model_ota_status, json_buff, json_len,
                                        MQTT_PUB_QOS1, false/*retain*/, true/*store*/);

    free(json_buff);

    if (msg_id < 0) {
        ESP_LOGW(TAG, "app_sensecraft_mqtt_report_taskflow_model_ota_status enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    }

    return ret;
}

esp_err_t app_sensecraft_mqtt_report_warn_event(intmax_t taskflow_id, 
                                                char *taskflow_name, 
                                                char *p_img, size_t img_len, 
                                                char *p_msg, size_t msg_len)
{
    int ret = ESP_OK;
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL) {
        return ESP_FAIL;
    }
    const char *json_fmt =  \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %jd,"
        "\"intent\": \"event\","
        "\"deviceEui\": \"%s\","
        "\"events\":  [{"
            "\"name\": \"measure-sensor\","
            "\"value\": [{"
                "\"channel\": 1,"
                "\"measurements\": {"
                    "\"5004\": ["
                        "{"
                            "\"tlid\": %jd,"
                            "\"tn\": \"%s\","
                            "\"image\": \"%.*s\","
                            "\"content\": \"%.*s\""
                        "}"
                    "]"
                "},"
                "\"measureTime\": %jd"
            "}]"
        "}]"
    "}";

    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_handle, ESP_FAIL, TAG, "mqtt_client is not inited yet [3]");
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_connected_flag, ESP_FAIL, TAG, "mqtt_client is not connected yet [3]");

    size_t json_buf_len = img_len + msg_len + 512;
    char *json_buff = psram_malloc( json_buf_len );
    ESP_RETURN_ON_FALSE(json_buff != NULL, ESP_FAIL, TAG, "psram_malloc failed");

    char uuid[37];
    time_t timestamp_ms = util_get_timestamp_ms();

    UUIDGen(uuid);
    size_t json_len = sniprintf(json_buff, json_buf_len, json_fmt, uuid, timestamp_ms, p_sensecraft->deviceinfo.eui,
              taskflow_id, taskflow_name, img_len, p_img, msg_len, p_msg, timestamp_ms);

    ESP_LOGD(TAG, "app_sensecraft_mqtt_report_warn_event: \r\n%s\r\nstrlen=%d", json_buff, json_len);

    int msg_id = esp_mqtt_client_enqueue(p_sensecraft->mqtt_handle, p_sensecraft->topic_up_warn_event_report, json_buff, json_len,
                                        MQTT_PUB_QOS0, false/*retain*/, true/*store*/);

    free(json_buff);

    if (msg_id < 0) {
        ESP_LOGW(TAG, "app_sensecraft_mqtt_report_warn_event enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    }
    return ret;
}

esp_err_t app_sensecraft_mqtt_report_device_status_generic(char *event_value_fields)
{
    int ret = ESP_OK;
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL) {
        return ESP_FAIL;
    }
    const char *json_fmt =  \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %jd,"
        "\"intent\": \"event\","
        "\"deviceEui\": \"%s\","
        "\"events\":  [{"
            "\"name\": \"change-device-status\","
            "\"value\": {%s},"
            "\"timestamp\": %jd"
        "}]"
    "}";

    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_handle, ESP_FAIL, TAG, "mqtt_client is not inited yet [4]");
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_connected_flag, ESP_FAIL, TAG, "mqtt_client is not connected yet [4]");

    char *json_buff = psram_malloc(3000);
    ESP_RETURN_ON_FALSE(json_buff != NULL, ESP_FAIL, TAG, "psram_malloc failed");

    char uuid[37];
    time_t timestamp_ms = util_get_timestamp_ms();

    UUIDGen(uuid);
    sniprintf(json_buff, 3000, json_fmt, uuid, timestamp_ms, p_sensecraft->deviceinfo.eui, event_value_fields, timestamp_ms);

    ESP_LOGD(TAG, "app_sensecraft_mqtt_report_device_status: \r\n%s\r\nstrlen=%d", json_buff, strlen(json_buff));

    int msg_id = esp_mqtt_client_enqueue(p_sensecraft->mqtt_handle, p_sensecraft->topic_up_change_device_status, json_buff, strlen(json_buff),
                                        MQTT_PUB_QOS0, false/*retain*/, true/*store*/);

    free(json_buff);

    if (msg_id < 0) {
        ESP_LOGW(TAG, "app_sensecraft_mqtt_report_device_status enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    }

    return ret;
}

esp_err_t app_sensecraft_mqtt_report_device_status(struct view_data_device_status *dev_status)
{
    int ret = ESP_OK;
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL) {
        return ESP_FAIL;
    }

    // please be carefull about the `comma` if you're appending more fields,
    // there should be NO trailing comma.
    const char *fields =  \
                "\"3000\": %d,"
                "\"3001\": \"%s\","
                "\"3502\": \"%s\"";

    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_handle, ESP_FAIL, TAG, "mqtt_client is not inited yet [4]");
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_connected_flag, ESP_FAIL, TAG, "mqtt_client is not connected yet [4]");

    const int buff_sz = 2048;
    char *json_buff = psram_calloc(1, buff_sz);
    ESP_RETURN_ON_FALSE(json_buff != NULL, ESP_FAIL, TAG, "psram_malloc failed");

    sniprintf(json_buff, buff_sz, fields, 
              dev_status->battery_per, dev_status->hw_version, dev_status->fw_version);

    if (dev_status->himax_fw_version) {
        // himax version might be NULL, if NULL don't include 3577
        int len = strlen(json_buff);
        const char *field3577 = ",\"3577\": \"%s\"";
        sniprintf(json_buff + len, buff_sz - len, field3577, dev_status->himax_fw_version);
    }

    ret = app_sensecraft_mqtt_report_device_status_generic(json_buff);

    free(json_buff);

    return ret;
}

esp_err_t app_sensecraft_mqtt_report_firmware_ota_status_generic(char *ota_status_fields_str)
{
    int ret = ESP_OK;
    struct app_sensecraft *p_sensecraft = gp_sensecraft;
    if (p_sensecraft == NULL)
    {
        return ESP_FAIL;
    }
    const char *json_fmt = \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %jd,"
        "\"intent\": \"event\","
        "\"deviceEui\": \"%s\","
        "\"events\":  [{"
            "\"name\": \"firmware-ota-status\","
            "\"value\": {%s},"
            "\"timestamp\": %jd"
        "}]"
    "}";

    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_handle, ESP_FAIL, TAG, "mqtt_client is not inited yet [4]");
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_connected_flag, ESP_FAIL, TAG, "mqtt_client is not connected yet [4]");

    char *json_buff = psram_malloc(3000);
    ESP_RETURN_ON_FALSE(json_buff != NULL, ESP_FAIL, TAG, "psram_malloc failed");

    char uuid[37];
    time_t timestamp_ms = util_get_timestamp_ms();

    UUIDGen(uuid);
    sniprintf(json_buff, 3000, json_fmt, uuid, timestamp_ms, p_sensecraft->deviceinfo.eui, ota_status_fields_str, timestamp_ms);

    ESP_LOGD(TAG, "app_sensecraft_mqtt_report_firmware_ota_status_generic: \r\n%s\r\nstrlen=%d", json_buff, strlen(json_buff));

    int msg_id = esp_mqtt_client_enqueue(p_sensecraft->mqtt_handle, p_sensecraft->topic_up_firmware_ota_status, json_buff,
                                            strlen(json_buff), MQTT_PUB_QOS1, false /*retain*/, true /*store*/);

    free(json_buff);

    if (msg_id < 0)
    {
        ESP_LOGW(TAG, "app_sensecraft_mqtt_report_firmware_ota_status_generic enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    }

    return ret;
}


esp_err_t app_sensecraft_mqtt_preview_upload_with_reduce_freq(char *p_img, size_t img_len)
{

    int ret = ESP_OK;
    time_t now = 0;
    struct app_sensecraft * p_sensecraft = gp_sensecraft;
    if( p_sensecraft == NULL) {
        return ESP_FAIL;
    }

    if( !p_sensecraft->preview_flag ) {
        return ESP_OK;
    }
    
    time(&now);
    if( difftime(now, p_sensecraft->preview_last_send_time) < (p_sensecraft->preview_interval) ) {
        return ESP_OK;
    }

    if( esp_mqtt_client_get_outbox_size(p_sensecraft->mqtt_handle) > 10240) {
        ESP_LOGW(TAG, "maybe have unsent image, skip.");
        __data_lock(p_sensecraft);
        p_sensecraft->preview_last_send_time = now;
        __data_unlock(p_sensecraft);
        return ESP_OK;
    }

    const char *json_fmt =  \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %jd,"
        "\"intent\": \"event\","
        "\"deviceEui\": \"%s\","
        "\"events\":  [{"
            "\"name\": \"camera-preview-upload\","
            "\"value\": [{"
                "\"data\": {"
                    "\"image\": \"%.*s\""
                "},"
                "\"measureTime\": %jd"
            "}]"
        "}]"
    "}";

    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_handle, ESP_FAIL, TAG, "mqtt_client is not inited yet [3]");
    ESP_RETURN_ON_FALSE(p_sensecraft->mqtt_connected_flag, ESP_FAIL, TAG, "mqtt_client is not connected yet [3]");

    size_t json_buf_len = img_len + 512;
    char *json_buff = psram_malloc( json_buf_len );
    ESP_RETURN_ON_FALSE(json_buff != NULL, ESP_FAIL, TAG, "psram_malloc failed");

    char uuid[37];
    time_t timestamp_ms = util_get_timestamp_ms();

    UUIDGen(uuid);
    size_t json_len = sniprintf(json_buff, json_buf_len, json_fmt, uuid, timestamp_ms, p_sensecraft->deviceinfo.eui,
                               img_len, p_img, timestamp_ms);

    ESP_LOGV(TAG, "app_sensecraft_mqtt_preview_upload: \r\n%s\r\nstrlen=%d", json_buff, json_len);

   
    int msg_id = esp_mqtt_client_enqueue(p_sensecraft->mqtt_handle, p_sensecraft->topic_up_preview_upload, json_buff, json_len,
                                        MQTT_PUB_QOS0, false/*retain*/, true/*store*/);
    ESP_LOGI(TAG, "upload preview image:%d", json_len);

    free(json_buff);

    if (msg_id < 0) {
        ESP_LOGW(TAG, "app_sensecraft_mqtt_preview_upload enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    } else {

        __data_lock(p_sensecraft);
        p_sensecraft->preview_last_send_time = now;
        if( !p_sensecraft->preview_continuous) {
            ESP_LOGI(TAG, "upload preview image done");
            p_sensecraft->preview_flag = false;
        }
        __data_unlock(p_sensecraft);
    }
    return ret;
}