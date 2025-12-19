/* 
 * PCF8574 I2C IO Expander Driver for ESP32
 * See project LICENSE for licensing information.
 */

#include "pcf8574.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PCF8574";

// I2C initialization
bool pcf8574_i2c_init(const pcf8574_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return false;
    }
    
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = config->clk_speed
    };
    
    esp_err_t err = i2c_param_config(config->i2c_port, &i2c_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return false;
    }
    
    err = i2c_driver_install(config->i2c_port, i2c_conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "I2C initialized on port %d, SDA=%d, SCL=%d, speed=%d", 
             config->i2c_port, config->sda_pin, config->scl_pin, config->clk_speed);
    return true;
}

// Device initialization
void pcf8574_init(pcf8574_dev_t *dev, uint8_t address, i2c_port_t i2c_port) {
    if (dev == NULL) {
        ESP_LOGE(TAG, "Device is NULL");
        return;
    }
    
    dev->i2c_port = i2c_port;
    dev->address = address;
    
    ESP_LOGI(TAG, "PCF8574 device initialized at address 0x%02X on port %d", 
             address, i2c_port);
}

// Read from PCF8574
uint8_t pcf8574_read(const pcf8574_dev_t *dev) {
    if (dev == NULL) {
        ESP_LOGE(TAG, "Device is NULL");
        return 0xFF;
    }
    
    uint8_t data = 0xFF;
    
    esp_err_t err = i2c_master_read_from_device(
        dev->i2c_port,
        dev->address,
        &data,
        sizeof(data),
        portMAX_DELAY
    );
    
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Read from 0x%02X: %s", dev->address, esp_err_to_name(err));
        return 0xFF;
    }
    
    ESP_LOGV(TAG, "Read 0x%02X from address 0x%02X", data, dev->address);
    return data;
}

// Write to PCF8574
bool pcf8574_write(const pcf8574_dev_t *dev, uint8_t data) {
    if (dev == NULL) {
        ESP_LOGE(TAG, "Device is NULL");
        return false;
    }
    
    uint8_t buffer[1] = {data};
    
    ESP_LOGV(TAG, "Writing 0x%02X to address 0x%02X", data, dev->address);
    
    // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: portMAX_DELAY вместо таймаута
    esp_err_t err = i2c_master_write_to_device(
        dev->i2c_port, 
        dev->address, 
        buffer, 
        sizeof(buffer), 
        portMAX_DELAY  // ← ЕДИНСТВЕННОЕ ИЗМЕНЕНИЕ: было pdMS_TO_TICKS(50), стало portMAX_DELAY
    );
    
    if (err == ESP_OK) {
        ESP_LOGV(TAG, "Write successful to 0x%02X", dev->address);
        return true;
    } else {
        // Логируем, но не как критическую ошибку
        ESP_LOGD(TAG, "Write to 0x%02X: %s", dev->address, esp_err_to_name(err));
        return false;
    }
}