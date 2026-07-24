// Auto-generated Random Forest model for BGL estimation using m2cgen.
// Trained on statistically augmented pilot dataset.
// MAE: 5.14 mg/dL, R^2: 0.898
// Do not modify directly.

#ifndef MODEL_COEFFICIENTS_H
#define MODEL_COEFFICIENTS_H

namespace m2c {
double score(double * input) {
    double var0;
    if (input[3] <= 11.474853754043579) {
        var0 = 40.0;
    } else {
        if (input[3] <= 299.84937286376953) {
            if (input[2] <= 142.50850677490234) {
                if (input[1] <= 43.48945236206055) {
                    var0 = 90.16511519082984;
                } else {
                    var0 = 102.71277056384011;
                }
            } else {
                if (input[2] <= 169.21522521972656) {
                    var0 = 138.0265529533507;
                } else {
                    var0 = 111.20582544759297;
                }
            }
        } else {
            if (input[3] <= 416.98388671875) {
                if (input[0] <= 72.77697372436523) {
                    var0 = 136.40591826642364;
                } else {
                    var0 = 133.62436241116265;
                }
            } else {
                if (input[3] <= 429.5696716308594) {
                    var0 = 129.5835907001349;
                } else {
                    var0 = 130.9819478555009;
                }
            }
        }
    }
    double var1;
    if (input[3] <= 11.302754640579224) {
        var1 = 40.0;
    } else {
        if (input[0] <= 74.38874435424805) {
            if (input[1] <= 126.79397964477539) {
                if (input[1] <= 65.40124988555908) {
                    var1 = 134.59114800726712;
                } else {
                    var1 = 141.92628311711783;
                }
            } else {
                if (input[3] <= 21.164618492126465) {
                    var1 = 111.8714453182611;
                } else {
                    var1 = 108.75227414112418;
                }
            }
        } else {
            if (input[2] <= 25.531519889831543) {
                if (input[3] <= 208.9889907836914) {
                    var1 = 90.03914155154484;
                } else {
                    var1 = 92.92137270714552;
                }
            } else {
                if (input[3] <= 84.42415618896484) {
                    var1 = 115.757872709382;
                } else {
                    var1 = 107.55137872507977;
                }
            }
        }
    }
    double var2;
    if (input[3] <= 11.289604306221008) {
        var2 = 40.0;
    } else {
        if (input[0] <= 92.98113250732422) {
            if (input[2] <= 52.723751068115234) {
                if (input[3] <= 414.95831298828125) {
                    var2 = 134.34236789979568;
                } else {
                    var2 = 129.743127479584;
                }
            } else {
                if (input[3] <= 94.9509162902832) {
                    var2 = 116.504800377895;
                } else {
                    var2 = 107.49487364814122;
                }
            }
        } else {
            if (input[0] <= 99.25302505493164) {
                if (input[0] <= 95.69868087768555) {
                    var2 = 92.55127299981508;
                } else {
                    var2 = 88.55578672301505;
                }
            } else {
                if (input[3] <= 190.60575103759766) {
                    var2 = 94.00282686776745;
                } else {
                    var2 = 92.39147723606051;
                }
            }
        }
    }
    double var3;
    if (input[3] <= 11.302754640579224) {
        if (input[1] <= 215.74847412109375) {
            var3 = 40.0;
        } else {
            if (input[1] <= 217.84063720703125) {
                var3 = 21.0;
            } else {
                var3 = 40.0;
            }
        }
    } else {
        if (input[3] <= 295.8774185180664) {
            if (input[2] <= 142.50850677490234) {
                if (input[0] <= 90.3719711303711) {
                    var3 = 103.42053015831623;
                } else {
                    var3 = 90.72617486311007;
                }
            } else {
                if (input[2] <= 169.21522521972656) {
                    var3 = 137.61758549058644;
                } else {
                    var3 = 112.58610914159175;
                }
            }
        } else {
            if (input[3] <= 387.37718200683594) {
                if (input[1] <= 30.39177417755127) {
                    var3 = 139.12941079877783;
                } else {
                    var3 = 134.51954277755354;
                }
            } else {
                if (input[1] <= 30.75639247894287) {
                    var3 = 133.0549179379266;
                } else {
                    var3 = 135.0720283301438;
                }
            }
        }
    }
    double var4;
    if (input[3] <= 11.375918626785278) {
        var4 = 40.0;
    } else {
        if (input[3] <= 295.8774185180664) {
            if (input[2] <= 142.50850677490234) {
                if (input[3] <= 154.3022003173828) {
                    var4 = 103.447578850881;
                } else {
                    var4 = 90.50017563637073;
                }
            } else {
                if (input[2] <= 170.5304412841797) {
                    var4 = 140.230398850777;
                } else {
                    var4 = 112.35700336218905;
                }
            }
        } else {
            if (input[0] <= 73.25767135620117) {
                if (input[2] <= 27.72432804107666) {
                    var4 = 135.23629568439833;
                } else {
                    var4 = 139.3404840328251;
                }
            } else {
                if (input[0] <= 74.69258880615234) {
                    var4 = 132.1929724359181;
                } else {
                    var4 = 136.6079907733125;
                }
            }
        }
    }
    return (var0 + var1 + var2 + var3 + var4) * 0.2;
}

} // namespace m2c

/**
 * Predicts BGL locally on the ESP32 using the m2cgen exported Random Forest.
 * Input array matches: [0] mean_hr, [1] sdnn, [2] rmssd, [3] ppg_amp
 */
inline float predictBGL(float mean_hr, float sdnn, float rmssd, float ppg_amp) {
  double input[4] = { (double)mean_hr, (double)sdnn, (double)rmssd, (double)ppg_amp };
  double pred = m2c::score(input);
  
  // Bound to physiological range (60 - 350 mg/dL)
  if (pred < 60.0) pred = 60.0;
  if (pred > 350.0) pred = 350.0;
  
  return (float)pred;
}

#endif // MODEL_COEFFICIENTS_H
