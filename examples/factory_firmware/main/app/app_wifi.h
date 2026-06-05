#ifndef APP_WIFI_H
#define APP_WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

// #define  PING_TEST_IP "192.168.100.1"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "data_defs.h"

#define PING_TEST_IP                    "223.5.5.5"
#define WIFI_SCAN_RESULT_CNT_MAX        8


typedef struct
{
    char *ssid;
    char *rssi;
    char *encryption;
} WiFiEntry;

typedef struct
{
    WiFiEntry *entries;
    int size;
    int capacity;
} WiFiStack;

extern WiFiStack wifiStack_scanned;
extern WiFiStack wifiStack_connected;
extern int wifi_connect_failed_reason;
extern TaskHandle_t xTask_wifi_config_entry;  


int app_wifi_init(void);
void wifi_scan(void);
void current_wifi_get(wifi_ap_record_t *p_st);

const char *print_auth_mode(int authmode);

#ifdef __cplusplus
}
#endif

#endif