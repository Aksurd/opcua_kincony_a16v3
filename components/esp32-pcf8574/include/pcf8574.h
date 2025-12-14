/* pcf8574.h - Original work (MIT). See project LICENSE and main file for details. */

#ifndef PCF8574_H
#define PCF8574_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C bus configuration structure for PCF8574
 * 
 * Contains all necessary parameters to initialize the I2C master interface
 * for communication with PCF8574 devices.
 */
typedef struct {
    i2c_port_t i2c_port;      /**< I2C port number (I2C_NUM_0 or I2C_NUM_1) */
    int sda_pin;              /**< GPIO pin number for SDA line */
    int scl_pin;              /**< GPIO pin number for SCL line */
    uint32_t clk_speed;       /**< I2C clock frequency in Hz (typically 100000 or 400000) */
} pcf8574_config_t;

/**
 * @brief PCF8574 device descriptor structure
 * 
 * Represents a specific PCF8574 device on the I2C bus.
 * This structure is used in all device-specific operations.
 */
typedef struct {
    uint8_t address;          /**< 7-bit I2C device address (0x20-0x27 for PCF8574, 0x38-0x3F for PCF8574A) */
    i2c_port_t i2c_port;      /**< I2C port number associated with this device */
} pcf8574_dev_t;

/* ============================================================================
 * PUBLIC FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize the I2C bus for PCF8574 communication
 * 
 * Configures the I2C master interface with the specified parameters.
 * This function must be called before any PCF8574 device operations.
 * 
 * @param config Pointer to pcf8574_config_t structure containing I2C parameters
 * @return true if I2C bus was successfully initialized
 * @return false if initialization failed (invalid config, hardware error)
 * 
 * @note Only needs to be called once per I2C port
 * @note Enables internal pull-up resistors on SDA and SCL lines
 * @note Default I2C timeout is configured by ESP-IDF (typically 1 second)
 */
bool pcf8574_i2c_init(const pcf8574_config_t *config);

/**
 * @brief Initialize a PCF8574 device descriptor
 * 
 * Creates a device descriptor for a specific PCF8574 chip on the I2C bus.
 * This descriptor is used in all subsequent device operations.
 * 
 * @param dev Pointer to pcf8574_dev_t structure to initialize
 * @param address 7-bit I2C address of the PCF8574 device
 * @param i2c_port I2C port number (must match previously initialized port)
 * 
 * @note Does not verify I2C bus initialization or device presence
 * @note I2C bus must be initialized with pcf8574_i2c_init() first
 */
void pcf8574_init(pcf8574_dev_t *dev, uint8_t address, i2c_port_t i2c_port);

/**
 * @brief Read a byte from PCF8574 device
 * 
 * Performs a single-byte read operation from the PCF8574.
 * Returns the state of all 8 I/O pins as a single byte.
 * 
 * @param dev Pointer to initialized pcf8574_dev_t structure
 * @return uint8_t Byte representing the state of all 8 I/O pins
 *         (0xFF if read operation failed)
 * 
 * @note Bit 0 corresponds to pin P0, bit 7 corresponds to pin P7
 * @note 0 = pin LOW, 1 = pin HIGH
 * @note Uses I2C master read transaction with 10ms timeout
 */
uint8_t pcf8574_read(const pcf8574_dev_t *dev);

/**
 * @brief Write a byte to PCF8574 device
 * 
 * Performs a single-byte write operation to the PCF8574.
 * Sets the state of all 8 I/O pins simultaneously.
 * 
 * @param dev Pointer to initialized pcf8574_dev_t structure
 * @param data Byte to write (each bit corresponds to an I/O pin)
 * @return true if write operation succeeded
 * @return false if write operation failed (device not responding, I2C error)
 * 
 * @note Bit 0 corresponds to pin P0, bit 7 corresponds to pin P7
 * @note 0 = set pin LOW (strong sink), 1 = set pin HIGH (weak pull-up)
 * @note Uses I2C master write transaction with 10ms timeout
 */
bool pcf8574_write(const pcf8574_dev_t *dev, uint8_t data);

/**
 * @brief Set individual output bit on PCF8574
 * 
 * Sets or clears a specific bit (pin) on the PCF8574 without affecting
 * other pins. Implements read-modify-write pattern.
 * 
 * @param dev Pointer to initialized pcf8574_dev_t structure
 * @param bit Bit position to modify (0-7, where 0 = P0, 7 = P7)
 * @param value true to set bit HIGH, false to set bit LOW
 * @return true if operation succeeded
 * @return false if operation failed (invalid parameters, I2C error)
 * 
 * @note Performs read-modify-write to preserve other bits
 * @note Suitable for output pins
 * @note For quasi-bidirectional I/O: HIGH = weak pull-up, LOW = strong sink
 */
bool pcf8574_set_bit(const pcf8574_dev_t *dev, uint8_t bit, bool value);

/**
 * @brief Read individual input bit from PCF8574
 * 
 * Reads the state of a specific pin on the PCF8574.
 * 
 * @param dev Pointer to initialized pcf8574_dev_t structure
 * @param bit Bit position to read (0-7, where 0 = P0, 7 = P7)
 * @return true if pin is HIGH (logic 1)
 * @return false if pin is LOW (logic 0) or read operation failed
 * 
 * @note Reads entire byte and extracts specific bit
 * @note Suitable for input pins
 * @note Returns false on read error, which may be indistinguishable from LOW state
 */
bool pcf8574_get_bit(const pcf8574_dev_t *dev, uint8_t bit);

#ifdef __cplusplus
}
#endif

#endif /* PCF8574_H */

/* ============================================================================
 *                           USAGE GUIDELINES
 * ============================================================================
 * 
 * Typical Usage Sequence:
 * 
 * 1. Include this header file:
 *    #include "pcf8574.h"
 * 
 * 2. Configure I2C bus parameters:
 *    pcf8574_config_t config = {
 *        .i2c_port = I2C_NUM_0,
 *        .sda_pin = GPIO_NUM_21,
 *        .scl_pin = GPIO_NUM_22,
 *        .clk_speed = 100000
 *    };
 * 
 * 3. Initialize I2C bus:
 *    if (!pcf8574_i2c_init(&config)) {
 *        // Handle initialization error
 *    }
 * 
 * 4. Initialize device descriptor:
 *    pcf8574_dev_t my_device;
 *    pcf8574_init(&my_device, 0x20, I2C_NUM_0);
 * 
 * 5. Use device functions:
 *    // Write to all pins
 *    pcf8574_write(&my_device, 0x55);
 *    
 *    // Read from all pins
 *    uint8_t inputs = pcf8574_read(&my_device);
 *    
 *    // Control individual pin
 *    pcf8574_set_bit(&my_device, 3, true);
 *    bool state = pcf8574_get_bit(&my_device, 3);
 * 
 * ============================================================================
 *                           TECHNICAL NOTES
 * ============================================================================
 * 
 * PCF8574 Quasi-Bidirectional I/O Characteristics:
 * 
 * When a pin is written as HIGH (1):
 *   - Weak internal pull-up resistor (~100μA) is enabled
 *   - Pin can be pulled LOW by external circuit (input mode)
 *   - Not suitable for driving LEDs or other loads directly
 * 
 * When a pin is written as LOW (0):
 *   - Strong sink capability (~10mA) is enabled
 *   - Pin actively drives LOW (output mode)
 *   - Can sink current to ground
 * 
 * I2C Addressing:
 * 
 * PCF8574:  Base address 0x40 (7-bit: 0x20-0x27)
 *           Full address format: 0100 A2 A1 A0 R/W
 * 
 * PCF8574A: Base address 0x70 (7-bit: 0x38-0x3F)
 *           Full address format: 0111 A2 A1 A0 R/W
 * 
 * Address pins A2, A1, A0 must be tied to VCC or GND to set address.
 * 
 * Pin Mapping:
 * 
 *   PCF8574 Pin | I/O Pin | Data Bit
 *   ------------|---------|----------
 *   4           | P0      | Bit 0
 *   5           | P1      | Bit 1
 *   6           | P2      | Bit 2
 *   7           | P3      | Bit 3
 *   9           | P4      | Bit 4
 *   10          | P5      | Bit 5
 *   11          | P6      | Bit 6
 *   12          | P7      | Bit 7
 * 
 * Interrupt Functionality (Not implemented in this driver):
 * 
 * PCF8574 provides an interrupt output (pin 13, active LOW) that triggers
 * when any input pin changes state. This can be used to implement efficient
 * event-driven input monitoring.
 * 
 * ============================================================================
 *                           ERROR HANDLING
 * ============================================================================
 * 
 * Common Error Conditions:
 * 
 * 1. I2C Bus Errors:
 *    - ESP_ERR_INVALID_ARG: Invalid I2C port or pin configuration
 *    - ESP_ERR_NOT_FOUND: I2C driver not installed
 *    - ESP_FAIL: General I2C failure
 * 
 * 2. Device Communication Errors:
 *    - Returns 0xFF from pcf8574_read() on failure
 *    - Returns false from pcf8574_write() on failure
 *    - Check ESP-IDF logs for specific error codes
 * 
 * 3. Parameter Validation:
 *    - Functions validate NULL pointers and bit range (0-7)
 *    - Logs errors to ESP-IDF logging system with TAG "PCF8574"
 * 
 * Debugging Tips:
 * 
 * 1. Enable verbose I2C debugging in ESP-IDF:
 *    idf.py menuconfig
 *    → Component config → I2C → Enable I2C debugging
 * 
 * 2. Monitor I2C traffic with logic analyzer
 * 3. Verify power supply and pull-up resistors
 * 4. Check I2C address with I2C scanner utility
 * 
 * ============================================================================
 *                           COMPATIBILITY NOTES
 * ============================================================================
 * 
 * Compatible Devices:
 * - PCF8574: Original Texas Instruments device
 * - PCF8574A: Alternate address range variant
 * - Compatible clones from various manufacturers
 * 
 * ESP32 Compatibility:
 * - Tested with ESP32, ESP32-S2, ESP32-S3, ESP32-C3
 * - Requires ESP-IDF v4.4 or later
 * - Uses standard ESP-IDF I2C driver API
 * 
 * Multi-Device Support:
 * - Supports multiple PCF8574 devices on same I2C bus
 * - Each device must have unique I2C address
 * - No arbitration or collision handling needed
 * 
 * ============================================================================
 */