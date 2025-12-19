# Main application CMakeLists.txt
# Note: This file should be in the main directory

# Main application source files
idf_component_register(SRCS "opcua_esp32.c" "config.c"
                    INCLUDE_DIRS "."
                    REQUIRES
                        esp_netif
                        esp_event
                        esp_system
                        nvs_flash
                        lwip
                        driver
                        freertos
                        model
                        network
                        network
                        network
                        open62541lib
                        spi_flash
                        bootloader_support
)

# Memory configuration for OPC UA
set(OPCUA_STACK_SIZE "24336")
set(OPCUA_TASK_PRIORITY "5")

# Component-specific flags - USE add_compile_definitions correctly
if(CONFIG_EXAMPLE_CONNECT_ETHERNET)
    add_compile_definitions(CONFIG_EXAMPLE_CONNECT_ETHERNET=1)
    message(STATUS "Ethernet support enabled")
endif()

if(CONFIG_EXAMPLE_CONNECT_WIFI)
    add_compile_definitions(CONFIG_EXAMPLE_CONNECT_WIFI=1)
    message(STATUS "Wi-Fi support enabled")
endif()

# PSRAM configuration (if available)
if(CONFIG_SPIRAM_SUPPORT)
    add_compile_definitions(CONFIG_ESP32_SPIRAM_SUPPORT=1)
    add_compile_definitions(CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=1)
    message(STATUS "PSRAM support enabled")
endif()

# Additional include directories
include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/components/model/include
    ${CMAKE_CURRENT_LIST_DIR}/components/network/include
)