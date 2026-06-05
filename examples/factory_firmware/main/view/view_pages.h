#ifndef VIEW_PAGES_H
#define VIEW_PAGES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui/ui.h"

/**
 * @brief Initialize view pages.
 * 
 * This function initializes the different view pages by calling their respective initialization functions.
 */
void view_pages_init();

/**
 * @brief Initialize the task error view.
 * 
 * This function sets up the task error view, including creating UI elements such as labels and buttons,
 * and setting their properties and styles.
 */
void view_task_error_init();

/**
 * @brief Initialize the emoji OTA (Over-The-Air) update view.
 * 
 * This function sets up the emoji OTA update view, including creating UI elements such as labels, arcs,
 * and buttons, and setting their properties and styles.
 */
void view_emoji_ota_init();

/**
 * @brief Initialize the standby mode view.
 * 
 * This function sets up the standby mode view, including creating the UI elements and setting their properties
 * and styles to be hidden and non-scrollable.
 */
void view_standby_mode_init();

void hide_all_overlays(void);

#ifdef __cplusplus
}
#endif


#endif