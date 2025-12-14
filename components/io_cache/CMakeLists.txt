# CMake build configuration for I/O Cache component
# See project LICENSE file for licensing information.

idf_component_register(SRCS "io_cache.c" "io_polling.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos model)