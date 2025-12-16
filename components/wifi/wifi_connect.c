#include <string.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip_addr.h"
#include "wifi_connect.h"

#define WIFI_CONNECTED_BIT BIT(0)
#define WIFI_FAIL_BIT      BIT(1)

#define WIFI_MAXIMUM_RETRY 5

static const char *TAG = "wifi_connect";
static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t *s_wifi_netif = NULL;
static int s_retry_num = 0;
static esp_event_handler_instance_t instance_any_id = NULL;
static esp_event_handler_instance_t instance_got_ip = NULL;

/* FreeRTOS event group to signal when we are connected*/
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP (%d/%d)", 
                     s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"Connection to AP failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_connect(void)
{
    // УДАЛЕНЫ: esp_netif_init() и esp_event_loop_create_default()
    // Они уже должны быть вызваны в network_manager_init()
    
    if (s_wifi_event_group != NULL) {
        ESP_LOGW(TAG, "Wi-Fi already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    s_wifi_event_group = xEventGroupCreate();
    
    // Создаем только Wi-Fi интерфейс (сетевой стек уже инициализирован)
    s_wifi_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi STA");
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        return ESP_FAIL;
    }

    // Инициализация Wi-Fi драйвера
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(ret));
        esp_netif_destroy(s_wifi_netif);
        s_wifi_netif = NULL;
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        return ret;
    }

    // Регистрируем обработчики событий (цикл событий уже создан)
    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &event_handler,
                                              NULL,
                                              &instance_any_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ret = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &event_handler,
                                              NULL,
                                              &instance_got_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Настройка Wi-Fi конфигурации
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi config: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ESP_LOGI(TAG, "Wi-Fi initialization finished. Connecting to SSID:%s",
             CONFIG_WIFI_SSID);

    // Ожидание подключения
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID:%s", CONFIG_WIFI_SSID);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", CONFIG_WIFI_SSID);
        goto cleanup;
    } else {
        ESP_LOGE(TAG, "Unexpected event");
        goto cleanup;
    }

cleanup:
    wifi_disconnect();
    return ESP_FAIL;
}

esp_err_t wifi_disconnect(void)
{
    // Отмена регистрации обработчиков событий
    if (instance_any_id != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
        instance_any_id = NULL;
    }
    if (instance_got_ip != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
        instance_got_ip = NULL;
    }

    // Остановка Wi-Fi
    esp_err_t ret = ESP_OK;
    if (esp_wifi_stop() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop Wi-Fi");
    }
    
    if (esp_wifi_deinit() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinit Wi-Fi");
    }

    // Уничтожение сетевого интерфейса
    if (s_wifi_netif != NULL) {
        esp_netif_destroy(s_wifi_netif);
        s_wifi_netif = NULL;
    }

    // Удаление группы событий
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    s_retry_num = 0;
    return ret;
}

esp_netif_t *get_wifi_netif(void)
{
    return s_wifi_netif;
}