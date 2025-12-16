idf_component_register(
    SRCS "ethernet_connect.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_eth lwip driver
    PRIV_REQUIRES main esp-eth-drivers
)