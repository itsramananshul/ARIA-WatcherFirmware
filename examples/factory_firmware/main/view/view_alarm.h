#ifndef VIEW_ALARM_H
#define VIEW_ALARM_H

#include "event_loops.h"
#include "lvgl.h"
#include "view_image_preview.h"

#include "tf_module_local_alarm.h"
#include "tf_module_util.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the alarm view.
 * 
 * This function sets up the alarm view by initializing the JPEG decoder, allocating memory for image buffers,
 * creating the alarm indicator and other UI elements, and setting up the alarm timer.
 * 
 * @param ui_screen Pointer to the LVGL screen object where the alarm view will be displayed.
 * @return int Returns 0 on success, otherwise returns an error code.
 */
int view_alarm_init(lv_obj_t *ui_screen);

/**
 * @brief Initialize the alarm panel UI elements.
 * 
 * This function creates and configures the UI elements for the alarm panel, including buttons and labels,
 * and sets their properties and styles.
 */
void view_alarm_panel_init();

/**
 * @brief Activate the alarm.
 * 
 * This function activates the alarm by starting the alarm timer, updating the display with the alarm information,
 * and initiating the alarm indicator animation.
 * 
 * @param alarm_st Pointer to the structure containing the local alarm information.
 * @return int Returns 0 on success, otherwise returns an error code.
 */
int view_alarm_on(struct tf_module_local_alarm_info *alarm_st);

/**
 * @brief Deactivate the alarm.
 * 
 * This function deactivates the alarm by stopping the alarm timer, hiding the alarm indicator and image,
 * and resetting the display to its previous state.
 * 
 * @param task_down A flag indicating whether the task is down.
 */
void view_alarm_off(uint8_t task_down);


#ifdef __cplusplus
}
#endif

#endif
