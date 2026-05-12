/**
 * Camera Driver Implementation
 * 
 * Handles low-level camera initialization, configuration, and frame management.
 * Uses ESP-IDF camera driver with support for multiple operating modes.
 */

#include "camera_driver.h"

// Current camera state tracking
static CameraMode currentCameraMode = CameraMode::PREVIEW_RGB565;
static framesize_t currentFrameSize = PREVIEW_FRAME_SIZE;
static volatile bool cameraLocked = false;

/**
 * Populate camera configuration structure based on operating mode
 * Configures GPIO pins, pixel format, frame size, and buffer count
 */
static void fillCameraConfig(camera_config_t &config, CameraMode mode) {
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    // Configure mode-specific settings
    switch (mode) {
        case CameraMode::PREVIEW_RGB565:
            config.pixel_format = PIXFORMAT_RGB565;
            config.frame_size = FRAMESIZE_240X240;
            config.fb_count = 2;
            break;
        case CameraMode::PHOTO_JPEG:
            config.pixel_format = PIXFORMAT_JPEG;
            config.frame_size = currentFrameSize;
            config.jpeg_quality = PHOTO_QUALITY;
            config.fb_count = 1;
            break;
        case CameraMode::VIDEO_JPEG:
            config.pixel_format = PIXFORMAT_JPEG;
            config.frame_size = FRAMESIZE_240X240;
            config.jpeg_quality = 12;
            config.fb_count = 2;
            break;
        case CameraMode::STREAM_JPEG:
            config.pixel_format = PIXFORMAT_JPEG;
            config.frame_size = FRAMESIZE_VGA;
            config.jpeg_quality = 10;
            config.fb_count = 2;
            break;
    }
}

/**
 * Initialize camera hardware with specified operating mode
 * Applies default image settings after successful initialization
 */
bool cameraInit(CameraMode mode) {
    camera_config_t config;
    fillCameraConfig(config, mode);

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return false;
    }

    currentCameraMode = mode;

    // Apply default image sensor settings
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
        s->set_special_effect(s, 0);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_wb_mode(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 0);
        s->set_ae_level(s, 0);
        s->set_aec_value(s, 300);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)0);
        s->set_bpc(s, 0);
        s->set_wpc(s, 1);
        s->set_raw_gma(s, 1);
        s->set_lenc(s, 1);
        s->set_hmirror(s, 0);
        s->set_vflip(s, 0);
        s->set_dcw(s, 1);
        s->set_colorbar(s, 0);
    }

    return true;
}

/**
 * Deinitialize camera and release hardware resources
 */
void cameraDeinit() {
    esp_camera_deinit();
}

/**
 * Switch camera operating mode by deinitializing and reinitializing
 * Returns true if mode switch successful
 */
bool cameraSwitchMode(CameraMode newMode) {
    if (newMode == currentCameraMode) return true;
    cameraDeinit();
    delay(100);
    bool ok = cameraInit(newMode);
    if (ok) {
        delay(100);
    }
    return ok;
}

/**
 * Capture single frame from camera (blocking)
 * Returns frame buffer pointer or NULL on failure
 */
camera_fb_t* cameraCaptureFrame() {
    return esp_camera_fb_get();
}

/**
 * Return frame buffer to camera driver pool after processing
 */
void cameraReturnFrame(camera_fb_t *fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

/**
 * Apply image parameters to camera sensor
 * Parameters: brightness (-2 to 2), contrast (-2 to 2), 
 * saturation (-2 to 2), white balance mode (0-4)
 */
void cameraApplySettings(int brightness, int contrast, int saturation, int wbMode) {
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, brightness);
        s->set_contrast(s, contrast);
        s->set_saturation(s, saturation);
        s->set_wb_mode(s, wbMode);
    }
}

/**
 * Set photo capture frame size
 */
void cameraSetFrameSize(framesize_t size) {
    currentFrameSize = size;
}

/**
 * Enable/disable horizontal mirroring
 */
void cameraSetHmirror(bool enable) {
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_hmirror(s, enable);
}

/**
 * Enable/disable vertical flipping
 */
void cameraSetVflip(bool enable) {
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_vflip(s, enable);
}

/**
 * Get current frame size setting
 */
framesize_t cameraGetCurrentFrameSize() {
    return currentFrameSize;
}

/**
 * Get current camera operating mode
 */
CameraMode cameraGetCurrentMode() {
    return currentCameraMode;
}

/**
 * Lock camera to prevent concurrent access
 */
void cameraLock() {
    cameraLocked = true;
}

/**
 * Unlock camera to allow access
 */
void cameraUnlock() {
    cameraLocked = false;
}

/**
 * Check if camera is currently locked
 */
bool cameraIsLocked() {
    return cameraLocked;
}
