// Simplified ESP32 Code for Sending Temperature, Humidity, and THI to Google Sheets
// Using HDC1080 Sensor via I2C

// --- Required Libraries ---
#include <Wire.h>          // For I2C communication with HDC1080
#include <WiFi.h>          // For Wi-Fi connectivity
#include <HTTPClient.h>    // For making HTTP POST requests
#include <WiFiManager.h>   // For easy Wi-Fi configuration
#include "ClosedCube_HDC1080.h" // Library for the HDC1080 sensor

// --- Google Forms & Data Configuration ---
// IMPORTANT: Replace these with the actual entry IDs from your Google Form.
#define FORM_URL "https://docs.google.com/forms/d/e/1FAIpQLScjJwk2TyKyagz7QOSk-lzjIF0Vyvbm-LZkOq40O7Xx1c8Ceg/formResponse?"
#define entry_temperature  "entry.2126709213"
#define entry_humidity     "entry.1111155013"
#define entry_thi          "entry.42069"

// --- System Variables ---
#define SENSOR_READ_INTERVAL 30000 // Interval for sending data (30 seconds)
unsigned long lastSensorReadMillis = 0;

// Create an HDC1080 sensor object
ClosedCube_HDC1080 hdc1080;

// Variables to store sensor data
float temperature_val = 0.0f;
float humidity_val = 0.0f;
float thi_val = 0.0f;

// --- Utility Function for URL Encoding ---
String urlEncode(const String& str) {
  String encoded = "";
  const char* hex = "0123456789ABCDEF";
  for (char c : str) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0xF];
      encoded += hex[c & 0xF];
    }
  }
  return encoded;
}

// --- Function to connect to Wi-Fi using WiFiManager ---
void setup_wifi_manager() {
  Serial.println("\nStarting WiFiManager configuration...");
  WiFiManager wm;
  bool res = wm.autoConnect("ESP32_Data_Logger", "password");

  if (!res) {
    Serial.println("Failed to connect to WiFi! Restarting...");
    delay(5000);
    ESP.restart();
  } else {
    Serial.println("WiFi connected successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }
}

// --- Function to calculate Temperature Humidity Index (THI) ---
// Formula: THI = T - (0.55 - 0.55 * RH/100) * (T - 14.5)
float calculateTHI(float temp, float rh) {
  return temp - (0.55 - 0.55 * rh / 100.0) * (temp - 14.5);
}

// --- Function to Send Data to Google Form ---
void sendToGoogle() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Skipping submission.");
    return;
  }

  HTTPClient http;

  // Construct the POST data string
  String postData =
    String(entry_temperature) + "=" + urlEncode(String(temperature_val, 1)) +
    "&" + String(entry_humidity) + "=" + urlEncode(String(humidity_val, 1)) +
    "&" + String(entry_thi) + "=" + urlEncode(String(thi_val, 1));

  Serial.println("\n--- Sending Data to Google Form ---");
  Serial.print("POSTing data: ");
  Serial.println(postData);

  http.begin(FORM_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int responseCode = http.POST(postData);

  if (responseCode == HTTP_CODE_OK) {
    Serial.println("Data submitted successfully!");
  } else {
    Serial.print("Error: HTTP response code ");
    Serial.println(responseCode);
  }
  http.end();
  Serial.println("-----------------------------------\n");
}

// --- Main Setup Function ---
void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\n--- ESP32 Data Logger Initializing ---");

  // Initialize I2C communication
  Wire.begin();
  
  // Initialize HDC1080 sensor
  hdc1080.begin(0x40);
  Serial.println("HDC1080 sensor initialized.");

  setup_wifi_manager();
}

// --- Main Loop Function ---
void loop() {
  // Ensure Wi-Fi connection is maintained
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Attempting to reconnect...");
    setup_wifi_manager();
  }

  // Time-based actions for data submission
  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorReadMillis >= SENSOR_READ_INTERVAL) {
    lastSensorReadMillis = currentMillis;

    // Read values from HDC1080 sensor
    temperature_val = hdc1080.readTemperature();
    humidity_val = hdc1080.readHumidity();

    // Calculate THI
    thi_val = calculateTHI(temperature_val, humidity_val);

    Serial.println("\n--- Calculated Values ---");
    Serial.print("Temperature: "); Serial.println(temperature_val);
    Serial.print("Humidity: "); Serial.println(humidity_val);
    Serial.print("THI: "); Serial.println(thi_val);

    // Send combined data to Google Form
    sendToGoogle();
  }

  delay(10);
}
