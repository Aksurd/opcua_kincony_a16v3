#include "network_manager.h"
#include "config.h"
#include "wifi_connect.h"
#include "ethernet_connect.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "esp_wifi.h"

static const char *TAG = "network_manager";

// Статические переменные
static bool s_network_initialized = false;
static network_state_callback_t s_state_callback = NULL;
static bool s_wifi_connected = false;
static bool s_eth_connected = false;

// Приватные функции
static void notify_state_change(bool connected, esp_netif_t *netif);

// Обработчик событий Wi-Fi
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Wi-Fi connected to AP");
        s_wifi_connected = true;
        notify_state_change(true, get_wifi_netif());
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected from AP");
        s_wifi_connected = false;
        notify_state_change(false, get_wifi_netif());
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        // Применяем конфигурацию IP если нужно
        wifi_apply_ip_config();
        
        char ip_str[16];
        config_int_to_ip(event->ip_info.ip.addr, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Wi-Fi IP address: %s", ip_str);
    }
}

// Обработчик событий Ethernet
static void eth_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == ETH_EVENT && event_id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Ethernet link up");
        s_eth_connected = true;
        notify_state_change(true, get_ethernet_netif());
    } else if (event_base == ETH_EVENT && event_id == ETHERNET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "Ethernet link down");
        s_eth_connected = false;
        notify_state_change(false, get_ethernet_netif());
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        // Применяем конфигурацию IP если нужно
        ethernet_apply_ip_config();
        
        char ip_str[16];
        config_int_to_ip(event->ip_info.ip.addr, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Ethernet IP address: %s", ip_str);
    }
}

static void notify_state_change(bool connected, esp_netif_t *netif)
{
    if (s_state_callback != NULL) {
        s_state_callback(connected, netif);
    }
    
    // Логируем общий статус
    ESP_LOGI(TAG, "=== Network Status ===");
    ESP_LOGI(TAG, "Wi-Fi: %s", s_wifi_connected ? "CONNECTED" : "DISCONNECTED");
    ESP_LOGI(TAG, "Ethernet: %s", s_eth_connected ? "CONNECTED" : "DISCONNECTED");
    ESP_LOGI(TAG, "Any connection: %s", network_manager_is_any_connected() ? "YES" : "NO");
}

esp_err_t network_manager_init(void)
{
    if (s_network_initialized) {
        ESP_LOGW(TAG, "Network manager already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_config.init_complete) {
        ESP_LOGE(TAG, "System configuration not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing network manager...");
    ESP_LOGI(TAG, "Wi-Fi enabled: %s", g_config.wifi.enable ? "YES" : "NO");
    ESP_LOGI(TAG, "Ethernet enabled: %s", g_config.eth.enable ? "YES" : "NO");
    
    // Инициализация TCP/IP стека
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Создание цикла событий
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Регистрация обработчиков событий для Wi-Fi
    if (g_config.wifi.enable) {
        ret = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                                  &wifi_event_handler, NULL, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register Wi-Fi connected handler");
        }
        
        ret = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                  &wifi_event_handler, NULL, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register Wi-Fi disconnected handler");
        }
        
        ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  &wifi_event_handler, NULL, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register Wi-Fi IP handler");
        }
    }
    
    // Регистрация обработчиков событий для Ethernet
    if (g_config.eth.enable) {
        ret = esp_event_handler_instance_register(ETH_EVENT, ETHERNET_EVENT_CONNECTED,
                                                  &eth_event_handler, NULL, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register Ethernet connected handler");
        }
        
        ret = esp_event_handler_instance_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED,
                                                  &eth_event_handler, NULL, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register Ethernet disconnected handler");
        }
        
        ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                                  &eth_event_handler, NULL, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register Ethernet IP handler");
        }
    }
    
    // Настройка IP форвардинга если нужно
    if (g_config.ip_forwarding) {
        ESP_LOGI(TAG, "IP forwarding enabled (both interfaces active)");
        // Включение форвардинга делается в lwip компоненте через menuconfig
    }
    
    s_network_initialized = true;
    ESP_LOGI(TAG, "Network manager initialized successfully");
    
    return ESP_OK;
}

esp_err_t network_manager_start(void)
{
    if (!s_network_initialized) {
        ESP_LOGE(TAG, "Network manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ESP_OK;
    esp_err_t wifi_ret = ESP_OK;
    esp_err_t eth_ret = ESP_OK;
    
    ESP_LOGI(TAG, "Starting network connections...");
    
    // Запуск Wi-Fi если включен
    if (g_config.wifi.enable) {
        ESP_LOGI(TAG, "Starting Wi-Fi connection to: %s", g_config.wifi.ssid);
        wifi_ret = wifi_connect();
        
        if (wifi_ret == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi connection initiated");
        } else if (wifi_ret == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Wi-Fi disabled in configuration");
        } else {
            ESP_LOGE(TAG, "Wi-Fi connection failed: %s", esp_err_to_name(wifi_ret));
        }
    }
    
    // Запуск Ethernet если включен
    if (g_config.eth.enable) {
        ESP_LOGI(TAG, "Starting Ethernet connection...");
        eth_ret = ethernet_connect();
        
        if (eth_ret == ESP_OK) {
            ESP_LOGI(TAG, "Ethernet connection initiated");
        } else if (eth_ret == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Ethernet disabled in configuration");
        } else {
            ESP_LOGE(TAG, "Ethernet connection failed: %s", esp_err_to_name(eth_ret));
        }
    }
    
    // Возвращаем ошибку только если оба не работают и должны работать
    if (g_config.wifi.enable && g_config.eth.enable) {
        if (wifi_ret != ESP_OK && eth_ret != ESP_OK) {
            ret = ESP_FAIL;
        }
    } else if (g_config.wifi.enable && wifi_ret != ESP_OK) {
        ret = ESP_FAIL;
    } else if (g_config.eth.enable && eth_ret != ESP_OK) {
        ret = ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Network connections started");
    
    return ret;
}

esp_err_t network_manager_stop(void)
{
    if (!s_network_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stopping network connections...");
    
    // Остановка Wi-Fi
    if (g_config.wifi.enable) {
        esp_err_t ret = wifi_disconnect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi disconnect returned: %s", esp_err_to_name(ret));
        }
        s_wifi_connected = false;
    }
    
    // Остановка Ethernet
    if (g_config.eth.enable) {
        esp_err_t ret = ethernet_disconnect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Ethernet disconnect returned: %s", esp_err_to_name(ret));
        }
        s_eth_connected = false;
    }
    
    ESP_LOGI(TAG, "Network connections stopped");
    
    return ESP_OK;
}

esp_netif_t *network_manager_get_active_netif(void)
{
    // Возвращаем первый подключенный интерфейс
    // В новой архитектуре оба работают одновременно, 
    // но для совместимости возвращаем Wi-Fi если он подключен
    if (s_wifi_connected) {
        return get_wifi_netif();
    } else if (s_eth_connected) {
        return get_ethernet_netif();
    }
    return NULL;
}

esp_netif_t *network_manager_get_wifi_netif(void)
{
    return get_wifi_netif();
}

esp_netif_t *network_manager_get_eth_netif(void)
{
    return get_ethernet_netif();
}

bool network_manager_wifi_is_connected(void)
{
    return s_wifi_connected;
}

bool network_manager_eth_is_connected(void)
{
    return s_eth_connected;
}

bool network_manager_is_any_connected(void)
{
    return s_wifi_connected || s_eth_connected;
}

void network_manager_set_state_callback(network_state_callback_t callback)
{
    s_state_callback = callback;
}