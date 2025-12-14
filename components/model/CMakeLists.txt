# CMake build configuration for OPC UA Model component
# See project LICENSE file for licensing information.

idf_component_register(SRCS "model.c"
                    INCLUDE_DIRS "include" "../open62541lib/include"
                    REQUIRES esp32-pcf8574 driver io_cache esp_adc)