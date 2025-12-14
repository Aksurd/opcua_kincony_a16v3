# CMake build configuration for Open62541 OPC UA Library component
# See project LICENSE file for licensing information.

idf_component_register(SRCS "open62541.c"
                    INCLUDE_DIRS "include")
component_compile_options(-Wno-error=format= -Wno-format -Wempty-body)

# ESP32-specific IEEE 754 floating point definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    UA_ARCHITECTURE_ESP32
    UA_ENABLE_NATIVE_IEEE_754
)
