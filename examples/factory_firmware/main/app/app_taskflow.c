#include "app_taskflow.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "event_loops.h"
#include "data_defs.h"
#include "storage.h"
#include "util.h"
#include "uuid.h"
#include "app_sensecraft.h"
#include "tf.h"
#include "tf_module_timer.h"
#include "tf_module_debug.h"
#include "tf_module_ai_camera.h"
#include "tf_module_img_analyzer.h"
#include "tf_module_local_alarm.h"
#include "tf_module_alarm_trigger.h"
#include "tf_module_sensecraft_alarm.h"
#include "tf_module_uart_alarm.h"
#include "tf_module_http_alarm.h"
#include "app_ota.h"

static const char *TAG = "taskflow";

#define TASK_FLOW_INFO_STORAGE   "taskflow-info"
#define TASK_FLOW_JSON_STORAGE   "taskflow-json"

#define TF_TYPE_LOCAL    0
#define TF_TYPE_MQTT     1
#define TF_TYPE_BLE      2
#define TF_TYPE_SR       3

#define TASK_FLOW_NULL  "{}"

struct app_taskflow *gp_taskflow = NULL;

const char local_taskflow_gesture[] = \
"{  \ 
	\"tlid\": 1,    \  
	\"ctd\": 1,    \  
	\"tn\": \"Local Gesture Detection\",    \  
	\"type\": 0,    \  
	\"task_flow\": [{    \  
		\"id\": 1,    \  
		\"type\": \"ai camera\",    \  
		\"index\": 0,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \ 
			\"model_type\": 3,    \  
			\"modes\": 0,    \
            \"model\": {    \
              \"arguments\": {  \
                \"iou\":45,     \
                \"conf\":65     \
              }   \
            },    \  
			\"conditions\": [{    \  
				\"class\": \"paper\",    \  
				\"mode\": 1,    \  
				\"type\": 2,    \  
				\"num\": 0    \  
			}],    \  
			\"conditions_combo\": 0,    \  
			\"silent_period\": {    \  
				\"silence_duration\": 5    \  
			},    \  
			\"output_type\": 0,    \  
			\"shutter\": 0    \  
		},    \  
		\"wires\": [    \  
			[2]    \  
		]    \  
	}, {    \  
		\"id\": 2,    \  
		\"type\": \"alarm trigger\",    \  
		\"index\": 1,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"text\": \"paper gesture detected\",    \  
			\"audio\": \"\"    \  
		},    \  
		\"wires\": [    \  
			[3, 4]    \ 
		]    \  
	}, {    \  
		\"id\": 3,    \  
		\"type\": \"local alarm\",    \  
		\"index\": 2,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"sound\": 1,    \  
			\"rgb\": 1,    \
			\"img\": 0,    \  
			\"text\": 0,    \    
			\"duration\": 1    \  
		},    \  
		\"wires\": []    \  
	}, {    \
        \"id\": 4,  \
        \"type\": \"sensecraft alarm\", \
        \"index\": 3,   \
        \"version\": \"1.0.0\", \
        \"params\": {   \
            \"silence_duration\": 30    \
        },  \
        \"wires\": []   \
    }   \
    ]   \  
}";

const char local_taskflow_pet[] = \
"{  \ 
	\"tlid\": 2,    \  
	\"ctd\": 2,    \  
	\"tn\": \"Local Pet Detection\",    \  
	\"type\": 0,    \  
	\"task_flow\": [{    \  
		\"id\": 1,    \  
		\"type\": \"ai camera\",    \  
		\"index\": 0,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"model_type\": 2,    \  
			\"modes\": 0,    \
            \"model\": {    \
              \"arguments\": {  \
                \"iou\":45,     \
                \"conf\":65     \
              }   \
            },    \    
			\"conditions\": [{    \  
				\"class\": \"dog\",    \  
				\"mode\": 1,    \  
				\"type\": 2,    \  
				\"num\": 0    \  
			},{    \  
				\"class\": \"cat\",    \  
				\"mode\": 1,    \  
				\"type\": 2,    \  
				\"num\": 0    \  
			}],    \  
			\"conditions_combo\": 1,    \  
			\"silent_period\": {    \  
				\"silence_duration\": 5    \  
			},    \  
			\"output_type\": 0,    \  
			\"shutter\": 0    \  
		},    \  
		\"wires\": [    \  
			[2]    \  
		]    \  
	}, {    \  
		\"id\": 2,    \  
		\"type\": \"alarm trigger\",    \  
		\"index\": 1,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"text\": \"dog or cat detected\",    \  
			\"audio\": \"\"    \  
		},    \  
		\"wires\": [    \  
			[3, 4]    \ 
		]    \  
	}, {    \  
		\"id\": 3,    \  
		\"type\": \"local alarm\",    \  
		\"index\": 2,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"sound\": 1,    \  
			\"rgb\": 1,    \
			\"img\": 0,    \  
			\"text\": 0,    \   
			\"duration\": 1    \  
		},    \  
		\"wires\": []    \  
	}, {    \
        \"id\": 4,  \
        \"type\": \"sensecraft alarm\", \
        \"index\": 3,   \
        \"version\": \"1.0.0\", \
        \"params\": {   \
            \"silence_duration\": 30    \
        },  \
        \"wires\": []   \
    }   \
    ]   \  
}";
const char local_taskflow_human[] = \
"{  \ 
	\"tlid\": 3,    \  
	\"ctd\": 3,    \  
	\"tn\": \"Local Human Detection\",    \  
	\"type\": 0,    \  
	\"task_flow\": [{    \  
		\"id\": 1,    \  
		\"type\": \"ai camera\",    \  
		\"index\": 0,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"model_type\": 1,    \  
			\"modes\": 0,    \
            \"model\": {    \
              \"arguments\": {  \
                \"iou\":45,     \
                \"conf\":50     \
              }   \
            },    \    
			\"conditions\": [{    \  
				\"class\": \"person\",    \  
				\"mode\": 1,    \  
				\"type\": 2,    \  
				\"num\": 0    \  
			}],    \  
			\"conditions_combo\": 0,    \  
			\"silent_period\": {    \  
				\"silence_duration\": 5    \  
			},    \  
			\"output_type\": 0,    \  
			\"shutter\": 0    \  
		},    \  
		\"wires\": [    \  
			[2]    \  
		]    \  
	}, {    \  
		\"id\": 2,    \  
		\"type\": \"alarm trigger\",    \  
		\"index\": 1,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"text\": \"human detected\",    \  
			\"audio\": \"\"    \  
		},    \  
		\"wires\": [    \  
			[3, 4]    \  
		]    \  
	}, {    \  
		\"id\": 3,    \  
		\"type\": \"local alarm\",    \  
		\"index\": 2,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"sound\": 1,    \  
			\"rgb\": 1,    \
			\"img\": 0,    \  
			\"text\": 0,    \  
			\"duration\": 1    \  
		},    \  
		\"wires\": []    \  
	}, {    \
        \"id\": 4,  \
        \"type\": \"sensecraft alarm\", \
        \"index\": 3,   \
        \"version\": \"1.0.0\", \
        \"params\": {   \
            \"silence_duration\": 30    \
        },  \
        \"wires\": []   \
    }   \
    ]    \  
}";

static void __data_lock(struct app_taskflow * p_taskflow )
{
    xSemaphoreTake(p_taskflow->sem_handle, portMAX_DELAY);
}
static void __data_unlock(struct app_taskflow * p_taskflow )
{
    xSemaphoreGive(p_taskflow->sem_handle);  
}

static void __report_lock(struct app_taskflow * p_taskflow )
{
    xSemaphoreTake(p_taskflow->report_sem_handle, portMAX_DELAY);
}
static void __report_unlock(struct app_taskflow * p_taskflow )
{
    xSemaphoreGive(p_taskflow->report_sem_handle);  
}

static void __task_flow_clean( void )
{
    esp_err_t ret = ESP_OK;

    struct app_taskflow_info info;
    info.len = 0;
    info.is_valid = false;
    ret = storage_write(TASK_FLOW_INFO_STORAGE, (void *)&info, sizeof(struct app_taskflow_info));
    if( ret != ESP_OK ) {
        ESP_LOGD(TAG, "taskflow info save err:%d", ret);
    } else {
        ESP_LOGD(TAG, "taskflow info save successful");
    }
}

static void __task_flow_save(const char *p_str, int len)
{
    esp_err_t ret = ESP_OK;

    __task_flow_clean();

    //save taskflow json
    ret = storage_write(TASK_FLOW_JSON_STORAGE, (void *)p_str, len);
    if( ret != ESP_OK ) {
        ESP_LOGD(TAG, "taskflow json save err:%d", ret);
        return;
    } else {
        ESP_LOGD(TAG, "taskflow json save successful");
    }

    // save taskflow info 
    struct app_taskflow_info info;
    info.len = len;
    info.is_valid = true;
    ret = storage_write(TASK_FLOW_INFO_STORAGE, (void *)&info, sizeof(struct app_taskflow_info));
    if( ret != ESP_OK ) {
        ESP_LOGD(TAG, "taskflow info save err:%d", ret);
    } else {
        ESP_LOGD(TAG, "taskflow info save successful");
    }
}

static void  __task_flow_restore(struct app_taskflow * p_taskflow)
{
    esp_err_t ret = ESP_OK;
    bool have_taskflow = false;
    struct app_taskflow_info info;
    size_t len = sizeof(info);
    ret = storage_read(TASK_FLOW_INFO_STORAGE, (void *)&info, &len);
    if( ret == ESP_OK  && len== (sizeof(info)) ) {
        ESP_LOGI(TAG, "Find taskflow info");
        if (info.is_valid && info.len > 0) {
            ESP_LOGI(TAG, "Need load taskflow...");
            char *p_json = psram_malloc(info.len);
            len = info.len;
            ret = storage_read(TASK_FLOW_JSON_STORAGE, (void *)p_json, &len);
            if( ret == ESP_OK ) {
                ESP_LOGI(TAG, "Start last taskflow");
                have_taskflow = true;
                tf_engine_flow_set(p_json, len);
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                    VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK, NULL, 0, portMAX_DELAY);
                                    
            } else {
                ESP_LOGE(TAG, "Faild to load taskflow json");
            }
            free(p_json);
        }
    }

    if( !have_taskflow ) {
        ESP_LOGI(TAG, "No taskflow, notify sensecraft");
        __data_lock(p_taskflow);
        p_taskflow->status_need_report = true;
        p_taskflow->p_taskflow_json = NULL;
        p_taskflow->status.tid = 0;
        p_taskflow->status.ctd = 0;
        p_taskflow->status.engine_status = TF_STATUS_IDLE;
        p_taskflow->status.module_status = 0;
        strncpy(p_taskflow->status.module_name, "unknown", sizeof(p_taskflow->status.module_name) - 1);
        __data_unlock(p_taskflow);
    }
}

static void __taskflow_reload_from_spiffs( struct app_taskflow * p_taskflow )
{
#define SPIFFS_TASKFLOW_FILE "/spiffs/taskflow.json"
    FILE *fp = fopen( SPIFFS_TASKFLOW_FILE, "r");
    if( fp != NULL ) {
        fseek(fp, 0, SEEK_END);
        int len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char *p_taskflow = psram_malloc(len+1);
        fread(p_taskflow, len, 1, fp);
        fclose(fp);
        ESP_LOGI(TAG, "taskflow load from SPIFFS success! %s", SPIFFS_TASKFLOW_FILE);

        tf_engine_flow_set(p_taskflow, len);
        free(p_taskflow);

        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                            VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK, NULL, 0, portMAX_DELAY);
    } else {
        ESP_LOGE(TAG, "taskflow load from SPIFFS fail:%s!", SPIFFS_TASKFLOW_FILE);
    }
}

static void __task_flow_status_cb(void *p_arg, intmax_t tid, int engine_status, const char *p_err_module)
{
    esp_err_t ret = ESP_OK;
    struct app_taskflow * p_taskflow = ( struct app_taskflow *)p_arg;
    struct view_data_taskflow_status status;
    bool need_report = false;
    bool need_notify_ui = false;
    char *p_module_name = NULL;
    char err_msg[64];
    char *p_json = NULL;
    intmax_t ctd = 0;

    tf_engine_ctd_get( &ctd );
    memset(&status, 0, sizeof(status));
    status.tid = tid;
    status.ctd = ctd;
    status.engine_status = engine_status;
    
    if( p_err_module != NULL ) {
        ESP_LOGI(TAG, "engine_status:%d, module:%s", engine_status, p_err_module);
        strncpy(status.module_name, p_err_module, sizeof(status.module_name) - 1);
        status.module_status = -1; // general error
        p_module_name = p_err_module;
    } else {
        ESP_LOGI(TAG, "engine_status:%d", engine_status);
        status.module_status = 0;  // no error
        strncpy(status.module_name, "unknown", sizeof(status.module_name) - 1);
        p_module_name = "unknown";
    }

    //notify ble
    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                    VIEW_EVENT_TASK_FLOW_STATUS, &status, sizeof(status), portMAX_DELAY);

    if( p_taskflow->mqtt_connect_flag ) {
        
        need_report = false;
        p_json = tf_engine_flow_get_with_simplify();
        if(  status.engine_status !=  TF_STATUS_STARTING) {

            
            __report_lock(p_taskflow); 
            if(  p_json != NULL ) {
                size_t len = 0;
                len = strlen(p_json);
                ret = app_sensecraft_mqtt_report_taskflow_info( tid, status.ctd,
                                                                    status.engine_status,
                                                                    p_module_name,
                                                                    status.module_status,
                                                                    p_json, len);
                free(p_json);
                p_json = NULL;

            } else {
                ret = app_sensecraft_mqtt_report_taskflow_status( tid, status.ctd,
                                                                    status.engine_status,
                                                                    p_module_name,
                                                                    status.module_status);
            }
            __report_unlock(p_taskflow);

            if( ret != ESP_OK ) {
                need_report = true;
                ESP_LOGW(TAG, "Failed to report taskflow ack status to MQTT server");
            } else {
                p_taskflow->report_cnt = 0;
            }

        }
    } else {
        need_report = true;
    }

    if(  need_report ) {
        p_json = tf_engine_flow_get_with_simplify();
        
        __data_lock(p_taskflow);
        if(p_taskflow->p_taskflow_json != NULL) {
            free(p_taskflow->p_taskflow_json);
            p_taskflow->p_taskflow_json = NULL;
        }
        p_taskflow->status_need_report = need_report;
        p_taskflow->p_taskflow_json = p_json;
        memcpy(&p_taskflow->status, &status, sizeof(struct view_data_taskflow_status));
        __data_unlock(p_taskflow);
    }

    memset(err_msg, 0, sizeof(err_msg));
    switch (engine_status)
    {
        case TF_STATUS_ERR_JSON_PARSE: {
            snprintf(err_msg, sizeof(err_msg) - 1, "Failed to parse json");
            need_notify_ui = true;
            break;
        }
        case TF_STATUS_ERR_MODULE_NOT_FOUND: {
            snprintf(err_msg, sizeof(err_msg) - 1, "[%s] not found", p_module_name);
            need_notify_ui = true;
            break;
        }
        case TF_STATUS_ERR_MODULES_INSTANCE: {
            snprintf(err_msg, sizeof(err_msg) - 1, "[%s] failed to create", p_module_name);
            need_notify_ui = true;
            break;
        }
        case TF_STATUS_ERR_MODULES_PARAMS: {
            snprintf(err_msg, sizeof(err_msg) - 1, "[%s]'s parameters error", p_module_name);
            need_notify_ui = true;
            break;
        }
        case TF_STATUS_ERR_MODULES_WIRES: {
            snprintf(err_msg, sizeof(err_msg) - 1, "[%s] failed to connect the next module", p_module_name);
            need_notify_ui = true;
            break;
        }
        case TF_STATUS_ERR_MODULES_START:{
            snprintf(err_msg, sizeof(err_msg) - 1, "[%s] failed to start", p_module_name);
            need_notify_ui = true;
            break;
        }
    default:
        break;
    }

    //notify UI
    if( need_notify_ui ) {
        ESP_LOGE(TAG, "Broken taskflow, clean it. err: %s", err_msg);
        __task_flow_clean();
        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                        VIEW_EVENT_TASK_FLOW_ERROR, err_msg, sizeof(err_msg), portMAX_DELAY);
    }
}


static void __task_flow_module_status_cb(void *p_arg, const char *p_name, int module_status)
{
    struct app_taskflow * p_taskflow = ( struct app_taskflow *)p_arg;
    struct view_data_taskflow_status status;
    bool need_report = false;
    intmax_t tid = 0;
    char *p_module_name = NULL;
    char err_msg[64];

    tf_engine_tid_get(&tid);

    memset(&status, 0, sizeof(status));
    status.tid = tid;
    status.engine_status = TF_STATUS_ERR_MODULES_INTERNAL; // module internal error
    
    if( p_name != NULL ) {
        ESP_LOGI(TAG, "module:%s, status:%d", p_name, module_status);
        strncpy(status.module_name, p_name, sizeof(status.module_name) - 1);
        status.module_status = module_status;
        p_module_name = p_name;
    } else {
        ESP_LOGI(TAG, "unknown module, status:%d", p_name, module_status);
        status.module_status = module_status;
        strncpy(status.module_name, "unknown", sizeof(status.module_name) - 1);
        p_module_name = "unknown";
    }

#if 0
    //notify UI and ble
    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                    VIEW_EVENT_TASK_FLOW_STATUS, &status, sizeof(status), portMAX_DELAY);

    if( p_taskflow->mqtt_connect_flag ) {
        need_report = false;
        app_sensecraft_mqtt_report_taskflow_module_status(tid, status.engine_status , p_module_name, status.module_status);
    } else {
        need_report = true;
    }

    __data_lock(p_taskflow);
    p_taskflow->status_need_report = need_report;
    memcpy(&p_taskflow->status, &status, sizeof(struct view_data_taskflow_status));
    __data_unlock(p_taskflow);

    if ( strcmp( p_module_name, TF_MODULE_AI_CAMERA_NAME) == 0 )
    {
        memset(err_msg, 0, sizeof(err_msg));
        snprintf(err_msg, sizeof(err_msg) - 1, "%s err:%d", TF_MODULE_AI_CAMERA_NAME, module_status);
        ESP_LOGE(TAG, "%s", err_msg);
        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                        VIEW_EVENT_TASK_FLOW_ERROR, err_msg, sizeof(err_msg), portMAX_DELAY);
    }
#endif

}

static void __taskflow_task(void *p_arg)
{
    struct app_taskflow * p_taskflow = ( struct app_taskflow *)p_arg;
    esp_err_t ret = ESP_OK;
    struct view_data_taskflow_status status;
    
    p_taskflow->report_cnt = 0;

    while(1) {
        
        if( p_taskflow->mqtt_connect_flag ) {

            if( p_taskflow->status_need_report ) {

                ESP_LOGI(TAG, "report cache taskflow status:%d", status.engine_status);

                __data_lock(p_taskflow);
                memcpy(&status, &p_taskflow->status, sizeof(struct view_data_taskflow_status));

                if( p_taskflow->p_taskflow_json != NULL ) {

                    __report_lock(p_taskflow);    
                    ret = app_sensecraft_mqtt_report_taskflow_info( status.tid, status.ctd,
                                                                    status.engine_status,
                                                                    status.module_name,
                                                                    status.module_status,
                                                                    p_taskflow->p_taskflow_json, strlen(p_taskflow->p_taskflow_json));
                    __report_unlock(p_taskflow);

                    if( ret != ESP_OK ) {
                        ESP_LOGW(TAG, "Failed to report taskflow ack status to MQTT server");
                    } else {
                        p_taskflow->status_need_report = false;
                        free(p_taskflow->p_taskflow_json);
                        p_taskflow->p_taskflow_json = NULL;
                    }
                } else {
                    __report_lock(p_taskflow);  
                    ret = app_sensecraft_mqtt_report_taskflow_status( status.tid, status.ctd, status.engine_status, 
                                                                            status.module_name,status.module_status);
                    __report_unlock(p_taskflow);
                    if( ret != ESP_OK ) {
                        ESP_LOGW(TAG, "Failed to report taskflow ack to MQTT server");
                    } else {
                        p_taskflow->status_need_report = false;
                    }
                }
                __data_unlock(p_taskflow);
                
                p_taskflow->report_cnt = 0; //reset cnt

            } else {
                
                // 3 min
                if ( p_taskflow->report_cnt  > 180 ) {
                    ESP_LOGI(TAG, "need report taskflow status");
                    intmax_t tlid = 0;
                    intmax_t ctd = 0;
                    int engine_status = 0;
                    struct view_data_taskflow_status status;

                    __report_lock(p_taskflow);
                    tf_engine_ctd_get( &ctd );
                    tf_engine_tid_get( &tlid );
                    tf_engine_status_get( &engine_status);
                    app_sensecraft_mqtt_report_taskflow_status( tlid, ctd, engine_status, NULL, 0);
                    __report_unlock(p_taskflow);
                    ESP_LOGI(TAG, "report taskflow status:%d", engine_status);

                    p_taskflow->report_cnt = 0;
                }
                p_taskflow->report_cnt++;
            }
        } 
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static esp_err_t __taskflow_tid_get(char *p_task_flow, intmax_t *p_tid, intmax_t *p_ctd)
{
    intmax_t tid = 0;
    intmax_t ctd = 0;
    cJSON *json_root = cJSON_Parse(p_task_flow);
    if( json_root == NULL) {
        ESP_LOGE(TAG, "Failed to parse cJSON");
        return ESP_FAIL;
    }
    cJSON *tlid_json = cJSON_GetObjectItem(json_root, "tlid");
    if (tlid_json == NULL || !cJSON_IsNumber(tlid_json))
    {
        ESP_LOGE(TAG, "tlid is not number");
    } else {
        tid = (intmax_t)tlid_json->valuedouble;
    }
    
    cJSON *ctd_json = cJSON_GetObjectItem(json_root, "ctd");
    if (ctd_json == NULL || !cJSON_IsNumber(ctd_json))
    {
        ESP_LOGE(TAG, "ctd is not number");
    } else {
        ctd = (intmax_t)ctd_json->valuedouble;
    }

    cJSON_Delete(json_root);
    *p_tid = tid;
    *p_ctd = ctd;
    return ESP_OK;
}

static void __view_event_handler(void* handler_args, 
                                 esp_event_base_t base, 
                                 int32_t id, 
                                 void* event_data)
{
    struct app_taskflow * p_taskflow = ( struct app_taskflow *)handler_args;

    switch (id)
    {
        case VIEW_EVENT_TASK_FLOW_STOP:
        {
            ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_STOP");
            int status = 0;
            esp_err_t ret = ESP_OK;
            tf_engine_status_get(&status);
            if( status == TF_STATUS_RUNNING  || status ==  TF_STATUS_STARTING || status ==  TF_STATUS_PAUSE) {
                tf_engine_stop();
                __task_flow_clean();
            } else {
                ESP_LOGI(TAG, "task flow already stopped");
                intmax_t tlid = 0;
                intmax_t ctd = 0;
                tf_engine_ctd_get( &ctd );
                tf_engine_tid_get( &tlid );
                __report_lock(p_taskflow);
                ret = app_sensecraft_mqtt_report_taskflow_status( tlid, ctd, status, NULL, 0);
                __report_unlock(p_taskflow);
                if( ret != ESP_OK ) {
                    ESP_LOGW(TAG, "Failed to report taskflow status to MQTT server");
                } else {
                    p_taskflow->report_cnt = 0; 
                }
            }
            break;
        }
        case VIEW_EVENT_TASK_FLOW_START_BY_LOCAL:
        {
            esp_err_t ret = ESP_OK;
            uint32_t *p_tf_num = (uint32_t *)event_data;
            const char *p_task_flow = NULL;
            ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_START_BY_LOCAL:%d", *p_tf_num);

            switch (*p_tf_num)
            {
                case 0: {
                    p_task_flow = local_taskflow_gesture;
                    break;
                }
                case 1: {
                    p_task_flow = local_taskflow_pet;
                    break;
                }
                case 2: {
                    p_task_flow = local_taskflow_human;
                    break;
                }
                default:
                    break;
            }
            if( p_task_flow ) {
                char uuid[37];
                size_t len = strlen(p_task_flow);
                UUIDGen(uuid);

                tf_engine_flow_set(p_task_flow, len);

                if( p_taskflow->mqtt_connect_flag ) {
                    intmax_t tid = 0;
                    intmax_t ctd = 0;
                    __report_lock(p_taskflow);
                    __taskflow_tid_get(p_task_flow, &tid, &ctd);
                    ret = app_sensecraft_mqtt_report_taskflow_status(tid, ctd, TF_STATUS_STARTING, NULL, 0);
                    __report_unlock(p_taskflow);
                    if( ret != ESP_OK ) {
                        ESP_LOGW(TAG, "Failed to report taskflow ack to MQTT server");
                    } else {
                        p_taskflow->report_cnt = 0; 
                    }
                }

            }
            break;
        }
        case VIEW_EVENT_OTA_STATUS: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_OTA_STATUS");
            struct view_data_ota_status * p_ota_st = (struct view_data_ota_status *)event_data;
            
            if( SENSECRAFT_OTA_STATUS_UPGRADING  == p_ota_st->status) {
                if( !p_taskflow->need_pause_taskflow ) {
                    ESP_LOGI(TAG, "taskflow need pause");
                    p_taskflow->need_pause_taskflow = true;
                    tf_engine_pause();
                }
            } else if ( SENSECRAFT_OTA_STATUS_FAIL == p_ota_st->status) {
                ESP_LOGI(TAG, "ota fail");
                tf_engine_resume(); //maybe need resume
            }            
            break;
        }
        case VIEW_EVENT_TASK_FLOW_PAUSE: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_PAUSE");
            tf_engine_pause();
            break;
        }
        case VIEW_EVENT_TASK_FLOW_RESUME: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_RESUME");
            tf_engine_resume();
            break;
        }
        default:
            break;
    }

}

static int __taskflow_stop_check(struct app_taskflow * p_taskflow, char *p_task_flow)
{
    bool need_stop = false;
    cJSON *json_root = cJSON_Parse(p_task_flow);
    if( json_root == NULL) {
        ESP_LOGE(TAG, "Failed to parse cJSON");
        free(p_task_flow);
        return -1;
    }

    cJSON *task_flow_json = cJSON_GetObjectItem(json_root, "task_flow");
    if( task_flow_json == NULL) {
        need_stop = true;
    } else {
        need_stop = false;
    }
    if( need_stop) {
        free(p_task_flow);
        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_TASK_FLOW_STOP, NULL, NULL, pdMS_TO_TICKS(10000));
    }
    cJSON_Delete(json_root);

    if ( need_stop) {
        return 1;
    } else {
        return 0;
    }
}

static void __taskflow_start(struct app_taskflow * p_taskflow, char *p_task_flow)
{

    esp_err_t ret = ESP_OK;
    size_t len = strlen(p_task_flow);
    char uuid[37];
    UUIDGen(uuid);

    tf_engine_flow_set(p_task_flow, len);
    __task_flow_save(p_task_flow, len); 

    if( p_taskflow->mqtt_connect_flag ) {
        intmax_t tid = 0;
        intmax_t ctd = 0;
        __report_lock(p_taskflow);
        __taskflow_tid_get(p_task_flow, &tid, &ctd);
        ret = app_sensecraft_mqtt_report_taskflow_status(tid, ctd, TF_STATUS_STARTING, NULL, 0);
        __report_unlock(p_taskflow);
        if( ret != ESP_OK ) {
            ESP_LOGW(TAG, "Failed to report taskflow ack to MQTT server");
        } else {
            p_taskflow->report_cnt = 0; 
        }
    }

    free(p_task_flow);

    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                            VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK, NULL, 0, portMAX_DELAY);
}

static void __ctrl_event_handler(void* handler_args, 
                                 esp_event_base_t base, 
                                 int32_t id, 
                                 void* event_data)
{
    struct app_taskflow * p_taskflow = ( struct app_taskflow *)handler_args;

    switch (id)
    {
        case CTRL_EVENT_MQTT_CONNECTED: {
            p_taskflow->mqtt_connect_flag = true;
            break;
        }
        case CTRL_EVENT_MQTT_DISCONNECTED: {
            p_taskflow->mqtt_connect_flag = false;
            break;
        }
        case CTRL_EVENT_OTA_AI_MODEL: {
            ESP_LOGI(TAG, "event: CTRL_EVENT_OTA_AI_MODEL");
            struct view_data_ota_status * ota_st = (struct view_data_ota_status *)event_data;
            esp_err_t ret = ESP_OK;
            intmax_t tlid = 0;
            intmax_t ctd = 0;
            tf_engine_ctd_get( &ctd );
            tf_engine_tid_get( &tlid );
            if(ota_st->status == 0) {
                ESP_LOGI(TAG, "model ota succeed.");
            } else if (ota_st->status == 1) {
                ESP_LOGI(TAG, "report model ota percentage: %d", ota_st->percentage);
            } else {
                ESP_LOGE(TAG, "model ota failed, error code: %d", ota_st->err_code);
            }
            ret = app_sensecraft_mqtt_report_taskflow_model_ota_status(tlid,ctd, ota_st->status, ota_st->percentage, ota_st->err_code );
            if( ret != ESP_OK ) {
                ESP_LOGW(TAG, "Failed to report taskflow model ota status to MQTT server");
            }
            break;
        }
        case CTRL_EVENT_TASK_FLOW_STATUS_REPORT: {
            ESP_LOGI(TAG, "event: CTRL_EVENT_TASK_FLOW_STATUS_REPORT");
            char * p_json = NULL;
            intmax_t tlid = 0;
            intmax_t ctd = 0;
            int engine_status = 0;
            struct view_data_taskflow_status status;

            tf_engine_ctd_get( &ctd );
            tf_engine_tid_get( &tlid );
            tf_engine_status_get( &engine_status);

            memset(&status,0, sizeof(struct view_data_taskflow_status));
            status.tid = tlid;
            status.ctd = ctd;
            status.engine_status = engine_status;
            status.module_status = 0; // don't care module status.
            strncpy(status.module_name, "unknown", sizeof(status.module_name) - 1); // don't care module name.
 
            p_json = tf_engine_flow_get_with_simplify();
            
            __data_lock(p_taskflow);
            if(p_taskflow->p_taskflow_json != NULL) {
                free(p_taskflow->p_taskflow_json);
                p_taskflow->p_taskflow_json = NULL;
            }
            p_taskflow->status_need_report = true;
            p_taskflow->p_taskflow_json = p_json;
            memcpy(&p_taskflow->status, &status, sizeof(struct view_data_taskflow_status));
            __data_unlock(p_taskflow);

            break;
        }
        
        case CTRL_EVENT_TASK_FLOW_START_BY_MQTT:{
            ESP_LOGI(TAG, "event: CTRL_EVENT_TASK_FLOW_START_BY_MQTT");
            char *p_task_flow = *(char **)event_data;
            size_t len = strlen(p_task_flow);
            printf("taskflow:%s\r\n", p_task_flow);
            
            tf_engine_flow_set(p_task_flow, len);
            __task_flow_save(p_task_flow, len ); 
            free(p_task_flow);
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                    VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK, NULL, 0, portMAX_DELAY);
            
            break;
        }
        case CTRL_EVENT_TASK_FLOW_START_BY_BLE: {
            ESP_LOGI(TAG, "event: CTRL_EVENT_TASK_FLOW_START_BY_BLE");
            char *p_task_flow_str = *(char **)event_data;
            int stop_flag = 0;
            if(p_task_flow_str == NULL) {
                ESP_LOGE(TAG, "BLE taskflow is NULL");
                break;
            }
            stop_flag = __taskflow_stop_check(p_taskflow, p_task_flow_str);
            if( stop_flag == -1 ) {
                ESP_LOGI(TAG, "BLE taskflow data error");
                break;
            } else if (stop_flag == 1) {
                ESP_LOGI(TAG, "BLE taskflow stop");
                break;
            }
            __taskflow_start(p_taskflow, p_task_flow_str);
            break;
        }
        case CTRL_EVENT_TASK_FLOW_START_BY_SR:
        {
            ESP_LOGI(TAG, "event: CTRL_EVENT_TASK_FLOW_START_BY_SR");            
            char *p_task_flow_str = *(char **)event_data;
            int stop_flag = 0;
            if(p_task_flow_str == NULL) {
                ESP_LOGE(TAG, "SR taskflow is NULL");
                break;
            }
            stop_flag = __taskflow_stop_check(p_taskflow, p_task_flow_str);
            if( stop_flag == -1 ) {
                ESP_LOGI(TAG, "SR taskflow data error");
                break;
            } else if (stop_flag == 1) {
                ESP_LOGI(TAG, "SR taskflow stop");
                break;
            }
            __taskflow_start(p_taskflow, p_task_flow_str);
            break;
        }
        case CTRL_EVENT_TASK_FLOW_START_BY_CMD: {
            ESP_LOGI(TAG, "event: CTRL_EVENT_TASK_FLOW_START_BY_CMD");            
            char *p_task_flow_str = *(char **)event_data;
            int stop_flag = 0;
            if(p_task_flow_str == NULL) {
                ESP_LOGE(TAG, "CMD taskflow is NULL");
                break;
            }
            stop_flag = __taskflow_stop_check(p_taskflow, p_task_flow_str);
            if( stop_flag == -1 ) {
                ESP_LOGI(TAG, "CMD taskflow data error");
                break;
            } else if (stop_flag == 1) {
                ESP_LOGI(TAG, "CMD taskflow stop");
                break;
            }
            __taskflow_start(p_taskflow, p_task_flow_str);
            break;
        }
        case CTRL_EVENT_LOCAL_SVC_CFG_TASK_FLOW: {
            ESP_LOGI(TAG, "event: CTRL_EVENT_LOCAL_SVC_CFG_TASK_FLOW");
            tf_engine_restart();
            break;
        }

        default:
            break;
    }
}


static  void taskflow_engine_module_init( struct app_taskflow * p_taskflow)
{
    ESP_ERROR_CHECK(tf_engine_init());
    ESP_ERROR_CHECK(tf_module_timer_register());
    ESP_ERROR_CHECK(tf_module_debug_register());
    ESP_ERROR_CHECK(tf_module_ai_camera_register());
    ESP_ERROR_CHECK(tf_module_img_analyzer_register());
    ESP_ERROR_CHECK(tf_module_local_alarm_register());
    ESP_ERROR_CHECK(tf_module_alarm_trigger_register());
    ESP_ERROR_CHECK(tf_module_sensecraft_alarm_register());
    ESP_ERROR_CHECK(tf_module_uart_alarm_register());
    ESP_ERROR_CHECK(tf_module_http_alarm_register());
    //add more module

    ESP_ERROR_CHECK(tf_engine_status_cb_register(__task_flow_status_cb, p_taskflow));
    ESP_ERROR_CHECK(tf_module_status_cb_register(__task_flow_module_status_cb, p_taskflow));
}

esp_err_t app_taskflow_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    esp_err_t ret = ESP_OK;
    struct app_taskflow * p_taskflow = NULL;
    gp_taskflow = (struct app_taskflow *) psram_malloc(sizeof(struct app_taskflow));
    if (gp_taskflow == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    p_taskflow = gp_taskflow;
    memset(p_taskflow, 0, sizeof( struct app_taskflow ));

    // engine init and module register
    taskflow_engine_module_init(p_taskflow);

    p_taskflow->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_taskflow->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_taskflow->report_sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_taskflow->report_sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_taskflow->p_task_stack_buf = (StackType_t *)psram_malloc(TASKFLOW_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_taskflow->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_taskflow->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_taskflow->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_taskflow->task_handle = xTaskCreateStatic(__taskflow_task,
                                                "app_taskflow",
                                                TASKFLOW_TASK_STACK_SIZE,
                                                (void *)p_taskflow,
                                                TASKFLOW_TASK_PRIO,
                                                p_taskflow->p_task_stack_buf,
                                                p_taskflow->p_task_buf);
    ESP_GOTO_ON_FALSE(p_taskflow->task_handle, ESP_FAIL, err, TAG, "Failed to create task");


    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        VIEW_EVENT_BASE, 
                                                        VIEW_EVENT_TASK_FLOW_STOP, 
                                                        __view_event_handler, 
                                                        p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        VIEW_EVENT_BASE, 
                                                        VIEW_EVENT_TASK_FLOW_START_BY_LOCAL, 
                                                        __view_event_handler, 
                                                        p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_OTA_STATUS, 
                                                    __view_event_handler, 
                                                    p_taskflow));
    
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_TASK_FLOW_PAUSE, 
                                                    __view_event_handler, 
                                                    p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_TASK_FLOW_RESUME, 
                                                    __view_event_handler, 
                                                    p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_TASK_FLOW_STATUS_REPORT, 
                                                        __ctrl_event_handler, 
                                                        p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_TASK_FLOW_START_BY_MQTT, 
                                                        __ctrl_event_handler, 
                                                        p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_TASK_FLOW_START_BY_BLE, 
                                                        __ctrl_event_handler, 
                                                        p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_TASK_FLOW_START_BY_SR, 
                                                        __ctrl_event_handler, 
                                                        p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_TASK_FLOW_START_BY_CMD, 
                                                        __ctrl_event_handler, 
                                                        p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_OTA_AI_MODEL, 
                                                        __ctrl_event_handler, 
                                                        p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_MQTT_CONNECTED, 
                                                        __ctrl_event_handler, 
                                                        p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    CTRL_EVENT_BASE, 
                                                    CTRL_EVENT_MQTT_DISCONNECTED, 
                                                    __ctrl_event_handler, 
                                                    p_taskflow));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    CTRL_EVENT_BASE, 
                                                    CTRL_EVENT_LOCAL_SVC_CFG_TASK_FLOW, 
                                                    __ctrl_event_handler,
                                                    p_taskflow));

    p_taskflow->mqtt_connect_flag = app_sensecraft_is_connected(); // Update connection flags.

#if CONFIG_ENABLE_TASKFLOW_FROM_SPIFFS
    __taskflow_reload_from_spiffs(p_taskflow);
#else
    __task_flow_restore(p_taskflow);
#endif

    return ESP_OK; 
err:
    if(p_taskflow->task_handle ) {
        vTaskDelete(p_taskflow->task_handle);
        p_taskflow->task_handle = NULL;
    }
    if( p_taskflow->p_task_stack_buf ) {
        free(p_taskflow->p_task_stack_buf);
        p_taskflow->p_task_stack_buf = NULL;
    }
    if( p_taskflow->p_task_buf ) {
        free(p_taskflow->p_task_buf);
        p_taskflow->p_task_buf = NULL;
    }
    if (p_taskflow->sem_handle) {
        vSemaphoreDelete(p_taskflow->sem_handle);
        p_taskflow->sem_handle = NULL;
    }
    if (p_taskflow->report_sem_handle) {
        vSemaphoreDelete(p_taskflow->report_sem_handle);
        p_taskflow->report_sem_handle = NULL;
    }

    if (p_taskflow) {
        free(p_taskflow);
        p_taskflow = NULL;
    }
    return ret;
}