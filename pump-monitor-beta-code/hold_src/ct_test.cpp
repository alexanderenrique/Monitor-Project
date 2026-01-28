// Diagnostic sampler for CT -> AC-coupling -> Vbias -> ESP32 ADC
// Prints raw min/max/avg, Vpp, Vrms (squared method), Vrms (from Vpp estimate).
// Adjust ADC_PIN to your pin.

#include <Arduino.h>

const int ADC_PIN = 0;
const float VREF = 3.3f;            // use 3.3V unless you have ADC calibration
const int SAMPLES = 800;            // total samples per run
const unsigned int SAMPLE_US = 200; // 200 us => 5 kHz sampling (adjustable)

void setup(){
  Serial.begin(9600);
  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN, ADC_11db); // ensure full 0-3.3V range
  delay(500);
  Serial.println("CT -> ADC diagnostic starting...");
}

void loop(){
  uint32_t sum = 0;
  uint32_t sumSq = 0;
  int raw;
  int rawMin = 4095;
  int rawMax = 0;

  // Warm-up (optional)
  for(int i=0;i<50;i++){
    analogRead(ADC_PIN);
    delayMicroseconds(100);
  }

  unsigned long t0 = micros();
  for(int i=0;i<SAMPLES;i++){
    raw = analogRead(ADC_PIN);
    sum += raw;
    sumSq += ((long)raw * (long)raw);
    if(raw < rawMin) rawMin = raw;
    if(raw > rawMax) rawMax = raw;
    // precise timing
    unsigned long target = t0 + (unsigned long)((i+1) * SAMPLE_US);
    unsigned long now = micros();
    if (now < target) delayMicroseconds(target - now);
  }

  // averages
  float avgRaw = (float)sum / (float)SAMPLES;
  float meanSqRaw = (float)sumSq / (float)SAMPLES;
  float rmsRaw = sqrt(meanSqRaw - avgRaw*avgRaw); // RMS of AC component in ADC counts

  // conversions to volts
  float minV = (rawMin * VREF) / 4095.0f;
  float maxV = (rawMax * VREF) / 4095.0f;
  float avgV = (avgRaw * VREF) / 4095.0f;
  float rmsV = (rmsRaw * VREF) / 4095.0f;

  // Vpp-based estimate for a sine: Vrms_est = Vpp/(2*sqrt(2))
  float vpp = maxV - minV;
  float vrms_from_vpp = vpp / (2.0f * 1.41421356237f);

  Serial.println("---- CT ADC DIAG ----");
  Serial.print("Samples: "); Serial.println(SAMPLES);
  Serial.print("Raw min/max: "); Serial.print(rawMin); Serial.print(" / "); Serial.println(rawMax);
  Serial.print("Raw avg: "); Serial.println((int)avgRaw);
  Serial.print("Raw RMS (AC counts): "); Serial.println((int)rmsRaw);
  Serial.print("Vmin / Vmax / Vavg: ");
  Serial.print(minV, 4); Serial.print(" V / ");
  Serial.print(maxV, 4); Serial.print(" V / ");
  Serial.print(avgV, 4); Serial.println(" V");
  Serial.print("Vpp: "); Serial.print(vpp, 4); Serial.println(" V");
  Serial.print("Vrms (squared method): "); Serial.print(rmsV, 4); Serial.println(" V");
  Serial.print("Vrms (from Vpp estimate): "); Serial.print(vrms_from_vpp, 4); Serial.println(" V");
  Serial.println("---------------------");
  delay(2000);
}
