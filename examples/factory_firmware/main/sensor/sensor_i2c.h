/**
 * I2C hal
 * Author: WayenWeng <jinyuan.weng@seeed.cc>
*/

#ifndef SENSOR_I2C_H
#define SENSOR_I2C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENSIRION_TIMEOUT_MS 2000
#define SENSIRION_WORD_SIZE 2
#define SENSIRION_CRC8_POLYNOMIAL 0x31
#define SENSIRION_CRC8_INIT 0xFF
#define SENSIRION_CRC8_LEN 1

/**
 * @brief Convert an array of bytes to an uint16_t
 *
 * Convert an array of bytes received from the sensor in big-endian/MSB-first
 * format to an uint16_t value in the correct system-endianness.
 *
 * @param bytes An array of at least two bytes (MSB first)
 * @return      The byte array represented as uint16_t
 */
uint16_t sensirion_common_bytes_to_uint16_t(const uint8_t* bytes);

/**
 * @brief Add a command to the buffer at offset.
 *
 * @param buffer  Pointer to buffer in which the write frame will be prepared.
 *                Caller needs to make sure that there is enough space after
 *                offset left to write the data into the buffer.
 * @param offset  Offset of the next free byte in the buffer.
 * @param command Command to be written into the buffer.
 *
 * @return        Offset of next free byte in the buffer after writing the data.
 */
uint16_t sensirion_i2c_add_command_to_buffer(uint8_t* buffer, uint16_t offset, uint16_t command);

/**
 * @brief Writes data to the Sensor.
 *
 * @param address I2C address to write to.
 * @param data    Pointer to the buffer containing the data to write.
 * @param length  Number of bytes to send to the Sensor.
 *
 * @return        0 on success, error code otherwise
 */
int16_t sensirion_i2c_write_data(uint8_t address, uint8_t *data, uint16_t length);

/**
 * @brief Reads data from the Sensor, use for SHT4x.
 *
 * @param address Sensor I2C address
 * @param data    Allocated buffer to store data as bytes. Needs
 *                to be big enough to store the data including
 *                CRC. 
 * @param length Number of bytes to read (without CRC).
 *
 * @return       0 on success, an error code otherwise
 */
int16_t sensirion_i2c_read_data_inplace(uint8_t address, uint8_t *data, uint16_t length);

/**
 * @brief Sleep for a given number of microseconds. The function should delay the
 * execution for at least the given time, but may also sleep longer.
 *
 * Despite the unit, a <10 millisecond precision is sufficient.
 *
 * @param useconds the sleep time in microseconds
 */
void sensirion_i2c_hal_sleep_usec(uint32_t useconds);

#ifdef __cplusplus
}
#endif

#endif
