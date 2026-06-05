#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sensecap-watcher.h"
#include "sensor_i2c.h"
#include "sensor_scd4x.h"

static const char *TAG = "scd4x";

int16_t sensor_scd4x_init(void)
{
    esp_err_t ret = ESP_OK;
    ret = bsp_i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed");
        return ret;
    }

    uint8_t buffer[2] = {0};
    sensirion_i2c_add_command_to_buffer(&buffer[0], 0, 0x36F6);
    sensirion_i2c_write_data(SENSOR_SCD4x_I2C_ADDR, &buffer[0], 2); // wake up, sensor does not acknowledge the wake-up call, error is ignored
    sensirion_i2c_hal_sleep_usec(20000);

    sensirion_i2c_add_command_to_buffer(&buffer[0], 0, 0x3F86);
    ret = sensirion_i2c_write_data(SENSOR_SCD4x_I2C_ADDR, &buffer[0], 2); // stop measurement
    ESP_RETURN_ON_ERROR(ret, TAG, "stop measurement error %d!", ret);
    sensirion_i2c_hal_sleep_usec(500000);

    sensirion_i2c_add_command_to_buffer(&buffer[0], 0, 0x3646);
    ret = sensirion_i2c_write_data(SENSOR_SCD4x_I2C_ADDR, &buffer[0], 2); // init
    ESP_RETURN_ON_ERROR(ret, TAG, "init error %d!", ret);
    sensirion_i2c_hal_sleep_usec(20000);

    sensirion_i2c_add_command_to_buffer(&buffer[0], 0, 0x21B1);
    sensirion_i2c_write_data(SENSOR_SCD4x_I2C_ADDR, &buffer[0], 2); // start measurement
    ESP_RETURN_ON_ERROR(ret, TAG, "start measurement error %d!", ret);
    sensirion_i2c_hal_sleep_usec(1000);

    return ret;
}

int16_t sensor_scd4x_uninit(void)
{
    esp_err_t ret = ESP_OK;
    // TODO
    return ret;
}

int16_t sensor_scd4x_get_data_ready_flag(bool* data_ready_flag)
{
    esp_err_t ret = ESP_OK;
    uint8_t buffer[4];
    uint16_t local_data_ready = 0;

    sensirion_i2c_add_command_to_buffer(&buffer[0], 0, 0xE4B8);
    ret = sensirion_i2c_write_data(SENSOR_SCD4x_I2C_ADDR, &buffer[0], 2);
    ESP_RETURN_ON_ERROR(ret, TAG, "write ready flag command error %d!", ret);
    sensirion_i2c_hal_sleep_usec(1000);

    ret = sensirion_i2c_read_data_inplace(SENSOR_SCD4x_I2C_ADDR, &buffer[0], 2);
    ESP_RETURN_ON_ERROR(ret, TAG, "read ready flag data error %d!", ret);

    local_data_ready = sensirion_common_bytes_to_uint16_t(&buffer[0]);
    *data_ready_flag = (local_data_ready & 0x07FF) != 0;
    return ret;
}

int16_t sensor_scd4x_read_measurement(uint32_t *co2, int32_t* temperature_m_deg_c,
                                        int32_t* humidity_m_percent_rh)
{
    esp_err_t ret = ESP_OK;
    uint8_t buffer[12];
    uint16_t offset = 0;
    uint16_t temperature;
    uint16_t humidity;

    offset = sensirion_i2c_add_command_to_buffer(&buffer[0], 0, 0xEC05);
    ret = sensirion_i2c_write_data(SENSOR_SCD4x_I2C_ADDR, &buffer[0], offset);
    ESP_RETURN_ON_ERROR(ret, TAG, "write measurement command error %d!", ret);
    sensirion_i2c_hal_sleep_usec(1000);

    ret = sensirion_i2c_read_data_inplace(SENSOR_SCD4x_I2C_ADDR, &buffer[0], 6);
    ESP_RETURN_ON_ERROR(ret, TAG, "read measurement data error %d!", ret);

    *co2 = (uint32_t)(sensirion_common_bytes_to_uint16_t(&buffer[0])) * 1000;
    temperature = sensirion_common_bytes_to_uint16_t(&buffer[2]);
    humidity = sensirion_common_bytes_to_uint16_t(&buffer[4]);
    *temperature_m_deg_c = ((21875 * (int32_t)temperature) >> 13) - 45000;
    *humidity_m_percent_rh = ((12500 * (int32_t)humidity) >> 13);

    return ret;
}
