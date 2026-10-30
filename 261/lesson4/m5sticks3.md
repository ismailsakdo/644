# Lesson 04: M5Stack Stick S3 Hardware Peripherals — Display, IMU, Buttons & Power Management

This module transitions from synthetic simulations to physical hardware execution on the **M5Stack Stick S3**. You will integrate the official **`M5Unified`** hardware abstraction library to interface directly with the device's onboard peripherals: the **ST7789v2 TFT LCD (135 $\times$ 240)**, the **BMI270 6-axis IMU**, the **AXP2101 / M5PM1 power management IC (PMIC)**, the **tactile switches (BtnA & BtnB)**, and the **piezo buzzer**.

```text
                  M5STACK STICK S3 ONBOARD PERIPHERAL BUS
                  
                         +------------------------+
                         | ESP32-S3 Dual-Core MCU |
                         +------------------------+
                                     |
         +-----------------+---------+---------+-----------------+
         | SPI Bus         | I2C Bus           | GPIO / PWM      | Internal ADC / PMIC
         v                 v                   v                 v
   +-----------+     +-----------+       +-----------+     +-----------+
   |  ST7789v2 |     |  BMI270   |       |  Buttons  |     |   PMIC    |
   | Color LCD |     | 6-Axis IMU|       | (BtnA/B)  |     | (Battery) |
   | (135x240) |     | Accel/Gyro|       |  & Buzzer |     | Fuel Gauge|
   +-----------+     +-----------+       +-----------+     +-----------+

```

---

## 1. Project Directory Layout & `platformio.ini`

Keep the workspace structure single-file and consistent with the rest of the journey:

```text
lesson04-hardware-peripherals/
├── platformio.ini
└── src/
    └── main.cpp

```

Initialize the project directory:

```bash
mkdir -p lesson04-hardware-peripherals/src
cd lesson04-hardware-peripherals

```

### Verified `platformio.ini`

This configuration eliminates obsolete platform tags and missing partition files. It uses the standard Espressif 32 toolchain, enables native USB CDC serial output, and pulls `M5Unified` directly from the registry:

```ini
; =====================================================================
; PlatformIO Configuration: Lesson 04 - M5Stack Stick S3 Peripherals
; Board: M5Stack Stick S3 (Espressif ESP32-S3)
; =====================================================================

[platformio]
default_envs = m5stack-s3

[env:m5stack-s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

; Serial Monitor & Upload Configuration
monitor_speed = 115200
upload_speed = 921600

; Native ESP32-S3 USB CDC Configuration
build_flags =
    -std=gnu++17
    -DESP32S3
    -DARDUINO_M5STACK_STICK_S3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -O2

; Hardware Driver Libraries
lib_deps =
    m5stack/M5Unified
    m5stack/M5GFX

```

---

## 2. The `M5Unified` Architecture on Stick S3

`M5Unified` provides a unified hardware driver tree across all M5 devices. To guarantee that the ESP32-S3 initializes the Stick S3 display, power rails, and buzzer correctly regardless of the generic board definition, configure `cfg.fallback_board = m5::board_t::board_M5StickS3;`:

* **`M5.begin(cfg)`:** Powers up internal I2C/SPI buses, enables PMIC output rails (L3A/L3B power lines), configures the ST7789 display panel, and initializes input GPIOs.
* **`M5.update()`:** Non-blocking hardware polling routine that **must be invoked once per `loop()` cycle**. It computes button debounce state machines (click, double-click, hold) and refreshes power telemetry.
* **`M5.Display`:** Controls the $135 \times 240$ ST7789 LCD (geometric primitives, text rendering, orientation, and backlight brightness).
* **`M5.Imu`:** Communicates with the Bosch BMI270 6-axis inertial sensor over internal I2C.
* **`M5.Power`:** Interfaces with the PMIC to read battery capacity percentage, bus voltage, and USB connection status.
* **`M5.Speaker`:** Drives the onboard acoustic transducer. Volume defaults to muted and must be unmuted via `M5.Speaker.setVolume()`.

---

## 3. Exercise 4.1: Display Canvas & Graphic Formatting

### Objective

Initialize the ST7789 display in landscape orientation ($240 \times 135$), set backlight brightness, eliminate screen flicker by pairing text foreground colors with explicit background colors, and render dynamic dashboard telemetry.

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 4.1: M5Stack Stick S3 Display Engine & Visual Layout
 * Hardware: ST7789v2 IPS LCD (240x135 Landscape)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>

static uint32_t frame_counter = 0;
static unsigned long previous_millis = 0;
static float current_fps = 0.0f;

void renderStaticLayout() {
    // 1. Clear display background
    M5.Display.fillScreen(0x1084); // Dark slate blue

    // 2. Top Header Bar (240x22 pixels)
    M5.Display.fillRect(0, 0, 240, 22, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("M5STICK S3 DISPLAY ENGINE", 120, 11, &fonts::Font2);

    // 3. Grid Dividers
    M5.Display.drawFastHLine(10, 28, 220, TFT_DARKGRAY);
    M5.Display.drawFastHLine(10, 80, 220, TFT_DARKGRAY);

    // 4. Static Field Labels
    M5.Display.setTextColor(TFT_LIGHTGRAY, 0x1084);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.drawString("FRAME:", 15, 36, &fonts::Font2);
    M5.Display.drawString("FPS:", 135, 36, &fonts::Font2);
    M5.Display.drawString("UPTIME:", 15, 58, &fonts::Font2);

    // 5. Progress Bar Bounding Box
    M5.Display.drawRect(10, 92, 220, 16, TFT_LIGHTGRAY);
}

void setup() {
    Serial.begin(115200);

    // Explicit hardware configuration for M5Stick S3
    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_spk = true;
    M5.begin(cfg);

    // Set 240x135 Landscape Mode and Backlight Brightness
    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

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

    // --- Dynamic Updates: Explicit text background prevents flicker ---

    // Zone 1: Frame Count
    char buf[32];
    snprintf(buf, sizeof(buf), "%-6u", frame_counter);
    M5.Display.setTextColor(TFT_GREENYELLOW, 0x1084);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.drawString(buf, 75, 36, &fonts::Font2);

    // Zone 2: FPS Counter
    snprintf(buf, sizeof(buf), "%4.1f", current_fps);
    M5.Display.setTextColor(TFT_CYAN, 0x1084);
    M5.Display.drawString(buf, 175, 36, &fonts::Font2);

    // Zone 3: Uptime Clock (HH:MM:SS)
    uint32_t total_sec = millis() / 1000;
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (total_sec / 3600) % 24, (total_sec / 60) % 60, total_sec % 60);
    M5.Display.setTextColor(TFT_WHITE, 0x1084);
    M5.Display.drawString(buf, 85, 58, &fonts::Font2);

    // Zone 4: Dynamic Progress Bar (216px internal width)
    int bar_progress = (millis() / 25) % 216;
    M5.Display.fillRect(12, 94, bar_progress, 12, TFT_BLUE);
    M5.Display.fillRect(12 + bar_progress, 94, 216 - bar_progress, 12, 0x1084);

    // Zone 5: Dynamic Heartbeat Indicator
    bool blink = (millis() / 500) % 2;
    M5.Display.fillCircle(224, 11, 4, blink ? TFT_GREEN : TFT_NAVY);

    delay(33); // Target ~30 FPS frame rate
}

```

### Build & Upload

```bash
pio run -t upload
pio device monitor

```

---

## 4. Exercise 4.2: Hardware Buttons & Acoustic Alerts

### Objective

Capture physical button inputs with debouncing via `M5.update()`, distinguish between short clicks and long holds, and generate acoustic feedback using `M5.Speaker`.

* **Button A (Front "M5" Face):** Increments manual audit counter; emits 1200 Hz confirmation chirp.
* **Button B (Side Edge):** Short press resets counter; long press ($> 800\text{ ms}$) toggles Siren/Alarm mode.

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
 * Exercise 4.2: Hardware Buttons & Acoustic Transducer
 * Hardware: M5Stack Stick S3 (BtnA, BtnB, Onboard Buzzer)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>

static uint32_t manual_audit_count = 0;
static bool alarm_mode_active = false;

void drawInterface() {
    M5.Display.fillScreen(TFT_BLACK);

    // Header Banner
    M5.Display.fillRect(0, 0, 240, 22, TFT_DARKCYAN);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("BUTTON & BUZZER DEMO", 120, 11, &fonts::Font2);

    // Instructions
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA (Front): Audit Count", 120, 38, &fonts::Font0);
    M5.Display.drawString("BtnB (Side): Reset | Hold Siren", 120, 52, &fonts::Font0);

    // Audit Counter Display
    char count_buf[32];
    snprintf(count_buf, sizeof(count_buf), "Audits: %u", manual_audit_count);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString(count_buf, 120, 80, &fonts::Font4);

    // Status Banner
    if (alarm_mode_active) {
        M5.Display.fillRect(0, 108, 240, 27, TFT_RED);
        M5.Display.setTextColor(TFT_WHITE, TFT_RED);
        M5.Display.drawString("!! SIREN MODE ACTIVE !!", 120, 121, &fonts::Font2);
    } else {
        M5.Display.fillRect(0, 108, 240, 27, TFT_BLACK);
        M5.Display.drawRect(10, 108, 220, 25, TFT_DARKGREEN);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.drawString("SYSTEM READY", 120, 121, &fonts::Font2);
    }
}

void setup() {
    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_spk = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    // Unmute speaker (Volume range: 0 - 255)
    M5.Speaker.setVolume(140);

    drawInterface();
}

void loop() {
    // Process button debouncing and hardware registers
    M5.update();

    // 1. Process Button A (Front Panel "M5" Button)
    if (M5.BtnA.wasClicked()) {
        manual_audit_count++;
        M5.Speaker.tone(1200, 80); // 1200 Hz tone for 80 ms
        drawInterface();
    }

    // 2. Process Button B (Side Button) - Click Event
    if (M5.BtnB.wasClicked()) {
        manual_audit_count = 0;
        alarm_mode_active = false;

        // Dual chirp acknowledgment
        M5.Speaker.tone(2000, 50);
        delay(60);
        M5.Speaker.tone(3000, 50);

        drawInterface();
    }

    // 3. Process Button B (Side Button) - Hold Event (> 800 ms)
    if (M5.BtnB.wasHold()) {
        alarm_mode_active = !alarm_mode_active;

        if (alarm_mode_active) {
            M5.Speaker.tone(800, 250);
        } else {
            M5.Speaker.tone(400, 150);
        }
        drawInterface();
    }

    delay(20);
}

```

### Build & Upload

```bash
pio run -t upload

```

---

## 5. Exercise 4.3: Real Sensor Acquisition — BMI270 6-Axis IMU

### Objective

Sample physical linear acceleration ($a_x, a_y, a_z$) and angular velocity ($g_x, g_y, g_z$) using the onboard Bosch **BMI270** sensor. Calculate the total vector acceleration magnitude ($A$) using the Euclidean norm:

$$\Vert{}A\Vert{} = \sqrt{a_x^2 + a_y^2 + a_z^2}$$

* Under stationary conditions on Earth: $\Vert{}A\Vert{} \approx 1.00\text{ G}$.
* Physical impacts or shaking increase the magnitude: $\Vert{}A\Vert{} > 1.65\text{ G}$.

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 4.3: Real Motion Sensing with the BMI270 IMU
 * Hardware: M5Stack Stick S3 (Bosch BMI270 6-Axis IMU)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>

const float IMPACT_THRESHOLD_G = 1.65f;

void setup() {
    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_spk = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setBrightness(180);

    M5.Speaker.setVolume(140);

    // Header
    M5.Display.fillRect(0, 0, 240, 22, TFT_MAROON);
    M5.Display.setTextColor(TFT_WHITE, TFT_MAROON);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("BMI270 REAL-TIME IMU", 120, 11, &fonts::Font2);
}

void loop() {
    M5.update();

    // Read real sensor registers
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    M5.Imu.getAccel(&ax, &ay, &az);
    M5.Imu.getGyro(&gx, &gy, &gz);

    // Compute Euclidean acceleration magnitude
    float total_magnitude = sqrtf((ax * ax) + (ay * ay) + (az * az));

    // Dynamic telemetry readout
    char buf[48];
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    snprintf(buf, sizeof(buf), "AccX: %+5.2f G   GyrX: %+6.1f d/s", ax, gx);
    M5.Display.drawString(buf, 10, 30, &fonts::Font0);

    snprintf(buf, sizeof(buf), "AccY: %+5.2f G   GyrY: %+6.1f d/s", ay, gy);
    M5.Display.drawString(buf, 10, 44, &fonts::Font0);

    snprintf(buf, sizeof(buf), "AccZ: %+5.2f G   GyrZ: %+6.1f d/s", az, gz);
    M5.Display.drawString(buf, 10, 58, &fonts::Font0);

    // Threshold Alert Zone
    M5.Display.setTextDatum(textdatum_t::middle_center);
    if (total_magnitude > IMPACT_THRESHOLD_G) {
        M5.Display.fillRect(10, 76, 220, 52, TFT_RED);
        M5.Display.setTextColor(TFT_WHITE, TFT_RED);
        M5.Display.drawString("SHOCK DETECTED", 120, 92, &fonts::Font2);

        snprintf(buf, sizeof(buf), "%.2f G EXCEEDED", total_magnitude);
        M5.Display.drawString(buf, 120, 114, &fonts::Font2);

        M5.Speaker.tone(1800, 30);
    } else {
        M5.Display.fillRect(10, 76, 220, 52, TFT_DARKGREEN);
        M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        M5.Display.drawString("MOTION NOMINAL", 120, 92, &fonts::Font2);

        snprintf(buf, sizeof(buf), "Vector: %.2f G", total_magnitude);
        M5.Display.drawString(buf, 120, 114, &fonts::Font2);
    }

    delay(40); // 25 Hz update rate
}

```

### Build & Upload

```bash
pio run -t upload

```

*Tilt the device along the X, Y, and Z axes to see gravitational alignment. Tap the device lightly against the desk to trigger the shock alert.*

---

## 6. Exercise 4.4: Integrated Handheld Telemetry Node

### Objective

Consolidate all subsystems into a unified, industrial-grade monitoring device:

1. **ST7789 Color Display:** Live status dashboard with targeted updates.
2. **BMI270 IMU:** Continuous vibration monitoring with peak-hold latching.
3. **PMIC Subsystem:** Real-time battery charge percentage and terminal voltage.
4. **Button Controls:** **BtnA** resets the peak held metric; **BtnB** toggles acoustic alerts ON/OFF.
5. **Acoustic Transducer:** Dynamic sound notifications on limit breach.

```text
+-------------------------------------------------------------+
| M5STICK S3 FLEET MONITOR               BAT: 92% (4.12V)     | <- PMIC
+-------------------------------------------------------------+
|  VIBRATION VECTOR                      OPERATING STATE      |
|                                                             |
|         1.03 G                             OPTIMAL          | <- BMI270 IMU
|  (Peak: 1.48 G)                                             |
+-------------------------------------------------------------+
| [BtnA] Reset Peak             | [BtnB] Audio: ON            | <- Buttons & Buzzer
+-------------------------------------------------------------+

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 4.4: Complete Hardware Peripherals Integration
 * Target: M5Stack Stick S3 (LCD, BMI270, PMIC, Buttons, Buzzer)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>

static float g_peak_vibration = 0.0f;
static bool g_sound_enabled = true;
const float VIBRATION_LIMIT_G = 1.45f;

void drawBaseDashboard() {
    M5.Display.fillScreen(TFT_BLACK);

    // Top Header
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);

    // Grid Lines
    M5.Display.drawFastHLine(0, 96, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 20, 76, TFT_DARKGRAY);

    // Section Labels
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VIB MAGNITUDE", 60, 25, &fonts::Font0);
    M5.Display.drawString("OPERATING STATE", 180, 25, &fonts::Font0);

    // Bottom Navigation Guide
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Reset Peak", 60, 116, &fonts::Font0);
}

void setup() {
    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_spk = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);
    M5.Speaker.setVolume(140);

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

    // 2. Power Subsystem Polling (PMIC)
    int battery_percentage = M5.Power.getBatteryLevel();
    int battery_mv = M5.Power.getBatteryVoltage();
    float battery_voltage = (battery_mv > 0) ? (battery_mv / 1000.0f) : 0.0f;

    // 3. Button Interactions
    if (M5.BtnA.wasClicked()) {
        g_peak_vibration = current_vib;
        if (g_sound_enabled) M5.Speaker.tone(1200, 40);
    }

    if (M5.BtnB.wasClicked()) {
        g_sound_enabled = !g_sound_enabled;
        if (g_sound_enabled) M5.Speaker.tone(2200, 60);
    }

    // 4. Header Bar: Title and Battery Gauge
    char buf[40];
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.drawString(" FLEET MONITOR", 5, 10, &fonts::Font2);

    M5.Display.setTextDatum(textdatum_t::middle_right);
    if (battery_percentage >= 0) {
        snprintf(buf, sizeof(buf), "BAT: %d%% (%.2fV) ", battery_percentage, battery_voltage);
    } else {
        snprintf(buf, sizeof(buf), "USB POWER ");
    }
    M5.Display.drawString(buf, 235, 10, &fonts::Font0);

    // 5. Left Panel: Vibration Telemetry
    M5.Display.setTextDatum(textdatum_t::middle_center);
    snprintf(buf, sizeof(buf), "%4.2f G", current_vib);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(buf, 60, 52, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Peak: %4.2f G", g_peak_vibration);
    M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5.Display.drawString(buf, 60, 78, &fonts::Font0);

    // 6. Right Panel: Operating State Card
    if (current_vib > VIBRATION_LIMIT_G) {
        M5.Display.fillRect(122, 21, 117, 74, TFT_RED);
        M5.Display.setTextColor(TFT_WHITE, TFT_RED);
        M5.Display.drawString("ALARM", 180, 46, &fonts::Font2);
        M5.Display.drawString("LIMIT BREACH", 180, 72, &fonts::Font0);

        if (g_sound_enabled) {
            M5.Speaker.tone(1600, 25);
        }
    } else {
        M5.Display.fillRect(122, 21, 117, 74, TFT_BLACK);
        M5.Display.drawFastVLine(120, 20, 76, TFT_DARKGRAY);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.drawString("OPTIMAL", 180, 46, &fonts::Font2);
        M5.Display.drawString("NOMINAL VIB", 180, 72, &fonts::Font0);
    }

    // 7. Audio State Toggle Button
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(g_sound_enabled ? TFT_GREENYELLOW : TFT_RED, TFT_BLACK);
    M5.Display.drawString(g_sound_enabled ? "BtnB: Sound [ON] " : "BtnB: Sound [OFF]", 180, 116, &fonts::Font0);

    delay(40);
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
| **PlatformIO build fails with missing package** | Invalid version tag like `espressif32@6.12.0`. | Use `platform = espressif32` in `platformio.ini`. |
| **Compilation error: `default_8MB.csv` not found** | Partition file declared but not present in workspace. | Remove `board_build.partitions = default_8MB.csv` from `platformio.ini`. |
| **Display remains black after flashing** | Board identity not recognized by M5Unified. | Add `cfg.fallback_board = m5::board_t::board_M5StickS3;` before `M5.begin(cfg);`. |
| **Speaker produces no sound** | Muted volume or unconfigured internal speaker. | Ensure `cfg.internal_spk = true;` is set before `M5.begin(cfg);` and call `M5.Speaker.setVolume(140);`. |
| **Buttons never respond to clicks** | Missing `M5.update();` in `loop()`. | `M5.update()` must execute every cycle to calculate debounce states. |
| **Text smears or overlaps** | Using transparent text backgrounds when updating numbers. | Set both foreground and background colors in `setTextColor()` (e.g., `setTextColor(TFT_WHITE, TFT_BLACK)`). |

---

## 8. Hands-On Verification Lab

Complete the following validation tasks:

* [ ] **Task 4.1:** Flash Exercise 4.4. Place the M5Stick S3 flat on your desk and confirm that the total vibration magnitude reads $\approx 1.00\text{ G}$ ($\pm 0.08\text{ G}$).
* [ ] **Task 4.2:** Tap the table near the device to exceed $1.45\text{ G}$. Verify that the right panel transitions to the red `ALARM` state, the buzzer beeps, and the peak value latches.
* [ ] **Task 4.3:** Click **Button A** on the front face to reset the latched peak value. Click **Button B** on the side to toggle audio mute, and verify that alarm events stay silent when `Sound [OFF]` is displayed.

---

## 9. Version Control Protocol

Commit your verified hardware peripheral code:

```bash
git add platformio.ini src/main.cpp
git commit -m "feat(lesson04): implement verified peripheral drivers for m5stick s3 (lcd, imu, buttons, pmic, speaker)"

```
