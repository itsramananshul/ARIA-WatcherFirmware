#include <time.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "sensecap-watcher.h"
#include "app_rgb.h"
#include "event_loops.h"
#include "data_defs.h"
#include "esp_check.h"

static const char *TAG = "app_rgb";
#define MAX_CALLERS 6
#define STACK_SIZE  10

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t max_brightness_led;
    uint8_t min_brightness_led;
    int step;
    int delay_time;
    int type;
} rgb_status;

static rgb_status rgb_status_instance;
static TaskHandle_t task_handle = NULL;
static StackType_t *app_rgb_stack = NULL;
static StaticTask_t app_rgb_stack_buffer;
static esp_timer_handle_t blink_timer_handle = NULL;

typedef struct
{
    int caller;
    rgb_service_t service;
    rgb_status status;
} caller_context_t;

static caller_context_t caller_contexts[STACK_SIZE];
static int stack_top = -1; // Empty stack

static SemaphoreHandle_t data_mutex;
static SemaphoreHandle_t rgb_mutex;

static esp_timer_handle_t rgb_timer_handle;
static uint8_t flag = 0;


static void __data_lock( void )
{
    xSemaphoreTake( data_mutex, portMAX_DELAY);
}
static void __data_unlock( void)
{
    xSemaphoreGive(data_mutex);  
}

static void __rgb_lock( void )
{
    xSemaphoreTake( rgb_mutex, portMAX_DELAY);
}
static void __rgb_unlock( void)
{
    xSemaphoreGive(rgb_mutex);  
}

static void __rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    __rgb_lock();
    bsp_rgb_set(r, g, b);
    __rgb_unlock();
}

/**
 * @brief Select and set the RGB service
 *
 * This function sets the RGB status based on the service requested by the caller.
 *
 * @param caller The ID of the caller
 * @param service The RGB service requested
 */
static void set_rgb_status(int r, int g, int b, int type, int step, int delay_time)
{
    rgb_status_instance.r = r;
    rgb_status_instance.g = g;
    rgb_status_instance.b = b;
    rgb_status_instance.type = type;
    rgb_status_instance.step = step;
    rgb_status_instance.delay_time = delay_time;
    rgb_status_instance.max_brightness_led = 255;
    rgb_status_instance.min_brightness_led = 0;
}

void __select_service_set_rgb(int caller, int service)
{
    static int __rgb_switch = 0;

    if (caller == UI_CALLER && service == RGB_ON)
    {
        __rgb_switch = 1;
    }
    else if (caller == UI_CALLER && service == RGB_OFF)
    {
        __rgb_switch = 0;
    }
    if (__rgb_switch == 1)
    {
        switch (service)
        {
            case RGB_BREATH_RED:
                set_rgb_status(70, 1, 1, 1, 3, 5);
                break;
            case RGB_BREATH_GREEN:
                set_rgb_status(0, 255, 0, 1, 3, 5);
                break;
            case RGB_BREATH_BLUE:
                set_rgb_status(0, 5, 55, 1, 3, 5);
                break;
            case RGB_BREATH_WHITE:
                set_rgb_status(255, 255, 255, 1, 1, 5);
                break;
            case RGB_BLINK_RED:
                set_rgb_status(70, 1, 1, 2, 10, 50);
                break;
            case RGB_BLINK_GREEN:
                set_rgb_status(0, 255, 0, 2, 10, 50);
                break;
            case RGB_BLINK_BLUE:
                set_rgb_status(0, 5, 55, 2, 10, 50);
                break;
            case RGB_BLINK_WHITE:
                set_rgb_status(255, 255, 255, 2, 10, 50);
                break;
            case RGB_FLARE_RED:
                set_rgb_status(70, 1, 1, 3, 5, 25);
                break;
            case RGB_FLARE_GREEN:
                set_rgb_status(0, 255, 0, 3, 5, 25);
                break;
            case RGB_FLARE_BLUE:
                set_rgb_status(0, 5, 55, 3, 5, 25);
                break;
            case RGB_FLARE_WHITE:
                set_rgb_status(255, 255, 255, 3, 5, 25);
                break;
            case RGB_OFF:
                set_rgb_status(0, 0, 0, 4, 0, 0);
                break;
            default:
                ESP_LOGW(TAG, "Unknown service: %d", service);
                set_rgb_status(0, 0, 0, 4, 0, 0);
                break;
        }
    }
    else
    {
        set_rgb_status(0, 0, 0, 4, 0, 0);
    }

}

/**
 * @brief Set breath color effect
 *
 * This function sets the RGB light to perform a breathing effect with the specified color.
 *
 * @param status The current RGB status
 */
static void __set_breath_color(rgb_status *status)
{
    static uint8_t brightness_led = 0;
    static bool increasing = true;

    if (increasing)
    {
        brightness_led += status->step;
        if (brightness_led >= status->max_brightness_led)
        {
            brightness_led = status->max_brightness_led;
            increasing = false;
        }
    }
    else
    {
        brightness_led -= status->step;
        if (brightness_led <= status->min_brightness_led)
        {
            brightness_led = status->min_brightness_led;
            increasing = true;
        }
    }

    uint8_t current_r = (status->r * brightness_led) / 255;
    uint8_t current_g = (status->g * brightness_led) / 255;
    uint8_t current_b = (status->b * brightness_led) / 255;

    __rgb_set(current_r, current_g, current_b);

    vTaskDelay(pdMS_TO_TICKS(status->delay_time));
}

/**
 * @brief Blink effect
 *
 * This function starts or stops the blink effect based on the start parameter.
 *
 * @param interval The interval for the blink effect in seconds
 * @param start True to start the effect, false to stop
 */
static bool led_on = false;
static void blink_timer_callback(void *arg)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (led_on)
    {
        __rgb_set(0, 0, 0);
    }
    else
    {
        __data_lock();
        r = rgb_status_instance.r;
        g = rgb_status_instance.g;
        b = rgb_status_instance.b;
        __data_unlock();

        __rgb_set(r, g, b);
    }
    led_on = !led_on;
}

static void __blink(double interval, bool start)
{
    static bool is_blinking = false;
    if (start)
    {
        if (!is_blinking)
        {
            is_blinking = true;

            if (esp_timer_is_active(blink_timer_handle))
            {
                esp_timer_stop(blink_timer_handle);
            }
            led_on = false;
            esp_timer_start_periodic(blink_timer_handle, interval * 1000000 * 0.5);
        }
    }
    else
    {
        if (is_blinking)
        {
            is_blinking = false;
            if (esp_timer_is_active(blink_timer_handle))
            {
                esp_timer_stop(blink_timer_handle);
            }
        }
    }
}


/**
 * @brief RGB breath effect task
 *
 * This task handles the breath effect of the RGB lights.
 *
 * @param arg Task argument (not used)
 */
void breath_effect_task(void *arg)
{
    rgb_status rgb_status_temp;
    while (true)
    {
        __data_lock();
        memcpy(&rgb_status_temp, &rgb_status_instance, sizeof(rgb_status));
        __data_unlock();
        
        switch (rgb_status_temp.type)
        {
            case 1:
                __blink(1, false);
                __set_breath_color(&rgb_status_temp);
                break;
            case 2:
                __blink(1, true);
                break;
            case 3:
                __blink(1, false);
                __rgb_set(rgb_status_temp.r, rgb_status_temp.g, rgb_status_temp.b);
                
                __data_lock();
                set_rgb_status(0, 0, 0, 5, 0, 0);
                __data_unlock();
                break;
            case 4:
                __blink(1, false);
                __rgb_set(rgb_status_temp.r, rgb_status_temp.g, rgb_status_temp.b);

                __data_lock();
                set_rgb_status(0, 0, 0, 5, 0, 0);
                __data_unlock();
                break;
            default:
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Initialize the RGB application
 *
 * This function initializes the RGB application, creating necessary tasks and resources.
 *
 * @return 0 on success, -1 on failure
 */
int app_rgb_init(void)
{
    esp_err_t ret = ESP_OK;

    //RGB off
    set_rgb_status(0, 0, 0, 4, 0, 0);

    data_mutex = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != data_mutex, ESP_ERR_NO_MEM, err, TAG, "Failed to create mutex");

    rgb_mutex = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != rgb_mutex, ESP_ERR_NO_MEM, err, TAG, "Failed to create mutex");

    app_rgb_stack = (StackType_t *)heap_caps_malloc(4096 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    ESP_GOTO_ON_FALSE(app_rgb_stack, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    task_handle = xTaskCreateStatic(&breath_effect_task, "app_rgb_task", 4096, &rgb_status_instance, 3, app_rgb_stack, &app_rgb_stack_buffer);
    ESP_GOTO_ON_FALSE( task_handle, ESP_FAIL, err, TAG, "Failed to create task");

    esp_timer_create_args_t timer_args = { .callback = &blink_timer_callback, .arg = NULL, .name = "blink_timer" };
    esp_timer_create(&timer_args, &blink_timer_handle);

    return ESP_OK;
err:
    if (data_mutex) {
        vSemaphoreDelete(data_mutex);
        data_mutex = NULL;
    }

    if (rgb_mutex) {
        vSemaphoreDelete(rgb_mutex);
        rgb_mutex = NULL;
    }

    if (app_rgb_stack) {
        heap_caps_free(app_rgb_stack);
        app_rgb_stack = NULL;
    }
    if (task_handle) {
        vTaskDelete(task_handle);
        task_handle = NULL;
    }
    return ret;
}


void app_rgb_set(int caller, rgb_service_t service)
{
    if (caller < 0 || caller >= MAX_CALLERS)
    {
        ESP_LOGE(TAG, "Invalid caller: %d", caller);
        return;
    }

    ESP_LOGI(TAG, "Caller: %d, Service: %d", caller, service);

    __data_lock();
    __select_service_set_rgb(caller, service);
    __data_unlock();
}

void app_rgb_status_set(int r, int g, int b, int mode, int step, int delay_time)
{
    __data_lock();
    set_rgb_status(r, g, b, mode, step, delay_time);
    __data_unlock();
}