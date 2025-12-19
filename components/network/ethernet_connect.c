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
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_random.h"
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
static esp_event_handler_instance_t instance_lost_ip = NULL;
static bool s_eth_initialized = false;
static bool s_dhcp_timeout_handled = false;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;
static bool s_ip_config_applied = false;

static void ethernet_unregister_event_handlers(void);

// Упрощенная функция для логирования базовой информации
static void log_interface_info(void)
{
    ESP_LOGI(TAG, "=== INTERFACE INFO ===");
    
    if (s_eth_netif == NULL) {
        ESP_LOGW(TAG, "Netif not created yet");
        return;
    }
    
    // Получаем MAC адрес
    uint8_t mac[6];
    esp_err_t ret = esp_netif_get_mac(s_eth_netif, mac);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGW(TAG, "Failed to get MAC: %s", esp_err_to_name(ret));
    }
    
    // Получаем IP информацию
    esp_netif_ip_info_t ip_info;
    ret = esp_netif_get_ip_info(s_eth_netif, &ip_info);
    if (ret == ESP_OK) {
        if (ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
            ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info.gw));
        } else {
            ESP_LOGI(TAG, "IP: 0.0.0.0 (not assigned)");
        }
    }
    
    // Статус DHCP
    esp_netif_dhcp_status_t dhcp_status;
    ret = esp_netif_dhcpc_get_status(s_eth_netif, &dhcp_status);
    if (ret == ESP_OK) {
        const char *status_str;
        switch (dhcp_status) {
            case ESP_NETIF_DHCP_INIT: status_str = "INIT"; break;
            case ESP_NETIF_DHCP_STARTED: status_str = "STARTED"; break;
            case ESP_NETIF_DHCP_STOPPED: status_str = "STOPPED"; break;
            default: status_str = "UNKNOWN"; break;
        }
        ESP_LOGI(TAG, "DHCP status: %s (%d)", status_str, dhcp_status);
    }
    
    ESP_LOGI(TAG, "=== END INFO ===");
}

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet link up");
                
                // Логируем информацию о интерфейсе
                log_interface_info();
                
                // Проверяем статус DHCP клиента
                esp_netif_dhcp_status_t dhcp_status;
                esp_err_t ret = esp_netif_dhcpc_get_status(s_eth_netif, &dhcp_status);
                ESP_LOGI(TAG, "DHCP client status after link up: %d (ret=%s)", 
                        dhcp_status, esp_err_to_name(ret));
                
                // Если DHCP режим
                if (g_config.eth.ip_config.mode == NET_DHCP) {
                    // Если DHCP не в состоянии STARTED, запускаем
                    if (dhcp_status != ESP_NETIF_DHCP_STARTED) {
                        ESP_LOGI(TAG, "Starting DHCP client after link up...");
                        
                        // Останавливаем если был в другом состоянии
                        if (dhcp_status != ESP_NETIF_DHCP_INIT) {
                            esp_netif_dhcpc_stop(s_eth_netif);
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
                        
                        // Запускаем DHCP
                        ret = esp_netif_dhcpc_start(s_eth_netif);
                        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
                            ESP_LOGE(TAG, "Failed to start DHCP client: %s", esp_err_to_name(ret));
                        } else {
                            ESP_LOGI(TAG, "DHCP client started successfully");
                            
                            // Ждем немного и проверяем статус
                            vTaskDelay(pdMS_TO_TICKS(500));
                            esp_netif_dhcpc_get_status(s_eth_netif, &dhcp_status);
                            ESP_LOGI(TAG, "DHCP client status after start: %d", dhcp_status);
                            
                            // Логируем информацию еще раз
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            log_interface_info();
                        }
                    } else {
                        ESP_LOGI(TAG, "DHCP client already in STARTED state");
                    }
                } 
                // Если статический режим
                else {
                    ESP_LOGI(TAG, "Applying static IP configuration after link up");
                    ethernet_apply_ip_config();
                }
                break;
                
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Ethernet link down");
                xEventGroupSetBits(s_eth_event_group, ETHERNET_FAIL_BIT);
                s_ip_config_applied = false;
                break;
                
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet started");
                break;
                
            case ETHERNET_EVENT_STOP:
                ESP_LOGW(TAG, "Ethernet stopped");
                s_ip_config_applied = false;
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_ETH_GOT_IP: {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                
                ESP_LOGI(TAG, "Ethernet got IP:" IPSTR, IP2STR(&event->ip_info.ip));
                ESP_LOGI(TAG, "Netmask:" IPSTR, IP2STR(&event->ip_info.netmask));
                ESP_LOGI(TAG, "Gateway:" IPSTR, IP2STR(&event->ip_info.gw));
                
                // ДЕБАГ: Проверяем источник IP (DHCP или статика)
                esp_netif_dhcp_status_t dhcp_status;
                esp_netif_dhcpc_get_status(s_eth_netif, &dhcp_status);
                ESP_LOGI(TAG, "IP source: %s", 
                        dhcp_status == ESP_NETIF_DHCP_STARTED ? "DHCP" : "STATIC");
                
                // Логируем информацию после получения IP
                log_interface_info();
                
                xEventGroupSetBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
                s_ip_config_applied = true;
                break;
            }
                
            case IP_EVENT_ETH_LOST_IP:
                ESP_LOGW(TAG, "Ethernet lost IP");
                xEventGroupClearBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
                s_ip_config_applied = false;
                break;
        }
    }
}

esp_err_t ethernet_apply_ip_config(void)
{
    if (s_eth_netif == NULL) {
        ESP_LOGE(TAG, "Network interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Applying Ethernet IP configuration, mode: %s",
             g_config.eth.ip_config.mode == NET_STATIC ? "STATIC" : "DHCP");
    
    if (g_config.eth.ip_config.mode == NET_STATIC) {
        ESP_LOGI(TAG, "Setting static IP configuration...");
        
        // Останавливаем DHCP клиент если запущен
        esp_err_t ret = esp_netif_dhcpc_stop(s_eth_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGW(TAG, "Failed to stop DHCP client: %s", esp_err_to_name(ret));
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
        
        ESP_LOGI(TAG, "Setting IP: " IPSTR, IP2STR(&g_config.eth.ip_config.ip_info.ip));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&g_config.eth.ip_config.ip_info.netmask));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&g_config.eth.ip_config.ip_info.gw));
        
        ret = esp_netif_set_ip_info(s_eth_netif, &g_config.eth.ip_config.ip_info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(ret));
            // Пробуем вернуться к DHCP
            esp_netif_dhcpc_start(s_eth_netif);
            return ret;
        }
        
        // Применяем DNS серверы если сконфигурированы
        if (g_config.eth.ip_config.dns_primary != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4.addr = g_config.eth.ip_config.dns_primary;
            dns.ip.type = IPADDR_TYPE_V4;
            ret = esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_MAIN, &dns);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to set primary DNS: %s", esp_err_to_name(ret));
            } else {
                char dns_str[16];
                config_int_to_ip(g_config.eth.ip_config.dns_primary, dns_str, sizeof(dns_str));
                ESP_LOGI(TAG, "Primary DNS set: %s", dns_str);
            }
        }
        
        if (g_config.eth.ip_config.dns_secondary != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4.addr = g_config.eth.ip_config.dns_secondary;
            dns.ip.type = IPADDR_TYPE_V4;
            ret = esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_BACKUP, &dns);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to set secondary DNS: %s", esp_err_to_name(ret));
            } else {
                char dns_str[16];
                config_int_to_ip(g_config.eth.ip_config.dns_secondary, dns_str, sizeof(dns_str));
                ESP_LOGI(TAG, "Secondary DNS set: %s", dns_str);
            }
        }
        
        xEventGroupSetBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
        s_ip_config_applied = true;
        
        ESP_LOGI(TAG, "Static IP configuration applied successfully");
        
    } else {
        // DHCP режим
        ESP_LOGI(TAG, "Starting DHCP client...");
        
        // Проверяем текущий статус DHCP
        esp_netif_dhcp_status_t dhcp_status;
        esp_err_t ret = esp_netif_dhcpc_get_status(s_eth_netif, &dhcp_status);
        
        ESP_LOGI(TAG, "Current DHCP status: %d", dhcp_status);
        
        // Если DHCP уже в любом активном состоянии, не перезапускаем
        if (ret == ESP_OK && (dhcp_status == ESP_NETIF_DHCP_STARTED || dhcp_status == ESP_NETIF_DHCP_INIT)) {
            ESP_LOGI(TAG, "DHCP client already in state: %d", dhcp_status);
            return ESP_OK; // Уже запущен
        }
        
        // Останавливаем если был в статическом режиме
        if (s_ip_config_applied) {
            esp_netif_dhcpc_stop(s_eth_netif);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        // Запускаем DHCP
        ret = esp_netif_dhcpc_start(s_eth_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ESP_LOGE(TAG, "Failed to start DHCP client: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "DHCP client started successfully");
        
        // Даем время для инициализации
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Проверяем статус еще раз
        esp_netif_dhcpc_get_status(s_eth_netif, &dhcp_status);
        ESP_LOGI(TAG, "DHCP client status after start: %d", dhcp_status);
        
        s_ip_config_applied = false;
        s_dhcp_timeout_handled = false;
        
        ESP_LOGI(TAG, "DHCP client started, waiting for IP assignment...");
        return ESP_OK;
    }
    
    return ESP_OK;
}

esp_err_t ethernet_connect(void)
{
    esp_err_t ret = ESP_OK;
    
    if (!g_config.init_complete) {
        ESP_LOGE(TAG, "System configuration not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_config.eth.enable) {
        ESP_LOGW(TAG, "Ethernet is disabled in configuration");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (s_eth_initialized) {
        ESP_LOGW(TAG, "Ethernet already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing Ethernet with W5500 controller");
    
    s_dhcp_timeout_handled = false;
    s_ip_config_applied = false;
    
    // Устанавливаем сервис прерываний GPIO
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Сброс W5500 если есть reset pin
    if (g_config.eth.reset_pin >= 0) {
        ESP_LOGI(TAG, "Performing hardware reset on pin %d", g_config.eth.reset_pin);
        
        ret = gpio_set_direction(g_config.eth.reset_pin, GPIO_MODE_OUTPUT);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set reset pin direction: %s", esp_err_to_name(ret));
        }
        
        gpio_set_level(g_config.eth.reset_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(g_config.eth.reset_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Создаем event group для отслеживания состояния
    s_eth_event_group = xEventGroupCreate();
    if (s_eth_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    // Сброс флагов
    xEventGroupClearBits(s_eth_event_group, ETHERNET_CONNECTED_BIT | ETHERNET_FAIL_BIT);
    
    // 1. СОЗДАЕМ СЕТЕВОЙ ИНТЕРФЕЙС
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&cfg);
    if (s_eth_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create network interface");
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Ethernet network interface created");
    
    // 2. УСТАНАВЛИВАЕМ HOSTNAME ДО присоединения драйвера (ВАЖНО!)
    if (strlen(g_config.eth.ip_config.hostname) > 0) {
        esp_netif_set_hostname(s_eth_netif, g_config.eth.ip_config.hostname);
        ESP_LOGI(TAG, "Hostname set: %s", g_config.eth.ip_config.hostname);
    }
    
    // 3. РЕГИСТРИРУЕМ ОБРАБОТЧИКИ СОБЫТИЙ
    ret = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                              &event_handler, NULL, &instance_any_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ETH_EVENT handler: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                              &event_handler, NULL, &instance_got_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP_EVENT_ETH_GOT_IP handler: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_LOST_IP,
                                              &event_handler, NULL, &instance_lost_ip);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register IP_EVENT_ETH_LOST_IP handler: %s", esp_err_to_name(ret));
    }
    
    // 4. ИНИЦИАЛИЗИРУЕМ SPI ШИНУ
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
    
    // 5. ДОБАВЛЯЕМ SPI УСТРОЙСТВО
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
    
    // 6. ПОЛУЧАЕМ MAC АДРЕС ИЗ СИСТЕМЫ ESP32-S3
    uint8_t eth_mac[6] = {0};
    esp_err_t mac_ret = esp_read_mac(eth_mac, ESP_MAC_ETH);
    
    if (mac_ret == ESP_OK) {
        ESP_LOGI(TAG, "Using system MAC for Ethernet: %02x:%02x:%02x:%02x:%02x:%02x",
                eth_mac[0], eth_mac[1], eth_mac[2],
                eth_mac[3], eth_mac[4], eth_mac[5]);
    } else {
        ESP_LOGW(TAG, "Failed to get system MAC: %s", esp_err_to_name(mac_ret));
        
        // Генерируем уникальный локально-администрируемый MAC адрес
        eth_mac[0] = 0x02;  // Локально администрируемый, универкаст
        eth_mac[1] = 0x00;
        eth_mac[2] = 0x00;
        eth_mac[3] = 0x12;
        eth_mac[4] = 0x34;
        // Последний байт делаем случайным для уникальности
        uint32_t random_num = esp_random();
        eth_mac[5] = 0x56 + (random_num & 0xFF);
        
        ESP_LOGI(TAG, "Using generated local MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                eth_mac[0], eth_mac[1], eth_mac[2],
                eth_mac[3], eth_mac[4], eth_mac[5]);
    }
    
    // 7. КОНФИГУРАЦИЯ ДЛЯ W5500
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(g_config.eth.host, &devcfg);
    w5500_config.int_gpio_num = g_config.eth.interrupt_pin;
    
    if (g_config.eth.interrupt_pin < 0) {
        w5500_config.poll_period_ms = 50;
    }
    
    // ВНИМАНИЕ: В ESP-IDF v5.x НЕ устанавливаем MAC через mac_config
    // eth_mac_config_t не имеет поля mac_addr
    
    // 8. КОНФИГУРАЦИЯ MAC
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.sw_reset_timeout_ms = 2000;
    mac_config.rx_task_stack_size = 4096;
    mac_config.rx_task_prio = 20;
    
    // 9. СОЗДАЕМ MAC ДЛЯ W5500
    s_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    if (s_mac == NULL) {
        ESP_LOGE(TAG, "Failed to create W5500 MAC");
        ret = ESP_FAIL;
        goto cleanup_spi_device;
    }
    
    // ВНИМАНИЕ: Убрана попытка установки MAC через esp_eth_mac_set_addr()
    // Этой функции нет в ESP-IDF v5.x
    
    // 10. СОЗДАЕМ PHY ДЛЯ W5500
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = g_config.eth.reset_pin;
    phy_config.autonego_timeout_ms = 3000;
    phy_config.phy_addr = 0;
    
    s_phy = esp_eth_phy_new_w5500(&phy_config);
    if (s_phy == NULL) {
        ESP_LOGE(TAG, "Failed to create W5500 PHY");
        ret = ESP_FAIL;
        goto cleanup_mac;
    }
    
    // 11. СОЗДАЕМ И УСТАНАВЛИВАЕМ ДРАЙВЕР ETHERNET
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    
    ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(ret));
        goto cleanup_phy;
    }
    
    // ИСПРАВЛЕНИЕ: Теперь правильно устанавливаем MAC через esp_eth_ioctl
    // 12. УСТАНАВЛИВАЕМ MAC АДРЕС В W5500
    ESP_LOGI(TAG, "Setting MAC address via ioctl...");
    ret = esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MAC via ioctl: %s", esp_err_to_name(ret));
        // Продолжаем, возможно драйвер сам установит
    } else {
        ESP_LOGI(TAG, "MAC address set via ioctl successfully");
    }
    
    // 13. ПРОВЕРЯЕМ MAC АДРЕС (ПОСЛЕ установки)
    uint8_t check_mac[6];
    ret = esp_eth_ioctl(s_eth_handle, ETH_CMD_G_MAC_ADDR, check_mac);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "W5500 configured MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                check_mac[0], check_mac[1], check_mac[2],
                check_mac[3], check_mac[4], check_mac[5]);
    } else {
        ESP_LOGW(TAG, "Failed to get MAC from W5500: %s", esp_err_to_name(ret));
    }
    
    // 14. ПРИСОЕДИНЯЕМ ДРАЙВЕР К СЕТЕВОМУ ИНТЕРФЕЙСУ
    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    ret = esp_netif_attach(s_eth_netif, s_eth_glue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach Ethernet to network interface: %s", esp_err_to_name(ret));
        goto cleanup_eth;
    }
    
    // 15. ЛОГИРУЕМ ФИНАЛЬНЫЙ MAC АДРЕС ИЗ NETIF
    uint8_t final_mac[6];
    ret = esp_netif_get_mac(s_eth_netif, final_mac);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Netif MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
                final_mac[0], final_mac[1], final_mac[2],
                final_mac[3], final_mac[4], final_mac[5]);
    } else {
        ESP_LOGW(TAG, "Failed to get MAC from netif: %s", esp_err_to_name(ret));
    }
    
    // 16. ЛОГИРУЕМ НАЧАЛЬНОЕ СОСТОЯНИЕ
    log_interface_info();
    
    // 17. ПРЕДВАРИТЕЛЬНАЯ КОНФИГУРАЦИЯ IP (если статика)
    if (g_config.eth.ip_config.mode == NET_STATIC) {
        ESP_LOGI(TAG, "Pre-configuring static IP before Ethernet start");
        ethernet_apply_ip_config();
    }
    // Для DHCP НЕ запускаем здесь - только после link up!
    
    // 18. ЗАПУСКАЕМ ETHERNET
    ret = esp_eth_start(s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Ethernet: %s", esp_err_to_name(ret));
        goto cleanup_attach;
    }
    
    s_eth_initialized = true;
    ESP_LOGI(TAG, "Ethernet initialization complete");
    
    // Ждем подключения с таймаутом
    ESP_LOGI(TAG, "Waiting for Ethernet connection (timeout: %d ms)...", ETHERNET_CONNECT_TIMEOUT_MS);
    
    EventBits_t bits = xEventGroupWaitBits(s_eth_event_group,
            ETHERNET_CONNECTED_BIT | ETHERNET_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(ETHERNET_CONNECT_TIMEOUT_MS));
    
    if (bits & ETHERNET_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Ethernet connected successfully!");
        
        // Получаем и логируем финальную IP конфигурацию
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_eth_netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "Final IP: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
            ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info.gw));
        }
        
        return ESP_OK;
    } else if (bits & ETHERNET_FAIL_BIT) {
        ESP_LOGE(TAG, "Ethernet connection failed (link down)");
        ret = ESP_FAIL;
    } else {
        ESP_LOGW(TAG, "Ethernet connection timeout after %d ms", ETHERNET_CONNECT_TIMEOUT_MS);
        
        // Проверяем, есть ли у нас IP (на случай если DHCP медленный)
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_eth_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "Got IP after timeout: " IPSTR, IP2STR(&ip_info.ip));
            return ESP_OK;
        }
        
        // Если DHCP не дал IP, пробуем использовать статический IP как запасной вариант
        if (g_config.eth.ip_config.mode == NET_DHCP && !s_dhcp_timeout_handled) {
            ESP_LOGI(TAG, "DHCP timeout, trying fallback to static IP...");
            s_dhcp_timeout_handled = true;
            
            // Сохраняем оригинальный режим
            net_ip_mode_t original_mode = g_config.eth.ip_config.mode;
            
            // Временно переключаем на статику (APIPA адрес)
            g_config.eth.ip_config.mode = NET_STATIC;
            
            // Используем APIPA адрес (169.254.x.x) как запасной вариант
            uint32_t fallback_ip = ESP_IP4TOADDR(169, 254, 1, 1);
            uint32_t fallback_mask = ESP_IP4TOADDR(255, 255, 0, 0);
            uint32_t fallback_gw = ESP_IP4TOADDR(169, 254, 1, 1);
            
            g_config.eth.ip_config.ip_info.ip.addr = fallback_ip;
            g_config.eth.ip_config.ip_info.netmask.addr = fallback_mask;
            g_config.eth.ip_config.ip_info.gw.addr = fallback_gw;
            
            esp_err_t ip_ret = ethernet_apply_ip_config();
            if (ip_ret == ESP_OK) {
                ESP_LOGI(TAG, "Fallback to APIPA address successful: 169.254.1.1");
                // Не возвращаем обратно DHCP режим - оставляем статику
                return ESP_OK;
            }
            
            // Возвращаем оригинальный режим если fallback не сработал
            g_config.eth.ip_config.mode = original_mode;
            ESP_LOGW(TAG, "Fallback to static IP failed");
        }
        
        ret = ESP_ERR_TIMEOUT;
    }
    
    // Подключение не удалось, выполняем cleanup
    ESP_LOGI(TAG, "Performing cleanup after failed connection");
    
    if (s_eth_initialized) {
        ethernet_disconnect();
    }
    
    return ret;
    
// Обработчики ошибок для корректной очистки ресурсов
cleanup_attach:
    if (s_eth_glue != NULL) {
        esp_eth_del_netif_glue(s_eth_glue);
        s_eth_glue = NULL;
    }
    
cleanup_eth:
    if (s_eth_handle != NULL) {
        esp_eth_driver_uninstall(s_eth_handle);
        s_eth_handle = NULL;
    }
    
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
    s_dhcp_timeout_handled = false;
    s_ip_config_applied = false;
    
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
    
    if (instance_lost_ip != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_ETH_LOST_IP, instance_lost_ip);
        instance_lost_ip = NULL;
    }
}

esp_err_t ethernet_disconnect(void)
{
    if (!s_eth_initialized) {
        ESP_LOGW(TAG, "Ethernet not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Disconnecting Ethernet...");
    
    // Отписываемся от обработчиков событий
    ethernet_unregister_event_handlers();
    
    // Останавливаем Ethernet
    if (s_eth_handle != NULL) {
        esp_eth_stop(s_eth_handle);
        esp_eth_driver_uninstall(s_eth_handle);
        s_eth_handle = NULL;
    }
    
    // Удаляем glue
    if (s_eth_glue != NULL) {
        esp_eth_del_netif_glue(s_eth_glue);
        s_eth_glue = NULL;
    }
    
    // Удаляем PHY
    if (s_phy != NULL) {
        s_phy->del(s_phy);
        s_phy = NULL;
    }
    
    // Удаляем MAC
    if (s_mac != NULL) {
        s_mac->del(s_mac);
        s_mac = NULL;
    }
    
    // Удаляем SPI устройство
    if (s_spi_handle != NULL) {
        spi_bus_remove_device(s_spi_handle);
        s_spi_handle = NULL;
    }
    
    // Освобождаем SPI шину
    if (g_config.eth.enable) {
        spi_bus_free(g_config.eth.host);
    }
    
    // Удаляем сетевой интерфейс
    if (s_eth_netif != NULL) {
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
    }
    
    // Удаляем event group
    if (s_eth_event_group != NULL) {
        vEventGroupDelete(s_eth_event_group);
        s_eth_event_group = NULL;
    }
    
    s_eth_initialized = false;
    s_dhcp_timeout_handled = false;
    s_ip_config_applied = false;
    
    ESP_LOGI(TAG, "Ethernet disconnected and cleaned up");
    
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