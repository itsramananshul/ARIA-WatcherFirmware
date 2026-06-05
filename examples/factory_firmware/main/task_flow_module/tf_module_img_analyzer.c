#include "tf_module_img_analyzer.h"
#include "tf_module_util.h"
#include <string.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"
#include "esp_check.h"
#include <mbedtls/base64.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "data_defs.h"
#include "event_loops.h"
#include "esp_event_base.h"
#include "factory_info.h"
#include "app_device_info.h"

static const char *TAG = "tfm.img_analyzer";

#define EVENT_STOP          BIT0
#define EVENT_STOP_DONE     BIT1 
#define EVENT_NEED_DELETE   BIT2
#define EVENT_TASK_DELETED  BIT3 

// #define IMG_ANALYZER_NET_CHECK_ENABLE 

#ifdef IMG_ANALYZER_NET_CHECK_ENABLE
static bool g_net_flag = false;
#endif
static void __data_lock( tf_module_img_analyzer_t *p_module)
{
    xSemaphoreTake(p_module->sem_handle, portMAX_DELAY);
}
static void __data_unlock( tf_module_img_analyzer_t *p_module)
{
    xSemaphoreGive(p_module->sem_handle);  
}
static void __parmas_printf(struct tf_module_img_analyzer_params *p_params)
{
    ESP_LOGD(TAG, "type:%d", p_params->type);
    if( p_params->p_prompt != NULL ){
        ESP_LOGD(TAG, "Prompt:%s", p_params->p_prompt);
    }
    if ( p_params->p_audio_txt != NULL ){
        ESP_LOGD(TAG, "Audio txt:%s", p_params->p_audio_txt);
    }
}
static void __parmas_default(struct tf_module_img_analyzer_params *p_params)
{
    p_params->p_prompt = NULL;
    p_params->p_audio_txt = NULL;
    p_params->type = TF_MODULE_IMG_ANALYZER_TYPE_MONITORING;
}
static int __params_parse(struct tf_module_img_analyzer_params *p_params, cJSON *p_json)
{
    cJSON *json_body = cJSON_GetObjectItem(p_json, "body");
    if (json_body != NULL && cJSON_IsObject(json_body)) {

        cJSON *json_prompt = cJSON_GetObjectItem(json_body, "prompt");
        if (json_prompt != NULL && cJSON_IsString(json_prompt) && (json_prompt->valuestring != NULL)) {
            p_params->p_prompt = (char *)tf_malloc(strlen(json_prompt->valuestring) + 1);
            if (p_params->p_prompt != NULL) {
                strcpy(p_params->p_prompt, json_prompt->valuestring);
            }
        } else {
            p_params->p_prompt = NULL;
        }

        cJSON *json_audio_txt = cJSON_GetObjectItem(json_body, "audio_txt");
        if (json_audio_txt != NULL && cJSON_IsString(json_audio_txt) && (json_audio_txt->valuestring != NULL)) {
            p_params->p_audio_txt = (char *)tf_malloc(strlen(json_audio_txt->valuestring) + 1);
            if (p_params->p_audio_txt != NULL) {
                strcpy(p_params->p_audio_txt, json_audio_txt->valuestring);
            }
        } else {
            p_params->p_prompt = NULL;
        }

        cJSON *json_type = cJSON_GetObjectItem(json_body, "type");
        if (json_type != NULL && cJSON_IsNumber(json_type)) {
            p_params->type = json_type->valueint;
        }
    }
    return 0;
}

#ifdef IMG_ANALYZER_NET_CHECK_ENABLE
static void __wifi_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    switch (id)
    {
        case VIEW_EVENT_WIFI_ST:
        {
            struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)p_event_data;
            if (p_st->is_network){
                g_net_flag= true;
            } else {
                g_net_flag = false;
            }
            break;
        }
        default:
            break;
    }
}
#endif

static void __event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    tf_module_img_analyzer_t *p_module_ins = (tf_module_img_analyzer_t *)handler_args;
    
    ESP_LOGI(TAG, "Input trigger");

    uint32_t type = ((uint32_t *)p_event_data)[0];
    if( type !=  TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE ) {
        ESP_LOGW(TAG, "Unsupport type %d", type);
        tf_data_free(p_event_data);
        return;
    }

#ifdef IMG_ANALYZER_NET_CHECK_ENABLE
    // TODO local area network ？
    if(!g_net_flag) {
        ESP_LOGE(TAG, "No network");
        tf_data_free(p_event_data);
        return;
    }
#endif

    if( xQueueSend(p_module_ins->queue_handle, p_event_data, ( TickType_t ) 0) != pdTRUE) {
        ESP_LOGW(TAG, "xQueueSend failed");
        tf_data_free(p_event_data);
    }

}
char * __token_gen(void)
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

static char *__request( const char *url,
                        esp_http_client_method_t method, 
                        const char *token, 
                        const char *content_type,
                        const char *head, 
                        uint8_t *data, size_t len,
                        int timeout_ms )
{
    esp_err_t  ret = ESP_OK;
    char *result = NULL;

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = timeout_ms,
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

static int __https_upload_image(tf_module_img_analyzer_t             *p_module_ins, 
                                tf_data_dualimage_with_inference_t   *p_data,
                                struct tf_module_img_analyzer_result *p_result)
{
    int ret = 0;
    struct tf_module_img_analyzer_params *p_params = &p_module_ins->params;
    char *p_str = NULL;
    cJSON *json = NULL;
    char *json_str = NULL;
    char *p_resp = NULL;

    json = cJSON_CreateObject();

    p_str = "";
    if(p_data->img_large.p_buf != NULL) {
        p_str = (char *)p_data->img_large.p_buf;
    }
    cJSON_AddItemToObject(json, "img", cJSON_CreateString(p_str));

    __data_lock(p_module_ins);
    p_str = "";
    if( p_params->p_prompt ) {
        p_str = p_params->p_prompt;
    }
    cJSON_AddItemToObject(json, "prompt", cJSON_CreateString(p_str));

    p_str = "";
    if( p_params->p_audio_txt ) {
        p_str = p_params->p_audio_txt;
    }
    cJSON_AddItemToObject(json, "audio_txt", cJSON_CreateString(p_str));

    cJSON_AddItemToObject(json, "type", cJSON_CreateNumber(p_params->type));
    __data_unlock(p_module_ins);

    json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Post %s", p_module_ins->url); 

    p_resp = __request(p_module_ins->url, 
                       HTTP_METHOD_POST, 
                       p_module_ins->token,
                       "application/json",
                       p_module_ins->head, 
                       (uint8_t *)json_str, strlen(json_str),
                       p_module_ins->timeout_ms);
    free(json_str);

    if (p_resp == NULL) {
        ESP_LOGE(TAG, "request failed");
        return -1;
    }

    // ESP_LOGD(TAG, "Response: %s", p_resp); 

    json = cJSON_Parse(p_resp);
    if (json == NULL) {
        ESP_LOGE(TAG, "Json parse failed");
        tf_free(p_resp);
        return -1;
    }

    ret = -1;
    cJSON *code = cJSON_GetObjectItem(json, "code");
    if (code != NULL && cJSON_IsNumber(code) && code->valueint == 200) {

        cJSON *json_data = cJSON_GetObjectItem(json, "data");
        if (json_data != NULL && cJSON_IsObject(json_data)) {

            cJSON *json_state = cJSON_GetObjectItem(json_data, "state");
            if ( json_state != NULL && cJSON_IsNumber(json_state)) {
                p_result->status = json_state->valueint;
            } else {
                p_result->status = 0;
            }

            cJSON *json_type = cJSON_GetObjectItem(json_data, "type");
            if ( json_type != NULL && cJSON_IsNumber(json_state)) {
                p_result->type = json_type->valueint;
            } else {
                p_result->type = p_params->type;
            }

            p_result->audio.p_buf = NULL;
            p_result->audio.len   = 0;
            cJSON *json_audio = cJSON_GetObjectItem(json_data, "audio");
            if (json_audio != NULL && cJSON_IsString(json_audio) && strlen(json_audio->valuestring) > 0 ) {
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
                            p_result->audio.p_buf = p_audio;
                            p_result->audio.len   = output_len;
                            ESP_LOGI(TAG, "audio:%d", output_len);
                        } else {
                            tf_free(p_audio);
                            ESP_LOGE(TAG, "base64 decode failed");
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "Base64 decode failed, ret: %d, len:%d", decode_ret, output_len);
                }
            }

            p_result->img.p_buf = NULL;
            p_result->img.len   = 0;
            cJSON *json_img = cJSON_GetObjectItem(json_data, "img");
            if ( json_img != NULL && cJSON_IsString(json_img)) {
                uint8_t *p_img = (uint8_t *)tf_malloc( strlen(json_img->valuestring) );
                if( p_img ) {
                    memcpy(p_img, json_img->valuestring, strlen(json_img->valuestring));
                    p_result->img.p_buf = p_img;
                    p_result->img.len   = strlen(json_img->valuestring);
                    p_result->img.time  = p_data->img_large.time;
                    ESP_LOGI(TAG, "img:%d", p_result->img.len);
                }
            }
            ret = 0; //success
        }
    } else {
        if( code != NULL ) {
            ESP_LOGE(TAG, "code: %d", code->valueint);
        }
    }

    tf_free(p_resp);
    cJSON_Delete(json);
    return ret;
}

static void img_analyzer_task(void *p_arg)
{
    int ret = 0;
    tf_module_img_analyzer_t *p_module_ins = (tf_module_img_analyzer_t *)p_arg;
    struct tf_module_img_analyzer_params *p_params = &p_module_ins->params;
    tf_data_dualimage_with_inference_t data;
    struct tf_module_img_analyzer_result result;
    tf_data_dualimage_with_audio_text_t output_data;
    EventBits_t bits;

    while(1) {
        
        bits = xEventGroupWaitBits(p_module_ins->event_group, \
                EVENT_NEED_DELETE | EVENT_STOP , pdTRUE, pdFALSE, ( TickType_t ) 10 );

        if (( bits & EVENT_NEED_DELETE ) != 0)  {
            ESP_LOGI(TAG, "EVENT_NEED_DELETE");
            while (xQueueReceive(p_module_ins->queue_handle, &data,0) == pdPASS ) {
                tf_data_free((void *)&data); //clear queue
            }
            xEventGroupSetBits(p_module_ins->event_group, EVENT_TASK_DELETED);
            vTaskDelete(NULL);
        }

        if (( bits & EVENT_STOP ) != 0)  {
            ESP_LOGI(TAG, "EVENT_STOP");
            while (xQueueReceive(p_module_ins->queue_handle, &data,0) == pdPASS ) {
                tf_data_free((void *)&data); //clear queue
            }
            xEventGroupSetBits(p_module_ins->event_group, EVENT_STOP_DONE);
        }

        if(xQueueReceive(p_module_ins->queue_handle, &data, ( TickType_t ) 10 ) == pdPASS ) {
            ESP_LOGI(TAG, "Start analyse image");
            memset( &result, 0, sizeof(result) );
            ret = __https_upload_image(p_module_ins, &data, &result);
            if( ret == 0) {
                // output
                ESP_LOGI(TAG, "img_analyzer result: %d", result.status);
                if( result.type == TF_MODULE_IMG_ANALYZER_TYPE_RECOGNIZE || result.status == 1) {
                    output_data.type = TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE_AUDIO_TEXT;
                    struct tf_data_buf   text;
                    text.p_buf = (uint8_t *)p_params->p_audio_txt;
                    text.len = strlen(p_params->p_audio_txt) + 1; // add \0

                    __data_lock(p_module_ins); 
                    for (int i = 0; i < p_module_ins->output_evt_num; i++) {
                        if( result.img.p_buf) {
                            tf_data_image_copy(&output_data.img_small, &result.img); //use cloud image
                        } else {
                            tf_data_image_copy(&output_data.img_small, &data.img_small);
                        }
                        tf_data_image_copy(&output_data.img_large, &data.img_large);
                        tf_data_inference_copy(&output_data.inference, &data.inference);
                        tf_data_buf_copy(&output_data.audio, &result.audio);
                        tf_data_buf_copy(&output_data.text, &text);
                        ret = tf_event_post(p_module_ins->p_output_evt_id[i], &output_data, sizeof(output_data), pdMS_TO_TICKS(10000));
                        if( ret != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to post event %d", p_module_ins->p_output_evt_id[i]);
                            tf_data_free(&output_data);
                        } else {
                            ESP_LOGI(TAG, "Output --> %d", p_module_ins->p_output_evt_id[i]);
                        }
                    }
                    __data_unlock(p_module_ins);
                }
                
                tf_data_image_free(&result.img);
                tf_data_buf_free(&result.audio);

            } else {
                ESP_LOGE(TAG, "Failed to analyse image");
            }

            tf_data_free((void *)&data);
        }

    }
}

static void img_analyzer_task_destroy( tf_module_img_analyzer_t *p_module_ins)
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
    if( p_module_ins->queue_handle ) {
        vQueueDelete(p_module_ins->queue_handle);
        p_module_ins->queue_handle = NULL;
    }
}

/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    char *p_token = NULL, *p_host = NULL;

    tf_module_img_analyzer_t *p_module_ins = (tf_module_img_analyzer_t *)p_module;
    struct tf_module_img_analyzer_params *p_params = &p_module_ins->params;

    // test local service cfg
    local_service_cfg_type1_t local_svc_cfg = { .enable = false, .url = NULL };
    esp_err_t ret = get_local_service_cfg_type1(MAX_CALLER, CFG_ITEM_TYPE1_IMAGE_ANALYZER, &local_svc_cfg);
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
        p_module_ins->timeout_ms = 2*60000; // Private deployment of services takes more time. 
    } else {
        p_module_ins->timeout_ms = 30000;
    }

    // host
    if (p_host == NULL) p_host = CONFIG_TF_MODULE_IMG_ANALYZER_SERV_HOST;
    snprintf(p_module_ins->url, sizeof(p_module_ins->url), "%s%s", p_host, CONFIG_TF_MODULE_IMG_ANALYZER_SERV_REQ_PATH);
    
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
    return 0;
}
static int __stop(void *p_module)
{
    tf_module_img_analyzer_t *p_module_ins = (tf_module_img_analyzer_t *)p_module;

    __data_lock(p_module_ins);
    if( p_module_ins->p_output_evt_id ) {
        tf_free(p_module_ins->p_output_evt_id); 
    }
    if( p_module_ins->params.p_audio_txt){
        tf_free(p_module_ins->params.p_audio_txt);
    }
    if( p_module_ins->params.p_prompt ) {
        tf_free(p_module_ins->params.p_prompt);
    }
    p_module_ins->p_output_evt_id = NULL;
    p_module_ins->output_evt_num = 0;
    __data_unlock(p_module_ins);

    esp_err_t ret = tf_event_handler_unregister(p_module_ins->input_evt_id, __event_handler);

    xEventGroupSetBits(p_module_ins->event_group, EVENT_STOP);
    xEventGroupWaitBits(p_module_ins->event_group, EVENT_STOP_DONE, 1, 1, portMAX_DELAY);

    return ret;
}
static int __cfg(void *p_module, cJSON *p_json)
{
    tf_module_img_analyzer_t *p_module_ins = (tf_module_img_analyzer_t *)p_module;
    __data_lock(p_module_ins);
    __parmas_default(&p_module_ins->params);
    __params_parse(&p_module_ins->params, p_json);
    __parmas_printf(&p_module_ins->params);
    __data_unlock(p_module_ins);
    return 0;
}
static int __msgs_sub_set(void *p_module, int evt_id)
{
    tf_module_img_analyzer_t *p_module_ins = (tf_module_img_analyzer_t *)p_module;
    esp_err_t ret;
    p_module_ins->input_evt_id = evt_id;
    ret = tf_event_handler_register(evt_id, __event_handler, p_module_ins);
    return ret;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    tf_module_img_analyzer_t *p_module_ins = (tf_module_img_analyzer_t *)p_module;
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
    tf_module_img_analyzer_t *p_module_ins = (tf_module_img_analyzer_t *) tf_malloc(sizeof(tf_module_img_analyzer_t));
    if (p_module_ins == NULL)
    {
        return NULL;
    }
    memset(p_module_ins, 0, sizeof(tf_module_img_analyzer_t));
    return tf_module_img_analyzer_init(p_module_ins);
}

static  void __module_destroy(tf_module_t *handle)
{
    if( handle ) {
        img_analyzer_task_destroy((tf_module_img_analyzer_t*)handle->p_module);
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
tf_module_t * tf_module_img_analyzer_init(tf_module_img_analyzer_t *p_module_ins)
{
    esp_err_t ret = ESP_OK;
    if ( NULL == p_module_ins)
    {
        return NULL;
    }
#if CONFIG_ENABLE_TF_MODULE_IMG_ANALYZER_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    p_module_ins->module_base.p_module = p_module_ins;
    p_module_ins->module_base.ops = &__g_module_ops;
    
    __parmas_default(&p_module_ins->params);

    p_module_ins->p_output_evt_id = NULL;
    p_module_ins->output_evt_num = 0;
    p_module_ins->input_evt_id = 0;
    p_module_ins->timeout_ms = 30000;

    p_module_ins->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_module_ins->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");

    p_module_ins->queue_handle = xQueueCreate(TF_MODULE_IMG_ANALYZER_QUEUE_SIZE, sizeof(tf_data_dualimage_with_inference_t));
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->queue_handle, ESP_FAIL, err, TAG, "Failed to create queue");

    p_module_ins->p_task_stack_buf = (StackType_t *)tf_malloc(TF_MODULE_IMG_ANALYZER_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    // task TCB must be allocated from internal memory 
    p_module_ins->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_module_ins->task_handle = xTaskCreateStatic(img_analyzer_task,
                                                "img_analyzer_task",
                                                TF_MODULE_IMG_ANALYZER_TASK_STACK_SIZE,
                                                (void *)p_module_ins,
                                                TF_MODULE_IMG_ANALYZER_TASK_PRIO,
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

esp_err_t tf_module_img_analyzer_register(void)
{

#ifdef IMG_ANALYZER_NET_CHECK_ENABLE
    esp_err_t ret = ESP_OK;
    ret = esp_event_handler_instance_register_with(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, \
                                                    __wifi_event_handler, NULL, NULL);
    if( ret != ESP_OK ) {
        return ret;
    }
#endif

    return tf_module_register(TF_MODULE_IMG_ANALYZER_NAME,
                              TF_MODULE_IMG_ANALYZER_DESC,
                              TF_MODULE_IMG_ANALYZER_VERSION,
                              &__g_module_mgmt);
}