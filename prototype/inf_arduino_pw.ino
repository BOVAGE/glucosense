/**
 * GlucoSense ESP32 — BGL Inference Firmware (inf_arduino.ino)
 *
 * Purpose: DSP validation build — hardcodes a real 3000-sample PPG signal
 * and runs the complete feature-extraction + XGBoost inference pipeline.
 * Serial output allows comparison against known Python pipeline values.
 *
 * Hardcoded signal: P003, fasting, BGL=110 mg/dL
 *   Source: glucosense_r_dataset_2026-09-17.csv, row 3, window [500:3500]
 *   (window matches train_and_generate.py line 36)
 *
 * Ground-truth features from training notebook:
 *   feat_df[['participant_id', *final_deploy_selected_features]].iloc[3]
 *   [0] is_postprandial  = 0.0
 *   [1] e_a_ratio        = 0.210747
 *   [2] reflection_index = 0.983279
 *   [3] apg_e            = 0.001409
 *   [4] peak_val         = 0.882984
 *   [5] HRV_VHF          = 0.008411
 *   [6] HRV_TP           = 0.131220
 *   [7] HRV_IALS         = 0.739130
 *   [8] HRV_SI           = 49.277053
 *   [9] HRV_HTI          = 18.250000
 *
 * Memory Architecture:
 *   - Heavy buffers split into two separate heap allocations to avoid a
 *     single large contiguous-block request failing on the ESP32 heap:
 *       SignalBuffers  — 6× N_SIGNAL floats  ≈ 72 KB
 *       BeatsBuffer    — MAX_BEATS×AVG_BEAT_LEN floats ≈ 60 KB
 *   - Board: ESP32 Dev Module, CPU 240 MHz, Partition: Default 4MB
 */

#include <Arduino.h>
#include <math.h>
#include "ppg_signal.h"      // Raw PPG, Butterworth coefficients, expected values
#include "xgb_bgl_model.h"   // XGBoost BGL model: double score(double* input)

// ─────────────────────────────────────────────────────────────────────────────
//  COMPILE-TIME CONSTANTS
// ─────────────────────────────────────────────────────────────────────────────
static const int    FS               = 50;     // Sampling rate (Hz)
static const int    N_SIGNAL         = 3000;   // PPG samples (60 s)
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
static const float  MIN_RR_SEC       = 0.30f;  // minimum RR spacing = NK2's mindelay=0.3
static const float  MAX_RR_SEC       = 2.00f;  // maximum RR spacing (s) — rejects stray peaks
static const float  PEAK_GUARD_SEC   = 0.50f;  // ignore peaks in first 0.5 s (filter settling)

// Physiological RR clamping for HRV histogram metrics
// NK2 signal_fixpeaks removes outlier peaks before HRV analysis.
// We achieve the same by excluding out-of-range RR intervals.
static const float  RR_HRV_MIN_MS    = 400.0f; // 150 BPM upper bound (ms)
static const float  RR_HRV_MAX_MS    = 2000.0f;// 30 BPM lower bound (ms)

// HRV frequency-domain parameters (matching NeuroKit2's Welch pipeline)
static const float  INTERP_RATE_HZ   = 4.0f;   // RRI uniform resampling rate
static const int    MAX_INTERP_N     = 512;    // max interpolated samples
static const int    FFT_SIZE         = 512;    // FFT size (power of 2 >= MAX_INTERP_N)

// HRV band limits (Hz)
static const float  BAND_LF_LO       = 0.04f;
static const float  BAND_LF_HI       = 0.15f;
static const float  BAND_HF_LO       = 0.15f;
static const float  BAND_HF_HI       = 0.40f;
static const float  BAND_VHF_LO      = 0.40f;
static const float  BAND_VHF_HI      = 0.50f;

// HRV histogram bin width for SI/HTI (ms)
static const float  HIST_BIN_MS      = 50.0f;
static const int    HIST_MAX_BINS    = 100;

// ─────────────────────────────────────────────────────────────────────────────
//  DYNAMIC WORKING BUFFERS (Allocated on Heap during pipeline execution)
//  Split into two structs so each malloc requests a smaller contiguous block:
//    SignalBuffers  ≈ 72 KB  (6 × N_SIGNAL floats)
//    BeatsBuffer    ≈ 60 KB  (MAX_BEATS × AVG_BEAT_LEN floats)
//  Eliminates static DRAM .bss section overflow (dram0_0_seg error).
// ─────────────────────────────────────────────────────────────────────────────
struct SignalBuffers {
  float  raw[N_SIGNAL];       // 12,000 bytes
  float  cleaned[N_SIGNAL];   // 12,000 bytes
  float  work[N_SIGNAL];      // 12,000 bytes
  float  sq[N_SIGNAL];        // 12,000 bytes
  float  ma_peak[N_SIGNAL];   // 12,000 bytes
  float  ma_beat[N_SIGNAL];   // 12,000 bytes
};

struct BeatsBuffer {
  float  beats[MAX_BEATS][AVG_BEAT_LEN];  // 60,000 bytes
};

// Small scalar/metadata arrays kept in BSS (~10 KB total — well within DRAM limit)
static int      g_peaks[MAX_PEAKS];                // peak indices
static int      g_troughs[MAX_PEAKS];              // trough indices
static float    g_corr[MAX_BEATS];                 // beat-to-median correlation
static float    g_avg_beat[AVG_BEAT_LEN];          // final average beat
static float    g_median_beat[AVG_BEAT_LEN];       // median beat (scratch)
static float    g_vpg[AVG_BEAT_LEN];               // VPG of avg beat
static float    g_apg[AVG_BEAT_LEN];               // APG of avg beat
static float    g_rr_ms[MAX_PEAKS];                // RR intervals in ms
static float    g_rr_clean[MAX_PEAKS];             // physiologically filtered RR intervals
static float    g_rri_interp[MAX_INTERP_N];        // uniformly interpolated RRI
static float    g_fft_re[FFT_SIZE];                // FFT real part
static float    g_fft_im[FFT_SIZE];                // FFT imaginary part
static float    g_psd[FFT_SIZE / 2 + 1];           // one-sided PSD
static int      g_hist[HIST_MAX_BINS];              // RR histogram

// ─────────────────────────────────────────────────────────────────────────────
//  UTILITY: Print separator
// ─────────────────────────────────────────────────────────────────────────────
static void printSep() {
  Serial.println(F("──────────────────────────────────────────────────────"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  DSP: Zero-Phase SOS Butterworth Filter
//  Matches scipy.signal.sosfiltfilt (Transposed Direct Form II)
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
  // Forward pass
  _sos_forward(in, out, n, sos, nsec);

  // Reverse out in-place
  for (int i = 0; i < n / 2; i++) {
    float t = out[i]; out[i] = out[n - 1 - i]; out[n - 1 - i] = t;
  }

  // Second forward pass (into work)
  _sos_forward(out, work, n, sos, nsec);

  // Reverse work back into out
  for (int i = 0; i < n / 2; i++) {
    float t = work[i]; work[i] = work[n - 1 - i]; work[n - 1 - i] = t;
  }
  memcpy(out, work, n * sizeof(float));
}

// ─────────────────────────────────────────────────────────────────────────────
//  DSP: Symmetric Moving Average
// ─────────────────────────────────────────────────────────────────────────────
static void movingAverage(const float* in, float* out, int n, int half_win) {
  for (int i = 0; i < n; i++) {
    int lo = i - half_win;
    int hi = i + half_win;
    if (lo < 0)   lo = 0;
    if (hi >= n)  hi = n - 1;
    float sum = 0;
    for (int j = lo; j <= hi; j++) sum += in[j];
    out[i] = sum / (hi - lo + 1);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DSP: Elgendi Peak Detection
// ─────────────────────────────────────────────────────────────────────────────
static int detectPeaksElgendi(const float* cleaned, int n, int* peaks,
                              float* sq, float* ma_peak, float* ma_beat) {
  // NK2 uses int(round(peakwindow * fs)) = int(round(0.111*50)) = 6 as window size.
  // uniform_filter1d size=6 ≈ symmetric half-window=2 (window 5) for peak detection purposes.
  // NK2 beat window: int(round(0.667*50)) = 33 → hw_beat = 16 → total = 33 (exact match).
  int hw_peak = 2;                                           // ≈ NK2's 6-sample window
  int hw_beat = (int)(BEAT_WIN_SEC * FS / 2.0f);            // = 16 → window 33

  // Step 1: clip negatives and square (NK2: signal_abs[signal_abs<0]=0; sqrd=signal_abs**2)
  //  NK2 does NOT subtract mean before clipping. Mean subtraction shifts the
  //  clip boundary and changes which samples contribute to the sq pulse,
  //  giving different block boundaries and different detected peaks.
  for (int i = 0; i < n; i++) {
    float v = cleaned[i];
    if (v < 0.0f) v = 0.0f;
    sq[i] = v * v;
  }

  // Step 2: mean of squared
  float mean_sq = 0;
  for (int i = 0; i < n; i++) mean_sq += sq[i];
  mean_sq /= n;

  // Step 3: two moving averages
  movingAverage(sq, ma_peak, n, hw_peak);
  movingAverage(sq, ma_beat, n, hw_beat);

  // Step 4 + 5: detect blocks, find peaks
  // min_len = Threshold 2 from Elgendi 2013:  blocks shorter than peakwindow
  // are noise — NK2: min_len = int(round(peakwindow * fs)) = 6
  int n_peaks      = 0;
  int min_rr_samp  = (int)(MIN_RR_SEC  * FS);     // = 15 (NK2 mindelay=0.3s)
  int min_len      = (int)roundf(PEAK_WIN_SEC * FS); // = 6 (NK2 Threshold 2)
  int peak_guard   = (int)(PEAK_GUARD_SEC * FS);   // = 25 — skip first 0.5 s (filter transient)
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
      // NK2 Threshold 2: skip blocks shorter than peakwindow (6 samples)
      if (block_len < min_len) continue;
      // Find argmax of cleaned signal in [block_start, i)
      float best = -1e38f;
      int   best_idx = block_start;
      for (int j = block_start; j < i; j++) {
        if (cleaned[j] > best) { best = cleaned[j]; best_idx = j; }
      }
      // Skip peaks in the filter startup transient zone
      if (best_idx < peak_guard) continue;
      // Enforce minimum distance
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

// ─────────────────────────────────────────────────────────────────────────────
//  Trough Detection
// ─────────────────────────────────────────────────────────────────────────────
static int detectTroughs(const float* cleaned, const int* peaks, int n_peaks,
                          int* troughs) {
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

// ─────────────────────────────────────────────────────────────────────────────
//  Beat Resampling — Catmull-Rom cubic spline to AVG_BEAT_LEN points
//  Matches Python interp1d(kind='cubic') used in the training notebook.
//  Catmull-Rom needs 4 control points: p_{i-1}, p_i, p_{i+1}, p_{i+2}.
//  Boundary points are clamped (repeated endpoint).
// ─────────────────────────────────────────────────────────────────────────────
static void resampleCubic(const float* beat, int beat_len, float* out, int out_len) {
  for (int k = 0; k < out_len; k++) {
    float pos = (float)k * (beat_len - 1) / (out_len - 1);
    int   i1  = (int)pos;                          // floor segment index
    float t   = pos - i1;                          // fractional part [0,1)

    // Clamp control-point indices to valid range
    int i0 = (i1 > 0)            ? i1 - 1 : 0;
    int i2 = (i1 + 1 < beat_len) ? i1 + 1 : beat_len - 1;
    int i3 = (i1 + 2 < beat_len) ? i1 + 2 : beat_len - 1;

    float p0 = beat[i0], p1 = beat[i1], p2 = beat[i2], p3 = beat[i3];

    // Catmull-Rom basis (alpha = 0.5)
    float t2 = t * t, t3 = t2 * t;
    out[k] = 0.5f * ((2.0f * p1) +
                     (-p0 + p2) * t +
                     (2.0f*p0 - 5.0f*p1 + 4.0f*p2 - p3) * t2 +
                     (-p0 + 3.0f*p1 - 3.0f*p2 + p3) * t3);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pearson Correlation
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
//  Median Beat Computation
// ─────────────────────────────────────────────────────────────────────────────
static void computeMedianBeat(const float beats[][AVG_BEAT_LEN], int n_beats,
                               float* median_out) {
  static float col[MAX_BEATS];
  for (int k = 0; k < AVG_BEAT_LEN; k++) {
    for (int b = 0; b < n_beats; b++) col[b] = beats[b][k];
    for (int i = 1; i < n_beats; i++) {
      float v = col[i]; int j = i - 1;
      while (j >= 0 && col[j] > v) { col[j+1] = col[j]; j--; }
      col[j+1] = v;
    }
    if (n_beats % 2 == 0)
      median_out[k] = (col[n_beats/2 - 1] + col[n_beats/2]) * 0.5f;
    else
      median_out[k] = col[n_beats/2];
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Build Average Beat
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
//  Numerical Gradient
// ─────────────────────────────────────────────────────────────────────────────
static void gradient(const float* in, float* out, int n) {
  if (n < 2) return;
  out[0] = in[1] - in[0];
  out[n-1] = in[n-1] - in[n-2];
  for (int i = 1; i < n - 1; i++)
    out[i] = (in[i+1] - in[i-1]) * 0.5f;
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

static void computePulseWidth(const float* avg_beat, float* pw50_out, float* pw25_out, float* pw75_out, float* pw_ratio_out) {
  int s_idx = 0;
  for (int k = 1; k < AVG_BEAT_LEN; k++) if (avg_beat[k] > avg_beat[s_idx]) s_idx = k;
  float peak_val = avg_beat[s_idx];
  if (peak_val <= 0.0f) { *pw50_out = 0; *pw25_out = 0; *pw75_out = 0; *pw_ratio_out = 0; return; }

  // PW50
  float t50 = 0.5f * peak_val;
  int l50 = s_idx; while (l50 > 0 && avg_beat[l50] > t50) l50--;
  int r50 = s_idx; while (r50 < AVG_BEAT_LEN - 1 && avg_beat[r50] > t50) r50++;
  *pw50_out = (float)(r50 - l50);

  // PW25
  float t25 = 0.25f * peak_val;
  int l25 = s_idx; while (l25 > 0 && avg_beat[l25] > t25) l25--;
  int r25 = s_idx; while (r25 < AVG_BEAT_LEN - 1 && avg_beat[r25] > t25) r25++;
  *pw25_out = (float)(r25 - l25);

  // PW75
  float t75 = 0.75f * peak_val;
  int l75 = s_idx; while (l75 > 0 && avg_beat[l75] > t75) l75--;
  int r75 = s_idx; while (r75 < AVG_BEAT_LEN - 1 && avg_beat[r75] > t75) r75++;
  *pw75_out = (float)(r75 - l75);

  *pw_ratio_out = (*pw25_out > 0.0f) ? (*pw50_out / *pw25_out) : 0.0f;
}

static void computeAPGFeatures(const float* avg_beat,
                                float* apg_e_out, float* e_a_ratio_out) {
  gradient(avg_beat, g_vpg, AVG_BEAT_LEN);
  gradient(g_vpg,    g_apg, AVG_BEAT_LEN);

  int lb = AVG_BEAT_LEN;

  int a_idx = 0;
  for (int k = 1; k < (int)(lb * 0.35f); k++)
    if (g_apg[k] > g_apg[a_idx]) a_idx = k;
  float a_val = (g_apg[a_idx] > 0) ? g_apg[a_idx] : 1e-5f;

  int b_end = (int)(lb * 0.5f);
  int b_idx = a_idx;
  if (b_end > a_idx + 2) {
    b_idx = a_idx;
    for (int k = a_idx + 1; k < b_end; k++)
      if (g_apg[k] < g_apg[b_idx]) b_idx = k;
  }

  int c_end = (int)(lb * 0.65f);
  int c_idx = b_idx;
  if (c_end > b_idx + 1) {
    c_idx = b_idx;
    for (int k = b_idx + 1; k < c_end; k++)
      if (g_apg[k] > g_apg[c_idx]) c_idx = k;
  }

  int d_end = (int)(lb * 0.8f);
  int d_idx = c_idx;
  if (d_end > c_idx + 1) {
    d_idx = c_idx;
    for (int k = c_idx + 1; k < d_end; k++)
      if (g_apg[k] < g_apg[d_idx]) d_idx = k;
  }

  int e_idx = d_idx;
  if (lb > d_idx + 1) {
    e_idx = d_idx;
    for (int k = d_idx + 1; k < lb; k++)
      if (g_apg[k] > g_apg[e_idx]) e_idx = k;
  }
  float e_val = g_apg[e_idx];

  *apg_e_out     = e_val;
  *e_a_ratio_out = e_val / a_val;
}

static int computeRR(const int* peaks, int n_peaks, float* rr_ms_out) {
  int n = n_peaks - 1;
  for (int i = 0; i < n; i++)
    rr_ms_out[i] = (peaks[i + 1] - peaks[i]) * (1000.0f / FS);
  return n;
}

static void computeSI_HTI(const float* rr_ms, int n_rr,
                           float* si_out, float* hti_out) {
  if (n_rr < 2) { *si_out = 0; *hti_out = 0; return; }

  // Filter physiologically implausible RR intervals before histogram.
  // NK2 signal_fixpeaks removes such intervals; we replicate by exclusion.
  int n_clean = 0;
  for (int i = 0; i < n_rr; i++) {
    if (rr_ms[i] >= RR_HRV_MIN_MS && rr_ms[i] <= RR_HRV_MAX_MS)
      g_rr_clean[n_clean++] = rr_ms[i];
  }
  if (n_clean < 2) { *si_out = 0; *hti_out = 0; return; }
  Serial.print(F("  HRV: using ")); Serial.print(n_clean);
  Serial.print(F(" / ")); Serial.print(n_rr); Serial.println(F(" RR intervals (filtered outliers)"));

  float rr_min = g_rr_clean[0], rr_max = g_rr_clean[0];
  for (int i = 1; i < n_clean; i++) {
    if (g_rr_clean[i] < rr_min) rr_min = g_rr_clean[i];
    if (g_rr_clean[i] > rr_max) rr_max = g_rr_clean[i];
  }

  int n_bins = (int)((rr_max - rr_min) / HIST_BIN_MS) + 1;
  if (n_bins > HIST_MAX_BINS) n_bins = HIST_MAX_BINS;

  memset(g_hist, 0, n_bins * sizeof(int));
  for (int i = 0; i < n_clean; i++) {
    int b = (int)((g_rr_clean[i] - rr_min) / HIST_BIN_MS);
    if (b < 0) b = 0;
    if (b >= n_bins) b = n_bins - 1;
    g_hist[b]++;
  }

  int max_count = 0, mode_bin = 0;
  for (int b = 0; b < n_bins; b++) {
    if (g_hist[b] > max_count) { max_count = g_hist[b]; mode_bin = b; }
  }

  float Mo_s   = (rr_min + (mode_bin + 0.5f) * HIST_BIN_MS) / 1000.0f;
  // AMo must be expressed as a percentage (0–100) per Baevsky's SI formula.
  // SI = AMo[%] / (2 × Mo[s] × MxDMn[s])
  float AMo    = (float)max_count * 100.0f / n_clean;
  float MxDMn  = (rr_max - rr_min) / 1000.0f;

  *si_out  = (MxDMn > 1e-10f) ? (AMo / (2.0f * Mo_s * MxDMn)) : 0.0f;
  *hti_out = (float)n_clean / max_count;
}

static float computeIALS(const float* rr_ms, int n_rr) {
  if (n_rr < 1) return 0.0f;
  float sum_inv = 0; int n_clean = 0;
  for (int i = 0; i < n_rr; i++) {
    if (rr_ms[i] >= RR_HRV_MIN_MS && rr_ms[i] <= RR_HRV_MAX_MS) {
      sum_inv += 1000.0f / rr_ms[i]; n_clean++;
    }
  }
  return (n_clean > 0 && sum_inv > 0) ? (float)n_clean / sum_inv : 0.0f;
}

static void fft_radix2(float* re, float* im, int n) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float tr = re[i]; re[i] = re[j]; re[j] = tr;
      float ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    float ang = -2.0f * (float)M_PI / len;
    float wre = cosf(ang), wim = sinf(ang);
    for (int i = 0; i < n; i += len) {
      float cur_re = 1.0f, cur_im = 0.0f;
      for (int j = 0; j < len / 2; j++) {
        float u_re = re[i + j],          u_im = im[i + j];
        float v_re = re[i+j+len/2] * cur_re - im[i+j+len/2] * cur_im;
        float v_im = re[i+j+len/2] * cur_im + im[i+j+len/2] * cur_re;
        re[i + j]        = u_re + v_re;   im[i + j]        = u_im + v_im;
        re[i+j+len/2]    = u_re - v_re;   im[i+j+len/2]    = u_im - v_im;
        float new_re = cur_re * wre - cur_im * wim;
        cur_im = cur_re * wim + cur_im * wre;
        cur_re = new_re;
      }
    }
  }
}

static void computeFreqHRV(const float* rr_ms, int n_rr,
                            float* hrv_tp_out, float* hrv_vhf_out) {
  if (n_rr < 4) { *hrv_tp_out = 0; *hrv_vhf_out = 0; return; }

  float t_rr[MAX_PEAKS];
  t_rr[0] = rr_ms[0] / 1000.0f;
  for (int i = 1; i < n_rr; i++) t_rr[i] = t_rr[i-1] + rr_ms[i] / 1000.0f;
  float t_end = t_rr[n_rr - 1];

  int n_interp = (int)(t_end * INTERP_RATE_HZ);
  if (n_interp < 4)  { *hrv_tp_out = 0; *hrv_vhf_out = 0; return; }
  if (n_interp > MAX_INTERP_N) n_interp = MAX_INTERP_N;

  int rr_idx = 0;
  for (int k = 0; k < n_interp; k++) {
    float t_k = (float)k / INTERP_RATE_HZ;
    while (rr_idx < n_rr - 2 && t_rr[rr_idx + 1] < t_k) rr_idx++;

    float t0 = (rr_idx == 0) ? 0.0f : t_rr[rr_idx - 1];
    float t1 = t_rr[rr_idx];
    float v0 = (rr_idx == 0) ? rr_ms[0] / 1000.0f : rr_ms[rr_idx - 1] / 1000.0f;
    float v1 = rr_ms[rr_idx] / 1000.0f;
    float dt = t1 - t0;
    float frac = (dt > 1e-6f) ? (t_k - t0) / dt : 0.0f;
    g_rri_interp[k] = v0 + frac * (v1 - v0);
  }

  float mean_interp = 0;
  for (int k = 0; k < n_interp; k++) mean_interp += g_rri_interp[k];
  mean_interp /= n_interp;
  for (int k = 0; k < n_interp; k++) g_rri_interp[k] -= mean_interp;

  float win_energy = 0;
  for (int k = 0; k < n_interp; k++) {
    float hann = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * k / (n_interp - 1)));
    g_rri_interp[k] *= hann;
    win_energy += hann * hann;
  }

  memset(g_fft_re, 0, FFT_SIZE * sizeof(float));
  memset(g_fft_im, 0, FFT_SIZE * sizeof(float));
  for (int k = 0; k < n_interp; k++) g_fft_re[k] = g_rri_interp[k];

  fft_radix2(g_fft_re, g_fft_im, FFT_SIZE);

  int n_psd = FFT_SIZE / 2 + 1;
  float scale = 2.0f / (INTERP_RATE_HZ * win_energy);
  for (int k = 0; k < n_psd; k++) {
    float power = g_fft_re[k] * g_fft_re[k] + g_fft_im[k] * g_fft_im[k];
    if (k == 0 || k == FFT_SIZE / 2) power *= 0.5f;
    g_psd[k] = power * scale;
  }
  float df = INTERP_RATE_HZ / FFT_SIZE;

  auto band_power = [&](float f_lo, float f_hi) -> float {
    float sum = 0;
    int k_lo = (int)(f_lo / df);
    int k_hi = (int)(f_hi / df);
    if (k_lo < 1) k_lo = 1;
    if (k_hi >= n_psd) k_hi = n_psd - 1;
    if (k_hi <= k_lo) return 0.0f;
    for (int k = k_lo; k <= k_hi - 1; k++)
      sum += 0.5f * (g_psd[k] + g_psd[k+1]) * df;
    return sum;
  };

  float lf  = band_power(BAND_LF_LO,  BAND_LF_HI);
  float hf  = band_power(BAND_HF_LO,  BAND_HF_HI);
  float vhf = band_power(BAND_VHF_LO, BAND_VHF_HI);

  *hrv_tp_out  = lf + hf + vhf;
  *hrv_vhf_out = vhf;
}

static void printFeature(int idx, const char* name, double val, double expected) {
  float err = (expected != 0.0) ? fabsf((float)((val - expected) / expected)) * 100.0f : 0.0f;
  Serial.print(F("  input["));  Serial.print(idx);
  Serial.print(F("] "));        Serial.print(name);
  Serial.print(F(" = "));       Serial.print((float)val, 8);
  Serial.print(F("  (expected: ")); Serial.print((float)expected, 8);
  Serial.print(F(",  err: ")); Serial.print(err, 2); Serial.println(F("%)"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  MAIN PIPELINE
// ─────────────────────────────────────────────────────────────────────────────
static bool runInferencePipeline(double* features_out) {
  // Allocate heavy working buffers on heap in two smaller blocks to avoid
  // a single 132 KB contiguous-allocation failure on the ESP32 heap.
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

  // ── 1. Copy PROGMEM signal to RAM as float ────────────────────────
  for (int i = 0; i < N_SIGNAL; i++)
    sig->raw[i] = (float)pgm_read_word_near(HARDCODED_PPG + i);

  Serial.println(F("Step 1: Raw signal loaded from PROGMEM."));
  Serial.print  (F("  raw[0..4] = "));
  for (int i = 0; i < 5; i++) { Serial.print(sig->raw[i], 1); Serial.print(' '); }
  Serial.println();

  // ── 1b. Remove DC offset before filtering ────────────────────────
  //  scipy sosfiltfilt uses sosfilt_zi to pre-warm filter state for DC
  //  steady-state. Our Arduino implementation starts with z0=z1=0, so a
  //  ~41 000 ADC step at sample 0 causes a massive startup transient.
  //  Subtracting the mean gives a near-zero-mean signal, making zero
  //  initial conditions correct and eliminating the transient.
  float dc_component = 0.0f;
  {
    float raw_mean = 0;
    for (int i = 0; i < N_SIGNAL; i++) raw_mean += sig->raw[i];
    raw_mean /= N_SIGNAL;
    dc_component = raw_mean; // Store raw signal mean as DC component
    for (int i = 0; i < N_SIGNAL; i++) sig->raw[i] -= raw_mean;
    Serial.print(F("  DC offset removed (mean = ")); Serial.print(raw_mean, 1); Serial.println(F(")"));
  }

  // ── 2. Zero-phase Butterworth bandpass filter ─────────────────────
  zeroPhaseSosFilter(sig->raw, sig->cleaned, N_SIGNAL, BUTTER_SOS, BUTTER_N_SECTIONS, sig->work);

  Serial.println(F("Step 2: Butterworth filter applied."));
  Serial.print  (F("  cleaned[0..4] = "));
  for (int i = 0; i < 5; i++) { Serial.print(sig->cleaned[i], 6); Serial.print(' '); }
  Serial.println();

  // ── 2b. (Normalisation deferred to AFTER Elgendi) ──────────────────
  //  NeuroKit2 runs Elgendi on the raw Butterworth-filtered signal (not
  //  pre-normalised). It clips negatives and squares. Running Elgendi on
  //  the normalised signal changes mean_sq and the block boundaries, causing
  //  different peaks than NK2 for the same signal.
  //  We normalise only after peak detection so that beat extraction &
  //  trough detection operate on the [0,1] version (matching the notebook's
  //  per-beat normalization before averaging).

  // ── 3. Elgendi peak detection (on raw AC signal, like NK2) ───────────
  int n_peaks = detectPeaksElgendi(sig->cleaned, N_SIGNAL, g_peaks, sig->sq, sig->ma_peak, sig->ma_beat);
  Serial.print(F("Step 3: Peaks detected = ")); Serial.println(n_peaks);
  if (n_peaks < 5) {
    Serial.println(F("ERROR: Too few peaks detected."));
    free(bb); free(sig);
    return false;
  }
  Serial.print(F("  First 5 peak indices: "));
  for (int i = 0; i < min(5, n_peaks); i++) { Serial.print(g_peaks[i]); Serial.print(' '); }
  Serial.println();

  // ── 4. Trough detection ───────────────────────────────────────────
  int n_troughs = detectTroughs(sig->cleaned, g_peaks, n_peaks, g_troughs);
  Serial.print(F("Step 4: Troughs found = ")); Serial.println(n_troughs);

  // ── 5. RR intervals ───────────────────────────────────────────────
  int n_rr = computeRR(g_peaks, n_peaks, g_rr_ms);
  Serial.print(F("Step 5: RR intervals = ")); Serial.print(n_rr);
  Serial.print(F(", mean = "));
  float rr_sum = 0;
  for (int i = 0; i < n_rr; i++) rr_sum += g_rr_ms[i];
  Serial.print(rr_sum / n_rr, 2); Serial.println(F(" ms"));
  Serial.print(F("  First 5 RR (ms): "));
  for (int i = 0; i < min(5, n_rr); i++) { Serial.print(g_rr_ms[i], 1); Serial.print(' '); }
  Serial.println();

  // ── 6. Average beat ───────────────────────────────────────────────
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

  // Signal buffers no longer needed — free before feature extraction
  free(sig); sig = nullptr;
  if (n_clean < 0) {
    Serial.println(F("ERROR: Average beat construction failed."));
    free(bb);
    return false;
  }
  Serial.print(F("Step 6: Avg beat built from ")); Serial.print(n_clean);
  Serial.println(F(" clean beats."));
  Serial.print(F("  avg_beat[0..4] = "));
  for (int i = 0; i < 5; i++) { Serial.print(g_avg_beat[i], 6); Serial.print(' '); }
  Serial.println();

  // ── 7. Extract stable 0th-order hardware & pulse width features ─────
  float peak_val         = computePeakVal(g_avg_beat);
  float reflection_index = computeReflectionIndex(g_avg_beat);

  // Pulse Width features
  float pw50, pw25, pw75, pw_ratio;
  computePulseWidth(g_avg_beat, &pw50, &pw25, &pw75, &pw_ratio);

  // RMS of average beat
  float rms_sq_sum = 0.0f;
  for (int k = 0; k < AVG_BEAT_LEN; k++) rms_sq_sum += g_avg_beat[k] * g_avg_beat[k];
  float rms_val = sqrtf(rms_sq_sum / (float)AVG_BEAT_LEN);

  // ── 8. Build stable 10-feature vector ─────────────────────────────
  features_out[0] = IS_POSTPRANDIAL;
  features_out[1] = reflection_index;
  features_out[2] = peak_val;
  features_out[3] = dc_component;
  features_out[4] = log_ac_dc_ratio;
  features_out[5] = rms_val;
  features_out[6] = pw50;
  features_out[7] = pw25;
  features_out[8] = pw75;
  features_out[9] = pw_ratio;

  // Free remaining allocated working buffers
  free(bb);
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println(F("\n"));
  Serial.println(F("╔══════════════════════════════════════════════════════╗"));
  Serial.println(F("║   GlucoSense — Feature Parity Validation (Row 3)    ║"));
  Serial.println(F("║   Phase 2: Pulse Width Feature Extensions           ║"));
  Serial.println(F("╚══════════════════════════════════════════════════════╝"));
  Serial.println(F("Signal: P003, fasting, expected BGL = 110 mg/dL"));
  printSep();

  double features[10] = {0};
  bool ok = runInferencePipeline(features);

  printSep();
  Serial.println(F("EXTRACTED FEATURES vs PYTHON GROUND-TRUTH (Row 3):"));
  printSep();

  if (ok) {
    printFeature(0, "is_postprandial ", features[0], 0.000000);
    printFeature(1, "reflection_index", features[1], 0.983279);
    printFeature(2, "peak_val        ", features[2], 0.882984);
    printFeature(3, "dc_component    ", features[3], 40952.474667);
    printFeature(4, "log_ac_dc_ratio ", features[4], -5.307745);
    printFeature(5, "rms_val         ", features[5], 0.599915);
    printFeature(6, "pulse_width_50  ", features[6], 62.000000);
    printFeature(7, "pulse_width_25  ", features[7], 88.000000);
    printFeature(8, "pulse_width_75  ", features[8], 41.000000);
    printFeature(9, "pw_ratio_50_25  ", features[9], 0.704545);
    printSep();

    // ── Run XGBoost BGL Model Inference ──────────────────────────────
    Serial.println(F("MODEL INFERENCE:"));
    double bgl_raw = score(features);

    if (bgl_raw < 40.0)  bgl_raw = 40.0;
    if (bgl_raw > 400.0) bgl_raw = 400.0;

    Serial.print(F("  Raw model output (score): ")); Serial.println((float)bgl_raw, 4);
    Serial.print(F("  Predicted BGL           : ")); Serial.print((float)bgl_raw, 1);
    Serial.println(F(" mg/dL"));
    Serial.print(F("  Expected BGL (actual)   : ")); Serial.print(EXPECTED_BGL_MGDL);
    Serial.println(F(" mg/dL"));
    float pred_err = fabsf((float)(bgl_raw - EXPECTED_BGL_MGDL));
    Serial.print(F("  Absolute error          : ")); Serial.print(pred_err, 1);
    Serial.println(F(" mg/dL"));
  } else {
    Serial.println(F("  PIPELINE FAILED — feature extraction incomplete."));
  }

  printSep();
  Serial.println(F("Done. Entering idle loop."));
}

void loop() {
  delay(10000);
}
