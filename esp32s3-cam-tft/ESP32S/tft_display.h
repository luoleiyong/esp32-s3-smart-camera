/**
 * TFT Display Interface
 * 
 * Provides functions for rendering camera preview, UI elements,
 * and messages on the ST77XX-based TFT display. Includes support
 * for JPEG decoding and Chinese character rendering.
 */

#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <TJpg_Decoder.h>
#include <SPI.h>
#include "camera_config.h"
#include "chinese_font.h"

// TFT display instance and offset configuration
extern Adafruit_ST7789 tft;
extern int16_t display_offset_x;
extern int16_t display_offset_y;

// Initialize TFT display hardware and configure SPI interface
bool tftInit();

// Clear display to black background
void tftClear();

// Draw UI frame including status bar and button hints
void tftDrawUIFrame();

// Update status bar with WiFi connection state and IP address
void tftUpdateStatusBar(bool wifiConnected, const char *ipStr);

// Display camera preview frame (RGB565 format)
void tftShowPreviewRGB565();

// Display camera preview frame (JPEG format with decoding)
void tftShowPreviewJPEG();

// Show shutter flash animation during photo capture
void tftShutterAnimation();

// Display centered message with specified color and auto-dismiss delay
void tftShowMessage(const char *msg, uint16_t color, int delayMs);

// Show photo saved confirmation with filename
void tftShowPhotoSaved(const char *filename);

// Draw gallery navigation info showing current/total photo count
void tftDrawGalleryInfo(int current, int total);

// Display a specific photo from SD card by filename and index
void tftDisplayPhoto(int index, const String &filename);

// TJpgDec callback function for rendering JPEG blocks to display
bool tftJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);

// Swap RGB565 byte order (little-endian to big-endian conversion)
uint16_t swapRGB565(uint16_t color);

// Swap byte order for entire RGB565 buffer
void swapRGB565Buffer(uint16_t *buffer, int32_t length);

#endif
