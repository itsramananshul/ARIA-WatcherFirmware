#pragma once
#include <stdint.h>
#include <esp_err.h>
#include "data_defs.h"


#define DEVICEINFO_STORAGE  "deviceinfo"

// a type of cfg item that includes a string and a switch
typedef struct {
    bool enable;
    char *url;
    char *token;
} local_service_cfg_type1_t;

// index of cfg items
// NOTICE: DO NOT reorder the indexies, they're stored in NVS in their order.
enum {
    CFG_ITEM_TYPE1_AUDIO_TASK_COMPOSER = 0,
    CFG_ITEM_TYPE1_IMAGE_ANALYZER,
    CFG_ITEM_TYPE1_TRAINING,
    CFG_ITEM_TYPE1_NOTIFICATION_PROXY,
    CFG_ITEM_TYPE1_MAX,
};

// struct holding all the cfg items for local service configuration
typedef struct {
    local_service_cfg_type1_t cfg_items_type1[CFG_ITEM_TYPE1_MAX];
} local_service_cfg_t;

uint8_t *get_sn(int caller);
uint8_t *get_eui();
uint8_t *get_qrcode_content();
uint8_t *get_bt_mac();
uint8_t *get_wifi_mac();
char *get_software_version(int caller);
char *get_himax_software_version(int caller);

int get_brightness(int caller);
esp_err_t set_brightness(int caller, int value);

int get_rgb_switch(int caller);
esp_err_t set_rgb_switch(int caller, int value);

int get_sound(int caller);
esp_err_t set_sound(int caller, int value);

int get_cloud_service_switch(int caller);
esp_err_t set_cloud_service_switch(int caller, int value);

esp_err_t get_local_service_cfg_type1(int caller, int cfg_index, local_service_cfg_type1_t *pcfg);
esp_err_t set_local_service_cfg_type1(int caller, int cfg_index, bool enable, char *url, char *token);

int get_usage_guide(int caller);
esp_err_t set_usage_guide(int caller, int value);

esp_err_t set_reset_factory(bool is_need_shutdown);

esp_err_t set_ble_switch(int caller, int status);
int get_ble_switch(int caller);

int get_screenoff_time(int caller);
int get_screenoff_switch(int caller);
esp_err_t set_screenoff_time(int caller, int status);
esp_err_t set_screenoff_switch(int caller, int status);

/**
 * all the following size unit is KiB.
*/
uint16_t get_spiffs_total_size(int caller);
uint16_t get_spiffs_free_size(int caller);
/**
 * all the following size unit is MiB.
*/
uint16_t get_sdcard_total_size(int caller);
uint16_t get_sdcard_free_size(int caller);

void app_device_info_init_early();
void app_device_info_init();

