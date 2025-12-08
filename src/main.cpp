#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>


// Pin configuration
#define CT_ADC_PIN 0
#define TEMP_1_ADC_PIN 1
#define TEMP_2_ADC_PIN 2

#define SDA_PIN 21
#define SCL_PIN 22

// MPU-6050 IMU object
Adafruit_MPU6050 mpu;

// Function prototypes
float readThermistor(int pin);
float readCurrentTransformer(int pin);
float takeAccelerometerMeasurement();

void setup() {
  Serial.begin(115200);

  pinMode(CT_ADC_PIN, INPUT);
  pinMode(TEMP_1_ADC_PIN, INPUT);
  pinMode(TEMP_2_ADC_PIN, INPUT);
  
  // Initialize ADC pins, set the range to be full range 
  analogSetAttenuation(ADC_11db);  // 11dB attenuation allows 0-3.3V input range
  
  // Initialize I2C for MPU-6050
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize MPU-6050
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }
  
  // Configure MPU-6050 accelerometer range (±2g, ±4g, ±8g, or ±16g)
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  
  Serial.println("MPU6050 initialized");
}

float readThermistor(int pin) {
    // average the ADC readings over ten readings, waiting 10ms between readings
    int sum = 0;
    for (int i=0 ; i<10 ; i++){
      sum +=  analogReadMilliVolts (pin);
      delay (10);
    }

    int adc_value = sum/10;
    //take the adc value and convert it to a voltage
    float voltage = adc_value * 3.3 / 4095;
    //from the voltage, calculate the resistance of the thermistor, but we need to know the resistance of the other resistor in the circuit 
    // I used a 2200 ohm resistor to shift things on the hotter side. 
    float resistance = 2200 * (3.3 - voltage) / voltage;
    
    // Convert resistance to temperature using Beta equation
    // Beta equation: 1/T = 1/T0 + (1/Beta) * ln(R/R0)
    // For 10kΩ NTC thermistor: Beta = 3950K, R0 = 10000Ω at 25°C, T0 = 298.15K
    const float BETA = 3950.0;           // Beta coefficient (K)
    const float R0 = 10000.0;            // Reference resistance at 25°C (Ω)
    const float T0 = 298.15;             // Reference temperature (K) = 25°C
    
    float ln_R_R0 = log(resistance / R0);
    float temp_kelvin = 1.0 / (1.0/T0 + (1.0/BETA) * ln_R_R0);
    float temp_celsius = temp_kelvin - 273.15;
    
    return temp_celsius;
}

float readCurrentTransformer(int pin) {
  const float DC_OFFSET_MV = 1650.0;
  const int SAMPLES_PER_CYCLE = 40;
  const int NUMBER_CYCLES = 2;
  const int TOTAL_SAMPLES = NUMBER_CYCLES*SAMPLES_PER_CYCLE;
  const float SAMPLE_INTERVAL_US = 16666.67 / SAMPLES_PER_CYCLE;  // ~416us for 40 samples/cycle
  const float CT_RATIO = 10.0;

  long sum_squared = 0;
  unsigned long start_time = micros();

  for (int i=0 ; i< TOTAL_SAMPLES ; i++){
    int voltage_mv = analogReadMilliVolts(pin);

    float ac_component = voltage_mv - DC_OFFSET_MV;
    sum_squared += (long)(ac_component * ac_component);

    unsigned long elapsed = micros()-start_time;
    unsigned long target_time = (i + 1) * SAMPLE_INTERVAL_US;
    if (elapsed < target_time) {
      delayMicroseconds(target_time - elapsed);
    }
  }

  float rms_voltage = sqrt(sum_squared / TOTAL_SAMPLES);
  float rms_current = (rms_voltage/1000.0) * CT_RATIO; // from milliVolts to volts, then times the ct ratio if 10A = 1V, say you measured 0.5V, then you have 5A. 
  return rms_current;
}

float takeAccelerometerMeasurement() {
    // Note: pin_number parameter is not used - MPU-6050 uses I2C communication
    // Read multiple samples and calculate RMS acceleration
    const int NUM_SAMPLES = 10;
    const int SAMPLE_DELAY_MS = 10;
    
    float sum_squared = 0.0;
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        // Get new sensor event
        sensors_event_t accel, gyro, temp;
        mpu.getEvent(&accel, &gyro, &temp);
        
        // Calculate magnitude of acceleration vector: sqrt(x^2 + y^2 + z^2)
        float magnitude = sqrt(accel.acceleration.x * accel.acceleration.x + 
                               accel.acceleration.y * accel.acceleration.y + 
                               accel.acceleration.z * accel.acceleration.z);
        
        // Accumulate squared values for RMS calculation
        sum_squared += magnitude * magnitude;
        
        delay(SAMPLE_DELAY_MS);
    }
    
    // Calculate RMS: sqrt(mean of squares)
    float rms_acceleration = sqrt(sum_squared / NUM_SAMPLES);
    
    return rms_acceleration;  // Returns RMS acceleration in m/s^2
}

void loop() {
  // Read thermistors (10kOhm)
  float temp1 = readThermistor(TEMP_1_ADC_PIN);
  Serial.println("Temperature 1: " + String(temp1));
  Serial.println("--------------------------------");
  float temp2 = readThermistor(TEMP_2_ADC_PIN);
  Serial.println("Temperature 2: " + String(temp2));
  Serial.println("--------------------------------");
  // Read current transformer (centered at 1.65V)
  float current = readCurrentTransformer(CT_ADC_PIN);
  Serial.println("Current: " + String(current));
  Serial.println("--------------------------------");
  // Read accelerometer
  float acceleration = takeAccelerometerMeasurement();
  Serial.println("Acceleration: " + String(acceleration));
  Serial.println("--------------------------------");
  delay(1000); // TODO: Adjust sampling rate
}

