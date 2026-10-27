# Lesson 04: M5Stack Stick S3 Hardware Peripherals — Display, IMU, Buttons & Power Management

This module transitions from synthetic simulations to physical hardware execution on the **M5Stack Stick S3**. You will integrate the official **`M5Unified`** hardware abstraction library to interface directly with the device's onboard peripherals: the **ST7789 TFT LCD**, the **BMI270 6-axis IMU**, the **AXP2101 power management IC (PMIC)**, physical **tactile switches**, and the **piezo buzzer**.

```text
                  M5STACK STICK S3 ONBOARD PERIPHERAL BUS
                  
                         +------------------------+
                         | ESP32-S3 Dual-Core MCU |
                         +------------------------+
                                     |
         +-----------------+---------+---------+-----------------+
         | SPI Bus         | I2C Bus           | GPIO / I2S      | Internal ADC
         v                 v                   v                 v
   +-----------+     +-----------+       +-----------+     +-----------+
   |  ST7789v2 |     |  BMI270   |       |  Buttons  |     |  AXP2101  |
   | Color LCD |     | 6-Axis IMU|       | (BtnA/B)  |     |   PMIC    |
   | (135x240) |     | Accel/Gyro|       |  & Buzzer |     | (Battery) |
   +-----------+     +-----------+       +-----------+     +-----------+

```

---

## 1. Project Directory Layout & `platformio.ini`

Keep the project structure clean and single-file:

```text
lesson04-hardware-peripherals/
├── platformio.ini
└── src/
    └── main.cpp

```

Create the directory and initialize the environment:

```bash
mkdir -p lesson04-hardware-peripherals/src
cd lesson04-hardware-peripherals

```

### `platformio.ini` Configuration

Update `platformio.ini` to declare **`M5Unified`** under `lib_deps`. PlatformIO handles downloading, compiling, and linking the peripheral drivers automatically.

```ini
[platformio]
default_envs = m5stack-s3

[env:m5stack-s3]
platform = espressif32@6.12.0
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
upload_speed = 921600

board_build.f_cpu = 240000000L
board_build.flash_size = 8MB
board_upload.flash_size = 8MB
board_build.partitions = default_8MB.csv
board_build.filesystem = littlefs

build_flags =
    -std=gnu++17
    -DESP32S3
    -DARDUINO_M5STACK_STICK_S3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -O3

lib_deps =
    M5Unified=https://github.com/m5stack/M5Unified

```

---

## 2. The `M5Unified` Architecture

`M5Unified` replaces low-level register configuration with a unified peripheral driver tree:

* **`M5.begin()`:** Detects connected board architecture, sets internal I2C bus frequencies, enables the PMIC output rails, and clears the display buffer.
* **`M5.update()`:** Polling method that **must run once per `loop()` cycle**. It performs non-blocking hardware button debouncing, calculates press/click/hold states, and updates power state registers.
* **`M5.Display`:** Controls the ST7789 1.14" color TFT screen (drawing primitives, text glyphs, rotations, and custom color canvases).
* **`M5.Imu`:** Interacts with the Bosch BMI270 6-axis motion sensor over internal I2C.
* **`M5.Power`:** Communicates with the AXP2101 PMIC to read battery charge percentages, rail voltages, and power button events.
* **`M5.Speaker`:** Generates square waves and tones across the onboard piezo transducer for audible alerts.

---

## 3. Exercise 4.1: Display Canvas & Graphic Formatting

### Objective

Configure the 1.14-inch ST7789 TFT display (135 $\times$ 240 resolution), rotate the screen into landscape mode, eliminate render flicker using formatted string prints with text background colors, and render dynamic user interface cards.

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * M5Stack StickS3 High-Performance Display & HMI Rendering Engine
 * Target: ST7789P3 IPS LCD (240x135 Landscape)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>

static uint32_t frame_counter = 0;
static unsigned long previous_millis = 0;
static float current_fps = 0.0f;

void renderStaticLayout() {
    // 1. Clear screen with deep dark slate background
    M5.Display.fillScreen(0x1084); // Custom dark gray/navy hex

    // 2. Top Header Bar (240x22 pixels)
    M5.Display.fillRect(0, 0, 240, 22, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("M5STICK-S3 DISPLAY ENGINE", 120, 11, &fonts::Font2);

    // 3. Divider line
    M5.Display.drawFastHLine(10, 28, 220, TFT_DARKGRAY);

    // 4. Static Labels for Dynamic Zones
    M5.Display.setTextColor(TFT_LIGHTGRAY, 0x1084);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.drawString("FRAME:", 15, 38, &fonts::Font0);
    M5.Display.drawString("FPS:", 135, 38, &fonts::Font0);
    M5.Display.drawString("UPTIME:", 15, 60, &fonts::Font0);
}

void setup() {
    Serial.begin(115200);

    // Initialize M5Stack StickS3 Hardware
    auto cfg = M5.config();
    M5.begin(cfg);

    // CRITICAL: Configure for 240x135 Landscape Mode
    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    // Render the initial static chrome
    renderStaticLayout();
}

void loop() {
    M5.update();
    frame_counter++;

    // Calculate real-time FPS every second
    unsigned long current_millis = millis();
    if (current_millis - previous_millis >= 1000) {
        current_fps = (frame_counter * 1000.0f) / (current_millis - previous_millis);
        frame_counter = 0;
        previous_millis = current_millis;
    }

    // --- Dynamic Data Update Zones (Targeted Refresh to prevent flicker) ---

    // Zone 1: Frame Count
    M5.Display.fillRect(65, 36, 60, 14, 0x1084);
    M5.Display.setTextColor(TFT_GREENYELLOW, 0x1084);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.drawString(String(frame_counter), 65, 36, &fonts::Font0);

    // Zone 2: FPS Tracker
    M5.Display.fillRect(165, 36, 60, 14, 0x1084);
    M5.Display.setTextColor(TFT_CYAN, 0x1084);
    M5.Display.drawString(String(current_fps, 1), 165, 36, &fonts::Font0);

    // Zone 3: Uptime Clock
    uint32_t seconds = millis() / 1000;
    uint32_t minutes = seconds / 60;
    char time_buffer[16];
    snprintf(time_buffer, sizeof(time_buffer), "%02u:%02u:%02u", (minutes / 60) % 24, minutes % 60, seconds % 60);
    
    M5.Display.fillRect(75, 58, 100, 16, 0x1084);
    M5.Display.setTextColor(TFT_WHITE, 0x1084);
    M5.Display.drawString(time_buffer, 75, 58, &fonts::Font2);

    // Zone 4: Dynamic Graphical Progress Bar
    int bar_progress = (millis() / 50) % 220; // Loops every 11 seconds across 220px
    M5.Display.drawRect(10, 85, 220, 14, TFT_DARKGRAY);
    M5.Display.fillRect(11, 86, bar_progress, 12, TFT_BLUE);
    M5.Display.fillRect(11 + bar_progress, 86, 218 - bar_progress, 12, TFT_BLACK);

    // Zone 5: Random Primitive Stress Test (Visualizing graphics hardware acceleration)
    int rx = 20 + (rand() % 200);
    int ry = 105 + (rand() % 20);
    uint16_t rcolor = rand();
    M5.Display.fillCircle(rx, ry, 4, rcolor);

    delay(33); // Target ~30 FPS frame pacing
}

```

### Build & Upload

```bash
pio run -t upload

```

---

## 4. Exercise 4.2: Hardware Buttons & Acoustic Alerts

### Objective

Capture physical button inputs with hardware debouncing via `M5.update()`, differentiate between short clicks and long holds, and generate acoustic feedback using `M5.Speaker`.

* **Button A (Front Face "M5"):** Increments manual audit counter, beeps at 1000 Hz.
* **Button B (Side Edge):** Resets counter on short press; plays alert pattern on long press (> 1 second).

```text
                  PHYSICAL BUTTON MAPPING
                  
          +--------------------------------------+
          |           [ LCD DISPLAY ]            |
          |                                      |
          | +----------------------------------+ |
          | |                                  | |
  Btn B   | |         M5Stick S3 Face          | |  Btn A
 [SIDE] ->| |                                  | |<- [M5 FRONT]
          | +----------------------------------+ |
          +--------------------------------------+

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 4.2: Hardware Interaction — Button Handlers & Acoustic Transducer
 * Hardware Target: M5Stack StickS3 (ESP32-S3 / ST7789P3, 240x135 Landscape)
 * ============================================================================
 */
#include <M5Unified.h>

static uint32_t manual_audit_count = 0;
static bool alarm_mode_active = false;

void drawInterface() {
    M5.Display.fillScreen(TFT_BLACK);
    
    // Header Banner (240x24 pixels)
    M5.Display.fillRect(0, 0, 240, 24, TFT_DARKCYAN);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("BUTTON & BUZZER DEMO", 120, 12);

    // Static Instructions
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA (Front): Audit Count", 120, 45);
    M5.Display.drawString("BtnB (Side): Reset / Hold", 120, 62);

    // Dynamic Metric Readout Field
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString("Audits: " + String(manual_audit_count), 120, 95);
    M5.Display.setTextSize(1);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // CRITICAL: Configure for 240x135 Landscape Orientation
    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);
    
    drawInterface();
}

void loop() {
    // REQUIRED: Updates button states and internal debounce timers every cycle
    M5.update();

    // 1. Process Button A (Front "M5" Panel Button)
    if (M5.BtnA.wasClicked()) {
        manual_audit_count++;
        
        // Acoustic feedback: 1000 Hz tone for 100 ms
        M5.Speaker.tone(1000, 100);

        // Targeted UI update for audit count to avoid full-screen refresh flicker
        M5.Display.fillRect(0, 80, 240, 30, TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.setTextDatum(textdatum_t::middle_center);
        M5.Display.drawString("Audits: " + String(manual_audit_count), 120, 95);
        M5.Display.setTextSize(1);
    }

    // 2. Process Button B (Side Edge Button) - Click Event
    if (M5.BtnB.wasClicked()) {
        manual_audit_count = 0;
        
        // Acoustic feedback: Dual-frequency confirmation chirp
        M5.Speaker.tone(2000, 50);
        delay(60);
        M5.Speaker.tone(3000, 50);

        // Redraw base UI layout
        drawInterface();
        alarm_mode_active = false; // Reset alarm state on counter reset
    }

    // 3. Process Button B (Side Edge Button) - Hold Event
    if (M5.BtnB.wasHold()) {
        alarm_mode_active = !alarm_mode_active;

        if (alarm_mode_active) {
            // Alarm activation sound & visual banner
            M5.Speaker.tone(800, 300);
            M5.Display.fillRect(0, 112, 240, 23, TFT_RED);
            M5.Display.setTextColor(TFT_WHITE, TFT_RED);
            M5.Display.setTextDatum(textdatum_t::middle_center);
            M5.Display.drawString("!! SIREN MODE ACTIVE !!", 120, 123);
        } else {
            // Deactivation confirmation
            M5.Speaker.tone(400, 200);
            M5.Display.fillRect(0, 112, 240, 23, TFT_BLACK);
        }
    }

    delay(20); // Maintain stable poll rate and reduce CPU idle load
}

```

### Build & Upload

```bash
pio run -t upload

```

---

## 5. Exercise 4.3: Real Sensor Acquisition — BMI270 6-Axis IMU

### Objective

Sample real physical acceleration and angular velocity using the onboard Bosch **BMI270 IMU**. Calculate total vector acceleration magnitude ($A$) using Euclidean norm math and detect physical shock events.

$$\Vert{}A\Vert{} = \sqrt{a_x^2 + a_y^2 + a_z^2}$$

* Under stationary conditions on Earth, $\Vert{}A\Vert{} \approx 1.00\text{ G}$.
* Shaking or impacting the device increases $\Vert{}A\Vert{} > 1.50\text{ G}$.

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Enterprise Flicker-Free & Overlap-Free HMI Engine for M5Stack StickS3
 * Hardware Target: ESP32-S3 / ST7789P3 (240x135 Landscape)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>

static uint32_t telemetry_counter = 0;
static unsigned long previous_millis = 0;

void drawStaticLayout() {
    // 1. Clear background to deep slate
    M5.Display.fillScreen(0x1084);

    // 2. Header Banner (240x22 pixels)
    M5.Display.fillRect(0, 0, 240, 22, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("M5STICK-S3 EDGE TELEMETRY", 120, 11, &fonts::Font2);

    // 3. Static Field Labels (Left Column)
    M5.Display.setTextColor(TFT_LIGHTGRAY, 0x1084);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.drawString("STATUS:", 10, 32, &fonts::Font0);
    M5.Display.drawString("COUNTER:", 10, 52, &fonts::Font0);
    M5.Display.drawString("UPTIME:", 10, 72, &fonts::Font0);

    // 4. Horizontal Separator Bar
    M5.Display.drawFastHLine(10, 95, 220, TFT_DARKGRAY);
}

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5.begin(cfg);

    // CRITICAL: Configure for 240x135 Landscape Mode
    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    drawStaticLayout();
}

void loop() {
    M5.update();
    telemetry_counter++;

    // --- DYNAMIC UPDATE ZONES (Strict Bounding Box Clearing to Prevent Overlap) ---

    // Zone 1: Status Indicator (Row Y: 32)
    // Clear exact bounding box before drawing new text
    M5.Display.fillRect(75, 32, 150, 14, 0x1084);
    M5.Display.setTextDatum(textdatum_t::top_left);
    if ((telemetry_counter / 10) % 2 == 0) {
        M5.Display.setTextColor(TFT_GREENYELLOW, 0x1084);
        M5.Display.drawString("RUNNING [OK]", 75, 32, &fonts::Font0);
    } else {
        M5.Display.setTextColor(TFT_ORANGE, 0x1084);
        M5.Display.drawString("PROCESSING...", 75, 32, &fonts::Font0);
    }

    // Zone 2: Fixed-Width Counter (Row Y: 52)
    // Using snprintf with zero padding (%06u) guarantees identical string length, eliminating ghosting
    char counter_buf[16];
    snprintf(counter_buf, sizeof(counter_buf), "%06u", telemetry_counter);
    
    M5.Display.fillRect(75, 52, 120, 14, 0x1084);
    M5.Display.setTextColor(TFT_CYAN, 0x1084);
    M5.Display.drawString(counter_buf, 75, 52, &fonts::Font0);

    // Zone 3: Uptime Clock (Row Y: 72)
    unsigned long seconds = millis() / 1000;
    unsigned long minutes = seconds / 60;
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu:%02lu", (minutes / 60) % 24, minutes % 60, seconds % 60);

    M5.Display.fillRect(75, 72, 100, 14, 0x1084);
    M5.Display.setTextColor(TFT_WHITE, 0x1084);
    M5.Display.drawString(time_buf, 75, 72, &fonts::Font0);

    // Zone 4: Graphical Progress Bar (Y: 105 to 125)
    int progress_width = (telemetry_counter % 100) * 2; // Scales 0 to 200px max width
    M5.Display.drawRect(20, 105, 200, 12, TFT_DARKGRAY);
    M5.Display.fillRect(21, 106, progress_width, 10, TFT_BLUE);
    M5.Display.fillRect(21 + progress_width, 106, 198 - progress_width, 10, 0x1084);

    delay(100); // Stable 10Hz refresh pacing
}

```

### Build & Upload

```bash
pio run -t upload

```

*Gently shake or tap the device to trigger the dynamic shock alert and buzzer.*

---

## 6. Exercise 4.4: Integrated Handheld Telemetry Node

### Objective

Consolidate all subsystems into a unified edge application:

1. **ST7789 Color Display:** Live status dashboard.
2. **BMI270 IMU:** Continuous physical vibration magnitude monitoring.
3. **AXP2101 PMIC:** Live battery voltage ($V_{bat}$) and remaining capacity gauge.
4. **Button A & B Interaction:** Real-time calibration and silence controls.
5. **Audible Transducer:** Dynamic alarms for critical battery or motion events.

```text
+-------------------------------------------------------------+
| M5STICK S3 FLEET MONITOR               BAT: 92% (4.12V)     | <- AXP2101 PMIC
+-------------------------------------------------------------+
|  VIBRATION VECTOR                      OPERATING STATE      |
|                                                             |
|         1.03 G                             OPTIMAL          | <- BMI270 IMU
|  (Peak: 1.48 G)                                             |
+-------------------------------------------------------------+
| [BtnA] Reset Peak Value       | [BtnB] Toggle Sound [ON]   | <- Buttons & Buzzer
+-------------------------------------------------------------+

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 4.4: Complete Hardware Peripherals Integration
 * ============================================================================
 */
#include <M5Unified.h>
#include <math.h>

// Operational State Variables
static float g_peak_vibration = 0.0f;
static bool g_sound_enabled = true;
const float VIBRATION_LIMIT = 1.45f;

void drawBaseDashboard() {
    M5.Display.fillScreen(TFT_BLACK);

    // Top Status Header
    M5.Display.fillRect(0, 0, 240, 20, TFT_DARKGRAY);

    // Panel Separators
    M5.Display.drawFastHLine(0, 95, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 20, 75, TFT_DARKGRAY);

    // Static Section Titles
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VIB MAGNITUDE", 60, 30);
    M5.Display.drawString("MACHINE STATUS", 180, 30);

    // Bottom Navigation Help
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Reset Peak", 60, 110);
    M5.Display.drawString("BtnB: Toggle Audio", 180, 110);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    drawBaseDashboard();
}

void loop() {
    M5.update();

    // 1. Hardware Sensor Polling (BMI270 IMU)
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float current_vib = sqrtf((ax * ax) + (ay * ay) + (az * az));

    if (current_vib > g_peak_vibration) {
        g_peak_vibration = current_vib;
    }

    // 2. Power Management Subsystem (AXP2101 PMIC)
    int battery_percentage = M5.Power.getBatteryLevel();
    float battery_voltage = M5.Power.getBatteryVoltage() / 1000.0f;

    // 3. User Input Processing
    if (M5.BtnA.wasClicked()) {
        g_peak_vibration = current_vib;
        if (g_sound_enabled) M5.Speaker.tone(1200, 50);
    }

    if (M5.BtnB.wasClicked()) {
        g_sound_enabled = !g_sound_enabled;
        if (g_sound_enabled) M5.Speaker.tone(2400, 80);
    }

    // 4. Render Dynamic Header (Battery & Audio Status)
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKGRAY);
    M5.Display.drawString(" FLEET MONITOR", 5, 10);

    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.printf("BAT: %d%% (%.2fV) ", battery_percentage, battery_voltage);

    // 5. Render Sensor Telemetry Values
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(String(current_vib, 2) + " G ", 60, 55);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5.Display.drawString("Peak: " + String(g_peak_vibration, 2) + " G", 60, 80);

    // 6. Evaluate Operating State
    if (current_vib > VIBRATION_LIMIT) {
        M5.Display.fillRect(122, 21, 117, 73, TFT_RED);
        M5.Display.setTextColor(TFT_WHITE, TFT_RED);
        M5.Display.setTextSize(2);
        M5.Display.drawString("ALARM", 180, 50);
        M5.Display.setTextSize(1);
        M5.Display.drawString("LIMIT BREACH", 180, 75);

        if (g_sound_enabled) {
            M5.Speaker.tone(1500, 30);
        }
    } else {
        M5.Display.fillRect(122, 21, 117, 73, TFT_BLACK);
        M5.Display.drawFastVLine(120, 20, 75, TFT_DARKGRAY);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.drawString("OPTIMAL", 180, 50);
        M5.Display.setTextSize(1);
        M5.Display.drawString("NOMINAL VIB", 180, 75);
    }

    // 7. Update Audio Toggle State
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(g_sound_enabled ? TFT_GREENYELLOW : TFT_RED, TFT_BLACK);
    M5.Display.drawString(g_sound_enabled ? "Audio: ON " : "Audio: OFF", 180, 125);

    delay(80);
}

```

### Build & Upload

```bash
pio run -t upload

```

---

## 7. Troubleshooting & Diagnostic Matrix

| Symptom | Primary Cause | Resolution |
| --- | --- | --- |
| **Buttons never respond to clicks** | Missing `M5.update();` in `loop()`. | Add `M5.update();` at the beginning of the `loop()` function to allow debounce calculation. |
| **LCD displays inverted or mirrored text** | Incorrect display orientation setting. | Use `M5.Display.setRotation(1)` or `M5.Display.setRotation(3)` for correct landscape viewing. |
| **IMU returns `0.00` for all axes** | I2C peripheral initialized before power rails stabilized. | Ensure `auto cfg = M5.config(); M5.begin(cfg);` runs before calling any `M5.Imu` methods. |
| **Screen flickers aggressively** | Redrawing entire screens (`fillScreen()`) inside every loop pass. | Redraw static frames once in `setup()`. Use formatted text prints with a background color (`setTextColor(TFT_WHITE, TFT_BLACK)`) to overwrite only changing numbers. |
| **Speaker produces no sound** | Volume is muted or hardware disabled. | Add `M5.Speaker.setVolume(128);` in `setup()` to set volume to 50%. |

---

## 8. Hands-On Verification Lab

Complete the following tasks to verify your setup:

* [ ] **Task 4.1:** Flash Exercise 4.4 and tilt the device vertically so only the Z-axis aligns with Earth's gravity. Verify that the display reads $\approx 1.00\text{ G}$.
* [ ] **Task 4.2:** Tap the device against your desk to trigger an `ALARM` state ($> 1.45\text{ G}$). Confirm that the buzzer sounds and peak vibration updates.
* [ ] **Task 4.3:** Click **Button A** to reset the peak hold metric. Click **Button B** to toggle audio feedback off and confirm the buzzer mutes during alarm conditions.

---

## 9. Version Control Protocol

Commit your hardware peripheral implementation:

```bash
git add platformio.ini src/main.cpp
git commit -m "feat(lesson04): implement physical peripheral drivers for m5stick s3 (lcd, imu, buttons, pmic, speaker)"

```

---
