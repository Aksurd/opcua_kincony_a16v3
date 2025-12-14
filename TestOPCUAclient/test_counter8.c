#include <open62541/client.h>
#include <open62541/client_highlevel.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>

// Function prototypes
int kbhit(void);
void print_help(const char* program_name);
void print_tag_value(const char* name, UA_Variant* value);

// Structure to store tag information and statistics
typedef struct {
    const char* name;            // Display name of the tag
    UA_NodeId nodeId;            // OPC UA NodeId
    double total_time;           // Total read time for this tag (ms)
    double min_time;             // Minimum read time (ms)
    double max_time;             // Maximum read time (ms)
    int read_count;              // Number of successful reads
    int error_count;             // Number of read errors
    const UA_DataType* data_type; // Data type of the tag
} TagInfo;

// Non-blocking keyboard check function
// Returns 1 if a key has been pressed, 0 otherwise
int kbhit(void) {
    struct termios oldt, newt;
    int ch;
    int oldf;
    
    // Save current terminal settings
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    
    // Disable canonical mode and echo
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    // Set non-blocking mode for stdin
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    
    // Try to read a character
    ch = getchar();
    
    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    
    // If we got a character, put it back and return true
    if(ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    
    return 0;
}

// Display help message
void print_help(const char* program_name) {
    printf("OPC UA HIGH-SPEED PERFORMANCE TEST CLIENT\n");
    printf("Usage: %s [OPTIONS] [SERVER_URL]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help           Show this help message\n");
    printf("  -v, --verbose        Enable verbose output\n");
    printf("  -i, --interval N     Set display interval (default: 10 cycles)\n");
    printf("  -t, --timeout N      Set connection timeout in ms (default: 500)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s opc.tcp://10.0.0.110:4840\n", program_name);
    printf("  %s -v -i 5 opc.tcp://opcua-esp32:4840\n", program_name);
    printf("  %s -t 1000 opc.tcp://10.0.0.110:4840\n", program_name);
    printf("\n");
    printf("Default server URL: opc.tcp://10.0.0.128:4840\n");
    printf("Press any key during test to stop\n");
}

// Function to display tag values with type information
void print_tag_value(const char* name, UA_Variant* value) {
    printf("%-20s = ", name);
    
    // Check if variant is empty
    if(UA_Variant_isEmpty(value)) {
        printf("[Empty]\n");
        return;
    }
    
    // Handle different data types with appropriate formatting
    if(UA_Variant_hasScalarType(value, &UA_TYPES[UA_TYPES_UINT16])) {
        printf("%u (UInt16)\n", *(UA_UInt16*)value->data);
    } else if(UA_Variant_hasScalarType(value, &UA_TYPES[UA_TYPES_UINT32])) {
        UA_UInt32 raw_val = *(UA_UInt32*)value->data;
        printf("%u (UInt32)\n", raw_val);
    } else if(UA_Variant_hasScalarType(value, &UA_TYPES[UA_TYPES_INT32])) {
        printf("%d (Int32)\n", *(UA_Int32*)value->data);
    } else if(UA_Variant_hasScalarType(value, &UA_TYPES[UA_TYPES_FLOAT])) {
        printf("%.2f (Float)\n", *(UA_Float*)value->data);
    } else if(UA_Variant_hasScalarType(value, &UA_TYPES[UA_TYPES_DOUBLE])) {
        printf("%.2f (Double)\n", *(UA_Double*)value->data);
    } else if(UA_Variant_hasScalarType(value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        printf("%s (Boolean)\n", *(UA_Boolean*)value->data ? "true" : "false");
    } else {
        // Unknown or complex type
        printf("[Type: %s]\n", value->type ? value->type->typeName : "Unknown");
    }
}

int main(int argc, char* argv[]) {
    // Default values
    char* server_url = "opc.tcp://10.0.0.128:4840";
    int verbose = 0;
    int display_interval = 10;
    int timeout_ms = 500;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interval") == 0) {
            if (i + 1 < argc) {
                display_interval = atoi(argv[++i]);
                if (display_interval <= 0) {
                    printf("Error: Interval must be positive\n");
                    return 1;
                }
            } else {
                printf("Error: Missing value for interval\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 < argc) {
                timeout_ms = atoi(argv[++i]);
                if (timeout_ms <= 0) {
                    printf("Error: Timeout must be positive\n");
                    return 1;
                }
            } else {
                printf("Error: Missing value for timeout\n");
                return 1;
            }
        } else if (argv[i][0] == '-') {
            printf("Unknown option: %s\n", argv[i]);
            printf("Use %s -h for help\n", argv[0]);
            return 1;
        } else {
            // Assume it's the server URL
            server_url = argv[i];
        }
    }
    
    if (verbose) {
        printf("Verbose mode enabled\n");
        printf("Display interval: every %d cycles\n", display_interval);
        printf("Connection timeout: %d ms\n", timeout_ms);
    }
    
    printf("=============================================\n");
    printf("   OPC UA HIGH-SPEED PERFORMANCE TEST\n");
    printf("   Full system test WITH ADC channels\n");
    printf("   Press any key to stop\n");
    printf("=============================================\n\n");
    
    // ========== CLIENT INITIALIZATION ==========
    
    // Create new OPC UA client
    UA_Client *client = UA_Client_new();
    UA_ClientConfig *config = UA_Client_getConfig(client);
    config->timeout = timeout_ms;
    
    // Connect to OPC UA server
    printf("Connecting to %s...\n", server_url);
    UA_StatusCode status = UA_Client_connect(client, server_url);
    
    // Check connection status
    if(status != UA_STATUSCODE_GOOD) {
        printf("Connection failed: 0x%08X\n", status);
        UA_Client_delete(client);
        return 1;
    }
    printf("Connected!\n\n");
    
    // ========== TAG DEFINITION ==========
    
    // Initialize tags array - now 9 tags total (5 system + 4 ADC)
    TagInfo tags[9];
    
    // OPC UA server node names (as they appear in the server)
    const char* tag_names[] = {
        "diagnostic_counter",      // System diagnostic counter
        "loopback_input",          // Loopback input for testing
        "loopback_output",         // Loopback output for testing
        "discrete_inputs",         // Discrete digital inputs
        "discrete_outputs",        // Discrete digital outputs (square wave target)
        "adc_channel_1",           // ADC channel 1
        "adc_channel_2",           // ADC channel 2
        "adc_channel_3",           // ADC channel 3
        "adc_channel_4"            // ADC channel 4
    };
    
    // Display names for better readability in output
    const char* tag_display_names[] = {
        "Diagnostic Counter",
        "Loopback Input", 
        "Loopback Output",
        "Discrete Inputs",
        "Discrete Outputs",
        "ADC Channel 1",
        "ADC Channel 2", 
        "ADC Channel 3",
        "ADC Channel 4"
    };
    
    int num_tags = 9;  // Total number of tags to test
    
    // Initialize tag structures
    for(int i = 0; i < num_tags; i++) {
        tags[i].name = tag_display_names[i];
        // Create NodeId for each tag (namespace 1, string identifier)
        tags[i].nodeId = UA_NODEID_STRING_ALLOC(1, tag_names[i]);
        tags[i].total_time = 0.0;
        tags[i].min_time = 999999.0;  // Initialize with high value
        tags[i].max_time = 0.0;
        tags[i].read_count = 0;
        tags[i].error_count = 0;
        tags[i].data_type = NULL;
    }
    
    int cycle_count = 0;  // Counter for test cycles
    UA_UInt16 word_counter = 0;  // ADDED: Word counter for loopback input
    
    // ========== TERMINAL SETUP ==========
    
    // Configure terminal for non-blocking input
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);  // Disable line buffering and echo
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    // Test information
    printf("Starting test...\n");
    printf("Generating square wave on discrete_outputs\n");
    printf("Writing word counter to loopback_input\n");
    printf("Reading all %d tags (5 system + 4 ADC channels)\n\n", num_tags);
    
    if (verbose) {
        printf("Configuration:\n");
        printf("  Display interval: %d cycles\n", display_interval);
        printf("  Timeout: %d ms\n", timeout_ms);
        printf("  Server: %s\n", server_url);
        printf("\n");
    }
    
    // ========== PERFORMANCE MEASUREMENT VARIABLES ==========
    
    struct timespec test_start, test_end;
    clock_gettime(CLOCK_MONOTONIC, &test_start);
    
    double total_cycle_time = 0.0;
    double min_cycle_time = 999999.0;
    double max_cycle_time = 0.0;
    UA_UInt16 square_state = 0;  // State for square wave generation
    
    // Display header for cycle statistics
    printf("Cycle | WordCnt | State | Time (ms)\n");
    printf("-----------------------------------\n");
    
    if (verbose) {
        printf("Debug: Reading %d tags total\n", num_tags);
    }
    
    // ========== INITIAL TAG VALUES READING ==========
    
    // Read and display initial values of all tags
    if (verbose) {
        printf("\n=== INITIAL TAG VALUES ===\n");
        for(int i = 0; i < num_tags; i++) {
            UA_Variant value;
            UA_Variant_init(&value);
            
            // Read tag value from server
            if(UA_Client_readValueAttribute(client, tags[i].nodeId, &value) == UA_STATUSCODE_GOOD) {
                print_tag_value(tags[i].name, &value);
            } else {
                printf("%s: ERROR\n", tags[i].name);
            }
            
            UA_Variant_clear(&value);
        }
        printf("\n");
    }
    
    // ========== ADC-SPECIFIC STATISTICS ==========
    
    // Separate statistics for ADC channels (indices 5-8)
    double adc_total_time = 0.0;
    double adc_min_time = 999999.0;
    double adc_max_time = 0.0;
    int adc_read_count = 0;
    int adc_error_count = 0;
    
    // ========== MAIN TEST LOOP ==========
    
    while(!kbhit()) {  // Continue until any key is pressed
        struct timespec cycle_start, cycle_end;
        clock_gettime(CLOCK_MONOTONIC, &cycle_start);
        
        word_counter++;  // ADDED: Increment word counter
        
        // ----- SQUARE WAVE GENERATION -----
        
        // Generate square wave on discrete_outputs
        UA_Variant write_val;
        UA_Variant_init(&write_val);
        UA_Variant_setScalar(&write_val, &square_state, &UA_TYPES[UA_TYPES_UINT16]);
        
        // Write square wave state to discrete_outputs (tag index 4)
        UA_Client_writeValueAttribute(client, tags[4].nodeId, &write_val);
        
        // ADDED: Write word counter to loopback_input (tag index 1)
        UA_Variant write_word;
        UA_Variant_init(&write_word);
        UA_Variant_setScalar(&write_word, &word_counter, &UA_TYPES[UA_TYPES_UINT16]);
        UA_Client_writeValueAttribute(client, tags[1].nodeId, &write_word);
        // НЕТ UA_Variant_clear(&write_word); - как в оригинале для write_val
        
        // ----- READ ALL TAGS -----
        
        // Read all 9 tags (5 system + 4 ADC)
        for(int i = 0; i < num_tags; i++) {
            struct timespec tag_start, tag_end;
            clock_gettime(CLOCK_MONOTONIC, &tag_start);
            
            UA_Variant read_val;
            UA_Variant_init(&read_val);
            
            // Read tag value from server
            UA_StatusCode read_status = UA_Client_readValueAttribute(client, tags[i].nodeId, &read_val);
            
            clock_gettime(CLOCK_MONOTONIC, &tag_end);
            
            // Calculate tag read time in milliseconds
            double tag_time_ms = (tag_end.tv_sec - tag_start.tv_sec) * 1000.0 + 
                               (tag_end.tv_nsec - tag_start.tv_nsec) / 1000000.0;
            
            // Process read result
            if(read_status == UA_STATUSCODE_GOOD) {
                // Update tag statistics
                tags[i].total_time += tag_time_ms;
                tags[i].read_count++;
                if(tag_time_ms < tags[i].min_time) tags[i].min_time = tag_time_ms;
                if(tag_time_ms > tags[i].max_time) tags[i].max_time = tag_time_ms;
                
                // Update ADC-specific statistics (tags 5-8 are ADC channels)
                if(i >= 5 && i <= 8) {
                    adc_total_time += tag_time_ms;
                    adc_read_count++;
                    if(tag_time_ms < adc_min_time) adc_min_time = tag_time_ms;
                    if(tag_time_ms > adc_max_time) adc_max_time = tag_time_ms;
                }
                
                // Store data type if not already known
                if(tags[i].data_type == NULL && read_val.type != NULL) {
                    tags[i].data_type = read_val.type;
                }
            } else {
                // Update error counters
                tags[i].error_count++;
                if(i >= 5 && i <= 8) {
                    adc_error_count++;
                }
            }
            
            // Clean up variant
            UA_Variant_clear(&read_val);
        }
        
        // ----- CYCLE TIME CALCULATION -----
        
        clock_gettime(CLOCK_MONOTONIC, &cycle_end);
        double cycle_time_ms = (cycle_end.tv_sec - cycle_start.tv_sec) * 1000.0 + 
                             (cycle_end.tv_nsec - cycle_start.tv_nsec) / 1000000.0;
        
        // Update cycle statistics
        total_cycle_time += cycle_time_ms;
        if(cycle_time_ms < min_cycle_time) min_cycle_time = cycle_time_ms;
        if(cycle_time_ms > max_cycle_time) max_cycle_time = cycle_time_ms;
        
        // Display cycle information at specified interval
        if(cycle_count % display_interval == 0) {
            printf("%5d | %7u | %5s | %9.3f\n", 
                   cycle_count, word_counter, square_state ? "HIGH" : "LOW", cycle_time_ms);
            fflush(stdout);
        }
        
        // Toggle square wave state for next cycle
        square_state = ~square_state;
        cycle_count++;
    }
    
    // ========== TEST COMPLETION ==========
    
    // Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    
    // Calculate total test time
    clock_gettime(CLOCK_MONOTONIC, &test_end);
    double total_test_time = (test_end.tv_sec - test_start.tv_sec) * 1000.0 + 
                           (test_end.tv_nsec - test_start.tv_nsec) / 1000000.0;
    
    // ========== FINAL TAG VALUES READING ==========
    
    // Read and display final values of all tags
    if (verbose) {
        printf("\n=== FINAL TAG VALUES ===\n");
        for(int i = 0; i < num_tags; i++) {
            UA_Variant value;
            UA_Variant_init(&value);
            
            if(UA_Client_readValueAttribute(client, tags[i].nodeId, &value) == UA_STATUSCODE_GOOD) {
                print_tag_value(tags[i].name, &value);
            }
            
            UA_Variant_clear(&value);
        }
        
        // ADDED: Display final word counter value
        printf("\nWord counter final value: %u\n", word_counter);
    }
    
    // ========== PERFORMANCE RESULTS ==========
    
    printf("\n=== DETAILED TAG STATISTICS ===\n");
    printf("%-20s %8s %8s %10s %10s %10s\n", 
           "TAG", "READS", "ERRORS", "AVG (ms)", "MIN (ms)", "MAX (ms)");
    printf("----------------------------------------------------------------\n");
    
    double total_all_tags_time = 0.0;
    int total_successful_reads = 0;
    int total_errors = 0;
    
    // Calculate and display statistics for each tag
    for(int i = 0; i < num_tags; i++) {
        double avg_time = tags[i].read_count > 0 ? tags[i].total_time / tags[i].read_count : 0.0;
        total_all_tags_time += tags[i].total_time;
        total_successful_reads += tags[i].read_count;
        total_errors += tags[i].error_count;
        
        printf("%-20s %8d %8d %10.3f %10.3f %10.3f\n",
               tags[i].name, 
               tags[i].read_count,
               tags[i].error_count,
               avg_time,
               tags[i].min_time,
               tags[i].max_time);
    }
    
    // Calculate ADC-specific statistics
    double adc_avg_time = adc_read_count > 0 ? adc_total_time / adc_read_count : 0.0;
    
    // ========== ADC-SPECIFIC RESULTS ==========
    
    printf("\n=== ADC CHANNELS SPECIFIC STATISTICS ===\n");
    printf("Total ADC channels:      %d\n", 4);
    printf("ADC total reads:         %d\n", adc_read_count);
    printf("ADC errors:              %d\n", adc_error_count);
    printf("ADC average read time:   %.3f ms\n", adc_avg_time);
    printf("ADC min read time:       %.3f ms\n", adc_min_time);
    printf("ADC max read time:       %.3f ms\n", adc_max_time);
    printf("ADC time jitter:         %.3f ms\n", adc_max_time - adc_min_time);
    
    // ========== PERFORMANCE SUMMARY ==========
    
    printf("\n=== PERFORMANCE SUMMARY ===\n");
    printf("Total test time:        %.3f ms\n", total_test_time);
    printf("Total cycles:           %d\n", cycle_count);
    printf("Word counter value:     %u\n", word_counter);
    printf("Average cycle time:     %.3f ms\n", total_cycle_time / cycle_count);
    printf("Min cycle time:         %.3f ms\n", min_cycle_time);
    printf("Max cycle time:         %.3f ms\n", max_cycle_time);
    printf("Cycle time jitter:      %.3f ms\n", max_cycle_time - min_cycle_time);
    printf("\n");
    printf("Total tag reads:        %d\n", total_successful_reads);
    printf("Total errors:           %d\n", total_errors);
    printf("Average per tag read:   %.3f ms\n", 
           total_successful_reads > 0 ? total_all_tags_time / total_successful_reads : 0.0);
    
    // ========== THEORETICAL THROUGHPUT CALCULATION ==========
    
    printf("\n=== THEORETICAL THROUGHPUT ===\n");
    printf("Max polling frequency:  %.1f Hz (all %d tags)\n", 
           1000.0 / (total_cycle_time / cycle_count), num_tags);
    printf("Max tag read frequency: %.1f Hz (individual tag)\n", 
           1000.0 / (total_all_tags_time / total_successful_reads));
    printf("Max ADC read frequency: %.1f Hz (per ADC channel)\n", 
           1000.0 / adc_avg_time);
    
    // ========== SQUARE WAVE ANALYSIS ==========
    
    double half_period_ms = total_cycle_time / cycle_count;
    printf("\n=== SQUARE WAVE ANALYSIS ===\n");
    printf("Wave period:            %.1f ms\n", 2 * half_period_ms);
    printf("Wave frequency:         %.1f Hz\n", 1000.0 / (2 * half_period_ms));
    printf("Duty cycle:             50%%\n");
    
    // ========== REQUIREMENTS COMPLIANCE CHECK ==========
    
    printf("\n=== REQUIREMENTS ANALYSIS ===\n");
    int tags_within_10ms = 0;
    int adc_tags_within_10ms = 0;
    
    // Check each tag against 10ms requirement
    for(int i = 0; i < num_tags; i++) {
        if(tags[i].read_count > 0) {
            double avg = tags[i].total_time / tags[i].read_count;
            if(avg <= 10.0) {
                if(i >= 5 && i <= 8) {
                    printf("✓ ADC %s: %.3f ms\n", tags[i].name, avg);
                    adc_tags_within_10ms++;
                } else {
                    printf("✓ %s: %.3f ms\n", tags[i].name, avg);
                }
                tags_within_10ms++;
            } else {
                if(i >= 5 && i <= 8) {
                    printf("✗ ADC %s: %.3f ms\n", tags[i].name, avg);
                } else {
                    printf("✗ %s: %.3f ms\n", tags[i].name, avg);
                }
            }
        }
    }
    
    printf("System tags: %d/%d meet 10ms requirement\n", tags_within_10ms - adc_tags_within_10ms, 5);
    printf("ADC tags:    %d/%d meet 10ms requirement\n", adc_tags_within_10ms, 4);
    printf("Total:       %d/%d tags meet 10ms requirement\n", tags_within_10ms, num_tags);
    
    // ========== RELIABILITY CHECK ==========
    
    printf("\n=== RELIABILITY CHECK ===\n");
    if(total_errors == 0) {
        printf("✓ 100%% reliable (0 errors)\n");
    } else {
        double success_rate = (1.0 - (double)total_errors / (total_successful_reads + total_errors)) * 100.0;
        printf("⚠ %.1f%% success rate (%d errors)\n", success_rate, total_errors);
    }
    
    if(adc_error_count == 0) {
        printf("✓ ADC channels: 100%% reliable (0 errors)\n");
    } else {
        double adc_success_rate = (1.0 - (double)adc_error_count / (adc_read_count + adc_error_count)) * 100.0;
        printf("⚠ ADC channels: %.1f%% success rate (%d errors)\n", adc_success_rate, adc_error_count);
    }
    
    // ========== CLEANUP AND RESET ==========
    
    // Reset discrete_outputs to 0
    UA_UInt16 zero_state = 0;
    UA_Variant final_val;
    UA_Variant_init(&final_val);
    UA_Variant_setScalar(&final_val, &zero_state, &UA_TYPES[UA_TYPES_UINT16]);
    UA_Client_writeValueAttribute(client, tags[4].nodeId, &final_val);
    
    // ADDED: Write final word counter value to loopback_input
    UA_Variant final_word;
    UA_Variant_init(&final_word);
    UA_Variant_setScalar(&final_word, &word_counter, &UA_TYPES[UA_TYPES_UINT16]);
    UA_Client_writeValueAttribute(client, tags[1].nodeId, &final_word);
    // НЕТ UA_Variant_clear(&final_word); - как в оригинале
    
    // НЕТ UA_Variant_clear(&final_val); - как в оригинале
    
    // Clean up allocated NodeId memory
    for(int i = 0; i < num_tags; i++) {
        UA_NodeId_clear(&tags[i].nodeId);
    }
    
    // Disconnect and delete client
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    
    // ========== FINAL SUMMARY ==========
    
    printf("\n=== TEST COMPLETED ===\n");
    printf("Word counter final value: %u\n", word_counter);
    printf("All outputs reset to 0\n");
    printf("Total system tags tested: %d\n", num_tags);
    printf("  - 5 system tags\n");
    printf("  - 4 ADC channels\n");
    printf("Server URL used: %s\n", server_url);
    
    return 0;
}
