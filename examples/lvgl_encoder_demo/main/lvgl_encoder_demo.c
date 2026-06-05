/*
 * SPDX-FileCopyrightText: 2024 Seeed Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "sensecap-watcher.h"


extern void lv_demo_keypad_encoder(void);

static const char *TAG = "Main";

lv_disp_t *lvgl_disp;


static lv_group_t * g;

static lv_obj_t * screen1;
static lv_obj_t * label1;
static lv_obj_t * btn11;
static lv_obj_t * label11;
static lv_obj_t * btn12;
static lv_obj_t * label12;

static lv_obj_t * screen2;
static lv_obj_t * label2;
static lv_obj_t * btn21;
static lv_obj_t * label21;
static lv_obj_t * btn22;
static lv_obj_t * label22;
static void sceen1_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if( code == LV_EVENT_FOCUSED) {
        lv_scr_load_anim(obj, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
    } else  if( code == LV_EVENT_CLICKED) {
        // lv_group_remove_all_objs(g);
        // lv_group_add_obj(g, btn21);
        // lv_group_add_obj(g, btn22);
    }
}

static void sceen2_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if( code == LV_EVENT_FOCUSED) {
        lv_scr_load_anim(obj, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
    }
}

static void long_press_event_cb(void)
{
    ESP_LOGI(TAG, "long_press_event_cb");
}

static void long_release_event_cb(void)
{
    ESP_LOGI(TAG, "long_release_event_cb");
}

void lv_demo_encoder(void)
{
    g = lv_group_get_default();
    if(g == NULL) {
        g = lv_group_create();
        lv_group_set_default(g);
    }

    lv_indev_t * cur_drv = NULL;
    for(;;) {
        cur_drv = lv_indev_get_next(cur_drv);
        if(!cur_drv) {
            break;
        }

        if(cur_drv->driver->type == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(cur_drv, g);
        }
    }

    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_GREEN), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);

    //screen1
    screen1 = lv_obj_create(NULL);
    lv_obj_add_flag(screen1, LV_OBJ_FLAG_CLICKABLE);

    label1 = lv_label_create(screen1);

    lv_obj_set_width( label1, LV_SIZE_CONTENT);
    lv_obj_set_height( label1, LV_SIZE_CONTENT); 
    lv_obj_set_y( label1, -60 );
    lv_obj_set_align(label1, LV_ALIGN_CENTER);
    lv_label_set_text(label1, "screen1");


    btn11 = lv_btn_create(screen1);
    lv_obj_set_align(btn11, LV_ALIGN_CENTER);
    lv_obj_set_size(btn11, 100, 50);
    lv_obj_set_y( btn11, 0 );
    label11 = lv_label_create(btn11);
    lv_label_set_text(label11, "Button11");
    lv_obj_center(label11);

    btn12 = lv_btn_create(screen1);
    lv_obj_set_align(btn12, LV_ALIGN_CENTER);
    lv_obj_set_size(btn12, 100, 50);
    lv_obj_set_y( btn12, 60 );
    label12 = lv_label_create(btn12);
    lv_label_set_text(label12, "Button12");
    lv_obj_center(label12);


    //screen2
    screen2 = lv_obj_create(NULL);
    lv_obj_add_flag(screen2, LV_OBJ_FLAG_CLICKABLE);

    label2 = lv_label_create(screen2);
    lv_label_set_text(label2, "screen2");
    lv_obj_set_align(label2, LV_ALIGN_CENTER);
    lv_obj_set_y( label2, -60);
    lv_obj_set_width( label2, LV_SIZE_CONTENT);
    lv_obj_set_height( label2, LV_SIZE_CONTENT); 

    btn21 = lv_btn_create(screen2);
    lv_obj_set_align(btn21, LV_ALIGN_CENTER);
    lv_obj_set_size(btn21, 100, 50);
    lv_obj_set_y( btn21, 0 );
    label21 = lv_label_create(btn21);
    lv_label_set_text(label21, "Button21");
    lv_obj_center(label21);

    btn22 = lv_btn_create(screen2);
    lv_obj_set_align(btn22, LV_ALIGN_CENTER);
    lv_obj_set_size(btn22, 100, 50);
    lv_obj_set_y( btn22, 60 );
    label22 = lv_label_create(btn22);
    lv_label_set_text(label22, "Button22");
    lv_obj_center(label22);
    
    lv_obj_add_event_cb(screen1, sceen1_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(screen2, sceen2_event_cb, LV_EVENT_ALL, NULL);

    lv_scr_load_anim(screen1, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);

    // lv_group_remove_all_objs(g);
    lv_group_add_obj(g, screen1);
    lv_group_add_obj(g, screen2);

    printf("group cnt:%d\r\n",lv_group_get_obj_count(g));
}


void app_main(void)
{
    bsp_io_expander_init();
    lvgl_disp = bsp_lvgl_init();
    assert(lvgl_disp != NULL);
    bsp_set_btn_long_press_cb(long_press_event_cb);
    bsp_set_btn_long_release_cb(long_release_event_cb);

    // lv_demo_keypad_encoder();
    lv_demo_encoder();
}
