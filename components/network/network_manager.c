#include "network_manager.h"
#include "config.h"
#include "wifi_connect.h"
#include "ethernet_connect.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"

static const char *TAG = "network_manager";

// Статические переменные
static bool s_network_initialized = false;
static network_state_callback_t s_state_callback = NULL;
static bool s_wifi_connected = false;
static bool s_eth_connected = false;
static EventGroupHandle_t s_wifi_event_group = NULL;
static EventGroupHandle_t s_eth_event_group = NULL;
static TimerHandle_t s_log_timer = NULL;

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================

#define LOG_SEPARATOR "════════════════════════════════════════"
#define WIFI_CONNECTED_BIT BIT(0)
#define WIFI_FAIL_BIT      BIT(1)
#define ETHERNET_CONNECTED_BIT BIT(0)
#define ETHERNET_FAIL_BIT      BIT(1)

static void log_timestamp(void) {
    TickType_t ticks = xTaskGetTickCount();
    uint32_t ms = ticks * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "[T+%d.%03ds]", ms / 1000, ms % 1000);
}

static void log_event(const char* event_name, const char* details) {
    log_timestamp();
    ESP_LOGI(TAG, "▒▒▒▒▒ %s ▒▒▒▒▒", event_name);
    if (details && strlen(details) > 0) {
        ESP_LOGI(TAG, "  └─ %s", details);
    }
}

static void log_config_state(void) {
    ESP_LOGI(TAG, LOG_SEPARATOR);
    ESP_LOGI(TAG, "📊 КОНФИГУРАЦИЯ СЕТИ:");
    ESP_LOGI(TAG, "  Wi-Fi: %s (%s)", 
             g_config.wifi.enable ? "ВКЛ" : "ВЫКЛ",
             g_config.wifi.ip_config.mode == NET_DHCP ? "DHCP" : "STATIC");
    ESP_LOGI(TAG, "  Ethernet: %s (%s)", 
             g_config.eth.enable ? "ВКЛ" : "ВЫКЛ",
             g_config.eth.ip_config.mode == NET_DHCP ? "DHCP" : "STATIC");
    
    if (g_config.eth.ip_config.mode == NET_STATIC) {
        char ip_str[16], mask_str[16], gw_str[16];
        config_int_to_ip(g_config.eth.ip_config.ip_info.ip.addr, ip_str, sizeof(ip_str));
        config_int_to_ip(g_config.eth.ip_config.ip_info.netmask.addr, mask_str, sizeof(mask_str));
        config_int_to_ip(g_config.eth.ip_config.ip_info.gw.addr, gw_str, sizeof(gw_str));
        ESP_LOGI(TAG, "  Ethernet статический IP: %s/%s шлюз:%s", ip_str, mask_str, gw_str);
    }
    ESP_LOGI(TAG, LOG_SEPARATOR);
}

static void log_network_status(void) {
    ESP_LOGI(TAG, "📡 СТАТУС СЕТИ:");
    ESP_LOGI(TAG, "  Wi-Fi: %s", s_wifi_connected ? "✅ ПОДКЛЮЧЕН" : "❌ ОТКЛЮЧЕН");
    ESP_LOGI(TAG, "  Ethernet: %s", s_eth_connected ? "✅ ПОДКЛЮЧЕН" : "❌ ОТКЛЮЧЕН");
    ESP_LOGI(TAG, "  Любое подключение: %s", 
             (s_wifi_connected || s_eth_connected) ? "✅ ДА" : "❌ НЕТ");
}

// Таймер для периодического логирования статуса
static void log_timer_callback(TimerHandle_t xTimer) {
    log_network_status();
}

static void notify_state_change(bool connected, esp_netif_t *netif)
{
    if (s_state_callback != NULL) {
        s_state_callback(connected, netif);
    }
    
    log_network_status();
}

// ==================== ОСНОВНАЯ ЛОГИКА ====================

// Приватная вспомогательная функция для применения IP конфигурации
static void apply_ip_configuration(esp_netif_t *netif, bool is_ethernet) 
{
    log_event("APPLY_IP_CONFIG", is_ethernet ? "Ethernet" : "Wi-Fi");
    
    if (netif == NULL) {
        ESP_LOGE(TAG, "❌ Сетевой интерфейс NULL!");
        return;
    }
    
    esp_err_t ret;
    
    if (is_ethernet) {
        // Для Ethernet
        if (g_config.eth.ip_config.mode == NET_STATIC) {
            ESP_LOGI(TAG, "  ⚡ Применяем СТАТИЧЕСКИЙ IP для Ethernet");
            ret = ethernet_apply_ip_config();
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "  ✅ Ethernet статический IP установлен");
                xEventGroupSetBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
            } else {
                ESP_LOGE(TAG, "  ❌ Ошибка установки статического IP: %s", 
                         esp_err_to_name(ret));
            }
        } else {
            ESP_LOGI(TAG, "  ⚡ Используем DHCP для Ethernet");
            ret = esp_netif_dhcpc_start(netif);
            if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
                ESP_LOGW(TAG, "  ⚠️ Не удалось запустить DHCP клиент: %s", 
                         esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, "  ✅ DHCP клиент запущен, ждем IP...");
            }
            return; // Для DHCP ждем IP_EVENT_ETH_GOT_IP
        }
    } else {
        // Для Wi-Fi
        if (g_config.wifi.ip_config.mode == NET_STATIC) {
            ESP_LOGI(TAG, "  ⚡ Применяем СТАТИЧЕСКИЙ IP для Wi-Fi");
            ret = wifi_apply_ip_config();
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "  ✅ Wi-Fi статический IP установлен");
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            } else {
                ESP_LOGE(TAG, "  ❌ Ошибка установки статического IP: %s", 
                         esp_err_to_name(ret));
            }
        } else {
            ESP_LOGI(TAG, "  ⚡ Используем DHCP для Wi-Fi");
            ret = esp_netif_dhcpc_start(netif);
            if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
                ESP_LOGW(TAG, "  ⚠️ Не удалось запустить DHCP клиент: %s", 
                         esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, "  ✅ DHCP клиент запущен, ждем IP...");
            }
            return; // Для DHCP ждем IP_EVENT_STA_GOT_IP
        }
    }
    
    ESP_LOGI(TAG, "  🎯 Результат apply_ip_configuration: %s", 
             esp_err_to_name(ret));
}

// Обработчик событий Wi-Fi
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                log_event("WIFI_EVENT_STA_START", "Wi-Fi станция запущена");
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                log_event("WIFI_EVENT_STA_CONNECTED", "Подключено к Wi-Fi точке доступа");
                s_wifi_connected = true;
                notify_state_change(true, get_wifi_netif());
                
                // Применяем IP конфигурацию (DHCP или Static)
                apply_ip_configuration(get_wifi_netif(), false);
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event = 
                    (wifi_event_sta_disconnected_t*) event_data;
                char reason[64];
                snprintf(reason, sizeof(reason), 
                        "Отключено от Wi-Fi, причина: %d", event->reason);
                log_event("WIFI_EVENT_STA_DISCONNECTED", reason);
                
                s_wifi_connected = false;
                notify_state_change(false, get_wifi_netif());
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                break;
            }
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                // Только для DHCP - статика уже обработана
                if (g_config.wifi.ip_config.mode == NET_DHCP) {
                    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                    char ip_str[16];
                    config_int_to_ip(event->ip_info.ip.addr, ip_str, sizeof(ip_str));
                    
                    char details[64];
                    snprintf(details, sizeof(details), 
                            "Wi-Fi получил IP по DHCP: %s", ip_str);
                    log_event("IP_EVENT_STA_GOT_IP", details);
                    
                    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                } else {
                    ESP_LOGI(TAG, "⚠️ IP_EVENT_STA_GOT_IP проигнорирован (используется статический IP)");
                }
                break;
                
            case IP_EVENT_STA_LOST_IP:
                log_event("IP_EVENT_STA_LOST_IP", "Wi-Fi потерял IP адрес");
                break;
        }
    }
}

// Обработчик событий Ethernet
static void eth_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_START:
                log_event("ETHERNET_EVENT_START", "Ethernet драйвер запущен");
                break;
                
            case ETHERNET_EVENT_CONNECTED:
                log_event("ETHERNET_EVENT_CONNECTED", "Ethernet линк поднят");
                s_eth_connected = true;
                notify_state_change(true, get_ethernet_netif());
                
                // Применяем IP конфигурацию (DHCP или Static)
                apply_ip_configuration(get_ethernet_netif(), true);
                break;
                
            case ETHERNET_EVENT_DISCONNECTED:
                log_event("ETHERNET_EVENT_DISCONNECTED", "Ethernet линк потерян");
                s_eth_connected = false;
                notify_state_change(false, get_ethernet_netif());
                xEventGroupSetBits(s_eth_event_group, ETHERNET_FAIL_BIT);
                break;
                
            case ETHERNET_EVENT_STOP:
                log_event("ETHERNET_EVENT_STOP", "Ethernet драйвер остановлен");
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_ETH_GOT_IP:
                // Только для DHCP - статика уже обработана
                if (g_config.eth.ip_config.mode == NET_DHCP) {
                    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                    char ip_str[16];
                    config_int_to_ip(event->ip_info.ip.addr, ip_str, sizeof(ip_str));
                    
                    char details[64];
                    snprintf(details, sizeof(details), 
                            "Ethernet получил IP по DHCP: %s", ip_str);
                    log_event("IP_EVENT_ETH_GOT_IP", details);
                    
                    xEventGroupSetBits(s_eth_event_group, ETHERNET_CONNECTED_BIT);
                } else {
                    ESP_LOGI(TAG, "⚠️ IP_EVENT_ETH_GOT_IP проигнорирован (используется статический IP)");
                }
                break;
                
            case IP_EVENT_ETH_LOST_IP:
                log_event("IP_EVENT_ETH_LOST_IP", "Ethernet потерял IP адрес");
                break;
        }
    }
}

// ==================== ПУБЛИЧНЫЕ ФУНКЦИИ ====================

esp_err_t network_manager_init(void)
{
    if (s_network_initialized) {
        ESP_LOGW(TAG, "⚠️ Network manager уже инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_config.init_complete) {
        ESP_LOGE(TAG, "❌ Конфигурация системы не инициализирована");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, LOG_SEPARATOR);
    ESP_LOGI(TAG, "🚀 ИНИЦИАЛИЗАЦИЯ NETWORK MANAGER");
    log_config_state();
    
    // Создание групп событий
    s_wifi_event_group = xEventGroupCreate();
    s_eth_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL || s_eth_event_group == NULL) {
        ESP_LOGE(TAG, "❌ Не удалось создать группы событий");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "✅ Группы событий созданы");
    
    // Инициализация TCP/IP стека
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Ошибка инициализации netif: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ TCP/IP стек инициализирован");
    
    // Создание цикла событий
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Ошибка создания цикла событий: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ Цикл событий создан");
    
    // Регистрация обработчиков событий для Wi-Fi
    if (g_config.wifi.enable) {
        ESP_LOGI(TAG, "📶 Регистрация обработчиков Wi-Fi событий");
        
        const struct {
            esp_event_base_t event_base;
            int32_t event_id;
            const char* desc;
        } wifi_handlers[] = {
            {WIFI_EVENT, WIFI_EVENT_STA_START, "STA_START"},
            {WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, "STA_CONNECTED"},
            {WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, "STA_DISCONNECTED"},
            {IP_EVENT, IP_EVENT_STA_GOT_IP, "STA_GOT_IP"},
            {IP_EVENT, IP_EVENT_STA_LOST_IP, "STA_LOST_IP"}
        };
        
        for (int i = 0; i < sizeof(wifi_handlers)/sizeof(wifi_handlers[0]); i++) {
            ret = esp_event_handler_instance_register(wifi_handlers[i].event_base,
                                                     wifi_handlers[i].event_id,
                                                     &wifi_event_handler, NULL, NULL);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "⚠️ Не удалось зарегистрировать %s: %s", 
                         wifi_handlers[i].desc, esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, "  ✅ %s зарегистрирован", wifi_handlers[i].desc);
            }
        }
    } else {
        ESP_LOGI(TAG, "📶 Wi-Fi отключен в конфигурации");
    }
    
    // Регистрация обработчиков событий для Ethernet
    if (g_config.eth.enable) {
        ESP_LOGI(TAG, "🔌 Регистрация обработчиков Ethernet событий");
        
        const struct {
            esp_event_base_t event_base;
            int32_t event_id;
            const char* desc;
        } eth_handlers[] = {
            {ETH_EVENT, ETHERNET_EVENT_START, "ETH_START"},
            {ETH_EVENT, ETHERNET_EVENT_CONNECTED, "ETH_CONNECTED"},
            {ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, "ETH_DISCONNECTED"},
            {ETH_EVENT, ETHERNET_EVENT_STOP, "ETH_STOP"},
            {IP_EVENT, IP_EVENT_ETH_GOT_IP, "ETH_GOT_IP"},
            {IP_EVENT, IP_EVENT_ETH_LOST_IP, "ETH_LOST_IP"}
        };
        
        for (int i = 0; i < sizeof(eth_handlers)/sizeof(eth_handlers[0]); i++) {
            ret = esp_event_handler_instance_register(eth_handlers[i].event_base,
                                                     eth_handlers[i].event_id,
                                                     &eth_event_handler, NULL, NULL);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "⚠️ Не удалось зарегистрировать %s: %s", 
                         eth_handlers[i].desc, esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, "  ✅ %s зарегистрирован", eth_handlers[i].desc);
            }
        }
    } else {
        ESP_LOGI(TAG, "🔌 Ethernet отключен в конфигурации");
    }
    
    /* ========== КОММЕНТИРУЕМ ТАЙМЕР (ВРЕМЕННО) ========== */
    /*
    // Создание таймера для логирования статуса (каждые 10 секунд)
    s_log_timer = xTimerCreate("NetStatusLog", 
                              pdMS_TO_TICKS(10000),
                              pdTRUE, 
                              (void*)0, 
                              log_timer_callback);
    if (s_log_timer != NULL) {
        xTimerStart(s_log_timer, 0);
        ESP_LOGI(TAG, "⏱️ Таймер логирования статуса запущен (10 сек)");
    }
    */
    /* ========== КОНЕЦ КОММЕНТИРОВАНИЯ ТАЙМЕРА ========== */
    
    // Настройка IP форвардинга если нужно
    if (g_config.ip_forwarding) {
        ESP_LOGI(TAG, "🔄 IP форвардинг включен (оба интерфейса активны)");
    }
    
    s_network_initialized = true;
    ESP_LOGI(TAG, "✅ Network manager успешно инициализирован");
    ESP_LOGI(TAG, LOG_SEPARATOR);
    
    return ESP_OK;
}

esp_err_t network_manager_start(void)
{
    if (!s_network_initialized) {
        ESP_LOGE(TAG, "❌ Network manager не инициализирован");
        return ESP_ERR_INVALID_STATE;
    }
    
    log_event("NETWORK_MANAGER_START", "Запуск сетевых подключений");
    
    esp_err_t ret = ESP_OK;
    esp_err_t wifi_ret = ESP_OK;
    esp_err_t eth_ret = ESP_OK;
    
    // Запуск Wi-Fi если включен
    if (g_config.wifi.enable) {
        ESP_LOGI(TAG, "📶 Запуск Wi-Fi подключения к: %s", g_config.wifi.ssid);
        wifi_ret = wifi_connect();
        
        if (wifi_ret == ESP_OK) {
            ESP_LOGI(TAG, "  ✅ Wi-Fi подключение инициировано");
        } else if (wifi_ret == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "  ⚠️ Wi-Fi отключен в конфигурации");
        } else {
            ESP_LOGE(TAG, "  ❌ Ошибка подключения Wi-Fi: %s", esp_err_to_name(wifi_ret));
        }
    }
    
    // Запуск Ethernet если включен
    if (g_config.eth.enable) {
        ESP_LOGI(TAG, "🔌 Запуск Ethernet подключения...");
        eth_ret = ethernet_connect();
        
        if (eth_ret == ESP_OK) {
            ESP_LOGI(TAG, "  ✅ Ethernet подключение инициировано");
        } else if (eth_ret == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "  ⚠️ Ethernet отключен в конфигурации");
        } else {
            ESP_LOGE(TAG, "  ❌ Ошибка подключения Ethernet: %s", esp_err_to_name(eth_ret));
        }
    }
    
    // Логирование итогов
    ESP_LOGI(TAG, "📊 ИТОГИ ЗАПУСКА:");
    ESP_LOGI(TAG, "  Wi-Fi: %s", 
             wifi_ret == ESP_OK ? "✅ УСПЕХ" : 
             wifi_ret == ESP_ERR_NOT_SUPPORTED ? "⚠️ ОТКЛЮЧЕН" : "❌ ОШИБКА");
    ESP_LOGI(TAG, "  Ethernet: %s", 
             eth_ret == ESP_OK ? "✅ УСПЕХ" : 
             eth_ret == ESP_ERR_NOT_SUPPORTED ? "⚠️ ОТКЛЮЧЕН" : "❌ ОШИБКА");
    
    // Возвращаем ошибку только если оба не работают и должны работать
    if (g_config.wifi.enable && g_config.eth.enable) {
        if (wifi_ret != ESP_OK && eth_ret != ESP_OK) {
            ret = ESP_FAIL;
        }
    } else if (g_config.wifi.enable && wifi_ret != ESP_OK) {
        ret = ESP_FAIL;
    } else if (g_config.eth.enable && eth_ret != ESP_OK) {
        ret = ESP_FAIL;
    }
    
    log_event("NETWORK_CONNECTIONS_STARTED", 
              ret == ESP_OK ? "✅ Успешно" : "⚠️ С ошибками");
    
    return ret;
}

esp_err_t network_manager_stop(void)
{
    if (!s_network_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    log_event("NETWORK_MANAGER_STOP", "Остановка сетевых подключений");
    
    /* ========== КОММЕНТИРУЕМ ОСТАНОВКУ ТАЙМЕРА ========== */
    /*
    // Остановка таймера логирования (если он был создан)
    if (s_log_timer != NULL) {
        xTimerStop(s_log_timer, 0);
        xTimerDelete(s_log_timer, 0);
        s_log_timer = NULL;
        ESP_LOGI(TAG, "⏱️ Таймер логирования остановлен");
    }
    */
    /* ========== КОНЕЦ КОММЕНТИРОВАНИЯ ========== */
    
    // Остановка Wi-Fi
    if (g_config.wifi.enable) {
        ESP_LOGI(TAG, "📶 Остановка Wi-Fi...");
        esp_err_t ret = wifi_disconnect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "⚠️ Wi-Fi отключение вернуло: %s", esp_err_to_name(ret));
        }
        s_wifi_connected = false;
        ESP_LOGI(TAG, "✅ Wi-Fi остановлен");
    }
    
    // Остановка Ethernet
    if (g_config.eth.enable) {
        ESP_LOGI(TAG, "🔌 Остановка Ethernet...");
        esp_err_t ret = ethernet_disconnect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "⚠️ Ethernet отключение вернуло: %s", esp_err_to_name(ret));
        }
        s_eth_connected = false;
        ESP_LOGI(TAG, "✅ Ethernet остановлен");
    }
    
    // Удаление групп событий
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    if (s_eth_event_group != NULL) {
        vEventGroupDelete(s_eth_event_group);
        s_eth_event_group = NULL;
    }
    ESP_LOGI(TAG, "✅ Группы событий удалены");
    
    s_network_initialized = false;
    
    log_event("NETWORK_CONNECTIONS_STOPPED", "Все подключения остановлены");
    
    return ESP_OK;
}

esp_netif_t *network_manager_get_active_netif(void)
{
    // Возвращаем первый подключенный интерфейс
    // В новой архитектуре оба работают одновременно, 
    // но для совместимости возвращаем Wi-Fi если он подключен
    if (s_wifi_connected) {
        ESP_LOGD(TAG, "Активный интерфейс: Wi-Fi");
        return get_wifi_netif();
    } else if (s_eth_connected) {
        ESP_LOGD(TAG, "Активный интерфейс: Ethernet");
        return get_ethernet_netif();
    }
    ESP_LOGD(TAG, "Нет активных интерфейсов");
    return NULL;
}

esp_netif_t *network_manager_get_wifi_netif(void)
{
    ESP_LOGD(TAG, "Получение Wi-Fi netif");
    return get_wifi_netif();
}

esp_netif_t *network_manager_get_eth_netif(void)
{
    ESP_LOGD(TAG, "Получение Ethernet netif");
    return get_ethernet_netif();
}

bool network_manager_wifi_is_connected(void)
{
    bool connected = s_wifi_connected;
    ESP_LOGD(TAG, "Wi-Fi подключен: %s", connected ? "ДА" : "НЕТ");
    return connected;
}

bool network_manager_eth_is_connected(void)
{
    bool connected = s_eth_connected;
    ESP_LOGD(TAG, "Ethernet подключен: %s", connected ? "ДА" : "НЕТ");
    return connected;
}

bool network_manager_is_any_connected(void)
{
    bool connected = s_wifi_connected || s_eth_connected;
    ESP_LOGD(TAG, "Любое подключение: %s", connected ? "ДА" : "НЕТ");
    return connected;
}

void network_manager_set_state_callback(network_state_callback_t callback)
{
    s_state_callback = callback;
    ESP_LOGI(TAG, "✅ Callback состояния сети установлен");
}