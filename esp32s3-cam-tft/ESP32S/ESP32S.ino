/**
 * ESP32-S3 Smart Camera - Main Entry Point
 * 
 * This is the main Arduino sketch that orchestrates all camera subsystems:
 * - Camera preview and capture
 * - TFT display and UI rendering
 * - Joystick input handling
 * - WiFi connectivity and web server
 * - Motion detection and timelapse photography
 * - Menu navigation system
 * 
 * Architecture: Dual-core design with camera preview running on Core 1
 * and main control loop running on Core 0 for optimal performance.
 */

#include "camera_config.h"
#include "camera_driver.h"
#include "tft_display.h"
#include "joystick.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "ota_update.h"
#include "photo_capture.h"
#include "motion_detect.h"
#include "timelapse.h"
#include "menu_system.h"
#include <SD_MMC.h>
#include <WiFi.h>

// FreeRTOS task handle for camera preview task (runs on Core 1)
TaskHandle_t cameraTaskHandle = NULL;
volatile bool cameraPaused = false;
static volatile bool btnPressed = false;
static volatile unsigned long btnPressTime = 0;
static volatile bool capturing = false;

/**
 * Hardware interrupt handler for physical button (IO0)
 * Records button press event and timestamp for debouncing in main loop
 */
void IRAM_ATTR onButtonPress() {
    if (digitalRead(BUTTON_PIN) == LOW && !capturing) {
        btnPressed = true;
        btnPressTime = millis();
    }
}

/**
 * Core 1 Task: Camera Preview and Motion Detection
 * Continuously renders camera frames to TFT display when in preview mode.
 * Performs motion detection analysis on captured frames and triggers
 * automatic photo capture when motion is detected.
 */
static void cameraTask(void *parameter) {
    for (;;) {
        if (currentMenuState == MenuState::PREVIEW && !cameraPaused && !cameraIsLocked()) {
            tftShowPreviewRGB565();

            if (motionIsEnabled() && cameraGetCurrentMode() == CameraMode::PREVIEW_RGB565 && !cameraPaused && !cameraIsLocked()) {
                camera_fb_t *fb = cameraCaptureFrame();
                if (fb && fb->format == PIXFORMAT_RGB565) {
                    if (motionDetect(fb)) {
                        Serial.println("Motion! Capturing...");
                        cameraPaused = true;
                        vTaskDelay(pdMS_TO_TICKS(100));
                        tftShutterAnimation();
                        photoCapture();
                        cameraPaused = false;
                    }
                }
                if (fb) cameraReturnFrame(fb);
            }
            vTaskDelay(pdMS_TO_TICKS(25));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/**
 * Start dual-core task distribution
 * Creates camera preview task pinned to Core 1
 */
static void startDualCore() {
    xTaskCreatePinnedToCore(cameraTask, "CameraTask", 8192, NULL, 1, &cameraTaskHandle, 1);
}

/**
 * Stop camera preview and deinitialize camera hardware
 * Used when entering menu system to free resources
 */
void stopPreviewAndCamera() {
    cameraPaused = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    cameraDeinit();
    Serial.println("[CAM] Deinit for menu");
}

/**
 * Reinitialize camera for preview mode with saved configuration
 * Applies user settings (brightness, contrast, etc.) from persistent storage
 */
void startPreviewAndCamera() {
    cameraInit(CameraMode::PREVIEW_RGB565);
    DeviceConfig cfg = configGet();
    cameraApplySettings(cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode);
    cameraSetHmirror(cfg.hmirror);
    cameraSetVflip(cfg.vflip);
    cameraPaused = false;
    Serial.println("[CAM] Reinit for preview");
}

/**
 * Pause camera preview and lock frame buffer before photo capture
 * Ensures exclusive access to camera hardware during capture
 */
void pauseForCapture() {
    cameraPaused = true;
    cameraLock();
    vTaskDelay(pdMS_TO_TICKS(150));
}

/**
 * Resume camera preview after photo capture completes
 */
void resumeAfterCapture() {
    cameraUnlock();
    cameraPaused = false;
}

/**
 * Arduino Setup Function
 * Initializes all hardware subsystems and starts dual-core operation
 */
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== ESP32-S3 Smart Camera ===");

    // Configure hardware button interrupt
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

    // Initialize configuration storage and joystick input
    configInit();
    joystickInit();

    // Initialize camera hardware in preview mode
    if (!cameraInit(CameraMode::PREVIEW_RGB565)) {
        Serial.println("Camera FAIL");
        while (1);
    }

    // Apply saved camera settings from persistent storage
    DeviceConfig cfg = configGet();
    cameraApplySettings(cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode);
    cameraSetFrameSize(cfg.photoFrameSize);
    cameraSetHmirror(cfg.hmirror);
    cameraSetVflip(cfg.vflip);

    // Initialize TFT display
    if (!tftInit()) { Serial.println("TFT FAIL"); while (1); }

    // Initialize SD card storage (SD_MMC 1-bit mode)
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    if (!SD_MMC.begin("/sdcard", true)) {
        tftShowMessage("SD Err!", ST77XX_RED, 3000);
    }

    // Initialize WiFi connectivity and web server
    bool wifiOk = wifiInit();
    if (wifiOk) {
        Serial.printf("WiFi: %s\n", wifiGetIP().c_str());
        webServerInit();
        otaInit();
    } else if (wifiGetState() == WiFiState::AP_MODE) {
        Serial.printf("AP: %s\n", wifiGetIP().c_str());
    }

    // Initialize motion detection and timelapse systems
    motionInit();
    timelapseInit();
    if (cfg.timelapseEnabled) timelapseStart(cfg.timelapseInterval);

    // Start dual-core camera preview task
    startDualCore();
    tftDrawUIFrame();
    tftUpdateStatusBar(wifiIsConnected(), wifiGetIP().c_str());
    menuInit();
    Serial.println("Ready!");
}

/**
 * Arduino Main Loop
 * Handles menu navigation, OTA updates, status bar refresh, and button capture
 */
void loop() {
    menuHandleNavigation();

    if (wifiIsConnected()) {
        otaHandle();
    } else if (wifiGetState() == WiFiState::AP_MODE) {
        wifiHandleAPClient();
    }

    if (currentMenuState == MenuState::PREVIEW) {
        static unsigned long lastStatusUpdate = 0;
        if (millis() - lastStatusUpdate > 1000) {
            tftUpdateStatusBar(wifiIsConnected(), wifiGetIP().c_str());
            lastStatusUpdate = millis();
        }
        timelapseUpdate();
    }

    // Handle button press for photo capture (with 80ms debounce)
    if (btnPressed && (millis() - btnPressTime > 80) && !capturing) {
        btnPressed = false;
        capturing = true;
        Serial.println("[BTN] IO0 pressed!");

        if (webServerIsStreaming()) {
            Serial.println("[BTN] Stop stream first...");
            webServerStopStream();
            delay(800);
        }

        pauseForCapture();
        Serial.println("[BTN] Capturing...");

        tftShutterAnimation();
        bool ok = photoCapture();

        if (ok) {
            Serial.println("[BTN] Photo saved OK!");
            tftShowPhotoSaved("OK!");
        } else {
            Serial.println("[BTN] Photo FAILED!");
            tftShowMessage("Fail!", ST77XX_RED, 2000);
        }

        tftDrawUIFrame();
        tftUpdateStatusBar(wifiIsConnected(), wifiGetIP().c_str());

        resumeAfterCapture();
        capturing = false;
    }
}
