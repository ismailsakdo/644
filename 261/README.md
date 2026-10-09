# PlatformIO CLI Setup & Deployment Guide for Windows

**Target Microcontroller:** M5Stack M5StickS3 (ESP32-S3)  
**Development Framework:** PlatformIO Core + Arduino / M5Unified  

---

## 1. Environment Path Configuration

To enable the PlatformIO Command Line Interface (`pio`) across Windows Command Prompt, PowerShell, and integrated terminals, register the core executable path in your system user profile.

### Step 1: Export Executable Path via PowerShell

Run the following command in **PowerShell**:

```powershell
[System.Environment]::SetEnvironmentVariable("Path", $env:Path + ";$env:USERPROFILE\.platformio\penv\Scripts", "User")

```

Restart all active terminal windows after executing the registration command.

### Step 2: Verify Path Registration

Run the version check command in a **new** terminal:

```cmd
pio --version

```

* **Expected Result:** `PlatformIO Core, version 6.x.x` (or newer)
* **Failure Resolution:** Confirm that `pio.exe` exists in `C:\Users\<YourUsername>\.platformio\penv\Scripts`.

---

## 2. PlatformIO Configuration (`platformio.ini`)

Place the following configuration inside the `platformio.ini` file located at your project root:

```ini
[platformio]
default_envs = m5sticks3

[env:m5sticks3]
platform = espressif32@6.12.0
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
upload_speed = 921600

; System Memory and Frequency Parameters
board_build.f_cpu = 240000000L
board_build.flash_size = 8MB
board_upload.flash_size = 8MB
board_build.partitions = default_8MB.csv
board_build.filesystem = littlefs

; ESP32-S3 Native USB CDC Configurations
build_flags =
    -std=gnu++17
    -DESP32S3
    -DARDUINO_M5STACK_STICK_S3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -O3

; Library Dependencies
lib_deps =
    M5Unified=[https://github.com/m5stack/M5Unified](https://github.com/m5stack/M5Unified)
    h2zero/NimBLE-Arduino@^1.4.1

```

---

## 3. Firmware Verification Source (`src/main.cpp`)

Save the following source code into `src/main.cpp`:

```cpp
#include <Arduino.h>
#include <M5Unified.h>

static uint32_t counter = 0;

void drawUI() {
    M5.Display.fillScreen(BLACK);

    // Header Bar
    M5.Display.fillRect(0, 0, 240, 28, BLUE);
    M5.Display.setTextColor(WHITE, BLUE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("M5StickS3 COUNTER", 120, 14, &fonts::Font2);

    // Footer Labels
    M5.Display.fillRect(0, 110, 118, 25, DARKGREEN);
    M5.Display.setTextColor(WHITE, DARKGREEN);
    M5.Display.drawString("BTN A: +1", 59, 122, &fonts::Font2);

    M5.Display.fillRect(122, 110, 118, 25, MAROON);
    M5.Display.setTextColor(WHITE, MAROON);
    M5.Display.drawString("BTN B: RESET", 181, 122, &fonts::Font2);
}

void renderValue(uint32_t value) {
    M5.Display.fillRect(0, 30, 240, 78, BLACK);
    M5.Display.setTextColor(GREEN, BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawNumber(value, 120, 70, &fonts::Font7);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(128);

    drawUI();
    renderValue(counter);

    Serial.begin(115200);
    Serial.println("[SYS] M5StickS3 Tally System Operational.");
}

void loop() {
    M5.update();

    if (M5.BtnA.wasPressed()) {
        counter++;
        renderValue(counter);
        Serial.printf("[INPUT] Button A -> Count: %u\n", counter);
    }

    if (M5.BtnB.wasPressed()) {
        counter = 0;
        renderValue(counter);
        Serial.println("[INPUT] Button B -> Counter Reset");
    }

    delay(10);
}

```

---

## 4. Execution Workflow

Run these commands in order from your project directory:

```cmd
:: 1. Purge intermediate build cache
pio run -e m5sticks3 -t clean

:: 2. Compile source files
pio run -e m5sticks3

:: 3. Upload binary to M5StickS3
pio run -e m5sticks3 -t upload

:: 4. Flash and automatically launch Serial Monitor
pio run -e m5sticks3 -t upload -t monitor

```

---

## 5. Mathematical Models & Performance Analysis

### 5.1 Power Consumption & Battery Discharge Calculations

The estimated operational runtime $T_{\text{battery}}$ (in hours) of the internal lithium-polymer battery can be derived using:

$$T_{\text{battery}} = \frac{C_{\text{nominal}} \cdot \eta_{\text{discharge}}}{I_{\text{avg}}}$$

Where:

* $C_{\text{nominal}}$ = Nominal battery capacity ($\text{mAh}$)
* $\eta_{\text{discharge}}$ = Discharge efficiency factor ($\approx 0.85$)
* $I_{\text{avg}}$ = Average operational current ($\text{mA}$)

The average current consumption $I_{\text{avg}}$ across $N$ operational states is defined as:

$$I_{\text{avg}} = \frac{1}{T_{\text{total}}} \sum_{i=1}^{N} \left( I_i \cdot t_i \right)$$

---

### 5.2 Signal Debouncing Model

Physical button contacts exhibit mechanical bounce. The state transition conditions are governed by the following timing inequality:

$$\Delta t = t_{\text{sample}} - t_{\text{last\_change}} > \tau_{\text{debounce}}$$

Where:

* $t_{\text{sample}}$ = Current timestamp measured via `millis()`
* $t_{\text{last\_change}}$ = Timestamp of the last raw pin state transition
* $\tau_{\text{debounce}}$ = Settling time threshold (configured to $50\text{ ms}$)

---

## 6. Windows Diagnostic Reference

| Error Message | Failure Cause | Corrective Action |
| --- | --- | --- |
| `'pio' is not recognized as an internal or external command` | Executable path missing from Windows `PATH`. | Run the PowerShell path export script from Section 1 and restart the shell. |
| `fatal error: TFT_eSPI.h: No such file or directory` | Hardware display library mismatch. | Remove `#include <TFT_eSPI.h>` and use `M5Unified` via `#include <M5Unified.h>`. |
| `A fatal error occurred: Could not open COMx` | Port is held open by another terminal session. | Close all active serial terminals, Putty, or IDE monitor instances, then retry `pio run -t upload`. |
| `Failed to connect to ESP32-S3` | USB-C cable lacks data lines or device CDC state hung. | Hold the **G0 button** on the M5StickS3 while connecting the USB-C cable to force bootloader mode. |
