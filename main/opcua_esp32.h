/*
 * ============================================================================
 * Network Connection Manager - Header File
 * ============================================================================
 * 
 * Unified network interface for Ethernet and Wi-Fi connectivity in ESP32.
 * Provides abstraction layer for network initialization and management.
 * 
 * Developer:    Alexander Dikunov
 * Contact:      wxid_ic7ytyv3mlh522 (WeChat)
 * 
 * Platform:     ESP32 (Expressif Systems)
 * Framework:    ESP-IDF v5.5.1
 * 
 * Version:      1.0.0
 * Date:         December 2025
 * 
 * Based on ESP-IDF example code (Public Domain/CC0 licensed)
 * 
 * ============================================================================
 * MIT License
 * ============================================================================
 * 
 * Copyright (c) 2025 Alexander Dikunov
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * ============================================================================
 */

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

/* PSRAM configuration for memory-intensive applications */
#define CONFIG_ESP32_SPIRAM_SUPPORT 1
#define CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC 1

/* Ethernet configuration section */
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

/* Wi-Fi configuration section */
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#define BASE_IP_EVENT WIFI_EVENT
#define GOT_IP_EVENT IP_EVENT_STA_GOT_IP
#define DISCONNECT_EVENT WIFI_EVENT_STA_DISCONNECTED
#define EXAMPLE_INTERFACE TCPIP_ADAPTER_IF_STA
#endif

/* ============================================================================
 * PUBLIC FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Establish network connection (Ethernet or Wi-Fi)
 * 
 * Initializes and connects to network based on configuration.
 * Blocks until connection is established and IP address is obtained.
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 * 
 * @note For Ethernet: Uses LAN8720 PHY with predefined GPIOs
 * @note For Wi-Fi: Uses SSID and password from sdkconfig
 * @note Blocks indefinitely until connection is established
 */
esp_err_t example_connect(void);

/**
 * @brief Disconnect from network
 * 
 * Gracefully disconnects from network and cleans up resources.
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 * 
 * @note Must be called before re-initializing network connection
 * @note Cleans up event handlers and network interfaces
 */
esp_err_t example_disconnect(void);

/**
 * @brief Get the network interface instance
 * 
 * Returns pointer to the active network interface (esp_netif_t).
 * 
 * @return esp_netif_t* Pointer to network interface, NULL if not initialized
 * 
 * @note Used for network configuration and status queries
 * @note Valid only after successful example_connect()
 */
esp_netif_t *get_example_netif(void);

/**
 * @brief Configure DNS server for network interface
 * 
 * Sets DNS server address for the specified network interface.
 * 
 * @param netif Pointer to network interface
 * @param addr DNS server IP address in network byte order
 * @param type DNS server type (main, backup, fallback)
 * @return esp_err_t ESP_OK on success, error code on failure
 * 
 * @note Typically called during network initialization
 * @note Supports IPv4 addresses only in this implementation
 */
esp_err_t set_dns_server(esp_netif_t *netif, uint32_t addr, esp_netif_dns_type_t type);

#ifdef __cplusplus
}
#endif