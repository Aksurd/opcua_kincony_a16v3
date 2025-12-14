# OPC UA Server for Kincony A16V3 - From IoT Controller to Industrial Gateway

This project transforms the affordable **Kincony A16V3 IoT controller** into a fully functional **Industrial OPC UA Gateway**, ready for Industry 4.0 ecosystems.

## 📋 Project Overview

**Objective:** Provide a complete, production-ready solution for integrating low-cost hardware controllers into industrial automation systems using the standardized OPC UA protocol.

**Core Concept:** "From IoT Controller to Industrial Gateway"

*   **Hardware Platform:** Kincony A16V3 (ESP32-based industrial controller)
*   **Protocol:** OPC UA (Open Platform Communications Unified Architecture)
*   **Purpose:** Create an OPC UA server that exposes the controller's inputs/outputs and status as a standardized OPC UA information model.

## 🎯 Current Status & Goals

*   **Core Functionality:** Implemented and operational
*   **Current Task:** Complete **anglicization and enhancement of source code documentation** (technical comments) **without altering program logic**
*   **Final Goal:** Prepare the project for clean publication with professional, fully English-language code documentation

## 📁 Project Structure & Processing Plan

The project consists of several key modules processed in priority order:

1.  **PCF8574 Driver** (Priority 1)
    *   `components/esp32-pcf8574/pcf8574.c`
    *   `components/esp32-pcf8574/include/pcf8574.h`
    *   *Status:* Processed. All comments translated to English, Doxygen-style documentation added.

2.  **OPC UA Data Model** (Priority 2)
    *   `components/model/model.c`
    *   `components/model/include/model.h`

3.  **I/O Caching System** (Priority 3)
    *   `components/io_cache/io_cache.c`
    *   `components/io_cache/include/io_cache.h`
    *   `components/io_cache/io_polling.c`

4.  **Network Module** (Priority 4)
    *   `components/ethernet/ethernet_connect.c`
    *   `components/ethernet/include/ethernet_connect.h`

## ✨ Code Documentation Principles

When enhancing documentation, strict rules are followed:
*   ✅ **Do not modify logic:** Algorithms, variable and structure names remain unchanged
*   ✅ **Translate comments:** All non-English comments (`//` and `/* ... */`) are translated to English
*   ✅ **Add Doxygen documentation:** Detailed `/** ... */` comments are added to each function, structure, and critical code block, explaining purpose, parameters, and return values
*   ✅ **Consistent style:** Adherence to the documentation style established in `main/opcua_esp32.c`
*   ✅ **License attribution:** Each file header includes copyright and licensing information

## 📄 Licensing

The project incorporates code under various licenses, as noted in file headers:
*   **MPL-2.0 Licensed (modified):** `components/model/`
*   **Apache 2.0 Licensed (from ESP-IDF):** `components/ethernet/`
*   **MIT Licensed (original):** `components/esp32-pcf8574/`
*   **Project Original Files:** `components/io_cache/`, `main/`

Please refer to the `LICENSE` file in the repository root and individual source file headers for detailed information.

## 🔧 Compilation, Building, and Flashing

### 1. ESP-IDF Environment Setup

Before building, activate the ESP-IDF environment:

```bash
# Navigate to ESP-IDF installation directory
cd ~/esp/esp-idf
# Activate ESP-IDF environment
. ./export.sh

# Return to project
cd ~/exchange/K868/git/opcua-kincony-a16v3
```

### 2. Project Compilation and Building

#### Basic Build:
```bash
# Clean previous builds (optional)
idf.py fullclean

# Configure project (if not configured)
idf.py menuconfig

# Build project
idf.py build
```

### 3. Flashing to Kincony A16V3 Device

#### Determine COM Port:
```bash
# Linux/Mac
ls /dev/ttyUSB* || ls /dev/ttyACM*

# Windows
# Identify COM port in Device Manager (e.g., COM3)
```

#### Flashing Methods:

**Option 1: Automatic Flashing (Recommended)**
```bash
# Set device port
export ESPPORT=/dev/ttyUSB0  # Linux/Mac
# or set ESPPORT=COM3       # Windows

# Flash with single command
idf.py flash
```

**Option 2: Manual Flashing via esptool.py**
```bash
# Write all components separately
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/opcua_esp32.bin
```

### 4. Device Monitoring

For debugging and monitoring device operation:

```bash
# Monitor serial port
idf.py monitor

# Monitor with auto-reconnect
idf.py monitor -p /dev/ttyUSB0 -b 115200

# To exit monitor, press Ctrl+]
```

## 🧪 Test Utility Compilation (test_counter8)

The `test_counter8` utility is used for OPC UA server performance testing.

### Building the Test Utility:

```bash
# Navigate to test utility directory
cd ~/exchange/K868/git/TEST_OPC_X86/test_counter

# Compile with GCC (Linux/Mac)
gcc -o test_counter8 test_counter8.c \
  -I/usr/local/include/open62541 \
  -L/usr/local/lib \
  -lopen62541 \
  -lpthread \
  -lm

# Alternative compilation with specific flags
gcc -Wall -Wextra -O2 -std=c99 -o test_counter8 test_counter8.c \
  -lopen62541 -lpthread -lm
```

### Running Performance Tests:

```bash
# Basic test (1000 ms interval)
./test_counter8 -t 1000 opc.tcp://10.0.0.128:4840

# Test with various intervals
./test_counter8 -t 500 opc.tcp://10.0.0.128:4840  # 500 ms
./test_counter8 -t 100 opc.tcp://10.0.0.128:4840  # 100 ms

# Show help
./test_counter8 --help
```

## 📊 Performance Test Results Analysis

### Test Parameters:
- **Interval:** 1000 ms
- **Server:** `opc.tcp://10.0.0.128:4840`
- **Tags:** 9 tags (5 system + 4 ADC channels)
- **Duration:** 2789.890 ms (≈2.8 sec)
- **Cycles:** 53

### Key Performance Metrics:

| Metric | Value | Assessment |
|--------|-------|------------|
| Average tag read time | 4.739 ms | ✅ Excellent |
| Minimum read time | 3.768 ms | ✅ Excellent |
| Maximum read time | 13.314 ms | ✅ Within acceptable range |
| Total reliability | 0 errors (100%) | ✅ Perfect |
| Average cycle time | 52.612 ms | ✅ Faster than scheduled (1000 ms) |

### Theoretical Throughput:
- **Max polling frequency for all 9 tags:** 19.0 Hz
- **Max single tag read frequency:** 211.0 Hz  
- **Max ADC channel read frequency:** 211.5 Hz

### Performance Assessment:

```
┌─────────────────────────────────────────────┐
│         PERFORMANCE TEST RESULTS            │
├─────────────────────────────────────────────┤
│  Performance:       ⭐⭐⭐⭐⭐ (5/5)      │
│  Reliability:       ⭐⭐⭐⭐⭐ (5/5)      │
│  Stability:         ⭐⭐⭐⭐⭐ (5/5)      │
│  Requirements Met:  ⭐⭐⭐⭐⭐ (5/5)      │
└─────────────────────────────────────────────┘
```

## 📋 Example Logs

### Server Log Example (ESP32):
```
I (102) cpu_start: Starting scheduler on PRO CPU.
I (123) gpio: GPIO[21]| InputEn: 1| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:3
I (133) pcf8574: I2C initialized on port 0, SDA=21, SCL=22, speed=100000
I (153) eth_connect: Ethernet Link Up
I (193) eth_connect: Got IP Address: 10.0.0.128
I (203) opcua_esp32: Network initialized, IP: 10.0.0.128
I (333) open62541lib: Server running on opc.tcp://10.0.0.128:4840/
```

### Test Utility Log Example:
```
=============================================
   OPC UA HIGH-SPEED PERFORMANCE TEST
   Full system test WITH ADC channels
   Press any key to stop
=============================================

Connecting to opc.tcp://10.0.0.128:4840...
[2025-12-12 03:06:39.171] info/channel TCP 5 | SC 2 | SecureChannel opened
Connected!

Starting test...
Generating square wave on discrete_outputs
Writing word counter to loopback_input
Reading all 9 tags (5 system + 4 ADC channels)

Cycle | WordCnt | State | Time (ms)
-----------------------------------
    0 |       1 |   LOW |    50.226
   10 |      11 |   LOW |    50.105
   20 |      21 |   LOW |    52.366

=== PERFORMANCE SUMMARY ===
Total test time:        2789.890 ms
Total cycles:           53
Total tag reads:        477
Total errors:           0
Average per tag read:   4.739 ms
```

## 🤝 Contribution Guidelines

Contributions are welcome. Please ensure all new comments and documentation are written in English, and coding style matches the existing codebase.

## 📞 Contact Information

**Project Maintainer:** Alex D  
**For global contacts:** aksurd@gmail.com  
**For China:** wxid_ic7ytyv3mlh522  
**GitHub:** [Aksurd](https://github.com/Aksurd)  
**Repository:** `https://github.com/Aksurd/opcua-kincony-a16v3`

## 📚 Additional Resources

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [Open62541 Documentation](https://open62541.org/doc/current/)
- [OPC UA Specification](https://opcfoundation.org/developer-tools/specifications-unified-architecture)
- [Kincony A16V3 Documentation](https://www.kincony.com/esp32-16-channel-relay-controller.html)

---

*This project demonstrates industrial-grade OPC UA server implementation on cost-effective hardware, bridging the gap between IoT devices and Industry 4.0 systems.*