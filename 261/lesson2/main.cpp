#include <M5Unified.h>

// Global Counter State
static uint32_t counter = 0;

void drawStaticUI() {
    M5.Display.fillScreen(BLACK);

    // Header Bar
    M5.Display.fillRect(0, 0, 240, 28, BLUE);
    M5.Display.setTextColor(WHITE, BLUE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("M5StickS3 TALLY", 120, 14, &fonts::Font2);

    // Footer Button Labels
    M5.Display.fillRect(0, 110, 118, 25, DARKGREEN);
    M5.Display.setTextColor(WHITE, DARKGREEN);
    M5.Display.drawString("BTN A: +1", 59, 122, &fonts::Font2);

    M5.Display.fillRect(122, 110, 118, 25, MAROON);
    M5.Display.setTextColor(WHITE, MAROON);
    M5.Display.drawString("BTN B: RESET", 181, 122, &fonts::Font2);
}

void renderCount(uint32_t countValue) {
    // Clear display area for number only to prevent full-screen flicker
    M5.Display.fillRect(0, 30, 240, 78, BLACK);
    M5.Display.setTextColor(GREEN, BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawNumber(countValue, 120, 70, &fonts::Font7); // Large 7-segment digital font
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // Set Display Orientation (1 = Landscape)
    M5.Display.setRotation(1);
    M5.Display.setBrightness(128); // 0-255

    // Initial Screen Draw
    drawStaticUI();
    renderCount(counter);

    Serial.begin(115200);
    Serial.println("[SYS] M5StickS3 Tally Counter Initialized.");
}

void loop() {
    // Keep hardware state updated (buttons, touch, power)
    M5.update();

    // Button A -> Increment Counter
    if (M5.BtnA.wasPressed()) {
        counter++;
        renderCount(counter);
        Serial.printf("[EVENT] Button A Pressed -> Counter: %u\n", counter);
    }

    // Button B -> Reset Counter
    if (M5.BtnB.wasPressed()) {
        counter = 0;
        renderCount(counter);
        Serial.println("[EVENT] Button B Pressed -> Counter Reset to 0");
    }

    delay(10); // Small yield to prevent CPU hogging
}
