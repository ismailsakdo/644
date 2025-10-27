// Simplified ESP32 Code for Sending Simulated Temperature, Humidity, and THI to Google Sheets
//https://docs.google.com/forms/d/e/1FAIpQLSc-OT6vLIN4JkxPS5U8ll4PRBiOq4Y7wPqQW81nTEKRpdomBQ/viewform?usp=pp_url&entry.772925471=1&entry.54319464=2&entry.1261448659=3

// --- Required Libraries ---
#include <WiFi.h>          // For Wi-Fi connectivity
#include <HTTPClient.h>    // For making HTTP POST requests
#include <WiFiManager.h>   // For easy Wi-Fi configuration

// --- Google Forms & Data Configuration ---
// Google Form URL for data submission
#define FORM_URL "https://docs.google.com/forms/d/e/1FAIpQLSc-OT6vLIN4JkxPS5U8ll4PRBiOq4Y7wPqQW81nTEKRpdomBQ/formResponse?"

// Google Form Entry IDs
// IMPORTANT: Replace these with your actual Google Form field IDs.
#define entry_temperature  "entry.772925471"
#define entry_humidity     "entry.54319464"
#define entry_thi          "entry.1261448659"

// --- System Variables ---
#define SERIAL_BAUD 115200         // Baud rate for serial output
#define DATA_SEND_INTERVAL 30000   // Interval for sending data (30 seconds)
unsigned long lastSendMillis = 0;

// Variables to store simulated data
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

void setup() {
  // Initialize serial communication and seed the random number generator
  Serial.begin(SERIAL_BAUD);
  while (!Serial);
  Serial.println("\n--- ESP32 Data Simulation Initializing ---");
  randomSeed(analogRead(0)); // Seed random number generator for better randomness

  setup_wifi_manager();
}

void loop() {
  // Ensure Wi-Fi connection is maintained
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Attempting to reconnect...");
    setup_wifi_manager();
  }

  // Time-based actions for data submission
  unsigned long currentMillis = millis();
  if (currentMillis - lastSendMillis >= DATA_SEND_INTERVAL) {
    lastSendMillis = currentMillis;

    // --- Generate simulated temperature and humidity data ---
    temperature_val = random(200, 300) / 10.0; // Random temp between 20.0 and 30.0 C
    humidity_val = random(400, 600) / 10.0;    // Random humidity between 40.0 and 60.0 %

    // Calculate THI
    thi_val = calculateTHI(temperature_val, humidity_val);

    Serial.println("\n--- Simulated Values ---");
    Serial.print("Temperature: "); Serial.print(temperature_val, 1); Serial.println(" °C");
    Serial.print("Humidity: "); Serial.print(humidity_val, 1); Serial.println(" %");
    Serial.print("THI: "); Serial.println(thi_val, 1);

    // Send combined data to Google Form
    sendToGoogle();
  }

  delay(10);
}
