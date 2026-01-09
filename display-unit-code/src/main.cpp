#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

// LVGL display buffer size
#define LVGL_BUFFER_SIZE 10 * 1024

// Display dimensions (adjust based on your display)
// Common sizes: 240x320 (ILI9341), 240x240 (ST7789), 320x240, etc.
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

// SPI Master Configuration (using HSPI bus - separate from display's VSPI)
#define SPI_CS_PIN 4        // Chip Select pin for SPI slave (changed from 4 to 15)
#define SD_CS_PIN 15         // Chip Select pin for SD card (changed from 15 to 4)
#define SPI_FREQ 10000      // SPI frequency: 10kHz (slow for debugging - each bit takes 100us)
#define SD_SPI_FREQ 25000000 // SD card SPI frequency: 25MHz (max for most cards)
#define SPI_POLL_INTERVAL 5000  // Poll slave every 5000ms (5 seconds)
#define SD_LOG_INTERVAL 900000  // Log to SD card every 15 minutes (900000ms)

// Alarm thresholds
#define TEMP_MIN 30.0       // Minimum temperature (°F) - alarm if below
#define VIBRATION_MAX 10.0  // Maximum vibration (g) - alarm if above
#define CURRENT_MAX 5.0     // Maximum current (A) - alarm if above

// HSPI pin definitions (HSPI bus pins on ESP32)
#define HSPI_MOSI 13        // GPIO13 - HSPI MOSI
#define HSPI_MISO 25        // GPIO25 - HSPI MISO (changed from GPIO12 to avoid boot strapping)
#define HSPI_SCK 14         // GPIO14 - HSPI Clock

// SPI Command codes
#define CMD_REQUEST_DATA 0x01

// HSPI instance (separate from display's VSPI) - shared by slave and SD card
SPIClass hspi(HSPI);

// SD card logging timer
unsigned long last_sd_log_time = 0;
bool sd_card_initialized = false;

// Alarm state
bool alarm_active = false;
bool alarm_acknowledged = false;

// Function forward declarations
void logAcknowledgmentToSD();

// TFT_eSPI instance
TFT_eSPI tft = TFT_eSPI();

// LVGL display buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;
static lv_color_t *buf2;

// Note: LV_TICK_CUSTOM is enabled in lv_conf.h, so LVGL uses millis() directly
// No manual tick increment needed

// Display flush callback for LVGL 8.3.11
void display_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    // Start TFT transaction
    tft.startWrite();
    
    // Set the display window
    tft.setAddrWindow(area->x1, area->y1, w, h);
    
    // Push pixels to display
    tft.pushPixels((uint16_t*)color_p, w * h);
    
    // End TFT transaction
    tft.endWrite();
    
    // Tell LVGL the flush is done
    lv_disp_flush_ready(disp_drv);
}

// Touch input read callback for LVGL 8.3.11
void touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    uint16_t x = 0, y = 0;
    bool touched = false;
    
    // Read touch - TFT_eSPI will use the TOUCH_CS pin configured in User_Setup.h
    #ifdef TOUCH_CS
    touched = tft.getTouch(&x, &y);
    #endif
    
    if (touched) {
        // Clamp coordinates to display bounds to prevent LVGL warnings
        if (x >= DISPLAY_WIDTH) x = DISPLAY_WIDTH - 1;
        if (y >= DISPLAY_HEIGHT) y = DISPLAY_HEIGHT - 1;
        
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        
        // If alarm is active, acknowledge it on touch
        if (alarm_active && !alarm_acknowledged) {
            alarm_acknowledged = true;
            Serial.println("Alarm acknowledged via touch");
            
            // Log acknowledgment event to SD card
            logAcknowledgmentToSD();
        }
        
        Serial.print("Touch - X: ");
        Serial.print(x);
        Serial.print(", Y: ");
        Serial.println(y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = 0;
        data->point.y = 0;
    }
}

// Global labels for values (so we can update them in loop())
static lv_obj_t *oil_temp_label;
static lv_obj_t *oil_temp_value_label;
static lv_obj_t *motor_temp_label;
static lv_obj_t *motor_temp_value_label;
static lv_obj_t *vibration_label;
static lv_obj_t *vibration_value_label;
static lv_obj_t *current_draw_label;
static lv_obj_t *current_draw_value_label;

// Sensor data structure (matches what slave will send)
struct SensorData {
    float oil_temp;      // Oil temperature in Fahrenheit
    float motor_temp;    // Motor temperature in Fahrenheit
    float vibration;     // Vibration in g
    float current_draw;  // Current draw in Amperes
};

// SPI polling timer
unsigned long last_poll_time = 0;

// Global styles (so we can change them for alarm mode)
static lv_style_t label_style;
static lv_style_t value_style;

// Create monitoring display screen
void create_monitoring_screen() {
    // Set screen background to white
    lv_obj_set_style_bg_color(lv_scr_act(), LV_COLOR_MAKE(255, 255, 255), 0);
    
    // Create styles for labels
    lv_style_init(&label_style);
    lv_style_set_text_color(&label_style, LV_COLOR_MAKE(0, 0, 0));  // Black text
    lv_style_set_text_font(&label_style, &lv_font_montserrat_14);  // Medium font size (14 is enabled)
    
    lv_style_init(&value_style);
    lv_style_set_text_color(&value_style, LV_COLOR_MAKE(0, 0, 0));  // Black text
    lv_style_set_text_font(&value_style, &lv_font_montserrat_24);  // Larger font for values (24 is enabled)
    
    // Calculate spacing for 4 fields on 320px height display
    int start_y = 30;
    int field_spacing = 70;  // Space between each field
    
    // Oil Temperature
    oil_temp_label = lv_label_create(lv_scr_act());
    lv_label_set_text(oil_temp_label, "Oil Temp:");
    lv_obj_add_style(oil_temp_label, &label_style, 0);
    lv_obj_set_pos(oil_temp_label, 20, start_y);
    
    oil_temp_value_label = lv_label_create(lv_scr_act());
    lv_label_set_text(oil_temp_value_label, "--°F");
    lv_obj_add_style(oil_temp_value_label, &value_style, 0);
    lv_obj_set_pos(oil_temp_value_label, 20, start_y + 25);
    
    // Motor Temperature
    motor_temp_label = lv_label_create(lv_scr_act());
    lv_label_set_text(motor_temp_label, "Motor Temp:");
    lv_obj_add_style(motor_temp_label, &label_style, 0);
    lv_obj_set_pos(motor_temp_label, 20, start_y + field_spacing);
    
    motor_temp_value_label = lv_label_create(lv_scr_act());
    lv_label_set_text(motor_temp_value_label, "--°F");
    lv_obj_add_style(motor_temp_value_label, &value_style, 0);
    lv_obj_set_pos(motor_temp_value_label, 20, start_y + field_spacing + 25);
    
    // Vibration
    vibration_label = lv_label_create(lv_scr_act());
    lv_label_set_text(vibration_label, "Vibration:");
    lv_obj_add_style(vibration_label, &label_style, 0);
    lv_obj_set_pos(vibration_label, 20, start_y + field_spacing * 2);
    
    vibration_value_label = lv_label_create(lv_scr_act());
    lv_label_set_text(vibration_value_label, "-- g");
    lv_obj_add_style(vibration_value_label, &value_style, 0);
    lv_obj_set_pos(vibration_value_label, 20, start_y + field_spacing * 2 + 25);
    
    // Current Draw
    current_draw_label = lv_label_create(lv_scr_act());
    lv_label_set_text(current_draw_label, "Current Draw:");
    lv_obj_add_style(current_draw_label, &label_style, 0);
    lv_obj_set_pos(current_draw_label, 20, start_y + field_spacing * 3);
    
    current_draw_value_label = lv_label_create(lv_scr_act());
    lv_label_set_text(current_draw_value_label, "-- A");
    lv_obj_add_style(current_draw_value_label, &value_style, 0);
    lv_obj_set_pos(current_draw_value_label, 20, start_y + field_spacing * 3 + 25);
}

// Super simple serial protocol - no strict timing, just long delays for stability
// 10ms per bit = 80ms per byte (very slow but very stable over 2m wire)
// No edge detection, no strict timing - just set data, wait, toggle clock, wait, read
uint8_t simpleSerialTransfer(uint8_t data) {
    uint8_t response = 0;
    
    // Send/receive 8 bits MSB first
    for (int i = 7; i >= 0; i--) {
        // Set MOSI bit
        digitalWrite(HSPI_MOSI, (data >> i) & 0x01);
        delay(10);  // 10ms - plenty of time for signal to propagate 2m
        
        // Toggle SCK HIGH (just a signal, not strict timing)
        digitalWrite(HSPI_SCK, HIGH);
        delay(10);  // 10ms - slave has time to read MOSI and set MISO
        
        // Read MISO (slave should have set it by now)
        response |= (digitalRead(HSPI_MISO) << i);
        
        // Toggle SCK LOW
        digitalWrite(HSPI_SCK, LOW);
        delay(10);  // 10ms - prepare for next bit
    }
    
    return response;
}

// Request sensor data from SPI slave (using BIT-BANG SPI for testing)
bool requestSensorDataBitBang(SensorData* data) {
    uint8_t buffer[16] = {0};
    
    Serial.println("\n=== MASTER SIMPLE SERIAL TRANSACTION START ===");
    
    // Configure pins manually
    pinMode(HSPI_MOSI, OUTPUT);
    pinMode(HSPI_SCK, OUTPUT);
    pinMode(HSPI_MISO, INPUT);
    pinMode(SPI_CS_PIN, OUTPUT);
    
    // Set initial states
    digitalWrite(HSPI_MOSI, LOW);
    digitalWrite(HSPI_SCK, LOW);  // Start with clock LOW
    digitalWrite(SPI_CS_PIN, HIGH);
    
    Serial.print("[SIMPLE] Initial pin states - CS: ");
    Serial.print(digitalRead(SPI_CS_PIN) == LOW ? "LOW" : "HIGH");
    Serial.print(", MOSI: ");
    Serial.print(digitalRead(HSPI_MOSI) == LOW ? "LOW" : "HIGH");
    Serial.print(", SCK: ");
    Serial.print(digitalRead(HSPI_SCK) == LOW ? "LOW" : "HIGH");
    Serial.print(", MISO: ");
    Serial.println(digitalRead(HSPI_MISO) == LOW ? "LOW" : "HIGH");
    
    // Pull CS LOW
    Serial.println("[SIMPLE] Pulling CS LOW...");
    digitalWrite(SPI_CS_PIN, LOW);
    delay(20);  // 20ms - give slave plenty of time to detect (was 100µs)
    
    // Send command byte
    Serial.print("[SIMPLE] Sending command: 0x");
    Serial.println(CMD_REQUEST_DATA, HEX);
    uint8_t cmd_response = simpleSerialTransfer(CMD_REQUEST_DATA);
    Serial.print("[SIMPLE] Received response: 0x");
    Serial.println(cmd_response, HEX);
    
    // Receive 16 bytes
    Serial.println("[SIMPLE] Receiving 16 bytes...");
    for (int i = 0; i < 16; i++) {
        buffer[i] = simpleSerialTransfer(0x00);
        if (i < 4) {
            Serial.print("[BIT-BANG] Byte ");
            Serial.print(i);
            Serial.print(": 0x");
            if (buffer[i] < 0x10) Serial.print("0");
            Serial.println(buffer[i], HEX);
        }
    }
    
    // Release CS
    digitalWrite(SPI_CS_PIN, HIGH);
    delay(20);  // 20ms - give slave time to finish (was 100µs)
    
    Serial.println("=== MASTER SIMPLE SERIAL TRANSACTION END ===\n");
    
    // Validate data
    bool all_ff = true;
    for (int i = 0; i < 16; i++) {
        if (buffer[i] != 0xFF) all_ff = false;
    }
    if (all_ff) {
        Serial.println("[SIMPLE] Error: Received all 0xFF");
        return false;
    }
    
    memcpy(&data->oil_temp, &buffer[0], sizeof(float));
    memcpy(&data->motor_temp, &buffer[4], sizeof(float));
    memcpy(&data->vibration, &buffer[8], sizeof(float));
    memcpy(&data->current_draw, &buffer[12], sizeof(float));
    
    return true;
}

// Request sensor data from SPI slave (using HSPI bus)
bool requestSensorData(SensorData* data) {
    uint8_t buffer[16] = {0};
    
    Serial.println("\n=== MASTER SPI TRANSACTION START ===");
    
    // Check initial pin states
    Serial.print("[MASTER] Initial pin states - CS: ");
    Serial.print(digitalRead(SPI_CS_PIN) == LOW ? "LOW" : "HIGH");
    Serial.print(", MOSI: ");
    Serial.print(digitalRead(HSPI_MOSI) == LOW ? "LOW" : "HIGH");
    Serial.print(", SCK: ");
    Serial.print(digitalRead(HSPI_SCK) == LOW ? "LOW" : "HIGH");
    Serial.print(", MISO: ");
    Serial.println(digitalRead(HSPI_MISO) == LOW ? "LOW" : "HIGH");
    
    // Pull CS LOW to select slave
    Serial.println("[MASTER] Pulling CS LOW...");
    digitalWrite(SPI_CS_PIN, LOW);
    delayMicroseconds(50);
    
    Serial.print("[MASTER] After CS LOW - CS: ");
    Serial.print(digitalRead(SPI_CS_PIN) == LOW ? "LOW" : "HIGH");
    Serial.print(", MISO: ");
    Serial.println(digitalRead(HSPI_MISO) == LOW ? "LOW" : "HIGH");
    
    // Begin SPI transaction with proper settings
    Serial.print("[MASTER] Beginning SPI transaction - Freq: ");
    Serial.print(SPI_FREQ);
    Serial.println(" Hz, Mode: MODE0");
    hspi.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
    
    Serial.print("[MASTER] After beginTransaction - MOSI: ");
    Serial.print(digitalRead(HSPI_MOSI) == LOW ? "LOW" : "HIGH");
    Serial.print(", SCK: ");
    Serial.print(digitalRead(HSPI_SCK) == LOW ? "LOW" : "HIGH");
    Serial.print(", MISO: ");
    Serial.println(digitalRead(HSPI_MISO) == LOW ? "LOW" : "HIGH");
    
    // Send command and receive response
    Serial.print("[MASTER] Sending command byte: 0x");
    Serial.print(CMD_REQUEST_DATA, HEX);
    Serial.println(" (0x01)");
    
    // Sample SCK before transfer to see if it toggles
    bool sck_samples_before[5];
    for (int i = 0; i < 5; i++) {
        sck_samples_before[i] = digitalRead(HSPI_SCK);
        delayMicroseconds(2);
    }
    Serial.print("[MASTER] SCK samples before transfer: ");
    for (int i = 0; i < 5; i++) {
        Serial.print(sck_samples_before[i] == LOW ? "L" : "H");
    }
    Serial.println();
    
    unsigned long transfer_start = micros();
    uint8_t cmd_response = hspi.transfer(CMD_REQUEST_DATA);
    unsigned long transfer_duration = micros() - transfer_start;
    
    // Sample SCK after transfer
    bool sck_samples_after[5];
    for (int i = 0; i < 5; i++) {
        sck_samples_after[i] = digitalRead(HSPI_SCK);
        delayMicroseconds(2);
    }
    Serial.print("[MASTER] SCK samples after transfer: ");
    for (int i = 0; i < 5; i++) {
        Serial.print(sck_samples_after[i] == LOW ? "L" : "H");
    }
    Serial.print(" | Transfer duration: ");
    Serial.print(transfer_duration);
    Serial.println(" us");
    
    Serial.print("[MASTER] After first transfer - MOSI: ");
    Serial.print(digitalRead(HSPI_MOSI) == LOW ? "LOW" : "HIGH");
    Serial.print(", SCK: ");
    Serial.print(digitalRead(HSPI_SCK) == LOW ? "LOW" : "HIGH");
    Serial.print(", MISO: ");
    Serial.print(digitalRead(HSPI_MISO) == LOW ? "LOW" : "HIGH");
    Serial.print(" | Received: 0x");
    Serial.println(cmd_response, HEX);
    
    // Small delay to ensure slave processes command
    delayMicroseconds(10);
    
    // Receive 16 bytes of sensor data
    Serial.println("[MASTER] Receiving 16 bytes of data...");
    for (int i = 0; i < 16; i++) {
        unsigned long byte_start = micros();
        buffer[i] = hspi.transfer(0x00);
        unsigned long byte_duration = micros() - byte_start;
        if (i < 4) {  // Debug first 4 bytes
            Serial.print("[MASTER] Byte ");
            Serial.print(i);
            Serial.print(": 0x");
            if (buffer[i] < 0x10) Serial.print("0");
            Serial.print(buffer[i], HEX);
            Serial.print(" (");
            Serial.print(byte_duration);
            Serial.println(" us)");
        }
    }
    
    // End transaction before releasing CS
    Serial.println("[MASTER] Ending SPI transaction...");
    hspi.endTransaction();
    
    Serial.print("[MASTER] After endTransaction - MOSI: ");
    Serial.print(digitalRead(HSPI_MOSI) == LOW ? "LOW" : "HIGH");
    Serial.print(", SCK: ");
    Serial.print(digitalRead(HSPI_SCK) == LOW ? "LOW" : "HIGH");
    Serial.print(", MISO: ");
    Serial.println(digitalRead(HSPI_MISO) == LOW ? "LOW" : "HIGH");
    
    // Small delay before releasing CS
    delayMicroseconds(10);
    
    // Release CS (pull HIGH)
    Serial.println("[MASTER] Releasing CS (pulling HIGH)...");
    digitalWrite(SPI_CS_PIN, HIGH);
    
    // Give slave time to process CS going HIGH
    delayMicroseconds(50);
    
    Serial.print("[MASTER] Final pin states - CS: ");
    Serial.print(digitalRead(SPI_CS_PIN) == LOW ? "LOW" : "HIGH");
    Serial.print(", MISO: ");
    Serial.println(digitalRead(HSPI_MISO) == LOW ? "LOW" : "HIGH");
    
    Serial.print("[MASTER] Command response: 0x");
    Serial.println(cmd_response, HEX);
    static unsigned long last_debug = 0;
    bool should_debug = (millis() - last_debug > 2000);
    if (should_debug) {
        Serial.print("[SPI DEBUG] Raw bytes received: ");
        for (int i = 0; i < 16; i++) {
            if (buffer[i] < 0x10) Serial.print("0");
            Serial.print(buffer[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
        last_debug = millis();
    }
    bool all_zeros = true;
    bool all_ff = true;
    for (int i = 0; i < 16; i++) {
        if (buffer[i] != 0x00) all_zeros = false;
        if (buffer[i] != 0xFF) all_ff = false;
    }
    if (all_zeros) {
        if (should_debug) Serial.println("[SPI DEBUG] Error: Received all zeros (no data)");
        return false;
    }
    if (all_ff) {
        if (should_debug) Serial.println("[SPI DEBUG] Error: Received all 0xFF (floating/high-Z)");
        return false;
    }
    memcpy(&data->oil_temp, &buffer[0], sizeof(float));
    memcpy(&data->motor_temp, &buffer[4], sizeof(float));
    memcpy(&data->vibration, &buffer[8], sizeof(float));
    memcpy(&data->current_draw, &buffer[12], sizeof(float));
    if (should_debug) {
        Serial.print("[SPI DEBUG] Parsed values - Oil: "); Serial.print(data->oil_temp); Serial.print("°F, Motor: ");
        Serial.print(data->motor_temp); Serial.print("°F, Vib: ");
        Serial.print(data->vibration); Serial.print("g, Current: ");
        Serial.print(data->current_draw); Serial.println("A");
    }
    if (isnan(data->oil_temp) || isnan(data->motor_temp) || isnan(data->vibration) || isnan(data->current_draw)) {
        if (should_debug) Serial.println("[SPI DEBUG] Error: NaN detected in parsed values");
        return false;
    }
    if (data->oil_temp < -50.0 || data->oil_temp > 500.0 ||
        data->motor_temp < -50.0 || data->motor_temp > 500.0 ||
        data->vibration < 0.0 || data->vibration > 100.0 ||
        data->current_draw < 0.0 || data->current_draw > 100.0) {
        if (should_debug) Serial.println("[SPI DEBUG] Error: Values out of reasonable range");
        return false;
    }
    return true;
}

// Update display colors based on alarm state
void updateDisplayColors(bool alarm) {
    lv_color_t text_color;
    lv_color_t bg_color;
    
    if (alarm && !alarm_acknowledged) {
        // Alarm mode: Red background, white text
        bg_color = LV_COLOR_MAKE(255, 0, 0);  // Red
        text_color = LV_COLOR_MAKE(255, 255, 255);  // White
    } else {
        // Normal mode: White background, black text
        bg_color = LV_COLOR_MAKE(255, 255, 255);  // White
        text_color = LV_COLOR_MAKE(0, 0, 0);  // Black
    }
    
    // Update screen background
    lv_obj_set_style_bg_color(lv_scr_act(), bg_color, 0);
    
    // Update text colors on all labels (both field names and values)
    lv_obj_set_style_text_color(oil_temp_label, text_color, 0);
    lv_obj_set_style_text_color(oil_temp_value_label, text_color, 0);
    lv_obj_set_style_text_color(motor_temp_label, text_color, 0);
    lv_obj_set_style_text_color(motor_temp_value_label, text_color, 0);
    lv_obj_set_style_text_color(vibration_label, text_color, 0);
    lv_obj_set_style_text_color(vibration_value_label, text_color, 0);
    lv_obj_set_style_text_color(current_draw_label, text_color, 0);
    lv_obj_set_style_text_color(current_draw_value_label, text_color, 0);
    
    // Invalidate to force redraw
    lv_obj_invalidate(lv_scr_act());
}

// Update display with sensor data
void updateDisplay(const SensorData& data) {
    char temp_str[16];
    
    // Update Oil Temp
    snprintf(temp_str, sizeof(temp_str), "%.1f°F", data.oil_temp);
    lv_label_set_text(oil_temp_value_label, temp_str);
    
    // Update Motor Temp
    snprintf(temp_str, sizeof(temp_str), "%.1f°F", data.motor_temp);
    lv_label_set_text(motor_temp_value_label, temp_str);
    
    // Update Vibration
    snprintf(temp_str, sizeof(temp_str), "%.2f g", data.vibration);
    lv_label_set_text(vibration_value_label, temp_str);
    
    // Update Current Draw
    snprintf(temp_str, sizeof(temp_str), "%.2f A", data.current_draw);
    lv_label_set_text(current_draw_value_label, temp_str);
}

// Check if sensor values are outside safe ranges (alarm condition)
bool checkAlarmConditions(const SensorData& data) {
    return (data.oil_temp < TEMP_MIN || 
            data.motor_temp < TEMP_MIN ||
            data.vibration > VIBRATION_MAX ||
            data.current_draw > CURRENT_MAX);
}

// Log sensor data to SD card
void logToSDCard(const SensorData& data, bool is_alarm) {
    if (!sd_card_initialized) {
        return;  // SD card not available
    }
    
    File dataFile = SD.open("/sensor_log.csv", FILE_WRITE);
    if (dataFile) {
        // CSV format: timestamp, oil_temp, motor_temp, vibration, current_draw, alarm_flag
        unsigned long timestamp = millis() / 1000;  // Seconds since boot
        
        dataFile.print(timestamp);
        dataFile.print(",");
        dataFile.print(data.oil_temp, 2);
        dataFile.print(",");
        dataFile.print(data.motor_temp, 2);
        dataFile.print(",");
        dataFile.print(data.vibration, 2);
        dataFile.print(",");
        dataFile.print(data.current_draw, 2);
        dataFile.print(",");
        dataFile.println(is_alarm ? "ALARM" : "NORMAL");
        
        dataFile.close();
        
        Serial.print("Logged to SD: ");
        if (is_alarm) {
            Serial.print("[ALARM] ");
        }
        Serial.print("Oil: ");
        Serial.print(data.oil_temp);
        Serial.print("°F, Motor: ");
        Serial.print(data.motor_temp);
        Serial.print("°F, Vib: ");
        Serial.print(data.vibration);
        Serial.print("g, Current: ");
        Serial.print(data.current_draw);
        Serial.println("A");
    } else {
        Serial.println("Error opening sensor_log.csv");
    }
}

// Log alarm acknowledgment event to SD card
void logAcknowledgmentToSD() {
    if (!sd_card_initialized) {
        return;  // SD card not available
    }
    
    File dataFile = SD.open("/sensor_log.csv", FILE_WRITE);
    if (dataFile) {
        unsigned long timestamp = millis() / 1000;  // Seconds since boot
        
        // Log acknowledgment event with empty sensor values and ACKNOWLEDGED status
        dataFile.print(timestamp);
        dataFile.print(",,,,");  // Empty sensor values (oil_temp, motor_temp, vibration, current_draw)
        dataFile.println("ACKNOWLEDGED");
        
        dataFile.close();
        
        Serial.println("Logged alarm acknowledgment to SD card");
    } else {
        Serial.println("Error opening sensor_log.csv for acknowledgment log");
    }
}

void setup() {
    Serial.begin(9600);
    delay(1000);
    Serial.println("ESP32 LVGL Project Starting...");
    
    // Initialize LVGL
    lv_init();
    
    // Allocate display buffers
    buf1 = (lv_color_t *)heap_caps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    buf2 = (lv_color_t *)heap_caps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    
    if (buf1 == NULL || buf2 == NULL) {
        Serial.println("ERROR: Failed to allocate display buffers!");
        return;
    }
    
    // Initialize TFT display
    tft.init();
    // Note: Rotation is also set in User_Setup.h via TFT_ROTATION
    // Make sure they match, or setRotation() will override User_Setup.h
    tft.fillScreen(TFT_BLACK);
    
    Serial.println("TFT display initialized");
    
    // Initialize HSPI Master for slave communication (separate from display's VSPI)
    pinMode(SPI_CS_PIN, OUTPUT);
    digitalWrite(SPI_CS_PIN, HIGH);  // CS high = slave not selected
    
    // Configure HSPI pins explicitly before begin()
    pinMode(HSPI_MOSI, OUTPUT);
    pinMode(HSPI_SCK, OUTPUT);
    pinMode(HSPI_MISO, INPUT_PULLUP);
    
    // Set initial states
    digitalWrite(HSPI_MOSI, LOW);
    digitalWrite(HSPI_SCK, LOW);
    
    // CRITICAL: CS pin should NOT be passed to begin() for master mode
    // CS must be controlled manually via digitalWrite()
    hspi.begin(HSPI_SCK, HSPI_MISO, HSPI_MOSI, -1);  // Initialize HSPI bus (CS=-1 means manual control)
    
    Serial.println("HSPI Master initialized");
    
    // Verify pin states after initialization
    Serial.print("[MASTER INIT] Pin states - MOSI: ");
    Serial.print(digitalRead(HSPI_MOSI) == LOW ? "LOW" : "HIGH");
    Serial.print(", SCK: ");
    Serial.print(digitalRead(HSPI_SCK) == LOW ? "LOW" : "HIGH");
    Serial.print(", MISO: ");
    Serial.print(digitalRead(HSPI_MISO) == LOW ? "LOW" : "HIGH");
    Serial.print(", CS: ");
    Serial.println(digitalRead(SPI_CS_PIN) == LOW ? "LOW" : "HIGH");
    Serial.print("HSPI Pins - MOSI: ");
    Serial.print(HSPI_MOSI);
    Serial.print(", MISO: ");
    Serial.print(HSPI_MISO);
    Serial.print(", SCK: ");
    Serial.print(HSPI_SCK);
    Serial.print(", Slave CS: ");
    Serial.print(SPI_CS_PIN);
    Serial.print(", SD CS: ");
    Serial.println(SD_CS_PIN);
    Serial.println("Display uses VSPI (MOSI=23, MISO=19, SCK=18)");
    
    // Initialize SD card on HSPI bus
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);  // CS high = SD card not selected
    
    // Initialize SD card with HSPI
    // Note: ESP32 SD library uses default SPI, we may need to configure HSPI as default
    // or use a library that supports custom SPI. For now, try standard begin.
    // If this doesn't work, you may need to use SdFat library instead.
    if (SD.begin(SD_CS_PIN)) {
        sd_card_initialized = true;
        Serial.println("SD card initialized successfully");
        
        // Create CSV header if file doesn't exist
        File dataFile = SD.open("/sensor_log.csv", FILE_READ);
        if (!dataFile) {
            // File doesn't exist, create it with header
            dataFile = SD.open("/sensor_log.csv", FILE_WRITE);
            if (dataFile) {
                dataFile.println("timestamp,oil_temp,motor_temp,vibration,current_draw,status");
                dataFile.close();
                Serial.println("Created sensor_log.csv with header");
            }
        } else {
            dataFile.close();
        }
    } else {
        sd_card_initialized = false;
        Serial.println("SD card initialization failed - logging disabled");
    }
    
    // Initialize display buffer for LVGL 8.3.11
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LVGL_BUFFER_SIZE);
    
    // Initialize and register display driver for LVGL 8.3.11
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = display_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    // Initialize touch input device for LVGL 8.3.11
    // Note: Touch is disabled by default - enable in touch_read() when hardware is configured
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);
    
    // LV_TICK_CUSTOM is enabled, so LVGL uses millis() directly - no timer needed
    
    // Create monitoring screen
    create_monitoring_screen();
    
    // Force a refresh to ensure the screen is displayed
    lv_refr_now(NULL);
    
    Serial.println("LVGL initialized successfully!");
}

void loop() {
    // Handle LVGL tasks (call this as frequently as possible)
    // LVGL 8.3.11 uses lv_task_handler(), not lv_timer_handler() (that's LVGL 9+)
    lv_task_handler();
    
    // Poll SPI slave every second
    unsigned long current_time = millis();
    if (current_time - last_poll_time >= SPI_POLL_INTERVAL) {
        last_poll_time = current_time;
        
        SensorData sensor_data;
        // Use bit-bang SPI to test connections
        if (requestSensorDataBitBang(&sensor_data)) {
            // Check for alarm conditions
            bool alarm = checkAlarmConditions(sensor_data);
            
            // Update alarm state
            if (alarm && !alarm_active) {
                // New alarm detected - reset acknowledgment
                alarm_active = true;
                alarm_acknowledged = false;
                Serial.println("*** ALARM CONDITION DETECTED ***");
            } else if (!alarm && alarm_active && alarm_acknowledged) {
                // Alarm cleared and acknowledged - return to normal
                alarm_active = false;
                alarm_acknowledged = false;
                Serial.println("Alarm cleared - returning to normal display");
            }
            
            // Update display colors based on alarm state
            // Screen stays red until touch acknowledges it, even if condition clears
            updateDisplayColors(alarm_active && !alarm_acknowledged);
            
            // Update display with received data
            updateDisplay(sensor_data);
            
            // Log to SD card if:
            // 1. Alarm condition detected, OR
            // 2. 15 minutes have passed since last log
            bool should_log = false;
            if (alarm) {
                should_log = true;
            } else if (current_time - last_sd_log_time >= SD_LOG_INTERVAL) {
                should_log = true;
            }
            
            if (should_log) {
                logToSDCard(sensor_data, alarm);
                last_sd_log_time = current_time;
            }
            
            // Optional: Print to serial for debugging
            Serial.print("Oil Temp: ");
            Serial.print(sensor_data.oil_temp);
            Serial.print("°F, Motor Temp: ");
            Serial.print(sensor_data.motor_temp);
            Serial.print("°F, Vibration: ");
            Serial.print(sensor_data.vibration);
            Serial.print("g, Current: ");
            Serial.print(sensor_data.current_draw);
            Serial.println("A");
        } else {
            Serial.println("Failed to receive valid sensor data");
            // Display error or keep previous values
        }
    }
    
    // Small delay to prevent watchdog issues
    delay(5);
}
