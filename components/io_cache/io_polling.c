/* io_polling.c - See the project LICENSE file and main/opcua_esp32.c for licensing and attribution. */

#include "io_cache.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "model.h"
#include <stdint.h>

static const char *TAG = "io_polling";

#define POLL_INPUTS_INTERVAL_MS     20    /**< Polling interval for discrete inputs in milliseconds */
#define POLL_ADC_INTERVAL_MS        100   /**< Polling interval for ADC channels in milliseconds */

/**
 * @brief Get current system time in milliseconds
 * 
 * @return uint64_t Current time in milliseconds since system start
 */
static uint64_t get_current_time_ms(void) {
    return (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * @brief I/O polling task function
 * 
 * This background task periodically polls hardware I/O (discrete inputs and ADC channels)
 * and updates the cache with current values. The task runs on Core 1 at high priority.
 * 
 * @param pvParameters Task parameters (not used)
 */
static void io_polling_task(void *pvParameters) {
    TickType_t xLastInputsTime = xTaskGetTickCount();
    static TickType_t xLastAdcTime;  // Declared as static for persistence
    
    // Initialize after declaration
    xLastAdcTime = xTaskGetTickCount();
    
    ESP_LOGI(TAG, "IO polling task started (240 MHz)");
    
    while (1) {
        TickType_t xNow = xTaskGetTickCount();
        
        // Poll discrete inputs
        if ((xNow - xLastInputsTime) * portTICK_PERIOD_MS >= POLL_INPUTS_INTERVAL_MS) {
            uint16_t inputs = read_discrete_inputs_slow();
            uint64_t timestamp = get_current_time_ms();
            io_cache_update_discrete_inputs(inputs, timestamp);
            xLastInputsTime = xNow;
        }
        
        // Poll ADC channels
        if ((xNow - xLastAdcTime) * portTICK_PERIOD_MS >= POLL_ADC_INTERVAL_MS) {
            update_all_adc_channels_slow();
            xLastAdcTime = xNow;
        }
        
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/**
 * @brief Start the I/O polling task
 * 
 * Creates and starts the background task that periodically polls hardware I/O.
 * The task is pinned to Core 1 with priority 8 for reliable real-time operation.
 */
void io_polling_task_start(void) {
    xTaskCreatePinnedToCore(io_polling_task, "io_poll", 4096, NULL, 
                           8, NULL, 1);
    ESP_LOGI(TAG, "IO polling task created");
}