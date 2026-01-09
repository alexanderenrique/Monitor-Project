#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>

// SPI Slave Configuration for ESP32-C3
// Seeed Studio C3 pin assignments
#define SPI_MOSI 10  // Master Out Slave In (slave receives on this pin)
#define SPI_MISO 9   // Master In Slave Out (slave transmits on this pin)
#define SPI_SCK 8    // Serial Clock
#define SPI_CS 5     // Chip Select (CS pin - master controls this) - Changed from GPIO2 to GPIO5

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
    
    // Configure CS pin as input - FLOATING (no pull-up/pull-down)
    // Master will drive CS LOW when selecting slave, HIGH when not selected
    pinMode(SPI_CS, INPUT);
    
    // Explicitly set CS to floating (no pull-up/pull-down)
    gpio_set_pull_mode((gpio_num_t)SPI_CS, GPIO_FLOATING);
    
    // Read initial CS state
    delay(10);
    bool initial_cs = digitalRead(SPI_CS);
    Serial.print("[DEBUG] Initial CS pin state: ");
    Serial.println(initial_cs == LOW ? "LOW" : "HIGH");
    Serial.flush();
    
    // CRITICAL: Configure MOSI and SCK as INPUT with NO pull-up/pull-down BEFORE SPI.begin()
    // MOSI and SCK on slave should be INPUT - master drives them, slave reads them
    // ESP32-C3 might default to pull-up, so we explicitly set them to INPUT (no pull)
    pinMode(SPI_MOSI, INPUT);
    pinMode(SPI_SCK, INPUT);
    
    // Configure MISO pin as OUTPUT (slave drives this, master reads it)
    // We'll configure it after SPI.begin() to ensure it stays as OUTPUT
    pinMode(SPI_MISO, OUTPUT);
    digitalWrite(SPI_MISO, HIGH);  // Start HIGH
    
    // Initialize SPI in slave mode
    // For ESP32-C3, SPI.begin() with pin parameters configures SPI slave mode
    // Parameters: SCK, MISO, MOSI, CS
    // CS pin parameter tells SPI which pin to monitor for slave mode
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS);
    
    // Note: ESP32-C3 SPI slave mode defaults to MODE0 (CPOL=0, CPHA=0)
    // The setDataMode() and setBitOrder() methods may not be available or needed
    // SPI mode is determined by the master's SPISettings during beginTransaction()
    // Slave automatically matches the master's mode
    
    // CRITICAL: Reconfigure MOSI and SCK as INPUT after SPI.begin() to remove any pull-up SPI.begin() added
    // SPI.begin() might configure them with pull-up, so we force them back to INPUT (no pull)
    // This ensures slave doesn't fight master's LOW signal
    pinMode(SPI_MOSI, INPUT);
    pinMode(SPI_SCK, INPUT);
    
    // ESP32-C3 specific: Explicitly disable pull-up and pull-down on MOSI and SCK pins
    // This ensures the pins don't fight the master
    gpio_set_pull_mode((gpio_num_t)SPI_MOSI, GPIO_FLOATING);
    gpio_set_pull_mode((gpio_num_t)SPI_SCK, GPIO_FLOATING);
    
    // Reconfigure MISO as OUTPUT after SPI.begin() to ensure slave can drive it
    pinMode(SPI_MISO, OUTPUT);
    digitalWrite(SPI_MISO, HIGH);  // Start HIGH
    
    // Test MISO output - toggle it to verify it's working
    Serial.println("[DEBUG] Testing MISO pin output...");
    digitalWrite(SPI_MISO, LOW);
    delay(10);
    bool miso_test_low = digitalRead(SPI_MISO);
    digitalWrite(SPI_MISO, HIGH);
    delay(10);
    bool miso_test_high = digitalRead(SPI_MISO);
    Serial.print("[DEBUG] MISO LOW test: ");
    Serial.print(miso_test_low == LOW ? "PASS" : "FAIL");
    Serial.print(" | MISO HIGH test: ");
    Serial.println(miso_test_high == HIGH ? "PASS" : "FAIL");
    Serial.print("[DEBUG] MISO pin number: GPIO");
    Serial.println(SPI_MISO);
    Serial.flush();
    
    // Verify MOSI is actually LOW (should be floating/low when nothing connected)
    delay(10);
    bool mosi_state = digitalRead(SPI_MOSI);
    Serial.print("[DEBUG] MOSI pin state after config: ");
    Serial.println(mosi_state == LOW ? "LOW" : "HIGH");
    Serial.print("[DEBUG] MOSI pin number: GPIO");
    Serial.println(SPI_MOSI);
    Serial.flush();
    
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

// Super simple serial read - no strict timing, just wait for SCK changes
// 10ms delays give plenty of time for 2m wire propagation
uint8_t simpleSerialRead() {
    uint8_t data = 0;
    
    // Read 8 bits MSB first
    for (int i = 7; i >= 0; i--) {
        // Wait for SCK to go HIGH (master signals "read this bit now")
        // No timeout - just wait (master controls timing)
        while (digitalRead(SPI_SCK) == LOW) {
            delay(1);  // Check every 1ms
        }
        
        // Read MOSI (master has set it and waited 10ms, so it's stable)
        bool mosi_bit = digitalRead(SPI_MOSI);
        if (mosi_bit) {
            data |= (1 << i);
        }
        
        // Wait for SCK to go LOW (master signals "done with this bit")
        while (digitalRead(SPI_SCK) == HIGH) {
            delay(1);  // Check every 1ms
        }
    }
    
    return data;
}

// Super simple serial write - no strict timing, just set MISO when SCK changes
// 10ms delays give plenty of time for 2m wire propagation
void simpleSerialWrite(uint8_t data) {
    // Send 8 bits MSB first
    for (int i = 7; i >= 0; i--) {
        // Set MISO bit (master will wait 10ms before reading, so we have time)
        digitalWrite(SPI_MISO, (data >> i) & 0x01);
        delay(5);  // Small delay to ensure signal is set
        
        // Wait for SCK to go HIGH (master signals "I'm reading MISO now")
        while (digitalRead(SPI_SCK) == LOW) {
            delay(1);  // Check every 1ms
        }
        
        // Wait for SCK to go LOW (master signals "done reading this bit")
        while (digitalRead(SPI_SCK) == HIGH) {
            delay(1);  // Check every 1ms
        }
    }
}

void loop() {
    // Poll CS pin - ESP32-C3 SPI slave needs to detect CS LOW
    bool cs_state = digitalRead(SPI_CS);
    
    // Check if CS is LOW (master is selecting us)
    if (cs_state == LOW) {
        Serial.println("\n=== SLAVE SPI TRANSACTION START ===");
        Serial.println("[SLAVE] CS detected LOW - transaction starting");
        
        // Check initial pin states
        Serial.print("[SLAVE] Initial pin states - CS: ");
        Serial.print(digitalRead(SPI_CS) == LOW ? "LOW" : "HIGH");
        Serial.print(", MOSI: ");
        Serial.print(digitalRead(SPI_MOSI) == LOW ? "LOW" : "HIGH");
        Serial.print(", SCK: ");
        Serial.print(digitalRead(SPI_SCK) == LOW ? "LOW" : "HIGH");
        Serial.print(", MISO: ");
        Serial.println(digitalRead(SPI_MISO) == LOW ? "LOW" : "HIGH");
        
        // Ensure pins are configured correctly
        pinMode(SPI_MISO, OUTPUT);
        digitalWrite(SPI_MISO, HIGH);  // Start HIGH (idle state)
        pinMode(SPI_MOSI, INPUT);
        gpio_set_pull_mode((gpio_num_t)SPI_MOSI, GPIO_FLOATING);
        
        Serial.println("[SLAVE] Pins configured - MISO: OUTPUT HIGH, MOSI: INPUT FLOATING");
        
        // Sample pins multiple times before transfer
        Serial.println("[SLAVE] Sampling pins before SPI.transfer()...");
        bool mosi_samples_before[10];
        bool sck_samples_before[10];
        for (int i = 0; i < 10; i++) {
            mosi_samples_before[i] = digitalRead(SPI_MOSI);
            sck_samples_before[i] = digitalRead(SPI_SCK);
            delayMicroseconds(5);
        }
        Serial.print("[SLAVE] MOSI samples before: ");
        for (int i = 0; i < 10; i++) {
            Serial.print(mosi_samples_before[i] == LOW ? "L" : "H");
        }
        Serial.print(" | SCK samples before: ");
        for (int i = 0; i < 10; i++) {
            Serial.print(sck_samples_before[i] == LOW ? "L" : "H");
        }
        Serial.println();
        
        // Use simple serial to read command
        Serial.println("[SLAVE] Using simple serial to read command...");
        Serial.print("[SLAVE] SCK state before: ");
        Serial.println(digitalRead(SPI_SCK) == LOW ? "LOW" : "HIGH");
        
        unsigned long transfer_start = millis();
        uint8_t cmd = simpleSerialRead();
        unsigned long transfer_duration = millis() - transfer_start;
        
        // Sample pins after transfer
        Serial.println("[SLAVE] Sampling pins after SPI.transfer()...");
        bool mosi_samples_after[10];
        bool sck_samples_after[10];
        for (int i = 0; i < 10; i++) {
            mosi_samples_after[i] = digitalRead(SPI_MOSI);
            sck_samples_after[i] = digitalRead(SPI_SCK);
            delayMicroseconds(5);
        }
        Serial.print("[SLAVE] MOSI samples after: ");
        for (int i = 0; i < 10; i++) {
            Serial.print(mosi_samples_after[i] == LOW ? "L" : "H");
        }
        Serial.print(" | SCK samples after: ");
        for (int i = 0; i < 10; i++) {
            Serial.print(sck_samples_after[i] == LOW ? "L" : "H");
        }
        Serial.print(" | Transfer duration: ");
        Serial.print(transfer_duration);
        Serial.println(" us");
        
        Serial.print("[SLAVE] Final pin states - MOSI: ");
        Serial.print(digitalRead(SPI_MOSI) == LOW ? "LOW" : "HIGH");
        Serial.print(", SCK: ");
        Serial.print(digitalRead(SPI_SCK) == LOW ? "LOW" : "HIGH");
        Serial.print(", MISO: ");
        Serial.print(digitalRead(SPI_MISO) == LOW ? "LOW" : "HIGH");
        Serial.print(" | Received command: 0x");
        Serial.println(cmd, HEX);
        
        // Analyze: Did SCK toggle?
        bool sck_toggled = false;
        for (int i = 1; i < 10; i++) {
            if (sck_samples_before[i] != sck_samples_before[0] || 
                sck_samples_after[i] != sck_samples_after[0]) {
                sck_toggled = true;
                break;
            }
        }
        Serial.print("[SLAVE] SCK toggled during transfer: ");
        Serial.println(sck_toggled ? "YES" : "NO");
        
        // Analyze: Did MOSI change?
        bool mosi_changed = false;
        for (int i = 0; i < 10; i++) {
            if (mosi_samples_before[i] != mosi_samples_after[i]) {
                mosi_changed = true;
                break;
            }
        }
        Serial.print("[SLAVE] MOSI changed during transfer: ");
        Serial.println(mosi_changed ? "YES" : "NO");
        
        Serial.flush();
        
        // Process command - always send sensor data for testing
        if (cmd == CMD_REQUEST_DATA) {
            Serial.println("[SLAVE] Command 0x01 received - sending sensor data");
        } else {
            Serial.print("[SLAVE] Received command: 0x");
            Serial.print(cmd, HEX);
            Serial.println(" (not 0x01, but sending sensor data anyway for testing)");
        }
        
        // Always generate and send sensor data (for testing)
        generateRandomSensorData();
        
        Serial.print("[SLAVE] Data to send - Oil: ");
        Serial.print(sensor_data.oil_temp);
        Serial.print("°F, Motor: ");
        Serial.print(sensor_data.motor_temp);
        Serial.print("°F, Vib: ");
        Serial.print(sensor_data.vibration);
        Serial.print("g, Current: ");
        Serial.print(sensor_data.current_draw);
        Serial.println("A");
        
        // Send sensor data (4 floats = 16 bytes) using bit-bang
        uint8_t* data_ptr = (uint8_t*)&sensor_data;
        Serial.println("[SLAVE] Sending 16 bytes of sensor data using bit-bang...");
        for (int i = 0; i < 16; i++) {
            unsigned long byte_start = micros();
                simpleSerialWrite(data_ptr[i]);
            unsigned long byte_duration = micros() - byte_start;
            if (i < 4) {  // Debug first 4 bytes
                Serial.print("[SLAVE] Sent byte ");
                Serial.print(i);
                Serial.print(": 0x");
                if (data_ptr[i] < 0x10) Serial.print("0");
                Serial.print(data_ptr[i], HEX);
                Serial.print(" (");
                Serial.print(byte_duration);
                Serial.println(" us)");
            }
        }
        Serial.println("[SLAVE] All 16 bytes sent");
        Serial.flush();
        
        // Wait for CS to go HIGH before next check
        Serial.println("[SLAVE] Waiting for CS to go HIGH...");
        unsigned long cs_wait_start = micros();
        while (digitalRead(SPI_CS) == LOW) {
            if (micros() - cs_wait_start > 10000) {
                Serial.println("[SLAVE] WARNING: CS LOW timeout after 10ms!");
                break;
            }
            delayMicroseconds(1);
        }
        unsigned long cs_wait_duration = micros() - cs_wait_start;
        Serial.print("[SLAVE] CS went HIGH after ");
        Serial.print(cs_wait_duration);
        Serial.println(" us");
        Serial.println("=== SLAVE SPI TRANSACTION END ===\n");
        Serial.flush();
    }
    
    // Small delay to prevent tight loop
    delayMicroseconds(10);
}

