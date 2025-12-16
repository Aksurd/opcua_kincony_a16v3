#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_netif.h"

// Убрал #include "wifi_connect.h" и "ethernet_connect.h"
// Вместо этого используем forward declarations

// Forward declarations to avoid circular dependencies
struct wifi_connect_s;  // Предварительное объявление
struct ethernet_connect_s;  // Предварительное объявление

// Прототипы функций используют только стандартные типы ESP-IDF
esp_err_t network_manager_init(void);
esp_err_t network_manager_start(void);
esp_netif_t *network_manager_get_eth_netif(void);
esp_netif_t *network_manager_get_wifi_netif(void);
bool network_manager_eth_is_connected(void);
bool network_manager_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif