#include <dirent.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "sensecap-watcher.h"

static const char *TAG = "main";

void app_main(void)
{
    bsp_io_expander_init();

    while (1)
    {
        bsp_i2c_detect(BSP_GENERAL_I2C_NUM);
        ESP_LOGI(TAG, "Hello World!\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}