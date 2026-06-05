#include "view.h"
#include "view_image_preview.h"
#include "view_alarm.h"
#include "view_pages.h"
#include "sensecap-watcher.h"

#include "util.h"
#include "ui/ui_helpers.h"
#include <time.h>
#include "app_device_info.h"
#include "app_png.h"
#include "app_voice_interaction.h"

#include "ui_manager/pm.h"
#include "ui_manager/animation.h"
#include "ui_manager/event.h"

static const char *TAG = "view";

extern BuiltInEmojiCount builtin_emoji_count;
extern CustomEmojiCount custom_emoji_count;
extern int cur_loaded_png_count;

static int png_loading_count = 0;
static bool battery_flag_toggle = 0;
static int battery_blink_count = 0;
static uint8_t system_mode = 0;     // 0: normal; 1: sleep; 2: standby
static uint32_t inactive_time = 0;

static struct view_data_ota_status ota_st;

lv_obj_t * pre_foucsed_obj = NULL;
uint8_t g_group_layer_ = 0;
uint8_t g_shutdown = 0;
uint8_t g_dev_binded = 0;
uint8_t g_push2talk_status = 0;
uint8_t g_taskflow_pause = 0;
uint8_t g_is_push2talk = 0;
extern uint8_t g_taskdown;
extern uint8_t g_swipeid; // 0 for shutdown, 1 for factoryreset
extern int g_guide_disable;
extern uint8_t g_avarlive;
extern uint8_t g_tasktype;
extern uint8_t g_backpage;
extern uint8_t g_push2talk_timer;
uint8_t g_push2talk_mode = 0;
char *push2talk_item[TASK_CFG_ID_MAX];
extern lv_obj_t *push2talk_textarea;

// sleep mode
extern int g_screenoff_switch;
extern uint8_t screenoff_mode;

extern uint8_t emoji_switch_scr;

extern lv_img_dsc_t *g_detect_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_speak_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_listen_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_analyze_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_standby_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_greet_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_detected_img_dsc[MAX_IMAGES];

extern lv_obj_t * ui_taskerrt2;
extern lv_obj_t * ui_task_error;

// view standby 
extern lv_obj_t * ui_Page_Standby;

// view_alarm obj extern
extern lv_obj_t * ui_viewavap;
extern lv_obj_t * ui_viewpbtn1;
extern lv_obj_t * ui_viewpt1;
extern lv_obj_t * ui_viewpbtn2;
extern lv_obj_t * ui_viewpt2;
extern lv_obj_t * ui_viewpbtn3;

// view standby 
extern lv_obj_t * ui_Page_Standby;

extern lv_obj_t * ui_Page_Emoji;
extern lv_obj_t * ui_failed;
extern lv_obj_t * ui_facet;
extern lv_obj_t * ui_facearc;
extern lv_obj_t * ui_faceper;
extern lv_obj_t * ui_facetper;
extern lv_obj_t * ui_facetsym;
extern lv_obj_t * ui_emoticonok;

extern GroupInfo group_page_main;
extern GroupInfo group_page_template;
extern GroupInfo group_page_notask;
extern GroupInfo group_page_extension;
extern GroupInfo group_page_set;
extern GroupInfo group_page_view;
extern GroupInfo group_page_brightness;
extern GroupInfo group_page_volume;
extern GroupInfo group_page_connectapp;
extern GroupInfo group_page_about;
extern GroupInfo group_page_guide;

extern int g_detect_image_count;
extern int g_speak_image_count;
extern int g_listen_image_count;
extern int g_analyze_image_count;
extern int g_standby_image_count;
extern int g_greet_image_count;
extern int g_detected_image_count;

static void update_ai_ota_progress(int percentage)
{
    lv_arc_set_value(ui_waitarc, percentage);
    char percentage_str[4];
    sprintf(percentage_str, "%d", percentage);
    lv_label_set_text(ui_otatper, percentage_str);
    ESP_LOGI(TAG, "OTA progress updated: %d%%", percentage);
}

static void update_ota_progress(int percentage)
{
    lv_arc_set_value(ui_otaarc, percentage);
}

static void toggle_image_visibility(lv_timer_t *timer)
{
    if(battery_flag_toggle){
        lv_obj_add_flag(ui_batteryimg, LV_OBJ_FLAG_HIDDEN);
    }else{
        lv_obj_clear_flag(ui_batteryimg, LV_OBJ_FLAG_HIDDEN);
    }
    battery_flag_toggle = !battery_flag_toggle;

    battery_blink_count++;

    if (battery_blink_count >= 4) {
        lv_timer_del(timer);
        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SHUTDOWN, NULL, 0, pdMS_TO_TICKS(10000));
    }
}

static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    lvgl_port_lock(0);
    if(base == VIEW_EVENT_BASE){
        switch (id)
        {
            case VIEW_EVENT_SCREEN_START: {
                _ui_screen_change(&ui_Page_Avatar, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Avatar_screen_init);
                break;
            }

            case VIEW_EVENT_PNG_LOADING: {
                png_loading_count++;
                static bool load_flag;
                
                int progress_percentage = (png_loading_count * 100) / cur_loaded_png_count;
                static int last_event_sent_percentage = 0;
                
                if (progress_percentage <= 100) {
                    lv_arc_set_value(ui_Arc1, progress_percentage);
                    
                    static char load_per[5];
                    sprintf(load_per, "%d%%", progress_percentage);
                    lv_label_set_text(ui_loadpert, load_per);
                }
                
                if (progress_percentage / 16 > last_event_sent_percentage / 16) {
                    lv_event_send(ui_Page_Loading, LV_EVENT_SCREEN_LOADED, NULL);
                    last_event_sent_percentage = progress_percentage;
                }
                
                break;
            }

            case VIEW_EVENT_EMOJI_DOWLOAD_BAR:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_EMOJI_DOWLOAD_BAR");
                int push2talk_direct_exit = 0;
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_VI_EXIT, &push2talk_direct_exit, sizeof(push2talk_direct_exit), pdMS_TO_TICKS(10000));
                if(ota_st.status == 1){break;}
                int *emoji_download_per = (int *)event_data;
                static char download_per[5];

                lv_obj_clear_flag(ui_Page_Emoji, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(ui_facearc, LV_OBJ_FLAG_HIDDEN);

                if(g_group_layer_ == 0)
                {
                    pre_foucsed_obj = lv_group_get_focused(g_main);
                }
                lv_group_add_obj(g_main, ui_emoticonok);
                lv_group_focus_obj(ui_emoticonok);
                lv_group_focus_freeze(g_main, true);

                lv_obj_add_flag(ui_failed, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(ui_Page_Emoji);
                lv_obj_set_style_bg_color(ui_emoticonok, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

                if(*emoji_download_per < 100){
                    lv_obj_clear_flag(ui_faceper, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_emoticonok, LV_OBJ_FLAG_HIDDEN);
                    lv_label_set_text(ui_facet, "Uploading\nface...");
                    lv_arc_set_value(ui_facearc, *emoji_download_per);
                    sprintf(download_per, "%d", *emoji_download_per);
                    lv_label_set_text(ui_facetper,download_per);
                }
                if(*emoji_download_per>=100)
                {
                    lv_obj_add_flag(ui_faceper, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(ui_emoticonok, LV_OBJ_FLAG_HIDDEN);
                    lv_arc_set_value(ui_facearc, 100);
                    lv_label_set_text(ui_facet, "Please reboot to update new faces");
                }

                g_group_layer_ = 1;
                break;
            }

            case VIEW_EVENT_EMOJI_DOWLOAD_FAILED:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_EMOJI_DOWLOAD_FAILED");
                if(ota_st.status == 1){break;}
                lv_obj_clear_flag(ui_Page_Emoji, LV_OBJ_FLAG_HIDDEN);

                if(g_group_layer_ == 0)
                {
                    pre_foucsed_obj = lv_group_get_focused(g_main);
                }
                lv_obj_clear_flag(ui_emoticonok, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_faceper, LV_OBJ_FLAG_HIDDEN);
                lv_group_add_obj(g_main, ui_emoticonok);
                lv_group_focus_obj(ui_emoticonok);
                lv_group_focus_freeze(g_main, true);

                lv_obj_add_flag(ui_facearc, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(ui_failed, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(ui_facet, "Please retry");
                lv_obj_set_style_bg_color(ui_emoticonok, lv_color_hex(0xD54941), LV_PART_MAIN | LV_STATE_DEFAULT);

                g_group_layer_ = 1;
                break;
            }

            case VIEW_EVENT_INFO_OBTAIN:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_INFO_OBTAIN");
                view_info_obtain();
                break;
            }

            case VIEW_EVENT_SCREEN_TRIGGER:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_SCREEN_TRIGGER");
                lv_disp_trig_activity(NULL);
                break;
            }

            case VIEW_EVENT_MODE_STANDBY:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_MODE_STANDBY");
                
                break;
            }

            case VIEW_EVENT_USAGE_GUIDE_SWITCH:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_USAGE_GUIDE_SWITCH");
                int *usage_guide_st = (int *)event_data;
                g_guide_disable = (*usage_guide_st);
                // ESP_LOGI(TAG, "g_guide_disable : %d", g_guide_disable);
                break;
            }

            case VIEW_EVENT_BATTERY_ST:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_BATTERY_ST");
                struct view_data_device_status * bat_st = (struct view_data_device_status *)event_data;
                ESP_LOGI(TAG, "battery_percentage: %d", bat_st->battery_per);
                static char load_per[5];
                sprintf(load_per, "%d", bat_st->battery_per);
                lv_label_set_text(ui_btpert, load_per);
                break;
            }

            case VIEW_EVENT_BAT_DRAIN_SHUTDOWN:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_BAT_DRAIN_SHUTDOWN");
                hide_all_overlays();
                _ui_screen_change(&ui_Page_Battery, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Battery_screen_init);
                lv_timer_t *timer = lv_timer_create(toggle_image_visibility, 500, NULL);
                
                break;
            }

            case VIEW_EVENT_CHARGE_ST:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_CHARGE_ST");
                uint8_t is_charging = *(uint8_t *)event_data;
                ESP_LOGI(TAG, "charging state changed: %d", is_charging);
                lv_obj_add_flag(ui_btpert, LV_OBJ_FLAG_HIDDEN);
                if(is_charging == 1)
                {
                    g_shutdown = 0;
                    if(g_swipeid==0)
                    {
                        lv_label_set_text(ui_setdownt, "Reboot");
                        lv_label_set_text(ui_sptext, "Swipe to reboot");
                        lv_label_set_text(ui_sptitle, "Reboot");
                    }
                    lv_obj_add_flag(ui_btpert, LV_OBJ_FLAG_HIDDEN);
                    lv_img_set_src(ui_mainb, &ui_img_battery_charging_png);
                }else if(is_charging == 0){
                    g_shutdown = 1;
                    if(g_swipeid==0)
                    {
                        lv_label_set_text(ui_setdownt, "Shutdown");
                        lv_label_set_text(ui_sptext, "Swipe to shut down");
                        lv_label_set_text(ui_sptitle, "Shut down");
                    }
                    lv_obj_clear_flag(ui_btpert, LV_OBJ_FLAG_HIDDEN);
                    lv_img_set_src(ui_mainb, &ui_img_battery_frame_png);
                }
                break;
            } 

            case VIEW_EVENT_TIME: {
                ESP_LOGI(TAG, "event: VIEW_EVENT_TIME");
                bool time_format_24 = true;
                if( event_data) {
                    time_format_24 = *( bool *)event_data;
                } 
                
                time_t now = 0;
                struct tm timeinfo = { 0 };

                time(&now);
                localtime_r(&now, &timeinfo);
                int hour = timeinfo.tm_hour;

                if( ! time_format_24 ) {
                    if( hour>=13 && hour<=23) {
                        hour = hour-12;
                    }
                }
                char buf1[32];
                lv_snprintf(buf1, sizeof(buf1), "%02d:%02d",hour, timeinfo.tm_min);
                lv_label_set_text(ui_maintime, buf1);
                break;
            }

            case VIEW_EVENT_WIFI_ST: {
                ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
                struct view_data_wifi_st *p_st = ( struct view_data_wifi_st *)event_data;
                uint8_t *p_src =NULL;
                if ( p_st->past_connected)
                {
                    g_dev_binded = 1;
                }else{
                    g_dev_binded = 0;
                }
                if ( p_st->is_network ) {
                    switch (wifi_rssi_level_get( p_st->rssi )) {
                        case 1:
                            p_src = &ui_img_wifi_0_png;
                            break;
                        case 2:
                            p_src = &ui_img_wifi_1_png;
                            break;
                        case 3:
                            p_src = &ui_img_wifi_2_png;
                        case 4:
                            p_src = &ui_img_wifi_3_png;
                            break;
                        default:
                            break;
                    }
        
                } else if( p_st->is_connected ) {
                    p_src = &ui_img_wifi_abnormal_png;
                } else {
                    p_src = &ui_img_no_wifi_png;
                }
                lv_img_set_src(ui_mainwifi , (void *)p_src);
                break;
            }

            case VIEW_EVENT_WIFI_CONFIG_SYNC:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CONFIG_SYNC");
                if(ota_st.status == 1){break;}
                int * wifi_config_sync = (int*)event_data;
                if(lv_scr_act() != ui_Page_Network)_ui_screen_change(&ui_Page_Network, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Network_screen_init);
                if(* wifi_config_sync == 0)
                {
                    waitForWifi();
                }else if(* wifi_config_sync== 1)
                {
                    waitForBinding();
                }else if(*wifi_config_sync == 2)
                {
                    waitForAddDev();
                }else if(* wifi_config_sync == 3)
                {
                    bindFinish();
                    g_dev_binded = 1;
                    lv_obj_add_flag(ui_virp, LV_OBJ_FLAG_HIDDEN);
                    _ui_screen_change(&ui_Page_Avatar, LV_SCR_LOAD_ANIM_NONE, 0, 3000, &ui_Page_Avatar_screen_init);
                }else if(* wifi_config_sync == 4)
                {
                    wifiConnectFailed();
                }else if (* wifi_config_sync == 5)
                {
                    lv_pm_open_page(g_main, &group_page_set, PM_ADD_OBJS_TO_GROUP, &ui_Page_Set, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Set_screen_init);
                }
                break;
            }

            case VIEW_EVENT_BLE_STATUS:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_BLE_STATUS");
                bool ble_connect_status = false;
                ble_connect_status = *(bool *)event_data;
                if(ble_connect_status)
                {
                    lv_obj_set_style_img_recolor(ui_mainble, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                else{
                    lv_obj_set_style_img_recolor(ui_mainble, lv_color_hex(0x171515), LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                break;
            }

            case VIEW_EVENT_BRIGHTNESS:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_BRIGHTNESS");
                uint8_t *bri_st = (uint8_t *)event_data;
                int32_t bri_value = (int32_t)(*bri_st);
                lv_slider_set_value(ui_bslider, bri_value, LV_ANIM_OFF);
                                
                break;
            }

            case VIEW_EVENT_RGB_SWITCH:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_RGB_SWITCH");
                int * rgb_st = (int *)event_data;

                break;
            }

            case VIEW_EVENT_BLE_SWITCH:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_BLE_SWITCH, %d", *(int *)event_data);
                int *sw = (int *)event_data;

                break;
            }

            case VIEW_EVENT_SOUND:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_SOUND");
                uint8_t * vol_st = (uint8_t *)event_data;
                int32_t vol_value = (int32_t)(*vol_st);
                lv_slider_set_value(ui_vslider, (int32_t *)vol_value, LV_ANIM_OFF);
                
                break;
            }

            case VIEW_EVENT_ALARM_ON:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_ALARM_ON");
                if(ota_st.status == 1){break;}
                struct tf_module_local_alarm_info *alarm_st = (struct tf_module_local_alarm_info *)event_data;
                             
                view_alarm_on(alarm_st);
                tf_data_buf_free(&(alarm_st->text));
                tf_data_image_free(&(alarm_st->img));

                if(g_screenoff_switch == 1 && screenoff_mode == 1)
                {
                    int brightness = get_brightness(UI_CALLER);
                    bsp_lcd_brightness_set(brightness);
                }

                break;
            }

            case VIEW_EVENT_ALARM_OFF:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_ALARM_OFF");
                if(ota_st.status == 1){break;}
                uint8_t * task_st = (uint8_t *)event_data;
                view_alarm_off(task_st);

                if(screenoff_mode == 1)
                {
                    bsp_lcd_brightness_set(0);
                }

                break;
            }

            case VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK");
                if(ota_st.status == 1){break;}
                g_tasktype = 1;
                g_taskdown = 0;
                g_backpage = 1;
                lv_obj_add_flag(ui_task_error, LV_OBJ_FLAG_HIDDEN);
                _ui_screen_change(&ui_Page_Revtask, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_Revtask_screen_init);
                break;
            }

            case VIEW_EVENT_TASK_FLOW_STOP:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_STOP");
                if(g_taskflow_pause == 1)g_taskflow_pause = 0;
                g_taskdown = 1;
                if(ota_st.status == 1){break;}
                lv_obj_add_flag(ui_viewavap, LV_OBJ_FLAG_HIDDEN);
                // event_post_to
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_ALARM_OFF, &g_taskdown, sizeof(uint8_t), pdMS_TO_TICKS(10000));
                if(g_tasktype == 0)
                {
                    if(!g_is_push2talk){
                        lv_pm_open_page(g_main, &group_page_template, PM_ADD_OBJS_TO_GROUP, &ui_Page_Example, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Example_screen_init);
                    }
                }else{
                    if(!g_is_push2talk){
                        lv_pm_open_page(g_main, &group_page_main, PM_ADD_OBJS_TO_GROUP, &ui_Page_Home, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Home_screen_init);
                        lv_group_focus_obj(ui_mainbtn2);
                    }
                }
                lv_group_set_wrap(g_main, true);
                // manually trigger an activity on a display
                lv_disp_trig_activity(NULL);
                break;
            }

            case VIEW_EVENT_AI_CAMERA_READY:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_AI_CAMERA_READY");
                if(ota_st.status == 1){break;}
                if((lv_scr_act() != ui_Page_ViewAva) && (lv_scr_act() != ui_Page_ViewLive) && (lv_scr_act() != ui_Page_Revtask) && (lv_scr_act() != ui_Page_ModelOTA))
                {
                    break;
                }
                if(g_avarlive == 0)
                {
                    if(g_guide_disable)
                    {
                        if(lv_scr_act() != ui_Page_ViewAva)lv_pm_open_page(g_main, &group_page_view, PM_ADD_OBJS_TO_GROUP, &ui_Page_ViewAva, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_ViewAva_screen_init);
                        lv_group_focus_obj(ui_Page_ViewAva);
                    }else{
                        _ui_screen_change(&ui_Page_Flag, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Flag_screen_init);
                        lv_group_remove_all_objs(g_main);
                        lv_group_add_obj(g_main, ui_guidebtn1);
                        lv_group_add_obj(g_main, ui_guidebtn2);
                        emoji_switch_scr = SCREEN_GUIDE;
                        emoji_timer(EMOJI_DETECTING);
                    }
                }else if(g_avarlive == 1)
                {
                    if(g_guide_disable)
                    {
                        if(lv_scr_act() != ui_Page_ViewLive)lv_pm_open_page(g_main, &group_page_view, PM_ADD_OBJS_TO_GROUP, &ui_Page_ViewLive, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_ViewLive_screen_init);
                        lv_group_focus_obj(ui_Page_ViewLive);
                    }else{
                        _ui_screen_change(&ui_Page_Flag, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Flag_screen_init);
                        lv_group_remove_all_objs(g_main);
                        lv_group_add_obj(g_main, ui_guidebtn1);
                        lv_group_add_obj(g_main, ui_guidebtn2);
                        emoji_switch_scr = SCREEN_GUIDE;
                        emoji_timer(EMOJI_DETECTING);
                    }
                }
                break;
            }

            case VIEW_EVENT_OTA_STATUS:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_OTA_STATUS");
                struct view_data_ota_status * ota_st_ptr = (struct view_data_ota_status *)event_data;
                ota_st.status = ota_st_ptr->status;
                ota_st.percentage = ota_st_ptr->percentage;
                ota_st.err_code = ota_st_ptr->err_code;
                
                ESP_LOGI(TAG, "VIEW_EVENT_OTA_STATUS: %d", ota_st_ptr->status);

                int push2talk_direct_exit = 0;
                if(lv_scr_act() != ui_Page_OTA && ota_st_ptr->status >= 1  && ota_st_ptr->status <= 3)
                {
                    lv_group_remove_all_objs(g_main);
                    _ui_screen_change(&ui_Page_OTA, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_OTA_screen_init);
                    hide_all_overlays();
                }
                if(ota_st_ptr->status == 1)
                {
                    update_ota_progress(ota_st_ptr->percentage);
                    lv_label_set_text(ui_otastatus, "Updating\nFirmware");
                    lv_obj_clear_flag(ui_otatext, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_otaback, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_otaicon, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(ui_otaspinner, LV_OBJ_FLAG_HIDDEN);
                    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_VI_EXIT, &push2talk_direct_exit, sizeof(push2talk_direct_exit), pdMS_TO_TICKS(10000));
                }else if (ota_st_ptr->status == 2)
                {
                    ESP_LOGI(TAG, "OTA download succeeded");
                    lv_label_set_text(ui_otastatus, "Update\nSuccessful");
                    lv_obj_set_x(ui_otastatus, 0);
                    lv_obj_set_y(ui_otastatus, 0);
                    lv_obj_add_flag(ui_otatext, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(ui_otaicon, LV_OBJ_FLAG_HIDDEN);
                    lv_img_set_src(ui_otaicon, &ui_img_wifiok_png);
                    lv_obj_add_flag(ui_otaspinner, LV_OBJ_FLAG_HIDDEN);
                }else if (ota_st_ptr->status == 3){
                    ESP_LOGE(TAG, "OTA download failed, error code: %d", ota_st_ptr->err_code);
                    lv_label_set_text(ui_otastatus, "Update Failed");
                    lv_obj_add_flag(ui_otatext, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(ui_otaicon, LV_OBJ_FLAG_HIDDEN);
                    lv_img_set_src(ui_otaicon, &ui_img_error_png);
                    lv_obj_add_flag(ui_otaspinner, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(ui_otaback, LV_OBJ_FLAG_HIDDEN);
                }
                break;
            }

            case VIEW_EVENT_TASK_FLOW_ERROR:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_ERROR");
                if(g_taskflow_pause == 1)g_taskflow_pause = 0;
                g_taskdown = 1;
                if(ota_st.status == 1){break;}
                const char* error_msg = (const char*)event_data;
                lv_obj_clear_flag(ui_task_error, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(ui_task_error);
                lv_label_set_text(ui_taskerrt2, error_msg);

                lv_group_remove_all_objs(g_main);
                break;
            }

            case VIEW_EVENT_VI_TASKFLOW_PAUSE:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_TASKFLOW_PAUSE");
                g_taskflow_pause = 1;
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_ALARM_OFF, &g_taskdown, sizeof(uint8_t), pdMS_TO_TICKS(10000));

                if(ota_st.status == 1){break;}
                lv_group_remove_all_objs(g_main);
                lv_obj_add_flag(ui_viewavap, LV_OBJ_FLAG_HIDDEN);
                
                lv_label_set_text(ui_revtext, "Task pausing\nfor push to talk");
                lv_obj_add_flag(ui_task_error, LV_OBJ_FLAG_HIDDEN);
                _ui_screen_change(&ui_Page_Revtask, LV_SCR_LOAD_ANIM_FADE_ON, 0, 0, &ui_Page_Revtask_screen_init);

                break;
            }

            case VIEW_EVENT_VI_RECORDING:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_RECORDING");
                if(ota_st.status == 1){break;}
                g_is_push2talk = 1;

                view_sleep_timer_stop();
                view_push2talk_animation_timer_stop();
                view_push2talk_timer_stop();
                lv_group_remove_all_objs(g_main);

                lv_obj_add_flag(ui_push2talkpanel2, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_push2talkpanel3, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_p2texit, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(push2talk_textarea, LV_OBJ_FLAG_HIDDEN);

                hide_all_overlays();

                lv_obj_set_x(ui_push2talkknob, -17);
                lv_obj_set_y(ui_push2talkknob, 151);
                lv_obj_set_align(ui_push2talkknob, LV_ALIGN_CENTER);
                lv_img_set_src(ui_push2talkknob, &ui_img_pushtotalk_scroll_png);
                lv_label_set_text(ui_push2talkp2t1, "Scroll to exit talking mode");
                lv_obj_set_style_text_color(ui_push2talkp2t1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_arc_color(ui_push2talkarc, lv_color_hex(0x91BF25), LV_PART_INDICATOR | LV_STATE_DEFAULT);

                emoji_switch_scr = SCREEN_PUSH2TALK;
                emoji_timer(EMOJI_LISTENING);
                _ui_screen_change(&ui_Page_Push2talk, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Push2talk_screen_init);
                g_push2talk_status = EMOJI_LISTENING;

                break;
            }

            case VIEW_EVENT_VI_ANALYZING:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_ANALYZING");
                if(ota_st.status == 1){break;}
                lv_obj_clear_flag(ui_p2texit, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(push2talk_textarea, LV_OBJ_FLAG_HIDDEN);
                lv_group_add_obj(g_main, ui_Page_Push2talk);
                
                emoji_switch_scr = SCREEN_PUSH2TALK;
                emoji_timer(EMOJI_ANALYZING);
                g_push2talk_status = EMOJI_ANALYZING;

                break;
            }

            case VIEW_EVENT_VI_PLAYING:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_PLAYING");
                if(ota_st.status == 1){break;}
                view_push2talkexpired_timer_start();
                struct view_data_vi_result *push2talk_result = (struct view_data_vi_result *)event_data;
                ESP_LOGI("push2talk", "result mode : %d", push2talk_result->mode);
                // mode 0 and mode 2
                if(push2talk_result->mode == 0 || push2talk_result->mode == 2){
                    if(push2talk_result->mode == 2)
                    {
                        g_push2talk_mode = 2;
                    }else{
                        g_push2talk_mode = 0;
                    }

                    g_push2talk_timer = 0;

                    if (push2talk_result->p_audio_text != NULL) {
                        // ESP_LOGI("push2talk", "audio text : %s", push2talk_result->p_audio_text);
                        int push2talk_audio_time = push2talk_result->audio_tm_ms;
                        ESP_LOGI("push2talk", "audio time : %d", push2talk_audio_time);

                        push2talk_start_animation(push2talk_result->p_audio_text, push2talk_audio_time);
                    } else {
                        ESP_LOGI("push2talk", "audio text is NULL");
                    }

                    lv_obj_add_flag(ui_p2texit, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(push2talk_textarea, LV_OBJ_FLAG_HIDDEN);

                    emoji_switch_scr = SCREEN_PUSH2TALK_SPEAK;
                    emoji_timer(EMOJI_SPEAKING);
                    g_push2talk_status = EMOJI_SPEAKING;
                }

                // mode 1
                else if (push2talk_result->mode == 1){
                    g_push2talk_timer = 1;
                    memset(push2talk_item, 0, sizeof(push2talk_item));
                    for (int i = 0; i < TASK_CFG_ID_MAX; i++) {
                        if (push2talk_result->items[i]) {
                            ESP_LOGI("push2talk", "item %d is : %s", i, push2talk_result->items[i]);
                            push2talk_item[i] = strdup(push2talk_result->items[i]);
                        }
                    }

                    lv_obj_add_flag(ui_p2texit, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(push2talk_textarea, LV_OBJ_FLAG_HIDDEN);
                    lv_group_remove_all_objs(g_main);

                    uint32_t child_cnt = lv_obj_get_child_cnt(ui_push2talkpanel3);
                    lv_obj_t *push2talk_panel_child;

                    for(uint8_t i =0; i< child_cnt; i++)
                    {
                        push2talk_panel_child = lv_obj_get_child(ui_push2talkpanel3, i);
                        lv_obj_add_flag(push2talk_panel_child, LV_OBJ_FLAG_HIDDEN);
                    }

                    lv_obj_clear_flag(ui_push2talkpanel3, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_scroll_snap_y(ui_push2talkpanel3, LV_SCROLL_SNAP_CENTER);
                    view_push2talk_msg_timer_start();
                }

                app_vi_result_free(push2talk_result);
                break;
            }

            case VIEW_EVENT_VI_PLAY_FINISH:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_PLAY_FINISH");
                if(ota_st.status == 1){break;}

                if(g_push2talk_timer == 0 )view_push2talk_timer_start();

                break;
            }

            case VIEW_EVENT_VI_ERROR:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_ERROR");
                if(ota_st.status == 1){break;}

                view_push2talk_timer_stop();
                lv_group_remove_all_objs(g_main);

                hide_all_overlays();

                lv_obj_set_x(ui_push2talkknob, -17);
                lv_obj_set_y(ui_push2talkknob, 151);
                lv_obj_set_align(ui_push2talkknob, LV_ALIGN_CENTER);
                lv_img_set_src(ui_push2talkknob, &ui_img_pushtotalk_error_png);
                lv_obj_clear_flag(ui_push2talkpanel2, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_push2talkpanel3, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_p2texit, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(push2talk_textarea, LV_OBJ_FLAG_HIDDEN);

                lv_group_add_obj(g_main, ui_push2talkarc);

                _ui_screen_change(&ui_Page_Push2talk, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Push2talk_screen_init);

                int push2talk_error_code = *(int *)event_data;
                static char error_code_str[100];
                const char* error_message;

                switch(push2talk_error_code) {
                    case ESP_ERR_VI_NO_MEM:
                        error_message = "MEM ALLOCATION";
                        break;
                    case ESP_ERR_VI_HTTP_CONNECT:
                        error_message = "HTTP CONNECT";
                        break;
                    case ESP_ERR_VI_HTTP_WRITE:
                        error_message = "HTTP WRITE";
                        break;
                    case ESP_ERR_VI_HTTP_RESP:
                        error_message = "HTTP RESPONSE";
                        break;
                    case ESP_ERR_VI_NET_CONNECT:
                        error_message = "WIFI CONNECT";
                        break;
                    default:
                        error_message = "UNKNOW";
                        break;
                }

                snprintf(error_code_str, sizeof(error_code_str), "[ 0x%x ]\n%s\nfailed", push2talk_error_code, error_message);

                lv_label_set_text(ui_push2talkp2t1, error_code_str);
                lv_obj_set_style_text_color(ui_push2talkp2t1, lv_color_hex(0xD54941), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_arc_color(ui_push2talkarc, lv_color_hex(0xD54941), LV_PART_INDICATOR | LV_STATE_DEFAULT);

                lv_arc_set_value(ui_push2talkarc, 0);
                g_push2talk_timer = 1;
                view_push2talk_timer_start();

                break;

            }

            case VIEW_EVENT_VI_EXIT:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_EXIT");
                view_sleep_timer_start();
                view_push2talkexpired_timer_stop();
                emoji_timer_stop();
                break;
            }

            case VIEW_EVENT_SENSOR:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_SENSOR");
                struct view_data_sensor * sensor_data = (struct view_data_sensor *) event_data;
                uint32_t extension_group_count = lv_group_get_obj_count(g_main);
                if(sensor_data->co2_valid || sensor_data->humidity_valid || sensor_data->temperature_valid)
                {
                    lv_obj_add_flag(ui_extensionNone, LV_OBJ_FLAG_HIDDEN);
                    if(lv_scr_act() != ui_Page_Extension)break;
                    if(extension_group_count != 3)
                    {
                        lv_group_remove_all_objs(g_main);
                        lv_group_add_obj(g_main, ui_extensionbubble2);
                        lv_group_add_obj(g_main, ui_extensionbubble3);
                        lv_group_add_obj(g_main, ui_extensionbubble4);
                    }
                }else{
                    lv_obj_clear_flag(ui_extensionNone, LV_OBJ_FLAG_HIDDEN);
                    if(lv_scr_act() != ui_Page_Extension)break;
                    if(extension_group_count != 1)
                    {
                        lv_group_remove_all_objs(g_main);
                        lv_group_add_obj(g_main, ui_extenNoneback);
                    }
                }

                char sensor_temp[6] = "--";
                char sensor_humi[6] = "--";
                char sensor_co2[6] = "--";
                char sensor_back[6] = "--";
                if (sensor_data->temperature_valid && sensor_data->temperature) {
                    ESP_LOGI(TAG, "Temperature: %0.1f", sensor_data->temperature);
                    snprintf(sensor_temp, sizeof(sensor_temp), "%.1f", sensor_data->temperature);
                } else {
                    ESP_LOGI(TAG, "Temperature: None");
                }

                if (sensor_data->humidity_valid && sensor_data->humidity) {
                    ESP_LOGI(TAG, "Humidity: %0.1f", sensor_data->humidity);
                    snprintf(sensor_humi, sizeof(sensor_humi), "%.1f", sensor_data->humidity);
                } else {
                    ESP_LOGI(TAG, "Humidity: None");
                }

                if (sensor_data->co2_valid && sensor_data->co2) {
                    ESP_LOGI(TAG, "CO2: %u", sensor_data->co2);
                    snprintf(sensor_co2, sizeof(sensor_co2), "%u", sensor_data->co2);
                } else {
                    ESP_LOGI(TAG, "CO2: None");
                }

                view_sensor_data_update(sensor_temp, sensor_humi, sensor_co2, sensor_back);
                break;
            }

            default:
                break;
        }
    }
    else if(base == CTRL_EVENT_BASE){
        switch (id)
        {
            case CTRL_EVENT_OTA_AI_MODEL:{
                ESP_LOGI(TAG, "event: CTRL_EVENT_OTA_AI_MODEL");
                if(g_taskdown == 0) // if the task is running
                {
                    if(lv_scr_act() != ui_Page_ModelOTA)
                    {
                        lv_group_remove_all_objs(g_main);
                        _ui_screen_change(&ui_Page_ModelOTA, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_ModelOTA_screen_init);
                        hide_all_overlays();
                    }
                    struct view_data_ota_status * ota_st_ptr = (struct view_data_ota_status *)event_data;
                    lv_obj_add_flag(ui_otaicon, LV_OBJ_FLAG_HIDDEN);
                    if(ota_st_ptr->status == 0)
                    {
                        ESP_LOGI(TAG, "OTA download succeeded");
                    }else if (ota_st_ptr->status == 1)
                    {
                        update_ai_ota_progress(ota_st_ptr->percentage);
                    }else{
                        ESP_LOGE(TAG, "OTA download failed, error code: %d", ota_st_ptr->err_code);
                    }
                }
                break;
            }

            case CTRL_EVENT_MQTT_CONNECTED:{
                ESP_LOGI(TAG, "CTRL_EVENT_MQTT_CONNECTED");
                lv_obj_add_flag(ui_wifimqtt, LV_OBJ_FLAG_HIDDEN);
                break;
            }

            case CTRL_EVENT_MQTT_DISCONNECTED:{
                ESP_LOGI(TAG, "CTRL_EVENT_MQTT_DISCONNECTED");
                lv_obj_clear_flag(ui_wifimqtt, LV_OBJ_FLAG_HIDDEN);

                break;
            }

            default:
                break;
        }
    }
    lvgl_port_unlock();
}

int view_init(void)
{
    static uint8_t bat_per;
    static bool    is_charging;
    bat_per = bsp_battery_get_percent();
    is_charging = (uint8_t)(bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0);

    lvgl_port_lock(0);
    ui_init();
    lv_pm_init();
    view_alarm_init(lv_layer_top());
    view_image_preview_init(ui_Page_ViewLive);
    view_pages_init();
    lvgl_port_unlock();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, 
                                                            __view_event_handler, NULL, NULL)); 
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_PNG_LOADING, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_TIME, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS, 
                                                            __view_event_handler, NULL, NULL));  

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_RGB_SWITCH, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SOUND, 
                                                            __view_event_handler, NULL, NULL)); 
                                                            
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SOFTWARE_VERSION_CODE, 
                                                            __view_event_handler, NULL, NULL));   

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_HIMAX_SOFTWARE_VERSION_CODE, 
                                                            __view_event_handler, NULL, NULL)); 
                                                            
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BLE_SWITCH, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, 
                                                            __view_event_handler, NULL, NULL)); 
                                                            
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONFIG_SYNC, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_ALARM_ON, 
                                                            __view_event_handler, NULL, NULL));   

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_ALARM_OFF, 
                                                            __view_event_handler, NULL, NULL));  
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BATTERY_ST, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BAT_DRAIN_SHUTDOWN, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_CHARGE_ST, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            CTRL_EVENT_BASE, CTRL_EVENT_OTA_AI_MODEL, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            CTRL_EVENT_BASE, CTRL_EVENT_MQTT_CONNECTED, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            CTRL_EVENT_BASE, CTRL_EVENT_MQTT_DISCONNECTED, 
                                                            __view_event_handler, NULL, NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_TASK_FLOW_STOP, 
                                                            __view_event_handler, NULL, NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_OTA_STATUS, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_TASK_FLOW_ERROR, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_AI_CAMERA_READY, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_INFO_OBTAIN, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_EMOJI_DOWLOAD_BAR, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_EMOJI_DOWLOAD_FAILED, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_MODE_STANDBY, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_VI_RECORDING, 
                                                            __view_event_handler, NULL, NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_VI_ANALYZING, 
                                                            __view_event_handler, NULL, NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_VI_PLAYING, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_VI_EXIT, 
                                                            __view_event_handler, NULL, NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_VI_ERROR, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_VI_TASKFLOW_PAUSE, 
                                                            __view_event_handler, NULL, NULL));
                                                        
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_VI_PLAY_FINISH, 
                                                            __view_event_handler, NULL, NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle,
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SENSOR, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle,
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_TRIGGER, 
                                                            __view_event_handler, NULL, NULL));

    if((bat_per < 1) && (! is_charging))
    {
        lv_disp_load_scr(ui_Page_Battery);
        vTaskDelay(pdMS_TO_TICKS(200));
        BSP_ERROR_CHECK_RETURN_ERR(bsp_lcd_brightness_set(50));

        ESP_LOGI(TAG, "Battery too low, wait for charging");
        while(1) {
            is_charging = (uint8_t)(bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0);
            if ( is_charging ) {
                ESP_LOGI(TAG, "Charging, exit low battery page");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    lv_disp_load_scr(ui____initial_actions0);
    lv_disp_load_scr(ui_Page_Startup);

    vTaskDelay(pdMS_TO_TICKS(200));
    BSP_ERROR_CHECK_RETURN_ERR(bsp_lcd_brightness_set(50));

    init_builtin_emoji_count(&builtin_emoji_count);
    init_custom_emoji_count(&custom_emoji_count);
    count_png_images(&builtin_emoji_count, &custom_emoji_count);

    read_and_store_selected_pngs("Custom_greeting",    "greeting", g_greet_img_dsc, &g_greet_image_count);
    read_and_store_selected_pngs("Custom_detecting",   "detecting", g_detect_img_dsc, &g_detect_image_count);
    read_and_store_selected_pngs("Custom_detected",    "detected", g_detected_img_dsc, &g_detected_image_count);
    read_and_store_selected_customed_pngs("Custom_speaking",    "speaking", g_speak_img_dsc, &g_speak_image_count);
    read_and_store_selected_pngs("Custom_listening",   "listening", g_listen_img_dsc, &g_listen_image_count);
    read_and_store_selected_pngs("Custom_analyzing",   "analyzing", g_analyze_img_dsc, &g_analyze_image_count);
    read_and_store_selected_pngs("Custom_standby",     "standby", g_standby_img_dsc, &g_standby_image_count);

    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, NULL, 0, pdMS_TO_TICKS(10000));
                    

    return 0;
}

void view_render_black(void)
{
    lvgl_port_lock(0);
    view_image_black_flush();
    lvgl_port_unlock();
}
