#include "touch_mapping.h"
#include <Arduino.h>

/*
 * TOUCH COORDINATE MAPPING
 * 
 * This file maps actual touch screen coordinates to button actions.
 * 
 * HOW TO CALIBRATE:
 * 1. Touch a button on the screen
 * 2. Check the serial monitor for the touch coordinates: [TOUCH] Raw: (x, y)
 * 3. Add a new entry to the touch_mappings array below with:
 *    - x_min, x_max: X coordinate range (give some margin, e.g., ±10 pixels)
 *    - y_min, y_max: Y coordinate range (give some margin, e.g., ±10 pixels)
 *    - &button_pointer: Pointer to the button variable (e.g., &year_up_btn)
 *    - "Button Name": Descriptive name for debugging
 * 
 * EXAMPLE:
 * If you touch Year UP button and see: [TOUCH] Raw: (57, 81)
 * Add: {50, 70, 75, 90, &year_up_btn, "Year UP"}
 *      (x: 50-70 covers 57±10, y: 75-90 covers 81±10)
 */

static TouchMapping touch_mappings[] = {
    // Year UP: Touch at (57, 81) should trigger year_up_btn
    {50, 70, 75, 90, &year_up_btn, "Year UP"},
    
    // Year DOWN: Touch at (91-93, 81-82) should trigger year_down_btn
    {85, 100, 75, 90, &year_down_btn, "Year DOWN"},
    
    // TODO: Add mappings for other buttons as we calibrate them
    // Format: {x_min, x_max, y_min, y_max, &button_pointer, "Button Name"}
    // 
    // Example entries (adjust coordinates based on actual touch):
    // {x_min, x_max, y_min, y_max, &month_up_btn, "Month UP"},
    // {x_min, x_max, y_min, y_max, &month_down_btn, "Month DOWN"},
    // {x_min, x_max, y_min, y_max, &day_up_btn, "Day UP"},
    // {x_min, x_max, y_min, y_max, &day_down_btn, "Day DOWN"},
    // {x_min, x_max, y_min, y_max, &hour_up_btn, "Hour UP"},
    // {x_min, x_max, y_min, y_max, &hour_down_btn, "Hour DOWN"},
    // {x_min, x_max, y_min, y_max, &minute_up_btn, "Minute UP"},
    // {x_min, x_max, y_min, y_max, &minute_down_btn, "Minute DOWN"},
    // {x_min, x_max, y_min, y_max, &second_up_btn, "Second UP"},
    // {x_min, x_max, y_min, y_max, &second_down_btn, "Second DOWN"},
    // {x_min, x_max, y_min, y_max, &set_time_btn, "Set Time"},
    
    // End marker - MUST be last entry
    {0, 0, 0, 0, nullptr, nullptr}
};

bool map_touch_to_button(uint16_t x, uint16_t y) {
    // COMMENTED OUT: Time entry screen disabled - always return false
    // Only map touches when on time entry screen
    // if (lv_scr_act() != time_entry_screen) {
    //     return false;
    // }
    return false;  // Time entry screen disabled
    
    // Check each mapping
    for (int i = 0; touch_mappings[i].button != nullptr; i++) {
        const TouchMapping &mapping = touch_mappings[i];
        
        // Check if touch is within this button's range
        if (x >= mapping.x_min && x <= mapping.x_max &&
            y >= mapping.y_min && y <= mapping.y_max) {
            
            // Get the button pointer
            lv_obj_t *button = *(mapping.button);
            
            if (button != nullptr) {
                Serial.print("[TOUCH MAP] ");
                Serial.print(mapping.name);
                Serial.print(" button triggered at (");
                Serial.print(x);
                Serial.print(", ");
                Serial.print(y);
                Serial.print(") - Range: x[");
                Serial.print(mapping.x_min);
                Serial.print("-");
                Serial.print(mapping.x_max);
                Serial.print("] y[");
                Serial.print(mapping.y_min);
                Serial.print("-");
                Serial.print(mapping.y_max);
                Serial.println("]");
                
                // Trigger the button
                lv_event_send(button, LV_EVENT_CLICKED, NULL);
                return true;
            }
        }
    }
    
    return false;
}
