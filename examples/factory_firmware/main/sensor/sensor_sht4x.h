/**
 * SHT4x driver
 * Author: WayenWeng <jinyuan.weng@seeed.cc>
*/

#ifndef SENSOR_SHT4x_H
#define SENSOR_SHT4x_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_SHT4x_I2C_ADDR 0x44

/**
 * @brief Initializes the sensor.
 *
 * @return 0 on success, an error code otherwise
 */
int16_t sensor_sht4x_init(void);

/**
 * @brief Uninitializes the sensor.
 *
 * @return 0 on success, an error code otherwise
 */
int16_t sensor_sht4x_unint(void);

/**
 * @brief Read sensor output and convert.
 *
 * @param temperature Temperature in milli degrees centigrade.
 *
 * @param humidity Humidity in milli percent relative humidity.
 *
 * @return 0 on success, an error code otherwise
 */
int16_t sensor_sht4x_read_measurement(int32_t *temperature, int32_t *humidity);

#ifdef __cplusplus
}
#endif

#endif