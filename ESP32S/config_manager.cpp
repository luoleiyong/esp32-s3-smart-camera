/**
 * Configuration Manager Implementation
 * 
 * Handles persistent storage of device settings using ESP32 Preferences API.
 * Provides load/save operations for WiFi credentials, camera parameters,
 * and image enhancement (zoom/exposure/anti-shake/scene) settings.
 */

#include "config_manager.h"
#include "camera_config.h"

static Preferences prefs;
static DeviceConfig cfg;

/**
 * Initialize preferences storage and load saved settings
 * Opens "esp32cam" namespace in read-write mode
 */
void configInit() {
    prefs.begin("esp32cam", false);
    configLoad();
}

/**
 * Load all settings from non-volatile storage
 * Uses default values from camera_config.h if no saved values exist
 */
void configLoad() {
    cfg.brightness = prefs.getInt("brightness", 0);
    cfg.contrast = prefs.getInt("contrast", 0);
    cfg.saturation = prefs.getInt("saturation", 0);
    cfg.wbMode = prefs.getInt("wbMode", 0);
    cfg.photoFrameSize = (framesize_t)prefs.getInt("frameSize", (int)PHOTO_FRAME_SIZE);
    cfg.photoQuality = prefs.getInt("quality", PHOTO_QUALITY);
    cfg.hmirror = prefs.getBool("hmirror", false);
    cfg.vflip = prefs.getBool("vflip", false);
    cfg.zoomLevel = prefs.getInt("zoom", 1);
    cfg.exposureMode = prefs.getInt("expMode", 0);
    cfg.shutterValue = prefs.getInt("shutter", 300);
    cfg.gainValue = prefs.getInt("gain", 0);
    cfg.antiShake = prefs.getBool("antishake", false);
    cfg.sceneMode = prefs.getInt("scene", 0);
    cfg.shotDelaySec = prefs.getInt("shotDelay", 0);
    cfg.watermark = prefs.getBool("watermark", false);

    // 高级功能（默认关闭，保证升级后行为不变）
    cfg.previewJpeg = prefs.getBool("prevJpeg", false);
    cfg.hwZoom = prefs.getBool("hwZoom", false);
    cfg.uploadAuto = prefs.getBool("upAuto", false);
    cfg.videoFps = prefs.getInt("vidFps", 10);
    String upUrl = prefs.getString("upUrl", "");
    strncpy(cfg.uploadUrl, upUrl.c_str(), sizeof(cfg.uploadUrl) - 1);
    cfg.uploadUrl[sizeof(cfg.uploadUrl) - 1] = '\0';

    String ssid = prefs.getString("wifiSSID", DEFAULT_WIFI_SSID);
    String pass = prefs.getString("wifiPass", DEFAULT_WIFI_PASS);
    strncpy(cfg.wifiSSID, ssid.c_str(), sizeof(cfg.wifiSSID) - 1);
    strncpy(cfg.wifiPassword, pass.c_str(), sizeof(cfg.wifiPassword) - 1);
    cfg.wifiSSID[sizeof(cfg.wifiSSID) - 1] = '\0';
    cfg.wifiPassword[sizeof(cfg.wifiPassword) - 1] = '\0';
}

/**
 * Save all current settings to non-volatile storage
 * Writes each parameter to Preferences with unique key
 */
void configSave() {
    prefs.putInt("brightness", cfg.brightness);
    prefs.putInt("contrast", cfg.contrast);
    prefs.putInt("saturation", cfg.saturation);
    prefs.putInt("wbMode", cfg.wbMode);
    prefs.putInt("frameSize", (int)cfg.photoFrameSize);
    prefs.putInt("quality", cfg.photoQuality);
    prefs.putBool("hmirror", cfg.hmirror);
    prefs.putBool("vflip", cfg.vflip);
    prefs.putInt("zoom", cfg.zoomLevel);
    prefs.putInt("expMode", cfg.exposureMode);
    prefs.putInt("shutter", cfg.shutterValue);
    prefs.putInt("gain", cfg.gainValue);
    prefs.putBool("antishake", cfg.antiShake);
    prefs.putInt("scene", cfg.sceneMode);
    prefs.putInt("shotDelay", cfg.shotDelaySec);
    prefs.putBool("watermark", cfg.watermark);
    prefs.putBool("prevJpeg", cfg.previewJpeg);
    prefs.putBool("hwZoom", cfg.hwZoom);
    prefs.putBool("upAuto", cfg.uploadAuto);
    prefs.putInt("vidFps", cfg.videoFps);
    prefs.putString("upUrl", String(cfg.uploadUrl));
    prefs.putString("wifiSSID", String(cfg.wifiSSID));
    prefs.putString("wifiPass", String(cfg.wifiPassword));
}

/**
 * Get current configuration structure (returns copy)
 */
DeviceConfig configGet() {
    return cfg;
}

// WiFi configuration functions
void configSetWiFiSSID(const char *ssid) {
    strncpy(cfg.wifiSSID, ssid, sizeof(cfg.wifiSSID) - 1);
    cfg.wifiSSID[sizeof(cfg.wifiSSID) - 1] = '\0';
}

void configSetWiFiPassword(const char *pass) {
    strncpy(cfg.wifiPassword, pass, sizeof(cfg.wifiPassword) - 1);
    cfg.wifiPassword[sizeof(cfg.wifiPassword) - 1] = '\0';
}

String configGetWiFiSSID() {
    return String(cfg.wifiSSID);
}

String configGetWiFiPassword() {
    return String(cfg.wifiPassword);
}

// Camera parameter functions
void configSetCameraParams(int brightness, int contrast, int saturation, int wbMode) {
    cfg.brightness = brightness;
    cfg.contrast = contrast;
    cfg.saturation = saturation;
    cfg.wbMode = wbMode;
}

void configSetPhotoParams(framesize_t frameSize, int quality) {
    cfg.photoFrameSize = frameSize;
    cfg.photoQuality = quality;
}

void configSetFlip(bool hmirror, bool vflip) {
    cfg.hmirror = hmirror;
    cfg.vflip = vflip;
}

// 图像增强配置：数字变焦 / 曝光 / 手持防抖 / 场景优化
void configSetImage(int zoomLevel, int exposureMode, int shutterValue,
                    int gainValue, bool antiShake, int sceneMode) {
    cfg.zoomLevel = zoomLevel;
    cfg.exposureMode = exposureMode;
    cfg.shutterValue = shutterValue;
    cfg.gainValue = gainValue;
    cfg.antiShake = antiShake;
    cfg.sceneMode = sceneMode;
}

// 自拍倒计时（秒）：仅接受 0/2/5/10，其余就近取值
void configSetShotDelay(int sec) {
    if (sec <= 1) cfg.shotDelaySec = 0;
    else if (sec <= 3) cfg.shotDelaySec = 2;
    else if (sec <= 7) cfg.shotDelaySec = 5;
    else cfg.shotDelaySec = 10;
}

// 时间水印开关
void configSetWatermark(bool on) {
    cfg.watermark = on;
}

// 高级功能配置：JPEG 预览 / 硬件窗口变焦 / 照片上传 / 机内录像帧率
void configSetAdvanced(bool previewJpeg, bool hwZoom,
                       const char *uploadUrl, bool uploadAuto, int videoFps) {
    cfg.previewJpeg = previewJpeg;
    cfg.hwZoom = hwZoom;
    cfg.uploadAuto = uploadAuto;
    cfg.videoFps = videoFps;
    if (uploadUrl) {
        strncpy(cfg.uploadUrl, uploadUrl, sizeof(cfg.uploadUrl) - 1);
        cfg.uploadUrl[sizeof(cfg.uploadUrl) - 1] = '\0';
    }
}
