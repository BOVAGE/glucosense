/**
 * GlucoSense ESP32 PPG Data Acquisition Firmware
 * 
 * Target Hardware:
 *   - ESP32 Development Board
 *   - MAX30100 PPG Sensor (GY-MAX30100) connected to I2C (SDA=21, SCL=22)
 *   - SSD1306 0.96" OLED Display connected to I2C (SDA=21, SCL=22)
 *   - Green Start Button: Pin 12 (with internal pullup, short to GND when pressed)
 *   - Red Refresh Button: Pin 14 (with internal pullup, short to GND when pressed)
 * 
 * Required Arduino Libraries:
 *   - MAX30100 (by oxullo)
 *   - Adafruit SSD1306 (by Adafruit)
 *   - Adafruit GFX (by Adafruit)
 *   - ArduinoJson (by Benoit Blanchon, v6.x or v7.x)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MAX30100.h>
#include <ArduinoJson.h>

// ==========================================
// USER CONFIGURATION (EDIT THESE CONSTANTS)
// ==========================================
#define WIFI_SSID "BOVAGE_MIFI"
#define WIFI_PASSWORD "Fuckyouman"

#define SUPABASE_URL "https://neafiznfdmlyuyzhjaok.supabase.co"
#define SUPABASE_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im5lYWZpem5mZG1seXV5emhqYW9rIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODIxMzE2NjAsImV4cCI6MjA5NzcwNzY2MH0.eZlIBStgtcB8l-O44CcsgT8d2mrr2M3Ye8Z7CiS-E1Q"
// ==========================================

// Hardware Pins Configuration
#define BTN_START 12   // Green button
#define BTN_REFRESH 14 // Red button

// OLED Display Parameters
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// MAX30100 Sensor Instance
MAX30100 sensor;

// States Definition
enum SystemState {
  STATE_BOOTING,
  STATE_CONNECTING_WIFI,
  STATE_POLLING,
  STATE_READY,
  STATE_RECORDING,
  STATE_UPLOADING,
  STATE_SAVED,
  STATE_ERROR
};

SystemState currentState = STATE_BOOTING;

// Session Tracking Variables
String activeSessionId = "";
String activeParticipantId = "";
String activeSessionKind = "";

// PPG Sampling Parameters
const int SAMPLING_RATE_HZ = 50;
const int SAMPLE_INTERVAL_MS = 20; // 1000ms / 50Hz = 20ms
const int STABILITY_SECS = 10;     // 10 seconds stabilization
const int RECORDING_SECS = 60;     // 60 seconds of signal
const int TOTAL_SECS = STABILITY_SECS + RECORDING_SECS;
const int TOTAL_SAMPLES = TOTAL_SECS * SAMPLING_RATE_HZ; // 70 * 50 = 3500 samples

// PPG Data Buffer
// 3,500 uint16_t samples = 7KB of RAM. Highly stable on ESP32.
uint16_t ppgBuffer[TOTAL_SAMPLES]; 

// Timer variables
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL_MS = 3000; // Poll Supabase every 3 seconds

// Display Helper Function
void updateDisplay(String header, String line1, String line2, String line3, String line4) {
  display.clearDisplay();
  
  // Header Area
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(header);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);
  
  // Body Lines
  display.setCursor(0, 16);
  display.print(line1);
  
  display.setCursor(0, 28);
  display.print(line2);
  
  display.setCursor(0, 40);
  display.print(line3);
  
  display.setCursor(0, 52);
  display.print(line4);
  
  display.display();
}

void setup() {
  Serial.begin(115200);
  
  // Initialize Hardware Buttons (active low with internal pullup)
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_REFRESH, INPUT_PULLUP);
  
  // Initialize Wire (I2C)
  Wire.begin();
  
  // Initialize SSD1306 OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED initialization failed"));
    currentState = STATE_ERROR;
    return;
  }
  
  updateDisplay("GLUCOSENSE v1.0", "", "  Booting device...", "", "");
  delay(1000);
  
  // Initialize MAX30100 PPG Sensor
  if (!sensor.begin()) {
    Serial.println(F("MAX30100 initialization failed"));
    updateDisplay("GLUCOSENSE ERROR", "Sensor failed!", "Check GY-MAX30100", "connections.", "Rebooting...");
    delay(5000);
    ESP.restart();
  }
  
  // Configure MAX30100 Register Settings matching working parameters
  sensor.setMode(MAX30100_MODE_SPO2_HR); // SPO2 mode enables both Red and IR LEDs
  sensor.setLedsCurrent(MAX30100_LED_CURR_50MA, MAX30100_LED_CURR_27_1MA); // Set IR current and reduce Red current to prevent clipping
  sensor.setLedsPulseWidth(MAX30100_SPC_PW_1600US_16BITS); // Max resolution
  sensor.setSamplingRate(MAX30100_SAMPRATE_50HZ); // 50 Hz sampling rate
  sensor.setHighresModeEnabled(true); // Enable 16-bit ADC resolution
  
  currentState = STATE_CONNECTING_WIFI;
}

void loop() {
  switch (currentState) {
    case STATE_CONNECTING_WIFI:
      connectWiFi();
      break;
      
    case STATE_POLLING:
      pollForActiveSession();
      break;
      
    case STATE_READY:
      handleReadyState();
      break;
      
    case STATE_RECORDING:
      recordPPGData();
      break;
      
    case STATE_UPLOADING:
      uploadData();
      break;
      
    case STATE_SAVED:
      handleSavedState();
      break;
      
    case STATE_ERROR:
      updateDisplay("SYSTEM ERROR", "Fatal Error occurred", "Check connections", "or verify setup.", "Press RESET button");
      delay(2000);
      break;
      
    default:
      break;
  }
}

// ----------------------------------------------------
// STATE HANDLERS & ACTION SUBROUTINES
// ----------------------------------------------------

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempt = 0;
  
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED && attempt < 25) {
    attempt++;
    String dots = "";
    for(int i=0; i<(attempt%4); i++) dots += ".";
    updateDisplay("WIFI CONNECTION", "SSID: " + String(WIFI_SSID), "Status: Connecting" + dots, "", "Hold on...");
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connected.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    currentState = STATE_POLLING;
  } else {
    Serial.println("\nConnection failed. Retrying...");
    updateDisplay("WIFI ERROR", "Failed to connect", "to " + String(WIFI_SSID), "Retrying connection", "");
    delay(2000);
  }
}

void pollForActiveSession() {
  // Ensure non-blocking polling timer
  if (millis() - lastPollTime < POLL_INTERVAL_MS) {
    return;
  }
  lastPollTime = millis();
  
  if (WiFi.status() != WL_CONNECTED) {
    currentState = STATE_CONNECTING_WIFI;
    return;
  }
  
  updateDisplay("POLLING SUPABASE", "Checking for sessions", "Status: Polling...", "", "WiFi: " + WiFi.localIP().toString());
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure(); // Bypass certificate validation for testing simplicity
  
  // URL to query for first pending session ordered by creation time from the join view
  String url = String(SUPABASE_URL) + "/rest/v1/v_sessions_details?status=eq.pending&order=created_at.asc&limit=1";
  
  if (http.begin(client, url)) {
    http.setConnectTimeout(2000);
    http.setTimeout(2000);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
    
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
      String response = http.getString();
      Serial.println("Polling Response: " + response);
      
      // Parse JSON array
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, response);
      
      if (!error && doc.size() > 0) {
        JsonObject session = doc[0];
        activeSessionId = session["id"].as<String>();
        activeParticipantId = session["participant_id"].as<String>();
        activeSessionKind = session["session_kind"].as<String>();
        
        Serial.println("Active Session Found: ID=" + activeSessionId + ", Participant=" + activeParticipantId);
        currentState = STATE_READY;
      } else {
        updateDisplay("POLLING SUPABASE", "No pending sessions", "Ready for dispatch", "from web dashboard.", "Waiting...");
      }
    } else {
      Serial.print("Error during polling: ");
      Serial.println(httpResponseCode);
      updateDisplay("HTTP ERROR", "Code: " + String(httpResponseCode), "Checking endpoint...", "Retrying...", "");
    }
    http.end();
  }
}

void handleReadyState() {
  // Display session information on screen and await Start button
  String kindText = activeSessionKind;
  if(kindText == "fasting") kindText = "Fasting";
  else if(kindText == "1hr_post_prandial") kindText = "1h Post Prandial";
  else if(kindText == "2hr_post_prandial") kindText = "2h Post Prandial";
  
  updateDisplay("READY TO ACQUIRE", 
                "ID: " + activeParticipantId, 
                "Kind: " + kindText, 
                "GRN = Start Record", 
                "RED = Reject/Refresh");
                
  // Check buttons
  if (digitalRead(BTN_START) == LOW) { // GREEN button pressed
    delay(50); // Debounce
    if (digitalRead(BTN_START) == LOW) {
      currentState = STATE_RECORDING;
    }
  }
  
  if (digitalRead(BTN_REFRESH) == LOW) { // RED button pressed
    delay(50); // Debounce
    if (digitalRead(BTN_REFRESH) == LOW) {
      // Clear active and go back to polling
      activeSessionId = "";
      activeParticipantId = "";
      activeSessionKind = "";
      currentState = STATE_POLLING;
      delay(500);
    }
  }
}

void patchSessionStatus(String status) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  
  String url = String(SUPABASE_URL) + "/rest/v1/ppg_sessions?id=eq." + activeSessionId;
  
  if (http.begin(client, url)) {
    http.setConnectTimeout(2000);
    http.setTimeout(2000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
    
    String payload = "{\"status\":\"" + status + "\"}";
    int httpCode = http.sendRequest("PATCH", payload);
    Serial.println("PATCH status response: " + String(httpCode));
    http.end();
  }
}

void recordPPGData() {
  Serial.println("Starting acquisition sequence...");
  
  // 1. Inform Supabase that we are now recording (timeout configured to 5s max)
  patchSessionStatus("recording");
  
  // 2. Perform a full I2C bus recovery reset to release any stuck SDA/SCL lines
  Serial.println("Resetting I2C bus...");
  Wire.end();
  delay(50);
  Wire.begin();
  delay(50);
  
  // 3. Re-initialize the MAX30100 sensor fresh (recovers from long-idle FIFO overflow)
  Serial.println("Initializing MAX30100 sensor...");
  if (!sensor.begin()) {
    Serial.println("MAX30100 initialization FAILED!");
    updateDisplay("SENSOR ERROR", "Failed to initialize", "MAX30100 sensor.", "Check connections.", "Press RED to reset");
    currentState = STATE_ERROR;
    return;
  }
  Serial.println("MAX30100 initialized successfully.");
  
  // 4. Re-apply register settings
  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setLedsCurrent(MAX30100_LED_CURR_50MA, MAX30100_LED_CURR_27_1MA);
  sensor.setLedsPulseWidth(MAX30100_SPC_PW_1600US_16BITS);
  sensor.setSamplingRate(MAX30100_SAMPRATE_50HZ);
  sensor.setHighresModeEnabled(true);
  
  // 5. Reset hardware FIFO pointer
  sensor.resetFifo();
  delay(10);
  
  // STABILITY PHASE (10 seconds)
  // Let the participant place their finger and allow the automatic gain control circuit in the MAX30100 to settle
  for (int sec = STABILITY_SECS; sec > 0; sec--) {
    updateDisplay("STABILIZATION", 
                  "Hold finger still!", 
                  "Settle time left: " + String(sec) + "s", 
                  "DO NOT MOVE SENSOR", 
                  "");
    
    // During this phase, we call sensor.update() repeatedly and drain the buffer to let automatic current settle
    unsigned long startSec = millis();
    while (millis() - startSec < 1000) {
      sensor.update();
      uint16_t dummyIr, dummyRed;
      while (sensor.getRawValues(&dummyIr, &dummyRed)) {
        // Pop and discard stability readings to keep the circular buffer moving
      }
      delay(1);
    }
  }
  
  // DATA ACQUISITION PHASE
  Serial.println("Recording active signal...");
  int sampleIdx = 0;
  
  // Drain any residual samples from the software buffer (no hardware reset needed as it was kept clear)
  uint16_t tempIr, tempRed;
  int drainedCount = 0;
  while (sensor.getRawValues(&tempIr, &tempRed)) {
    drainedCount++;
  }
  Serial.print("Drained ");
  Serial.print(drainedCount);
  Serial.println(" residual samples from circular buffer.");
  
  Serial.println("Starting acquisition loop...");
  
  while (sampleIdx < TOTAL_SAMPLES) {
    // Poll the sensor FIFO registers and read them into the library's buffer
    sensor.update();
    
    uint16_t irVal, redVal;
    // Pop all available samples from the library's buffer
    while (sensor.getRawValues(&irVal, &redVal) && sampleIdx < TOTAL_SAMPLES) {
      // Write sample (Infrared LED represents target AC/DC PPG signal)
      ppgBuffer[sampleIdx] = irVal;
      sampleIdx++;
      
      // Update display details every 1 second (50 samples)
      if (sampleIdx % 50 == 0) {
        int secondsElapsed = sampleIdx / 50;
        int secondsLeft = TOTAL_SECS - secondsElapsed;
        if (secondsLeft < 0) secondsLeft = 0;
        
        // Construct progress bar string
        String bar = "[";
        int filledChars = (sampleIdx * 10) / TOTAL_SAMPLES;
        for (int i = 0; i < 10; i++) {
          if (i < filledChars) bar += "=";
          else bar += " ";
        }
        bar += "]";
        
        updateDisplay("ACQUISITION: 50Hz", 
                      "Time Left: " + String(secondsLeft) + "s", 
                      "Samples: " + String(sampleIdx) + "/" + String(TOTAL_SAMPLES), 
                      bar, 
                      "Measuring PPG...");
        
        Serial.print("Acquired ");
        Serial.print(sampleIdx);
        Serial.print("/");
        Serial.print(TOTAL_SAMPLES);
        Serial.println(" samples...");
      }
    }
    
    // Small delay (5ms) to prevent I2C bus flooding and allow background task execution
    delay(5);
  }
  
  Serial.println("Acquisition loop completed successfully.");
  currentState = STATE_UPLOADING;
}

void uploadData() {
  updateDisplay("UPLOADING DATA", "Acquisition done.", "Serializing packet...", "Uploading to cloud...", "");
  Serial.println("Serializing data to JSON payload...");
  
  if (WiFi.status() != WL_CONNECTED) {
    currentState = STATE_CONNECTING_WIFI;
    return;
  }
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  
  String url = String(SUPABASE_URL) + "/rest/v1/ppg_sessions?id=eq." + activeSessionId;
  
  if (http.begin(client, url)) {
    http.setConnectTimeout(2000);
    http.setTimeout(2000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
    http.addHeader("Prefer", "return=minimal");
    
    // Build JSON manually to prevent massive memory overhead of deserializing inside dynamic maps.
    // 3500 samples * ~5 chars per sample + formatting = ~21KB string. 
    // We pre-allocate heap memory to prevent memory fragmentation.
    String body;
    body.reserve(28000);
    
    body += "{\"status\":\"uploaded\",\"ppg_data\":[";
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
      body += ppgBuffer[i];
      if (i < TOTAL_SAMPLES - 1) {
        body += ",";
      }
    }
    body += "]}";
    
    Serial.println("Sending PATCH request payload of size: " + String(body.length()) + " bytes");
    
    int httpResponseCode = http.sendRequest("PATCH", body);
    http.end();
    
    if (httpResponseCode >= 200 && httpResponseCode < 300) {
      Serial.println("Data uploaded successfully!");
      currentState = STATE_SAVED;
    } else {
      Serial.print("HTTP Upload Failed, Code: ");
      Serial.println(httpResponseCode);
      
      updateDisplay("UPLOAD FAILED", 
                    "HTTP Code: " + String(httpResponseCode), 
                    "Network drop?", 
                    "GRN = Retry Upload", 
                    "RED = Cancel & Reset");
                    
      // Wait for researcher decision
      while (true) {
        if (digitalRead(BTN_START) == LOW) {
          delay(50);
          if (digitalRead(BTN_START) == LOW) {
            currentState = STATE_UPLOADING; // Loop back and call uploadData again
            delay(500);
            break;
          }
        }
        if (digitalRead(BTN_REFRESH) == LOW) {
          delay(50);
          if (digitalRead(BTN_REFRESH) == LOW) {
            // Discard session and return to polling state
            activeSessionId = "";
            activeParticipantId = "";
            activeSessionKind = "";
            currentState = STATE_POLLING;
            delay(500);
            break;
          }
        }
        delay(10);
      }
    }
  } else {
    Serial.println("Failed to connect to Supabase endpoint");
    updateDisplay("CONN ERROR", 
                  "Could not connect", 
                  "to endpoint.", 
                  "GRN = Retry Upload", 
                  "RED = Cancel & Reset");
                  
    // Wait for researcher decision
    while (true) {
      if (digitalRead(BTN_START) == LOW) {
        delay(50);
        if (digitalRead(BTN_START) == LOW) {
          currentState = STATE_UPLOADING;
          delay(500);
          break;
        }
      }
      if (digitalRead(BTN_REFRESH) == LOW) {
        delay(50);
        if (digitalRead(BTN_REFRESH) == LOW) {
          activeSessionId = "";
          activeParticipantId = "";
          activeSessionKind = "";
          currentState = STATE_POLLING;
          delay(500);
          break;
        }
      }
      delay(10);
    }
  }
}

void handleSavedState() {
  updateDisplay("SAVED OK ✓", 
                "ID: " + activeParticipantId, 
                "Enter BGL on website", 
                "-----------------", 
                "RED = Next Session");
                
  // Await researcher confirmation via Red Refresh button to reset back to polling
  if (digitalRead(BTN_REFRESH) == LOW) {
    delay(50);
    if (digitalRead(BTN_REFRESH) == LOW) {
      // Clear data and return to polling loop
      activeSessionId = "";
      activeParticipantId = "";
      activeSessionKind = "";
      currentState = STATE_POLLING;
      delay(500);
    }
  }
}
