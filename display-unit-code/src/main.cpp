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

// Create a simple test screen
void create_test_screen() {
    // Set screen background to medium gray for color testing
    // LV_COLOR_MAKE creates RGB565 color: (R, G, B) where each is 0-255
    lv_obj_set_style_bg_color(lv_scr_act(), LV_COLOR_MAKE(128, 128, 128), 0);  // Medium gray
    
    // Create a label with "Hello World"
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello World");
    
    // Create a style for larger text
    static lv_style_t label_style;
    lv_style_init(&label_style);
    
    // Set font size - change the number to use different sizes (16, 18, 20, 22, 24, 28, 36, 48)
    // Make sure the font is enabled in include/lv_conf.h (LV_FONT_MONTSERRAT_XX = 1)
    lv_style_set_text_font(&label_style, &lv_font_montserrat_24);  // Try 16, 18, 20, 22, 24, 28, 36, or 48
    
    // Set text color to white for contrast on gray background
    lv_style_set_text_color(&label_style, LV_COLOR_MAKE(255, 255, 255));
    
    // Apply the style to the label
    lv_obj_add_style(label, &label_style, 0);
    
    // Center the label
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    // Add color test rectangles to check RGB565 color accuracy
    // Red rectangle (top left)
    lv_obj_t *red_rect = lv_obj_create(lv_scr_act());
    lv_obj_set_size(red_rect, 60, 40);
    lv_obj_set_pos(red_rect, 10, 10);
    lv_obj_set_style_bg_color(red_rect, LV_COLOR_MAKE(255, 0, 0), 0);  // Pure red
    lv_obj_set_style_border_width(red_rect, 0, 0);  // Remove border
    lv_obj_set_style_radius(red_rect, 0, 0);  // Remove rounded corners for crisp edges
    
    // Green rectangle (top right)
    lv_obj_t *green_rect = lv_obj_create(lv_scr_act());
    lv_obj_set_size(green_rect, 60, 40);
    lv_obj_set_pos(green_rect, DISPLAY_WIDTH - 70, 10);
    lv_obj_set_style_bg_color(green_rect, LV_COLOR_MAKE(0, 255, 0), 0);  // Pure green
    lv_obj_set_style_border_width(green_rect, 0, 0);
    lv_obj_set_style_radius(green_rect, 0, 0);  // Remove rounded corners
    
    // Blue rectangle (bottom left)
    lv_obj_t *blue_rect = lv_obj_create(lv_scr_act());
    lv_obj_set_size(blue_rect, 60, 40);
    lv_obj_set_pos(blue_rect, 10, DISPLAY_HEIGHT - 50);
    lv_obj_set_style_bg_color(blue_rect, LV_COLOR_MAKE(0, 0, 255), 0);  // Pure blue
    lv_obj_set_style_border_width(blue_rect, 0, 0);
    lv_obj_set_style_radius(blue_rect, 0, 0);  // Remove rounded corners
    
    // Cyan rectangle (bottom right) - tests color mixing
    lv_obj_t *cyan_rect = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cyan_rect, 60, 40);
    lv_obj_set_pos(cyan_rect, DISPLAY_WIDTH - 70, DISPLAY_HEIGHT - 50);
    lv_obj_set_style_bg_color(cyan_rect, LV_COLOR_MAKE(0, 255, 255), 0);  // Cyan (green + blue)
    lv_obj_set_style_border_width(cyan_rect, 0, 0);
    lv_obj_set_style_radius(cyan_rect, 0, 0);  // Remove rounded corners
    
    // White rectangle (center top) - tests white level
    lv_obj_t *white_rect = lv_obj_create(lv_scr_act());
    lv_obj_set_size(white_rect, 60, 40);
    lv_obj_set_pos(white_rect, (DISPLAY_WIDTH - 60) / 2, 10);
    lv_obj_set_style_bg_color(white_rect, LV_COLOR_MAKE(255, 255, 255), 0);  // Pure white
    lv_obj_set_style_border_width(white_rect, 0, 0);
    lv_obj_set_style_radius(white_rect, 0, 0);  // Remove rounded corners
    
    // Black rectangle (center bottom) - tests black level
    lv_obj_t *black_rect = lv_obj_create(lv_scr_act());
    lv_obj_set_size(black_rect, 60, 40);
    lv_obj_set_pos(black_rect, (DISPLAY_WIDTH - 60) / 2, DISPLAY_HEIGHT - 50);
    lv_obj_set_style_bg_color(black_rect, LV_COLOR_MAKE(0, 0, 0), 0);  // Pure black
    lv_obj_set_style_border_width(black_rect, 0, 0);
    lv_obj_set_style_radius(black_rect, 0, 0);  // Remove rounded corners
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
    
    // Create test screen
    create_test_screen();
    
    // Force a refresh to ensure the screen is displayed
    lv_refr_now(NULL);
    
    Serial.println("LVGL initialized successfully!");
}

void loop() {
    // Handle LVGL tasks (call this as frequently as possible)
    // LVGL 8.3.11 uses lv_task_handler(), not lv_timer_handler() (that's LVGL 9+)
    lv_task_handler();
    
    // Small delay to prevent watchdog issues
    delay(5);
}

