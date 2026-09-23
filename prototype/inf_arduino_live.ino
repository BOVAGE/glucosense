/**
 * GlucoSense ESP32 — Live Non-Invasive BGL & HR Inference Firmware
 *
 * Target Hardware:
 *   - ESP32 Development Board
 *   - GY-MAX30100 PPG Sensor connected via I2C (SDA=21, SCL=22)
 *   - SSD1306 0.96" OLED Display connected via I2C (SDA=21, SCL=22, Addr=0x3C)
 *   - Green Start Button: Pin 12 (active-low with internal pullup)
 *   - Red Refresh Button: Pin 14 (active-low with internal pullup)
 *
 * Pipeline Architecture:
 *   1. Live Acquisition: MAX30100 @ 50 Hz (10s stabilization + 60s window = 3000 samples).
 *   2. Finger Contact Check: Requires IR ADC >= MIN_FINGER_DC (20000 counts).
 *   3. Zero-Phase Butterworth SOS Bandpass Filter (0.5 - 8.0 Hz @ 50 Hz).
 *   4. Elgendi Peak & Trough Detection.
 *   5. Real-Time Heart Rate (BPM) Calculation from mean RR intervals.
 *   6. Catmull-Rom Cubic Spline Resampling (100 pts) & Clean Average Beat Construction (r >= 0.80).
 *   7. 7-Feature Vector Extraction (is_postprandial, reflection_index, peak_val, dc_component, log_ac_dc_ratio, rms_val, avg_ppg_amp).
 *   8. Embedded LightGBM C Model Inference (lgbm_bgl_model.h).
 *   9. OLED Display Rendering of Predicted BGL (mg/dL) & Heart Rate (BPM).
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MAX30100.h>
#include <math.h>
#include "lgbm_bgl_model.h"   // LightGBM BGL model: double score(double* input)

// ─────────────────────────────────────────────────────────────────────────────
//  HARDWARE & PERIPHERAL CONSTANTS
// ─────────────────────────────────────────────────────────────────────────────
#define BTN_START     12   // Green button
#define BTN_REFRESH   14   // Red button

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

static const int    FS               = 50;     // Sampling rate (Hz)
static const int    STABILITY_SECS   = 10;     // 10s stabilization countdown
static const int    RECORDING_SECS   = 60;     // 60s active signal acquisition
static const int    N_SIGNAL         = 3000;   // 60s * 50Hz = 3000 samples
static const uint16_t MIN_FINGER_DC  = 20000;  // Minimum IR ADC count to confirm finger contact

static const int    AVG_BEAT_LEN     = 100;    // Resampled beat length
static const int    MAX_PEAKS        = 200;    // Maximum detectable peaks
static const int    MAX_BEATS        = 150;    // Maximum beats to store
static const float  CORR_THRESHOLD   = 0.80f;  // Pearson r threshold for clean beats
static const int    MIN_BEAT_LEN     = 18;     // Minimum beat length (samples)
static const int    MAX_BEAT_LEN     = 80;     // Maximum beat length (samples)

// Elgendi peak detection parameters
static const float  PEAK_WIN_SEC     = 0.111f; // MA peak window (s)
static const float  BEAT_WIN_SEC     = 0.667f; // MA beat window (s)
static const float  BEAT_OFFSET      = 0.02f;  // threshold offset factor
static const float  MIN_RR_SEC       = 0.30f;  // minimum RR spacing = 0.3s
static const float  PEAK_GUARD_SEC   = 0.50f;  // ignore peaks in first 0.5 s (filter settling)

// ── Butterworth SOS Filter Coefficients (0.5 – 8.0 Hz @ 50 Hz) ────────────────
#define BUTTER_N_SECTIONS  3
static const double BUTTER_SOS[BUTTER_N_SECTIONS][6] = {
    { 4.953299635725317e-02, 9.906599271450635e-02, 4.953299635725317e-02, 1.0, -8.016302031785858e-01, 4.495993327764674e-01 },
    { 1.000000000000000e+00, 0.000000000000000e+00, -1.000000000000000e+00, 1.0, -1.279916626863965e+00, 3.249196962329065e-01 },
    { 1.000000000000000e+00, -2.000000000000000e+00, 1.000000000000000e+00, 1.0, -1.939004303836035e+00, 9.430300670802169e-01 }
};

// ─────────────────────────────────────────────────────────────────────────────
//  PERIPHERAL OBJECTS & STATE DEFINITIONS
// ─────────────────────────────────────────────────────────────────────────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
MAX30100 sensor;

enum SystemState {
  STATE_BOOTING,
  STATE_READY,
  STATE_STABILIZING,
  STATE_RECORDING,
  STATE_INFERENCE,
  STATE_RESULT,
  STATE_NO_FINGER_ERROR,
  STATE_HARDWARE_ERROR
};

SystemState currentState = STATE_BOOTING;
bool isPostprandial = false; // false = Fasting, true = Postprandial

// Live PPG Raw Buffer (3000 uint16_t samples = 6 KB RAM)
uint16_t ppgBuffer[N_SIGNAL];

// Dynamic Working Buffers for DSP (allocated on heap strictly during inference)
struct SignalBuffers {
  float raw[N_SIGNAL];
  float cleaned[N_SIGNAL];
  float work[N_SIGNAL];
  float sq[N_SIGNAL];
  float ma_peak[N_SIGNAL];
  float ma_beat[N_SIGNAL];
};

struct BeatsBuffer {
  float beats[MAX_BEATS][AVG_BEAT_LEN];
};

static int      g_peaks[MAX_PEAKS];
static int      g_troughs[MAX_PEAKS];
static float    g_corr[MAX_BEATS];
static float    g_avg_beat[AVG_BEAT_LEN];
static float    g_median_beat[AVG_BEAT_LEN];
static float    g_rr_ms[MAX_PEAKS];

// Result Globals
float g_predicted_bgl = 0.0f;
float g_heart_rate_bpm = 0.0f;

// ─────────────────────────────────────────────────────────────────────────────
//  OLED HELPER FUNCTION
// ─────────────────────────────────────────────────────────────────────────────
void updateDisplay(String header, String line1, String line2, String line3, String line4) {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(header);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);
  
  display.setCursor(0, 16); display.print(line1);
  display.setCursor(0, 28); display.print(line2);
  display.setCursor(0, 40); display.print(line3);
  display.setCursor(0, 52); display.print(line4);
  
  display.display();
}

static void printSep() {
  Serial.println(F("──────────────────────────────────────────────────────"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  DSP: Zero-Phase SOS Butterworth Bandpass Filter
// ─────────────────────────────────────────────────────────────────────────────
static void _sos_forward(const float* in, float* out, int n,
                          const double sos[][6], int nsec) {
  float z0[BUTTER_N_SECTIONS] = {0};
  float z1[BUTTER_N_SECTIONS] = {0};

  for (int i = 0; i < n; i++) {
    float x = in[i];
    for (int s = 0; s < nsec; s++) {
      float y  = (float)sos[s][0] * x + z0[s];
      z0[s]     = (float)sos[s][1] * x - (float)sos[s][4] * y + z1[s];
      z1[s]     = (float)sos[s][2] * x - (float)sos[s][5] * y;
      x = y;
    }
    out[i] = x;
  }
}

static void zeroPhaseSosFilter(const float* in, float* out, int n,
                                const double sos[][6], int nsec, float* work) {
  _sos_forward(in, out, n, sos, nsec);
  for (int i = 0; i < n / 2; i++) { float t = out[i]; out[i] = out[n - 1 - i]; out[n - 1 - i] = t; }
  _sos_forward(out, work, n, sos, nsec);
  for (int i = 0; i < n / 2; i++) { float t = work[i]; work[i] = work[n - 1 - i]; work[n - 1 - i] = t; }
  memcpy(out, work, n * sizeof(float));
}

// ─────────────────────────────────────────────────────────────────────────────
//  DSP: Symmetric Moving Average & Elgendi Peak Detection
// ─────────────────────────────────────────────────────────────────────────────
static void movingAverage(const float* in, float* out, int n, int half_win) {
  for (int i = 0; i < n; i++) {
    int lo = max(0, i - half_win);
    int hi = min(n - 1, i + half_win);
    float sum = 0;
    for (int j = lo; j <= hi; j++) sum += in[j];
    out[i] = sum / (hi - lo + 1);
  }
}

static int detectPeaksElgendi(const float* cleaned, int n, int* peaks,
                              float* sq, float* ma_peak, float* ma_beat) {
  int hw_peak = 2;
  int hw_beat = (int)(BEAT_WIN_SEC * FS / 2.0f);

  for (int i = 0; i < n; i++) {
    float v = cleaned[i];
    sq[i] = (v > 0.0f) ? (v * v) : 0.0f;
  }

  float mean_sq = 0;
  for (int i = 0; i < n; i++) mean_sq += sq[i];
  mean_sq /= n;

  movingAverage(sq, ma_peak, n, hw_peak);
  movingAverage(sq, ma_beat, n, hw_beat);

  int n_peaks      = 0;
  int min_rr_samp  = (int)(MIN_RR_SEC  * FS);
  int min_len      = (int)roundf(PEAK_WIN_SEC * FS);
  int peak_guard   = (int)(PEAK_GUARD_SEC * FS);
  bool in_block    = false;
  int block_start  = 0;
  int last_peak    = -min_rr_samp - 1;

  for (int i = 0; i < n; i++) {
    float thresh = ma_beat[i] + BEAT_OFFSET * mean_sq;
    bool above = (ma_peak[i] > thresh);

    if (above && !in_block) {
      in_block = true;
      block_start = i;
    } else if (!above && in_block) {
      in_block = false;
      int block_len = i - block_start;
      if (block_len < min_len) continue;
      float best = -1e38f;
      int   best_idx = block_start;
      for (int j = block_start; j < i; j++) {
        if (cleaned[j] > best) { best = cleaned[j]; best_idx = j; }
      }
      if (best_idx < peak_guard) continue;
      if (n_peaks == 0 || best_idx - last_peak >= min_rr_samp) {
        if (n_peaks < MAX_PEAKS) {
          peaks[n_peaks++] = best_idx;
          last_peak = best_idx;
        }
      } else if (n_peaks > 0 && cleaned[best_idx] > cleaned[peaks[n_peaks - 1]]) {
        peaks[n_peaks - 1] = best_idx;
        last_peak = best_idx;
      }
    }
  }
  return n_peaks;
}

static int detectTroughs(const float* cleaned, const int* peaks, int n_peaks, int* troughs) {
  int n_troughs = 0;
  for (int i = 0; i < n_peaks - 1; i++) {
    int p1 = peaks[i], p2 = peaks[i + 1];
    if (p2 - p1 <= 10) continue;
    float best = cleaned[p1];
    int   best_idx = p1;
    for (int j = p1; j < p2; j++) {
      if (cleaned[j] < best) { best = cleaned[j]; best_idx = j; }
    }
    troughs[n_troughs++] = best_idx;
  }
  return n_troughs;
}

static void resampleCubic(const float* beat, int beat_len, float* out, int out_len) {
  for (int k = 0; k < out_len; k++) {
    float pos = (float)k * (beat_len - 1) / (out_len - 1);
    int   i1  = (int)pos;
    float t   = pos - i1;

    int i0 = max(0, i1 - 1);
    int i2 = min(beat_len - 1, i1 + 1);
    int i3 = min(beat_len - 1, i1 + 2);

    float p0 = beat[i0], p1 = beat[i1], p2 = beat[i2], p3 = beat[i3];
    float t2 = t * t, t3 = t2 * t;
    out[k] = 0.5f * ((2.0f * p1) +
                     (-p0 + p2) * t +
                     (2.0f*p0 - 5.0f*p1 + 4.0f*p2 - p3) * t2 +
                     (-p0 + 3.0f*p1 - 3.0f*p2 + p3) * t3);
  }
}

static float pearsonCorr(const float* a, const float* b, int n) {
  float sum_a = 0, sum_b = 0;
  for (int i = 0; i < n; i++) { sum_a += a[i]; sum_b += b[i]; }
  float ma = sum_a / n, mb = sum_b / n;
  float num = 0, da2 = 0, db2 = 0;
  for (int i = 0; i < n; i++) {
    float da = a[i] - ma, db = b[i] - mb;
    num += da * db; da2 += da * da; db2 += db * db;
  }
  float denom = sqrtf(da2 * db2);
  return denom < 1e-12f ? 0.0f : (num / denom);
}

static void computeMedianBeat(const float beats[][AVG_BEAT_LEN], int n_beats, float* median_out) {
  static float col[MAX_BEATS];
  for (int k = 0; k < AVG_BEAT_LEN; k++) {
    for (int b = 0; b < n_beats; b++) col[b] = beats[b][k];
    for (int i = 1; i < n_beats; i++) {
      float v = col[i]; int j = i - 1;
      while (j >= 0 && col[j] > v) { col[j+1] = col[j]; j--; }
      col[j+1] = v;
    }
    median_out[k] = (n_beats % 2 == 0) ? (col[n_beats/2 - 1] + col[n_beats/2]) * 0.5f : col[n_beats/2];
  }
}

static int buildAverageBeat(const float* cleaned, const int* troughs,
                             int n_troughs, float* avg_beat,
                             float beats[][AVG_BEAT_LEN]) {
  int n_valid = 0;

  for (int i = 0; i < n_troughs - 1 && n_valid < MAX_BEATS; i++) {
    int t1 = troughs[i], t2 = troughs[i + 1];
    int beat_len = t2 - t1;
    if (beat_len < MIN_BEAT_LEN || beat_len > MAX_BEAT_LEN) continue;

    resampleCubic(cleaned + t1, beat_len, beats[n_valid], AVG_BEAT_LEN);

    float b_min = beats[n_valid][0], b_max = beats[n_valid][0];
    for (int k = 1; k < AVG_BEAT_LEN; k++) {
      if (beats[n_valid][k] < b_min) b_min = beats[n_valid][k];
      if (beats[n_valid][k] > b_max) b_max = beats[n_valid][k];
    }
    float rng = b_max - b_min;
    if (rng < 1e-5f) continue;

    for (int k = 0; k < AVG_BEAT_LEN; k++)
      beats[n_valid][k] = (beats[n_valid][k] - b_min) / rng;

    n_valid++;
  }

  if (n_valid < 3) return -1;

  computeMedianBeat(beats, n_valid, g_median_beat);

  int n_clean = 0;
  for (int b = 0; b < n_valid; b++) {
    g_corr[b] = pearsonCorr(beats[b], g_median_beat, AVG_BEAT_LEN);
    if (g_corr[b] >= CORR_THRESHOLD) n_clean++;
  }

  if (n_clean < 2) {
    for (int k = 0; k < AVG_BEAT_LEN; k++) avg_beat[k] = g_median_beat[k];
    return n_valid;
  }

  memset(avg_beat, 0, AVG_BEAT_LEN * sizeof(float));
  int count = 0;
  for (int b = 0; b < n_valid; b++) {
    if (g_corr[b] >= CORR_THRESHOLD) {
      for (int k = 0; k < AVG_BEAT_LEN; k++) avg_beat[k] += beats[b][k];
      count++;
    }
  }
  for (int k = 0; k < AVG_BEAT_LEN; k++) avg_beat[k] /= count;

  return n_clean;
}

static float computePeakVal(const float* avg_beat) {
  float mx = avg_beat[0];
  for (int k = 1; k < AVG_BEAT_LEN; k++) if (avg_beat[k] > mx) mx = avg_beat[k];
  return mx;
}

static float computeReflectionIndex(const float* avg_beat) {
  int s_idx = 0;
  for (int k = 1; k < AVG_BEAT_LEN; k++) if (avg_beat[k] > avg_beat[s_idx]) s_idx = k;
  float s_val = avg_beat[s_idx];

  float d_val = 0.0f;
  int search_start = s_idx + 5;
  int search_end   = AVG_BEAT_LEN - 5;
  if (search_start < search_end) {
    int d_idx = search_start;
    for (int k = search_start + 1; k < search_end; k++)
      if (avg_beat[k] > avg_beat[d_idx]) d_idx = k;
    d_val = avg_beat[d_idx];
  }
  return d_val / (s_val + 1e-5f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MAIN DSP & INFERENCE PIPELINE
// ─────────────────────────────────────────────────────────────────────────────
static bool runInferencePipeline(double* features_out) {
  Serial.print(F("Heap free before alloc: "));
  Serial.print(ESP.getFreeHeap());
  Serial.println(F(" bytes"));

  SignalBuffers* sig = (SignalBuffers*)malloc(sizeof(SignalBuffers));
  if (!sig) {
    Serial.println(F("ERROR: Failed to allocate SignalBuffers (~72 KB) on heap!"));
    return false;
  }

  BeatsBuffer* bb = (BeatsBuffer*)malloc(sizeof(BeatsBuffer));
  if (!bb) {
    Serial.println(F("ERROR: Failed to allocate BeatsBuffer (~60 KB) on heap!"));
    free(sig);
    return false;
  }

  // 1. Copy uint16 ppgBuffer to float signal array
  for (int i = 0; i < N_SIGNAL; i++) sig->raw[i] = (float)ppgBuffer[i];

  // 1b. Compute DC component & remove offset
  float dc_component = 0.0f;
  {
    float raw_mean = 0;
    for (int i = 0; i < N_SIGNAL; i++) raw_mean += sig->raw[i];
    raw_mean /= N_SIGNAL;
    dc_component = raw_mean;
    for (int i = 0; i < N_SIGNAL; i++) sig->raw[i] -= raw_mean;
  }

  // 2. Zero-phase Butterworth SOS filter
  zeroPhaseSosFilter(sig->raw, sig->cleaned, N_SIGNAL, BUTTER_SOS, BUTTER_N_SECTIONS, sig->work);

  // 3. Elgendi Peak Detection
  int n_peaks = detectPeaksElgendi(sig->cleaned, N_SIGNAL, g_peaks, sig->sq, sig->ma_peak, sig->ma_beat);
  if (n_peaks < 5) {
    Serial.println(F("ERROR: Too few peaks detected."));
    free(bb); free(sig);
    return false;
  }

  // 4. Trough Detection
  int n_troughs = detectTroughs(sig->cleaned, g_peaks, n_peaks, g_troughs);

  // 5. RR intervals & Heart Rate Calculation
  int n_rr = n_peaks - 1;
  float rr_sum = 0;
  for (int i = 0; i < n_rr; i++) {
    g_rr_ms[i] = (g_peaks[i + 1] - g_peaks[i]) * (1000.0f / FS);
    rr_sum += g_rr_ms[i];
  }
  float mean_rr_ms = (n_rr > 0) ? (rr_sum / (float)n_rr) : 0.0f;
  g_heart_rate_bpm = (mean_rr_ms > 0.0f) ? (60000.0f / mean_rr_ms) : 0.0f;

  Serial.print(F("DSP Step 5: RR intervals = ")); Serial.print(n_rr);
  Serial.print(F(", mean RRI = ")); Serial.print(mean_rr_ms, 2); Serial.print(F(" ms"));
  Serial.print(F(" -> Calculated Heart Rate = ")); Serial.print(g_heart_rate_bpm, 1); Serial.println(F(" BPM"));

  // 6. Average Beat Construction
  int n_clean = buildAverageBeat(sig->cleaned, g_troughs, n_troughs, g_avg_beat, bb->beats);

  // Compute AC peak-to-peak amplitude dynamically from clean signal
  float cmin = sig->cleaned[0], cmax = sig->cleaned[0];
  for (int i = 1; i < N_SIGNAL; i++) {
    if (sig->cleaned[i] < cmin) cmin = sig->cleaned[i];
    if (sig->cleaned[i] > cmax) cmax = sig->cleaned[i];
  }
  float ac_pp_amp = cmax - cmin;
  float ac_dc_ratio = ac_pp_amp / (dc_component + 1e-9f);
  float log_ac_dc_ratio = logf(ac_dc_ratio > 1e-9f ? ac_dc_ratio : 1e-9f);

  free(sig); sig = nullptr;
  if (n_clean < 0) {
    Serial.println(F("ERROR: Average beat construction failed."));
    free(bb);
    return false;
  }

  // 7. Extract stable 0th-order hardware features
  float peak_val         = computePeakVal(g_avg_beat);
  float reflection_index = computeReflectionIndex(g_avg_beat);

  float rms_sq_sum = 0.0f;
  for (int k = 0; k < AVG_BEAT_LEN; k++) rms_sq_sum += g_avg_beat[k] * g_avg_beat[k];
  float rms_val = sqrtf(rms_sq_sum / (float)AVG_BEAT_LEN);

  // 8. Build 7-feature vector for LightGBM
  features_out[0] = isPostprandial ? 1.0 : 0.0;
  features_out[1] = reflection_index;
  features_out[2] = peak_val;
  features_out[3] = dc_component;
  features_out[4] = log_ac_dc_ratio;
  features_out[5] = rms_val;
  features_out[6] = peak_val; // avg_ppg_amp

  free(bb);
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_REFRESH, INPUT_PULLUP);

  Wire.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED initialization failed"));
    currentState = STATE_HARDWARE_ERROR;
    return;
  }

  updateDisplay("GLUCOSENSE LIVE", "", "  Booting device...", "", "");
  delay(1000);

  if (!sensor.begin()) {
    Serial.println(F("MAX30100 initialization failed!"));
    updateDisplay("GLUCOSENSE ERROR", "Sensor failed!", "Check GY-MAX30100", "I2C wiring", "Rebooting...");
    delay(4000);
    ESP.restart();
  }

  // Configure MAX30100 matching data acquisition settings
  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setLedsCurrent(MAX30100_LED_CURR_50MA, MAX30100_LED_CURR_27_1MA);
  sensor.setLedsPulseWidth(MAX30100_SPC_PW_1600US_16BITS);
  sensor.setSamplingRate(MAX30100_SAMPRATE_50HZ);
  sensor.setHighresModeEnabled(true);

  currentState = STATE_READY;
}

// ─────────────────────────────────────────────────────────────────────────────
//  MAIN LOOP STATE MACHINE
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  switch (currentState) {
    case STATE_READY:
      handleReadyState();
      break;

    case STATE_STABILIZING:
      handleStabilizingState();
      break;

    case STATE_RECORDING:
      handleRecordingState();
      break;

    case STATE_INFERENCE:
      handleInferenceState();
      break;

    case STATE_RESULT:
      handleResultState();
      break;

    case STATE_NO_FINGER_ERROR:
      handleNoFingerErrorState();
      break;

    case STATE_HARDWARE_ERROR:
      updateDisplay("HARDWARE ERROR", "Sensor/OLED Fault", "Check I2C SDA=21 SCL=22", "", "Press RESET");
      delay(2000);
      break;

    default:
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  STATE HANDLERS
// ─────────────────────────────────────────────────────────────────────────────

void handleReadyState() {
  String kindStr = isPostprandial ? "Postprandial (1)" : "Fasting (0)";

  updateDisplay("GLUCOSENSE READY",
                "State: " + kindStr,
                "-----------------",
                "GRN = Start Test",
                "RED = Toggle State");

  if (digitalRead(BTN_START) == LOW) {
    delay(50);
    if (digitalRead(BTN_START) == LOW) {
      currentState = STATE_STABILIZING;
      delay(300);
    }
  }

  if (digitalRead(BTN_REFRESH) == LOW) {
    delay(50);
    if (digitalRead(BTN_REFRESH) == LOW) {
      isPostprandial = !isPostprandial;
      delay(300);
    }
  }
}

void handleStabilizingState() {
  Serial.println(F("Resetting MAX30100 for acquisition..."));
  Wire.end(); delay(50);
  Wire.begin(); delay(50);

  if (!sensor.begin()) {
    currentState = STATE_HARDWARE_ERROR;
    return;
  }

  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setLedsCurrent(MAX30100_LED_CURR_50MA, MAX30100_LED_CURR_27_1MA);
  sensor.setLedsPulseWidth(MAX30100_SPC_PW_1600US_16BITS);
  sensor.setSamplingRate(MAX30100_SAMPRATE_50HZ);
  sensor.setHighresModeEnabled(true);
  sensor.resetFifo();
  delay(10);

  Serial.println(F("Starting 10s Stabilization Phase..."));
  for (int sec = STABILITY_SECS; sec > 0; sec--) {
    updateDisplay("STABILIZATION",
                  "Hold finger still!",
                  "Settle time: " + String(sec) + "s",
                  "DO NOT MOVE FINGER",
                  "");

    unsigned long startSec = millis();
    uint16_t lastIr = 0;
    while (millis() - startSec < 1000) {
      sensor.update();
      uint16_t dummyIr, dummyRed;
      while (sensor.getRawValues(&dummyIr, &dummyRed)) {
        lastIr = dummyIr;
      }
      delay(1);
    }

    // Verify finger contact during stabilization
    if (lastIr < MIN_FINGER_DC && sec < STABILITY_SECS - 2) {
      Serial.println(F("WARNING: No finger detected during stabilization!"));
      currentState = STATE_NO_FINGER_ERROR;
      return;
    }
  }

  currentState = STATE_RECORDING;
}

void handleRecordingState() {
  Serial.println(F("Starting 60s PPG Signal Recording..."));
  int sampleIdx = 0;

  uint16_t tempIr, tempRed;
  while (sensor.getRawValues(&tempIr, &tempRed)) { /* drain residual */ }

  unsigned long startTime = millis();

  while (sampleIdx < N_SIGNAL) {
    sensor.update();

    uint16_t irVal, redVal;
    while (sensor.getRawValues(&irVal, &redVal) && sampleIdx < N_SIGNAL) {
      // Finger contact check
      if (irVal < MIN_FINGER_DC) {
        Serial.println(F("ERROR: Finger removed mid-test!"));
        currentState = STATE_NO_FINGER_ERROR;
        return;
      }

      ppgBuffer[sampleIdx] = irVal;
      sampleIdx++;

      if (sampleIdx % 50 == 0) {
        int secondsElapsed = sampleIdx / 50;
        int secondsLeft = RECORDING_SECS - secondsElapsed;
        if (secondsLeft < 0) secondsLeft = 0;

        String bar = "[";
        int filled = (sampleIdx * 10) / N_SIGNAL;
        for (int i = 0; i < 10; i++) bar += (i < filled) ? "=" : " ";
        bar += "]";

        updateDisplay("ACQUISITION: 50Hz",
                      "Time Left: " + String(secondsLeft) + "s",
                      "Samples: " + String(sampleIdx) + "/" + String(N_SIGNAL),
                      bar,
                      "Measuring PPG...");
      }
    }
    delay(5);
  }

  Serial.println(F("PPG Signal Recording Complete."));
  currentState = STATE_INFERENCE;
}

void handleInferenceState() {
  updateDisplay("GLUCOSENSE DSP", "Signal acquired OK", "Filtering PPG...", "Extracting features", "Running LightGBM...");
  printSep();
  Serial.println(F("EXECUTING REAL-TIME DSP & LIGHTGBM INFERENCE:"));
  printSep();

  double features[7] = {0};
  bool ok = runInferencePipeline(features);

  if (ok) {
    Serial.println(F("EXTRACTED LIVE FEATURES:"));
    Serial.print(F("  [0] is_postprandial  : ")); Serial.println((float)features[0], 1);
    Serial.print(F("  [1] reflection_index : ")); Serial.println((float)features[1], 6);
    Serial.print(F("  [2] peak_val         : ")); Serial.println((float)features[2], 6);
    Serial.print(F("  [3] dc_component     : ")); Serial.println((float)features[3], 2);
    Serial.print(F("  [4] log_ac_dc_ratio  : ")); Serial.println((float)features[4], 6);
    Serial.print(F("  [5] rms_val          : ")); Serial.println((float)features[5], 6);
    Serial.print(F("  [6] avg_ppg_amp      : ")); Serial.println((float)features[6], 6);

    double raw_score = score(features);
    if (raw_score < 40.0)  raw_score = 40.0;
    if (raw_score > 400.0) raw_score = 400.0;
    g_predicted_bgl = (float)raw_score;

    Serial.print(F("\nINFERENCE RESULT:\n  Predicted BGL : "));
    Serial.print(g_predicted_bgl, 1); Serial.println(F(" mg/dL"));
    Serial.print(F("  Heart Rate    : "));
    Serial.print(g_heart_rate_bpm, 1); Serial.println(F(" BPM\n"));

    currentState = STATE_RESULT;
  } else {
    Serial.println(F("ERROR: DSP Pipeline execution failed."));
    currentState = STATE_NO_FINGER_ERROR;
  }
}

void handleResultState() {
  String kindStr = isPostprandial ? "Postprandial" : "Fasting";
  String bglStr = String(g_predicted_bgl, 1) + " mg/dL";
  String hrStr  = "HR: " + String(g_heart_rate_bpm, 1) + " BPM";

  updateDisplay("BGL ESTIMATE",
                "BGL: " + bglStr,
                hrStr,
                "State: " + kindStr,
                "RED = Next Test");

  if (digitalRead(BTN_REFRESH) == LOW) {
    delay(50);
    if (digitalRead(BTN_REFRESH) == LOW) {
      currentState = STATE_READY;
      delay(300);
    }
  }
}

void handleNoFingerErrorState() {
  updateDisplay("NO FINGER DETECTED",
                "Finger missing or",
                "removed mid-test!",
                "Keep finger firm",
                "RED = Retry Test");

  if (digitalRead(BTN_REFRESH) == LOW) {
    delay(50);
    if (digitalRead(BTN_REFRESH) == LOW) {
      currentState = STATE_READY;
      delay(300);
    }
  }
}
