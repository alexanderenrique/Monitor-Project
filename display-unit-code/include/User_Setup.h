/**
 * @file User_Setup.h
 * TFT_eSPI User Configuration for ESP32
 * 
 * This file configures TFT_eSPI for your specific display.
 * Uncomment and configure the driver for your display chip.
 */

// Driver selection - Uncomment ONE of these for your display:
#define ILI9341_DRIVER      // ILI9341 TFT display
// #define ST7789_DRIVER       // ST7789 TFT display (240x240, 240x320, etc.)
// #define ST7735_DRIVER       // ST7735 TFT display
// #define ILI9163_DRIVER      // ILI9163 TFT display
// #define S6D02A1_DRIVER      // S6D02A1 TFT display
// #define RPI_ILI9486_DRIVER  // Raspberry Pi ILI9486 TFT display
// #define HX8357D_DRIVER      // HX8357D TFT display
// #define ILI9481_DRIVER      // ILI9481 TFT display
// #define ILI9488_DRIVER      // ILI9488 TFT display
// #define ILI9486_DRIVER      // ILI9486 TFT display
// #define ST7796_DRIVER       // ST7796 TFT display

// Default to ST7789 (common for ESP32 projects)
// #ifndef ST7789_DRIVER
// #define ST7789_DRIVER
// #endif

// ESP32 pin configuration - Adjust these to match your wiring:
#define TFT_MOSI 23  // GPIO23 - SPI MOSI
#define TFT_SCLK 18  // GPIO18 - SPI Clock
#define TFT_CS   16   // GPIO16  - Chip Select
#define TFT_DC   5   // GPIO5  - Data/Command
#define TFT_RST  17   // GPIO4  - Reset (or -1 if not connected)

// Optional: Backlight control
// #define TFT_BL   32  // GPIO32 - Backlight control
// #define TFT_BACKLIGHT_ON HIGH  // Backlight ON state

// SPI frequency (adjust if needed)
#define SPI_FREQUENCY  27000000  // 27MHz (can go up to 40MHz for some displays)
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

// Display orientation
#define TFT_ROTATION 0  // 0=Portrait, 1=Landscape, 2=Portrait inverted, 3=Landscape inverted

// Color order (most displays use RGB)
#define TFT_RGB_ORDER TFT_BGR  // TFT_RGB or TFT_BGR - switched to BGR because green/blue were swapped

// Optional: Invert colors
// #define TFT_INVERSION_ON

// Optional: Enable DMA for faster transfers (recommended for ESP32)
#define ESP32_DMA_CHANNEL 1

// ============================================================================
// Touch Controller Configuration
// ============================================================================

// Touch controller chip select pin (for SPI touch controllers like XPT2046)
// Set this to the GPIO pin connected to your touch controller's CS pin
// Set to -1 if you don't have a touch screen
#define TOUCH_CS 27   // GPIO pin for touch controller chip select (separate from TFT_CS)

// Optional: Touch interrupt pin (set to -1 if not used)
// #define TOUCH_IRQ -1  // GPIO pin for touch interrupt

// Touch calibration values (adjust these based on your display)
// You may need to calibrate these values for your specific display
// These are default values for XPT2046 - adjust as needed
#define XPT2046_X_MIN     200
#define XPT2046_X_MAX     3800
#define XPT2046_Y_MIN     200
#define XPT2046_Y_MAX     3800

// Note: SPI_TOUCH_FREQUENCY is already defined above (line 41)
// For I2C touch controllers (like FT6236), configure I2C pins separately

