# Lesson 05: Edge-to-Cloud Telemetry — Wi-Fi Networking, ThingSpeak REST & Google Forms HTTPS

This module connects the physical hardware layer engineered in Lesson 04 to the dual cloud-ingestion pipelines validated in Lesson 02. You will configure the **ESP32-S3 2.4 GHz Wi-Fi radio**, implement non-blocking connection lifecycles with automatic recovery, and stream **multimodal edge telemetry**—including **BMI270 IMU vibration vectors**, calibrated **SPM1423 sound levels (dB SPL)**, and **AXP2101 battery fuel gauge metrics**—to **ThingSpeak** (HTTP REST) and **Google Forms** (headless HTTPS POST).

```text
                           EDGE-TO-CLOUD TELEMETRY TOPOLOGY
                           
  +-----------------------------------------------------------------------------+
  | M5STACK STICK S3 (EDGE SENSING TIER)                                        |
  |  - BMI270: 6-Axis Motion Vector -> Acceleration Magnitude ||A|| (g)         |
  |  - SPM1423: Acoustic MEMS Microphone -> Ambient Sound Level (dB SPL)        |
  |  - AXP2101: PMIC Fuel Gauge -> Battery Percentage (%) & Voltage (V)         |
  |  - BtnA & BtnB: Operator Event Trigger & Display Control                    |
  |  - ST7789: Live Local HMI Dashboard & Network Ticker                        |
  +-----------------------------------------------------------------------------+
                                         |
                            2.4 GHz Wi-Fi (802.11 b/g/n)
                                         |
                                         v
  +-----------------------------------------------------------------------------+
  | LOCAL NETWORK GATEWAY / ROUTER                                              |
  +-----------------------------------------------------------------------------+
                 |                                              |
                 | HTTP GET (Port 80)                           | HTTPS POST (Port 443 / TLS 1.2)
                 | Rate Throttled (>= 16 sec)                   | Event-Driven or Interval (60 sec)
                 v                                              v
  +------------------------------+              +-------------------------------+
  | PIPELINE A: THINGSPEAK PaaS  |              | PIPELINE B: GOOGLE CLOUD      |
  | `api.thingspeak.com`         |              | `docs.google.com/forms`       |
  +------------------------------+              +-------------------------------+
                 |                                              |
                 v                                              v
  +------------------------------+              +-------------------------------+
  | - Continuous Time-Series     |              | - Headless Form Processing    |
  | - Field 1: Vibration (g)     |              | - Auto-Synced Google Sheet    |
  | - Field 2: Sound Level (dB)  |              | - Operator Shift Audit Logs   |
  | - Field 3: Battery Level (%) |              | - Looker Studio BI Dashboard  |
  | - Field 4: Anomaly Alert (1) |              | - Explicit Anomaly Records    |
  +------------------------------+              +-------------------------------+

```

---

## 1. Enterprise Network Architecture & Embedded Protocol Constraints

Embedded network programming introduces operational and architectural constraints that workstation software abstract away:

### 1.1 Wi-Fi Baseband & Frequency Allocation

The ESP32-S3 integrated radio subsystem supports IEEE 802.11 b/g/n strictly within the **2.4 GHz ISM band**. Enterprise access points broadcasting solely on 5 GHz (802.11 a/ac/ax) will not enumerate during an active beacon scan. The network infrastructure must broadcast a distinct 2.4 GHz SSID.

### 1.2 Non-Blocking Execution Model

Standard Arduino tutorials frequently use blocking delays (e.g., `delay(15000)`) to pace network requests. In production firmware, this approach causes operational failures:

* It stops `M5.update()` from executing, which halts button debouncing and hardware state calculation.


* It starves the FreeRTOS idle task, which risks tripping the hardware Task Watchdog Timer (TWDT) and forcing a CPU panic.


* It freezes display canvas rendering and frame updates.



All network tasks, polling cycles, and cooldown schedules must use timestamp arithmetic via `millis()`.

### 1.3 Transport Layer Security (TLS 1.2) on Constrained Silicon

Google Forms strictly enforces HTTPS encryption over TCP port 443. Standard embedded systems struggle with TLS because loading, parsing, and evaluating full X.509 Root CA certificate trust bundles can consume over 35 KB of contiguous SRAM per TLS socket. On the ESP32-S3, `WiFiClientSecure` is configured in non-validating mode via `.setInsecure()`. This performs the cryptographic TLS 1.2 cipher negotiation, handshake, and payload encryption without exhausting heap memory or crashing with a stack overflow.

### 1.4 Rate Limiting & Cloud Validation Rules

* **ThingSpeak (PaaS):** Free tier accounts enforce a hard **15-second rate-limiting throttle**. Transmissions sent within a 15-second window are rejected: the server returns HTTP `200 OK`, but the response payload evaluates to `0`. Firmware must enforce a local transmission cooldown of at least 16 seconds and verify that the returned `entry_id` is an integer greater than zero.


* **Google Forms (SaaS):** Google Form ingress accepts `application/x-www-form-urlencoded` payloads via HTTP POST. Requests must target `/formResponse` rather than `/viewform`. String parameters that contain spaces (e.g., `"Manual Audit"`) must be URL-encoded (e.g., `"Manual+Audit"`), or the Google HTTP ingress proxy will reject the packet with an HTTP `400 Bad Request`.



---

## 2. Workspace Scaffold, Directory Layout & `platformio.ini`

Keep the project layout structured and maintain strict credential isolation:

```text
lesson05-cloud-telemetry/
├── .gitignore
├── platformio.ini
├── include/
│   ├── secrets.example.h
│   └── secrets.h            <- Untracked local configuration
└── src/
    └── main.cpp

```

Initialize your project directory:

```bash
mkdir -p lesson05-cloud-telemetry/include lesson05-cloud-telemetry/src
cd lesson05-cloud-telemetry

```

### 2.1 Configuration File: `platformio.ini`

This configuration defines the hardware parameters for the ESP32-S3, enables native USB CDC routing, and establishes fallback targets for legacy hardware without unresolvable dependencies or non-breaking space characters:

```ini
; =====================================================================
; PlatformIO Configuration: Lesson 05 - Edge-to-Cloud Telemetry
; Targets: M5Stack Stick S3 & M5StickC / Plus Fallback
; =====================================================================

[platformio]
default_envs = m5stack-s3

; ---------------------------------------------------------------------
; Environment 1: M5Stack Stick S3 (ESP32-S3 Dual-Core Xtensa LX7)
; ---------------------------------------------------------------------
[env:m5stack-s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

; Serial Terminal & Flashing Rates
monitor_speed = 115200
upload_speed = 921600

; Memory & Clock Architecture
board_build.f_cpu = 240000000L
board_build.flash_mode = qio
board_build.flash_size = 8MB

; Build Flags & Native USB Routing
build_flags =
    -std=gnu++17
    -DESP32S3
    -DARDUINO_M5STACK_STICK_S3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -O2

; Driver Dependencies
lib_deps =
    m5stack/M5Unified
    m5stack/M5GFX

; ---------------------------------------------------------------------
; Environment 2: M5StickC / M5StickC Plus (ESP32 Classic Fallback)
; ---------------------------------------------------------------------
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

### 2.2 Credential Isolation Template (`include/secrets.example.h`)

Create `include/secrets.example.h` to define the network and cloud ingestion configuration schemas:

```cpp
#pragma once

// -----------------------------------------------------------------------------
// 1. Local Wi-Fi Access Point (2.4 GHz ONLY)
// -----------------------------------------------------------------------------
namespace NetworkConfig {
    const char* WIFI_SSID     = "YOUR_2.4GHZ_WIFI_SSID";
    const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
}

// -----------------------------------------------------------------------------
// 2. Pipeline A: ThingSpeak IoT Configuration
// -----------------------------------------------------------------------------
namespace ThingSpeakConfig {
    const char* WRITE_API_KEY = "YOUR_THINGSPEAK_WRITE_KEY";
    const unsigned long POST_INTERVAL_MS = 16000; // >= 15000 ms mandatory
}

// -----------------------------------------------------------------------------
// 3. Pipeline B: Google Forms Configuration (Extracted in Lesson 02)
// -----------------------------------------------------------------------------
namespace GoogleFormConfig {
    // Target endpoint MUST end in /formResponse
    const char* FORM_URL    = "https://docs.google.com/forms/d/e/YOUR_FORM_ID/formResponse";
    
    // Replace these entry IDs with the exact keys from your pre-filled link
    const char* ENTRY_UUID  = "entry.102938475"; // Device UUID
    const char* ENTRY_VIB   = "entry.584930291"; // Vibration magnitude (G)
    const char* ENTRY_SOUND = "entry.774839201"; // Sound level (dB)
    const char* ENTRY_BAT   = "entry.918273645"; // Battery level (%)
    const char* ENTRY_STATE = "entry.192837465"; // System health state
    const char* ENTRY_NOTES = "entry.564738291"; // Operator remark
    
    const unsigned long POST_INTERVAL_MS = 60000; // 60-second periodic sync
}

```

Copy the template to `include/secrets.h` and update it with your real network credentials and API keys:

```bash
cp include/secrets.example.h include/secrets.h

```

Ensure `secrets.h` is excluded from git version control:

```bash
cat <<EOT >> .gitignore
.pio
include/secrets.h
EOT

```

---

## 3. Exercise 5.1: Non-Blocking Wi-Fi Connection Manager & Signal Analyzer

### Objective

Implement an asynchronous Wi-Fi connection manager that maintains network connectivity in the background, updates real-time link diagnostics (SSID, IP, MAC address, and RSSI signal quality) on the ST7789 LCD display, and executes non-blocking reconnection routines if signal dropouts occur.

```text
+-------------------------------------------------------------+
| WI-FI CONNECTION MANAGER                     CH: 6 | -58dBm |
+-------------------------------------------------------------+
| STATUS: CONNECTED (802.11 b/g/n Station)                    |
|                                                             |
| SSID:   Enterprise_IoT_Mesh                                 |
| IP:     192.168.1.142                                       |
| MAC:    7C:02:B8:4D:3A:10                                   |
| LINK:   EXCELLENT (-58 dBm) [||||||||||||||||||||]          |
+-------------------------------------------------------------+
| Uptime: 00:04:12               Heap Available: 324,812 B    |
+-------------------------------------------------------------+

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 5.1: Non-Blocking Wi-Fi Connection Manager & Signal UI
 * Hardware: M5Stack Stick S3 (ST7789 LCD, ESP32-S3 Wi-Fi Radio)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include "secrets.h"

// Operational State Tracking
static wl_status_t g_last_wifi_status = WL_DISCONNECTED;
static unsigned long g_last_rssi_poll_ms = 0;
static unsigned long g_last_reconnect_attempt_ms = 0;
static const unsigned long RECONNECT_INTERVAL_MS = 5000;

void drawBaseHeader() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.drawString(" WI-FI FLEET MANAGER", 5, 10, &fonts::Font2);
    M5.Display.drawFastHLine(0, 114, 240, TFT_DARKGRAY);
}

void renderNetworkDashboard() {
    wl_status_t status = WiFi.status();
    M5.Display.setTextDatum(textdatum_t::top_left);

    if (status == WL_CONNECTED) {
        int32_t rssi = WiFi.RSSI();

        // 1. Status Banner
        M5.Display.fillRect(10, 24, 220, 18, TFT_DARKGREEN);
        M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        M5.Display.drawString(" STATUS: CONNECTED", 15, 26, &fonts::Font2);

        // 2. Addressing Metrics
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        char buf[64];
        
        snprintf(buf, sizeof(buf), "SSID: %-16s", WiFi.SSID().c_str());
        M5.Display.drawString(buf, 10, 46, &fonts::Font0);

        snprintf(buf, sizeof(buf), "IP:   %-16s", WiFi.localIP().toString().c_str());
        M5.Display.drawString(buf, 10, 58, &fonts::Font0);

        snprintf(buf, sizeof(buf), "MAC:  %s", WiFi.macAddress().c_str());
        M5.Display.drawString(buf, 10, 70, &fonts::Font0);

        // 3. Dynamic Signal Quality Bar
        const char* quality_str = (rssi > -65) ? "EXCELLENT" : ((rssi > -80) ? "FAIR" : "POOR");
        uint16_t quality_color = (rssi > -65) ? TFT_GREEN : ((rssi > -80) ? TFT_YELLOW : TFT_RED);

        snprintf(buf, sizeof(buf), "Signal: %d dBm (%s)", rssi, quality_str);
        M5.Display.setTextColor(quality_color, TFT_BLACK);
        M5.Display.drawString(buf, 10, 84, &fonts::Font0);

        // Map RSSI (-100 dBm to -40 dBm) across 220 pixels
        int clamped_rssi = max(-100, min(-40, (int)rssi));
        int bar_width = map(clamped_rssi, -100, -40, 0, 220);
        M5.Display.drawRect(10, 96, 220, 12, TFT_DARKGRAY);
        M5.Display.fillRect(11, 97, bar_width, 10, quality_color);
        M5.Display.fillRect(11 + bar_width, 97, 218 - bar_width, 10, TFT_BLACK);

        // Header RSSI Ticker
        M5.Display.setTextDatum(textdatum_t::middle_right);
        M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
        snprintf(buf, sizeof(buf), "CH: %d | %d dBm ", WiFi.channel(), rssi);
        M5.Display.drawString(buf, 235, 10, &fonts::Font0);

    } else {
        // Disconnected State
        M5.Display.fillRect(10, 24, 220, 18, TFT_MAROON);
        M5.Display.setTextColor(TFT_WHITE, TFT_MAROON);
        M5.Display.drawString(" STATUS: DISCONNECTED", 15, 26, &fonts::Font2);

        M5.Display.fillRect(10, 46, 220, 62, TFT_BLACK);
        M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
        M5.Display.drawString("Scanning 2.4GHz spectrum...", 10, 52, &fonts::Font0);
        M5.Display.drawString("Auto-reconnect active.", 10, 68, &fonts::Font0);

        M5.Display.setTextDatum(textdatum_t::middle_right);
        M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
        M5.Display.drawString("OFFLINE ", 235, 10, &fonts::Font0);
    }

    // 4. Footer System Runtime Readout
    uint32_t total_sec = millis() / 1000;
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "Up: %02u:%02u:%02u | Heap: %u B",
             (total_sec / 3600) % 24, (total_sec / 60) % 60, total_sec % 60, ESP.getFreeHeap());
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString(time_str, 120, 124, &fonts::Font0);
}

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_spk = false;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    drawBaseHeader();

    // Configure 2.4 GHz Wi-Fi Station
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    Serial.printf("[WIFI] Connecting to SSID: %s\n", NetworkConfig::WIFI_SSID);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    renderNetworkDashboard();
}

void loop() {
    M5.update();
    wl_status_t current_status = WiFi.status();

    // Detect Wi-Fi State Changes
    if (current_status != g_last_wifi_status) {
        g_last_wifi_status = current_status;
        renderNetworkDashboard();

        if (current_status == WL_CONNECTED) {
            Serial.printf("[WIFI] Associated successfully. IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("[WIFI] Connection lost. Entering reconnection loop.");
        }
    }

    // Handle Background Reconnection if disconnected
    if (current_status != WL_CONNECTED) {
        unsigned long now = millis();
        if (now - g_last_reconnect_attempt_ms >= RECONNECT_INTERVAL_MS) {
            g_last_reconnect_attempt_ms = now;
            Serial.println("[WIFI] Attempting reconnect...");
            WiFi.reconnect();
        }
    }

    // Refresh Signal Strength UI every 2 seconds while connected
    if (current_status == WL_CONNECTED && (millis() - g_last_rssi_poll_ms >= 2000)) {
        g_last_rssi_poll_ms = millis();
        renderNetworkDashboard();
    }

    delay(30);
}

```

### Build & Verification Commands

```bash
pio run -e m5stack-s3 -t upload
pio device monitor -b 115200

```

---

## 4. Exercise 5.2: Multimodal Telemetry Ingestion to ThingSpeak (HTTP REST)

### Objective

Capture live physical acceleration magnitude ($\Vert{}A\Vert{}$) from the **BMI270**, sample ambient acoustic sound pressure level ($\text{dB SPL}$) using the **SPM1423 MEMS microphone**, read the **AXP2101 PMIC** battery fuel gauge, and stream the payload to ThingSpeak using an HTTP GET REST query string. Enforce a non-blocking 16-second rate limit with an on-screen countdown timer.

```text
                  THINGSPEAK INGESTION PROTOCOL SEQUENCE
                  
  M5Stack Stick S3 (Client Node)                   ThingSpeak Cloud API (Port 80)
         |                                                       |
         | --- 1. Compute Accel Magnitude ||A|| (g) ------------> |
         | --- 2. Calculate Microphone Acoustic Level (dB) -----> |
         | --- 3. Sample AXP2101 Battery Gauge (%) -------------> |
         |                                                       |
         | HTTP GET /update?api_key=...&field1=..&field2=...     |
         |------------------------------------------------------>|
         |                                                       | (Process Time-Series)
         | HTTP 200 OK [Payload: "148" (Entry ID)]               |
         |<------------------------------------------------------|
         |                                                       |
         | --- 4. Validate Entry ID > 0 ------------------------> |
         | --- 5. Reset Non-Blocking 16-Second Cooldown Timer --> |

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 5.2: ThingSpeak Time-Series Stream (HTTP GET / REST)
 * Hardware: M5Stack Stick S3 (BMI270, SPM1423 Mic, AXP2101 PMIC, LCD)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>
#include "secrets.h"

// Acoustic Sampling Definitions
static constexpr size_t MIC_SAMPLES = 256;
static int16_t g_mic_buffer[MIC_SAMPLES];
static float g_smoothed_db = 38.0f;
static unsigned long g_last_mic_ms = 0;

// Transmission Scheduling
static unsigned long g_last_upload_ms = 0;
static uint32_t g_last_entry_id = 0;
static int g_last_http_code = 0;

void drawBaseDashboard() {
    M5.Display.fillScreen(TFT_BLACK);
    
    // Header
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.drawString(" THINGSPEAK TELEMETRY", 5, 10, &fonts::Font2);

    // Dividers
    M5.Display.drawFastHLine(0, 72, 240, TFT_DARKGRAY);
    M5.Display.drawFastHLine(0, 114, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 20, 52, TFT_DARKGRAY);

    // Labels
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.drawString("VIB MAGNITUDE", 60, 24, &fonts::Font0);
    M5.Display.drawString("SOUND LEVEL", 180, 24, &fonts::Font0);
}

void sampleMicrophone() {
    unsigned long now = millis();
    if (now - g_last_mic_ms < 40) return; // 25 Hz sampling
    g_last_mic_ms = now;

    if (M5.Mic.record(g_mic_buffer, MIC_SAMPLES, 16000)) {
        while (M5.Mic.isRecording()) delay(1);

        float sum = 0.0f;
        for (size_t i = 0; i < MIC_SAMPLES; i++) {
            sum += (float)g_mic_buffer[i] * (float)g_mic_buffer[i];
        }
        float rms = sqrtf(sum / (float)MIC_SAMPLES);
        float raw_db = 20.0f * log10f(rms + 1.0f);
        raw_db = max(30.0f, min(100.0f, raw_db));

        // IEC FAST Smoothing Filter
        g_smoothed_db = (0.30f * raw_db) + (0.70f * g_smoothed_db);
    }
}

bool postToThingSpeak(float vib_g, float sound_db, int bat_pct, int alert_flag) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ThingSpeak] Transmission aborted: Wi-Fi offline.");
        return false;
    }

    HTTPClient http;
    String url = "http://api.thingspeak.com/update";
    url += "?api_key=" + String(ThingSpeakConfig::WRITE_API_KEY);
    url += "&field1="  + String(vib_g, 3);
    url += "&field2="  + String(sound_db, 1);
    url += "&field3="  + String(bat_pct);
    url += "&field4="  + String(alert_flag);

    Serial.printf("[ThingSpeak] Dispatching HTTP GET: %s\n", url.c_str());

    http.begin(url);
    int http_code = http.GET();
    g_last_http_code = http_code;
    bool success = false;

    if (http_code == 200) {
        String payload = http.getString();
        uint32_t entry_id = payload.toInt();

        if (entry_id > 0) {
            g_last_entry_id = entry_id;
            success = true;
            Serial.printf("[ThingSpeak] Ingestion Accepted. Entry ID: %u\n", entry_id);
        } else {
            Serial.println("[ThingSpeak] Rate limit rejected (Entry ID: 0).");
        }
    } else {
        Serial.printf("[ThingSpeak] Transport Failed. HTTP Code: %d\n", http_code);
    }

    http.end();
    return success;
}

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_mic = true;
    cfg.internal_spk = false; // Disable speaker to avoid I2S bus conflicts
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    // Initialize Microphone
    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate = 16000;
    M5.Mic.config(mic_cfg);
    M5.Mic.begin();

    drawBaseDashboard();

    // Initialize Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Associating with Wi-Fi...", 120, 85, &fonts::Font2);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WIFI] Connected.");
    drawBaseDashboard();
}

void loop() {
    M5.update();

    // 1. Hardware Sensing
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float vibration_g = sqrtf((ax * ax) + (ay * ay) + (az * az));
    sampleMicrophone();
    int battery_pct = M5.Power.getBatteryLevel();
    if (battery_pct < 0) battery_pct = 100;
    int alert_flag = (vibration_g > 1.45f || g_smoothed_db > 75.0f) ? 1 : 0;

    // 2. Render Live Measurements
    char buf[32];
    M5.Display.setTextDatum(textdatum_t::middle_center);
    
    snprintf(buf, sizeof(buf), "%4.2f G", vibration_g);
    M5.Display.setTextColor((vibration_g > 1.45f) ? TFT_RED : TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(buf, 60, 48, &fonts::Font4);

    snprintf(buf, sizeof(buf), "%4.1f dB", g_smoothed_db);
    M5.Display.setTextColor((g_smoothed_db > 75.0f) ? TFT_RED : TFT_GREENYELLOW, TFT_BLACK);
    M5.Display.drawString(buf, 180, 48, &fonts::Font4);

    // 3. Rate-Limit Timing & Progress
    unsigned long now = millis();
    unsigned long elapsed = now - g_last_upload_ms;
    unsigned long cooldown = ThingSpeakConfig::POST_INTERVAL_MS;

    if (elapsed >= cooldown) {
        g_last_upload_ms = now;

        M5.Display.fillRect(0, 74, 240, 39, TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.drawString("Uploading to ThingSpeak...", 120, 84, &fonts::Font2);

        bool success = postToThingSpeak(vibration_g, g_smoothed_db, battery_pct, alert_flag);

        M5.Display.fillRect(0, 74, 240, 39, TFT_BLACK);
        if (success) {
            M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
            snprintf(buf, sizeof(buf), "SYNC OK | Entry #%u", g_last_entry_id);
            M5.Display.drawString(buf, 120, 84, &fonts::Font2);
        } else {
            M5.Display.setTextColor(TFT_RED, TFT_BLACK);
            snprintf(buf, sizeof(buf), "FAIL | HTTP %d", g_last_http_code);
            M5.Display.drawString(buf, 120, 84, &fonts::Font2);
        }
    }

    // 4. Ingress Cooldown Bar
    int remaining_sec = (elapsed < cooldown) ? (int)((cooldown - elapsed) / 1000) : 0;
    int bar_width = (elapsed < cooldown) ? (int)((elapsed * 220) / cooldown) : 220;

    M5.Display.drawRect(10, 102, 220, 8, TFT_DARKGRAY);
    M5.Display.fillRect(11, 103, bar_width, 6, TFT_DARKCYAN);
    M5.Display.fillRect(11 + bar_width, 103, 218 - bar_width, 6, TFT_BLACK);

    // Footer Status
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Next Sync: %2ds | Bat: %d%% | Alert: %d", remaining_sec, battery_pct, alert_flag);
    M5.Display.drawString(buf, 120, 124, &fonts::Font0);

    delay(30);
}

```

---

## 5. Exercise 5.3: Headless HTTPS Ingestion to Google Forms & Sheets

### Objective

Configure a secure, encrypted HTTPS client using `WiFiClientSecure` to submit operational telemetry directly into a Google Form and linked Google Sheet. Implement an `application/x-www-form-urlencoded` body formatter that encodes spaces to `+`, and create a dual-trigger architecture: scheduled heartbeats (every 60 seconds) and immediate, on-demand shift audits triggered by **Button A**.

```text
               GOOGLE FORMS HTTPS INGESTION SEQUENCE
               
  M5Stack Stick S3 (Edge Node)                     Google Ingress Endpoint (Port 443)
         |                                                       |
         | --- 1. TLS 1.2 Handshake (Port 443) ----------------> |
         | <--- Client/Server Cipher Negotiation --------------> |
         |                                                       |
         | POST /formResponse HTTP/1.1                           |
         | Host: docs.google.com                                 |
         | Content-Type: application/x-www-form-urlencoded       |
         |                                                       |
         | entry.102=S3_7C02&entry.584=1.02&entry.774=42.1...   |
         |------------------------------------------------------>|
         |                                                       | (Append Row to Sheets)
         | HTTP/1.1 200 OK (or 302 Temporary Redirect)           |
         |<------------------------------------------------------|

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 5.3: Headless HTTPS POST to Google Forms & Google Sheets
 * Hardware: M5Stack Stick S3 (BMI270, SPM1423 Mic, Button A, LCD)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include "secrets.h"

static unsigned long g_last_gform_post_ms = 0;
static uint32_t g_form_records_sent = 0;
static int g_last_http_code = 0;
static char g_device_uuid[16];

// Acoustic Sampling
static constexpr size_t MIC_SAMPLES = 256;
static int16_t g_mic_buffer[MIC_SAMPLES];
static float g_smoothed_db = 38.0f;
static unsigned long g_last_mic_ms = 0;

void drawGoogleFormUI() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.drawString(" GOOGLE SHEETS INGRESS", 5, 10, &fonts::Font2);

    M5.Display.drawFastHLine(0, 114, 240, TFT_DARKGRAY);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Force Immediate Audit Log", 120, 36, &fonts::Font0);
}

void sampleMicrophone() {
    unsigned long now = millis();
    if (now - g_last_mic_ms < 40) return;
    g_last_mic_ms = now;

    if (M5.Mic.record(g_mic_buffer, MIC_SAMPLES, 16000)) {
        while (M5.Mic.isRecording()) delay(1);
        float sum = 0.0f;
        for (size_t i = 0; i < MIC_SAMPLES; i++) {
            sum += (float)g_mic_buffer[i] * (float)g_mic_buffer[i];
        }
        float rms = sqrtf(sum / (float)MIC_SAMPLES);
        float raw_db = 20.0f * log10f(rms + 1.0f);
        raw_db = max(30.0f, min(100.0f, raw_db));
        g_smoothed_db = (0.30f * raw_db) + (0.70f * g_smoothed_db);
    }
}

bool postToGoogleForm(float vib_g, float sound_db, int bat_pct, const String& state, const String& note) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[GoogleForm] Cannot post: Wi-Fi offline.");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure(); // Bypass Root CA bundle evaluation to preserve SRAM

    HTTPClient http;
    http.begin(client, GoogleFormConfig::FORM_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Construct URL-encoded body
    String payload = "";
    payload += String(GoogleFormConfig::ENTRY_UUID)  + "=" + String(g_device_uuid) + "&";
    payload += String(GoogleFormConfig::ENTRY_VIB)   + "=" + String(vib_g, 3) + "&";
    payload += String(GoogleFormConfig::ENTRY_SOUND) + "=" + String(sound_db, 1) + "&";
    payload += String(GoogleFormConfig::ENTRY_BAT)   + "=" + String(bat_pct) + "&";
    payload += String(GoogleFormConfig::ENTRY_STATE) + "=" + state + "&";
    payload += String(GoogleFormConfig::ENTRY_NOTES) + "=" + note;

    Serial.printf("[GoogleForm] Submitting HTTPS POST to: %s\n", GoogleFormConfig::FORM_URL);
    Serial.printf("[GoogleForm] Payload: %s\n", payload.c_str());

    int http_code = http.POST(payload);
    g_last_http_code = http_code;
    Serial.printf("[GoogleForm] HTTP Status: %d\n", http_code);

    bool success = false;
    // Google Forms returns HTTP 200 or HTTP 302 upon successful row insertion
    if (http_code == 200 || http_code == 302) {
        g_form_records_sent++;
        success = true;
    }

    http.end();
    return success;
}

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_mic = true;
    cfg.internal_spk = false;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate = 16000;
    M5.Mic.config(mic_cfg);
    M5.Mic.begin();

    // Derive Hardware UUID
    uint64_t mac = ESP.getEfuseMac();
    snprintf(g_device_uuid, sizeof(g_device_uuid), "S3_%04X", (uint16_t)mac);

    drawGoogleFormUI();

    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Connecting to Wi-Fi...", 120, 75, &fonts::Font2);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WIFI] Connected.");
    drawGoogleFormUI();
}

void loop() {
    M5.update();

    // 1. Hardware Sensing
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float vibration_g = sqrtf((ax * ax) + (ay * ay) + (az * az));
    sampleMicrophone();
    int battery_pct = M5.Power.getBatteryLevel();
    if (battery_pct < 0) battery_pct = 100;
    String system_state = (vibration_g > 1.45f || g_smoothed_db > 75.0f) ? "ALARM" : "OPTIMAL";

    // 2. Trigger Evaluation
    bool manual_trigger = M5.BtnA.wasClicked();
    unsigned long now = millis();
    bool timer_trigger = (now - g_last_gform_post_ms >= GoogleFormConfig::POST_INTERVAL_MS);

    if (manual_trigger || timer_trigger) {
        g_last_gform_post_ms = now;

        // Encode spaces as '+' to comply with URL-encoding rules
        String remark = manual_trigger ? "Manual+Operator+Audit" : "Scheduled+Heartbeat";

        M5.Display.fillRect(0, 52, 240, 60, TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setTextDatum(textdatum_t::middle_center);
        M5.Display.drawString("Posting to Google Sheets...", 120, 68, &fonts::Font2);

        bool success = postToGoogleForm(vibration_g, g_smoothed_db, battery_pct, system_state, remark);

        M5.Display.fillRect(0, 52, 240, 60, TFT_BLACK);
        if (success) {
            M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
            M5.Display.drawString("INGRESS SUCCESSFUL", 120, 62, &fonts::Font2);
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5.Display.drawString("Rows Logged: " + String(g_form_records_sent), 120, 80, &fonts::Font2);
        } else {
            M5.Display.setTextColor(TFT_RED, TFT_BLACK);
            M5.Display.drawString("INGRESS FAILED", 120, 62, &fonts::Font2);
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5.Display.drawString("HTTP Error: " + String(g_last_http_code), 120, 80, &fonts::Font2);
        }

        M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
        M5.Display.drawString("Trigger: " + remark, 120, 98, &fonts::Font0);
    }

    // 3. Footer System Ticker
    char buf[48];
    snprintf(buf, sizeof(buf), "Vib: %.2fG | Mic: %.1fdB | Bat: %d%%", vibration_g, g_smoothed_db, battery_pct);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString(buf, 120, 124, &fonts::Font0);

    delay(30);
}

```

---

## 6. Exercise 5.4: Production-Grade Dual-Channel Telemetry Gateway

### Objective

Unify multimodal physical telemetry, graphics subsystem buffering, and dual cloud transmission into a production-grade IoT architecture. The edge node transmits continuous numerical time-series metrics to ThingSpeak every 16 seconds and submits tabular operational audit records to Google Forms every 60 seconds (or immediately when Button A is pressed).

```text
+-------------------------------------------------------------+
| FLEET TELEMETRY NODE           WIFI: -64 dBm | BAT: 94%     |
+-------------------------------------------------------------+
| VIB: 1.02 G | SOUND: 48.2 dB   STATE: OPTIMAL               |
+-------------------------------------------------------------+
| THINGSPEAK (Time-Series)       | GOOGLE SHEETS (Audit Log)  |
| Next Post:  12s                | Next Sync:  48s            |
| Status: #142 (HTTP 200)        | Status: 14 Rows (HTTP 200) |
+-------------------------------------------------------------+
| [BtnA] Force Audit Ingress     | [BtnB] Clear Local State   |
+-------------------------------------------------------------+

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 5.4: Integrated Dual-Channel Cloud Telemetry Node
 * Hardware: M5Stack Stick S3 (BMI270, SPM1423 Mic, AXP2101 PMIC, LCD)
 * Cloud: ThingSpeak (HTTP GET) + Google Forms / Sheets (HTTPS POST)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include "secrets.h"

// Hardware Identity
static char g_device_uuid[16];

// Cloud Transmission Timers
static unsigned long g_last_ts_post_ms = 0;
static unsigned long g_last_gf_post_ms = 0;
static uint32_t g_ts_entry_id = 0;
static uint32_t g_gf_rows_sent = 0;
static int g_last_ts_code = 0;
static int g_last_gf_code = 0;

// Acoustic Sampling
static constexpr size_t MIC_SAMPLES = 256;
static int16_t g_mic_buffer[MIC_SAMPLES];
static float g_smoothed_db = 38.0f;
static unsigned long g_last_mic_ms = 0;

// Unified Telemetry Data Structure
struct EdgeTelemetry {
    float vibration_g;
    float sound_db;
    int battery_pct;
    float battery_v;
    bool is_alarm_active;
};

EdgeTelemetry acquireTelemetry() {
    EdgeTelemetry data;

    // 1. IMU Linear Acceleration Vector
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    data.vibration_g = sqrtf((ax * ax) + (ay * ay) + (az * az));

    // 2. Battery Metrics
    data.battery_pct = M5.Power.getBatteryLevel();
    if (data.battery_pct < 0) data.battery_pct = 100;
    int mv = M5.Power.getBatteryVoltage();
    data.battery_v = (mv > 0) ? (mv / 1000.0f) : 0.0f;

    // 3. Microphone Sampling
    unsigned long now = millis();
    if (now - g_last_mic_ms >= 40) {
        g_last_mic_ms = now;
        if (M5.Mic.record(g_mic_buffer, MIC_SAMPLES, 16000)) {
            while (M5.Mic.isRecording()) delay(1);
            float sum = 0.0f;
            for (size_t i = 0; i < MIC_SAMPLES; i++) {
                sum += (float)g_mic_buffer[i] * (float)g_mic_buffer[i];
            }
            float rms = sqrtf(sum / (float)MIC_SAMPLES);
            float raw_db = 20.0f * log10f(rms + 1.0f);
            raw_db = max(30.0f, min(100.0f, raw_db));
            g_smoothed_db = (0.30f * raw_db) + (0.70f * g_smoothed_db);
        }
    }
    data.sound_db = g_smoothed_db;

    // 4. Threshold Logic
    data.is_alarm_active = (data.vibration_g > 1.45f || data.sound_db > 75.0f);

    return data;
}

void drawSystemDashboard() {
    M5.Display.fillScreen(TFT_BLACK);

    // Header Ribbon
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.drawString(" FLEET TELEMETRY", 5, 10, &fonts::Font2);

    // Section Dividers
    M5.Display.fillRect(0, 21, 240, 32, TFT_BLACK);
    M5.Display.drawFastHLine(0, 53, 240, TFT_DARKGRAY);
    M5.Display.drawFastHLine(0, 114, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 54, 60, TFT_DARKGRAY);

    // Panel Headers
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("THINGSPEAK", 60, 56, &fonts::Font0);
    M5.Display.drawString("GOOGLE FORMS", 180, 56, &fonts::Font0);

    // Softkey Navigation
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Force Audit Ingress | BtnB: Reset", 120, 124, &fonts::Font0);
}

bool transmitThingSpeak(const EdgeTelemetry& data) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    String url = "http://api.thingspeak.com/update";
    url += "?api_key=" + String(ThingSpeakConfig::WRITE_API_KEY);
    url += "&field1="  + String(data.vibration_g, 3);
    url += "&field2="  + String(data.sound_db, 1);
    url += "&field3="  + String(data.battery_pct);
    url += "&field4="  + String(data.is_alarm_active ? 1 : 0);

    http.begin(url);
    g_last_ts_code = http.GET();
    bool success = false;

    if (g_last_ts_code == 200) {
        uint32_t entry = http.getString().toInt();
        if (entry > 0) {
            g_ts_entry_id = entry;
            success = true;
        }
    }
    http.end();
    return success;
}

bool transmitGoogleForm(const EdgeTelemetry& data, const String& note) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClientSecure secure_client;
    secure_client.setInsecure();

    HTTPClient http;
    http.begin(secure_client, GoogleFormConfig::FORM_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String payload = "";
    payload += String(GoogleFormConfig::ENTRY_UUID)  + "=" + String(g_device_uuid) + "&";
    payload += String(GoogleFormConfig::ENTRY_VIB)   + "=" + String(data.vibration_g, 3) + "&";
    payload += String(GoogleFormConfig::ENTRY_SOUND) + "=" + String(data.sound_db, 1) + "&";
    payload += String(GoogleFormConfig::ENTRY_BAT)   + "=" + String(data.battery_pct) + "&";
    payload += String(GoogleFormConfig::ENTRY_STATE) + "=" + (data.is_alarm_active ? "ALARM" : "OPTIMAL") + "&";
    payload += String(GoogleFormConfig::ENTRY_NOTES) + "=" + note;

    g_last_gf_code = http.POST(payload);
    bool success = false;

    if (g_last_gf_code == 200 || g_last_gf_code == 302) {
        g_gf_rows_sent++;
        success = true;
    }
    http.end();
    return success;
}

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_mic = true;
    cfg.internal_spk = false;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate = 16000;
    M5.Mic.config(mic_cfg);
    M5.Mic.begin();

    uint64_t mac = ESP.getEfuseMac();
    snprintf(g_device_uuid, sizeof(g_device_uuid), "S3_%04X", (uint16_t)mac);

    drawSystemDashboard();

    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Associating with 2.4G AP...", 120, 80, &fonts::Font2);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\n[WIFI] Associated. Dual-pipeline engine running.");
    drawSystemDashboard();
}

void loop() {
    M5.update();

    // 1. Telemetry Sampling
    EdgeTelemetry current_data = acquireTelemetry();

    // 2. Render Header (Wi-Fi Signal & Battery)
    char buf[48];
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    snprintf(buf, sizeof(buf), "%d dBm | %d%% ", WiFi.RSSI(), current_data.battery_pct);
    M5.Display.drawString(buf, 235, 10, &fonts::Font2);

    // 3. Render Live Metrics Bar
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), " %.2fG | %.1fdB", current_data.vibration_g, current_data.sound_db);
    M5.Display.drawString(buf, 5, 36, &fonts::Font2);

    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor(current_data.is_alarm_active ? TFT_RED : TFT_GREEN, TFT_BLACK);
    M5.Display.drawString(current_data.is_alarm_active ? "ALARM " : "OPTIMAL ", 235, 36, &fonts::Font2);

    // 4. Pipeline A Execution: ThingSpeak Timer
    unsigned long now = millis();
    if (now - g_last_ts_post_ms >= ThingSpeakConfig::POST_INTERVAL_MS) {
        g_last_ts_post_ms = now;
        transmitThingSpeak(current_data);
    }

    // 5. Pipeline B Execution: Google Forms Timer & Event Handling
    bool manual_audit = M5.BtnA.wasClicked();
    if (manual_audit || (now - g_last_gf_post_ms >= GoogleFormConfig::POST_INTERVAL_MS)) {
        g_last_gf_post_ms = now;
        String note = manual_audit ? "Manual+Audit+BtnA" : "Scheduled+Sync";
        transmitGoogleForm(current_data, note);
    }

    // 6. Manual Reset of Counters via Button B
    if (M5.BtnB.wasClicked()) {
        g_ts_entry_id = 0;
        g_gf_rows_sent = 0;
    }

    // 7. Render Left Column: ThingSpeak Metrics
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Entry: #%-6u", g_ts_entry_id);
    M5.Display.drawString(buf, 10, 72, &fonts::Font0);

    M5.Display.setTextColor((g_last_ts_code == 200) ? TFT_GREEN : TFT_RED, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Status: HTTP %-3d", g_last_ts_code);
    M5.Display.drawString(buf, 10, 88, &fonts::Font0);

    // 8. Render Right Column: Google Forms Metrics
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Rows:  %-6u", g_gf_rows_sent);
    M5.Display.drawString(buf, 130, 72, &fonts::Font0);

    M5.Display.setTextColor((g_last_gf_code == 200 || g_last_gf_code == 302) ? TFT_GREEN : TFT_RED, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Status: HTTP %-3d", g_last_gf_code);
    M5.Display.drawString(buf, 130, 88, &fonts::Font0);

    delay(30);
}

```

---

## 7. Troubleshooting & Network Diagnostic Matrix

| Error Signature | Underlying Failure | Corrective Protocol |
| --- | --- | --- |
| **`WiFi.status()` loops indefinitely in disconnected state** | The access point is broadcasting on 5 GHz only, or the SSID is hidden.

 | Configure the wireless router to broadcast an isolated 2.4 GHz 802.11 b/g/n SSID.

 |
| **ThingSpeak consistently returns HTTP code `-1` (`HTTPC_ERROR_CONNECTION_REFUSED`)** | DNS failed to resolve `api.thingspeak.com`, or the local gateway blocked port 80.

 | Confirm that the local router allows outbound TCP traffic on port 80 and verify that DNS resolves correctly.

 |
| **ThingSpeak returns HTTP `200` but payload evaluates to `0**` | Ingress rate limit breached ($< 15\text{ seconds}$).

 | Verify that `ThingSpeakConfig::POST_INTERVAL_MS` is set to $\ge 16000\text{ ms}$.

 |
| **Google Form returns HTTP `400 Bad Request**` | Entry token mismatch, malformed payload schema, or unencoded spaces in string fields.

 | Check that every `entry.XXXXXXXXX` key matches the pre-filled link, and use `+` instead of spaces in strings.

 |
| **Google Form returns HTTP `401 Unauthorized` / `403 Forbidden**` | Google Form settings require user account sign-in.

 | Open Google Form Settings $\rightarrow$ Responses: Disable **"Limit to 1 response"** and **"Restrict to users in [Domain]"**.

 |
| **Device crashes with `Guru Meditation Error` during TLS handshake** | Heap exhaustion or stack overflow caused by loading X.509 Root CA certificates into internal SRAM.

 | Ensure `WiFiClientSecure` calls `.setInsecure()` before executing `http.begin()`.

 |
| **Microphone returns flat zero or saturated digital noise** | I2S DMA clock contention caused by driving the onboard speaker simultaneously. | Set `cfg.internal_spk = false;` in `setup()`, and avoid calling `M5.Speaker.tone()` concurrently with `M5.Mic.record()`. |

---

## 8. Hands-On Verification Lab

Complete the following validation tasks before progressing to Lesson 06:

* [ ] **Task 5.1:** Flash Exercise 5.4. Verify that the device connects to Wi-Fi within 10 seconds and displays an active IP and RSSI on the screen.


* [ ] **Task 5.2:** Open your ThingSpeak Channel Dashboard. Verify that at least 5 consecutive data frames (Field 1: Vibration, Field 2: Sound, Field 3: Battery, Field 4: Alert) appear spaced 16 seconds apart.


* [ ] **Task 5.3:** Create a physical vibration event ($> 1.45\text{ G}$) or clap near the microphone ($> 75\text{ dB}$). Confirm that the local LCD displays the red `ALARM` state, and verify that `field4` on ThingSpeak logs a value of `1` on the following sync cycle.


* [ ] **Task 5.4:** Click **Button A** on the front panel. Open your linked Google Sheet and confirm that a new row appears immediately containing your device's unique MAC UUID, the current sensor metrics, and the remark `"Manual Audit BtnA"`.


* [ ] **Task 5.5:** Disconnect the router power or step out of range. Confirm that the firmware continues local sensor sampling, updates the LCD, and reconnects automatically without triggering a CPU watchdog reset.



---

## 9. Version Control Protocol

Stage and commit your cloud telemetry firmware:

```bash
# Verify credentials in include/secrets.h remain uncommitted
git status

# Stage PlatformIO configuration, source files, and template
git add platformio.ini include/secrets.example.h src/main.cpp .gitignore

# Commit changes
git commit -m "feat(lesson05): implement dual-channel multimodal edge telemetry gateway (thingspeak rest and google forms https)"

```
