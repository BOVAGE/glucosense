I want you to build a complete, research-grade Jupyter Notebook for my final-year research project.

Do NOT approach this as simply "train several ML models and find the highest R²."

I want the notebook to perform a **complete analysis of my experimental PPG-BGL dataset**, starting with participant/dataset demographics and descriptive statistics, then exploring the physiological/signal relationships, performing literature-informed PPG feature engineering, and finally developing and rigorously evaluating machine-learning regression models.

The notebook should be suitable as the computational foundation for **Chapter 4 (Results and Analysis)** of my final-year project.

My research topic is:

**Non-invasive blood glucose estimation using PPG and machine learning**

The fundamental research question is:

> Can blood glucose level (BGL) be estimated from PPG-derived features using machine-learning regression?

The model must ultimately use **PPG-derived information only** as predictors.

---

# 1. DATASET BACKGROUND

The dataset was collected by me from human participants in a clinical setting.

I currently have:

**73 paired PPG-BGL recordings.**

Each recording contains:

- PPG signal
- blood glucose measurement obtained using a glucometer
- participant information/demographic information where available

PPG acquisition:

- Sensor: **GY-MAX30100**
- Sampling frequency: **50 Hz**
- Original recording duration: **70 seconds**
- First 5 seconds should be discarded
- Last 5 seconds should be discarded
- Final analysis window: **60 seconds**
- Final samples per recording: **3,000 samples**

Target:

```python
BGL_mg_dL
```

Unit:

**mg/dL**

---

# 2. VERY IMPORTANT: FIRST UNDERSTAND THE DATASET

Before building any ML model, inspect the actual uploaded dataset.

Do NOT assume column names or data organisation.

Determine whether the CSV is:

### Format A

One row per recording:

```text
recording_id | participant_id | age | sex | weight | height | BGL | ppg_1 | ppg_2 | ...
```

or:

### Format B

One row per PPG sample:

```text
recording_id | participant_id | age | sex | weight | height | BGL | timestamp | PPG
```

or another structure.

Build a clean internal representation such as:

```python
recordings = [
    {
        "recording_id": ...,
        "participant_id": ...,
        "bgl": ...,
        "ppg": np.array(...),
        "demographics": {...}
    }
]
```

The notebook should be adaptable to the actual dataset.

---

# 3. CHAPTER 4 — DATASET AND PARTICIPANT CHARACTERISTICS

Before doing any ML, produce a proper descriptive analysis of the dataset.

This section should generate tables and figures that can directly support Chapter 4.

## 3.1 Dataset overview

Report:

- total number of recordings
- number of unique participants
- recordings per participant
- minimum recordings per participant
- maximum recordings per participant
- average recordings per participant
- median recordings per participant
- number of missing observations
- duplicate records
- invalid/corrupted recordings
- PPG duration
- sampling frequency

Create a clean summary table.

---

# 4. DEMOGRAPHIC ANALYSIS

If the dataset contains demographic information, analyse it properly.

Possible variables include:

- Age
- Sex
- Weight
- Height
- BMI, if available or calculable
- Diabetes status
- Diabetes duration
- Other non-sensitive study variables present in the dataset

Do NOT invent variables that do not exist.

## Continuous variables

For variables such as:

- age
- weight
- height
- BMI
- BGL

calculate:

- N
- mean
- standard deviation
- minimum
- Q1
- median
- Q3
- maximum
- IQR

Present them in a publication-style descriptive statistics table.

Example:

| Variable | N | Mean | SD | Min | Median | Q1 | Q3 | Max |
|---|---:|---:|---:|---:|---:|---:|---:|---:|

Use appropriate units:

- Age: years
- Weight: kg
- Height: cm
- BMI: kg/m²
- BGL: mg/dL

---

# 5. CATEGORICAL DEMOGRAPHIC VARIABLES

For variables such as sex, report:

- frequency
- percentage

Example:

| Variable | Category | n | % |
|---|---|---:|---:|

Create appropriate visualisations such as:

- bar charts
- histograms
- boxplots

Do NOT create misleading pie charts unless there is a strong reason.

---

# 6. BGL DISTRIBUTION

Perform a detailed analysis of the target variable.

Calculate:

- N
- mean
- SD
- minimum
- Q1
- median
- Q3
- maximum
- IQR
- coefficient of variation
- skewness
- kurtosis

Create:

- histogram
- KDE where appropriate
- boxplot
- violin plot if useful

Investigate whether BGL values are:

- narrowly distributed
- approximately normal
- skewed
- affected by outliers
- concentrated in particular ranges

If clinically meaningful glucose categories are used, report them descriptively only.

Do NOT convert the regression problem into classification.

The target remains continuous BGL in mg/dL.

---

# 7. BGL BY PARTICIPANT

Analyse how BGL varies between participants.

Produce:

- participant-level BGL distributions
- participant-level mean BGL
- participant-level median BGL
- participant-level range

This is important because participant-specific variation may explain why random CV and participant-wise CV produce different results.

---

# 8. BGL AND DEMOGRAPHIC RELATIONSHIPS

Where demographic variables exist, explore relationships between:

- age vs BGL
- weight vs BGL
- height vs BGL
- BMI vs BGL
- sex vs BGL

Use appropriate statistical methods and visualisations.

However:

**These variables must NOT be used as predictors in the final PPG-only ML models.**

They are only for descriptive/exploratory analysis.

Do not claim causation from correlations.

---

# 9. PPG SIGNAL QUALITY ANALYSIS

Now analyse the actual PPG signals.

For every recording:

1. Load the 70-second signal.
2. Remove the first 5 seconds.
3. Remove the final 5 seconds.
4. Retain the middle 60 seconds.

Expected:

```text
60 seconds × 50 Hz = 3,000 samples
```

Verify this for every recording.

Report recordings that do not meet expectations.

---

# 10. RAW PPG VISUALISATION

Plot representative PPG signals.

Include examples of:

- good-quality signals
- noisy signals
- low-amplitude signals
- signals with baseline drift
- unusual waveforms

Do not hide poor-quality signals.

Also generate an overview of all recordings if practical.

---

# 11. PPG PREPROCESSING

Develop a scientifically defensible preprocessing pipeline.

Investigate appropriate methods for PPG sampled at 50 Hz.

Potential operations:

- detrending
- band-pass filtering
- baseline correction
- smoothing where justified
- normalisation

Use scipy and/or NeuroKit2 where appropriate.

Clearly document:

- filter type
- filter order
- low cutoff
- high cutoff
- sampling frequency
- zero-phase filtering
- normalisation method

Do not choose preprocessing parameters merely because they produce better ML performance.

The preprocessing should be physiologically justified.

Show:

```text
Raw PPG
   ↓
Filtered PPG
   ↓
Cleaned PPG
```

for several recordings.

---

# 12. PPG SIGNAL QUALITY METRICS

Develop signal-quality indicators where possible.

Investigate:

- amplitude range
- standard deviation
- SNR
- pulse detection success rate
- percentage of valid beats
- heart-rate plausibility
- missing samples
- abnormal signal segments

Create a quality-control table for all 73 recordings.

Do not automatically delete recordings.

Flag questionable recordings and allow explicit exclusion.

---

# 13. BEAT/PULSE DETECTION

Detect individual PPG pulses from each 60-second recording.

Use NeuroKit2 or a scientifically appropriate method.

Validate the detector visually on representative recordings.

Calculate:

- number of detected pulses
- pulse rate
- inter-pulse interval
- pulse-rate distribution

Flag recordings with implausible detection results.

Do not blindly trust automated peak detection.

---

# 14. LITERATURE-INFORMED FEATURE ENGINEERING

This is a major requirement.

Before implementing advanced features, review relevant literature on:

- PPG-based blood glucose estimation
- PPG morphology
- APG/second derivative PPG
- HRV/PRV
- frequency-domain PPG features
- nonlinear physiological signal features
- machine-learning-based glucose estimation

Use literature to determine which features have previously been investigated.

For each feature family, document:

1. Feature name
2. Mathematical/algorithmic definition
3. Physiological interpretation where established
4. Relevant literature
5. Whether the feature is appropriate for 50-Hz PPG
6. Whether it can realistically be extracted from a 60-second recording

Do not add features simply because they are available in a library.

---

# 15. FEATURE ENGINEERING — TIME DOMAIN

Extract appropriate statistical PPG features.

Potential features include:

- mean
- median
- SD
- variance
- RMS
- minimum
- maximum
- range
- peak-to-peak
- coefficient of variation
- skewness
- kurtosis
- percentiles
- interquartile range
- median absolute deviation

Calculate these at both:

### Whole-recording level

and, where appropriate:

### Pulse/beat level followed by aggregation

For beat-level features calculate:

- mean
- median
- SD
- coefficient of variation

rather than treating individual beats as independent clinical observations.

---

# 16. PULSE MORPHOLOGY FEATURES

From detected PPG pulses investigate:

- pulse amplitude
- pulse duration
- rise time
- fall time
- systolic peak location
- time to peak
- maximum slope
- minimum slope
- pulse area
- width at 25%
- width at 50%
- width at 75%

Where reliably detectable, investigate:

- dicrotic notch
- diastolic peak
- reflection characteristics

Use caution because the sampling rate is only 50 Hz.

If a feature cannot be reliably estimated at 50 Hz, document that limitation rather than forcing the feature into the model.

---

# 17. FIRST AND SECOND DERIVATIVE PPG

Calculate:

### VPG — first derivative

### APG/SDPPG — second derivative

Extract relevant morphology features.

Investigate the standard APG waves:

- a
- b
- c
- d
- e

and ratios such as:

- b/a
- c/a
- d/a
- e/a

Only use these where the signal quality and sampling resolution make the feature reliable.

The notebook should investigate whether these features are actually useful for BGL estimation in my dataset rather than assuming they are useful.

---

# 18. FREQUENCY-DOMAIN FEATURES

Using appropriate spectral analysis:

Calculate features such as:

- dominant frequency
- spectral centroid
- spectral bandwidth
- spectral entropy
- total power
- relative power
- band power

Use Welch's method where appropriate.

Be careful about physiological interpretation at 50 Hz.

Do not use arbitrary frequency bands without justification.

---

# 19. HR / PRV FEATURES

Because this is PPG rather than ECG, clearly distinguish:

**Pulse Rate Variability (PRV)**

from ECG-derived HRV.

Where pulse detection is reliable, calculate appropriate:

### Time-domain PRV

- mean NN/PP interval
- SDNN-equivalent measure
- RMSSD
- pNN-type metrics

### Frequency-domain PRV

- LF power
- HF power
- LF/HF where meaningful

### Nonlinear PRV

- sample entropy
- approximate entropy
- Poincaré features
- DFA
- other robust nonlinear measures

Document limitations caused by the 60-second recording length.

Do not calculate unstable frequency-domain metrics simply to increase the number of features.

---

# 20. NONLINEAR FEATURES

Where computationally and scientifically justified, investigate:

- Sample Entropy
- Approximate Entropy
- Permutation Entropy
- Lempel-Ziv Complexity
- DFA
- multifractal features

Again:

Do not blindly include every possible nonlinear feature.

Assess whether 60 seconds at 50 Hz provides enough data for each method.

---

# 21. FEATURE ENGINEERING SUMMARY

Produce a master feature dataset:

```text
recording_id
participant_id
BGL
feature_1
feature_2
...
feature_n
```

The final feature dataset must contain **one row per recording**.

Therefore:

73 recordings → at most 73 rows.

Do NOT treat the 3,000 PPG samples within a recording as separate observations.

---

# 22. FEATURE QUALITY ANALYSIS

For every feature calculate:

- missing %
- mean
- SD
- min
- Q1
- median
- Q3
- max
- skewness
- kurtosis
- number of unique values

Identify:

- constant features
- near-zero variance features
- highly missing features
- extreme outliers

Create a feature-quality report.

---

# 23. RELATIONSHIP BETWEEN PPG FEATURES AND BGL

This is one of the most important sections.

Do not jump directly to machine learning.

Investigate whether the PPG-derived features actually contain information related to BGL.

For every feature calculate:

### Pearson correlation

and

### Spearman correlation

with BGL.

Report:

- correlation coefficient
- p-value where appropriate
- confidence interval where feasible

Create:

- correlation heatmap
- ranked feature-BGL correlation table
- feature vs BGL scatterplots for important features

Interpret correlations carefully.

A correlation does NOT establish that the feature physiologically causes or determines BGL.

---

# 24. NONLINEAR FEATURE-BGL RELATIONSHIPS

Correlation alone may miss nonlinear relationships.

Investigate:

- mutual information
- rank relationships
- partial dependence where appropriate
- simple nonlinear plots

Determine whether features with weak Pearson correlation may still have predictive information.

---

# 25. FEATURE-TO-FEATURE RELATIONSHIPS

Investigate multicollinearity.

Create:

- Pearson feature correlation matrix
- Spearman feature correlation matrix

Identify groups of highly correlated features.

For example:

```text
feature A ─────┐
feature B ─────┤ highly correlated
feature C ─────┘
```

This is important because PPG feature engineering may produce many redundant features.

---

# 26. PARTICIPANT-SPECIFIC FEATURE PATTERNS

This is extremely important.

Investigate whether PPG features cluster by participant.

For important features:

- plot feature distributions by participant
- calculate within-participant vs between-participant variation where feasible
- examine whether features appear strongly participant-specific

This will help explain differences between:

**Random KFold**

and

**GroupKFold**

performance.

---

# 27. BASELINE MODEL

Before complex ML:

Create a simple baseline that always predicts:

**mean training BGL**

Evaluate it using cross-validation.

This establishes whether ML models are actually learning anything beyond the mean predictor.

---

# 28. MACHINE LEARNING MODELS

Train and compare multiple regression models.

At minimum:

### Linear models

- Mean baseline
- Linear Regression
- Ridge
- Lasso
- Elastic Net

### Tree models

- Decision Tree
- Random Forest
- Extra Trees
- Gradient Boosting
- HistGradientBoosting

### Gradient boosting

- XGBoost
- LightGBM
- CatBoost

### Kernel

- SVR with RBF

Use other models only where justified.

---

# 29. FEATURE SET EXPERIMENTS

Do not use only one giant feature set.

Compare:

### Feature Set A

Basic statistical PPG features.

### Feature Set B

Statistical + morphology.

### Feature Set C

Statistical + morphology + derivative/APG.

### Feature Set D

Statistical + morphology + spectral.

### Feature Set E

All valid features.

### Feature Set F

Selected features.

This will help determine which PPG feature families actually contribute to BGL estimation.

---

# 30. FEATURE SELECTION

Feature selection must be performed correctly.

Do NOT select features using the entire dataset before cross-validation.

Investigate:

- correlation filtering
- mutual information
- SelectKBest
- RFE
- LASSO
- tree-based importance
- permutation importance

All target-dependent feature selection must occur **inside the training fold**.

Report feature-selection stability across folds.

---

# 31. DATA PREPROCESSING FOR ML

Use proper pipelines.

For models that require scaling:

```text
Imputation
↓
Scaling
↓
Feature selection
↓
Model
```

All fitted inside the CV process.

Tree models generally do not require scaling.

Do not perform preprocessing on the full dataset before cross-validation if it learns parameters from the data.

---

# 32. CROSS-VALIDATION EXPERIMENTS

This is a critical part of the research.

Run three different evaluation strategies.

---

## Experiment A — IN-SAMPLE PERFORMANCE

Train the model on all available observations and predict those same observations.

Report:

- MAE
- RMSE
- R²
- Median Absolute Error

Clearly label this:

**IN-SAMPLE / TRAINING PERFORMANCE**

It must NOT be presented as generalisation performance.

This analysis is specifically useful because some PPG-BGL literature reports very high performance from small datasets, and I want to investigate how much of this apparent performance comes from model fitting versus genuine out-of-sample prediction.

---

# 33. EXPERIMENT B — RANDOM 5-FOLD K-FOLD

Use:

```python
KFold(
    n_splits=5,
    shuffle=True,
    random_state=42
)
```

Generate out-of-fold predictions.

Report:

- fold MAE
- fold RMSE
- fold R²
- mean
- SD
- confidence intervals where appropriate

Save every prediction.

---

# 34. EXPERIMENT C — PARTICIPANT-WISE GROUP K-FOLD

This is the most important validation experiment.

Use participant ID as the grouping variable.

```python
GroupKFold(n_splits=5)
```

No participant can appear in both training and validation in the same fold.

Report:

- fold MAE
- fold RMSE
- fold R²
- mean
- SD
- confidence intervals

Save:

```text
recording_id
participant_id
actual_BGL
predicted_BGL
residual
fold
```

This should be considered the primary assessment of generalisation to unseen participants.

---

# 35. LEAVE-ONE-GROUP-OUT

If the number of unique participants makes this statistically meaningful, also run:

```python
LeaveOneGroupOut()
```

Each participant is held out once.

Report:

- overall MAE
- RMSE
- R²
- participant-level MAE
- participant-level bias

Clearly explain the limitations of LOGO with a small number of participants.

---

# 36. NESTED GROUP CROSS-VALIDATION

For the strongest candidate models, implement nested CV where computationally feasible.

Outer loop:

```text
GroupKFold
```

Inner loop:

```text
GroupKFold
```

Use the inner loop for:

- hyperparameter tuning
- feature selection
- model selection

The outer validation participant must remain completely untouched.

This should be treated as the strongest estimate of model generalisation.

---

# 37. HYPERPARAMETER OPTIMISATION

Use modest searches because the dataset is small.

Possible methods:

- RandomizedSearchCV
- GridSearchCV

Do not conduct huge searches.

Do not tune against the final test/validation data.

Important:

Hyperparameter tuning must occur within the training data.

---

# 38. METRICS

For every model calculate:

### Primary

- MAE
- RMSE
- R²

### Additional

- Median Absolute Error
- MAPE where mathematically appropriate
- Mean Absolute Relative Difference (MARD)

Be careful with percentage metrics when BGL approaches zero.

Do not rely on R² alone.

---

# 39. ACTUAL VS PREDICTED

For the strongest models generate:

- actual vs predicted BGL scatter plot
- identity line
- regression line
- MAE
- RMSE
- R²

Create separate plots for:

- in-sample
- random KFold OOF
- GroupKFold OOF

This should visually demonstrate the difference between fitting and generalisation.

---

# 40. RESIDUAL ANALYSIS

For the strongest models investigate:

- residual vs predicted BGL
- residual vs actual BGL
- residual distribution
- residual by participant
- residual vs important PPG features

Investigate whether errors are systematically higher:

- at low BGL
- at high BGL
- for particular participants

---

# 41. BLAND–ALTMAN ANALYSIS

For the strongest model, create a Bland–Altman analysis comparing:

**measured BGL vs predicted BGL**

Report:

- mean bias
- limits of agreement

Explain the limitations of using this analysis with a very small sample.

---

# 42. CLARKE ERROR GRID / CLINICAL ERROR ANALYSIS

If feasible, implement:

- Clarke Error Grid
- Parkes Error Grid

for the strongest model.

However, clearly state:

This dataset is an experimental research dataset and is not sufficient to establish clinical safety or clinical validity.

Error-grid results should be presented as exploratory.

---

# 43. FEATURE IMPORTANCE

For the best tree-based models calculate:

- feature importance
- permutation importance
- SHAP values where feasible

Identify the most important PPG-derived features.

Also analyse feature importance stability across folds.

Do not interpret feature importance as proof of physiological causation.

---

# 44. FEATURE SELECTION STABILITY

Across CV folds, record which features are selected.

Generate:

| Feature | Folds Selected | Selection Frequency |
|---|---:|---:|

This is particularly important because the dataset contains only 73 recordings.

A feature that ranks #1 in one fold and disappears in every other fold should not be treated as a robust finding.

---

# 45. LEARNING CURVES

For the best models, generate learning curves.

Investigate:

```text
Training size
       ↓
Model performance
```

Determine whether:

- performance is still improving as more recordings are added
- the model appears data-limited
- 73 recordings are likely insufficient

Use participant-wise validation where feasible.

---

# 46. SENSITIVITY ANALYSIS

Where reasonable, test the effect of:

- different feature counts
- different preprocessing choices
- removing clearly corrupted recordings
- removing extreme BGL observations

Do not silently cherry-pick the configuration that gives the highest score.

Clearly document every sensitivity analysis.

---

# 47. SYNTHETIC DATA AUGMENTATION — OPTIONAL

Do NOT make synthetic augmentation the main experiment.

First establish the complete real-data baseline.

Only after this should the notebook optionally investigate synthetic augmentation.

If synthetic augmentation is implemented:

- generate synthetic data only from training folds
- never generate synthetic versions of validation/test observations
- never allow synthetic samples derived from participant X into a validation fold containing participant X

Compare:

```text
REAL ONLY
vs
REAL + SYNTHETIC
```

using untouched real participant-wise validation data.

The purpose is to determine whether synthetic augmentation genuinely improves generalisation.

---

# 48. LITERATURE BENCHMARK

Create a table for comparing my results against published PPG-BGL studies.

Columns:

| Study | Subjects | Recordings | PPG duration | Sampling Rate | Features | Model | Validation | MAE | RMSE | R² |
|---|---:|---:|---:|---:|---|---|---|---:|---:|---:|

Very important:

Do not treat all published R² values as directly comparable.

Clearly distinguish:

- training/in-sample performance
- random train/test split
- random KFold
- participant-wise CV
- independent test set
- subject-specific modelling
- subject-independent modelling

If a paper does not clearly describe its validation methodology, mark it:

**Validation methodology unclear/not reported**

Do not assume that an impressive R² is an out-of-sample result.

---

# 49. FINAL MODEL COMPARISON TABLE

Generate:

| Model | Feature Set | Validation | MAE | RMSE | R² | MedAE | MARD |
|---|---|---|---:|---:|---:|---:|---:|

Include:

- in-sample
- random KFold
- GroupKFold
- LOGO where available
- nested GroupKFold for selected models

---

# 50. MODEL SELECTION

Do NOT automatically select the model with the highest R².

The primary model-selection criterion should be:

**participant-wise generalisation performance**

with particular attention to:

1. MAE
2. RMSE
3. R²
4. stability across folds

A model with slightly worse mean performance but substantially better stability may be preferable.

---

# 51. IMPORTANT INVESTIGATION: WHY DOES PERFORMANCE CHANGE?

This is a major research objective.

Compare:

```text
In-sample
     ↓
Random KFold
     ↓
GroupKFold
```

If performance decreases substantially from random KFold to GroupKFold, investigate why.

Potential explanations to examine:

- participant-specific PPG morphology
- repeated recordings
- participant-specific baseline amplitude
- BGL distribution differences
- feature distribution shifts
- insufficient participant diversity
- overfitting
- leakage

Do not simply say "the model overfits."

Provide evidence from the data.

---

# 52. DATASET SIZE ANALYSIS

Explicitly investigate whether the dataset is data-limited.

The dataset currently has only:

**73 recordings**

Determine whether model performance appears to stabilise as the number of training recordings increases.

Discuss the implications of the small dataset in the final results.

Do NOT artificially inflate the effective sample size by treating the 3,000 samples per recording as independent observations.

---

# 53. FINAL CHAPTER 4 OUTPUTS

The notebook should generate tables/figures suitable for Chapter 4.

At minimum:

### Table 1
Participant demographic characteristics.

### Table 2
BGL descriptive statistics.

### Table 3
PPG signal quality summary.

### Table 4
PPG feature descriptive statistics.

### Table 5
Feature-BGL correlation analysis.

### Table 6
Model performance comparison.

### Table 7
Participant-wise CV performance.

### Table 8
Feature importance/selection stability.

Figures:

1. Age distribution
2. Sex distribution
3. BGL distribution
4. BGL by participant
5. Representative raw PPG
6. Representative cleaned PPG
7. Feature correlation heatmap
8. Important feature vs BGL plots
9. Actual vs predicted BGL
10. Residual plot
11. Bland–Altman plot
12. Feature importance plot
13. Learning curve

---

# 54. SAVE ALL RESULTS

Automatically create:

```text
results/
    demographic_statistics.csv
    bgl_statistics.csv
    signal_quality.csv
    feature_statistics.csv
    feature_bgl_correlations.csv
    feature_selection_frequency.csv
    model_comparison.csv
    fold_results.csv
    oof_predictions.csv
    participant_performance.csv
    feature_importance.csv
```

And:

```text
figures/
```

for plots.

Save trained models using joblib where appropriate.

---

# 55. REPRODUCIBILITY

Set random seeds.

Create a configuration section:

```python
RANDOM_STATE = 42
SAMPLING_RATE = 50
ORIGINAL_DURATION = 70
TRIM_SECONDS = 5
ANALYSIS_DURATION = 60
N_SPLITS = 5
```

Keep all important parameters in one place.

---

# 56. FINAL AUTOMATED RESEARCH SUMMARY

At the end of the notebook, generate a structured summary:

## Dataset

- recordings
- participants
- demographic characteristics
- BGL range

## Signal

- preprocessing
- signal quality
- important PPG characteristics

## Features

- total features generated
- features retained
- strongest BGL associations
- feature stability

## Models

- best in-sample model
- best random-CV model
- best participant-wise model
- best nested-CV model

## Generalisation

Explicitly compare:

```text
Training performance
vs
Random CV
vs
Participant-wise CV
```

Explain the magnitude of performance degradation if present.

## Research interpretation

Discuss whether the evidence suggests:

- useful PPG-BGL relationship
- participant-specific effects
- overfitting
- insufficient sample size
- feature instability
- potential benefit of additional data
- potential benefit of synthetic augmentation

---

# 57. SCIENTIFIC WRITING REQUIREMENT

Throughout the notebook, use careful scientific language.

Avoid statements such as:

> "Feature X causes glucose changes."

Instead use:

> "Feature X demonstrated an association with BGL."

Avoid:

> "The model accurately measures blood glucose."

Use:

> "The model estimates BGL under the evaluated experimental conditions."

Avoid:

> "The model is clinically accurate."

Unless the evidence actually supports that claim.

---

# 58. LITERATURE USE

You are explicitly allowed to search the scientific literature while developing the notebook.

Use literature to inform:

- PPG preprocessing
- feature engineering
- pulse morphology
- APG features
- PRV features
- nonlinear features
- PPG-BGL relationships
- ML models
- validation methodology
- synthetic data augmentation

Prioritise:

- peer-reviewed papers
- PubMed
- IEEE
- Elsevier/ScienceDirect
- Springer
- Nature
- reputable scientific journals

For important feature-engineering decisions, include citations in the notebook markdown.

Do not simply cite papers because they use PPG.

Citations should support the actual methodological choice.

---

# 59. FINAL PRINCIPLE

The objective is NOT:

> "Find a model that gives the highest R²."

The objective is:

> **Determine whether PPG-derived features contain a reproducible and generalisable relationship with blood glucose, and determine which machine-learning approach best exploits that relationship under rigorous validation.**

A model that obtains R² = 0.95 in-sample but R² = -0.10 under participant-wise CV should be recognised as overfitting rather than presented as a successful model.

Conversely, a model with modest R² but stable participant-wise MAE may be scientifically more valuable.

The notebook should therefore prioritise:

**scientific validity > leakage prevention > reproducibility > generalisation > interpretability > performance optimisation.**

Build the notebook as an actual executable research workflow, not a collection of disconnected code snippets.