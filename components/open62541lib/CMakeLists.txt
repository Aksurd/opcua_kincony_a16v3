# CMake build configuration for Open62541 OPC UA Library component
# See project LICENSE file for licensing information.

idf_component_register(SRCS "open62541.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_netif esp_eth esp_driver_spi)  # ← ЭТА СТРОКА ИСПРАВЛЕНА

component_compile_options(-Wno-error=format= -Wno-format -Wempty-body)

# ESP32-specific IEEE 754 floating point definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    UA_ARCHITECTURE_ESP32
    UA_ENABLE_NATIVE_IEEE_754
)

#  Kастомный плагин контроля доступа
file(GLOB_RECURSE opcua_access_control_srcs
    "opcua_access_control/*.c"
)

target_sources(${COMPONENT_LIB} PRIVATE
    ${opcua_access_control_srcs}
)

target_include_directories(${COMPONENT_LIB} PUBLIC
    "opcua_access_control/include"
)

# Добавьте путь к main для доступа к config.h
target_include_directories(${COMPONENT_LIB} PRIVATE
    "../../main"
)