/* io_cache.h - See the project LICENSE file and main/opcua_esp32.c for licensing and attribution. */

#ifndef IO_CACHE_H
#define IO_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I/O cache system
 * 
 * This function initializes the cache structures and creates the mutex
 * for thread-safe operations. Must be called before using any cache functions.
 */
void io_cache_init(void);

/**
 * @brief Get cached discrete input values
 * 
 * Retrieves the current cached value of discrete inputs (16 bits).
 * 
 * @param source_timestamp Optional pointer to store source timestamp
 * @param server_timestamp Optional pointer to store server timestamp
 * @return uint16_t Cached discrete input value (0-65535)
 */
uint16_t io_cache_get_discrete_inputs(uint64_t *source_timestamp, uint64_t *server_timestamp);

/**
 * @brief Get cached discrete output values
 * 
 * Retrieves the current cached value of discrete outputs (16 bits).
 * 
 * @param source_timestamp Optional pointer to store source timestamp
 * @param server_timestamp Optional pointer to store server timestamp
 * @return uint16_t Cached discrete output value (0-65535)
 */
uint16_t io_cache_get_discrete_outputs(uint64_t *source_timestamp, uint64_t *server_timestamp);

/**
 * @brief Update discrete input values in cache
 * 
 * Updates the cached value of discrete inputs with new hardware readings.
 * 
 * @param new_val New discrete input value (16 bits)
 * @param source_timestamp_ms Source timestamp from hardware reading
 */
void io_cache_update_discrete_inputs(uint16_t new_val, uint64_t source_timestamp_ms);

/**
 * @brief Update discrete output values in cache
 * 
 * Updates the cached value of discrete outputs with new hardware readings.
 * 
 * @param new_val New discrete output value (16 bits)
 * @param source_timestamp_ms Source timestamp from hardware reading
 */
void io_cache_update_discrete_outputs(uint16_t new_val, uint64_t source_timestamp_ms);

/**
 * @brief Start I/O polling task
 * 
 * Starts the background task that periodically polls hardware I/O
 * and updates the cache with current values.
 */
void io_polling_task_start(void);

#ifdef __cplusplus
}
#endif

#endif /* IO_CACHE_H */

/* ============================================================================
 * ADC Cache Functions
 * ============================================================================ */

/**
 * @brief Get cached ADC channel value
 * 
 * Retrieves the cached value of a specific ADC channel if it's valid.
 * 
 * @param channel ADC channel number (0 to NUM_ADC_CHANNELS-1)
 * @param value Pointer to store the ADC value
 * @param source_timestamp Optional pointer to store source timestamp
 * @param server_timestamp Optional pointer to store server timestamp
 * @return true if value was successfully retrieved
 * @return false if channel is invalid or value is not valid
 */
bool io_cache_get_adc_channel(int channel, float *value, uint64_t *source_timestamp, uint64_t *server_timestamp);

/**
 * @brief Get pointer to all ADC channel values
 * 
 * Returns a pointer to the array containing all ADC channel values.
 * Use with caution - no thread safety or validity checking.
 * 
 * @return float* Pointer to ADC values array
 */
float* io_cache_get_all_adc_channels(void);

/**
 * @brief Update single ADC channel value in cache
 * 
 * Updates the cached value of a specific ADC channel.
 * 
 * @param channel ADC channel number (0 to NUM_ADC_CHANNELS-1)
 * @param new_value New ADC value
 * @param source_timestamp_ms Source timestamp from hardware reading
 */
void io_cache_update_adc_channel(int channel, float new_value, uint64_t source_timestamp_ms);

/**
 * @brief Update all ADC channel values in cache
 * 
 * Updates all ADC channel values with new readings in a single operation.
 * 
 * @param values Array of new ADC values (must contain NUM_ADC_CHANNELS elements)
 * @param source_timestamp_ms Source timestamp from hardware reading
 */
void io_cache_update_all_adc_channels(float* values, uint64_t source_timestamp_ms);