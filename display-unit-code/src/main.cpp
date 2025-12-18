#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>

// LVGL display buffer size
#define LVGL_BUFFER_SIZE 10 * 1024

// Display dimensions (adjust based on your display)
// Common sizes: 240x320 (ILI9341), 240x240 (ST7789), 320x240, etc.
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

// TFT_eSPI instance
TFT_eSPI tft = TFT_eSPI();

// LVGL display buffer
static lv_display_t *display;
static lv_color_t *buf1;
static lv_color_t *buf2;

// LVGL input device (touch)
static lv_indev_t *indev_touch;

// Timer for lv_tick_inc() - needs to be called every 1ms
hw_timer_t *timer = NULL;
void IRAM_ATTR onTimer() {
    lv_tick_inc(1);  // Increment LVGL tick by 1ms
}

// Display flush callback for LVGL
void display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    // Start TFT transaction
    tft.startWrite();
    
    // Set the display window
    tft.setAddrWindow(area->x1, area->y1, w, h);
    
    // Push pixels to display
    tft.pushPixels((uint16_t*)px_map, w * h);
    
    // End TFT transaction
    tft.endWrite();
    
    // Tell LVGL the flush is done
    lv_display_flush_ready(disp);
}

// Touch input read callback for LVGL
void touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t x = 0, y = 0;
    bool touched = tft.getTouch(&x, &y);
    
    if (touched) {
        // Clamp coordinates to display bounds to prevent LVGL warnings
        // The touch controller may return values outside the display resolution
        if (x >= DISPLAY_WIDTH) x = DISPLAY_WIDTH - 1;
        if (y >= DISPLAY_HEIGHT) y = DISPLAY_HEIGHT - 1;
        
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        
        Serial.print("Touch - X: ");
        Serial.print(x);
        Serial.print(", Y: ");
        Serial.println(y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Create a simple test screen
void create_test_screen() {
    // Create a label with "Hello World"
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello World");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
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
    tft.setRotation(0);  // Adjust rotation: 0-3 (0=portrait, 1=landscape, etc.)
    tft.fillScreen(TFT_BLACK);
    
    Serial.println("TFT display initialized");
    
    // Create LVGL display
    display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(display, buf1, buf2, LVGL_BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    
    // Initialize touch input device
    indev_touch = lv_indev_create();
    lv_indev_set_type(indev_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touch, touch_read);
    
    // Setup hardware timer for lv_tick_inc() - called every 1ms
    timer = timerBegin(0, 80, true);  // Timer 0, prescaler 80 (1MHz), count up
    timerAttachInterrupt(timer, onTimer, true);  // true = edge-triggered interrupt
    timerAlarmWrite(timer, 1000, true);  // 1000 microseconds = 1ms, auto-reload
    timerAlarmEnable(timer);
    
    Serial.println("LVGL timer initialized");
    
    // Create test screen
    create_test_screen();
    
    Serial.println("LVGL initialized successfully!");
}

void loop() {
    // Handle LVGL tasks (call this as frequently as possible)
    // Touch input is now handled automatically by LVGL via touch_read callback
    lv_timer_handler();
    
    // Small delay to prevent watchdog issues
    delay(5);
}

