#include "tf.h"
#include <string.h>
#include <stdlib.h>
#include "tf_parse.h"
#include "tf_util.h"
#include "esp_log.h"
#include "esp_check.h"

ESP_EVENT_DEFINE_BASE(TF_EVENT_BASE);

static const char *TAG = "tf.engine";

static tf_engine_t *gp_engine = NULL;

#define EVENT_START           BIT0
#define EVENT_STOP            BIT1
#define EVENT_ERR_EXIT        BIT2
#define EVENT_PAUSE           BIT3
#define EVENT_RESUME          BIT4
#define EVENT_RESTART         BIT5
#define EVENT_PAUSE_DONE      BIT6

#define MODULE_FLAG_INIT_DONE      BIT0
#define MODULE_FLAG_INSTANCE_DONE  BIT1
#define MODULE_FLAG_CFG_DONE       BIT2
#define MODULE_FLAG_SUB_SET_DONE   BIT3
#define MODULE_FLAG_PUB_SET_DONE   BIT4
#define MODULE_FLAG_START_DONE     BIT5

static void __data_lock( tf_engine_t *p_engine)
{
    xSemaphoreTake(p_engine->sem_handle, portMAX_DELAY);
}
static void __data_unlock( tf_engine_t *p_engine)
{
    xSemaphoreGive(p_engine->sem_handle);  
}

static void __status_cb( tf_engine_t *p_engine, int status, const char *p_err_module)
{
    tf_engine_status_cb_t  status_cb = NULL;
    void * p_status_cb_arg = NULL;
    intmax_t tid = 0;

    __data_lock(p_engine);
    p_engine->status = status;
    status_cb = p_engine->status_cb;
    p_status_cb_arg = p_engine->p_status_cb_arg;
    tid = p_engine->tf_info.tid;
    __data_unlock(p_engine);

    if( status_cb ) {
        status_cb(p_status_cb_arg, tid, status, p_err_module);
    }
}
void __modules_item_print(tf_module_item_t *p_head, int num)
{
    ESP_LOGI(TAG, "modules:");
    for(int i = 0; i < num && p_head; i++) {
        ESP_LOGI(TAG, "    %s-%d", p_head[i].p_name, p_head[i].id);
    }
}

int __modules_index_compare(const void *a, const void *b)
{
    return ((tf_module_item_t *)b)->index - ((tf_module_item_t *)a)->index;
}
static int __modules_init(tf_engine_t *p_engine, tf_module_item_t *p_head, int num, const char **pp_err_module)
{
    *pp_err_module = NULL;   
    if( p_head == NULL || num <= 0 ) {
        return ESP_FAIL;
    }
    qsort(p_head, num, sizeof(tf_module_item_t), __modules_index_compare);
    
    ESP_LOGI(TAG, "==== after sort ===");
    __modules_item_print(p_head, num);

    for(int i = 0; i < num; i++) {
        __data_lock(p_engine);
        p_head[i].flag = 0;
        if (!SLIST_EMPTY(&(p_engine->module_nodes)))
        {
            tf_module_node_t *it = NULL;
            SLIST_FOREACH(it, &(p_engine->module_nodes), next)
            {
                if (strcmp(it->p_name, p_head[i].p_name) == 0)
                {
                    p_head[i].mgmt_handle = it->mgmt_handle;
                    break;
                }
            }
        }
        __data_unlock(p_engine);
        if( p_head[i].mgmt_handle == NULL ) {
            ESP_LOGE(TAG, "Not find module: %s", p_head[i].p_name);
            *pp_err_module = p_head[i].p_name;
            return ESP_FAIL;
        }
        p_head[i].flag |= MODULE_FLAG_INIT_DONE;
    }
    return ESP_OK;
}
static int __modules_instance(tf_module_item_t *p_head, int num, const char **pp_err_module)
{
    *pp_err_module = NULL;
    if( p_head ==NULL || num <= 0 ) {
        return ESP_FAIL;
    }
    for(int i = 0; i < num; i++) {
        p_head[i].handle = p_head[i].mgmt_handle->tf_module_instance();
        if(p_head[i].handle == NULL) {
            ESP_LOGE(TAG, "module %s instance failed", p_head[i].p_name);
            *pp_err_module = p_head[i].p_name;
            return ESP_FAIL;
        }
        p_head[i].flag |= MODULE_FLAG_INSTANCE_DONE;
    }
   return ESP_OK;
}
static int __modules_destroy(tf_module_item_t *p_head, int num)
{
    if( p_head ==NULL || num <= 0 ) {
        return ESP_FAIL;
    }
    for(int i = 0; i < num; i++) {
        if(p_head[i].handle != NULL && p_head[i].mgmt_handle != NULL &&
            (p_head[i].flag & MODULE_FLAG_INSTANCE_DONE) && (p_head[i].flag & MODULE_FLAG_INIT_DONE)) {
            p_head[i].mgmt_handle->tf_module_destroy(p_head[i].handle);
        }
    }
   return ESP_OK;
}
static int __modules_start(tf_module_item_t *p_head, int num, const char **pp_err_module)
{
    int ret = ESP_OK;
    *pp_err_module = NULL;
    if( p_head ==NULL || num <= 0 ) {
        return ESP_FAIL;
    }
    for(int i = 0; i < num; i++) {
        ret = tf_module_start(p_head[i].handle);
        if(ret != ESP_OK) {
            ESP_LOGE(TAG, "Module %s start failed", p_head[i].p_name);
            *pp_err_module = p_head[i].p_name;
            return ret;
        }
        p_head[i].flag |= MODULE_FLAG_START_DONE;
    }
   return ESP_OK;
}
static int __modules_stop(tf_module_item_t *p_head, int num)
{
    int ret = ESP_OK;
    if( p_head ==NULL || num <= 0 ) {
        return ESP_FAIL;
    }
    // Reverse Order to STOP
    for(int i = (num-1); i >= 0; i--) {
        if ( p_head[i].handle != NULL  && ( \
            p_head[i].flag & MODULE_FLAG_START_DONE || 
            p_head[i].flag & MODULE_FLAG_CFG_DONE   ||
            p_head[i].flag & MODULE_FLAG_PUB_SET_DONE ||
            p_head[i].flag & MODULE_FLAG_SUB_SET_DONE)) {

            ret = tf_module_stop(p_head[i].handle);
            if(ret != ESP_OK) {
                ESP_LOGE(TAG, "Module %s stop failed", p_head[i].p_name);
            }
        } else {
            ESP_LOGI(TAG, "Module %s no need to stop", p_head[i].p_name);
        }
    }
   return ESP_OK;
}
static int __modules_cfg(tf_module_item_t *p_head, int num, const char **pp_err_module)
{
    int ret = ESP_OK;
    *pp_err_module = NULL;
    if( p_head ==NULL || num <= 0 ) {
        return ESP_FAIL;
    }

    for(int i = 0; i < num; i++) {
        ret = tf_module_cfg(p_head[i].handle, p_head[i].p_params);
        if(ret != ESP_OK) {
            ESP_LOGE(TAG, "Module %s cfg failed", p_head[i].p_name);
            *pp_err_module = p_head[i].p_name;
            return ret;
        }
        p_head[i].flag |= MODULE_FLAG_CFG_DONE;
    }
   return ESP_OK;
}

static int __modules_msgs_sub_set(tf_module_item_t *p_head, int num, const char **pp_err_module)
{
    int ret = ESP_OK;
    *pp_err_module = NULL;
    if( p_head ==NULL || num <= 0 ) {
        return ESP_FAIL;
    }
    for(int i = 0; i < num; i++) {
        ret = tf_module_msgs_sub_set(p_head[i].handle, p_head[i].id);
        if(ret != ESP_OK) {
            ESP_LOGE(TAG, "Module %s msgs sub set failed", p_head[i].p_name);
            *pp_err_module = p_head[i].p_name;
            return ret;
        }
        p_head[i].flag |= MODULE_FLAG_SUB_SET_DONE;
    }
   return ESP_OK;
}
static int __modules_wires_check(tf_module_item_t *p_head, int num, struct tf_module_wires *p_wires)
{
    int i = 0;
    int j = 0;
    for( i = 0; i < p_wires->num; i++) {
        for( j = 0; j < num; j++ ) {
            if(p_head[j].id == p_wires->p_evt_id[i]) {
                break;
            }
        }
        if( j >= num ) {
            ESP_LOGE(TAG, "Not find wire: %d", p_wires->p_evt_id[i]);
            return ESP_FAIL;
        }
    }
   return ESP_OK;
}

static int __modules_msgs_pub_set(tf_module_item_t *p_head, int num, const char **pp_err_module)
{
    int ret = ESP_OK;
    *pp_err_module = NULL;
    if( p_head ==NULL || num <= 0 ) {
        return ESP_FAIL;
    }
    for(int i = 0; i < num; i++) {
        for(int j = 0; j < p_head[i].output_port_num; j++) {

            ret = __modules_wires_check(p_head, num, p_head[i].p_wires);
            if(ret != ESP_OK) {
                *pp_err_module = p_head[i].p_name;
                return ret;
            }
            ret = tf_module_msgs_pub_set(p_head[i].handle, j,  \
                                         p_head[i].p_wires->p_evt_id, p_head[i].p_wires->num);
            if(ret != ESP_OK) {
                ESP_LOGE(TAG, "Module %s msgs pub set failed", p_head[i].p_name);
                *pp_err_module = p_head[i].p_name;
                return ret;
            }
            p_head[i].flag |= MODULE_FLAG_PUB_SET_DONE;
        }
    }
   return ESP_OK;
}
static int __stop(tf_engine_t *p_engine)
{
    __modules_stop(p_engine->p_module_head, p_engine->module_item_num);
    __modules_destroy(p_engine->p_module_head, p_engine->module_item_num);    
    return ESP_OK;
}
static int __clear(tf_engine_t *p_engine)
{
    // don't clear tf_info
    __data_lock(p_engine);
    tf_parse_free(p_engine->cur_flow_root, p_engine->p_module_head, p_engine->module_item_num);
    p_engine->cur_flow_root = NULL;
    p_engine->p_module_head = NULL;
    p_engine->module_item_num = 0;
    __data_unlock(p_engine);
    return ESP_OK;
}
static int __run(tf_engine_t *p_engine)
{
    int ret =  0;
    const char *p_err_module = NULL;

    ESP_LOGI(TAG, "======= START ======");
    ESP_LOGI(TAG, "tlid: %jd", p_engine->tf_info.tid);
    ESP_LOGI(TAG, "name: %s", p_engine->tf_info.p_tf_name);
    ESP_LOGI(TAG, "type: %ld", p_engine->tf_info.type);
    ESP_LOGI(TAG, "num:  %d", p_engine->module_item_num);
    __modules_item_print(p_engine->p_module_head, p_engine->module_item_num);
    ESP_LOGI(TAG, "====================");
    
    ret = __modules_init(p_engine, p_engine->p_module_head, p_engine->module_item_num, &p_err_module);
    if( ret != ESP_OK ) {
        __status_cb(p_engine, TF_STATUS_ERR_MODULE_NOT_FOUND, p_err_module);
        return ESP_FAIL;
    }

    ret =  __modules_instance(p_engine->p_module_head, p_engine->module_item_num, &p_err_module);
    if( ret != ESP_OK ) {
        __status_cb(p_engine, TF_STATUS_ERR_MODULES_INSTANCE, p_err_module);
        return ESP_FAIL;
    }

    ret =  __modules_cfg(p_engine->p_module_head, p_engine->module_item_num, &p_err_module);
    if( ret != ESP_OK ) {
        __status_cb(p_engine, TF_STATUS_ERR_MODULES_PARAMS, p_err_module);
        return ESP_FAIL;
    }

    ret =  __modules_msgs_sub_set(p_engine->p_module_head, p_engine->module_item_num, &p_err_module);
    if( ret != ESP_OK ) {
        __status_cb(p_engine, TF_STATUS_ERR_MODULES_WIRES, p_err_module);
        return ESP_FAIL;
    }

    ret = __modules_msgs_pub_set(p_engine->p_module_head, p_engine->module_item_num, &p_err_module);
    if( ret != ESP_OK ) {
        __status_cb(p_engine, TF_STATUS_ERR_MODULES_WIRES, p_err_module);
        return ESP_FAIL;
    }

    ret = __modules_start(p_engine->p_module_head, p_engine->module_item_num, &p_err_module);
    if( ret != ESP_OK ) {
        __status_cb(p_engine, TF_STATUS_ERR_MODULES_START, p_err_module);
        return ESP_FAIL;
    }
    __status_cb(p_engine, TF_STATUS_RUNNING, NULL);
    
    return ESP_OK;
}


static void __tf_engine_task(void *p_arg)
{
    tf_engine_t *p_engine = (tf_engine_t *)p_arg;
    tf_flow_data_t  flow;
    EventBits_t bits;

    int ret =  0;
    ESP_LOGI(TAG, "tf engine task start");
    bool run_flag = false;
    bool pause_flag = false;
    

    while (1)
    { 
        bits = xEventGroupWaitBits(p_engine->event_group, \
                EVENT_START | EVENT_STOP | EVENT_PAUSE | EVENT_RESUME | EVENT_RESTART, pdTRUE, pdFALSE, ( TickType_t ) 10);

        if( ( bits & EVENT_START ) != 0  &&  run_flag) {
            ESP_LOGI(TAG, "EVENT_START");
            //TODO
        }
        
        if( ( bits & EVENT_STOP ) != 0  &&  run_flag) {
            ESP_LOGI(TAG, "EVENT_STOP on run");
            __status_cb(p_engine, TF_STATUS_STOP, NULL);
            __stop(p_engine);
            __clear(p_engine);
            run_flag = false;
        } else if( ( bits & EVENT_STOP ) != 0  &&  pause_flag ) {
            ESP_LOGI(TAG, "EVENT_STOP on pause");
            __status_cb(p_engine, TF_STATUS_STOP, NULL);
            __clear(p_engine);
            pause_flag = false;
        }

        if( (bits & EVENT_PAUSE)  != 0 && run_flag ) {
            ESP_LOGI(TAG, "EVENT_PAUSE");
            __status_cb(p_engine, TF_STATUS_PAUSE, NULL);
            __stop(p_engine);
            pause_flag = true;
            run_flag = false;
            ESP_LOGI(TAG, "EVENT_PAUSE_DONE");
            xEventGroupSetBits(gp_engine->event_group, EVENT_PAUSE_DONE);
        }else if( (bits & EVENT_PAUSE)  != 0) {
            ESP_LOGI(TAG, "don't pause");
            xEventGroupSetBits(gp_engine->event_group, EVENT_PAUSE_DONE);
        }

        if( (bits & EVENT_RESUME)  != 0 && pause_flag ) {
            ESP_LOGI(TAG, "EVENT_RESUME");
            pause_flag = false;
            ret = __run(p_engine);
            if(  ret == ESP_OK ) {
                run_flag = true;
            } else {
                __stop(p_engine);
                __clear(p_engine);
                run_flag = false;
            }
        }
        
        if( ( bits & EVENT_RESTART ) != 0  &&  run_flag) {
            ESP_LOGI(TAG, "EVENT_RESTART");
            __stop(p_engine);
            ret = __run(p_engine);
            if(  ret == ESP_OK ) {
                run_flag = true;
            } else {
                __stop(p_engine);
                __clear(p_engine);
                run_flag = false;
            }
        }

        if( xQueueReceive(p_engine->queue_handle, &flow, ( TickType_t ) 10 ) == pdPASS ) {

            ESP_LOGI(TAG, "RECV NEW TASK");
            if(run_flag) {
                ESP_LOGI(TAG, "STOP LAST TASK");
                __stop(p_engine);
                __clear(p_engine);
                run_flag = false;
            } else if( pause_flag ) {
                ESP_LOGI(TAG, "CLEAR LAST TASK");
                __clear(p_engine);
                pause_flag = false;
            }

            __data_lock(p_engine);
            ret = tf_parse_json_with_length( flow.p_data, flow.len, &p_engine->cur_flow_root, &p_engine->p_module_head, &p_engine->tf_info);
            p_engine->module_item_num = ret;
            __data_unlock(p_engine);

            tf_free(flow.p_data);

            __status_cb(p_engine, TF_STATUS_STARTING, NULL);

            if( ret  <= 0) {
                ESP_LOGE(TAG, "parse json failed");
                __status_cb(p_engine, TF_STATUS_ERR_JSON_PARSE, NULL);
                continue;
            }
            ret = __run(p_engine);
            if(  ret == ESP_OK ) {
                run_flag = true;
            } else {
                __stop(p_engine);
                __clear(p_engine);
                run_flag = false;
            }
        }
    }
}

esp_err_t tf_engine_init(void)
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_err_t ret = ESP_OK;
    gp_engine = (tf_engine_t *)tf_malloc(sizeof(tf_engine_t));
    ESP_GOTO_ON_FALSE(gp_engine, ESP_ERR_NO_MEM, err, TAG, "no mem for tf engine");
    memset(gp_engine, 0, sizeof(tf_engine_t));

    esp_event_loop_args_t event_task_args = {
        .queue_size = 32,
        .task_name = "tf_event_task",
        .task_priority = 14,
        .task_stack_size = 1024 * 3,
        .task_core_id = 1
    };
    ret = esp_event_loop_create(&event_task_args, &gp_engine->event_handle);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "event_loop_create failed");

    SLIST_INIT(&(gp_engine->module_nodes));

    gp_engine->status = TF_STATUS_IDLE;

    gp_engine->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != gp_engine->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    gp_engine->queue_handle = xQueueCreate(TF_ENGINE_QUEUE_SIZE, sizeof(tf_flow_data_t));
    ESP_GOTO_ON_FALSE(gp_engine->queue_handle, ESP_FAIL, err, TAG, "Failed to create queue");

    gp_engine->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != gp_engine->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");
    
    gp_engine->p_task_stack_buf = (StackType_t *)tf_malloc(TF_ENGINE_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(gp_engine->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    // task TCB must be allocated from internal memory 
    gp_engine->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(gp_engine->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    gp_engine->task_handle = xTaskCreateStatic(
        __tf_engine_task,
        "tf_engine_task",
        TF_ENGINE_TASK_STACK_SIZE,
        (void *)gp_engine,
        TF_ENGINE_TASK_PRIO,
        gp_engine->p_task_stack_buf,
        gp_engine->p_task_buf);

    ESP_GOTO_ON_FALSE(gp_engine->task_handle, ESP_FAIL, err, TAG, "create task failed");

    return ret;
err:
    if (gp_engine)
    {
        if (gp_engine->p_task_stack_buf)
        {
            tf_free(gp_engine->p_task_stack_buf);
            gp_engine->p_task_stack_buf = NULL;
        }

        if( gp_engine->p_task_buf ) {
            free(gp_engine->p_task_buf);
            gp_engine->p_task_buf = NULL;
        }

        if (gp_engine->task_handle)
        {
            vTaskDelete(gp_engine->task_handle);
            gp_engine->task_handle = NULL;
        }

        if (gp_engine->queue_handle)
        {
            vQueueDelete(gp_engine->queue_handle);
            gp_engine->queue_handle = NULL;
        }

        if (gp_engine->sem_handle) {
            vSemaphoreDelete(gp_engine->sem_handle);
            gp_engine->sem_handle = NULL;
        }

        if (gp_engine->event_group) {
            vEventGroupDelete(gp_engine->event_group);
            gp_engine->event_group = NULL;
        }
        tf_free(gp_engine);
        gp_engine = NULL;
    }

    return ret;
}

esp_err_t tf_engine_run(void)
{
    assert(gp_engine);
    xEventGroupSetBits(gp_engine->event_group, EVENT_START); //TODO
    return ESP_OK;
}

esp_err_t tf_engine_stop(void)
{
    assert(gp_engine);
    xEventGroupSetBits(gp_engine->event_group, EVENT_STOP);
    return ESP_OK;
}

esp_err_t tf_engine_restart(void)
{
    assert(gp_engine);
    xEventGroupSetBits(gp_engine->event_group, EVENT_RESTART);
    return ESP_OK;
}

esp_err_t tf_engine_pause(void)
{
    assert(gp_engine);
    xEventGroupSetBits(gp_engine->event_group, EVENT_PAUSE);
    return ESP_OK;
}

esp_err_t tf_engine_pause_block(TickType_t xTicksToWait)
{
    assert(gp_engine);
    xEventGroupClearBits(gp_engine->event_group, EVENT_PAUSE_DONE);
    xEventGroupSetBits(gp_engine->event_group, EVENT_PAUSE);
    EventBits_t bits = xEventGroupWaitBits(gp_engine->event_group, EVENT_PAUSE_DONE, pdTRUE, pdTRUE, xTicksToWait);
    if( !(bits & EVENT_PAUSE_DONE)) {
        ESP_LOGW(TAG, "Pause wait timeout");
    }
    return ESP_OK;
}

esp_err_t tf_engine_resume(void)
{
    assert(gp_engine);
    xEventGroupSetBits(gp_engine->event_group, EVENT_RESUME);
    return ESP_OK;
}

esp_err_t tf_engine_flow_set(const char *p_str, size_t len)
{
    assert(gp_engine);
    esp_err_t ret = ESP_OK;
    tf_flow_data_t flow;

    if( p_str == NULL || len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char *p_data = ( char *)tf_malloc(len);
    if( p_data == NULL ) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(p_data, p_str, len);

    flow.p_data = p_data;
    flow.len = len;

    if( xQueueSend(gp_engine->queue_handle, &flow, ( TickType_t )1000 ) != pdTRUE) {
        tf_free(p_data);
        return ESP_FAIL;
    }
    return ESP_OK;
}

char* tf_engine_flow_get(void)
{
    assert(gp_engine);
    char *p_json = NULL;
    __data_lock(gp_engine);
    if( gp_engine->cur_flow_root){
        p_json = cJSON_PrintUnformatted(gp_engine->cur_flow_root);
    }
    __data_unlock(gp_engine);
    return p_json;
}

char* tf_engine_flow_get_with_simplify(void)
{
    assert(gp_engine);
    char *p_json = NULL;
    char *p_json_simplify = NULL;
    p_json =  tf_engine_flow_get();
    if( p_json == NULL ) {
        return NULL;
    }
    p_json_simplify = tf_parse_util_simplify_json(p_json);
    tf_free(p_json);
    return p_json_simplify;
}

esp_err_t tf_engine_tid_get(intmax_t *p_tid)
{
    assert(gp_engine);
    __data_lock(gp_engine);
    *p_tid = gp_engine->tf_info.tid;
    __data_unlock(gp_engine);
    return ESP_OK; 
}
esp_err_t tf_engine_ctd_get(intmax_t *p_ctd)
{
    assert(gp_engine);
    __data_lock(gp_engine);
    *p_ctd = gp_engine->tf_info.ctd;
    __data_unlock(gp_engine);
    return ESP_OK; 
}
esp_err_t tf_engine_type_get(int *p_type)
{
    assert(gp_engine);
    __data_lock(gp_engine);
    *p_type = gp_engine->tf_info.type;
    __data_unlock(gp_engine);
    return ESP_OK; 
}
esp_err_t tf_engine_info_get(tf_info_t *p_info)
{
    assert(gp_engine);
    __data_lock(gp_engine);
    memcpy(p_info, &gp_engine->tf_info, sizeof(tf_info_t));
    p_info->p_tf_name = tf_strdup(gp_engine->tf_info.p_tf_name);
    __data_unlock(gp_engine);
    return ESP_OK;
}

esp_err_t tf_engine_status_get(int *p_status)
{
    assert(gp_engine);
    __data_lock(gp_engine);
    *p_status = gp_engine->status;
    __data_unlock(gp_engine);
    return ESP_OK;
}

esp_err_t tf_engine_status_cb_register(tf_engine_status_cb_t engine_status_cb, void *p_arg)
{
    assert(gp_engine);
    __data_lock(gp_engine);
    gp_engine->status_cb = engine_status_cb;
    gp_engine->p_status_cb_arg = p_arg;
    __data_unlock(gp_engine);
    return ESP_OK;
}

esp_err_t tf_module_status_set(const char *p_module_name, int status)
{
    assert(gp_engine);
    tf_module_status_cb_t  module_status_cb;
    void * p_module_status_cb_arg;

    __data_lock(gp_engine);
    module_status_cb = gp_engine->module_status_cb;
    p_module_status_cb_arg = gp_engine->p_module_status_cb_arg;
    __data_unlock(gp_engine);

    // call user callback
    if( module_status_cb == NULL ) {
        module_status_cb(p_module_status_cb_arg, p_module_name, status);
    }
    return ESP_OK;
}


esp_err_t tf_module_status_cb_register(tf_module_status_cb_t module_status_cb, void *p_arg)
{
    assert(gp_engine);
    __data_lock(gp_engine);
    gp_engine->module_status_cb = module_status_cb;
    gp_engine->p_module_status_cb_arg = p_arg;
    __data_unlock(gp_engine);
    return ESP_OK;
}
esp_err_t tf_module_register(const char *p_name,
                             const char *p_desc,
                             const char *p_version,
                             tf_module_mgmt_t *mgmt_handle)
{
    assert(gp_engine);
    esp_err_t ret = ESP_OK;

    if (p_name == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    __data_lock(gp_engine);
    if (!SLIST_EMPTY(&(gp_engine->module_nodes)))
    {
        tf_module_node_t *it = NULL;
        SLIST_FOREACH(it, &(gp_engine->module_nodes), next)
        {
            if (strcmp(it->p_name, p_name) == 0)
            {
                __data_unlock(gp_engine);
                ESP_LOGW(TAG, "module %s already exist", p_name);
                return ESP_ERR_INVALID_STATE;
            }
        }
    }

    tf_module_node_t *p_node = (tf_module_node_t *)tf_malloc(sizeof(tf_module_node_t));
    if (p_node == NULL)
    {
        __data_unlock(gp_engine);
        return ESP_ERR_NO_MEM;
    }

    p_node->p_name = p_name;
    p_node->p_desc = p_desc;
    p_node->p_version = p_version;
    p_node->mgmt_handle = mgmt_handle;
    SLIST_INSERT_HEAD(&(gp_engine->module_nodes), p_node, next);
    __data_unlock(gp_engine);

    ESP_LOGI(TAG, "module %s register success", p_name);
    return ESP_OK;
}

esp_err_t tf_modules_report(void)
{
    return ESP_OK;
}

esp_err_t tf_event_post(int32_t event_id,
                        const void *event_data,
                        size_t event_data_size,
                        TickType_t ticks_to_wait)
{
    assert(gp_engine);
    return esp_event_post_to(gp_engine->event_handle, TF_EVENT_BASE, event_id, event_data, event_data_size, ticks_to_wait);
}

esp_err_t tf_event_handler_register(int32_t event_id,
                                    esp_event_handler_t event_handler,
                                    void *event_handler_arg)
{
    assert(gp_engine);
    return esp_event_handler_register_with(gp_engine->event_handle, TF_EVENT_BASE, event_id, event_handler, event_handler_arg);
}

esp_err_t tf_event_handler_unregister(int32_t event_id,
                                      esp_event_handler_t event_handler)
{
    assert(gp_engine);
    return esp_event_handler_unregister_with(gp_engine->event_handle, TF_EVENT_BASE, event_id, event_handler);
}