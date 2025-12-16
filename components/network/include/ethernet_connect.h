#ifndef ETHERNET_CONNECT_H
#define ETHERNET_CONNECT_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация и подключение Ethernet
esp_err_t ethernet_connect(void);

// Отключение Ethernet
esp_err_t ethernet_disconnect(void);

// Получение сетевого интерфейса Ethernet
esp_netif_t *get_ethernet_netif(void);

// Получение статуса подключения
bool ethernet_is_connected(void);

// Применение IP конфигурации к интерфейсу
esp_err_t ethernet_apply_ip_config(void);

#ifdef __cplusplus
}
#endif

#endif // ETHERNET_CONNECT_H