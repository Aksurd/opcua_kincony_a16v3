/* pcf8574.c - Original work (MIT). See project LICENSE and main file for details. */

#include "pcf8574.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PCF8574";

static bool i2c_initialized = false;
static i2c_port_t current_i2c_port = I2C_NUM_0;

/**
 * @brief Initialize the I2C bus for PCF8574 communication.
 * 
 * This function configures and installs the I2C driver according to the provided
 * configuration. It ensures that initialization happens only once per I2C port.
 * 
 * @param config Pointer to the pcf8574_config_t structure containing I2C parameters
 *               (port, SDA pin, SCL pin, clock speed).
 * @return true if I2C initialization succeeded or was already initialized.
 * @return false if configuration is invalid or I2C driver installation fails.
 */
bool pcf8574_i2c_init(const pcf8574_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return false;
    }
    
    if (i2c_initialized && current_i2c_port == config->i2c_port) {
        return true; // Already initialized
    }
    
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = config->clk_speed,
    };
    
    esp_err_t err = i2c_param_config(config->i2c_port, &i2c_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return false;
    }
    
    err = i2c_driver_install(config->i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }
    
    i2c_initialized = true;
    current_i2c_port = config->i2c_port;
    ESP_LOGI(TAG, "I2C initialized on port %d, SDA=%d, SCL=%d, speed=%lu",
             config->i2c_port, config->sda_pin, config->scl_pin, config->clk_speed);
    
    return true;
}

/**
 * @brief Initialize a PCF8574 device structure.
 * 
 * This function initializes the device descriptor with the provided I2C address
 * and port number. It does not perform any communication with the device.
 * 
 * @param dev Pointer to the pcf8574_dev_t structure to initialize.
 * @param address I2C address of the PCF8574 device (7-bit format).
 * @param i2c_port I2C port number to use for communication.
 */
void pcf8574_init(pcf8574_dev_t *dev, uint8_t address, i2c_port_t i2c_port) {
    if (dev == NULL) {
        ESP_LOGE(TAG, "Device descriptor is NULL");
        return;
    }
    
    dev->address = address;
    dev->i2c_port = i2c_port;
    
    ESP_LOGI(TAG, "PCF8574 device initialized at address 0x%02X on port %d", 
             address, i2c_port);
}

/**
 * @brief Read a byte from the PCF8574 device.
 * 
 * Performs a single-byte read operation from the PCF8574. The PCF8574 has
 * quasi-bidirectional I/O ports, so reading returns the actual pin states.
 * 
 * @param dev Pointer to the initialized PCF8574 device structure.
 * @return uint8_t The byte read from the device (0xFF on error).
 */
uint8_t pcf8574_read(const pcf8574_dev_t *dev) {
    if (dev == NULL) {
        ESP_LOGE(TAG, "Device descriptor is NULL");
        return 0xFF;
    }
    
    uint8_t data = 0xFF;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->address << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read failed from 0x%02X: %s", dev->address, esp_err_to_name(ret));
        return 0xFF;
    }
    
    return data;
}

/**
 * @brief Write a byte to the PCF8574 device.
 * 
 * Performs a single-byte write operation to the PCF8574. Each bit corresponds
 * to an output pin state (1 = high, 0 = low).
 * 
 * @param dev Pointer to the initialized PCF8574 device structure.
 * @param data Byte to write to the device.
 * @return true if write operation succeeded.
 * @return false if device is NULL or I2C communication failed.
 */
bool pcf8574_write(const pcf8574_dev_t *dev, uint8_t data) {
    if (dev == NULL) {
        ESP_LOGE(TAG, "Device descriptor is NULL");
        return false;
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(dev->i2c_port, cmd, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write failed to 0x%02X: %s", dev->address, esp_err_to_name(ret));
        return false;
    }
    
    return true;
}

/**
 * @brief Set a specific output bit on the PCF8574.
 * 
 * This function reads the current state of the PCF8574, modifies only the
 * specified bit, and writes the new byte back. Useful for controlling
 * individual output pins without affecting others.
 * 
 * @param dev Pointer to the initialized PCF8574 device structure.
 * @param bit Bit position to set (0-7).
 * @param value true to set the bit (high), false to clear it (low).
 * @return true if operation succeeded.
 * @return false if parameters are invalid or communication failed.
 */
bool pcf8574_set_bit(const pcf8574_dev_t *dev, uint8_t bit, bool value) {
    if (dev == NULL || bit > 7) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }
    
    // First read the current state
    uint8_t current = pcf8574_read(dev);
    if (current == 0xFF) {
        return false; // Read error
    }
    
    // Modify the specified bit
    if (value) {
        current |= (1 << bit);   // Set bit
    } else {
        current &= ~(1 << bit);  // Clear bit
    }
    
    // Write back
    return pcf8574_write(dev, current);
}

/**
 * @brief Read a specific input bit from the PCF8574.
 * 
 * This function reads the entire byte from the PCF8574 and extracts
 * the state of the specified bit. Useful for reading individual
 * input pins.
 * 
 * @param dev Pointer to the initialized PCF8574 device structure.
 * @param bit Bit position to read (0-7).
 * @return true if the bit is set (high).
 * @return false if the bit is clear (low) or parameters are invalid.
 */
bool pcf8574_get_bit(const pcf8574_dev_t *dev, uint8_t bit) {
    if (dev == NULL || bit > 7) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }
    
    uint8_t data = pcf8574_read(dev);
    if (data == 0xFF) {
        return false; // Read error
    }
    
    return (data >> bit) & 0x01;
}