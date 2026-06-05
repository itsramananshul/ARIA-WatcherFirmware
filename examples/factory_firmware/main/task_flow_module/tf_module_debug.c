#include "tf_module_debug.h"
#include "tf_module_util.h"
#include <string.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"

#define MODULE_DECLARE(p_module_ins, p_module) tf_module_debug_t *p_module_ins = (tf_module_debug_t *)p_module

static const char *TAG = "tfm.debug";


static void __event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    tf_module_debug_t *p_module_ins = (tf_module_debug_t *)handler_args;
    uint32_t type = ((uint32_t *)p_event_data)[0];
    switch (type)
    {
        case TF_DATA_TYPE_BUFFER: {
            tf_data_buffer_t * p_buf = (tf_data_buffer_t *)p_event_data;
            if( p_buf->data.p_buf != NULL ) {
                printf("len:%d, data:%s", p_buf->data.len , p_buf->data.p_buf);
            }
            break;
        }
        // add other data type debug...
        
        default:
            ESP_LOGW(TAG, "unsupport type %d", type);
            break;
    }

    tf_data_free(p_event_data);
}

/*************************************************************************
 * Interface implementation
 ************************************************************************/

static int __start(void *p_module)
{
    tf_module_debug_t *p_module_ins = (tf_module_debug_t *)p_module;
    return 0;
}
static int __stop(void *p_module)
{
    tf_module_debug_t *p_module_ins = (tf_module_debug_t *)p_module;
    esp_err_t ret = tf_event_handler_unregister(p_module_ins->evt_id, __event_handler);
    return 0;
}
static int __cfg(void *p_module, cJSON *p_json)
{
    tf_module_debug_t *p_module_ins = (tf_module_debug_t *)p_module;
    return 0;
}
static int __msgs_sub_set(void *p_module, int evt_id)
{
    tf_module_debug_t *p_module_ins = (tf_module_debug_t *)p_module;
    esp_err_t ret;
    p_module_ins->evt_id = evt_id;
    ret = tf_event_handler_register(evt_id, __event_handler, p_module_ins);
    return ret;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    tf_module_debug_t *p_module_ins = (tf_module_debug_t *)p_module;
    if (num)
    {
        ESP_LOGW(TAG, "none output");
    } 
    return 0;
}

static tf_module_t * __module_instance(void)
{
    tf_module_debug_t *p_module_ins = (tf_module_debug_t *) tf_malloc(sizeof(tf_module_debug_t));
    if (p_module_ins == NULL)
    {
        return NULL;
    }
    memset(p_module_ins, 0, sizeof(tf_module_debug_t));
    return tf_module_debug_init(p_module_ins);
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

const static  struct tf_module_mgmt __g_module_mgmt = {
    .tf_module_instance = __module_instance,
    .tf_module_destroy = __module_destroy,
};

/*************************************************************************
 * API
 ************************************************************************/

tf_module_t * tf_module_debug_init(tf_module_debug_t *p_module_ins)
{
    if ( NULL == p_module_ins)
    {
        return NULL;
    }
    p_module_ins->module_base.p_module = p_module_ins;
    p_module_ins->module_base.ops = &__g_module_ops;

    return &p_module_ins->module_base;
}

esp_err_t tf_module_debug_register(void)
{
    return tf_module_register(TF_MODULE_DEBUG_NAME,
                              TF_MODULE_DEBUG_DESC,
                              TF_MODULE_DEBUG_VERSION,
                              &__g_module_mgmt);
}