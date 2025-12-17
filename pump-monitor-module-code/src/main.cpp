#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
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

// WiFi and Collector Configuration
// WiFi credentials are set via build flags in platformio.ini
#define WIFI_SSID "SMARTLAB_2.4G"
#define WIFI_PASSWORD "74Lynx#155BLUE=92+top"

// Collector endpoint - CHANGE THIS TO YOUR COLLECTOR'S IP ADDRESS
#define COLLECTOR_HOST "192.168.140.5"  // Update with your collector's IP
#define COLLECTOR_PORT 8000
#define COLLECTOR_PATH "/esp/receive_send"

// Sensor IDs (used to identify each sensor in the NEMO database)
#define SENSOR_ID_CURRENT 29      // Current Transformer
#define SENSOR_ID_TEMP_1 30        // Temperature Sensor 1
#define SENSOR_ID_TEMP_2 31        // Temperature Sensor 2
#define SENSOR_ID_ACCELEROMETER 32 // MPU6050 Accelerometer

// Operating mode configuration
#ifdef TEST_MODE
  const unsigned long READING_INTERVAL_MS = 30000;  // 30 seconds for test mode
  const bool ENABLE_WIFI = false;                   // No WiFi in test mode
  const bool ENABLE_UPLOAD = false;                 // No uploads in test mode
#else
  const unsigned long READING_INTERVAL_MS = 900000; // 15 minutes for production
  const bool ENABLE_WIFI = true;                    // Enable WiFi in production
  const bool ENABLE_UPLOAD = true;                  // Enable uploads in production
#endif

// MPU-6050 IMU object
Adafruit_MPU6050 mpu;
bool mpu6050_initialized = false;
bool wifi_connected = false;

// WiFi reconnection timing
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 300000; // 5 minutes in milliseconds
unsigned long last_wifi_reconnect_attempt = 0;

// Function prototypes
float readThermistor(int pin, float& millivolts, float& resistance);
float readCurrentTransformer(int pin, float& rms_millivolts);
float takeAccelerometerMeasurement();
void scanI2C();
bool connectWiFi();
bool sendSensorData(float value, int sensor_id);
void checkAndReconnectWiFi();

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
  
  // Connect to WiFi (only in production mode)
  #ifndef TEST_MODE
  Serial.println("\n=== Connecting to WiFi ===");
  wifi_connected = connectWiFi();
  
  if (wifi_connected) {
    Serial.println("✅ WiFi connected successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("❌ WiFi connection failed!");
    Serial.println("Continuing without WiFi - sensor data will not be sent");
    Serial.println("Will retry WiFi connection every 5 minutes");
  }
  
  // Initialize reconnection timer
  last_wifi_reconnect_attempt = millis();
  #else
  Serial.println("\n=== TEST MODE ===");
  Serial.println("WiFi disabled - data will only be printed to Serial");
  Serial.println("Reading sensors every 30 seconds");
  #endif
  
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
    const int TOTAL_SAMPLES = 4000;    // 10x increase for better AC cycle coverage
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
    const float MILLIVOLTS_PER_AMP = 3.6; // placeholder — adjust after calibration

    float amps = rms_mv / MILLIVOLTS_PER_AMP;
  #ifndef TEST_MODE
  Serial.println("rms_mv: " + String(rms_mv));
  #endif
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

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  const int MAX_ATTEMPTS = 20;  // 20 seconds timeout
  
  while (WiFi.status() != WL_CONNECTED && attempts < MAX_ATTEMPTS) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  } else {
    Serial.println("WiFi connection timeout");
    return false;
  }
}

void checkAndReconnectWiFi() {
  unsigned long current_time = millis();
  
  // Check if WiFi is disconnected and enough time has passed since last attempt
  if (WiFi.status() != WL_CONNECTED) {
    // Check if it's time to retry (handles millis() overflow)
    if (current_time - last_wifi_reconnect_attempt >= WIFI_RECONNECT_INTERVAL_MS || 
        current_time < last_wifi_reconnect_attempt) {  // Handle millis() overflow
      
      Serial.println("\n=== WiFi Disconnected - Attempting Reconnection ===");
      wifi_connected = connectWiFi();
      last_wifi_reconnect_attempt = current_time;
      
      if (wifi_connected) {
        Serial.println("✅ WiFi reconnected successfully!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
      } else {
        Serial.println("❌ WiFi reconnection failed - will retry in 5 minutes");
      }
    }
  } else {
    // WiFi is connected, update the flag
    if (!wifi_connected) {
      wifi_connected = true;
      Serial.println("✅ WiFi connection restored!");
    }
  }
}

bool sendSensorData(float value, int sensor_id) {
  if (!wifi_connected || WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  HTTPClient http;
  String url = "http://" + String(COLLECTOR_HOST) + ":" + String(COLLECTOR_PORT) + COLLECTOR_PATH;
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // Create JSON payload matching collector.py expected format
  // Format: {"value": 22.5, "sensor": 27}
  String jsonPayload = "{\"value\":" + String(value, 4) + ",\"sensor\":" + String(sensor_id) + "}";
  
  int httpResponseCode = http.POST(jsonPayload);
  
  bool success = (httpResponseCode == 200);
  
  if (success) {
    Serial.print("✅ Sent sensor ");
    Serial.print(sensor_id);
    Serial.print(": ");
    Serial.print(value, 4);
    Serial.print(" (HTTP ");
    Serial.print(httpResponseCode);
    Serial.println(")");
  } else {
    Serial.print("❌ Failed to send sensor ");
    Serial.print(sensor_id);
    Serial.print(" (HTTP ");
    Serial.print(httpResponseCode);
    Serial.println(")");
  }
  
  http.end();
  return success;
}

void loop() {
  // Check WiFi connection and reconnect if necessary (only in production mode)
  #ifndef TEST_MODE
  checkAndReconnectWiFi();
  #endif
  
  Serial.println("\n=== Reading Sensors ===");
  
  // 1. Read Current Transformer (sensor ID 1)
  Serial.println("Reading Current Transformer...");
  const unsigned long SMOOTHING_DURATION_MS = 5000; // 5 seconds
  unsigned long start_time = millis();
  float sum_current = 0.0;
  float sum_rms_mv = 0.0;
  int reading_count = 0;
  
  while (millis() - start_time < SMOOTHING_DURATION_MS) {
    float current_rms_mv;
    float current = readCurrentTransformer(CT_ADC_PIN, current_rms_mv);
    sum_current += current;
    sum_rms_mv += current_rms_mv;
    reading_count++;
    delay(100);
  }
  
  float smoothed_current = sum_current / reading_count;
  Serial.println("Current (5s avg): " + String(smoothed_current) + " A");
  #ifndef TEST_MODE
  if (ENABLE_UPLOAD) {
    sendSensorData(smoothed_current, SENSOR_ID_CURRENT);
  }
  #endif
  
  // 2. Read Temperature Sensor 1 (sensor ID 2)
  Serial.println("Reading Temperature Sensor 1...");
  float temp1_mv, temp1_resistance;
  float temp1 = readThermistor(TEMP_1_ADC_PIN, temp1_mv, temp1_resistance);
  Serial.println("Temperature 1: " + String(temp1) + " °C");
  #ifndef TEST_MODE
  if (ENABLE_UPLOAD) {
    sendSensorData(temp1, SENSOR_ID_TEMP_1);
  }
  #endif
  
  // 3. Read Temperature Sensor 2 (sensor ID 3)
  Serial.println("Reading Temperature Sensor 2...");
  float temp2_mv, temp2_resistance;
  float temp2 = readThermistor(TEMP_2_ADC_PIN, temp2_mv, temp2_resistance);
  Serial.println("Temperature 2: " + String(temp2) + " °C");
  #ifndef TEST_MODE
  if (ENABLE_UPLOAD) {
    sendSensorData(temp2, SENSOR_ID_TEMP_2);
  }
  #endif
  
  // 4. Read Accelerometer (sensor ID 4)
  Serial.println("Reading Accelerometer...");
  float acceleration = takeAccelerometerMeasurement();
  if (acceleration < 0) {
    if (acceleration == -1.0) {
      Serial.println("Acceleration: MPU6050 not initialized - skipping");
    } else if (acceleration == -2.0) {
      Serial.println("Acceleration: I2C communication error - skipping");
    } else {
      Serial.println("Acceleration: MPU6050 not available - skipping");
    }
  } else {
    Serial.println("Acceleration: " + String(acceleration) + " m/s²");
    #ifndef TEST_MODE
    if (ENABLE_UPLOAD) {
      sendSensorData(acceleration, SENSOR_ID_ACCELEROMETER);
    }
    #endif
  }
  
  Serial.println("=== Sensor Reading Cycle Complete ===\n");
  
  // Wait before next reading cycle
  delay(READING_INTERVAL_MS);
}

