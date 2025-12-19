// network_manager.c 

#include "network_manager.h"
#include "config.h"
#include "wifi_connect.h"
#include "ethernet_connect.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_netif.h"

static const char *TAG = "net";
static network_state_callback_t s_state_callback = NULL;
static bool s_manager_initialized = false;
static esp_netif_t *s_active_netif = NULL;
static bool s_network_connected = false;

// Вспомогательная функция для уведомления о состоянии
static void notify_network_state(bool connected, esp_netif_t *netif)
{
    if (s_state_callback != NULL) {
        ESP_LOGI(TAG, "Calling network state callback: connected=%d, netif=%p", connected, netif);
        s_state_callback(connected, netif);
    } else {
        ESP_LOGW(TAG, "No network state callback registered (connected=%d)", connected);
    }
}

// Слушатель событий сети
static void network_event_handler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data)
{
    ESP_LOGD(TAG, "Network event: base=%s, id=%ld", event_base, (long)event_id);
    
    if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
            case IP_EVENT_ETH_GOT_IP: {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                
                s_network_connected = true;
                
                // Определяем активный интерфейс
                if (event_id == IP_EVENT_STA_GOT_IP) {
                    s_active_netif = get_wifi_netif();
                    ESP_LOGI(TAG, "Wi-Fi connected, netif: %p", s_active_netif);
                } else {
                    s_active_netif = get_ethernet_netif();
                    ESP_LOGI(TAG, "Ethernet connected, netif: %p", s_active_netif);
                }
                
                // Уведомляем callback о подключении
                notify_network_state(true, s_active_netif);
                break;
            }
            
            case IP_EVENT_STA_LOST_IP:
            case IP_EVENT_ETH_LOST_IP:
                ESP_LOGW(TAG, "Lost IP");
                s_network_connected = false;
                s_active_netif = NULL;
                notify_network_state(false, NULL);
                break;
        }
    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "Wi-Fi disconnected");
                s_network_connected = false;
                s_active_netif = NULL;
                notify_network_state(false, NULL);
                break;
        }
    } else if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Ethernet disconnected");
                s_network_connected = false;
                s_active_netif = NULL;
                notify_network_state(false, NULL);
                break;
        }
    }
}

esp_err_t network_manager_init(void)
{
    if (!g_config.init_complete) {
        ESP_LOGE(TAG, "config not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Network manager initializing");
    
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event loop failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Регистрируем обработчик событий сети
    ret = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                             &network_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to register IP_EVENT handler");
    }
    
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &network_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to register WIFI_EVENT handler");
    }
    
    ret = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                             &network_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to register ETH_EVENT handler");
    }
    
    s_manager_initialized = true;
    ESP_LOGI(TAG, "Network manager initialized");
    ESP_LOGI(TAG, "Configuration: Wi-Fi=%s, Ethernet=%s",
             g_config.wifi.enable ? "ENABLED" : "DISABLED",
             g_config.eth.enable ? "ENABLED" : "DISABLED");
    
    return ESP_OK;
}

esp_err_t network_manager_start(void)
{
    if (!s_manager_initialized) {
        ESP_LOGE(TAG, "Network manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting network connection");
    
    esp_err_t ret = ESP_OK;
    
    if (g_config.wifi.enable && g_config.eth.enable) {
        ESP_LOGI(TAG, "Both adapters enabled, using only one");
        
        if (g_config.wifi.priority >= g_config.eth.priority) {
            ESP_LOGI(TAG, "Using Wi-Fi (priority: %d)", g_config.wifi.priority);
            ret = wifi_connect();
            if (ret == ESP_OK) {
                s_active_netif = get_wifi_netif();
                ESP_LOGI(TAG, "Wi-Fi started successfully");
            }
        } else {
            ESP_LOGI(TAG, "Using Ethernet (priority: %d)", g_config.eth.priority);
            ret = ethernet_connect();
            if (ret == ESP_OK) {
                s_active_netif = get_ethernet_netif();
                ESP_LOGI(TAG, "Ethernet started successfully");
            }
        }
        
    } else if (g_config.wifi.enable) {
        ESP_LOGI(TAG, "Starting Wi-Fi connection");
        ret = wifi_connect();
        if (ret == ESP_OK) {
            s_active_netif = get_wifi_netif();
            ESP_LOGI(TAG, "Wi-Fi started successfully");
        }
        
    } else if (g_config.eth.enable) {
        ESP_LOGI(TAG, "Starting Ethernet connection");
        ret = ethernet_connect();
        if (ret == ESP_OK) {
            s_active_netif = get_ethernet_netif();
            ESP_LOGI(TAG, "Ethernet started successfully");
        }
        
    } else {
        ESP_LOGW(TAG, "No adapters enabled in config");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Connection failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Network connection started successfully");
    return ESP_OK;
}

esp_err_t network_manager_stop(void)
{
    ESP_LOGW(TAG, "Stopping network connection");
    
    s_network_connected = false;
    s_active_netif = NULL;
    
    if (g_config.wifi.enable) {
        ESP_LOGI(TAG, "Stopping Wi-Fi connection");
        wifi_disconnect();
        
    } else if (g_config.eth.enable) {
        ESP_LOGI(TAG, "Stopping Ethernet connection");
        ethernet_disconnect();
    }
    
    ESP_LOGW(TAG, "Network connection stopped");
    return ESP_OK;
}

esp_netif_t *network_manager_get_active_netif(void)
{
    return s_active_netif;
}

esp_netif_t *network_manager_get_wifi_netif(void)
{
    if (g_config.wifi.enable) {
        return get_wifi_netif();
    }
    return NULL;
}

esp_netif_t *network_manager_get_eth_netif(void)
{
    if (g_config.eth.enable) {
        return get_ethernet_netif();
    }
    return NULL;
}

bool network_manager_wifi_is_connected(void)
{
    if (g_config.wifi.enable) {
        return wifi_is_connected();
    }
    return false;
}

bool network_manager_eth_is_connected(void)
{
    if (g_config.eth.enable) {
        return ethernet_is_connected();
    }
    return false;
}

bool network_manager_is_any_connected(void)
{
    return s_network_connected;
}

void network_manager_set_state_callback(network_state_callback_t callback)
{
    s_state_callback = callback;
    ESP_LOGI(TAG, "Network state callback set: %p", callback);
}
