# Lesson 07: Enterprise MQTT Protocol — Industrial Pub/Sub Messaging on M5Stack Stick S3

While HTTP REST operates on a request-response model that incurs high packet overhead and latency, **MQTT (Message Queuing Telemetry Transport)** is the industrial messaging standard for distributed IoT fleets.

In this lesson, you will connect the **M5Stack Stick S3** to an enterprise MQTT broker using the **Publish/Subscribe (Pub/Sub)** pattern. You will stream real-time sensor telemetry with lightweight 2-byte headers and implement a bi-directional command subscriber that allows remote operators to control the device's screen and buzzer from any web browser worldwide.

```text
                     MQTT INDUSTRIAL PUB/SUB ARCHITECTURE
                     
  +-------------------------------------------------------------------------+
  | EDGE NODE: M5Stack Stick S3                                             |
  |  - Publisher: Transmits IMU & Battery telemetry to Cloud                |
  |  - Subscriber: Listens for incoming operational control commands        |
  +-------------------------------------------------------------------------+
                    |                                       ^
     PUBLISH        |                                       | SUBSCRIBE
     Topic:         | Port 1883 (TCP)                       | Topic:
     `.../telemetry`|                                       | `.../command`
                    v                                       |
  +-------------------------------------------------------------------------+
  | CENTRAL MESSAGE BROKER: HiveMQ Cloud / EMQX                             |
  |  - Decouples edge hardware from enterprise software clients             |
  |  - Routes messages instantly by topic match (<20ms latency)             |
  +-------------------------------------------------------------------------+
                    |                                       ^
     SUBSCRIBE      |                                       | PUBLISH
     Topic:         | WebSockets (Port 8000 / 8884)         | Topic:
     `.../telemetry`|                                       | `.../command`
                    v                                       |
  +-------------------------------------------------------------------------+
  | OPERATIONS CONTROL: HiveMQ Web Client / Corporate Dashboard            |
  |  - Displays live streaming vibration & state data                       |
  |  - Dispatches remote actuator commands: `{"buzzer":"ON"}`               |
  +-------------------------------------------------------------------------+

```

---

## 1. Executive Business Comparison: HTTP REST vs. MQTT Pub/Sub

Choosing the correct messaging protocol determines the battery life, cellular bandwidth costs, and scaling limits of an enterprise IoT deployment:

| Architectural Metric | HTTP REST (Lessons 05 & 06) | MQTT Pub/Sub (Lesson 07) |
| --- | --- | --- |
| **Communication Model** | Client-Server (Pull / Polling) | Publish-Subscribe (Push / Event-Driven) |
| **Packet Overhead** | 200–800 bytes per request (HTML headers) | **2 bytes** fixed header |
| **Connection Lifecycle** | Ephemeral (Open TCP $\rightarrow$ Send $\rightarrow$ Tear down) | **Persistent** low-power TCP connection with keep-alive |
| **Network Latency** | 200 ms to 1500 ms | **< 20 ms** real-time delivery |
| **Bi-Directional Control** | Requires complex firewall punching / polling | **Native**: Broker pushes commands directly to device |
| **Cellular / Data Cost** | High data consumption; costly at fleet scale | **Over 80% bandwidth reduction** vs. HTTP |
| **Primary Business Role** | Infrequent form logs, batch database sync | Mission-critical telemetry, live remote operations |

---

## 2. Topic Taxonomies & Quality of Service (QoS)

MQTT routes messages based on **Topics** formatted like file system paths. This allows corporate subscribers to filter telemetry across complex asset hierarchies:

```text
  Enterprise Fleet Topic Taxonomy:
  
  mba/iot/fleet / {device_id} / {channel}
        │              │             │
        │              │             └─ Channel (`telemetry` vs `command`)
        │              └─ Hardware eFuse Identifier (e.g., `S3_7C02`)
        └─ Organizational Domain & Root Application Namespace

```

### Quality of Service (QoS) Levels for Business Decisions

* **QoS 0 (At most once):** "Fire and forget." Lowest latency, minimal bandwidth. A dropped packet is ignored. Best for high-frequency vibration streams where the next reading arrives in milliseconds.
* **QoS 1 (At least once):** Packet delivery is guaranteed via a server acknowledgment (`PUBACK`). Packets may duplicate if network flutters. Best for state changes, fault logs, and operator audits.
* **QoS 2 (Exactly once):** Strict four-step handshake guaranteeing single-instance delivery. High network overhead. Reserved for financial billing meters or mission-critical safety shutdowns.

---

## 3. Project Directory & `platformio.ini` Setup

We use the standard **`PubSubClient`** library alongside **`M5Unified`**:

```text
lesson07-mqtt-protocol/
├── .gitignore
├── platformio.ini
├── include/
│   ├── secrets.example.h
│   └── secrets.h
└── src/
    └── main.cpp

```

Initialize your workspace:

```bash
mkdir -p lesson07-mqtt-protocol/include lesson07-mqtt-protocol/src
cd lesson07-mqtt-protocol

```

### `platformio.ini`

Create `platformio.ini` in the project root:

```ini
; =====================================================================
; PlatformIO Configuration: Lesson 07 - Enterprise MQTT Protocol
; Board: M5Stack Stick S3 (Espressif ESP32-S3)
; =====================================================================

[env:m5stack-s3]
platform = espressif32 @ ^6.5.0
board = esp32-s3-devkitc-1
framework = arduino

monitor_speed = 115200
upload_speed = 921600

build_flags = 
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1

lib_deps = 
    m5stack/M5Unified @ ^0.1.16
    knolleary/PubSubClient @ ^2.8

```

---

## 4. Credential & Broker Configuration (`include/secrets.h`)

We use the public **HiveMQ** broker (`broker.hivemq.com`), which requires zero sign-up or credit card registration, enabling instant validation of MQTT messaging over standard port 1883.

Create `include/secrets.example.h`:

```cpp
#pragma once

namespace NetworkConfig {
    const char* WIFI_SSID     = "YOUR_2.4GHZ_WIFI_SSID";
    const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
}

namespace MqttConfig {
    const char* BROKER_HOST    = "broker.hivemq.com";
    const uint16_t BROKER_PORT = 1883;
    const char* ROOT_TOPIC     = "mba/iot/fleet";
    const unsigned long PUB_INTERVAL_MS = 2000; // Stream telemetry every 2 seconds
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

## 5. Firmware Implementation: Bi-Directional MQTT Node

Create `src/main.cpp`. This single-file firmware demonstrates a complete bi-directional IoT edge architecture:

1. **Publisher:** Streams physical IMU acceleration magnitude, battery state, and core temperature to `mba/iot/fleet/S3_XXXX/telemetry` every 2 seconds.
2. **Subscriber:** Subscribes to `mba/iot/fleet/S3_XXXX/command` and listens for remote payload instructions:
* Payload `"BEEP"`: Triggers the physical onboard buzzer.
* Payload `"RED"`: Changes the LCD status card to red.
* Payload `"GREEN"`: Resets the LCD status card to normal green.


3. **Local HMI:** Renders real-time telemetry, connection state, and the latest remote command directly on the ST7789 display.

```cpp
/**
 * ============================================================================
 * Project: M5Stack Stick S3 Journey - Lesson 07
 * Description: Bi-Directional Industrial MQTT Messaging (Pub/Sub)
 * Target Audience: MBA Business Analytics & IoT Leadership
 * ============================================================================
 */

#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <math.h>
#include "secrets.h"

// Hardware Identity & Dynamic Topic Buffers
static char g_device_id[16];
static char g_topic_telemetry[64];
static char g_topic_command[64];

// Networking & MQTT Primitives
static WiFiClient g_tcp_client;
static PubSubClient g_mqtt(g_tcp_client);

// Operational Telemetry Counters
static unsigned long g_last_publish_ms = 0;
static uint32_t g_packets_published = 0;
static uint32_t g_commands_received = 0;
static String g_last_command_str = "NONE";
static uint16_t g_card_color = TFT_DARKGREEN;

// ----------------------------------------------------------------------------
// 1. MQTT INCOMING COMMAND CALLBACK (SUBSCRIBER)
// ----------------------------------------------------------------------------
void mqttMessageCallback(char* topic, byte* payload, unsigned int length) {
    char message_buffer[length + 1];
    memcpy(message_buffer, payload, length);
    message_buffer[length] = '\0';

    String cmd = String(message_buffer);
    cmd.trim();
    cmd.toUpperCase();

    g_commands_received++;
    g_last_command_str = cmd;

    Serial.printf("\n[MQTT RX] Received command on topic [%s]: %s\n", topic, cmd.c_str());

    // Execute edge actions based on incoming cloud command
    if (cmd == "BEEP") {
        M5.Speaker.tone(2000, 150);
    } else if (cmd == "RED") {
        g_card_color = TFT_RED;
        M5.Speaker.tone(800, 100);
    } else if (cmd == "GREEN") {
        g_card_color = TFT_DARKGREEN;
        M5.Speaker.tone(1200, 50);
    } else {
        Serial.println("[MQTT RX] Unrecognized command parameter.");
    }
}

// ----------------------------------------------------------------------------
// 2. CONNECTION LIFECYCLE MANAGEMENT
// ----------------------------------------------------------------------------
void ensureMqttConnection() {
    while (!g_mqtt.connected()) {
        Serial.printf("[MQTT] Attempting broker connection to %s:%d...\n", 
                      MqttConfig::BROKER_HOST, MqttConfig::BROKER_PORT);

        if (g_mqtt.connect(g_device_id)) {
            Serial.println("[MQTT] Broker connection established successfully.");

            // Subscribe to remote supervisory command channel
            g_mqtt.subscribe(g_topic_command);
            Serial.printf("[MQTT] Subscribed to topic: %s\n", g_topic_command);

            M5.Speaker.tone(2400, 80);
        } else {
            Serial.printf("[MQTT] Connection failed, rc=%d. Retrying in 2 seconds...\n", g_mqtt.state());
            delay(2000);
        }
    }
}

// ----------------------------------------------------------------------------
// 3. GRAPHICAL USER INTERFACE
// ----------------------------------------------------------------------------
void drawBaseUI() {
    M5.Display.fillScreen(TFT_BLACK);

    // Top Navigation Ribbon
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextSize(1);
    M5.Display.drawString(" MQTT INDUSTRIAL PUB/SUB", 5, 10);

    // Panel Dividers
    M5.Display.drawFastHLine(0, 54, 240, TFT_DARKGRAY);
    M5.Display.drawFastHLine(0, 114, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 20, 34, TFT_DARKGRAY);

    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VIB (MAGNITUDE)", 60, 23);
    M5.Display.drawString("BATTERY", 180, 23);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Force Telemetry Publish", 120, 124);
}

// ----------------------------------------------------------------------------
// 4. MAIN FIRMWARE SETUP & LOOP
// ----------------------------------------------------------------------------
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    drawBaseUI();

    Serial.begin(115200);
    delay(2000);

    // Derive unique hardware MAC address
    uint64_t mac = ESP.getEfuseMac();
    snprintf(g_device_id, sizeof(g_device_id), "S3_%04X", (uint16_t)mac);

    // Build unique topic paths
    snprintf(g_topic_telemetry, sizeof(g_topic_telemetry), "%s/%s/telemetry", MqttConfig::ROOT_TOPIC, g_device_id);
    snprintf(g_topic_command,   sizeof(g_topic_command),   "%s/%s/command",   MqttConfig::ROOT_TOPIC, g_device_id);

    // Initialize 2.4 GHz Wi-Fi radio
    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Connecting Wi-Fi...", 120, 80);

    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
    }
    Serial.println("\n[WIFI] Connected.");

    // Configure MQTT Broker Engine
    g_mqtt.setServer(MqttConfig::BROKER_HOST, MqttConfig::BROKER_PORT);
    g_mqtt.setCallback(mqttMessageCallback);

    ensureMqttConnection();
    drawBaseUI();
}

void loop() {
    M5.update();

    if (!g_mqtt.connected()) {
        ensureMqttConnection();
    }
    g_mqtt.loop();

    // 1. Acquire Physical Sensory Metrics
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float vibration_g = sqrtf((ax * ax) + (ay * ay) + (az * az));
    int battery_pct = M5.Power.getBatteryLevel();
    float core_temp = temperatureRead();

    // 2. Render Real-Time Numbers on Display
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString(String(vibration_g, 2) + " g ", 60, 42);

    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(String(battery_pct) + " % ", 180, 42);

    // 3. Render Status Card Reflecting Cloud Commands
    M5.Display.fillRect(0, 55, 240, 58, g_card_color);
    M5.Display.setTextColor(TFT_WHITE, g_card_color);
    M5.Display.setTextSize(2);
    M5.Display.drawString("CMD: " + g_last_command_str, 120, 74);
    M5.Display.setTextSize(1);
    M5.Display.printf("Published: %u | Received: %u", g_packets_published, g_commands_received);

    // Top Right Status Ribbon Indicator
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.printf("MQTT LIVE | %d dBm ", WiFi.RSSI());

    // 4. Publish Telemetry Stream to Broker (Timer or Button A)
    bool force_button_pub = M5.BtnA.wasClicked();
    unsigned long current_ms = millis();

    if (force_button_pub || (current_ms - g_last_publish_ms >= MqttConfig::PUB_INTERVAL_MS)) {
        g_last_publish_ms = current_ms;

        // Construct lightweight JSON payload (<120 bytes)
        char json_payload[128];
        snprintf(json_payload, sizeof(json_payload),
                 "{\"device\":\"%s\",\"vib\":%.3f,\"bat\":%d,\"temp\":%.1f,\"btn\":%d}",
                 g_device_id,
                 vibration_g,
                 battery_pct,
                 core_temp,
                 force_button_pub ? 1 : 0);

        if (g_mqtt.publish(g_topic_telemetry, json_payload)) {
            g_packets_published++;
            Serial.printf("[MQTT TX] Sent -> %s\n", json_payload);
            if (force_button_pub) M5.Speaker.tone(1800, 40);
        } else {
            Serial.println("[MQTT TX] Publish failed.");
        }
    }

    delay(50);
}

```

---

## 6. Real-Time Remote Testing via HiveMQ Web Client

Validate bi-directional communication using your browser without writing desktop code:

1. Open the [HiveMQ WebSocket Client](http://www.hivemq.com/demos/websocket-client/).
2. Click **Connect** (Host: `broker.hivemq.com`, Port: `8000`).
3. **Step 1: Subscribe to Telemetry:**
* In the **Subscriptions** panel, click **Add New Topic Subscription**.
* **Topic:** `mba/iot/fleet/+/telemetry` (The wildcard `+` listens to all fleet nodes).
* Click **Subscribe**.
* Observe live JSON packets streaming in every 2 seconds from your M5Stick S3.


4. **Step 2: Dispatch Commands to the Hardware:**
* In the **Publish** panel:
* **Topic:** `mba/iot/fleet/YOUR_DEVICE_ID/command` (e.g., `mba/iot/fleet/S3_7C02/command` — check the ID printed on your device's screen or serial console).
* **Message:** `BEEP`
* Click **Publish**. Confirm that the physical device sounds its buzzer immediately.
* Publish `RED` to turn the screen card red; publish `GREEN` to turn it green.



---

## 7. Version Control Protocol

Commit your complete MQTT messaging firmware:

```bash
git add platformio.ini src/main.cpp include/secrets.example.h .gitignore
git commit -m "feat(lesson07): implement bi-directional industrial mqtt pub/sub messaging on m5stick s3"

```
