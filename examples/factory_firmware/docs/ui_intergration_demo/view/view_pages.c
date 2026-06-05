// #include "view_pages.h"
// #include "ui_manager/event.h"

// // view taskflow error
// lv_obj_t * ui_taskerrt2;
// lv_obj_t * ui_task_error;
// lv_obj_t * ui_taskerrt;
// lv_obj_t * ui_taskerrbtn;
// lv_obj_t * ui_taskerrbt;

// // view emoji ota
// lv_obj_t * ui_Page_Emoji;
// lv_obj_t * ui_failed;
// lv_obj_t * ui_facet;
// lv_obj_t * ui_facearc;
// lv_obj_t * ui_faceper;
// lv_obj_t * ui_facetper;
// lv_obj_t * ui_facetsym;
// lv_obj_t * ui_emoticonok;

// // view standby mode
// lv_obj_t * ui_Page_Standby;

// void view_pages_init()
// {
//     view_task_error_init();
//     view_emoji_ota_init();
//     view_standby_mode_init();
// }


// void view_task_error_init()
// {
//     ui_task_error = lv_obj_create(lv_layer_top());
//     lv_obj_set_width(ui_task_error, 412);
//     lv_obj_set_height(ui_task_error, 412);
//     lv_obj_set_align(ui_task_error, LV_ALIGN_CENTER);
//     lv_obj_add_flag(ui_task_error, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag(ui_task_error, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
//     lv_obj_set_style_radius(ui_task_error, 190, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_task_error, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_opa(ui_task_error, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_border_color(ui_task_error, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_border_opa(ui_task_error, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

//     ui_taskerrt = lv_label_create(ui_task_error);
//     lv_obj_set_width(ui_taskerrt, LV_SIZE_CONTENT);   /// 1
//     lv_obj_set_height(ui_taskerrt, LV_SIZE_CONTENT);    /// 1
//     lv_obj_set_x(ui_taskerrt, 0);
//     lv_obj_set_y(ui_taskerrt, -104);
//     lv_obj_set_align(ui_taskerrt, LV_ALIGN_CENTER);
//     lv_label_set_text(ui_taskerrt, "TASK ERROR");
//     lv_obj_set_style_text_color(ui_taskerrt, lv_color_hex(0xBE2C2C), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_opa(ui_taskerrt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_font(ui_taskerrt, &ui_font_fbold24, LV_PART_MAIN | LV_STATE_DEFAULT);

//     ui_taskerrt2 = lv_label_create(ui_task_error);
//     lv_obj_set_width(ui_taskerrt2, 289);
//     lv_obj_set_height(ui_taskerrt2, 108);
//     lv_obj_set_x(ui_taskerrt2, 0);
//     lv_obj_set_y(ui_taskerrt2, -5);
//     lv_obj_set_align(ui_taskerrt2, LV_ALIGN_CENTER);
//     lv_label_set_text(ui_taskerrt2, "[sensecraft alarm] failed to connect the next module");
//     lv_obj_set_style_text_color(ui_taskerrt2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_opa(ui_taskerrt2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_align(ui_taskerrt2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_font(ui_taskerrt2, &ui_font_fontbold26, LV_PART_MAIN | LV_STATE_DEFAULT);

//     ui_taskerrbtn = lv_btn_create(ui_task_error);
//     lv_obj_set_width(ui_taskerrbtn, 150);
//     lv_obj_set_height(ui_taskerrbtn, 60);
//     lv_obj_set_x(ui_taskerrbtn, 0);
//     lv_obj_set_y(ui_taskerrbtn, 110);
//     lv_obj_set_align(ui_taskerrbtn, LV_ALIGN_CENTER);
//     lv_obj_set_style_radius(ui_taskerrbtn, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_taskerrbtn, lv_color_hex(0xB80808), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_opa(ui_taskerrbtn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_taskerrbtn, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_opa(ui_taskerrbtn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

//     ui_taskerrbt = lv_label_create(ui_taskerrbtn);
//     lv_obj_set_width(ui_taskerrbt, LV_SIZE_CONTENT);   /// 1
//     lv_obj_set_height(ui_taskerrbt, LV_SIZE_CONTENT);    /// 1
//     lv_obj_set_align(ui_taskerrbt, LV_ALIGN_CENTER);
//     lv_label_set_text(ui_taskerrbt, "End Task");
//     lv_obj_set_style_text_color(ui_taskerrbt, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_opa(ui_taskerrbt, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_align(ui_taskerrbt, LV_TEXT_ALIGN_AUTO, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_font(ui_taskerrbt, &ui_font_fbold24, LV_PART_MAIN | LV_STATE_DEFAULT);

//     lv_obj_add_event_cb(ui_taskerrbtn, taskerrc_cb, LV_EVENT_CLICKED, NULL);
// }

// void view_emoji_ota_init(void)
// {
//     ui_Page_Emoji = lv_obj_create(lv_layer_top());
//     lv_obj_set_width(ui_Page_Emoji, 412);
//     lv_obj_set_height(ui_Page_Emoji, 412);
//     lv_obj_set_align(ui_Page_Emoji, LV_ALIGN_CENTER);
//     lv_obj_clear_flag(ui_Page_Emoji, LV_OBJ_FLAG_SCROLLABLE);
//     lv_obj_add_flag(ui_Page_Emoji, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_set_style_bg_color(ui_Page_Emoji, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_opa(ui_Page_Emoji, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_border_color(ui_Page_Emoji, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_border_opa(ui_Page_Emoji, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

//     ui_facet = lv_label_create(ui_Page_Emoji);
//     lv_obj_set_width(ui_facet, 380);
//     lv_obj_set_height(ui_facet, 100);
//     lv_obj_set_x(ui_facet, 0);
//     lv_obj_set_y(ui_facet, 10);
//     lv_obj_set_align(ui_facet, LV_ALIGN_CENTER);
//     lv_label_set_text(ui_facet, "Uploading\nface...");
//     lv_obj_set_style_text_color(ui_facet, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_opa(ui_facet, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_align(ui_facet, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_font(ui_facet, &ui_font_font_bold, LV_PART_MAIN | LV_STATE_DEFAULT);

//     ui_failed = lv_label_create(ui_Page_Emoji);
//     lv_obj_set_width(ui_failed, LV_SIZE_CONTENT);   /// 1
//     lv_obj_set_height(ui_failed, LV_SIZE_CONTENT);    /// 1
//     lv_obj_set_x(ui_failed, 0);
//     lv_obj_set_y(ui_failed, -75);
//     lv_obj_set_align(ui_failed, LV_ALIGN_CENTER);
//     lv_label_set_text(ui_failed, "Upload failed");
//     lv_obj_set_style_text_color(ui_failed, lv_color_hex(0xD54941), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_opa(ui_failed, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_font(ui_failed, &ui_font_semibold42, LV_PART_MAIN | LV_STATE_DEFAULT);

//     ui_facearc = lv_arc_create(ui_Page_Emoji);
//     lv_obj_set_width(ui_facearc, 412);
//     lv_obj_set_height(ui_facearc, 412);
//     lv_obj_set_x(ui_facearc, -1);
//     lv_obj_set_y(ui_facearc, -1);
//     lv_obj_set_align(ui_facearc, LV_ALIGN_CENTER);
//     lv_obj_clear_flag(ui_facearc, LV_OBJ_FLAG_CLICKABLE);      /// Flags
//     lv_arc_set_value(ui_facearc, 20);
//     lv_arc_set_bg_angles(ui_facearc, 0, 360);
//     lv_arc_set_rotation(ui_facearc, 270);
//     lv_obj_set_style_arc_color(ui_facearc, lv_color_hex(0x1D2608), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_arc_opa(ui_facearc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

//     lv_obj_set_style_arc_color(ui_facearc, lv_color_hex(0xA1D42A), LV_PART_INDICATOR | LV_STATE_DEFAULT);
//     lv_obj_set_style_arc_opa(ui_facearc, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);

//     lv_obj_set_style_bg_color(ui_facearc, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_opa(ui_facearc, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

//     ui_faceper = lv_obj_create(ui_Page_Emoji);
//     lv_obj_set_width(ui_faceper, 100);
//     lv_obj_set_height(ui_faceper, 50);
//     lv_obj_set_x(ui_faceper, 0);
//     lv_obj_set_y(ui_faceper, 50);
//     lv_obj_set_align(ui_faceper, LV_ALIGN_CENTER);
//     lv_obj_set_flex_flow(ui_faceper, LV_FLEX_FLOW_ROW);
//     lv_obj_set_flex_align(ui_faceper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
//     lv_obj_clear_flag(ui_faceper, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
//     lv_obj_set_style_bg_color(ui_faceper, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_opa(ui_faceper, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_border_color(ui_faceper, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_border_opa(ui_faceper, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

//     ui_facetper = lv_label_create(ui_faceper);
//     lv_obj_set_width(ui_facetper, LV_SIZE_CONTENT);   /// 1
//     lv_obj_set_height(ui_facetper, LV_SIZE_CONTENT);    /// 1
//     lv_obj_set_align(ui_facetper, LV_ALIGN_CENTER);
//     lv_label_set_text(ui_facetper, "0");
//     lv_obj_set_style_text_color(ui_facetper, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_opa(ui_facetper, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_font(ui_facetper, &ui_font_fontbold26, LV_PART_MAIN | LV_STATE_DEFAULT);

//     ui_facetsym = lv_label_create(ui_faceper);
//     lv_obj_set_width(ui_facetsym, LV_SIZE_CONTENT);   /// 1
//     lv_obj_set_height(ui_facetsym, LV_SIZE_CONTENT);    /// 1
//     lv_obj_set_x(ui_facetsym, 20);
//     lv_obj_set_y(ui_facetsym, 0);
//     lv_obj_set_align(ui_facetsym, LV_ALIGN_CENTER);
//     lv_label_set_text(ui_facetsym, "%");
//     lv_obj_set_style_text_color(ui_facetsym, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_opa(ui_facetsym, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_text_font(ui_facetsym, &ui_font_fontbold26, LV_PART_MAIN | LV_STATE_DEFAULT);

//     ui_emoticonok = lv_btn_create(ui_Page_Emoji);
//     lv_obj_set_width(ui_emoticonok, 60);
//     lv_obj_set_height(ui_emoticonok, 60);
//     lv_obj_set_x(ui_emoticonok, 0);
//     lv_obj_set_y(ui_emoticonok, 128);
//     lv_obj_set_align(ui_emoticonok, LV_ALIGN_CENTER);
//     lv_obj_add_flag(ui_emoticonok, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
//     lv_obj_clear_flag(ui_emoticonok, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
//     lv_obj_set_style_radius(ui_emoticonok, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_emoticonok, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_opa(ui_emoticonok, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_img_src(ui_emoticonok, &ui_img_wifiok_png, LV_PART_MAIN | LV_STATE_DEFAULT);

//     lv_obj_add_event_cb(ui_emoticonok, ui_event_emoticonok, LV_EVENT_ALL, NULL);

// }


// void view_standby_mode_init(void)
// {
//     ui_Page_Standby = lv_obj_create(lv_layer_top());
//     lv_obj_set_width(ui_Page_Standby, 412);
//     lv_obj_set_height(ui_Page_Standby, 412);
//     lv_obj_set_align(ui_Page_Standby, LV_ALIGN_CENTER);
//     lv_obj_clear_flag(ui_Page_Standby, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
//     lv_obj_add_flag(ui_Page_Standby, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_set_style_bg_color(ui_Page_Standby, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_opa(ui_Page_Standby, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_border_color(ui_Page_Standby, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
//     lv_obj_set_style_border_opa(ui_Page_Standby, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
// }
