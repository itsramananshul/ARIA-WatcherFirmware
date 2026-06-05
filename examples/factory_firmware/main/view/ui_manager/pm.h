#ifndef PM_H
#define PM_H

#include "lvgl.h"

extern lv_group_t *g_main;
extern lv_indev_t *cur_drv;

typedef struct
{
  lv_obj_t *g_prepage;
  lv_obj_t *g_curpage;
  lv_obj_t *g_prefocused_obj;
  lv_obj_t *g_curfocused_obj;
} lv_pm_page_record;

#define MAX_OBJECTS_IN_GROUP 12

typedef struct
{
  lv_obj_t * group[MAX_OBJECTS_IN_GROUP];     // Array of pointers to objects
  uint8_t obj_count;                          // Number of objects in group
} GroupInfo;


typedef enum
{
  PM_ADD_OBJS_TO_GROUP, // 0: change screen, and add all the objs to group
  PM_NO_OPERATION,      // 1: only change screen without operation
  PM_CLEAR_GROUP        // 2: change screen, and clear all the objs of group
} pm_operation_t;

/**
 * @brief Initialize the page manager (PM) system.
 * 
 * This function initializes the page manager system by creating the main LVGL group, assigning it to the encoder input device,
 * and initializing the different groups for various UI components.
 */
void lv_pm_init(void);

/**
 * @brief Open a new page and manage the group of objects.
 * 
 * This function handles the transition to a new page, including setting the focused objects and page records, 
 * managing the group of objects based on the specified operation, and applying the screen transition animation.
 * 
 * @param group Pointer to the LVGL group.
 * @param groupInfo Pointer to the GroupInfo structure containing the objects to be managed.
 * @param operation The operation to be performed on the group (e.g., add objects, clear group).
 * @param target Pointer to the target LVGL object (page) to be opened.
 * @param fademode The screen load animation mode.
 * @param spd The speed of the screen transition animation.
 * @param delay The delay before starting the screen transition animation.
 * @param target_init Pointer to the function that initializes the target page.
 */
void lv_pm_open_page(lv_group_t * group, GroupInfo *groupInfo, pm_operation_t operation, lv_obj_t **target,
                    lv_scr_load_anim_t fademode, int spd, int delay, void (*target_init)(void));

#endif