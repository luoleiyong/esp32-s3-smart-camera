/**
 * Camera Hardware Configuration
 * 
 * Defines all GPIO pin assignments, camera parameters, WiFi settings,
 * and system-wide configuration constants for the ESP32-S3 smart camera.
 * 
 * Hardware: ESP32-S3-EYE development board with OV2640 camera module
 */

#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include <Arduino.h>
#include <esp_camera.h>

// Target camera board model
#define CAMERA_MODEL_ESP32S3_EYE

// Camera module SCCB (I2C-like) and data pins
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

// TFT display SPI interface pins
#define TFT_CS            1
#define TFT_DC            19
#define TFT_RST           20
#define TFT_MOSI          21
#define TFT_SCLK          47
#define TFT_BL            45

// TFT SPI 时钟：实测 27MHz 在本模块上出现整屏白光（数据发不进屏），
// 回退到 20MHz（已验证稳定）。若换用短排线/质量更好的模块，可再往上试 27MHz
#define TFT_SPI_FREQ_HZ   20000000

// Physical button pin (IO0 on most ESP32-S3 boards)
#define BUTTON_PIN        0

// Onboard RGB LED (WS2812) pin
#define LED_RGB_PIN       48

// Joystick analog and digital pins
#define JOYSTICK_VRX      14
#define JOYSTICK_VRY      3
#define JOYSTICK_SW       46

// SD card SD_MMC interface pins (1-bit mode)
#define SD_MMC_CLK        39
#define SD_MMC_CMD        38
#define SD_MMC_D0         40

// NTP time synchronization settings (UTC+8)
#define NTP_SERVER        "pool.ntp.org"
#define GMT_OFFSET_SEC    (8 * 3600)
#define DAYLIGHT_OFFSET_SEC 0

// Camera frame sizes and quality settings
#define PREVIEW_FRAME_SIZE FRAMESIZE_128X128
#define PREVIEW_QUALITY   12
#define PHOTO_FRAME_SIZE  FRAMESIZE_UXGA
#define PHOTO_QUALITY     10

// Preview refresh interval (ms): higher = lower frame rate, less heat,
// longer sensor exposure time (brighter image in low light)
#define PREVIEW_INTERVAL_MS 66

// WiFi connection defaults
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASS ""

// Access point mode credentials
#define AP_SSID           "ESP32-S3-CAM-Setup"
#define AP_PASS           "12345678"

// mDNS/Bonjour hostname
#define MDNS_HOSTNAME     "esp32s3cam"

#endif
