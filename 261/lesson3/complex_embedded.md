# Lesson 03: Embedded C++ Compilation Mechanics & Dual-Core FreeRTOS Architecture in PlatformIO

This module explores the embedded C++ build pipeline, memory layouts, and toolchain mechanics for the **Espressif ESP32-S3** microcontroller using **PlatformIO Core**.

Rather than treating the compiler as a black box, this lesson examines the transformation of source files into linked hardware binaries, configures custom partition tables, and implements multi-threaded firmware scheduled across the dual-core Xtensa LX7 processor using FreeRTOS.

```text
                       ESP32-S3 PLATFORMIO COMPILATION PIPELINE
                       
   +------------------+   +--------------------+   +-------------------+
   |   `src/*.cpp`    |   |    `include/*.h`   |   |   Library Headers |
   +------------------+   +--------------------+   +-------------------+
             \                      |                     /
              \                     |                    /
               v                    v                   v
        +------------------------------------------------------+
        | PREPROCESSOR (xtensa-esp32s3-elf-gcc -E)            |
        | - Macro expansion (#define, #ifdef)                  |
        | - Header file inclusion (#include)                   |
        +------------------------------------------------------+
                                    |
                                    v
        +------------------------------------------------------+
        | COMPILER (xtensa-esp32s3-elf-g++ -c -O2 -std=gnu++17)|
        | - Syntax / Semantic AST parsing                      |
        | - Generation of Assembly instructions (.s)           |
        +------------------------------------------------------+
                                    |
                                    v
        +------------------------------------------------------+
        | ASSEMBLER (xtensa-esp32s3-elf-as)                    |
        | - Translates Assembly into Machine Relocatable (.o)  |
        +------------------------------------------------------+
                                    |
                                    v
        +------------------------------------------------------+
        | LINKER (xtensa-esp32s3-elf-ld via Linker Scripts .ld)|
        | - Resolves symbols, addresses, vector tables         |
        | - Memory map allocation: DRAM, IRAM, Flash (.rodata) |
        | - Generates `firmware.elf` & symbol map (`.map`)     |
        +------------------------------------------------------+
                                    |
                                    v
        +------------------------------------------------------+
        | BINARY PACKAGER (`esptool.py`)                       |
        | - Extracts text/data sections into Flash format      |
        | - Combines Bootloader, Partitions, and `firmware.bin`|
        +------------------------------------------------------+

```

---

## 1. ESP32-S3 Silicon Memory Model & Linker Sections

The ESP32-S3 incorporates a dual-core Xtensa LX7 CPU running up to 240 MHz. Efficient embedded C++ development requires understanding where variables and instructions reside in physical silicon:

```text
                  ESP32-S3 MEMORY MAP ARCHITECTURE
                  
  0x3FC8_0000 +------------------------------------------+
              | DRAM (Data RAM - Internal SRAM0/1)       |
              | - Non-static local stack variables       |
              | - Dynamic allocations (`malloc`, `new`)  |
              | - Initialized mutable globals (.data)    |
              | - Uninitialized globals / zeroed (.bss)  |
  0x3FCE_0000 +------------------------------------------+
  0x4037_0000 +------------------------------------------+
              | IRAM (Instruction RAM - Internal SRAM0)  |
              | - Critical Interrupt Handlers (IRAM_ATTR)|
              | - Time-critical subroutines (Zero-wait)  |
  0x403E_0000 +------------------------------------------+
  0x3C00_0000 +------------------------------------------+
              | DROM (External SPI Flash via Cache MMU)  |
              | - String literals, `const` tables        |
              | - Read-Only Data (.rodata)               |
  0x4200_0000 +------------------------------------------+
              | IROM (External SPI Flash Code Execution) |
              | - Standard C++ compiled methods/classes  |
              +------------------------------------------+

```

### Memory Segments Explained

* **IRAM (Instruction RAM):** Instructions executed here run at full bus speed without flash cache penalties. Interrupt Service Routines (ISRs) must reside here using the `IRAM_ATTR` macro.
* **DRAM (Data RAM):** Byte-addressable RAM used for heap memory (`malloc`/`new`) and the execution call stacks of individual FreeRTOS tasks.
* **DROM / Flash (.rodata):** Constant variables marked `const` or string literals reside directly in external SPI flash mapped via the Memory Management Unit (MMU), preserving internal SRAM.
* **IROM / Flash (.text):** Standard program instructions fetched and executed through the unified instruction cache.

---

## 2. Advanced `platformio.ini` Deep Dive

The default configuration file does not expose compiler optimization levels, symbol table generation, or custom memory partitions.

Update your root `platformio.ini` to enforce strict compiler standards, optimization flags, and memory diagnostic tracking:

```ini
; =====================================================================
; PlatformIO Configuration: Lesson 03 - Advanced C++ Build Engine
; Hardware: M5Stack Stick S3 (ESP32-S3FN8 / Dual-Core Xtensa LX7)
; =====================================================================

[platformio]
default_envs = m5stack_s3_release

[env]
platform = espressif32 @ ^6.5.0
framework = arduino
board = esp32-s3-devkitc-1
monitor_speed = 115200
upload_speed = 921600

; Memory & Flash Hardware Alignment
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.f_flash = 80000000L
board_build.flash_mode = qio
board_upload.flash_size = 8MB
board_upload.maximum_size = 8388608

; Custom Partition Scheme
board_build.partitions = partitions_custom.csv

; Base Preprocessor Flags
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -DCORE_DEBUG_LEVEL=3
    -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG
    -Wall
    -Wextra
    -Wunused-variable

; =====================================================================
; RELEASE TARGET: Highly Optimized (-O2), Stripped Assertions
; =====================================================================
[env:m5stack_s3_release]
build_type = release
build_flags = 
    ${env.build_flags}
    -O2
    -DNDEBUG
    -DAPP_VERSION=\"1.0.0-PROD\"

; =====================================================================
; DEBUG TARGET: Symbol Tables (-g3), Memory Map Generation
; =====================================================================
[env:m5stack_s3_debug]
build_type = debug
build_flags = 
    ${env.build_flags}
    -Og
    -g3
    -DAPP_VERSION=\"1.0.0-DEBUG\"
    -Wl,-Map,firmware.map

```

---

## 3. Flash Partition Table Engineering

The ESP32-S3's 8MB external Quad-SPI flash stores the bootloader, partition table, NVS keys, and application firmware. By default, Arduino allocates a large portion of storage to an OTA updater.

Create a custom partition file named `partitions_custom.csv` in your project root:

```csv
# Name,   Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x340000,
app1,     app,  ota_1,   0x350000,0x340000,
spiffs,   data, spiffs,  0x690000,0x160000,
coredump, data, coredump,0x7F0000,0x10000,

```

### Partition Allocations Breakdown

* `nvs` (20 KB): Non-Volatile Storage for runtime key-value configuration.
* `otadata` (8 KB): Bootloader arbitration data determining whether `app0` or `app1` boots.
* `app0` / `app1` (3.25 MB each): Symmetric dual-slot firmware partitions enabling fail-safe Over-the-Air updates.
* `spiffs` (1.375 MB): Internal flash file system used in Lesson 10 for local offline data buffering.
* `coredump` (64 KB): Dedicated flash memory used to save register crash logs in the event of an unhandled kernel panic.

---

## 4. Multi-Threaded Dual-Core Embedded C++ Architecture

Microcontroller systems often handle real-time sensor sampling alongside network/HMI processing. Running both sequentially inside a single `loop()` introduces timing jitter and dropped sensor frames.

To address this, we leverage the ESP-IDF FreeRTOS implementation:

* **Core 0 (Protocol & System Thread):** Handles high-latency, blocking network requests, serial logging, and memory telemetry.
* **Core 1 (Sensory Acquisition Thread):** Executes deterministic, high-frequency sensor filtering and physical data calculation without interruption.

```text
                  DUAL-CORE FREERTOS EXECUTION MODEL
                  
         CORE 0: PROTOCOL & TELEMETRY         CORE 1: SENSORY ACQUISITION
        +-----------------------------+     +-----------------------------+
        | `vTelemetryTask` (Pri: 1)   |     | `vSensorEngineTask` (Pri: 2)|
        | - Consumes from FreeRTOS Q  |     | - High-rate sensor sampling |
        | - Heap & Memory tracking    |     | - Digital vector arithmetic |
        | - Transmits to interfaces   |     | - Pushes into FreeRTOS Q    |
        +-----------------------------+     +-----------------------------+
                       ^                                   |
                       |       xQueueSendToBack()          |
                       +===================================+
                                 FreeRTOS Queue 
                           `telemetryQueue` (Depth: 10)

```

---

## 5. Implementation: Modular C++ Firmwares

We construct a multi-file architecture demonstrating header segregation, custom types, FreeRTOS queue allocation, and dual-core processor affinity.

### Step 5.1: Create Data Structure Header (`include/telemetry_types.h`)

Create `include/telemetry_types.h`:

```cpp
#pragma once

#include <Arduino.h>

/**
 * @brief Discrete operational status flags for the state machine
 */
enum class SystemState : uint8_t {
    STATE_BOOTING      = 0,
    STATE_OPTIMAL      = 1,
    STATE_WARNING_VIB  = 2,
    STATE_CRITICAL_ERR = 3
};

/**
 * @brief High-density struct for inter-core FreeRTOS queue transfers
 */
struct SensorTelemetryFrame {
    uint32_t sequence_id;      // Monotonically increasing packet counter
    uint32_t timestamp_ms;     // Hardware uptime timestamp
    float acceleration_rms;    // Processed root-mean-square vibration
    float chip_temperature_c;  // Internal SoC thermal sensor
    SystemState current_state; // Discrete operating status
    uint32_t free_heap_bytes;  // Heap memory telemetry at sampling instant
};

```

### Step 5.2: Create Memory Diagnostics Module (`include/memory_tracker.h`)

Create `include/memory_tracker.h`:

```cpp
#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

class MemoryTracker {
public:
    static void printMemorySummary() {
        size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        size_t min_dram  = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        size_t max_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

        Serial.println("\n----------------- SILICON MEMORY MAP REPORT -----------------");
        Serial.printf("  Free Internal DRAM:     %7u bytes\n", free_dram);
        Serial.printf("  Historic Low DRAM:      %7u bytes\n", min_dram);
        Serial.printf("  Largest Contiguous Blk: %7u bytes\n", max_block);
        Serial.println("------------------------------------------------------------\n");
    }

    static void logTaskStackWatermark(TaskHandle_t task_handle, const char* task_name) {
        UBaseType_t high_water_mark = uxTaskGetStackHighWaterMark(task_handle);
        Serial.printf("[STACK MONITOR] Task '%s' -> Unused Stack Margin: %u words (%u bytes)\n",
                      task_name,
                      high_water_mark,
                      high_water_mark * sizeof(StackType_t));
    }
};

```

### Step 5.3: Main Application Source (`src/main.cpp`)

Create `src/main.cpp`:

```cpp
/**
 * ============================================================================
 * Project: M5Stack Stick S3 Journey - Lesson 03
 * Description: FreeRTOS Dual-Core Execution, Memory Tracking & Queue Engine
 * ============================================================================
 */

#include <Arduino.h>
#include "telemetry_types.h"
#include "memory_tracker.h"

// FreeRTOS Synchronization Primitives
static QueueHandle_t telemetryQueue = nullptr;
static const uint8_t QUEUE_LENGTH = 10;

// Task Handles for Stack Diagnostics
static TaskHandle_t hSensorTask = nullptr;
static TaskHandle_t hTelemetryTask = nullptr;

/**
 * ============================================================================
 * TASK 1: Sensor Processing Engine (Bound strictly to CORE 1)
 * ============================================================================
 */
void vSensorEngineTask(void* pvParameters) {
    (void)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // Strict 10 Hz sampling rate

    uint32_t local_packet_index = 0;
    float synthetic_angle = 0.0f;

    Serial.printf("[CORE 1] Sensor Engine Task spawned on Core %d\n", xPortGetCoreID());

    for (;;) {
        local_packet_index++;
        synthetic_angle += 0.1f;

        // Simulate mathematical acceleration vector calculation: A = 1.0 + sin(theta) * 0.4
        float simulated_accel = 1.0f + (sinf(synthetic_angle) * 0.4f);

        // Evaluate operating state
        SystemState evaluated_state = SystemState::STATE_OPTIMAL;
        if (simulated_accel > 1.30f) {
            evaluated_state = SystemState::STATE_WARNING_VIB;
        }

        // Construct telemetry packet
        SensorTelemetryFrame frame;
        frame.sequence_id = local_packet_index;
        frame.timestamp_ms = millis();
        frame.acceleration_rms = simulated_accel;
        frame.chip_temperature_c = temperatureRead();
        frame.current_state = evaluated_state;
        frame.free_heap_bytes = esp_get_free_heap_size();

        // Push data to queue with a non-blocking timeout
        if (xQueueSend(telemetryQueue, &frame, (TickType_t)0) != pdPASS) {
            Serial.println("[QUEUE WARN] Telemetry queue full! Dropping frame.");
        }

        // Enforce deterministic execution interval
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * ============================================================================
 * TASK 2: Telemetry Logger & Protocol Task (Bound strictly to CORE 0)
 * ============================================================================
 */
void vTelemetryTask(void* pvParameters) {
    (void)pvParameters;
    SensorTelemetryFrame received_frame;
    uint32_t processed_counter = 0;

    Serial.printf("[CORE 0] Telemetry Processing Task spawned on Core %d\n", xPortGetCoreID());

    for (;;) {
        // Block indefinitely until a telemetry frame arrives in the queue
        if (xQueueReceive(telemetryQueue, &received_frame, portMAX_DELAY) == pdTRUE) {
            processed_counter++;

            // Format discrete state into readable text
            const char* state_str = "UNKNOWN";
            switch (received_frame.current_state) {
                case SystemState::STATE_BOOTING:      state_str = "BOOTING";  break;
                case SystemState::STATE_OPTIMAL:      state_str = "OPTIMAL";  break;
                case SystemState::STATE_WARNING_VIB:  state_str = "WARN_VIB"; break;
                case SystemState::STATE_CRITICAL_ERR: state_str = "CRITICAL"; break;
            }

            // Print formatted telemetry stream
            Serial.printf("[FRAME #%05u | UP: %7u ms] VIB: %.3f g | TEMP: %.1f C | STATUS: %-8s | HEAP: %u B [CORE %d]\n",
                          received_frame.sequence_id,
                          received_frame.timestamp_ms,
                          received_frame.acceleration_rms,
                          received_frame.chip_temperature_c,
                          state_str,
                          received_frame.free_heap_bytes,
                          xPortGetCoreID());

            // Run heap diagnostics periodically every 25 frames
            if (processed_counter % 25 == 0) {
                MemoryTracker::printMemorySummary();
                MemoryTracker::logTaskStackWatermark(hSensorTask, "SensorEngine");
                MemoryTracker::logTaskStackWatermark(hTelemetryTask, "TelemetryLogger");
            }
        }
    }
}

/**
 * ============================================================================
 * Hardware Setup & Initialization
 * ============================================================================
 */
void setup() {
    Serial.begin(115200);
    delay(2500); // Allow CDC serial port enumeration

    Serial.println("\n=======================================================");
    Serial.println(" M5STACK STICK S3: FREERTOS DUAL-CORE ENGINE BOOTING  ");
    Serial.printf(" Build Version: %s | GCC: %s\n", APP_VERSION, __VERSION__);
    Serial.println("=======================================================");

    // Initialize the FreeRTOS inter-task communication queue
    telemetryQueue = xQueueCreate(QUEUE_LENGTH, sizeof(SensorTelemetryFrame));
    if (telemetryQueue == nullptr) {
        Serial.println("[FATAL ERROR] Failed to allocate FreeRTOS telemetry queue! Halting.");
        while (1) { delay(1000); }
    }

    // Pin SensorEngineTask to Core 1 (Stack: 4096 bytes, Priority: 2)
    xTaskCreatePinnedToCore(
        vSensorEngineTask,
        "SensorEngine",
        4096,
        nullptr,
        2,
        &hSensorTask,
        1 // Core 1
    );

    // Pin TelemetryTask to Core 0 (Stack: 4096 bytes, Priority: 1)
    xTaskCreatePinnedToCore(
        vTelemetryTask,
        "TelemetryLogger",
        4096,
        nullptr,
        1,
        &hTelemetryTask,
        0 // Core 0
    );

    Serial.println("[SYSTEM] Real-time tasks successfully allocated and scheduled.\n");
}

void loop() {
    // The Arduino default loop runs on Core 1 with priority 1.
    // Putting it to sleep eliminates unnecessary CPU load.
    vTaskDelay(pdMS_TO_TICKS(10000));
}

```

---

## 6. Compilation, Binary Inspection & Flashing

Execute builds and inspect outputs using the PlatformIO Core CLI:

### Step 6.1: Compile Release Binary

```bash
pio run -e m5stack_s3_release

```

**Expected Terminal Output:**

```text
Processing m5stack_s3_release (platform: espressif32 @ ^6.5.0; board: esp32-s3-devkitc-1; framework: arduino)
-------------------------------------------------------------------------------------------------------------------
Compiling .pio/build/m5stack_s3_release/src/main.cpp.o
Linking .pio/build/m5stack_s3_release/firmware.elf
Retrieving maximum program size .pio/build/m5stack_s3_release/firmware.elf
Checking size .pio/build/m5stack_s3_release/firmware.elf
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [=         ]   6.3% (used 20688 bytes from 327680 bytes)
Flash: [=         ]   4.4% (used 151592 bytes from 3407872 bytes)
Building .pio/build/m5stack_s3_release/firmware.bin
esptool.py v4.5.1
Creating esp32s3 image...
Merged 2 ELF sections
Successfully created esp32s3 image.
========================================== [SUCCESS] Took 3.12 seconds ==========================================

```

### Step 6.2: Analyze Memory Footprint via PlatformIO CLI

Inspect the internal structure of your flash binary to evaluate consumption across sections:

```bash
pio run -e m5stack_s3_release -t size

```

**Output Analysis:**

```text
<MEMCHECK>
  Section                Size        Target Address
  .rtc.text              0x0         0x600fe000
  .rtc.data              0x0         0x600fe000
  .iram0.text            0xa120      0x40378000  (Critical instructions, 40 KB)
  .dram0.data            0x1d40      0x3fc88000  (Initialized mutable RAM, 7.3 KB)
  .dram0.bss             0x3210      0x3fc89d40  (Uninitialized RAM globals, 12.5 KB)
  .flash.text            0x18ab4     0x42000020  (Execution from Flash, 98.6 KB)
  .flash.rodata          0x5820      0x3c000020  (Constants and Strings, 22 KB)
===================================================================================================
Total Estimated RAM Usage: 20688 bytes (Internal SRAM)
Total Flash Footprint:    151592 bytes (App Partition)

```

### Step 6.3: Upload and Stream Serial Output

```bash
pio run -e m5stack_s3_release --target upload
pio device monitor

```

**Expected Runtime Serial Log:**

```text
--- Terminal on /dev/ttyACM0 | 115200 8-N-1 ---
=======================================================
 M5STACK STICK S3: FREERTOS DUAL-CORE ENGINE BOOTING  
 Build Version: 1.0.0-PROD | GCC: 8.4.0
=======================================================
[CORE 1] Sensor Engine Task spawned on Core 1
[CORE 0] Telemetry Processing Task spawned on Core 0
[SYSTEM] Real-time tasks successfully allocated and scheduled.

[FRAME #00001 | UP:    2510 ms] VIB: 1.040 g | TEMP: 37.8 C | STATUS: OPTIMAL  | HEAP: 384112 B [CORE 0]
[FRAME #00002 | UP:    2610 ms] VIB: 1.079 g | TEMP: 37.8 C | STATUS: OPTIMAL  | HEAP: 384112 B [CORE 0]
[FRAME #00003 | UP:    2710 ms] VIB: 1.119 g | TEMP: 37.9 C | STATUS: OPTIMAL  | HEAP: 384112 B [CORE 0]
[FRAME #00004 | UP:    2810 ms] VIB: 1.157 g | TEMP: 37.9 C | STATUS: OPTIMAL  | HEAP: 384112 B [CORE 0]
[FRAME #00005 | UP:    2910 ms] VIB: 1.194 g | TEMP: 38.0 C | STATUS: OPTIMAL  | HEAP: 384112 B [CORE 0]
[FRAME #00006 | UP:    3010 ms] VIB: 1.228 g | TEMP: 38.0 C | STATUS: OPTIMAL  | HEAP: 384112 B [CORE 0]
[FRAME #00007 | UP:    3110 ms] VIB: 1.258 g | TEMP: 38.0 C | STATUS: OPTIMAL  | HEAP: 384112 B [CORE 0]
[FRAME #00008 | UP:    3210 ms] VIB: 1.284 g | TEMP: 38.1 C | STATUS: OPTIMAL  | HEAP: 384112 B [CORE 0]
[FRAME #00009 | UP:    3310 ms] VIB: 1.304 g | TEMP: 38.1 C | STATUS: WARN_VIB | HEAP: 384112 B [CORE 0]

----------------- SILICON MEMORY MAP REPORT -----------------
  Free Internal DRAM:      384112 bytes
  Historic Low DRAM:       381480 bytes
  Largest Contiguous Blk:  315380 bytes
------------------------------------------------------------

[STACK MONITOR] Task 'SensorEngine'    -> Unused Stack Margin: 812 words (3248 bytes)
[STACK MONITOR] Task 'TelemetryLogger' -> Unused Stack Margin: 748 words (2992 bytes)

```

---

## 7. Troubleshooting Build & Runtime Failures

| Error Signature | Underlying Fault | Remediation |
| --- | --- | --- |
| **`fatal error: telemetry_types.h: No such file`** | Header placed outside the project include path. | Place all global header files inside `include/` or alongside `src/`. |
| **`Guru Meditation Error: Core 1 panic'ed (LoadProhibited)`** | Dereferencing a `nullptr` or accessing an uninitialized queue handle. | Verify `xQueueCreate()` evaluates to a valid handle before calling `xTaskCreatePinnedToCore()`. |
| **`Stack canary watchpoint triggered (SensorEngine)`** | Task stack overflow ($< 4096$ bytes assigned for the task's local allocations). | Increase task stack allocation in `xTaskCreatePinnedToCore()` from `4096` to `8192` bytes. |
| **`region 'app0' overflowed by XXX bytes`** | Firmware size exceeds the custom partition ceiling defined in `partitions_custom.csv`. | Increase the partition size in `partitions_custom.csv` or set compiler flag `-Os` to optimize for binary size. |
| **`Task watchdog got triggered on Core 0/1`** | A high-priority task executed an infinite loop without yielding CPU control to FreeRTOS. | Add a non-blocking delay: `vTaskDelay(pdMS_TO_TICKS(x))` to allow the Idle task to reset the watchdog timer. |

---

## 8. Hands-On Verification Lab

Complete the following technical validations to finish Lesson 3:

* [ ] **Task 3.1:** Modify `xFrequency` in `vSensorEngineTask` to sample at 50 Hz (20 ms interval). Recompile and verify that Core 0 receives frames without triggering queue overflow warnings.
* [ ] **Task 3.2:** Introduce an artificial stack variable (`char dummy[2048]; memset(dummy, 0, sizeof(dummy));`) inside `vSensorEngineTask`. Observe how the unused stack margin changes in `uxTaskGetStackHighWaterMark`.
* [ ] **Task 3.3:** Run `pio run -e m5stack_s3_debug` and locate the generated `firmware.map` file in the project root. Inspect it to identify the largest compiled functions in the flash text section.

---

## 9. Version Control Protocol

Commit the multi-threaded firmware architecture and custom partition table:

```bash
# Add custom partition table and firmware modules
git add platformio.ini partitions_custom.csv include/telemetry_types.h include/memory_tracker.h src/main.cpp

# Commit changes
git commit -m "feat(lesson03): implement dual-core freertos pipeline, memory tracking, and custom partition table"

```
