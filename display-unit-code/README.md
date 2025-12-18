# ESP32 LVGL Display Project

A PlatformIO project for ESP32 using LVGL (Light and Versatile Graphics Library).

## Requirements

- PlatformIO IDE or PlatformIO CLI
- ESP32 development board
- Display hardware (SPI/TFT display)

## Setup

1. Install PlatformIO if you haven't already
2. Clone or download this project
3. Open the project in PlatformIO
4. Install dependencies: `pio lib install`

## Configuration

### TFT_eSPI Display Setup

This project uses **TFT_eSPI** for display driver support. You need to configure it for your specific display:

1. **Edit `include/User_Setup.h`**:
   - Uncomment the driver for your display chip (e.g., `ST7789_DRIVER`, `ILI9341_DRIVER`)
   - Update the GPIO pin definitions to match your wiring:
     - `TFT_MOSI` - SPI MOSI pin (default: GPIO23)
     - `TFT_SCLK` - SPI Clock pin (default: GPIO18)
     - `TFT_CS` - Chip Select pin (default: GPIO5)
     - `TFT_DC` - Data/Command pin (default: GPIO2)
     - `TFT_RST` - Reset pin (default: GPIO4, or -1 if not connected)
   - Adjust SPI frequency if needed
   - Set display rotation/orientation

2. **Update display dimensions** in `src/main.cpp`:
   - Set `DISPLAY_WIDTH` and `DISPLAY_HEIGHT` to match your display
   - Common sizes: 240x320, 240x240, 320x240, etc.

3. **Adjust rotation** in `setup()`:
   - Change `tft.setRotation(0)` to match your display orientation (0-3)

### Supported Display Drivers

TFT_eSPI supports many common displays:
- ST7789 (240x240, 240x320, 135x240)
- ILI9341 (240x320)
- ST7735 (128x160, 128x128)
- ILI9488 (320x480)
- And many more...

## Building and Uploading

```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Monitor serial output
pio device monitor
```

## Project Structure

```
.
├── platformio.ini      # PlatformIO configuration
├── src/
│   └── main.cpp        # Main application code
└── README.md           # This file
```

## Next Steps

1. Configure your display driver
2. Implement touch input (if applicable)
3. Customize the UI in `create_test_screen()`
4. Add your application logic

## Resources

- [LVGL Documentation](https://docs.lvgl.io/)
- [PlatformIO ESP32](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [LVGL ESP32 Examples](https://github.com/lvgl/lv_port_esp32)


