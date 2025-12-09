#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>


// Pin configuration
#define CT_ADC_PIN 2
#define TEMP_1_ADC_PIN 3
#define TEMP_2_ADC_PIN 4

// Seeed Studio XIAO ESP32-C3 I2C pins
#define SDA_PIN 6
#define SCL_PIN 7

// MPU-6050 IMU object
Adafruit_MPU6050 mpu;
bool mpu6050_initialized = false;

// Function prototypes
float readThermistor(int pin, float& millivolts, float& resistance);
float readCurrentTransformer(int pin, float& rms_millivolts);
float takeAccelerometerMeasurement();
void scanI2C();

void setup() {
  // Start Serial immediately
  Serial.begin(9600);
  
  // Wait a moment for Serial to initialize
  delay(2000);
  
  // Very first output - if you see this, Serial is working
  Serial.println("\n\n\n=== STARTING ===");
  Serial.flush();
  delay(100);
  
  Serial.println("Serial.begin() completed");
  Serial.flush();
  delay(100);
  
  Serial.println("=== Pump Monitor Module Starting ===");
  Serial.println("Serial communication established");
  Serial.flush();

  pinMode(CT_ADC_PIN, INPUT);
  pinMode(TEMP_1_ADC_PIN, INPUT);
  pinMode(TEMP_2_ADC_PIN, INPUT);
  
  Serial.println("ADC pins configured");
  
  // Initialize ADC pins, set the range to be full range 
  analogSetAttenuation(ADC_11db);  // 11dB attenuation allows 0-3.3V input range
  
  Serial.print("I2C initializing on SDA=");
  Serial.print(SDA_PIN);
  Serial.print(" SCL=");
  Serial.println(SCL_PIN);
  Serial.flush();
  delay(100);
  
  // Initialize I2C for MPU-6050
  // IMPORTANT: Wire.begin() must be called BEFORE setClock()
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(200);  // Give I2C time to stabilize
  
  // Set I2C clock speed (MPU6050 supports up to 400kHz)
  Wire.setClock(100000);  // Start with 100kHz for reliability
  
  Serial.println("I2C Wire.begin() completed");
  Serial.print("I2C clock speed: 100kHz");
  Serial.flush();
  delay(100);
  
  // Scan I2C bus to see what devices are present
  Serial.println("\nScanning I2C bus...");
  scanI2C();
  Serial.flush();
  delay(500);
  
  // Initialize MPU-6050
  Serial.println("Attempting to initialize MPU6050...");
  Serial.flush();
  
  // Try default address first (0x68), then alternate (0x69)
  bool init_success = false;
  
  Serial.println("Trying MPU6050 at address 0x68...");
  if (mpu.begin(0x68)) {
    init_success = true;
    Serial.println("Found MPU6050 at address 0x68!");
  } else {
    Serial.println("Not found at 0x68, trying 0x69...");
    delay(100);
    if (mpu.begin(0x69)) {
      init_success = true;
      Serial.println("Found MPU6050 at address 0x69!");
    }
  }
  
  if (!init_success) {
    Serial.println("ERROR: Failed to initialize MPU6050!");
    Serial.println("Check I2C connections and pull-up resistors.");
    Serial.println("MPU6050 should be at address 0x68 (or 0x69 if AD0 is high)");
    mpu6050_initialized = false;
  } else {
    // Configure MPU-6050 accelerometer range (±2g, ±4g, ±8g, or ±16g)
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu6050_initialized = true;
    Serial.println("MPU6050 initialized successfully!");
  }
  Serial.flush();
  
  Serial.println("=== Setup Complete ===\n");
}

void scanI2C() {
  byte error, address;
  int nDevices = 0;
  
  Serial.println("Scanning I2C bus...");
  
  for(address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.print(address, HEX);
      Serial.println(" !");
      nDevices++;
    } else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
    }
  }
  
  if (nDevices == 0) {
    Serial.println("No I2C devices found!");
    Serial.println("Check wiring and pull-up resistors (4.7kΩ recommended)");
  } else {
    Serial.print("Found ");
    Serial.print(nDevices);
    Serial.println(" device(s)");
  }
}

float readThermistor(int pin, float& millivolts, float& resistance) {
    // average the ADC readings over ten readings, waiting 10ms between readings
    int sum = 0;
    for (int i=0 ; i<10 ; i++){
      sum +=  analogReadMilliVolts (pin);
      delay (10);
    }

    int adc_value = sum/10;
    // Store millivolts for output
    millivolts = adc_value;
    //take the adc value and convert it to a voltage
    float voltage = adc_value/1000.0;
    //from the voltage, calculate the resistance of the thermistor, but we need to know the resistance of the other resistor in the circuit 
    // I used a 2200 ohm resistor to shift things on the hotter side. 
    // NTC is on the bottom of the voltage divider, so: R_ntc = (V_ntc * R_fixed) / (V_supply - V_ntc)
    resistance = (voltage * 2200) / (3.3 - voltage);
    
    // Convert resistance to temperature using simplified Steinhart-Hart equation
    // Simplified Steinhart-Hart: 1/T = A + B*ln(R)
    // Calibrated from two points:
    //   Point 1: 22°C (295.15K) @ 12000Ω (2800mV) - room temperature
    //   Point 2: 100°C (373.15K) @ 1324Ω (1240mV) - boiling water
    // B = (1/T1 - 1/T2) / (ln(R1) - ln(R2))
    // A = 1/T1 - B*ln(R1)
    const float SH_A = 0.000374;          // Steinhart-Hart A coefficient
    const float SH_B = 0.0003211;        // Steinhart-Hart B coefficient
    
    float ln_R = log(resistance);  // log() is natural logarithm (ln) in C/C++
    float temp_kelvin = 1.0 / (SH_A + SH_B * ln_R);
    float temp_celsius = temp_kelvin - 273.15;
    
    return temp_celsius;
}

float readCurrentTransformer(int pin, float &rms_millivolts)
{
    const int TOTAL_SAMPLES = 400;     // about 400–1000 is great
    const int SAMPLE_DELAY_US = 200;   // ~5 kHz sampling
    float sum = 0;
    float sum_sq = 0;

    // 1) First pass: measure average/bias
    for (int i = 0; i < TOTAL_SAMPLES; i++)
    {
        float mv = analogReadMilliVolts(pin);
        sum += mv;
        delayMicroseconds(SAMPLE_DELAY_US);
    }

    float dc_offset = sum / TOTAL_SAMPLES;

    // 2) Second pass: calculate RMS
    sum_sq = 0;
    for (int i = 0; i < TOTAL_SAMPLES; i++)
    {
        float mv = analogReadMilliVolts(pin);
        float ac = mv - dc_offset;
        sum_sq += ac * ac;
        delayMicroseconds(SAMPLE_DELAY_US);
    }

    float rms_mv = sqrt(sum_sq / TOTAL_SAMPLES);
    rms_millivolts = rms_mv;

    // You MUST calibrate this number:
    const float MILLIVOLTS_PER_AMP = 100.0; // placeholder — adjust after calibration

    float amps = rms_mv / MILLIVOLTS_PER_AMP;
    return amps;
}


float takeAccelerometerMeasurement() {
    if (!mpu6050_initialized) {
        return -1.0;  // Return error value if MPU6050 not initialized
    }
    
    // Note: pin_number parameter is not used - MPU-6050 uses I2C communication
    // Read multiple samples and calculate RMS acceleration
    const int NUM_SAMPLES = 10;
    const int SAMPLE_DELAY_MS = 10;
    
    float sum_squared = 0.0;
    int successful_samples = 0;
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        // Add small delay before I2C operations to prevent bus lock
        delay(5);
        
        // Get new sensor event
        sensors_event_t accel, gyro, temp;
        mpu.getEvent(&accel, &gyro, &temp);

        // Check if values are valid (NaN check)
        if (isnan(accel.acceleration.x) || isnan(accel.acceleration.y) || isnan(accel.acceleration.z)) {
            // Invalid data, skip this sample
            continue;
        }

        float z_minus_g = accel.acceleration.z - 9.81;
        
        // Calculate magnitude of acceleration vector: sqrt(x^2 + y^2 + z^2)
        float magnitude = sqrt(accel.acceleration.x * accel.acceleration.x + 
                               accel.acceleration.y * accel.acceleration.y + 
                               z_minus_g * z_minus_g);
        
        // Accumulate squared values for RMS calculation
        sum_squared += (magnitude * magnitude);
        successful_samples++;
        
        delay(SAMPLE_DELAY_MS);
    }
    
    // Return error if no successful samples
    if (successful_samples == 0) {
        return -2.0;  // Return different error value for I2C communication failure
    }
    
    // Calculate RMS: sqrt(mean of squares)
    float rms_acceleration = sqrt(sum_squared / successful_samples);
    
    return rms_acceleration;  // Returns RMS acceleration in m/s^2
}

void loop() {
  Serial.println("LOOP RUNNING");  // Simple test output
  delay(100);
  
  // Read thermistors (10kOhm)
  float temp1_mv, temp1_resistance;
  float temp1 = readThermistor(TEMP_1_ADC_PIN, temp1_mv, temp1_resistance);
  Serial.println("Temperature 1: " + String(temp1));
  Serial.println("Millivolts: " + String(temp1_mv));
  Serial.println("Resistance: " + String(temp1_resistance) + " Ω");
  Serial.println("--------------------------------");
  float temp2_mv, temp2_resistance;
  float temp2 = readThermistor(TEMP_2_ADC_PIN, temp2_mv, temp2_resistance);
  Serial.println("Temperature 2: " + String(temp2));
  Serial.println("Millivolts: " + String(temp2_mv));
  Serial.println("Resistance: " + String(temp2_resistance) + " Ω");
  Serial.println("--------------------------------");
  // Read current transformer (centered at 1.65V)
  float current_rms_mv;
  float current = readCurrentTransformer(CT_ADC_PIN, current_rms_mv);
  Serial.println("Current: " + String(current));
  Serial.println("RMS Millivolts: " + String(current_rms_mv));
  Serial.println("--------------------------------");
  // Read accelerometer
  float acceleration = takeAccelerometerMeasurement();
  if (acceleration < 0) {
    if (acceleration == -1.0) {
      Serial.println("Acceleration: MPU6050 not initialized");
    } else if (acceleration == -2.0) {
      Serial.println("Acceleration: I2C communication error");
    } else {
      Serial.println("Acceleration: MPU6050 not available");
    }
  } else {
    Serial.println("Acceleration: " + String(acceleration));
  }
  Serial.println("--------------------------------");
  delay(1000); // TODO: Adjust sampling rate
}

