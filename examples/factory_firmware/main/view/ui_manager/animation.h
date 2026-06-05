#ifndef PM_ANIMA_H
#define PM_ANIMA_H

#include "pm.h"

/**
 * @brief Callback function for handling the scroll event in the main screen.
 * 
 * This function adjusts the translation and opacity of child objects based on their vertical position relative to the container's center.
 * 
 * @param e Pointer to the event structure.
 */
void main_scroll_cb(lv_event_t* e);

/**
 * @brief Callback function for handling the scroll event in the menu screen.
 * 
 * This function adjusts the translation and opacity of child objects based on their horizontal position relative to the container's center.
 * 
 * @param e Pointer to the event structure.
 */
void menu_scroll_cb(lv_event_t* e);

/**
 * @brief Callback function for handling the scroll event in the settings screen.
 * 
 * This function adjusts the translation of child objects based on their vertical position relative to the container's center.
 * 
 * @param e Pointer to the event structure.
 */
void set_scroll_cb(lv_event_t *e);

/**
 * @brief Animation for the sidelines.
 * 
 * This function creates an animation for the height and opacity of the target object with a specified delay.
 * 
 * @param TargetObject Pointer to the target LVGL object.
 * @param delay The delay before starting the animation in milliseconds.
 */
void sidelines_Animation( lv_obj_t *TargetObject, int delay);

/**
 * @brief Animation for the second line.
 * 
 * This function creates an animation for the height and opacity of the target object with a specified delay.
 * 
 * @param TargetObject Pointer to the target LVGL object.
 * @param delay The delay before starting the animation in milliseconds.
 */
void secondline_Animation( lv_obj_t *TargetObject, int delay);

/**
 * @brief Animation from top to bottom.
 * 
 * This function creates a short animation for the height and opacity of the target object with a specified delay.
 * 
 * @param TargetObject Pointer to the target LVGL object.
 * @param delay The delay before starting the animation in milliseconds.
 */
void shorttoptobottom_Animation( lv_obj_t *TargetObject, int delay);

/**
 * @brief Animation from bottom to top.
 * 
 * This function creates a short animation for the height and opacity of the target object with a specified delay.
 * 
 * @param TargetObject Pointer to the target LVGL object.
 * @param delay The delay before starting the animation in milliseconds.
 */
void shortbottomtotop_Animation( lv_obj_t *TargetObject, int delay);

/**
 * @brief Enable scroll animations for various UI elements.
 * 
 * This function sets up scroll snap and direction, and adds scroll event callbacks for the main list, menu list, and settings panel.
 */
void scroll_anim_enable();

/**
 * @brief Initialize loading animations.
 * 
 * This function creates and configures multiple objects for the loading animation, setting their styles, positions, and flags.
 */
void loading_anim_init();

#endif
