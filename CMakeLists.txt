# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)



set(CMAKE_C_COMPILER "/home/user5/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc")
set(CMAKE_CXX_COMPILER "/home/user5/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++")
set(CMAKE_ASM_COMPILER "/home/user5/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENTS_DIRS "components/open62541lib" "components/ethernet" "components/model")

project(opcua_esp32)
