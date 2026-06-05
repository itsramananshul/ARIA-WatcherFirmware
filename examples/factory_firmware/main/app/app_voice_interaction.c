#include "app_voice_interaction.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "event_loops.h" 
#include <mbedtls/base64.h>

#include "sensecap-watcher.h"
#include "util.h"
#include "uuid.h"
#include "app_audio_player.h"
#include "app_audio_recorder.h"
#include "app_rgb.h"
#include "factory_info.h"
#include "app_device_info.h"
#include "tf.h"
#include "app_ota.h"
#include "app_ble.h"

static const char *TAG = "vi";

struct app_voice_interaction *gp_voice_interaction = NULL;

#define EVENT_RECORD_START        BIT0
#define EVENT_RECORD_STOP         BIT1
#define EVENT_VI_STOP             BIT2
#define EVENT_VI_EXIT             BIT3


extern const uint8_t sound_push2talk_data[6370];

static void __data_lock(struct app_voice_interaction  *p_vi)
{
    xSemaphoreTake(p_vi->sem_handle, portMAX_DELAY);
}
static void __data_unlock(struct app_voice_interaction *p_vi)
{
    xSemaphoreGive(p_vi->sem_handle);  
}

static void __vi_stop( struct app_voice_interaction *p_vi)
{
    xEventGroupSetBits(p_vi->event_group, EVENT_VI_STOP);
    if( (p_vi->is_wait_resp || p_vi->is_connecting || p_vi->is_http_write) &&  p_vi->client != NULL ){
        ESP_LOGI(TAG, " stop, resp:%d, connecting:%d, write:%d", p_vi->is_wait_resp, p_vi->is_connecting, p_vi->is_http_write);
        esp_http_client_cancel_request(p_vi->client);
    }
}

static void __vi_exit( struct app_voice_interaction *p_vi)
{
    xEventGroupSetBits(p_vi->event_group, EVENT_VI_EXIT);
    if( p_vi->cur_status !=  VI_STATUS_IDLE ){
        ESP_LOGI(TAG, "vi not idle, stop it first");
        __vi_stop(p_vi);
    }
}

static void __record_start( struct app_voice_interaction *p_vi)
{
    if(p_vi->is_ota) {
        ESP_LOGW(TAG, "not support vi on ota mode");
        return;
    }
    //Maybe it has already started
    if( p_vi->cur_status !=  VI_STATUS_IDLE ){
        ESP_LOGI(TAG, "vi not idle, stop it first");
        __vi_stop(p_vi);
    }
    if( ! p_vi->is_recording ) {
        xEventGroupSetBits(p_vi->event_group, EVENT_RECORD_START);
    } else {
        ESP_LOGI(TAG, "already record");
    }
}

static void __record_stop( struct app_voice_interaction *p_vi)
{
    if( p_vi->is_connecting &&  p_vi->client != NULL ){
        ESP_LOGI(TAG, " stop connecting"); 
        esp_http_client_cancel_request(p_vi->client);
    }
    if (esp_timer_is_active( p_vi->timer_handle ) == true){
        esp_timer_stop(p_vi->timer_handle);
        ESP_LOGI(TAG, "stop timer");
    }
    ESP_LOGI(TAG, "start timer 5s");
    esp_timer_start_once(p_vi->timer_handle, 5 * 1000000); //5s must  exit record
    
    xEventGroupSetBits(p_vi->event_group, EVENT_RECORD_STOP);
}

static bool __is_need_start_record(struct app_voice_interaction *p_vi, int ms)
{
    return !((xEventGroupWaitBits( p_vi->event_group, EVENT_RECORD_START, pdTRUE, pdFALSE, pdMS_TO_TICKS(ms)) & EVENT_RECORD_START) == 0 );
}

static bool __is_need_stop_record(struct app_voice_interaction *p_vi)
{
    return !((xEventGroupWaitBits( p_vi->event_group, EVENT_RECORD_STOP, pdTRUE, pdFALSE, 0) & EVENT_RECORD_STOP) == 0 );
}

static bool __is_need_stop(struct app_voice_interaction *p_vi)
{
    return !((xEventGroupWaitBits( p_vi->event_group, EVENT_VI_STOP, pdTRUE, pdFALSE, 0) & EVENT_VI_STOP) == 0 );
}
static bool __is_need_exit(struct app_voice_interaction *p_vi)
{
    return !((xEventGroupWaitBits( p_vi->event_group, EVENT_VI_EXIT, pdTRUE, pdFALSE, 0) & EVENT_VI_EXIT) == 0 );
}

static char * __default_token_gen(void)
{
    static char token[41] = {0};
    esp_err_t ret = ESP_OK;
    const char *eui = NULL;
    const char *key = NULL;
    size_t str_len = 0;
    size_t token_len = 0;
    char deviceinfo_buf[40];

    if( strlen(token) > 0 ) {
        return token;
    }

    eui = factory_info_eui_get();
    key = factory_info_ai_key_get();
    if( eui == NULL || key == NULL ) {
        ESP_LOGE(TAG, "EUI or key not set");
        return NULL;
    }

    memset(deviceinfo_buf, 0, sizeof(deviceinfo_buf));
    str_len = snprintf(deviceinfo_buf, sizeof(deviceinfo_buf), "%s:%s", eui, key);
    if( str_len >= 30 ) {
        ESP_LOGE(TAG, "EUI or key too long");
        return NULL;
    }
    ret = mbedtls_base64_encode(( uint8_t *)token, sizeof(token), &token_len, ( uint8_t *)deviceinfo_buf, str_len);
    if( ret != 0  ||  token_len < 40 ) {
        ESP_LOGE(TAG, "mbedtls_base64_encode failed:%d,", ret);
        return NULL;
    }
    return token;
}

static void __url_token_set(struct app_voice_interaction *p_vi)
{
    char *p_token = NULL, *p_host = NULL;

    local_service_cfg_type1_t local_svc_cfg = { .enable = false, .url = NULL };
    esp_err_t ret = get_local_service_cfg_type1(MAX_CALLER, CFG_ITEM_TYPE1_AUDIO_TASK_COMPOSER, &local_svc_cfg);
    if (ret == ESP_OK && local_svc_cfg.enable) {
        if (local_svc_cfg.url != NULL && strlen(local_svc_cfg.url) > 7) {
            ESP_LOGI(TAG, "got local service cfg, url=%s", local_svc_cfg.url);
            int len = strlen(local_svc_cfg.url);
            if (local_svc_cfg.url[len - 1] == '/') local_svc_cfg.url[len - 1] = '\0';  //remove trail '/'
            p_host = local_svc_cfg.url;
        }
        // token
        if (local_svc_cfg.token != NULL && strlen(local_svc_cfg.token) > 0) {
            ESP_LOGI(TAG, "got local service cfg, token=%s", local_svc_cfg.token);
            p_token = local_svc_cfg.token;
        }
        p_vi->use_local_svc = true;
    } else {
        p_vi->use_local_svc = false;
    }

    // host
    if (p_host == NULL) p_host = CONFIG_TALK_SERV_HOST;
    snprintf(p_vi->stream_url, sizeof(p_vi->stream_url), "%s%s", p_host, CONFIG_TALK_AUDIO_STREAM_PATH);
    snprintf(p_vi->taskflow_url, sizeof(p_vi->taskflow_url), "%s%s", p_host, CONFIG_TASKFLOW_DETAIL_PATH);
    
    // token
    if (p_token == NULL) p_token = __default_token_gen();
    if (p_token) {
        if (local_svc_cfg.enable) {
            snprintf(p_vi->token, sizeof(p_vi->token), "%s", p_token);
        } else {
            snprintf(p_vi->token, sizeof(p_vi->token), "Device %s", p_token);
        }
    } else {
        p_vi->token[0] = '\0';
    }
    
    if (local_svc_cfg.url != NULL) {
        free(local_svc_cfg.url);
    }
    if (local_svc_cfg.token != NULL) {
        free(local_svc_cfg.token);
    }
}

static char *__request( const char *url,
                        esp_http_client_method_t method, 
                        const char *token, 
                        const char *session_id,
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
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "session-id", session_id);
    if( token !=NULL && strlen(token) > 0 ) {
        ESP_LOGI(TAG, "token: %s", token);
        esp_http_client_set_header(client, "Authorization", token);
    }
    const char *eui = factory_info_eui_get();
    if( eui ){
        esp_http_client_set_header(client, "API-OBITER-DEVICE-EUI", eui);
    }

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
        ESP_LOGI(TAG, "chunked response");
        esp_http_client_get_chunk_length(client, &content_length);
    }
    ESP_LOGI(TAG, "content_length=%d", content_length);
    ESP_GOTO_ON_FALSE(content_length >= 0, ESP_FAIL, err, TAG, "HTTP client fetch headers failed!");

    result = (char *)psram_malloc(content_length + 1);
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
        ESP_LOGI(TAG, "taskflow: %s", result);
    }
err:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result != NULL ? result : NULL;
}

static int __taskflow_http_get(struct app_voice_interaction *p_vi)
{
    esp_err_t ret = ESP_FAIL;

    char *p_resp = __request(p_vi->taskflow_url, HTTP_METHOD_GET, p_vi->token, p_vi->session_id, NULL, 0);
    if (p_resp == NULL) {
        ESP_LOGE(TAG, "Failed to get taskflow");
        return ESP_FAIL;
    }
    cJSON *json = NULL;
    json = cJSON_Parse(p_resp);
    if (json == NULL) {
        ESP_LOGE(TAG, "Json parse failed");
        free(p_resp);
        return ESP_FAIL;
    }

    cJSON *code = cJSON_GetObjectItem(json, "code");
    if ( code != NULL && cJSON_IsNumber(code) && code->valueint == 200 ) {

        cJSON *json_data = cJSON_GetObjectItem(json, "data");
        if (json_data != NULL && cJSON_IsObject(json_data)) {

            cJSON *json_tl = cJSON_GetObjectItem(json_data, "tl");
            if (json_tl != NULL && cJSON_IsObject(json_tl)) {
                char *tl_str = cJSON_PrintUnformatted(json_tl); // don't free
                if (tl_str != NULL) {
                    esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_TASK_FLOW_START_BY_SR, 
                                                &tl_str,
                                                sizeof(void *), /* ptr size */
                                                pdMS_TO_TICKS(10000));   
                    ret = ESP_OK; //success
                }
            }
        }
    } else {
        if( code != NULL ) {
            ESP_LOGE(TAG, "code: %d", code->valueint);
        }
    }
    free(p_resp);
    cJSON_Delete(json);
    return ret;
}

static int  __stream_data_parse(uint8_t *p_buf, size_t len, const char **json_str, size_t *json_len, uint8_t **bin_data, size_t *bin_len) 
{
    
    static const char *boundary = "---sensecraftboundary---";
    if (p_buf == NULL || len < sizeof(boundary) || json_str == NULL || json_len == NULL || bin_data == NULL || bin_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t boundary_len = strlen(boundary);

    uint8_t *first_boundary = (uint8_t *)memmem(p_buf, len, boundary, boundary_len);
    if (first_boundary == NULL) {
        return ESP_FAIL;
    }

    *json_str = (const char *)p_buf;
    *json_len = first_boundary - p_buf;

    *bin_data = first_boundary + boundary_len + 1; // skip \n
    *bin_len = len - ( *bin_data - p_buf );

    return ESP_OK;
}

static int __audio_stream_result_get(uint8_t *p_buf, size_t len,  struct view_data_vi_result  *p_result, uint8_t **pp_bin_data, size_t *p_bin_len)
{
    esp_err_t ret = ESP_OK;
    const char *json_str = NULL;
    size_t json_len = 0;
    ret = __stream_data_parse(p_buf, len, &json_str, &json_len, pp_bin_data, p_bin_len);
    if (ret != ESP_OK) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "audio stream result(%d):\r\n%.*s\r\n", json_len, json_len, json_str);
    ret = app_vi_result_parse(json_str, json_len, p_result);
    return ret;
}

static int __audio_stream_http_connect(struct app_voice_interaction *p_vi)
{
    esp_err_t  ret = ESP_OK;
    ESP_LOGI(TAG, "URL: %s", p_vi->stream_url);
    esp_http_client_config_t config = {
        .url = p_vi->stream_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    p_vi->need_delete_client = true;
    p_vi->client = esp_http_client_init(&config);

    esp_http_client_set_header(p_vi->client, "Transfer-Encoding", "chunked");
    esp_http_client_set_header(p_vi->client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(p_vi->client, "session-id", p_vi->session_id);
    ESP_LOGI(TAG, "session-id:%s", p_vi->session_id);
    const char *eui = factory_info_eui_get();
    if( eui ){
        esp_http_client_set_header(p_vi->client, "API-OBITER-DEVICE-EUI", eui);
    }

    char *token =  p_vi->token;
    if( token !=NULL && strlen(token) > 0 ) {
        ESP_LOGI(TAG, "token: %s", token);
        esp_http_client_set_header(p_vi->client, "Authorization", token);
    }
    // when len=-1, will use transfer-encoding: chunked
    return esp_http_client_open(p_vi->client, -1);
}


// The conversation process is as follows:
// 1. Long press to wake up
// 2. Wake-up preprocessing (generate session ID, RGB blue breathing, play wake-up sound effect, pause task)
// 3. Start recording, establish http connection, send recording data, wait for recording to end
// 4. Wait for analysis results
// 5. Extract task results, read audio, play audio,
// 6. Single conversation ends, close connection
// 7. Exit (resume task, clear session ID)
static void __status_machine_handle(struct app_voice_interaction *p_vi)
{
    esp_err_t  ret = ESP_OK;
    switch (p_vi->cur_status) {
        case VI_STATUS_IDLE:{
            if(__is_need_exit(p_vi)) {
                p_vi->next_status = VI_STATUS_EXIT;
                break;
            }
            if(__is_need_start_record(p_vi, 20)) {
                // maybe have cached
                xEventGroupClearBits(p_vi->event_group, EVENT_RECORD_STOP | EVENT_VI_STOP | EVENT_VI_EXIT);
                p_vi->next_status = VI_STATUS_WAKE_START;
            } else {
                p_vi->next_status = VI_STATUS_IDLE;
            }
            break;
        }
        case VI_STATUS_WAKE_START: {
            ESP_LOGI(TAG, "VI_STATUS_WAKE_START");
            if( !p_vi->net_flag ) {
                ESP_LOGI(TAG, "network not ready");
                p_vi->err_code = ESP_ERR_VI_NET_CONNECT;
                p_vi->next_status = VI_STATUS_ERROR;
                break;
            }

            if( p_vi->new_session || p_vi->session_id[0] == 0 ) {
                UUIDGen( p_vi->session_id );
                ESP_LOGI(TAG, "session-id:%s", p_vi->session_id);
            }
            
            if( p_vi->new_session ) {
                p_vi->new_session = false;
                
                //check ble
                app_ble_adv_pause(); //Don't care whether the current ble state is open

                // check taskflow
                int engine_status = 0;
                tf_engine_status_get( &engine_status);
                if( (engine_status == TF_STATUS_RUNNING) || (engine_status == TF_STATUS_STARTING) ) {
                    p_vi->taskflow_pause =  true;
                    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                        VIEW_EVENT_VI_TASKFLOW_PAUSE, NULL, NULL, pdMS_TO_TICKS(10000));

                    tf_engine_pause_block(pdMS_TO_TICKS(15000)); // maybe take 17s

                    ESP_LOGI(TAG, "taskflow pause 1");
                } else {
                    p_vi->taskflow_pause =  false;
                }

            } else {
                int engine_status = 0;
                tf_engine_status_get( &engine_status);
                if( (engine_status == TF_STATUS_RUNNING) ) {
                    p_vi->taskflow_pause =  true;
                    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                        VIEW_EVENT_VI_TASKFLOW_PAUSE, NULL, NULL, pdMS_TO_TICKS(10000));
                    tf_engine_pause_block(pdMS_TO_TICKS(15000)); // maybe take 17s
                    ESP_LOGI(TAG, "taskflow pause 2");
                }
            }
            
            p_vi->next_status = VI_STATUS_RECORDING;

            break;
        }
        case VI_STATUS_RECORDING: {
            ESP_LOGI(TAG, "VI_STATUS_RECORDING");
            enum app_voice_interaction_status next_status = VI_STATUS_ANALYZING;
            int64_t start = 0, end = 0;
            uint8_t *p_data = NULL;
            size_t data_len = 0;
            bool first = true;
            char chunk_size_str[32+3];
            size_t chunk_size_str_len = 0;
            int  write_len = 0;
            size_t send_len = 0;
            int64_t net_send_tm_us = 0;
            esp_http_client_handle_t client;
            bool stop_send = false;
            
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                                    VIEW_EVENT_VI_RECORDING, NULL, NULL, pdMS_TO_TICKS(10000));

            app_rgb_set(SR, RGB_BREATH_BLUE); //set RGB
            app_audio_player_mem_block(sound_push2talk_data, sizeof(sound_push2talk_data), false, pdMS_TO_TICKS(570)); //file 370ms, take 562ms.

            app_audio_recorder_stream_start();
            p_vi->is_connecting = true;
            ret = __audio_stream_http_connect(p_vi);
            p_vi->is_connecting = false;
            if (ret != ESP_OK) {
                app_audio_recorder_stream_stop();
                ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(ret));
                p_vi->next_status = VI_STATUS_ERROR;
                p_vi->err_code = ESP_ERR_VI_HTTP_CONNECT;
                break;
            } else {
                ESP_LOGI(TAG, "http connect success");
                client = p_vi->client;
            }
            
            esp_http_client_set_timeout_ms(client, 5000);
            start = esp_timer_get_time();
            p_vi->is_recording = true;
            while(1) {
                p_data = app_audio_recorder_stream_recv(&data_len, pdMS_TO_TICKS(500));
                if( p_data != NULL ) {
                    if( first ) {
                        first = false;
                        chunk_size_str_len = snprintf(chunk_size_str,sizeof(chunk_size_str), "%X\r\n", data_len);
                    } else {
                        chunk_size_str_len = snprintf(chunk_size_str,sizeof(chunk_size_str), "\r\n%X\r\n", data_len);
                    }
                    int64_t chunk_send_start= esp_timer_get_time();
                    
                    write_len = esp_http_client_write(client, (const char *)chunk_size_str, chunk_size_str_len);
                    if( write_len <= 0) {
                        ESP_LOGE(TAG, "esp_http_client_write failed");
                        app_audio_recorder_stream_free((uint8_t *)p_data);
                        break;
                    }
                    send_len += write_len;
                    write_len = esp_http_client_write(client, (const char *)p_data, data_len);
                    if( write_len <= 0) {
                        ESP_LOGE(TAG, "esp_http_client_write failed");
                        app_audio_recorder_stream_free((uint8_t *)p_data);
                        break;
                    }
                    send_len += write_len;

                    int64_t chunk_send_end= esp_timer_get_time();
                    app_audio_recorder_stream_free((uint8_t *)p_data);
                    net_send_tm_us += (chunk_send_end - chunk_send_start);
                    ESP_LOGI(TAG, "send:%d", data_len);
                } else {
                    ESP_LOGI(TAG, "no data, wait for 10ms");
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
                if(__is_need_stop_record(p_vi)) {
                    app_audio_recorder_stream_stop();
                    ESP_LOGI(TAG, "EVENT_RECORD_STOP");
                    break;
                }
            }
            p_vi->is_recording = false;
            if (esp_timer_is_active( p_vi->timer_handle ) == true){
                ESP_LOGI(TAG, "stop timer");
                esp_timer_stop(p_vi->timer_handle);
            }

            if( write_len <= 0) {
                p_vi->next_status = VI_STATUS_ERROR;
                p_vi->err_code = ESP_ERR_VI_HTTP_WRITE;
                app_audio_recorder_stream_stop();
                ESP_LOGI(TAG, "EVENT_RECORD_STOP");
                break;
            }

            app_rgb_set(SR, RGB_OFF);

            // The audio may not be sent completely, but the UI needs to show that it is being analyzed.
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                        VIEW_EVENT_VI_ANALYZING, NULL, NULL, pdMS_TO_TICKS(10000));

            // continue to send data from ringbuffer
            p_vi->is_http_write = true;
            while(1) {
                p_data = app_audio_recorder_stream_recv(&data_len, pdMS_TO_TICKS(500));
                if( p_data != NULL ) {
                    chunk_size_str_len = snprintf(chunk_size_str,sizeof(chunk_size_str), "\r\n%X\r\n", data_len);
                    int64_t chunk_send_start= esp_timer_get_time();

                    write_len = esp_http_client_write(client, (const char *)chunk_size_str, chunk_size_str_len);
                    if( write_len <= 0) {
                        ESP_LOGE(TAG, "esp_http_client_write failed");
                        app_audio_recorder_stream_free((uint8_t *)p_data);
                        break;
                    }
                    send_len += write_len;
                    write_len = esp_http_client_write(client, (const char *)p_data, data_len);
                    if( write_len <= 0) {
                        ESP_LOGE(TAG, "esp_http_client_write failed");
                        app_audio_recorder_stream_free((uint8_t *)p_data);
                        break;
                    }
                    send_len += write_len;

                    int64_t chunk_send_end= esp_timer_get_time();
                    app_audio_recorder_stream_free((uint8_t *)p_data);
                    net_send_tm_us += (chunk_send_end - chunk_send_start);
                    ESP_LOGI(TAG, "send:%d", data_len);
                } else {
                    ESP_LOGI(TAG, "send finish");
                    break;
                }
                if(__is_need_stop(p_vi)) {
                    ESP_LOGI(TAG, "stop send");
                    next_status = VI_STATUS_STOP;
                    stop_send = true;
                    break;
                }
            }
            p_vi->is_http_write = false;

            if( write_len <= 0) {
                p_vi->next_status = VI_STATUS_ERROR;
                p_vi->err_code = ESP_ERR_VI_HTTP_WRITE;
                break;
            }

            if( stop_send ) {
                p_vi->next_status = VI_STATUS_STOP; 
                break;
            }

            write_len = esp_http_client_write(client, "\r\n0\r\n\r\n", strlen("\r\n0\r\n\r\n"));
            if( write_len <= 0) {
                ESP_LOGE(TAG, "esp_http_client_write failed");
                p_vi->next_status = VI_STATUS_ERROR;
                p_vi->err_code = ESP_ERR_VI_HTTP_WRITE;
                break;
            }
            send_len += write_len;
            end = esp_timer_get_time();
            ESP_LOGI(TAG, " === Record stop === ");
            ESP_LOGI(TAG, "send:%d, time:%lld ms, Net rate:%.1fKB/s, average rate=%.1fKB/s, ", send_len,  (end - start) / 1000, \
                        (send_len * 976.5625) / net_send_tm_us,  (send_len * 976.5625) / (end - start));

            p_vi->next_status = next_status;
            break;
        }

        case VI_STATUS_ANALYZING: {
            ESP_LOGI(TAG, "VI_STATUS_ANALYZING");
            enum app_voice_interaction_status next_status = VI_STATUS_PLAYING;
            int64_t start = 0, end = 0;
            esp_http_client_handle_t client = p_vi->client;
            int code = 0;

            if( p_vi->use_local_svc ) {
                esp_http_client_set_timeout_ms(client, 60000 * 2);
            } else {
                esp_http_client_set_timeout_ms(client, 30000);
            }
            
            p_vi->is_wait_resp = true;
            start = esp_timer_get_time();
            int content_length = esp_http_client_fetch_headers(client);
            if (esp_http_client_is_chunked_response(client))
            {
                ESP_LOGI(TAG, "chunk data");
                esp_http_client_get_chunk_length(client, &content_length);
            }
            end = esp_timer_get_time();
            code = esp_http_client_get_status_code(client);
            p_vi->is_wait_resp = false;

            ESP_LOGI(TAG, "code=%d, content_length=%d, time=%lld ms", code, content_length, (end - start) / 1000);
            
            if( __is_need_stop(p_vi)) {
                ESP_LOGI(TAG, "STOP analy");
                next_status = VI_STATUS_STOP;
            } else if( content_length >= 0  &&  code == 200 ) {
                p_vi->content_length = content_length;
                next_status = VI_STATUS_PLAYING;
            } else {
                ESP_LOGE(TAG, "response error");
                next_status = VI_STATUS_ERROR;
                p_vi->err_code = ESP_ERR_VI_HTTP_RESP;
            }
            p_vi->next_status = next_status;
            break;
        }

        case VI_STATUS_PLAYING: {
            ESP_LOGI(TAG, "VI_STATUS_PLAYING");

            enum app_voice_interaction_status next_status = VI_STATUS_FINISH;
            int64_t start = 0, end = 0;
            esp_http_client_handle_t client = p_vi->client;
            int read_len = 0;
            size_t read_total_len = 0;
            int64_t net_read_tm_us = 0;
            size_t chunk_len = AUDIO_PLAYER_RINGBUF_CHUNK_SIZE * 2; // TODO
            int content_length = p_vi->content_length;
            bool  first_start = true;
            int   cache_len =  MIN( content_length/2, AUDIO_PLAYER_RINGBUF_CACHE_SIZE);
            struct view_data_vi_result  result;
            int player_status = 0;
            size_t bin_offset  = 0;
            bool first_chunk = true;
            uint8_t *p_bin_data = NULL;
            size_t bin_len = 0;
            int play_chunk_time_ms = 0;

            memset(&result, 0, sizeof(result));

            char* recv_buf = (char *)psram_malloc(chunk_len);
            if (recv_buf == NULL) {
                ESP_LOGE(TAG, "psram_malloc failed");
                next_status = VI_STATUS_ERROR;
                p_vi->err_code = ESP_ERR_VI_NO_MEM;
                p_vi->next_status = next_status;
                break;
            }   

            esp_http_client_set_timeout_ms(client, 10000);
            start = esp_timer_get_time();
            app_audio_player_stream_init(content_length);
            while (read_total_len < content_length) {
                int64_t read_start= esp_timer_get_time();
                read_len = esp_http_client_read(client, recv_buf, chunk_len);
                int64_t read_end= esp_timer_get_time();
                net_read_tm_us = (read_end - read_start) + net_read_tm_us;

                if (read_len <= 0) {
                    ESP_LOGI(TAG, "recv:%d", read_len);
                    break;
                } else {
                    ESP_LOGI(TAG, "recv:%d", read_len);
                    read_total_len += read_len;

                    if(first_chunk) {
                        
                        if( read_len < 1024 ) {
                            ESP_LOGI(TAG, "%s", recv_buf); // debug
                        }
                        first_chunk = false; //TODO  maybe  first_chunk can't find result.
                        ret = __audio_stream_result_get((uint8_t *)recv_buf, read_len, &result, &p_bin_data, &bin_len);
                        if(  ret == ESP_OK  && bin_len != 0) {
                            ESP_LOGI(TAG, "audio len:%d", bin_len);
                            app_audio_player_stream_send((uint8_t *)p_bin_data, bin_len, pdMS_TO_TICKS(500));
                        } else {
                            app_audio_player_stream_send((uint8_t *)recv_buf, read_len, pdMS_TO_TICKS(500));
                        }
                        ESP_LOGI(TAG, "stream time: %dms", app_audio_player_stream_time_get(content_length -(read_len-bin_len)));
                        play_chunk_time_ms = app_audio_player_stream_time_get(chunk_len) + 200;
                        ESP_LOGI(TAG, "play_chunk_time_ms:%d", play_chunk_time_ms);
                    } else {        
                       app_audio_player_stream_send((uint8_t *)recv_buf, read_len, pdMS_TO_TICKS(play_chunk_time_ms)); 
                    }
                }
                if(__is_need_stop(p_vi)) {
                    ESP_LOGI(TAG, "stop play");
                    app_audio_player_stream_stop();
                    next_status = VI_STATUS_STOP;
                    break;
                }
                if( read_total_len >= cache_len  && first_start) {
                    first_start = false;
                    ESP_LOGI(TAG, "start play");
                    ESP_LOGI(TAG, "mode:%d", result.mode);
                    ESP_LOGI(TAG, "sst_text:%s", result.p_sst_text ? result.p_sst_text : "UNKNOWN");
                    ESP_LOGI(TAG, "audio_text:%s", result.p_audio_text ? result.p_audio_text : "UNKNOWN");
                    // printf("audio_text:%s\r\n", result.p_audio_text ? result.p_audio_text : "UNKNOWN"); // for test
                    ESP_LOGI(TAG, "audio_tm_ms:%d", result.audio_tm_ms);
                    for (size_t i = 0; i < TASK_CFG_ID_MAX; i++)
                    {
                        ESP_LOGI(TAG, "items:%d: %s", i, result.items[i] ? result.items[i] : "UNKNOWN");
                    }   
                    
                    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                            VIEW_EVENT_VI_PLAYING, &result,  sizeof(result), pdMS_TO_TICKS(10000)); //listener  need free
                    app_audio_player_stream_start();
                    app_rgb_set(SR, RGB_FLARE_BLUE);
                }
            }
            free(recv_buf);
            app_audio_player_stream_finish();
            end = esp_timer_get_time();

            ESP_LOGI(TAG, " === Download end === ");
            ESP_LOGI(TAG, "recv:%d, time:%lld ms, Net rate:%.1fKB/s, average rate=%.1fKB/s, ", read_total_len,  (end - start) / 1000, \
                        (read_total_len * 976.5625) / net_read_tm_us,  (read_total_len * 976.5625) / (end - start));
            
            // wait play finish
            while(1) {
                player_status = app_audio_player_status_get();
                if( player_status == AUDIO_PLAYER_STATUS_PLAYING_STREAM ) {
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                } else {
                    break;
                }
                if(__is_need_stop(p_vi)) {
                    ESP_LOGI(TAG, "stop play");
                    app_audio_player_stream_stop();
                    next_status = VI_STATUS_STOP;
                    break;
                }
            }

            p_vi->next_status = next_status;
            break;
        }
        case VI_STATUS_FINISH:
        {
            ESP_LOGI(TAG, "VI_STATUS_FINISH");
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                        VIEW_EVENT_VI_PLAY_FINISH, NULL, NULL, pdMS_TO_TICKS(10000));
            
            app_rgb_set(SR, RGB_OFF);
            // No need for break
        }
        case VI_STATUS_STOP: 
        {
            ESP_LOGI(TAG, "VI_STATUS_STOP");
            esp_http_client_handle_t client = p_vi->client;

            // No need to turn off RGB, UI will set blue flashing.

            if( p_vi->need_delete_client && client != NULL) {
                p_vi->need_delete_client = false;
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                p_vi->client = NULL;
            }
            p_vi->next_status = VI_STATUS_IDLE;
            break;
        }
        case VI_STATUS_PRE_EXIT: {
            ESP_LOGI(TAG, "VI_STATUS_PRE_EXIT");
            //reserve
            p_vi->next_status = VI_STATUS_IDLE;
            break;
        }
        case VI_STATUS_EXIT: {
            ESP_LOGI(TAG, "VI_STATUS_EXIT");
            app_rgb_set(SR, RGB_OFF);
            p_vi->new_session = true;
            
            //resume ble
            app_ble_adv_resume(get_ble_switch(MAX_CALLER));

            // resume taskflow
            if( p_vi->need_get_taskflow ) {
                //Do not resume the last task, and run the new task directly
                p_vi->need_get_taskflow = false;
                p_vi->next_status = VI_STATUS_TASKFLOW_GET;
            } else {
                if( p_vi->taskflow_pause) {
                    ESP_LOGI(TAG, "resume taskflow");
                    tf_engine_resume();
                }
                p_vi->taskflow_pause = false;
                p_vi->next_status = VI_STATUS_IDLE;
            }
            
            break;
        }
        case VI_STATUS_ERROR: {
            ESP_LOGI(TAG, "VI_STATUS_ERROR");
            app_rgb_set(SR, RGB_OFF);
            esp_http_client_handle_t client = p_vi->client;
            if( p_vi->need_delete_client && client != NULL) {
                p_vi->need_delete_client = false;
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                p_vi->client = NULL;
            }
            ESP_LOGE(TAG, "err_code:0x%X", p_vi->err_code);

            if(__is_need_exit(p_vi)) {
                p_vi->next_status = VI_STATUS_EXIT;
                break;
            } else {
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                            VIEW_EVENT_VI_ERROR, &p_vi->err_code, sizeof(p_vi->err_code), pdMS_TO_TICKS(10000));
                p_vi->next_status = VI_STATUS_IDLE;
            }
            break;
        }
        case VI_STATUS_TASKFLOW_GET:{
            ESP_LOGI(TAG, "VI_STATUS_TASKFLOW_GET");
            ret = __taskflow_http_get(p_vi);
            if( ret != ESP_OK ) {
                ESP_LOGE(TAG, "taskflow get failed");
                char err_msg[64];
                snprintf(err_msg, sizeof(err_msg) - 1, "Failed to download");
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                                VIEW_EVENT_TASK_FLOW_ERROR, err_msg, sizeof(err_msg), portMAX_DELAY);
                //Don't resume the last task
            }
            p_vi->next_status = VI_STATUS_IDLE;
            break;
        }
        default: {
            ESP_LOGW(TAG, "unknown status:%d", p_vi->cur_status);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            p_vi->next_status = VI_STATUS_IDLE;
            break; 
        }
    }

    if( p_vi->next_status != p_vi->cur_status ) {
        p_vi->cur_status = p_vi->next_status;
    }
}

static void app_voice_interaction_task(void *p_arg)
{
    struct app_voice_interaction *p_vi = (struct app_voice_interaction *)p_arg;
    while(1) {
        __status_machine_handle(p_vi);
    }
}

/*************************************************************************
 * callback or event handle
 ************************************************************************/

static void __long_press_event_cb(void)
{
    ESP_LOGI(TAG, "long_press_event_cb");
    struct app_voice_interaction *p_vi = (struct app_voice_interaction *)gp_voice_interaction;
    __record_start(p_vi);
}

static void __long_release_event_cb(void)
{
    ESP_LOGI(TAG, "long_release_event_cb");
    struct app_voice_interaction *p_vi = (struct app_voice_interaction *)gp_voice_interaction;
    __record_stop(p_vi);
}

static void __event_handler(void* handler_args, 
                                 esp_event_base_t base, 
                                 int32_t id, 
                                 void* event_data)
{
    struct app_voice_interaction *p_vi = (struct app_voice_interaction *)handler_args;

     if( base == VIEW_EVENT_BASE) {
        switch (id) {
            case VIEW_EVENT_WIFI_ST:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
                struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
                if (p_st->is_network) {
                    p_vi->net_flag = true;
                } else {
                    p_vi->net_flag = false;
                }
                break;
            }
            case VIEW_EVENT_VI_STOP:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_STOP");
                __vi_stop(p_vi);
                break;
            }
            case VIEW_EVENT_VI_PRE_EXIT:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_PRE_EXIT");
                break;
            }
            case VIEW_EVENT_VI_EXIT:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_EXIT");
                int exit_mode = *(int *)event_data;
                if( exit_mode ) {
                    ESP_LOGI(TAG, "need get taskflow");
                    p_vi->need_get_taskflow = true;
                } else {
                    p_vi->need_get_taskflow = false;
                }
                __vi_exit(p_vi);
                break;
            }
            case VIEW_EVENT_OTA_STATUS:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_OTA_STATUS");
                struct view_data_ota_status * p_ota_st = (struct view_data_ota_status *)event_data;
                if( SENSECRAFT_OTA_STATUS_UPGRADING  == p_ota_st->status) {
                    p_vi->is_ota = true;
                } else {
                    p_vi->is_ota = false;
                }
                break;
            }
        default:
            break;
        }

    } else if( base == CTRL_EVENT_BASE ) {
        switch (id)
        {
            case CTRL_EVENT_LOCAL_SVC_CFG_PUSH2TALK:
            {
                ESP_LOGI(TAG, "event: CTRL_EVENT_LOCAL_SVC_CFG_PUSH2TALK");
                __url_token_set(p_vi);
                break;
            }
            case CTRL_EVENT_VI_RECORD_WAKEUP:
            {   
                ESP_LOGI(TAG, "event: CTRL_EVENT_VI_RECORD_WAKEUP");
                __record_start(p_vi);
                break;
            }
            case CTRL_EVENT_VI_RECORD_STOP:
            {
                ESP_LOGI(TAG, "event: CTRL_EVENT_VI_RECORD_STOP");
                __record_stop(p_vi);
                break;
            }
            case CTRL_EVENT_OTA_AI_MODEL:
            {
                ESP_LOGI(TAG, "event: CTRL_EVENT_OTA_AI_MODEL");
                struct view_data_ota_status * p_ota_st = (struct view_data_ota_status *)event_data;
                if( OTA_STATUS_DOWNLOADING  == p_ota_st->status) {
                    p_vi->is_ota = true;
                } else {
                    p_vi->is_ota = false;
                }
                break;
            }
            default:
                break;
        }
    }
}

static void __timer_callback(void* p_arg)
{
    struct app_voice_interaction *p_vi = (struct app_voice_interaction *)p_arg;
    ESP_LOGI(TAG, "record end timeout");
    if( p_vi->is_recording  && p_vi->client ) {
        ESP_LOGI(TAG, "timer cancel request");
        esp_http_client_cancel_request(p_vi->client);
    }
}

/*************************************************************************
 * API
 ************************************************************************/

esp_err_t app_voice_interaction_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    esp_err_t ret = ESP_OK;
    struct app_voice_interaction * p_vi = NULL;
    gp_voice_interaction = (struct app_voice_interaction *) psram_malloc(sizeof(struct app_voice_interaction));
    if (gp_voice_interaction == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    p_vi = gp_voice_interaction;
    memset(p_vi, 0, sizeof( struct app_voice_interaction ));
    
    __url_token_set(p_vi); 
    p_vi->cur_status = VI_STATUS_IDLE;
    p_vi->next_status = VI_STATUS_IDLE;
    p_vi->need_delete_client = false;
    p_vi->content_length = 0;
    p_vi->err_code = 0;
    p_vi->is_wait_resp = false;
    p_vi->is_connecting = false;
    p_vi->is_recording = false;
    p_vi->need_get_taskflow = false;
    p_vi->taskflow_pause = false;
    p_vi->new_session = true;
    p_vi->is_ota = false;

    p_vi->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_vi->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_vi->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != p_vi->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");

    p_vi->p_task_stack_buf = (StackType_t *)psram_malloc(VOICE_INTERACTION_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_vi->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_vi->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_vi->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_vi->task_handle = xTaskCreateStaticPinnedToCore(app_voice_interaction_task,
                                                                "app_voice_interaction",
                                                                VOICE_INTERACTION_TASK_STACK_SIZE,
                                                                (void *)p_vi,
                                                                VOICE_INTERACTION_TASK_PRIO,
                                                                p_vi->p_task_stack_buf,
                                                                p_vi->p_task_buf,
                                                                VOICE_INTERACTION_TASK_CORE);
    ESP_GOTO_ON_FALSE(p_vi->task_handle, ESP_FAIL, err, TAG, "Failed to create task");

    const esp_timer_create_args_t timer_args = {
            .callback = &__timer_callback,
            .arg = (void*) p_vi,
            .name = "sensecraft_timer"
    };
    ret = esp_timer_create(&timer_args, &p_vi->timer_handle);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "esp_timer_create failed");

    // view event
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_WIFI_ST, 
                                                    __event_handler, 
                                                    p_vi));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_VI_STOP, 
                                                    __event_handler, 
                                                    p_vi));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_VI_PRE_EXIT, 
                                                    __event_handler, 
                                                    p_vi));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_VI_EXIT, 
                                                    __event_handler, 
                                                    p_vi));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_OTA_STATUS, 
                                                    __event_handler, 
                                                    p_vi));
    // ctrl event
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    CTRL_EVENT_BASE, 
                                                    CTRL_EVENT_LOCAL_SVC_CFG_PUSH2TALK, 
                                                    __event_handler,
                                                    p_vi));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    CTRL_EVENT_BASE, 
                                                    CTRL_EVENT_VI_RECORD_WAKEUP, 
                                                    __event_handler,
                                                    p_vi));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    CTRL_EVENT_BASE, 
                                                    CTRL_EVENT_VI_RECORD_STOP, 
                                                    __event_handler,
                                                    p_vi));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    CTRL_EVENT_BASE, 
                                                    CTRL_EVENT_OTA_AI_MODEL, 
                                                    __event_handler, 
                                                    p_vi));

    //maybe miss ota status event 
    p_vi->is_ota = app_ota_is_running();

    bsp_set_btn_long_press_cb(__long_press_event_cb);
    bsp_set_btn_long_release_cb(__long_release_event_cb);

    return ESP_OK;

err:
    if(p_vi->task_handle ) {
        vTaskDelete(p_vi->task_handle);
        p_vi->task_handle = NULL;
    }
    if( p_vi->p_task_stack_buf ) {
        free(p_vi->p_task_stack_buf);
        p_vi->p_task_stack_buf = NULL;
    }
    if( p_vi->p_task_buf ) {   
        free(p_vi->p_task_buf);
        p_vi->p_task_buf = NULL;
    }
    if (p_vi->event_group) {
        vEventGroupDelete(p_vi->event_group);
        p_vi->event_group = NULL;
    }
    if (p_vi->sem_handle) {
        vSemaphoreDelete(p_vi->sem_handle);
        p_vi->sem_handle = NULL;
    }
    if (p_vi) {
        free(p_vi);
        gp_voice_interaction = NULL;
    }
    ESP_LOGE(TAG, "app_voice_interaction_init fail %d!", ret);
    return ret;
}

bool app_vi_session_is_running(void)
{
    struct app_voice_interaction * p_vi = gp_voice_interaction;
    if( p_vi == NULL) {
        return false;
    }
    return !p_vi->new_session;
}

int app_vi_result_parse(const char *p_str, size_t len,
                        struct view_data_vi_result *p_ret)
{
    esp_err_t ret = ESP_OK;
    cJSON *p_json_root = NULL;
    cJSON *p_code = NULL;
    cJSON *p_data = NULL;

    memset(p_ret, 0, sizeof(struct view_data_vi_result));

    p_json_root = cJSON_ParseWithLength(p_str, len);
    ESP_GOTO_ON_FALSE(p_json_root, ESP_ERR_INVALID_ARG, err, TAG, "json parse failed");

    p_code = cJSON_GetObjectItem(p_json_root, "code");
    if( p_code == NULL || !cJSON_IsNumber(p_code)) {
        ESP_LOGE(TAG, "code is not number or not find");
        goto err;
    } else {
        ESP_LOGI(TAG, "code:%d", p_code->valueint);
    }

    p_data = cJSON_GetObjectItem(p_json_root, "data");
    if( p_data == NULL || !cJSON_IsObject(p_data)) {
        ESP_LOGE(TAG, "data is not object");
        goto err;
    }

    cJSON *p_mode = cJSON_GetObjectItem(p_data, "mode");
    if ( p_mode && cJSON_IsNumber(p_mode)) {
        p_ret->mode = p_mode->valueint;
    } else {
        p_ret->mode = VI_MODE_CHAT;
    }

    cJSON *p_duration = cJSON_GetObjectItem(p_data, "duration");
    if ( p_duration && cJSON_IsNumber(p_duration)) {
        p_ret->audio_tm_ms = p_duration->valueint;
    } else {
        p_ret->audio_tm_ms = 0;
    }

    cJSON *p_stt_result= cJSON_GetObjectItem(p_data, "stt_result");
    if ( p_stt_result && cJSON_IsString(p_stt_result)) {
        p_ret->p_sst_text = strdup(p_stt_result->valuestring);
    }

    cJSON *p_screen_text = cJSON_GetObjectItem(p_data, "screen_text");
    if ( p_screen_text && cJSON_IsString(p_screen_text)) {
        p_ret->p_audio_text = strdup(p_screen_text->valuestring);
    }

    cJSON *p_task_summary = cJSON_GetObjectItem(p_data, "task_summary");
    if ( p_task_summary && cJSON_IsObject(p_task_summary)) {

        cJSON *p_object = cJSON_GetObjectItem(p_task_summary, "object");
        if ( p_object && cJSON_IsString(p_object)) {
            p_ret->items[TASK_CFG_ID_OBJECT] = strdup(p_object->valuestring);
        }

        cJSON *p_condition = cJSON_GetObjectItem(p_task_summary, "condition");
        if ( p_condition && cJSON_IsString(p_condition)) {
            p_ret->items[TASK_CFG_ID_CONDITION] = strdup(p_condition->valuestring);
        }

        cJSON *p_behavior = cJSON_GetObjectItem(p_task_summary, "behavior");
        if ( p_behavior && cJSON_IsString(p_behavior)) {
            p_ret->items[TASK_CFG_ID_BEHAVIOR] = strdup(p_behavior->valuestring);
        }

        cJSON *p_feature = cJSON_GetObjectItem(p_task_summary, "feature");
        if ( p_feature && cJSON_IsString(p_feature)) {
            p_ret->items[TASK_CFG_ID_FEATURE] = strdup(p_feature->valuestring);
        }
        
        cJSON *p_time = cJSON_GetObjectItem(p_task_summary, "time");
        if ( p_time && cJSON_IsString(p_time)) {
            p_ret->items[TASK_CFG_ID_TIME] = strdup(p_time->valuestring);
        }

        cJSON *p_frequency = cJSON_GetObjectItem(p_task_summary, "frequency");
        if ( p_frequency && cJSON_IsString(p_frequency)) {
            p_ret->items[TASK_CFG_ID_FREQUENCY] = strdup(p_frequency->valuestring);
        }

        // "notification": ["app push message","rgb"]
        cJSON *p_notification = cJSON_GetObjectItem(p_task_summary, "notification");
        if ( p_notification && cJSON_IsArray(p_notification)) {
            
            size_t notification_msg_len = 1024 * 2;
            char *result = psram_malloc(notification_msg_len);
            if (result == NULL) {
                ESP_LOGE(TAG, "malloc failed");
                goto err;
            }
            memset(result, 0, notification_msg_len);

            size_t length = cJSON_GetArraySize(p_notification);
            for (size_t i = 0; i < length; i++) {
                cJSON *item = cJSON_GetArrayItem(p_notification, i);
                size_t item_len = strlen(item->valuestring);

                if(result[0] != 0 && ((strlen(result) + item_len) + 2 > notification_msg_len)) {
                    ESP_LOGE(TAG, "notification msg too long");
                    break;
                }
                strcat(result, item->valuestring);
                if (i < length - 1) {
                    strcat(result, "\n");
                }
            }
            p_ret->items[TASK_CFG_ID_NOTIFICATION] = result;
        }


    }

err:
    if (p_json_root != NULL) {
        cJSON_Delete(p_json_root);
    }
    return ESP_OK;
}

int app_vi_result_free(struct view_data_vi_result *p_ret)
{
    if (p_ret->p_audio_text) {
        free(p_ret->p_audio_text);
        p_ret->p_audio_text = NULL;
    }
    
    if (p_ret->p_sst_text) {
        free(p_ret->p_sst_text);
        p_ret->p_sst_text = NULL;
    }

    for (int i = 0; i < TASK_CFG_ID_MAX; i++) {
        if (p_ret->items[i]) {
            free(p_ret->items[i]);
            p_ret->items[i] = NULL;
        }
    }
    return 0;

}