#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_netif.h"

/**
 * @brief Establish Wi-Fi connection
 * 
 * Initializes Wi-Fi in station mode and connects to configured network.
 * Blocks until connection is established and IP address is obtained.
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t wifi_connect(void);

/**
 * @brief Disconnect from Wi-Fi network
 * 
 * Gracefully disconnects from Wi-Fi and cleans up resources.
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t wifi_disconnect(void);

/**
 * @brief Get the Wi-Fi network interface
 * 
 * Returns pointer to the active Wi-Fi network interface.
 * 
 * @return esp_netif_t* Pointer to Wi-Fi network interface, NULL if not initialized
 */
esp_netif_t *get_wifi_netif(void);

#ifdef __cplusplus
}
#endif