#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct factory_info{
    const char *sn;
    const char *eui;
    const char *code;
    const char *device_key;
    const char *batchid;
    const char *access_key;
    const char *ai_key; 
    const char *device_control_key;
    uint8_t platform;
} factory_info_t;

esp_err_t factory_info_init(void);

const factory_info_t *factory_info_get(void);
const char *factory_info_eui_get(void);
const char *factory_info_sn_get(void);
const char *factory_info_code_get(void);
const char *factory_info_device_key_get(void);
const char *factory_info_ai_key_get(void);
const char *factory_info_batchid_get(void);
const char *factory_info_access_key_get(void);
const char *factory_info_device_control_key_get(void);
uint8_t factory_info_platform_get(void);

void factory_info_print(void);

#ifdef __cplusplus
}
#endif

