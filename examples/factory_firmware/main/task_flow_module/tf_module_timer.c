#include "tf_module_timer.h"
#include "tf_module_util.h"
#include <string.h>
#include <time.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"

static const char *TAG = "tfm.timer";

static void __timer_callback(void* p_arg)
{
    esp_err_t ret = ESP_OK;
    tf_module_timer_t *p_module_ins = (tf_module_timer_t *)p_arg;

    time_t now = 0;
    time(&now);
    
    tf_data_time_t buf_data;
    buf_data.type = TF_DATA_TYPE_TIME;
    buf_data.time = now;

    for(int i = 0; i < p_module_ins->output_evt_num; i++) {
        ret = tf_event_post(p_module_ins->p_output_evt_id[i], &buf_data, sizeof(buf_data), pdMS_TO_TICKS(10000));
        if( ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to post event %d", p_module_ins->p_output_evt_id[i]);
        } else {
            ESP_LOGI(TAG, "Output --> %d", p_module_ins->p_output_evt_id[i]);
        }
    }
}

/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    tf_module_timer_t *p_module_ins = (tf_module_timer_t *)p_module;
    esp_err_t ret = ESP_OK;
    const esp_timer_create_args_t timer_args = {
            .callback = &__timer_callback,
            .arg = (void*) p_module_ins,
            .name = "module timer"
    };
    ret = esp_timer_create(&timer_args, &p_module_ins->timer_handle);
    if(ret != ESP_OK) {
        return NULL;
    }
    esp_timer_start_periodic(p_module_ins->timer_handle, 1000000 * p_module_ins->period_s);
    return 0;
}
static int __stop(void *p_module)
{
    tf_module_timer_t *p_module_ins = (tf_module_timer_t *)p_module;
    esp_timer_stop(p_module_ins->timer_handle);
    esp_timer_delete(p_module_ins->timer_handle);
    tf_free(p_module_ins->p_output_evt_id);
    return 0;
}
static int __cfg(void *p_module, cJSON *p_json)
{
    tf_module_timer_t *p_module_ins = (tf_module_timer_t *)p_module;

    cJSON *p_period = NULL;
    p_period = cJSON_GetObjectItem(p_json, "period");
    if (p_period == NULL || !cJSON_IsNumber(p_period))
    {
        ESP_LOGE(TAG, "params period err, default 5s");
        p_module_ins->period_s = 5;
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "params period %d", p_period->valueint);
        p_module_ins->period_s = p_period->valueint;
    }
    return 0;
}
static int __msgs_sub_set(void *p_module, int evt_id)
{
    tf_module_timer_t *p_module_ins = (tf_module_timer_t *)p_module;
    p_module_ins->id = evt_id;
    return 0;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    tf_module_timer_t *p_module_ins = (tf_module_timer_t *)p_module;
    if (output_index == 0 && num > 0)
    {
        p_module_ins->p_output_evt_id = (int *)tf_malloc(sizeof(int) * num);
        if (p_module_ins->p_output_evt_id )
        {
            memcpy(p_module_ins->p_output_evt_id, p_evt_id, sizeof(int) * num);
            p_module_ins->output_evt_num = num;
        } else {
            ESP_LOGE(TAG, "malloc p_output_evt_id failed!");
            p_module_ins->output_evt_num = 0;
        }
    }
    else
    {
        ESP_LOGW(TAG, "only support output port 0, ignore %d", output_index);
    }
    return 0;
}

static tf_module_t * __module_instance(void)
{
    tf_module_timer_t *p_module_ins = (tf_module_timer_t *) tf_malloc(sizeof(tf_module_timer_t));
    if (p_module_ins == NULL)
    {
        return NULL;
    }
    memset(p_module_ins, 0, sizeof(tf_module_timer_t));
    return tf_module_timer_init(p_module_ins);
}

static  void __module_destroy(tf_module_t *handle)
{
    if( handle ) {
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

tf_module_t * tf_module_timer_init(tf_module_timer_t *p_module_ins)
{
    if ( NULL == p_module_ins)
    {
        return NULL;
    }
    p_module_ins->module_base.p_module = p_module_ins;
    p_module_ins->module_base.ops = &__g_module_ops;

    return &p_module_ins->module_base;
}

esp_err_t tf_module_timer_register(void)
{
    return tf_module_register(TF_MODULE_TIMER_NAME,
                              TF_MODULE_TIMER_DESC,
                              TF_MODULE_TIMER_VERSION,
                              &__g_module_mgmt);
}