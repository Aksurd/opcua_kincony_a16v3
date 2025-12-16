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

#define ETHERNET_CONNECTED_BIT BIT(0)
#define ETHERNET_FAIL_BIT      BIT(1)
#define ETHERNET_CONNECT_TIMEOUT_MS 30000

static const char *TAG = "eth";
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

static void ethernet_unregister_event_handlers(void);

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGW(TAG, "link up");
                break;
                
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "link down");
                xEventGroupSetBits(s_eth_event_group, ETHERNET_FAIL_BIT);
                break;
                
            case ETHERNET_EVENT_START:
                ESP_LOGW(TAG, "started");
                break;
                
            case ETHERNET_EVENT_STOP:
                ESP_LOGW(TAG, "stopped");
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        if (g_config.eth.ip_config.mode == NET_DHCP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGW(TAG, "DHCP IP:" IPSTR, IP2STR(&event->ip_info.ip));
            ethernet_apply_ip_config();
            xEventGroupSetBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
        } else {
            ESP_LOGW(TAG, "static IP configured");
        }
    }
}

esp_err_t ethernet_apply_ip_config(void)
{
    if (s_eth_netif == NULL) {
        ESP_LOGE(TAG, "netif not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_config.eth.ip_config.mode == NET_STATIC) {
        ESP_LOGW(TAG, "applying static IP");
        
        esp_err_t ret = esp_netif_dhcpc_stop(s_eth_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGW(TAG, "failed to stop DHCP: %s", esp_err_to_name(ret));
        }
        
        ret = esp_netif_set_ip_info(s_eth_netif, &g_config.eth.ip_config.ip_info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to set static IP: %s", esp_err_to_name(ret));
            return ret;
        }
        
        if (g_config.eth.ip_config.dns_primary != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4.addr = g_config.eth.ip_config.dns_primary;
            dns.ip.type = IPADDR_TYPE_V4;
            ret = esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_MAIN, &dns);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "failed to set primary DNS: %s", esp_err_to_name(ret));
            }
        }
        
        if (g_config.eth.ip_config.dns_secondary != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4.addr = g_config.eth.ip_config.dns_secondary;
            dns.ip.type = IPADDR_TYPE_V4;
            ret = esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_BACKUP, &dns);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "failed to set secondary DNS: %s", esp_err_to_name(ret));
            }
        }
        
        if (strlen(g_config.eth.ip_config.hostname) > 0) {
            esp_netif_set_hostname(s_eth_netif, g_config.eth.ip_config.hostname);
        }
        
        if (s_eth_event_group != NULL) {
            xEventGroupSetBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
        }
        
    } else {
        ESP_LOGW(TAG, "using DHCP");
        esp_err_t ret = esp_netif_dhcpc_start(s_eth_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ESP_LOGW(TAG, "failed to start DHCP: %s", esp_err_to_name(ret));
        }
        
        if (strlen(g_config.eth.ip_config.hostname) > 0) {
            esp_netif_set_hostname(s_eth_netif, g_config.eth.ip_config.hostname);
        }
    }
    
    return ESP_OK;
}

esp_err_t ethernet_connect(void)
{
    esp_err_t ret = ESP_OK;
    
    if (!g_config.init_complete) {
        ESP_LOGE(TAG, "config not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_config.eth.enable) {
        ESP_LOGW(TAG, "disabled in config");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (s_eth_initialized) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "initializing W5500");
    
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "failed to install GPIO ISR: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (g_config.eth.reset_pin >= 0) {
        ESP_LOGW(TAG, "performing hardware reset");
        
        ret = gpio_set_direction(g_config.eth.reset_pin, GPIO_MODE_OUTPUT);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "failed to set reset pin direction: %s", esp_err_to_name(ret));
        }
        
        gpio_set_level(g_config.eth.reset_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(g_config.eth.reset_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    s_eth_event_group = xEventGroupCreate();
    if (s_eth_event_group == NULL) {
        ESP_LOGE(TAG, "failed to create event group");
        return ESP_FAIL;
    }
    
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&cfg);
    if (s_eth_netif == NULL) {
        ESP_LOGE(TAG, "failed to create netif");
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    ret = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, 
                                             &event_handler, NULL, &instance_any_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to register ETH_EVENT handler");
        goto cleanup;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, 
                                             &event_handler, NULL, &instance_got_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to register IP_EVENT handler");
        goto cleanup;
    }
    
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
    
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(g_config.eth.host, &devcfg);
    w5500_config.int_gpio_num = g_config.eth.interrupt_pin;
    
    if (g_config.eth.interrupt_pin < 0) {
        w5500_config.poll_period_ms = 50;
    }
    
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.sw_reset_timeout_ms = 2000;
    mac_config.rx_task_stack_size = 4096;
    mac_config.rx_task_prio = 20;
    
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = g_config.eth.reset_pin;
    phy_config.autonego_timeout_ms = 3000;
    phy_config.phy_addr = 0;
    
    s_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    if (s_mac == NULL) {
        ESP_LOGE(TAG, "failed to create W5500 MAC");
        ret = ESP_FAIL;
        goto cleanup_spi_device;
    }
    
    s_phy = esp_eth_phy_new_w5500(&phy_config);
    if (s_phy == NULL) {
        ESP_LOGE(TAG, "failed to create W5500 PHY");
        ret = ESP_FAIL;
        goto cleanup_mac;
    }
    
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    
    ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "driver install failed: %s", esp_err_to_name(ret));
        goto cleanup_phy;
    }
    
    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    ret = esp_netif_attach(s_eth_netif, s_eth_glue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to attach to netif: %s", esp_err_to_name(ret));
        goto cleanup_eth;
    }
    
    ret = esp_eth_start(s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(ret));
        goto cleanup_attach;
    }
    
    s_eth_initialized = true;
    ESP_LOGW(TAG, "initialization complete");
    
    EventBits_t bits = xEventGroupWaitBits(s_eth_event_group,
            ETHERNET_CONNECTED_BIT | ETHERNET_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(ETHERNET_CONNECT_TIMEOUT_MS));
    
    if (bits & ETHERNET_CONNECTED_BIT) {
        ESP_LOGW(TAG, "connected successfully");
        return ESP_OK;
    } else if (bits & ETHERNET_FAIL_BIT) {
        ESP_LOGE(TAG, "connection failed (link down)");
        ret = ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "connection timeout");
        ret = ESP_ERR_TIMEOUT;
    }
    
    ethernet_disconnect();
    return ret;
    
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

esp_err_t ethernet_disconnect(void)
{
    if (!s_eth_initialized) {
        ESP_LOGW(TAG, "not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "disconnecting...");
    
    if (s_eth_handle != NULL) {
        esp_eth_stop(s_eth_handle);
        esp_eth_driver_uninstall(s_eth_handle);
        s_eth_handle = NULL;
    }
    
    if (s_eth_glue != NULL) {
        esp_eth_del_netif_glue(s_eth_glue);
        s_eth_glue = NULL;
    }
    
    if (s_phy != NULL) {
        s_phy->del(s_phy);
        s_phy = NULL;
    }
    
    if (s_mac != NULL) {
        s_mac->del(s_mac);
        s_mac = NULL;
    }
    
    if (s_spi_handle != NULL) {
        spi_bus_remove_device(s_spi_handle);
        s_spi_handle = NULL;
        spi_bus_free(g_config.eth.host);
    }
    
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
    ESP_LOGW(TAG, "disconnected");
    
    return ESP_OK;
}

esp_netif_t *get_ethernet_netif(void)
{
    return s_eth_netif;
}

bool ethernet_is_connected(void)
{
    if (s_eth_event_group == NULL) {
        return false;
    }
    
    EventBits_t bits = xEventGroupGetBits(s_eth_event_group);
    return (bits & ETHERNET_CONNECTED_BIT) != 0;
}