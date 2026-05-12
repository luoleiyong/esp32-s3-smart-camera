/**
 * TFT Display Implementation
 * 
 * Implements all display rendering functions including camera preview,
 * UI elements, JPEG decoding, and Chinese character rendering.
 */

#include "tft_display.h"
#include "camera_driver.h"
#include "chinese_font.h"
#include <WiFi.h>
#include <SD_MMC.h>

// TFT display instance with CS, DC, and RST pins
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// Display offset for positioning JPEG output
int16_t display_offset_x = 0;
int16_t display_offset_y = 0;

/**
 * TJpgDec callback function for rendering decoded JPEG blocks
 * Handles clipping and positioning for blocks partially outside display area
 */
bool tftJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
    int16_t pos_x = x + display_offset_x;
    int16_t pos_y = y + display_offset_y;

    if (pos_x >= 240 || pos_y >= 240) return 1;
    if (pos_x < 0) { w += pos_x; pos_x = 0; }
    if (pos_y < 0) { h += pos_y; pos_y = 0; }
    if (pos_x + w > 240) w = 240 - pos_x;
    if (pos_y + h > 240) h = 240 - pos_y;

    if (w <= 0 || h <= 0) return 1;

    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            tft.drawPixel(pos_x + i, pos_y + j, bitmap[j * w + i]);
        }
    }
    return 1;
}

/**
 * Swap RGB565 byte order for little-endian to big-endian conversion
 * Required because camera sensor outputs RGB565 in different byte order than TFT expects
 */
uint16_t swapRGB565(uint16_t color) {
    return ((color & 0xFF) << 8) | ((color >> 8) & 0xFF);
}

/**
 * Swap byte order for entire RGB565 buffer
 * Used to convert camera frame data before rendering to TFT
 */
void swapRGB565Buffer(uint16_t *buffer, int32_t length) {
    for (int32_t i = 0; i < length; i++) {
        buffer[i] = swapRGB565(buffer[i]);
    }
}

/**
 * Initialize TFT display hardware
 * Configures SPI interface, sets display rotation, and initializes JPEG decoder
 */
bool tftInit() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    delay(100);

    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_RST, OUTPUT);

    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_RST, HIGH);
    delay(100);

    digitalWrite(TFT_RST, LOW);
    delay(100);
    digitalWrite(TFT_RST, HIGH);
    delay(100);

    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.init(240, 240);
    tft.setSPISpeed(80000000);
    tft.setRotation(2);
    tft.fillScreen(ST77XX_BLACK);

    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tftJpgOutput);

    return true;
}

/**
 * Clear entire display to black
 */
void tftClear() {
    tft.fillScreen(ST77XX_BLACK);
}

/**
 * Draw UI frame including top status bar and bottom button area
 * Renders "[IO0] Photo" hint and placeholder for IP address
 */
void tftDrawUIFrame() {
    tft.fillScreen(ST77XX_BLACK);

    // Clear top status bar area (y: 0-29)
    tft.fillRect(0, 0, 240, 30, ST77XX_BLACK);
    tft.drawFastHLine(0, 29, 240, ST77XX_BLACK);

    // Clear bottom area and draw divider line (y: 184-240)
    tft.drawFastHLine(0, 184, 240, ST77XX_BLACK);
    tft.fillRect(0, 185, 240, 55, ST77XX_BLACK);

    // Draw photo capture button hint
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(55, 200);
    tft.print("[IO0]");
    tft.setTextSize(2);
    tft.setCursor(90, 197);
    tft.print("Photo");

    // Draw IP address placeholder
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 220);
    tft.print("IP: ---.---.---.---");
}

/**
 * Update status bar with current time, WiFi state, and IP address
 * Shows time in HH:MM:SS format, WiFi icon (green/red), and device IP
 */
void tftUpdateStatusBar(bool wifiConnected, const char *ipStr) {
    // Clear previous time display
    tft.fillRect(60, 5, 120, 20, ST77XX_BLACK);

    // Get and display current time from RTC
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        tft.setTextColor(ST77XX_WHITE);
        tft.setTextSize(2);
        tft.setCursor(75, 7);
        tft.printf("%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }

    // Draw WiFi connection indicator
    tft.setTextSize(1);
    if (wifiConnected) {
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(8, 12);
        tft.print("WiFi");
    } else {
        tft.setTextColor(ST77XX_RED);
        tft.setCursor(8, 12);
        tft.print("----");
    }

    // Update IP address display at bottom
    tft.fillRect(35, 220, 200, 12, ST77XX_BLACK);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 220);
    if (wifiConnected && ipStr) {
        tft.print("IP: ");
        tft.print(ipStr);
    } else {
        tft.print("IP: Not connected");
    }
}

/**
 * Display camera preview frame (RGB565 format)
 * Captures frame, adjusts byte order, and renders centered on display
 */
void tftShowPreviewRGB565() {
    camera_fb_t *fb = cameraCaptureFrame();
    if (!fb) return;

    const int16_t PREVIEW_X = 0;
    const int16_t PREVIEW_Y = 30;
    const int16_t PREVIEW_W = 240;
    const int16_t PREVIEW_H = 155;

    // Clear preview area on first draw
    static bool firstDraw = true;
    if (firstDraw) {
        tft.fillRect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, ST77XX_BLACK);
        firstDraw = false;
    }

    if (fb->format == PIXFORMAT_RGB565) {
        int16_t img_w = fb->width;
        int16_t img_h = fb->height;

        // Clip image dimensions to preview area
        if (img_h > PREVIEW_H) img_h = PREVIEW_H;
        if (img_w > PREVIEW_W) img_w = PREVIEW_W;

        // Center image within preview area
        int16_t x = PREVIEW_X + (PREVIEW_W - img_w) / 2;
        int16_t y = PREVIEW_Y + (PREVIEW_H - img_h) / 2;

        int32_t pixel_count = img_w * img_h;
        uint16_t *rgb_buffer = (uint16_t *)fb->buf;

        // Convert byte order and render
        swapRGB565Buffer(rgb_buffer, pixel_count);
        tft.drawRGBBitmap(x, y, rgb_buffer, img_w, img_h);
    }

    cameraReturnFrame(fb);
}

/**
 * Display camera preview frame (JPEG format with TJpgDec decoding)
 * Falls back to direct RGB565 rendering if frame is not JPEG
 */
void tftShowPreviewJPEG() {
    camera_fb_t *fb = cameraCaptureFrame();
    if (!fb) return;

    const int16_t PREVIEW_X = 0;
    const int16_t PREVIEW_Y = 30;
    const int16_t PREVIEW_W = 240;
    const int16_t PREVIEW_H = 155;

    tft.fillRect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, ST77XX_BLACK);

    if (fb->format == PIXFORMAT_JPEG) {
        display_offset_x = PREVIEW_X;
        display_offset_y = PREVIEW_Y;
        TJpgDec.drawJpg(0, 0, fb->buf, fb->len);
    } else if (fb->format == PIXFORMAT_RGB565) {
        int16_t img_w = fb->width;
        int16_t img_h = fb->height;
        int16_t x = PREVIEW_X + (PREVIEW_W - img_w) / 2;
        int16_t y = PREVIEW_Y + (PREVIEW_H - img_h) / 2;
        tft.drawRGBBitmap(x, y, (uint16_t *)fb->buf, img_w, img_h);
    }

    cameraReturnFrame(fb);
}

/**
 * Show shutter flash animation (white flash then black)
 * Provides visual feedback during photo capture
 */
void tftShutterAnimation() {
    const int16_t PREVIEW_X = 0;
    const int16_t PREVIEW_Y = 30;
    const int16_t PREVIEW_W = 240;
    const int16_t PREVIEW_H = 155;

    tft.fillRect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, ST77XX_WHITE);
    delay(50);
    tft.fillRect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, ST77XX_BLACK);
    delay(50);
}

/**
 * Display centered message with specified color and optional delay
 * Used for showing temporary status messages
 */
void tftShowMessage(const char *msg, uint16_t color, int delayMs) {
    tft.fillRect(50, 90, 140, 50, ST77XX_BLACK);
    tft.setTextColor(color);
    tft.setTextSize(2);
    tft.setCursor(65, 105);
    tft.println(msg);
    if (delayMs > 0) delay(delayMs);
}

/**
 * Show photo saved confirmation with green "Saved!" text and filename
 * Auto-dismisses after 3 seconds
 */
void tftShowPhotoSaved(const char *filename) {
    tft.fillRect(30, 80, 180, 70, ST77XX_BLACK);
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(2);
    tft.setCursor(60, 95);
    tft.println("Saved!");
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(40, 125);
    tft.println(filename);
    delay(3000);
}

/**
 * Draw gallery navigation info showing current/total photo count
 * and "L/R Browse" hint for joystick navigation
 */
void tftDrawGalleryInfo(int current, int total) {
    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(1);
    tft.setCursor(10, 210);
    tft.printf("%d/%d", current + 1, total);
    tft.setCursor(120, 210);
    tft.println("L/R Browse");
}

/**
 * Display a specific photo from SD card by filename
 * Loads JPEG into PSRAM, decodes with TJpgDec, and renders to preview area
 */
void tftDisplayPhoto(int index, const String &filename) {
    const int16_t PREVIEW_X = 5;
    const int16_t PREVIEW_Y = 40;
    const int16_t PREVIEW_W = 230;
    const int16_t PREVIEW_H = 160;

    tft.fillRect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, ST77XX_BLACK);

    // Open photo file from SD card
    fs::FS &fs = SD_MMC;
    File file = fs.open("/" + filename);
    if (!file) {
        tft.setTextColor(ST77XX_RED);
        tft.setTextSize(1);
        tft.setCursor(PREVIEW_X + 10, PREVIEW_Y + 70);
        tft.println("File error!");
        return;
    }

    // Allocate PSRAM buffer for JPEG data
    size_t fileSize = file.size();
    uint8_t *jpgBuffer = (uint8_t *)ps_malloc(fileSize);
    if (!jpgBuffer) {
        tft.setTextColor(ST77XX_RED);
        tft.setTextSize(1);
        tft.setCursor(PREVIEW_X + 10, PREVIEW_Y + 70);
        tft.println("Memory error!");
        file.close();
        return;
    }

    // Read file and decode JPEG with scale factor 8 for faster display
    file.read(jpgBuffer, fileSize);
    file.close();

    display_offset_x = PREVIEW_X;
    display_offset_y = PREVIEW_Y;
    TJpgDec.setJpgScale(8);
    TJpgDec.drawJpg(0, 0, jpgBuffer, fileSize);

    free(jpgBuffer);
}

/**
 * Draw a single 16x16 Chinese character at specified position
 * Reads character bitmap from chineseFont array in program memory
 * Parameters: x, y position; character index; color
 */
void drawChineseChar(int16_t x, int16_t y, ChineseCharIndex index, uint16_t color) {
    if (index >= sizeof(chineseFont) / sizeof(chineseFont[0])) return;

    unsigned char buffer[32];
    memcpy_P(buffer, chineseFont[index], 32);

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            int byteIndex = col / 8;
            int bitIndex = 7 - (col % 8);
            if (!(buffer[row * 2 + byteIndex] & (1 << bitIndex))) {
                tft.drawPixel(x + col, y + row, color);
            }
        }
    }
}

/**
 * Draw a sequence of Chinese characters in a row
 * Parameters: starting position; array of character indices; count; color; spacing
 */
void drawChineseWord(int16_t x, int16_t y, const ChineseCharIndex *chars, int count, uint16_t color, int16_t spacing) {
    for (int i = 0; i < count; i++) {
        drawChineseChar(x + i * (16 + spacing), y, chars[i], color);
    }
}
