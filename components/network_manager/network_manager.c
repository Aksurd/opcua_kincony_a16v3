#include "network_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "wifi_connect.h"
#include "ethernet_connect.h"

static const char *TAG = "network_manager";

// Static variables for both interfaces
static esp_netif_t *eth_netif = NULL;
static esp_netif_t *wifi_netif = NULL;
static bool eth_connected = false;
static bool wifi_connected = false;
static bool system_initialized = false;

// Event handler for IP events
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Ethernet Got IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        eth_connected = true;
        // Можно добавить запуск OPC UA сервера здесь, если требуется
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi Got IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        // Можно добавить запуск OPC UA сервера здесь, если требуется
    }
}

// Initialize network system (TCP/IP stack and event loop)
// Вызывается ТОЛЬКО ОДИН РАЗ в начале работы
esp_err_t network_manager_init(void)
{
    if (system_initialized) {
        ESP_LOGW(TAG, "Network system already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing network system...");
    
    // 1. Инициализация сетевого стека
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 2. Создание цикла событий
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 3. Создание Ethernet интерфейса (если сконфигурирован)
    //    Примечание: сам драйвер Ethernet инициализируется в example_connect()
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    esp_netif_config_t eth_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&eth_cfg);
    if (eth_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create Ethernet network interface");
    } else {
        ESP_LOGI(TAG, "Ethernet network interface created");
        // Регистрируем обработчик IP событий для Ethernet
        ret = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register Ethernet IP handler: %s", esp_err_to_name(ret));
        }
    }
#endif
    
    // 4. Wi-Fi интерфейс НЕ создаем здесь - он будет создан в wifi_connect.c
    //    Но регистрируем обработчик событий для Wi-Fi
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_LOGI(TAG, "Wi-Fi interface will be created in wifi_connect()");
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register Wi-Fi IP handler: %s", esp_err_to_name(ret));
    }
#endif
    
    system_initialized = true;
    ESP_LOGI(TAG, "Network system initialized successfully");
    
    return ESP_OK;
}

// Start both network connections
esp_err_t network_manager_start(void)
{
    if (!system_initialized) {
        ESP_LOGE(TAG, "Network system not initialized. Call network_manager_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ESP_OK;
    esp_err_t eth_ret = ESP_OK;
    esp_err_t wifi_ret = ESP_OK;
    
    // Start Ethernet (if configured)
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    if (eth_netif != NULL) {
        ESP_LOGI(TAG, "Starting Ethernet connection...");
        eth_ret = example_connect();
        if (eth_ret != ESP_OK) {
            ESP_LOGE(TAG, "Ethernet connection failed: %s", esp_err_to_name(eth_ret));
        } else {
            ESP_LOGI(TAG, "Ethernet connection initiated");
        }
    } else {
        ESP_LOGW(TAG, "Ethernet interface not available");
    }
#endif
    
    // Start Wi-Fi (if configured)
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_LOGI(TAG, "Starting Wi-Fi connection...");
    wifi_ret = wifi_connect();
    if (wifi_ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connection failed: %s", esp_err_to_name(wifi_ret));
    } else {
        wifi_netif = get_wifi_netif();  // Получаем интерфейс, созданный в wifi_connect.c
        if (wifi_netif != NULL) {
            ESP_LOGI(TAG, "Wi-Fi connection initiated and interface obtained");
        } else {
            ESP_LOGW(TAG, "Wi-Fi connected but interface is NULL");
        }
    }
#endif
    
    // Возвращаем ошибку, только если оба соединения не удались
    if (eth_ret != ESP_OK && wifi_ret != ESP_OK) {
        ret = ESP_FAIL;
    }
    
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

// Deinitialize network system
esp_err_t network_manager_deinit(void)
{
    if (!system_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Deinitializing network system...");
    
    // Отменяем регистрацию обработчиков событий
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler);
#endif
    
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_event_handler);
#endif
    
    // Уничтожаем сетевые интерфейсы
    if (eth_netif != NULL) {
        esp_netif_destroy(eth_netif);
        eth_netif = NULL;
    }
    
    // Wi-Fi интерфейс уничтожается в wifi_disconnect()
    wifi_netif = NULL;
    
    // Сбрасываем флаги
    eth_connected = false;
    wifi_connected = false;
    system_initialized = false;
    
    ESP_LOGI(TAG, "Network system deinitialized");
    return ESP_OK;
}