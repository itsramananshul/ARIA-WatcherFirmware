// ui_Page_Chat.c — the "Chat" home tile / ARIA chat view.
//
// Opened reliably via lv_pm_open_page (a freshly-created screen, like the
// Recording page). Shows ARIA's greeting face; hold the wheel to talk (global
// push-to-talk), and every turn is logged to GitHub. This screen is hand-built
// (NOT the SquareLine-generated Settings menu), so we can safely add controls
// here with a manual encoder group — e.g. the TTS-engine toggle.

#include "../ui.h"
#include "../../ui_manager/pm.h"     // lv_pm_open_page, PM_*
#include "app_recording.h"           // app_chat_end_flush
#include "storage.h"                 // NVS aria_eng (TTS engine)

extern lv_group_t *g_main;
extern GroupInfo group_page_main;
extern lv_img_dsc_t *g_greet_img_dsc[];   // ARIA greeting emoji frames
extern int g_greet_image_count;

lv_obj_t *ui_Page_Chat;
lv_obj_t *ui_chat_back;

static lv_obj_t *ui_chat_eng = NULL;   // TTS-engine toggle button
static lv_obj_t *s_eng_lbl   = NULL;   // its label (shows current engine)

static void chat_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_chat_end_flush();   // commit this conversation to GitHub
    lv_pm_open_page(g_main, &group_page_main, PM_ADD_OBJS_TO_GROUP, &ui_Page_Home,
                    LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_Home_screen_init);
}

// Reflect the saved TTS engine (NVS aria_eng: 1=Fast/Live, 0=Normal/Current).
static void chat_eng_refresh(void)
{
    if (!s_eng_lbl) return;
    uint8_t eng = 0; size_t len = sizeof(eng);
    storage_read("aria_eng", &eng, &len);
    lv_label_set_text(s_eng_lbl, eng ? "Voice: Fast" : "Voice: Normal");
}

static void chat_eng_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    uint8_t eng = 0; size_t len = sizeof(eng);
    storage_read("aria_eng", &eng, &len);
    eng = eng ? 0 : 1;                       // toggle
    storage_write("aria_eng", &eng, sizeof(eng));
    chat_eng_refresh();
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
        lv_obj_set_y(face, -40);
        lv_obj_move_background(face);
    } else {
        lv_obj_t *title = lv_label_create(ui_Page_Chat);
        lv_obj_set_align(title, LV_ALIGN_CENTER);
        lv_obj_set_y(title, -90);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(title, "ARIA");
    }

    lv_obj_t *hint = lv_label_create(ui_Page_Chat);
    lv_obj_set_align(hint, LV_ALIGN_CENTER);
    lv_obj_set_y(hint, 18);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(hint, "Hold the wheel to talk");

    // TTS-engine toggle (Normal / Fast) — short-press the wheel on it to switch.
    ui_chat_eng = lv_btn_create(ui_Page_Chat);
    lv_obj_set_width(ui_chat_eng, 200);
    lv_obj_set_height(ui_chat_eng, 42);
    lv_obj_set_align(ui_chat_eng, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_chat_eng, 66);
    lv_obj_set_style_radius(ui_chat_eng, 21, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_chat_eng, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_chat_eng, lv_color_hex(0x8FC31F), LV_PART_MAIN | LV_STATE_FOCUSED);
    s_eng_lbl = lv_label_create(ui_chat_eng);
    lv_obj_set_align(s_eng_lbl, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(s_eng_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_eng_lbl, "Voice: Normal");
    lv_obj_add_event_cb(ui_chat_eng, chat_eng_cb, LV_EVENT_CLICKED, NULL);
    chat_eng_refresh();   // set initial label from NVS

    ui_chat_back = lv_btn_create(ui_Page_Chat);
    lv_obj_set_width(ui_chat_back, 130);
    lv_obj_set_height(ui_chat_back, 42);
    lv_obj_set_align(ui_chat_back, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_chat_back, 118);
    lv_obj_set_style_radius(ui_chat_back, 21, LV_PART_MAIN | LV_STATE_DEFAULT);
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
    lv_group_add_obj(g_main, ui_chat_eng);    // rotate the wheel between these two
    lv_group_add_obj(g_main, ui_chat_back);
    lv_group_focus_obj(ui_chat_back);
}
