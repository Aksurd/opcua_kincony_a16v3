/* ethernet_connect.h - Based on ESP-IDF examples (Apache-2.0). See project LICENSE and main file. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip_addr.h"
#include <lwip/sockets.h>
#include "freertos/task.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"

#define CONFIG_ESP32_SPIRAM_SUPPORT 1
#define CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC 1

#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
#include "esp_eth.h"
#define CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET 1
#define CONFIG_EXAMPLE_ETH_PHY_LAN8720 1
#define CONFIG_EXAMPLE_ETH_MDC_GPIO 23
#define CONFIG_EXAMPLE_ETH_MDIO_GPIO 18
#define CONFIG_EXAMPLE_ETH_PHY_ADDR 0
#define EXAMPLE_INTERFACE TCPIP_ADAPTER_IF_ETH
#define BASE_IP_EVENT ETH_EVENT
#define GOT_IP_EVENT IP_EVENT_ETH_GOT_IP
#define DISCONNECT_EVENT ETHERNET_EVENT_DISCONNECTED
#endif

#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#define BASE_IP_EVENT WIFI_EVENT
#define GOT_IP_EVENT IP_EVENT_STA_GOT_IP
#define DISCONNECT_EVENT WIFI_EVENT_STA_DISCONNECTED
#define EXAMPLE_INTERFACE TCPIP_ADAPTER_IF_STA
#endif

/**
 * @brief Establish network connection (Ethernet or Wi-Fi)
 * 
 * This function initializes the network connection based on the
 * configuration in sdkconfig.h. It can connect via Ethernet or
 * Wi-Fi depending on the build configuration.
 * 
 * @return ESP_OK if connection was successfully established
 * @return ESP_ERR_INVALID_STATE if already connected
 * 
 * @note This function blocks until IP address is obtained
 * @note Automatically registers shutdown handler for cleanup
 */
esp_err_t example_connect(void);

/**
 * @brief Disconnect from network
 * 
 * This function tears down the network connection and cleans up
 * all allocated resources.
 * 
 * @return ESP_OK if successfully disconnected
 * @return ESP_ERR_INVALID_STATE if not currently connected
 */
esp_err_t example_disconnect(void);

/**
 * @brief Get the current network interface
 * 
 * Returns a pointer to the active network interface (Ethernet or Wi-Fi).
 * 
 * @return esp_netif_t* Pointer to the network interface, or NULL if not connected
 */
esp_netif_t *get_example_netif(void);

/**
 * @brief Configure DNS server for network interface
 * 
 * Sets the DNS server address for the specified network interface.
 * Can be used for both primary and backup DNS servers.
 * 
 * @param netif Pointer to network interface
 * @param addr DNS server IP address in network byte order
 * @param type DNS server type (MAIN, BACKUP, FALLBACK)
 * @return ESP_OK if DNS server was configured successfully
 */
esp_err_t set_dns_server(esp_netif_t *netif, uint32_t addr, esp_netif_dns_type_t type);

#ifdef __cplusplus
}
#endif