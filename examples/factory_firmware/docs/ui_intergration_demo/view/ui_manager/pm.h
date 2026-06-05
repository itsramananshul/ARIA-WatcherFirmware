#ifndef PM_H
#define PM_H

#include "lvgl.h"


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

void lv_pm_init(void);
void lv_pm_open_page(lv_group_t * group, GroupInfo *groupInfo, pm_operation_t operation, lv_obj_t **target,
                    lv_scr_load_anim_t fademode, int spd, int delay, void (*target_init)(void));

#endif