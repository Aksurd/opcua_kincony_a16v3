#include "config.h"
#include "esp_log.h"
#include "lwip/ip_addr.h"
#include <string.h>

static const char *TAG = "config";

// Глобальная конфигурация системы (СУЩЕСТВУЮЩАЯ без изменений)
system_config_t g_config = {
    .init_complete = false,
    .config_changed = false,
    .ip_forwarding = true,
    .prefer_wifi = true,
    
    // Wi-Fi конфигурация (как было)
    .wifi = {
        .enable = true,
        .ssid = "Mz6",
        .password = "123qWe123Q",
        .authmode = 3, // WIFI_AUTH_WPA2_PSK
        .max_retry = 5,
        .scan_timeout_ms = 5000,
        .channel = 0, // авто
        .priority = 200,
        .ip_config = {
            .mode = NET_DHCP,  // ОСТАВЛЕНО как было (DHCP)
            .ip_info = {
                .ip = { .addr = ESP_IP4TOADDR(10, 0, 0, 129) },
                .netmask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) },
                .gw = { .addr = ESP_IP4TOADDR(10, 0, 0, 1) }
            },
            .dns_primary = ESP_IP4TOADDR(10, 0, 0, 1),
            .dns_secondary = ESP_IP4TOADDR(8, 8, 8, 8),
            .hostname = "esp32-wifi"
        }
    },
    
    // Ethernet конфигурация (как было)
    .eth = {
        .enable = false,
        .mosi_pin = 43,
        .miso_pin = 44,
        .sclk_pin = 42,
        .cs_pin = 15,
        .reset_pin = 1,
        .interrupt_pin = 2,
        .clock_speed_hz = 36000000,
        .host = SPI2_HOST,
        .duplex = ETH_DUPLEX_FULL,
        .speed = ETH_SPEED_100M,
        .priority = 100,
        .ip_config = {
            .mode = NET_DHCP,  // ОСТАВЛЕНО как было (DHCP)
            .ip_info = {
                .ip = { .addr = ESP_IP4TOADDR(10, 0, 0, 128) },
                .netmask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) },
                .gw = { .addr = ESP_IP4TOADDR(10, 0, 0, 1) }
            },
            .dns_primary = ESP_IP4TOADDR(10, 0, 0, 1),
            .dns_secondary = ESP_IP4TOADDR(8, 8, 8, 8),
            .hostname = "esp32-eth"
        }
    },
    
    // Конфигурация времени (как было)
    .time = {
        .mode = TIME_SYNC_SNTP,
        .ntp_server1 = "pool.ntp.org",
        .ntp_server2 = "time.google.com",
        .ntp_server3 = "time.windows.com",
        .timezone = "UTC+3",
        .sync_interval = 3600,
        .sync_on_ip_obtained = true
    },
    
    // НОВЫЕ поля OPC UA (инициализированы в config_init_defaults)
    .opcua_auth_enable = false,
    .opcua_user_count = 0  // Будет установлено в config_init_defaults
};

/* ==================== Существующие функции (БЕЗ ИЗМЕНЕНИЙ) ==================== */

void config_init_defaults(void)
{
    // Инициализация новых полей OPC UA
    g_config.opcua_auth_enable = true; // Авторизация включена
    g_config.opcua_user_count = 3;
    
    // Пользователь 0: operator (только чтение и просмотр)
    strcpy(g_config.opcua_users[0].username, "operator");
    strcpy(g_config.opcua_users[0].password, "readonly123");
    g_config.opcua_users[0].rights = OPCUA_ROLE_VIEWER;
    g_config.opcua_users[0].enabled = true;
    
    // Пользователь 1: engineer (чтение/запись)
    strcpy(g_config.opcua_users[1].username, "engineer");
    strcpy(g_config.opcua_users[1].password, "readwrite456");
    g_config.opcua_users[1].rights = OPCUA_ROLE_OPERATOR;
    g_config.opcua_users[1].enabled = true;
    
    // Пользователь 2: admin (все права)
    strcpy(g_config.opcua_users[2].username, "admin");
    strcpy(g_config.opcua_users[2].password, "admin789");
    g_config.opcua_users[2].rights = OPCUA_ROLE_ADMIN;
    g_config.opcua_users[2].enabled = true;
    
    // Остальные пользователи отключены
    for (int i = 3; i < 10; i++) {
        g_config.opcua_users[i].username[0] = '\0';
        g_config.opcua_users[i].password[0] = '\0';
        g_config.opcua_users[i].rights = OPCUA_RIGHT_NONE;
        g_config.opcua_users[i].enabled = false;
    }
    
    g_config.init_complete = true;
    ESP_LOGI(TAG, "System configuration initialized with defaults");
    ESP_LOGI(TAG, "OPC UA users: operator, engineer, admin (auth disabled by default)");
}

void config_wifi_set_static_ip(const char *ip, const char *netmask, const char *gateway)
{
    if (!g_config.init_complete) return;
    
    g_config.wifi.ip_config.mode = NET_STATIC;
    g_config.wifi.ip_config.ip_info.ip.addr = config_ip_to_int(ip);
    g_config.wifi.ip_config.ip_info.netmask.addr = config_ip_to_int(netmask);
    g_config.wifi.ip_config.ip_info.gw.addr = config_ip_to_int(gateway);
    g_config.config_changed = true;
    
    ESP_LOGI(TAG, "Wi-Fi static IP set: %s/%s gw:%s", ip, netmask, gateway);
}

void config_eth_set_static_ip(const char *ip, const char *netmask, const char *gateway)
{
    if (!g_config.init_complete) return;
    
    g_config.eth.ip_config.mode = NET_STATIC;
    g_config.eth.ip_config.ip_info.ip.addr = config_ip_to_int(ip);
    g_config.eth.ip_config.ip_info.netmask.addr = config_ip_to_int(netmask);
    g_config.eth.ip_config.ip_info.gw.addr = config_ip_to_int(gateway);
    g_config.config_changed = true;
    
    ESP_LOGI(TAG, "Ethernet static IP set: %s/%s gw:%s", ip, netmask, gateway);
}

void config_wifi_set_dhcp(void)
{
    if (!g_config.init_complete) return;
    
    g_config.wifi.ip_config.mode = NET_DHCP;
    g_config.config_changed = true;
    ESP_LOGI(TAG, "Wi-Fi set to DHCP mode");
}

void config_eth_set_dhcp(void)
{
    if (!g_config.init_complete) return;
    
    g_config.eth.ip_config.mode = NET_DHCP;
    g_config.config_changed = true;
    ESP_LOGI(TAG, "Ethernet set to DHCP mode");
}

void config_set_dns_servers(const char *primary, const char *secondary)
{
    if (!g_config.init_complete) return;
    
    g_config.wifi.ip_config.dns_primary = config_ip_to_int(primary);
    g_config.wifi.ip_config.dns_secondary = config_ip_to_int(secondary);
    
    g_config.eth.ip_config.dns_primary = g_config.wifi.ip_config.dns_primary;
    g_config.eth.ip_config.dns_secondary = g_config.wifi.ip_config.dns_secondary;
    
    g_config.config_changed = true;
    ESP_LOGI(TAG, "DNS servers set: %s, %s", primary, secondary);
}

void config_set_ntp_servers(const char *server1, const char *server2, const char *server3)
{
    if (!g_config.init_complete) return;
    
    strncpy(g_config.time.ntp_server1, server1, sizeof(g_config.time.ntp_server1) - 1);
    g_config.time.ntp_server1[sizeof(g_config.time.ntp_server1) - 1] = '\0';
    
    if (server2) {
        strncpy(g_config.time.ntp_server2, server2, sizeof(g_config.time.ntp_server2) - 1);
        g_config.time.ntp_server2[sizeof(g_config.time.ntp_server2) - 1] = '\0';
    }
    
    if (server3) {
        strncpy(g_config.time.ntp_server3, server3, sizeof(g_config.time.ntp_server3) - 1);
        g_config.time.ntp_server3[sizeof(g_config.time.ntp_server3) - 1] = '\0';
    }
    
    g_config.config_changed = true;
    ESP_LOGI(TAG, "NTP servers set: %s, %s, %s", 
             server1, 
             server2 ? server2 : "none", 
             server3 ? server3 : "none");
}

uint32_t config_ip_to_int(const char *ip_str)
{
    struct ip4_addr ip;
    if (ip4addr_aton(ip_str, &ip)) {
        return ip.addr;
    }
    return 0;
}

void config_int_to_ip(uint32_t ip_int, char *buf, size_t buf_size)
{
    struct ip4_addr ip = { .addr = ip_int };
    ip4addr_ntoa_r(&ip, buf, buf_size);
}

/* ==================== НОВЫЕ функции для OPC UA ==================== */

opcua_user_t* config_find_opcua_user(const char *username)
{
    if (!g_config.init_complete || !username) {
        return NULL;
    }
    
    for (int i = 0; i < g_config.opcua_user_count; i++) {
        opcua_user_t *user = &g_config.opcua_users[i];
        if (user->enabled && strcmp(user->username, username) == 0) {
            return user;
        }
    }
    
    return NULL;
}

bool config_check_opcua_password(opcua_user_t *user, const char *password)
{
    if (!user || !password || !user->enabled) {
        return false;
    }
    
    return (strcmp(user->password, password) == 0);
}

bool config_check_opcua_rights(opcua_user_t *user, uint16_t required_rights)
{
    if (!user || !user->enabled) {
        return false;
    }
    
    // Если аутентификация отключена, разрешаем все (анонимный доступ)
    if (!g_config.opcua_auth_enable) {
        return true;
    }
    
    return ((user->rights & required_rights) == required_rights);
}

bool config_is_opcua_auth_enabled(void) {
    return g_config.opcua_auth_enable;
}

void config_set_opcua_auth_enabled(bool enabled) {
    g_config.opcua_auth_enable = enabled;
    g_config.config_changed = true;
    ESP_LOGI(TAG, "OPC UA authentication %s", enabled ? "enabled" : "disabled");
}