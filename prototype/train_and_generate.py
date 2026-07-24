import os
import json
import numpy as np
import pandas as pd
import neurokit2 as nk
import m2cgen as m2c
from sklearn.ensemble import RandomForestRegressor
from sklearn.metrics import mean_absolute_error, r2_score

# Paths
csv_path = "/home/bovage/Desktop/MY_PART_5/FYP/code/data/test/glucosense_dataset_2026-07-16.csv"
output_dir = "/home/bovage/Desktop/MY_PART_5/FYP/code/prototype"

print(f"Loading dataset: {csv_path}")
df = pd.read_csv(csv_path)

features_list = []
targets = []

print("Extracting candidate features from dataset rows...")
for idx, row in df.iterrows():
    if pd.isna(row['bgl_mgdl']):
        continue
        
    ppg_raw_str = row['ppg_data']
    if pd.isna(ppg_raw_str):
        continue
        
    try:
        # Parse semicolon-separated PPG data
        ppg_signal = np.array([float(x) for x in str(ppg_raw_str).split(';') if x.strip()])
        
        # Sliced stable signal
        if len(ppg_signal) < 3500:
            continue
        sliced_signal = ppg_signal[500:3500]
        
        # Clean signal
        cleaned = nk.ppg_clean(sliced_signal, sampling_rate=50, method='elgendi')
        
        # Detect peaks
        peaks_dict = nk.ppg_findpeaks(cleaned, sampling_rate=50, method='elgendi')
        peaks = peaks_dict['PPG_Peaks']
        
        if len(peaks) < 5:
            continue
            
        # NN Intervals
        nn_intervals = np.diff(peaks) * 20.0 
        
        # Compute features
        hr = nk.signal_rate(peaks, sampling_rate=50, desired_length=len(cleaned))
        mean_hr = np.mean(hr)
        sdnn = np.std(nn_intervals)
        nn_diff = np.diff(nn_intervals)
        rmssd = np.sqrt(np.mean(nn_diff ** 2))
        ppg_amp = np.std(cleaned)
        
        features_list.append({
            'mean_hr': mean_hr,
            'sdnn': sdnn,
            'rmssd': rmssd,
            'ppg_amp': ppg_amp
        })
        
        targets.append(float(row['bgl_mgdl']))
        
    except Exception as e:
        pass

print(f"Extraction complete. Found {len(features_list)} valid base samples.")

if len(features_list) == 0:
    print("Error: No valid samples found.")
    exit(1)

# Statistically Augment Pilot Dataset (Expand 15 samples to 500 samples)
print("\nPerforming statistical data augmentation...")
augmented_features = []
augmented_targets = []

# Reproducibility seed
np.random.seed(42)

for i in range(len(features_list)):
    base_feat = features_list[i]
    base_bgl = targets[i]
    
    # Keep the original
    augmented_features.append([base_feat['mean_hr'], base_feat['sdnn'], base_feat['rmssd'], base_feat['ppg_amp']])
    augmented_targets.append(base_bgl)
    
    # Generate 33 synthetic variants per sample by adding localized Gaussian noise
    for _ in range(33):
        # Adding small noise (~5% standard deviation) to simulate minor measurement variances
        new_hr = base_feat['mean_hr'] + np.random.normal(0, base_feat['mean_hr'] * 0.03)
        new_sdnn = max(1.0, base_feat['sdnn'] + np.random.normal(0, max(1.0, base_feat['sdnn'] * 0.05)))
        new_rmssd = max(1.0, base_feat['rmssd'] + np.random.normal(0, max(1.0, base_feat['rmssd'] * 0.05)))
        new_amp = max(1.0, base_feat['ppg_amp'] + np.random.normal(0, base_feat['ppg_amp'] * 0.05))
        
        # Add slight variation to BGL as well
        new_bgl = max(40.0, base_bgl + np.random.normal(0, 3.0))
        
        augmented_features.append([new_hr, new_sdnn, new_rmssd, new_amp])
        augmented_targets.append(new_bgl)

X = np.array(augmented_features)
y = np.array(augmented_targets)

print(f"Augmented dataset size: {X.shape[0]} samples.")

# Pearson Correlation check on Augmented Data
df_aug = pd.DataFrame(X, columns=['mean_hr', 'sdnn', 'rmssd', 'ppg_amp'])
df_aug['bgl_mgdl'] = y
print("\n=== PEARSON CORRELATION MATRIX (Augmented PPG vs. BGL) ===")
print(df_aug.corr()['bgl_mgdl'].to_string())

# Train RandomForestRegressor
print("\nTraining RandomForestRegressor (5 estimators, max_depth=4)...")
model = RandomForestRegressor(n_estimators=5, max_depth=4, random_state=42)
model.fit(X, y)

# Predictions & Metrics
y_pred = model.predict(X)
mae = mean_absolute_error(y, y_pred)
r2 = r2_score(y, y_pred)

print(f"\nModel Performance on Augmented Dataset:")
print(f"  Mean Absolute Error (MAE): {mae:.2f} mg/dL")
print(f"  R^2 Score: {r2:.3f}")

# Export model with m2cgen
print("\nExporting Random Forest model to C using m2cgen...")
m2c_code = m2c.export_to_c(model)

# Wrap the generated code for C++ compatibility and namespaces
cpp_code = f"""// Auto-generated Random Forest model for BGL estimation using m2cgen.
// Trained on statistically augmented pilot dataset.
// MAE: {mae:.2f} mg/dL, R^2: {r2:.3f}
// Do not modify directly.

#ifndef MODEL_COEFFICIENTS_H
#define MODEL_COEFFICIENTS_H

namespace m2c {{
{m2c_code}
}} // namespace m2c

/**
 * Predicts BGL locally on the ESP32 using the m2cgen exported Random Forest.
 * Input array matches: [0] mean_hr, [1] sdnn, [2] rmssd, [3] ppg_amp
 */
inline float predictBGL(float mean_hr, float sdnn, float rmssd, float ppg_amp) {{
  double input[4] = {{ (double)mean_hr, (double)sdnn, (double)rmssd, (double)ppg_amp }};
  double pred = m2c::score(input);
  
  // Bound to physiological range (60 - 350 mg/dL)
  if (pred < 60.0) pred = 60.0;
  if (pred > 350.0) pred = 350.0;
  
  return (float)pred;
}}

#endif // MODEL_COEFFICIENTS_H
"""

cpp_out_path = os.path.join(output_dir, "model_coefficients.h")
with open(cpp_out_path, 'w', encoding='utf-8') as f:
    f.write(cpp_code)
print(f"\n✓ C++ header saved to: {cpp_out_path}")
