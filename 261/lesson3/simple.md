# Lesson 03: Embedded C++ Compilation with PlatformIO on M5Stack Stick S3

This lesson introduces the mechanics of compiling and executing standard C++ code on the **M5Stack Stick S3** (ESP32-S3) using PlatformIO without external libraries. You will work through three progressive, single-file exercises in `src/main.cpp` to validate the compilation toolchain, query silicon diagnostics, and build a simulated sensor telemetry engine.

```text
                       C++ COMPILATION WORKFLOW
                       
   +------------------+                    +------------------+
   |  platformio.ini  |                    |   src/main.cpp   |
   | (Build Settings) |                    |   (C++ Source)   |
   +------------------+                    +------------------+
            \                                      /
             \                                    /
              v                                  v
       +------------------------------------------------+
       |             PlatformIO Build Engine            |
       |  1. Preprocess (#include, #define)             |
       |  2. Compile C++ to Assembly (xtensa g++)       |
       |  3. Assemble to Object Code (.o)               |
       |  4. Link libraries & map RAM/Flash (.elf)      |
       +------------------------------------------------+
                                |
                                v
       +------------------------------------------------+
       |                  firmware.bin                  |
       |          (Flashed via USB-C to ESP32-S3)       |
       +------------------------------------------------+

```

---

## 1. Project Directory Layout

Keep the workspace structure minimal:

```text
lesson03-cpp-compilation/
├── platformio.ini
└── src/
    └── main.cpp

```

Initialize this folder in your terminal:

```bash
mkdir -p lesson03-cpp-compilation/src
cd lesson03-cpp-compilation

```

---

## 2. Shared `platformio.ini` Configuration

Create a single `platformio.ini` file in the project root. This configuration routes serial data through the ESP32-S3 native USB CDC controller and sets the correct baud rate.

```ini
; =====================================================================
; PlatformIO Configuration: Lesson 03 - Embedded C++ Compilation
; Board: M5Stack Stick S3 (Espressif ESP32-S3)
; =====================================================================

[env:m5stack-s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

; Serial communication parameters
monitor_speed = 115200
upload_speed = 921600

; Native USB CDC configuration
build_flags = 
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1

```

---

## 3. Exercise 3.1: USB CDC Serial & Clock Rate Verification

### Objective

Verify that the C++ preprocessor resolves `<Arduino.h>`, initializes the USB CDC communication channel, and prints runtime execution counters to the serial console.

### Source Code (`src/main.cpp`)

Replace `src/main.cpp` with the following code:

```cpp
/**
 * ============================================================================
 * Exercise 3.1: USB CDC Serial Communication & Heartbeat
 * ============================================================================
 */
#include <Arduino.h>

static uint32_t counter = 0;

void setup() {
    Serial.begin(115200);

    // Wait for the computer to recognize the native USB CDC serial port
    delay(2500);

    Serial.println("\n--- EXERCISE 3.1: SYSTEM BOOT COMPLETE ---");
    Serial.printf("CPU Frequency: %u MHz\n", getCpuFrequencyMhz());
    Serial.println("Starting execution loop...\n");
}

void loop() {
    counter++;
    Serial.printf("[HEARTBEAT #%04u] Uptime: %6lu ms | Target Core: %d\n",
                  counter,
                  millis(),
                  xPortGetCoreID());
    delay(1000);
}

```

### Build & Upload

```bash
pio run -t upload
pio device monitor

```

### Expected Output

```text
--- Terminal on /dev/ttyACM0 | 115200 8-N-1 ---

--- EXERCISE 3.1: SYSTEM BOOT COMPLETE ---
CPU Frequency: 240 MHz
Starting execution loop...

[HEARTBEAT #0001] Uptime:   2510 ms | Target Core: 1
[HEARTBEAT #0002] Uptime:   3510 ms | Target Core: 1
[HEARTBEAT #0003] Uptime:   4510 ms | Target Core: 1

```

---

## 4. Exercise 3.2: Silicon & Memory Diagnostics Functions

### Objective

Practice using custom C++ functions and pass-by-value parameters to inspect internal ESP32-S3 hardware metrics: dynamic RAM allocation, flash storage capacity, and internal SoC die temperature.

### Source Code (`src/main.cpp`)

Replace `src/main.cpp` with the following code:

```cpp
/**
 * ============================================================================
 * Exercise 3.2: Internal Silicon & Memory Diagnostics Functions
 * ============================================================================
 */
#include <Arduino.h>

// User-defined function to print static silicon properties
void displayHardwareProfile() {
    Serial.println("\n=======================================================");
    Serial.println("        ESP32-S3 SILICON DIAGNOSTIC PROFILE            ");
    Serial.println("=======================================================");
    Serial.printf(" CPU Clock Speed:   %u MHz\n", getCpuFrequencyMhz());
    Serial.printf(" Flash Chip Size:   %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf(" Total Heap Memory: %u bytes\n", ESP.getHeapSize());
    Serial.println("=======================================================\n");
}

// User-defined function to evaluate current memory consumption
void auditMemoryState(uint32_t step) {
    uint32_t free_ram = ESP.getFreeHeap();
    float core_temp = temperatureRead(); // Built-in ESP32-S3 thermal sensor

    Serial.printf("[AUDIT #%03u] Free SRAM: %6u bytes | Core Temp: %.1f C\n",
                  step,
                  free_ram,
                  core_temp);
}

void setup() {
    Serial.begin(115200);
    delay(2500);

    displayHardwareProfile();
}

void loop() {
    static uint32_t audit_cycle = 0;
    audit_cycle++;

    auditMemoryState(audit_cycle);
    delay(1500);
}

```

### Build & Upload

```bash
pio run -t upload
pio device monitor

```

### Expected Output

```text
=======================================================
        ESP32-S3 SILICON DIAGNOSTIC PROFILE            
=======================================================
 CPU Clock Speed:   240 MHz
 Flash Chip Size:   8 MB
 Total Heap Memory: 327680 bytes
=======================================================

[AUDIT #001] Free SRAM: 304212 bytes | Core Temp: 38.1 C
[AUDIT #002] Free SRAM: 304212 bytes | Core Temp: 38.2 C
[AUDIT #003] Free SRAM: 304212 bytes | Core Temp: 38.2 C

```

---

## 5. Exercise 3.3: Data Structures & Telemetry Engine

### Objective

Structure data using a C++ `struct`, perform basic signal simulation with `<math.h>`, and pass data to functions via `const` reference for clean, efficient processing.

### Source Code (`src/main.cpp`)

Replace `src/main.cpp` with the following code:

```cpp
/**
 * ============================================================================
 * Exercise 3.3: Structured Data Packets & Math Simulation Logic
 * ============================================================================
 */
#include <Arduino.h>
#include <math.h>

// 1. C++ struct grouping telemetry variables
struct TelemetryFrame {
    uint32_t sequence_id;
    float simulated_vibration;
    float core_temp;
    bool threshold_exceeded;
};

// 2. Global variables and thresholds
static uint32_t frame_index = 0;
const float VIBRATION_ALERT_LEVEL = 1.30f; // Threshold in Gs

// 3. Mathematical model: Generates a sinusoidal vibration signal
float generateSimulatedVibration(uint32_t step) {
    float radians = step * 0.2f;
    float base_gravity = 1.0f; // 1.0G Earth gravity baseline
    float wave = sinf(radians) * 0.45f;
    return base_gravity + wave;
}

// 4. Function processing the struct passed by const reference (no memory copy)
void logTelemetryFrame(const TelemetryFrame& frame) {
    Serial.printf("[FRAME #%04u] Up: %6lu ms | Vib: %.3f G | Temp: %.1f C | State: %s\n",
                  frame.sequence_id,
                  millis(),
                  frame.simulated_vibration,
                  frame.core_temp,
                  frame.threshold_exceeded ? ">> ALARM <<" : "NOMINAL");
}

void setup() {
    Serial.begin(115200);
    delay(2500);

    Serial.println("\n--- EXERCISE 3.3: TELEMETRY ENGINE RUNNING ---");
}

void loop() {
    frame_index++;

    // Populate the struct
    TelemetryFrame current_frame;
    current_frame.sequence_id = frame_index;
    current_frame.simulated_vibration = generateSimulatedVibration(frame_index);
    current_frame.core_temp = temperatureRead();

    // Check threshold condition
    if (current_frame.simulated_vibration > VIBRATION_ALERT_LEVEL) {
        current_frame.threshold_exceeded = true;
    } else {
        current_frame.threshold_exceeded = false;
    }

    // Output through the logging function
    logTelemetryFrame(current_frame);

    delay(500);
}

```

### Build & Upload

```bash
pio run -t upload
pio device monitor

```

### Expected Output

```text
--- EXERCISE 3.3: TELEMETRY ENGINE RUNNING ---
[FRAME #0001] Up:   2510 ms | Vib: 1.089 G | Temp: 38.1 C | State: NOMINAL
[FRAME #0002] Up:   3010 ms | Vib: 1.175 G | Temp: 38.1 C | State: NOMINAL
[FRAME #0003] Up:   3510 ms | Vib: 1.254 G | Temp: 38.2 C | State: NOMINAL
[FRAME #0004] Up:   4010 ms | Vib: 1.322 G | Temp: 38.2 C | State: >> ALARM <<
[FRAME #0005] Up:   4510 ms | Vib: 1.378 G | Temp: 38.2 C | State: >> ALARM <<
[FRAME #0006] Up:   5010 ms | Vib: 1.419 G | Temp: 38.3 C | State: >> ALARM <<
[FRAME #0007] Up:   5510 ms | Vib: 1.442 G | Temp: 38.3 C | State: >> ALARM <<
[FRAME #0008] Up:   6010 ms | Vib: 1.445 G | Temp: 38.3 C | State: >> ALARM <<
[FRAME #0009] Up:   6510 ms | Vib: 1.428 G | Temp: 38.3 C | State: >> ALARM <<
[FRAME #0010] Up:   7010 ms | Vib: 1.391 G | Temp: 38.3 C | State: >> ALARM <<
[FRAME #0011] Up:   7510 ms | Vib: 1.336 G | Temp: 38.4 C | State: >> ALARM <<
[FRAME #0012] Up:   8010 ms | Vib: 1.267 G | Temp: 38.4 C | State: NOMINAL

```

---

## 6. Troubleshooting

| Issue | Cause | Solution |
| --- | --- | --- |
| **Blank terminal output** | Serial monitor opened before the CDC port enumerated. | Keep `delay(2500);` right after `Serial.begin(115200);` in `setup()`. |
| **`Upload port not found`** | The ESP32-S3 needs manual bootloader mode. | Hold down the side button (GPIO0 / Reset), plug in the USB-C cable, release after 2 seconds, and re-run upload. |
| **`fatal error: math.h: No such file`** | Header typographical error. | Use `#include <math.h>` with standard lowercase syntax. |

---

## 7. Version Control Commit

Stage and commit this simplified lesson to your repository:

```bash
git add platformio.ini src/main.cpp
git commit -m "feat(lesson03): implement single-file embedded c++ compilation exercises on m5stick s3"

```
