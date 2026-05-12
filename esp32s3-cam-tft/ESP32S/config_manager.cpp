/**
 * Configuration Manager Implementation
 * 
 * Handles persistent storage of device settings using ESP32 Preferences API.
 * Provides load/save operations for WiFi credentials, camera parameters,
 * motion detection, and timelapse settings.
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
    cfg.motionThreshold = prefs.getInt("motionTh", MOTION_THRESHOLD);
    cfg.motionEnabled = prefs.getBool("motionEn", false);
    cfg.timelapseInterval = prefs.getULong("tlInterval", TIMELAPSE_MIN_INTERVAL_MS);
    cfg.timelapseEnabled = prefs.getBool("tlEnabled", false);

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
    prefs.putInt("motionTh", cfg.motionThreshold);
    prefs.putBool("motionEn", cfg.motionEnabled);
    prefs.putULong("tlInterval", cfg.timelapseInterval);
    prefs.putBool("tlEnabled", cfg.timelapseEnabled);
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

// Motion detection functions
void configSetMotion(bool enabled, int threshold) {
    cfg.motionEnabled = enabled;
    cfg.motionThreshold = threshold;
}

// Timelapse functions
void configSetTimelapse(bool enabled, unsigned long intervalMs) {
    cfg.timelapseEnabled = enabled;
    cfg.timelapseInterval = intervalMs;
}
