#include "config.h"
#include "esp_log.h"
#include "lwip/ip_addr.h"
#include <string.h>

static const char *TAG = "config";

// Глобальная конфигурация системы
system_config_t g_config = {
    .init_complete = false,
    .config_changed = false,
    .ip_forwarding = true,
    .prefer_wifi = true,
    
    // Wi-Fi конфигурация по умолчанию
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
            .mode = NET_STATIC,  // БЫЛО: NET_DHCP - СТАЛО: NET_STATIC
            .ip_info = {
		    .ip = { .addr = ESP_IP4TOADDR(10, 0, 0, 129) },   // 10.0.0.128
		    .netmask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) },  // 255.255.255.0
		    .gw = { .addr = ESP_IP4TOADDR(10, 0, 0, 1) }      // 10.0.0.1
            },
		    .dns_primary = ESP_IP4TOADDR(10, 0, 0, 1),      // Google DNS
		    .dns_secondary = ESP_IP4TOADDR(8, 8, 8, 8),    // Google DNS вторичный
            .hostname = "esp32-wifi"
        }
    },
    
    // Ethernet конфигурация по умолчанию
    .eth = {
	    .enable = false,
	    .mosi_pin = 43,
	    .miso_pin = 44,
	    .sclk_pin = 42,
	    .cs_pin = 15,
	    .reset_pin = 1,
	    .interrupt_pin = 2,
	    .clock_speed_hz = 36000000, // 20 MHz
	    .host = SPI2_HOST,  // ИЗМЕНИТЬ С SPI3_HOST на SPI2_HOST (для ESP32-S3)
	    .duplex = ETH_DUPLEX_FULL,
	    .speed = ETH_SPEED_100M,
	    .priority = 100,
	    .ip_config = {
            .mode = NET_STATIC,  // БЫЛО: NET_DHCP - СТАЛО: NET_STATIC
		    .ip_info = {
			    .ip = { .addr = ESP_IP4TOADDR(10, 0, 0, 128) },   // 10.0.0.128
			    .netmask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) },  // 255.255.255.0
			    .gw = { .addr = ESP_IP4TOADDR(10, 0, 0, 1) }      // 10.0.0.1
		    },
			    .dns_primary = ESP_IP4TOADDR(10, 0, 0, 1),      // Google DNS
			    .dns_secondary = ESP_IP4TOADDR(8, 8, 8, 8),    // Google DNS вторичный
            	.hostname = "esp32-eth"
        }
    },
    
    // Конфигурация времени по умолчанию
    .time = {
        .mode = TIME_SYNC_SNTP,
        .ntp_server1 = "pool.ntp.org",
        .ntp_server2 = "time.google.com",
        .ntp_server3 = "time.windows.com",
        .timezone = "UTC+3",
        .sync_interval = 3600, // 1 час
        .sync_on_ip_obtained = true
    }
};

void config_init_defaults(void)
{
    // Уже инициализировано статически выше
    g_config.init_complete = true;
    ESP_LOGI(TAG, "System configuration initialized with defaults");
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
    
    // Копируем те же DNS для Ethernet
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
