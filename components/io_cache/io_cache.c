/* io_cache.c - See the project LICENSE file and main/opcua_esp32.c for licensing and attribution. */

#include "io_cache.h"
#include "esp_log.h"
#include "model.h" 
#include <string.h>


static const char *TAG = "io_cache";

#define NUM_DISCRETE_INPUTS 16
#define NUM_DISCRETE_OUTPUTS 16
// REMOVED: #define NUM_TEMP_SENSORS 4

/**
 * @brief I/O cache data structure
 * 
 * This structure holds cached values for discrete inputs/outputs and ADC channels
 * with associated timestamps for data synchronization between hardware polling
 * and OPC UA server access.
 */
typedef struct {
    uint16_t discrete_inputs_cache;         /**< Cached discrete input values (16 bits) */
    uint16_t discrete_outputs_cache;        /**< Cached discrete output values (16 bits) */
    // REMOVED: float temperature_cache[NUM_TEMP_SENSORS];
    // REMOVED: bool temp_valid[NUM_TEMP_SENSORS];
    uint64_t inputs_timestamp_ms;           /**< Source timestamp for inputs (hardware read time) */
    uint64_t outputs_timestamp_ms;          /**< Source timestamp for outputs (hardware read time) */
    // REMOVED: uint64_t temp_timestamp_ms[NUM_TEMP_SENSORS];
    uint64_t inputs_server_timestamp_ms;    /**< Server timestamp for inputs (cache update time) */
    uint64_t outputs_server_timestamp_ms;   /**< Server timestamp for outputs (cache update time) */
    // REMOVED: uint64_t temp_server_timestamp_ms[NUM_TEMP_SENSORS];
    SemaphoreHandle_t mutex;                /**< Mutex for thread-safe access to cache */
} io_cache_t;

/**
 * @brief ADC cache data structure
 * 
 * Separate structure for ADC channel values to support multiple analog inputs
 * with individual validity flags and timestamps.
 */
typedef struct {
    float adc_cache[NUM_ADC_CHANNELS];              /**< Cached ADC channel values */
    uint64_t adc_timestamps_ms[NUM_ADC_CHANNELS];   /**< Source timestamps for ADC values */
    uint64_t adc_server_timestamps_ms[NUM_ADC_CHANNELS]; /**< Server timestamps for ADC values */
    bool adc_valid[NUM_ADC_CHANNELS];               /**< Validity flags for ADC channels */
} io_cache_adc_t;

static io_cache_t io_cache;               /**< Main I/O cache instance */
static io_cache_adc_t adc_cache;          /**< ADC cache instance */

/**
 * @brief Get current system time in milliseconds
 * 
 * @return uint64_t Current time in milliseconds since system start
 */
static uint64_t get_current_time_ms(void) {
    return (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * @brief Initialize I/O cache system
 * 
 * This function initializes the cache structures and creates the mutex
 * for thread-safe operations. Must be called before using any cache functions.
 */
void io_cache_init(void) {
    // Initialize main I/O cache
    memset(&io_cache, 0, sizeof(io_cache_t));
    io_cache.mutex = xSemaphoreCreateMutex();
    if (io_cache.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }
    
    // Initialize ADC cache
    memset(&adc_cache, 0, sizeof(io_cache_adc_t));
    
    ESP_LOGI(TAG, "I/O cache initialized");
}

/**
 * @brief Get cached discrete input values
 * 
 * Retrieves the current cached value of discrete inputs (16 bits).
 * 
 * @param source_timestamp Optional pointer to store source timestamp
 * @param server_timestamp Optional pointer to store server timestamp
 * @return uint16_t Cached discrete input value (0-65535)
 */
uint16_t io_cache_get_discrete_inputs(uint64_t *source_timestamp, uint64_t *server_timestamp) {
    uint16_t val = 0;
    if (xSemaphoreTake(io_cache.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        val = io_cache.discrete_inputs_cache;
        if (source_timestamp) *source_timestamp = io_cache.inputs_timestamp_ms;
        if (server_timestamp) *server_timestamp = io_cache.inputs_server_timestamp_ms;
        xSemaphoreGive(io_cache.mutex);
    }
    return val;
}

/**
 * @brief Get cached discrete output values
 * 
 * Retrieves the current cached value of discrete outputs (16 bits).
 * 
 * @param source_timestamp Optional pointer to store source timestamp
 * @param server_timestamp Optional pointer to store server timestamp
 * @return uint16_t Cached discrete output value (0-65535)
 */
uint16_t io_cache_get_discrete_outputs(uint64_t *source_timestamp, uint64_t *server_timestamp) {
    uint16_t val = 0;
    if (xSemaphoreTake(io_cache.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        val = io_cache.discrete_outputs_cache;
        if (source_timestamp) *source_timestamp = io_cache.outputs_timestamp_ms;
        if (server_timestamp) *server_timestamp = io_cache.outputs_server_timestamp_ms;
        xSemaphoreGive(io_cache.mutex);
    }
    return val;
}

/**
 * @brief Update discrete input values in cache
 * 
 * Updates the cached value of discrete inputs with new hardware readings.
 * 
 * @param new_val New discrete input value (16 bits)
 * @param source_timestamp_ms Source timestamp from hardware reading
 */
void io_cache_update_discrete_inputs(uint16_t new_val, uint64_t source_timestamp_ms) {
    if (xSemaphoreTake(io_cache.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        io_cache.discrete_inputs_cache = new_val;
        io_cache.inputs_timestamp_ms = source_timestamp_ms;
        io_cache.inputs_server_timestamp_ms = get_current_time_ms();
        xSemaphoreGive(io_cache.mutex);
    }
}

/**
 * @brief Update discrete output values in cache
 * 
 * Updates the cached value of discrete outputs with new hardware readings.
 * 
 * @param new_val New discrete output value (16 bits)
 * @param source_timestamp_ms Source timestamp from hardware reading
 */
void io_cache_update_discrete_outputs(uint16_t new_val, uint64_t source_timestamp_ms) {
    if (xSemaphoreTake(io_cache.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        io_cache.discrete_outputs_cache = new_val;
        io_cache.outputs_timestamp_ms = source_timestamp_ms;
        io_cache.outputs_server_timestamp_ms = get_current_time_ms();
        xSemaphoreGive(io_cache.mutex);
    }
}

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
bool io_cache_get_adc_channel(int channel, float *value, uint64_t *source_timestamp, uint64_t *server_timestamp) {
    if (channel < 0 || channel >= NUM_ADC_CHANNELS || !adc_cache.adc_valid[channel]) {
        return false;
    }
    
    if (xSemaphoreTake(io_cache.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        *value = adc_cache.adc_cache[channel];
        if (source_timestamp) *source_timestamp = adc_cache.adc_timestamps_ms[channel];
        if (server_timestamp) *server_timestamp = adc_cache.adc_server_timestamps_ms[channel];
        xSemaphoreGive(io_cache.mutex);
        return true;
    }
    return false;
}

/**
 * @brief Get pointer to all ADC channel values
 * 
 * Returns a pointer to the array containing all ADC channel values.
 * Use with caution - no thread safety or validity checking.
 * 
 * @return float* Pointer to ADC values array
 */
float* io_cache_get_all_adc_channels(void) {
    return adc_cache.adc_cache;
}

/**
 * @brief Update single ADC channel value in cache
 * 
 * Updates the cached value of a specific ADC channel.
 * 
 * @param channel ADC channel number (0 to NUM_ADC_CHANNELS-1)
 * @param new_value New ADC value
 * @param source_timestamp_ms Source timestamp from hardware reading
 */
void io_cache_update_adc_channel(int channel, float new_value, uint64_t source_timestamp_ms) {
    if (channel < 0 || channel >= NUM_ADC_CHANNELS) return;
    
    if (xSemaphoreTake(io_cache.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        adc_cache.adc_cache[channel] = new_value;
        adc_cache.adc_timestamps_ms[channel] = source_timestamp_ms;
        adc_cache.adc_server_timestamps_ms[channel] = get_current_time_ms();
        adc_cache.adc_valid[channel] = true;
        xSemaphoreGive(io_cache.mutex);
    }
}

/**
 * @brief Update all ADC channel values in cache
 * 
 * Updates all ADC channel values with new readings in a single operation.
 * 
 * @param values Array of new ADC values (must contain NUM_ADC_CHANNELS elements)
 * @param source_timestamp_ms Source timestamp from hardware reading
 */
void io_cache_update_all_adc_channels(float* values, uint64_t source_timestamp_ms) {
    if (!values) return;
    
    if (xSemaphoreTake(io_cache.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        for (int i = 0; i < NUM_ADC_CHANNELS; i++) {
            adc_cache.adc_cache[i] = values[i];
            adc_cache.adc_timestamps_ms[i] = source_timestamp_ms;
            adc_cache.adc_server_timestamps_ms[i] = get_current_time_ms();
            adc_cache.adc_valid[i] = true;
        }
        xSemaphoreGive(io_cache.mutex);
    }
}