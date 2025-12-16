#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"  // <-- ДОБАВИТЬ для WIFI_EVENT констант

// Forward declarations to avoid circular dependencies
struct wifi_connect_s;
struct ethernet_connect_s;

// Тип коллбэка для изменения состояния сети
typedef void (*network_state_callback_t)(bool connected, esp_netif_t *netif);

// Прототипы функций
esp_err_t network_manager_init(void);
esp_err_t network_manager_start(void);
esp_err_t network_manager_stop(void);
esp_netif_t *network_manager_get_active_netif(void);
esp_netif_t *network_manager_get_eth_netif(void);
esp_netif_t *network_manager_get_wifi_netif(void);
bool network_manager_wifi_is_connected(void);
bool network_manager_eth_is_connected(void);
bool network_manager_is_any_connected(void);
void network_manager_set_state_callback(network_state_callback_t callback);

#ifdef __cplusplus
}
#endif