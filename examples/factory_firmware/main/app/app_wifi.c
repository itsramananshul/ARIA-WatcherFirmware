#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOSConfig.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"

#include "app_wifi.h"
#include "data_defs.h"
#include "event_loops.h"
#include "at_cmd.h"
#include "util.h"

#define WIFI_CONFIG_ENTRY_STACK_SIZE 10240
#define WIFI_CONNECTED_BIT           BIT0
#define WIFI_FAIL_BIT                BIT1
#define PING_PERIOD_MAX              60         // n * 5seconds, 60 means 5 minutes
#define PING_PERIOD_DECAY_STEP       20         // time counting step when mqtt disconnect

struct app_wifi
{
    struct view_data_wifi_st st;
    bool is_cfg;
    int wifi_reconnect_cnt;
};

static struct app_wifi _g_wifi_cfg;
static SemaphoreHandle_t __g_wifi_mutex;
static SemaphoreHandle_t __g_data_mutex;
static SemaphoreHandle_t __g_net_check_sem;
static volatile atomic_int __g_ping_period_cnt = ATOMIC_VAR_INIT(0);

static int s_retry_num = 0;
static int wifi_retry_max = 3;
static bool __g_ping_done = true;
int wifi_connect_failed_reason = 70;
static EventGroupHandle_t __wifi_event_group;
static StaticTask_t wifi_task_buffer;
static StackType_t *wifi_task_stack = NULL;
static const char *TAG = "app-wifi";

static int min(int a, int b)
{
    return (a < b) ? a : b;
}

static void __wifi_st_set(struct view_data_wifi_st *p_st)
{
    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    memcpy(&_g_wifi_cfg.st, p_st, sizeof(struct view_data_wifi_st));
    xSemaphoreGive(__g_data_mutex);
}

static void __wifi_st_get(struct view_data_wifi_st *p_st)
{
    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    memcpy(p_st, &_g_wifi_cfg.st, sizeof(struct view_data_wifi_st));
    xSemaphoreGive(__g_data_mutex);
}

void current_wifi_get(wifi_ap_record_t *p_st)
{
    if (esp_wifi_sta_get_ap_info(p_st) == ESP_OK)
    {
        ESP_LOGI(TAG, "SSID: %s", p_st->ssid);
        ESP_LOGI(TAG, "RSSI: %d", p_st->rssi);
    }
    else
    {
        ESP_LOGI(TAG, " wifi  disconnected");
    }
}

extern SemaphoreHandle_t semaphorewificonnected;
extern SemaphoreHandle_t semaphorewifidisconnected;
static wifi_config_t previous_wifi_config;
static void __wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
        case WIFI_EVENT_STA_START: {
            ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_START");
            struct view_data_wifi_st st;
            __wifi_st_get(&st);
            st.is_connected = false;
            st.is_network = false;
            st.is_connecting = true;
            st.rssi = 0;
            __wifi_st_set(&st);

            esp_wifi_connect();
            break;
        }
        case WIFI_EVENT_STA_CONNECTED: {
            wifi_connect_failed_reason = 0;
            xSemaphoreGive(semaphorewificonnected);
            ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_CONNECTED");
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
            struct view_data_wifi_st st;

            __wifi_st_get(&st);
            memset(st.ssid, 0, sizeof(st.ssid));
            memcpy(st.ssid, event->ssid, event->ssid_len);
            st.rssi = -50; // todo
            st.is_connected = true;
            st.is_connecting = false;
            __wifi_st_set(&st);

            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(10000));

            struct view_data_wifi_connet_ret_msg msg;
            msg.ret = 0;
            strcpy(msg.msg, "Connection successful");
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET, &msg, sizeof(msg), pdMS_TO_TICKS(10000));

            // Save the current WiFi config as the previous config
            esp_wifi_get_config(WIFI_IF_STA, &previous_wifi_config);
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED: {
            ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_DISCONNECTED");
            wifi_event_sta_disconnected_t *disconnected_event = (wifi_event_sta_disconnected_t *)event_data;

            switch (disconnected_event->reason)
            {
                case WIFI_REASON_AUTH_FAIL:
                    ESP_LOGI(TAG, "Authentication failed, incorrect password");
                    wifi_connect_failed_reason = disconnected_event->reason;
                    break;
                case WIFI_REASON_NO_AP_FOUND:
                    wifi_connect_failed_reason = disconnected_event->reason;
                    ESP_LOGI(TAG, "AP not found");
                    break;
                // Add more cases as needed
                default:
                    wifi_connect_failed_reason = disconnected_event->reason;
                    ESP_LOGI(TAG, "Other disconnection reason: %d", disconnected_event->reason);
                    break;
            }
            xSemaphoreGive(semaphorewificonnected);

            if ((wifi_retry_max == -1) || s_retry_num < wifi_retry_max)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
            }
            else
            {
                struct view_data_wifi_st st;
                __wifi_st_get(&st);
                st.is_connected = false;
                st.is_network = false;
                st.is_connecting = false;
                __wifi_st_set(&st);

                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(10000));

                struct view_data_wifi_connet_ret_msg msg;
                msg.ret = 0;
                strcpy(msg.msg, "Connection failure");
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET, &msg, sizeof(msg), pdMS_TO_TICKS(10000));
            }
            break;
        }
        default:
            break;
    }
}

static void __ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;

        // xEventGroupSetBits(__wifi_event_group, WIFI_CONNECTED_BIT);
        xSemaphoreGive(__g_net_check_sem); // goto check network
    }
}

TaskHandle_t xTask_wifi_config_entry;
WiFiStack wifiStack_scanned;
WiFiStack wifiStack_connected;

/***Already abandoned**/
/*
static int __wifi_scan()
{
    wifi_ap_record_t *p_ap_info = (wifi_ap_record_t *)heap_caps_malloc(5 * sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM);
    uint16_t number = 5;
    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_err_t ret = esp_wifi_scan_start(NULL, true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "wifi scan start failed");
        return -1;
    }
    else
    {
        ESP_LOGI(TAG, "wifi scan start success");
    }
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, p_ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, " scan ap cont: %d", ap_count);

    struct view_data_wifi_st wifi_table_element;
    for (int i = 0; (i < number) && (i < ap_count); i++)
    {
        ESP_LOGI(TAG, "SSID: %s, RSSI:%d, Channel: %d", p_ap_info[i].ssid, p_ap_info[i].rssi, p_ap_info[i].primary);
        wifi_table_element.rssi = p_ap_info[i].rssi;
        wifi_table_element.is_connected = false;
        wifi_table_element.is_network = false;
        wifi_table_element.is_connecting = false;
        wifi_table_element.authmode = p_ap_info[i].authmode;
        strcpy(wifi_table_element.ssid, (char *)p_ap_info[i].ssid);
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST, &wifi_table_element, sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(10000));
    }
    return ap_count;
}
*/
/***************/

/*----------------------------------------------------------------------------------------------------------*/
/*add scanned wifi entry  into wifi stack*/
const char *print_auth_mode(int authmode)
{
    switch (authmode)
    {
        case WIFI_AUTH_OPEN:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_OPEN");
            return "OPEN";
            break;
        case WIFI_AUTH_OWE:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_OWE");
            return "UNKNOWN";
            break;
        case WIFI_AUTH_WEP:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WEP");
            return "WEP";
            break;
        case WIFI_AUTH_WPA_PSK:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA_PSK");
            return "WPA";
            break;
        case WIFI_AUTH_WPA2_PSK:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_PSK");
            return "WPA2";
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA_WPA2_PSK");
            return "WPA/WPA2";
            break;
        case WIFI_AUTH_ENTERPRISE:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_ENTERPRISE");
            return "UNKNOWN";
            break;
        case WIFI_AUTH_WPA3_PSK:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_PSK");
            return "WPA3";
            break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA2_WPA3_PSK");
            return "WPA2/WPA3";
            break;
        case WIFI_AUTH_WPA3_ENT_192:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_ENT_192");
            return "WPA2/WPA3";
            break;
        case WIFI_AUTH_WPA3_EXT_PSK:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_EXT_PSK");
            return "WPA2/WPA3";
            break;
        case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE");
            return "WPA2/WPA3";
            break;
        default:
            ESP_LOGI(TAG, "Authmode \tWIFI_AUTH_UNKNOWN");
            return "UNKNOWN";
            break;
    }
}
static void print_cipher_type(int pairwise_cipher, int group_cipher)
{
    switch (pairwise_cipher)
    {
        case WIFI_CIPHER_TYPE_NONE:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_NONE");
            break;
        case WIFI_CIPHER_TYPE_WEP40:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP40");
            break;
        case WIFI_CIPHER_TYPE_WEP104:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP104");
            break;
        case WIFI_CIPHER_TYPE_TKIP:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP");
            break;
        case WIFI_CIPHER_TYPE_CCMP:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_CCMP");
            break;
        case WIFI_CIPHER_TYPE_TKIP_CCMP:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
            break;
        case WIFI_CIPHER_TYPE_AES_CMAC128:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_AES_CMAC128");
            break;
        case WIFI_CIPHER_TYPE_SMS4:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_SMS4");
            break;
        case WIFI_CIPHER_TYPE_GCMP:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_GCMP");
            break;
        case WIFI_CIPHER_TYPE_GCMP256:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_GCMP256");
            break;
        default:
            ESP_LOGI(TAG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
            break;
    }

    switch (group_cipher)
    {
        case WIFI_CIPHER_TYPE_NONE:
            ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_NONE");
            break;
        case WIFI_CIPHER_TYPE_WEP40:
            ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP40");
            break;
        case WIFI_CIPHER_TYPE_WEP104:
            ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP104");
            break;
        case WIFI_CIPHER_TYPE_TKIP:
            ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP");
            break;
        case WIFI_CIPHER_TYPE_CCMP:
            ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_CCMP");
            break;
        case WIFI_CIPHER_TYPE_TKIP_CCMP:
            ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP");
            break;
        case WIFI_CIPHER_TYPE_SMS4:
            ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_SMS4");
            break;
        case WIFI_CIPHER_TYPE_GCMP:
            ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_GCMP");
            break;
        case WIFI_CIPHER_TYPE_GCMP256:
            ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_GCMP256");
            break;
        default:
            ESP_LOGI(TAG, "Group Cipher \tWIFI_CIPHER_TYPE_UNKNOWN");
            break;
    }
}

void addWiFiEntryToStack(WiFiStack *stack, uint8_t ssid[33], int8_t rssi, const char *encryption)
{
    char ssid_str[33];
    char rssi_str[5]; // Enough to hold -128 to 127 and null-terminator

    // Convert ssid to string
    snprintf(ssid_str, sizeof(ssid_str), "%s", ssid);

    // Convert rssi to string
    snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);

    // Create a WiFiEntry
    WiFiEntry entry = { .ssid = strdup_psram(ssid_str), .rssi = strdup_psram(rssi_str), .encryption = strdup_psram(encryption) };

    // Push the entry to the stack
    pushWiFiStack(stack, entry);
}

void wifi_scan(void)
{
    esp_err_t ret;
    const int max_cnt = WIFI_SCAN_RESULT_CNT_MAX;
    wifi_ap_record_t *ap_info = psram_calloc(1, sizeof(wifi_ap_record_t) * max_cnt);
    uint16_t ap_cnt = max_cnt;

    ESP_GOTO_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), wifi_scan_end, TAG, "esp_wifi_set_mode failed");
    ESP_GOTO_ON_ERROR(esp_wifi_start(), wifi_scan_end, TAG, "esp_wifi_start failed");
    ESP_GOTO_ON_ERROR(esp_wifi_scan_start(NULL, true), wifi_scan_end, TAG, "esp_wifi_scan_start failed");
    ESP_GOTO_ON_ERROR(esp_wifi_scan_get_ap_records(&ap_cnt, ap_info), wifi_scan_end, TAG, "esp_wifi_scan_get_ap_records failed");
    ESP_LOGI(TAG, "Max number of APs want: %d, scanned = %d", max_cnt, ap_cnt);
    for (int i = 0; i < ap_cnt; i++)
    {
        ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
        ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
        const char *encryption = print_auth_mode(ap_info[i].authmode);
        if (ap_info[i].authmode != WIFI_AUTH_WEP)
        {
            print_cipher_type(ap_info[i].pairwise_cipher, ap_info[i].group_cipher);
        }
        addWiFiEntryToStack(&wifiStack_scanned, ap_info[i].ssid, ap_info[i].rssi, encryption);
        ESP_LOGI(TAG, "Channel \t\t%d", ap_info[i].primary);
    }
wifi_scan_end:
    free(ap_info);
}

/*---------------------------------------------------------------------------------------------------------*/
/*basic wifi connect function*/
static int __wifi_connect(const char *p_ssid, const char *p_password, int retry_num)
{
    wifi_retry_max = retry_num; // todo
    s_retry_num = 0;

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, p_ssid, sizeof(wifi_config.sta.ssid));
    ESP_LOGI(TAG, "ssid: %s", p_ssid);
    if (p_password)
    {
        ESP_LOGI(TAG, "password: %s", p_password);
        strlcpy((char *)wifi_config.sta.password, p_password, sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // todo
    }
    else
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    _g_wifi_cfg.is_cfg = true;

    struct view_data_wifi_st st = { 0 };
    st.is_connected = false;
    st.is_connecting = false;
    st.is_network = false;
    st.past_connected = true;
    __wifi_st_set(&st);

    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(10000));

    ESP_ERROR_CHECK(esp_wifi_start());
    // esp_wifi_connect();

    ESP_LOGI(TAG, "connect...");

    return ESP_OK;
}

static void __wifi_cfg_restore(void)
{
    _g_wifi_cfg.is_cfg = false;

    struct view_data_wifi_st st = { 0 };
    st.is_connected = false;
    st.is_connecting = false;
    st.is_network = false;
    __wifi_st_set(&st);

    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(10000));

    // restore and stop
    esp_wifi_restore();
}

static void __wifi_shutdown(void)
{
    _g_wifi_cfg.is_cfg = false; // disable reconnect

    struct view_data_wifi_st st = { 0 };
    st.is_connected = false;
    st.is_connecting = false;
    st.is_network = false;
    __wifi_st_set(&st);

    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(10000));

    esp_wifi_stop();
}

static void __ping_end(esp_ping_handle_t hdl, void *args)
{
    ip_addr_t target_addr;
    uint32_t transmitted = 0;
    uint32_t received = 0;
    uint32_t total_time_ms = 0;
    uint32_t loss = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));

    if (transmitted > 0)
    {
        loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
    }
    else
    {
        loss = 100;
    }

    if (IP_IS_V4(&target_addr))
    {
        printf("\n--- %s ping statistics ---\n", inet_ntoa(*ip_2_ip4(&target_addr)));
    }
    else
    {
        printf("\n--- %s ping statistics ---\n", inet6_ntoa(*ip_2_ip6(&target_addr)));
    }
    printf("%ld packets transmitted, %ld received, %ld%% packet loss, time %ldms\n", transmitted, received, loss, total_time_ms);

    esp_ping_delete_session(hdl);

    struct view_data_wifi_st st;
    if (received > 0)
    {
        wifi_ap_record_t ap_st;
        esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_st);
        
        __wifi_st_get(&st);
        if( ret == ESP_OK ) {
            st.rssi = ap_st.rssi;
            memset(st.ssid, 0, sizeof(st.ssid));
            strncpy(st.ssid, (char *)ap_st.ssid, sizeof(st.ssid));
        }
        st.is_network = true;
        atomic_store(&__g_ping_period_cnt, 0);  //reset the counter if network is good
        __wifi_st_set(&st);
    }
    else
    {
        __wifi_st_get(&st);
        st.is_network = false;
        __wifi_st_set(&st);
    }
    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(10000));
    __g_ping_done = true;
}

static void __ping_start(void)
{
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();

    ip_addr_t target_addr;
    ipaddr_aton(PING_TEST_IP, &target_addr);

    config.target_addr = target_addr;

    esp_ping_callbacks_t cbs = { .cb_args = NULL, .on_ping_success = NULL, .on_ping_timeout = NULL, .on_ping_end = __ping_end };
    esp_ping_handle_t ping;
    esp_ping_new_session(&config, &cbs, &ping);
    __g_ping_done = false;
    esp_ping_start(ping);
}

// net check
static void __app_wifi_task(void *p_arg)
{
    int cnt = 0;
    struct view_data_wifi_st st;

    while (1)
    {
        xSemaphoreTake(__g_net_check_sem, pdMS_TO_TICKS(5000));
        __wifi_st_get(&st);

        // Periodically check the network connection status
        if (st.is_connected)
        {
            if (__g_ping_done)
            {
                if (st.is_network)
                {
                    cnt++;
                    // 5min check network
                    if (cnt > PING_PERIOD_MAX)
                    {
                        cnt = 0;
                        ESP_LOGI(TAG, "Network normal last time, retry check network...");
                        __ping_start();
                    } else if (atomic_load(&__g_ping_period_cnt) > PING_PERIOD_MAX) {
                        atomic_store(&__g_ping_period_cnt, 0);
                        ESP_LOGW(TAG, "network seems to be down, sensed by MQTT, ping now ...");
                        __ping_start();
                    }
                }
                else
                {
                    cnt = 0;
                    ESP_LOGI(TAG, "Last network exception, check network...");
                    __ping_start();
                }
            }
        }
        else if (_g_wifi_cfg.is_cfg && !st.is_connecting)
        {
            // Periodically check the wifi connection status

            // 1min retry connect
            if (_g_wifi_cfg.wifi_reconnect_cnt > 12)
            {
                ESP_LOGI(TAG, " Wifi reconnect...");
                _g_wifi_cfg.wifi_reconnect_cnt = 0;
                wifi_retry_max = 3;
                s_retry_num = 0;

                esp_wifi_stop();
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_start());
            }
            _g_wifi_cfg.wifi_reconnect_cnt++;
        }
    }
}

/*------------------------------------------------------------------------------------------------------------*/
/*wifi event_loop process handler*/
static void __view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
        case VIEW_EVENT_WIFI_CONNECT: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CONNECT");
            struct view_data_wifi_config *p_cfg = (struct view_data_wifi_config *)event_data;

            if (p_cfg->have_password)
            {
                __wifi_connect(p_cfg->ssid, (const char *)p_cfg->password, 3);
            }
            else
            {
                __wifi_connect(p_cfg->ssid, NULL, 3);
            }
            break;
        }
        case VIEW_EVENT_WIFI_CFG_DELETE: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CFG_DELETE");
            __wifi_cfg_restore();
            break;
        }
        case CTRL_EVENT_MQTT_DISCONNECTED: {
            ESP_LOGI(TAG, "event: CTRL_EVENT_MQTT_DISCONNECTED, speed up ping time counting ...");
            atomic_fetch_add(&__g_ping_period_cnt, PING_PERIOD_DECAY_STEP);
            break;
        }
        default:
            break;
    }
}

/*------------------------------------------------------------------------------------------------------------*/

static void __wifi_cfg_init(void)
{
    memset(&_g_wifi_cfg, 0, sizeof(_g_wifi_cfg));
}

extern SemaphoreHandle_t xBinarySemaphore_wifitable;
void __wifi_config_task(void *pvParameters)
{
    uint32_t ulNotificationValue;
    xTask_wifi_config_entry = xTaskGetCurrentTaskHandle();
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        wifi_scan();
        xSemaphoreGive(xBinarySemaphore_wifitable);
    }
}

void app_wifi_config_entry_init()
{
    wifi_task_stack = (StackType_t *)heap_caps_malloc(4096 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (wifi_task_stack == NULL)
    {
        printf("Failed to allocate memory for WiFi task stack\n");
        return;
    }

    TaskHandle_t wifi_task_handle = xTaskCreateStatic(__wifi_config_task, "wifi_config", 4096, NULL, 9, wifi_task_stack, &wifi_task_buffer);

    if (wifi_task_handle == NULL)
    {
        printf("Failed to create WiFi task\n");
        free(wifi_task_stack);
        wifi_task_stack = NULL;
    }
}
int app_wifi_init(void)
{
    __g_wifi_mutex = xSemaphoreCreateMutex();
    __g_data_mutex = xSemaphoreCreateMutex();
    __g_net_check_sem = xSemaphoreCreateBinary();
    semaphorewifidisconnected = xSemaphoreCreateBinary();
    semaphorewificonnected = xSemaphoreCreateBinary();
    __wifi_cfg_init();
    app_wifi_config_entry_init();

    const uint32_t stack_size = 10 * 1024;
    StackType_t *task_stack = (StackType_t *)psram_calloc(1, stack_size * sizeof(StackType_t));
    static StaticTask_t task_tcb;
    xTaskCreateStatic(__app_wifi_task, "app_wifi_task", stack_size, NULL, 10, task_stack, &task_tcb);

    ESP_ERROR_CHECK(esp_netif_init());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    ESP_LOGI(TAG, "esp_wifi_init:%d, %s", ret, esp_err_to_name(ret));
    ESP_ERROR_CHECK(ret);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &__wifi_event_handler, 0, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &__ip_event_handler, 0, &instance_got_ip));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT, __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CFG_DELETE, __view_event_handler, NULL, NULL));


    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_DISCONNECTED, __view_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg;
    struct view_data_wifi_st wifi_table_element_connected;

    memset(&wifi_table_element_connected, 0, sizeof(struct view_data_wifi_st));
    esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
    // wifi_table_element_connected.= wifi_cfg.sta.password;
    strcpy(wifi_table_element_connected.ssid, (char *)wifi_cfg.sta.ssid);
    // esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ, &wifi_table_element_connected, sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(10000));

    if (strlen((const char *)wifi_cfg.sta.ssid))
    {
        _g_wifi_cfg.is_cfg = true;
        ESP_LOGI(TAG, "last config ssid: %s", wifi_cfg.sta.ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        wifi_table_element_connected.past_connected = true;
        __wifi_st_set(&wifi_table_element_connected);
        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &wifi_table_element_connected, sizeof(struct view_data_wifi_st), pdMS_TO_TICKS(10000));
    }
    else
    {
        ESP_LOGI(TAG, "Not config wifi, Entry wifi config screen");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    return 0;
}