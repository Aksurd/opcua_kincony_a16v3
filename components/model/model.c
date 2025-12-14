/* model.c - Based on the opcua-esp32 project (MPL-2.0). See project LICENSE and main file. */

#include "open62541.h"
#include "model.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "io_cache.h"
#include "pcf8574.h"
#include "esp_log.h"

static const char *TAG = "model";

/* ============================================================================
 * DISCRETE I/O FUNCTIONS
 * ============================================================================ */

/** PCF8574 device descriptors */
static pcf8574_dev_t dio_in1, dio_in2, dio_out1, dio_out2;
static bool dio_initialized = false;

/**
 * @brief Initialize discrete I/O hardware
 * 
 * This function initializes the PCF8574 I/O expanders for the KC868-A16v3
 * controller. It configures I2C communication and sets all outputs to a
 * safe state (off).
 */
void discrete_io_init(void) {
    if (dio_initialized) {
        return;
    }
    
    // I2C configuration
    pcf8574_config_t i2c_config = {
        .i2c_port = I2C_NUM_0,
        .sda_pin = 9,
        .scl_pin = 10,
        .clk_speed = 400000
    };
    
    if (!pcf8574_i2c_init(&i2c_config)) {
        ESP_LOGE(TAG, "Failed to initialize I2C for discrete I/O");
        return;
    }
    
    // Initialize devices
    pcf8574_init(&dio_in1, DIO_IN1_ADDR, I2C_NUM_0);
    pcf8574_init(&dio_in2, DIO_IN2_ADDR, I2C_NUM_0);
    pcf8574_init(&dio_out1, DIO_OUT1_ADDR, I2C_NUM_0);
    pcf8574_init(&dio_out2, DIO_OUT2_ADDR, I2C_NUM_0);
    
    // Initialize outputs to safe state (all off)
    pcf8574_write(&dio_out1, 0xFF); // All bits = 1 (off)
    pcf8574_write(&dio_out2, 0xFF);
    
    dio_initialized = true;
    ESP_LOGI(TAG, "Discrete I/O initialized");
}

/**
 * @brief Read 16 discrete inputs from hardware (slow function for cache update)
 * 
 * This function performs direct hardware read of all 16 discrete input channels.
 * It uses lazy initialization - hardware is initialized on first call.
 * 
 * @return uint16_t Current state of discrete inputs (16 bits)
 */
uint16_t read_discrete_inputs_slow(void) {
    // Lazy initialization on first call
    if (!dio_initialized) {
        ESP_LOGI(TAG, "First call to discrete I/O - initializing...");
        discrete_io_init();
        if (!dio_initialized) {
            ESP_LOGE(TAG, "Failed to initialize discrete I/O");
            return 0xFFFF;
        }
    }
    
    uint8_t in1 = pcf8574_read(&dio_in1);
    uint8_t in2 = pcf8574_read(&dio_in2);
    
    // Invert: PCF8574: 0=signal present, 1=no signal -> make 1=signal present
    in1 = ~in1;
    in2 = ~in2;
    
    uint16_t inputs = ((uint16_t)in2 << 8) | in1;
    ESP_LOGD(TAG, "Direct read inputs: 0x%04X", inputs);
    return inputs;
}

/**
 * @brief Write 16 discrete outputs to hardware
 * 
 * This function performs direct hardware write to all 16 discrete output channels.
 * It uses lazy initialization - hardware is initialized on first call.
 * 
 * @param outputs Value to write to outputs (16 bits)
 */
void write_discrete_outputs_slow(uint16_t outputs) {
    // Lazy initialization on first call
    if (!dio_initialized) {
        ESP_LOGI(TAG, "First call to discrete I/O - initializing...");
        discrete_io_init();
        if (!dio_initialized) {
            ESP_LOGE(TAG, "Failed to initialize discrete I/O");
            return;
        }
    }
    
    uint8_t out1 = outputs & 0xFF;
    uint8_t out2 = (outputs >> 8) & 0xFF;
    
    // Invert: 1 in bit = turn output on -> PCF8574: 0=turn on
    out1 = ~out1;
    out2 = ~out2;
    
    pcf8574_write(&dio_out1, out1);
    pcf8574_write(&dio_out2, out2);
    
    ESP_LOGD(TAG, "Direct write outputs: 0x%04X", outputs);
}

/* ============================================================================
 * OPC UA FUNCTIONS FOR DISCRETE I/O
 * ============================================================================ */

/**
 * @brief OPC UA read callback for discrete inputs (uses cache)
 * 
 * This function is called when OPC UA clients read the discrete inputs variable.
 * It retrieves values from the I/O cache instead of accessing hardware directly.
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
                  UA_DataValue *dataValue) {
    // Use cache instead of direct reading
    uint64_t source_ts = 0, server_ts = 0;
    UA_UInt16 inputs = io_cache_get_discrete_inputs(&source_ts, &server_ts);
    
    UA_Variant_setScalarCopy(&dataValue->value, &inputs,
                           &UA_TYPES[UA_TYPES_UINT16]);
    
    // Set timestamps if requested
    if (sourceTimeStamp && source_ts > 0) {
        dataValue->sourceTimestamp = UA_DateTime_fromUnixTime((UA_Int64)(source_ts / 1000));
    }
    
    dataValue->hasValue = true;
    ESP_LOGD(TAG, "Inputs from cache: 0x%04X (source ts: %llu)", inputs, source_ts);
    return UA_STATUSCODE_GOOD;
}

/**
 * @brief OPC UA read callback for discrete outputs (uses cache)
 * 
 * This function is called when OPC UA clients read the discrete outputs variable.
 * It retrieves values from the I/O cache instead of accessing hardware directly.
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
                   UA_DataValue *dataValue) {
    // Use cache instead of direct reading
    uint64_t source_ts = 0, server_ts = 0;
    UA_UInt16 outputs = io_cache_get_discrete_outputs(&source_ts, &server_ts);
    
    UA_Variant_setScalarCopy(&dataValue->value, &outputs,
                           &UA_TYPES[UA_TYPES_UINT16]);
    
    // Set timestamps if requested
    if (sourceTimeStamp && source_ts > 0) {
        dataValue->sourceTimestamp = UA_DateTime_fromUnixTime((UA_Int64)(source_ts / 1000));
    }
    
    dataValue->hasValue = true;
    ESP_LOGD(TAG, "Outputs from cache: 0x%04X (source ts: %llu)", outputs, source_ts);
    return UA_STATUSCODE_GOOD;
}

/**
 * @brief OPC UA write callback for discrete outputs
 * 
 * This function is called when OPC UA clients write to the discrete outputs variable.
 * It updates both the hardware and the cache.
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
                    const UA_NumericRange *range, const UA_DataValue *data) {
    if (data->hasValue && UA_Variant_isScalar(&data->value) &&
        data->value.type == &UA_TYPES[UA_TYPES_UINT16]) {
        UA_UInt16 outputs = *(UA_UInt16*)data->value.data;
        
        // 1. Update physical device (slow)
        write_discrete_outputs_slow((uint16_t)outputs);
        
        // 2. Update cache with current timestamp
        uint64_t timestamp_ms = (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        io_cache_update_discrete_outputs((uint16_t)outputs, timestamp_ms);
        
        ESP_LOGD(TAG, "Outputs written: 0x%04X (ts: %llu)", (uint16_t)outputs, timestamp_ms);
        return UA_STATUSCODE_GOOD;
    }
    return UA_STATUSCODE_BADTYPEMISMATCH;
}

/**
 * @brief Add discrete I/O variables to OPC UA server
 * 
 * Creates OPC UA nodes for discrete inputs and outputs in the server
 * address space. The input variable is read-only, while the output
 * variable supports both read and write operations.
 * 
 * @param server OPC UA server instance
 */
void
addDiscreteIOVariables(UA_Server *server) {
    // 1. Variable for reading inputs (read-only)
    UA_VariableAttributes inputAttr = UA_VariableAttributes_default;
    inputAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Discrete Inputs");
    inputAttr.description = UA_LOCALIZEDTEXT("en-US", "16 discrete inputs with caching");
    inputAttr.dataType = UA_TYPES[UA_TYPES_UINT16].typeId;
    inputAttr.accessLevel = UA_ACCESSLEVELMASK_READ;
    
    UA_DataSource inputDataSource;
    inputDataSource.read = readDiscreteInputs;
    inputDataSource.write = NULL;
    
    UA_NodeId inputNodeId = UA_NODEID_STRING(1, "discrete_inputs");
    UA_QualifiedName inputName = UA_QUALIFIEDNAME(1, "Discrete Inputs");
    UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_NodeId variableTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
    
    UA_Server_addDataSourceVariableNode(server, inputNodeId, parentNodeId,
                                        parentReferenceNodeId, inputName,
                                        variableTypeNodeId, inputAttr,
                                        inputDataSource, NULL, NULL);
    
    // 2. Variable for outputs (read/write)
    UA_VariableAttributes outputAttr = UA_VariableAttributes_default;
    outputAttr.displayName = UA_LOCALIZEDTEXT("en-US", "Discrete Outputs");
    outputAttr.description = UA_LOCALIZEDTEXT("en-US", "16 discrete outputs with caching");
    outputAttr.dataType = UA_TYPES[UA_TYPES_UINT16].typeId;
    outputAttr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    
    UA_DataSource outputDataSource;
    outputDataSource.read = readDiscreteOutputs;
    outputDataSource.write = writeDiscreteOutputs;
    
    UA_NodeId outputNodeId = UA_NODEID_STRING(1, "discrete_outputs");
    UA_QualifiedName outputName = UA_QUALIFIEDNAME(1, "Discrete Outputs");
    
    UA_Server_addDataSourceVariableNode(server, outputNodeId, parentNodeId,
                                        parentReferenceNodeId, outputName,
                                        variableTypeNodeId, outputAttr,
                                        outputDataSource, NULL, NULL);
    
    ESP_LOGI(TAG, "Discrete I/O variables added to OPC UA server (with caching)");
}

/* ============================================================================
 * MAIN INIT FUNCTION
 * ============================================================================ */

/**
 * @brief Model initialization task
 * 
 * This function initializes all model hardware components including
 * discrete I/O. It should be called during system startup.
 */
void model_init_task(void) {
    // Initialize discrete I/O
    discrete_io_init();
    
    ESP_LOGI(TAG, "Model initialized with Discrete I/O");
}

/* ============================================================================
 * SIMPLE FAST FUNCTIONS (cache-based)
 * ============================================================================ */

/**
 * @brief Read discrete inputs from cache (fast)
 * 
 * @return uint16_t Cached discrete input value
 */
uint16_t read_discrete_inputs_fast(void) {
    return io_cache_get_discrete_inputs(NULL, NULL);
}

/**
 * @brief Read discrete outputs from cache (fast)
 * 
 * @return uint16_t Cached discrete output value
 */
uint16_t read_discrete_outputs_fast(void) {
    return io_cache_get_discrete_outputs(NULL, NULL);
}

/* ============================================================================
 * DIAGNOSTIC TAGS FOR PERFORMANCE MEASUREMENT
 * ============================================================================ */

static uint16_t diagnostic_counter = 0;
static uint16_t loopback_input = 0;
static uint16_t loopback_output = 0;

/**
 * @brief Get diagnostic counter value
 * 
 * @return uint16_t Diagnostic counter value (increments on each call)
 */
uint16_t get_diagnostic_counter(void) {
    diagnostic_counter++;
    return diagnostic_counter;
}

/**
 * @brief Get loopback input value
 * 
 * @return uint16_t Current loopback input value
 */
uint16_t get_loopback_input(void) {
    return loopback_input;
}

/**
 * @brief Set loopback input value
 * 
 * @param val Value to set for loopback
 */
void set_loopback_input(uint16_t val) {
    loopback_input = val;
    loopback_output = val;  /* Instant loopback */
}

/**
 * @brief Get loopback output value
 * 
 * @return uint16_t Current loopback output value
 */
uint16_t get_loopback_output(void) {
    return loopback_output;
}

/* ============================================================================
 * OPC UA CALLBACKS FOR DIAGNOSTIC TAGS
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
UA_StatusCode
readDiagnosticCounter(UA_Server *server,
                     const UA_NodeId *sessionId, void *sessionContext,
                     const UA_NodeId *nodeId, void *nodeContext,
                     UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                     UA_DataValue *dataValue) {
    diagnostic_counter++;
    UA_UInt16 counter = diagnostic_counter;
    UA_Variant_setScalarCopy(&dataValue->value, &counter,
                           &UA_TYPES[UA_TYPES_UINT16]);
    dataValue->hasValue = true;
    dataValue->sourceTimestamp = UA_DateTime_now();
    return UA_STATUSCODE_GOOD;
}

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
UA_StatusCode
readLoopbackInput(UA_Server *server,
                  const UA_NodeId *sessionId, void *sessionContext,
                  const UA_NodeId *nodeId, void *nodeContext,
                  UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                  UA_DataValue *dataValue) {
    UA_UInt16 value = loopback_input;
    UA_Variant_setScalarCopy(&dataValue->value, &value,
                           &UA_TYPES[UA_TYPES_UINT16]);
    dataValue->hasValue = true;
    dataValue->sourceTimestamp = UA_DateTime_now();
    return UA_STATUSCODE_GOOD;
}

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
UA_StatusCode
writeLoopbackInput(UA_Server *server,
                   const UA_NodeId *sessionId, void *sessionContext,
                   const UA_NodeId *nodeId, void *nodeContext,
                   const UA_NumericRange *range, const UA_DataValue *data) {
    if (data->hasValue && UA_Variant_isScalar(&data->value) &&
        data->value.type == &UA_TYPES[UA_TYPES_UINT16]) {
        UA_UInt16 value = *(UA_UInt16*)data->value.data;
        loopback_input = (uint16_t)value;
        loopback_output = (uint16_t)value;  /* Instant loopback */
        return UA_STATUSCODE_GOOD;
    }
    return UA_STATUSCODE_BADTYPEMISMATCH;
}

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
UA_StatusCode
readLoopbackOutput(UA_Server *server,
                   const UA_NodeId *sessionId, void *sessionContext,
                   const UA_NodeId *nodeId, void *nodeContext,
                   UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                   UA_DataValue *dataValue) {
    UA_UInt16 value = loopback_output;
    UA_Variant_setScalarCopy(&dataValue->value, &value,
                           &UA_TYPES[UA_TYPES_UINT16]);
    dataValue->hasValue = true;
    dataValue->sourceTimestamp = UA_DateTime_now();
    return UA_STATUSCODE_GOOD;
}

/* ============================================================================
 * ADC FUNCTIONS
 * ============================================================================ */

static uint16_t adc_cache[NUM_ADC_CHANNELS] = {0};
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static bool adc_initialized = false;
static uint64_t adc_timestamps_ms[NUM_ADC_CHANNELS] = {0};
static uint64_t adc_server_timestamps_ms[NUM_ADC_CHANNELS] = {0};

/**
 * @brief Initialize ADC hardware
 * 
 * Configures the ESP32 ADC unit and 4 analog input channels for
 * the KC868-A16v3 controller.
 */
void adc_init(void) {
    if (adc1_handle != NULL) {
        return;
    }
    
    // ADC unit configuration
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));
    
    // Channel configuration
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    
    // Configure 4 channels
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, OUR_ADC_CHANNEL_1, &config)); // GPIO4
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, OUR_ADC_CHANNEL_2, &config)); // GPIO6  
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, OUR_ADC_CHANNEL_3, &config)); // GPIO7
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, OUR_ADC_CHANNEL_4, &config)); // GPIO5
    
    adc_initialized = true;
    ESP_LOGI(TAG, "ADC initialized with oneshot driver (4 channels)");
}

/**
 * @brief Read ADC channel from hardware (slow)
 * 
 * Performs direct hardware read of a specific ADC channel.
 * 
 * @param channel ADC channel number (0-3)
 * @return uint16_t Raw ADC value (0-4095)
 */
uint16_t read_adc_channel_slow(uint8_t channel) {
    if (adc1_handle == NULL || channel >= NUM_ADC_CHANNELS) {
        return 0;
    }
    
    adc_channel_t channel_id;
    switch(channel) {
        case 0: channel_id = OUR_ADC_CHANNEL_1; break;  // GPIO4
        case 1: channel_id = OUR_ADC_CHANNEL_2; break;  // GPIO6
        case 2: channel_id = OUR_ADC_CHANNEL_3; break;  // GPIO7
        case 3: channel_id = OUR_ADC_CHANNEL_4; break;  // GPIO5
        default: return 0;
    }
    
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, channel_id, &raw));
    
    // Return raw value (0-4095)
    return (uint16_t)raw;
}

/**
 * @brief Update all ADC channels from hardware
 * 
 * Reads all ADC channels, updates the local cache and the global I/O cache.
 * Used by the polling task to refresh ADC values.
 */
void update_all_adc_channels_slow(void) {
    if (adc1_handle == NULL) {
        adc_init();
        if (adc1_handle == NULL) {
            return;
        }
    }
    
    uint64_t timestamp = (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    
    for (int i = 0; i < NUM_ADC_CHANNELS; i++) {
        uint16_t value = read_adc_channel_slow(i);
        adc_cache[i] = value;
        adc_timestamps_ms[i] = timestamp;
        adc_server_timestamps_ms[i] = timestamp;
        
        // Also update global cache
        io_cache_update_adc_channel(i, (float)value, timestamp);
    }
}

/**
 * @brief Read ADC channel from cache (fast)
 * 
 * @param channel ADC channel number (0-3)
 * @return uint16_t Cached ADC value
 */
uint16_t read_adc_channel_fast(uint8_t channel) {
    if (channel >= NUM_ADC_CHANNELS) {
        return 0;
    }
    return adc_cache[channel];
}

/**
 * @brief Get pointer to all ADC channel values
 * 
 * @return uint16_t* Pointer to ADC values array
 */
uint16_t* get_all_adc_channels_fast(void) {
    return adc_cache;
}

/* ============================================================================
 * OPC UA FUNCTIONS FOR ADC
 * ============================================================================ */

/**
 * @brief OPC UA read callback for ADC channel
 * 
 * Called when OPC UA clients read an ADC channel variable.
 * Returns cached ADC values with timestamp information.
 * 
 * @param server OPC UA server instance
 * @param sessionId Client session ID
 * @param sessionContext Session context (not used)
 * @param nodeId Node ID being read
 * @param nodeContext Channel number stored as pointer
 * @param sourceTimeStamp Whether to include source timestamp
 * @param range Data range (not used)
 * @param dataValue Pointer to store read data
 * @return UA_StatusCode Status of read operation
 */
UA_StatusCode readAdcChannel(UA_Server *server,
                           const UA_NodeId *sessionId, void *sessionContext,
                           const UA_NodeId *nodeId, void *nodeContext,
                           UA_Boolean sourceTimeStamp, const UA_NumericRange *range,
                           UA_DataValue *dataValue) {
    uint8_t channel = (uintptr_t)nodeContext;
    
    if (channel >= NUM_ADC_CHANNELS) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }
    
    uint16_t value = adc_cache[channel];
    UA_Variant_setScalarCopy(&dataValue->value, &value, &UA_TYPES[UA_TYPES_UINT16]);
    
    if (sourceTimeStamp && adc_timestamps_ms[channel] > 0) {
        dataValue->sourceTimestamp = UA_DateTime_fromUnixTime((UA_Int64)(adc_timestamps_ms[channel] / 1000));
    }
    
    dataValue->hasValue = true;
    return UA_STATUSCODE_GOOD;
}

/**
 * @brief Add ADC variables to OPC UA server
 * 
 * Creates OPC UA nodes for all 4 ADC channels in the server address space.
 * Each channel is represented as a separate read-only variable.
 * 
 * @param server OPC UA server instance
 */
void addAdcVariables(UA_Server *server) {
    char* channel_names[] = {"ADC1", "ADC2", "ADC3", "ADC4"};
    char* descriptions[] = {
        "Analog Input 1 (GPIO4) - Raw ADC code",
        "Analog Input 2 (GPIO6) - Raw ADC code", 
        "Analog Input 3 (GPIO7) - Raw ADC code",
        "Analog Input 4 (GPIO5) - Raw ADC code"
    };
    
    for (int i = 0; i < NUM_ADC_CHANNELS; i++) {
        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.displayName = UA_LOCALIZEDTEXT("en-US", channel_names[i]);
        attr.description = UA_LOCALIZEDTEXT("en-US", descriptions[i]);
        attr.dataType = UA_TYPES[UA_TYPES_UINT16].typeId;
        attr.accessLevel = UA_ACCESSLEVELMASK_READ;
        
        UA_DataSource dataSource;
        dataSource.read = readAdcChannel;
        dataSource.write = NULL;
        
        char nodeIdStr[32];
        snprintf(nodeIdStr, sizeof(nodeIdStr), "adc_channel_%d", i + 1);
        
        UA_NodeId nodeId = UA_NODEID_STRING(1, nodeIdStr);
        UA_QualifiedName name = UA_QUALIFIEDNAME(1, channel_names[i]);
        UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
        UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
        UA_NodeId variableTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
        
        UA_Server_addDataSourceVariableNode(server, nodeId, parentNodeId,
                                          parentReferenceNodeId, name,
                                          variableTypeNodeId, attr,
                                          dataSource, (void*)(uintptr_t)i, NULL);
    }
    
    ESP_LOGI(TAG, "ADC variables added to OPC UA server (%d channels, raw codes)", NUM_ADC_CHANNELS);
}