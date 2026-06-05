
/**
 * Sensor sample
 * Author: WayenWeng <jinyuan.weng@seeed.cc>
*/
#ifndef APP_SENSOR_H
#define APP_SENSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SENSOR_SUPPORT_MAX  2
#define APP_SENSOR_SAMPLE_WAIT  5000

enum eAppSensorType
{
    SENSOR_SHT4x = 0,
    SENSOR_SCD4x = 1,
    SENSOR_NONE = 0xff
};

typedef struct app_sensor_data
{
    uint32_t type;
    uint32_t state;
    union {
        struct {
            int32_t temperature;
            uint32_t humidity;
        }sht4x;
        struct {
            int32_t temperature;
            uint32_t humidity;
            uint32_t co2;
        }scd4x;
    }context;
}app_sensor_data_t;

/**
 * @brief Initialize the sensor application
 *
 * @return 0 on success, -1 on failure
 */
int16_t app_sensor_init(void);

/**
 * @brief Read the sensor measurement
 * 
 * @param data Pointer to the buffer containing the sensor measurement to read.
 *             Needs to be big enough to store the sensor measurement.
 *
 * @param length Number of buffer to read the measurement.
 * 
 * @return The number of sensor.
 */
uint8_t app_sensor_read_measurement(app_sensor_data_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif
