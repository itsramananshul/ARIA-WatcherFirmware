// ui_Page_Chat.c — the "Chat" home tile / ARIA chat view.
//
// Opened reliably via lv_pm_open_page (a freshly-created screen, like the
// Recording page) — the bare avatar screen can't be reopened from a tile
// because its pointer is stale after the page manager moves away from it.
// We show ARIA's greeting face here so it still feels like her talk view; hold
// the wheel to talk (global push-to-talk), and every turn is logged to GitHub.

#include "../ui.h"
#include "../../ui_manager/pm.h"     // lv_pm_open_page, PM_*
#include "app_recording.h"           // app_chat_end_flush

extern lv_group_t *g_main;
extern GroupInfo group_page_main;
extern lv_img_dsc_t *g_greet_img_dsc[];   // ARIA greeting emoji frames
extern int g_greet_image_count;

lv_obj_t *ui_Page_Chat;
lv_obj_t *ui_chat_back;

static void chat_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_chat_end_flush();   // commit this conversation to GitHub
    lv_pm_open_page(g_main, &group_page_main, PM_ADD_OBJS_TO_GROUP, &ui_Page_Home,
                    LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Home_screen_init);
}

void ui_Page_Chat_screen_init(void)
{
    ui_Page_Chat = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Page_Chat, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Page_Chat, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Page_Chat, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // ARIA's face (a greeting emoji frame), centered, sitting behind the UI.
    if (g_greet_image_count > 0 && g_greet_img_dsc[0] != NULL) {
        lv_obj_t *face = lv_img_create(ui_Page_Chat);
        lv_img_set_src(face, g_greet_img_dsc[0]);
        lv_obj_set_align(face, LV_ALIGN_CENTER);
        lv_obj_set_y(face, -16);
        lv_obj_move_background(face);
    } else {
        lv_obj_t *title = lv_label_create(ui_Page_Chat);
        lv_obj_set_align(title, LV_ALIGN_CENTER);
        lv_obj_set_y(title, -70);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(title, "ARIA");
    }

    lv_obj_t *hint = lv_label_create(ui_Page_Chat);
    lv_obj_set_align(hint, LV_ALIGN_CENTER);
    lv_obj_set_y(hint, 44);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(hint, "Hold the wheel to talk");

    ui_chat_back = lv_btn_create(ui_Page_Chat);
    lv_obj_set_width(ui_chat_back, 130);
    lv_obj_set_height(ui_chat_back, 44);
    lv_obj_set_align(ui_chat_back, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_chat_back, 100);
    lv_obj_set_style_radius(ui_chat_back, 22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_chat_back, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_chat_back, lv_color_hex(0x8FC31F), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_t *bl = lv_label_create(ui_chat_back);
    lv_obj_set_align(bl, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(bl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(bl, "Done");
    lv_obj_add_event_cb(ui_chat_back, chat_back_cb, LV_EVENT_CLICKED, NULL);
}

void ui_chat_open(void)
{
    lv_pm_open_page(g_main, NULL, PM_CLEAR_GROUP, &ui_Page_Chat,
                    LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Chat_screen_init);
    lv_group_remove_all_objs(g_main);
    lv_group_add_obj(g_main, ui_chat_back);
    lv_group_focus_obj(ui_chat_back);
}
