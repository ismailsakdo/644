# Lesson 05: Edge-to-Cloud Telemetry — Wi-Fi Networking, ThingSpeak REST & Google Forms HTTPS

This module connects the physical hardware layer engineered in Lesson 04 to the dual cloud-ingestion pipelines validated in Lesson 02. You will configure the **ESP32-S3 2.4 GHz Wi-Fi radio**, implement robust connection lifecycles with automatic recovery, and stream live **BMI270 IMU vibration vectors** and **AXP2101 battery fuel gauge** telemetry to **ThingSpeak** (HTTP REST) and **Google Forms** (headless HTTPS POST).

```text
                     EDGE-TO-CLOUD TELEMETRY TOPOLOGY
                     
  +------------------------------------------------------------------------+
  | M5STACK STICK S3 (EDGE LAYER)                                          |
  |  - BMI270: 6-Axis Motion Vector -> Acceleration Magnitude ||A|| (g)    |
  |  - AXP2101: Battery Fuel Gauge -> Percentage (%) & Voltage (V)         |
  |  - BtnA: Manual Industrial Audit Trigger                               |
  |  - ST7789: Live Local HMI Dashboard                                    |
  +------------------------------------------------------------------------+
                                      |
                         2.4 GHz Wi-Fi (802.11 b/g/n)
                                      |
                                      v
  +------------------------------------------------------------------------+
  | LOCAL NETWORK GATEWAY / ROUTER                                         |
  +------------------------------------------------------------------------+
                 |                                         |
                 | HTTP GET (Port 80)                      | HTTPS POST (Port 443)
                 | Interval >= 15 seconds                  | Immediate / Event-Driven
                 v                                         v
  +-------------------------------+         +------------------------------+
  | PIPELINE A: THINGSPEAK PaaS   |         | PIPELINE B: GOOGLE CLOUD     |
  | `api.thingspeak.com`          |         | `docs.google.com/forms`      |
  +-------------------------------+         +------------------------------+
                 |                                         |
                 v                                         v
  +-------------------------------+         +------------------------------+
  | - Time-Series Real-Time Plots |         | - Headless Form Processor    |
  | - Field 1: Vibration (g)      |         | - Auto-Synced Google Sheet   |
  | - Field 2: Battery Level (%)  |         | - Shift Log & Audit Records  |
  | - Field 3: Core Temp (°C)     |         | - Looker Studio Ingestion    |
  | - Field 4: Audit Alert Flag   |         |                              |
  +-------------------------------+         +------------------------------+

```

---

## 1. Network Constraints & Non-Blocking Design

Embedded network programming differs significantly from workstation scripting. The following operational constraints dictate the firmware design:

1. **2.4 GHz Frequency Band:** The ESP32-S3 Wi-Fi MAC and baseband only support IEEE 802.11 b/g/n in the 2.4 GHz spectrum. It cannot detect or associate with 5 GHz access points.
2. **Non-Blocking Architecture:** Never use blocking delays (`delay(15000)`) to pace cloud transmissions. Halting the CPU prevents `M5.update()` from executing, which freezes the LCD rendering, stops button debouncing, and risks triggering the hardware task watchdog timer (WDT). All network schedules use timestamp comparison via `millis()`.
3. **Transport Security (TLS 1.2):** Google Forms strictly mandates HTTPS over port 443. We utilize `WiFiClientSecure` with certificate verification configured to non-validating mode (`setInsecure()`) to perform the TLS 1.2 handshake without exhausting internal SRAM with massive X.509 root CA certificate bundles.

---

## 2. Project Directory Layout & `platformio.ini`

Organize the project with strict credential isolation:

```text
lesson05-cloud-telemetry/
├── .gitignore
├── platformio.ini
├── include/
│   ├── secrets.example.h
│   └── secrets.h            <- Excluded from Git tracking
└── src/
    └── main.cpp

```

Initialize the project directory:

```bash
mkdir -p lesson05-cloud-telemetry/include lesson05-cloud-telemetry/src
cd lesson05-cloud-telemetry

```

### 2.1 Configuration File: `platformio.ini`

Declare the board target, native USB CDC console flags, and the `M5Unified` driver dependency:

```ini
; =====================================================================
; PlatformIO Configuration: Lesson 05 - Edge-to-Cloud Telemetry
; Board: M5Stack Stick S3 (Espressif ESP32-S3)
; =====================================================================

[env:m5stack-s3]
platform = espressif32 @ ^6.5.0
board = esp32-s3-devkitc-1
framework = arduino

; Serial Monitor & Upload Configuration
monitor_speed = 115200
upload_speed = 921600

; USB CDC Terminal Redirection
build_flags = 
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1

; Core Peripheral Drivers
lib_deps = 
    m5stack/M5Unified @ ^0.1.16

```

### 2.2 Credential Isolation Template (`include/secrets.example.h`)

Create `include/secrets.example.h` to define the network and cloud configuration namespace:

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
    // URL must end in /formResponse
    const char* FORM_URL    = "https://docs.google.com/forms/d/e/YOUR_FORM_ID/formResponse";
    const char* ENTRY_UUID  = "entry.104523912";
    const char* ENTRY_VIB   = "entry.839201441";
    const char* ENTRY_BAT   = "entry.471928411";
    const char* ENTRY_STATE = "entry.193847261";
    const char* ENTRY_NOTES = "entry.901827364";
    const unsigned long POST_INTERVAL_MS = 30000; // Event or interval driven
}

```

Copy the template to `include/secrets.h` and populate it with your live credentials:

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

## 3. Exercise 5.1: Wi-Fi Lifecycle Management & Signal Diagnostic UI

### Objective

Implement a connection manager that connects to a 2.4 GHz network, handles automatic reconnection during packet loss, and renders real-time network telemetry (SSID, IP address, and RSSI signal quality) on the ST7789 display.

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 5.1: Wi-Fi Connection Manager & Signal Strength UI
 * ============================================================================
 */
#include <M5Unified.h>
#include <WiFi.h>
#include "secrets.h"

// Wi-Fi Status Tracking Variables
static wl_status_t g_last_wifi_status = WL_DISCONNECTED;
static unsigned long g_last_rssi_check_ms = 0;

void drawNetworkHeader() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.fillRect(0, 0, 240, 22, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(1);
    M5.Display.drawString("WI-FI FLEET DIAGNOSTICS", 120, 11);
}

void connectToWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    Serial.printf("[WIFI] Connecting to SSID: %s\n", NetworkConfig::WIFI_SSID);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Associating with AP...", 120, 60);
}

void renderConnectionDetails() {
    M5.Display.fillRect(0, 24, 240, 111, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::top_left);

    if (WiFi.status() == WL_CONNECTED) {
        int32_t rssi = WiFi.RSSI();

        // Status Card
        M5.Display.fillRect(10, 30, 220, 24, TFT_DARKGREEN);
        M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        M5.Display.drawString(" STATUS: CONNECTED", 20, 36);

        // Network Metrics
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(10, 62);
        M5.Display.printf("SSID: %s\n", WiFi.SSID().c_str());
        M5.Display.setCursor(10, 78);
        M5.Display.printf("IP:   %s\n", WiFi.localIP().toString().c_str());
        M5.Display.setCursor(10, 94);
        M5.Display.printf("MAC:  %s\n", WiFi.macAddress().c_str());

        // Signal Quality (RSSI Bar)
        uint16_t rssi_color = (rssi > -65) ? TFT_GREEN : ((rssi > -80) ? TFT_YELLOW : TFT_RED);
        M5.Display.setCursor(10, 110);
        M5.Display.setTextColor(rssi_color, TFT_BLACK);
        M5.Display.printf("Signal: %d dBm (%s)\n", rssi, 
                          (rssi > -65) ? "EXCELLENT" : ((rssi > -80) ? "FAIR" : "POOR"));
    } else {
        // Disconnected Card
        M5.Display.fillRect(10, 30, 220, 24, TFT_MAROON);
        M5.Display.setTextColor(TFT_WHITE, TFT_MAROON);
        M5.Display.drawString(" STATUS: DISCONNECTED", 20, 36);

        M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
        M5.Display.setCursor(10, 65);
        M5.Display.println("Searching for 2.4GHz AP...");
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    drawNetworkHeader();

    Serial.begin(115200);
    delay(2000);

    connectToWiFi();
}

void loop() {
    M5.update();

    wl_status_t current_status = WiFi.status();

    // Trigger UI redraw upon state change
    if (current_status != g_last_wifi_status) {
        g_last_wifi_status = current_status;
        renderConnectionDetails();
        
        if (current_status == WL_CONNECTED) {
            M5.Speaker.tone(2000, 100);
            Serial.printf("[WIFI] Connected! IP Address: %s\n", WiFi.localIP().toString().c_str());
        } else {
            M5.Speaker.tone(500, 250);
            Serial.println("[WIFI] Lost connection. Attempting reconnection...");
            WiFi.reconnect();
        }
    }

    // Periodically update the RSSI metric every 3 seconds while connected
    if (current_status == WL_CONNECTED && (millis() - g_last_rssi_check_ms >= 3000)) {
        g_last_rssi_check_ms = millis();
        renderConnectionDetails();
    }

    delay(50);
}

```

### Build & Upload

```bash
pio run -t upload
pio device monitor

```

---

## 4. Exercise 5.2: Real IMU Telemetry Stream to ThingSpeak (HTTP REST)

### Objective

Capture live acceleration magnitude from the **BMI270** sensor and state-of-charge from the **AXP2101 PMIC**, assemble an HTTP GET request, enforce the mandatory 15-second rate limit using non-blocking timers, and transmit the data payload to ThingSpeak.

```text
                  THINGSPEAK INGESTION SEQUENCE
                  
  M5Stick S3 (Edge Node)                         ThingSpeak Cloud API
         |                                                 |
         | --- 1. Compute Accel Magnitude ||A|| (g) -----> |
         | --- 2. Query AXP2101 Battery (%) -------------> |
         |                                                 |
         | HTTP GET /update?api_key=...&field1=..&field2=..|
         |------------------------------------------------>|
         |                                                 | (Append Time-Series)
         | HTTP 200 OK [Payload: Entry ID, e.g., "42"]     |
         |<------------------------------------------------|
         |                                                 |
         | --- 3. Render Status & Entry ID on Screen ----> |
         | --- 4. Enforce 16-second local wait timer ----> |

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 5.2: Real Sensor Telemetry Stream to ThingSpeak REST API
 * ============================================================================
 */
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>
#include "secrets.h"

static unsigned long g_last_thingspeak_ms = 0;
static uint32_t g_last_entry_id = 0;
static int g_last_http_code = 0;

void drawBaseUI() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.fillRect(0, 0, 240, 22, TFT_DARKCYAN);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("THINGSPEAK TELEMETRY NODE", 120, 11);

    M5.Display.drawFastHLine(0, 92, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 22, 70, TFT_DARKGRAY);

    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("LIVE ACCEL", 60, 26);
    M5.Display.drawString("BATTERY", 180, 26);
}

bool postToThingSpeak(float vibration_g, int battery_pct, float temp_c, int alert_flag) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ThingSpeak] Error: Wi-Fi not associated.");
        return false;
    }

    HTTPClient http;

    // Construct REST URL Query String
    String url = "http://api.thingspeak.com/update";
    url += "?api_key=" + String(ThingSpeakConfig::WRITE_API_KEY);
    url += "&field1="  + String(vibration_g, 3);
    url += "&field2="  + String(battery_pct);
    url += "&field3="  + String(temp_c, 1);
    url += "&field4="  + String(alert_flag);

    Serial.printf("[ThingSpeak] Disagreeing payload URL: %s\n", url.c_str());

    http.begin(url);
    int http_response_code = http.GET();
    g_last_http_code = http_response_code;

    bool transmission_success = false;

    if (http_response_code == 200) {
        String response_payload = http.getString();
        uint32_t entry_id = response_payload.toInt();

        if (entry_id > 0) {
            g_last_entry_id = entry_id;
            transmission_success = true;
            Serial.printf("[ThingSpeak] Success! Appended Entry ID: %u\n", entry_id);
            M5.Speaker.tone(2400, 50);
        } else {
            Serial.println("[ThingSpeak] Rate limit rejected (Entry ID: 0).");
        }
    } else {
        Serial.printf("[ThingSpeak] HTTP transport failure. Code: %d\n", http_response_code);
    }

    http.end();
    return transmission_success;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    drawBaseUI();

    Serial.begin(115200);
    delay(2000);

    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Connecting Wi-Fi...", 120, 60);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WIFI] Connected.");
    drawBaseUI();
}

void loop() {
    M5.update();

    // 1. Acquire Physical Telemetry
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float vibration_magnitude = sqrtf((ax * ax) + (ay * ay) + (az * az));
    int battery_percentage = M5.Power.getBatteryLevel();
    float core_temp = temperatureRead();
    int alert_flag = (vibration_magnitude > 1.35f) ? 1 : 0;

    // 2. Render Live Measurements
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(String(vibration_magnitude, 2) + " g ", 60, 58);

    M5.Display.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    M5.Display.drawString(String(battery_percentage) + " % ", 180, 58);
    M5.Display.setTextSize(1);

    // 3. Enforce Non-Blocking Transmission Schedule
    unsigned long current_ms = millis();
    if (current_ms - g_last_thingspeak_ms >= ThingSpeakConfig::POST_INTERVAL_MS) {
        g_last_thingspeak_ms = current_ms;

        // Visual Ingress Indicator
        M5.Display.fillRect(0, 93, 240, 42, TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.drawString("Uploading to ThingSpeak...", 120, 104);

        bool status = postToThingSpeak(vibration_magnitude, battery_percentage, core_temp, alert_flag);

        // Update Bottom Status Panel
        M5.Display.fillRect(0, 93, 240, 42, TFT_BLACK);
        if (status) {
            M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
            M5.Display.drawString("SYNC OK | Entry ID: " + String(g_last_entry_id), 120, 104);
        } else {
            M5.Display.setTextColor(TFT_RED, TFT_BLACK);
            M5.Display.drawString("SYNC FAILED | Code: " + String(g_last_http_code), 120, 104);
        }

        M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
        M5.Display.drawString("Interval: 16s | Free RAM: " + String(ESP.getFreeHeap()) + " B", 120, 122);
    }

    delay(50);
}

```

### Build & Upload

```bash
pio run -t upload
pio device monitor

```

*Open your ThingSpeak Channel in a browser to confirm real sensor datapoints arrive every 16 seconds.*

---

## 5. Exercise 5.3: Headless HTTPS POST to Google Forms

### Objective

Stream edge operational records into Google Sheets via the headless Google Forms ingestion endpoint. This requires formatting an `application/x-www-form-urlencoded` POST request body and managing the SSL/TLS 1.2 handshake over port 443 using `WiFiClientSecure`.

```text
               GOOGLE FORMS HTTPS INGESTION SEQUENCE
               
  M5Stick S3 (ESP32-S3)                          Google Ingress Endpoint
         |                                                 |
         | --- 1. TLS 1.2 Handshake (Port 443) ----------> |
         | <--- Client/Server Cipher Negotiation --------> |
         |                                                 |
         | POST /formResponse HTTP/1.1                     |
         | Host: docs.google.com                           |
         | Content-Type: application/x-www-form-urlencoded |
         |                                                 |
         | entry.104=M5S3_NODE&entry.839=1.12&...          |
         |------------------------------------------------>|
         |                                                 | (Append Row to Sheets)
         | HTTP/1.1 200 OK (or 302 Temporary Redirect)     |
         |<------------------------------------------------|

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 5.3: Headless HTTPS POST to Google Forms & Sheets
 * ============================================================================
 */
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include "secrets.h"

static unsigned long g_last_gform_post_ms = 0;
static uint32_t g_form_records_sent = 0;

void drawGoogleFormUI() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.fillRect(0, 0, 240, 22, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("GOOGLE SHEETS INGRESS", 120, 11);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Force Immediate Post", 120, 40);
}

bool postToGoogleForm(const String& device_id, float vibration_g, int battery_pct, const String& state, const String& notes) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[GoogleForm] Cannot post: Wi-Fi disconnected.");
        return false;
    }

    WiFiClientSecure secure_client;
    // Bypass full CA certificate bundle verification to conserve SRAM
    secure_client.setInsecure();

    HTTPClient http;
    http.begin(secure_client, GoogleFormConfig::FORM_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Construct URL-encoded POST payload
    String payload = "";
    payload += String(GoogleFormConfig::ENTRY_UUID)  + "=" + device_id + "&";
    payload += String(GoogleFormConfig::ENTRY_VIB)   + "=" + String(vibration_g, 3) + "&";
    payload += String(GoogleFormConfig::ENTRY_BAT)   + "=" + String(battery_pct) + "&";
    payload += String(GoogleFormConfig::ENTRY_STATE) + "=" + state + "&";
    payload += String(GoogleFormConfig::ENTRY_NOTES) + "=" + notes;

    Serial.printf("[GoogleForm] Dispatching HTTPS POST to: %s\n", GoogleFormConfig::FORM_URL);
    Serial.printf("[GoogleForm] Encoded Form Body: %s\n", payload.c_str());

    int http_response_code = http.POST(payload);
    Serial.printf("[GoogleForm] HTTP Status: %d\n", http_response_code);

    bool post_successful = false;
    // Google Forms responds with 200 or 302 Redirect upon accepted submission
    if (http_response_code == 200 || http_response_code == 302) {
        post_successful = true;
        g_form_records_sent++;
        M5.Speaker.tone(1800, 60);
    }

    http.end();
    return post_successful;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    drawGoogleFormUI();

    Serial.begin(115200);
    delay(2000);

    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.drawString("Connecting Wi-Fi...", 120, 70);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\n[WIFI] Connected.");
    drawGoogleFormUI();
}

void loop() {
    M5.update();

    // 1. Acquire Physical Telemetry
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float vibration_g = sqrtf((ax * ax) + (ay * ay) + (az * az));
    int battery_pct = M5.Power.getBatteryLevel();
    String system_state = (vibration_g > 1.35f) ? "WARN_VIB" : "OPTIMAL";

    // 2. Identify Unique Device Identifier via eFuse MAC
    uint64_t mac = ESP.getEfuseMac();
    char dev_id_str[16];
    snprintf(dev_id_str, sizeof(dev_id_str), "M5S3_%04X", (uint16_t)mac);

    bool trigger_transmission = false;
    String submission_note = "Interval_Sample";

    // Trigger A: Manual Operator Audit via Button A
    if (M5.BtnA.wasClicked()) {
        trigger_transmission = true;
        submission_note = "Manual_Operator_Audit";
        Serial.println("[EVENT] Button A pressed: Forcing immediate cloud transmission.");
    }

    // Trigger B: Non-Blocking Interval Schedule (Every 30 seconds)
    if (millis() - g_last_gform_post_ms >= GoogleFormConfig::POST_INTERVAL_MS) {
        trigger_transmission = true;
    }

    // 3. Execute Transmission If Triggered
    if (trigger_transmission) {
        g_last_gform_post_ms = millis();

        M5.Display.fillRect(0, 60, 240, 75, TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.drawString("Posting to Google Sheets...", 120, 75);

        bool success = postToGoogleForm(String(dev_id_str), vibration_g, battery_pct, system_state, submission_note);

        M5.Display.fillRect(0, 60, 240, 75, TFT_BLACK);
        if (success) {
            M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
            M5.Display.drawString("INGRESS SUCCESSFUL", 120, 75);
        } else {
            M5.Display.setTextColor(TFT_RED, TFT_BLACK);
            M5.Display.drawString("INGRESS FAILED", 120, 75);
        }

        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.drawString("Rows Appended: " + String(g_form_records_sent), 120, 95);
        M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
        M5.Display.drawString("Last Note: " + submission_note, 120, 115);
    }

    delay(20);
}

```

### Build & Upload

```bash
pio run -t upload
pio device monitor

```

*Press **Button A** on the front of the M5Stick S3 and observe a new row immediately append to your connected Google Sheet.*

---

## 6. Exercise 5.4: Production-Grade Dual-Channel Telemetry System

### Objective

Consolidate physical sensing, graphic UI state-machines, and dual cloud ingress into a unified enterprise firmware architecture:

* **ThingSpeak:** Continual physical vibration trend logging (Paced at 16-second intervals).
* **Google Forms:** Periodic and event-driven operational shift logging (Paced at 60-second intervals or forced immediately upon clicking Button A).
* **LCD Display:** Renders real-time telemetry, network status, and independent upload progress for both pipelines.

```text
+-------------------------------------------------------------+
| M5S3 FLEET TELEMETRY           WIFI: -68dBm | BAT: 94%      |
+-------------------------------------------------------------+
| CURRENT ACCEL: 1.02 g          OPERATING STATE: NOMINAL     |
+-------------------------------------------------------------+
| PIPELINE A: THINGSPEAK         | PIPELINE B: GOOGLE SHEETS  |
| Next Post:  12s                | Next Post:  48s            |
| Last Entry: #104 (200 OK)      | Rows Sent:  12 (200 OK)    |
+-------------------------------------------------------------+
| [BtnA] Force Audit Event       | [BtnB] Toggle LCD Sleep    |
+-------------------------------------------------------------+

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 5.4: Integrated Dual-Channel Cloud Telemetry Node
 * ============================================================================
 */
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include "secrets.h"

// Telemetry Timers & State Variables
static unsigned long g_last_ts_post_ms = 0;
static unsigned long g_last_gf_post_ms = 0;
static uint32_t g_ts_entry_id = 0;
static uint32_t g_gf_rows_sent = 0;
static int g_last_ts_code = 0;
static int g_last_gf_code = 0;

void drawSystemDashboard() {
    M5.Display.fillScreen(TFT_BLACK);

    // Top Navigation & Network Ribbon
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.drawString(" FLEET TELEMETRY", 5, 10);

    // Dynamic Sensor Metrics Bar
    M5.Display.fillRect(0, 21, 240, 32, TFT_BLACK);
    M5.Display.drawFastHLine(0, 53, 240, TFT_DARKGRAY);

    // Dual Pipeline Panel Layout
    M5.Display.drawFastHLine(0, 114, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 54, 60, TFT_DARKGRAY);

    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("THINGSPEAK", 60, 56);
    M5.Display.drawString("GOOGLE FORMS", 180, 56);

    // Bottom Softkey Hints
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Audit Event | BtnB: Silence", 120, 124);
}

bool transmitThingSpeak(float vib_g, int bat_pct, float temp_c, int alert_flag) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    String url = "http://api.thingspeak.com/update";
    url += "?api_key=" + String(ThingSpeakConfig::WRITE_API_KEY);
    url += "&field1="  + String(vib_g, 3);
    url += "&field2="  + String(bat_pct);
    url += "&field3="  + String(temp_c, 1);
    url += "&field4="  + String(alert_flag);

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

bool transmitGoogleForm(const String& dev_id, float vib_g, int bat_pct, const String& state, const String& note) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClientSecure secure_client;
    secure_client.setInsecure();

    HTTPClient http;
    http.begin(secure_client, GoogleFormConfig::FORM_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String payload = "";
    payload += String(GoogleFormConfig::ENTRY_UUID)  + "=" + dev_id + "&";
    payload += String(GoogleFormConfig::ENTRY_VIB)   + "=" + String(vib_g, 3) + "&";
    payload += String(GoogleFormConfig::ENTRY_BAT)   + "=" + String(bat_pct) + "&";
    payload += String(GoogleFormConfig::ENTRY_STATE) + "=" + state + "&";
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
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    drawSystemDashboard();

    Serial.begin(115200);
    delay(2000);

    // Initialize 2.4GHz Wi-Fi Radio
    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Associating with 2.4G AP...", 120, 80);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\n[WIFI] Connected. Dual-pipeline engine starting.");
    drawSystemDashboard();
}

void loop() {
    M5.update();

    // 1. Read Physical Hardware Registers
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float vibration_g = sqrtf((ax * ax) + (ay * ay) + (az * az));
    int battery_pct = M5.Power.getBatteryLevel();
    float core_temp = temperatureRead();
    int alert_flag = (vibration_g > 1.35f) ? 1 : 0;
    String operational_state = (alert_flag == 1) ? "ANOMALY_VIB" : "NOMINAL";

    uint64_t mac = ESP.getEfuseMac();
    char dev_uuid[16];
    snprintf(dev_uuid, sizeof(dev_uuid), "S3_%04X", (uint16_t)mac);

    // 2. Render Top Status Bar (RSSI & Battery)
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.printf("%d dBm | %d%% ", WiFi.RSSI(), battery_pct);

    // 3. Render Live Physical Telemetry
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.drawString(" VIB: " + String(vibration_g, 2) + " g", 10, 36);

    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor((alert_flag == 1) ? TFT_RED : TFT_GREEN, TFT_BLACK);
    M5.Display.drawString("STATUS: " + operational_state + " ", 230, 36);

    // 4. Pipeline A Execution: ThingSpeak Non-Blocking Timer
    unsigned long current_ms = millis();
    if (current_ms - g_last_ts_post_ms >= ThingSpeakConfig::POST_INTERVAL_MS) {
        g_last_ts_post_ms = current_ms;
        transmitThingSpeak(vibration_g, battery_pct, core_temp, alert_flag);
    }

    // 5. Pipeline B Execution: Google Forms Timer & Event Handler
    bool force_audit_event = M5.BtnA.wasClicked();
    if (force_audit_event || (current_ms - g_last_gf_post_ms >= GoogleFormConfig::POST_INTERVAL_MS)) {
        g_last_gf_post_ms = current_ms;
        String note = force_audit_event ? "Manual_Audit_BtnA" : "Scheduled_Sync";
        transmitGoogleForm(String(dev_uuid), vibration_g, battery_pct, operational_state, note);
        if (force_audit_event) M5.Speaker.tone(2000, 100);
    }

    // 6. Render Dynamic Pipeline Metrics
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    // Left Column: ThingSpeak Metrics
    M5.Display.setCursor(10, 72);
    M5.Display.printf("Entry: #%u   \n", g_ts_entry_id);
    M5.Display.setCursor(10, 88);
    M5.Display.setTextColor((g_last_ts_code == 200) ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Display.printf("HTTP:  %d    \n", g_last_ts_code);

    // Right Column: Google Forms Metrics
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(130, 72);
    M5.Display.printf("Rows:  %u   \n", g_gf_rows_sent);
    M5.Display.setCursor(130, 88);
    M5.Display.setTextColor((g_last_gf_code == 200 || g_last_gf_code == 302) ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Display.printf("HTTP:  %d    \n", g_last_gf_code);

    delay(50);
}

```

### Build & Upload

```bash
pio run -t upload
pio device monitor

```

---

## 7. Troubleshooting & Network Diagnostic Matrix

| Error Signature | Underlying Failure | Corrective Protocol |
| --- | --- | --- |
| **`WiFi.status()` loops indefinitely in disconnected state** | Access point is broadcasting on 5 GHz only, or SSID is hidden. | Configure the router to broadcast an isolated 2.4 GHz 802.11 b/g/n SSID. |
| **ThingSpeak consistently returns HTTP code `-1` (`HTTPC_ERROR_CONNECTION_REFUSED`)** | DNS failed to resolve `api.thingspeak.com` or local gateway blocked port 80. | Verify router allows outbound TCP traffic on port 80. Test gateway DNS configuration. |
| **ThingSpeak returns HTTP `200` but payload is `0**` | Ingress rate limit breached ($< 15\text{ seconds}$). | Ensure `ThingSpeakConfig::POST_INTERVAL_MS` is set to $\ge 16000\text{ ms}$. |
| **Google Form returns HTTP `400 Bad Request**` | Entry token mismatch or payload schema formatting error. | Verify that every `entry.XXXXXXXXX` key matches the pre-filled link generated in Lesson 02. |
| **Google Form returns HTTP `401 Unauthorized` / `403 Forbidden**` | Form settings require account authentication. | Open Google Form Settings $\rightarrow$ Responses: Disable **"Limit to 1 response"** and **"Restrict to users in [Domain]"**. |
| **Crash with `Guru Meditation Error: Core 0 panic'ed` during TLS handshake** | Stack overflow on task handling `WiFiClientSecure`. | Increase stack size if allocating inside FreeRTOS tasks. In standard `loop()`, ensure `client.setInsecure()` is used. |

---

## 8. Hands-On Verification Lab

Complete the following validation tasks:

* [ ] **Task 5.1:** Flash Exercise 5.4. Verify on your ThingSpeak channel dashboard that at least 5 consecutive data frames appear spaced 16 seconds apart.
* [ ] **Task 5.2:** Tap the M5Stick S3 to generate a vibration spike ($> 1.35\text{ g}$). Verify that `field4` on ThingSpeak logs a value of `1`.
* [ ] **Task 5.3:** Click **Button A** on the front panel. Open your linked Google Sheet and confirm that a new row appears with the remark `"Manual_Audit_BtnA"` and a current timestamp.

---

## 9. Version Control Protocol

Stage and commit your cloud telemetry firmware:

```bash
# Verify secrets.h remains uncommitted
git status

# Add firmware assets and example templates
git add platformio.ini include/secrets.example.h src/main.cpp .gitignore

# Commit changes
git commit -m "feat(lesson05): implement dual-channel edge telemetry (thingspeak rest and google forms https)"

```

---
