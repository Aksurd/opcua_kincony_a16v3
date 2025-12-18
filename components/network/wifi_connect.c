// wifi_connect.c

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

static const char *TAG = "wifi";
static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_wifi_netif = NULL;
static int s_retry_num = 0;
static esp_event_handler_instance_t instance_any_id = NULL;
static esp_event_handler_instance_t instance_got_ip = NULL;
static bool s_wifi_initialized = false;
static bool s_ip_config_applied = false;
static bool s_connection_in_progress = false;

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi STA started, attempting to connect...");
                esp_wifi_connect();
                s_connection_in_progress = true;
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP: %s", g_config.wifi.ssid);
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
                ESP_LOGW(TAG, "Disconnected from AP, reason: %d", event->reason);
                
                s_ip_config_applied = false;
                s_connection_in_progress = false;
                
                if (s_retry_num < g_config.wifi.max_retry) {
                    ESP_LOGI(TAG, "Retrying connection (%d/%d)...", 
                            s_retry_num + 1, g_config.wifi.max_retry);
                    esp_wifi_connect();
                    s_retry_num++;
                    s_connection_in_progress = true;
                } else {
                    ESP_LOGE(TAG, "Max retries reached, connection failed");
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
                break;
            }
                
            case WIFI_EVENT_STA_AUTHMODE_CHANGE:
                ESP_LOGI(TAG, "Auth mode changed");
                break;
                
            default:
                ESP_LOGD(TAG, "Unhandled Wi-Fi event: %ld", (long)event_id);
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                
                ESP_LOGI(TAG, "Got IP via DHCP:" IPSTR, IP2STR(&event->ip_info.ip));
                ESP_LOGI(TAG, "Netmask:" IPSTR ", Gateway:" IPSTR,
                        IP2STR(&event->ip_info.netmask),
                        IP2STR(&event->ip_info.gw));
                
                if (g_config.wifi.ip_config.mode == NET_STATIC) {
                    ESP_LOGI(TAG, "Static IP configured, applying custom configuration...");
                    esp_err_t ret = wifi_apply_ip_config();
                    if (ret == ESP_OK) {
                        s_retry_num = 0;
                        s_connection_in_progress = false;
                        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                        ESP_LOGI(TAG, "Static IP applied successfully");
                    } else {
                        ESP_LOGE(TAG, "Static IP configuration failed: %s", esp_err_to_name(ret));
                    }
                } else {
                    ESP_LOGI(TAG, "Using DHCP-assigned IP");
                    s_retry_num = 0;
                    s_connection_in_progress = false;
                    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                }
                break;
            }
                
            case IP_EVENT_STA_LOST_IP:
                ESP_LOGW(TAG, "Lost IP address");
                s_ip_config_applied = false;
                break;
                
            default:
                ESP_LOGD(TAG, "Unhandled IP event: %ld", (long)event_id);
                break;
        }
    }
}

esp_err_t wifi_apply_ip_config(void)
{
    if (s_wifi_netif == NULL) {
        ESP_LOGE(TAG, "Network interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Applying IP configuration, mode: %s", 
             g_config.wifi.ip_config.mode == NET_STATIC ? "STATIC" : "DHCP");
    
    if (g_config.wifi.ip_config.mode == NET_STATIC) {
        ESP_LOGI(TAG, "Setting static IP configuration...");
        
        // Stop DHCP client first
        esp_err_t ret = esp_netif_dhcpc_stop(s_wifi_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGW(TAG, "Failed to stop DHCP client: %s", esp_err_to_name(ret));
            // Continue anyway
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Apply static IP configuration
        ESP_LOGI(TAG, "Setting IP: " IPSTR, IP2STR(&g_config.wifi.ip_config.ip_info.ip));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&g_config.wifi.ip_config.ip_info.netmask));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&g_config.wifi.ip_config.ip_info.gw));
        
        ret = esp_netif_set_ip_info(s_wifi_netif, &g_config.wifi.ip_config.ip_info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Apply DNS servers if configured
        if (g_config.wifi.ip_config.dns_primary != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4.addr = g_config.wifi.ip_config.dns_primary;
            dns.ip.type = IPADDR_TYPE_V4;
            ret = esp_netif_set_dns_info(s_wifi_netif, ESP_NETIF_DNS_MAIN, &dns);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to set primary DNS: %s", esp_err_to_name(ret));
            } else {
                char dns_str[16];
                config_int_to_ip(g_config.wifi.ip_config.dns_primary, dns_str, sizeof(dns_str));
                ESP_LOGI(TAG, "Primary DNS set: %s", dns_str);
            }
        }
        
        if (g_config.wifi.ip_config.dns_secondary != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4.addr = g_config.wifi.ip_config.dns_secondary;
            dns.ip.type = IPADDR_TYPE_V4;
            ret = esp_netif_set_dns_info(s_wifi_netif, ESP_NETIF_DNS_BACKUP, &dns);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to set secondary DNS: %s", esp_err_to_name(ret));
            } else {
                char dns_str[16];
                config_int_to_ip(g_config.wifi.ip_config.dns_secondary, dns_str, sizeof(dns_str));
                ESP_LOGI(TAG, "Secondary DNS set: %s", dns_str);
            }
        }
    } else {
        ESP_LOGI(TAG, "Starting DHCP client...");
        esp_err_t ret = esp_netif_dhcpc_start(s_wifi_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ESP_LOGW(TAG, "Failed to start DHCP client: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "DHCP client started");
        }
    }
    
    // Set hostname if configured
    if (strlen(g_config.wifi.ip_config.hostname) > 0) {
        esp_netif_set_hostname(s_wifi_netif, g_config.wifi.ip_config.hostname);
        ESP_LOGI(TAG, "Hostname set: %s", g_config.wifi.ip_config.hostname);
    }
    
    s_ip_config_applied = true;
    ESP_LOGI(TAG, "IP configuration applied successfully");
    return ESP_OK;
}

esp_err_t wifi_connect(void)
{
    if (!g_config.init_complete) {
        ESP_LOGE(TAG, "System configuration not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_config.wifi.enable) {
        ESP_LOGW(TAG, "Wi-Fi is disabled in configuration");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (s_wifi_initialized) {
        ESP_LOGW(TAG, "Wi-Fi already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_connection_in_progress) {
        ESP_LOGW(TAG, "Connection already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing Wi-Fi with SSID: %s", g_config.wifi.ssid);
    
    // Create event group for Wi-Fi connection status
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    // Create default Wi-Fi STA interface
    s_wifi_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create network interface");
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Network interface created");
    
    // Register event handlers
    esp_err_t ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, NULL, &instance_any_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &event_handler, NULL, &instance_got_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Initialize Wi-Fi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Configure Wi-Fi connection parameters
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .threshold.authmode = g_config.wifi.authmode,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .sae_h2e_identifier = "",
        },
    };
    
    // Copy SSID and password (safe with strncpy)
    strncpy((char*)wifi_config.sta.ssid, g_config.wifi.ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    
    strncpy((char*)wifi_config.sta.password, g_config.wifi.password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
    
    // Configure PMF (Protected Management Frames)
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    // Configure scanning
    if (g_config.wifi.channel > 0) {
        wifi_config.sta.channel = g_config.wifi.channel;
        wifi_config.sta.scan_method = WIFI_FAST_SCAN;
        ESP_LOGI(TAG, "Using fixed channel: %d", g_config.wifi.channel);
    } else {
        wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        ESP_LOGI(TAG, "Using automatic channel selection");
    }
    
    // Set Wi-Fi mode to STA
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Apply Wi-Fi configuration
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi configuration: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Start Wi-Fi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    s_wifi_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi initialization complete, connecting...");
    
    // Apply pre-connection IP configuration if static
    if (g_config.wifi.ip_config.mode == NET_STATIC) {
        ESP_LOGI(TAG, "Pre-configuring static IP before connection");
        wifi_apply_ip_config();
    }
    
    // Wait for connection with timeout
    uint32_t timeout_ms = g_config.wifi.scan_timeout_ms > 0 ? 
                         g_config.wifi.scan_timeout_ms : 30000;
    
    ESP_LOGI(TAG, "Waiting for connection (timeout: %d ms)...", timeout_ms);
    
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(timeout_ms));
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connected successfully!");
        ESP_LOGI(TAG, "SSID: %s", g_config.wifi.ssid);
        
        // Get and log final IP configuration
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_wifi_netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "Final IP: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
            ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info.gw));
        }
        
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Wi-Fi connection failed after %d retries", s_retry_num);
    } else {
        ESP_LOGE(TAG, "Wi-Fi connection timeout (%d ms)", timeout_ms);
    }
    
    // Connection failed, cleanup
    ESP_LOGI(TAG, "Performing cleanup after failed connection");
    
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
    
    s_connection_in_progress = false;
    s_ip_config_applied = false;
    s_retry_num = 0;
    
    return ESP_FAIL;
}

esp_err_t wifi_disconnect(void)
{
    if (!s_wifi_initialized) {
        ESP_LOGW(TAG, "Wi-Fi not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Disconnecting Wi-Fi...");
    
    // Unregister event handlers
    if (instance_any_id != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
        instance_any_id = NULL;
    }
    
    if (instance_got_ip != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
        instance_got_ip = NULL;
    }
    
    // Stop Wi-Fi
    if (s_wifi_initialized) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    
    // Clean up network interface
    if (s_wifi_netif != NULL) {
        esp_netif_destroy(s_wifi_netif);
        s_wifi_netif = NULL;
    }
    
    // Clean up event group
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    
    s_ip_config_applied = false;
    s_connection_in_progress = false;
    s_retry_num = 0;
    
    ESP_LOGI(TAG, "Wi-Fi disconnected and cleaned up");
    
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

bool wifi_is_connecting(void)
{
    return s_connection_in_progress;
}

bool wifi_is_initialized(void)
{
    return s_wifi_initialized;
}
