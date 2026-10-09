#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>

// ============================================================================
// SYSTEM CONSTANTS & CONFIGURATION
// ============================================================================
constexpr uint32_t TELEMETRY_INTERVAL_MS = 2000; // 2-second non-blocking sampling loop
constexpr uint32_t HOLD_CALIBRATION_MS   = 1500; // 1.5s Hold for Flash Reset/Cal

// ============================================================================
// DATA STRUCTURES & APPLICATION STATE
// ============================================================================
enum DisplayPage : uint8_t {
    PAGE_HYDRATION = 0,
    PAGE_DIAGNOSTICS,
    PAGE_COUNT
};

struct PlantMonitorState {
    float moisturePercent = 45.0f; // Initial interactive state [%]
    uint16_t dryAdcVal    = 3200;  // NVS persistent calibration limit
    uint16_t wetAdcVal    = 1200;  // NVS persistent calibration limit
    DisplayPage page      = PAGE_HYDRATION;
    bool needsFullRedraw  = true;
};

// Global System Objects
PlantMonitorState systemState;
Preferences prefs;

// ============================================================================
// NON-VOLATILE STORAGE (NVS) CONTROLLER
// ============================================================================
void loadNVSConfiguration() {
    prefs.begin("plant_app", true); // Open namespace in read-only mode
    systemState.dryAdcVal = prefs.getUShort("dry_adc", 3200);
    systemState.wetAdcVal = prefs.getUShort("wet_adc", 1200);
    prefs.end();
    Serial.printf("[NVS] Loaded Calibration Limits -> Dry: %u, Wet: %u\n", 
                  systemState.dryAdcVal, systemState.wetAdcVal);
}

void saveNVSConfiguration() {
    prefs.begin("plant_app", false); // Open namespace in read-write mode
    prefs.putUShort("dry_adc", systemState.dryAdcVal);
    prefs.putUShort("wet_adc", systemState.wetAdcVal);
    prefs.end();
    Serial.printf("[NVS] Saved Calibration Limits -> Dry: %u, Wet: %u\n", 
                  systemState.dryAdcVal, systemState.wetAdcVal);
}

// ============================================================================
// DISPLAY GRAPHICS PIPELINE (M5GFX)
// ============================================================================
void renderHeader(const char* title) {
    M5.Display.fillRect(0, 0, 240, 24, BLUE);
    M5.Display.setTextColor(WHITE, BLUE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(title, 120, 12, &fonts::Font2);
}

void renderFooter(const char* leftLabel, const char* rightLabel) {
    // Left Box (Button A)
    M5.Display.fillRect(0, 112, 118, 23, DARKGREEN);
    M5.Display.setTextColor(WHITE, DARKGREEN);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(leftLabel, 59, 123, &fonts::Font2);

    // Right Box (Button B)
    M5.Display.fillRect(122, 112, 118, 23, MAROON);
    M5.Display.setTextColor(WHITE, MAROON);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(rightLabel, 181, 123, &fonts::Font2);
}

void renderHydrationPage() {
    if (systemState.needsFullRedraw) {
        M5.Display.fillScreen(BLACK);
        renderHeader("SMART PLANT MONITOR");
        renderFooter("BTN A: DRY (-5%)", "BTN B: WATER (+5%)");
        M5.Display.drawRect(20, 85, 200, 18, WHITE); // Progress Bar Frame
        systemState.needsFullRedraw = false;
    }

    // Determine Status Color Based on Hydration Rules
    uint16_t statusColor = GREEN;
    if (systemState.moisturePercent < 25.0f) {
        statusColor = RED;
    } else if (systemState.moisturePercent < 50.0f) {
        statusColor = YELLOW;
    }

    // Partial Screen Invalidation: Erase Center Numeric Region Only
    M5.Display.fillRect(0, 28, 240, 52, BLACK);
    M5.Display.setTextColor(statusColor, BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawFloat(systemState.moisturePercent, 1, 120, 52, &fonts::Font7);

    // Dynamic Progress Bar Invalidation
    uint16_t barWidth = (uint16_t)((systemState.moisturePercent / 100.0f) * 196.0f);
    M5.Display.fillRect(22, 87, 196, 14, BLACK); // Clear previous bar fill
    if (barWidth > 0) {
        M5.Display.fillRect(22, 87, barWidth, 14, statusColor);
    }
}

void renderDiagnosticsPage() {
    if (systemState.needsFullRedraw) {
        M5.Display.fillScreen(BLACK);
        renderHeader("HARDWARE DIAGNOSTICS");
        renderFooter("BTN A: TOGGLE", "HOLD B: CAL NVS");
        systemState.needsFullRedraw = false;
    }

    M5.Display.fillRect(0, 28, 240, 80, BLACK);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(CYAN, BLACK);
    
    M5.Display.drawString("Moisture Val: " + String(systemState.moisturePercent, 1) + "%", 15, 32, &fonts::Font2);
    M5.Display.drawString("Dry Cal Limit: " + String(systemState.dryAdcVal), 15, 52, &fonts::Font2);
    M5.Display.drawString("Wet Cal Limit: " + String(systemState.wetAdcVal), 15, 72, &fonts::Font2);
    M5.Display.drawString("Uptime:        " + String(millis() / 1000) + "s", 15, 92, &fonts::Font2);
}

void updateDisplayPipeline() {
    switch (systemState.page) {
        case PAGE_HYDRATION:   renderHydrationPage(); break;
        case PAGE_DIAGNOSTICS: renderDiagnosticsPage(); break;
        default: break;
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================
void setup() {
    Serial.begin(115200);

    // Initialize M5Unified System Architecture
    auto cfg = M5.config();
    M5.begin(cfg);

    // Configure Display Subsystem
    M5.Display.setRotation(1);     // Landscape Mode (240x135)
    M5.Display.setBrightness(128); // Standard Backlight Intensity

    // Load Non-Volatile Memory Settings
    loadNVSConfiguration();

    // Initial Screen Draw
    updateDisplayPipeline();

    Serial.println("[SYS] Self-Contained M5StickS3 System Ready.");
}

// ============================================================================
// EVENT LOOP
// ============================================================================
void loop() {
    static uint32_t lastTelemetryMs = 0;
    uint32_t now = millis();

    // Sample Hardware State & Buttons via M5Unified Clock
    M5.update();

    // --- INPUT EVENT 1: Button A (Simulate Drying / Page Toggle) ---
    if (M5.BtnA.wasPressed()) {
        if (systemState.page == PAGE_HYDRATION) {
            systemState.moisturePercent -= 5.0f;
            if (systemState.moisturePercent < 0.0f) systemState.moisturePercent = 0.0f;
            Serial.printf("[SIMULATOR] Button A -> Decreased Hydration: %.1f%%\n", systemState.moisturePercent);
        } else {
            systemState.page = PAGE_HYDRATION;
            systemState.needsFullRedraw = true;
        }
        updateDisplayPipeline();
    }

    // --- INPUT EVENT 2: Button B (Simulate Watering / Toggle) ---
    if (M5.BtnB.wasPressed()) {
        if (systemState.page == PAGE_HYDRATION) {
            systemState.moisturePercent += 5.0f;
            if (systemState.moisturePercent > 100.0f) systemState.moisturePercent = 100.0f;
            Serial.printf("[SIMULATOR] Button B -> Increased Hydration: %.1f%%\n", systemState.moisturePercent);
        } else {
            systemState.page = PAGE_DIAGNOSTICS;
            systemState.needsFullRedraw = true;
        }
        updateDisplayPipeline();
    }

    // --- INPUT EVENT 3: Hold Button B (Flash Calibration) ---
    if (M5.BtnB.pressedFor(HOLD_CALIBRATION_MS)) {
        systemState.dryAdcVal = 3100; // Calibrate new dry threshold
        systemState.wetAdcVal = 1150; // Calibrate new wet threshold
        saveNVSConfiguration();

        // Flash confirmation overlay
        M5.Display.fillScreen(BLUE);
        M5.Display.setTextColor(WHITE, BLUE);
        M5.Display.setTextDatum(middle_center);
        M5.Display.drawString("NVS CALIBRATION SAVED!", 120, 67, &fonts::Font2);
        delay(1000);

        systemState.needsFullRedraw = true;
        updateDisplayPipeline();
    }

    // --- TELEMETRY: Periodic Health Output ---
    if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryMs = now;
        Serial.printf("[TELEMETRY] Current Hydration: %.1f%% | System Time: %lu ms\n", 
                      systemState.moisturePercent, now);
    }

    delay(10); // Small delay to prevent CPU hogging
}
