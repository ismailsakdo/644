# Lesson 06: Private Cloud Database Integration — AwardSpace Hosting, MySQL & PHP Middleware Ingestion

This module transitions from third-party public cloud platforms to a **private, enterprise-owned cloud backend**. You will provision a remote **relational MySQL database** on **AwardSpace**, engineer a secure server-side **PHP ingestion API** using prepared SQL statements, validate the backend headlessly, and program the **M5Stack Stick S3** to stream real-time **BMI270 IMU** and **AXP2101 power telemetry** directly into your own SQL database table.

```text
                     PRIVATE CLOUD DATABASE ARCHITECTURE
                     
  +------------------------------------------------------------------------+
  | EDGE HARDWARE TIER: M5Stack Stick S3                                    |
  |  - Sensors: BMI270 IMU (Vibration Vector), AXP2101 PMIC (Battery Level)|
  |  - Hardware ID: Unique Silicon eFuse MAC Address                       |
  |  - Network: 2.4 GHz 802.11 b/g/n Wi-Fi Client                          |
  +------------------------------------------------------------------------+
                                      |
                     HTTP POST (Port 80 / Port 443)
                     Payload: `api_key=...&device_id=...&vibration=...`
                                      |
                                      v
  +------------------------------------------------------------------------+
  | APPLICATION SERVER TIER: AwardSpace Web Host                           |
  |  - Apache Web Server                                                   |
  |  - PHP REST Ingestion Endpoint (`post_data.php`)                       |
  |    * Validates Shared Secret API Key (Authentication)                  |
  |    * Sanitizes Data Payloads                                           |
  |    * Executes Parameterized Prepared Statements (Prevents SQLi)        |
  +------------------------------------------------------------------------+
                                      |
                               TCP / IP Socket
                                      |
                                      v
  +------------------------------------------------------------------------+
  | DATA PERSISTENCE TIER: MySQL Relational Database                       |
  |  - Host: `fdbXX.awardspace.net`                                        |
  |  - Database: `iot_fleet_db` | Table: `sensor_logs`                     |
  |  - Managed via phpMyAdmin & Custom Query Interfaces                    |
  +------------------------------------------------------------------------+

```

---

## 1. Enterprise Context: Public PaaS vs. Private Relational Backends

While ThingSpeak and Google Forms provide rapid prototyping, enterprise IoT deployments frequently require dedicated SQL backends for regulatory compliance, data ownership, and analytical flexibility (ADE644 Lesson 3 & 4):

| Architectural Dimension | Public IoT Platforms (e.g., ThingSpeak) | Private Relational Backends (MySQL / MariaDB) |
| --- | --- | --- |
| **Data Sovereignty & Governance** | Data resides on multi-tenant third-party infrastructure. | Full ownership of storage schema, retention, and access policies.

 |
| **Query Flexibility** | Limited to single-channel time-series indices. | Complex relational `JOIN` operations, aggregations (`AVG`, `MAX`), and indexing. |
| **Ingestion Constraints** | Strict rate limits (e.g., 15s throttles on free tiers). | Governed only by server bandwidth and database connection pooling. |
| **Integration Capabilities** | Bound to vendor dashboards and proprietary webhooks. | Native integration with ERPs, Power BI, Grafana, Node.js, and Python ML pipelines.

 |
| **Cost at Scale** | Tiered subscription pricing per message/channel. | Low-cost virtual private server (VPS) or shared hosting hosting millions of rows. |

---

## 2. AwardSpace Provisioning & MySQL Database Creation

[AwardSpace](https://www.awardspace.com/) provides a free LAMP (Linux, Apache, MySQL, PHP) hosting environment suitable for prototyping private IoT endpoints.

### Step 2.1: Database Instance Setup

1. Log in to your **AwardSpace Control Panel**.
2. Navigate to **Database Manager** $\rightarrow$ **MySQL Databases**.
3. Create a new database:
* **Database Name:** e.g., `3849102_iot` (AwardSpace automatically prepends your account ID).
* **Database User:** e.g., `3849102_iot`.
* **Password:** Generate a strong, random password.


4. Note your connection details:
* **Database Host:** (e.g., `fdb1030.awardspace.net` — **Do not assume `localhost**`; check the exact host displayed on your AwardSpace dashboard).
* **Database Name:** (Full prefixed string).
* **Database User:** (Full prefixed string).
* **Database Password:** (Your chosen password).



### Step 2.2: Schema Initialization via phpMyAdmin

1. In the **Database Manager**, click the **phpMyAdmin** icon next to your new database.
2. Select your database from the left navigation panel.
3. Open the **SQL** query tab and run the following Data Definition Language (DDL) command:

```sql
CREATE TABLE `sensor_logs` (
    `id` INT(11) UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    `device_id` VARCHAR(32) NOT NULL,
    `vibration_g` FLOAT(6,3) NOT NULL,
    `battery_pct` TINYINT(3) UNSIGNED NOT NULL,
    `temperature_c` FLOAT(4,1) NOT NULL,
    `system_state` VARCHAR(20) NOT NULL DEFAULT 'NOMINAL',
    `reading_timestamp` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX `idx_device_time` (`device_id`, `reading_timestamp`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

```

#### Field Schema Explanation:

* `id`: Auto-incrementing primary key uniquely identifying every ingested telemetry frame.
* `device_id`: Alpha-numeric hardware identifier (e.g., based on the ESP32-S3 eFuse MAC).
* `vibration_g`: Acceleration magnitude stored as a single-precision floating-point value.
* `battery_pct`: Battery percentage stored as an unsigned tiny integer (`0` to `100`).
* `temperature_c`: Internal die temperature from the ESP32-S3 sensor.
* `system_state`: Operating condition flag (`NOMINAL`, `WARN_VIB`, `CRITICAL`).
* `reading_timestamp`: Automatic server-side UTC timestamp generated upon `INSERT`.
* `INDEX (device_id, reading_timestamp)`: Composite database index to accelerate time-series queries.

---

## 3. Server-Side Middleware: PHP Ingestion API

To protect the database, the M5Stick S3 does not connect to port 3306 directly. Instead, it dispatches an HTTP POST request to an intermediary PHP script (`post_data.php`). This architecture isolates database credentials from edge devices and prevents direct database exposure to the internet.

```text
                  API SECURITY & SANITIZATION PIPELINE
                  
  HTTP POST Payload
  +-------------------------------------------------------------+
  | api_key=SECRET_TOKEN & device_id=M5S3_01 & vibration=1.04 ...|
  +-------------------------------------------------------------+
                               |
                               v
  [STEP 1: AUTHENTICATION CHECK]
  Does $_POST['api_key'] match$PRE_SHARED_SECRET?
    NO  --> HTTP 403 Forbidden (Halt execution)
    YES --> Proceed
                               |
                               v
  [STEP 2: PAYLOAD EXTRACTION & SANITIZATION]
  Cast numerical inputs: `floatval()`, `intval()`
  Strip dangerous characters: `trim()`, `htmlspecialchars()`
                               |
                               v
  [STEP 3: PARAMETERIZED PREPARED STATEMENT]
  $stmt =$conn->prepare("INSERT INTO sensor_logs (...) VALUES (?, ?, ?, ?, ?)");
  $stmt->bind_param("sddss", ...);
  $stmt->execute();
                               |
                               v
  [STEP 4: RESPONSE DISPATCH]
  Return HTTP 200 OK with JSON: {"status":"success","record_id":142}

```

### Step 3.1: Deploying `post_data.php`

1. In the AwardSpace Control Panel, open **File Manager**.
2. Navigate to your root web directory (usually `/www/[yourdomain.atwebpages.com/](https://yourdomain.atwebpages.com/)` or `/public_html/`).
3. Create a new file named `post_data.php` and paste the following script:

```php
<?php
/**
 * ============================================================================
 * File: post_data.php
 * Description: Intermediary Ingestion API for M5Stack Stick S3 Telemetry
 * Security: Pre-Shared Key Authentication + Parameterized Prepared Statements
 * ============================================================================
 */

// Strict Response Header
header("Content-Type: application/json; charset=UTF-8");

// -----------------------------------------------------------------------------
// 1. CONFIGURATION: Update with your AwardSpace DB Credentials
// -----------------------------------------------------------------------------
define("DB_HOST", "fdb1030.awardspace.net"); // Replace with your exact DB Host
define("DB_USER", "YOUR_DB_USER");          // Replace with your DB Username
define("DB_PASS", "YOUR_DB_PASSWORD");      // Replace with your DB Password
define("DB_NAME", "YOUR_DB_NAME");          // Replace with your DB Name

// Shared Secret Token (Must match the key configured in the M5Stick S3 firmware)
define("API_SECRET_KEY", "MBA_IOT_SECURE_TOKEN_2026");

// -----------------------------------------------------------------------------
// 2. REQUEST METHOD & AUTHENTICATION VALIDATION
// -----------------------------------------------------------------------------
if ($_SERVER["REQUEST_METHOD"] !== "POST") {
    http_response_code(405); // Method Not Allowed
    echo json_encode(["status" => "error", "message" => "Only HTTP POST requests are accepted."]);
    exit();
}

$provided_key = $_POST["api_key"] ?? "";

if (!hash_equals(API_SECRET_KEY, $provided_key)) {
    http_response_code(403); // Forbidden
    echo json_encode(["status" => "error", "message" => "Unauthorized: Invalid or missing API key."]);
    exit();
}

// -----------------------------------------------------------------------------
// 3. INPUT PARSING & SANITIZATION
// -----------------------------------------------------------------------------
$device_id     = isset($_POST["device_id"]) ? substr(trim($_POST["device_id"]), 0, 32) : "UNKNOWN_NODE";
$vibration_g   = isset($_POST["vibration_g"]) ? floatval($_POST["vibration_g"]) : 0.0;
$battery_pct   = isset($_POST["battery_pct"]) ? intval($_POST["battery_pct"]) : 0;
$temperature_c = isset($_POST["temperature_c"]) ? floatval($_POST["temperature_c"]) : 0.0;
$system_state  = isset($_POST["system_state"]) ? substr(trim($_POST["system_state"]), 0, 20) : "NOMINAL";

// Enforce boundary constraints
$battery_pct = max(0, min(100, $battery_pct));

// -----------------------------------------------------------------------------
// 4. DATABASE PERSISTENCE (PREPARED STATEMENTS)
// -----------------------------------------------------------------------------
$conn = new mysqli(DB_HOST, DB_USER, DB_PASS, DB_NAME);

if ($conn->connect_error) {
    http_response_code(500); // Internal Server Error
    echo json_encode(["status" => "error", "message" => "Database connection failed: " . $conn->connect_error]);
    exit();
}

// Prepare parameterized query (Mitigates SQL Injection completely)
$sql = "INSERT INTO sensor_logs (device_id, vibration_g, battery_pct, temperature_c, system_state) 
        VALUES (?, ?, ?, ?, ?)";

$stmt = $conn->prepare($sql);

if (!$stmt) {
    http_response_code(500);
    echo json_encode(["status" => "error", "message" => "Statement preparation failed: " . $conn->error]);
    $conn->close();
    exit();
}

// "sdids" -> String, Double (Float), Integer, Double (Float), String
$stmt->bind_param("sdids", $device_id, $vibration_g, $battery_pct, $temperature_c, $system_state);

if ($stmt->execute()) {
    $inserted_id = $stmt->insert_id;
    http_response_code(200);
    echo json_encode([
        "status"    => "success",
        "record_id" => $inserted_id,
        "message"   => "Telemetry successfully persisted."
    ]);
} else {
    http_response_code(500);
    echo json_encode(["status" => "error", "message" => "Execution failed: " . $stmt->error]);
}

// -----------------------------------------------------------------------------
// 5. RESOURCE CLEANUP
// -----------------------------------------------------------------------------
$stmt->close();
$conn->close();
?>

```

### Step 3.2: Deploying a Web Data Viewer (`view_data.php`)

To inspect your stored telemetry directly in a web browser without opening phpMyAdmin every time, create a second file named `view_data.php` in the same directory:

```php
<?php
/**
 * ============================================================================
 * File: view_data.php
 * Description: Executive Web Dashboard for Stored Sensor Telemetry
 * ============================================================================
 */
define("DB_HOST", "fdb1030.awardspace.net");
define("DB_USER", "YOUR_DB_USER");
define("DB_PASS", "YOUR_DB_PASSWORD");
define("DB_NAME", "YOUR_DB_NAME");

$conn = new mysqli(DB_HOST, DB_USER, DB_PASS, DB_NAME);
if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
}

$sql = "SELECT id, device_id, vibration_g, battery_pct, temperature_c, system_state, reading_timestamp 
        FROM sensor_logs ORDER BY id DESC LIMIT 50";
$result = $conn->query($sql);
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta http-equiv="refresh" content="10">
    <title>M5Stick S3 Telemetry Lake</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; margin: 20px; background: #0d1117; color: #c9d1d9; }
        h1 { color: #58a6ff; font-size: 22px; }
        .meta { font-size: 13px; color: #8b949e; margin-bottom: 15px; }
        table { border-collapse: collapse; width: 100%; max-width: 950px; background: #161b22; border-radius: 6px; overflow: hidden; }
        th, td { padding: 10px 14px; text-align: left; border-bottom: 1px solid #30363d; font-size: 14px; }
        th { background: #21262d; color: #f0f6fc; }
        tr:hover { background: #1f242c; }
        .badge { padding: 3px 8px; border-radius: 12px; font-weight: bold; font-size: 11px; }
        .badge-nominal { background: #238636; color: white; }
        .badge-warn { background: #d29922; color: black; }
        .badge-critical { background: #da3633; color: white; }
    </style>
</head>
<body>
    <h1>M5Stick S3 Enterprise IoT Database</h1>
    <div class="meta">Connected Database: <strong><?= htmlspecialchars(DB_NAME) ?></strong> | Auto-refreshes every 10 seconds</div>
    <table>
        <thead>
            <tr>
                <th>ID</th>
                <th>Timestamp (UTC)</th>
                <th>Device UUID</th>
                <th>Vibration (g)</th>
                <th>Battery</th>
                <th>Core Temp</th>
                <th>Status</th>
            </tr>
        </thead>
        <tbody>
        <?php if ($result && $result->num_rows > 0): ?>
            <?php while($row = $result->fetch_assoc()): ?>
                <?php
                    $state_class = "badge-nominal";
                    if ($row["system_state"] === "WARN_VIB") $state_class = "badge-warn";
                    if ($row["system_state"] === "CRITICAL") $state_class = "badge-critical";
                ?>
                <tr>
                    <td>#<?= $row["id"] ?></td>
                    <td><?= $row["reading_timestamp"] ?></td>
                    <td><code><?= htmlspecialchars($row["device_id"]) ?></code></td>
                    <td><strong><?= number_format($row["vibration_g"], 3) ?> g</strong></td>
                    <td><?= $row["battery_pct"] ?>%</td>
                    <td><?= number_format($row["temperature_c"], 1) ?> &deg;C</td>
                    <td><span class="badge <?= $state_class ?>"><?= htmlspecialchars($row["system_state"]) ?></span></td>
                </tr>
            <?php endwhile; ?>
        <?php else: ?>
            <tr><td colspan="7" style="text-align:center; padding: 20px;">No telemetry records found.</td></tr>
        <?php endif; ?>
        </tbody>
    </table>
</body>
</html>
<?php $conn->close(); ?>

```

---

## 4. Pre-Hardware API Validation via Python Test Harness

Before programming the microcontroller, verify that `post_data.php` correctly authenticates payloads, handles errors, and inserts rows into MySQL.

Create a verification script on your computer: `tools/test_database_api.py`.

```python
#!/usr/bin/env python3
"""
M5Stack Stick S3 Journey - Lesson 06
Headless Database API Ingestion Verification Harness
"""

import time
import requests

# =====================================================================
# CONFIGURATION: Update with your AwardSpace endpoint URL
# =====================================================================
API_URL = "http://yourdomain.atwebpages.com/post_data.php"
VALID_API_KEY = "MBA_IOT_SECURE_TOKEN_2026"
# =====================================================================

def test_unauthorized_access():
    """Verify that requests with invalid API keys are rejected with HTTP 403."""
    print("[TEST 1] Testing authentication barrier with invalid key...")
    payload = {
        "api_key": "WRONG_KEY_12345",
        "device_id": "TEST_HACKER_NODE"
    }
    response = requests.post(API_URL, data=payload)
    print(f" -> HTTP Status: {response.status_code}")
    print(f" -> Server Reply: {response.text}")
    assert response.status_code == 403, f"Expected 403 Forbidden, got {response.status_code}"
    print(" [PASS] Unauthorized access blocked successfully.\n")

def test_valid_telemetry_post():
    """Verify that valid structured payloads insert rows and return HTTP 200."""
    print("[TEST 2] Testing valid telemetry injection...")
    payload = {
        "api_key": VALID_API_KEY,
        "device_id": "M5S3_EMULATOR_MAC_7C02",
        "vibration_g": 1.145,
        "battery_pct": 92,
        "temperature_c": 38.4,
        "system_state": "NOMINAL"
    }
    start_time = time.time()
    response = requests.post(API_URL, data=payload)
    latency_ms = round((time.time() - start_time) * 1000, 1)

    print(f" -> HTTP Status: {response.status_code} (Latency: {latency_ms} ms)")
    print(f" -> Server Reply: {response.text}")
    assert response.status_code == 200, f"Expected 200 OK, got {response.status_code}"
    
    json_data = response.json()
    assert json_data.get("status") == "success"
    print(f" [PASS] Database inserted Record ID: {json_data.get('record_id')}\n")

def main():
    print("=" * 65)
    print("  AWARDSPACE MYSQL INGESTION API VERIFICATION")
    print(f"  Target: {API_URL}")
    print("=" * 65 + "\n")

    test_unauthorized_access()
    test_valid_telemetry_post()

    print("=" * 65)
    print("API verified successfully. Open view_data.php in your browser.")
    print("=" * 65)

if __name__ == "__main__":
    main()

```

### Run the Script:

```bash
python3 tools/test_database_api.py

```

---

## 5. M5Stack Stick S3 Firmware Implementation

Now that the backend is validated, program the **M5Stack Stick S3** to poll the onboard **BMI270 IMU** and **AXP2101 PMIC**, evaluate thresholds, and post the data directly to your AwardSpace database.

### 5.1 Project Layout & `platformio.ini`

```text
lesson06-database-awardspaces/
├── .gitignore
├── platformio.ini
├── include/
│   ├── secrets.example.h
│   └── secrets.h
└── src/
    └── main.cpp

```

Initialize the project:

```bash
mkdir -p lesson06-database-awardspaces/include lesson06-database-awardspaces/src
cd lesson06-database-awardspaces

```

`platformio.ini`:

```ini
; =====================================================================
; PlatformIO Configuration: Lesson 06 - AwardSpace Database Ingestion
; Board: M5Stack Stick S3 (ESP32-S3)
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

```

### 5.2 Header Configuration (`include/secrets.h`)

Create `include/secrets.h`:

```cpp
#pragma once

namespace NetworkConfig {
    const char* WIFI_SSID     = "YOUR_2.4GHZ_WIFI_SSID";
    const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
}

namespace DatabaseConfig {
    // URL to your deployed post_data.php script
    const char* INGEST_URL    = "http://yourdomain.atwebpages.com/post_data.php";
    const char* API_KEY       = "MBA_IOT_SECURE_TOKEN_2026";
    const unsigned long INGEST_INTERVAL_MS = 10000; // Ingest every 10 seconds
}

```

### 5.3 Firmware Implementation (`src/main.cpp`)

Create `src/main.cpp`:

```cpp
/**
 * ============================================================================
 * Project: M5Stack Stick S3 Journey - Lesson 06
 * Description: Real-Time Telemetry Streaming to Private AwardSpace MySQL DB
 * Peripherals: BMI270 IMU, ST7789 TFT LCD, AXP2101 PMIC, Hardware Buttons
 * ============================================================================
 */

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>
#include "secrets.h"

// Runtime Telemetry & Database Transaction Variables
static unsigned long g_last_ingest_ms = 0;
static uint32_t g_last_record_id = 0;
static int g_last_http_status = 0;
static uint32_t g_total_records_sent = 0;

// Draw Base User Interface
void drawDashboard() {
    M5.Display.fillScreen(TFT_BLACK);

    // Top Status Ribbon
    M5.Display.fillRect(0, 0, 240, 22, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextSize(1);
    M5.Display.drawString(" MYSQL CLOUD INGRESS", 6, 11);

    // Section Dividers
    M5.Display.drawFastHLine(0, 56, 240, TFT_DARKGRAY);
    M5.Display.drawFastHLine(0, 112, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 22, 34, TFT_DARKGRAY);

    // Panel Labels
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VIBRATION", 60, 25);
    M5.Display.drawString("BATTERY", 180, 25);

    // Bottom Navigation Hint
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Force Immediate SQL Insert", 120, 123);
}

// Execute HTTP POST to AwardSpace Middleware
bool postTelemetryToDatabase(const String& device_id, float vibration_g, int battery_pct, float temp_c, const String& state) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[DATABASE] Aborting POST: Wi-Fi not connected.");
        return false;
    }

    HTTPClient http;
    http.begin(DatabaseConfig::INGEST_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Construct URL-encoded POST payload
    String payload = "";
    payload += "api_key="       + String(DatabaseConfig::API_KEY) + "&";
    payload += "device_id="     + device_id + "&";
    payload += "vibration_g="   + String(vibration_g, 3) + "&";
    payload += "battery_pct="   + String(battery_pct) + "&";
    payload += "temperature_c=" + String(temp_c, 1) + "&";
    payload += "system_state="  + state;

    Serial.printf("[DATABASE] Dispatching POST to: %s\n", DatabaseConfig::INGEST_URL);
    Serial.printf("[DATABASE] Payload: %s\n", payload.c_str());

    unsigned long start_time = millis();
    int http_code = http.POST(payload);
    unsigned long latency = millis() - start_time;
    g_last_http_status = http_code;

    bool success = false;

    if (http_code == 200) {
        String response_body = http.getString();
        Serial.printf("[DATABASE] Ingress Success (%lu ms): %s\n", latency, response_body.c_str());

        // Extract inserted record ID using simple substring parsing
        int id_pos = response_body.indexOf("\"record_id\":");
        if (id_pos != -1) {
            int end_pos = response_body.indexOf(",", id_pos);
            if (end_pos == -1) end_pos = response_body.indexOf("}", id_pos);
            String id_str = response_body.substring(id_pos + 12, end_pos);
            g_last_record_id = id_str.toInt();
        }

        g_total_records_sent++;
        success = true;
        M5.Speaker.tone(2200, 60);
    } else {
        Serial.printf("[DATABASE] Ingress Failed. HTTP Status: %d\n", http_code);
        M5.Speaker.tone(600, 200);
    }

    http.end();
    return success;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    drawDashboard();

    Serial.begin(115200);
    delay(2000);

    // Initialize 2.4 GHz Wi-Fi Radio
    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Connecting to Wi-Fi...", 120, 80);

    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
    }

    Serial.println("\n[WIFI] Connected.");
    drawDashboard();
}

void loop() {
    M5.update();

    // 1. Acquire Physical Sensor Telemetry
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    float vibration_g = sqrtf((ax * ax) + (ay * ay) + (az * az));
    int battery_pct = M5.Power.getBatteryLevel();
    float core_temp = temperatureRead();

    // Edge Anomaly Logic
    String system_state = "NOMINAL";
    if (vibration_g > 1.45f) {
        system_state = "CRITICAL";
    } else if (vibration_g > 1.25f) {
        system_state = "WARN_VIB";
    }

    // Hardware UUID from Silicon eFuse MAC
    uint64_t mac = ESP.getEfuseMac();
    char dev_uuid[18];
    snprintf(dev_uuid, sizeof(dev_uuid), "S3_%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);

    // 2. Render Real-Time Measurements
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(String(vibration_g, 2) + " g ", 60, 42);

    M5.Display.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    M5.Display.drawString(String(battery_pct) + " % ", 180, 42);
    M5.Display.setTextSize(1);

    // 3. Render Top Right Wi-Fi Strength Indicator
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.printf("%d dBm ", WiFi.RSSI());

    // 4. Transmission Triggers: Timer or Button A Click
    bool force_manual_insert = M5.BtnA.wasClicked();
    unsigned long current_ms = millis();

    if (force_manual_insert || (current_ms - g_last_ingest_ms >= DatabaseConfig::INGEST_INTERVAL_MS)) {
        g_last_ingest_ms = current_ms;

        // Visual Ingress Indicator
        M5.Display.fillRect(0, 58, 240, 53, TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setTextDatum(textdatum_t::middle_center);
        M5.Display.drawString("Streaming to MySQL...", 120, 72);

        bool success = postTelemetryToDatabase(String(dev_uuid), vibration_g, battery_pct, core_temp, system_state);

        // Update Ingress Results Panel
        M5.Display.fillRect(0, 58, 240, 53, TFT_BLACK);
        M5.Display.setTextDatum(textdatum_t::top_left);

        if (success) {
            M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
            M5.Display.drawString("SQL INSERT SUCCESS (HTTP 200)", 15, 62);
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5.Display.printf("Record ID: #%-6u | Total Sent: %u\n", g_last_record_id, g_total_records_sent);
        } else {
            M5.Display.setTextColor(TFT_RED, TFT_BLACK);
            M5.Display.printf("INSERT FAILED (HTTP %d)\n", g_last_http_status);
            M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
            M5.Display.drawString("Check API URL, Key, or DB Host", 15, 78);
        }

        M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
        M5.Display.setCursor(15, 94);
        M5.Display.printf("Status: %-8s | Temp: %.1f C\n", system_state.c_str(), core_temp);
    }

    delay(50);
}

```

### Build & Flash:

```bash
pio run -t upload
pio device monitor

```

---

## 6. Verification: Inspecting Live Data Lake Ingestion

1. Keep the **M5Stack Stick S3** running on your desk.
2. In your web browser, open the URL to `view_data.php`:
```text
http://yourdomain.atwebpages.com/view_data.php

```


3. Verify that the table populates every 10 seconds with:
* The unique hardware MAC identifier of your ESP32-S3.
* Dynamic linear acceleration values from the BMI270.
* Real-time battery percentage from the AXP2101 PMIC.


4. **Physical Anomaly Test:** Shake the M5Stick S3 firmly. Verify that:
* The status changes from `NOMINAL` to `WARN_VIB` or `CRITICAL`.
* The badge in `view_data.php` turns yellow or red.


5. **Event-Driven Test:** Press **Button A** on the face of the device. Confirm that a record is generated immediately without waiting for the 10-second timer.

---

## 7. Troubleshooting & Diagnostic Matrix

| Error Signature | Primary Cause | Resolution |
| --- | --- | --- |
| **HTTP `403 Forbidden**` | API key mismatch between `secrets.h` and `post_data.php`. | Verify that `API_SECRET_KEY` in PHP strictly equals `DatabaseConfig::API_KEY` in C++. |
| **HTTP `500 Internal Server Error**` | Database connection failure in `post_data.php`. | Check `fdbXX.awardspace.net` host string. Confirm the database user has been granted privileges on that specific database name. |
| **HTTP `-1` (`HTTPC_ERROR_CONNECTION_REFUSED`)** | Device could not resolve domain name or connect to port 80. | Verify Wi-Fi network has active internet access and DNS resolution is functioning. |
| **`Statement preparation failed: Table doesn't exist`** | Table name mismatch in the SQL query. | Ensure the table was created as `sensor_logs` in phpMyAdmin inside the active database. |
| **Inserted records show `0.000` for vibration** | Variable name mismatch in the POST payload. | Verify `vibration_g` in `main.cpp` matches `$_POST['vibration_g']` in `post_data.php`. |

---

## 8. Hands-On Verification Lab

Complete the following validation tasks before advancing to Lesson 07:

* [ ] **Task 6.1:** Run `tools/test_database_api.py` and confirm that test 1 receives an HTTP `403` and test 2 receives an HTTP `200`.
* [ ] **Task 6.2:** Flash the M5Stick S3 firmware. Let it execute for at least 1 minute and verify that 6 consecutive records appear in `view_data.php`.
* [ ] **Task 6.3:** Execute an analytical query directly in phpMyAdmin to aggregate fleet metrics:
```sql
SELECT 
    device_id, 
    COUNT(*) AS total_samples, 
    AVG(vibration_g) AS mean_vibration, 
    MAX(vibration_g) AS peak_vibration,
    MIN(battery_pct) AS lowest_battery
FROM sensor_logs 
GROUP BY device_id;

```



---

## 9. Version Control Protocol

Commit your private cloud database implementation to Git:

```bash
# Verify credentials in include/secrets.h remain ignored
git status

# Stage firmware, tools, and PHP middleware templates
git add platformio.ini src/main.cpp tools/test_database_api.py .gitignore

# Commit changes
git commit -m "feat(lesson06): implement private cloud telemetry pipeline with awardspace mysql and php middleware"

```

---
