#include <Arduino.h>

// UART Slave Configuration for ESP32-C3 (half-duplex: TX only)
#define UART_TX_PIN 21  // GPIO 21 - TX pin for sending data to master
#define UART_BAUD 9600  // UART baud rate (must match master)
#define SEND_INTERVAL 5000  // Send sensor data every 5000ms (5 seconds)

// Sensor data structure (must match master)
struct SensorData {
    float oil_temp;      // Oil temperature in Fahrenheit
    float motor_temp;    // Motor temperature in Fahrenheit
    float vibration;     // Vibration in g
    float current_draw;  // Current draw in Amperes
};

// Current sensor data
SensorData sensor_data;

// Timer for periodic data transmission
unsigned long last_send_time = 0;

// Function to generate random sensor values (0-10 range)
void generateRandomSensorData() {
    // Generate random values between 0.0 and 10.0
    sensor_data.oil_temp = random(0, 1001) / 100.0;      // 0.00 to 10.00
    sensor_data.motor_temp = random(0, 1001) / 100.0;     // 0.00 to 10.00
    sensor_data.vibration = random(0, 1001) / 100.0;      // 0.00 to 10.00
    sensor_data.current_draw = random(0, 1001) / 100.0;   // 0.00 to 10.00
}

void setup() {
    // ESP32-C3 uses native USB Serial - needs longer delay for USB to initialize
    Serial.begin(9600);
    delay(2000);  // Give USB Serial time to initialize on ESP32-C3
    
    Serial.println("\n\n========================================");
    Serial.println("ESP32-C3 UART Slave Starting...");
    Serial.println("========================================\n");
    
    // Initialize random seed (using analog noise or time)
    randomSeed(analogRead(A0) + millis());
    
    // Initialize sensor data with random values
    generateRandomSensorData();
    
    // Initialize UART for master communication (half-duplex: TX only)
    // Serial1 (UART1) with TX on GPIO 21
    // Slave TX -> Master RX (for data)
    Serial1.begin(UART_BAUD, SERIAL_8N1, -1, UART_TX_PIN);  // TX only, no RX needed
    
    Serial.println("UART Slave initialized (half-duplex TX only)");
    Serial.print("UART TX Pin: GPIO ");
    Serial.println(UART_TX_PIN);
    Serial.print("UART Baud Rate: ");
    Serial.println(UART_BAUD);
    Serial.print("Send Interval: ");
    Serial.print(SEND_INTERVAL);
    Serial.println(" ms");
    Serial.flush();
    
    Serial.println("\nSending sensor data periodically...");
    Serial.println("Current sensor values:");
    Serial.print("  Oil Temp: ");
    Serial.print(sensor_data.oil_temp);
    Serial.println("°F");
    Serial.print("  Motor Temp: ");
    Serial.print(sensor_data.motor_temp);
    Serial.println("°F");
    Serial.print("  Vibration: ");
    Serial.print(sensor_data.vibration);
    Serial.println("g");
    Serial.print("  Current Draw: ");
    Serial.print(sensor_data.current_draw);
    Serial.println("A");
    Serial.println("========================================\n");
    Serial.flush();
}

void loop() {
    // Send sensor data periodically (half-duplex: slave initiates transmission)
    unsigned long current_time = millis();
    if (current_time - last_send_time >= SEND_INTERVAL) {
        last_send_time = current_time;
        
        // Generate new sensor data
        generateRandomSensorData();
        
        Serial.print("[UART] Sending sensor data - Oil: ");
        Serial.print(sensor_data.oil_temp);
        Serial.print("°F, Motor: ");
        Serial.print(sensor_data.motor_temp);
        Serial.print("°F, Vib: ");
        Serial.print(sensor_data.vibration);
        Serial.print("g, Current: ");
        Serial.print(sensor_data.current_draw);
        Serial.println("A");
        
        // Send sensor data (4 floats = 16 bytes) via UART
        uint8_t* data_ptr = (uint8_t*)&sensor_data;
        Serial1.write(data_ptr, 16);
        Serial1.flush();  // Wait for transmission to complete
        
        Serial.println("[UART] 16 bytes sent");
        Serial.flush();
    }
    
    // Small delay to prevent tight loop
    delay(1);
}

