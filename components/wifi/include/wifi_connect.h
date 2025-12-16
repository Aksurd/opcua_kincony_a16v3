#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация и подключение Wi-Fi
esp_err_t wifi_connect(void);

// Отключение Wi-Fi
esp_err_t wifi_disconnect(void);

// Получение сетевого интерфейса Wi-Fi
esp_netif_t *get_wifi_netif(void);

// Получение статуса подключения
bool wifi_is_connected(void);

// Применение IP конфигурации к интерфейсу
esp_err_t wifi_apply_ip_config(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_CONNECT_H