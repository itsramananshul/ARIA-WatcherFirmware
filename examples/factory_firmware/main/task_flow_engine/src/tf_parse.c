#include "tf_parse.h"
#include <string.h>
#include "tf_util.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "tf.parse";

static void remove_audio_from_alarm_trigger(cJSON *json) {
    cJSON *task_flow = cJSON_GetObjectItem(json, "task_flow");
    if (!cJSON_IsArray(task_flow)) {
        return;
    }
    // Iterate through the task_flow array
    cJSON *module = NULL;
    cJSON_ArrayForEach(module, task_flow) {
        cJSON *type = cJSON_GetObjectItem(module, "type");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "alarm trigger") == 0) {
            // Get the params object
            cJSON *params = cJSON_GetObjectItem(module, "params");
            if (cJSON_IsObject(params)) {
                // Delete the audio field
                cJSON_DeleteItemFromObject(params, "audio");
            }
        }
    }
}

static void __module_item_free(tf_module_item_t *p_item, int num)
{
    for (int i = 0; i < num; i++)
    {
        int output_port_num = p_item[i].output_port_num;
        for (int j = 0; j < output_port_num; j++)
        {
            if (p_item[i].p_wires[j].p_evt_id != NULL)
            {
                tf_free(p_item[i].p_wires[j].p_evt_id);
            }
        }
        if (p_item[i].p_wires != NULL)
        {
            tf_free(p_item[i].p_wires);
        }
    }
}

int tf_parse_json_with_length(const char *p_str, size_t len,
                              cJSON **pp_json_root,
                              tf_module_item_t **pp_head,
                              tf_info_t *p_info)
{
    esp_err_t ret = ESP_OK;

    *pp_json_root = NULL;
    *pp_head = NULL;

    cJSON *p_json_root = NULL;
    cJSON *p_tasklist = NULL;

    cJSON *p_tlid = NULL;
    cJSON *p_ctd = NULL;
    cJSON *p_tn = NULL;
    cJSON *p_tf_type = NULL;

    tf_module_item_t *p_list_head = NULL;

    int module_item_num = 0;

    p_json_root = cJSON_ParseWithLength(p_str, len);
    ESP_GOTO_ON_FALSE(p_json_root, ESP_ERR_INVALID_ARG, err, TAG, "json parse failed");


    p_tf_type = cJSON_GetObjectItem(p_json_root, "type");
    if (p_tf_type == NULL || !cJSON_IsNumber(p_tf_type))
    {
        ESP_LOGE(TAG, "type is not number");
        goto err;
    } else {
        p_info->type = p_tf_type->valueint;
    }

    p_tlid = cJSON_GetObjectItem(p_json_root, "tlid");
    if (p_tlid == NULL || !cJSON_IsNumber(p_tlid))
    {
        ESP_LOGE(TAG, "tlid is not number");
        goto err;
    } else {
        p_info->tid = (intmax_t)p_tlid->valuedouble;
    }

    p_ctd = cJSON_GetObjectItem(p_json_root, "ctd");
    if (p_ctd == NULL || !cJSON_IsNumber(p_ctd))
    {
        ESP_LOGE(TAG, "ctd is not number");
        goto err;
    } else {
        p_info->ctd = (intmax_t)p_ctd->valuedouble;
    }

    p_tn = cJSON_GetObjectItem(p_json_root, "tn");
    if (p_tn == NULL || !cJSON_IsString(p_tn))
    {
        ESP_LOGE(TAG, "tn is missing or not a string");
        goto err;
    } else {
        p_info->p_tf_name = p_tn->valuestring;
    }

    p_tasklist = cJSON_GetObjectItem(p_json_root, "task_flow");
    if (p_tasklist == NULL || !cJSON_IsArray(p_tasklist))
    {
        ESP_LOGE(TAG, "task_flow is not array");
        goto err;
    }

    module_item_num = cJSON_GetArraySize(p_tasklist);
    ESP_GOTO_ON_FALSE(module_item_num, ESP_ERR_INVALID_ARG, err, TAG, "tasklist is empty");

    p_list_head = (tf_module_item_t *)tf_malloc(sizeof(tf_module_item_t) * module_item_num);
    ESP_GOTO_ON_FALSE(p_list_head, ESP_ERR_NO_MEM, err, TAG, "malloc failed");

    memset((void *)p_list_head, 0, sizeof(tf_module_item_t) * module_item_num);

    for (int i = 0; i < module_item_num; i++)
    {
        cJSON *p_id = NULL;
        cJSON *p_type = NULL;
        cJSON *p_index = NULL;
        cJSON *p_params = NULL;
        cJSON *p_wires = NULL;
        int output_port_num = 0;

        cJSON *p_item = cJSON_GetArrayItem(p_tasklist, i);
        if (p_item == NULL || !cJSON_IsObject(p_item))
        {
            ESP_LOGE(TAG, "tasklist[%d] is not object", i);
            goto err;
        }

        p_id = cJSON_GetObjectItem(p_item, "id");
        if (p_id == NULL || !cJSON_IsNumber(p_id))
        {
            ESP_LOGE(TAG, "tasklist[%d] id is not number", i);
            goto err;
        }

        p_type = cJSON_GetObjectItem(p_item, "type");
        if (p_type == NULL || !cJSON_IsString(p_type))
        {
            ESP_LOGE(TAG, "tasklist[%d] type is not string", i);
            goto err;
        }

        p_index = cJSON_GetObjectItem(p_item, "index");
        if (p_index == NULL || !cJSON_IsNumber(p_index))
        {
            ESP_LOGE(TAG, "tasklist[%d] index is not number", i);
            goto err;
        }

        p_params = cJSON_GetObjectItem(p_item, "params");
        if (p_params == NULL || !cJSON_IsObject(p_params))
        {
            ESP_LOGE(TAG, "tasklist[%d] params is not object", i);
            goto err;
        }

        p_wires = cJSON_GetObjectItem(p_item, "wires");
        if (p_wires == NULL || !cJSON_IsArray(p_wires))
        {
            ESP_LOGE(TAG, "tasklist[%d] wires is not array", i);
            goto err;
        }

        p_list_head[i].id = p_id->valueint;
        p_list_head[i].p_name = p_type->valuestring;
        p_list_head[i].index = p_index->valueint;
        p_list_head[i].p_params = p_params;

        // handle wires parse
        output_port_num = cJSON_GetArraySize(p_wires);
        if (output_port_num)
        {
            p_list_head[i].p_wires = (struct tf_module_wires *)tf_malloc(sizeof(struct tf_module_wires) * output_port_num);
            ESP_GOTO_ON_FALSE(p_list_head[i].p_wires, ESP_ERR_NO_MEM, err, TAG, "malloc failed");
            memset((void *)p_list_head[i].p_wires, 0, sizeof(struct tf_module_wires) * output_port_num);
            p_list_head[i].output_port_num = output_port_num;

            for (int m = 0; m < output_port_num; m++)
            {
                int evt_id_num = 0;
                cJSON *p_wires_item_tmp = cJSON_GetArrayItem(p_wires, m);
                if (p_wires_item_tmp == NULL || !cJSON_IsArray(p_wires_item_tmp))
                {
                    ESP_LOGE(TAG, "tasklist[%d] wires[%d] is not array", i, m);
                    goto err;
                }
                evt_id_num = cJSON_GetArraySize(p_wires_item_tmp);
                if (evt_id_num <= 0)
                {
                    ESP_LOGE(TAG, "tasklist[%d] wires[%d] is empty", i, m);
                    goto err;
                }
                p_list_head[i].p_wires[m].p_evt_id = (int *)tf_malloc(sizeof(int) * evt_id_num);
                ESP_GOTO_ON_FALSE(p_list_head[i].p_wires[m].p_evt_id, ESP_ERR_NO_MEM, err, TAG, "malloc failed");
                memset((void *)p_list_head[i].p_wires[m].p_evt_id, 0, sizeof(int) * evt_id_num);
                for (int n = 0; n < evt_id_num; n++)
                {
                    p_list_head[i].p_wires[m].p_evt_id[n] = cJSON_GetArrayItem(p_wires_item_tmp, n)->valueint;
                }
                p_list_head[i].p_wires[m].num = evt_id_num;
            }
        }
    }
    *pp_json_root = p_json_root;
    *pp_head = p_list_head;

    return module_item_num;

err:
    if (p_json_root != NULL)
    {
        cJSON_Delete(p_json_root);
    }
    if (p_list_head != NULL)
    {
        __module_item_free(p_list_head, module_item_num);
        free(p_list_head);
    }
    return -1;
}

int tf_parse_json(const char *p_str,
                  cJSON **pp_json_root,
                  tf_module_item_t **pp_head,
                  tf_info_t *p_info)
{
    return tf_parse_json_with_length(p_str, strlen(p_str), pp_json_root, pp_head, p_info);
}
void tf_parse_free(cJSON *p_json_root, tf_module_item_t *p_head, int num)
{
    if (p_json_root)
    {
        cJSON_Delete(p_json_root);
    }
    if (p_head != NULL)
    {
        __module_item_free(p_head, num);
        tf_free(p_head);
    }
}

char* tf_parse_util_simplify_json(const char *p_str)
{
    esp_err_t ret = ESP_OK;
    cJSON *p_json_root = NULL;
    char *p_json = NULL;

    if( p_str == NULL ) {
        return NULL;
    }
    p_json_root = cJSON_ParseWithLength(p_str, strlen(p_str));
    ESP_GOTO_ON_FALSE(p_json_root, ESP_ERR_INVALID_ARG, err, TAG, "json parse failed");

    remove_audio_from_alarm_trigger(p_json_root);

    p_json = cJSON_PrintUnformatted(p_json_root);

err:
    if (p_json_root != NULL)
    {
        cJSON_Delete(p_json_root);
    }
    return p_json;
}

