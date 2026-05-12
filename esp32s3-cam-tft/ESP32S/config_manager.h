/**
 * Configuration Manager Interface
 * 
 * Provides persistent storage for device settings using ESP32 Preferences API.
 * Manages WiFi credentials, camera parameters, motion detection settings,
 * and timelapse configuration.
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <esp_camera.h>

// Device configuration structure containing all persistent settings
struct DeviceConfig {
    char wifiSSID[64];
    char wifiPassword[64];
    int brightness;
    int contrast;
    int saturation;
    int wbMode;
    framesize_t photoFrameSize;
    int photoQuality;
    bool hmirror;
    bool vflip;
    int motionThreshold;
    bool motionEnabled;
    unsigned long timelapseInterval;
    bool timelapseEnabled;
};

// Initialize configuration storage and load saved settings
void configInit();

// Load settings from non-volatile storage
void configLoad();

// Save current settings to non-volatile storage
void configSave();

// Get current configuration (returns copy)
DeviceConfig configGet();

// WiFi configuration setters/getters
void configSetWiFiSSID(const char *ssid);
void configSetWiFiPassword(const char *pass);
String configGetWiFiSSID();
String configGetWiFiPassword();

// Camera parameter configuration
void configSetCameraParams(int brightness, int contrast, int saturation, int wbMode);
void configSetPhotoParams(framesize_t frameSize, int quality);
void configSetFlip(bool hmirror, bool vflip);

// Motion detection configuration
void configSetMotion(bool enabled, int threshold);

// Timelapse configuration
void configSetTimelapse(bool enabled, unsigned long intervalMs);

#endif
