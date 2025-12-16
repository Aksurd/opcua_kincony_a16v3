/**
 * @file ethernet_connect.c
 * @brief Ethernet connection manager for ESP32 with W5500
 */

#include <string.h>
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip_addr.h"
#include "esp_netif.h"
#include "ethernet_connect.h"
#include "config.h"

/* Event group bits */
#define ETHERNET_CONNECTED_BIT BIT(0)
#define ETHERNET_FAIL_BIT      BIT(1)

/* Ethernet connection timeout */
#define ETHERNET_CONNECT_TIMEOUT_MS 30000

static const char *TAG = "ethernet_connect";

/* Static variables */
static EventGroupHandle_t s_eth_event_group = NULL;
static esp_netif_t *s_eth_netif = NULL;
static esp_eth_handle_t s_eth_handle = NULL;
static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static spi_device_handle_t s_spi_handle = NULL;
static esp_event_handler_instance_t instance_any_id = NULL;
static esp_event_handler_instance_t instance_got_ip = NULL;
static bool s_eth_initialized = false;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;

/* Internal helper functions */
static void ethernet_unregister_event_handlers(void);

/**
 * @brief Ethernet event handler
 */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet link up");
                break;
                
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Ethernet link down");
                xEventGroupSetBits(s_eth_event_group, ETHERNET_FAIL_BIT);
                break;
                
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet started");
                break;
                
            case ETHERNET_EVENT_STOP:
                ESP_LOGI(TAG, "Ethernet stopped");
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        // ВАЖНО: Проверяем режим (DHCP или Static)
        if (g_config.eth.ip_config.mode == NET_DHCP) {
            // Только для DHCP обрабатываем событие получения IP
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "Ethernet Got IP via DHCP:" IPSTR, IP2STR(&event->ip_info.ip));
            
            // Применяем IP конфигурацию если нужно
            ethernet_apply_ip_config();
            
            xEventGroupSetBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
        } else {
            // Для статики - IP уже установлен, игнорируем DHCP событие
            ESP_LOGI(TAG, "Ethernet static IP already configured, ignoring DHCP event");
            // Для статики флаг CONNECTED_BIT уже установлен в apply_ip_configuration()
        }
    }
}

/**
 * @brief Apply IP configuration to Ethernet interface
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_apply_ip_config(void)
{
    if (s_eth_netif == NULL) {
        ESP_LOGE(TAG, "Ethernet netif not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Если установлен статический IP - применяем его
    if (g_config.eth.ip_config.mode == NET_STATIC) {
        ESP_LOGI(TAG, "Applying static IP configuration for Ethernet");
        
        // Останавливаем DHCP клиент
        esp_err_t ret = esp_netif_dhcpc_stop(s_eth_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGW(TAG, "Failed to stop DHCP client: %s", esp_err_to_name(ret));
        }
        
        // Устанавливаем статический IP
        ret = esp_netif_set_ip_info(s_eth_netif, &g_config.eth.ip_config.ip_info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "Ethernet static IP set: " IPSTR, 
                 IP2STR(&g_config.eth.ip_config.ip_info.ip));
        
        // Устанавливаем DNS серверы если указаны
        if (g_config.eth.ip_config.dns_primary != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4.addr = g_config.eth.ip_config.dns_primary;
            dns.ip.type = IPADDR_TYPE_V4;
            ret = esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_MAIN, &dns);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Ethernet primary DNS set");
            } else {
                ESP_LOGW(TAG, "Failed to set primary DNS: %s", esp_err_to_name(ret));
            }
        }
        
        if (g_config.eth.ip_config.dns_secondary != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4.addr = g_config.eth.ip_config.dns_secondary;
            dns.ip.type = IPADDR_TYPE_V4;
            ret = esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_BACKUP, &dns);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Ethernet secondary DNS set");
            } else {
                ESP_LOGW(TAG, "Failed to set secondary DNS: %s", esp_err_to_name(ret));
            }
        }
        
        // Устанавливаем hostname если указан
        if (strlen(g_config.eth.ip_config.hostname) > 0) {
            esp_netif_set_hostname(s_eth_netif, g_config.eth.ip_config.hostname);
            ESP_LOGI(TAG, "Ethernet hostname set: %s", g_config.eth.ip_config.hostname);
        }
        
        // Для статики устанавливаем флаг подключения СРАЗУ
        if (s_eth_event_group != NULL) {
            xEventGroupSetBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
        }
        
    } else {
        ESP_LOGI(TAG, "Using DHCP for Ethernet");
        // Запускаем DHCP клиент если не запущен
        esp_err_t ret = esp_netif_dhcpc_start(s_eth_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ESP_LOGW(TAG, "Failed to start DHCP client: %s", esp_err_to_name(ret));
        }
        
        // Устанавливаем hostname если указан
        if (strlen(g_config.eth.ip_config.hostname) > 0) {
            esp_netif_set_hostname(s_eth_netif, g_config.eth.ip_config.hostname);
            ESP_LOGI(TAG, "Ethernet hostname set: %s", g_config.eth.ip_config.hostname);
        }
    }
    
    return ESP_OK;
}

/**
 * @brief Initialize and connect Ethernet
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_connect(void)
{
    esp_err_t ret = ESP_OK;
    
    if (!g_config.init_complete) {
        ESP_LOGE(TAG, "System configuration not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_config.eth.enable) {
        ESP_LOGI(TAG, "Ethernet disabled in configuration");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (s_eth_initialized) {
        ESP_LOGW(TAG, "Ethernet already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing Ethernet W5500");
    ESP_LOGI(TAG, "SPI pins: MOSI=%d, MISO=%d, SCLK=%d, CS=%d, RESET=%d, INT=%d",
             g_config.eth.mosi_pin, g_config.eth.miso_pin,
             g_config.eth.sclk_pin, g_config.eth.cs_pin,
             g_config.eth.reset_pin, g_config.eth.interrupt_pin);
    
    /* ========== КРИТИЧЕСКИЕ ИСПРАВЛЕНИЯ ========== */
    
    /* Шаг 0: Инициализация сервиса прерываний GPIO (ВАЖНО!) */
    ret = gpio_install_isr_service(0); // 0 = флаги по умолчанию
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE означает, что сервис уже установлен - это нормально
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "GPIO ISR service initialized");
    
    /* Шаг 1: Аппаратный сброс W5500 (требуется для стабильной работы) */
    if (g_config.eth.reset_pin >= 0) {
        ESP_LOGI(TAG, "Performing W5500 hardware reset on GPIO %d", g_config.eth.reset_pin);
        
        // Настраиваем пин сброса как выход
        ret = gpio_set_direction(g_config.eth.reset_pin, GPIO_MODE_OUTPUT);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set reset pin direction: %s", esp_err_to_name(ret));
        }
        
        // Активный низкий уровень для сброса (удерживаем 10 мс)
        gpio_set_level(g_config.eth.reset_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Отпускаем сброс (переводим в высокий уровень)
        gpio_set_level(g_config.eth.reset_pin, 1);
        
        // Даем время W5500 на запуск (минимум 500 мс)
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(TAG, "W5500 hardware reset complete");
    }
    
    /* ========== КОНЕЦ ИСПРАВЛЕНИЙ ========== */
    
    /* Step 1: Create event group */
    s_eth_event_group = xEventGroupCreate();
    if (s_eth_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    /* Step 2: Create network interface */
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&cfg);
    if (s_eth_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create Ethernet network interface");
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    /* Step 3: Register event handlers */
    ret = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, 
                                             &event_handler, NULL, &instance_any_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ETH_EVENT handler");
        goto cleanup;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, 
                                             &event_handler, NULL, &instance_got_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP_EVENT handler");
        goto cleanup;
    }
    
    /* Step 4: Initialize SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num = g_config.eth.mosi_pin,
        .miso_io_num = g_config.eth.miso_pin,
        .sclk_io_num = g_config.eth.sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };
    
    ret = spi_bus_initialize(g_config.eth.host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialize failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    /* Step 5: Add SPI device (W5500) */
    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0,
        .clock_speed_hz = g_config.eth.clock_speed_hz,
        .spics_io_num = g_config.eth.cs_pin,
        .queue_size = 20,
        .flags = 0
    };
    
    ret = spi_bus_add_device(g_config.eth.host, &devcfg, &s_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        goto cleanup_spi;
    }
    
/* Step 6: Конфигурация W5500 - исправленная версия для ESP-IDF v5.5.1 */
eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(g_config.eth.host, &devcfg);
w5500_config.int_gpio_num = g_config.eth.interrupt_pin;

// УДАЛИТЬ ВСЁ ЭТО:
// uint8_t eth_mac[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56}; // Локально администрируемый MAC
// w5500_config.mac_addr = eth_mac;
// ESP_LOGI(TAG, "W5500 MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
//          eth_mac[0], eth_mac[1], eth_mac[2], 
//          eth_mac[3], eth_mac[4], eth_mac[5]);
    
    // Если используем режим опроса (interrupt_pin = -1)
    if (g_config.eth.interrupt_pin < 0) {
        w5500_config.poll_period_ms = 50; // период опроса в мс
    }
    
    /* Step 7: Configure MAC */
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.sw_reset_timeout_ms = 2000;
    mac_config.rx_task_stack_size = 4096;  // Увеличить стек
    mac_config.rx_task_prio = 20;          // Приоритет задачи
    
    /* Step 8: Configure PHY */
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = g_config.eth.reset_pin;
    phy_config.autonego_timeout_ms = 3000;
    phy_config.phy_addr = 0;  // W5500 PHY address fixed at 0
    
    /* Step 9: Create MAC and PHY instances */
    s_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    if (s_mac == NULL) {
        ESP_LOGE(TAG, "Failed to create W5500 MAC");
        ret = ESP_FAIL;
        goto cleanup_spi_device;
    }
    
    s_phy = esp_eth_phy_new_w5500(&phy_config);
    if (s_phy == NULL) {
        ESP_LOGE(TAG, "Failed to create W5500 PHY");
        ret = ESP_FAIL;
        goto cleanup_mac;
    }
    
    /* Step 10: Create Ethernet driver */
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    
    ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(ret));
        goto cleanup_phy;
    }
    
    /* Step 11: Attach Ethernet to network interface */
    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    ret = esp_netif_attach(s_eth_netif, s_eth_glue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach Ethernet to netif: %s", esp_err_to_name(ret));
        goto cleanup_eth;
    }
    
    /* Step 12: Start Ethernet */
    ret = esp_eth_start(s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet start failed: %s", esp_err_to_name(ret));
        goto cleanup_attach;
    }
    
    s_eth_initialized = true;
    ESP_LOGI(TAG, "Ethernet initialization complete");
    
    /* Step 13: Wait for connection */
    EventBits_t bits = xEventGroupWaitBits(s_eth_event_group,
            ETHERNET_CONNECTED_BIT | ETHERNET_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(ETHERNET_CONNECT_TIMEOUT_MS));
    
    if (bits & ETHERNET_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Ethernet connected successfully");
        return ESP_OK;
    } else if (bits & ETHERNET_FAIL_BIT) {
        ESP_LOGE(TAG, "Ethernet connection failed (link down)");
        ret = ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Ethernet connection timeout");
        ret = ESP_ERR_TIMEOUT;
    }
    
    /* Cleanup on connection failure */
    ethernet_disconnect();
    return ret;
    
/* Cleanup labels for error handling */
cleanup_attach:
    if (s_eth_glue != NULL) {
        esp_eth_del_netif_glue(s_eth_glue);
        s_eth_glue = NULL;
    }
    
cleanup_eth:
    esp_eth_driver_uninstall(s_eth_handle);
    s_eth_handle = NULL;
    
cleanup_phy:
    if (s_phy != NULL) {
        s_phy->del(s_phy);
        s_phy = NULL;
    }
    
cleanup_mac:
    if (s_mac != NULL) {
        s_mac->del(s_mac);
        s_mac = NULL;
    }
    
cleanup_spi_device:
    if (s_spi_handle != NULL) {
        spi_bus_remove_device(s_spi_handle);
        s_spi_handle = NULL;
    }
    
cleanup_spi:
    spi_bus_free(g_config.eth.host);
    
cleanup:
    ethernet_unregister_event_handlers();
    
    if (s_eth_netif != NULL) {
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
    }
    
    if (s_eth_event_group != NULL) {
        vEventGroupDelete(s_eth_event_group);
        s_eth_event_group = NULL;
    }
    
    s_eth_initialized = false;
    return ret;
}

/**
 * @brief Unregister Ethernet event handlers
 */
static void ethernet_unregister_event_handlers(void)
{
    if (instance_any_id != NULL) {
        esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
        instance_any_id = NULL;
    }
    
    if (instance_got_ip != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, instance_got_ip);
        instance_got_ip = NULL;
    }
}

/**
 * @brief Disconnect and deinitialize Ethernet
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_disconnect(void)
{
    if (!s_eth_initialized) {
        ESP_LOGW(TAG, "Ethernet not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Disconnecting Ethernet...");
    
    /* Step 1: Stop Ethernet */
    if (s_eth_handle != NULL) {
        esp_eth_stop(s_eth_handle);
        esp_eth_driver_uninstall(s_eth_handle);
        s_eth_handle = NULL;
    }
    
    /* Step 2: Detach from network interface */
    if (s_eth_glue != NULL) {
        esp_eth_del_netif_glue(s_eth_glue);
        s_eth_glue = NULL;
    }
    
    /* Step 3: Delete MAC and PHY */
    if (s_phy != NULL) {
        s_phy->del(s_phy);
        s_phy = NULL;
    }
    
    if (s_mac != NULL) {
        s_mac->del(s_mac);
        s_mac = NULL;
    }
    
    /* Step 4: Cleanup SPI */
    if (s_spi_handle != NULL) {
        spi_bus_remove_device(s_spi_handle);
        s_spi_handle = NULL;
        spi_bus_free(g_config.eth.host);
    }
    
    /* Step 5: Unregister event handlers */
    ethernet_unregister_event_handlers();
    
    /* Step 6: Destroy network interface */
    if (s_eth_netif != NULL) {
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
    }
    
    /* Step 7: Delete event group */
    if (s_eth_event_group != NULL) {
        vEventGroupDelete(s_eth_event_group);
        s_eth_event_group = NULL;
    }
    
    s_eth_initialized = false;
    ESP_LOGI(TAG, "Ethernet disconnected and deinitialized");
    
    return ESP_OK;
}

/**
 * @brief Get Ethernet network interface
 * @return Pointer to Ethernet netif or NULL if not initialized
 */
esp_netif_t *get_ethernet_netif(void)
{
    return s_eth_netif;
}

/**
 * @brief Check if Ethernet is connected
 * @return true if connected, false otherwise
 */
bool ethernet_is_connected(void)
{
    if (s_eth_event_group == NULL) {
        return false;
    }
    
    EventBits_t bits = xEventGroupGetBits(s_eth_event_group);
    return (bits & ETHERNET_CONNECTED_BIT) != 0;
}
