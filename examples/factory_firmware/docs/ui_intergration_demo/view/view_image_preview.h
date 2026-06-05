#ifndef VIEW_IMAGE_PREVIEW_H
#define VIEW_IMAGE_PREVIEW_H

#include "event_loops.h"
#include "data_defs.h"
#include "lvgl.h"
#include "tf_module_ai_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

// #define IMG_WIDTH            416
// #define IMG_HEIGHT           416

// #define IMG_JPEG_BUF_SIZE   48 * 1024
// #define IMG_RAM_BUF_SIZE    (IMG_WIDTH * IMG_HEIGHT * LV_COLOR_DEPTH / 8)

// int view_image_preview_init(lv_obj_t *ui_screen);

int view_image_preview_flush(struct tf_module_ai_camera_preview_info *p_info);

// void view_image_black_flush();

#ifdef __cplusplus
}
#endif

#endif
