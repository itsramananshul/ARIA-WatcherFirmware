#include "tf_module_local_alarm.h"
#include "tf_module_util.h"
#include <string.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"
#include "esp_check.h"
#include <mbedtls/base64.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "audio_player.h"
#include "app_audio_player.h"
#include "data_defs.h"
#include "event_loops.h"
#include "app_rgb.h"



static const char *TAG = "tfm.local_alarm";
static tf_module_t *g_handle = NULL;

static void __data_lock( tf_module_local_alarm_t *p_module)
{
}
static void __data_unlock( tf_module_local_alarm_t *p_module)
{

}
static void __parmas_default(struct tf_module_local_alarm_params *p_params)
{
    p_params->duration = 10;
    p_params->rgb      = true;
    p_params->sound    = true;
    p_params->img      = true;
    p_params->text     = true;
}
static int __params_parse(struct tf_module_local_alarm_params *p_params, cJSON *p_json)
{
    cJSON *json_sound = cJSON_GetObjectItem(p_json, "sound");
    if (json_sound != NULL  && tf_cJSON_IsGeneralBool(json_sound)) {
        p_params->sound = tf_cJSON_IsGeneralTrue(json_sound);
    }

    cJSON *json_rgb = cJSON_GetObjectItem(p_json, "rgb");
    if (json_rgb != NULL  && tf_cJSON_IsGeneralBool(json_rgb)) {
        p_params->rgb = tf_cJSON_IsGeneralTrue(json_rgb);
    }
    cJSON *json_img = cJSON_GetObjectItem(p_json, "img");
    if (json_img != NULL  && tf_cJSON_IsGeneralBool(json_img)) {
        p_params->img = tf_cJSON_IsGeneralTrue(json_img);
    }
    cJSON *json_text = cJSON_GetObjectItem(p_json, "text");
    if (json_text != NULL  && tf_cJSON_IsGeneralBool(json_text)) {
        p_params->text = tf_cJSON_IsGeneralTrue(json_text);
    }
    cJSON *json_duration = cJSON_GetObjectItem(p_json, "duration");
    if (json_duration != NULL  && cJSON_IsNumber(json_duration)) {
        p_params->duration = json_duration->valueint;
    }
    return 0;
}
static void __alarm_off( tf_module_local_alarm_t *p_module_ins )
{
    struct tf_module_local_alarm_params *p_params = &p_module_ins->params;
    //TODO 
    // RGB OFF
    // SOUND OFF
    if( p_params->rgb && p_module_ins->is_rgb_on) {
        ESP_LOGI(TAG, "RGB OFF");
        p_module_ins->is_rgb_on = false;
        app_rgb_set(ALARM, RGB_OFF);
    }
    if( p_params->sound) {
        // TODO SOUND OFF
        ESP_LOGI(TAG, "SOUND OFF");
    }
}

#if TF_MODULE_LOCAL_ALARM_TIMER_ENABLE
static void __timer_callback(void* p_arg)
{
    tf_module_local_alarm_t *p_module_ins = (tf_module_local_alarm_t *)p_arg;
    struct tf_module_local_alarm_params *p_params = &p_module_ins->params;
    __alarm_off(p_module_ins);
}
#endif

//  static void __audio_play_finish_cb(void)
//  {  
//     ESP_LOGI(TAG, "audio play finish");
//     if( g_handle != NULL) {
//         tf_module_local_alarm_t *p_module_ins = (tf_module_local_alarm_t *)g_handle->p_module;
//         tf_data_buf_free(&p_module_ins->audio);
//         p_module_ins->is_audio_playing = false;
//     }
//  }

static void __alarm_off_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    tf_module_local_alarm_t *p_module_ins = (tf_module_local_alarm_t *)handler_args;
    __alarm_off(p_module_ins);
}

static void __event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    esp_err_t ret = ESP_OK;
    tf_module_local_alarm_t *p_module_ins = (tf_module_local_alarm_t *)handler_args;
    struct tf_module_local_alarm_params *p_params = &p_module_ins->params;
   
    uint32_t type = ((uint32_t *)p_event_data)[0];
    if( type !=  TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE_AUDIO_TEXT) {
        ESP_LOGW(TAG, "unsupport type %d", type);
        tf_data_free(p_event_data);
        return;
    }

    struct tf_module_local_alarm_info info;
    tf_data_dualimage_with_audio_text_t *p_data = (tf_data_dualimage_with_audio_text_t*)p_event_data;

    bool img_small_used = false;
    bool img_large_used = false;
    bool text_used      = false;
    bool audio_used     = false;

    //notify screen
    memset(&info, 0, sizeof(info));
    info.duration = p_params->duration;
    info.is_show_img = p_params->img;
    info.is_show_text = p_params->text;
    if( info.is_show_img ) {
        info.img.p_buf = p_data->img_small.p_buf;
        info.img.time = p_data->img_small.time;
        info.img.len = p_data->img_small.len;
        img_small_used = true;
    }
    if( info.is_show_text ) {
        info.text.p_buf = p_data->text.p_buf;
        info.text.len = p_data->text.len;
        text_used = true;
    }

    ret = esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                    VIEW_EVENT_ALARM_ON, &info, sizeof(info), portMAX_DELAY);
    if( ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post alram on event");
        tf_data_image_free(&info.img);
        tf_data_buf_free(&info.text);
    }

    if(p_params->rgb) {
        // TODO RGB ON
        ESP_LOGI(TAG, "RGB ON");
        p_module_ins->is_rgb_on = true;
        app_rgb_set(ALARM, RGB_BLINK_RED);
    }

    if(p_params->sound) {
        ESP_LOGI(TAG, "SOUND ON");
        FILE *fp = NULL;
        esp_err_t status = ESP_FAIL;

        if( app_audio_player_status_get() == AUDIO_PLAYER_STATUS_IDLE ) {
            if( p_data->audio.p_buf != NULL && p_data->audio.len > 0 ) {
                ESP_LOGI(TAG,"play audio buf");
                ret = app_audio_player_mem(p_data->audio.p_buf, p_data->audio.len, true);
                if( ret == ESP_OK) {
                    audio_used = true;
                }
            } else {
                ESP_LOGI(TAG,"play audio file:%s" ,TF_MODULE_LOCAL_ALARM_DEFAULT_AUDIO_FILE);
                app_audio_player_file(TF_MODULE_LOCAL_ALARM_DEFAULT_AUDIO_FILE);
            }
        } else {
            ESP_LOGW(TAG, "audio is playing");
        }
    }
    
    // free data
    if( !img_small_used ) {
        tf_data_image_free(&p_data->img_small);
    }
    if( !img_large_used ) {
        tf_data_image_free(&p_data->img_large);
    }
    if( !text_used ) {
        tf_data_buf_free(&p_data->text);
    }
    if( !audio_used ) {
        tf_data_buf_free(&p_data->audio);
    }
    
    tf_data_inference_free(&p_data->inference);
#if TF_MODULE_LOCAL_ALARM_TIMER_ENABLE
    // esp_timer_start_once(p_module_ins->timer_handle, p_params->duration * 1000000);
#endif

}
/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    tf_module_local_alarm_t *p_module_ins = (tf_module_local_alarm_t *)p_module;
    return 0;
}
static int __stop(void *p_module)
{
    tf_module_local_alarm_t *p_module_ins = (tf_module_local_alarm_t *)p_module;
#if TF_MODULE_LOCAL_ALARM_TIMER_ENABLE
    esp_timer_stop(p_module_ins->timer_handle);
#endif
    __alarm_off(p_module_ins);
    esp_err_t ret = tf_event_handler_unregister(p_module_ins->input_evt_id, __event_handler);
    return ret;
}
static int __cfg(void *p_module, cJSON *p_json)
{
    ESP_LOGI(TAG, "cfg");

    tf_module_local_alarm_t *p_module_ins = (tf_module_local_alarm_t *)p_module;
    __data_lock(p_module_ins);
    __parmas_default(&p_module_ins->params);
    __params_parse(&p_module_ins->params, p_json);
    __data_unlock(p_module_ins);
    return 0;
}
static int __msgs_sub_set(void *p_module, int evt_id)
{
    tf_module_local_alarm_t *p_module_ins = (tf_module_local_alarm_t *)p_module;
    esp_err_t ret;
    p_module_ins->input_evt_id = evt_id;
    ret = tf_event_handler_register(evt_id, __event_handler, p_module_ins);
    return ret;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    tf_module_local_alarm_t *p_module_ins = (tf_module_local_alarm_t *)p_module;
    if ( num > 0) {
        ESP_LOGW(TAG, "nonsupport output");
    }
    return 0;
}

static tf_module_t * __module_instance(void)
{
    if (g_handle)
    {
        return g_handle;
    }

    tf_module_local_alarm_t *p_module_ins = (tf_module_local_alarm_t *) tf_malloc(sizeof(tf_module_local_alarm_t));
    if (p_module_ins == NULL)
    {
        return NULL;
    }
    memset(p_module_ins, 0, sizeof(tf_module_local_alarm_t));
    return tf_module_local_alarm_init(p_module_ins);
}

static  void __module_destroy(tf_module_t *handle)
{
    //Don't free handle
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
tf_module_t * tf_module_local_alarm_init(tf_module_local_alarm_t *p_module_ins)
{
    esp_err_t ret = ESP_OK;
    if ( NULL == p_module_ins)
    {
        return NULL;
    }
#if CONFIG_ENABLE_TF_MODULE_LOCAL_ALARM_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    p_module_ins->module_base.p_module = p_module_ins;
    p_module_ins->module_base.ops = &__g_module_ops;
    
    __parmas_default(&p_module_ins->params);

    p_module_ins->input_evt_id = 0;

#if TF_MODULE_LOCAL_ALARM_TIMER_ENABLE
    const esp_timer_create_args_t timer_args = {
            .callback = &__timer_callback,
            .arg = (void*) p_module_ins,
            .name = "alarm timer"
    };
    ret = esp_timer_create(&timer_args, &p_module_ins->timer_handle);
    if(ret != ESP_OK) {
        return NULL;
    }
#endif

    ret = esp_event_handler_instance_register_with(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_ALARM_OFF, \
                                                    __alarm_off_event_handler, p_module_ins, NULL);
    if( ret != ESP_OK ) {
        ESP_LOGI(TAG, "Failed to register alarm off event: %d", ret);
        return NULL;
    }

    // audio_register_play_finish_cb(__audio_play_finish_cb);

    return &p_module_ins->module_base;
}

esp_err_t tf_module_local_alarm_register(void)
{
    g_handle = __module_instance(); // Must be instantiated
    if (g_handle == NULL)
    {
        return ESP_FAIL;
    }
    return tf_module_register(TF_MODULE_LOCAL_ALARM_NAME,
                              TF_MODULE_LOCAL_ALARM_DESC,
                              TF_MODULE_LOCAL_ALARM_RVERSION,
                              &__g_module_mgmt);
}