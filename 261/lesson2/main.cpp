#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>

// ============================================================================
// HARDWARE CONSTANTS & PIN DEFINITIONS
// ============================================================================
constexpr uint8_t PIN_SOIL_ADC = 1;          // Analog Soil Sensor on Grove Port (GPIO 1)
constexpr uint32_t TELEMETRY_INTERVAL = 3000; // 3-second non-blocking read cycle
constexpr uint32_t HOLD_RESET_TIME_MS = 1500; // 1.5s Hold for Calibration

// ============================================================================
// SYSTEM STATES & TYPES
// ============================================================================
enum DisplayPage : uint8_t {
    PAGE_SOIL_GAUGE = 0,
    PAGE_ENVIRONMENT,
    PAGE_DIAGNOSTICS,
    PAGE_COUNT
};

struct PlantData {
    uint16_t rawAdc = 0;
    float moisturePercent = 0.0f;
    uint16_t dryAdcVal = 3200; // Default dry calibration limit
    uint16_t wetAdcVal = 1200; // Default wet calibration limit
    DisplayPage currentPage = PAGE_SOIL_GAUGE;
    bool needsFullRedraw = true;
};

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
PlantData appData;
Preferences prefs;

// ============================================================================
// NVS STORAGE CONTROLLER
// ============================================================================
void loadCalibrationFromNVS() {
    prefs.begin("plant_app", true); // Read-only mode
    appData.dryAdcVal = prefs.getUShort("dry_adc", 3200);
    appData.wetAdcVal = prefs.getUShort("wet_adc", 1200);
    prefs.end();
    Serial.printf("[NVS] Loaded Calibration -> Dry: %u, Wet: %u\n", 
                  appData.dryAdcVal, appData.wetAdcVal);
}

void saveCalibrationToNVS() {
    prefs.begin("plant_app", false); // Read-Write mode
    prefs.putUShort("dry_adc", appData.dryAdcVal);
    prefs.putUShort("wet_adc", appData.wetAdcVal);
    prefs.end();
    Serial.printf("[NVS] Saved Calibration -> Dry: %u, Wet: %u\n", 
                  appData.dryAdcVal, appData.wetAdcVal);
}

// ============================================================================
// SENSOR PROCESSING & MATHEMATICAL MODELING
// ============================================================================
float calculateMoisturePercentage(uint16_t rawAdc, uint16_t dryVal, uint16_t wetVal) {
    if (dryVal == wetVal) return 0.0f; // Prevent division by zero
    
    // Inverse relationship: High ADC = Dry Soil, Low ADC = Wet Soil
    float percentage = 100.0f * (1.0f - ((float)(rawAdc - wetVal) / (float)(dryVal - wetVal)));
    
    // Constrain output bounds [0.0%, 100.0%]
    if (percentage > 100.0f) percentage = 100.0f;
    if (percentage < 0.0f) percentage = 0.0f;
    
    return percentage;
}

void sampleSoilSensor() {
    appData.rawAdc = analogRead(PIN_SOIL_ADC);
    appData.moisturePercent = calculateMoisturePercentage(
        appData.rawAdc, appData.dryAdcVal, appData.wetAdcVal
    );
}

// ============================================================================
// DISPLAY GRAPHICS PIPELINE (M5GFX)
// ============================================================================
void drawUIHeader(const char* title) {
    M5.Display.fillRect(0, 0, 240, 24, BLUE);
    M5.Display.setTextColor(WHITE, BLUE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(title, 120, 12, &fonts::Font2);
}

void drawUIFooter(const char* btnALabel, const char* btnBLabel) {
    // Left Button Legend (Btn A)
    M5.Display.fillRect(0, 112, 118, 23, DARKGREEN);
    M5.Display.setTextColor(WHITE, DARKGREEN);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(btnALabel, 59, 123, &fonts::Font2);

    // Right Button Legend (Btn B)
    M5.Display.fillRect(122, 112, 118, 23, MAROON);
    M5.Display.setTextColor(WHITE, MAROON);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(btnBLabel, 181, 123, &fonts::Font2);
}

void renderSoilGaugePage() {
    if (appData.needsFullRedraw) {
        M5.Display.fillScreen(BLACK);
        drawUIHeader("SOIL HYDRATION");
        drawUIFooter("NEXT PAGE", "SAMPLE");
        
        // Outer Gauge Border
        M5.Display.drawRect(20, 85, 200, 18, WHITE);
        appData.needsFullRedraw = false;
    }

    // Dynamic Central Value Update (Partial Redraw)
    M5.Display.fillRect(0, 28, 240, 52, BLACK);
    
    uint16_t color = GREEN;
    if (appData.moisturePercent < 25.0f) color = RED;
    else if (appData.moisturePercent < 50.0f) color = YELLOW;

    M5.Display.setTextColor(color, BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawFloat(appData.moisturePercent, 1, 120, 52, &fonts::Font7);

    // Dynamic Progress Bar Update
    uint16_t barWidth = (uint16_t)((appData.moisturePercent / 100.0f) * 196.0f);
    M5.Display.fillRect(22, 87, 196, 14, BLACK); // Clear inner bar
    if (barWidth > 0) {
        M5.Display.fillRect(22, 87, barWidth, 14, color);
    }
}

void renderEnvironmentPage() {
    if (appData.needsFullRedraw) {
        M5.Display.fillScreen(BLACK);
        drawUIHeader("AIR & LIGHT STATUS");
        drawUIFooter("NEXT PAGE", "REFRESH");
        appData.needsFullRedraw = false;
    }

    M5.Display.fillRect(0, 28, 240, 80, BLACK);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.drawString("Status: Active", 15, 35, &fonts::Font2);
    
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.drawString("Soil Condition:", 15, 60, &fonts::Font2);
    
    if (appData.moisturePercent < 25.0f) {
        M5.Display.setTextColor(RED, BLACK);
        M5.Display.drawString("NEEDS WATERING", 15, 80, &fonts::Font4);
    } else {
        M5.Display.setTextColor(GREEN, BLACK);
        M5.Display.drawString("HYDRATED", 15, 80, &fonts::Font4);
    }
}

void renderDiagnosticsPage() {
    if (appData.needsFullRedraw) {
        M5.Display.fillScreen(BLACK);
        drawUIHeader("HARDWARE DIAGNOSTICS");
        drawUIFooter("NEXT PAGE", "HOLD: CAL");
        appData.needsFullRedraw = false;
    }

    M5.Display.fillRect(0, 28, 240, 80, BLACK);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(YELLOW, BLACK);
    
    M5.Display.drawString("ADC Raw: " + String(appData.rawAdc), 15, 32, &fonts::Font2);
    M5.Display.drawString("Dry Cal: " + String(appData.dryAdcVal), 15, 52, &fonts::Font2);
    M5.Display.drawString("Wet Cal: " + String(appData.wetAdcVal), 15, 72, &fonts::Font2);
    M5.Display.drawString("Uptime:  " + String(millis() / 1000) + "s", 15, 92, &fonts::Font2);
}

void updateDisplayPipeline() {
    switch (appData.currentPage) {
        case PAGE_SOIL_GAUGE:   renderSoilGaugePage(); break;
        case PAGE_ENVIRONMENT:  renderEnvironmentPage(); break;
        case PAGE_DIAGNOSTICS:  renderDiagnosticsPage(); break;
        default: break;
    }
}

// ============================================================================
// SETUP & INITIALIZATION
// ============================================================================
void setup() {
    Serial.begin(115200);

    // Initialize ADC Pin Direction
    pinMode(PIN_SOIL_ADC, INPUT);

    // Initialize M5Unified Subsystems
    auto cfg = M5.config();
    M5.begin(cfg);

    // Display Hardware Configuration
    M5.Display.setRotation(1);     // Landscape mode (240x135)
    M5.Display.setBrightness(128); // Backlight Intensity

    // Load Saved NVS Calibration Parameters
    loadCalibrationFromNVS();

    // Initial Sampling & Render
    sampleSoilSensor();
    updateDisplayPipeline();

    Serial.println("[SYS] Smart Plant Hydration Monitor Operational.");
}

// ============================================================================
// NON-BLOCKING EVENT LOOP
// ============================================================================
void loop() {
    static uint32_t lastSampleTime = 0;
    uint32_t now = millis();

    // Poll Hardware and Update Internal State Machine
    M5.update();

    // ------------------------------------------------------------------------
    // EVENT 1: Page Navigation (Button A Pressed)
    // ------------------------------------------------------------------------
    if (M5.BtnA.wasPressed()) {
        appData.currentPage = static_cast<DisplayPage>((appData.currentPage + 1) % PAGE_COUNT);
        appData.needsFullRedraw = true;
        updateDisplayPipeline();
        Serial.printf("[UI] Navigated to Page: %u\n", appData.currentPage);
    }

    // ------------------------------------------------------------------------
    // EVENT 2: Manual Sample Trigger (Button B Single Click)
    // ------------------------------------------------------------------------
    if (M5.BtnB.wasPressed()) {
        sampleSoilSensor();
        updateDisplayPipeline();
        Serial.printf("[SENSOR] Manual Trigger -> Raw ADC: %u | Moisture: %.1f%%\n", 
                      appData.rawAdc, appData.moisturePercent);
    }

    // ------------------------------------------------------------------------
    // EVENT 3: Calibration Trigger (Button B Hold 1.5s)
    // ------------------------------------------------------------------------
    if (M5.BtnB.pressedFor(HOLD_RESET_TIME_MS)) {
        // Calibrate current ADC reading as Dry Value if > 2000, else Wet Value
        if (appData.rawAdc > 2000) {
            appData.dryAdcVal = appData.rawAdc;
            Serial.printf("[CAL] Dry Threshold Set to: %u\n", appData.dryAdcVal);
        } else {
            appData.wetAdcVal = appData.rawAdc;
            Serial.printf("[CAL] Wet Threshold Set to: %u\n", appData.wetAdcVal);
        }
        
        saveCalibrationToNVS();
        
        // Provide visual confirmation overlay
        M5.Display.fillScreen(BLUE);
        M5.Display.setTextColor(WHITE, BLUE);
        M5.Display.setTextDatum(middle_center);
        M5.Display.drawString("CALIBRATION SAVED!", 120, 67, &fonts::Font2);
        delay(1000); // Visual pause confirmation
        
        appData.needsFullRedraw = true;
        updateDisplayPipeline();
    }

    // ------------------------------------------------------------------------
    // EVENT 4: Periodic Non-Blocking Sensor Sampling
    // ------------------------------------------------------------------------
    if (now - lastSampleTime >= TELEMETRY_INTERVAL) {
        lastSampleTime = now;
        sampleSoilSensor();
        updateDisplayPipeline();
    }

    delay(10); // Small yield to prevent CPU starvation
}
