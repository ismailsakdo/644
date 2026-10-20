# Lesson 02: Cloud Telemetry Ingestion — ThingSpeak IoT & Google Forms Reverse Engineering

This module details how to provision, configure, and independently validate two distinct cloud-ingestion pipelines for edge IoT fleets: **ThingSpeak** (a specialized time-series IoT PaaS) and **Google Forms/Sheets** (a serverless, zero-cost tabular ledger).

To eliminate configuration collisions and protocol confusion, **ThingSpeak** (HTTP GET / REST) and **Google Forms** (HTTP POST / URL-encoded form submissions) are implemented and tested as **two isolated, standalone Python testing workflows**.

```text
                             CLOUD INGESTION ARCHITECTURE
                             
                     +-----------------------------------------+
                     |  M5Stack Stick S3 / Local Test Engine   |
                     +-----------------------------------------+
                                          |
                +-------------------------+-------------------------+
                |                                                   |
      [PIPELINE A: TIME-SERIES]                           [PIPELINE B: TABULAR LOGS]
                |                                                   |
                | HTTPS GET (REST API)                              | HTTPS POST (URL-Encoded)
                v                                                   v
  +---------------------------+                       +---------------------------+
  |    ThingSpeak IoT PaaS    |                       |   Google Forms Ingress    |
  |  `api.thingspeak.com`     |                       |  `docs.google.com/forms`  |
  +---------------------------+                       +---------------------------+
                |                                                   |
                v                                                   v
  +---------------------------+                       +---------------------------+
  | - 15-Sec Ingestion Window |                       | - Sub-second Ingestion    |
  | - Field 1 to Field 4      |                       | - entry.XXXXXXXXX Schema  |
  | - Integrated MATLAB Plots |                       | - Auto-Synced Sheets DB   |
  +---------------------------+                       +---------------------------+

```

---

## 1. Architectural Trade-Off Analysis

| Architectural Metric | Pipeline A: ThingSpeak (PaaS) | Pipeline B: Google Forms/Sheets (SaaS) |
| --- | --- | --- |
| **Ingestion Protocol** | HTTP REST (`GET` / `POST`), MQTT | HTTPS `POST` (`application/x-www-form-urlencoded`) |
| **Rate Limit Constraint** | **1 entry per 15 seconds** (Free Tier) | **No hard throttle** (~1 entry per 1–2 sec practical) |
| **Data Topology** | Strict numerical time-series (Fields 1–8) | Unstructured tabular strings, floats, text notes |
| **Native Tooling** | Built-in MATLAB analytics & time-plots | Spreadsheet formulas, AppSheet, Looker Studio |
| **Authentication** | Static 16-character Write API Key | Headless Form Endpoint + Input Tokens |
| **Primary Business Role** | Continuous condition monitoring & analytics

 | Shift auditing, inventory ledger, lifecycle logs

 |

---

## 2. Pipeline A: ThingSpeak Setup & Dedicated Validation

ThingSpeak requires strict compliance with its **15-second rate limit** and utilizes numerical field indexing.

### Step 2.1: Channel Provisioning

1. Log in to [ThingSpeak.com](https://thingspeak.com/).
2. Select **Channels** $\rightarrow$ **My Channels** $\rightarrow$ **New Channel**.
3. Fill in the channel settings:
* **Name:** `M5StickS3_Vibration_Fleet`
* **Field 1:** `Vibration_RMS_G` (Target: Accelerometer magnitude)
* **Field 2:** `Battery_Level_Pct` (Target: PMIC battery fuel gauge)
* **Field 3:** `Core_Temperature_C` (Target: ESP32-S3 internal temperature)
* **Field 4:** `Alert_Flag` (Target: 0 = Normal, 1 = Exceeded Threshold)


4. Click **Save Channel**.

### Step 2.2: Extract API Keys

1. Click the **API Keys** tab.
2. Note your **Channel ID** and copy your **Write API Key**.

### Step 2.3: Baseline Verification (cURL)

Run this command in your terminal to ensure the channel accepts data:

```bash
curl -X GET "https://api.thingspeak.com/update?api_key=YOUR_WRITE_API_KEY&field1=1.02&field2=95&field3=37.2&field4=0"

```

* **Success:** Returns a positive integer (the continuous Entry ID, e.g., `1`).
* **Rate-Limited / Fail:** Returns `0`.

---

### Step 2.4: Dedicated Python Script: `test_thingspeak.py`

This standalone script simulates edge physics (vibration vector magnitude with random walk drift, battery drain, and thermal rise), enforces the mandatory 16-second sleep window, and prints real-time HTTP metrics.

Create `tools/test_thingspeak.py`:

```python
#!/usr/bin/env python3
"""
===============================================================================
M5Stack Stick S3 Journey - Lesson 02: Pipeline A
Dedicated ThingSpeak IoT Telemetry Test Harness
===============================================================================
"""

import time
import math
import random
import requests

# -----------------------------------------------------------------------------
# USER CONFIGURATION
# -----------------------------------------------------------------------------
THINGSPEAK_WRITE_KEY = "YOUR_THINGSPEAK_WRITE_KEY"  # Replace with 16-char key
UPDATE_INTERVAL_SEC  = 16  # Must be >= 15 seconds for free tier
CYCLES_TO_RUN        = 3
# -----------------------------------------------------------------------------

def generate_telemetry_frame(step: int) -> dict:
    """Simulates multi-variable physical telemetry from an M5Stick S3."""
    # Simulates baseline 1.0g gravity with harmonic machine vibration
    vibration_rms = round(1.0 + (0.25 * math.sin(step * 0.8)) + random.uniform(-0.04, 0.04), 3)
    
    # Battery percentage slowly draining
    battery_pct = max(5, 100 - (step * 2))
    
    # Internal SoC temperature (Celsius)
    temp_c = round(36.5 + (step * 0.4) + random.uniform(-0.2, 0.2), 1)
    
    # Edge threshold logic: Flag alarm if vibration exceeds 1.20g
    alert_flag = 1 if vibration_rms > 1.20 else 0
    
    return {
        "field1": vibration_rms,
        "field2": battery_pct,
        "field3": temp_c,
        "field4": alert_flag
    }

def post_to_thingspeak(payload: dict) -> bool:
    """Executes HTTP GET request against ThingSpeak REST API."""
    url = "https://api.thingspeak.com/update"
    params = {
        "api_key": THINGSPEAK_WRITE_KEY,
        **payload
    }
    
    try:
        start_time = time.time()
        response = requests.get(url, params=params, timeout=5.0)
        elapsed_ms = round((time.time() - start_time) * 1000, 1)
        
        if response.status_code == 200:
            entry_id = response.text.strip()
            if entry_id == "0":
                print(f"[REJECTED] HTTP 200 received, but Entry ID is 0. Rate limit violated!")
                return False
            else:
                print(f"[SUCCESS] Ingested -> Entry ID: {entry_id} (Server Latency: {elapsed_ms} ms)")
                return True
        else:
            print(f"[ERROR] HTTP Failed with status {response.status_code}: {response.text}")
            return False
            
    except requests.exceptions.RequestException as exc:
        print(f"[NETWORK ERROR] Failed to connect to ThingSpeak: {exc}")
        return False

def main():
    print("=" * 70)
    print("  THINGSPEAK DEDICATED TELEMETRY PIPELINE TEST")
    print(f"  Target: api.thingspeak.com | Interval: {UPDATE_INTERVAL_SEC}s")
    print("=" * 70)
    
    for cycle in range(1, CYCLES_TO_RUN + 1):
        print(f"\n[Cycle {cycle}/{CYCLES_TO_RUN}] Polling simulated edge sensors...")
        data = generate_telemetry_frame(cycle)
        
        print(f"  - Vibration RMS  (Field 1): {data['field1']} g")
        print(f"  - Battery Level  (Field 2): {data['field2']} %")
        print(f"  - Internal Temp  (Field 3): {data['field3']} °C")
        print(f"  - Anomaly Alert  (Field 4): {data['field4']}")
        
        success = post_to_thingspeak(data)
        
        if cycle < CYCLES_TO_RUN:
            print(f"  Waiting {UPDATE_INTERVAL_SEC} seconds to comply with rate limits...")
            for remaining in range(UPDATE_INTERVAL_SEC, 0, -1):
                print(f"\r  Cooldown: {remaining:02d}s remaining...", end="", flush=True)
                time.sleep(1)
            print("\r" + " " * 40 + "\r", end="", flush=True)

    print("\n" + "=" * 70)
    print("ThingSpeak test completed. Check your charts at: https://thingspeak.com")
    print("=" * 70)

if __name__ == "__main__":
    main()

```

---

## 3. Pipeline B: Google Forms Setup & Dedicated Validation

Google Forms operates as an open HTTPS endpoint that pipes inputs directly into a persistent Google Sheet.

### Step 3.1: Form Creation & Permissions

1. Go to [Google Forms](https://forms.google.com/) and create a new blank form.
2. Form Title: `M5StickS3 Enterprise Asset Tracker`.
3. Create four **Short answer** questions:
* Question 1: `Device_UUID`
* Question 2: `Vibration_G`
* Question 3: `System_Health`
* Question 4: `Audit_Remark`


4. **Permissions Check:** Under **Settings** $\rightarrow$ **Responses**:
* Turn **OFF** "Limit to 1 response".
* Turn **OFF** "Restrict to users in [Domain]" (if using an organizational account).



### Step 3.2: Link to Google Sheets

1. Switch to the **Responses** tab in Google Forms.
2. Click **Link to Sheets** $\rightarrow$ **Create a new spreadsheet** $\rightarrow$ click **Create**.
3. Keep the newly created Google Sheet open in your browser to inspect live rows as they arrive.

### Step 3.3: Extracting Form Endpoint & Entry Field Tokens

1. In the Google Form editor, click the **three dots** (top right) $\rightarrow$ **Get pre-filled link**.
2. Type these markers into each field:
* `Device_UUID` $\rightarrow$ `MARKER_UUID`
* `Vibration_G` $\rightarrow$ `MARKER_VIB`
* `System_Health` $\rightarrow$ `MARKER_HEALTH`
* `Audit_Remark` $\rightarrow$ `MARKER_REMARK`


3. Click **Get link** $\rightarrow$ **Copy link**.
4. Paste the URL into your text editor:
```text
https://docs.google.com/forms/d/e/1FAIpQLSeXyZ_987654321/viewform?usp=pp_url&entry.102938475=MARKER_UUID&entry.584930291=MARKER_VIB&entry.774839201=MARKER_HEALTH&entry.192837465=MARKER_REMARK

```


5. Extract the two necessary parameters:
* **Endpoint Target:** Replace `/viewform` with `/formResponse`:
```text
https://docs.google.com/forms/d/e/1FAIpQLSeXyZ_987654321/formResponse

```


* **Field Mapping:**
* `entry.102938475` $\rightarrow$ Device UUID
* `entry.584930291` $\rightarrow$ Vibration
* `entry.774839201` $\rightarrow$ System Health
* `entry.192837465` $\rightarrow$ Audit Remark





---

### Step 3.4: Dedicated Python Script: `test_googleform.py`

This standalone script formats an `application/x-www-form-urlencoded` POST request and injects telemetry directly into Google Sheets. Notice that unlike ThingSpeak, you can safely send multiple packets in quick succession (e.g., every 2 seconds).

Create `tools/test_googleform.py`:

```python
#!/usr/bin/env python3
"""
===============================================================================
M5Stack Stick S3 Journey - Lesson 02: Pipeline B
Dedicated Google Forms Headless Ingestion Test Harness
===============================================================================
"""

import time
import random
import requests

# -----------------------------------------------------------------------------
# USER CONFIGURATION: Replace with your extracted parameters
# -----------------------------------------------------------------------------
GFORM_ENDPOINT = "https://docs.google.com/forms/d/e/YOUR_FORM_ID/formResponse"

ENTRY_MAP = {
    "device_uuid":   "entry.102938475",
    "vibration_g":   "entry.584930291",
    "system_health": "entry.774839201",
    "audit_remark":  "entry.192837465"
}

TRANSMISSION_DELAY_SEC = 2  # Google Forms does not require a 15-second wait
BATCH_SIZE = 3
# -----------------------------------------------------------------------------

def generate_fleet_record(index: int) -> dict:
    """Generates synthetic fleet health data."""
    vib = round(random.uniform(0.75, 1.85), 3)
    health = "CRITICAL" if vib > 1.50 else ("WARNING" if vib > 1.20 else "OPTIMAL")
    remarks = [
        "Routine_Edge_Ping",
        "Harmonic_Resonance_Detected",
        "Bearing_Wear_Suspected",
        "Post_Maintenance_Check"
    ]
    
    return {
        "uuid": f"M5STICK_S3_NODE_{index:02d}",
        "vib": str(vib),
        "health": health,
        "remark": random.choice(remarks)
    }

def post_to_google_form(record: dict) -> bool:
    """Executes an HTTPS POST submission directly into the Form endpoint."""
    payload = {
        ENTRY_MAP["device_uuid"]:   record["uuid"],
        ENTRY_MAP["vibration_g"]:   record["vib"],
        ENTRY_MAP["system_health"]: record["health"],
        ENTRY_MAP["audit_remark"]:  record["remark"]
    }
    
    headers = {
        "Content-Type": "application/x-www-form-urlencoded"
    }
    
    try:
        start_time = time.time()
        response = requests.post(GFORM_ENDPOINT, data=payload, headers=headers, timeout=5.0)
        elapsed_ms = round((time.time() - start_time) * 1000, 1)
        
        # Google Forms responds with 200 (Success) or 302 (Redirect to acknowledgment)
        if response.status_code in [200, 302]:
            print(f"[SUCCESS] Appended row to Google Sheet (Status: {response.status_code}, Time: {elapsed_ms} ms)")
            return True
        else:
            print(f"[FAILED] Submission rejected with HTTP status: {response.status_code}")
            return False
            
    except requests.exceptions.RequestException as exc:
        print(f"[NETWORK ERROR] Failed to connect to Google Endpoint: {exc}")
        return False

def main():
    print("=" * 70)
    print("  GOOGLE FORMS HEADLESS INGESTION PIPELINE TEST")
    print(f"  Target: {GFORM_ENDPOINT[:60]}...")
    print("=" * 70)
    
    for batch_idx in range(1, BATCH_SIZE + 1):
        print(f"\n[Batch {batch_idx}/{BATCH_SIZE}] Constructing Form Payload...")
        record = generate_fleet_record(batch_idx)
        
        print(f"  - Device UUID   : {record['uuid']}")
        print(f"  - Vibration (G) : {record['vib']}")
        print(f"  - System Health : {record['health']}")
        print(f"  - Audit Remark  : {record['remark']}")
        
        post_to_google_form(record)
        
        if batch_idx < BATCH_SIZE:
            time.sleep(TRANSMISSION_DELAY_SEC)

    print("\n" + "=" * 70)
    print("Google Forms test completed. Check your connected Google Sheet!")
    print("=" * 70)

if __name__ == "__main__":
    main()

```

---

## 4. Execution & Expected Console Outputs

Run each script independently in separate terminal executions to confirm clear, uncoupled results:

### Test 1: Run ThingSpeak Pipeline

```bash
python3 tools/test_thingspeak.py

```

```text
======================================================================
  THINGSPEAK DEDICATED TELEMETRY PIPELINE TEST
  Target: api.thingspeak.com | Interval: 16s
======================================================================

[Cycle 1/3] Polling simulated edge sensors...
  - Vibration RMS  (Field 1): 1.018 g
  - Battery Level  (Field 2): 98 %
  - Internal Temp  (Field 3): 36.8 °C
  - Anomaly Alert  (Field 4): 0
[SUCCESS] Ingested -> Entry ID: 142 (Server Latency: 320.4 ms)
  Waiting 16 seconds to comply with rate limits...

```

### Test 2: Run Google Forms Pipeline

```bash
python3 tools/test_googleform.py

```

```text
======================================================================
  GOOGLE FORMS HEADLESS INGESTION PIPELINE TEST
  Target: https://docs.google.com/forms/d/e/1FAIpQLSe...
======================================================================

[Batch 1/3] Constructing Form Payload...
  - Device UUID   : M5STICK_S3_NODE_01
  - Vibration (G) : 0.941
  - System Health : OPTIMAL
  - Audit Remark  : Routine_Edge_Ping
[SUCCESS] Appended row to Google Sheet (Status: 200, Time: 285.1 ms)

[Batch 2/3] Constructing Form Payload...
  - Device UUID   : M5STICK_S3_NODE_02
  - Vibration (G) : 1.624
  - System Health : CRITICAL
  - Audit Remark  : Bearing_Wear_Suspected
[SUCCESS] Appended row to Google Sheet (Status: 200, Time: 210.6 ms)

```

---

## 5. C++ Header Structuring for Future Firmware (`secrets.h`)

To maintain clean separation when programming the ESP32-S3 in Lesson 5, structure your keys into isolated namespaces.

Create `include/secrets.example.h`:

```cpp
/**
 * ============================================================================
 * Configuration Template: Cloud Ingestion Credentials
 * Action: Copy this file to 'secrets.h' and fill in your actual credentials.
 * ============================================================================
 */

#ifndef SECRETS_H
#define SECRETS_H

// Network Credentials
namespace NetworkConfig {
    const char* WIFI_SSID     = "YOUR_WIFI_SSID_2.4G";
    const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
}

// Pipeline A: ThingSpeak Configuration
namespace ThingSpeakConfig {
    const char* WRITE_API_KEY = "YOUR_16_CHAR_WRITE_KEY";
    const unsigned long MIN_INTERVAL_MS = 15000;
}

// Pipeline B: Google Forms Configuration
namespace GoogleFormConfig {
    const char* FORM_URL    = "https://docs.google.com/forms/d/e/YOUR_FORM_ID/formResponse";
    const char* ENTRY_UUID  = "entry.102938475";
    const char* ENTRY_VIB   = "entry.584930291";
    const char* ENTRY_STATE = "entry.774839201";
    const char* ENTRY_NOTES = "entry.192837465";
}

#endif // SECRETS_H

```

---

## 6. Pipeline Diagnostic Matrix

| Symptom | Affected Pipeline | Primary Cause | Resolution |
| --- | --- | --- | --- |
| **Server returns `0**` | ThingSpeak | Breached the 15-second rate limit. | Ensure `UPDATE_INTERVAL_SEC >= 16` in code. |
| **HTTP `400 Bad Request**` | Google Forms | Form URL contains `/viewform` or bad entry keys. | Ensure URL ends with `/formResponse` and keys strictly follow `entry.123456789`. |
| **HTTP `401 / 403 Forbidden**` | Google Forms | Account domain policy requires authentication. | Uncheck **"Restrict to users in..."** in Google Form settings. |
| **Missing entries in Sheet** | Google Forms | Form is not linked to a target spreadsheet. | In Forms $\rightarrow$ Responses tab, click **Link to Sheets**. |
| **Values placed in wrong columns** | ThingSpeak | Inverted `fieldX` parameters in query string. | Verify `field1` to `field4` match your configured channel schema. |

---

## 7. Hands-On Verification Lab

Before proceeding to Lesson 3, complete these tasks:

* [ ] **Task 2.1:** Execute `python3 tools/test_thingspeak.py` and confirm 3 consecutive points appear in your ThingSpeak Private View graphs.
* [ ] **Task 2.2:** Execute `python3 tools/test_googleform.py` and confirm 3 new rows appear in your linked Google Sheet.
* [ ] **Task 2.3:** Apply a conditional color scale in Google Sheets to automatically flag high vibration values in column B ($> 1.20\text{ G}$).

---

## 8. Version Control Protocol

Commit the separated testing tools to your repository:

```bash
# Ensure secrets are untracked
echo "include/secrets.h" >> .gitignore

# Stage separate test scripts and template
git add tools/test_thingspeak.py tools/test_googleform.py include/secrets.example.h .gitignore

# Commit
git commit -m "feat(lesson02): provide isolated test harnesses for thingspeak and google form ingestion"

```

---
