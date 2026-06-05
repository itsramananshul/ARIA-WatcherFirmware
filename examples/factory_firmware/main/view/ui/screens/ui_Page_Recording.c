// ui_Page_Recording.c — ARIA voice recorder screen.
//
// Opened from the "Recording" home tile. A single record/stop control plus a
// status line driven by app_recording's state. Lazily created by
// lv_pm_open_page; focus is managed directly on g_main like ui_Page_WifiScan.

#include "../ui.h"
#include "../../ui_manager/pm.h"     // lv_pm_open_page, PM_ADD_OBJS_TO_GROUP
#include "app_recording.h"
#include <stdio.h>

extern lv_group_t *g_main;
extern GroupInfo group_page_main;

lv_obj_t *ui_Page_Recording;
lv_obj_t *ui_rec_status;
lv_obj_t *ui_rec_btn;
lv_obj_t *ui_rec_btn_lbl;
lv_obj_t *ui_rec_back;

static lv_timer_t *s_rec_timer = NULL;

static void rec_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!ui_rec_status) return;

    app_recording_state_t st = app_recording_get_state();
    uint32_t ms = app_recording_elapsed_ms();
    uint32_t s = ms / 1000;
    char buf[64];

    switch (st) {
        case REC_RECORDING:
            snprintf(buf, sizeof(buf), "#ff5050 ● REC#  %lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
            lv_label_set_text(ui_rec_btn_lbl, "Stop");
            break;
        case REC_FINALIZING:
            snprintf(buf, sizeof(buf), "Saving...  %lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
            lv_label_set_text(ui_rec_btn_lbl, "Stop");
            break;
        case REC_UPLOADING:
            snprintf(buf, sizeof(buf), "Uploading...");
            lv_label_set_text(ui_rec_btn_lbl, "Wait");
            break;
        case REC_DONE_UPLOADED:
            snprintf(buf, sizeof(buf), "#8fc31f %s#\n%lu:%02lu", app_recording_status_text(), (unsigned long)(s / 60), (unsigned long)(s % 60));
            lv_label_set_text(ui_rec_btn_lbl, "Record");
            break;
        case REC_DONE_SAVED_SD:
            snprintf(buf, sizeof(buf), "Saved to SD\n(uploads on WiFi)");
            lv_label_set_text(ui_rec_btn_lbl, "Record");
            break;
        case REC_ERR_NO_SD_OFFLINE:
            snprintf(buf, sizeof(buf), "#ff5050 No SD + offline#");
            lv_label_set_text(ui_rec_btn_lbl, "Record");
            break;
        case REC_ERR:
            snprintf(buf, sizeof(buf), "#ff5050 %s#", app_recording_status_text());
            lv_label_set_text(ui_rec_btn_lbl, "Record");
            break;
        default:
            snprintf(buf, sizeof(buf), "Tap to record");
            lv_label_set_text(ui_rec_btn_lbl, "Record");
            break;
    }
    lv_label_set_text(ui_rec_status, buf);
}

static void rec_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_recording_state_t st = app_recording_get_state();
    if (st == REC_RECORDING) {
        app_recording_stop();
    } else if (st != REC_FINALIZING && st != REC_UPLOADING) {
        app_recording_start();
    }
    rec_timer_cb(NULL);
}

static void rec_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (app_recording_get_state() == REC_RECORDING) {
        app_recording_stop();   // don't leave a take running with no way to stop it
    }
    if (s_rec_timer) { lv_timer_del(s_rec_timer); s_rec_timer = NULL; }
    lv_pm_open_page(g_main, &group_page_main, PM_ADD_OBJS_TO_GROUP, &ui_Page_Home,
                    LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Home_screen_init);
}

void ui_Page_Recording_screen_init(void)
{
    ui_Page_Recording = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Page_Recording, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Page_Recording, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Page_Recording, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = lv_label_create(ui_Page_Recording);
    lv_obj_set_align(title, LV_ALIGN_CENTER);
    lv_obj_set_y(title, -120);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(title, "Recording");

    ui_rec_status = lv_label_create(ui_Page_Recording);
    lv_obj_set_width(ui_rec_status, 240);
    lv_obj_set_align(ui_rec_status, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_rec_status, -40);
    lv_obj_set_style_text_align(ui_rec_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_rec_status, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_rec_status, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_recolor(ui_rec_status, true);
    lv_label_set_text(ui_rec_status, "Tap to record");

    ui_rec_btn = lv_btn_create(ui_Page_Recording);
    lv_obj_set_width(ui_rec_btn, 150);
    lv_obj_set_height(ui_rec_btn, 50);
    lv_obj_set_align(ui_rec_btn, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_rec_btn, 30);
    lv_obj_set_style_radius(ui_rec_btn, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_rec_btn, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_rec_btn, lv_color_hex(0x8FC31F), LV_PART_MAIN | LV_STATE_FOCUSED);
    ui_rec_btn_lbl = lv_label_create(ui_rec_btn);
    lv_obj_set_align(ui_rec_btn_lbl, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(ui_rec_btn_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_rec_btn_lbl, "Record");
    lv_obj_add_event_cb(ui_rec_btn, rec_btn_cb, LV_EVENT_CLICKED, NULL);

    ui_rec_back = lv_btn_create(ui_Page_Recording);
    lv_obj_set_width(ui_rec_back, 150);
    lv_obj_set_height(ui_rec_back, 44);
    lv_obj_set_align(ui_rec_back, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_rec_back, 92);
    lv_obj_set_style_radius(ui_rec_back, 22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_rec_back, lv_color_hex(0x222222), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_rec_back, lv_color_hex(0x8FC31F), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_t *bl = lv_label_create(ui_rec_back);
    lv_obj_set_align(bl, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(bl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(bl, "Back");
    lv_obj_add_event_cb(ui_rec_back, rec_back_cb, LV_EVENT_CLICKED, NULL);
}

// Open the recording screen (called from the home tile click).
void ui_recording_open(void)
{
    lv_pm_open_page(g_main, NULL, PM_CLEAR_GROUP, &ui_Page_Recording,
                    LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Recording_screen_init);

    lv_group_remove_all_objs(g_main);
    lv_group_add_obj(g_main, ui_rec_btn);
    lv_group_add_obj(g_main, ui_rec_back);
    lv_group_focus_obj(ui_rec_btn);

    if (s_rec_timer) lv_timer_del(s_rec_timer);
    s_rec_timer = lv_timer_create(rec_timer_cb, 250, NULL);
    rec_timer_cb(NULL);
}
