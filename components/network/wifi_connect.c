#include <string.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip_addr.h"
#include "wifi_connect.h"
#include "config.h"

#define WIFI_CONNECTED_BIT BIT(0)
#define WIFI_FAIL_BIT      BIT(1)

static const char *TAG = "wifi_connect";
static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_wifi_netif = NULL;
static int s_retry_num = 0;
static esp_event_handler_instance_t instance_any_id = NULL;
static esp_event_handler_instance_t instance_got_ip = NULL;
static bool s_wifi_initialized = false;

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi STA started");
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP: %s", g_config.wifi.ssid);
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
                ESP_LOGW(TAG, "Disconnected from AP, reason: %d", event->reason);
                
                if (s_retry_num < g_config.wifi.max_retry) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "Retry to connect (%d/%d)", s_retry_num, g_config.wifi.max_retry);
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
                break;
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        
        // Применяем IP конфигурацию если нужно
        wifi_apply_ip_config();
        
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_apply_ip_config(void)
{
    if (s_wifi_netif == NULL) {
        ESP_LOGE(TAG, "WiFi netif not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Если установлен статический IP - применяем его
    if (g_config.wifi.ip_config.mode == NET_STATIC) {
        ESP_LOGI(TAG, "Applying static IP configuration for WiFi");
        
        // Останавливаем DHCP клиент
        esp_err_t ret = esp_netif_dhcpc_stop(s_wifi_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGW(TAG, "Failed to stop DHCP client: %s", esp_err_to_name(ret));
        }
        
        // Устанавливаем статический IP
        ret = esp_netif_set_ip_info(s_wifi_netif, &g_config.wifi.ip_config.ip_info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "WiFi static IP set: " IPSTR, 
                 IP2STR(&g_config.wifi.ip_config.ip_info.ip));
        
        // Устанавливаем DNS серверы если указаны
        if (g_config.wifi.ip_config.dns_primary != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4.addr = g_config.wifi.ip_config.dns_primary;
            dns.ip.type = IPADDR_TYPE_V4;
            ret = esp_netif_set_dns_info(s_wifi_netif, ESP_NETIF_DNS_MAIN, &dns);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "WiFi primary DNS set");
            } else {
                ESP_LOGW(TAG, "Failed to set primary DNS: %s", esp_err_to_name(ret));
            }
        }
        
        if (g_config.wifi.ip_config.dns_secondary != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4.addr = g_config.wifi.ip_config.dns_secondary;
            dns.ip.type = IPADDR_TYPE_V4;
            ret = esp_netif_set_dns_info(s_wifi_netif, ESP_NETIF_DNS_BACKUP, &dns);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "WiFi secondary DNS set");
            } else {
                ESP_LOGW(TAG, "Failed to set secondary DNS: %s", esp_err_to_name(ret));
            }
        }
    } else {
        ESP_LOGI(TAG, "Using DHCP for WiFi");
        // Запускаем DHCP клиент если не запущен
        esp_err_t ret = esp_netif_dhcpc_start(s_wifi_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ESP_LOGW(TAG, "Failed to start DHCP client: %s", esp_err_to_name(ret));
        }
    }
    
    // Устанавливаем hostname если указан
    if (strlen(g_config.wifi.ip_config.hostname) > 0) {
        esp_netif_set_hostname(s_wifi_netif, g_config.wifi.ip_config.hostname);
        ESP_LOGI(TAG, "WiFi hostname set: %s", g_config.wifi.ip_config.hostname);
    }
    
    return ESP_OK;
}

esp_err_t wifi_connect(void)
{
    if (!g_config.init_complete) {
        ESP_LOGE(TAG, "System configuration not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_config.wifi.enable) {
        ESP_LOGI(TAG, "WiFi disabled in configuration");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (s_wifi_initialized) {
        ESP_LOGW(TAG, "WiFi already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi, SSID: %s", g_config.wifi.ssid);
    
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    // Инициализация сетевого интерфейса
    s_wifi_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi network interface");
        vEventGroupDelete(s_wifi_event_group);
        return ESP_FAIL;
    }
    
    // Регистрация обработчиков событий
    esp_err_t ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, NULL, &instance_any_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler");
        goto cleanup;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &event_handler, NULL, &instance_got_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP_EVENT handler");
        goto cleanup;
    }
    
    // Инициализация WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Конфигурация WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
        },
    };
    
    strncpy((char*)wifi_config.sta.ssid, g_config.wifi.ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, g_config.wifi.password, sizeof(wifi_config.sta.password));
    
    // Примечание: bssid удален, так как его нет в структуре app_wifi_config_t
    // Если нужна поддержка bssid, добавьте поле в config.h
    
    wifi_config.sta.threshold.authmode = g_config.wifi.authmode; // Исправлено: authmode вместо auth_mode
    
    // Настройка PMF (Protected Management Frames)
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    // Установка канала, если указан
    if (g_config.wifi.channel > 0) {
        wifi_config.sta.channel = g_config.wifi.channel;
        wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    } else {
        wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    }
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Запуск WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    s_wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi initialization complete");
    
    // Ожидание подключения
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(g_config.wifi.scan_timeout_ms > 0 ? 
                         g_config.wifi.scan_timeout_ms : 30000)); // Исправлено: scan_timeout_ms вместо connect_timeout_ms
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGW(TAG, "WiFi connection failed");
        goto cleanup;
    }
    
    ESP_LOGE(TAG, "Unexpected event");
    
cleanup:
    if (instance_any_id != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
        instance_any_id = NULL;
    }
    
    if (instance_got_ip != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
        instance_got_ip = NULL;
    }
    
    if (s_wifi_netif != NULL) {
        esp_netif_destroy(s_wifi_netif);
        s_wifi_netif = NULL;
    }
    
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    
    if (s_wifi_initialized) {
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    
    return ESP_FAIL;
}

esp_err_t wifi_disconnect(void)
{
    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Отмена регистрации обработчиков
    if (instance_any_id != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
        instance_any_id = NULL;
    }
    
    if (instance_got_ip != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
        instance_got_ip = NULL;
    }
    
    // Остановка WiFi
    esp_wifi_stop();
    esp_wifi_deinit();
    
    // Удаление сетевого интерфейса
    if (s_wifi_netif != NULL) {
        esp_netif_destroy(s_wifi_netif);
        s_wifi_netif = NULL;
    }
    
    // Удаление группы событий
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    
    s_wifi_initialized = false;
    ESP_LOGI(TAG, "WiFi disconnected and deinitialized");
    
    return ESP_OK;
}

esp_netif_t *get_wifi_netif(void)
{
    return s_wifi_netif;
}

bool wifi_is_connected(void)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }
    
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}