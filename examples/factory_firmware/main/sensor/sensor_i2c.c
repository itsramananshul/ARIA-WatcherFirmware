#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sensecap-watcher.h"
#include "sensor_i2c.h"

static const char *TAG = "i2c_hal";

static uint8_t sensirion_i2c_generate_crc(const uint8_t* data, uint16_t count)
{
    uint16_t current_byte;
    uint8_t crc = SENSIRION_CRC8_INIT;
    uint8_t crc_bit;

    for (current_byte = 0; current_byte < count; ++current_byte) {
        crc ^= (data[current_byte]);
        for (crc_bit = 8; crc_bit > 0; --crc_bit) {
            if (crc & 0x80)
                crc = (crc << 1) ^ SENSIRION_CRC8_POLYNOMIAL;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}

static int16_t sensirion_i2c_check_crc(const uint8_t* data, uint16_t count, uint8_t checksum)
{
    if (sensirion_i2c_generate_crc(data, count) != checksum)
        return -1;
    return ESP_OK;
}

uint16_t sensirion_common_bytes_to_uint16_t(const uint8_t* bytes)
{
    return (uint16_t)bytes[0] << 8 | (uint16_t)bytes[1];
}

uint16_t sensirion_i2c_add_command_to_buffer(uint8_t* buffer, uint16_t offset, uint16_t command)
{
    buffer[offset++] = (uint8_t)((command & 0xFF00) >> 8);
    buffer[offset++] = (uint8_t)((command & 0x00FF) >> 0);
    return offset;
}

int16_t sensirion_i2c_write_data(uint8_t address, uint8_t *data, uint16_t length)
{
    esp_err_t ret = ESP_OK;
    int8_t cnt = 3;
    while (cnt --) {
        ret = i2c_master_write_to_device(BSP_GENERAL_I2C_NUM, address, data, length, SENSIRION_TIMEOUT_MS / portTICK_PERIOD_MS);
        if (ret == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return ret;
}

int16_t sensirion_i2c_read_data_inplace(uint8_t address, uint8_t *data, uint16_t length)
{
    esp_err_t ret = ESP_OK;
    uint16_t i, j;
    uint16_t size = (length / SENSIRION_WORD_SIZE) * (SENSIRION_WORD_SIZE + SENSIRION_CRC8_LEN);

    if (length % 2 != 0) {
        return -1;
    }

    int8_t cnt = 3;
    while (cnt --) {
        ret = i2c_master_read_from_device(BSP_GENERAL_I2C_NUM, address, data, size, SENSIRION_TIMEOUT_MS / portTICK_PERIOD_MS);
        if (ret == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "I2C read error %d!", ret);

    for (i = 0, j = 0; i < size; i += SENSIRION_WORD_SIZE + SENSIRION_CRC8_LEN) {
        ret = sensirion_i2c_check_crc(&data[i], SENSIRION_WORD_SIZE, data[i + SENSIRION_WORD_SIZE]);
        ESP_RETURN_ON_ERROR(ret, TAG, "I2C crc error %d!", ret);
        
        data[j++] = data[i];
        data[j++] = data[i + 1];
    }

    return ret;
}

void sensirion_i2c_hal_sleep_usec(uint32_t useconds)
{
    vTaskDelay(((useconds / 1000) + portTICK_PERIOD_MS) / portTICK_PERIOD_MS);
}
