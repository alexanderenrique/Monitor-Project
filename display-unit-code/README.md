# ESP32 Monitor Display Unit

A PlatformIO project for ESP32 monitoring system with LVGL display, SD card logging, RTC time management, and sensor monitoring capabilities.

## Features

- **LVGL 8.3.11 Display Interface** - Modern touch-enabled UI for monitoring and control
- **SD Card Logging** - Automatic sensor data logging to CSV files organized by week
- **RTC Time Management** - DS3231 real-time clock for accurate timestamps
- **Sensor Monitoring** - Monitor oil temperature, motor temperature, vibration, and current draw
- **Alarm System** - Configurable thresholds with visual and logging alerts
- **UART Communication** - Half-duplex communication with slave ESP32C3 module
- **Touch Input** - XPT2046 touch controller support
- **File Timestamps** - Automatic file modification date updates

## Pinout Summary

### Complete Pin Assignment

| GPIO | Function | Description | Bus/Protocol |
|------|----------|-------------|--------------|
| 3 | UART2 RX | Slave communication (RX only) | UART2 |
| 5 | TFT_DC | TFT data/command control | SPI (VSPI) |
| 16 | TFT_CS | TFT display chip select | SPI (VSPI) |
| 17 | TFT_RST | TFT display reset | SPI (VSPI) |
| 18 | SPI SCK | SPI clock (shared: TFT + SD) | SPI (VSPI) |
| 19 | SPI MISO | SD card data input | SPI (VSPI) |
| 21 | I2C SDA | RTC data line | I2C |
| 22 | I2C SCL | RTC clock line | I2C |
| 23 | SPI MOSI | SPI data out (shared: TFT + SD) | SPI (VSPI) |
| 27 | TOUCH_CS | Touch controller chip select | SPI (VSPI) |
| 33 | SD_CS | SD card chip select | SPI (VSPI) |

### Bus Configuration

**VSPI (SPI3)** - Shared between TFT Display, SD Card, and Touch Controller
- MOSI: GPIO 23
- MISO: GPIO 19 
- SCK: GPIO 18
- CS Pins: GPIO 16 (TFT), GPIO 33 (SD), GPIO 27 (Touch)

**I2C** - DS3231 RTC Module
- SDA: GPIO 21
- SCL: GPIO 22

**UART2** - Slave ESP32C3 Communication
- RX: GPIO 3
- TX: Not used (half-duplex, master only receives)
- Baud Rate: 9600

## Requirements

- PlatformIO IDE or PlatformIO CLI
- ESP32 development board
- ILI9341 TFT display (240x320)
- XPT2046 touch controller
- DS3231 RTC module
- SD card module (SPI)
- Slave ESP32C3 module (for sensor data)

## Setup

1. Install PlatformIO if you haven't already
2. Clone or download this project
3. Open the project in PlatformIO
4. Install dependencies: `pio lib install`

## Configuration

### TFT_eSPI Display Setup

This project uses **TFT_eSPI** for display driver support. Configuration is in `include/User_Setup.h`:

- **Driver**: ILI9341 (240x320)
- **SPI Pins**: 
  - MOSI: GPIO 23
  - SCK: GPIO 18
  - CS: GPIO 16
  - DC: GPIO 5
  - RST: GPIO 17
- **SPI Frequency**: 27 MHz
- **Rotation**: Portrait (0)

### Touch Controller

- **Controller**: XPT2046
- **CS Pin**: GPIO 27
- **SPI Frequency**: 2.5 MHz

### SD Card

- **CS Pin**: GPIO 33
- **SPI Bus**: VSPI (shared with TFT)
- **Frequency**: 10 MHz
- **Mount Point**: `/sd`
- **File Format**: CSV files named `/Week_YYYY_WW_sensor_log.csv`

### RTC (DS3231)

- **I2C Pins**: SDA=GPIO 21, SCL=GPIO 22
- **Time Source**: Set from compile-time macros (`__DATE__` and `__TIME__`)

### Alarm Thresholds

Configured in `src/main.cpp`:
- **Minimum Temperature**: 30.0°F (alarm if below)
- **Maximum Vibration**: 10.0g (alarm if above)
- **Maximum Current**: 5.0A (alarm if above)

## SD Card Logging

### File Organization

Sensor data is logged to CSV files organized by week:
- **Format**: `/Week_YYYY_WW_sensor_log.csv`
- **Example**: `/Week_2026_03_sensor_log.csv`
- Files are created automatically in the SD card root directory

### CSV Format

```
datetime,oil_temp,motor_temp,vibration,current_draw,status
2026-01-16 10:30:45,45.2,48.5,3.2,2.1,NORMAL
2026-01-16 10:31:15,25.0,48.5,3.2,2.1,OIL_TEMP_LOW
2026-01-16 10:31:45,25.0,25.0,12.5,6.8,OIL_TEMP_LOW,MOTOR_TEMP_LOW,VIBRATION_HIGH,CURRENT_HIGH
```

### Status Column

The status column indicates which sensors are alarming:
- `NORMAL` - No alarms
- `OIL_TEMP_LOW` - Oil temperature below threshold
- `MOTOR_TEMP_LOW` - Motor temperature below threshold
- `VIBRATION_HIGH` - Vibration exceeds threshold
- `CURRENT_HIGH` - Current draw exceeds threshold
- Multiple alarms are comma-separated (e.g., `OIL_TEMP_LOW,MOTOR_TEMP_LOW`)

### Logging Interval

- **Default**: Every 30 seconds
- Configured via `SD_LOG_INTERVAL` (30000ms)

### Special Events

- **Alarm Acknowledgment**: Logged with `ACKNOWLEDGED` status and empty sensor values
- **File Timestamps**: Automatically updated on each write to reflect modification time

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
├── platformio.ini          # PlatformIO configuration
├── src/
│   ├── main.cpp           # Main application code
│   └── touch_mapping.cpp  # Touch input handling
├── include/
│   ├── User_Setup.h       # TFT_eSPI display configuration
│   ├── touch_mapping.h    # Touch mapping definitions
│   └── lv_conf.h         # LVGL configuration
└── README.md              # This file
```

## Key Features Implementation

### Sensor Data Monitoring

- Receives sensor data via UART from slave ESP32C3 module
- Monitors: Oil temperature, motor temperature, vibration, current draw
- Updates display in real-time with color-coded alarm states

### Alarm System

- Visual alarm indication (red background, white text)
- Touch-to-acknowledge functionality
- Detailed alarm logging to SD card
- Multiple simultaneous alarms supported

### Time Management

- RTC time set automatically from firmware compile time
- Accurate timestamps for all logged data
- Display shows current RTC time

### Display Features

- Real-time sensor value display
- Alarm status indication
- Touch-enabled interface
- Color-coded status (normal vs alarm)

## Resources

- [LVGL Documentation](https://docs.lvgl.io/)
- [PlatformIO ESP32](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [TFT_eSPI Library](https://github.com/Bodmer/TFT_eSPI)
- [LVGL ESP32 Examples](https://github.com/lvgl/lv_port_esp32)

## Notes

- SD card must be formatted (FAT32) - auto-formatting enabled for blank cards
- RTC time is set from compile-time macros - ensure accurate system time when compiling
- SPI bus is shared between TFT, SD card, and touch controller - proper CS pin management is critical
- File timestamps may show 1980 if FatFS has RTC disabled (FF_FS_NORTC = 1)
