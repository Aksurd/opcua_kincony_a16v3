idf_component_register(
    SRCS "network_manager.c" "wifi_connect.c" "ethernet_connect.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_wifi esp_eth lwip driver
    PRIV_REQUIRES main esp-eth-drivers
)
