// ui_Page_WifiScan.c  — ARIA: on-device Wi-Fi network scan + selection screen.
// Not from SquareLine. Mirrors the project's manual-focus pattern (like
// ui_Page_Network): screen is lazily created by lv_pm_open_page, focus is
// managed directly with lv_group_*.
//
// Scan uses the firmware's existing, proven pattern (see at_cmd.c handle_wifi_table):
//   resetWiFiStack(&wifiStack_scanned); xTaskNotifyGive(xTask_wifi_config_entry);
//   then the wifi_config task runs the BLOCKING wifi_scan() and gives
//   xBinarySemaphore_wifitable. We must NOT block the LVGL thread, so instead of
//   xSemaphoreTake(..., portMAX_DELAY) we poll it with a 0 timeout from an
//   lv_timer (which runs on the LVGL task — UI-safe).

#include "../ui.h"
#include "../../ui_manager/pm.h"   // lv_pm_open_page, PM_CLEAR_GROUP
#include "esp_log.h"
#include "esp_wifi.h"
#include "app_wifi.h"
#include "event_loops.h"   // app_event_loop_handle, VIEW_EVENT_BASE
#include "data_defs.h"     // struct view_data_wifi_config, VIEW_EVENT_WIFI_CONNECT
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "storage.h"       // ARIA: NVS store of saved networks (Stage 2)
#include <string.h>

// from pm.c / at_cmd.c (extern'd here to avoid include-path churn)
extern lv_group_t *g_main;
extern SemaphoreHandle_t xBinarySemaphore_wifitable;
extern void resetWiFiStack(WiFiStack *stack);

#define WIFISCAN_ROWS 8
static const char *TAGW = "wifiscan";

lv_obj_t *ui_Page_WifiScan;
lv_obj_t *ui_wifiscan_title;
lv_obj_t *ui_wifiscan_list;
lv_obj_t *ui_wifiscan_rows[WIFISCAN_ROWS];
lv_obj_t *ui_wifiscan_lbls[WIFISCAN_ROWS];
lv_obj_t *ui_wifiscan_back;

static lv_timer_t *s_scan_timer = NULL;
static bool s_scanning = false;
static uint32_t s_ticks = 0;

// ── Stage 2: saved networks (NVS) ─────────────────────────────────────────
#define WIFI_SAVED_MAX 8
typedef struct { char ssid[33]; char pw[65]; } wifi_saved_t;
static wifi_saved_t s_saved[WIFI_SAVED_MAX];
static char s_sel_pw[65];   // password used for the in-flight connect (to save on success)

static void wifi_saved_load(void)
{
    size_t len = sizeof(s_saved);
    char key[] = "wifi_saved";
    if (storage_read(key, s_saved, &len) != ESP_OK || len != sizeof(s_saved)) {
        memset(s_saved, 0, sizeof(s_saved));
    }
}
static const char *wifi_saved_lookup(const char *ssid)
{
    if (!ssid) return NULL;
    for (int i = 0; i < WIFI_SAVED_MAX; i++)
        if (s_saved[i].ssid[0] && strcmp(s_saved[i].ssid, ssid) == 0) return s_saved[i].pw;
    return NULL;
}
static void wifi_saved_add(const char *ssid, const char *pw)
{
    if (!ssid || !ssid[0]) return;
    int slot = -1, empty = -1;
    for (int i = 0; i < WIFI_SAVED_MAX; i++) {
        if (s_saved[i].ssid[0] && strcmp(s_saved[i].ssid, ssid) == 0) { slot = i; break; }
        if (empty < 0 && !s_saved[i].ssid[0]) empty = i;
    }
    if (slot < 0) slot = (empty >= 0) ? empty : 0;   // overwrite oldest if full
    strlcpy(s_saved[slot].ssid, ssid, sizeof(s_saved[slot].ssid));
    strlcpy(s_saved[slot].pw, pw ? pw : "", sizeof(s_saved[slot].pw));
    char key[] = "wifi_saved";
    storage_write(key, s_saved, sizeof(s_saved));
}

static void __render_results(void)
{
    int n = wifiStack_scanned.size;
    if (n > WIFISCAN_ROWS) n = WIFISCAN_ROWS;

    lv_group_remove_all_objs(g_main);
    int focus_first = -1;
    for (int i = 0; i < WIFISCAN_ROWS; i++) {
        if (i < n && wifiStack_scanned.entries[i].ssid && wifiStack_scanned.entries[i].ssid[0]) {
            char rowbuf[48];
            strlcpy(rowbuf, wifiStack_scanned.entries[i].ssid, sizeof(rowbuf));
            if (wifi_saved_lookup(wifiStack_scanned.entries[i].ssid)) strlcat(rowbuf, "  " LV_SYMBOL_OK, sizeof(rowbuf));  // saved
            lv_label_set_text(ui_wifiscan_lbls[i], rowbuf);
            lv_obj_clear_flag(ui_wifiscan_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_group_add_obj(g_main, ui_wifiscan_rows[i]);
            if (focus_first < 0) focus_first = i;
        } else {
            lv_obj_add_flag(ui_wifiscan_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_group_add_obj(g_main, ui_wifiscan_back);
    lv_label_set_text(ui_wifiscan_title, focus_first >= 0 ? "Wi-Fi" : "No networks");
    lv_group_focus_obj(focus_first >= 0 ? ui_wifiscan_rows[focus_first] : ui_wifiscan_back);
}

static void __scan_timer_cb(lv_timer_t *t)
{
    // Self-stop if the user navigated away from this screen.
    if (lv_scr_act() != ui_Page_WifiScan) {
        if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
        s_scanning = false;
        return;
    }
    if (!s_scanning) return;

    if (xSemaphoreTake(xBinarySemaphore_wifitable, 0) == pdTRUE) {
        s_scanning = false;
        ESP_LOGI(TAGW, "scan done, %d networks", wifiStack_scanned.size);
        __render_results();
        if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
    } else if (++s_ticks > 20) {   // ~10s timeout (500ms * 20)
        s_scanning = false;
        ESP_LOGW(TAGW, "scan timeout");
        __render_results();
        if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
    }
}

// Trigger a scan and start polling for the result. Safe to call from a UI click.
void ui_wifiscan_start(void)
{
    wifi_saved_load();   // Stage 2: know which networks we've saved
    for (int i = 0; i < WIFISCAN_ROWS; i++) lv_obj_add_flag(ui_wifiscan_rows[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_wifiscan_title, "Scanning...");
    lv_group_remove_all_objs(g_main);
    lv_group_add_obj(g_main, ui_wifiscan_back);
    lv_group_focus_obj(ui_wifiscan_back);

    // drain any stale completion signal, then kick a fresh scan
    xSemaphoreTake(xBinarySemaphore_wifitable, 0);
    resetWiFiStack(&wifiStack_scanned);
    if (xTask_wifi_config_entry) xTaskNotifyGive(xTask_wifi_config_entry);

    s_scanning = true;
    s_ticks = 0;
    if (s_scan_timer) { lv_timer_del(s_scan_timer); s_scan_timer = NULL; }
    s_scan_timer = lv_timer_create(__scan_timer_cb, 500, NULL);
}

void ui_Page_WifiScan_screen_init(void)
{
    ui_Page_WifiScan = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Page_WifiScan, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Page_WifiScan, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Page_WifiScan, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_wifiscan_title = lv_label_create(ui_Page_WifiScan);
    lv_label_set_text(ui_wifiscan_title, "Wi-Fi");
    lv_obj_set_align(ui_wifiscan_title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_wifiscan_title, 14);
    lv_obj_set_style_text_color(ui_wifiscan_title, lv_color_hex(0xA9DE2C), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_wifiscan_title, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_wifiscan_list = lv_obj_create(ui_Page_WifiScan);
    lv_obj_set_width(ui_wifiscan_list, 392);
    lv_obj_set_height(ui_wifiscan_list, 296);
    lv_obj_set_align(ui_wifiscan_list, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_wifiscan_list, 8);
    lv_obj_set_flex_flow(ui_wifiscan_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_wifiscan_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(ui_wifiscan_list, LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scrollbar_mode(ui_wifiscan_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui_wifiscan_list, LV_DIR_VER);
    lv_obj_set_style_bg_opa(ui_wifiscan_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_wifiscan_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(ui_wifiscan_list, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    for (int i = 0; i < WIFISCAN_ROWS; i++) {
        lv_obj_t *row = lv_btn_create(ui_wifiscan_list);
        lv_obj_set_width(row, 360);
        lv_obj_set_height(row, 48);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_style_radius(row, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x222222), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(row, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x8FC31F), LV_PART_MAIN | LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(row, 220, LV_PART_MAIN | LV_STATE_FOCUSED);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, 332);
        lv_label_set_text(lbl, "");
        lv_obj_set_align(lbl, LV_ALIGN_LEFT_MID);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        ui_wifiscan_rows[i] = row;
        ui_wifiscan_lbls[i] = lbl;
        lv_obj_add_event_cb(row, ui_event_wifiscan_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    ui_wifiscan_back = lv_btn_create(ui_Page_WifiScan);
    lv_obj_set_width(ui_wifiscan_back, 130);
    lv_obj_set_height(ui_wifiscan_back, 44);
    lv_obj_set_align(ui_wifiscan_back, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(ui_wifiscan_back, -6);
    lv_obj_set_style_radius(ui_wifiscan_back, 22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_wifiscan_back, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_wifiscan_back, lv_color_hex(0x8FC31F), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_t *bl = lv_label_create(ui_wifiscan_back);
    lv_label_set_text(bl, "Back");
    lv_obj_center(bl);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_wifiscan_back, ui_event_wifiscan_back, LV_EVENT_CLICKED, NULL);
}

// ───────────────────────── Password entry + connect ──────────────────────
lv_obj_t *ui_Page_WifiPwd;
lv_obj_t *ui_wifipwd_title;
lv_obj_t *ui_wifipwd_ta;
lv_obj_t *ui_wifipwd_kb;
lv_obj_t *ui_wifipwd_status;

static char s_sel_ssid[33];
static lv_timer_t *s_conn_timer = NULL;
static uint32_t s_conn_ticks = 0;

// Show connection status on whichever wifi screen is active.
static void __set_conn_status(const char *txt)
{
    if (lv_scr_act() == ui_Page_WifiPwd && ui_wifipwd_status) lv_label_set_text(ui_wifipwd_status, txt);
    else if (lv_scr_act() == ui_Page_WifiScan && ui_wifiscan_title) lv_label_set_text(ui_wifiscan_title, txt);
}

static void __do_connect(const char *ssid, const char *password, bool have_pw)
{
    struct view_data_wifi_config cfg = {0};
    strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));
    if (have_pw && password && password[0]) {
        strlcpy(cfg.password, password, sizeof(cfg.password));
        cfg.have_password = true;
    } else {
        cfg.have_password = false;
    }
    ESP_LOGI(TAGW, "connecting to '%s' (pw=%d)", ssid, (int)cfg.have_password);
    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT,
                      &cfg, sizeof(cfg), pdMS_TO_TICKS(10000));
}

// Poll for connection result (LVGL thread, UI-safe).
static void __conn_timer_cb(lv_timer_t *t)
{
    wifi_ap_record_t rec = {0};
    current_wifi_get(&rec);
    if (rec.ssid[0] && strcmp((char *)rec.ssid, s_sel_ssid) == 0) {
        __set_conn_status("Connected!");
        if (s_sel_pw[0]) wifi_saved_add(s_sel_ssid, s_sel_pw);   // Stage 2: remember it for next time
        if (s_conn_timer) { lv_timer_del(s_conn_timer); s_conn_timer = NULL; }
        return;
    }
    if (++s_conn_ticks > 12) {   // ~12s
        __set_conn_status("Failed - check password");
        if (s_conn_timer) { lv_timer_del(s_conn_timer); s_conn_timer = NULL; }
    }
}

static void __start_conn_poll(void)
{
    s_conn_ticks = 0;
    if (s_conn_timer) { lv_timer_del(s_conn_timer); s_conn_timer = NULL; }
    s_conn_timer = lv_timer_create(__conn_timer_cb, 1000, NULL);
}

static void __pwd_kb_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {            // checkmark -> connect
        const char *pw = lv_textarea_get_text(ui_wifipwd_ta);
        strlcpy(s_sel_pw, pw, sizeof(s_sel_pw));   // remember to save on success
        __set_conn_status("Connecting...");
        __do_connect(s_sel_ssid, pw, true);
        __start_conn_poll();
    } else if (code == LV_EVENT_CANCEL) {    // X -> back to the scan list
        lv_pm_open_page(g_main, NULL, PM_CLEAR_GROUP, &ui_Page_WifiScan, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_WifiScan_screen_init);
        ui_wifiscan_start();
    }
}

// Custom keyboard for the ROUND display: OK (connect) and CLOSE (back) sit in
// the visible CENTRE (the default puts them in the clipped bottom corners),
// with a short space shoved to the (clipped, blank) sides. Same ctrl for all
// three modes (lower/upper/special) since their row counts match: 10/9/9/4.
#define KB_CTRL (LV_BTNMATRIX_CTRL_NO_REPEAT | LV_BTNMATRIX_CTRL_CLICK_TRIG)
static const char * const kb_map_lc[] = {
    "q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","\n",
    "ABC","z","x","c","v","b","n","m",LV_SYMBOL_BACKSPACE,"\n",
    "1#",LV_SYMBOL_CLOSE,LV_SYMBOL_OK," ",""
};
static const char * const kb_map_uc[] = {
    "Q","W","E","R","T","Y","U","I","O","P","\n",
    "A","S","D","F","G","H","J","K","L","\n",
    "abc","Z","X","C","V","B","N","M",LV_SYMBOL_BACKSPACE,"\n",
    "1#",LV_SYMBOL_CLOSE,LV_SYMBOL_OK," ",""
};
static const char * const kb_map_spec[] = {
    "1","2","3","4","5","6","7","8","9","0","\n",
    "@","#","$","_","&","-","+","(",")","\n",
    "abc","/","*","\"","'",":",";","?",LV_SYMBOL_BACKSPACE,"\n",
    "1#",LV_SYMBOL_CLOSE,LV_SYMBOL_OK," ",""
};
static const lv_btnmatrix_ctrl_t kb_ctrl[] = {
    2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,
    KB_CTRL | 3, 2,2,2,2,2,2,2, KB_CTRL | 3,
    KB_CTRL | 3, KB_CTRL | 2, KB_CTRL | 2, 3
};

void ui_Page_WifiPwd_screen_init(void)
{
    // NOTE: 412x412 ROUND display — keep content within the centre band, away
    // from the clipped corners. Keyboard is lifted so its OK/cancel keys are
    // not hidden in the bottom corners.
    ui_Page_WifiPwd = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Page_WifiPwd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Page_WifiPwd, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Page_WifiPwd, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_wifipwd_title = lv_label_create(ui_Page_WifiPwd);
    lv_label_set_long_mode(ui_wifipwd_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ui_wifipwd_title, 230);
    lv_label_set_text(ui_wifipwd_title, "");
    lv_obj_set_align(ui_wifipwd_title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_wifipwd_title, 48);
    lv_obj_set_style_text_align(ui_wifipwd_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_wifipwd_title, lv_color_hex(0xA9DE2C), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_wifipwd_title, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_wifipwd_ta = lv_textarea_create(ui_Page_WifiPwd);
    lv_textarea_set_one_line(ui_wifipwd_ta, true);
    lv_textarea_set_placeholder_text(ui_wifipwd_ta, "password");
    lv_obj_set_width(ui_wifipwd_ta, 240);
    lv_obj_set_align(ui_wifipwd_ta, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_wifipwd_ta, 86);
    lv_obj_set_style_text_align(ui_wifipwd_ta, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_wifipwd_ta, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Combined hint/status line — sits clearly between the password box and the
    // keyboard (no longer overlaps the password box).
    ui_wifipwd_status = lv_label_create(ui_Page_WifiPwd);
    lv_label_set_text(ui_wifipwd_status, "");
    lv_obj_set_width(ui_wifipwd_status, 290);
    lv_label_set_long_mode(ui_wifipwd_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_align(ui_wifipwd_status, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_wifipwd_status, 144);
    lv_obj_set_style_text_align(ui_wifipwd_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_wifipwd_status, lv_color_hex(0xCFCFCF), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_wifipwd_kb = lv_keyboard_create(ui_Page_WifiPwd);
    // ROUND display: keep the keyboard narrow so EVERY row (not just OK/cancel)
    // fits inside the visible circle instead of clipping at the curved sides.
    lv_obj_set_size(ui_wifipwd_kb, 300, 196);
    lv_obj_set_align(ui_wifipwd_kb, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(ui_wifipwd_kb, -10);
    lv_obj_set_style_pad_all(ui_wifipwd_kb, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(ui_wifipwd_kb, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_keyboard_set_map(ui_wifipwd_kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_lc,   kb_ctrl);
    lv_keyboard_set_map(ui_wifipwd_kb, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_uc,   kb_ctrl);
    lv_keyboard_set_map(ui_wifipwd_kb, LV_KEYBOARD_MODE_SPECIAL,    kb_map_spec, kb_ctrl);
    lv_keyboard_set_textarea(ui_wifipwd_kb, ui_wifipwd_ta);
    lv_obj_add_event_cb(ui_wifipwd_kb, __pwd_kb_event, LV_EVENT_ALL, NULL);
}

// Open the password screen for a given SSID.
void ui_wifipwd_open(const char *ssid)
{
    strlcpy(s_sel_ssid, ssid, sizeof(s_sel_ssid));
    lv_pm_open_page(g_main, NULL, PM_CLEAR_GROUP, &ui_Page_WifiPwd, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_WifiPwd_screen_init);
    lv_label_set_text(ui_wifipwd_title, ssid);
    lv_textarea_set_text(ui_wifipwd_ta, "");
    lv_label_set_text(ui_wifipwd_status, LV_SYMBOL_OK " connect    " LV_SYMBOL_CLOSE " back");
    lv_group_remove_all_objs(g_main);
    lv_group_add_obj(g_main, ui_wifipwd_kb);
    lv_group_focus_obj(ui_wifipwd_kb);
}

// A scanned-network row was selected: open networks connect directly, locked
// networks open the password keyboard.
void ui_wifiscan_row_selected(int idx)
{
    if (idx < 0 || idx >= wifiStack_scanned.size) return;
    const char *ssid = wifiStack_scanned.entries[idx].ssid;
    const char *enc  = wifiStack_scanned.entries[idx].encryption;
    if (!ssid || !ssid[0]) return;
    bool open = (enc && (strcmp(enc, "OPEN") == 0 || strcmp(enc, "open") == 0 || enc[0] == '\0'));
    const char *saved = wifi_saved_lookup(ssid);
    if (open) {
        strlcpy(s_sel_ssid, ssid, sizeof(s_sel_ssid));
        s_sel_pw[0] = '\0';
        __set_conn_status("Connecting...");
        __do_connect(ssid, NULL, false);
        __start_conn_poll();
    } else if (saved) {
        // Stage 2: known network -> reconnect instantly with the saved password
        strlcpy(s_sel_ssid, ssid, sizeof(s_sel_ssid));
        strlcpy(s_sel_pw, saved, sizeof(s_sel_pw));
        __set_conn_status("Connecting...");
        __do_connect(ssid, saved, true);
        __start_conn_poll();
    } else {
        ui_wifipwd_open(ssid);
    }
}
