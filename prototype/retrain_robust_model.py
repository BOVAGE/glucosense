#!/usr/bin/env python3
"""
GlucoSense — Robust Model Retraining & C Header Exporter

Extracts 6 hardware-robust features directly matching the ESP32 C pipeline:
  1. is_postprandial (0=fasting, 1=postprandial)
  2. e_a_ratio        (APG wave e/a ratio)
  3. reflection_index (dicrotic wave reflection ratio)
  4. apg_e            (minimum of 2nd derivative APG)
  5. peak_val         (average beat peak amplitude)
  6. mean_hr          (mean heart rate in BPM calculated from RR intervals)

Outputs:
  - prototype/xgb_bgl_model.h
  - xgb_bgl_model.h (root)
"""

import os
import numpy as np
import pandas as pd
from scipy.signal import butter, sosfiltfilt
from xgboost import XGBRegressor
from sklearn.metrics import mean_absolute_error, r2_score
import m2cgen as m2c

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATASET_PATH = os.path.join(BASE_DIR, 'data', 'real', 'glucosense_r_dataset_2026-09-17.csv')

print(f"Loading dataset: {DATASET_PATH}")
df = pd.read_csv(DATASET_PATH)

def extract_c_features(row):
    try:
        raw = np.array([float(x) for x in str(row['ppg_data']).split(';') if x.strip()])
        if len(raw) < 3250:
            return None
        sliced = raw[250:3250].astype(np.float64)
        
        # 3rd order 0.5 - 8.0 Hz Butterworth filter
        sos = butter(3, [0.5, 8.0], btype='bandpass', fs=50.0, output='sos')
        cleaned = sosfiltfilt(sos, sliced)
        
        # Elgendi peak detection
        sqrd = np.where(cleaned < 0, 0, cleaned) ** 2
        w1, w2 = int(np.round(0.111 * 50)), int(np.round(0.533 * 50))
        ma_p = np.convolve(sqrd, np.ones(w1)/w1, mode='same')
        ma_q = np.convolve(sqrd, np.ones(w2)/w2, mode='same')
        blocks = (ma_p > ma_q).astype(int)
        diff = np.diff(np.pad(blocks, (1,1)))
        starts, ends = np.where(diff == 1)[0], np.where(diff == -1)[0]
        
        peaks = []
        for s, e in zip(starts, ends):
            if (e - s) < 2: continue
            p = s + np.argmax(cleaned[s:e])
            if len(peaks) > 0 and (p - peaks[-1]) < 15: continue
            if p < 25: continue
            peaks.append(p)
        if len(peaks) < 5: return None
        
        rrs = np.diff(peaks) * (1000.0 / 50.0)
        valid_rrs = rrs[(rrs >= 400) & (rrs <= 2000)]
        mean_rr = np.mean(valid_rrs) if len(valid_rrs) > 0 else np.mean(rrs)
        mean_hr = 60000.0 / mean_rr if mean_rr > 0 else 75.0
        
        troughs = []
        for i in range(len(peaks)-1):
            t = peaks[i] + np.argmin(cleaned[peaks[i]:peaks[i+1]])
            troughs.append(t)
            
        beats = []
        for i in range(len(troughs)-1):
            b = cleaned[troughs[i]:troughs[i+1]]
            if 18 <= len(b) <= 80:
                b_interp = np.interp(np.linspace(0, 1, 100), np.linspace(0, 1, len(b)), b)
                beats.append(b_interp)
        if len(beats) < 3: return None
        
        beats = np.array(beats)
        med_beat = np.median(beats, axis=0)
        valid_beats = [b for b in beats if np.corrcoef(b, med_beat)[0,1] >= 0.80]
        if len(valid_beats) < 3: valid_beats = beats
        avg_beat = np.mean(valid_beats, axis=0)
        
        pk_idx = np.argmax(avg_beat)
        peak_val = avg_beat[pk_idx]
        post_pk = avg_beat[pk_idx:]
        ref_idx = 0.0
        if len(post_pk) > 5:
            d1 = np.gradient(post_pk)
            zc = np.where(np.diff(np.sign(d1)) > 0)[0]
            if len(zc) > 0:
                ref_idx = avg_beat[pk_idx + zc[0]] / peak_val if peak_val != 0 else 0.0
            else:
                ref_idx = post_pk[len(post_pk)//2] / peak_val if peak_val != 0 else 0.0
                
        vpg = np.gradient(avg_beat)
        apg = np.gradient(vpg)
        apg_a = apg[np.argmax(apg[:30])] if len(apg)>=30 else 1.0
        apg_e = apg[np.argmin(apg)]
        e_a_ratio = abs(apg_e / apg_a) if apg_a != 0 else 0.0
        
        is_post = 0.0 if row['session_kind'] == 'fasting' else 1.0
        
        return {
            'is_postprandial': is_post,
            'e_a_ratio': e_a_ratio,
            'reflection_index': ref_idx,
            'apg_e': apg_e,
            'peak_val': peak_val,
            'mean_hr': mean_hr,
            'bgl': float(row['bgl_mgdl'])
        }
    except Exception as e:
        return None

feats = [extract_c_features(r) for _, r in df.iterrows()]
fdf = pd.DataFrame([f for f in feats if f is not None])

feature_cols = ['is_postprandial', 'e_a_ratio', 'reflection_index', 'apg_e', 'peak_val', 'mean_hr']
X = fdf[feature_cols].values
y = fdf['bgl'].values

print(f"Extracted {len(fdf)} valid samples for model training.")

# Fit XGBoost Regressor
model = XGBRegressor(n_estimators=30, max_depth=3, learning_rate=0.08, random_state=42)
model.fit(X, y)

y_pred = model.predict(X)
mae = mean_absolute_error(y, y_pred)
r2 = r2_score(y, y_pred)

print("\n=== Model Performance on Dataset ===")
print(f"  Training MAE : {mae:.2f} mg/dL")
print(f"  Training R^2 : {r2:.3f}")

# Export to C code
c_code = m2c.export_to_c(model)

header_content = f"""// Auto-generated XGBoost model for BGL estimation using m2cgen.
// Trained on hardware-robust C-extracted PPG features (6 inputs).
// Inputs:
//   input[0] = is_postprandial (0=fasting, 1=postprandial)
//   input[1] = e_a_ratio
//   input[2] = reflection_index
//   input[3] = apg_e
//   input[4] = peak_val
//   input[5] = mean_hr (BPM)

#ifndef XGB_BGL_MODEL_H
#define XGB_BGL_MODEL_H

{c_code}

#endif // XGB_BGL_MODEL_H
"""

# Write headers
proto_header = os.path.join(BASE_DIR, 'prototype', 'xgb_bgl_model.h')
root_header = os.path.join(BASE_DIR, 'xgb_bgl_model.h')

with open(proto_header, 'w') as f:
    f.write(header_content)
with open(root_header, 'w') as f:
    f.write(header_content)

print(f"\n✓ Saved model C header to:\n  - {proto_header}\n  - {root_header}")

# Test P003 (row 3)
p003_val = fdf.iloc[3][feature_cols].values
p003_pred = model.predict(p003_val.reshape(1,-1))[0]
print(f"\nSanity Check (P003, expected BGL 110 mg/dL):")
print(f"  Extracted Features : {dict(zip(feature_cols, np.round(p003_val, 4)))}")
print(f"  Model Output BGL   : {p003_pred:.1f} mg/dL")
