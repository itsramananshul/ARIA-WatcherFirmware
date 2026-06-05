#ifndef EVENT_H
#define EVENT_H

#include "ui/ui.h"

extern lv_obj_t *ui_Left1;
extern lv_obj_t *ui_Left2;
extern lv_obj_t *ui_Left3;
extern lv_obj_t *ui_Left4;
extern lv_obj_t *ui_Left5;
extern lv_obj_t *ui_Left6;
extern lv_obj_t *ui_Left7;
extern lv_obj_t *ui_Left8;
extern lv_obj_t *ui_Right1;
extern lv_obj_t *ui_Right2;
extern lv_obj_t *ui_Right3;
extern lv_obj_t *ui_Right4;
extern lv_obj_t *ui_Right5;
extern lv_obj_t *ui_Right6;
extern lv_obj_t *ui_Right7;
extern lv_obj_t *ui_Right8;

// taskflow error msg event
void taskerrc_cb(lv_event_t *e);

// Wi-Fi config status
void waitForWifi();
void waitForBinding();
void waitForAddDev();
void bindFinish();
void wifiConnectFailed();

// view alarm panel event
void ui_event_alarm_panel(lv_event_t * e);

// view emoji ota event;
void ui_event_emoticonok(lv_event_t * e);

void viewInfoInit();
void view_info_obtain();

void emoji_timer(uint8_t emoji_type);

enum
{
    SCREEN_VIRTUAL, // display emoticon on virtual page
    SCREEN_AVATAR,  // display emoticon on avatar page
    SCREEN_GUIDE,    // display emoticon on guide page
    SCREEN_STANDBY  // display emoticon on standby page
};

enum
{
    EMOJI_GREETING,
    EMOJI_DETECTING,
    EMOJI_DETECTED,
    EMOJI_SPEAKING,
    EMOJI_LISTENING,
    EMOJI_ANALYZING,
    EMOJI_STANDBY,
    EMOJI_STOP
};

#endif