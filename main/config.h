#ifndef CONFIG_H
#define CONFIG_H

#include "esp_netif.h"
//#include "driver/spi_master.h"
#include "esp_eth.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== OPC UA Аутентификация и права ==================== */

typedef enum {
    OPCUA_RIGHT_NONE      = 0x0000,
    OPCUA_RIGHT_CONNECT   = 0x0001,  // Может подключиться к серверу
    OPCUA_RIGHT_BROWSE    = 0x0002,  // Просмотр адресного пространства
    OPCUA_RIGHT_READ      = 0x0004,  // Чтение переменных
    OPCUA_RIGHT_WRITE     = 0x0008,  // Запись переменных
    OPCUA_RIGHT_SUBSCRIBE = 0x0010,  // Подписка на изменения
    OPCUA_RIGHT_CALL      = 0x0020,  // Вызов методов
    OPCUA_RIGHT_CONFIG    = 0x0040,  // Изменение конфигурации
    OPCUA_RIGHT_ADMIN     = 0x8000   // Административные права
} opcua_user_rights_t;

#define OPCUA_ROLE_VIEWER      (OPCUA_RIGHT_CONNECT | OPCUA_RIGHT_BROWSE | OPCUA_RIGHT_READ)
#define OPCUA_ROLE_OPERATOR    (OPCUA_ROLE_VIEWER | OPCUA_RIGHT_WRITE | OPCUA_RIGHT_SUBSCRIBE)
#define OPCUA_ROLE_MAINTAINER  (OPCUA_ROLE_OPERATOR | OPCUA_RIGHT_CALL | OPCUA_RIGHT_CONFIG)
#define OPCUA_ROLE_ADMIN       (0xFFFF)

typedef struct {
    char username[24];
    char password[24];
    uint16_t rights;
    bool enabled;
} opcua_user_t;

/* ==================== Существующие типы ==================== */

typedef enum {
    NET_DHCP = 0,
    NET_STATIC = 1
} net_ip_mode_t;

typedef enum {
    TIME_SYNC_NONE = 0,
    TIME_SYNC_SNTP = 1
} time_sync_mode_t;

typedef struct {
    net_ip_mode_t mode;
    esp_netif_ip_info_t ip_info;
    uint32_t dns_primary;
    uint32_t dns_secondary;
    char hostname[32];
} ip_config_t;

typedef struct {
    bool enable;
    char ssid[32];
    char password[64];
    uint8_t authmode;
    uint8_t max_retry;
    uint16_t scan_timeout_ms;
    uint8_t channel;
    ip_config_t ip_config;
    uint8_t priority;
} app_wifi_config_t;

typedef struct {
    bool enable;
    int mosi_pin;
    int miso_pin;
    int sclk_pin;
    int cs_pin;
    int reset_pin;
    int interrupt_pin;
    int clock_speed_hz;
    spi_host_device_t host;
    eth_duplex_t duplex;
    eth_speed_t speed;
    ip_config_t ip_config;
    uint8_t priority;
} eth_config_t;

typedef struct {
    time_sync_mode_t mode;
    char ntp_server1[64];
    char ntp_server2[64];
    char ntp_server3[64];
    char timezone[32];
    uint32_t sync_interval;
    bool sync_on_ip_obtained;
} time_config_t;

/* ==================== Глобальная конфигурация ==================== */

typedef struct {
    // Существующие поля
    app_wifi_config_t wifi;
    eth_config_t eth;
    time_config_t time;
    bool ip_forwarding;
    bool prefer_wifi;
    bool init_complete;
    bool config_changed;
    
    // НОВЫЕ поля для OPC UA
    bool opcua_auth_enable;          // Включена ли авторизация
    bool opcua_anonymous_enable;     // Разрешен ли анонимный доступ при включенной авторизации
    opcua_user_t opcua_users[10];
    uint8_t opcua_user_count;
} system_config_t;

extern system_config_t g_config;

/* ==================== Существующие функции ==================== */

void config_init_defaults(void);
void config_wifi_set_static_ip(const char *ip, const char *netmask, const char *gateway);
void config_eth_set_static_ip(const char *ip, const char *netmask, const char *gateway);
void config_wifi_set_dhcp(void);
void config_eth_set_dhcp(void);
void config_set_dns_servers(const char *primary, const char *secondary);
void config_set_ntp_servers(const char *server1, const char *server2, const char *server3);
uint32_t config_ip_to_int(const char *ip_str);
void config_int_to_ip(uint32_t ip_int, char *buf, size_t buf_size);

/* ==================== НОВЫЕ функции для OPC UA ==================== */

opcua_user_t* config_find_opcua_user(const char *username);
bool config_check_opcua_password(opcua_user_t *user, const char *password);
bool config_check_opcua_rights(opcua_user_t *user, uint16_t required_rights);
bool config_is_opcua_auth_enabled(void);
void config_set_opcua_auth_enabled(bool enabled);
bool config_is_opcua_anonymous_enabled(void);
void config_set_opcua_anonymous_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H