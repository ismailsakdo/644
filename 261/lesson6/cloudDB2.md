# Lesson 06: Private Cloud Database Integration — AwardSpace Hosting, MySQL & PHP Middleware Ingestion

This module transitions from third-party public cloud platforms to a **private, enterprise-owned cloud data lake**. You will provision a remote **relational MySQL database** on **AwardSpace**, engineer a secure server-side **PHP ingestion API** using parameterized SQL statements, validate the backend headlessly with Python, and program the **M5Stack Stick S3** to stream real-time **BMI270 IMU**, **SPM1423 microphone**, and **AXP2101 power telemetry** directly into your own SQL database table.

```text
                     PRIVATE CLOUD DATABASE ARCHITECTURE
                     
  +------------------------------------------------------------------------+
  | EDGE HARDWARE TIER: M5Stack Stick S3                                   |
  |  - Sensors: BMI270 (Vibration), SPM1423 (Sound dB), AXP2101 (Battery)  |
  |  - Hardware ID: Unique Silicon eFuse MAC Address                       |
  |  - Network: 2.4 GHz 802.11 b/g/n Wi-Fi Client                          |
  +------------------------------------------------------------------------+
                                      |
                     HTTP POST (Port 80 / Port 443)
                     Payload: `api_key=...&device_id=...&vibration_g=...`
                                      |
                                      v
  +------------------------------------------------------------------------+
  | APPLICATION SERVER TIER: AwardSpace Web Host                           |
  |  - Apache Web Server (Virtual Host Gateway)                            |
  |  - PHP Ingestion Endpoint (`post_data.php`)                            |
  |    * Timing-Safe Shared Secret Validation (hash_equals)                |
  |    * Input Boundary Sanitization & Type Coercion                       |
  |    * Parameterized Prepared Statements (100% SQLi Defense)             |
  +------------------------------------------------------------------------+
                                      |
                         Internal TCP Socket (Port 3306)
                                      |
                                      v
  +------------------------------------------------------------------------+
  | DATA PERSISTENCE TIER: MySQL Relational Database                       |
  |  - Host: `fdb10XX.awardspace.net` (External Host Cluster)              |
  |  - Database: `[AccountID]_iot` | Table: `sensor_logs`                  |
  |  - Query Interfaces: phpMyAdmin & `view_data.php` Console              |
  +------------------------------------------------------------------------+

```

---

## 1. Enterprise Architecture: Public PaaS vs. Private Relational Backends

While platforms like ThingSpeak and Google Forms offer rapid deployment, enterprise fleets mandate dedicated relational database backends:

| Architectural Dimension | Public IoT Platforms (ThingSpeak / Google Forms)

 | Private Relational Backends (MySQL / MariaDB on Cloud Host)

 |
| --- | --- | --- |
| **Data Sovereignty & Governance**

 | Data resides on multi-tenant, third-party US infrastructure.

 | Full institutional control over physical data residency, schemas, and retention.

 |
| **Query Engine Capability**

 | Limited single-channel slicing; no multi-table joins.

 | Full ANSI SQL support: relational `JOIN`, `GROUP BY`, dynamic window functions.

 |
| **Ingestion Latency & Rate Limits**

 | Enforced hardware throttles (e.g., ThingSpeak 15-second lock).

 | Governed only by server compute and database connection pool limits.

 |
| **Enterprise Integration**

 | Proprietary webhooks and vendor-locked export APIs.

 | Native ODBC/JDBC driver integration for Power BI, Grafana, ERPs, and Python ML pipelines.

 |
| **Cost at Scale**

 | Tiered subscription per device, channel, or message quota.

 | Fixed infrastructure hosting cost supporting millions of records.

 |

---

## 2. AwardSpace Provisioning & MySQL Database Creation

AwardSpace provides a LAMP (Linux, Apache, MySQL, PHP) hosting stack with phpMyAdmin administration.

### Step 2.1: Provisioning the Database Instance

1. Log in to your **AwardSpace Control Panel**.


2. Navigate to **Database Manager** $\rightarrow$ **MySQL Databases**.


3. Click **Create a Database** and supply:
* **Database Name:** e.g., `iot_fleet` (AwardSpace automatically prepends your account ID, creating a final string like `4192831_iot_fleet`).


* **Database User:** e.g., `db_user` (AwardSpace prepends your ID, creating `4192831_db_user`).


* **Password:** Generate a strong, random password (avoid special characters that require URL-encoding, such as `#`, `&`, or spaces).




4. **Identify the Database Host:** Note the exact hostname listed in your AwardSpace Database Manager (e.g., `fdb1030.awardspace.net`).



> **Critical Architecture Rule:** The database server and web server run on separate nodes within the AwardSpace datacenter cluster. You **must never** use `localhost`. Always use the explicit `fdbXX.awardspace.net` host address provided in your dashboard.
> 
> 

---

### Step 2.2: Schema Initialization via phpMyAdmin

1. In the **Database Manager**, click the **phpMyAdmin** icon beside your newly created database.


2. Select your active database from the left-hand navigation tree.


3. Click the **SQL** tab and execute the following Data Definition Language (DDL) statement:



```sql
CREATE TABLE `sensor_logs` (
    `id` INT(11) UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    `device_id` VARCHAR(32) NOT NULL,
    `vibration_g` FLOAT(6,3) NOT NULL,
    `sound_db` FLOAT(5,1) NOT NULL,
    `battery_pct` TINYINT(3) UNSIGNED NOT NULL,
    `temperature_c` FLOAT(4,1) NOT NULL,
    `system_state` VARCHAR(20) NOT NULL DEFAULT 'NOMINAL',
    `reading_timestamp` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX `idx_device_time` (`device_id`, `reading_timestamp`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

```

#### Field Design Specifications:

* `id`: Unsigned auto-incrementing integer serving as the immutable surrogate primary key.


* `device_id`: Hardware identifier derived from the ESP32-S3 eFuse MAC address (e.g., `S3_7C02B84D`).


* `vibration_g`: Euclidean acceleration vector magnitude ($\Vert{}A\Vert{}$) stored to 3 decimal places.


* `sound_db`: Ambient acoustic sound pressure level sampled from the SPM1423 MEMS microphone.


* `battery_pct`: Fuel gauge percentage from the AXP2101 PMIC (`0` to `100`).


* `temperature_c`: Internal silicon die temperature of the ESP32-S3 SoC.


* `system_state`: Operating condition classification flag (`NOMINAL`, `WARN_VIB`, `WARN_SOUND`, `CRITICAL`).


* `reading_timestamp`: Server-side UTC timestamp generated at `INSERT`.


* `INDEX idx_device_time`: Composite B-Tree index optimizing high-frequency time-series queries filtered by device.



---

## 3. Server-Side Middleware: PHP Ingestion API & Web Dashboard

To protect database integrity, the M5Stack Stick S3 does not connect directly to MySQL over TCP port 3306. Instead, an intermediary PHP application layer authenticates incoming traffic, sanitizes data, and manages database connection pooling.

```text
                  API SECURITY & SANITIZATION PIPELINE
                  
  Incoming HTTP POST Payload
  +------------------------------------------------------------------------+
  | api_key=SECRET & device_id=S3_7C02 & vibration_g=1.042 & sound_db=52.1 |
  +------------------------------------------------------------------------+
                                      |
                                      v
  [STAGE 1: METHOD & AUTHENTICATION VALIDATION]
  Enforce HTTP POST method. Verify $_POST['api_key'] using hash_equals().
    -> MISMATCH: Return HTTP 403 Forbidden and exit.
                                      |
                                      v
  [STAGE 2: EXTRACTION, SANITIZATION & BOUNDARY CHECKS]
  Cast numeric strings: floatval(), intval().
  Clamp boundaries: battery_pct in [0, 100], sound_db in [30, 120].
                                      |
                                      v
  [STAGE 3: PARAMETERIZED PREPARED STATEMENT]
  Compile SQL statement: INSERT INTO sensor_logs (...) VALUES (?,?,?,?,?,?)
  Bind typed parameters: "sddids" -> Eliminates SQL Injection risks.
                                      |
                                      v
  [STAGE 4: STRUCTURED JSON RESPONSE DISPATCH]
  Return HTTP 200 OK: {"status":"success","record_id":184,"server_time":"..."}

```

### Step 3.1: Deploying `post_data.php`

1. In the AwardSpace Control Panel, open **File Manager**.


2. Navigate to your website's root directory (e.g., `/www/[yourdomain.atwebpages.com/](https://yourdomain.atwebpages.com/)` or `/public_html/`).


3. Create a file named `post_data.php` and insert the following code:



```php
<?php
/**
 * ============================================================================
 * File: post_data.php
 * Description: Hardened REST Ingestion API for M5Stack Stick S3 Telemetry
 * Security: Timing-Safe Token Auth + Parameterized Prepared Statements
 * ============================================================================
 */

// Strict Response Headers
header("Content-Type: application/json; charset=UTF-8");
header("X-Content-Type-Options: nosniff");

// -----------------------------------------------------------------------------
// 1. CONFIGURATION: Configure with your AwardSpace DB Credentials
// -----------------------------------------------------------------------------
define("DB_HOST", "fdb1030.awardspace.net"); // Replace with your exact AwardSpace DB Host
define("DB_USER", "YOUR_DB_USER");          // Full prefixed user (e.g., 4192831_db_user)
define("DB_PASS", "YOUR_DB_PASSWORD");      // Your chosen DB password
define("DB_NAME", "YOUR_DB_NAME");          // Full prefixed name (e.g., 4192831_iot_fleet)

// Pre-Shared Secret Authentication Key
define("API_SECRET_KEY", "MBA_IOT_SECURE_TOKEN_2026");

// -----------------------------------------------------------------------------
// 2. REQUEST METHOD & AUTHENTICATION ENFORCEMENT
// -----------------------------------------------------------------------------
if ($_SERVER["REQUEST_METHOD"] !== "POST") {
    http_response_code(405); // Method Not Allowed
    echo json_encode([
        "status"  => "error",
        "message" => "Invalid protocol. Only HTTP POST requests are accepted."
    ]);
    exit();
}

$provided_key = isset($_POST["api_key"]) ? (string)$_POST["api_key"] : "";

// Use timing-safe string comparison to prevent timing attacks
if (!hash_equals(API_SECRET_KEY, $provided_key)) {
    http_response_code(403); // Forbidden
    echo json_encode([
        "status"  => "error",
        "message" => "Access denied. Invalid or missing API secret key."
    ]);
    exit();
}

// -----------------------------------------------------------------------------
// 3. INPUT PARSING, TYPE CASTING & BOUNDARY SANITIZATION
// -----------------------------------------------------------------------------
$device_id     = isset($_POST["device_id"]) ? substr(trim((string)$_POST["device_id"]), 0, 32) : "UNKNOWN_NODE";
$vibration_g   = isset($_POST["vibration_g"]) ? floatval($_POST["vibration_g"]) : 0.0;
$sound_db      = isset($_POST["sound_db"]) ? floatval($_POST["sound_db"]) : 0.0;
$battery_pct   = isset($_POST["battery_pct"]) ? intval($_POST["battery_pct"]) : 0;
$temperature_c = isset($_POST["temperature_c"]) ? floatval($_POST["temperature_c"]) : 0.0;
$system_state  = isset($_POST["system_state"]) ? substr(trim((string)$_POST["system_state"]), 0, 20) : "NOMINAL";

// Enforce operational boundaries
$battery_pct = max(0, min(100, $battery_pct));
$sound_db    = max(0.0, min(140.0, $sound_db));
$vibration_g = max(0.0, min(20.0, $vibration_g));

// -----------------------------------------------------------------------------
// 4. DATABASE TRANSACTIONS VIA PREPARED STATEMENTS
// -----------------------------------------------------------------------------
$conn = new mysqli(DB_HOST, DB_USER, DB_PASS, DB_NAME);

if ($conn->connect_error) {
    http_response_code(500);
    echo json_encode([
        "status"  => "error",
        "message" => "Database connection failure: " . $conn->connect_error
    ]);
    exit();
}

// Parameterized Query Construction
$sql = "INSERT INTO sensor_logs 
        (device_id, vibration_g, sound_db, battery_pct, temperature_c, system_state) 
        VALUES (?, ?, ?, ?, ?, ?)";

$stmt = $conn->prepare($sql);

if (!$stmt) {
    http_response_code(500);
    echo json_encode([
        "status"  => "error",
        "message" => "SQL preparation failed: " . $conn->error
    ]);
    $conn->close();
    exit();
}

// Data Type Map:
// s = string, d = double (float), i = integer
// device_id(s), vibration_g(d), sound_db(d), battery_pct(i), temperature_c(d), system_state(s)
$stmt->bind_param("sddids", $device_id, $vibration_g, $sound_db, $battery_pct, $temperature_c, $system_state);

if ($stmt->execute()) {
    $inserted_id = $stmt->insert_id;
    http_response_code(200);
    echo json_encode([
        "status"      => "success",
        "record_id"   => $inserted_id,
        "device_id"   => $device_id,
        "server_time" => gmdate("Y-m-d H:i:s") . " UTC"
    ]);
} else {
    http_response_code(500);
    echo json_encode([
        "status"  => "error",
        "message" => "Execution error: " . $stmt->error
    ]);
}

$stmt->close();
$conn->close();
?>

```

---

### Step 3.2: Deploying the Web Data Viewer (`view_data.php`)

Create `view_data.php` in the same directory to inspect ingested records in real time without opening phpMyAdmin:

```php
<?php
/**
 * ============================================================================
 * File: view_data.php
 * Description: Executive Web Dashboard for Stored Sensor Telemetry
 * ============================================================================
 */
define("DB_HOST", "fdb1030.awardspace.net"); // Replace with your AwardSpace DB Host
define("DB_USER", "YOUR_DB_USER");          // Replace with your DB User
define("DB_PASS", "YOUR_DB_PASSWORD");      // Replace with your DB Password
define("DB_NAME", "YOUR_DB_NAME");          // Replace with your DB Name

$conn = new mysqli(DB_HOST, DB_USER, DB_PASS, DB_NAME);
if ($conn->connect_error) {
    die("Database connection failed: " . htmlspecialchars($conn->connect_error));
}

$sql = "SELECT id, device_id, vibration_g, sound_db, battery_pct, temperature_c, system_state, reading_timestamp 
        FROM sensor_logs ORDER BY id DESC LIMIT 50";
$result = $conn->query($sql);
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta http-equiv="refresh" content="10">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>M5Stack Stick S3 Private Telemetry Lake</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; margin: 24px; background: #0d1117; color: #c9d1d9; }
        h1 { color: #58a6ff; font-size: 20px; margin-bottom: 6px; }
        .meta-bar { font-size: 13px; color: #8b949e; margin-bottom: 18px; padding-bottom: 12px; border-bottom: 1px solid #30363d; }
        table { border-collapse: collapse; width: 100%; max-width: 1100px; background: #161b22; border-radius: 8px; overflow: hidden; border: 1px solid #30363d; }
        th, td { padding: 10px 14px; text-align: left; border-bottom: 1px solid #21262d; font-size: 13px; }
        th { background: #21262d; color: #f0f6fc; font-weight: 600; }
        tr:hover { background: #1f242c; }
        .badge { padding: 3px 8px; border-radius: 12px; font-weight: bold; font-size: 11px; text-transform: uppercase; }
        .badge-nominal { background: #238636; color: #ffffff; }
        .badge-warn { background: #d29922; color: #000000; }
        .badge-critical { background: #da3633; color: #ffffff; }
        code { background: #262c36; padding: 2px 6px; border-radius: 4px; color: #79c0ff; font-family: monospace; }
    </style>
</head>
<body>
    <h1>M5Stack Stick S3 Enterprise Telemetry Lake</h1>
    <div class="meta-bar">
        Target Database: <code><?= htmlspecialchars(DB_NAME) ?></code> | Auto-refreshes every 10 seconds | Showing latest 50 records
    </div>
    <table>
        <thead>
            <tr>
                <th>Record ID</th>
                <th>UTC Timestamp</th>
                <th>Device ID</th>
                <th>Vibration</th>
                <th>Sound Level</th>
                <th>Battery</th>
                <th>Core Temp</th>
                <th>System State</th>
            </tr>
        </thead>
        <tbody>
        <?php if ($result && $result->num_rows > 0): ?>
            <?php while($row = $result->fetch_assoc()): ?>
                <?php
                    $state = $row["system_state"];
                    $badge = "badge-nominal";
                    if (strpos($state, "WARN") !== false) $badge = "badge-warn";
                    if (strpos($state, "CRITICAL") !== false || strpos($state, "ALARM") !== false) $badge = "badge-critical";
                ?>
                <tr>
                    <td>#<?= (int)$row["id"] ?></td>
                    <td><?= htmlspecialchars($row["reading_timestamp"]) ?></td>
                    <td><code><?= htmlspecialchars($row["device_id"]) ?></code></td>
                    <td><strong><?= number_format($row["vibration_g"], 3) ?> G</strong></td>
                    <td><?= number_format($row["sound_db"], 1) ?> dB</td>
                    <td><?= (int)$row["battery_pct"] ?>%</td>
                    <td><?= number_format($row["temperature_c"], 1) ?> &deg;C</td>
                    <td><span class="badge <?= $badge ?>"><?= htmlspecialchars($state) ?></span></td>
                </tr>
            <?php endwhile; ?>
        <?php else: ?>
            <tr><td colspan="8" style="text-align: center; padding: 32px; color: #8b949e;">No telemetry records found. Awaiting edge transmissions.</td></tr>
        <?php endif; ?>
        </tbody>
    </table>
</body>
</html>
<?php $conn->close(); ?>

```

---

## 4. Pre-Hardware Verification: Headless Python Test Harness

Before running embedded code, test the API endpoints using an automated test harness to confirm that security rejection and row insertion function as expected.

Create `tools/test_database_api.py` on your workstation:

```python
#!/usr/bin/env python3
"""
===============================================================================
Lesson 06: Private Cloud Database Integration
Pre-Hardware Headless Verification Harness (HTTP POST)
===============================================================================
"""

import time
import requests

# -----------------------------------------------------------------------------
# USER CONFIGURATION: Update with your AwardSpace endpoint URL
# -----------------------------------------------------------------------------
API_URL = "http://yourdomain.atwebpages.com/post_data.php"
VALID_API_KEY = "MBA_IOT_SECURE_TOKEN_2026"
# -----------------------------------------------------------------------------

def test_unauthorized_rejection():
    """Verify that requests missing a valid key are rejected with HTTP 403."""
    print("[TEST 1] Testing security barrier with unauthorized key...")
    payload = {
        "api_key": "INVALID_EXPLOIT_KEY",
        "device_id": "ROGUE_NODE"
    }
    response = requests.post(API_URL, data=payload, timeout=8.0)
    print(f" -> HTTP Status: {response.status_code}")
    print(f" -> Server Response: {response.text.strip()}")
    assert response.status_code == 403, f"Security Breach: Expected 403, got {response.status_code}"
    print(" [PASS] Unauthorized submission successfully blocked.\n")

def test_valid_telemetry_insertion():
    """Verify that structured telemetry is inserted and returns HTTP 200 with record ID."""
    print("[TEST 2] Testing valid multimodal telemetry ingestion...")
    payload = {
        "api_key": VALID_API_KEY,
        "device_id": "M5S3_TEST_BENCH",
        "vibration_g": 1.082,
        "sound_db": 58.4,
        "battery_pct": 94,
        "temperature_c": 37.8,
        "system_state": "NOMINAL"
    }
    
    start_time = time.time()
    response = requests.post(API_URL, data=payload, timeout=8.0)
    latency_ms = round((time.time() - start_time) * 1000, 1)

    print(f" -> HTTP Status: {response.status_code} (Round-Trip Latency: {latency_ms} ms)")
    print(f" -> Server Response: {response.text.strip()}")
    assert response.status_code == 200, f"Ingestion Failure: Expected 200, got {response.status_code}"

    json_data = response.json()
    assert json_data.get("status") == "success", "Response JSON does not contain success status"
    new_id = json_data.get("record_id")
    print(f" [PASS] Database persisted record successfully. New ID: #{new_id}\n")

def main():
    print("=" * 70)
    print("  AWARDSPACE PRIVATE CLOUD INGESTION API TEST")
    print(f"  Target: {API_URL}")
    print("=" * 70 + "\n")

    test_unauthorized_rejection()
    test_valid_telemetry_insertion()

    print("=" * 70)
    print("All backend checks passed. Open view_data.php in your browser.")
    print("=" * 70)

if __name__ == "__main__":
    main()

```

### Execution:

```bash
python3 tools/test_database_api.py

```

---

## 5. M5Stack Stick S3 Firmware Implementation

Once the cloud backend is verified, configure the **M5Stack Stick S3** to sample the **BMI270 IMU**, the **SPM1423 MEMS microphone**, and the **AXP2101 PMIC**, evaluate threshold states, and stream the data to AwardSpace.

### 5.1 Project Layout & `platformio.ini`

```text
lesson06-database-awardspace/
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
mkdir -p lesson06-database-awardspace/include lesson06-database-awardspace/src
cd lesson06-database-awardspace

```

#### `platformio.ini`:

```ini
; =====================================================================
; PlatformIO Configuration: Lesson 06 - AwardSpace MySQL Ingestion
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
    -O2

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

---

### 5.2 Header Configuration (`include/secrets.h`)

Create `include/secrets.h`:

```cpp
#pragma once

namespace NetworkConfig {
    const char* WIFI_SSID     = "YOUR_2.4GHZ_WIFI_SSID";
    const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
}

namespace DatabaseConfig {
    // Explicit URL to your deployed post_data.php script
    const char* INGEST_URL    = "http://yourdomain.atwebpages.com/post_data.php";
    const char* API_KEY       = "MBA_IOT_SECURE_TOKEN_2026";
    const unsigned long INGEST_INTERVAL_MS = 15000; // Ingestion interval (15 seconds)
}

```

---

### 5.3 Microcontroller Firmware (`src/main.cpp`)

Create `src/main.cpp`. This firmware integrates non-blocking sensing across all peripherals, arbitrates I2S bus usage, and uses `HTTPClient` to ingest records into MySQL:

```cpp
/**
 * ============================================================================
 * Project: M5Stack Stick S3 Journey - Lesson 06
 * Description: Telemetry Streaming to Private AwardSpace MySQL Database
 * Peripherals: BMI270, SPM1423 Mic, ST7789 LCD, AXP2101 PMIC, Button A
 * ============================================================================
 */
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>
#include "secrets.h"

// Hardware Identity & Cloud Telemetry State
static char g_device_uuid[20];
static unsigned long g_last_ingest_ms = 0;
static uint32_t g_last_record_id = 0;
static int g_last_http_status = 0;
static uint32_t g_total_records_sent = 0;

// Acoustic Sampling Setup (SPM1423 via I2S DMA)
static constexpr size_t MIC_SAMPLES = 256;
static int16_t g_mic_buffer[MIC_SAMPLES];
static float g_smoothed_db = 38.0f;
static unsigned long g_last_mic_ms = 0;

// Multimodal Telemetry Frame
struct TelemetryPayload {
    float vibration_g;
    float sound_db;
    int battery_pct;
    float core_temp;
    String system_state;
};

void drawBaseDashboard() {
    M5.Display.fillScreen(TFT_BLACK);

    // Top Navigation Ribbon
    M5.Display.fillRect(0, 0, 240, 20, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.drawString(" MYSQL CLOUD INGRESS", 5, 10, &fonts::Font2);

    // Grid Layout Separators
    M5.Display.drawFastHLine(0, 56, 240, TFT_DARKGRAY);
    M5.Display.drawFastHLine(0, 114, 240, TFT_DARKGRAY);
    M5.Display.drawFastVLine(120, 20, 36, TFT_DARKGRAY);

    // Metric Header Labels
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VIBRATION", 60, 23, &fonts::Font0);
    M5.Display.drawString("SOUND LEVEL", 180, 23, &fonts::Font0);

    // Softkey Navigation
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("BtnA: Force Immediate SQL Insert", 120, 124, &fonts::Font0);
}

void sampleMicrophone() {
    unsigned long now = millis();
    if (now - g_last_mic_ms < 40) return; // Sample at 25 Hz
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

        // Smooth acoustic readings (IEC FAST filter)
        g_smoothed_db = (0.30f * raw_db) + (0.70f * g_smoothed_db);
    }
}

TelemetryPayload acquireSensoryData() {
    TelemetryPayload data;

    // 1. BMI270 Linear Acceleration Magnitude
    float ax, ay, az;
    M5.Imu.getAccel(&ax, &ay, &az);
    data.vibration_g = sqrtf((ax * ax) + (ay * ay) + (az * az));

    // 2. SPM1423 Sound Pressure Level
    sampleMicrophone();
    data.sound_db = g_smoothed_db;

    // 3. AXP2101 PMIC Battery Fuel Gauge
    data.battery_pct = M5.Power.getBatteryLevel();
    if (data.battery_pct < 0) data.battery_pct = 100;

    // 4. SoC Silicon Die Temperature
    data.core_temp = temperatureRead();

    // 5. System State Decision Matrix
    if (data.vibration_g > 1.65f || data.sound_db > 80.0f) {
        data.system_state = "CRITICAL";
    } else if (data.vibration_g > 1.35f) {
        data.system_state = "WARN_VIB";
    } else if (data.sound_db > 70.0f) {
        data.system_state = "WARN_SOUND";
    } else {
        data.system_state = "NOMINAL";
    }

    return data;
}

bool postTelemetryToDatabase(const TelemetryPayload& data) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[DATABASE] Ingestion skipped: Wi-Fi offline.");
        return false;
    }

    HTTPClient http;
    http.begin(DatabaseConfig::INGEST_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Construct URL-encoded payload matching the PHP backend schema
    String payload = "";
    payload += "api_key="       + String(DatabaseConfig::API_KEY) + "&";
    payload += "device_id="     + String(g_device_uuid) + "&";
    payload += "vibration_g="   + String(data.vibration_g, 3) + "&";
    payload += "sound_db="      + String(data.sound_db, 1) + "&";
    payload += "battery_pct="   + String(data.battery_pct) + "&";
    payload += "temperature_c=" + String(data.core_temp, 1) + "&";
    payload += "system_state="  + data.system_state;

    Serial.printf("[DATABASE] Dispatching POST to: %s\n", DatabaseConfig::INGEST_URL);
    unsigned long start_time = millis();
    int http_code = http.POST(payload);
    unsigned long latency = millis() - start_time;
    g_last_http_status = http_code;

    bool success = false;

    if (http_code == 200) {
        String response_body = http.getString();
        Serial.printf("[DATABASE] Success (%lu ms): %s\n", latency, response_body.c_str());

        // Extract auto-incremented record ID from JSON response
        int id_pos = response_body.indexOf("\"record_id\":");
        if (id_pos != -1) {
            int end_pos = response_body.indexOf(",", id_pos);
            if (end_pos == -1) end_pos = response_body.indexOf("}", id_pos);
            String id_str = response_body.substring(id_pos + 12, end_pos);
            g_last_record_id = id_str.toInt();
        }

        g_total_records_sent++;
        success = true;
    } else {
        Serial.printf("[DATABASE] Ingestion Failed. HTTP Status: %d\n", http_code);
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

    // Derive Hardware UUID
    uint64_t mac = ESP.getEfuseMac();
    snprintf(g_device_uuid, sizeof(g_device_uuid), "S3_%04X%08X", 
             (uint16_t)(mac >> 32), (uint32_t)mac);

    drawBaseDashboard();

    // Initialize Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(NetworkConfig::WIFI_SSID, NetworkConfig::WIFI_PASSWORD);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Associating with Wi-Fi...", 120, 80, &fonts::Font2);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WIFI] Connected.");
    drawBaseDashboard();
}

void loop() {
    M5.update();

    // 1. Multi-Sensor Data Acquisition
    TelemetryPayload current_data = acquireSensoryData();

    // 2. Render Top Header RSSI & Battery Telemetry
    char buf[48];
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    snprintf(buf, sizeof(buf), "%d dBm | %d%% ", WiFi.RSSI(), current_data.battery_pct);
    M5.Display.drawString(buf, 235, 10, &fonts::Font2);

    // 3. Render Live Operational Measurements
    M5.Display.setTextDatum(textdatum_t::middle_center);
    
    // Vibration Metric
    snprintf(buf, sizeof(buf), "%4.2f G", current_data.vibration_g);
    M5.Display.setTextColor((current_data.vibration_g > 1.35f) ? TFT_RED : TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(buf, 60, 42, &fonts::Font4);

    // Acoustic Metric
    snprintf(buf, sizeof(buf), "%4.1f dB", current_data.sound_db);
    M5.Display.setTextColor((current_data.sound_db > 70.0f) ? TFT_RED : TFT_GREENYELLOW, TFT_BLACK);
    M5.Display.drawString(buf, 180, 42, &fonts::Font4);

    // 4. Transmission Logic: Non-Blocking Interval or Button A Trigger
    bool force_manual_insert = M5.BtnA.wasClicked();
    unsigned long now = millis();

    if (force_manual_insert || (now - g_last_ingest_ms >= DatabaseConfig::INGEST_INTERVAL_MS)) {
        g_last_ingest_ms = now;

        // Visual Ingress Indicator
        M5.Display.fillRect(0, 58, 240, 54, TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setTextDatum(textdatum_t::middle_center);
        M5.Display.drawString("Streaming to MySQL...", 120, 72, &fonts::Font2);

        bool success = postTelemetryToDatabase(current_data);

        // Render Ingress Results
        M5.Display.fillRect(0, 58, 240, 54, TFT_BLACK);
        M5.Display.setTextDatum(textdatum_t::top_left);

        if (success) {
            M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
            M5.Display.drawString("SQL INSERT SUCCESS (HTTP 200)", 10, 62, &fonts::Font0);
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            snprintf(buf, sizeof(buf), "Record ID: #%-6u | Total: %u", g_last_record_id, g_total_records_sent);
            M5.Display.drawString(buf, 10, 78, &fonts::Font0);
        } else {
            M5.Display.setTextColor(TFT_RED, TFT_BLACK);
            snprintf(buf, sizeof(buf), "INSERT FAILED (HTTP %d)", g_last_http_status);
            M5.Display.drawString(buf, 10, 62, &fonts::Font0);
            M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
            M5.Display.drawString("Check URL, API Key, or DB Host", 10, 78, &fonts::Font0);
        }

        M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
        snprintf(buf, sizeof(buf), "State: %-9s | Temp: %.1f C", current_data.system_state.c_str(), current_data.core_temp);
        M5.Display.drawString(buf, 10, 96, &fonts::Font0);
    }

    delay(30);
}

```

### Build & Upload:

```bash
pio run -e m5stack-s3 -t upload
pio device monitor -b 115200

```

---

## 6. Live Ingestion Verification

1. Leave the **M5Stack Stick S3** powered and operating on your desk.


2. In your desktop browser, open your live `view_data.php` URL:


```text
http://yourdomain.atwebpages.com/view_data.php

```


3. Confirm that new rows append to the table every 15 seconds:


* The device's unique MAC string populates the `Device ID` column.


* Physical acceleration ($\sim 1.00\text{ G}$) and ambient noise ($\sim 38\text{--}45\text{ dB}$) register dynamically.
* Internal die temperature and battery fuel percentage reflect live device values.




4. **Physical Anomaly Test:** Shake the device or speak loudly into the microphone hole. Confirm that the state transitions to `WARN_VIB`, `WARN_SOUND`, or `CRITICAL`, and verify that the badge color updates to yellow or red in `view_data.php`.


5. **Event-Driven Test:** Press **Button A** on the front of the M5Stick S3. Confirm that a record is submitted immediately without waiting for the 15-second timer.



---

## 7. Troubleshooting & Diagnostic Matrix

| Error Signature | Underlying Root Cause | Corrective Action |
| --- | --- | --- |
| **HTTP `403 Forbidden**`

 | `API_SECRET_KEY` in `post_data.php` does not match `DatabaseConfig::API_KEY` in `secrets.h`.

 | Align both strings character-for-character. Ensure no trailing spaces exist in either file.

 |
| **HTTP `500 Internal Server Error**`

 | Database connection failed or PHP cannot reach the MySQL instance.

 | Verify the database host string (e.g., `fdb1030.awardspace.net`). **Do not use `localhost**`. Confirm user credentials via phpMyAdmin.

 |
| **HTTP `-1` (`HTTPC_ERROR_CONNECTION_REFUSED`)**

 | DNS failed to resolve your AwardSpace domain or gateway blocked port 80.

 | Verify router internet connectivity. Test if the domain resolves on your workstation terminal via `ping yourdomain.atwebpages.com`.

 |
| **`Statement preparation failed: Table doesn't exist`**

 | Table name mismatch between DDL script and PHP statement.

 | In phpMyAdmin, verify that the table is named `sensor_logs` within your assigned database.

 |
| **`vibration_g` or `sound_db` registers as `0.000**`

 | Key name mismatch between C++ POST string and PHP `$_POST` array.

 | Verify that parameter keys match across files (`vibration_g`, `sound_db`, `battery_pct`, `temperature_c`, `system_state`).

 |
| **Microphone returns saturated noise** | I2S DMA clock contention caused by driving the onboard speaker simultaneously. | Set `cfg.internal_spk = false;` in `setup()`. Do not call `M5.Speaker` methods while `M5.Mic` is recording. |

---

## 8. Hands-On Verification Lab

Complete the following validation tasks before advancing to Lesson 07:

* [ ] **Task 6.1:** Run `tools/test_database_api.py` and confirm that Test 1 returns an HTTP `403` rejection and Test 2 returns an HTTP `200` insertion.


* [ ] **Task 6.2:** Flash the M5Stick S3 firmware. Let it run for 2 minutes and confirm that at least 8 consecutive records appear in `view_data.php`.


* [ ] **Task 6.3:** Log in to phpMyAdmin, open your database, and execute an analytical aggregation query to calculate fleet telemetry metrics:


```sql
SELECT 
    device_id, 
    COUNT(*) AS total_samples, 
    ROUND(AVG(vibration_g), 3) AS mean_vibration_g, 
    ROUND(MAX(vibration_g), 3) AS peak_vibration_g,
    ROUND(AVG(sound_db), 1) AS mean_sound_db,
    ROUND(MAX(sound_db), 1) AS peak_sound_db,
    MIN(battery_pct) AS min_battery_pct
FROM sensor_logs 
GROUP BY device_id;

```


Confirm that the query successfully aggregates your device's historical telemetry records.

---

## 9. Version Control Protocol

Commit your private cloud database implementation to Git:

```bash
# Verify that include/secrets.h is excluded from staging
git status

# Stage firmware, test tools, configuration, and middleware templates
git add platformio.ini src/main.cpp include/secrets.example.h tools/test_database_api.py .gitignore

# Commit changes
git commit -m "feat(lesson06): implement private cloud database pipeline with awardspace mysql and php middleware"

```
