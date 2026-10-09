#include <Arduino.h>
#include <M5Unified.h>

// ============================================================================
// GLOBAL APPLICATION STATE
// ============================================================================
static uint32_t counter = 0;
static uint32_t lastRenderTimeMs = 0;
static uint32_t loopIterations = 0;

// ============================================================================
// DISPLAY GRAPHICS FUNCTIONS
// ============================================================================

/**
 * @brief Renders the static UI skeleton once during boot.
 * 
 * CONCEPT: Full screen erases are slow and cause display flickering. 
 * Drawing background banners once in setup() isolates dynamic updates 
 * to target screen sub-regions.
 */
void drawStaticUI() {
    M5.Display.fillScreen(BLACK);

    // Header Bar (Top 28 pixels)
    M5.Display.fillRect(0, 0, 240, 28, BLUE);
    M5.Display.setTextColor(WHITE, BLUE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("M5StickS3 TALLY", 120, 14, &fonts::Font2);

    // Footer Legend Banners
    // Button A Box (Left)
    M5.Display.fillRect(0, 110, 118, 25, DARKGREEN);
    M5.Display.setTextColor(WHITE, DARKGREEN);
    M5.Display.drawString("BTN A: +1", 59, 122, &fonts::Font2);

    // Button B Box (Right)
    M5.Display.fillRect(122, 110, 118, 25, MAROON);
    M5.Display.setTextColor(WHITE, MAROON);
    M5.Display.drawString("BTN B: RESET", 181, 122, &fonts::Font2);
}

/**
 * @brief Performs partial screen invalidation to update the counter value.
 * @param countValue Current counter integer to display.
 * 
 * CONCEPT: Partial Screen Invalidation erases ONLY the bounding box containing
 * the digit text (y=30 to y=108), leaving the header/footer untouched.
 */
void updateCounterDisplay(uint32_t countValue) {
    // Clear display area for number region only
    M5.Display.fillRect(0, 30, 240, 78, BLACK);
    
    // Draw updated counter text using 7-segment font
    M5.Display.setTextColor(GREEN, BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawNumber(countValue, 120, 70, &fonts::Font7);

    // Diagnostic logging
    Serial.printf("[DISPLAY] Rendered Value: %u | Timestamp: %lu ms\n", 
                  countValue, millis());
}

// ============================================================================
// SETUP & INITIALIZATION
// ============================================================================
void setup() {
    // 1. Initialize Serial Communication over USB CDC
    Serial.begin(115200);
    
    // 2. Instantiate and apply M5Unified Hardware Configuration
    auto cfg = M5.config();
    M5.begin(cfg);

    // 3. Configure Physical Display Driver
    M5.Display.setRotation(1);     // Landscape mode (240x135)
    M5.Display.setBrightness(128); // Backlight power (0-255)

    // 4. Initial Screen Render
    drawStaticUI();
    updateCounterDisplay(counter);

    Serial.println("[SYSTEM] M5StickS3 Hardware Subsystems Successfully Initialized.");
}

// ============================================================================
// MAIN NON-BLOCKING EXECUTION LOOP
// ============================================================================
void loop() {
    // ------------------------------------------------------------------------
    // MECHANIC 1: Hardware State Sampling
    // ------------------------------------------------------------------------
    // M5.update() MUST be called on every iteration of loop().
    // It polls physical GPIO pins, processes software debouncing timers,
    // and calculates edge transitions (wasPressed, wasReleased, isHolding).
    M5.update();
    loopIterations++;

    // ------------------------------------------------------------------------
    // MECHANIC 2: Event-Driven State Transitions
    // ------------------------------------------------------------------------
    // Button A: Increment Counter State
    if (M5.BtnA.wasPressed()) {
        counter++;
        updateCounterDisplay(counter);
        Serial.printf("[INPUT] Button A Pressed -> Counter Incremented to: %u\n", counter);
    }

    // Button B: Reset Counter State
    if (M5.BtnB.wasPressed()) {
        counter = 0;
        updateCounterDisplay(counter);
        Serial.println("[INPUT] Button B Pressed -> Counter Reset to 0.");
    }

    // ------------------------------------------------------------------------
    // MECHANIC 3: Non-Blocking Telemetry Reporting
    // ------------------------------------------------------------------------
    // Demonstrates non-blocking interval execution without using delay()
    uint32_t currentMs = millis();
    if (currentMs - lastRenderTimeMs >= 5000) { // Every 5 Seconds
        lastRenderTimeMs = currentMs;
        Serial.printf("[HEALTH] Loop Iterations in 5s: %u | System Time: %lu ms\n", 
                      loopIterations, currentMs);
        loopIterations = 0; // Reset loop counter
    }
}
