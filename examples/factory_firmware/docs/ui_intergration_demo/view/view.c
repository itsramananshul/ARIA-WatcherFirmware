#include "view.h"
#include "view_image_preview.h"
#include "view_alarm.h"
#include "view_pages.h"
#include "sensecap-watcher.h"

#include "util.h"
#include "ui/ui_helpers.h"
#include <time.h>
#include "app_device_info.h"
#include "app_png.h"
#include "ui_manager/pm.h"
#include "ui_manager/animation.h"
#include "ui_manager/event.h"

#define PNG_IMG_NUMS 24

static const char *TAG = "view";

extern GroupInfo group_page_example;
extern lv_obj_t * g_example;


int view_init(void)
{
    lvgl_port_lock(0);
    ui_init();
    lv_pm_init();
    lv_pm_open_page(g_example, &group_page_example, PM_ADD_OBJS_TO_GROUP, &ui_Page_example, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_Page_example_screen_init);

    lvgl_port_unlock();

    return 0;
}
