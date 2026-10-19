#include <Arduino.h>
#include <M5Unified.h>

// --- Hardware Pin Definitions for M5StickS3 ---
static constexpr int_fast16_t PIN_IR_TX = 46;
static constexpr int_fast16_t PIN_IR_RX = 42;

// --- Carrier Frequency & PWM Setup ---
static constexpr uint32_t IR_CARRIER_FREQ_HZ = 38000;
static constexpr uint8_t  PWM_CHANNEL        = 0;
static constexpr uint8_t  PWM_RESOLUTION_BITS= 8;
static constexpr uint8_t  PWM_DUTY_50_PCT    = 128;

// --- Demodulator Physics Timing ---
// 38 kHz = 26.3 us per cycle. Demodulator requires >= 12 cycles to latch output LOW.
static constexpr uint32_t SAMPLE_INTERVAL_US = 1000; // 1 kHz interrogation rate
static constexpr uint32_t BURST_DURATION_US  = 440;  // ~17 cycles of 38 kHz carrier
static constexpr uint32_t RX_SETTLE_DELAY_US = 340;  // Sample after ~13 cycles

// --- Envelope Extraction Constraints (ms) ---
static constexpr uint32_t MIN_VALID_PULSE_MS = 15;   // Glitch rejection filter
static constexpr uint32_t MAX_VALID_PULSE_MS = 900;  // Static hover rejection
static constexpr uint32_t GAP_TIMEOUT_MS     = 300;  // Inter-burst silence declaring scan complete
static constexpr uint32_t RESULT_HOLD_MS     = 2000; // Time to latch result before auto-rearm

enum IdentificationResult {
    ID_IDLE = 0,
    ID_ITEM_A,       // 1 Stripe
    ID_ITEM_B,       // 2 Stripes
    ID_ITEM_C,       // 3 Stripes
    ID_ERR_UNKNOWN   // Malformed sequence / > 3 stripes
};

// --- System State ---
static uint32_t pulse_count = 0;
static uint32_t pulse_durations[8];
static bool is_reflecting = false;
static uint32_t state_transition_time = 0;
static uint32_t last_pulse_end_time = 0;
static uint32_t last_sample_micros = 0;
static uint32_t result_display_start = 0;
static uint32_t last_heartbeat_ms = 0;
static bool showing_result = false;
static bool diagnostic_stream = false;

// Hardware Carrier Control
void setup_ir_hardware() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(PIN_IR_TX, IR_CARRIER_FREQ_HZ, PWM_RESOLUTION_BITS);
    ledcWrite(PIN_IR_TX, 0);
#else
    ledcSetup(PWM_CHANNEL, IR_CARRIER_FREQ_HZ, PWM_RESOLUTION_BITS);
    ledcAttachPin(PIN_IR_TX, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);
#endif
    pinMode(PIN_IR_RX, INPUT_PULLUP);
}

void set_carrier(bool active) {
    uint32_t duty = active ? PWM_DUTY_50_PCT : 0;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(PIN_IR_TX, duty);
#else
    ledcWrite(PWM_CHANNEL, duty);
#endif
}

// Strobe 38 kHz carrier and read active-LOW demodulator
bool strobe_ir_probe() {
    set_carrier(true);
    delayMicroseconds(RX_SETTLE_DELAY_US);
    
    // Active LOW: 0 = Reflective target detected, 1 = Idle / Absorbing
    bool detected = (digitalRead(PIN_IR_RX) == LOW);
    
    if (BURST_DURATION_US > RX_SETTLE_DELAY_US) {
        delayMicroseconds(BURST_DURATION_US - RX_SETTLE_DELAY_US);
    }
    set_carrier(false);
    return detected;
}

void play_audio_feedback(IdentificationResult id) {
    switch (id) {
        case ID_ITEM_A:
            M5.Speaker.tone(1800, 100);
            break;
        case ID_ITEM_B:
            M5.Speaker.tone(2200, 80);
            delay(100);
            M5.Speaker.tone(2200, 80);
            break;
        case ID_ITEM_C:
            M5.Speaker.tone(2600, 60);
            delay(80);
            M5.Speaker.tone(2600, 60);
            delay(80);
            M5.Speaker.tone(2600, 60);
            break;
        default:
            M5.Speaker.tone(600, 250);
            break;
    }
    M5.Speaker.stop(); // Shut down amplifier to prevent high-frequency EMI leakage
}

void display_idle_screen() {
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    M5.Display.drawString("READY TO SCAN", 10, 12);
    
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(LIGHTGREY);
    M5.Display.drawString("Autonomous Hand Sweep", 10, 45);
    M5.Display.drawString("1 Strip: A | 2: B | 3: C", 10, 65);
    M5.Display.drawString("BtnA: Reset | BtnB: Stream", 10, 85);
    
    M5.Display.fillRect(10, 110, 14, 14, DARKGREY);
    M5.Display.drawString("IR RX Standby", 32, 113);
}

void display_result_screen(IdentificationResult id, uint32_t count) {
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextDatum(top_left);

    switch (id) {
        case ID_ITEM_A:
            M5.Display.fillRect(0, 0, 240, 34, GREEN);
            M5.Display.setTextColor(BLACK);
            M5.Display.setTextSize(2);
            M5.Display.drawString("IDENTIFIED: BOX A", 10, 8);
            M5.Display.setTextColor(WHITE);
            M5.Display.setTextSize(2);
            M5.Display.drawString("1 STRIPE DETECTED", 10, 48);
            M5.Display.setTextSize(1);
            M5.Display.setCursor(10, 80);
            M5.Display.printf("Pulse Width: %u ms\r\n", pulse_durations[0]);
            break;

        case ID_ITEM_B:
            M5.Display.fillRect(0, 0, 240, 34, CYAN);
            M5.Display.setTextColor(BLACK);
            M5.Display.setTextSize(2);
            M5.Display.drawString("IDENTIFIED: BOX B", 10, 8);
            M5.Display.setTextColor(WHITE);
            M5.Display.setTextSize(2);
            M5.Display.drawString("2 STRIPES DETECTED", 10, 48);
            M5.Display.setTextSize(1);
            M5.Display.setCursor(10, 80);
            M5.Display.printf("P1: %u ms | P2: %u ms\r\n", pulse_durations[0], pulse_durations[1]);
            break;

        case ID_ITEM_C:
            M5.Display.fillRect(0, 0, 240, 34, YELLOW);
            M5.Display.setTextColor(BLACK);
            M5.Display.setTextSize(2);
            M5.Display.drawString("IDENTIFIED: BOX C", 10, 8);
            M5.Display.setTextColor(WHITE);
            M5.Display.setTextSize(2);
            M5.Display.drawString("3 STRIPES DETECTED", 10, 48);
            M5.Display.setTextSize(1);
            M5.Display.setCursor(10, 80);
            M5.Display.printf("[%u, %u, %u] ms\r\n", pulse_durations[0], pulse_durations[1], pulse_durations[2]);
            break;

        default:
            M5.Display.fillRect(0, 0, 240, 34, RED);
            M5.Display.setTextColor(WHITE);
            M5.Display.setTextSize(2);
            M5.Display.drawString("ERROR: UNKNOWN", 10, 8);
            M5.Display.setTextSize(1);
            M5.Display.setTextColor(LIGHTGREY);
            M5.Display.setCursor(10, 50);
            M5.Display.printf("Detected %u pulses (Out of bounds)\r\n", count);
            M5.Display.drawString("Check hand sweep speed / distance", 10, 75);
            break;
    }
}

void reset_detection_engine() {
    pulse_count = 0;
    is_reflecting = false;
    showing_result = false;
    state_transition_time = millis();
    display_idle_screen();
    Serial.println("[ENGINE] Reset complete. Front-end armed.\r");
}

void setup() {
    // 1. Configure M5Unified without taking over USB CDC
    auto cfg = M5.config();
    cfg.serial_baudrate = 0; 
    M5.begin(cfg);
    
    // 2. CRITICAL: Energize 5V Rail via PMIC for IR Hardware
    M5.Power.setExtOutput(true);

    M5.Display.setRotation(1);
    M5.Speaker.setVolume(120);

    // 3. Initialize Native USB CDC
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // Never stall the loop if terminal is detached

    // Allow terminal connection window
    uint32_t t_sync = millis();
    while (!Serial && (millis() - t_sync < 2000)) {
        delay(10);
    }

    Serial.println("\r\n==============================================");
    Serial.println("  M5StickS3 AUTONOMOUS RETROREFLECTIVE SCANNER");
    Serial.println("==============================================");
    Serial.println("[HARDWARE] EXT_5V_EN Rail: ACTIVE");
    Serial.printf("[HARDWARE] IR TX Carrier Pin: GPIO %d (38 kHz)\r\n", PIN_IR_TX);
    Serial.printf("[HARDWARE] IR RX Demod Pin:   GPIO %d (Active LOW)\r\n", PIN_IR_RX);
    Serial.printf("[PROFILE]  Min Pulse: %u ms | Gap Timeout: %u ms\r\n", MIN_VALID_PULSE_MS, GAP_TIMEOUT_MS);
    Serial.println("==============================================\r\n");

    setup_ir_hardware();
    reset_detection_engine();
}

void loop() {
    M5.update();
    uint32_t now_ms = millis();
    uint32_t now_us = micros();

    // Button Controls
    if (M5.BtnA.wasPressed()) {
        Serial.println("[EVENT] Button A pressed: Resetting engine.\r");
        reset_detection_engine();
        return;
    }
    if (M5.BtnB.wasPressed()) {
        diagnostic_stream = !diagnostic_stream;
        Serial.printf("[EVENT] Button B pressed: Real-Time Stream = %s\r\n", diagnostic_stream ? "ON" : "OFF");
        return;
    }

    // 1-Second Serial Heartbeat (Keeps Serial Active & Confirms Loop Execution)
    if (now_ms - last_heartbeat_ms >= 1000) {
        last_heartbeat_ms = now_ms;
        if (!showing_result) {
            Serial.printf("[HEARTBEAT] Up: %06u ms | RX State: %s | Active Pulses: %u\r\n",
                          now_ms, 
                          (digitalRead(PIN_IR_RX) == LOW) ? "REFLECTING" : "IDLE", 
                          pulse_count);
        }
    }

    // 1 kHz Optical Interrogation Engine
    if (now_us - last_sample_micros >= SAMPLE_INTERVAL_US) {
        last_sample_micros = now_us;
        bool detected = strobe_ir_probe();

        // Real-Time Optical Diagnostic Stream
        if (diagnostic_stream && detected) {
            Serial.println("  [PROBE] Real-time reflection detected (RX == LOW)");
        }

        // Freeze period while displaying result
        if (showing_result) {
            if (now_ms - result_display_start >= RESULT_HOLD_MS) {
                reset_detection_engine();
            }
            return;
        }

        // Live Optical Indicator on Display
        static bool last_indicator_state = false;
        if (detected != last_indicator_state && !showing_result) {
            last_indicator_state = detected;
            M5.Display.fillRect(10, 110, 14, 14, detected ? GREEN : DARKGREY);
        }

        // Edge Transition Detection
        if (detected != is_reflecting) {
            uint32_t elapsed = now_ms - state_transition_time;

            if (is_reflecting) {
                // Falling Edge: Exiting reflective surface
                if (elapsed >= MIN_VALID_PULSE_MS && elapsed <= MAX_VALID_PULSE_MS) {
                    if (pulse_count < 8) {
                        pulse_durations[pulse_count] = elapsed;
                        Serial.printf("  -> [PULSE LOGGED] Stripe #%u: Width = %u ms\r\n", pulse_count + 1, elapsed);
                        pulse_count++;
                    }
                    last_pulse_end_time = now_ms;
                } else {
                    Serial.printf("  -> [GLITCH REJECTED] Duration: %u ms (Valid range: %u - %u ms)\r\n", 
                                  elapsed, MIN_VALID_PULSE_MS, MAX_VALID_PULSE_MS);
                }
            } else {
                // Rising Edge: Entering reflective surface
                Serial.printf("[OPTICAL] Reflective edge detected after %u ms non-reflective gap.\r\n", elapsed);
            }

            is_reflecting = detected;
            state_transition_time = now_ms;
        }

        // Gap Timeout: Signals hand-sweep sequence completion
        if (!is_reflecting && pulse_count > 0) {
            if (now_ms - last_pulse_end_time >= GAP_TIMEOUT_MS) {
                IdentificationResult final_id = ID_ERR_UNKNOWN;

                if (pulse_count == 1)      final_id = ID_ITEM_A;
                else if (pulse_count == 2) final_id = ID_ITEM_B;
                else if (pulse_count == 3) final_id = ID_ITEM_C;

                Serial.println("\r\n----------------------------------------------");
                Serial.printf("[CLASSIFICATION] Inter-stripe timeout reached (%u ms).\r\n", GAP_TIMEOUT_MS);
                Serial.printf("[CLASSIFICATION] Total Valid Stripes Counted: %u\r\n", pulse_count);

                switch (final_id) {
                    case ID_ITEM_A:
                        Serial.println("[RESULT] >>> IDENTIFIED: BOX A (1 Stripe) <<<");
                        break;
                    case ID_ITEM_B:
                        Serial.println("[RESULT] >>> IDENTIFIED: BOX B (2 Stripes) <<<");
                        break;
                    case ID_ITEM_C:
                        Serial.println("[RESULT] >>> IDENTIFIED: BOX C (3 Stripes) <<<");
                        break;
                    default:
                        Serial.println("[RESULT] >>> ERROR: UNKNOWN PATTERN <<<");
                        break;
                }
                Serial.println("----------------------------------------------\r\n");

                showing_result = true;
                result_display_start = now_ms;
                display_result_screen(final_id, pulse_count);
                play_audio_feedback(final_id);
            }
        }
    }
}
