# List of components to include in the project
set(EXTRA_COMPONENT_DIRS 
    components/ethernet
    components/model  
    components/open62541lib

)

idf_component_register(SRCS "opcua_esp32.c"
                    INCLUDE_DIRS ".")
