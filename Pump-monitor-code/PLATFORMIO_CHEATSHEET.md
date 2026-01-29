# PlatformIO Cheatsheet

## Basic Commands

### Build & Upload
```bash
# Build project
pio run

# Build specific environment
pio run -e ATtiny402

# Upload firmware
pio run --target upload

# Upload to specific port
pio run --target upload --upload-port /dev/ttyUSB0

# Build and upload in one command
pio run --target upload
```

### Monitor Serial
```bash
# Monitor serial output
pio device monitor

# Monitor with specific baud rate
pio device monitor -b 115200

# Monitor and upload
pio run --target upload && pio device monitor
```

### Clean & Rebuild
```bash
# Clean build files
pio run --target clean

# Clean and rebuild
pio run --target clean && pio run
```

## Library Management

### Install Libraries
```bash
# Install library by name
pio lib install "Library Name"

# Install library by ID
pio lib install 1234

# Install from Git repository
pio lib install https://github.com/user/repo.git

# Install library from library.json
pio lib install
```

### List & Search
```bash
# List installed libraries
pio lib list

# Search for libraries
pio lib search "search term"

# Show library details
pio lib show "Library Name"
```

### Update & Remove
```bash
# Update all libraries
pio lib update

# Update specific library
pio lib update "Library Name"

# Remove library
pio lib uninstall "Library Name"
```

## Project Management

### Initialize Project
```bash
# Create new project
pio project init

# Initialize with board
pio project init --board ATtiny402
```

### Update Platform
```bash
# Update PlatformIO Core
pio update

# Update platforms
pio platform update

# Update specific platform
pio platform update atmelmegaavr
```

## Device Management

### List Devices
```bash
# List connected devices
pio device list

# Show device details
pio device monitor --list
```

## Useful Shortcuts

### Combined Commands
```bash
# Build, upload, and monitor
pio run --target upload && pio device monitor

# Clean, build, upload
pio run --target clean && pio run --target upload
```

### Verbose Output
```bash
# Verbose build output
pio run -v

# Verbose upload
pio run --target upload -v
```

## Configuration Tips

### platformio.ini Common Options
```ini
[env:ATtiny402]
platform = atmelmegaavr
board = ATtiny402
framework = arduino

; Upload speed
upload_speed = 115200

; Monitor settings
monitor_speed = 115200
monitor_filters = default

; Build flags
build_flags = -DDEBUG

; Library dependencies
lib_deps = 
    LibraryName@^1.0.0
```

## Debugging

### Serial Monitor Filters
```bash
# Colorize output
pio device monitor --filter colorize

# Echo (show sent commands)
pio device monitor --filter echo

# Time (show timestamps)
pio device monitor --filter time
```

## Quick Reference

| Task | Command |
|------|---------|
| Build | `pio run` |
| Upload | `pio run --target upload` |
| Monitor | `pio device monitor` |
| Clean | `pio run --target clean` |
| Install lib | `pio lib install "Name"` |
| List devices | `pio device list` |
| Update | `pio update` |

## Tips

- Use `pio run -e <env>` to target specific environments
- Combine commands with `&&` for sequential execution
- Use `-v` flag for verbose output when debugging
- Check `platformio.ini` for project-specific settings
- Libraries are managed per-project in `lib/` directory
