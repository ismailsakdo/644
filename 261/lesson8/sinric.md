# Lab Exercise: Cloud-to-Edge IoT Provisioning & Remote Asset Control
**Course:** IoT for Business (MBA)  
**Hardware:** M5Stack Stick S3 (`ESP32-S3-PICO-1-N8R8`)  
**Cloud Platform:** Sinric Pro (Custom Device / Elements)  

---

## Demo Video (Student Submission — Fill Before Submitting)
* **Student Name:** `[Your Full Name]`
* **Video Link:** `[Paste Unlisted YouTube / Google Drive / Loom URL Here]`

---

## 1. Executive Summary & Business Context

In commercial IoT deployments—such as smart retail signage, hotel room automation, warehouse status indicators, or industrial equipment monitoring—two operational bottlenecks determine whether a solution can scale profitably:

1. **Zero-Touch Field Provisioning (SoftAP):** Hardcoding Wi-Fi credentials into firmware is non-viable at scale. Commercial hardware must boot into a **Software-Enabled Access Point (SoftAP)** so non-technical field technicians or end customers can configure local network credentials via a captive web portal.
2. **Custom Control Interfaces & Bidirectional Sync (Sinric Pro Elements):** Commercial devices rarely fit rigid consumer smart-home templates. Using modular UI elements (Power, Toggle, Range Sliders) allows product managers to build tailored operational dashboards while maintaining real-time bidirectional state synchronization between cloud operators and physical edge controls.

In this lab, you will build, provision, and demonstrate a cloud-connected display controller on the **M5Stack Stick S3** that toggles screen power and adjusts backlight brightness remotely via **Sinric Pro Elements**, while syncing physical button presses back to the cloud.

---

## 2. Learning Objectives

Upon completing this exercise, you will be able to:
* Configure a PlatformIO firmware project for ESP32-S3 (`qio_opi` flash/PSRAM) with an mbedTLS 4.0 HMAC compatibility shim.
* Implement a **SoftAP captive portal** (`WiFiManager`) to separate local network credentials from compiled firmware.
* Architect a custom IoT device model using **Sinric Pro 5.x Elements** (`PowerStateController`, `ToggleController`, and `RangeController`).
* Validate bidirectional **Cloud-to-Edge** and **Edge-to-Cloud** telemetry synchronization with live LCD status feedback.
* Evaluate the commercial implications of provisioning friction, credential security, and power management in an enterprise IoT rollout.

---

## 3. Prerequisites & Repository Structure

### Hardware
* **M5Stack Stick S3** (ESP32-S3 Dual-Core Xtensa LX7, 8MB Flash / 8MB OPI PSRAM) *(or M5StickC / M5StickC Plus fallback)*
* **USB-C Data Cable** (must support data transfer, not charging only)
* **Smartphone or Laptop** with 2.4 GHz Wi-Fi capability (for SoftAP provisioning)

### Software & Accounts
* **Visual Studio Code** with the **PlatformIO IDE** extension installed
* **Sinric Pro Account** (Free tier at [portal.sinric.pro](https://portal.sinric.pro/))
* **Git & GitHub Account** to fork/submit your repository and video link

### Project File Layout
Ensure your repository matches this structure:

```text
├── include/
│   └── mbedtls_hmac_shim.h   # mbedTLS 4.0 HMAC compatibility header for SinricPro
├── src/
│   └── main.cpp              # SoftAP + SinricPro Elements + M5Unified LCD firmware
├── platformio.ini            # PlatformIO build & board configuration
├── README.md                 # Lab instructions & video demo submission link
└── REFLECTION.md             # MBA strategic business analysis answers
```

---

## 4. Step 1: Sinric Pro Elements Cloud Setup

1. **Log in to Sinric Pro:** Navigate to [portal.sinric.pro](https://portal.sinric.pro/) and sign in.
2. **Retrieve API Credentials:**
   * Go to **Credentials** in the left sidebar.
   * Copy your **App Key** and **App Secret** (keep these private; never commit real keys to a public GitHub repository).
3. **Create a Custom Device (Elements):**
   * Go to **Devices** -> **Add Device**.
   * Set **Device Name** to `M5StickS3 Display Controller`.
   * Set **Device Type** to **Custom** (Elements).
   * Assign a room/group and save the device. Copy the generated **Device ID**.
4. **Configure Your Elements Layout:**
   * Open the **Elements / Device UI Designer** for your newly created custom device.
   * Add a **Power** element and/or a **Toggle** element to control screen ON/OFF state.
   * *(Optional)* Add a **Range / Slider** element configured from `0` to `100` to control screen brightness.
   * Note the **Element Instance IDs** shown in the element properties panel (typically `toggleInstance1` for the first toggle and `rangeInstance1` for the first slider).

---

## 5. Step 2: Project Configuration & Firmware

### A. Configure `platformio.ini`

Replace your project's `platformio.ini` with the configuration below. It uses the standard `espressif32` platform, configures the Stick S3's 8MB flash and OPI PSRAM (`qio_opi`), enables USB-CDC serial output, and injects `include/mbedtls_hmac_shim.h` during compilation.

```ini
[platformio]
default_envs = m5stack-s3

[env]
framework = arduino
monitor_speed = 115200
lib_deps =
    m5stack/M5Unified
    m5stack/M5GFX
    sinricpro/SinricPro
    bblanchon/ArduinoJson
    links2004/WebSockets
    tzapu/WiFiManager

; ====================================================================
; Environment 1: M5Stack Stick S3 (ESP32-S3-PICO-1-N8R8)
; ====================================================================
[env:m5stack-s3]
platform = espressif32
board = esp32-s3-devkitc-1
upload_speed = 921600
board_build.arduino.partitions = default_8MB.csv
board_build.arduino.memory_type = qio_opi

build_flags =
    -std=gnu++17
    -DESP32S3
    -DBOARD_HAS_PSRAM
    -DARDUINO_M5STACK_STICK_S3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -include include/mbedtls_hmac_shim.h
    -O2

; ====================================================================
; Environment 2: M5StickC / M5StickC Plus (ESP32 Classic Fallback)
; ====================================================================
[env:m5stick-plus]
platform = espressif32
board = m5stick-c
upload_speed = 1500000

build_flags =
    -include include/mbedtls_hmac_shim.h
```

### B. Create `include/mbedtls_hmac_shim.h`

Newer `espressif32` toolchains ship with **mbedTLS 4.0**, which removed legacy `mbedtls_md_hmac_*` helper functions that `SinricProSignature.cpp` expects. Create `include/mbedtls_hmac_shim.h` with the exact RFC-2104 HMAC-SHA256 shim below:

```c
#pragma once
#include <string.h>
#include <mbedtls/md.h>

#ifdef __cplusplus
extern "C" {
#endif

static unsigned char _shim_o_key_pad[64];
static mbedtls_md_context_t _shim_sha_ctx;
static int _shim_sha_initialized = 0;

static inline int mbedtls_md_hmac_starts(mbedtls_md_context_t *ctx, const unsigned char *key, size_t keylen) {
    (void)ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!_shim_sha_initialized) {
        mbedtls_md_init(&_shim_sha_ctx);
        mbedtls_md_setup(&_shim_sha_ctx, info, 0);
        _shim_sha_initialized = 1;
    }

    unsigned char k_pad[64];
    unsigned char i_key_pad[64];
    memset(k_pad, 0, 64);

    if (keylen > 64) {
        mbedtls_md_starts(&_shim_sha_ctx);
        mbedtls_md_update(&_shim_sha_ctx, key, keylen);
        mbedtls_md_finish(&_shim_sha_ctx, k_pad);
    } else {
        memcpy(k_pad, key, keylen);
    }

    for (int i = 0; i < 64; i++) {
        i_key_pad[i] = k_pad[i] ^ 0x36;
        _shim_o_key_pad[i] = k_pad[i] ^ 0x5c;
    }

    mbedtls_md_starts(&_shim_sha_ctx);
    return mbedtls_md_update(&_shim_sha_ctx, i_key_pad, 64);
}

static inline int mbedtls_md_hmac_update(mbedtls_md_context_t *ctx, const unsigned char *input, size_t ilen) {
    (void)ctx;
    return mbedtls_md_update(&_shim_sha_ctx, input, ilen);
}

static inline int mbedtls_md_hmac_finish(mbedtls_md_context_t *ctx, unsigned char *output) {
    (void)ctx;
    unsigned char inner_hash[32];
    mbedtls_md_finish(&_shim_sha_ctx, inner_hash);
    mbedtls_md_starts(&_shim_sha_ctx);
    mbedtls_md_update(&_shim_sha_ctx, _shim_o_key_pad, 64);
    mbedtls_md_update(&_shim_sha_ctx, inner_hash, 32);
    return mbedtls_md_finish(&_shim_sha_ctx, output);
}

#ifdef __cplusplus
}
#endif
```

### C. Configure `src/main.cpp`

Paste the firmware below into `src/main.cpp` and insert your own `APP_KEY`, `APP_SECRET`, and `DEVICE_ID` locally before flashing.

> **Security Rule for GitHub Submission:** Before committing and pushing your code to GitHub, replace your live `APP_KEY`, `APP_SECRET`, and `DEVICE_ID` with placeholder strings (e.g., `"YOUR-APP-KEY"`). Pushing live API secrets to a public repository will result in a point deduction.

```cpp
#define ENABLE_DEBUG

#include <Arduino.h>
#include <WiFi.h>
#include <M5Unified.h>
#include <WiFiManager.h>

#include <SinricPro.h>
#include <SinricProDevice.h>
#include <Capabilities/PowerStateController.h>
#include <Capabilities/ToggleController.h>
#include <Capabilities/RangeController.h>

// ====================================================================
// Sinric Pro Credentials & Custom Device ID (Replace locally only)
// ====================================================================
#define APP_KEY          "YOUR-APP-KEY"
#define APP_SECRET       "YOUR-APP-SECRET"
#define DEVICE_ID        "YOUR-CUSTOM-DEVICE-ID"

// Element Instance IDs from your Sinric Pro Elements configuration
#define TOGGLE_INSTANCE  "toggleInstance1"
#define RANGE_INSTANCE   "rangeInstance1"

// SoftAP Network Name broadcast during Wi-Fi setup
#define AP_SSID          "M5StickS3-Setup"

class M5ScreenDevice : public SinricProDevice,
                       public PowerStateController<M5ScreenDevice>,
                       public ToggleController<M5ScreenDevice>,
                       public RangeController<M5ScreenDevice> {
    friend class PowerStateController<M5ScreenDevice>;
    friend class ToggleController<M5ScreenDevice>;
    friend class RangeController<M5ScreenDevice>;
public:
    M5ScreenDevice(const String &deviceId) : SinricProDevice(deviceId) {}
};

M5ScreenDevice *myDevice = nullptr;
bool isScreenOn = true;
bool isSinricConnected = false;
uint8_t screenBrightness = 128; // Range: 0-255

void drawStatusScreen() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 12);
    if (isSinricConnected) {
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.println("Sinric: ONLINE");
    } else {
        M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
        M5.Display.println("Sinric: WAIT..");
    }

    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, 45);
    M5.Display.printf("WiFi: %s\n", WiFi.SSID().c_str());
    M5.Display.setCursor(10, 65);
    M5.Display.printf("IP:   %s\n", WiFi.localIP().toString().c_str());
    M5.Display.setCursor(10, 88);
    M5.Display.printf("Brightness: %d%%\n", map(screenBrightness, 0, 255, 0, 100));
    M5.Display.setCursor(10, 112);
    M5.Display.println("[BtnA] Toggle Screen");
}

void setScreenPower(bool turnOn) {
    isScreenOn = turnOn;
    if (isScreenOn) {
        M5.Display.wakeup();
        M5.Display.setBrightness(screenBrightness > 0 ? screenBrightness : 128);
        drawStatusScreen();
    } else {
        M5.Display.setBrightness(0);
        M5.Display.sleep();
    }
}

// 1. Power Element Callback
bool onPowerState(const String &deviceId, bool &state) {
    Serial.printf("[Elements] Power -> %s\r\n", state ? "ON" : "OFF");
    setScreenPower(state);
    return true;
}

// 2. Toggle Element Callback
bool onToggleState(const String &deviceId, const String &instance, bool &state) {
    Serial.printf("[Elements] Toggle (%s) -> %s\r\n", instance.c_str(), state ? "ON" : "OFF");
    if (instance == TOGGLE_INSTANCE) {
        setScreenPower(state);
    }
    return true;
}

// 3. Range / Slider Element Callback (Brightness Control: 0-100)
bool onRangeValue(const String &deviceId, const String &instance, int &rangeValue) {
    Serial.printf("[Elements] Range (%s) -> %d\r\n", instance.c_str(), rangeValue);
    if (instance == RANGE_INSTANCE) {
        screenBrightness = map(constrain(rangeValue, 0, 100), 0, 100, 0, 255);
        if (isScreenOn) {
            M5.Display.setBrightness(screenBrightness);
            drawStatusScreen();
        }
    }
    return true;
}

// Displayed on LCD when device enters SoftAP Provisioning Mode
void configModeCallback(WiFiManager *myWiFiManager) {
    Serial.println("[WiFiManager] Entered SoftAP mode: " + myWiFiManager->getConfigPortalSSID());
    M5.Display.fillScreen(TFT_NAVY);
    M5.Display.setTextColor(TFT_YELLOW, TFT_NAVY);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 15);
    M5.Display.println("Wi-Fi Setup");

    M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, 50);
    M5.Display.println("1. Connect phone/PC to AP:");
    M5.Display.setCursor(10, 68);
    M5.Display.printf("   %s\n", myWiFiManager->getConfigPortalSSID().c_str());
    M5.Display.setCursor(10, 92);
    M5.Display.println("2. Open 192.168.4.1");
}

void setupWiFiWithSoftAP() {
    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    wm.setAPCallback(configModeCallback);

    // Hold BtnA during boot to wipe saved Wi-Fi credentials
    M5.update();
    if (M5.BtnA.isPressed()) {
        Serial.println("[WiFiManager] Resetting saved Wi-Fi credentials...");
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(10, 50);
        M5.Display.println("Resetting Wi-Fi settings...");
        wm.resetSettings();
        delay(1000);
    }

    if (!wm.autoConnect(AP_SSID)) {
        Serial.println("[WiFiManager] Failed to connect, restarting...");
        delay(2000);
        ESP.restart();
    }
    Serial.println("[WiFi] Connected! IP: " + WiFi.localIP().toString());
}

void setupSinricPro() {
    M5ScreenDevice &device = SinricPro[DEVICE_ID];
    myDevice = &device;

    myDevice->onPowerState(onPowerState);
    myDevice->onToggleState(TOGGLE_INSTANCE, onToggleState);
    myDevice->onRangeValue(RANGE_INSTANCE, onRangeValue);

    SinricPro.onConnected([]() {
        isSinricConnected = true;
        Serial.println("[SinricPro] CONNECTED TO CLOUD!");
        if (isScreenOn) drawStatusScreen();
    });
    SinricPro.onDisconnected([]() {
        isSinricConnected = false;
        Serial.println("[SinricPro] DISCONNECTED!");
        if (isScreenOn) drawStatusScreen();
    });

    SinricPro.begin(APP_KEY, APP_SECRET);
}

void setup() {
    Serial.begin(115200);
    delay(1500); // Allow USB-CDC Serial Monitor to attach
    Serial.println("\n[BOOT] Starting M5StickS3 SinricPro...");

    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(screenBrightness);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, 55);
    M5.Display.println("Connecting to Wi-Fi...");

    setupWiFiWithSoftAP();
    setupSinricPro();
    setScreenPower(true);
}

void loop() {
    M5.update();
    SinricPro.handle();

    // Sync local physical button press back to Sinric Pro Elements
    if (M5.BtnA.wasPressed()) {
        setScreenPower(!isScreenOn);
        if (myDevice != nullptr) {
            myDevice->sendPowerStateEvent(isScreenOn);
            myDevice->sendToggleStateEvent(TOGGLE_INSTANCE, isScreenOn);
        }
    }
}
```

---

## 6. Step 3: Build, Flash & SoftAP Provisioning

1. **Compile, Upload & Open Serial Monitor:**
   Run the following command in your PlatformIO terminal:
   ```bash
   pio run -e m5stack-s3 -t upload -t monitor
   ```
   *(If the screen stays blank immediately after `upload success`, single-click the side power/reset button once to reboot out of ESP32-S3 USB bootloader mode.)*
2. **Trigger SoftAP Mode:**
   * On first boot (no saved Wi-Fi credentials), the screen turns **navy blue** and displays `"Wi-Fi Setup"`.
   * *Note:* If Wi-Fi credentials were previously saved on the chip, hold down the front button (**BtnA**) while resetting/powering on until `"Resetting Wi-Fi settings..."` appears.
3. **Provision Home/Lab Wi-Fi:**
   * Open Wi-Fi settings on your phone or laptop and connect to the open network **`M5StickS3-Setup`**.
   * A captive portal page should pop up automatically. If not, open a browser and go to **`http://192.168.4.1`**.
   * Click **Configure WiFi**, select your **2.4 GHz** network, enter the password, and click **Save**.
4. **Verify Connection:**
   * The Stick S3 will connect to your router, authenticate with Sinric Pro, and update the top LCD line from orange `Sinric: WAIT..` to green **`Sinric: ONLINE`** alongside your Wi-Fi SSID and IP address.

---

## 7. Step 4: Functional Testing & Verification

1. **Verify Element Instance IDs:**
   * Watch the PlatformIO **Serial Monitor** (`115200` baud) while clicking your Power/Toggle and Brightness slider elements in the Sinric Pro web portal or mobile app.
   * Confirm that the serial log prints `[Elements] Power -> OFF/ON` or `[Elements] Toggle (toggleInstance1) -> OFF/ON`. If your dashboard uses a different instance ID (e.g., `toggleInstance2`), update `TOGGLE_INSTANCE` in `src/main.cpp` and re-upload.
2. **Test Cloud-to-Edge Control:**
   * Toggle the switch **OFF** in Sinric Pro. Confirm the Stick S3 screen turns completely black and enters display sleep mode (`M5.Display.sleep()`).
   * Toggle the switch **ON** in Sinric Pro. Confirm the screen wakes up and displays `Sinric: ONLINE`.
3. **Test Edge-to-Cloud Synchronization:**
   * Press physical **BtnA** on the front of the M5Stack Stick S3.
   * Confirm the screen toggles locally **and** the UI toggle inside the Sinric Pro dashboard updates automatically within 1–2 seconds.

---

## 8. Step 5: MBA Business Analysis (`REFLECTION.md`)

Complete the `REFLECTION.md` file in the root of your repository by answering all four strategic questions (150–250 words per question):

1. **Provisioning Economics & Customer Onboarding:** How does SoftAP provisioning impact Customer Acquisition Cost (CAC), field installation labor, and support ticket volume compared to pre-flashed credentials or Bluetooth Low Energy (BLE) provisioning?
2. **Security & Credential Lifecycle Management:** Why is separating Wi-Fi credentials (via NVS/SoftAP) from cloud API keys (`APP_KEY` / `APP_SECRET`) critical, and what risks arise if device-level secrets are compromised in a B2B deployment of 10,000 units?
3. **Bidirectional State & SLA Accountability:** Why is Edge-to-Cloud state reporting (`sendPowerStateEvent` when `BtnA` is pressed) essential for enterprise Service Level Agreements (SLAs), energy auditing, and remote troubleshooting?
4. **Commercial Use-Case Adaptation:** Propose one concrete B2B or B2C commercial product built on this exact architecture (ESP32-S3 + SoftAP + Custom Cloud Elements). Define the target buyer, value proposition, and pricing model (hardware margin vs. recurring SaaS subscription).

---

## 9. Step 6: Mandatory Video Demonstration & Submission

Every student must submit a **2 to 3 minute unedited video demonstration** proving end-to-end hardware and cloud execution.

### Required Video Sequence
1. **Introduction & Hardware View (15 sec):** State your name, course section, and show the M5Stack Stick S3 connected via USB-C.
2. **SoftAP Provisioning Demo (45 sec):** Hold `BtnA` during boot to reset Wi-Fi, show the navy `"Wi-Fi Setup"` SoftAP screen on the Stick S3, connect to `M5StickS3-Setup` on your phone/laptop, and submit your Wi-Fi credentials via `192.168.4.1`.
3. **Cloud-to-Edge Control (45 sec):** Show both the **Sinric Pro dashboard/app** and the **M5Stack Stick S3 screen** (`Sinric: ONLINE`) side-by-side. Turn the switch **OFF** (showing the screen turn off) and **ON** (showing the screen wake up), plus adjust brightness if configured.
4. **Edge-to-Cloud Physical Sync (20 sec):** Press physical **BtnA** on the Stick S3 and show the Sinric Pro dashboard toggle updating automatically.
5. **Executive Business Pitch (30 sec):** Briefly explain one commercial application of this setup and how SoftAP + Cloud Elements creates business value.

### How to Submit on GitHub
1. Upload your video to **YouTube (Unlisted)**, **Google Drive (Anyone with link can view)**, or **Loom**.
2. Fork this repository and commit your `platformio.ini`, `include/mbedtls_hmac_shim.h`, sanitized `src/main.cpp`, `README.md`, and `REFLECTION.md`.
3. Paste your video link at the top of `README.md` under **Demo Video** and submit your GitHub repository URL on the course LMS before the deadline.

---

## 10. Grading Rubric (100 Points)

| Category | Criteria | Weight |
| :--- | :--- | :---: |
| **SoftAP Provisioning** | Device boots into `M5StickS3-Setup`, displays instructions on LCD, and connects to Wi-Fi via captive portal without hardcoded SSID/password. | **25 pts** |
| **Cloud-to-Edge Control** | Device reaches `Sinric: ONLINE` and Sinric Pro Elements (`Power` / `Toggle` / `Range`) reliably turns the Stick S3 screen ON/OFF and adjusts brightness. | **25 pts** |
| **Edge-to-Cloud Sync** | Pressing physical `BtnA` toggles the screen locally and updates the Sinric Pro dashboard state in real time. | **15 pts** |
| **Video Demonstration** | Clear 2–3 min video showing hardware, SoftAP setup, bidirectional control, and concise business pitch. | **15 pts** |
| **MBA Business Reflection** | Thorough, commercially grounded answers in `REFLECTION.md` addressing provisioning ROI, security, SLAs, and monetization. | **15 pts** |
| **Repository Hygiene** | Includes `mbedtls_hmac_shim.h` and clean commits; live `APP_KEY` and `APP_SECRET` removed before pushing to GitHub. | **5 pts** |

---

## 11. Troubleshooting

| Symptom | Likely Cause | Fix |
| :--- | :--- | :--- |
| **`'mbedtls_md_hmac_starts' was not declared`** | Missing mbedTLS 4.0 compatibility shim. | Ensure `include/mbedtls_hmac_shim.h` exists and `-include include/mbedtls_hmac_shim.h` is in `platformio.ini`. |
| **`OSError: [Errno 28] No space left on device`** | Mac/PC SSD ran out of space caching PlatformIO packages. | Run `rm -rf ~/.platformio/.cache/* && pio system prune -f` and keep `platform = espressif32`. |
| **Screen stays black immediately after upload** | ESP32-S3 remains in USB bootloader mode after flashing. | Single-click the side power/reset button on the Stick S3 once to boot the firmware. |
| **`M5StickS3-Setup` AP does not appear** | Previous Wi-Fi credentials are already saved in ESP32 NVS flash. | Hold **BtnA** while resetting/powering on until `"Resetting Wi-Fi settings..."` appears on screen. |
| **Connects to SoftAP, fails to join home Wi-Fi** | Attempting to connect to a **5 GHz** Wi-Fi network. | ESP32-S3 supports **2.4 GHz Wi-Fi only**. Select a 2.4 GHz SSID or enable 2.4 GHz compatibility on your hotspot/router. |
| **LCD stays on orange `Sinric: WAIT..`** | Incorrect `APP_KEY`/`APP_SECRET` or firewall blocking WebSockets (`wss://ws.sinric.pro`). | Check Serial Monitor (`115200` baud) `[SinricPro]` debug output, verify credentials, or test via a mobile 2.4 GHz hotspot. |
