#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <stdatomic.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "sensecap-watcher.h"

#include "data_defs.h"
#include "event_loops.h"
#include "app_ble.h"
#include "factory_info.h"
#include "at_cmd.h"
#include "util.h"

/*
Bluetooth LE uses four association models depending on the I/O capabilities of the devices.

Just Works: designed for scenarios where at least one of the devices does not have a display capable of displaying a six digit number nor does it have a keyboard capable of entering six decimal digits.

Numeric Comparison: designed for scenarios where both devices are capable of displaying a six digit number and both are capable of having the user enter “yes” or “no”. A good example of this model is the cell phone / PC scenario.

Out of Band: designed for scenarios where an Out of Band mechanism is used to both discover the devices as well as to exchange or transfer cryptographic numbers used in the pairing process.

Passkey Entry: designed for the scenario where one device has input capability but does not have the capability to display six digits and the other device has output capabilities. A good example of this model is the PC and keyboard scenario.
*/
#define BLE_SM_IO_CAP_DISP_ONLY         0
#define BLE_SM_IO_CAP_DISP_YES_NO       1
#define BLE_SM_IO_CAP_KEYBOARD_ONLY     2
#define BLE_SM_IO_CAP_NO_IO             3
#define BLE_SM_IO_CAP_KEYBOARD_DISP     4

#define SENSECAP_SN_STR_LEN             18

//event group events
#define EVENT_INDICATE_SENDING          BIT0


static const char *TAG = "ble";

/* GAP */
static uint8_t adv_data[31] = {
    0x05, 0x03, 0x86, 0x28, 0x86, 0xA8,
    0x18, 0x09, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', '0', '1', '-', 'W', 'A', 'C', 'H'
};

static uint8_t ble_mac_addr[6] = {0};
static uint8_t own_addr_type;
static SemaphoreHandle_t g_sem_mac_addr;
static SemaphoreHandle_t g_sem_data;
static EventGroupHandle_t g_eg_ble;
static volatile atomic_bool g_ble_synced = ATOMIC_VAR_INIT(false);
static volatile atomic_bool g_ble_adv = ATOMIC_VAR_INIT(false);
static volatile atomic_bool g_ble_connected = ATOMIC_VAR_INIT(false);
static volatile atomic_int g_curr_mtu = ATOMIC_VAR_INIT(23);
static volatile atomic_int g_ble_adv_pause_cnt = ATOMIC_VAR_INIT(0);

static uint16_t g_curr_ble_conn_handle = 0xffff;

void ble_store_config_init(void);
static int bleprph_gap_event(struct ble_gap_event *event, void *arg);

/* GATT */
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x55, 0xE4, 0x05, 0xD2, 0xAF, 0x9F, 0xA9, 0x8F, 0xE5, 0x4A, 0x7D, 0xFE, 0x43, 0x53, 0x53, 0x49);

/* A characteristic that can be subscribed to */
static uint16_t gatt_svr_chr_handle_write;
static uint16_t gatt_svr_chr_handle_read;
static const ble_uuid128_t gatt_svr_chr_uuid_write =
    BLE_UUID128_INIT(0xB3, 0x9B, 0x72, 0x34, 0xBE, 0xEC, 0xD4, 0xA8, 0xF4, 0x43, 0x41, 0x88, 0x43, 0x53, 0x53, 0x49);
static const ble_uuid128_t gatt_svr_chr_uuid_read =
    BLE_UUID128_INIT(0x16, 0x96, 0x24, 0x47, 0xC6, 0x23, 0x61, 0xBA, 0xD9, 0x4B, 0x4D, 0x1E, 0x43, 0x53, 0x53, 0x49);

static int gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /*** Service ***/
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[])
        {
            {
                /*** This characteristic can be subscribed to by writing 0x00 and 0x01 to the CCCD ***/
                .uuid = &gatt_svr_chr_uuid_write.u,
                .access_cb = gatt_svc_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &gatt_svr_chr_handle_write,
            },
            {
                /*** This characteristic can be subscribed to by writing 0x00 and 0x01 to the CCCD ***/
                .uuid = &gatt_svr_chr_uuid_read.u,
                .access_cb = gatt_svc_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &gatt_svr_chr_handle_read,
            },
            {
                0, /* No more characteristics in this service. */
            }
        },
    },

    {
        0, /* No more services. */
    },
};


/**
 * Access callback whenever a characteristic/descriptor is read or written to.
 * Here reads and writes need to be handled.
 * ctxt->op tells weather the operation is read or write and
 * weather it is on a characteristic or descriptor,
 * ctxt->dsc->uuid tells which characteristic/descriptor is accessed.
 * attr_handle give the value handle of the attribute being accessed.
 * Accordingly do:
 *     Append the value to ctxt->om if the operation is READ
 *     Write ctxt->om to the value if the operation is WRITE
 **/
static int gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const ble_uuid_t *uuid;
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "Characteristic read; conn_handle=%d attr_handle=%d\n", conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG, "Characteristic read by NimBLE stack; attr_handle=%d\n", attr_handle);
        }
        uuid = ctxt->chr->uuid;
        if (attr_handle == gatt_svr_chr_handle_read) {
            char dummy[4] = { 0 };
            rc = os_mbuf_append(ctxt->om, &dummy, sizeof(dummy));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        } else {
            ESP_LOGE(TAG, "should not read on this chr");
        }
        goto unknown;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "Characteristic write; conn_handle=%d attr_handle=%d", conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG, "Characteristic write by NimBLE stack; attr_handle=%d", attr_handle);
        }
        uuid = ctxt->chr->uuid;
        if (attr_handle == gatt_svr_chr_handle_write) {
            size_t ble_msg_len = OS_MBUF_PKTLEN(ctxt->om);
            ble_msg_t ble_msg = {.size = ble_msg_len, .msg = psram_calloc(1, ble_msg_len + 1)};  // 1 for null-terminator
            uint16_t real_len = 0;
            rc = ble_hs_mbuf_to_flat(ctxt->om, ble_msg.msg, ble_msg_len, &real_len);
            if (rc != 0) {
                free(ble_msg.msg);
                return BLE_ATT_ERR_UNLIKELY;
            }
            ESP_LOGI(TAG, "mbuf_len: %d, copied %d bytes from ble stack.", ble_msg_len, real_len);
            ESP_LOGD(TAG, "ble msg: %s", ble_msg.msg);

            if (xQueueSend(ble_msg_queue, &ble_msg, pdMS_TO_TICKS(10)) != pdPASS) {
                ESP_LOGW(TAG, "failed to send ble msg to queue, maybe at_cmd task stalled???");
            } else {
                ESP_LOGD(TAG, "ble msg enqueued");
            }

            // ble_gatts_chr_updated(attr_handle);
            // ESP_LOGI(TAG, "Notification/Indication scheduled for all subscribed peers.\n");
            return rc;
        }
        goto unknown;

    case BLE_GATT_ACCESS_OP_READ_DSC:
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "Descriptor read; conn_handle=%d attr_handle=%d\n", conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG, "Descriptor read by NimBLE stack; attr_handle=%d\n", attr_handle);
        }
        // uuid = ctxt->dsc->uuid;
        // if (ble_uuid_cmp(uuid, &gatt_svr_dsc_uuid.u) == 0) {
        //     rc = os_mbuf_append(ctxt->om,
        //                         &gatt_svr_dsc_val,
        //                         sizeof(gatt_svr_chr_val));
        //     return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        // }
        goto unknown;

    case BLE_GATT_ACCESS_OP_WRITE_DSC:
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "Descriptor write; conn_handle=%d attr_handle=%d\n", conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG, "Descriptor write by NimBLE stack; attr_handle=%d\n", attr_handle);
        }
        goto unknown;

    default:
        goto unknown;
    }

unknown:
    /* Unknown characteristic/descriptor;
     * The NimBLE host should not have called this function;
     */
    ESP_LOGE(TAG, "this should not happen, op: %d", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "registered service %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "registering characteristic %s with "
                    "def_handle=%d val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle,
                    ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "registering descriptor %s with handle=%d\n",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
        break;

    default:
        break;
    }
}

int gatt_svr_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Logs information about a connection to the console.
 */
static void bleprph_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    char *addr = (char *)desc->our_ota_addr.val;
    ESP_LOGI(TAG, "handle=%d our_ota_addr_type=%d our_ota_addr=%02X:%02X:%02X:%02X:%02X:%02X",
                desc->conn_handle, desc->our_ota_addr.type,
                addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    addr = (char *)desc->our_id_addr.val;
    ESP_LOGI(TAG, " our_id_addr_type=%d our_id_addr=%02X:%02X:%02X:%02X:%02X:%02X",
                desc->our_id_addr.type, addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    addr = (char *)desc->peer_ota_addr.val;
    ESP_LOGI(TAG, " peer_ota_addr_type=%d peer_ota_addr=%02X:%02X:%02X:%02X:%02X:%02X",
                desc->peer_ota_addr.type, addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    addr = (char *)desc->peer_id_addr.val;
    ESP_LOGI(TAG, " peer_id_addr_type=%d peer_id_addr=%02X:%02X:%02X:%02X:%02X:%02X",
                desc->peer_id_addr.type, addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    ESP_LOGI(TAG, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

/**
 * Enables advertising with Seeed defined data
 */
static void bleprph_advertise_start(void)
{
    struct ble_gap_adv_params adv_params;
    const char *name;
    int rc;

    rc = ble_gap_adv_set_data(adv_data, sizeof(adv_data));
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
        return;
    }
}

static void bleprph_advertise_stop(void)
{
    ble_gap_adv_stop();
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        ESP_LOGI(TAG, "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            bleprph_print_conn_desc(&desc);
            atomic_store(&g_ble_connected, true);
            g_curr_ble_conn_handle = event->connect.conn_handle;
            bool status = true;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, &status, sizeof(bool), pdMS_TO_TICKS(10000));
        }

        if (event->connect.status != 0 && atomic_load(&g_ble_adv)) {
            /* Connection failed; resume advertising. */
            bleprph_advertise_start();
        }

        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d ", event->disconnect.reason);
        bleprph_print_conn_desc(&event->disconnect.conn);
        atomic_store(&g_ble_connected, false);
        bool status = false;
        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, &status, sizeof(bool), pdMS_TO_TICKS(10000));
        
        /* Connection terminated; resume advertising. */
        if (atomic_load(&g_ble_adv))
            bleprph_advertise_start();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        ESP_LOGI(TAG, "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertise complete; reason=%d",
                    event->adv_complete.reason);
        if (atomic_load(&g_ble_adv))
            bleprph_advertise_start();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        ESP_LOGI(TAG, "encryption change event; status=%d ",
                    event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        ESP_LOGI(TAG, "notify_tx event; conn_handle=%d attr_handle=%d "
                    "status=%d is_indication=%d",
                    event->notify_tx.conn_handle,
                    event->notify_tx.attr_handle,
                    event->notify_tx.status,
                    event->notify_tx.indication);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d "
                    "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.reason,
                    event->subscribe.prev_notify,
                    event->subscribe.cur_notify,
                    event->subscribe.prev_indicate,
                    event->subscribe.cur_indicate);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        if (event->mtu.value < 20) {
            ESP_LOGW(TAG, "mtu become less than 20??? really?");
        }
        atomic_store(&g_curr_mtu, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(TAG, "PASSKEY_ACTION_EVENT started");
        // struct ble_sm_io pkey = {0};
        // int key = 0;

        // if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
        //     pkey.action = event->passkey.params.action;
        //     pkey.passkey = 123456; // This is the passkey to be entered on peer
        //     ESP_LOGI(TAG, "Enter passkey %" PRIu32 "on the peer side", pkey.passkey);
        //     rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        //     ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        // } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
        //     ESP_LOGI(TAG, "Passkey on device's display: %" PRIu32 , event->passkey.params.numcmp);
        //     ESP_LOGI(TAG, "Accept or reject the passkey through console in this format -> key Y or key N");
        //     pkey.action = event->passkey.params.action;
        //     if (scli_receive_key(&key)) {
        //         pkey.numcmp_accept = key;
        //     } else {
        //         pkey.numcmp_accept = 0;
        //         ESP_LOGE(TAG, "Timeout! Rejecting the key");
        //     }
        //     rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        //     ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        // } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
        //     static uint8_t tem_oob[16] = {0};
        //     pkey.action = event->passkey.params.action;
        //     for (int i = 0; i < 16; i++) {
        //         pkey.oob[i] = tem_oob[i];
        //     }
        //     rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        //     ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        // } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
        //     ESP_LOGI(TAG, "Enter the passkey through console in this format-> key 123456");
        //     pkey.action = event->passkey.params.action;
        //     if (scli_receive_key(&key)) {
        //         pkey.passkey = key;
        //     } else {
        //         pkey.passkey = 0;
        //         ESP_LOGE(TAG, "Timeout! Passing 0 as the key");
        //     }
        //     rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        //     ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        // }
        return 0;

    case BLE_GAP_EVENT_AUTHORIZE:
        ESP_LOGI(TAG, "authorize event: conn_handle=%d attr_handle=%d is_read=%d",
                    event->authorize.conn_handle,
                    event->authorize.attr_handle,
                    event->authorize.is_read);

        /* The default behaviour for the event is to reject authorize request */
        event->authorize.out_response = BLE_GAP_AUTHORIZE_REJECT;
        return 0;
    }

    return 0;
}

static void __bleprph_on_reset(int reason)
{
    ESP_LOGI(TAG, ">>> on_reset, reason=%d\n", reason);
    atomic_store(&g_ble_synced, false);
}

static void __bleprph_on_sync(void)
{
    int rc;

    ESP_LOGI(TAG, ">>> on_sync ...");

    /* Make sure we have proper identity address set (public preferred) */
    ESP_ERROR_CHECK(ble_hs_util_ensure_addr(0));

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "error determining address type; rc=%d", rc);
        return;
    }

    /* Printing ADDR */
    uint8_t addr_val[6];
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    xSemaphoreTake(g_sem_mac_addr, pdMS_TO_TICKS(10000));
    memset(ble_mac_addr, 0, sizeof(ble_mac_addr));
    for (int i = 0; i < 6 && rc == 0; i++)
    {
        ble_mac_addr[i] = addr_val[5 - i];
    }
    xSemaphoreGive(g_sem_mac_addr);

    ESP_LOGI(TAG, "BLE Address: %02X:%02X:%02X:%02X:%02X:%02X", 
                    addr_val[5], addr_val[4], addr_val[3], addr_val[2], addr_val[1], addr_val[0]);

    atomic_store(&g_ble_synced, true);
}

static void __bleprph_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();
    nimble_port_freertos_deinit();
    ESP_LOGD(TAG, "BLE Host Task Ended");
}

static void __ble_monitor_task(void *p_arg)
{
    bool last_ble_adv_state = false;

    while (1) {
        bool cur_ble_adv = atomic_load(&g_ble_adv);
        if (cur_ble_adv) {
            if (cur_ble_adv != last_ble_adv_state && atomic_load(&g_ble_synced) && !atomic_load(&g_ble_connected)) {
                bleprph_advertise_start();
                last_ble_adv_state = cur_ble_adv;
            }
        } else {
            if (cur_ble_adv != last_ble_adv_state && atomic_load(&g_ble_synced)) {

                if (atomic_load(&g_ble_connected)) {
                    //wait app_ble_send_indicate() done if it's under call
                    while (xEventGroupGetBits(g_eg_ble) & EVENT_INDICATE_SENDING) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                    ESP_LOGW(TAG, "going to terminate the active connections!!!");
                    ble_gap_terminate(g_curr_ble_conn_handle, BLE_HS_EDISABLED);
                }

                bleprph_advertise_stop();

                last_ble_adv_state = cur_ble_adv;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t app_ble_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    esp_err_t ret;

    g_sem_mac_addr = xSemaphoreCreateMutex();
    g_sem_data = xSemaphoreCreateMutex();
    g_eg_ble = xEventGroupCreate();

    ret = nimble_port_init();
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to init nimble %d ", ret);

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = __bleprph_on_reset;
    ble_hs_cfg.sync_cb = __bleprph_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;  //Security Manager Local Input Output Capabilities
    ble_hs_cfg.sm_sc = 0;  //Security Manager Secure Connections flag

    ESP_ERROR_CHECK(gatt_svr_init());

    const char *sn = factory_info_sn_get();
    if( sn != NULL ) {
        memcpy(adv_data + 8, sn, SENSECAP_SN_STR_LEN);
    }
    char ble_name[24] = { 0 };
    memcpy((char *)ble_name, adv_data + 8, SENSECAP_SN_STR_LEN + 5/*-WACH*/);
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(ble_name));

    ble_store_config_init();

    nimble_port_freertos_init(__bleprph_host_task);

    //create a side task to monitor the ble switch
    const uint32_t stack_size = 10 * 1024;
    StackType_t *task_stack1 = (StackType_t *)psram_calloc(1, stack_size * sizeof(StackType_t));
    StaticTask_t *task_tcb1 = heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    xTaskCreateStatic(__ble_monitor_task, "app_ble_monitor", stack_size, NULL, 4, task_stack1, task_tcb1);

    return ret;
}

uint8_t *app_ble_get_mac_address(void)
{
    uint8_t *btaddr = NULL;

    xSemaphoreTake(g_sem_mac_addr, pdMS_TO_TICKS(10000));
    if (ble_mac_addr[5] != 0)
        btaddr = ble_mac_addr;
    xSemaphoreGive(g_sem_mac_addr);

    return btaddr;
}

esp_err_t app_ble_adv_switch(bool switch_on)
{
    atomic_store(&g_ble_adv, switch_on);
    return ESP_OK;
}

int app_ble_get_current_mtu(void)
{
    int mtu = atomic_load(&g_curr_mtu);
    return mtu;
}

esp_err_t app_ble_adv_pause(void)
{
    
    xSemaphoreTake(g_sem_data, pdMS_TO_TICKS(10000));
    int cnt = atomic_load(&g_ble_adv_pause_cnt) + 1 ;
    atomic_store(&g_ble_adv_pause_cnt, cnt);
    app_ble_adv_switch(0);
    ESP_LOGI(TAG, "ble pause: %d", cnt );
    xSemaphoreGive(g_sem_data);
    return ESP_OK;
}

esp_err_t app_ble_adv_resume( int cur_switch )
{
    xSemaphoreTake(g_sem_data, pdMS_TO_TICKS(10000));
    int cnt = atomic_load(&g_ble_adv_pause_cnt);
    if (cnt > 0) {
        cnt = cnt - 1;
        atomic_store(&g_ble_adv_pause_cnt, cnt);
    }
    if( cnt == 0) {
        app_ble_adv_switch(cur_switch);
    }
    ESP_LOGI(TAG, "ble resume: %d", cnt);
    xSemaphoreGive(g_sem_data);
    return ESP_OK;
}

/**
 * Send an indicate to NimBLE stack, normally from the at cmd task context.
*/
esp_err_t app_ble_send_indicate(uint8_t *data, int len)
{
    if (!atomic_load(&g_ble_connected) || !atomic_load(&g_ble_adv)) return BLE_HS_ENOTCONN;
    int rc = ESP_FAIL;
    struct os_mbuf *txom = NULL;

    xEventGroupSetBits(g_eg_ble, EVENT_INDICATE_SENDING);

    int mtu = app_ble_get_current_mtu();
    int txlen = 0, txed_len = 0;

    const int wait_step_climb = 10, wait_max = 10000;  //ms
    int wait = 0, wait_step = wait_step_climb, wait_sum = 0;  //ms
    const int retry_max = 10;
    int retry_cnt = 0;
    while (len > 0 && atomic_load(&g_ble_connected) && atomic_load(&g_ble_adv)) {
        txlen = MIN(len, mtu - 3);
        txom = ble_hs_mbuf_from_flat(data + txed_len, txlen);
        ESP_LOGD(TAG, "after mbuf alloc, os_msys_count: %d, os_msys_num_free: %d", os_msys_count(), os_msys_num_free());
        if (!txom) {
            wait += wait_step;
            wait_step += wait_step_climb;
            ESP_LOGD(TAG, "app_ble_send_indicate, mbuf alloc failed, wait %dms", wait);
            vTaskDelay(pdMS_TO_TICKS(wait));
            wait_sum += wait;
            if (wait_sum > wait_max) {
                ESP_LOGE(TAG, "app_ble_send_indicate, mbuf alloc timeout!!!");
                rc = BLE_HS_ENOMEM;
                goto indicate_end;
            }
            continue;
        }
        wait = wait_sum = 0;
        wait_step = wait_step_climb;
        
        rc = ble_gatts_indicate_custom(g_curr_ble_conn_handle, gatt_svr_chr_handle_read, txom);
        //txom will be consumed anyways, we don't need to release it here.
        if (rc != 0) {
            ESP_LOGD(TAG, "ble_gatts_indicate_custom failed (rc=%d, mtu=%d, txlen=%d, remain_len=%d), retry ...", rc, mtu, txlen, len);
            retry_cnt++;
            if (retry_cnt > retry_max) {
                ESP_LOGE(TAG, "ble_gatts_indicate_custom failed overall after %d retries!!!", retry_max);
                rc = BLE_HS_ESTALLED;
                goto indicate_end;
            }
            continue;
        }
        txed_len += txlen;
        len -= txlen;
        ESP_LOGI(TAG, "indication sent successfully, mtu=%d, txlen=%d, remain_len=%d", mtu, txlen, len);
        // vTaskDelay(pdMS_TO_TICKS(100));  // to avoid watchdog dead in cpu1
    }

    if (len != 0) {
        rc = BLE_ERR_CONN_TERM_LOCAL;
    }

indicate_end:
    ESP_LOGD(TAG, "before app_ble_send_indicate return, os_msys_count: %d, os_msys_num_free: %d", os_msys_count(), os_msys_num_free());
    xEventGroupClearBits(g_eg_ble, EVENT_INDICATE_SENDING);
    return rc;
}
