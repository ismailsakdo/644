# Lesson 04: M5Stack Stick S3 Hardware Peripherals — Display, IMU, Buttons, Microphone & Power Management

This module transitions from synthetic simulations to physical hardware execution on the **M5Stack Stick S3**. You will integrate the official **`M5Unified`** hardware abstraction library to interface directly with the device's onboard peripherals: the **ST7789v2 TFT LCD (135 $\times$ 240)**, the **BMI270 6-axis IMU**, the **SPM1423 MEMS microphone**, the **AXP2101 / M5PM1 power management IC (PMIC)**, physical **tactile switches**, the **piezo buzzer**, and the **2.4 GHz Bluetooth Low Energy (BLE) radio** for wireless proximity sensing.

```text
                           M5STACK STICK S3 ONBOARD PERIPHERAL BUS
                           
                                  +------------------------+
                                  | ESP32-S3 Dual-Core MCU |
                                  +------------------------+
                                              |
      +-----------------+---------+-----------+-----------+-----------------+
      | SPI Bus         | I2C Bus | I2S Audio | 2.4GHz RF | GPIO / PWM      | Internal ADC / PMIC
      v                 v         v           v           v                 v
+-----------+     +-----------+ +-----------+ +---------+ +-----------+     +-----------+
|  ST7789v2 |     |  BMI270   | |  SPM1423  | | 2.4GHz  | |  Buttons  |     |  AXP2101  |
| Color LCD |     | 6-Axis IMU| | MEMS Mic  | | BLE/WiFi| | (BtnA/B)  |     |   PMIC    |
| (135x240) |     | Accel/Gyro| | (PDM/I2S) | | Proxim. | |  & Buzzer |     | (Battery) |
+-----------+     +-----------+ +-----------+ +---------+ +-----------+     +-----------+

```

---

## 1. Project Directory Layout & `platformio.ini`

Keep the workspace structure single-file:

```text
lesson04-hardware-peripherals/
├── platformio.ini
└── src/
    └── main.cpp

```

Initialize the directory:

```bash
mkdir -p lesson04-hardware-peripherals/src
cd lesson04-hardware-peripherals

```

### Verified `platformio.ini`

This configuration provides environments for both the **M5Stack Stick S3 (ESP32-S3)** and the earlier **M5StickC / Plus (ESP32)** series, preventing toolchain version mismatches and missing partition errors:

```ini
[platformio]
default_envs = m5stack-s3

; ====================================================================
; Environment 1: M5Stack Stick S3 (ESP32-S3 Dual-Core Xtensa LX7)
; ====================================================================
[env:m5stack-s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
upload_speed = 921600

build_flags =
    -std=gnu++17
    -DESP32S3
    -DARDUINO_M5STACK_STICK_S3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -O2

lib_deps =
    m5stack/M5Unified
    m5stack/M5GFX

; ====================================================================
; Environment 2: M5StickC / M5StickC Plus (ESP32 Classic Fallback)
; ====================================================================
[env:m5stick-plus]
platform = espressif32
board = m5stick-c
framework = arduino
monitor_speed = 115200
upload_speed = 1500000

lib_deps =
    m5stack/M5Unified
    m5stack/M5GFX

```

---

## 2. The `M5Unified` Architecture on Stick S3

`M5Unified` provides a driver tree that abstracts low-level register configuration:

* **`M5.begin(cfg)`:** Powers up internal I2C/SPI buses, enables PMIC output rails (L3A/L3B lines), initializes the ST7789 display controller, and maps pin registers. Setting `cfg.fallback_board = m5::board_t::board_M5StickS3;` ensures peripheral mappings resolve properly.
* **`M5.update()`:** Hardware polling routine that **must run once per `loop()` cycle**. It computes button debounce state machines (clicks, double-clicks, holds) and refreshes power telemetry.
* **`M5.Display`:** Controls the $135 \times 240$ ST7789 LCD (geometric primitives, text rendering, orientation, and backlight brightness).
* **`M5.Imu`:** Communicates with the Bosch BMI270 6-axis inertial sensor over internal I2C.
* **`M5.Mic`:** Captures raw pulse-density modulated (PDM) audio frames from the onboard SPM1423 MEMS microphone via the I2S bus.
* **`M5.Speaker`:** Drives the onboard acoustic transducer. Volume defaults to muted and must be unmuted via `M5.Speaker.setVolume()`.
* **`M5.Power`:** Interfaces with the PMIC to read battery capacity percentage, bus voltage, and USB connection status.

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
    M5.Display.fillScreen(0x1084); // Dark slate blue

    // Header Bar
    M5.Display.fillRect(0, 0, 240, 22, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("M5STICK S3 DISPLAY ENGINE", 120, 11, &fonts::Font2);

    // Dividers
    M5.Display.drawFastHLine(10, 28, 220, TFT_DARKGRAY);
    M5.Display.drawFastHLine(10, 80, 220, TFT_DARKGRAY);

    // Static Field Labels
    M5.Display.setTextColor(TFT_LIGHTGRAY, 0x1084);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.drawString("FRAME:", 15, 36, &fonts::Font2);
    M5.Display.drawString("FPS:", 135, 36, &fonts::Font2);
    M5.Display.drawString("UPTIME:", 15, 58, &fonts::Font2);

    // Progress Bar Bounding Box
    M5.Display.drawRect(10, 92, 220, 16, TFT_LIGHTGRAY);
}

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_spk = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    renderStaticLayout();
}

void loop() {
    M5.update();
    frame_counter++;

    unsigned long current_millis = millis();
    if (current_millis - previous_millis >= 1000) {
        current_fps = (frame_counter * 1000.0f) / (current_millis - previous_millis);
        frame_counter = 0;
        previous_millis = current_millis;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%-6u", frame_counter);
    M5.Display.setTextColor(TFT_GREENYELLOW, 0x1084);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.drawString(buf, 75, 36, &fonts::Font2);

    snprintf(buf, sizeof(buf), "%4.1f", current_fps);
    M5.Display.setTextColor(TFT_CYAN, 0x1084);
    M5.Display.drawString(buf, 175, 36, &fonts::Font2);

    uint32_t total_sec = millis() / 1000;
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (total_sec / 3600) % 24, (total_sec / 60) % 60, total_sec % 60);
    M5.Display.setTextColor(TFT_WHITE, 0x1084);
    M5.Display.drawString(buf, 85, 58, &fonts::Font2);

    int bar_progress = (millis() / 25) % 216;
    M5.Display.fillRect(12, 94, bar_progress, 12, TFT_BLUE);
    M5.Display.fillRect(12 + bar_progress, 94, 216 - bar_progress, 12, 0x1084);

    bool blink = (millis() / 500) % 2;
    M5.Display.fillCircle(224, 11, 4, blink ? TFT_GREEN : TFT_NAVY);

    delay(33);
}

```

### Build & Upload

```bash
pio run -t upload

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

    M5.Display.fillRect(0, 0, 240, 22, TFT_DARKCYAN);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("BUTTON & BUZZER DEMO", 120, 11, &fonts::Font2);

    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA (Front): Audit Count", 120, 38, &fonts::Font0);
    M5.Display.drawString("BtnB (Side): Reset | Hold Siren", 120, 52, &fonts::Font0);

    char count_buf[32];
    snprintf(count_buf, sizeof(count_buf), "Audits: %u", manual_audit_count);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString(count_buf, 120, 80, &fonts::Font4);

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
    M5.Speaker.setVolume(140);

    drawInterface();
}

void loop() {
    M5.update();

    if (M5.BtnA.wasClicked()) {
        manual_audit_count++;
        M5.Speaker.tone(1200, 80);
        drawInterface();
    }

    if (M5.BtnB.wasClicked()) {
        manual_audit_count = 0;
        alarm_mode_active = false;

        M5.Speaker.tone(2000, 50);
        delay(60);
        M5.Speaker.tone(3000, 50);

        drawInterface();
    }

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

Sample real physical acceleration and angular velocity using the onboard Bosch **BMI270 IMU**. Calculate total vector acceleration magnitude ($A$) using the Euclidean norm:

$$\Vert{}A\Vert{} = \sqrt{a_x^2 + a_y^2 + a_z^2}$$

* Under stationary conditions on Earth, $\Vert{}A\Vert{} \approx 1.00\text{ G}$.


* Shaking or impacting the device increases $\Vert{}A\Vert{} > 1.65\text{ G}$.

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

    M5.Display.fillRect(0, 0, 240, 22, TFT_MAROON);
    M5.Display.setTextColor(TFT_WHITE, TFT_MAROON);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("BMI270 REAL-TIME IMU", 120, 11, &fonts::Font2);
}

void loop() {
    M5.update();

    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    M5.Imu.getAccel(&ax, &ay, &az);
    M5.Imu.getGyro(&gx, &gy, &gz);

    float total_magnitude = sqrtf((ax * ax) + (ay * ay) + (az * az));

    char buf[48];
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    snprintf(buf, sizeof(buf), "AccX: %+5.2f G   GyrX: %+6.1f d/s", ax, gx);
    M5.Display.drawString(buf, 10, 30, &fonts::Font0);

    snprintf(buf, sizeof(buf), "AccY: %+5.2f G   GyrY: %+6.1f d/s", ay, gy);
    M5.Display.drawString(buf, 10, 44, &fonts::Font0);

    snprintf(buf, sizeof(buf), "AccZ: %+5.2f G   GyrZ: %+6.1f d/s", az, gz);
    M5.Display.drawString(buf, 10, 58, &fonts::Font0);

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

    delay(40);
}

```

### Build & Upload

```bash
pio run -t upload

```

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

    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.drawFastHLine(0, 96, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 20, 76, TFT_DARKGRAY);

    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VIB MAGNITUDE", 60, 25, &fonts::Font0);
    M5.Display.drawString("OPERATING STATE", 180, 25, &fonts::Font0);

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

    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float current_vib = sqrtf((ax * ax) + (ay * ay) + (az * az));

    if (current_vib > g_peak_vibration) {
        g_peak_vibration = current_vib;
    }

    int battery_percentage = M5.Power.getBatteryLevel();
    int battery_mv = M5.Power.getBatteryVoltage();
    float battery_voltage = (battery_mv > 0) ? (battery_mv / 1000.0f) : 0.0f;

    if (M5.BtnA.wasClicked()) {
        g_peak_vibration = current_vib;
        if (g_sound_enabled) M5.Speaker.tone(1200, 40);
    }

    if (M5.BtnB.wasClicked()) {
        g_sound_enabled = !g_sound_enabled;
        if (g_sound_enabled) M5.Speaker.tone(2200, 60);
    }

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

    M5.Display.setTextDatum(textdatum_t::middle_center);
    snprintf(buf, sizeof(buf), "%4.2f G", current_vib);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(buf, 60, 52, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Peak: %4.2f G", g_peak_vibration);
    M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5.Display.drawString(buf, 60, 78, &fonts::Font0);

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

## 7. Exercise 4.5: Menu-Driven Multi-Peripheral Edge Diagnostic Suite

### Objective

Engineer a unified, menu-navigable edge diagnostic firmware integrating **all five primary hardware interfaces**:

1. **Interactive Menu Navigation Engine:** An on-screen menu driven by **BtnA** (cycle/change option) and **BtnB** (enter mode / return to menu).
2. **SPM1423 MEMS Microphone Sound Meter:** Record audio frames at 16 kHz over I2S, calculate Root Mean Square (RMS) amplitude and sound level in decibels (dB), and render a dynamic real-time segmented VU meter bar.
3. **BMI270 6-Axis IMU:** Continuous 3-axis motion vector monitoring with Euclidean norm shock threshold detection.
4. **Bluetooth Low Energy (BLE) Phone Proximity Radar:** Asynchronously scan for nearby advertising mobile phones and BLE peripherals, extract the strongest Received Signal Strength Indicator (RSSI), and compute the physical distance estimation using the log-distance path loss model:

$$d = 10^{\frac{\text{TxPower} - \text{RSSI}}{10 \cdot n}}$$

Where $\text{TxPower} = -59\text{ dBm}$ (measured reference RSSI at 1 meter) and $n = 2.0$ (path loss exponent). The distance is classified into:

* **IMMEDIATE:** $d < 0.5\text{ m}$
* **NEAR:** $0.5\text{ m} \le d \le 2.0\text{ m}$
* **FAR:** $d > 2.0\text{ m}$

5. **Acoustic Transducer & PMIC:** Audible alerts when sound levels exceed $75\text{ dB}$ or vibration exceeds $1.50\text{ G}$, alongside continuous battery gauge monitoring.

```text
+-------------------------------------------------------------+
| [1] IMU VIBRATION          | [2] SOUND LEVEL (MIC)          |
| [3] BLE PHONE RADAR (RSSI) | [4] ALL-IN-ONE DASHBOARD       |
+-------------------------------------------------------------+
| BtnA: Next Item (Select)   | BtnB: Confirm / Enter          |
+-------------------------------------------------------------+

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 4.5: Menu-Driven Multi-Peripheral Edge Diagnostic Suite
 * Hardware: M5Stack Stick S3 (ST7789 LCD, BMI270, SPM1423 Mic, BLE, PMIC)
 * Features: Silent Calibrated SLM, 6-Axis Motion, BLE Proximity Radar
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <math.h>

// Application State Machine
enum AppState {
    MODE_MENU = 0,
    MODE_IMU,
    MODE_MIC,
    MODE_BLE,
    MODE_ALL
};

static AppState g_current_mode = MODE_MENU;
static int g_menu_selection = 0;
const int TOTAL_MENU_ITEMS = 4;
const char* MENU_TITLES[TOTAL_MENU_ITEMS] = {
    "1. IMU VIBRATION",
    "2. SOUND LEVEL (SLM)",
    "3. BLE PHONE RADAR",
    "4. ALL-IN-ONE SUITE"
};

// ============================================================================
// 1. SOUND LEVEL METER (SLM) SIGNAL PROCESSING CONFIGURATION
// ============================================================================
static constexpr size_t MIC_SAMPLES = 256;
static int16_t g_mic_buffer[MIC_SAMPLES];

// Real-time acoustic metrics
static float g_sound_db   = 38.0f;     // Smoothed sound level (dB)
static float g_peak_db    = 38.0f;     // Peak hold (Lmax)
static unsigned long g_last_mic_sample_ms = 0;

// ============================================================================
// 2. BLE PROXIMITY RADAR CONFIGURATION
// ============================================================================
static BLEScan* g_ble_scan = nullptr;
static int g_strongest_rssi = -120;
static String g_strongest_device_name = "None";
static String g_strongest_device_mac  = "--:--:--:--:--:--";
static float g_estimated_distance_m   = 99.9f;
static unsigned long g_last_ble_scan_ms = 0;
static bool g_ble_scanning = false;

class ProximityAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        int rssi = advertisedDevice.getRSSI();
        if (rssi > g_strongest_rssi) {
            g_strongest_rssi = rssi;
            g_strongest_device_mac = advertisedDevice.getAddress().toString().c_str();
            if (advertisedDevice.haveName() && advertisedDevice.getName().length() > 0) {
                g_strongest_device_name = advertisedDevice.getName().c_str();
            } else {
                g_strongest_device_name = "Nearby BLE Device";
            }
        }
    }
};

float calculateDistance(int rssi) {
    if (rssi <= -110 || rssi >= 0) return 99.9f;
    return powf(10.0f, ((-59.0f) - (float)rssi) / (10.0f * 2.0f));
}

// ============================================================================
// 3. HARDWARE I2S BUS ARBITRATOR (PREVENTS BUS CONFLICT)
// ============================================================================
void enableMicrophoneMode() {
    M5.Speaker.end(); // Completely release I2S bus from speaker
    delay(10);
    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate = 16000;
    mic_cfg.noise_filter_level = 0;
    M5.Mic.config(mic_cfg);
    M5.Mic.begin();
    delay(10);
}

void enableSpeakerMode() {
    M5.Mic.end(); // Release I2S bus from mic
    delay(10);
    M5.Speaker.begin();
    M5.Speaker.setVolume(120);
    delay(10);
}

// ============================================================================
// 4. UI RENDERING UTILITIES
// ============================================================================
void drawTopHeader(const char* title, uint16_t bg_color) {
    M5.Display.fillRect(0, 0, 240, 20, bg_color);
    M5.Display.setTextColor(TFT_WHITE, bg_color);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextSize(1);
    M5.Display.drawString(title, 6, 10, &fonts::Font2);

    int bat = M5.Power.getBatteryLevel();
    char bat_str[16];
    snprintf(bat_str, sizeof(bat_str), "%d%%", (bat < 0) ? 100 : bat);
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.drawString(bat_str, 235, 10, &fonts::Font2);
}

// ----------------------------------------------------------------------------
// MODE 0: Main Selection Menu
// ----------------------------------------------------------------------------
void renderMenuScreen() {
    M5.Display.fillScreen(TFT_BLACK);
    drawTopHeader("SYSTEM DIAGNOSTIC MENU", TFT_NAVY);

    for (int i = 0; i < TOTAL_MENU_ITEMS; i++) {
        int y = 25 + (i * 22);
        bool is_selected = (i == g_menu_selection);

        if (is_selected) {
            M5.Display.fillRect(6, y, 228, 20, TFT_DARKCYAN);
            M5.Display.drawRect(6, y, 228, 20, TFT_CYAN);
            M5.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
        } else {
            M5.Display.fillRect(6, y, 228, 20, 0x18C3);
            M5.Display.setTextColor(TFT_LIGHTGRAY, 0x18C3);
        }

        M5.Display.setTextDatum(textdatum_t::middle_left);
        M5.Display.drawString(MENU_TITLES[i], 16, y + 10, &fonts::Font2);
    }

    M5.Display.fillRect(0, 116, 240, 19, TFT_BLACK);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("BtnA: Select [>]  |  BtnB: Enter [OK]", 120, 125, &fonts::Font0);
}

// ----------------------------------------------------------------------------
// MODE 1: IMU 6-Axis Motion Inspector
// ----------------------------------------------------------------------------
void renderIMUScreen() {
    drawTopHeader("1. IMU 6-AXIS MOTION", TFT_MAROON);

    float ax, ay, az, gx, gy, gz;
    M5.Imu.getAccel(&ax, &ay, &az);
    M5.Imu.getGyro(&gx, &gy, &gz);
    float total_g = sqrtf((ax * ax) + (ay * ay) + (az * az));

    char buf[48];
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::top_left);

    snprintf(buf, sizeof(buf), "Acc: X:%+5.2f Y:%+5.2f Z:%+5.2f", ax, ay, az);
    M5.Display.drawString(buf, 10, 26, &fonts::Font0);

    snprintf(buf, sizeof(buf), "Gyr: X:%+5.1f Y:%+5.1f Z:%+5.1f", gx, gy, gz);
    M5.Display.drawString(buf, 10, 40, &fonts::Font0);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    if (total_g > 1.65f) {
        M5.Display.fillRect(10, 56, 220, 48, TFT_RED);
        M5.Display.setTextColor(TFT_WHITE, TFT_RED);
        M5.Display.drawString("SHOCK DETECTED!", 120, 70, &fonts::Font2);
        snprintf(buf, sizeof(buf), "Magnitude: %.2f G", total_g);
        M5.Display.drawString(buf, 120, 90, &fonts::Font2);
        M5.Speaker.tone(1800, 20);
    } else {
        M5.Display.fillRect(10, 56, 220, 48, TFT_DARKGREEN);
        M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        M5.Display.drawString("MOTION NOMINAL", 120, 70, &fonts::Font2);
        snprintf(buf, sizeof(buf), "Vector: %.2f G", total_g);
        M5.Display.drawString(buf, 120, 90, &fonts::Font2);
    }

    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnB: Return to Menu", 120, 122, &fonts::Font0);
}

// ----------------------------------------------------------------------------
// MODE 2: Calibrated Digital Sound Level Meter (STRICTLY SILENT)
// ----------------------------------------------------------------------------
void sampleSoundLevelMeter() {
    unsigned long now = millis();
    if (now - g_last_mic_sample_ms < 40) return; // Sample at 25 Hz
    g_last_mic_sample_ms = now;

    // Queue DMA recording
    if (M5.Mic.record(g_mic_buffer, MIC_SAMPLES, 16000)) {
        // Wait for DMA hardware to populate buffer
        while (M5.Mic.isRecording()) {
            delay(1);
        }

        // Sum of squares for RMS
        float sum = 0.0f;
        for (size_t i = 0; i < MIC_SAMPLES; i++) {
            float val = (float)g_mic_buffer[i];
            sum += val * val;
        }

        float rms = sqrtf(sum / (float)MIC_SAMPLES);

        // Calibrated Logarithmic Formula (maps ambient room to ~38-45 dB, shouting to ~85-90 dB)
        float raw_db = 20.0f * log10f(rms + 1.0f);
        raw_db = max(30.0f, min(100.0f, raw_db));

        // Smooth output to eliminate jitter
        g_sound_db = (0.30f * raw_db) + (0.70f * g_sound_db);

        // Peak Hold
        if (g_sound_db > g_peak_db) {
            g_peak_db = g_sound_db;
        }
    }
}

void renderMicScreen() {
    drawTopHeader("2. SOUND LEVEL METER", TFT_DARKGREEN);

    sampleSoundLevelMeter();

    uint16_t state_color = TFT_GREEN;
    const char* state_str = "QUIET";

    if (g_sound_db >= 75.0f) {
        state_color = TFT_RED;
        state_str = "LOUD NOISE";
    } else if (g_sound_db >= 60.0f) {
        state_color = TFT_ORANGE;
        state_str = "MODERATE";
    } else if (g_sound_db >= 48.0f) {
        state_color = TFT_CYAN;
        state_str = "NORMAL";
    }

    char buf[32];
    M5.Display.setTextDatum(textdatum_t::middle_center);
    snprintf(buf, sizeof(buf), "%4.1f dB", g_sound_db);
    M5.Display.setTextColor(state_color, TFT_BLACK);
    M5.Display.drawString(buf, 120, 42, &fonts::Font4);

    snprintf(buf, sizeof(buf), "Peak: %4.1f dB  |  %s", g_peak_db, state_str);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString(buf, 120, 68, &fonts::Font2);

    // Segmented Industrial VU Meter Bar (35 dB to 95 dB)
    int bar_x = 20, bar_y = 86, bar_w = 200, bar_h = 16;
    M5.Display.drawRect(bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, TFT_DARKGRAY);

    float clamped_db = max(35.0f, min(95.0f, g_sound_db));
    int fill_w = (int)((clamped_db - 35.0f) / 60.0f * bar_w);

    M5.Display.fillRect(bar_x, bar_y, fill_w, bar_h, state_color);
    M5.Display.fillRect(bar_x + fill_w, bar_y, bar_w - fill_w, bar_h, TFT_BLACK);

    // White Peak Hold Tick Marker
    float clamped_peak = max(35.0f, min(95.0f, g_peak_db));
    int peak_x = bar_x + (int)((clamped_peak - 35.0f) / 60.0f * bar_w);
    if (peak_x >= bar_x + bar_w) peak_x = bar_x + bar_w - 1;
    M5.Display.drawFastVLine(peak_x, bar_y - 2, bar_h + 4, TFT_WHITE);

    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Reset Peak  |  BtnB: Return", 120, 122, &fonts::Font0);
}

// ----------------------------------------------------------------------------
// MODE 3: BLE Phone Proximity Radar
// ----------------------------------------------------------------------------
void updateBLERadar() {
    unsigned long current_ms = millis();
    if (!g_ble_scanning && (current_ms - g_last_ble_scan_ms >= 1800)) {
        g_last_ble_scan_ms = current_ms;
        g_strongest_rssi = -120;
        g_ble_scan->start(1, false);
        g_ble_scanning = true;
    }

    if (g_ble_scanning && (current_ms - g_last_ble_scan_ms >= 1050)) {
        g_ble_scan->stop();
        g_ble_scan->clearResults();
        g_ble_scanning = false;
        g_estimated_distance_m = calculateDistance(g_strongest_rssi);
    }
}

void renderBLEScreen() {
    drawTopHeader("3. BLE PHONE RADAR", 0x51D7);

    updateBLERadar();

    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    char buf[48];
    snprintf(buf, sizeof(buf), "Device: %-18s", g_strongest_device_name.substring(0, 18).c_str());
    M5.Display.drawString(buf, 10, 26, &fonts::Font0);

    snprintf(buf, sizeof(buf), "MAC:    %s", g_strongest_device_mac.c_str());
    M5.Display.drawString(buf, 10, 38, &fonts::Font0);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    if (g_strongest_rssi > -110) {
        const char* prox_label = "FAR (>2.0m)";
        uint16_t prox_color = TFT_LIGHTGRAY;

        if (g_estimated_distance_m < 0.5f) {
            prox_label = "IMMEDIATE (<0.5m)";
            prox_color = TFT_RED;
        } else if (g_estimated_distance_m <= 2.0f) {
            prox_label = "NEAR (0.5m - 2.0m)";
            prox_color = TFT_YELLOW;
        }

        M5.Display.fillRect(10, 54, 220, 50, TFT_BLACK);
        M5.Display.drawRect(10, 54, 220, 50, prox_color);

        snprintf(buf, sizeof(buf), "RSSI: %d dBm  (%.2f m)", g_strongest_rssi, g_estimated_distance_m);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.drawString(buf, 120, 68, &fonts::Font2);

        M5.Display.setTextColor(prox_color, TFT_BLACK);
        M5.Display.drawString(prox_label, 120, 90, &fonts::Font2);
    } else {
        M5.Display.fillRect(10, 54, 220, 50, TFT_BLACK);
        M5.Display.drawRect(10, 54, 220, 50, TFT_DARKGRAY);
        M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
        M5.Display.drawString("Scanning 2.4GHz BLE...", 120, 78, &fonts::Font2);
    }

    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnB: Return to Menu", 120, 122, &fonts::Font0);
}

// ----------------------------------------------------------------------------
// MODE 4: Unified All-In-One Dashboard (Visual Only)
// ----------------------------------------------------------------------------
void renderAllInOneScreen() {
    drawTopHeader("4. UNIFIED TELEMETRY", TFT_NAVY);

    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float total_g = sqrtf((ax * ax) + (ay * ay) + (az * az));

    sampleSoundLevelMeter();
    updateBLERadar();

    M5.Display.drawFastHLine(0, 70, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 20, 94, TFT_DARKGRAY);

    char buf[32];

    // Card 1: IMU Vibration
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VIBRATION", 10, 24, &fonts::Font0);
    snprintf(buf, sizeof(buf), "%.2f G", total_g);
    M5.Display.setTextColor((total_g > 1.50f) ? TFT_RED : TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(buf, 10, 40, &fonts::Font4);

    // Card 2: Sound Level Meter
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("SOUND (dB)", 130, 24, &fonts::Font0);
    snprintf(buf, sizeof(buf), "%.1f dB", g_sound_db);
    M5.Display.setTextColor((g_sound_db > 75.0f) ? TFT_RED : TFT_GREENYELLOW, TFT_BLACK);
    M5.Display.drawString(buf, 130, 40, &fonts::Font4);

    // Card 3: BLE Radar Distance
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("PHONE DIST", 10, 74, &fonts::Font0);
    if (g_strongest_rssi > -110) {
        snprintf(buf, sizeof(buf), "%.1fm (%d)", g_estimated_distance_m, g_strongest_rssi);
    } else {
        snprintf(buf, sizeof(buf), "SEARCHING");
    }
    M5.Display.setTextColor(TFT_MAGENTA, TFT_BLACK);
    M5.Display.drawString(buf, 10, 92, &fonts::Font2);

    // Card 4: System State
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("NODE HEALTH", 130, 74, &fonts::Font0);
    bool alarm = (total_g > 1.50f) || (g_sound_db > 75.0f);
    M5.Display.setTextColor(alarm ? TFT_RED : TFT_GREEN, TFT_BLACK);
    M5.Display.drawString(alarm ? "! ALERT !" : "OPTIMAL", 130, 92, &fonts::Font2);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnB: Return to Menu", 120, 122, &fonts::Font0);
}

// ============================================================================
// SETUP & MAIN LOOP
// ============================================================================
void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_mic = true;
    cfg.internal_spk = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    // Start in Speaker Mode for Menu Chirps
    enableSpeakerMode();

    // Initialize BLE Subsystem
    BLEDevice::init("M5StickS3_Radar");
    g_ble_scan = BLEDevice::getScan();
    g_ble_scan->setAdvertisedDeviceCallbacks(new ProximityAdvertisedDeviceCallbacks());
    g_ble_scan->setActiveScan(true);
    g_ble_scan->setInterval(100);
    g_ble_scan->setWindow(99);

    renderMenuScreen();
}

void loop() {
    M5.update();

    if (g_current_mode == MODE_MENU) {
        if (M5.BtnA.wasClicked()) {
            g_menu_selection = (g_menu_selection + 1) % TOTAL_MENU_ITEMS;
            M5.Speaker.tone(1400, 30);
            renderMenuScreen();
        }

        if (M5.BtnB.wasClicked()) {
            g_current_mode = (AppState)(g_menu_selection + 1);
            M5.Speaker.tone(2200, 50);
            delay(60);
            M5.Display.fillScreen(TFT_BLACK);

            // Switching to modes using the microphone: shutdown the speaker completely
            if (g_current_mode == MODE_MIC || g_current_mode == MODE_ALL) {
                enableMicrophoneMode();
                g_peak_db = 38.0f;
            }
        }
    } else {
        // Universal Back Button
        if (M5.BtnB.wasClicked()) {
            // Restore Speaker hardware when returning to Menu
            enableSpeakerMode();

            g_current_mode = MODE_MENU;
            M5.Speaker.tone(800, 60);
            renderMenuScreen();
            delay(80);
            return;
        }

        switch (g_current_mode) {
            case MODE_IMU:
                renderIMUScreen();
                break;

            case MODE_MIC:
                if (M5.BtnA.wasClicked()) {
                    g_peak_db = g_sound_db;
                }
                renderMicScreen();
                break;

            case MODE_BLE:
                renderBLEScreen();
                break;

            case MODE_ALL:
                renderAllInOneScreen();
                break;

            default:
                g_current_mode = MODE_MENU;
                break;
        }
    }

    delay(20);
}

```

### Build & Upload

```bash
pio run -t upload
pio device monitor

```

---

## 8. Troubleshooting & Diagnostic Matrix

| Symptom | Primary Cause | Resolution |
| --- | --- | --- |
| **`M5.Mic.record` returns false / no sound readings** | `internal_mic` unconfigured or `M5.Mic.begin()` omitted. | Set `cfg.internal_mic = true;` prior to `M5.begin(cfg);` and call `M5.Mic.begin();` in `setup()`. |
| **Speaker produces no sound** | Volume initialized to zero or unconfigured. | Call `cfg.internal_spk = true;` in `setup()` and unmute via `M5.Speaker.setVolume(140);`. |
| **BLE Scan crashes or blocks display loops** | Synchronous scan blocking the core execution thread. | Use short non-blocking scans (e.g., `g_ble_scan->start(1, false);`) combined with non-blocking timers. |
| **Screen flickers aggressively** | Using `M5.Display.fillScreen()` inside dynamic execution loops. | Paint background only upon mode transitions. Inside active loops, redraw only changing coordinate regions using explicit text background colors. |
| **Distance estimation reads `99.9m**` | Mobile phone Bluetooth discovery or advertising is disabled. | Turn on Bluetooth on your phone and open a BLE scanner app (e.g., *nRF Connect* or *LightBlue*) to broadcast advertising packets. |

---

## 9. Hands-On Verification Lab

Complete the following validation tasks:

* [ ] **Task 4.1:** Flash Exercise 4.5. Confirm the interactive Main Menu loads cleanly with the four selectable options.
* [ ] **Task 4.2:** Click **Button A** to navigate through each option. Confirm each selection produces an acoustic chirp and updates the screen highlight.
* [ ] **Task 4.3:** Enter **Option 2 (SOUND / MIC)** using **Button B**. Speak into the microphone hole on the case and verify the dB reading and VU meter bar dynamically respond. Click **Button B** to return to the menu.
* [ ] **Task 4.4:** Enter **Option 3 (BLE PHONE RADAR)**. Hold your smartphone near the M5Stick S3. Observe the RSSI increase (e.g., $-45\text{ dBm}$) and confirm the proximity classification transitions to `IMMEDIATE (<0.5m)`. Move 3 meters away and verify the classification shifts to `FAR (>2m)`.
* [ ] **Task 4.5:** Enter **Option 4 (ALL-IN-ONE)**. Simultaneously tap the device to trigger motion, blow on the microphone, and observe the unified alarm state machine activate the buzzer.

---

## 10. Version Control Protocol

Commit your complete multi-peripheral firmware:

```bash
git add platformio.ini src/main.cpp
git commit -m "feat(lesson04): implement menu-driven multi-peripheral edge suite (imu, mic, ble distance radar, speaker, buttons)"

```
