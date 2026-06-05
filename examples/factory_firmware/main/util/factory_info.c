#include "factory_info.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "util.h"


#define FACTORY_INFO_TAG "factorynvs"

#define FACTORY_NVS_PART_NAME "nvsfactory"

#define SN          "SN_SK"
#define EUI         "EUI_SK"
#define CODE        "CODE_SK"
#define DEVICE_KEY  "DEV_KEY_SK"
#define AI_KEY      "AI_KEY_SK"
#define DEV_CTL_KEY "DEV_CTL_KEY_SK"
#define ACCESS_KEY  "ACCESS_KEY_SK"
#define BATCHID     "BATCHID_SK"
#define PLATFORM    "PLATFORM_SK"


static bool flag = false;
static factory_info_t *gp_info = NULL;

esp_err_t factory_info_init(void)
{
    esp_err_t ret =  nvs_flash_init_partition(FACTORY_NVS_PART_NAME);
    if (ret != ESP_OK) {
        ESP_LOGE(FACTORY_INFO_TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    nvs_handle_t handle;
    size_t len = 0;

    gp_info = psram_malloc(sizeof(factory_info_t));
    if( gp_info == NULL ) {
        ESP_LOGE(FACTORY_INFO_TAG, "malloc failed");
        return ESP_ERR_NO_MEM;
    }
    memset(gp_info, 0, sizeof(factory_info_t));
    
    ret = nvs_open_from_partition(FACTORY_NVS_PART_NAME, "device_info", NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        free(gp_info);
        gp_info = NULL;
        ESP_LOGE(FACTORY_INFO_TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    flag = true;

    nvs_get_str(handle, SN, NULL, &len);
    if (len > 0) {
        gp_info->sn = psram_malloc(len);
        nvs_get_str(handle, SN, gp_info->sn, &len);
        ESP_LOGI("", "# SN: %s",gp_info->sn ? gp_info->sn : "N/A");
    } else {
        ESP_LOGI("", "# SN: N/A");
    }

    nvs_get_str(handle, EUI, NULL, &len);
    if (len > 0) {
        gp_info->eui = psram_malloc(len);
        nvs_get_str(handle, EUI, gp_info->eui, &len);
        ESP_LOGI("", "# EUI: %s",gp_info->eui ? gp_info->eui : "N/A");
    } else {
        ESP_LOGI("", "# EUI: N/A");
    }

    nvs_get_str(handle, CODE, NULL, &len);
    if (len > 0) {
        gp_info->code = psram_malloc(len);
        nvs_get_str(handle, CODE, gp_info->code, &len);
        ESP_LOGI("", "# CODE: %s",gp_info->code ? gp_info->code : "N/A");
    } else {
        ESP_LOGI("", "# CODE: N/A");
    }

    nvs_get_str(handle, DEVICE_KEY, NULL, &len);
    if (len > 0)
    {
        gp_info->device_key = psram_malloc(len);
        nvs_get_str(handle, DEVICE_KEY, gp_info->device_key, &len);
        ESP_LOGI("", "# DEVICE_KEY: %s",gp_info->device_key ? gp_info->device_key : "N/A");
    } else {
        ESP_LOGI("", "# DEVICE_KEY: N/A");
    }

    nvs_get_str(handle, BATCHID, NULL, &len);
    if (len > 0) {
        gp_info->batchid = psram_malloc(len);
        nvs_get_str(handle, BATCHID, gp_info->batchid, &len);
        ESP_LOGI("", "# BATCHID: %s",gp_info->batchid ? gp_info->batchid : "N/A");
    } else {
        ESP_LOGI("", "# BATCHID: N/A");
    }

    nvs_get_str(handle, ACCESS_KEY, NULL, &len);
    if (len > 0) {
        gp_info->access_key = psram_malloc(len);
        nvs_get_str(handle, ACCESS_KEY, gp_info->access_key, &len);
        ESP_LOGI("", "# ACCESS_KEY: %s",gp_info->access_key ? gp_info->access_key : "N/A");
    } else {
        ESP_LOGI("", "# ACCESS_KEY: N/A");
    }

    nvs_get_str(handle, AI_KEY, NULL, &len);
    if (len > 0) {
        gp_info->ai_key = psram_malloc(len);
        nvs_get_str(handle, AI_KEY, gp_info->ai_key, &len);
        ESP_LOGI("", "# AI_KEY: %s",gp_info->ai_key ? gp_info->ai_key : "N/A");
    } else {
        ESP_LOGI("", "# AI_KEY: N/A");
    }   

    nvs_get_str(handle, DEV_CTL_KEY, NULL, &len);
    if (len > 0) {
        gp_info->device_control_key = psram_malloc(len);
        nvs_get_str(handle, DEV_CTL_KEY, gp_info->device_control_key, &len);
        ESP_LOGI("", "# DEV_CTL_KEY: %s",gp_info->device_control_key ? gp_info->device_control_key : "N/A");
    } else {
        ESP_LOGI("", "# DEV_CTL_KEY: N/A");
    }

    nvs_get_str(handle, PLATFORM, NULL, &len);
    if (len > 0) {
        char *value = psram_malloc(len);
        nvs_get_str(handle, PLATFORM, value, &len);

        if (strcmp(value, "1") == 0) { 
            gp_info->platform = 1;
        } else {
            gp_info->platform = 0;  
        }
        free(value);
        ESP_LOGI("", "# PLATFORM: %d",gp_info->platform);
    } else {
        ESP_LOGI("", "# PLATFORM: 0 (default)");
    }
    nvs_close(handle);
    return ESP_OK;
}

const factory_info_t *factory_info_get(void)
{
    if (gp_info == NULL && !flag) {
        return NULL;
    }
    return ( const factory_info_t *)gp_info;
}

const char *factory_info_eui_get(void)
{
    if (gp_info != NULL && gp_info->eui != NULL) {
        return gp_info->eui;
    }
    return NULL;
}
const char *factory_info_sn_get(void)
{
    if (gp_info != NULL && gp_info->sn != NULL) {
        return gp_info->sn;
    }
    return NULL;
}
const char *factory_info_code_get(void)
{
    if (gp_info != NULL && gp_info->code != NULL) {
        return gp_info->code;
    }
    return NULL;
}
const char *factory_info_device_key_get(void)
{
    if (gp_info != NULL && gp_info->device_key != NULL) {
        return gp_info->device_key;
    }
    return NULL;
}
const char *factory_info_ai_key_get(void)
{
    if (gp_info != NULL && gp_info->ai_key != NULL) {
        return gp_info->ai_key;
    }
    return NULL;
}
const char *factory_info_batchid_get(void)
{
    if (gp_info != NULL && gp_info->batchid != NULL) {
        return gp_info->batchid;
    }
    return NULL;
}
const char *factory_info_access_key_get(void)
{
    if (gp_info != NULL && gp_info->access_key != NULL) {
        return gp_info->access_key;
    }
    return NULL;
}
const char *factory_info_device_control_key_get(void)
{
    if (gp_info != NULL && gp_info->device_control_key != NULL) {
        return gp_info->device_control_key;
    }
    return NULL;
}
uint8_t factory_info_platform_get(void)
{
    if (gp_info != NULL) {
        return gp_info->platform;
    }
    return 0;
}
void factory_info_print(void)
{
    if (gp_info != NULL) {
        printf( "# SN: %s\n",gp_info->sn ? gp_info->sn : "N/A");
        printf( "# EUI: %s\n",gp_info->eui ? gp_info->eui : "N/A");
        printf( "# CODE: %s\n",gp_info->code ? gp_info->code : "N/A");
        printf( "# DEVICE_KEY: %s\n",gp_info->device_key ? gp_info->device_key : "N/A");
        printf( "# AI_KEY: %s\n",gp_info->ai_key ? gp_info->ai_key : "N/A");
        printf( "# BATCHID: %s\n",gp_info->batchid ? gp_info->batchid : "N/A");
        printf( "# ACCESS_KEY: %s\n",gp_info->access_key ? gp_info->access_key : "N/A");
        printf( "# DEV_CTL_KEY: %s\n",gp_info->device_control_key ? gp_info->device_control_key : "N/A");
        printf( "# PLATFORM: %d\n",gp_info->platform);
    } 
}