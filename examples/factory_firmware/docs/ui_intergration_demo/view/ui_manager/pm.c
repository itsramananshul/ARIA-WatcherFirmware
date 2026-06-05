#include "pm.h"
#include "animation.h"
#include "ui/ui.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdio.h>

static const char *TAG = "PM_EVENT";
#define PM_PAGE_PRINTER (0)

lv_pm_page_record g_page_record;
lv_group_t *g_example;
lv_indev_t *cur_drv;

GroupInfo group_page_example;

static void lv_pm_obj_group(lv_group_t * group, GroupInfo *groupInfo);

// Function to add objects to the group
static void addObjToGroup(GroupInfo *groupInfo, lv_obj_t *objects[], int count) {
    if (count > MAX_OBJECTS_IN_GROUP) {
        printf("Error: Object count exceeds maximum limit\n");
        return;
    }

    // Copy objects into the group
    for (int i = 0; i < count; i++) {
        groupInfo->group[i] = objects[i];
    }
    // Update object count
    groupInfo->obj_count = count;
}

// Function to print objects in the group
static void printGroup(GroupInfo *groupInfo) {
    ESP_LOGI(TAG, "obj_count in group: %d", groupInfo->obj_count);
    for (int i = 0; i < groupInfo->obj_count; i++) {
        ESP_LOGI(TAG, "%d: %p\n", i+1, groupInfo->group[i]);
    }
}

// Function to open page and add group
void lv_pm_open_page(lv_group_t * group, GroupInfo *groupInfo, pm_operation_t operation, lv_obj_t **target, lv_scr_load_anim_t fademode, int spd, int delay, void (*target_init)(void))
{
    static lv_obj_t * focused_obj;
    if (g_page_record.g_curfocused_obj != NULL)
    {
        g_page_record.g_prefocused_obj = g_page_record.g_curfocused_obj;
    }
    g_page_record.g_curfocused_obj = lv_group_get_focused(group);

    if (g_page_record.g_curpage != NULL)
    {
        g_page_record.g_prepage = g_page_record.g_curpage;
    }
    g_page_record.g_curpage = *target;
    if (*target == NULL)
        target_init();

#if PM_PAGE_PRINTER
    if (g_page_record.g_prepage)
    {
        const char *prepage_name = lv_obj_get_user_data(g_page_record.g_prepage);
        ESP_LOGI(TAG, "The Previous Page : %s", prepage_name);
    }
    if (g_page_record.g_curpage)
    {
        const char *curpage_name = lv_obj_get_user_data(g_page_record.g_curpage);
        ESP_LOGI(TAG, "The Current  Page : %s", curpage_name);
    }
    if (g_page_record.g_prefocused_obj)
    {
        const char *preobj_name = lv_obj_get_user_data(g_page_record.g_prefocused_obj);
        ESP_LOGI(TAG, "The Previous_focused obj : %s", preobj_name);
    }
    if (g_page_record.g_curfocused_obj)
    {
        const char *curobj_name = lv_obj_get_user_data(g_page_record.g_curfocused_obj);
        ESP_LOGI(TAG, "The Current_focused obj : %s", curobj_name);
    }
#endif
    switch (operation)
    {
        case PM_ADD_OBJS_TO_GROUP:
            if ((group != NULL) && (groupInfo != NULL)){
                lv_pm_obj_group(group, groupInfo);
            }
            break;
        case PM_NO_OPERATION:
            break;
        case PM_CLEAR_GROUP:
                lv_group_remove_all_objs(group);
            break;
    }
    lv_group_focus_obj(g_page_record.g_prefocused_obj);
    lv_scr_load_anim(*target, fademode, spd, delay, false);
}

static void lv_pm_obj_group(lv_group_t * group, GroupInfo *groupInfo)
{
    lv_group_remove_all_objs(group);
    for (uint8_t index = 0; index < groupInfo->obj_count; index++)
    {
        lv_group_add_obj(group, groupInfo->group[index]);
        const char *group_obj_name = lv_obj_get_user_data(groupInfo->group[index]);
    }
}


// Function to init groups
void initGroup()
{
    // define objects array
    lv_obj_t * example_objects[]           = {ui_Button1, ui_Button2, ui_Button3};

    addObjToGroup(&group_page_example, example_objects, sizeof(example_objects) / sizeof(example_objects[0]));
}


// Function to init pm components
void lv_pm_init(void)
{
    g_example = lv_group_create();
    cur_drv = NULL;
    while ((cur_drv = lv_indev_get_next(cur_drv)))
    {
        if (cur_drv->driver->type == LV_INDEV_TYPE_ENCODER)
        {
            lv_indev_set_group(cur_drv, g_example);
            break;
        }
    }

    initGroup();
    
    // printGroup(&group_page_main);

#if PM_PAGE_PRINTER
    lv_obj_set_user_data(ui_Page_Vir, "ui_Page_Virtual");
    lv_obj_set_user_data(ui_Page_main, "ui_Page_main");
    lv_obj_set_user_data(ui_Page_Connect, "ui_Page_Connect");
    lv_obj_set_user_data(ui_Page_nwifi, "ui_Page_nwifi");
    lv_obj_set_user_data(ui_Page_Wifi, "ui_Page_Wifi");
    lv_obj_set_user_data(ui_Page_CurTask1, "ui_Page_CurTask1");
    lv_obj_set_user_data(ui_Page_CurTask2, "ui_Page_CurTask2");
    lv_obj_set_user_data(ui_Page_CurTask3, "ui_Page_CurTask3");
    lv_obj_set_user_data(ui_Page_ViewAva, "ui_Page_ViewAva");
    lv_obj_set_user_data(ui_Page_ViewLive, "ui_Page_ViewLive");

    lv_obj_set_user_data(ui_Page_LocTask, "ui_Page_LocTask");
    lv_obj_set_user_data(ui_Page_Set, "ui_Page_Set");
    lv_obj_set_user_data(ui_Page_SAbout, "ui_Page_SAbout");
    lv_obj_set_user_data(ui_Page_HA, "ui_Page_HA");
    lv_obj_set_user_data(ui_Page_Swipe, "ui_Page_Swipe");
    lv_obj_set_user_data(ui_Page_STime, "ui_Page_STime");
    lv_obj_set_user_data(ui_Page_brivol, "ui_Page_brivol");

    lv_obj_set_user_data(ui_Page_Start, "ui_Page_Start");
    lv_obj_set_user_data(ui_mainbtn1, "ui_mainbtn1");
    lv_obj_set_user_data(ui_mainbtn2, "ui_mainbtn2");
    lv_obj_set_user_data(ui_mainbtn3, "ui_mainbtn3");
    lv_obj_set_user_data(ui_mainbtn4, "ui_mainbtn4");

    lv_obj_set_user_data(ui_setapp, "ui_setapp");
    lv_obj_set_user_data(ui_setback, "ui_setback");
#endif

    // scroll_anim_enable();
    // loading_anim_init();
}