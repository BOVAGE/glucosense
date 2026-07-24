/**
 * GlucoSense v2.1 — 100% Offline On-Device ML Inference Firmware
 * 
 * Target Hardware:
 *   - ESP32 Development Board
 *   - GY-MAX30100 PPG Sensor connected to I2C (SDA=21, SCL=22)
 *   - SSD1306 OLED Display (128x64) connected to I2C (SDA=21, SCL=22)
 *   - Green Start Button: Pin 12 (Internal Pullup)
 *   - Red Reset Button: Pin 14 (Internal Pullup)
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MAX30100.h>
#include "model_coefficients.h"

// Hardware Pins
#define BTN_START 12   // Green button
#define BTN_REFRESH 14 // Red button

// OLED Parameters
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Sensor Instance
MAX30100 sensor;

// FSM States
enum SystemState {
  STATE_BOOTING,
  STATE_READY,
  STATE_RECORDING,
  STATE_PROCESSING,
  STATE_SAVED,
  STATE_ERROR
};

SystemState currentState = STATE_BOOTING;

// PPG Parameters
const int SAMPLING_RATE_HZ = 50;
const int STABILITY_SECS = 10;     // 10s stabilization (discarded on-device)
const int RECORDING_SECS = 60;     // 60s active measurement (stored in buffer)
const int TOTAL_SECS = STABILITY_SECS + RECORDING_SECS;
const int TOTAL_SAMPLES = TOTAL_SECS * SAMPLING_RATE_HZ; // 70 * 50 = 3500 samples

// Buffer to store the active 3,500 raw samples
uint16_t ppgBuffer[TOTAL_SAMPLES];

// Prediction variables
float finalHR = 0.0;
float finalBGL = 0.0;
int finalBeats = 0;

// ----------------------------------------------------
// IIR BUTTERWORTH FILTER CONFIGURATION
// ----------------------------------------------------
// 3rd-order Butterworth bandpass filter coefficients (0.5 - 8.0 Hz @ 50 Hz)
// Creates a 6th-order Direct Form I IIR difference equation.
const float b_coef[7] = { 0.0495329964f, 0.0f, -0.1485989891f, 0.0f, 0.1485989891f, 0.0f, -0.0495329964f };
const float a_coef[7] = { 1.0f, -4.0205511339f, 6.7796971838f, -6.2901289387f, 3.4648889390f, -1.0715499578f, 0.1377613013f };

// Filter history shift registers
float x_history[6] = {0.0f};
float y_history[6] = {0.0f};

// Reset filter state
void resetFilter() {
  for (int i = 0; i < 6; i++) {
    x_history[i] = 0.0f;
    y_history[i] = 0.0f;
  }
}

// Single step filtering
float filterSample(float x) {
  float y = b_coef[0] * x 
          + b_coef[1] * x_history[0] 
          + b_coef[2] * x_history[1] 
          + b_coef[3] * x_history[2] 
          + b_coef[4] * x_history[3] 
          + b_coef[5] * x_history[4] 
          + b_coef[6] * x_history[5]
          - a_coef[1] * y_history[0] 
          - a_coef[2] * y_history[1] 
          - a_coef[3] * y_history[2] 
          - a_coef[4] * y_history[3] 
          - a_coef[5] * y_history[4] 
          - a_coef[6] * y_history[5];
          
  // Shift history registers
  for (int i = 5; i > 0; i--) {
    x_history[i] = x_history[i-1];
    y_history[i] = y_history[i-1];
  }
  x_history[0] = x;
  y_history[0] = y;
  
  return y;
}

// Helper to compute moving average matching SciPy's uniform_filter1d with mode="nearest"
void computeMovingAverage(float* input, float* output, int len, int windowSize) {
  int startOffset = -windowSize / 2;
  int endOffset = (windowSize % 2 == 0) ? (windowSize / 2 - 1) : (windowSize / 2);
  
  for (int i = 0; i < len; i++) {
    float sum = 0.0f;
    for (int w = startOffset; w <= endOffset; w++) {
      int idx = i + w;
      // mode="nearest": clamp index to boundary limits
      if (idx < 0) idx = 0;
      if (idx >= len) idx = len - 1;
      sum += input[idx];
    }
    output[i] = sum / (float)windowSize;
  }
}

// Display Helper
void updateDisplay(String header, String line1, String line2, String line3, String line4) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(header);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);
  
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
  
  // Setup Buttons
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_REFRESH, INPUT_PULLUP);
  
  // Setup Wire I2C
  Wire.begin();
  
  // Initialize Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed!");
    currentState = STATE_ERROR;
    return;
  }
  
  updateDisplay("GLUCOSENSE v2.1", "", "  Booting device...", "  [OFFLINE MODE]", "");
  delay(1500);
  
  // Initialize Sensor
  if (!sensor.begin()) {
    Serial.println("MAX30100 init failed!");
    updateDisplay("GLUCOSENSE ERROR", "Sensor failed!", "Check GY-MAX30100", "connections.", "Rebooting...");
    delay(5000);
    ESP.restart();
  }
  
  // Configure MAX30100 Register Settings
  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setLedsCurrent(MAX30100_LED_CURR_50MA, MAX30100_LED_CURR_27_1MA);
  sensor.setLedsPulseWidth(MAX30100_SPC_PW_1600US_16BITS);
  sensor.setSamplingRate(MAX30100_SAMPRATE_50HZ);
  sensor.setHighresModeEnabled(true);
  
  currentState = STATE_READY;
}

void loop() {
  switch (currentState) {
    case STATE_READY:
      handleReadyState();
      break;
      
    case STATE_RECORDING:
      recordPPGData();
      break;
      
    case STATE_PROCESSING:
      processDataAndPredict();
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

void handleReadyState() {
  updateDisplay("GLUCOSENSE READY", 
                "Place index finger", 
                "flat on sensor window", 
                "-----------------", 
                "GRN = Start 70s Scan");
                
  if (digitalRead(BTN_START) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BTN_START) == LOW) {
      currentState = STATE_RECORDING;
      delay(500); // Debounce delay
    }
  }
}

void recordPPGData() {
  Serial.println("Starting offline acquisition...");
  
  // Reset I2C bus & sensor pointer
  Wire.end();
  delay(50);
  Wire.begin();
  delay(50);
  
  if (!sensor.begin()) {
    Serial.println("MAX30100 initialization failed on retry");
    currentState = STATE_ERROR;
    return;
  }
  
  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setLedsCurrent(MAX30100_LED_CURR_50MA, MAX30100_LED_CURR_27_1MA);
  sensor.setLedsPulseWidth(MAX30100_SPC_PW_1600US_16BITS);
  sensor.setSamplingRate(MAX30100_SAMPRATE_50HZ);
  sensor.setHighresModeEnabled(true);
  sensor.resetFifo();
  delay(10);
  
  // 1. Stabilization Phase (10 seconds, values discarded)
  for (int sec = STABILITY_SECS; sec > 0; sec--) {
    updateDisplay("STABILIZATION", 
                  "Hold finger still!", 
                  "Settle time left: " + String(sec) + "s", 
                  "DO NOT MOVE SENSOR", 
                  "");
    unsigned long startSec = millis();
    while (millis() - startSec < 1000) {
      sensor.update();
      uint16_t dummyIr, dummyRed;
      while (sensor.getRawValues(&dummyIr, &dummyRed)) { }
      delay(1);
    }
  }
  
  // Clean residual circular buffer
  uint16_t tempIr, tempRed;
  while (sensor.getRawValues(&tempIr, &tempRed)) { }
  
  // 2. Data Acquisition Loop (70 seconds = 3500 samples stored)
  Serial.println("Recording samples...");
  int sampleIdx = 0;
  
  while (sampleIdx < TOTAL_SAMPLES) {
    sensor.update();
    uint16_t irVal, redVal;
    
    while (sensor.getRawValues(&irVal, &redVal) && sampleIdx < TOTAL_SAMPLES) {
      ppgBuffer[sampleIdx] = irVal;
      sampleIdx++;
      
      if (sampleIdx % 50 == 0) {
        int secondsElapsed = sampleIdx / 50;
        int secondsLeft = TOTAL_SECS - secondsElapsed;
        if (secondsLeft < 0) secondsLeft = 0;
        
        // Progress Bar
        String bar = "[";
        int filled = (sampleIdx * 10) / TOTAL_SAMPLES;
        for (int i = 0; i < 10; i++) {
          if (i < filled) bar += "=";
          else bar += " ";
        }
        bar += "]";
        
        updateDisplay("ACQUISITION: 50Hz", 
                      "Time Left: " + String(secondsLeft) + "s", 
                      "Samples: " + String(sampleIdx) + "/" + String(TOTAL_SAMPLES), 
                      bar, 
                      "Measuring PPG...");
      }
    }
    delay(5);
  }
  
  Serial.println("Acquisition complete.");
  currentState = STATE_PROCESSING;
}

// ----------------------------------------------------
// ON-DEVICE DIGITAL SIGNAL PROCESSING & INFERENCE
// ----------------------------------------------------

void processDataAndPredict() {
  updateDisplay("PROCESSING...", "Running filters...", "Extracting features...", "Computing TinyML...", "Hold on...");
  Serial.println("Starting signal processing pipeline (Elgendi-aligned)...");
  
  const int analysisLen = 3000; // Perform DSP on the stable 3,000 samples
  float* filtered = (float*)malloc(analysisLen * sizeof(float));
  float* squared = (float*)malloc(analysisLen * sizeof(float));
  float* w1 = (float*)malloc(analysisLen * sizeof(float));
  float* w2 = (float*)malloc(analysisLen * sizeof(float));
  
  if (filtered == NULL || squared == NULL || w1 == NULL || w2 == NULL) {
    Serial.println("Memory allocation failed in DSP pipeline!");
    updateDisplay("DSP ERROR", "Out of Memory", "Alloc failed.", "Press RED to restart", "");
    if (filtered) free(filtered);
    if (squared) free(squared);
    if (w1) free(w1);
    if (w2) free(w2);
    currentState = STATE_ERROR;
    return;
  }
  
  // Step 1: Pre-run IIR filter on the first 500 stabilization samples to settle transient ringing
  resetFilter();
  for (int i = 0; i < 500; i++) {
    filterSample((float)ppgBuffer[i]);
  }
  
  // Filter the active 3,000 samples
  for (int i = 0; i < analysisLen; i++) {
    filtered[i] = filterSample((float)ppgBuffer[500 + i]);
  }
  
  // Step 2: Squaring Signal (setting negative values to 0 and squaring positive ones, matching Elgendi)
  for (int i = 0; i < analysisLen; i++) {
    float val = filtered[i];
    if (val < 0.0f) val = 0.0f;
    squared[i] = val * val;
  }
  
  // Step 3: Compute Dual Moving Averages (matching NeuroKit2 Elgendi defaults at 50Hz)
  // W1 = peakwindow (111ms) = round(0.111 * 50) = 6 samples
  // W2 = beatwindow (667ms) = round(0.667 * 50) = 33 samples
  computeMovingAverage(squared, w1, analysisLen, 6);
  computeMovingAverage(squared, w2, analysisLen, 33);
  
  // Step 4: Trace Blocks and Locate Systolic Peaks (Elgendi Method)
  float sumSquared = 0.0f;
  for (int i = 0; i < analysisLen; i++) {
    sumSquared += squared[i];
  }
  float beta = 0.02f * (sumSquared / analysisLen); // Threshold offset coefficient
  
  int peakIndices[120];
  int peakCount = 0;
  bool inBlock = false;
  int blockStart = 0;
  
  for (int i = 0; i < analysisLen; i++) {
    bool active = (w1[i] > (w2[i] + beta));
    
    if (active && !inBlock) {
      inBlock = true;
      blockStart = i;
    } else if (!active && inBlock) {
      inBlock = false;
      int blockEnd = i - 1;
      
      // Locate the index of the maximum value of the FILTERED signal inside this block
      float maxVal = -999999.0f;
      int maxIdx = blockStart;
      for (int j = blockStart; j <= blockEnd; j++) {
        if (filtered[j] > maxVal) {
          maxVal = filtered[j];
          maxIdx = j;
        }
      }
      
      // Enforce a minimum block size matching Elgendi peakwindow (6 samples / 120ms)
      if ((blockEnd - blockStart + 1) >= 6) {
        peakIndices[peakCount] = maxIdx;
        peakCount++;
        if (peakCount >= 120) break;
      }
    }
  }
  
  // Handle edge block open at sample 2999
  if (inBlock && peakCount < 120) {
    int blockEnd = 2999;
    float maxVal = -999999.0f;
    int maxIdx = blockStart;
    for (int j = blockStart; j <= blockEnd; j++) {
      if (filtered[j] > maxVal) {
        maxVal = filtered[j];
        maxIdx = j;
      }
    }
    if ((blockEnd - blockStart + 1) >= 6) {
      peakIndices[peakCount] = maxIdx;
      peakCount++;
    }
  }
  
  Serial.print("Detected systolic peaks count: ");
  Serial.println(peakCount);
  
  // Step 5: Feature Extraction
  float meanHR = 72.0f; // Default baseline fallback
  float sdnn = 0.0f;
  float rmssd = 0.0f;
  float ppgAmp = 0.0f;
  
  if (peakCount >= 5) {
    int intervalsCount = peakCount - 1;
    float* nnIntervals = (float*)malloc(intervalsCount * sizeof(float));
    
    if (nnIntervals != NULL) {
      float sumIbis = 0.0f;
      for (int i = 0; i < intervalsCount; i++) {
        // Interval in ms: sample difference * 20.0ms (50Hz)
        nnIntervals[i] = (float)(peakIndices[i+1] - peakIndices[i]) * 20.0f;
        sumIbis += nnIntervals[i];
      }
      float meanIbi = sumIbis / intervalsCount;
      meanHR = 60000.0f / meanIbi;
      
      // SDNN
      float varianceSum = 0.0f;
      for (int i = 0; i < intervalsCount; i++) {
        varianceSum += pow(nnIntervals[i] - meanIbi, 2);
      }
      sdnn = sqrt(varianceSum / intervalsCount);
      
      // RMSSD
      float sqDiffSum = 0.0f;
      for (int i = 0; i < intervalsCount - 1; i++) {
        float diff = nnIntervals[i+1] - nnIntervals[i];
        sqDiffSum += pow(diff, 2);
      }
      rmssd = sqrt(sqDiffSum / (intervalsCount - 1));
      
      free(nnIntervals);
    }
  } else {
    Serial.println("Warning: Insufficient systolic peaks detected!");
  }
  
  // Calculate PPG Amplitude (Standard deviation of the filtered wave)
  float sumFiltered = 0.0f;
  for (int i = 0; i < analysisLen; i++) {
    sumFiltered += filtered[i];
  }
  float meanFiltered = sumFiltered / analysisLen;
  
  float varFilteredSum = 0.0f;
  for (int i = 0; i < analysisLen; i++) {
    varFilteredSum += pow(filtered[i] - meanFiltered, 2);
  }
  ppgAmp = sqrt(varFilteredSum / analysisLen);
  
  // Free buffers
  free(filtered);
  free(squared);
  free(w1);
  free(w2);
  
  // Step 6: Evaluate Random Forest Regressor via predictBGL
  float predictedBGL = predictBGL(meanHR, sdnn, rmssd, ppgAmp);
  
  Serial.print("--- Predictions ---");
  Serial.print("\nMean HR: "); Serial.print(meanHR); Serial.print(" BPM");
  Serial.print("\nSDNN:    "); Serial.print(sdnn); Serial.print(" ms");
  Serial.print("\nRMSSD:   "); Serial.print(rmssd); Serial.print(" ms");
  Serial.print("\nPPG Amp: "); Serial.print(ppgAmp);
  Serial.print("\nEst BGL: "); Serial.print(predictedBGL); Serial.println(" mg/dL");
  
  finalHR = meanHR;
  finalBGL = predictedBGL;
  finalBeats = peakCount;
  
  currentState = STATE_SAVED;
}

void handleSavedState() {
  updateDisplay("INFERENCE RESULT", 
                "BGL: " + String(finalBGL, 1) + " mg/dL", 
                "HR:  " + String(finalHR, 1) + " BPM", 
                "Beats detected: " + String(finalBeats), 
                "RED = Reset Console");
                
  if (digitalRead(BTN_REFRESH) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BTN_REFRESH) == LOW) {
      finalHR = 0.0;
      finalBGL = 0.0;
      finalBeats = 0;
      currentState = STATE_READY;
      delay(500); // Cool-down delay
    }
  }
}
