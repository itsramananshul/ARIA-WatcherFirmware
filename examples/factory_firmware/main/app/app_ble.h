/**
 * BLE service
 * Author: Jack <jack.shao@seeed.cc>
*/


#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

//forward declaration
struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

esp_err_t app_ble_init(void);
uint8_t *app_ble_get_mac_address(void);
esp_err_t app_ble_adv_switch(bool switch_on);
int app_ble_get_current_mtu(void);
esp_err_t app_ble_send_indicate(uint8_t *data, int len);

// note: Must be called in pairs
esp_err_t app_ble_adv_pause(void);
esp_err_t app_ble_adv_resume( int cur_switch );

#ifdef __cplusplus
}
#endif