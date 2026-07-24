3.3.2 Software System Architecture
The software architecture comprises four sequential processing stages implemented in firmware on the MCU, together with an offline data processing and model development pipeline executed in Python on a host computer.


Figure 3.1(a): Block diagram of data flow during data collection mode

Figure 3.1(b): Block diagram of data flow during data collection mode
3.3.3 Two-Mode Operation
The same hardware platform will operate in two distinct firmware modes, distinguished solely by the firmware image loaded onto the MCU. Figure 3.1 illustrates the block-level data flow through the system, from the Sensing Unit through Signal Processing, Timekeeping, Data Logging, and the Alert Mechanism.
Table 3.2: Comparison of  Data Collection and Inference Operating Modes 
Attribute
Data Collection Mode (DAQ)
Inference Mode (Deployment)
Primary function
Capture and log raw physiological signals


Predict hypoglycemic episode and alert patient
Sensor operation
Continuous 50 Hz sampling, all data written to SD
Continuous 50 Hz sampling, buffered in RAM
Processing
Minimal — timestamp + raw write only
Full pipeline: filter, RR extraction, HRV features, TinyML inference
SD Card output
Raw CSV: timestamp, PPG IBI, ax, ay, az
Inference log: timestamp, P(hypo), label, feature vector
OLED display
"Recording" + elapsed time + HH:MM
Current HR + "Normal" / "ALERT"
Buzzer
Not active
3-beep pattern when P(hypo) is detected


3.4.2 Software System Components
The software system comprises two distinct pipeline environments: the on-device firmware (embedded C++, deployed on the Arduino Nano 33 BLE Sense), and the offline data processing and machine learning pipeline (Python, executed on a host computer).
Table 3.3 summarises the software tools, frameworks and libraries to be used across both pipeline environments.
Table 3.3: Software Tools and Libraries
Tool/Library
Version
Environment
Role in this Project
Python
3.12+
Offline
Primary language for data processing and ML pipeline
pandas
2.x
Offline
CSV loading, timestamp alignment, resampling, windowing
numpy
1.24+
Offline
Numerical computation for feature extraction
neurokit2
0.2+
Offline
PPG cleaning, peak detection, HRV feature computation (RMSSD, SDNN, pNN50, LF/HF)
scipy
1.11+
Offline
Bandpass filter design, PSD (Welch), spectral analysis of tremor band
scikit-learn
1.3+
Offline
Random Forest classifier, train/test split, cross-validation, performance metrics (AUROC, sensitivity, specificity, FPR)
matplotlib
3.7+
Offline
ROC curves, feature importance plots, glucose-signal alignment visualisation
Edge Impulse
Cloud
Deployment
TinyML pipeline: model upload, optimisation, TF Lite micro firmware generation for nRF52840
Arduino C++
2.x IDE
On-device
Firmware for both data collection and inference mode
TF Lite Micro
2.14+
On-device
Quantised model inference engine on NRF52840 Cortex-M4F


3.5 Dataset Preparation
This section describes the complete pipeline from raw sensor output and CGM export to the final labelled feature dataset used for machine learning model training and evaluation.
3.5.1 Data Collection Protocol
Data collection will be conducted as a free-living observational study over 14 consecutive days. A single T2DM participant recruited through the endocrinology outpatient clinic of OAU Teaching Hospital, Ile-Ife, will be enrolled following informed consent. The participant will wear two concurrent sensing systems:
    • The custom DAQ wearable device on the inner wrist, operating in Data Collection Mode, logging raw PPG and accelerometry at 50 Hz to the microSD card with RTC timestamps.
    • The Abbott FreeStyle Libre 2 CGM sensor applied to the upper arm, scanning automatically every 5 minutes and exporting the full glucose history via the LibreView website as a structured CSV file.
The participant additionally maintains a daily diary recording meal times, food descriptions. Three in-person study visits occur at Day 0 (enrollment and device fitting), Day 7 (mid-study check, sensor replacement, data backup), and Day 14 (device retrieval and data extraction).
In the event that no spontaneous hypoglycaemic event is captured during the 14-day free-living period, a single controlled induction session will be conducted at the OAU Teaching Hospital endocrinology clinic under the direct supervision of the co-supervising endocrinologist. The participant will attend in a fasting state; supervised insulin administration will gradually lower blood glucose toward the 70 mg/dL threshold under continuous clinical monitoring. The DAQ device and CGM will be worn throughout the procedure. Immediate glucose intervention (oral or intravenous) will be administered by the clinical team the moment the threshold is confirmed. As a further contingency, if insufficient hypoglycaemic events are captured to support binary classification, the study will pivot to a blood glucose level prediction framework in which the model outputs predicted glucose values at t+30 minutes as a continuous target, with hypoglycemia detection retained as a derived output (predicted value < 70 mg/dL triggers the alert).
3.5.2 Raw Data Files
The data collection phase should produce three source files:
    • DAQ CSV (from SD card): columns timestamp (ISO 8601), ppg_ibi (inter-beat interval), ax, ay, az (acceleration in g units × 1000), sampled at 50 Hz. Estimated size: ≈2.5 GB per 14-day session.
    • CGM CSV (from LibreView): columns Device, Serial Number, Timestamp, Record Type, Historic Glucose (mmol/L), Scan Glucose (mmol/L), Notes. One row per 5-minute interval.
    • Participant diary: columns date, time, meal detail.
3.5.3 Data Cleaning and Alignment
The following cleaning and alignment steps will be applied using Python (pandas, numpy):
Parse timestamps to UTC-aware datetime objects; convert all streams to a common timezone reference.
Resample the DAQ stream to 5-minute mean aggregates to match the CGM sampling resolution.
Merge the resampled DAQ and CGM DataFrames on the 5-minute timestamp using a left join; forward-fill CGM values for gaps ≤15 minutes (one missed scan); drop windows where the gap exceeds 15 minutes.
Identify and remove windows with excessive signal noise: PPG windows where the interquartile range of the raw waveform falls below a minimum quality threshold (indicating sensor dislodgement) are flagged and excluded.
Device-off detection: windows where all accelerometry values are zero for ≥1 minute are flagged as off-wrist periods and excluded. 
3.5.4 Labelling with 30-Minute Prediction Horizon
Each 5-minute window is assigned a binary label based on whether the CGM reading at t+30 minutes falls below 70 mg/dL (the clinical hypoglycemia threshold). The continuous glucose value at t+30 minutes is retained in the dataset as an additional column for contingency regression modelling but is excluded from the model input feature vector to prevent data leakage.
