# CMake build configuration for OPC UA ESP32-S3 Server
# See project LICENSE file for licensing information.

# Minimum required CMake version
cmake_minimum_required(VERSION 3.16)

# Project name and version
set(PROJECT_NAME "opcua-kincony-a16v3")
set(PROJECT_VERSION "0.0.1")

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set project name and version - ЭТО ДОЛЖНО БЫТЬ ПЕРЕД project()
set(PROJECT_NAME "opcua-kincony-a16v3")
set(PROJECT_VERSION "0.0.1")

# THIS LINE IS CRITICAL - must be exactly like this
project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})

# NO other commands between project() and idf_component_register()