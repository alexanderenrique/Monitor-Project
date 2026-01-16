#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <Wire.h>
#include <RTClib.h>
#include <cstring>
#include "touch_mapping.h"

// LVGL display buffer size
#define LVGL_BUFFER_SIZE 10 * 1024

// Display dimensions (adjust based on your display)
// Common sizes: 240x320 (ILI9341), 240x240 (ST7789), 320x240, etc.
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

// UART Configuration for slave communication (half-duplex: master only receives)
#define UART_RX_PIN 3        // GPIO 3 - RX pin for receiving data from slave
#define UART_BAUD 9600       // UART baud rate
// SD card VSPI pin configuration (shares SPI bus with TFT display)
// Using default SPI instance (VSPI) that TFT_eSPI already initialized
#define SD_CS_PIN 33         // Chip Select pin for SD card
#define SD_SPI_FREQ 10000000 // SD card SPI frequency: 10MHz (conservative for GPIO matrix pins)
#define SD_LOG_INTERVAL 30000  // Log to SD card every 30 seconds (30000ms)

// I2C Configuration for DS3231 RTC
#define I2C_SDA_PIN 21       // GPIO 21 - SDA pin for I2C
#define I2C_SCL_PIN 22       // GPIO 22 - SCL pin for I2C

// Alarm thresholds
#define TEMP_MIN 30.0       // Minimum temperature (°F) - alarm if below
#define VIBRATION_MAX 10.0  // Maximum vibration (g) - alarm if above
#define CURRENT_MAX 5.0     // Maximum current (A) - alarm if above

// UART instance for slave communication (Serial2 = UART2, RX only)
HardwareSerial SlaveUART(2);

// RTC instance
RTC_DS3231 rtc;

// Using default SPI instance (VSPI) that TFT_eSPI already initialized
// No need to create separate SPI instance - TFT_eSPI handles VSPI initialization

// SD card logging timer
unsigned long last_sd_log_time = 0;
bool sd_card_initialized = false;

// Alarm state
bool alarm_active = false;
bool alarm_acknowledged = false;

// RTC time set flag
bool rtc_time_set = false;

// Function forward declarations
void logAcknowledgmentToSD();
void updateTimeDisplay();
void setRTCTime(int year, int month, int day, int hour, int minute, int second);
void checkSerialCommands();
bool isRTCTimeSet();
void create_time_entry_screen();
void show_monitoring_screen();
static void time_entry_btn_event_handler(lv_event_t * e);
static void arrow_btn_event_handler(lv_event_t * e);
void update_time_entry_display();
void setRTCFromCompileTime();

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
        // TFT_eSPI getTouch() should handle rotation automatically, but we may need to
        // apply additional calibration based on your specific touch controller
        
        // Clamp coordinates to display bounds to prevent LVGL warnings
        if (x >= DISPLAY_WIDTH) x = DISPLAY_WIDTH - 1;
        if (y >= DISPLAY_HEIGHT) y = DISPLAY_HEIGHT - 1;
        
        // Store raw coordinates for debugging
        uint16_t raw_x = x;
        uint16_t raw_y = y;
        
        // Apply coordinate transformation if needed
        // With TFT_ROTATION 0 (portrait), coordinates should already be correct
        // But some touch controllers need manual transformation
        
        // If touch coordinates seem swapped or inverted, uncomment and adjust:
        // For portrait mode, if X and Y are swapped:
        // uint16_t temp = x;
        // x = y;
        // y = temp;
        // If Y axis is inverted:
        // y = DISPLAY_HEIGHT - y;
        
        // COMMENTED OUT: Touch mapping for time entry screen disabled
        // Try to map touch to button first (for time entry screen)
        // if (map_touch_to_button(x, y)) {
        //     // Touch was mapped to a button, but we still need to provide valid data to LVGL
        //     // Use a coordinate that won't interfere (e.g., off-screen or neutral position)
        //     data->point.x = 0;
        //     data->point.y = 0;
        //     data->state = LV_INDEV_STATE_RELEASED;  // Set to released so LVGL doesn't process it
        //     return;
        // }
        
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
        
        // Debug output - show both raw and processed coordinates
        Serial.print("[TOUCH] Raw: (");
        Serial.print(raw_x);
        Serial.print(", ");
        Serial.print(raw_y);
        Serial.print(") -> LVGL: (");
        Serial.print(x);
        Serial.print(", ");
        Serial.print(y);
        Serial.print(")");
        
        // Check if touch is near any button areas (for debugging)
        // Expanded ranges to account for coordinate offset
        // Year buttons: x=55-90, y=62-127 (UP at 60-85/67-92, DOWN at 60-85/97-122)
        // Month buttons: x=130-165, y=62-127
        // Day buttons: x=205-240, y=62-127
        // Hour buttons: x=55-90, y=132-197
        // Minute buttons: x=130-165, y=132-197
        // Second buttons: x=205-240, y=132-197
        // Set Time button: x=15-225, y=180-230
        
        bool near_button = false;
        if ((x >= 55 && x <= 90) || (x >= 130 && x <= 165) || (x >= 205 && x <= 240)) {
            if ((y >= 62 && y <= 127) || (y >= 132 && y <= 197)) {
                near_button = true;
                Serial.print(" [NEAR ARROW BUTTON]");
            }
        }
        if (x >= 15 && x <= 225 && y >= 180 && y <= 230) {
            near_button = true;
            Serial.print(" [NEAR SET TIME BUTTON]");
        }
        if (!near_button) {
            Serial.print(" [NOT NEAR BUTTON]");
        }
        Serial.println();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = 0;
        data->point.y = 0;
    }
}

// Global labels for values (so we can update them in loop())
static lv_obj_t *time_label;
static lv_obj_t *oil_temp_label;
static lv_obj_t *oil_temp_value_label;
static lv_obj_t *motor_temp_label;
static lv_obj_t *motor_temp_value_label;
static lv_obj_t *vibration_label;
static lv_obj_t *vibration_value_label;
static lv_obj_t *current_draw_label;
static lv_obj_t *current_draw_value_label;

// Time entry screen objects
lv_obj_t *time_entry_screen;  // Made non-static so touch_mapping.cpp can access it
static lv_obj_t *year_label;
static lv_obj_t *month_label;
static lv_obj_t *day_label;
static lv_obj_t *hour_label;
static lv_obj_t *minute_label;
static lv_obj_t *second_label;
lv_obj_t *set_time_btn;  // Made non-static so touch_mapping.cpp can access it

// Arrow buttons for each field
// Made non-static so touch_mapping.cpp can access them
lv_obj_t *year_up_btn, *year_down_btn;
lv_obj_t *month_up_btn, *month_down_btn;
lv_obj_t *day_up_btn, *day_down_btn;
lv_obj_t *hour_up_btn, *hour_down_btn;
lv_obj_t *minute_up_btn, *minute_down_btn;
lv_obj_t *second_up_btn, *second_down_btn;

// Current time values
// Note: current_year stores 2-digit year (0-99), will be converted to 20xx when setting RTC
static int current_year = 24;  // 2-digit year (24 = 2024)
static int current_month = 12;
static int current_day = 15;
static int current_hour = 12;
static int current_minute = 0;
static int current_second = 0;

// Sensor data structure (matches what slave will send)
struct SensorData {
    float oil_temp;      // Oil temperature in Fahrenheit
    float motor_temp;    // Motor temperature in Fahrenheit
    float vibration;     // Vibration in g
    float current_draw;  // Current draw in Amperes
};

// No polling needed - slave sends data periodically

// Global styles (so we can change them for alarm mode)
static lv_style_t label_style;
static lv_style_t value_style;
static lv_style_t input_style;
static lv_style_t header_style;

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
    
    // Date and time display at the top
    time_label = lv_label_create(lv_scr_act());
    lv_label_set_text(time_label, "--/--/----\n--:--:--");
    lv_obj_add_style(time_label, &value_style, 0);
    lv_obj_set_pos(time_label, 20, 5);
    
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

// Helper function to create up/down arrow buttons
static void create_arrow_buttons(lv_obj_t *parent, lv_obj_t **up_btn, lv_obj_t **down_btn, 
                                  int x, int y, int btn_size) {
    // Up arrow button
    *up_btn = lv_btn_create(parent);
    lv_obj_set_size(*up_btn, btn_size, btn_size);
    lv_obj_set_pos(*up_btn, x, y);
    
    // Buttons are clickable by default in LVGL - add event callbacks
    // Try multiple event types to see what works
    lv_obj_add_event_cb(*up_btn, arrow_btn_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(*up_btn, arrow_btn_event_handler, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(*up_btn, arrow_btn_event_handler, LV_EVENT_RELEASED, NULL);
    
    lv_obj_t *up_label = lv_label_create(*up_btn);
    lv_label_set_text(up_label, LV_SYMBOL_UP);
    lv_obj_center(up_label);
    
    Serial.print("[DEBUG] Created UP button at (");
    Serial.print(x);
    Serial.print(", ");
    Serial.print(y);
    Serial.print("), size: ");
    Serial.print(btn_size);
    Serial.print(", covers area: x=");
    Serial.print(x);
    Serial.print("-");
    Serial.print(x + btn_size);
    Serial.print(", y=");
    Serial.print(y);
    Serial.print("-");
    Serial.print(y + btn_size);
    Serial.print(", pointer: 0x");
    Serial.print((uint32_t)*up_btn, HEX);
    Serial.print(", parent: 0x");
    Serial.println((uint32_t)parent, HEX);
    
    // Down arrow button - increase spacing to avoid overlap
    int down_y = y + btn_size + 10;  // Increased from 5 to 10 pixels spacing
    *down_btn = lv_btn_create(parent);
    lv_obj_set_size(*down_btn, btn_size, btn_size);
    lv_obj_set_pos(*down_btn, x, down_y);
    
    // Buttons are clickable by default in LVGL - add event callbacks
    // Try multiple event types to see what works
    lv_obj_add_event_cb(*down_btn, arrow_btn_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(*down_btn, arrow_btn_event_handler, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(*down_btn, arrow_btn_event_handler, LV_EVENT_RELEASED, NULL);
    
    lv_obj_t *down_label = lv_label_create(*down_btn);
    lv_label_set_text(down_label, LV_SYMBOL_DOWN);
    lv_obj_center(down_label);
    
    Serial.print("[DEBUG] Created DOWN button at (");
    Serial.print(x);
    Serial.print(", ");
    Serial.print(down_y);
    Serial.print("), size: ");
    Serial.print(btn_size);
    Serial.print(", covers area: x=");
    Serial.print(x);
    Serial.print("-");
    Serial.print(x + btn_size);
    Serial.print(", y=");
    Serial.print(down_y);
    Serial.print("-");
    Serial.print(down_y + btn_size);
    Serial.print(", pointer: 0x");
    Serial.print((uint32_t)*down_btn, HEX);
    Serial.print(", parent: 0x");
    Serial.println((uint32_t)parent, HEX);
}

// Update time entry display labels
void update_time_entry_display() {
    char buf[16];
    
    Serial.print("[DEBUG] update_time_entry_display() - Current values: ");
    Serial.print(current_year);
    Serial.print("/");
    Serial.print(current_month);
    Serial.print("/");
    Serial.print(current_day);
    Serial.print(" ");
    Serial.print(current_hour);
    Serial.print(":");
    Serial.print(current_minute);
    Serial.print(":");
    Serial.println(current_second);
    
    // Display year as 2 digits (last 2 digits of year)
    int year_2digit = current_year % 100;
    snprintf(buf, sizeof(buf), "%02d", year_2digit);
    lv_label_set_text(year_label, buf);
    Serial.print("[DEBUG] Year label updated to: ");
    Serial.print(buf);
    Serial.print(" (full year: ");
    Serial.print(current_year);
    Serial.println(")");
    
    snprintf(buf, sizeof(buf), "%02d", current_month);
    lv_label_set_text(month_label, buf);
    Serial.print("[DEBUG] Month label updated to: ");
    Serial.println(buf);
    
    snprintf(buf, sizeof(buf), "%02d", current_day);
    lv_label_set_text(day_label, buf);
    Serial.print("[DEBUG] Day label updated to: ");
    Serial.println(buf);
    
    snprintf(buf, sizeof(buf), "%02d", current_hour);
    lv_label_set_text(hour_label, buf);
    Serial.print("[DEBUG] Hour label updated to: ");
    Serial.println(buf);
    
    snprintf(buf, sizeof(buf), "%02d", current_minute);
    lv_label_set_text(minute_label, buf);
    Serial.print("[DEBUG] Minute label updated to: ");
    Serial.println(buf);
    
    snprintf(buf, sizeof(buf), "%02d", current_second);
    lv_label_set_text(second_label, buf);
    Serial.print("[DEBUG] Second label updated to: ");
    Serial.println(buf);
    
    Serial.println("[DEBUG] All labels updated, forcing refresh");
    lv_refr_now(NULL);
}

// Create time entry screen
void create_time_entry_screen() {
    // Initialize current values from RTC if available, otherwise use defaults
    if (rtc.begin()) {
        DateTime now = rtc.now();
        // Convert 4-digit year to 2-digit for display (2024 -> 24)
        current_year = now.year() % 100;
        current_month = now.month();
        current_day = now.day();
        current_hour = now.hour();
        current_minute = now.minute();
        current_second = now.second();
    }
    
    // Create a new screen for time entry
    // Use lv_obj_create(NULL) to create a screen object
    time_entry_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(time_entry_screen, LV_COLOR_MAKE(255, 255, 255), 0);
    
    Serial.print("[DEBUG] Created time_entry_screen, pointer: 0x");
    Serial.println((uint32_t)time_entry_screen, HEX);
    
    // Create styles
    lv_style_init(&label_style);
    lv_style_set_text_color(&label_style, LV_COLOR_MAKE(0, 0, 0));
    lv_style_set_text_font(&label_style, &lv_font_montserrat_14);
    
    lv_style_init(&value_style);
    lv_style_set_text_color(&value_style, LV_COLOR_MAKE(0, 0, 0));
    lv_style_set_text_font(&value_style, &lv_font_montserrat_24);
    
    // Header style for title (size 24)
    lv_style_init(&header_style);
    lv_style_set_text_color(&header_style, LV_COLOR_MAKE(0, 0, 0));
    lv_style_set_text_font(&header_style, &lv_font_montserrat_24);
    
    // Title
    lv_obj_t *title_label = lv_label_create(time_entry_screen);
    lv_label_set_text(title_label, "Set Current Time");
    lv_obj_add_style(title_label, &header_style, 0);
    lv_obj_set_pos(title_label, 20, 10);
    
    // Layout constants - optimized for 240px wide display
    int start_y = 45;
    int btn_size = 25;           // Smaller buttons to fit better
    int value_width = 45;         // Width for value display
    int spacing_x = 75;           // Horizontal spacing between columns (75px per column)
    int spacing_y = 70;           // Vertical spacing between rows
    int value_y_offset = 22;      // Vertical offset for value below label
    int btn_x_offset = 5;        // Horizontal offset for buttons from value
    
    // Explicit button coordinates - manually set for precise touch alignment
    // Based on Year UP working at touch (57, 80), button positioned at (56, 67)
    // Adjust these values based on actual touch coordinates when testing each button
    
    int col1_x = 10;
    int col2_x = 85;   // 10 + 75 spacing
    int col3_x = 160;  // 10 + 150 spacing
    
    // Year field (Column 1, Row 1) - WORKING at x=56, y=67
    lv_obj_t *year_field_label = lv_label_create(time_entry_screen);
    lv_label_set_text(year_field_label, "Year:");
    lv_obj_add_style(year_field_label, &label_style, 0);
    lv_obj_set_pos(year_field_label, col1_x, start_y);
    
    year_label = lv_label_create(time_entry_screen);
    lv_obj_add_style(year_label, &value_style, 0);
    lv_obj_set_pos(year_label, col1_x, start_y + value_y_offset);
    
    // Year buttons: Visual positions (buttons stay where they look good)
    // Touch coordinates will be mapped manually in touch_read()
    create_arrow_buttons(time_entry_screen, &year_up_btn, &year_down_btn, 
                         col1_x + btn_x_offset, start_y + value_y_offset + btn_size, btn_size);
    
    // Month field (Column 2, Row 1) - NEEDS CALIBRATION
    lv_obj_t *month_field_label = lv_label_create(time_entry_screen);
    lv_label_set_text(month_field_label, "Month:");
    lv_obj_add_style(month_field_label, &label_style, 0);
    lv_obj_set_pos(month_field_label, col2_x, start_y);
    
    month_label = lv_label_create(time_entry_screen);
    lv_obj_add_style(month_label, &value_style, 0);
    lv_obj_set_pos(month_label, col2_x, start_y + value_y_offset);
    
    // Month buttons: Back to original position
    create_arrow_buttons(time_entry_screen, &month_up_btn, &month_down_btn, 
                         131, 67, btn_size);
    
    // Day field (Column 3, Row 1) - NEEDS CALIBRATION
    lv_obj_t *day_field_label = lv_label_create(time_entry_screen);
    lv_label_set_text(day_field_label, "Day:");
    lv_obj_add_style(day_field_label, &label_style, 0);
    lv_obj_set_pos(day_field_label, col3_x, start_y);
    
    day_label = lv_label_create(time_entry_screen);
    lv_obj_add_style(day_label, &value_style, 0);
    lv_obj_set_pos(day_label, col3_x, start_y + value_y_offset);
    
    // Day buttons: Back to original position
    create_arrow_buttons(time_entry_screen, &day_up_btn, &day_down_btn, 
                         206, 67, btn_size);
    
    // Hour field (Column 1, Row 2) - Moved down 20 pixels for better spacing
    lv_obj_t *hour_field_label = lv_label_create(time_entry_screen);
    lv_label_set_text(hour_field_label, "Hour:");
    lv_obj_add_style(hour_field_label, &label_style, 0);
    lv_obj_set_pos(hour_field_label, col1_x, start_y + spacing_y + 20);
    
    hour_label = lv_label_create(time_entry_screen);
    lv_obj_add_style(hour_label, &value_style, 0);
    lv_obj_set_pos(hour_label, col1_x, start_y + spacing_y + 20 + value_y_offset);
    
    // Hour buttons: Moved down 20 pixels
    create_arrow_buttons(time_entry_screen, &hour_up_btn, &hour_down_btn, 
                         56, start_y + spacing_y + 20 + value_y_offset, btn_size);
    
    // Minute field (Column 2, Row 2) - Moved down 20 pixels for better spacing
    lv_obj_t *minute_field_label = lv_label_create(time_entry_screen);
    lv_label_set_text(minute_field_label, "Minute:");
    lv_obj_add_style(minute_field_label, &label_style, 0);
    lv_obj_set_pos(minute_field_label, col2_x, start_y + spacing_y + 20);
    
    minute_label = lv_label_create(time_entry_screen);
    lv_obj_add_style(minute_label, &value_style, 0);
    lv_obj_set_pos(minute_label, col2_x, start_y + spacing_y + 20 + value_y_offset);
    
    // Minute buttons: Moved down 20 pixels
    create_arrow_buttons(time_entry_screen, &minute_up_btn, &minute_down_btn, 
                         131, start_y + spacing_y + 20 + value_y_offset, btn_size);
    
    // Second field (Column 3, Row 2) - Moved down 20 pixels for better spacing
    lv_obj_t *second_field_label = lv_label_create(time_entry_screen);
    lv_label_set_text(second_field_label, "Second:");
    lv_obj_add_style(second_field_label, &label_style, 0);
    lv_obj_set_pos(second_field_label, col3_x, start_y + spacing_y + 20);
    
    second_label = lv_label_create(time_entry_screen);
    lv_obj_add_style(second_label, &value_style, 0);
    lv_obj_set_pos(second_label, col3_x, start_y + spacing_y + 20 + value_y_offset);
    
    // Second buttons: Moved down 20 pixels
    create_arrow_buttons(time_entry_screen, &second_up_btn, &second_down_btn, 
                         206, start_y + spacing_y + 20 + value_y_offset, btn_size);
    
    // Set Time button (centered at bottom of screen with 5px buffer)
    set_time_btn = lv_btn_create(time_entry_screen);
    lv_obj_set_size(set_time_btn, 200, 40);
    // Position at bottom: screen height (320) - button height (40) - buffer (5) = 275
    lv_obj_set_pos(set_time_btn, 20, DISPLAY_HEIGHT - 40 - 5);
    lv_obj_add_event_cb(set_time_btn, time_entry_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_label = lv_label_create(set_time_btn);
    lv_label_set_text(btn_label, "Set Time");
    lv_obj_center(btn_label);
    
    // Initialize display
    update_time_entry_display();
}

// Arrow button event handler for adjusting time values
static void arrow_btn_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    
    Serial.print("[DEBUG] Arrow button event - Code: ");
    Serial.print(code);
    Serial.print(" (CLICKED=");
    Serial.print(LV_EVENT_CLICKED);
    Serial.print(", PRESSED=");
    Serial.print(LV_EVENT_PRESSED);
    Serial.print(", RELEASED=");
    Serial.print(LV_EVENT_RELEASED);
    Serial.print("), Button pointer: 0x");
    Serial.println((uint32_t)btn, HEX);
    
    if(code == LV_EVENT_CLICKED) {
        Serial.println("[DEBUG] LV_EVENT_CLICKED detected!");
        
        bool increment = false;
        int *target_value = NULL;
        int min_val = 0, max_val = 0;
        const char *field_name = "UNKNOWN";
        
        // Determine which button was pressed and set limits
        Serial.print("[DEBUG] Comparing button pointer 0x");
        Serial.print((uint32_t)btn, HEX);
        Serial.print(" with Year UP: 0x");
        Serial.print((uint32_t)year_up_btn, HEX);
        Serial.print(", Year DOWN: 0x");
        Serial.println((uint32_t)year_down_btn, HEX);
        
        if (btn == year_up_btn) {
            increment = true;
            target_value = &current_year;
            min_val = 0;   // Display as 2-digit (00-99)
            max_val = 99;  // Display as 2-digit (00-99)
            field_name = "YEAR";
            Serial.println("[DEBUG] Year UP button pressed - MATCHED!");
        } else if (btn == year_down_btn) {
            increment = false;
            target_value = &current_year;
            min_val = 0;   // Display as 2-digit (00-99)
            max_val = 99;  // Display as 2-digit (00-99)
            field_name = "YEAR";
            Serial.println("[DEBUG] Year DOWN button pressed - MATCHED!");
        } else if (btn == month_up_btn) {
            increment = true;
            target_value = &current_month;
            min_val = 1;
            max_val = 12;
            field_name = "MONTH";
            Serial.println("[DEBUG] Month UP button pressed");
        } else if (btn == month_down_btn) {
            increment = false;
            target_value = &current_month;
            min_val = 1;
            max_val = 12;
            field_name = "MONTH";
            Serial.println("[DEBUG] Month DOWN button pressed");
        } else if (btn == day_up_btn) {
            increment = true;
            target_value = &current_day;
            min_val = 1;
            max_val = 31;  // Will validate against actual month later
            field_name = "DAY";
            Serial.println("[DEBUG] Day UP button pressed");
        } else if (btn == day_down_btn) {
            increment = false;
            target_value = &current_day;
            min_val = 1;
            max_val = 31;
            field_name = "DAY";
            Serial.println("[DEBUG] Day DOWN button pressed");
        } else if (btn == hour_up_btn) {
            increment = true;
            target_value = &current_hour;
            min_val = 0;
            max_val = 23;
            field_name = "HOUR";
            Serial.println("[DEBUG] Hour UP button pressed");
        } else if (btn == hour_down_btn) {
            increment = false;
            target_value = &current_hour;
            min_val = 0;
            max_val = 23;
            field_name = "HOUR";
            Serial.println("[DEBUG] Hour DOWN button pressed");
        } else if (btn == minute_up_btn) {
            increment = true;
            target_value = &current_minute;
            min_val = 0;
            max_val = 59;
            field_name = "MINUTE";
            Serial.println("[DEBUG] Minute UP button pressed");
        } else if (btn == minute_down_btn) {
            increment = false;
            target_value = &current_minute;
            min_val = 0;
            max_val = 59;
            field_name = "MINUTE";
            Serial.println("[DEBUG] Minute DOWN button pressed");
        } else if (btn == second_up_btn) {
            increment = true;
            target_value = &current_second;
            min_val = 0;
            max_val = 59;
            field_name = "SECOND";
            Serial.println("[DEBUG] Second UP button pressed");
        } else if (btn == second_down_btn) {
            increment = false;
            target_value = &current_second;
            min_val = 0;
            max_val = 59;
            field_name = "SECOND";
            Serial.println("[DEBUG] Second DOWN button pressed");
        } else {
            Serial.print("[DEBUG] WARNING: Unknown button pressed! Button pointer: 0x");
            Serial.println((uint32_t)btn, HEX);
            Serial.println("[DEBUG] Known button pointers:");
            Serial.print("  Year UP: 0x");
            Serial.print((uint32_t)year_up_btn, HEX);
            Serial.print(", Year DOWN: 0x");
            Serial.println((uint32_t)year_down_btn, HEX);
            Serial.print("  Month UP: 0x");
            Serial.print((uint32_t)month_up_btn, HEX);
            Serial.print(", Month DOWN: 0x");
            Serial.println((uint32_t)month_down_btn, HEX);
            Serial.print("  Day UP: 0x");
            Serial.print((uint32_t)day_up_btn, HEX);
            Serial.print(", Day DOWN: 0x");
            Serial.println((uint32_t)day_down_btn, HEX);
        }
        
        // Update the value
        if (target_value != NULL) {
            int old_value = *target_value;
            
            if (increment) {
                (*target_value)++;
                if (*target_value > max_val) {
                    *target_value = min_val;  // Wrap around
                }
            } else {
                (*target_value)--;
                if (*target_value < min_val) {
                    *target_value = max_val;  // Wrap around
                }
            }
            
            Serial.print("[DEBUG] ");
            Serial.print(field_name);
            Serial.print(" changed: ");
            Serial.print(old_value);
            Serial.print(" -> ");
            Serial.println(*target_value);
            
            // Validate day against month (simplified - doesn't account for leap years)
            if (target_value == &current_day) {
                int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                int max_day = days_in_month[current_month - 1];
                if (current_day > max_day) {
                    Serial.print("[DEBUG] Day adjusted for month - was ");
                    Serial.print(current_day);
                    Serial.print(", now ");
                    current_day = max_day;
                    Serial.println(current_day);
                }
                if (current_day < 1) {
                    current_day = 1;
                }
            }
            
            // Update display
            Serial.println("[DEBUG] Calling update_time_entry_display()");
            update_time_entry_display();
        } else {
            Serial.println("[DEBUG] ERROR: target_value is NULL!");
        }
    } else {
        Serial.print("[DEBUG] Event code is not CLICKED: ");
        Serial.println(code);
    }
}

// Button event handler for Set Time button
static void time_entry_btn_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    Serial.print("[DEBUG] Set Time button event - Code: ");
    Serial.println(code);
    
    if(code == LV_EVENT_CLICKED) {
        Serial.println("[DEBUG] Set Time button CLICKED!");
        
        // Use stored values
        // Convert 2-digit year to 4-digit (prepend "20")
        int year_2digit = current_year;
        int year_4digit = 2000 + year_2digit;  // Convert 24 -> 2024
        int month = current_month;
        int day = current_day;
        int hour = current_hour;
        int minute = current_minute;
        int second = current_second;
        
        Serial.print("[DEBUG] Attempting to set time: ");
        Serial.print(year_2digit);
        Serial.print(" (");
        Serial.print(year_4digit);
        Serial.print(") / ");
        Serial.print(month);
        Serial.print("/");
        Serial.print(day);
        Serial.print(" ");
        Serial.print(hour);
        Serial.print(":");
        Serial.print(minute);
        Serial.print(":");
        Serial.println(second);
        
        // Validate and set time
        // Year validation: 2-digit year should be 0-99 (becomes 2000-2099)
        if (year_2digit >= 0 && year_2digit <= 99 &&
            month >= 1 && month <= 12 &&
            day >= 1 && day <= 31 &&
            hour >= 0 && hour <= 23 &&
            minute >= 0 && minute <= 59 &&
            second >= 0 && second <= 59) {
            
            Serial.println("[DEBUG] Basic validation passed");
            
            // Additional day validation
            int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            int max_day = days_in_month[month - 1];
            if (day > max_day) {
                Serial.print("[DEBUG] Day adjusted for month - was ");
                Serial.print(day);
                Serial.print(", now ");
                day = max_day;
                Serial.println(day);
            }
            
            Serial.print("[DEBUG] Calling setRTCTime() with 4-digit year: ");
            Serial.println(year_4digit);
            setRTCTime(year_4digit, month, day, hour, minute, second);
            
            Serial.println("[DEBUG] Calling show_monitoring_screen()");
            // Switch to monitoring screen
            show_monitoring_screen();
        } else {
            Serial.println("[DEBUG] ERROR: Invalid time values!");
            Serial.print("[DEBUG] Validation failed - Year (2-digit): ");
            Serial.print(year_2digit >= 0 && year_2digit <= 99);
            Serial.print(", Month: ");
            Serial.print(month >= 1 && month <= 12);
            Serial.print(", Day: ");
            Serial.print(day >= 1 && day <= 31);
            Serial.print(", Hour: ");
            Serial.print(hour >= 0 && hour <= 23);
            Serial.print(", Minute: ");
            Serial.print(minute >= 0 && minute <= 59);
            Serial.print(", Second: ");
            Serial.println(second >= 0 && second <= 59);
        }
    } else {
        Serial.print("[DEBUG] Set Time button event code is not CLICKED: ");
        Serial.println(code);
    }
}

// Show monitoring screen
void show_monitoring_screen() {
    // Switch to default screen (monitoring screen)
    lv_scr_load(lv_scr_act());
    rtc_time_set = true;  // Ensure flag is set
    lv_refr_now(NULL);  // Force refresh
    Serial.println("Switched to monitoring screen");
}

// Read sensor data from UART slave (half-duplex: slave sends periodically)
bool readSensorData(SensorData* data) {
    // Check if we have at least 16 bytes available (4 floats)
    if (SlaveUART.available() < 16) {
        return false;  // Not enough data yet
    }
    
    uint8_t buffer[16] = {0};
    
    // Read exactly 16 bytes
    for (int i = 0; i < 16; i++) {
        buffer[i] = SlaveUART.read();
    }
    
    // Validate data
    bool all_ff = true;
    bool all_zeros = true;
    for (int i = 0; i < 16; i++) {
        if (buffer[i] != 0xFF) all_ff = false;
        if (buffer[i] != 0x00) all_zeros = false;
    }
    
    if (all_ff) {
        Serial.println("[UART] Error: Received all 0xFF");
        return false;
    }
    if (all_zeros) {
        Serial.println("[UART] Error: Received all zeros");
        return false;
    }
    
    // Parse the 4 floats from buffer
    memcpy(&data->oil_temp, &buffer[0], sizeof(float));
    memcpy(&data->motor_temp, &buffer[4], sizeof(float));
    memcpy(&data->vibration, &buffer[8], sizeof(float));
    memcpy(&data->current_draw, &buffer[12], sizeof(float));
    
    // Validate parsed values
    if (isnan(data->oil_temp) || isnan(data->motor_temp) || isnan(data->vibration) || isnan(data->current_draw)) {
        Serial.println("[UART] Error: NaN detected in parsed values");
        return false;
    }
    if (data->oil_temp < -50.0 || data->oil_temp > 500.0 ||
        data->motor_temp < -50.0 || data->motor_temp > 500.0 ||
        data->vibration < 0.0 || data->vibration > 100.0 ||
        data->current_draw < 0.0 || data->current_draw > 100.0) {
        Serial.println("[UART] Error: Values out of reasonable range");
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
    lv_obj_set_style_text_color(time_label, text_color, 0);
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

// Calculate week number (1-52/53) based on day of year
// Simple calculation: week = (day_of_year - 1) / 7 + 1
int getWeekNumber(int year, int month, int day) {
    // Days in each month (non-leap year)
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    // Check for leap year
    bool is_leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (is_leap) {
        days_in_month[1] = 29;  // February has 29 days in leap year
    }
    
    // Calculate day of year
    int day_of_year = 0;
    for (int i = 0; i < month - 1; i++) {
        day_of_year += days_in_month[i];
    }
    day_of_year += day;
    
    // Calculate week number (week 1 starts on Jan 1)
    int week = (day_of_year - 1) / 7 + 1;
    
    // Ensure week is in range 1-53
    if (week < 1) week = 1;
    if (week > 53) week = 53;
    
    return week;
}

// Get the current week folder path (e.g., "/Week_2024_01/")
String getWeekFolderPath() {
    if (!rtc.begin()) {
        return "/Week_0000_00/";  // Fallback if RTC not available
    }
    
    DateTime now = rtc.now();
    int year = now.year();
    int week = getWeekNumber(year, now.month(), now.day());
    
    char folder_path[20];
    snprintf(folder_path, sizeof(folder_path), "/Week_%04d_%02d/", year, week);
    return String(folder_path);
}

// Get the current log file path with week folder
String getCurrentLogFilePath() {
    return getWeekFolderPath() + "sensor_log.csv";
}

// Ensure the week folder exists, create it if it doesn't
// Uses SD.mkdir() to explicitly create the directory
bool ensureWeekFolderExists() {
    String folder_path = getWeekFolderPath();
    
    // Remove trailing slash for mkdir (if present)
    String dir_path = folder_path;
    if (dir_path.endsWith("/")) {
        dir_path = dir_path.substring(0, dir_path.length() - 1);
    }
    
    // Try to create the directory using SD.mkdir()
    // Note: mkdir() returns true if directory already exists or was created successfully
    if (SD.mkdir(dir_path)) {
        Serial.print("[SD] Week folder ready: ");
        Serial.println(folder_path);
        return true;
    } else {
        Serial.print("[SD] Warning: Could not create week folder: ");
        Serial.println(folder_path);
        Serial.println("[SD] Directory may already exist, continuing...");
        // Return true anyway - directory might already exist
        // We'll find out when we try to create the CSV file
        return true;
    }
}

// Ensure CSV header exists in the file, create it if file is new
bool ensureCSVHeaderExists(const String& filepath) {
    // Check if file exists
    File dataFile = SD.open(filepath, FILE_READ);
    if (!dataFile) {
        // File doesn't exist, create it with header
        dataFile = SD.open(filepath, FILE_WRITE);
        if (dataFile) {
            dataFile.println("timestamp,datetime,oil_temp,motor_temp,vibration,current_draw,status");
            dataFile.close();
            Serial.print("[SD] Created new CSV file with header: ");
            Serial.println(filepath);
            return true;
        } else {
            Serial.print("[SD] Error: Could not create CSV file: ");
            Serial.println(filepath);
            return false;
        }
    } else {
        // File exists, check if it has content (header)
        if (dataFile.size() == 0) {
            // File exists but is empty, add header
            dataFile.close();
            dataFile = SD.open(filepath, FILE_WRITE);
            if (dataFile) {
                dataFile.println("timestamp,datetime,oil_temp,motor_temp,vibration,current_draw,status");
                dataFile.close();
                Serial.print("[SD] Added header to empty CSV file: ");
                Serial.println(filepath);
                return true;
            }
        } else {
            // File exists and has content, assume header is there
            dataFile.close();
        }
    }
    
    return true;
}

// COMMENTED OUT: Timestamp update function - may cause SD card mounting issues
// Update file timestamp to reflect last write time
// Note: ESP32 SD library doesn't directly support setting timestamps,
// so we "touch" the file by reopening it briefly to trigger timestamp update
// void updateFileTimestamp(const String& filepath) {
//     // Try to update file timestamp by opening in append mode and closing
//     // This may trigger FatFS to update the modification time
//     // Note: This is a workaround - FatFS may not update timestamps if FF_FS_NORTC is enabled
//     // Opening in FILE_APPEND mode is safe - it won't overwrite existing content
//     File touchFile = SD.open(filepath, FILE_APPEND);
//     if (touchFile) {
//         // File opened successfully - just close it to trigger timestamp update
//         // The file pointer is already at the end, so no data is written
//         touchFile.close();
//     }
//     // If file open fails, timestamp update is not critical - continue anyway
// }

// Log sensor data to SD card
void logToSDCard(const SensorData& data, bool is_alarm) {
    if (!sd_card_initialized) {
        return;  // SD card not available
    }
    
    // Get the current log file path (includes week folder)
    String filepath = getCurrentLogFilePath();
    
    // Try to ensure week folder exists (may fail silently if it already exists)
    ensureWeekFolderExists();
    
    // Ensure CSV header exists (this will also create the directory if needed)
    ensureCSVHeaderExists(filepath);
    
    // Open file for appending (FILE_APPEND ensures data is added to end, not overwritten)
    File dataFile = SD.open(filepath, FILE_APPEND);
    if (dataFile) {
        // CSV format: timestamp, datetime, oil_temp, motor_temp, vibration, current_draw, status
        unsigned long timestamp = millis() / 1000;  // Seconds since boot
        
        // Add RTC datetime if available
        String datetime_str = "";
        if (rtc.begin()) {
            DateTime now = rtc.now();
            char dt_buf[32];
            snprintf(dt_buf, sizeof(dt_buf), "%04d-%02d-%02d %02d:%02d:%02d",
                    now.year(), now.month(), now.day(),
                    now.hour(), now.minute(), now.second());
            datetime_str = String(dt_buf);
        }
        
        dataFile.print(timestamp);
        dataFile.print(",");
        dataFile.print(datetime_str);
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
        
        // COMMENTED OUT: Timestamp update - may cause SD card mounting issues
        // Update file timestamp to reflect last write time
        // updateFileTimestamp(filepath);
        
        // Print success message with data details
        Serial.print("[SD] Successfully saved to ");
        Serial.print(filepath);
        Serial.print(": ");
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
        Serial.print("[SD] Error: Failed to open ");
        Serial.print(filepath);
        Serial.println(" for writing");
    }
}

// Log alarm acknowledgment event to SD card
void logAcknowledgmentToSD() {
    if (!sd_card_initialized) {
        return;  // SD card not available
    }
    
    // Get the current log file path (includes week folder)
    String filepath = getCurrentLogFilePath();
    
    // Try to ensure week folder exists (may fail silently if it already exists)
    ensureWeekFolderExists();
    
    // Ensure CSV header exists (this will also create the directory if needed)
    ensureCSVHeaderExists(filepath);
    
    // Open file for appending (FILE_APPEND ensures data is added to end, not overwritten)
    File dataFile = SD.open(filepath, FILE_APPEND);
    if (dataFile) {
        unsigned long timestamp = millis() / 1000;  // Seconds since boot
        
        // Add RTC datetime if available
        String datetime_str = "";
        if (rtc.begin()) {
            DateTime now = rtc.now();
            char dt_buf[32];
            snprintf(dt_buf, sizeof(dt_buf), "%04d-%02d-%02d %02d:%02d:%02d",
                    now.year(), now.month(), now.day(),
                    now.hour(), now.minute(), now.second());
            datetime_str = String(dt_buf);
        }
        
        // Log acknowledgment event with empty sensor values and ACKNOWLEDGED status
        dataFile.print(timestamp);
        dataFile.print(",");
        dataFile.print(datetime_str);
        dataFile.print(",,,,");  // Empty sensor values (oil_temp, motor_temp, vibration, current_draw)
        dataFile.println("ACKNOWLEDGED");
        
        dataFile.close();
        
        // COMMENTED OUT: Timestamp update - may cause SD card mounting issues
        // Update file timestamp to reflect last write time
        // updateFileTimestamp(filepath);
        
        Serial.print("[SD] Successfully saved alarm acknowledgment to ");
        Serial.println(filepath);
    } else {
        Serial.print("[SD] Error: Failed to open ");
        Serial.print(filepath);
        Serial.println(" for acknowledgment log");
    }
}

// Update time display from RTC
void updateTimeDisplay() {
    if (!rtc.begin()) {
        return;  // RTC not available
    }
    
    DateTime now = rtc.now();
    char datetime_str[32];
    // Format: MM/DD/YYYY on first line, HH:MM:SS on second line
    snprintf(datetime_str, sizeof(datetime_str), "%02d/%02d/%04d\n%02d:%02d:%02d", 
             now.month(), now.day(), now.year(),
             now.hour(), now.minute(), now.second());
    lv_label_set_text(time_label, datetime_str);
}

// Check if RTC time is set (not at default value)
// DS3231 defaults to 2000-01-01 00:00:00 when first powered or after losing power
bool isRTCTimeSet() {
    if (!rtc.begin()) {
        return false;  // RTC not available
    }
    
    DateTime now = rtc.now();
    // Check if time is at default (2000-01-01 00:00:00)
    if (now.year() == 2000 && now.month() == 1 && now.day() == 1 && 
        now.hour() == 0 && now.minute() == 0 && now.second() == 0) {
        return false;  // Time is at default, not set
    }
    
    // Also check if year is reasonable (not before 2020)
    if (now.year() < 2020) {
        return false;  // Time seems invalid
    }
    
    return true;  // Time appears to be set
}

// Parse compile-time date and time from __DATE__ and __TIME__ macros
// __DATE__ format: "Mmm dd yyyy" (e.g., "Dec 15 2024")
// __TIME__ format: "hh:mm:ss" (e.g., "12:34:56")
void setRTCFromCompileTime() {
    if (!rtc.begin()) {
        Serial.println("ERROR: RTC not initialized!");
        return;
    }
    
    // Parse __DATE__: "Mmm dd yyyy"
    const char* date_str = __DATE__;
    const char* month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    
    char month_str[4];
    int day, year;
    sscanf(date_str, "%s %d %d", month_str, &day, &year);
    
    // Find month number
    int month = 1;
    for (int i = 0; i < 12; i++) {
        if (strcmp(month_str, month_names[i]) == 0) {
            month = i + 1;
            break;
        }
    }
    
    // Parse __TIME__: "hh:mm:ss"
    const char* time_str = __TIME__;
    int hour, minute, second;
    sscanf(time_str, "%d:%d:%d", &hour, &minute, &second);
    
    // Set RTC time
    Serial.print("Setting RTC from compile time: ");
    Serial.print(__DATE__);
    Serial.print(" ");
    Serial.println(__TIME__);
    
    setRTCTime(year, month, day, hour, minute, second);
}

// Set RTC time (called from Serial commands or UI)
void setRTCTime(int year, int month, int day, int hour, int minute, int second) {
    if (!rtc.begin()) {
        Serial.println("ERROR: RTC not initialized!");
        return;
    }
    
    // Validate inputs
    if (year < 2020 || year > 2099 ||
        month < 1 || month > 12 ||
        day < 1 || day > 31 ||
        hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        Serial.println("ERROR: Invalid time values!");
        return;
    }
    
    rtc.adjust(DateTime(year, month, day, hour, minute, second));
    rtc_time_set = true;  // Mark time as set
    
    Serial.print("RTC time set to: ");
    Serial.print(year);
    Serial.print("/");
    Serial.print(month);
    Serial.print("/");
    Serial.print(day);
    Serial.print(" ");
    Serial.print(hour);
    Serial.print(":");
    Serial.print(minute);
    Serial.print(":");
    Serial.println(second);
}

// Check for Serial commands to set time
void checkSerialCommands() {
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        // Command format: SETTIME YYYY MM DD HH MM SS
        if (command.startsWith("SETTIME ")) {
            int year, month, day, hour, minute, second;
            if (sscanf(command.c_str(), "SETTIME %d %d %d %d %d %d", 
                      &year, &month, &day, &hour, &minute, &second) == 6) {
                setRTCTime(year, month, day, hour, minute, second);
            } else {
                Serial.println("ERROR: Invalid SETTIME format. Use: SETTIME YYYY MM DD HH MM SS");
            }
        }
        // Note: SETTIME_NOW removed - user must set time via UI or SETTIME command
        // Command: SHOWTIME - displays current RTC time
        else if (command == "SHOWTIME") {
            if (rtc.begin()) {
                DateTime now = rtc.now();
                Serial.print("Current RTC time: ");
                Serial.print(now.year());
                Serial.print("/");
                Serial.print(now.month());
                Serial.print("/");
                Serial.print(now.day());
                Serial.print(" ");
                Serial.print(now.hour());
                Serial.print(":");
                Serial.print(now.minute());
                Serial.print(":");
                Serial.println(now.second());
            } else {
                Serial.println("ERROR: RTC not initialized!");
            }
        }
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
    
    // Initialize UART for slave communication (half-duplex: RX only)
    // Serial2 (UART2) with RX on GPIO 3
    // Slave TX -> Master RX (for data)
    SlaveUART.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, -1);  // RX only, no TX needed
    
    Serial.println("UART Master initialized (half-duplex RX only)");
    Serial.print("UART RX Pin: GPIO ");
    Serial.println(UART_RX_PIN);
    Serial.print("UART Baud Rate: ");
    Serial.println(UART_BAUD);
    
    // Initialize I2C for DS3231 RTC
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println("I2C initialized");
    Serial.print("I2C SDA Pin: GPIO ");
    Serial.println(I2C_SDA_PIN);
    Serial.print("I2C SCL Pin: GPIO ");
    Serial.println(I2C_SCL_PIN);
    
    // Initialize DS3231 RTC
    if (!rtc.begin()) {
        Serial.println("ERROR: Couldn't find RTC!");
        Serial.println("RTC will not be available");
        rtc_time_set = false;
    } else {
        Serial.println("DS3231 RTC initialized successfully");
        
        // COMMENTED OUT: Check if RTC time is set - now setting at compile time instead
        // Check if RTC time is set (not at default)
        // rtc_time_set = isRTCTimeSet();
        
        // Set RTC time from compile-time macros (__DATE__ and __TIME__)
        // This automatically uses the date/time when the firmware was compiled
        setRTCFromCompileTime();
        rtc_time_set = true;  // Mark time as set after compile-time initialization
        
        // Display current RTC time
        DateTime now = rtc.now();
        Serial.print("Current RTC time: ");
        Serial.print(now.year());
        Serial.print("/");
        Serial.print(now.month());
        Serial.print("/");
        Serial.print(now.day());
        Serial.print(" ");
        Serial.print(now.hour());
        Serial.print(":");
        Serial.print(now.minute());
        Serial.print(":");
        Serial.println(now.second());
        
        // COMMENTED OUT: Time setting via UI/Serial disabled - using compile-time time
        // if (rtc_time_set) {
        //     Serial.println("RTC time is set and valid");
        // } else {
        //     Serial.println("RTC time is NOT set (at default) - user must set time");
        // }
        // 
        // Serial.println("\nTo set RTC time via Serial:");
        // Serial.println("  SETTIME YYYY MM DD HH MM SS");
        // Serial.println("  SHOWTIME (displays current time)");
    }
    
    // Initialize SD card on VSPI bus (shares SPI bus with TFT display)
    // Using default SPI instance (VSPI) that TFT_eSPI already initialized
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);  // CS high = SD card not selected
    
    // Ensure TFT CS is held high (TFT not selected) before SD init
    // TFT_CS is GPIO 16 (from User_Setup.h)
    pinMode(16, OUTPUT);
    digitalWrite(16, HIGH);
    delay(100);  // Small delay to let bus stabilize
    
    Serial.println("Using default SPI instance (VSPI) initialized by TFT_eSPI");
    Serial.print("SD CS Pin: GPIO ");
    Serial.println(SD_CS_PIN);
    Serial.println("SPI pins shared with TFT: MOSI=23, SCK=18");
    
    // Initialize SD card with default SPI instance (VSPI)
    // Try with a conservative frequency first (4MHz), then increase if needed
    Serial.println("Attempting to initialize SD card...");
    Serial.print("Using SPI frequency: ");
    Serial.print(SD_SPI_FREQ / 1000000);
    Serial.println(" MHz");
    Serial.println("[SD] Auto-format enabled: blank cards will be formatted automatically");
    
    // Try initializing with the specified frequency
    // Parameters: CS pin, SPI instance, frequency, mount point, max files, format_if_empty
    // format_if_empty=true will automatically format blank/unformatted cards
    Serial.println("[SD] Step 1: Attempting to mount SD card...");
    // Use default SPI instance (VSPI) - TFT_eSPI already initialized it
    if (SD.begin(SD_CS_PIN, SPI, SD_SPI_FREQ, "/sd", 5, true)) {
        Serial.println("[SD] Step 1: SUCCESS - SD card mounted!");
        sd_card_initialized = true;
        
        // Test write capability
        Serial.println("[SD] Step 2: Testing write capability...");
        File testFile = SD.open("/test_write.txt", FILE_WRITE);
        if (testFile) {
            testFile.println("SD card write test - OK");
            testFile.close();
            Serial.println("[SD] Step 2: SUCCESS - Write test passed!");
            
            // Clean up test file
            SD.remove("/test_write.txt");
            
            // Test week folder creation and CSV file setup
            Serial.println("[SD] Step 3: Testing week folder structure...");
            if (ensureWeekFolderExists()) {
                String week_path = getWeekFolderPath();
                Serial.print("[SD] Week folder: ");
                Serial.println(week_path);
                
                String log_path = getCurrentLogFilePath();
                Serial.print("[SD] Log file path: ");
                Serial.println(log_path);
                
                if (ensureCSVHeaderExists(log_path)) {
                    Serial.println("[SD] Step 3: SUCCESS - Week folder structure ready");
                } else {
                    Serial.println("[SD] Step 3: WARNING - Could not create CSV file");
                }
            } else {
                Serial.println("[SD] Step 3: WARNING - Could not create week folder");
            }
            
            Serial.println("[SD] ========================================");
            Serial.println("[SD] SD CARD FULLY OPERATIONAL");
            Serial.println("[SD] Ready to log sensor data every 30 seconds");
            Serial.println("[SD] ========================================");
        } else {
            Serial.println("[SD] Step 2: FAILED - Cannot write to SD card");
            Serial.println("[SD] Card is detected but write operations fail");
            Serial.println("[SD] Possible causes:");
            Serial.println("[SD]   - Write-protect switch enabled (if card has one)");
            Serial.println("[SD]   - Card is read-only");
            Serial.println("[SD]   - Filesystem corruption");
            sd_card_initialized = false;
        }
    } else {
        sd_card_initialized = false;
        Serial.println("[SD] Step 1: FAILED - SD card mount failed!");
        Serial.println("[SD] ========================================");
        Serial.println("[SD] DIAGNOSIS: Card detection/communication issue");
        Serial.println("[SD] ========================================");
        Serial.println("[SD] Possible causes:");
        Serial.println("[SD]   1. SD card not inserted or not making contact");
        Serial.println("[SD]   2. Incorrect wiring:");
        Serial.println("[SD]      - CLK should be GPIO 18 (shared with TFT)");
        Serial.println("[SD]      - MISO should be GPIO 19");
        Serial.println("[SD]      - MOSI should be GPIO 23 (shared with TFT)");
        Serial.println("[SD]      - CS should be GPIO 33");
        Serial.println("[SD]   3. Loose connections or bad solder joints");
        Serial.println("[SD]   4. SPI frequency too high (currently 10MHz)");
        Serial.println("[SD]   5. Power supply issues (SD cards need stable power)");
        Serial.println("[SD] Note: Auto-format is enabled but card must be detected first");
        Serial.println("[SD] Logging disabled until SD card is available");
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
    
    // Create monitoring screen (default screen)
    create_monitoring_screen();
    
    // Create time entry screen
    // COMMENTED OUT: Time setting screen disabled - using compile-time time instead
    // create_time_entry_screen();
    
    // Show appropriate screen based on RTC time status
    // COMMENTED OUT: Always show monitoring screen - time set at compile time
    // if (!rtc_time_set) {
    //     // Time not set - show time entry screen
    //     lv_scr_load(time_entry_screen);
    //     Serial.println("Showing time entry screen - time must be set");
    // } else {
    //     // Time is set - show monitoring screen
    //     lv_scr_load(lv_scr_act());
    //     Serial.println("Showing monitoring screen");
    // }
    
    // Always show monitoring screen (time set at compile time)
    lv_scr_load(lv_scr_act());
    Serial.println("Showing monitoring screen");
    
    // Force a refresh to ensure the screen is displayed
    lv_refr_now(NULL);
    
    Serial.println("LVGL initialized successfully!");
}

void loop() {
    // Handle LVGL tasks (call this as frequently as possible)
    // LVGL 8.3.11 uses lv_task_handler(), not lv_timer_handler() (that's LVGL 9+)
    lv_task_handler();
    
    // Check for Serial commands (time setting, etc.)
    checkSerialCommands();
    
    // COMMENTED OUT: Always process sensor data (time set at compile time)
    // Only process sensor data if time is set
    // if (rtc_time_set) {
    {
        // Update time display every second
        static unsigned long last_time_update = 0;
        if (millis() - last_time_update >= 1000) {
            updateTimeDisplay();
            last_time_update = millis();
        }
        
        // Check for incoming sensor data from slave (UART hardware detects when data arrives)
        SensorData sensor_data;
        if (readSensorData(&sensor_data)) {
            unsigned long current_time = millis();
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
            
            // Log to SD card every 30 seconds (regardless of alarm status)
            // This ensures consistent logging interval even during persistent alarms
            if (current_time - last_sd_log_time >= SD_LOG_INTERVAL) {
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
        }
        // If no valid data, just keep previous values displayed
        delay(5);
    // COMMENTED OUT: Time entry screen handling disabled
    // } else {
    //     // Time not set - stay on time entry screen
    //     // User interaction handled by LVGL touch events
    // }
    }
}