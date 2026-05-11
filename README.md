# ESP32-S3 Smart Camera

A feature-rich smart camera project built on ESP32-S3 platform with TFT display, joystick control, and WiFi connectivity.

## Features

- **Real-time Camera Preview**: Live video streaming on TFT display (240x240 resolution)
- **Photo Capture**: High-resolution photo capture (up to UXGA 1600x1200) with SD card storage
- **Dual-Core Architecture**: Camera preview runs on Core 1, main loop on Core 0 for optimal performance
- **Motion Detection**: Automatic motion-triggered photo capture
- **Timelapse Photography**: Configurable interval-based timelapse shooting
- **WiFi Connectivity**:
  - Station mode: Connect to existing WiFi network
  - AP mode: Access point mode for direct configuration (SSID: `ESP32-S3-CAM-Setup`)
- **Web Server**: Remote camera control and live streaming via web interface
- **OTA Updates**: Over-the-air firmware updates
- **Menu System**: Chinese font supported menu navigation via joystick
- **TFT Display**: ST77XX-based color display with UI frame and status bar
- **SD Card Storage**: SD\_MMC support for photo storage

## Hardware Requirements

### Microcontroller

- **ESP32-S3** (recommended: ESP32-S3-EYE development board)

### Camera Module

- **OV2640** or compatible OV series camera module

### Display

- **ST7735/ST7789** TFT display (128x128 or 240x240 pixels)

### Controls

- **Joystick Module**: 2-axis analog joystick with push button
  - VRX: GPIO 14
  - VRY: GPIO 3
  - SW: GPIO 46

### Storage

- **MicroSD Card**: SD\_MMC interface (1-bit mode)
  - CLK: GPIO 39
  - CMD: GPIO 38
  - D0: GPIO 40

### Additional

- **Button**: IO0 (built-in button on most ESP32-S3 boards)

## Pin Configuration

### Camera (ESP32S3-EYE)

| Signal        | GPIO                         |
| ------------- | ---------------------------- |
| XCLK          | 15                           |
| SDA (SIOD)    | 4                            |
| SCL (SIOC)    | 5                            |
| D0-D7 (Y2-Y9) | 11, 9, 8, 10, 12, 18, 17, 16 |
| VSYNC         | 6                            |
| HREF          | 7                            |
| PCLK          | 13                           |

### TFT Display (SPI)

| Signal         | GPIO |
| -------------- | ---- |
| CS             | 1    |
| DC             | 19   |
| RST            | 20   |
| MOSI           | 21   |
| SCLK           | 47   |
| BL (Backlight) | 45   |

## Software Dependencies

### Arduino Libraries

This project requires the following Arduino libraries:

- **Arduino ESP32 Core** (v2.x or v3.x)
- **Adafruit GFX Library**: Graphics rendering
- **Adafruit ST7735/ST7789 Library**: TFT display driver
- **TJpg\_Decoder**: JPEG decoding
- **SD/SD\_MMC**: SD card support
- **WiFi**: WiFi connectivity
- **WebServer**: HTTP server
- **Preferences**: Non-volatile storage
- **ArduinoOTA**: OTA updates
- **ESPmDNS**: mDNS/Bonjour support
- **Wire**: I2C communication
- **SPI**: SPI communication

### Installation

1. Install Arduino IDE (v2.x recommended)
2. Add ESP32 board support via Board Manager
3. Install required libraries via Library Manager:
   - Adafruit GFX Library
   - Adafruit ST7735 and ST7789 Library
   - TJpg\_Decoder

## Project Structure

```
ESP32S/
├── ESP32S.ino              # Main entry point and dual-core setup
├── camera_config.h         # Pin definitions and configuration constants
├── camera_driver.cpp/h     # Camera initialization and control
├── chinese_font.h          # Chinese font data for display
├── config_manager.cpp/h    # Settings persistence via Preferences
├── html_pages.cpp/h        # Web interface HTML/CSS/JS content
├── joystick.cpp/h          # Joystick input handling
├── menu_system.cpp/h       # On-device menu navigation system
├── motion_detect.cpp/h     # Motion detection algorithm
├── ota_update.cpp/h        # OTA firmware update handler
├── photo_capture.cpp/h     # Photo capture and SD card saving
├── tft_display.cpp/h       # TFT display and UI rendering
├── timelapse.cpp/h         # Timelapse photography controller
├── web_server.cpp/h        # HTTP server and streaming endpoints
├── wifi_manager.cpp/h      # WiFi connection and AP mode management
└── partitions.csv          # Flash partition scheme
```

## Building and Flashing

### Using Arduino IDE

1. Open `ESP32S.ino` in Arduino IDE
2. Select board: **ESP32S3 Dev Module**
3. Select partition scheme: **8MB Flash (3MB APP / 1.5MB SPIFFS)** or similar
4. Configure flash size and PSRAM settings according to your board
5. Click **Upload**

### Using PlatformIO (Alternative)

If you prefer PlatformIO, create a `platformio.ini`:

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
lib_deps = 
    adafruit/Adafruit GFX Library
    adafruit/Adafruit ST7735 and ST7789 Library
    bodmer/TJpg_Decoder
```

## Usage Guide

### Initial Setup

1. **Power On**: Connect USB power to ESP32-S3
2. **Camera Preview**: The TFT display shows live camera preview
3. **Status Bar**: Top bar shows WiFi status and IP address
4. **First-time WiFi**:
   - If no saved WiFi credentials, device enters AP mode
   - Connect to `ESP32-S3-CAM-Setup` (password: `12345678`)
   - Access web interface at `http://192.168.4.1`

### Controls

#### Physical Button (IO0)

- **Short Press**: Capture photo

#### Joystick

- **Up/Down**: Navigate menu items
- **Left/Right**: Adjust values
- **Press**: Enter/confirm selection

#### Menu System

Access menu by pressing joystick button:

- **Camera Settings**: Brightness, contrast, saturation, white balance
- **Resolution**: Select photo resolution (VGA to UXGA)
- **Quality**: JPEG quality setting
- **Gallery**: Browse saved photos
- **Motion Settings**: Enable/disable motion detection, adjust sensitivity
- **Timelapse Settings**: Configure interval and enable/disable
- **WiFi Info**: View current connection status and IP
- **About**: Device information

### Web Interface

Access via browser at `http://<device-ip>`:

- **Live Stream**: View real-time camera feed
- **Photo Capture**: Trigger photo capture remotely
- **Settings**: Configure camera parameters
- **Gallery**: View and download saved photos

## Configuration

All settings are persisted in ESP32's non-volatile storage (Preferences). Key configurations in `camera_config.h`:

```cpp
// WiFi
#define DEFAULT_WIFI_SSID ""           // Default WiFi SSID
#define DEFAULT_WIFI_PASS ""           // Default WiFi password
#define AP_SSID "ESP32-S3-CAM-Setup"   // Access point SSID
#define AP_PASS "12345678"             // Access point password

// Camera
#define PREVIEW_FRAME_SIZE FRAMESIZE_240X240  // Preview resolution
#define PHOTO_FRAME_SIZE FRAMESIZE_UXGA       // Photo resolution
#define PHOTO_QUALITY 10                      // JPEG quality (lower = better)

// Motion Detection
#define MOTION_THRESHOLD 15                  // Pixel difference threshold
#define MOTION_MIN_FRAMES 3                  // Consecutive frames required

// Timelapse
#define TIMELAPSE_MIN_INTERVAL_MS 5000       // Minimum interval (5 seconds)

// NTP Time Sync
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC (8 * 3600)            // UTC+8
```

## Advanced Features

### Dual-Core Architecture

The project utilizes ESP32-S3's dual-core capability:

- **Core 1**: Camera preview rendering and motion detection
- **Core 0**: Main loop, menu handling, web server, OTA

### Motion Detection Algorithm

Uses frame-by-frame RGB565 comparison with configurable threshold and minimum frame count to reduce false positives.

### Timelapse Mode

Captures photos at configurable intervals. Minimum interval is 5 seconds to ensure reliable SD card writes.

### OTA Updates

Firmware updates can be pushed over WiFi using ArduinoOTA. Ensure sufficient flash space is available.

## Troubleshooting

### Camera Not Initializing

- Check camera wiring and pin configuration
- Verify PSRAM is enabled in board settings
- Ensure adequate power supply (500mA+ recommended)

### SD Card Not Detected

- Verify SD card is formatted (FAT32 recommended)
- Check SD\_MMC pin connections
- Try 1-bit mode if 4-bit mode fails

### WiFi Connection Issues

- Reset WiFi settings via menu
- Ensure router is within range
- Check for IP conflicts

### Display Not Working

- Verify SPI pin connections
- Check display initialization in `tft_display.cpp`
- Ensure backlight pin (TFT\_BL) is configured correctly

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs, feature requests, or improvements.

### Development Guidelines

- Follow existing code style and naming conventions
- Add comments for complex logic
- Test hardware changes on actual ESP32-S3 device
- Update documentation for new features

## Acknowledgments

- ESP32-Arduino core team
- Adafruit for GFX and display libraries
- TJpg\_Decoder library contributors
- Open-source hardware community

## Contact

For questions or support, please open an issue on the project repository.
