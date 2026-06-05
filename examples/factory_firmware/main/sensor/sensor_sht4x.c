#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sensecap-watcher.h"
#include "sensor_i2c.h"
#include "sensor_sht4x.h"

static const char *TAG = "sht4x";

static int32_t convert_ticks_to_celsius(uint16_t ticks)
{
    return ((21875 * (int32_t)ticks) >> 13) - 45000;
}

static int32_t convert_ticks_to_percent_rh(uint16_t ticks)
{
    return ((15625 * (int32_t)ticks) >> 13) - 6000;
}

int16_t sensor_sht4x_init(void)
{
    esp_err_t ret = ESP_OK;
    ret = bsp_i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed");
        return ret;
    }

    uint8_t buffer[2] = {0};
    buffer[0] = 0x94;
    ret = sensirion_i2c_write_data(SENSOR_SHT4x_I2C_ADDR, buffer, 1); // soft reset
    ESP_RETURN_ON_ERROR(ret, TAG, "soft reset error %d!", ret);
    sensirion_i2c_hal_sleep_usec(10 * 1000);

    return ret;
}

int16_t sensor_sht4x_unint(void)
{
    esp_err_t ret = ESP_OK;
    // TODO
    return ret;
}

int16_t sensor_sht4x_read_measurement(int32_t *temperature, int32_t *humidity)
{
    esp_err_t ret = ESP_OK;
    uint8_t buffer[8] = {0};
    uint16_t temperature_ticks;
    uint16_t humidity_ticks;
    
    // buffer[0] = 0xfd; // high precision
    // ret = sensirion_i2c_write_data(SENSOR_SHT4x_I2C_ADDR, buffer, 1);
    // sensirion_i2c_hal_sleep_usec(10 * 1000);

    // buffer[0] = 0xf6; // medium precision
    // ret = sensirion_i2c_write_data(SENSOR_SHT4x_I2C_ADDR, buffer, 1);
    // sensirion_i2c_hal_sleep_usec(5 * 1000);

    buffer[0] = 0xe0; // lowest precision
    ret = sensirion_i2c_write_data(SENSOR_SHT4x_I2C_ADDR, buffer, 1);
    sensirion_i2c_hal_sleep_usec(2 * 1000);
    ESP_RETURN_ON_ERROR(ret, TAG, "write measurement command error %d!", ret);

    ret = sensirion_i2c_read_data_inplace(SENSOR_SHT4x_I2C_ADDR, buffer, 4);
    ESP_RETURN_ON_ERROR(ret, TAG, "resd measurement data error %d!", ret);

    temperature_ticks = sensirion_common_bytes_to_uint16_t(&buffer[0]);
    humidity_ticks = sensirion_common_bytes_to_uint16_t(&buffer[2]);

    *temperature = convert_ticks_to_celsius(temperature_ticks);
    *humidity = convert_ticks_to_percent_rh(humidity_ticks);

    return ret;
}
