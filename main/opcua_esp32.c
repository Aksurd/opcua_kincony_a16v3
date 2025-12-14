/*
 * ============================================================================
 * OPC UA ESP32-S3 Server - Kincony A16V3 Industrial Controller
 * ============================================================================
 *
 * Copyright (c) 2025 Alexander Dikunov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * ----------------------------------------------------------------------------
 *                    MULTI-LICENSE COMPONENT NOTICE
 * ----------------------------------------------------------------------------
 * This firmware is a derivative work and integration of multiple components
 * under different open-source licenses. The MIT license above applies to the
 * project as a whole and to original components created by the project author.
 *
 * For complete licensing details of all third-party components, see the
 * LICENSE file in the project root directory.
 *
 * COMPONENT ATTRIBUTION AND LICENSES:
 *
 * 1. OPC UA Server Core & Hardware Adaptation
 *    - Derived from: https://github.com/cmbahadir/opcua-esp32
 *    - Original Author: Cem Bahadir
 *    - Original License: Mozilla Public License, v. 2.0 (MPL-2.0)
 *    - Modifications: Adapted for Kincony A16V3 hardware, refactored I/O model,
 *      enhanced watchdog and timing logic, added comprehensive documentation.
 *
 * 2. Network Connection Module
 *    - Derived from: ESP-IDF Example Code
 *    - Original Author: Espressif Systems (Shanghai) CO LTD
 *    - Original License: Apache License, Version 2.0
 *    - Modifications: Integrated into project structure, documentation.
 *
 * 3. PCF8574 I2C Expander Driver
 *    - Status: Original work created for this project.
 *    - Author: Alexander Dikunov
 *    - License: MIT License (covered by the project-wide license above)
 *
 * 4. OPC UA Protocol Stack
 *    - Library: open62541 (https://github.com/open62541/open62541)
 *    - Authors: open62541 Contributors
 *    - License: Mozilla Public License, v. 2.0 (MPL-2.0)
 *    - Usage: Linked as an unmodified library.
 *
 * 5. Hardware Platform
 *    - Manufacturer and Designer: Hangzhou Kincony Electronics Co., Ltd.
 *    - Product: Kincony A16V3 (ESP32-S3 based industrial controller)
 *    - Note: 'KinCony' and 'KControl' are trademarks of the manufacturer.
 *            This firmware is designed for, but not officially affiliated with, this hardware.
 *
 * ----------------------------------------------------------------------------
 *                        PROJECT INFORMATION
 * ----------------------------------------------------------------------------
 * Project Author and Maintainer: Alexander Dikunov
 * Contact:      wxid_ic7ytyv3mlh522 (WeChat)
 *
 * Platform:     Kincony A16V3 Industrial Controller (ESP32-S3 based)
 *               - ESP32-S3 dual-core Xtensa LX7 @ 240MHz
 *               - 16x Opto-isolated Digital I/O
 *               - 4x Analog Inputs (AI1-AI2: 0-20mA, AI3-AI4: 0-5V)
 * Framework:    ESP-IDF v5.5.1
 * OPC UA Stack: open62541 v1.3+
 *
 * Version:      0.0.1-LAN (Pre-Alpha, Hardware-Specific Build)
 * Status:       EXPERIMENTAL - Hardware validation in progress
 * Date:         December 2025
 *
 * WARNING: This firmware is SPECIFIC to Kincony A16V3 hardware.
 *          Use only in isolated test environments.
 *
 * ============================================================================
 *                           BUILD INSTRUCTIONS
 * ============================================================================
 *
 * 1. ESP-IDF Environment Setup (v5.5.1):
 *    ----------------------------------------------
 *    # Install prerequisites
 *    sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv \
 *        cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
 *
 *    # Clone ESP-IDF
 *    git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git
 *    cd esp-idf
 *    ./install.sh esp32s3
 *    . ./export.sh
 *
 * 2. Project Setup:
 *    ----------------------------------------------
 *    # Clone this repository
 *    git clone https://github.com/Aksurd/opcua-kincony-a16v3
 *    cd opcua-kincony-a16v3
 *
 *    # Configure for Kincony A16V3
 *    idf.py set-target esp32s3
 *    idf.py menuconfig
 *
 *    IMPORTANT CONFIGURATION:
 *    → Component config → ESP32S3-specific → CPU Frequency: 240MHz
 *    → Component config → Ethernet → Use ESP32 internal EMAC
 *    → Component config → Ethernet → PHY interface: RMII
 *    → Component config → Ethernet → PHY address: 1
 *    → Component config → open62541 → Custom buffer sizes
 *
 * 3. Build and Flash:
 *    ----------------------------------------------
 *    # Build the firmware
 *    idf.py build
 *
 *    # Flash to Kincony A16V3 (via USB-C)
 *    idf.py -p /dev/ttyACM0 flash monitor
 *
 *    # Alternative: Flash with baud rate override
 *    idf.py -p /dev/ttyACM0 -b 921600 flash monitor
 *
 * 4. Monitor Debug Output:
 *    ----------------------------------------------
 *    idf.py -p /dev/ttyACM0 monitor
 *    # Press Ctrl+] to exit monitor
 *
 * ============================================================================
 */


#include "opcua_esp32.h"
#include "model.h"
#include "io_cache.h"
#include "esp_task_wdt.h"          /* Watchdog timer functions */
#include "esp_sntp.h"              /* SNTP time synchronization */
#include "nvs_flash.h"             /* Non-volatile storage */
#include "esp_err.h"               /* ESP error codes */
#include "esp_flash.h"             /* Flash encryption functions */
#include "esp_flash_encrypt.h"     /* Flash encryption utilities */

#define EXAMPLE_ESP_MAXIMUM_RETRY 10

#define TAG "OPCUA_ESP32"
#define SNTP_TAG "SNTP"
#define WDT_TAG "WATCHDOG"

static bool obtain_time(void);
static void initialize_sntp(void);

UA_ServerConfig *config;
static UA_Boolean esp_sntp_initialized = false;
static UA_Boolean running = true;
static UA_Boolean isServerCreated = false;
RTC_DATA_ATTR static int boot_count = 0;
static struct tm timeinfo;
static time_t now = 0;

static UA_StatusCode
UA_ServerConfig_setUriName(UA_ServerConfig *uaServerConfig, const char *uri, const char *name)
{
    // delete pre-initialized values
    UA_String_clear(&uaServerConfig->applicationDescription.applicationUri);
    UA_LocalizedText_clear(&uaServerConfig->applicationDescription.applicationName);

    uaServerConfig->applicationDescription.applicationUri = UA_String_fromChars(uri);
    uaServerConfig->applicationDescription.applicationName.locale = UA_STRING_NULL;
    uaServerConfig->applicationDescription.applicationName.text = UA_String_fromChars(name);

    for (size_t i = 0; i < uaServerConfig->endpointsSize; i++)
    {
        UA_String_clear(&uaServerConfig->endpoints[i].server.applicationUri);
        UA_LocalizedText_clear(
            &uaServerConfig->endpoints[i].server.applicationName);

        UA_String_copy(&uaServerConfig->applicationDescription.applicationUri,
                       &uaServerConfig->endpoints[i].server.applicationUri);

        UA_LocalizedText_copy(&uaServerConfig->applicationDescription.applicationName,
                              &uaServerConfig->endpoints[i].server.applicationName);
    }

    return UA_STATUSCODE_GOOD;
}

static void opcua_task(void *arg)
{
    // BufferSize's got to be decreased due to latest refactorings in open62541 v1.2rc.
    UA_Int32 sendBufferSize = 16384;
    UA_Int32 recvBufferSize = 16384;

    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err != ESP_OK) {
        ESP_LOGE(WDT_TAG, "Failed to add task to WDT: %s", esp_err_to_name(wdt_err));
    } else {
        ESP_LOGI(WDT_TAG, "Task added to watchdog");
    }

    ESP_LOGI(TAG, "OPC UA Server starting");
    
    UA_Server *server = UA_Server_new();
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to create OPC UA server!");
        vTaskDelete(NULL);
        return;
    }
    
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setMinimalCustomBuffer(config, 4840, 0, sendBufferSize, recvBufferSize);

    const char *appUri = "open62541.esp32.server";
    UA_String hostName = UA_STRING("opcua-esp32");
    
    UA_ServerConfig_setUriName(config, appUri, "OPC_UA_Server_ESP32");
    UA_ServerConfig_setCustomHostname(config, hostName);

    // Define Node IDs for all variables
    UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_NodeId variableTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);

    /* Add diagnostic variables */

    // 1. Counter
    UA_VariableAttributes counterAttr = UA_VariableAttributes_default;
    counterAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Diagnostic Counter");
    counterAttr.description = UA_LOCALIZEDTEXT("en-US", "Incremental counter for timing tests");
    counterAttr.dataType = UA_TYPES[UA_TYPES_UINT16].typeId;
    counterAttr.accessLevel = UA_ACCESSLEVELMASK_READ;

    UA_DataSource counterDataSource;
    counterDataSource.read = readDiagnosticCounter;
    counterDataSource.write = NULL;

    UA_NodeId counterNodeId = UA_NODEID_STRING(1, "diagnostic_counter");
    UA_QualifiedName counterName = UA_QUALIFIEDNAME(1, "Diagnostic Counter");

    UA_StatusCode add_status = UA_Server_addDataSourceVariableNode(server, counterNodeId, parentNodeId,
                                        parentReferenceNodeId, counterName,
                                        variableTypeNodeId, counterAttr,
                                        counterDataSource, NULL, NULL);
    if (add_status != UA_STATUSCODE_GOOD) {
        ESP_LOGE(TAG, "Failed to add diagnostic counter: 0x%08X", add_status);
    }

    // 2. Loopback Input
    UA_VariableAttributes loopbackInAttr = UA_VariableAttributes_default;
    loopbackInAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Loopback Input");
    loopbackInAttr.description = UA_LOCALIZEDTEXT("en-US", "Write value here, read from Loopback Output");
    loopbackInAttr.dataType = UA_TYPES[UA_TYPES_UINT16].typeId;
    loopbackInAttr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    UA_DataSource loopbackInDataSource;
    loopbackInDataSource.read = readLoopbackInput;
    loopbackInDataSource.write = writeLoopbackInput;

    UA_NodeId loopbackInNodeId = UA_NODEID_STRING(1, "loopback_input");
    UA_QualifiedName loopbackInName = UA_QUALIFIEDNAME(1, "Loopback Input");

    add_status = UA_Server_addDataSourceVariableNode(server, loopbackInNodeId, parentNodeId,
                                        parentReferenceNodeId, loopbackInName,
                                        variableTypeNodeId, loopbackInAttr,
                                        loopbackInDataSource, NULL, NULL);
    if (add_status != UA_STATUSCODE_GOOD) {
        ESP_LOGE(TAG, "Failed to add loopback input: 0x%08X", add_status);
    }

    // 3. Loopback Output
    UA_VariableAttributes loopbackOutAttr = UA_VariableAttributes_default;
    loopbackOutAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Loopback Output");
    loopbackOutAttr.description = UA_LOCALIZEDTEXT("en-US", "Mirror of Loopback Input (read-only)");
    loopbackOutAttr.dataType = UA_TYPES[UA_TYPES_UINT16].typeId;
    loopbackOutAttr.accessLevel = UA_ACCESSLEVELMASK_READ;

    UA_DataSource loopbackOutDataSource;
    loopbackOutDataSource.read = readLoopbackOutput;
    loopbackOutDataSource.write = NULL;

    UA_NodeId loopbackOutNodeId = UA_NODEID_STRING(1, "loopback_output");
    UA_QualifiedName loopbackOutName = UA_QUALIFIEDNAME(1, "Loopback Output");

    add_status = UA_Server_addDataSourceVariableNode(server, loopbackOutNodeId, parentNodeId,
                                        parentReferenceNodeId, loopbackOutName,
                                        variableTypeNodeId, loopbackOutAttr,
                                        loopbackOutDataSource, NULL, NULL);
    if (add_status != UA_STATUSCODE_GOOD) {
        ESP_LOGE(TAG, "Failed to add loopback output: 0x%08X", add_status);
    }

    /* Add Information Model Objects Here */
    // REMOVED: addDSTemperatureDataSourceVariable(server);
    addDiscreteIOVariables(server);
    addAdcVariables(server);
    
    ESP_LOGI(TAG, "OPC UA server initialized");
    
    UA_StatusCode retval = UA_Server_run_startup(server);
    if (retval != UA_STATUSCODE_GOOD)
    {
        ESP_LOGE(TAG, "OPC UA server startup failed: 0x%08X", retval);
        UA_Server_delete(server);
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "OPC UA server running");
    
    uint32_t watchdog_reset_errors = 0;
    const uint32_t max_watchdog_errors = 10;
    
    while (running)
    {
        // CRITICAL FIX: use 10ms timeout instead of blocking call
        UA_Server_run_iterate(server, 10); // 10ms timeout
        
        // Reset watchdog
        esp_err_t reset_err = esp_task_wdt_reset();
        if (reset_err != ESP_OK) {
            watchdog_reset_errors++;
            ESP_LOGE(WDT_TAG, "Watchdog reset failed: %s (error %d/%d)", 
                     esp_err_to_name(reset_err), 
                     watchdog_reset_errors, 
                     max_watchdog_errors);
            
            if (watchdog_reset_errors >= max_watchdog_errors) {
                ESP_LOGE(WDT_TAG, "Too many watchdog errors, restarting task");
                break;
            }
        } else {
            watchdog_reset_errors = 0;
        }
        
        // CRITICAL FIX: give time to other tasks
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    ESP_LOGW(TAG, "OPC UA server shutting down");
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    
    esp_err_t delete_err = esp_task_wdt_delete(NULL);
    if (delete_err != ESP_OK) {
        ESP_LOGE(WDT_TAG, "Failed to delete task from WDT: %s", esp_err_to_name(delete_err));
    }
    
    vTaskDelete(NULL);
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(SNTP_TAG, "Time synchronized");
}

static void initialize_sntp(void)
{
    ESP_LOGI(SNTP_TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    
    esp_sntp_init();
    esp_sntp_initialized = true;
}

static bool obtain_time(void)
{
    initialize_sntp();
    
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err != ESP_OK) {
        ESP_LOGE(WDT_TAG, "Failed to add SNTP task to WDT: %s", esp_err_to_name(wdt_err));
    }
    
    memset(&timeinfo, 0, sizeof(struct tm));
    int retry = 0;
    const int retry_count = 10;
    
    ESP_LOGI(SNTP_TAG, "Getting time from NTP...");
    
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= retry_count)
    {
        if (retry % 3 == 0) {
            ESP_LOGW(SNTP_TAG, "Waiting for NTP response... (%d/%d)", retry, retry_count);
        }
        
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        
        esp_err_t reset_err = esp_task_wdt_reset();
        if (reset_err != ESP_OK) {
            ESP_LOGE(WDT_TAG, "SNTP WDT reset failed: %s", esp_err_to_name(reset_err));
        }
    }
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    esp_err_t delete_err = esp_task_wdt_delete(NULL);
    if (delete_err != ESP_OK) {
        ESP_LOGE(WDT_TAG, "Failed to delete SNTP task from WDT: %s", esp_err_to_name(delete_err));
    }
    
    if (timeinfo.tm_year <= (2016 - 1900)) {
        ESP_LOGE(SNTP_TAG, "Failed to get valid time from NTP");
        return false;
    }
    
    ESP_LOGI(SNTP_TAG, "Time obtained: %04d-%02d-%02d %02d:%02d:%02d", 
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    return true;
}

static void opc_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (esp_sntp_initialized != true)
    {
        if (timeinfo.tm_year < (2016 - 1900))
        {
            ESP_LOGI(SNTP_TAG, "Getting time from NTP");
            if (!obtain_time())
            {
                ESP_LOGE(SNTP_TAG, "NTP failed, using default time");
                now = 0;
            }
            time(&now);
        }
        localtime_r(&now, &timeinfo);
    }

    if (!isServerCreated)
    {
        ESP_LOGI(TAG, "Creating OPC UA task...");
        // CRITICAL FIX: run on core 0, priority 5
        BaseType_t task_created = xTaskCreatePinnedToCore(opcua_task, "opcua_task", 
                                                          24336, NULL, 5, NULL, 0);
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create OPC UA task!");
        } else {
            isServerCreated = true;
        }
    }
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGW(TAG, "Network disconnected");
    running = false;
}

static void connection_scan(void)
{
    ESP_LOGI(TAG, "Initializing network...");
    
    esp_err_t netif_err = esp_netif_init();
    if (netif_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init netif: %s", esp_err_to_name(netif_err));
    }
    
    esp_err_t event_err = esp_event_loop_create_default();
    if (event_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(event_err));
    }
    
    esp_err_t handler_err = esp_event_handler_register(IP_EVENT, GOT_IP_EVENT, &opc_event_handler, NULL);
    if (handler_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(handler_err));
    }
    
    handler_err = esp_event_handler_register(BASE_IP_EVENT, DISCONNECT_EVENT, &disconnect_handler, NULL);
    if (handler_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register disconnect handler: %s", esp_err_to_name(handler_err));
    }
    
    ESP_LOGI(TAG, "Connecting to network...");
    example_connect();
}

void app_main(void)
{
    ++boot_count;
    ESP_LOGI(TAG, "Boot count: %d", boot_count);
    
    /* INITIALIZE CACHE AND POLLING TASK */
    ESP_LOGI(TAG, "Initializing IO cache system...");
    io_cache_init();
    adc_init();
    io_polling_task_start();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Workaround for CVE-2019-15894
    nvs_flash_init();
    if (esp_flash_encryption_enabled())
    {
        esp_flash_write_protect_crypt_cnt();
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_LOGI(TAG, "Erasing NVS partition...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "NVS initialized");
    }
    
    connection_scan();
}