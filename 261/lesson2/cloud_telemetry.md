# Lesson 02: Cloud Telemetry Ingestion — ThingSpeak IoT & Google Forms Reverse Engineering

This module guides you through configuring and validating dual cloud-ingestion pipelines for edge IoT fleets: **ThingSpeak** (a specialized IoT Platform-as-a-Service by MathWorks) and **Google Forms/Sheets** (a serverless, zero-cost operational data ledger).

Before transmitting data from physical microcontrollers, you must establish and verify these cloud endpoints using software-based API testing tools to isolate networking issues from firmware bugs.

```
                    CLOUD INGESTION TIER ARCHITECTURE
                    
                        +----------------------------+
                        |  M5Stack Stick S3 (Edge)   |
                        +----------------------------+
                                      |
                 +--------------------+--------------------+
                 | HTTPS / TLS 1.2                         | HTTPS / TLS 1.2
                 v                                         v
   +---------------------------+             +---------------------------+
   |    ThingSpeak IoT PaaS    |             |   Google Cloud Endpoint   |
   | (Time-Series Telemetry)   |             |  (Serverless Form Logger) |
   +---------------------------+             +---------------------------+
                 |                                         |
                 v                                         v
   +---------------------------+             +---------------------------+
   | - Real-time Field Charts  |             | - Google Forms Response   |
   | - MATLAB Signal Analytics |             | - Google Sheets Database  |
   | - React & Email Alerts    |             | - Business KPI Dashboard  |
   +---------------------------+             +---------------------------+

```

---

## 1. Cloud Architecture & Enterprise Ingestion Trade-Offs

Choosing an IoT data ingestion pattern requires balancing development velocity, throughput, analytical capacity, and operational cost:

| Evaluation Metric | ThingSpeak (MathWorks PaaS) | Google Forms / Sheets (SaaS Pipeline) |
| --- | --- | --- |
| **Primary Ingestion Protocol** | HTTP REST (`GET` / `POST`), MQTT | HTTPS `POST` (`application/x-www-form-urlencoded`) |
| **Free Tier Ingestion Rate** | 1 entry per **15 seconds** (max 3M msgs/yr) | ~1 entry per **1–2 seconds** (governed by Google quotas) |
| **Data Payload Format** | URL Query parameters / JSON | URL-encoded Form body parameters |
| **Built-in Analytics** | Native MATLAB analytics & visualizations | Google Sheets formulas, Apps Script, Looker Studio |
| **Authentication Scheme** | Static Write/Read API Keys | Open headless Form ID + hidden input field tokens |
| **Optimal Business Role** | High-frequency physical machine telemetry | Operational shift reporting, low-frequency audit logging |

---

## 2. Module A: ThingSpeak IoT Setup & API Provisioning

ThingSpeak provides hosted time-series storage designed for sensor logging, featuring built-in charting and integration with MATLAB for signal processing.

### Step 2.1: Channel Creation

1. Navigate to [ThingSpeak.com](https://thingspeak.com/) and sign in with a MathWorks account.
2. Select **Channels** $\rightarrow$ **My Channels** $\rightarrow$ **New Channel**.
3. Configure the channel metadata:
* **Name:** `M5StickS3_Industrial_Fleet`
* **Description:** `Vibration, battery health, and operational diagnostics stream.`


4. Enable and label the following fields:
* `Field 1`: `Vibration_RMS_G` (Vector acceleration magnitude)
* `Field 2`: `Battery_Level_Pct` (PMIC battery fuel gauge)
* `Field 3`: `Hardware_Temperature_C` (Internal SoC temperature)
* `Field 4`: `Audit_Trigger_Code` (Manual physical button state)


5. Scroll down and click **Save Channel**.

### Step 2.2: Extract API Credentials

1. Click the **API Keys** tab on your newly created channel.
2. Record the following values in your workspace configuration notes:
* **Channel ID:** (e.g., `2847192`)
* **Write API Key:** (16-character alphanumeric string, e.g., `WXYZ1234ABCD5678`)
* **Read API Key:** (Used in later modules for data retrieval)



> **Security Notice:** The **Write API Key** grants write access to your cloud channel. Never commit raw API keys to public version control repositories.

### Step 2.3: Headless Endpoint Verification via Terminal (cURL)

Before writing microcontroller firmware, verify that your channel accepts data over raw HTTP. Open your terminal and issue an HTTP request containing dummy telemetry:

#### macOS / Linux / Windows PowerShell:

```bash
# Ingest synthetic telemetry into Fields 1, 2, 3, and 4
curl -X GET "https://api.thingspeak.com/update?api_key=YOUR_WRITE_API_KEY&field1=0.98&field2=87&field3=38.5&field4=1"

```

#### Verification Checkpoint:

* The ThingSpeak server returns a positive integer representing the **Entry ID** (e.g., `1`, `2`, `3...`).
* If the terminal returns `0`, your request was throttled by the 15-second rate limit or the API key is incorrect.
* Refresh your ThingSpeak channel dashboard in your browser. Verify that points appear on all 4 channel graphs.

---

## 3. Module B: Google Forms Telemetry Ingestion (Headless Endpoint)

Google Forms is designed for human input through a web browser, but its underlying submission engine uses standard HTTP POST requests. By analyzing the form schema, you can use Google Forms as an authenticated cloud ingress API that pipes edge telemetry into Google Sheets.

```
                  GOOGLE FORMS INGESTION FLOW
                  
   M5Stick S3                 Google Ingress Engine               Google Sheets
  [Edge Node]                   [Cloud Serverless]              [Persistent Table]
       |                                |                               |
       |  HTTPS POST /formResponse      |                               |
       |  entry.X=Node01&entry.Y=1.04   |                               |
       |------------------------------->|                               |
       |                                | Parse payload & append row    |
       |                                |------------------------------>|
       |  HTTP 200 OK / 302 Redirect    |                               |
       |<-------------------------------|                               |

```

### Step 3.1: Form Configuration

1. Open [Google Forms](https://forms.google.com/) and create a new blank form.
2. Set the Title to: `M5StickS3 Fleet Data Lake`.
3. Create five **Short answer** questions:
* Question 1: `Device_UUID`
* Question 2: `Vibration_RMS`
* Question 3: `Battery_Level`
* Question 4: `Operational_State`
* Question 5: `Diagnostic_Notes`


4. In the top settings bar, verify that **Collect email addresses** and **Limit to 1 response** are **disabled** so headless programmatic submissions can proceed without browser authentication.

### Step 3.2: Extracting Underlying Form Entry IDs

Google Forms maps each input to an internal identifier (e.g., `entry.184729104`). To find these identifiers:

1. Click the **three vertical dots** in the top-right corner $\rightarrow$ select **Get pre-filled link**.
2. Type distinctive placeholder text into each field:
* `Device_UUID` $\rightarrow$ `VAL_UUID`
* `Vibration_RMS` $\rightarrow$ `VAL_VIB`
* `Battery_Level` $\rightarrow$ `VAL_BAT`
* `Operational_State` $\rightarrow$ `VAL_STATE`
* `Diagnostic_Notes` $\rightarrow$ `VAL_NOTES`


3. Click **Get link** at the bottom, then click **Copy link** from the toast notification.
4. Paste the URL into a text editor. It will look like this:
```text
https://docs.google.com/forms/d/e/1FAIpQLScXyzEXAMPLE9876543210/viewform?usp=pp_url&entry.104523912=VAL_UUID&entry.839201441=VAL_VIB&entry.471928411=VAL_BAT&entry.193847261=VAL_STATE&entry.901827364=VAL_NOTES

```



### Step 3.3: Constructing the Headless Ingestion URL

To convert this human-facing link into a machine-facing ingress endpoint:

1. Replace `/viewform` with `/formResponse`.
2. Map the extracted `entry.XXXXXXXXX` IDs:

```text
Form Response Target: 
https://docs.google.com/forms/d/e/1FAIpQLScXyzEXAMPLE9876543210/formResponse

Extracted Parameters:
- Device_UUID        -> entry.104523912
- Vibration_RMS      -> entry.839201441
- Battery_Level      -> entry.471928411
- Operational_State  -> entry.193847261
- Diagnostic_Notes   -> entry.901827364

```

### Step 3.4: Headless Validation via Terminal (cURL)

Submit synthetic operational data to the endpoint using an HTTP POST request:

```bash
curl -s -o /dev/null -w "%{http_code}" -X POST \
  "https://docs.google.com/forms/d/e/YOUR_FORM_ID_HERE/formResponse" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "entry.104523912=M5StickS3_SIM_01" \
  -d "entry.839201441=0.042" \
  -d "entry.471928411=96" \
  -d "entry.193847261=NOMINAL" \
  -d "entry.901827364=Initial_API_Ingress_Verification"

```

#### Verification Checkpoint:

* The command returns an HTTP status code:
* `200` (Success - Form submission acknowledged).
* `302` (Redirect - Form submission processed; Google redirects to confirmation page).


* If an error occurs (`400` or `404`), verify that the Form ID and entry numbers match your form link.
* Go to the **Responses** tab in Google Forms. Click **Link to Sheets** $\rightarrow$ **Create a new spreadsheet**.
* Open the Google Sheet and confirm that the timestamp, device ID, and telemetry fields populate automatically in real time.

---

## 4. Cross-Platform Automated Validation Script

To verify both cloud endpoints before writing ESP32-S3 firmware, use this Python test harness to emulate an active edge device streaming telemetry over time.

Create a verification script on your computer: `tools/cloud_validation_harness.py`.

```python
#!/usr/bin/env python3
#"""
#M5Stack Stick S3 Journey - Lesson 02: Cloud Telemetry Test Harness
#Emulates an edge device sending time-series data to ThingSpeak and Google Forms.
#"""

import time
import math
import random
import requests

# =====================================================================
# USER CONFIGURATION: Paste your validated keys and endpoints below
# =====================================================================
THINGSPEAK_WRITE_API_KEY = "YOUR_THINGSPEAK_WRITE_KEY"
GOOGLE_FORM_ENDPOINT = "https://docs.google.com/forms/d/e/YOUR_FORM_ID/formResponse"

GOOGLE_FORM_ENTRIES = {
    "device_id": "entry.104523912",
    "vibration": "entry.839201441",
    "battery": "entry.471928411",
    "status": "entry.193847261",
    "notes": "entry.901827364"
}
# =====================================================================

DEVICE_UUID = "M5S3_EMULATOR_MAC_7C02"

def generate_telemetry(step_index: int):
    """Generates synthetic physics telemetry."""
    vibration = round(1.0 + 0.3 * math.sin(step_index * 0.5) + random.uniform(-0.05, 0.05), 3)
    battery = max(10, 100 - (step_index * 2))
    soc_temp = round(35.0 + random.uniform(0.5, 3.2), 1)
    status = "ALERT_VIB" if vibration > 1.25 else "NORMAL"
    notes = "Simulated_Edge_Drift" if status == "ALERT_VIB" else "Steady_State"
    
    return {
        "vib": vibration,
        "bat": battery,
        "temp": soc_temp,
        "status": status,
        "notes": notes
    }

def push_thingspeak(data: dict):
    url = "https://api.thingspeak.com/update"
    params = {
        "api_key": THINGSPEAK_WRITE_API_KEY,
        "field1": data["vib"],
        "field2": data["bat"],
        "field3": data["temp"],
        "field4": 1 if data["status"] == "ALERT_VIB" else 0
    }
    try:
        res = requests.get(url, params=params, timeout=5)
        if res.status_code == 200 and res.text != "0":
            print(f"[ThingSpeak] Ingested Entry ID: {res.text.strip()}")
        else:
            print(f"[ThingSpeak] Throttled or Rejected (Response: {res.text})")
    except Exception as e:
        print(f"[ThingSpeak] Transport Error: {e}")

def push_google_form(data: dict):
    payload = {
        GOOGLE_FORM_ENTRIES["device_id"]: DEVICE_UUID,
        GOOGLE_FORM_ENTRIES["vibration"]: str(data["vib"]),
        GOOGLE_FORM_ENTRIES["battery"]: str(data["bat"]),
        GOOGLE_FORM_ENTRIES["status"]: data["status"],
        GOOGLE_FORM_ENTRIES["notes"]: data["notes"]
    }
    headers = {"Content-Type": "application/x-www-form-urlencoded"}
    try:
        res = requests.post(GOOGLE_FORM_ENDPOINT, data=payload, headers=headers, timeout=5)
        if res.status_code in [200, 302]:
            print(f"[GoogleForm] Successfully posted response (HTTP {res.status_code})")
        else:
            print(f"[GoogleForm] Failed with HTTP {res.status_code}")
    except Exception as e:
        print(f"[GoogleForm] Transport Error: {e}")

def main():
    print("=" * 60)
    print("M5STACK STICK S3: CLOUD INGESTION BENCHMARK HARNESS")
    print("=" * 60)
    
    for cycle in range(1, 4):
        print(f"\n[CYCLE {cycle}/3] Generating Telemetry Batch...")
        telemetry = generate_telemetry(cycle)
        print(f" -> Telemetry: {telemetry}")
        
        push_thingspeak(telemetry)
        push_google_form(telemetry)
        
        if cycle < 3:
            print("[THROTTLING] Sleeping 16 seconds to respect ThingSpeak rate limit...")
            time.sleep(16)

    print("\n[COMPLETE] Cloud pipeline verified. Verify live updates in your browser.")

if __name__ == "__main__":
    main()

```

### Execution

Run the verification script from your local workstation:

```bash
python3 tools/cloud_validation_harness.py

```

---

## 5. Structuring Credentials for Future C++ Firmware

To ensure secrets (Wi-Fi passwords, API keys, Form IDs) are never pushed to public Git repositories, establish a configuration template now. This header file will be used in Lesson 5 when deploying firmware to physical hardware.

Create `include/secrets.example.h`:

```cpp
/**
 * ============================================================================
 * Configuration Template: Cloud Ingestion Credentials
 * Action: Copy this file to 'secrets.h' and fill in your actual credentials.
 * Warning: Never commit 'secrets.h' to public version control!
 * ============================================================================
 */

#ifndef SECRETS_H
#define SECRETS_H

// Local Wi-Fi Credentials
const char* WIFI_SSID     = "YOUR_WIFI_SSID_2.4G";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ThingSpeak Configuration
const char* THINGSPEAK_API_KEY = "YOUR_16_CHAR_WRITE_KEY";

// Google Forms Ingestion Parameters
const char* GFORM_URL = "https://docs.google.com/forms/d/e/YOUR_FORM_ID/formResponse";
const char* GENTRY_DEVICE_ID = "entry.104523912";
const char* GENTRY_VIBRATION = "entry.839201441";
const char* GENTRY_BATTERY   = "entry.471928411";
const char* GENTRY_STATUS    = "entry.193847261";

#endif // SECRETS_H

```

---

## 6. Troubleshooting & Edge Cases

| Error / Symptom | Root Cause | Resolution |
| --- | --- | --- |
| **ThingSpeak returns `0` on HTTP `GET**` | Rapid requests submitted within the 15-second rate-limit window. | Enforce an edge timer ($\ge 15000\text{ ms}$) between consecutive posts using `millis()`. |
| **Google Form returns HTTP `400 Bad Request**` | Data type mismatch or missing a required field. | Ensure all required fields receive valid strings, or disable the "Required" toggle in Form edit mode. |
| **Google Form returns HTTP `401 Unauthorized**` | Form is restricted to users within a specific Google Workspace domain. | In Form Settings $\rightarrow$ Responses: Disable **"Restrict to users in [Domain]"**. |
| **ThingSpeak chart shows zero points despite `200 OK**` | Data was mapped to an invalid field parameter name (e.g., `Field1` instead of lowercase `field1`). | Ensure URL query parameters use strict lowercase formatting: `field1`, `field2`, etc. |

---

## 7. Hands-On Verification Lab

Complete the following verification steps before advancing to Lesson 3:

* [ ] **Task 2.1:** Configure a custom MATLAB visualization widget on your ThingSpeak channel to display an alert banner when `Field 1` exceeds $1.20\text{ G}$.
* [ ] **Task 2.2:** Add conditional formatting in your linked Google Sheet: highlight rows in red if `Operational_State` equals `"ALERT_VIB"`.
* [ ] **Task 2.3:** Run `tools/cloud_validation_harness.py` and confirm that all 3 test cycles append new data to both platforms.

---

## 8. Version Control Commit

Stage the test harness and credential template while keeping actual credentials ignored:

```bash
# Append secrets.h to .gitignore
echo "include/secrets.h" >> .gitignore

# Stage verification assets
git add tools/cloud_validation_harness.py include/secrets.example.h .gitignore

# Commit changes
git commit -m "feat(lesson02): establish thingspeak channel, reverse-engineer google forms endpoint, and add test harness"

```
