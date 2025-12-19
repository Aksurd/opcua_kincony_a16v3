#ifndef CONFIG_H
#define CONFIG_H

#include "esp_netif.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== Типы и константы ====================
typedef enum {
    NET_DHCP = 0,
    NET_STATIC = 1
} net_ip_mode_t;

typedef enum {
    TIME_SYNC_NONE = 0,
    TIME_SYNC_SNTP = 1
} time_sync_mode_t;

// ==================== Конфигурация IP ====================
typedef struct {
    net_ip_mode_t mode;          // DHCP или статика
    
    // Статический IP (используется если mode == NET_STATIC)
    esp_netif_ip_info_t ip_info; // IP, маска, шлюз
    
    // DNS серверы (если 0 - используется DHCP)
    uint32_t dns_primary;        // Основной DNS
    uint32_t dns_secondary;      // Вторичный DNS
    
    // Hostname (если пусто - используется дефолтный)
    char hostname[32];
} ip_config_t;

// ==================== Конфигурация Wi-Fi ====================
// ИЗМЕНЕНИЕ: переименовали wifi_config_t чтобы избежать конфликта
typedef struct {
    bool enable;                 // Включить Wi-Fi
    
    // Параметры сети
    char ssid[32];
    char password[64];
    uint8_t authmode;           // WIFI_AUTH_WPA2_PSK и т.д.
    
    // Параметры подключения
    uint8_t max_retry;          // Максимальное количество попыток
    uint16_t scan_timeout_ms;   // Таймаут сканирования
    
    // Настройки канала (0 = авто)
    uint8_t channel;
    
    // IP конфигурация для этого адаптера
    ip_config_t ip_config;
    
    // Приоритет (если оба адаптера активны)
    uint8_t priority;           // 0-255, чем больше - тем выше приоритет
} app_wifi_config_t;  // <-- ИЗМЕНЕНО С wifi_config_t на app_wifi_config_t

// ==================== Конфигурация Ethernet (W5500) ====================
typedef struct {
    bool enable;                 // Включить Ethernet
    
    // SPI пины для W5500
    int mosi_pin;
    int miso_pin;
    int sclk_pin;
    int cs_pin;
    int reset_pin;
    int interrupt_pin;
    
    // SPI настройки
    int clock_speed_hz;
    spi_host_device_t host;
    
    // Настройки PHY
    eth_duplex_t duplex;        // Полный/полудуплекс
    eth_speed_t speed;          // 10/100 Mbps
    
    // IP конфигурация для этого адаптера
    ip_config_t ip_config;
    
    // Приоритет (если оба адаптера активны)
    uint8_t priority;           // 0-255, чем больше - тем выше приоритет
} eth_config_t;

// ==================== Конфигурация времени ====================
typedef struct {
    time_sync_mode_t mode;      // Режим синхронизации
    
    // SNTP серверы (если mode == TIME_SYNC_SNTP)
    char ntp_server1[64];
    char ntp_server2[64];
    char ntp_server3[64];
    
    // Таймзона (например, "UTC+3" или "MSK-3")
    char timezone[32];
    
    // Интервал синхронизации (секунды)
    uint32_t sync_interval;
    
    // Автосинхронизация при получении IP
    bool sync_on_ip_obtained;
} time_config_t;

// ==================== Глобальная конфигурация ====================
typedef struct {
    // Конфигурации адаптеров
    app_wifi_config_t wifi;  // <-- ИЗМЕНЕНО С wifi_config_t на app_wifi_config_t
    eth_config_t eth;
    
    // Конфигурация времени
    time_config_t time;
    
    // Настройки маршрутизации
    bool ip_forwarding;         // Включить форвардинг пакетов
    bool prefer_wifi;           // Предпочитать Wi-Fi как основной шлюз
    
    // Флаги состояния
    bool init_complete;
    bool config_changed;        // Флаг изменения конфигурации
} system_config_t;

// Глобальный экземпляр конфигурации
extern system_config_t g_config;

// ==================== Функции управления конфигурацией ====================

// Инициализация конфигурации значениями по умолчанию
void config_init_defaults(void);

// Установка статического IP для Wi-Fi
void config_wifi_set_static_ip(const char *ip, const char *netmask, const char *gateway);

// Установка статического IP для Ethernet
void config_eth_set_static_ip(const char *ip, const char *netmask, const char *gateway);

// Включение DHCP для Wi-Fi
void config_wifi_set_dhcp(void);

// Включение DHCP для Ethernet
void config_eth_set_dhcp(void);

// Установка DNS серверов
void config_set_dns_servers(const char *primary, const char *secondary);

// Установка NTP серверов
void config_set_ntp_servers(const char *server1, const char *server2, const char *server3);

// Утилиты преобразования IP
uint32_t config_ip_to_int(const char *ip_str);
void config_int_to_ip(uint32_t ip_int, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H