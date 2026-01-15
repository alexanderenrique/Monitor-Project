#ifndef TOUCH_MAPPING_H
#define TOUCH_MAPPING_H

#include <stdint.h>
#include <lvgl.h>

// Forward declarations
extern lv_obj_t *time_entry_screen;
extern lv_obj_t *year_up_btn, *year_down_btn;
extern lv_obj_t *month_up_btn, *month_down_btn;
extern lv_obj_t *day_up_btn, *day_down_btn;
extern lv_obj_t *hour_up_btn, *hour_down_btn;
extern lv_obj_t *minute_up_btn, *minute_down_btn;
extern lv_obj_t *second_up_btn, *second_down_btn;
extern lv_obj_t *set_time_btn;

// Structure to define a touch coordinate range and which button it maps to
struct TouchMapping {
    uint16_t x_min;      // Minimum X coordinate
    uint16_t x_max;      // Maximum X coordinate
    uint16_t y_min;      // Minimum Y coordinate
    uint16_t y_max;      // Maximum Y coordinate
    lv_obj_t **button;   // Pointer to button pointer (so we can update it)
    const char *name;    // Name for debugging
};

// Function to map touch coordinates to button actions
// Returns true if a button was triggered, false otherwise
bool map_touch_to_button(uint16_t x, uint16_t y);

#endif // TOUCH_MAPPING_H
