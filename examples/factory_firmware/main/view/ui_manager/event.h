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

/**
 * @brief Callback function for handling the task flow error message event.
 * 
 * This function hides the task error UI element and posts a task flow stop event.
 * 
 * @param e Pointer to the event structure.
 */
void taskerrc_cb(lv_event_t *e);

// Wi-Fi config status
void waitForWifi();
void waitForBinding();
void waitForAddDev();
void bindFinish();
void wifiConnectFailed();

/**
 * @brief Event handler for the alarm panel UI.
 * 
 * This function handles various events for the alarm panel, such as button clicks and focus changes.
 * 
 * @param e Pointer to the event structure.
 */
void ui_event_alarm_panel(lv_event_t * e);

/**
 * @brief Event handler for the emoji OTA (Over-The-Air) update.
 * 
 * This function handles the click event for the emoji update confirmation button.
 * 
 * @param e Pointer to the event structure.
 */
void ui_event_emoticonok(lv_event_t * e);

/**
 * @brief Initialize view information.
 * 
 * This function sets up the initial states and properties for various UI elements.
 */
void viewInfoInit();

/**
 * @brief Obtain view information.
 * 
 * This function retrieves and sets up various settings and information for the UI elements.
 */
void view_info_obtain();

/**
 * @brief Start a timer for the emoji animation.
 * 
 * This function initializes and starts a timer to handle the animation of emojis based on the specified emoji type.
 * 
 * @param emoji_type The type of emoji animation to be displayed.
 */
void emoji_timer(uint8_t emoji_type);

/**
 * @brief Initialize the Push-to-Talk interface.
 *
 * This function sets up the user interface for the Push-to-Talk feature, including the textarea
 * for displaying text and a button to start the animation.
 *
 * @return void
 */
void push2talk_init(void);

/**
 * @brief Start the character-by-character text animation.
 *
 * This function displays the given text character by character over the specified duration.
 * It checks the parameters for validity before starting the animation.
 *
 * @param text The text to display. Must be a null-terminated string.
 * @param duration_ms The total duration for the animation in milliseconds. Must be greater than 0.
 */
void push2talk_start_animation(const char *text, uint32_t duration_ms);

void view_ble_switch_timer_start();
void view_sleep_timer_start();
void view_sleep_timer_stop();
void view_push2talk_timer_start();
void view_push2talk_timer_stop();
void view_push2talk_msg_timer_start();
void view_push2talk_msg_timer_stop();
void view_push2talk_animation_timer_stop();

void view_extension_timer_start();
void view_extension_timer_stop();

void view_push2talkexpired_timer_start();
void view_push2talkexpired_timer_stop();

void view_sensor_data_update(const char *data1, const char *data2, const char *data3, const char *data4);

void emoji_timer_stop();

enum
{
    SCREEN_VIRTUAL, // display emoticon on virtual page
    SCREEN_AVATAR,  // display emoticon on avatar page
    SCREEN_GUIDE,    // display emoticon on guide page
    SCREEN_STANDBY,  // display emoticon on standby page
    SCREEN_PUSH2TALK, // display emoticon on push2talk page
    SCREEN_PUSH2TALK_SPEAK
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

// extension sensor
enum {
    EXTENSION_TEMP = 0,
    EXTENSION_HUMI,
    EXTENSION_CO2,
    EXTENSION_BACK
};

#endif