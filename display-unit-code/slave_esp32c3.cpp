#include <Arduino.h>
#include <SPI.h>

// SPI Slave Configuration for ESP32-C3
// Seeed Studio C3 pin assignments
#define SPI_MOSI 10  // Master Out Slave In (slave receives on this pin)
#define SPI_MISO 9   // Master In Slave Out (slave transmits on this pin)
#define SPI_SCK 8    // Serial Clock
#define SPI_CS 2     // Chip Select (CS pin - master controls this)

// SPI Command codes (must match master)
#define CMD_REQUEST_DATA 0x01

// Sensor data structure (must match master)
struct SensorData {
    float oil_temp;      // Oil temperature in Fahrenheit
    float motor_temp;    // Motor temperature in Fahrenheit
    float vibration;     // Vibration in g
    float current_draw;  // Current draw in Amperes
};

// Current sensor data
SensorData sensor_data;

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
    Serial.println("ESP32-C3 SPI Slave Starting...");
    Serial.println("========================================\n");
    
    // Initialize random seed (using analog noise or time)
    randomSeed(analogRead(A0) + millis());
    
    // Initialize sensor data with random values
    generateRandomSensorData();
    
    // Configure CS pin as input with pull-up (master controls this)
    // Pull-up ensures CS reads HIGH when not connected (prevents false triggers)
    pinMode(SPI_CS, INPUT_PULLUP);
    
    // Initialize SPI in slave mode
    // For ESP32-C3, we need to use the SPI.begin() with pin parameters
    // Note: ESP32-C3 SPI slave mode - CS pin must be specified
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS);
    
    Serial.println("SPI Slave initialized");
    Serial.print("MOSI (RX): ");
    Serial.print(SPI_MOSI);
    Serial.print(", MISO (TX): ");
    Serial.print(SPI_MISO);
    Serial.print(", SCK: ");
    Serial.print(SPI_SCK);
    Serial.print(", CS: ");
    Serial.println(SPI_CS);
    Serial.flush();
    
    Serial.println("\nWaiting for master requests...");
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
    // Heartbeat to show loop is running
    static unsigned long last_heartbeat = 0;
    if (millis() - last_heartbeat > 5000) {  // Print every 5 seconds
        Serial.print("[HEARTBEAT] Loop running - CS: ");
        Serial.println(digitalRead(SPI_CS) == LOW ? "LOW" : "HIGH");
        Serial.flush();
        last_heartbeat = millis();
    }
    
    // Check if CS is low (master is selecting us for communication)
    // Note: CS should have pull-up resistor, so LOW means master is actively selecting us
    if (digitalRead(SPI_CS) == LOW) {
        // Small delay to debounce and ensure stable CS signal
        delayMicroseconds(10);
        if (digitalRead(SPI_CS) == LOW) {  // Double-check CS is still LOW
            Serial.println("[SPI] CS LOW detected - starting transaction");
            Serial.flush();
            
            // Master is starting a transaction
            // Read the command byte
            uint8_t cmd = SPI.transfer(0x00);
            Serial.print("[SPI] Received command: 0x");
            Serial.println(cmd, HEX);
            Serial.flush();
            
            if (cmd == CMD_REQUEST_DATA) {
                Serial.println("[SPI] CMD_REQUEST_DATA recognized - sending data");
                Serial.flush();
                
                // Generate new random values for this request
                generateRandomSensorData();
                
                // Send sensor data (4 floats = 16 bytes)
                // ESP32 is little-endian, so we can send bytes directly
                uint8_t* data_ptr = (uint8_t*)&sensor_data;
                Serial.print("[SPI] Sending bytes: ");
                for (int i = 0; i < 16; i++) {
                    uint8_t sent_byte = SPI.transfer(data_ptr[i]);
                    if (i < 4) {  // Print first 4 bytes for debugging
                        if (data_ptr[i] < 0x10) Serial.print("0");
                        Serial.print(data_ptr[i], HEX);
                        Serial.print(" ");
                    }
                }
                Serial.println("...");
                Serial.flush();
                
                // Debug output (only print occasionally to avoid flooding serial)
                static unsigned long last_print = 0;
                if (millis() - last_print > 1000) {  // Print once per second max
                    Serial.print("[DATA] Sent - Oil: ");
                    Serial.print(sensor_data.oil_temp);
                    Serial.print("°F, Motor: ");
                    Serial.print(sensor_data.motor_temp);
                    Serial.print("°F, Vib: ");
                    Serial.print(sensor_data.vibration);
                    Serial.print("g, Current: ");
                    Serial.print(sensor_data.current_draw);
                    Serial.println("A");
                    Serial.flush();
                    last_print = millis();
                }
            } else {
                // Only print unknown command if it's not 0xFF (noise/floating pin)
                if (cmd != 0xFF) {
                    Serial.print("[SPI] Unknown command received: 0x");
                    Serial.println(cmd, HEX);
                    Serial.flush();
                } else {
                    Serial.println("[SPI] Received 0xFF (no command or noise)");
                    Serial.flush();
                }
                // Unknown command - send dummy data or ignore
                Serial.println("[SPI] Sending dummy zeros");
                Serial.flush();
                for (int i = 0; i < 16; i++) {
                    SPI.transfer(0x00);
                }
            }
            Serial.println("[SPI] Transaction complete");
            Serial.flush();
        }
    }
    
    // Small delay to prevent tight loop
    delay(1);
}

