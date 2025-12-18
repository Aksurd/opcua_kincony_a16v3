// opcua_esp32.c

/*
 * ============================================================================
 * OPC UA ESP32-S3 Server - Kincony A16V3 Industrial Controller
 * ============================================================================
 */

#include "opcua_esp32.h"
#include "model.h"
#include "io_cache.h"
#include "network_manager.h"
#include "esp_task_wdt.h"          
#include "esp_sntp.h"              
#include "nvs_flash.h"             
#include "esp_err.h"               
#include "esp_flash.h"             
#include "esp_flash_encrypt.h"     
#include "esp_event.h"
#include "esp_eth.h"
#include "config.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY 10

#define TAG "OPCUA_ESP32"
#define SNTP_TAG "SNTP"
#define WDT_TAG "WATCHDOG"
#define NET_TAG "NETWORK"

// Объявление функций
static void opcua_task(void *arg);
static bool obtain_time(void);
static void initialize_sntp(void);
static void opc_network_state_callback(bool connected, esp_netif_t *netif);
static void start_opcua_fallback(void *arg);
static void check_and_start_opcua(void);

UA_ServerConfig *config;
static UA_Boolean esp_sntp_initialized = false;
static UA_Boolean running = true;
static UA_Boolean isServerCreated = false;
RTC_DATA_ATTR static int boot_count = 0;
static struct tm timeinfo;
static time_t now = 0;
static bool network_initialized = false;

// Флаг для принудительного запуска OPC UA через таймер
static bool fallback_triggered = false;

// Callback для состояния сети
static void opc_network_state_callback(bool connected, esp_netif_t *netif) {
    ESP_LOGI(TAG, "Network state callback called: connected=%d, netif=%p", connected, netif);
    
    if (connected) {
        ESP_LOGI(TAG, "Network is now connected!");
        network_initialized = true;
        
        // Даем сети немного времени на стабилизацию
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        check_and_start_opcua();
    } else {
        ESP_LOGW(TAG, "Network disconnected");
        network_initialized = false;
        running = false;
    }
}

// Fallback: запуск OPC UA через 10 секунд, даже если callback не сработал
static void start_opcua_fallback(void *arg) {
    ESP_LOGW(TAG, "Fallback timer started - waiting 10 seconds for network...");
    
    // Ждем 10 секунд
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    if (!isServerCreated && !fallback_triggered) {
        fallback_triggered = true;
        ESP_LOGW(TAG, "Fallback: forcing OPC UA server start...");
        check_and_start_opcua();
    }
    
    vTaskDelete(NULL);
}

// Функция проверки и запуска OPC UA
static void check_and_start_opcua(void) {
    if (isServerCreated) {
        ESP_LOGI(TAG, "OPC UA server already created");
        return;
    }
    
    ESP_LOGI(TAG, "Attempting to start OPC UA server...");
    
    // Проверяем, есть ли активный сетевой интерфейс
    esp_netif_t *active_netif = network_manager_get_active_netif();
    if (active_netif == NULL) {
        ESP_LOGW(TAG, "No active network interface yet, checking alternatives...");
        active_netif = network_manager_get_eth_netif();
        if (active_netif == NULL) {
            active_netif = network_manager_get_wifi_netif();
        }
    }
    
    if (active_netif == NULL) {
        ESP_LOGW(TAG, "Still no network interface, will try again later");
        return;
    }
    
    // Получаем IP информацию для диагностики
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(active_netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "Active interface IP: " IPSTR, IP2STR(&ip_info.ip));
    }
    
    // Запускаем OPC UA сервер
    BaseType_t task_created = xTaskCreatePinnedToCore(opcua_task, "opcua_task", 
                                                      24336, NULL, 5, NULL, 0);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OPC UA task!");
        // Попробуем еще раз с меньшим стеком
        task_created = xTaskCreatePinnedToCore(opcua_task, "opcua_task", 
                                               16384, NULL, 5, NULL, 0);
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create OPC UA task even with smaller stack!");
        } else {
            isServerCreated = true;
            ESP_LOGI(TAG, "OPC UA task created with smaller stack");
        }
    } else {
        isServerCreated = true;
        ESP_LOGI(TAG, "OPC UA task created successfully");
    }
}

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
    ESP_LOGI(TAG, "OPC UA Server task starting on core %d", xPortGetCoreID());
    
    // BufferSize's got to be decreased due to latest refactorings in open62541 v1.2rc.
    UA_Int32 sendBufferSize = 16384;
    UA_Int32 recvBufferSize = 16384;

    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err != ESP_OK) {
        ESP_LOGE(WDT_TAG, "Failed to add task to WDT: %s", esp_err_to_name(wdt_err));
    } else {
        ESP_LOGI(WDT_TAG, "Task added to watchdog");
    }

    ESP_LOGI(TAG, "Creating OPC UA server...");
    
    UA_Server *server = UA_Server_new();
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to create OPC UA server!");
        
        // Попробуем освободить память и создать снова
        esp_restart(); // Простой способ в случае неудачи
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "OPC UA server created, configuring...");
    
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setMinimalCustomBuffer(config, 4840, 0, sendBufferSize, recvBufferSize);

    const char *appUri = "open62541.esp32.server";
    UA_String hostName = UA_STRING("opcua-esp32");
    
    UA_ServerConfig_setUriName(config, appUri, "OPC_UA_Server_ESP32");
    UA_ServerConfig_setCustomHostname(config, hostName);

    ESP_LOGI(TAG, "Server configured, adding variables...");

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
    } else {
        ESP_LOGI(TAG, "Diagnostic counter added");
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
    } else {
        ESP_LOGI(TAG, "Loopback input added");
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
    } else {
        ESP_LOGI(TAG, "Loopback output added");
    }

    /* Add Information Model Objects Here */
    ESP_LOGI(TAG, "Adding discrete I/O variables...");
    addDiscreteIOVariables(server);
    
    ESP_LOGI(TAG, "Adding ADC variables...");
    addAdcVariables(server);
    
    ESP_LOGI(TAG, "All variables added, starting server...");
    
    UA_StatusCode retval = UA_Server_run_startup(server);
    if (retval != UA_STATUSCODE_GOOD)
    {
        ESP_LOGE(TAG, "OPC UA server startup failed: 0x%08X", retval);
        UA_Server_delete(server);
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "OPC UA server running on port 4840");
    ESP_LOGI(TAG, "Server URI: opc.tcp://[IP]:4840");
    
    // Получаем и выводим IP адрес для удобства
    esp_netif_t *netif = network_manager_get_active_netif();
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "Connect using: opc.tcp://%s:4840", ip_str);
        }
    }
    
    uint32_t watchdog_reset_errors = 0;
    const uint32_t max_watchdog_errors = 10;
    
    running = true;
    
    while (running)
    {
        UA_Server_run_iterate(server, 10);
        
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
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    ESP_LOGW(TAG, "OPC UA server shutting down");
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    
    esp_err_t delete_err = esp_task_wdt_delete(NULL);
    if (delete_err != ESP_OK) {
        ESP_LOGE(WDT_TAG, "Failed to delete task from WDT: %s", esp_err_to_name(delete_err));
    }
    
    isServerCreated = false;
    ESP_LOGI(TAG, "OPC UA task finished");
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

static void connection_scan(void)
{
    ESP_LOGI(NET_TAG, "Initializing network manager with both Ethernet and Wi-Fi...");
    
    config_init_defaults();
    ESP_LOGI(NET_TAG, "Configuration system initialized");
    
    // Initialize network manager
    esp_err_t nm_err = network_manager_init();
    if (nm_err != ESP_OK) {
        ESP_LOGE(NET_TAG, "Failed to initialize network manager: %s", esp_err_to_name(nm_err));
        return;
    }
    
    // Устанавливаем callback для уведомления о состоянии сети
    network_manager_set_state_callback(opc_network_state_callback);
    ESP_LOGI(NET_TAG, "Network callback registered");
    
    // Запускаем fallback таймер (на всякий случай)
    xTaskCreate(start_opcua_fallback, "fallback_timer", 2048, NULL, 2, NULL);
    ESP_LOGI(NET_TAG, "Fallback timer started (10 seconds)");
    
    // Start both network connections
    nm_err = network_manager_start();
    if (nm_err != ESP_OK) {
        ESP_LOGW(NET_TAG, "Some network connections failed, continuing...");
    }
    
    ESP_LOGI(NET_TAG, "Network initialization complete");
    ESP_LOGI(NET_TAG, "Ethernet interface: %s", network_manager_get_eth_netif() ? "available" : "not available");
    ESP_LOGI(NET_TAG, "Wi-Fi interface: %s", network_manager_get_wifi_netif() ? "available" : "not available");
    ESP_LOGI(NET_TAG, "Any connected: %s", network_manager_is_any_connected() ? "YES" : "NO");
}

void app_main(void)
{
    ++boot_count;
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "OPC UA ESP32-S3 Server v1.0");
    ESP_LOGI(TAG, "Boot count: %d", boot_count);
    ESP_LOGI(TAG, "========================================");
    
    // Включаем подробные логи
    esp_log_level_set("OPCUA_ESP32", ESP_LOG_VERBOSE);
    esp_log_level_set("net", ESP_LOG_VERBOSE);
    esp_log_level_set("eth", ESP_LOG_INFO);
    esp_log_level_set("wifi", ESP_LOG_INFO);
    
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
    
    ESP_LOGI(TAG, "Starting network scan...");
    connection_scan();
    
    ESP_LOGI(TAG, "app_main() completed, system is running...");
}