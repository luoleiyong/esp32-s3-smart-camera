/**
 * Photo Capture Implementation
 * 
 * Handles complete photo capture workflow including camera mode switching,
 * warmup capture, image capture, and SD card file writing.
 * Uses timestamp-based filenames or sequential counter if NTP not synced.
 */

#include "photo_capture.h"
#include "camera_driver.h"
#include "config_manager.h"
#include "tft_display.h"
#include <time.h>

/**
 * Capture photo and save to SD card
 * Workflow: switch to JPEG mode -> warmup capture -> real capture -> write to SD -> restore preview mode
 * Returns true if photo successfully saved
 */
bool photoCapture(const char *customPath) {
    Serial.println("[PHOTO] === START ===");

    DeviceConfig cfg = configGet();

    Serial.println("[PHOTO] Switching to JPEG mode...");
    if (!cameraSwitchMode(CameraMode::PHOTO_JPEG)) {
        Serial.println("[PHOTO] Mode switch FAILED!");
        tftShowMessage("Cam Err!", ST77XX_RED, 2000);
        cameraSwitchMode(CameraMode::PREVIEW_RGB565);
        return false;
    }
    Serial.println("[PHOTO] Mode switch OK");

    delay(100);

    // Apply camera settings before capture
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_wb_mode(s, 1);
        s->set_brightness(s, cfg.brightness);
        s->set_contrast(s, cfg.contrast);
        s->set_saturation(s, cfg.saturation);
    }
    delay(150);

    Serial.println("[PHOTO] Warmup capture...");
    camera_fb_t *tempFb = cameraCaptureFrame();
    if (tempFb) {
        Serial.printf("[PHOTO] Warmup: %u bytes, fmt=%d\n", tempFb->len, tempFb->format);
        cameraReturnFrame(tempFb);
    } else {
        Serial.println("[PHOTO] Warmup frame NULL");
    }
    delay(100);

    Serial.println("[PHOTO] Real capture...");
    camera_fb_t *fb = cameraCaptureFrame();
    if (!fb) {
        Serial.println("[PHOTO] Capture FAILED - NULL frame!");
        tftShowMessage("Cap Err!", ST77XX_RED, 2000);
        cameraSwitchMode(CameraMode::PREVIEW_RGB565);
        return false;
    }
    Serial.printf("[PHOTO] Captured: %u bytes, fmt=%d\n", fb->len, fb->format);

    if (fb->format != PIXFORMAT_JPEG) {
        Serial.println("[PHOTO] Wrong format - not JPEG!");
        cameraReturnFrame(fb);
        tftShowMessage("Fmt Err!", ST77XX_RED, 2000);
        cameraSwitchMode(CameraMode::PREVIEW_RGB565);
        return false;
    }

    // Generate filename: timestamp-based or sequential counter
    char filename[64];
    if (customPath) {
        strncpy(filename, customPath, sizeof(filename) - 1);
    } else {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            strftime(filename, sizeof(filename), "/photo_%Y%m%d_%H%M%S.jpg", &timeinfo);
        } else {
            static unsigned long photoCounter = 0;
            photoCounter++;
            snprintf(filename, sizeof(filename), "/photo_%lu.jpg", photoCounter);
        }
    }

    Serial.printf("[PHOTO] Saving to: %s\n", filename);
    File file = SD_MMC.open(filename, FILE_WRITE);
    if (file) {
        size_t written = file.write(fb->buf, fb->len);
        file.close();
        Serial.printf("[PHOTO] Written: %u/%u bytes\n", written, fb->len);

        if (written != fb->len) {
            Serial.println("[PHOTO] Write size mismatch!");
        }
    } else {
        Serial.println("[PHOTO] File open FAILED!");
        cameraReturnFrame(fb);
        tftShowMessage("SD Err!", ST77XX_RED, 2000);
        cameraSwitchMode(CameraMode::PREVIEW_RGB565);
        return false;
    }

    cameraReturnFrame(fb);

    Serial.println("[PHOTO] Restoring preview mode...");
    cameraSwitchMode(CameraMode::PREVIEW_RGB565);

    Serial.println("[PHOTO] === DONE ===");
    return true;
}

/**
 * List all .jpg files on SD card
 * Populates list array with filenames, sets count to number found
 */
void photoList(String *list, int *count, int maxCount) {
    *count = 0;
    File root = SD_MMC.open("/");
    File file = root.openNextFile();

    while (file && *count < maxCount) {
        if (!file.isDirectory()) {
            String name = file.name();
            if (name.endsWith(".jpg")) {
                list[*count] = name;
                (*count)++;
            }
        }
        file = root.openNextFile();
    }
}

/**
 * Delete photo file from SD card
 */
bool photoDelete(const char *filename) {
    String path = "/" + String(filename);
    return SD_MMC.remove(path);
}

/**
 * Check if photo file exists on SD card
 */
bool photoExists(const char *filename) {
    String path = "/" + String(filename);
    File f = SD_MMC.open(path, FILE_READ);
    if (f) {
        f.close();
        return true;
    }
    return false;
}
