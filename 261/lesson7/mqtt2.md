# Lesson 07: Enterprise MQTT Protocol — Industrial Pub/Sub Messaging on M5Stack Stick S3

While HTTP REST incurs heavy TCP connection overhead and request-response latency, **MQTT (Message Queuing Telemetry Transport)** is the global standard for industrial IoT. MQTT uses persistent TCP sessions with a fixed 2-byte header, enabling sub-50 ms latency for real-time edge telemetry and remote cloud actuation.

In this module, you will interface the **M5Stack Stick S3** directly with two enterprise MQTT platforms:

1. **Adafruit IO MQTT:** A real-time, low-latency Pub/Sub broker powering sub-second bi-directional control (cloud dashboard toggles triggering physical buzzers and LCD color shifts; physical button clicks driving cloud gauges instantly).
2. **ThingSpeak MQTT:** An industrial time-series broker utilizing authenticated channel-based publishing (`mqtt3.thingspeak.com`) to log structured multi-field telemetry without HTTP overhead.

```text
                  ENTERPRISE BI-DIRECTIONAL MQTT ARCHITECTURE
                  
  +-------------------------------------------------------------------------+
  | EDGE NODE: M5Stack Stick S3                                             |
  |  - Sensors: BMI270 (Vibration Magnitude), AXP2101 (Battery Gauge)       |
  |  - Actuators: Piezo Buzzer (Acoustic Siren), ST7789 Color LCD Panel     |
  |  - Inputs: BtnA (Instant Audit Publisher), BtnB (Local Reset)           |
  +-------------------------------------------------------------------------+
                    |                                       ^
     PUBLISH        | TCP Port 1883                         | SUBSCRIBE
     Telemetry /    | Persistent Keep-Alive                 | Actuator Command
     Vibration      v                                       | (Instant Push)
  +-------------------------------------------------------------------------+
  | CENTRAL MESSAGE BROKERS                                                 |
  |                                                                         |
  |  [BROKER A: ADAFRUIT IO]               [BROKER B: THINGSPEAK MQTT]      |
  |  Host: `io.adafruit.com:1883`          Host: `mqtt3.thingspeak.com:1883`|
  |  - Sub-second push/pull                - Long-term time-series storage  |
  |  - Feeds: `/vibration`, `/command`     - Topic: `channels/{id}/publish` |
  +-------------------------------------------------------------------------+
                    |                                       ^
     SUBSCRIBE      | WebSockets / SSL                      | PUBLISH
     Live Telemetry v                                       | Remote Switches
  +-------------------------------------------------------------------------+
  | CLOUD OPERATIONS COMMAND CENTER                                         |
  |  - Adafruit IO Live Dashboard (Gauges, Interactive Buttons, Color Pick) |
  |  - ThingSpeak Analytical Charts & Condition Monitoring Plots            |
  +-------------------------------------------------------------------------+

```

---

## 1. Technical Architecture: Adafruit IO vs. ThingSpeak MQTT vs. HTTP REST

Selecting an IoT ingestion protocol directly governs battery consumption, packet overhead, and bidirectional capabilities:

| Protocol / Broker | Wire Overhead | Latency | Model | Bi-Directional Actuation | Free-Tier Limits | Primary Enterprise Role |
| --- | --- | --- | --- | --- | --- | --- |
| **HTTP REST** (Lesson 05)

 | 200–800 bytes per request (HTTP headers)

 | 300–1500 ms (TCP handshake per request) | Request-Response (Client Polling)

 | Difficult (Requires continuous polling or port forwarding)

 | ThingSpeak: 15s throttle

 | Infrequent batched audit logs

 |
| **Adafruit IO (MQTT)** | **2-byte fixed header**

 | **< 30 ms** | Publish-Subscribe (Push)

 | **Native & Instant** (Broker pushes directly to subscribed device)

 | 30 data points/minute (1 req/2 sec) | Rapid dashboard interaction & remote physical control |
| **ThingSpeak (MQTT)** | **2-byte fixed header**

 | **< 50 ms** | Publish-Subscribe (Push)

 | Available via channel subscription | 15s rate limit per channel | Structured fleet time-series without HTTP connection tearing |

### MQTT Mechanics Under the Hood

* **Persistent TCP Session:** The ESP32-S3 establishes a single TCP socket over port 1883 and maintains it with lightweight `PINGREQ`/`PINGRESP` keep-alive packets. The device does not tear down and renegotiate TCP/IP and TLS sessions on every reading.


* **Quality of Service (QoS):**
* `QoS 0` (At most once): "Fire and forget." Lowest latency, minimal RAM footprint. Ideal for high-frequency IMU vibration telemetry.


* `QoS 1` (At least once): Guaranteed delivery via `PUBACK`. Used for critical operator commands (e.g., remote buzzer triggering).




* **Packet Size Arbitration:** The default Arduino `PubSubClient` buffer is 128 or 256 bytes. When publishing JSON frames or subscribing to long topic paths, the buffer must be configured via `client.setBufferSize(512)` or compilation flags to prevent truncated packets.

---

## 2. Project Directory Layout & `platformio.ini`

Maintain a single clean workspace structure:

```text
lesson07-mqtt-protocol/
├── .gitignore
├── platformio.ini
├── include/
│   ├── secrets.example.h
│   └── secrets.h            <- Excluded from Git tracking
└── src/
    └── main.cpp

```

Initialize your workspace:

```bash
mkdir -p lesson07-mqtt-protocol/include lesson07-mqtt-protocol/src
cd lesson07-mqtt-protocol

```

### Verified `platformio.ini` (Clean ASCII INI)

This configuration defines the hardware parameters for the ESP32-S3, sets memory speeds, routes serial output through the native USB CDC controller, and downloads `M5Unified` and `PubSubClient`:

```ini
; =====================================================================
; PlatformIO Configuration: Lesson 07 - Enterprise MQTT Protocol
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

monitor_speed = 115200
upload_speed = 921600

board_build.f_cpu = 240000000L
board_build.flash_mode = qio
board_build.flash_size = 8MB

build_flags =
    -std=gnu++17
    -DESP32S3
    -DARDUINO_M5STACK_STICK_S3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -DMQTT_MAX_PACKET_SIZE=512
    -O2

lib_deps =
    m5stack/M5Unified
    m5stack/M5GFX
    knolleary/PubSubClient@^2.8

; ---------------------------------------------------------------------
; Environment 2: M5StickC / M5StickC Plus (ESP32 Classic Fallback)
; ---------------------------------------------------------------------
[env:m5stick-plus]
platform = espressif32
board = m5stick-c
framework = arduino
monitor_speed = 115200
upload_speed = 1500000

build_flags =
    -DMQTT_MAX_PACKET_SIZE=512

lib_deps =
    m5stack/M5Unified
    m5stack/M5GFX
    knolleary/PubSubClient@^2.8

```

---

## 3. Cloud Provisioning & Broker Configuration

### 3.1 Adafruit IO Setup (Bi-Directional Command & Telemetry)

1. Navigate to [io.adafruit.com](https://io.adafruit.com/) and create a free account.
2. Click **Feeds** $\rightarrow$ **New Feed** and create two feeds:
* Name: `stick-vibration` (Used by M5Stick S3 to publish live IMU data).
* Name: `stick-command` (Used by the web dashboard to publish commands to the M5Stick S3).


3. Click **Dashboards** $\rightarrow$ **New Dashboard** $\rightarrow$ `M5Stick S3 Command Center`.
4. Add two interactive UI blocks:
* **Gauge Block:** Link to feed `stick-vibration`. Range: `0.0` to `3.0`.
* **Momentary Push Button Block:** Link to feed `stick-command`. Set value to `BEEP`.
* **Toggle Switch Block:** Link to feed `stick-command`. Set **ON value** to `ALARM_ON` and **OFF value** to `ALARM_OFF`.


5. Click the yellow key icon (**My Key**) in the top right corner. Copy your **Username** and **Active Key**.

```text
Adafruit IO MQTT Topic Taxonomy:
  {AIO_USERNAME}/feeds/stick-vibration   <-- Device publishes telemetry here
  {AIO_USERNAME}/feeds/stick-command     <-- Device subscribes to commands here

```

---

### 3.2 ThingSpeak MQTT Setup (Channel Pub/Sub)

ThingSpeak does not use your regular web password for MQTT. It mandates unique **MQTT Device Credentials**:

1. Log in to [thingspeak.com](https://thingspeak.com/) and open your channel from Lesson 02/05.


2. Click **Devices** in the top navigation bar $\rightarrow$ **MQTT Devices**.
3. Click **Add Device**:
* Name: `M5StickS3_Gateway`
* Select your Channel from the list.


4. Click **Add Device** and save the generated credentials:
* **Client ID** (e.g., `OAMQEwwXNDc2ORkQIRsgFgc`)
* **Username** (e.g., `OAMQEwwXNDc2ORkQIRsgFgc`)
* **Password** (e.g., `kYxU8q9T7wZv1L2m...`)



```text
ThingSpeak MQTT Topic Taxonomy:
  channels/{CHANNEL_ID}/publish   <-- Device publishes payload string: field1=X&field2=Y
  channels/{CHANNEL_ID}/subscribe <-- Optional subscriber topic

```

---

### 3.3 Credential Isolation Template (`include/secrets.example.h`)

Create `include/secrets.example.h`:

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
// 2. Broker A: Adafruit IO Configuration
// -----------------------------------------------------------------------------
namespace AdafruitIOConfig {
    const char* BROKER_HOST   = "io.adafruit.com";
    const uint16_t BROKER_PORT= 1883;
    const char* USERNAME      = "YOUR_AIO_USERNAME";
    const char* AIO_KEY       = "YOUR_AIO_ACTIVE_KEY";
    
    // Feeds
    const char* FEED_VIB_PUB  = "YOUR_AIO_USERNAME/feeds/stick-vibration";
    const char* FEED_CMD_SUB  = "YOUR_AIO_USERNAME/feeds/stick-command";
    const unsigned long PUB_INTERVAL_MS = 2000; // Paced at 2 seconds
}

// -----------------------------------------------------------------------------
// 3. Broker B: ThingSpeak MQTT Configuration
// -----------------------------------------------------------------------------
namespace ThingSpeakMqttConfig {
    const char* BROKER_HOST   = "mqtt3.thingspeak.com";
    const uint16_t BROKER_PORT= 1883;
    const char* CLIENT_ID     = "YOUR_THINGSPEAK_MQTT_CLIENT_ID";
    const char* USERNAME      = "YOUR_THINGSPEAK_MQTT_USERNAME";
    const char* PASSWORD      = "YOUR_THINGSPEAK_MQTT_PASSWORD";
    const long  CHANNEL_ID    = 1234567; // Your Channel ID
    
    const unsigned long PUB_INTERVAL_MS = 16000; // Mandatory 16s cooldown
}

```

Copy the example file to `include/secrets.h` and configure your credentials:

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

## 4. Exercise 7.1: Low-Latency Bi-Directional Node with Adafruit IO MQTT

### Objective

Interface the M5Stack Stick S3 with **Adafruit IO** over MQTT:

1. **Cloud Actuation (Downlink):** The device subscribes to `{username}/feeds/stick-command`. When you click buttons or toggles on your Adafruit IO web dashboard, the broker pushes the message to the ESP32-S3 instantly (<50 ms):
* `"BEEP"`: Triggers a high-pitch acoustic chirp.
* `"ALARM_ON"`: Turns the LCD screen red and sounds the piezo alarm.
* `"ALARM_OFF"`: Resets the screen to dark green and silences the buzzer.


2. **Telemetry Streaming (Uplink):** The device publishes live Euclidean vibration ($\Vert{}A\Vert{}$) to `{username}/feeds/stick-vibration` every 2 seconds. Clicking physical **Button A** forces an immediate telemetry burst.

```text
+-------------------------------------------------------------+
| ADAFRUIT IO BI-DIRECTIONAL MQTT              WIFI: -54 dBm  |
+-------------------------------------------------------------+
| VIBRATION TELEMETRY          | REMOTE ACTUATOR STATE        |
|                              |                              |
|          1.02 G              |       CMD: ALARM_ON          |
|    (Peak: 1.48 G)            |       [RED SCREEN]           |
+-------------------------------------------------------------+
| [BtnA] Force Instant Pub     | Latency: < 40 ms [TCP LIVE]  |
+-------------------------------------------------------------+

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 7.1: Adafruit IO Low-Latency Bi-Directional MQTT Node
 * Hardware: M5Stack Stick S3 (BMI270, Speaker, LCD, Buttons)
 * Protocol: MQTT Port 1883 via PubSubClient
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>
#include "secrets.h"

// Networking & MQTT Clients
static WiFiClient g_tcp_client;
static PubSubClient g_mqtt(g_tcp_client);

// Operational Telemetry Counters
static unsigned long g_last_pub_ms = 0;
static uint32_t g_packets_published = 0;
static uint32_t g_commands_received = 0;
static String g_latest_command = "NONE";
static uint16_t g_status_color = TFT_DARKGREEN;
static bool g_alarm_state = false;
static float g_peak_vibration = 0.0f;

// ----------------------------------------------------------------------------
// 1. INCOMING MQTT SUBSCRIBER CALLBACK (DOWNLINK COMMAND PROCESSOR)
// ----------------------------------------------------------------------------
void onMqttCommandReceived(char* topic, byte* payload, unsigned int length) {
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    String cmd = String(message);
    cmd.trim();
    cmd.toUpperCase();

    g_commands_received++;
    g_latest_command = cmd;

    Serial.printf("\n[MQTT RX] Topic: %s | Payload: %s\n", topic, cmd.c_str());

    // Execute Immediate Physical Actuation
    if (cmd == "BEEP") {
        M5.Speaker.tone(2400, 100);
    } 
    else if (cmd == "ALARM_ON") {
        g_alarm_state = true;
        g_status_color = TFT_RED;
        M5.Speaker.tone(1200, 250);
    } 
    else if (cmd == "ALARM_OFF") {
        g_alarm_state = false;
        g_status_color = TFT_DARKGREEN;
        M5.Speaker.tone(600, 100);
    }
}

// ----------------------------------------------------------------------------
// 2. CONNECTION LIFECYCLE MANAGEMENT
// ----------------------------------------------------------------------------
void ensureMqttConnection() {
    while (!g_mqtt.connected()) {
        if (WiFi.status() != WL_CONNECTED) {
            delay(100);
            return;
        }

        Serial.printf("[MQTT] Connecting to Adafruit IO (%s:%d)...\n", 
                      AdafruitIOConfig::BROKER_HOST, AdafruitIOConfig::BROKER_PORT);

        // Derive unique client ID from MAC
        uint64_t mac = ESP.getEfuseMac();
        char client_id[32];
        snprintf(client_id, sizeof(client_id), "M5S3_%04X", (uint16_t)mac);

        // Connect with AIO Username & Key
        if (g_mqtt.connect(client_id, AdafruitIOConfig::USERNAME, AdafruitIOConfig::AIO_KEY)) {
            Serial.println("[MQTT] Connected to Adafruit IO Broker.");

            // Subscribe to remote command feed
            g_mqtt.subscribe(AdafruitIOConfig::FEED_CMD_SUB);
            Serial.printf("[MQTT] Subscribed to command feed: %s\n", AdafruitIOConfig::FEED_CMD_SUB);

            M5.Speaker.tone(2000, 60);
        } else {
            Serial.printf("[MQTT] Connect failed, state: %d. Retrying in 2 seconds...\n", g_mqtt.state());
            delay(2000);
        }
    }
}

// ----------------------------------------------------------------------------
// 3. UI RENDERING ENGINE
// ----------------------------------------------------------------------------
void drawBaseUI() {
    M5.Display.fillScreen(TFT_BLACK);

    // Top Header Ribbon
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.drawString(" AIO BI-DIRECTIONAL MQTT", 5, 10, &fonts::Font2);

    // Grid Dividers
    M5.Display.drawFastHLine(0, 56, 240, TFT_DARKGRAY);
    M5.Display.drawFastHLine(0, 114, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 20, 36, TFT_DARKGRAY);

    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VIB (MAGNITUDE)", 60, 23, &fonts::Font0);
    M5.Display.drawString("BATTERY FUEL", 180, 23, &fonts::Font0);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Force Publish | BtnB: Reset", 120, 124, &fonts::Font0);
}

void renderDashboard(float current_vib, int battery_pct) {
    char buf[32];

    // Top Right Wi-Fi & MQTT Ticker
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    snprintf(buf, sizeof(buf), "%d dBm | LIVE ", WiFi.RSSI());
    M5.Display.drawString(buf, 235, 10, &fonts::Font0);

    // Telemetry Panel
    M5.Display.setTextDatum(textdatum_t::middle_center);
    snprintf(buf, sizeof(buf), "%4.2f G", current_vib);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(buf, 60, 42, &fonts::Font4);

    snprintf(buf, sizeof(buf), "%d%%", battery_pct);
    M5.Display.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    M5.Display.drawString(buf, 180, 42, &fonts::Font4);

    // Downlink Status Panel (Actuator Output Display)
    M5.Display.fillRect(0, 58, 240, 54, g_status_color);
    M5.Display.setTextColor(TFT_WHITE, g_status_color);
    M5.Display.setTextSize(2);
    snprintf(buf, sizeof(buf), "CMD: %s", g_latest_command.c_str());
    M5.Display.drawString(buf, 120, 74);
    M5.Display.setTextSize(1);

    snprintf(buf, sizeof(buf), "Published: %u  |  Received: %u", g_packets_published, g_commands_received);
    M5.Display.drawString(buf, 120, 96, &fonts::Font0);
}

// ----------------------------------------------------------------------------
// 4. MAIN SETUP & LOOP
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_spk = true;
    cfg.internal_mic = false; // Mute mic to give speaker full I2S DMA access
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);
    M5.Speaker.setVolume(140);

    drawBaseUI();

    // Configure Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Connecting to 2.4G Wi-Fi...", 120, 85, &fonts::Font2);

    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
    }
    Serial.println("\n[WIFI] Connected.");

    // Setup MQTT Broker
    g_mqtt.setServer(AdafruitIOConfig::BROKER_HOST, AdafruitIOConfig::BROKER_PORT);
    g_mqtt.setCallback(onMqttCommandReceived);
    g_mqtt.setBufferSize(512);

    ensureMqttConnection();
    drawBaseUI();
}

void loop() {
    M5.update();

    if (!g_mqtt.connected()) {
        ensureMqttConnection();
    }
    g_mqtt.loop(); // CRITICAL: Processes incoming subscriber packets & sends keep-alive pings

    // 1. Acquire Telemetry
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float current_vib = sqrtf((ax * ax) + (ay * ay) + (az * az));
    if (current_vib > g_peak_vibration) g_peak_vibration = current_vib;

    int battery_pct = M5.Power.getBatteryLevel();
    if (battery_pct < 0) battery_pct = 100;

    // 2. Local Inputs
    bool force_publish = M5.BtnA.wasClicked();
    if (M5.BtnB.wasClicked()) {
        g_latest_command = "RESET";
        g_status_color = TFT_DARKGREEN;
        g_peak_vibration = current_vib;
        M5.Speaker.tone(1500, 50);
    }

    // 3. Publish Telemetry (Interval or Instant Button Trigger)
    unsigned long now = millis();
    if (force_publish || (now - g_last_pub_ms >= AdafruitIOConfig::PUB_INTERVAL_MS)) {
        g_last_pub_ms = now;

        char payload_str[16];
        snprintf(payload_str, sizeof(payload_str), "%.3f", current_vib);

        if (g_mqtt.publish(AdafruitIOConfig::FEED_VIB_PUB, payload_str)) {
            g_packets_published++;
            Serial.printf("[MQTT TX] Published to [%s]: %s\n", AdafruitIOConfig::FEED_VIB_PUB, payload_str);
            if (force_publish) M5.Speaker.tone(2000, 40);
        } else {
            Serial.println("[MQTT TX] Publish failed.");
        }
    }

    // 4. Render UI
    renderDashboard(current_vib, battery_pct);

    delay(20);
}

```

---

## 5. Exercise 7.2: Structured Telemetry Publishing via ThingSpeak MQTT

### Objective

Stream multi-field sensor telemetry directly to **ThingSpeak** over MQTT (`mqtt3.thingspeak.com`) using device-authenticated channel publishing.

Instead of formatting bulky HTTP GET URLs, construct lightweight URL-encoded payload strings (`field1=1.02&field2=85&field3=38.2&status=OK`) and publish them directly to `channels/{channelID}/publish`. Maintain non-blocking rate pacing ($\ge 16\text{ seconds}$).

```text
                  THINGSPEAK MQTT PROTOCOL INGESTION
                  
  M5Stack Stick S3                                ThingSpeak MQTT Broker (Port 1883)
         |                                                       |
         | --- 1. CONNECT (ClientID, Username, Password) ------> |
         | <--- 2. CONNACK (Connection Accepted) -------------- |
         |                                                       |
         | --- 3. PUBLISH: `channels/{id}/publish` ------------> |
         |        Payload: "field1=1.042&field2=94&field3=37.8"  |
         |                                                       | (Appends to Channel DB)
         | --- 4. Enforce Non-Blocking 16s Cooldown -----------> |
         | --- 5. PINGREQ / PINGRESP (Keep-Alive) -------------> |

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 7.2: ThingSpeak Dedicated MQTT Channel Publisher
 * Hardware: M5Stack Stick S3 (BMI270 IMU, ST7789 LCD, AXP2101 PMIC)
 * Protocol: MQTT Port 1883 via ThingSpeak MQTT Device Credentials
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>
#include "secrets.h"

static WiFiClient g_tcp_client;
static PubSubClient g_mqtt(g_tcp_client);

static unsigned long g_last_pub_ms = 0;
static uint32_t g_telemetry_sequence = 0;
static bool g_last_pub_success = false;
static char g_topic_publish[64];

void drawThingSpeakUI() {
    M5.Display.fillScreen(TFT_BLACK);

    // Header
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.drawString(" THINGSPEAK MQTT ENGINE", 5, 10, &fonts::Font2);

    // Grid Dividers
    M5.Display.drawFastHLine(0, 70, 240, TFT_DARKGRAY);
    M5.Display.drawFastHLine(0, 114, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 20, 50, TFT_DARKGRAY);

    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VIB ACCELERATION", 60, 24, &fonts::Font0);
    M5.Display.drawString("BATTERY FUEL", 180, 24, &fonts::Font0);
}

void ensureMqttConnection() {
    while (!g_mqtt.connected()) {
        if (WiFi.status() != WL_CONNECTED) {
            delay(100);
            return;
        }

        Serial.printf("[MQTT] Connecting to ThingSpeak Broker (%s:%d)...\n", 
                      ThingSpeakMqttConfig::BROKER_HOST, ThingSpeakMqttConfig::BROKER_PORT);

        if (g_mqtt.connect(ThingSpeakMqttConfig::CLIENT_ID, 
                           ThingSpeakMqttConfig::USERNAME, 
                           ThingSpeakMqttConfig::PASSWORD)) {
            Serial.println("[MQTT] Connected to ThingSpeak MQTT Gateway.");
            M5.Speaker.tone(1800, 50);
        } else {
            Serial.printf("[MQTT] Connection failed, rc=%d. Retrying in 2 seconds...\n", g_mqtt.state());
            delay(2000);
        }
    }
}

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_spk = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);
    M5.Speaker.setVolume(120);

    drawThingSpeakUI();

    // Construct ThingSpeak Topic: channels/{channelID}/publish
    snprintf(g_topic_publish, sizeof(g_topic_publish), "channels/%ld/publish", ThingSpeakMqttConfig::CHANNEL_ID);

    // Wi-Fi Connection
    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Connecting to Wi-Fi...", 120, 85, &fonts::Font2);

    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
    }
    Serial.println("\n[WIFI] Connected.");

    // Setup MQTT
    g_mqtt.setServer(ThingSpeakMqttConfig::BROKER_HOST, ThingSpeakMqttConfig::BROKER_PORT);
    g_mqtt.setBufferSize(512);

    ensureMqttConnection();
    drawThingSpeakUI();
}

void loop() {
    M5.update();

    if (!g_mqtt.connected()) {
        ensureMqttConnection();
    }
    g_mqtt.loop();

    // 1. Acquire Telemetry
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float vibration_g = sqrtf((ax * ax) + (ay * ay) + (az * az));
    int battery_pct = M5.Power.getBatteryLevel();
    if (battery_pct < 0) battery_pct = 100;
    float core_temp = temperatureRead();
    int alert_flag = (vibration_g > 1.45f) ? 1 : 0;

    // 2. Render Numerical Readouts
    char buf[48];
    M5.Display.setTextDatum(textdatum_t::middle_center);

    snprintf(buf, sizeof(buf), "%4.2f G", vibration_g);
    M5.Display.setTextColor((vibration_g > 1.45f) ? TFT_RED : TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(buf, 60, 48, &fonts::Font4);

    snprintf(buf, sizeof(buf), "%d%%", battery_pct);
    M5.Display.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    M5.Display.drawString(buf, 180, 48, &fonts::Font4);

    // 3. Non-Blocking 16-Second Rate Pacing
    unsigned long now = millis();
    unsigned long elapsed = now - g_last_pub_ms;
    unsigned long cooldown = ThingSpeakMqttConfig::PUB_INTERVAL_MS;

    if (elapsed >= cooldown) {
        g_last_pub_ms = now;

        // Construct ThingSpeak multi-field string
        // field1=Vib & field2=Battery & field3=Temp & field4=Alert
        char payload[128];
        snprintf(payload, sizeof(payload), 
                 "field1=%.3f&field2=%d&field3=%.1f&field4=%d&status=MQTT_OK",
                 vibration_g, battery_pct, core_temp, alert_flag);

        Serial.printf("[ThingSpeak MQTT TX] Topic: %s | Payload: %s\n", g_topic_publish, payload);

        g_last_pub_success = g_mqtt.publish(g_topic_publish, payload);
        if (g_last_pub_success) {
            g_telemetry_sequence++;
            M5.Speaker.tone(2200, 40);
        }
    }

    // 4. Ingress Status & Cooldown UI
    M5.Display.fillRect(0, 72, 240, 24, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    if (g_last_pub_success) {
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        snprintf(buf, sizeof(buf), "SYNC ACTIVE | Frames Sent: #%u", g_telemetry_sequence);
    } else {
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        snprintf(buf, sizeof(buf), "STANDBY | Next Ingress: %2us", 
                 (elapsed < cooldown) ? (unsigned int)((cooldown - elapsed) / 1000) : 0);
    }
    M5.Display.drawString(buf, 120, 84, &fonts::Font2);

    // Progress Bar
    int bar_width = (elapsed < cooldown) ? (int)((elapsed * 220) / cooldown) : 220;
    M5.Display.drawRect(10, 98, 220, 8, TFT_DARKGRAY);
    M5.Display.fillRect(11, 99, bar_width, 6, TFT_DARKCYAN);
    M5.Display.fillRect(11 + bar_width, 99, 220 - bar_width, 6, TFT_BLACK);

    // Footer Status
    snprintf(buf, sizeof(buf), "Temp: %.1f C | Channel: %ld", core_temp, ThingSpeakMqttConfig::CHANNEL_ID);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString(buf, 120, 124, &fonts::Font0);

    delay(30);
}

```

---

## 6. Exercise 7.3: Unified Dual-Broker Edge Gateway

### Objective

Unify high-speed remote interaction with enterprise time-series aggregation in a single non-blocking architecture:

* **Adafruit IO Client:** Runs at sub-second speeds. Listens for operator actuator overrides (e.g., `BEEP`, `ALARM_ON`, `ALARM_OFF`) and publishes instant peak alerts.
* **ThingSpeak MQTT Client:** Periodically packages and pushes aggregated physical telemetry (vibration, battery, temperature) on a strict 16-second schedule.
* **Local HMI Dashboard:** Displays active broker states, transmission counters, and live sensor readouts on the ST7789 display.

```text
+-------------------------------------------------------------+
| DUAL-BROKER MQTT GATEWAY                     WIFI: -52 dBm  |
+-------------------------------------------------------------+
| VIBRATION: 1.04 G (Peak: 1.62G) | BATTERY FUEL: 92%         |
+-------------------------------------------------------------+
| AIO INTERACTION (Downlink/Up)  | THINGSPEAK ARCHIVE (Uplink)|
| Status: CMD: ALARM_OFF         | Status: Frame #18 Ingested |
| Latency: < 30 ms               | Interval: 16s Cadence      |
+-------------------------------------------------------------+
| [BtnA] Force AIO Burst         | [BtnB] Clear Local Peak    |
+-------------------------------------------------------------+

```

### Source Code (`src/main.cpp`)

```cpp
/**
 * ============================================================================
 * Exercise 7.3: Unified Dual-Broker Edge Gateway
 * Hardware: M5Stack Stick S3 (BMI270, Speaker, LCD, Buttons)
 * Services: Adafruit IO (Sub-second Control) + ThingSpeak (Time-Series)
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>
#include "secrets.h"

// Dedicated TCP Clients & MQTT Instances
static WiFiClient g_tcp_aio;
static PubSubClient g_mqtt_aio(g_tcp_aio);

static WiFiClient g_tcp_ts;
static PubSubClient g_mqtt_ts(g_tcp_ts);

// Telemetry & State Variables
static unsigned long g_last_aio_pub_ms = 0;
static unsigned long g_last_ts_pub_ms = 0;
static uint32_t g_aio_tx_count = 0;
static uint32_t g_aio_rx_count = 0;
static uint32_t g_ts_tx_count = 0;

static String g_aio_command = "NOMINAL";
static uint16_t g_alarm_color = TFT_DARKGREEN;
static float g_peak_vibration = 0.0f;
static char g_ts_topic[64];

// ----------------------------------------------------------------------------
// ADAFRUIT IO SUBSCRIBER CALLBACK
// ----------------------------------------------------------------------------
void onAioCommand(char* topic, byte* payload, unsigned int length) {
    char msg[length + 1];
    memcpy(msg, payload, length);
    msg[length] = '\0';

    String cmd = String(msg);
    cmd.trim();
    cmd.toUpperCase();

    g_aio_rx_count++;
    g_aio_command = cmd;

    Serial.printf("[AIO RX] Command received: %s\n", cmd.c_str());

    if (cmd == "BEEP") {
        M5.Speaker.tone(2200, 80);
    } else if (cmd == "ALARM_ON") {
        g_alarm_color = TFT_RED;
        M5.Speaker.tone(1000, 200);
    } else if (cmd == "ALARM_OFF") {
        g_alarm_color = TFT_DARKGREEN;
        M5.Speaker.tone(500, 100);
    }
}

// ----------------------------------------------------------------------------
// BROKER CONNECTION MANAGERS
// ----------------------------------------------------------------------------
void maintainAioConnection() {
    if (g_mqtt_aio.connected()) return;

    uint64_t mac = ESP.getEfuseMac();
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "AIO_%04X", (uint16_t)mac);

    Serial.println("[AIO MQTT] Reconnecting...");
    if (g_mqtt_aio.connect(client_id, AdafruitIOConfig::USERNAME, AdafruitIOConfig::AIO_KEY)) {
        Serial.println("[AIO MQTT] Connected.");
        g_mqtt_aio.subscribe(AdafruitIOConfig::FEED_CMD_SUB);
    }
}

void maintainThingSpeakConnection() {
    if (g_mqtt_ts.connected()) return;

    Serial.println("[TS MQTT] Reconnecting...");
    if (g_mqtt_ts.connect(ThingSpeakMqttConfig::CLIENT_ID, 
                          ThingSpeakMqttConfig::USERNAME, 
                          ThingSpeakMqttConfig::PASSWORD)) {
        Serial.println("[TS MQTT] Connected.");
    }
}

// ----------------------------------------------------------------------------
// GRAPHICAL DASHBOARD
// ----------------------------------------------------------------------------
void drawBaseUI() {
    M5.Display.fillScreen(TFT_BLACK);

    // Header Ribbon
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.drawString(" DUAL MQTT GATEWAY", 5, 10, &fonts::Font2);

    // Dividers
    M5.Display.drawFastHLine(0, 52, 240, TFT_DARKGRAY);
    M5.Display.drawFastHLine(0, 114, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 52, 62, TFT_DARKGRAY);

    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("ADAFRUIT IO", 60, 54, &fonts::Font0);
    M5.Display.drawString("THINGSPEAK", 180, 54, &fonts::Font0);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Force Publish | BtnB: Reset Peak", 120, 124, &fonts::Font0);
}

void updateDashboard(float vib, int bat) {
    char buf[48];

    // Header Signal Ticker
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    snprintf(buf, sizeof(buf), "%d dBm | DUAL-LIVE ", WiFi.RSSI());
    M5.Display.drawString(buf, 235, 10, &fonts::Font0);

    // Telemetry Summary Bar
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), " VIB: %.2fG (Pk: %.2f)", vib, g_peak_vibration);
    M5.Display.drawString(buf, 5, 36, &fonts::Font2);

    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    snprintf(buf, sizeof(buf), "BAT: %d%% ", bat);
    M5.Display.drawString(buf, 235, 36, &fonts::Font2);

    // Left Panel: Adafruit IO State
    M5.Display.fillRect(2, 68, 116, 44, g_alarm_color);
    M5.Display.setTextColor(TFT_WHITE, g_alarm_color);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString(g_aio_command, 60, 82, &fonts::Font2);
    snprintf(buf, sizeof(buf), "TX:%u | RX:%u", g_aio_tx_count, g_aio_rx_count);
    M5.Display.drawString(buf, 60, 100, &fonts::Font0);

    // Right Panel: ThingSpeak State
    M5.Display.fillRect(122, 68, 116, 44, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Frames: #%u", g_ts_tx_count);
    M5.Display.drawString(buf, 180, 82, &fonts::Font2);
    
    unsigned long ts_cooldown = ThingSpeakMqttConfig::PUB_INTERVAL_MS;
    unsigned long ts_elapsed = millis() - g_last_ts_pub_ms;
    int next_sync = (ts_elapsed < ts_cooldown) ? (int)((ts_cooldown - ts_elapsed) / 1000) : 0;
    snprintf(buf, sizeof(buf), "Next Sync: %2ds", next_sync);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString(buf, 180, 100, &fonts::Font0);
}

// ----------------------------------------------------------------------------
// SETUP & LOOP
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5StickS3;
    cfg.internal_spk = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);
    M5.Speaker.setVolume(140);

    drawBaseUI();

    snprintf(g_ts_topic, sizeof(g_ts_topic), "channels/%ld/publish", ThingSpeakMqttConfig::CHANNEL_ID);

    // Connect to 2.4 GHz Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Initializing Wi-Fi...", 120, 85, &fonts::Font2);

    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
    }
    Serial.println("\n[WIFI] Connected.");

    // Setup Broker A (Adafruit IO)
    g_mqtt_aio.setServer(AdafruitIOConfig::BROKER_HOST, AdafruitIOConfig::BROKER_PORT);
    g_mqtt_aio.setCallback(onAioCommand);
    g_mqtt_aio.setBufferSize(512);

    // Setup Broker B (ThingSpeak)
    g_mqtt_ts.setServer(ThingSpeakMqttConfig::BROKER_HOST, ThingSpeakMqttConfig::BROKER_PORT);
    g_mqtt_ts.setBufferSize(512);

    maintainAioConnection();
    maintainThingSpeakConnection();

    drawBaseUI();
}

void loop() {
    M5.update();

    if (WiFi.status() == WL_CONNECTED) {
        maintainAioConnection();
        maintainThingSpeakConnection();
    }

    g_mqtt_aio.loop();
    g_mqtt_ts.loop();

    // 1. Acquire Telemetry
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float current_vib = sqrtf((ax * ax) + (ay * ay) + (az * az));
    if (current_vib > g_peak_vibration) g_peak_vibration = current_vib;

    int battery_pct = M5.Power.getBatteryLevel();
    if (battery_pct < 0) battery_pct = 100;
    float core_temp = temperatureRead();

    // 2. Hardware Buttons
    bool force_pub = M5.BtnA.wasClicked();
    if (M5.BtnB.wasClicked()) {
        g_peak_vibration = current_vib;
        M5.Speaker.tone(1400, 60);
    }

    unsigned long now = millis();

    // 3. Broker A Pipeline: Adafruit IO (2-Second Rapid Stream or Button Burst)
    if (force_pub || (now - g_last_aio_pub_ms >= AdafruitIOConfig::PUB_INTERVAL_MS)) {
        g_last_aio_pub_ms = now;

        char aio_payload[16];
        snprintf(aio_payload, sizeof(aio_payload), "%.3f", current_vib);

        if (g_mqtt_aio.publish(AdafruitIOConfig::FEED_VIB_PUB, aio_payload)) {
            g_aio_tx_count++;
            if (force_pub) M5.Speaker.tone(2400, 40);
        }
    }

    // 4. Broker B Pipeline: ThingSpeak (16-Second Aggregation Stream)
    if (now - g_last_ts_pub_ms >= ThingSpeakMqttConfig::PUB_INTERVAL_MS) {
        g_last_ts_pub_ms = now;

        char ts_payload[128];
        snprintf(ts_payload, sizeof(ts_payload),
                 "field1=%.3f&field2=%d&field3=%.1f&status=OK",
                 current_vib, battery_pct, core_temp);

        if (g_mqtt_ts.publish(g_ts_topic, ts_payload)) {
            g_ts_tx_count++;
        }
    }

    // 5. Render Local HMI
    updateDashboard(current_vib, battery_pct);

    delay(20);
}

```

---

## 7. Troubleshooting & Network Diagnostic Matrix

| Error Signature | Underlying Failure | Corrective Protocol |
| --- | --- | --- |
| **`rc = -2` (`MQTT_CONNECT_FAILED`)** | Broker host unreachable, bad port, or Wi-Fi offline. | Confirm Wi-Fi status is `WL_CONNECTED`. Check for typos in `io.adafruit.com` or `mqtt3.thingspeak.com`. Ensure outbound port 1883 is unblocked. |
| **`rc = 4` (`MQTT_CONNECT_BAD_CREDENTIALS`)** | Username or Secret Key mismatch. | On Adafruit IO, check **My Key** for the active key (do not use your web login password). On ThingSpeak, verify credentials were created under **Devices** $\rightarrow$ **MQTT Devices**.

 |
| **`rc = 5` (`MQTT_CONNECT_UNAUTHORIZED`)** | Client ID collision or unauthorized channel access. | In ThingSpeak, confirm the **Client ID** matches the generated device token exactly. For Adafruit IO, ensure the client ID is unique on the network.

 |
| **Commands sent from cloud dashboard do not trigger callback** | Topic mismatch or missing `g_mqtt.loop()` in `loop()`. | Verify the subscribed topic string character-for-character (`{username}/feeds/feed-name`). Ensure `g_mqtt.loop()` runs without blocking delays.

 |
| **Incoming messages truncated or dropped** | Payload exceeds the default `PubSubClient` buffer size. | Set `build_flags = -DMQTT_MAX_PACKET_SIZE=512` in `platformio.ini` or invoke `client.setBufferSize(512)` in `setup()`. |
| **ThingSpeak drops updates or registers gaps** | Breached the 15-second channel rate limit. | Set `ThingSpeakMqttConfig::PUB_INTERVAL_MS` to $\ge 16000\text{ ms}$.

 |

---

## 8. Hands-On Verification Lab

Complete the following validation tasks:

* [ ] **Task 7.1:** Flash Exercise 7.1. Open your **Adafruit IO Dashboard** and observe the live gauge widget updating every 2 seconds as you move the M5Stack Stick S3.
* [ ] **Task 7.2:** Click the **Momentary Push Button** (`BEEP`) on your Adafruit IO Dashboard. Confirm the physical device sounds its buzzer within 100 ms of the click.
* [ ] **Task 7.3:** Toggle the **Switch Widget** on Adafruit IO to `ALARM_ON`. Verify that the M5Stick S3 screen turns red immediately. Toggle back to `ALARM_OFF` and verify the screen resets to green.
* [ ] **Task 7.4:** Flash Exercise 7.3. Let the gateway run for 2 minutes. Open your **ThingSpeak Channel** and confirm that numerical telemetry arrives on a strict 16-second cadence, while the Adafruit IO dashboard maintains live sub-second updates.



---

## 9. Version Control Protocol

Commit your complete enterprise MQTT firmware:

```bash
# Verify credentials in include/secrets.h remain uncommitted
git status

# Stage PlatformIO configuration, firmware, and secrets template
git add platformio.ini src/main.cpp include/secrets.example.h .gitignore

# Commit changes
git commit -m "feat(lesson07): implement enterprise bi-directional mqtt gateway with adafruit io and thingspeak"

```
