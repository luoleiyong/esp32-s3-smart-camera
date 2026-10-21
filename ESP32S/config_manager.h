/**
 * Configuration Manager Interface
 * 
 * Provides persistent storage for device settings using ESP32 Preferences API.
 * Manages WiFi credentials, camera parameters, image enhancement (zoom,
 * exposure, anti-shake) and scene optimization settings.
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
    int zoomLevel;      // 数字变焦倍数：1=1x 2=2x 3=3x 4=4x
    int exposureMode;   // 曝光模式：0=自动 1=手动（快门+增益固定）
    int shutterValue;   // 手动曝光值（aec_value 0-1200，越小曝光时间越短）
    int gainValue;      // 手动增益（agc_gain 0-30）
    bool antiShake;     // 手持防抖：连拍多帧选最清晰的一张
    int sceneMode;      // 场景优化：0=标准 1=夜间增强 2=逆光补偿
    int shotDelaySec;   // 自拍倒计时秒数：0=关 2/5/10
    bool watermark;     // 时间水印：右下角烧录日期时间

    // ===== 高级功能（默认全部关闭，不影响现有行为）=====
    bool previewJpeg;   // 预览改 JPEG 解码模式：降低 PSRAM 带宽与发热，可提高帧率
    bool hwZoom;        // 拍照/推流使用 OV2640 硬件窗口变焦（实验；预览仍用软件变焦）
    char uploadUrl[192];// 照片上传地址（HTTP POST multipart，字段名 photo）
    bool uploadAuto;    // 拍照成功后自动上传
    int videoFps;       // 机内录像帧率（5/10/15）
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

// 图像增强配置：数字变焦 / 曝光模式与快门增益 / 手持防抖 / 场景优化
void configSetImage(int zoomLevel, int exposureMode, int shutterValue,
                    int gainValue, bool antiShake, int sceneMode);
void configSetShotDelay(int sec);
void configSetWatermark(bool on);

// 高级功能配置：JPEG 预览 / 硬件窗口变焦 / 照片上传 / 机内录像帧率
void configSetAdvanced(bool previewJpeg, bool hwZoom,
                       const char *uploadUrl, bool uploadAuto, int videoFps);

#endif
