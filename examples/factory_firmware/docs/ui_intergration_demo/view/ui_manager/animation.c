// #include "animation.h"
// #include "ui/ui.h"
// #include "esp_log.h"
// #include <inttypes.h>

// static const char *TAG = "animation_event";

// lv_obj_t *ui_Left1;
// lv_obj_t *ui_Left2;
// lv_obj_t *ui_Left3;
// lv_obj_t *ui_Left4;
// lv_obj_t *ui_Left5;
// lv_obj_t *ui_Left6;
// lv_obj_t *ui_Left7;
// lv_obj_t *ui_Left8;
// lv_obj_t *ui_Right1;
// lv_obj_t *ui_Right2;
// lv_obj_t *ui_Right3;
// lv_obj_t *ui_Right4;
// lv_obj_t *ui_Right5;
// lv_obj_t *ui_Right6;
// lv_obj_t *ui_Right7;
// lv_obj_t *ui_Right8;

// void main_scroll_cb(lv_event_t *e)
// {
//     lv_obj_t *cont = lv_event_get_target(e);

//     lv_area_t cont_a;
//     lv_obj_get_coords(cont, &cont_a);
//     int32_t cont_y_center = lv_area_get_height(&cont_a) / 2;
//     // ESP_LOGI(TAG, "cont_y_center: %d", cont_y_center);

//     // int32_t r = lv_obj_get_height(cont);
//     int32_t r = 245;
//     uint32_t i;
//     uint32_t child_cnt = lv_obj_get_child_cnt(cont);

//     for (i = 0; i < child_cnt; i++)
//     {
//         lv_obj_t *child = lv_obj_get_child(cont, i);
//         lv_area_t child_a;
//         lv_obj_get_coords(child, &child_a);

//         int32_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;

//         int32_t diff_y = child_y_center - cont_y_center;
//         diff_y = LV_ABS(diff_y);

//         /*Get the x of diff_y on a circle.*/
//         int32_t x;
//         /*If diff_y is out of the circle use the last point of the circle (the radius)*/
//         if (diff_y >= r)
//         {
//             x = -r;
//         }
//         else
//         {
//             /*Use Pythagoras theorem to get x from radius and y*/
//             uint32_t x_sqr = r * r - diff_y * diff_y;
//             lv_sqrt_res_t res;
//             lv_sqrt(x_sqr, &res, 0x8000); /*Use lvgl's built in sqrt root function*/
//             x = -(r - res.i);
//         }
//         /*Translate the item by the calculated X coordinate*/
//         lv_obj_set_style_translate_x(child, x, 0);
//         lv_obj_set_style_opa(child, 250 + x + x, 0);
        
//         // lv_obj_set_size(lv_obj_get_child(child, 0), x+80, x+80);
//         // ESP_LOGI(TAG, "The value is: %" PRId32 "\n", x);
//     }
// }

// void menu_scroll_cb(lv_event_t *e)
// {
//     lv_obj_t *cont = lv_event_get_target(e);

//     lv_area_t cont_a;
//     lv_obj_get_coords(cont, &cont_a);
//     int32_t cont_x_center = lv_area_get_width(&cont_a) / 2; // Change to use the center point of the width
//     // ESP_LOGI(TAG, "cont_x_center: %d", cont_x_center);

//     // int32_t r = lv_obj_get_height(cont); // Change to use the width as the radius
//     int32_t r = 140;

//     uint32_t i;
//     uint32_t child_cnt = lv_obj_get_child_cnt(cont);

//     for (i = 0; i < child_cnt; i++)
//     {
//         lv_obj_t *child = lv_obj_get_child(cont, i);
//         lv_area_t child_a;
//         lv_obj_get_coords(child, &child_a);

//         int32_t child_x_center = child_a.x1 + lv_area_get_width(&child_a) / 2; // Change to calculate the center point of the width

//         int32_t diff_x = child_x_center - cont_x_center; // Change to calculate the horizontal difference
//         diff_x = LV_ABS(diff_x);

//         /*Get the y of diff_x on a circle.*/
//         int32_t y;
//         /*If diff_x is out of the circle use the last point of the circle (the radius).*/
//         if (diff_x >= r)
//         {
//             y = -r; // Change to use a negative value for y on the lower semicircle
//         }
//         else
//         {
//             /*Use Pythagoras theorem to get y from radius and x.*/
//             uint32_t y_sqr = r * r - diff_x * diff_x;
//             lv_sqrt_res_t res;
//             lv_sqrt(y_sqr, &res, 0x8000); // Use the built-in sqrt function from lvgl
//             y = -(r - res.i);             // Change to use a negative value for y on the lower semicircle
//         }
//         /*Translate the item by the calculated Y coordinate.*/
//         lv_obj_set_style_translate_y(child, y, 0);
//         // ESP_LOGI(TAG, "The value is: %" PRId32 "\n", y);
//     }
// }

// void set_scroll_cb(lv_event_t *e)
// {
//     lv_obj_t *cont = lv_event_get_target(e);
//     lv_area_t cont_a;
//     lv_obj_get_coords(cont, &cont_a);
//     int32_t cont_y_center = lv_area_get_height(&cont_a) / 2;
//     // ESP_LOGI(TAG, "cont_y_center: %d", cont_y_center);

//     // int32_t r = lv_obj_get_height(cont);
//     int32_t r = 412;
//     uint32_t i;
//     uint32_t child_cnt = lv_obj_get_child_cnt(cont);

//     for (i = 0; i < child_cnt; i++)
//     {
//         lv_obj_t *child = lv_obj_get_child(cont, i);
//         lv_area_t child_a;
//         lv_obj_get_coords(child, &child_a);

//         int32_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;

//         int32_t diff_y = child_y_center - cont_y_center;
//         diff_y = LV_ABS(diff_y);

//         /*Get the x of diff_y on a circle.*/
//         int32_t x;
//         /*If diff_y is out of the circle use the last point of the circle (the radius)*/
//         if (diff_y >= r)
//         {
//             x = r;
//         }
//         else
//         {
//             /*Use Pythagoras theorem to get x from radius and y*/
//             uint32_t x_sqr = r * r - diff_y * diff_y;
//             lv_sqrt_res_t res;
//             lv_sqrt(x_sqr, &res, 0x8000); /*Use lvgl's built in sqrt root function*/
//             x = r - res.i;
//         }
//         /*Translate the item by the calculated X coordinate*/
//         lv_obj_set_style_translate_x(child, x, 0);
//     }
// }

// void sidelines_Animation(lv_obj_t *TargetObject, int delay)
// {
//     ui_anim_user_data_t *PropertyAnimation_0_user_data = lv_mem_alloc(sizeof(ui_anim_user_data_t));
//     PropertyAnimation_0_user_data->target = TargetObject;
//     PropertyAnimation_0_user_data->val = -1;
//     lv_anim_t PropertyAnimation_0;
//     lv_anim_init(&PropertyAnimation_0);
//     lv_anim_set_time(&PropertyAnimation_0, 2500);
//     lv_anim_set_user_data(&PropertyAnimation_0, PropertyAnimation_0_user_data);
//     lv_anim_set_custom_exec_cb(&PropertyAnimation_0, _ui_anim_callback_set_height);
//     lv_anim_set_values(&PropertyAnimation_0, 0, 53);
//     lv_anim_set_path_cb(&PropertyAnimation_0, lv_anim_path_ease_in_out);
//     lv_anim_set_delay(&PropertyAnimation_0, delay + 2000);
//     lv_anim_set_deleted_cb(&PropertyAnimation_0, _ui_anim_callback_free_user_data);
//     lv_anim_set_playback_time(&PropertyAnimation_0, 0);
//     lv_anim_set_playback_delay(&PropertyAnimation_0, 0);
//     lv_anim_set_repeat_count(&PropertyAnimation_0, 0);
//     lv_anim_set_repeat_delay(&PropertyAnimation_0, 0);
//     lv_anim_set_early_apply(&PropertyAnimation_0, false);
//     lv_anim_set_get_value_cb(&PropertyAnimation_0, &_ui_anim_callback_get_height);
//     lv_anim_start(&PropertyAnimation_0);
//     ui_anim_user_data_t *PropertyAnimation_1_user_data = lv_mem_alloc(sizeof(ui_anim_user_data_t));
//     PropertyAnimation_1_user_data->target = TargetObject;
//     PropertyAnimation_1_user_data->val = -1;
//     lv_anim_t PropertyAnimation_1;
//     lv_anim_init(&PropertyAnimation_1);
//     lv_anim_set_time(&PropertyAnimation_1, 1000);
//     lv_anim_set_user_data(&PropertyAnimation_1, PropertyAnimation_1_user_data);
//     lv_anim_set_custom_exec_cb(&PropertyAnimation_1, _ui_anim_callback_set_opacity);
//     lv_anim_set_values(&PropertyAnimation_1, 0, 255);
//     lv_anim_set_path_cb(&PropertyAnimation_1, lv_anim_path_linear);
//     lv_anim_set_delay(&PropertyAnimation_1, delay + 1000);
//     lv_anim_set_deleted_cb(&PropertyAnimation_1, _ui_anim_callback_free_user_data);
//     lv_anim_set_playback_time(&PropertyAnimation_1, 0);
//     lv_anim_set_playback_delay(&PropertyAnimation_1, 0);
//     lv_anim_set_repeat_count(&PropertyAnimation_1, 0);
//     lv_anim_set_repeat_delay(&PropertyAnimation_1, 0);
//     lv_anim_set_early_apply(&PropertyAnimation_1, false);
//     lv_anim_set_get_value_cb(&PropertyAnimation_1, &_ui_anim_callback_get_opacity);
//     lv_anim_start(&PropertyAnimation_1);
// }

// void secondline_Animation(lv_obj_t *TargetObject, int delay)
// {
//     ui_anim_user_data_t *PropertyAnimation_0_user_data = lv_mem_alloc(sizeof(ui_anim_user_data_t));
//     PropertyAnimation_0_user_data->target = TargetObject;
//     PropertyAnimation_0_user_data->val = -1;
//     lv_anim_t PropertyAnimation_0;
//     lv_anim_init(&PropertyAnimation_0);
//     lv_anim_set_time(&PropertyAnimation_0, 3000);
//     lv_anim_set_user_data(&PropertyAnimation_0, PropertyAnimation_0_user_data);
//     lv_anim_set_custom_exec_cb(&PropertyAnimation_0, _ui_anim_callback_set_height);
//     lv_anim_set_values(&PropertyAnimation_0, 0, 100);
//     lv_anim_set_path_cb(&PropertyAnimation_0, lv_anim_path_linear);
//     lv_anim_set_delay(&PropertyAnimation_0, delay + 2000);
//     lv_anim_set_deleted_cb(&PropertyAnimation_0, _ui_anim_callback_free_user_data);
//     lv_anim_set_playback_time(&PropertyAnimation_0, 0);
//     lv_anim_set_playback_delay(&PropertyAnimation_0, 0);
//     lv_anim_set_repeat_count(&PropertyAnimation_0, 0);
//     lv_anim_set_repeat_delay(&PropertyAnimation_0, 0);
//     lv_anim_set_early_apply(&PropertyAnimation_0, false);
//     lv_anim_set_get_value_cb(&PropertyAnimation_0, &_ui_anim_callback_get_height);
//     lv_anim_start(&PropertyAnimation_0);
//     ui_anim_user_data_t *PropertyAnimation_1_user_data = lv_mem_alloc(sizeof(ui_anim_user_data_t));
//     PropertyAnimation_1_user_data->target = TargetObject;
//     PropertyAnimation_1_user_data->val = -1;
//     lv_anim_t PropertyAnimation_1;
//     lv_anim_init(&PropertyAnimation_1);
//     lv_anim_set_time(&PropertyAnimation_1, 1000);
//     lv_anim_set_user_data(&PropertyAnimation_1, PropertyAnimation_1_user_data);
//     lv_anim_set_custom_exec_cb(&PropertyAnimation_1, _ui_anim_callback_set_opacity);
//     lv_anim_set_values(&PropertyAnimation_1, 0, 255);
//     lv_anim_set_path_cb(&PropertyAnimation_1, lv_anim_path_linear);
//     lv_anim_set_delay(&PropertyAnimation_1, delay + 1000);
//     lv_anim_set_deleted_cb(&PropertyAnimation_1, _ui_anim_callback_free_user_data);
//     lv_anim_set_playback_time(&PropertyAnimation_1, 0);
//     lv_anim_set_playback_delay(&PropertyAnimation_1, 0);
//     lv_anim_set_repeat_count(&PropertyAnimation_1, 0);
//     lv_anim_set_repeat_delay(&PropertyAnimation_1, 0);
//     lv_anim_set_early_apply(&PropertyAnimation_1, false);
//     lv_anim_start(&PropertyAnimation_1);
// }

// void shorttoptobottom_Animation(lv_obj_t *TargetObject, int delay)
// {
//     ui_anim_user_data_t *PropertyAnimation_0_user_data = lv_mem_alloc(sizeof(ui_anim_user_data_t));
//     PropertyAnimation_0_user_data->target = TargetObject;
//     PropertyAnimation_0_user_data->val = -1;
//     lv_anim_t PropertyAnimation_0;
//     lv_anim_init(&PropertyAnimation_0);
//     lv_anim_set_time(&PropertyAnimation_0, 1500);
//     lv_anim_set_user_data(&PropertyAnimation_0, PropertyAnimation_0_user_data);
//     lv_anim_set_custom_exec_cb(&PropertyAnimation_0, _ui_anim_callback_set_height);
//     lv_anim_set_values(&PropertyAnimation_0, 0, 30);
//     lv_anim_set_path_cb(&PropertyAnimation_0, lv_anim_path_linear);
//     lv_anim_set_delay(&PropertyAnimation_0, delay + 2000);
//     lv_anim_set_deleted_cb(&PropertyAnimation_0, _ui_anim_callback_free_user_data);
//     lv_anim_set_playback_time(&PropertyAnimation_0, 0);
//     lv_anim_set_playback_delay(&PropertyAnimation_0, 0);
//     lv_anim_set_repeat_count(&PropertyAnimation_0, 0);
//     lv_anim_set_repeat_delay(&PropertyAnimation_0, 0);
//     lv_anim_set_early_apply(&PropertyAnimation_0, false);
//     lv_anim_set_get_value_cb(&PropertyAnimation_0, &_ui_anim_callback_get_height);
//     lv_anim_start(&PropertyAnimation_0);
//     ui_anim_user_data_t *PropertyAnimation_1_user_data = lv_mem_alloc(sizeof(ui_anim_user_data_t));
//     PropertyAnimation_1_user_data->target = TargetObject;
//     PropertyAnimation_1_user_data->val = -1;
//     lv_anim_t PropertyAnimation_1;
//     lv_anim_init(&PropertyAnimation_1);
//     lv_anim_set_time(&PropertyAnimation_1, 1000);
//     lv_anim_set_user_data(&PropertyAnimation_1, PropertyAnimation_1_user_data);
//     lv_anim_set_custom_exec_cb(&PropertyAnimation_1, _ui_anim_callback_set_opacity);
//     lv_anim_set_values(&PropertyAnimation_1, 0, 255);
//     lv_anim_set_path_cb(&PropertyAnimation_1, lv_anim_path_linear);
//     lv_anim_set_delay(&PropertyAnimation_1, delay + 1000);
//     lv_anim_set_deleted_cb(&PropertyAnimation_1, _ui_anim_callback_free_user_data);
//     lv_anim_set_playback_time(&PropertyAnimation_1, 0);
//     lv_anim_set_playback_delay(&PropertyAnimation_1, 0);
//     lv_anim_set_repeat_count(&PropertyAnimation_1, 0);
//     lv_anim_set_repeat_delay(&PropertyAnimation_1, 0);
//     lv_anim_set_early_apply(&PropertyAnimation_1, false);
//     lv_anim_start(&PropertyAnimation_1);
// }

// void shortbottomtotop_Animation(lv_obj_t *TargetObject, int delay)
// {
//     ui_anim_user_data_t *PropertyAnimation_0_user_data = lv_mem_alloc(sizeof(ui_anim_user_data_t));
//     PropertyAnimation_0_user_data->target = TargetObject;
//     PropertyAnimation_0_user_data->val = -1;
//     lv_anim_t PropertyAnimation_0;
//     lv_anim_init(&PropertyAnimation_0);
//     lv_anim_set_time(&PropertyAnimation_0, 1500);
//     lv_anim_set_user_data(&PropertyAnimation_0, PropertyAnimation_0_user_data);
//     lv_anim_set_custom_exec_cb(&PropertyAnimation_0, _ui_anim_callback_set_height);
//     lv_anim_set_values(&PropertyAnimation_0, 0, 40);
//     lv_anim_set_path_cb(&PropertyAnimation_0, lv_anim_path_linear);
//     lv_anim_set_delay(&PropertyAnimation_0, delay + 2000);
//     lv_anim_set_deleted_cb(&PropertyAnimation_0, _ui_anim_callback_free_user_data);
//     lv_anim_set_playback_time(&PropertyAnimation_0, 0);
//     lv_anim_set_playback_delay(&PropertyAnimation_0, 0);
//     lv_anim_set_repeat_count(&PropertyAnimation_0, 0);
//     lv_anim_set_repeat_delay(&PropertyAnimation_0, 0);
//     lv_anim_set_early_apply(&PropertyAnimation_0, false);
//     lv_anim_set_get_value_cb(&PropertyAnimation_0, &_ui_anim_callback_get_height);
//     lv_anim_start(&PropertyAnimation_0);
//     ui_anim_user_data_t *PropertyAnimation_1_user_data = lv_mem_alloc(sizeof(ui_anim_user_data_t));
//     PropertyAnimation_1_user_data->target = TargetObject;
//     PropertyAnimation_1_user_data->val = -1;
//     lv_anim_t PropertyAnimation_1;
//     lv_anim_init(&PropertyAnimation_1);
//     lv_anim_set_time(&PropertyAnimation_1, 1000);
//     lv_anim_set_user_data(&PropertyAnimation_1, PropertyAnimation_1_user_data);
//     lv_anim_set_custom_exec_cb(&PropertyAnimation_1, _ui_anim_callback_set_opacity);
//     lv_anim_set_values(&PropertyAnimation_1, 0, 255);
//     lv_anim_set_path_cb(&PropertyAnimation_1, lv_anim_path_linear);
//     lv_anim_set_delay(&PropertyAnimation_1, delay + 1000);
//     lv_anim_set_deleted_cb(&PropertyAnimation_1, _ui_anim_callback_free_user_data);
//     lv_anim_set_playback_time(&PropertyAnimation_1, 0);
//     lv_anim_set_playback_delay(&PropertyAnimation_1, 0);
//     lv_anim_set_repeat_count(&PropertyAnimation_1, 0);
//     lv_anim_set_repeat_delay(&PropertyAnimation_1, 0);
//     lv_anim_set_early_apply(&PropertyAnimation_1, false);
//     lv_anim_set_get_value_cb(&PropertyAnimation_1, &_ui_anim_callback_get_opacity);
//     lv_anim_start(&PropertyAnimation_1);
// }

// void scroll_anim_enable()
// {
//     // Page_main_scroll
//     lv_obj_set_scroll_snap_y(ui_mainlist, LV_SCROLL_SNAP_CENTER);
//     lv_obj_set_scroll_dir(ui_mainlist, LV_DIR_VER);

//     lv_obj_add_event_cb(ui_mainlist, main_scroll_cb, LV_EVENT_SCROLL, NULL);
//     lv_obj_scroll_to_view(lv_obj_get_child(ui_mainlist, 0), LV_ANIM_OFF);
//     lv_event_send(ui_mainlist, LV_EVENT_SCROLL, NULL);

//     // Page_menu_scroll
//     lv_obj_set_scroll_snap_x(ui_menulist, LV_SCROLL_SNAP_CENTER);
//     lv_obj_set_scroll_dir(ui_menulist, LV_DIR_HOR);

//     lv_obj_add_event_cb(ui_menulist, menu_scroll_cb, LV_EVENT_SCROLL, NULL);
//     lv_obj_scroll_to_view(lv_obj_get_child(ui_menulist, 0), LV_ANIM_OFF);
//     lv_event_send(ui_menulist, LV_EVENT_SCROLL, NULL);

//     // Page_set_scroll
//     lv_obj_set_scroll_snap_y(ui_Set_panel, LV_SCROLL_SNAP_CENTER);
//     lv_obj_set_scroll_dir(ui_Set_panel, LV_DIR_VER);

//     lv_obj_add_event_cb(ui_Set_panel, set_scroll_cb, LV_EVENT_SCROLL, NULL);
//     lv_obj_scroll_to_view(lv_obj_get_child(ui_Set_panel, 0), LV_ANIM_OFF);
//     lv_event_send(ui_Set_panel, LV_EVENT_SCROLL, NULL);
// }

// void loading_anim_init()
// {
//     ui_Left1 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Left1);
//     lv_obj_set_width( ui_Left1, 10);
//     lv_obj_set_height( ui_Left1, 10);
//     lv_obj_set_x( ui_Left1, 60 );
//     lv_obj_set_y( ui_Left1, 174 );
//     lv_obj_add_flag(ui_Left1, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Left1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Left1, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Left1, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Left1, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Left1, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Left1, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Left1, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Left1, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Left1, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Left1, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Left1, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Left1, 5, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_radius(ui_Left1, 180, LV_PART_MAIN| LV_STATE_CHECKED);
//     lv_obj_set_style_bg_color(ui_Left1, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_CHECKED );
//     lv_obj_set_style_bg_opa(ui_Left1, 255, LV_PART_MAIN| LV_STATE_CHECKED);
//     lv_obj_set_style_outline_color(ui_Left1, lv_color_hex(0xBDFF7A), LV_PART_MAIN | LV_STATE_CHECKED );
//     lv_obj_set_style_outline_opa(ui_Left1, 255, LV_PART_MAIN| LV_STATE_CHECKED);
//     lv_obj_set_style_outline_width(ui_Left1, 2, LV_PART_MAIN| LV_STATE_CHECKED);
//     lv_obj_set_style_outline_pad(ui_Left1, 0, LV_PART_MAIN| LV_STATE_CHECKED);
//     lv_obj_set_style_shadow_color(ui_Left1, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_CHECKED );
//     lv_obj_set_style_shadow_opa(ui_Left1, 100, LV_PART_MAIN| LV_STATE_CHECKED);
//     lv_obj_set_style_shadow_width(ui_Left1, 10, LV_PART_MAIN| LV_STATE_CHECKED);
//     lv_obj_set_style_shadow_spread(ui_Left1, 5, LV_PART_MAIN| LV_STATE_CHECKED);

//     ui_Left2 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Left2);
//     lv_obj_set_width( ui_Left2, 10);
//     lv_obj_set_height( ui_Left2, 10);
//     lv_obj_set_x( ui_Left2, 83 );
//     lv_obj_set_y( ui_Left2, 150 );
//     lv_obj_add_flag(ui_Left2, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Left2, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Left2, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Left2, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Left2, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Left2, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Left2, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Left2, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Left2, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Left2, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Left2, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Left2, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Left2, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Left3 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Left3);
//     lv_obj_set_width( ui_Left3, 10);
//     lv_obj_set_height( ui_Left3, 10);
//     lv_obj_set_x( ui_Left3, 106 );
//     lv_obj_set_y( ui_Left3, 136 );
//     lv_obj_add_flag(ui_Left3, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Left3, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Left3, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Left3, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Left3, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Left3, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Left3, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Left3, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Left3, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Left3, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Left3, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Left3, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Left3, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Left4 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Left4);
//     lv_obj_set_width( ui_Left4, 10);
//     lv_obj_set_height( ui_Left4, 10);
//     lv_obj_set_x( ui_Left4, 131 );
//     lv_obj_set_y( ui_Left4, 136 );
//     lv_obj_add_flag(ui_Left4, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Left4, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Left4, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Left4, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Left4, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Left4, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Left4, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Left4, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Left4, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Left4, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Left4, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Left4, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Left4, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Left5 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Left5);
//     lv_obj_set_width( ui_Left5, 10);
//     lv_obj_set_height( ui_Left5, 10);
//     lv_obj_set_x( ui_Left5, 155 );
//     lv_obj_set_y( ui_Left5, 150 );
//     lv_obj_add_flag(ui_Left5, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Left5, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Left5, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Left5, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Left5, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Left5, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Left5, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Left5, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Left5, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Left5, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Left5, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Left5, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Left5, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Left6 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Left6);
//     lv_obj_set_width( ui_Left6, 10);
//     lv_obj_set_height( ui_Left6, 10);
//     lv_obj_set_x( ui_Left6, 180 );
//     lv_obj_set_y( ui_Left6, 174 );
//     lv_obj_add_flag(ui_Left6, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Left6, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Left6, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Left6, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Left6, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Left6, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Left6, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Left6, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Left6, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Left6, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Left6, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Left6, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Left6, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Left7 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Left7);
//     lv_obj_set_width( ui_Left7, 10);
//     lv_obj_set_height( ui_Left7, 10);
//     lv_obj_set_x( ui_Left7, 131 );
//     lv_obj_set_y( ui_Left7, -140 );
//     lv_obj_add_flag(ui_Left7, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_set_align( ui_Left7, LV_ALIGN_BOTTOM_LEFT );
//     lv_obj_clear_flag( ui_Left7, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Left7, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Left7, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Left7, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Left7, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Left7, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Left7, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Left7, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Left7, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Left7, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Left7, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Left7, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Left8 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Left8);
//     lv_obj_set_width( ui_Left8, 10);
//     lv_obj_set_height( ui_Left8, 10);
//     lv_obj_set_x( ui_Left8, 106 );
//     lv_obj_set_y( ui_Left8, -140 );
//     lv_obj_add_flag(ui_Left8, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_set_align( ui_Left8, LV_ALIGN_BOTTOM_LEFT );
//     lv_obj_clear_flag( ui_Left8, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Left8, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Left8, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Left8, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Left8, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Left8, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Left8, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Left8, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Left8, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Left8, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Left8, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Left8, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Right1 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Right1);
//     lv_obj_set_width( ui_Right1, 10);
//     lv_obj_set_height( ui_Right1, 10);
//     lv_obj_set_x( ui_Right1, 230 );
//     lv_obj_set_y( ui_Right1, 174 );
//     lv_obj_add_flag(ui_Right1, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Right1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Right1, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Right1, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Right1, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Right1, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Right1, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Right1, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Right1, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Right1, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Right1, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Right1, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Right1, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Right2 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Right2);
//     lv_obj_set_width( ui_Right2, 10);
//     lv_obj_set_height( ui_Right2, 10);
//     lv_obj_set_x( ui_Right2, 254 );
//     lv_obj_set_y( ui_Right2, 150 );
//     lv_obj_add_flag(ui_Right2, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Right2, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Right2, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Right2, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Right2, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Right2, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Right2, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Right2, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Right2, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Right2, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Right2, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Right2, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Right2, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Right3 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Right3);
//     lv_obj_set_width( ui_Right3, 10);
//     lv_obj_set_height( ui_Right3, 10);
//     lv_obj_set_x( ui_Right3, 278 );
//     lv_obj_set_y( ui_Right3, 137 );
//     lv_obj_add_flag(ui_Right3, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Right3, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Right3, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Right3, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Right3, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Right3, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Right3, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Right3, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Right3, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Right3, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Right3, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Right3, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Right3, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Right4 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Right4);
//     lv_obj_set_width( ui_Right4, 10);
//     lv_obj_set_height( ui_Right4, 10);
//     lv_obj_set_x( ui_Right4, 302 );
//     lv_obj_set_y( ui_Right4, 136 );
//     lv_obj_add_flag(ui_Right4, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Right4, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Right4, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Right4, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Right4, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Right4, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Right4, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Right4, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Right4, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Right4, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Right4, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Right4, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Right4, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Right5 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Right5);
//     lv_obj_set_width( ui_Right5, 10);
//     lv_obj_set_height( ui_Right5, 10);
//     lv_obj_set_x( ui_Right5, 327 );
//     lv_obj_set_y( ui_Right5, 150 );
//     lv_obj_add_flag(ui_Right5, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Right5, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Right5, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Right5, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Right5, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Right5, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Right5, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Right5, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Right5, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Right5, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Right5, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Right5, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Right5, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Right6 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Right6);
//     lv_obj_set_width( ui_Right6, 10);
//     lv_obj_set_height( ui_Right6, 10);
//     lv_obj_set_x( ui_Right6, 352 );
//     lv_obj_set_y( ui_Right6, 174 );
//     lv_obj_add_flag(ui_Right6, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_clear_flag( ui_Right6, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Right6, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Right6, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Right6, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Right6, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Right6, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Right6, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Right6, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Right6, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Right6, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Right6, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Right6, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Right7 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Right7);
//     lv_obj_set_width( ui_Right7, 10);
//     lv_obj_set_height( ui_Right7, 10);
//     lv_obj_set_x( ui_Right7, 302 );
//     lv_obj_set_y( ui_Right7, -140 );
//     lv_obj_add_flag(ui_Right7, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_set_align( ui_Right7, LV_ALIGN_BOTTOM_LEFT );
//     lv_obj_clear_flag( ui_Right7, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Right7, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Right7, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Right7, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Right7, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Right7, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Right7, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Right7, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Right7, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Right7, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Right7, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Right7, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

//     ui_Right8 = lv_obj_create(ui_Page_Loading);
//     lv_obj_remove_style_all(ui_Right8);
//     lv_obj_set_width( ui_Right8, 10);
//     lv_obj_set_height( ui_Right8, 10);
//     lv_obj_set_x( ui_Right8, 278 );
//     lv_obj_set_y( ui_Right8, -140 );
//     lv_obj_add_flag(ui_Right8, LV_OBJ_FLAG_HIDDEN);
//     lv_obj_set_align( ui_Right8, LV_ALIGN_BOTTOM_LEFT );
//     lv_obj_clear_flag( ui_Right8, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
//     lv_obj_set_style_radius(ui_Right8, 180, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_bg_color(ui_Right8, lv_color_hex(0xF0FFE2), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_bg_opa(ui_Right8, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_color(ui_Right8, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_outline_opa(ui_Right8, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_width(ui_Right8, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_outline_pad(ui_Right8, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_color(ui_Right8, lv_color_hex(0x8DFF56), LV_PART_MAIN | LV_STATE_DEFAULT );
//     lv_obj_set_style_shadow_opa(ui_Right8, 100, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_width(ui_Right8, 10, LV_PART_MAIN| LV_STATE_DEFAULT);
//     lv_obj_set_style_shadow_spread(ui_Right8, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

// }