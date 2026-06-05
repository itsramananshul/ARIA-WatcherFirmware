#ifndef VIEW_IMAGE_PREVIEW_H
#define VIEW_IMAGE_PREVIEW_H

#include "event_loops.h"
#include "data_defs.h"
#include "lvgl.h"
#include "tf_module_ai_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IMG_WIDTH            416
#define IMG_HEIGHT           416

#define IMG_JPEG_BUF_SIZE   48 * 1024
#define IMG_RAM_BUF_SIZE    (IMG_WIDTH * IMG_HEIGHT * LV_COLOR_DEPTH / 8)

/**
 * @brief Initialize the image preview view.
 * 
 * This function initializes the image preview view by setting up the JPEG decoder, allocating memory for image buffers,
 * and creating UI elements such as the image display and bounding boxes.
 * 
 * @param ui_screen Pointer to the LVGL screen object where the image preview will be displayed.
 * @return int Returns 0 on success, otherwise returns an error code.
 */
int view_image_preview_init(lv_obj_t *ui_screen);

/**
 * @brief Flush the image preview with new image data.
 * 
 * This function decodes the base64-encoded JPEG image data, processes the decoded image, and updates the image display
 * and bounding boxes based on the provided inference data.
 * 
 * @param p_info Pointer to the AI camera preview information structure containing image data and inference results.
 * @return int Returns 0 on success, otherwise returns an error code.
 */
int view_image_preview_flush(struct tf_module_ai_camera_preview_info *p_info);

/**
 * @brief Render a black screen.
 * 
 * This function changes the current screen to a black screen by creating a new LVGL object with a black background color.
 */
void view_image_black_flush();

// return 0 check success
int view_image_check(uint8_t *p_buf, size_t len, size_t ram_buf_len);

#ifdef __cplusplus
}
#endif

#endif
