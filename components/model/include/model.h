/* model.h - Based on the opcua-esp32 project (MPL-2.0). See project LICENSE and main file. */

#ifndef MODEL_H
#define MODEL_H

#include "open62541.h"

/* ============================================================================
 * PCF8574 Addresses for KC868-A16v3
 * ============================================================================ */

/** @brief I2C address for input module 1 */
#define DIO_IN1_ADDR  0x22
/** @brief I2C address for input module 2 */
#define DIO_IN2_ADDR  0x21
/** @brief I2C address for relay/output module 1 */
#define DIO_OUT1_ADDR 0x24
/** @brief I2C address for relay/output module 2 */
#define DIO_OUT2_ADDR 0x25

/* ============================================================================
 * Discrete I/O Functions
 * ============================================================================ */

/**
 * @brief Initialize discrete I/O hardware
 * 
 * Initializes PCF8574 I/O expanders and configures GPIO pins
 * for the KC868-A16v3 controller.
 */
void discrete_io_init(void);

/**
 * @brief Read all discrete inputs from hardware
 * 
 * Direct hardware read of all 16 discrete input channels.
 * 
 * @return uint16_t Current state of discrete inputs (16 bits)
 */
uint16_t read_discrete_inputs(void);

/**
 * @brief Write discrete outputs to hardware
 * 
 * Direct hardware write to all 16 discrete output channels.
 * 
 * @param outputs Value to write to outputs (16 bits)
 */
void write_discrete_outputs(uint16_t outputs);

/**
 * @brief Get current outputs state
 * 
 * Returns the last written value to discrete outputs.
 * 
 * @return uint16_t Current state of discrete outputs (16 bits)
 */
uint16_t get_current_outputs(void);

/**
 * @brief OPC UA read callback for discrete inputs
 * 
 * Called by OPC UA server when discrete inputs are read by a client.
 * 
 * @param server OPC UA server instance
 * @param sessionId Client session ID
 * @param sessionContext Session context (not used)
 * @param nodeId Node ID being read
 * @param nodeContext Node context (not used)
 * @param sourceTimeStamp Whether to include source timestamp
 * @param range Data range (not used)
 * @param dataValue Pointer to store read data
 * @return UA_StatusCode Status of read operation
 */
UA_StatusCode
readDiscreteInputs(UA_Server *server,
                  const UA_NodeId *sessionId, void *sessionContext,
                  const UA_NodeId *nodeId, void *nodeContext,
                  UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                  UA_DataValue *dataValue);

/**
 * @brief OPC UA read callback for discrete outputs
 * 
 * Called by OPC UA server when discrete outputs are read by a client.
 * 
 * @param server OPC UA server instance
 * @param sessionId Client session ID
 * @param sessionContext Session context (not used)
 * @param nodeId Node ID being read
 * @param nodeContext Node context (not used)
 * @param sourceTimeStamp Whether to include source timestamp
 * @param range Data range (not used)
 * @param dataValue Pointer to store read data
 * @return UA_StatusCode Status of read operation
 */
UA_StatusCode
readDiscreteOutputs(UA_Server *server,
                   const UA_NodeId *sessionId, void *sessionContext,
                   const UA_NodeId *nodeId, void *nodeContext,
                   UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                   UA_DataValue *dataValue);

/**
 * @brief OPC UA write callback for discrete outputs
 * 
 * Called by OPC UA server when discrete outputs are written by a client.
 * 
 * @param server OPC UA server instance
 * @param sessionId Client session ID
 * @param sessionContext Session context (not used)
 * @param nodeId Node ID being written
 * @param nodeContext Node context (not used)
 * @param range Data range (not used)
 * @param data Data value to write
 * @return UA_StatusCode Status of write operation
 */
UA_StatusCode
writeDiscreteOutputs(UA_Server *server,
                    const UA_NodeId *sessionId, void *sessionContext,
                    const UA_NodeId *nodeId, void *nodeContext,
                    const UA_NumericRange *range, const UA_DataValue *data);

/**
 * @brief Add discrete I/O variables to OPC UA server
 * 
 * Creates OPC UA nodes for discrete inputs and outputs in the server
 * address space.
 * 
 * @param server OPC UA server instance
 */
void addDiscreteIOVariables(UA_Server *server);

/**
 * @brief Model initialization task
 * 
 * Task that initializes the model hardware and starts I/O polling.
 */
void model_init_task(void);

/* ============================================================================
 * Fast Functions for OPC UA (cache-based)
 * ============================================================================ */

/**
 * @brief Read discrete inputs from cache (fast)
 * 
 * Reads discrete inputs from cache without accessing hardware.
 * 
 * @return uint16_t Cached discrete input value
 */
uint16_t read_discrete_inputs_fast(void);

/**
 * @brief Read discrete outputs from cache (fast)
 * 
 * Reads discrete outputs from cache without accessing hardware.
 * 
 * @return uint16_t Cached discrete output value
 */
uint16_t read_discrete_outputs_fast(void);

/* ============================================================================
 * Slow Functions for I/O Polling (hardware access)
 * ============================================================================ */

/**
 * @brief Read discrete inputs from hardware (slow)
 * 
 * Direct hardware access to discrete inputs. Used by polling task.
 * 
 * @return uint16_t Current discrete input value from hardware
 */
uint16_t read_discrete_inputs_slow(void);

/**
 * @brief Write discrete outputs to hardware (slow)
 * 
 * Direct hardware access to discrete outputs. Used by polling task.
 * 
 * @param outputs Value to write to outputs
 */
void write_discrete_outputs_slow(uint16_t outputs);

/* ============================================================================
 * Diagnostic Tags for Performance Measurement
 * ============================================================================ */

/**
 * @brief Get diagnostic counter value
 * 
 * Returns a counter that increments on each OPC UA read operation.
 * 
 * @return uint16_t Diagnostic counter value
 */
uint16_t get_diagnostic_counter(void);

/**
 * @brief Get loopback input value
 * 
 * Returns the current loopback input value (for testing).
 * 
 * @return uint16_t Loopback input value
 */
uint16_t get_loopback_input(void);

/**
 * @brief Set loopback input value
 * 
 * Sets a loopback input value (for testing and diagnostics).
 * 
 * @param val Value to set
 */
void set_loopback_input(uint16_t val);

/**
 * @brief Get loopback output value
 * 
 * Returns the current loopback output value (for testing).
 * 
 * @return uint16_t Loopback output value
 */
uint16_t get_loopback_output(void);

/* ============================================================================
 * Diagnostic OPC UA Functions
 * ============================================================================ */

/**
 * @brief OPC UA read callback for diagnostic counter
 * 
 * @param server OPC UA server instance
 * @param sessionId Client session ID
 * @param sessionContext Session context (not used)
 * @param nodeId Node ID being read
 * @param nodeContext Node context (not used)
 * @param sourceTimeStamp Whether to include source timestamp
 * @param range Data range (not used)
 * @param dataValue Pointer to store read data
 * @return UA_StatusCode Status of read operation
 */
UA_StatusCode readDiagnosticCounter(UA_Server *server,
                                   const UA_NodeId *sessionId, void *sessionContext,
                                   const UA_NodeId *nodeId, void *nodeContext,
                                   UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                                   UA_DataValue *dataValue);

/**
 * @brief OPC UA read callback for loopback input
 * 
 * @param server OPC UA server instance
 * @param sessionId Client session ID
 * @param sessionContext Session context (not used)
 * @param nodeId Node ID being read
 * @param nodeContext Node context (not used)
 * @param sourceTimeStamp Whether to include source timestamp
 * @param range Data range (not used)
 * @param dataValue Pointer to store read data
 * @return UA_StatusCode Status of read operation
 */
UA_StatusCode readLoopbackInput(UA_Server *server,
                               const UA_NodeId *sessionId, void *sessionContext,
                               const UA_NodeId *nodeId, void *nodeContext,
                               UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                               UA_DataValue *dataValue);

/**
 * @brief OPC UA write callback for loopback input
 * 
 * @param server OPC UA server instance
 * @param sessionId Client session ID
 * @param sessionContext Session context (not used)
 * @param nodeId Node ID being written
 * @param nodeContext Node context (not used)
 * @param range Data range (not used)
 * @param data Data value to write
 * @return UA_StatusCode Status of write operation
 */
UA_StatusCode writeLoopbackInput(UA_Server *server,
                                const UA_NodeId *sessionId, void *sessionContext,
                                const UA_NodeId *nodeId, void *nodeContext,
                                const UA_NumericRange *range, const UA_DataValue *data);

/**
 * @brief OPC UA read callback for loopback output
 * 
 * @param server OPC UA server instance
 * @param sessionId Client session ID
 * @param sessionContext Session context (not used)
 * @param nodeId Node ID being read
 * @param nodeContext Node context (not used)
 * @param sourceTimeStamp Whether to include source timestamp
 * @param range Data range (not used)
 * @param dataValue Pointer to store read data
 * @return UA_StatusCode Status of read operation
 */
UA_StatusCode readLoopbackOutput(UA_Server *server,
                                const UA_NodeId *sessionId, void *sessionContext,
                                const UA_NodeId *nodeId, void *nodeContext,
                                UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                                UA_DataValue *dataValue);

#endif /* MODEL_H */

/* ============================================================================
 * ADC Functions
 * ============================================================================ */

/** @brief ADC channel 1 configuration (GPIO4 - ANALOG_A1) */
#define OUR_ADC_CHANNEL_1     ADC_CHANNEL_3
/** @brief ADC channel 2 configuration (GPIO6 - ANALOG_A2) */
#define OUR_ADC_CHANNEL_2     ADC_CHANNEL_5
/** @brief ADC channel 3 configuration (GPIO7 - ANALOG_A3) */
#define OUR_ADC_CHANNEL_3     ADC_CHANNEL_6
/** @brief ADC channel 4 configuration (GPIO5 - ANALOG_A4) */
#define OUR_ADC_CHANNEL_4     ADC_CHANNEL_4

/** @brief Number of ADC channels available */
#define NUM_ADC_CHANNELS  4

/**
 * @brief Initialize ADC hardware
 * 
 * Configures ADC channels and calibration for analog inputs.
 */
void adc_init(void);

/**
 * @brief Read ADC channel from hardware (slow)
 * 
 * Direct hardware read of ADC channel. Used by polling task.
 * 
 * @param channel ADC channel number (0-3)
 * @return uint16_t Raw ADC value (0-4095)
 */
uint16_t read_adc_channel_slow(uint8_t channel);

/**
 * @brief Update all ADC channels from hardware (slow)
 * 
 * Reads all ADC channels and updates the cache.
 */
void update_all_adc_channels_slow(void);

/**
 * @brief Read ADC channel from cache (fast)
 * 
 * Reads ADC channel value from cache without hardware access.
 * 
 * @param channel ADC channel number (0-3)
 * @return uint16_t Cached ADC value
 */
uint16_t read_adc_channel_fast(uint8_t channel);

/**
 * @brief Get pointer to all ADC channel values (fast)
 * 
 * Returns pointer to array of all ADC channel values from cache.
 * 
 * @return uint16_t* Pointer to ADC values array
 */
uint16_t* get_all_adc_channels_fast(void);

/**
 * @brief OPC UA read callback for ADC channel
 * 
 * Called by OPC UA server when ADC channel is read by a client.
 * 
 * @param server OPC UA server instance
 * @param sessionId Client session ID
 * @param sessionContext Session context (not used)
 * @param nodeId Node ID being read
 * @param nodeContext Node context (not used)
 * @param sourceTimeStamp Whether to include source timestamp
 * @param range Data range (not used)
 * @param dataValue Pointer to store read data
 * @return UA_StatusCode Status of read operation
 */
UA_StatusCode readAdcChannel(UA_Server *server,
                           const UA_NodeId *sessionId, void *sessionContext,
                           const UA_NodeId *nodeId, void *nodeContext,
                           UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                           UA_DataValue *dataValue);

/**
 * @brief Add ADC variables to OPC UA server
 * 
 * Creates OPC UA nodes for ADC channels in the server address space.
 * 
 * @param server OPC UA server instance
 */
void addAdcVariables(UA_Server *server);