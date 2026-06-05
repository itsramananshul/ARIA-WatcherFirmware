#include <stdio.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "cJSON.h"

#include "tf.h"
#include "tf_util.h"
#include "tf_module_uart_alarm.h"
#include "tf_module_util.h"
#include "util.h"


static const char *TAG = "tfm.uart_alarm";
static volatile atomic_int g_ins_cnt = ATOMIC_VAR_INIT(0);


static void __event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    tf_module_uart_alarm_t *p_module_ins = (tf_module_uart_alarm_t *)handler_args;
   
    uint32_t type = ((uint32_t *)p_event_data)[0];
    if( type !=  TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE_AUDIO_TEXT) {
        ESP_LOGW(TAG, "unsupported type %d", type);
        tf_data_free(p_event_data);
        return;
    }

    tf_data_dualimage_with_audio_text_t *p_data = (tf_data_dualimage_with_audio_text_t*)p_event_data;
    uint32_t total_len = 0;
    uint8_t *buffer = NULL;
    cJSON *json = NULL;

    //prompt
    tf_info_t tf_info;
    memset(&tf_info, 0, sizeof(tf_info_t));
    char *prompt = NULL;
    if (p_module_ins->text != NULL && strlen(p_module_ins->text) > 0) {
        prompt = p_module_ins->text;
    } else {
        tf_engine_info_get(&tf_info);
        prompt = tf_info.p_tf_name;
        if ( prompt == NULL ){
            prompt = "";
        }
    }
    if (p_module_ins->output_format == 0) {
        //binary output
        uint32_t prompt_len = strlen(prompt);
        buffer = psram_calloc(1, prompt_len + 4);
        memcpy(buffer, &prompt_len, 4);
        memcpy(buffer + 4, prompt, prompt_len);
        total_len += prompt_len + 4;
    } else {
        //json output
        json = cJSON_CreateObject();
        cJSON_AddItemToObject(json, "prompt", cJSON_CreateString(prompt));
    }
    
    if( tf_info.p_tf_name ) {
        free(tf_info.p_tf_name);
    }

    //big image
    if (p_module_ins->include_big_image) {
        ESP_LOGI(TAG, "include_big_image: %d", (int)p_module_ins->include_big_image);
        if (p_module_ins->output_format == 0) {
            //binary output
            uint32_t big_image_len = p_data->img_large.len;
            buffer = psram_realloc(buffer, total_len + big_image_len + 4);
            memcpy(buffer + total_len, &big_image_len, 4);
            memcpy(buffer + total_len + 4, p_data->img_large.p_buf, big_image_len);
            total_len += big_image_len + 4;
        } else {
            //json output
            cJSON_AddItemToObject(json, "big_image", cJSON_CreateString((char *)p_data->img_large.p_buf));
        }
    } else if( p_module_ins->output_format == 0 ) {
        uint32_t big_image_len = 0;
        buffer = psram_realloc(buffer, total_len  + 4);
        memcpy(buffer + total_len, &big_image_len, 4);
        total_len+=4;
    } 

    //small image
    if (p_module_ins->include_small_image) {
        ESP_LOGI(TAG, "include_small_image: %d", (int)p_module_ins->include_small_image);
        if (p_module_ins->output_format == 0) {
            //binary output
            uint32_t small_image_len = p_data->img_small.len;
            buffer = psram_realloc(buffer, total_len + small_image_len + 4);
            memcpy(buffer + total_len, &small_image_len, 4);
            memcpy(buffer + total_len + 4, p_data->img_small.p_buf, small_image_len);
            total_len += small_image_len + 4;
        } else {
            //json output
            cJSON_AddItemToObject(json, "small_image", cJSON_CreateString((char *)p_data->img_small.p_buf));
        }
    } else if( p_module_ins->output_format == 0 ) {
        uint32_t small_image_len = 0;
        buffer = psram_realloc(buffer, total_len  + 4);
        memcpy(buffer + total_len, &small_image_len, 4);
        total_len+=4;
    } 

    //inference
    if (p_data->inference.is_valid) {

        if (p_module_ins->output_format == 0) {

            switch (p_data->inference.type)
            {
                case INFERENCE_TYPE_BOX:
                {
                    uint8_t inference_type = 1;
                    uint32_t boxes_cnt =  p_data->inference.cnt ;
                    buffer = psram_realloc(buffer, total_len + boxes_cnt*10 + 4 + 1);
                    memcpy(buffer + total_len, &inference_type, 1);
                    memcpy(buffer + total_len + 1 , &boxes_cnt, 4);
                    for (size_t i = 0; i < boxes_cnt; i++)
                    {
                        sscma_client_box_t *p_box =  &p_data->inference.p_data[i];
                        uint16_t x = p_box->x;
                        uint16_t y = p_box->y;
                        uint16_t w = p_box->w;
                        uint16_t h = p_box->h;
                        uint8_t score = p_box->score;
                        uint8_t target = p_box->target;

                        memcpy(buffer + total_len + 5 + i*10 + 0, &x, 2);
                        memcpy(buffer + total_len + 5 + i*10 + 2, &y, 2);
                        memcpy(buffer + total_len + 5 + i*10 + 4, &w, 2);
                        memcpy(buffer + total_len + 5 + i*10 + 6, &h, 2);
                        memcpy(buffer + total_len + 5 + i*10 + 8, &score, 1);
                        memcpy(buffer + total_len + 5 + i*10 + 9, &target, 1);
                    }
                    total_len +=  boxes_cnt*10 + 4 + 1;
                    break;
                }
                case INFERENCE_TYPE_CLASS:
                {
                    uint8_t inference_type = 2;
                    uint32_t classes_cnt =  p_data->inference.cnt ;
                    buffer = psram_realloc(buffer, total_len + classes_cnt*2 + 4 + 1);
                    memcpy(buffer + total_len, &inference_type, 1);
                    memcpy(buffer + total_len + 1 , &classes_cnt, 4);
                    for (size_t i = 0; i < classes_cnt; i++)
                    {
                        sscma_client_class_t *p_class =  &p_data->inference.p_data[i];
                        uint8_t score = p_class->score;
                        uint8_t target = p_class->target;
                        memcpy(buffer + total_len + 5 + i*2 + 0, &score, 1);
                        memcpy(buffer + total_len + 5 + i*2 + 1, &target, 1);
                    }
                    total_len +=  classes_cnt*10 + 4 + 1;
                    break;
                }
               default:
                    uint8_t inference_type = 3;
                    uint32_t cnt = 0;
                    buffer = psram_realloc(buffer, total_len + 4 + 1);
                    memcpy(buffer + total_len, &inference_type, 1);
                    memcpy(buffer + total_len + 1 , &cnt, 4);
                    ESP_LOGE(TAG, "unsupport inference type: %d", p_data->inference.type);
                    break;
            }
            uint32_t name_cnt = 0;
            int classes_name_len = 0; 
            for (size_t i = 0; p_data->inference.classes[i] != NULL; i++)
            {
                name_cnt++;
                classes_name_len += strlen(p_data->inference.classes[i]) + 1;
            }
            
            buffer = psram_realloc(buffer, total_len + classes_name_len + 4);
            memcpy(buffer + total_len, &name_cnt, 4);
            
            int index = 4; 
            for (size_t i = 0; p_data->inference.classes[i] != NULL; i++)
            {
                strcpy((char *)(buffer + total_len + index), p_data->inference.classes[i]);
                index += strlen(p_data->inference.classes[i]);
                buffer[total_len + index] = '\0';
                index += 1;
            }
        
            total_len +=  (classes_name_len + 4);
        } else {

            cJSON *inference = cJSON_CreateObject();
            cJSON_AddItemToObject(json, "inference", inference);

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
    } else if( p_module_ins->output_format == 0 ) {
        uint8_t inference_type = 0;
        buffer = psram_realloc(buffer, total_len  + 1);
        memcpy(buffer + total_len, &inference_type, 1);
        total_len+=1;
    } 

    //output the packet
    if (p_module_ins->output_format == 0) {
        const char *header = "SEEED";
        uart_write_bytes(UART_NUM_2, header, strlen(header));
        ESP_LOGD(TAG, "uart magic header sent, output_format=%d", p_module_ins->output_format);
        uart_write_bytes(UART_NUM_2, buffer, total_len);
        free(buffer);
    } else {
        char *str = cJSON_PrintUnformatted(json);
        total_len = strlen(str);
        ESP_LOGD(TAG, "output json:\n%s\ntotal_len=%d", str, total_len);
        uart_write_bytes(UART_NUM_2, str, total_len);
        uart_write_bytes(UART_NUM_2, "\r\n", 2);
        free(str);
        cJSON_Delete(json);
    }

    // data is used up, consumer frees it
    tf_data_free(p_event_data);
}

/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    tf_module_uart_alarm_t *p_module_ins = (tf_module_uart_alarm_t *)p_module;
    return 0;
}

static int __stop(void *p_module)
{
    tf_module_uart_alarm_t *p_module_ins = (tf_module_uart_alarm_t *)p_module;
    if (p_module_ins->text != NULL) {
        tf_free(p_module_ins->text);
    }
    return tf_event_handler_unregister(p_module_ins->input_evt_id, __event_handler);
}

static int __cfg(void *p_module, cJSON *p_json)
{
    tf_module_uart_alarm_t *p_module_ins = (tf_module_uart_alarm_t *)p_module;

    cJSON *output_format = cJSON_GetObjectItem(p_json, "output_format");
    if (output_format == NULL || !cJSON_IsNumber(output_format))
    {
        ESP_LOGE(TAG, "params output_format missing, default 0 (binary output)");
        p_module_ins->output_format = 1;
    } else {
        ESP_LOGI(TAG, "params output_format=%d", output_format->valueint);
        p_module_ins->output_format = output_format->valueint;
    }

    cJSON *text = cJSON_GetObjectItem(p_json, "text");
    if (text == NULL || !cJSON_IsString(text))
    {
        ESP_LOGE(TAG, "params text missing, default NULL");
        p_module_ins->text = NULL;
    } else {
        ESP_LOGI(TAG, "params text=%s", text->valuestring);
        p_module_ins->text = strdup_psram(text->valuestring);
    }

    cJSON *include_big_image = cJSON_GetObjectItem(p_json, "include_big_image");
    if (include_big_image == NULL || !tf_cJSON_IsGeneralBool(include_big_image))
    {
        ESP_LOGE(TAG, "params include_big_image missing, default false");
        p_module_ins->include_big_image = false;
    } else {
        ESP_LOGI(TAG, "params include_big_image=%s", tf_cJSON_IsGeneralTrue(include_big_image)?"true":"false");
        p_module_ins->include_big_image = tf_cJSON_IsGeneralTrue(include_big_image);
    }

    cJSON *include_small_image = cJSON_GetObjectItem(p_json, "include_small_image");
    if (include_small_image == NULL || !tf_cJSON_IsGeneralBool(include_small_image))
    {
        ESP_LOGE(TAG, "params include_small_image missing, default false");
        p_module_ins->include_small_image = false;
    } else {
        ESP_LOGI(TAG, "params include_small_image=%s", tf_cJSON_IsGeneralTrue(include_small_image)?"true":"false");
        p_module_ins->include_small_image = tf_cJSON_IsGeneralTrue(include_small_image);
    }

    return 0;
}

static int __msgs_sub_set(void *p_module, int evt_id)
{
    tf_module_uart_alarm_t *p_module_ins = (tf_module_uart_alarm_t *)p_module;
    p_module_ins->input_evt_id = evt_id;
    return tf_event_handler_register(evt_id, __event_handler, p_module_ins);
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    tf_module_uart_alarm_t *p_module_ins = (tf_module_uart_alarm_t *)p_module;
    if ( num > 0) {
        ESP_LOGW(TAG, "this module should have no output");
    }
    return 0;
}

const static struct tf_module_ops  __g_module_ops = {
    .start = __start,
    .stop = __stop,
    .cfg = __cfg,
    .msgs_sub_set = __msgs_sub_set,
    .msgs_pub_set = __msgs_pub_set
};

/*************************************************************************
 * API
 ************************************************************************/

tf_module_t *tf_module_uart_alarm_instance(void)
{
    tf_module_uart_alarm_t *p_module_ins = (tf_module_uart_alarm_t *) tf_malloc(sizeof(tf_module_uart_alarm_t));
    if (p_module_ins == NULL)
    {
        return NULL;
    }
    p_module_ins->module_base.p_module = p_module_ins;
    p_module_ins->module_base.ops = &__g_module_ops;

    if (atomic_fetch_add(&g_ins_cnt, 1) == 0) {
        // the 1st time instance, we should init the hardware
        esp_err_t ret;
        uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        };
        const int buffer_size = 2 * 1024;
        ESP_GOTO_ON_ERROR(uart_param_config(UART_NUM_2, &uart_config), err, TAG, "uart_param_config failed");
        ESP_GOTO_ON_ERROR(uart_set_pin(UART_NUM_2, GPIO_NUM_19/*TX*/, GPIO_NUM_20/*RX*/, -1, -1), err, TAG, "uart_set_pin failed");
        ESP_GOTO_ON_ERROR(uart_driver_install(UART_NUM_2, buffer_size, buffer_size, 0, NULL, ESP_INTR_FLAG_SHARED), err, TAG, "uart_driver_install failed");
        ESP_LOGI(TAG, "uart driver is installed.");
    }

    return &p_module_ins->module_base;

err:
    free(p_module_ins);
    return NULL;
}

void tf_module_uart_alarm_destroy(tf_module_t *p_module_base)
{
    if (p_module_base) {
        if (atomic_fetch_sub(&g_ins_cnt, 1) <= 1) {
            // this is the last destroy call, de-init the uart
            uart_driver_delete(UART_NUM_2);
            ESP_LOGI(TAG, "uart driver is deleted.");
        }
        if (p_module_base->p_module) {
            free(p_module_base->p_module);
        }
    }
}

const static struct tf_module_mgmt __g_module_management = {
    .tf_module_instance = tf_module_uart_alarm_instance,
    .tf_module_destroy = tf_module_uart_alarm_destroy,
};

esp_err_t tf_module_uart_alarm_register(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    return tf_module_register(TF_MODULE_UART_ALARM_NAME,
                              TF_MODULE_UART_ALARM_DESC,
                              TF_MODULE_UART_ALARM_VERSION,
                              &__g_module_management);
}