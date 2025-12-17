#include "network_manager.h"
#include "config.h"
#include "wifi_connect.h"
#include "ethernet_connect.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"

static const char *TAG = "net";

static bool s_network_initialized = false;
static network_state_callback_t s_state_callback = NULL;
static bool s_wifi_connected = false;
static bool s_eth_connected = false;
static EventGroupHandle_t s_wifi_event_group = NULL;
static EventGroupHandle_t s_eth_event_group = NULL;

#define WIFI_CONNECTED_BIT BIT(0)
#define WIFI_FAIL_BIT      BIT(1)
#define ETHERNET_CONNECTED_BIT BIT(0)
#define ETHERNET_FAIL_BIT      BIT(1)

static void apply_ip_configuration(esp_netif_t *netif, bool is_ethernet) 
{
    if (netif == NULL) {
        ESP_LOGE(TAG, "netif NULL");
        return;
    }
    
    esp_err_t ret;
    
    if (is_ethernet) {
        if (g_config.eth.ip_config.mode == NET_STATIC) {
            ret = ethernet_apply_ip_config();
            if (ret == ESP_OK) {
                xEventGroupSetBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
            }
        } else {
            ret = esp_netif_dhcpc_start(netif);
        }
    } else {
        if (g_config.wifi.ip_config.mode == NET_STATIC) {
            ret = wifi_apply_ip_config();
            if (ret == ESP_OK) {
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            }
        } else {
            ret = esp_netif_dhcpc_start(netif);
        }
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGW(TAG, "Wi-Fi starting");
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGW(TAG, "Wi-Fi connected");
                s_wifi_connected = true;
                if (s_state_callback != NULL) {
                    s_state_callback(true, get_wifi_netif());
                }
                apply_ip_configuration(get_wifi_netif(), false);
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "Wi-Fi disconnected");
                s_wifi_connected = false;
                if (s_state_callback != NULL) {
                    s_state_callback(false, get_wifi_netif());
                }
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                if (g_config.wifi.ip_config.mode == NET_DHCP) {
                    ESP_LOGW(TAG, "Wi-Fi got DHCP IP");
                    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                }
                break;
        }
    }
}

static void eth_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGW(TAG, "ETH link up");
                s_eth_connected = true;
                if (s_state_callback != NULL) {
                    s_state_callback(true, get_ethernet_netif());
                }
                apply_ip_configuration(get_ethernet_netif(), true);
                break;
                
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "ETH link down");
                s_eth_connected = false;
                if (s_state_callback != NULL) {
                    s_state_callback(false, get_ethernet_netif());
                }
                xEventGroupSetBits(s_eth_event_group, ETHERNET_FAIL_BIT);
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_ETH_GOT_IP:
                if (g_config.eth.ip_config.mode == NET_DHCP) {
                    ESP_LOGW(TAG, "ETH got DHCP IP");
                    xEventGroupSetBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
                }
                break;
        }
    }
}

esp_err_t network_manager_init(void)
{
    if (s_network_initialized) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_config.init_complete) {
        ESP_LOGE(TAG, "config not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "initializing");
    
    // СОЗДАЕМ event groups только если адаптер включен
    if (g_config.wifi.enable) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL) {
            ESP_LOGE(TAG, "failed to create Wi-Fi event group");
            return ESP_FAIL;
        }
    }
    
    if (g_config.eth.enable) {
        s_eth_event_group = xEventGroupCreate();
        if (s_eth_event_group == NULL) {
            ESP_LOGE(TAG, "failed to create Ethernet event group");
            // Удаляем Wi-Fi event group если он был создан
            if (s_wifi_event_group != NULL) {
                vEventGroupDelete(s_wifi_event_group);
                s_wifi_event_group = NULL;
            }
            return ESP_FAIL;
        }
    }
    
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event loop failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // РЕГИСТРИРУЕМ ОБРАБОТЧИКИ ТОЛЬКО ДЛЯ ВКЛЮЧЕННЫХ АДАПТЕРОВ
    
    if (g_config.wifi.enable) {
        ESP_LOGI(TAG, "Registering Wi-Fi event handlers");
        
        const struct {
            esp_event_base_t event_base;
            int32_t event_id;
        } wifi_handlers[] = {
            {WIFI_EVENT, WIFI_EVENT_STA_START},
            {WIFI_EVENT, WIFI_EVENT_STA_CONNECTED},
            {WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED},
            {IP_EVENT, IP_EVENT_STA_GOT_IP}
        };
        
        for (int i = 0; i < sizeof(wifi_handlers)/sizeof(wifi_handlers[0]); i++) {
            ret = esp_event_handler_instance_register(wifi_handlers[i].event_base,
                                                     wifi_handlers[i].event_id,
                                                     &wifi_event_handler, NULL, NULL);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to register Wi-Fi handler %d: %s", 
                         i, esp_err_to_name(ret));
            }
        }
        ESP_LOGI(TAG, "Wi-Fi handlers registered");
    } else {
        ESP_LOGW(TAG, "Wi-Fi disabled in config, skipping handlers");
    }
    
    // ВАЖНО: Если Ethernet отключен в конфиге - НЕ регистрируем его обработчики!
    if (g_config.eth.enable) {
        ESP_LOGI(TAG, "Registering Ethernet event handlers");
        
        const struct {
            esp_event_base_t event_base;
            int32_t event_id;
        } eth_handlers[] = {
            {ETH_EVENT, ETHERNET_EVENT_CONNECTED},
            {ETH_EVENT, ETHERNET_EVENT_DISCONNECTED},
            {IP_EVENT, IP_EVENT_ETH_GOT_IP}
        };
        
        for (int i = 0; i < sizeof(eth_handlers)/sizeof(eth_handlers[0]); i++) {
            ret = esp_event_handler_instance_register(eth_handlers[i].event_base,
                                                     eth_handlers[i].event_id,
                                                     &eth_event_handler, NULL, NULL);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to register Ethernet handler %d: %s", 
                         i, esp_err_to_name(ret));
            }
        }
        ESP_LOGI(TAG, "Ethernet handlers registered");
    } else {
        ESP_LOGW(TAG, "Ethernet disabled in config, skipping handlers");
    }
    
    s_network_initialized = true;
    ESP_LOGW(TAG, "initialized");
    
    // Логируем конфигурацию для отладки
    ESP_LOGI(TAG, "Network configuration: Wi-Fi=%s, Ethernet=%s",
             g_config.wifi.enable ? "ENABLED" : "DISABLED",
             g_config.eth.enable ? "ENABLED" : "DISABLED");
    
    return ESP_OK;

cleanup:
    // Очищаем event groups при ошибке
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    if (s_eth_event_group != NULL) {
        vEventGroupDelete(s_eth_event_group);
        s_eth_event_group = NULL;
    }
    return ret;
}

esp_err_t network_manager_start(void)
{
    if (!s_network_initialized) {
        ESP_LOGE(TAG, "not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "starting connections");
    
    esp_err_t wifi_ret = ESP_OK;
    esp_err_t eth_ret = ESP_OK;
    
    // ЗАПУСКАЕМ ТОЛЬКО ВКЛЮЧЕННЫЕ АДАПТЕРЫ
    if (g_config.wifi.enable) {
        ESP_LOGI(TAG, "Starting Wi-Fi connection");
        wifi_ret = wifi_connect();
        if (wifi_ret != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi failed: %s", esp_err_to_name(wifi_ret));
        }
    }
    
    if (g_config.eth.enable) {
        ESP_LOGI(TAG, "Starting Ethernet connection");
        eth_ret = ethernet_connect();
        if (eth_ret != ESP_OK) {
            ESP_LOGE(TAG, "ETH failed: %s", esp_err_to_name(eth_ret));
        }
    }
    
    // ПРОВЕРЯЕМ РЕЗУЛЬТАТЫ ТОЛЬКО ДЛЯ ВКЛЮЧЕННЫХ АДАПТЕРОВ
    if (g_config.wifi.enable && g_config.eth.enable) {
        // Оба включены (хотя в вашем случае такого не должно быть)
        if (wifi_ret != ESP_OK && eth_ret != ESP_OK) {
            ESP_LOGE(TAG, "both connections failed");
            return ESP_FAIL;
        }
    } else if (g_config.wifi.enable) {
        // Только Wi-Fi включен
        if (wifi_ret != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi failed");
            return ESP_FAIL;
        }
    } else if (g_config.eth.enable) {
        // Только Ethernet включен
        if (eth_ret != ESP_OK) {
            ESP_LOGE(TAG, "ETH failed");
            return ESP_FAIL;
        }
    } else {
        // Ни один адаптер не включен
        ESP_LOGW(TAG, "No adapters enabled in config");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    ESP_LOGW(TAG, "connections started");
    return ESP_OK;
}

esp_err_t network_manager_stop(void)
{
    if (!s_network_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "stopping connections");
    
    // ОСТАНАВЛИВАЕМ ТОЛЬКО ВКЛЮЧЕННЫЕ АДАПТЕРЫ
    if (g_config.wifi.enable) {
        wifi_disconnect();
        s_wifi_connected = false;
    }
    
    if (g_config.eth.enable) {
        ethernet_disconnect();
        s_eth_connected = false;
    }
    
    // УДАЛЯЕМ ТОЛЬКО СОЗДАННЫЕ EVENT GROUPS
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    if (s_eth_event_group != NULL) {
        vEventGroupDelete(s_eth_event_group);
        s_eth_event_group = NULL;
    }
    
    s_network_initialized = false;
    ESP_LOGW(TAG, "connections stopped");
    
    return ESP_OK;
}

esp_netif_t *network_manager_get_active_netif(void)
{
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