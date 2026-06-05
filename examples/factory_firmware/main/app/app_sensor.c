#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sensecap-watcher.h"
#include "sensor_sht4x.h"
#include "sensor_scd4x.h"
#include "app_sensor.h"
#include "event_loops.h"
#include "data_defs.h"

static const char *TAG = "app_sensor";

static SemaphoreHandle_t app_sensor_data_sem = NULL;
static TaskHandle_t app_sensor_task_handle = NULL;
static StackType_t *app_sensor_stack = NULL;
static StaticTask_t app_sensor_stack_buffer;

static uint8_t app_sensor_def[APP_SENSOR_SUPPORT_MAX] = {
    SENSOR_SHT4x_I2C_ADDR,
    SENSOR_SCD4x_I2C_ADDR
};

static uint8_t app_sensor_det[APP_SENSOR_SUPPORT_MAX] = {0};
static uint8_t app_sensor_det_temp[APP_SENSOR_SUPPORT_MAX] = {0};

static bool app_sensor_update_data = false;
static app_sensor_data_t app_sensor_data[APP_SENSOR_SUPPORT_MAX] = {0};
static app_sensor_data_t app_sensor_data_temp[APP_SENSOR_SUPPORT_MAX] = {0};
static uint8_t app_sensor_det_num = 0;
static uint8_t app_sensor_det_num_temp = 0;

extern esp_err_t bsp_i2c_check(i2c_port_t i2c_num, uint8_t address);

static int16_t app_sensor_detect(void)
{
    esp_err_t ret = ESP_OK;
    ret = bsp_i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed");
        return ret;
    }

    memset(app_sensor_det, 0xff, APP_SENSOR_SUPPORT_MAX);
    for (uint8_t i = 0; i < APP_SENSOR_SUPPORT_MAX; i ++) {
        if (bsp_i2c_check(BSP_GENERAL_I2C_NUM, app_sensor_def[i]) == ESP_OK)
            app_sensor_det[i] = app_sensor_def[i];
    }

    if (memcmp(app_sensor_det_temp, app_sensor_det, APP_SENSOR_SUPPORT_MAX) != 0) {
        memcpy(app_sensor_det_temp, app_sensor_det, APP_SENSOR_SUPPORT_MAX);
        app_sensor_update_data = true;
        app_sensor_det_num = 0;

        for (uint8_t i = 0; i < APP_SENSOR_SUPPORT_MAX; i ++) {
            app_sensor_data[i].state = false;
            app_sensor_data[i].type = SENSOR_NONE;
        }

        for (uint8_t i = 0; i < APP_SENSOR_SUPPORT_MAX; i ++) {
            if (app_sensor_det[i] == SENSOR_SHT4x_I2C_ADDR) {
                sensor_sht4x_init();
                app_sensor_data[app_sensor_det_num].state = true;
                app_sensor_data[app_sensor_det_num].type = SENSOR_SHT4x;
                app_sensor_data[app_sensor_det_num].context.sht4x.temperature = 0;
                app_sensor_data[app_sensor_det_num].context.sht4x.humidity = 0;
                app_sensor_det_num ++;
            } else if (app_sensor_det[i] == SENSOR_SCD4x_I2C_ADDR) {
                sensor_scd4x_init();
                app_sensor_data[app_sensor_det_num].state = true;
                app_sensor_data[app_sensor_det_num].type = SENSOR_SCD4x;
                app_sensor_data[app_sensor_det_num].context.scd4x.temperature = 0;
                app_sensor_data[app_sensor_det_num].context.scd4x.humidity = 0;
                app_sensor_data[app_sensor_det_num].context.scd4x.co2 = 0;
                app_sensor_det_num ++;
            }
        }
        app_sensor_update_data = false;
    }
    
    return ret;
}

static int16_t app_sensor_uptate(void)
{
    static uint8_t sensor_error_cnt = 0;
    esp_err_t ret = ESP_OK;

    struct view_data_sensor view_data;
    view_data.temperature_valid = false;
    view_data.humidity_valid = false;
    view_data.co2_valid = false;
    view_data.temperature = 255;
    view_data.humidity = 255;
    view_data.co2 = 65535;

    app_sensor_update_data = true;

    for (uint8_t i = 0; i < APP_SENSOR_SUPPORT_MAX; i ++) {
        if (app_sensor_data[i].state) {
            if (app_sensor_data[i].type == SENSOR_SHT4x) {
                int32_t temperature = 0, humidity = 0;
                ret = sensor_sht4x_read_measurement(&temperature, &humidity);
                if (ret == 0) {
                    app_sensor_data[i].context.sht4x.temperature = temperature;
                    app_sensor_data[i].context.sht4x.humidity = humidity;
                    ESP_LOGI(TAG, "T: %d, H: %d", temperature, humidity);

                    view_data.temperature_valid = true;
                    view_data.humidity_valid = true;
                    view_data.temperature = (float)temperature / 1000;
                    view_data.humidity = (float)humidity / 1000;
                } else {
                    sensor_error_cnt ++;
                }
            } else if (app_sensor_data[i].type == SENSOR_SCD4x) {
                bool ready_flag = 0;
                uint32_t co2 = 0;
                int32_t temperature = 0, humidity = 0;
                ret = sensor_scd4x_get_data_ready_flag(&ready_flag);
                if ( ready_flag ) {
                    ret = sensor_scd4x_read_measurement(&co2, &temperature, &humidity);
                    if (ret == 0) {
                        app_sensor_data[i].context.scd4x.temperature = temperature;
                        app_sensor_data[i].context.scd4x.humidity = humidity;
                        app_sensor_data[i].context.scd4x.co2 = co2;
                        ESP_LOGI(TAG, "T: %d, H: %d, CO2: %d", temperature, humidity, co2);

                        if (view_data.temperature_valid == false) {
                            view_data.temperature_valid = true;
                            view_data.temperature = (float)temperature / 1000;
                        }
                        if (view_data.humidity_valid == false) {
                            view_data.humidity_valid = true;
                            view_data.humidity = (float)humidity / 1000;
                        }
                        view_data.co2_valid = true;
                        view_data.co2 = co2 / 1000;
                    }
                } else {
                    sensor_error_cnt ++;
                }
            }
        }
    }

    if (sensor_error_cnt >= 3 ) {
        sensor_error_cnt = 0;
        memset(app_sensor_det_temp, 0xff, APP_SENSOR_SUPPORT_MAX);
    }

    app_sensor_update_data = false;

    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR, 
                    &view_data, sizeof(struct view_data_sensor), pdMS_TO_TICKS(10000));

    return ret;
}

static void sensor_sample_task(void *p_arg)
{
    app_sensor_detect();
    while(1) {
        app_sensor_detect();
        app_sensor_uptate();
        vTaskDelay(pdMS_TO_TICKS(APP_SENSOR_SAMPLE_WAIT));
    }
}

int16_t app_sensor_init(void)
{
    esp_err_t ret = ESP_OK;
    app_sensor_data_sem = xSemaphoreCreateMutex();
    app_sensor_stack = (StackType_t *)heap_caps_malloc(4096 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    app_sensor_task_handle = xTaskCreateStatic(&sensor_sample_task, "app_sensor_task", 4096, NULL, 3, app_sensor_stack, &app_sensor_stack_buffer);
    return ret;
}

uint8_t app_sensor_read_measurement(app_sensor_data_t *data, uint16_t length)
{
    if (data == NULL)
        return 0;
    if (length < sizeof(app_sensor_data_t) * app_sensor_det_num)
        return 0;

    xSemaphoreTake(app_sensor_data_sem, portMAX_DELAY);
    if (!app_sensor_update_data) {
        app_sensor_det_num_temp = app_sensor_det_num;
        for (uint8_t i = 0; i < APP_SENSOR_SUPPORT_MAX; i ++)
            memcpy(app_sensor_data_temp + i, app_sensor_data + i, sizeof(app_sensor_data_t));
    }

    for (uint8_t i = 0; i < app_sensor_det_num_temp; i ++) {
        memcpy(data + i, app_sensor_data_temp + i, sizeof(app_sensor_data_t));
    }

    xSemaphoreGive(app_sensor_data_sem);
    return app_sensor_det_num_temp;
}
