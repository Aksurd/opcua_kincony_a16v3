#include "network_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Include headers for specific functionality
#include "esp_wifi.h"
#include "esp_eth.h"

// CORRECTED: Include the actual wifi_connect.h header
#include "wifi_connect.h"
#include "ethernet_connect.h"

static const char *TAG = "network_manager";

// Static variables for both interfaces
static esp_netif_t *eth_netif = NULL;
static esp_netif_t *wifi_netif = NULL;
static bool eth_connected = false;
static bool wifi_connected = false;
// Note: 'eth_handle' was declared in error but is managed by 'ethernet_connect.c'

// Event handler for IP events
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Ethernet Got IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        eth_connected = true;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi Got IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
    }
}

// Initialize both network interfaces
esp_err_t network_manager_init(void)
{
    esp_err_t ret = ESP_OK;
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create Ethernet network interface (if configured)
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    esp_netif_config_t eth_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&eth_cfg);
    
    // Register Ethernet event handlers.
    // In ESP-IDF v5.x, we register for the general base and filter later,
    // or rely on the example_connect() to set up its own handlers.
    // The old 'ETH_EVENT' constant is not available here.
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));
#endif
    
    // Create Wi-Fi station network interface (if configured)
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    wifi_netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_event_handler, NULL));
#endif
    
    return ret;
}

// Start both connections
esp_err_t network_manager_start(void)
{
    esp_err_t ret = ESP_OK;
    
    // Start Ethernet (if configured)
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_LOGI(TAG, "Starting Ethernet connection...");
    // This function is from the ethernet component
    ret = example_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet connection failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Ethernet connection initiated");
    }
#endif
    
    // Start Wi-Fi (if configured)
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_LOGI(TAG, "Starting Wi-Fi connection...");
    // This function is from the wifi component. Ensure the header is included.
    // We declared it in network_manager.h, but the definition is in wifi_connect.c
    ret = wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connection failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Wi-Fi connection initiated");
    }
#endif
    
    return ret;
}

// Get network interfaces
esp_netif_t *network_manager_get_eth_netif(void)
{
    return eth_netif;
}

esp_netif_t *network_manager_get_wifi_netif(void)
{
    return wifi_netif;
}

// Get connection status
bool network_manager_eth_is_connected(void)
{
    return eth_connected;
}

bool network_manager_wifi_is_connected(void)
{
    return wifi_connected;
}