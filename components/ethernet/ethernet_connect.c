/* ethernet_connect.c - Based on ESP-IDF examples (Apache-2.0). See project LICENSE and main file. */

#include <string.h>
#include "sdkconfig.h"
#include "esp_event.h"

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
#include "esp_eth.h"
#endif

#include "ethernet_connect.h"

#define GOT_IPV4_BIT BIT(0)
#define GOT_IPV6_BIT BIT(1)
#define CONNECTED_BITS (GOT_IPV4_BIT)

static EventGroupHandle_t s_connect_event_group;
static esp_ip4_addr_t s_ip_addr;
static const char *s_connection_name;
static esp_netif_t *s_example_esp_netif = NULL;

static const char *TAG = "online_connection";
static void start(void);
static void stop(void);

/**
 * @brief Event handler for IP address acquisition
 * 
 * This function is called when the system receives an IP address
 * from DHCP or static configuration. It stores the IP address
 * and signals the connection event group.
 * 
 * @param arg User-defined argument (not used)
 * @param event_base Event base identifier
 * @param event_id Event ID (IP_EVENT_GOT_IP)
 * @param event_data Pointer to IP event data structure
 */
static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Got IP event!");
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    memcpy(&s_ip_addr, &event->ip_info.ip, sizeof(s_ip_addr));
    xEventGroupSetBits(s_connect_event_group, GOT_IPV4_BIT);
}

/**
 * @brief Establish network connection (Ethernet or Wi-Fi)
 * 
 * This function initializes the network connection based on the
 * configuration in sdkconfig.h. It can connect via Ethernet or
 * Wi-Fi depending on the build configuration.
 * 
 * @return ESP_OK if connection was successfully established
 * @return ESP_ERR_INVALID_STATE if already connected
 * 
 * @note This function blocks until IP address is obtained
 * @note Automatically registers shutdown handler for cleanup
 */
esp_err_t example_connect(void)
{
    if (s_connect_event_group != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    s_connect_event_group = xEventGroupCreate();
    start();
    ESP_ERROR_CHECK(esp_register_shutdown_handler(&stop));
    ESP_LOGI(TAG, "Waiting for IP");
    xEventGroupWaitBits(s_connect_event_group, CONNECTED_BITS, true, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to %s", s_connection_name);
    ESP_LOGI(TAG, "IPv4 address: " IPSTR, IP2STR(&s_ip_addr));
    return ESP_OK;
}

/**
 * @brief Disconnect from network
 * 
 * This function tears down the network connection and cleans up
 * all allocated resources.
 * 
 * @return ESP_OK if successfully disconnected
 * @return ESP_ERR_INVALID_STATE if not currently connected
 */
esp_err_t example_disconnect(void)
{
    if (s_connect_event_group == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    vEventGroupDelete(s_connect_event_group);
    s_connect_event_group = NULL;
    stop();
    ESP_LOGI(TAG, "Disconnected from %s", s_connection_name);
    s_connection_name = NULL;
    return ESP_OK;
}

#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
static esp_eth_handle_t eth_handle = NULL;
static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static void *s_eth_glue = NULL;

/**
 * @brief Start Ethernet connection
 * 
 * Initializes Ethernet hardware, configures network interface,
 * and starts the Ethernet driver. Supports both dynamic DHCP
 * and static IP configuration.
 */
static void start(void)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    assert(eth_netif);
    s_example_esp_netif = eth_netif;

#ifdef CONFIG_USE_STATIC_IP
    esp_netif_ip_info_t ipInfo;
    ipInfo.ip.addr = esp_ip4addr_aton(CONFIG_ETHERNET_HELPER_STATIC_IP4_ADDRESS);
    ipInfo.gw.addr = esp_ip4addr_aton(CONFIG_ETHERNET_HELPER_STATIC_GATEWAY);
    ipInfo.netmask.addr = esp_ip4addr_aton(CONFIG_ETHERNET_HELPER_STATIC_NETMASK);
    if (ipInfo.ip.addr != 0 && ipInfo.netmask.addr != 0 && ipInfo.gw.addr != 0)
    {
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(get_example_netif()));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(get_example_netif(), &ipInfo));
    }
    ESP_ERROR_CHECK(set_dns_server(eth_netif, ipaddr_addr(CONFIG_DNS_ADDRESS), ESP_NETIF_DNS_MAIN));
#endif
    // Set default handlers to process TCP/IP stuffs
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip, NULL));

    // Configuration using LAN8720
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    phy_config.phy_addr = CONFIG_EXAMPLE_ETH_PHY_ADDR;

    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_mdc_gpio_num = CONFIG_EXAMPLE_ETH_MDC_GPIO;
    esp32_emac_config.smi_mdio_gpio_num = CONFIG_EXAMPLE_ETH_MDIO_GPIO;
    s_mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    s_phy = esp_eth_phy_new_lan87xx(&phy_config);

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    s_connection_name = "ETH";
}

/**
 * @brief Stop Ethernet connection
 * 
 * Stops Ethernet driver, unregisters event handlers, and cleans up
 * all allocated resources for Ethernet connection.
 */
static void stop(void)
{
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip));
    ESP_ERROR_CHECK(esp_eth_stop(eth_handle));
    ESP_ERROR_CHECK(esp_eth_del_netif_glue(s_eth_glue));
    ESP_ERROR_CHECK(esp_eth_driver_uninstall(eth_handle));
    ESP_ERROR_CHECK(s_phy->del(s_phy));
    ESP_ERROR_CHECK(s_mac->del(s_mac));

    esp_netif_destroy(s_example_esp_netif);
    s_example_esp_netif = NULL;
}
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
/**
 * @brief Event handler for Wi-Fi disconnection
 * 
 * Automatically attempts to reconnect when Wi-Fi connection is lost.
 * 
 * @param arg User-defined argument (not used)
 * @param event_base Event base identifier
 * @param event_id Event ID (WIFI_EVENT_STA_DISCONNECTED)
 * @param event_data Pointer to Wi-Fi event data structure
 */
static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED)
    {
        return;
    }
    ESP_ERROR_CHECK(err);
}

/**
 * @brief Start Wi-Fi connection
 * 
 * Initializes Wi-Fi station mode, configures network interface,
 * and connects to the configured Wi-Fi network. Supports both
 * dynamic DHCP and static IP configuration.
 */
static void start(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();

    esp_netif_t *netif = esp_netif_new(&netif_config);

    assert(netif);

    esp_netif_attach_wifi_station(netif);
    esp_wifi_set_default_wifi_sta_handlers();

    s_example_esp_netif = netif;

#ifdef CONFIG_USE_STATIC_IP
    esp_netif_ip_info_t ipInfo;
    ipInfo.ip.addr = esp_ip4addr_aton(CONFIG_ETHERNET_HELPER_STATIC_IP4_ADDRESS);
    ipInfo.gw.addr = esp_ip4addr_aton(CONFIG_ETHERNET_HELPER_STATIC_GATEWAY);
    ipInfo.netmask.addr = esp_ip4addr_aton(CONFIG_ETHERNET_HELPER_STATIC_NETMASK);
    if (ipInfo.ip.addr != 0 && ipInfo.netmask.addr != 0 && ipInfo.gw.addr != 0)
    {
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(get_example_netif()));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(get_example_netif(), &ipInfo));
    }
    ESP_ERROR_CHECK(set_dns_server(netif, ipaddr_addr(CONFIG_DNS_ADDRESS), ESP_NETIF_DNS_MAIN));
#endif // CONFIG_USE_STATIC_IP

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect, netif));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
#endif

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    s_connection_name = CONFIG_WIFI_SSID;
}

/**
 * @brief Stop Wi-Fi connection
 * 
 * Disconnects from Wi-Fi network, stops Wi-Fi driver, and cleans up
 * all allocated resources for Wi-Fi connection.
 */
static void stop(void)
{
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect));
#endif
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT)
    {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_example_esp_netif));
    esp_netif_destroy(s_example_esp_netif);
    s_example_esp_netif = NULL;
}
#endif // CONFIG_EXAMPLE_CONNECT_WIFI

/**
 * @brief Get the current network interface
 * 
 * Returns a pointer to the active network interface (Ethernet or Wi-Fi).
 * 
 * @return esp_netif_t* Pointer to the network interface, or NULL if not connected
 */
esp_netif_t *get_example_netif(void)
{
    return s_example_esp_netif;
}

/**
 * @brief Configure DNS server for network interface
 * 
 * Sets the DNS server address for the specified network interface.
 * Can be used for both primary and backup DNS servers.
 * 
 * @param netif Pointer to network interface
 * @param addr DNS server IP address in network byte order
 * @param type DNS server type (MAIN, BACKUP, FALLBACK)
 * @return ESP_OK if DNS server was configured successfully
 */
esp_err_t set_dns_server(esp_netif_t *netif, uint32_t addr, esp_netif_dns_type_t type)
{
    if (addr && (addr != IPADDR_NONE)) {
        esp_netif_dns_info_t dns;
        dns.ip.u_addr.ip4.addr = addr;
        dns.ip.type = IPADDR_TYPE_V4;
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, type, &dns));
    }
    return ESP_OK;
}