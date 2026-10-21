/**
 * Camera Driver Interface
 * 
 * Provides high-level camera control including initialization, mode switching,
 * frame capture, and image parameter adjustment. Supports multiple operating
 * modes optimized for preview, photo capture, and streaming.
 */

#ifndef CAMERA_DRIVER_H
#define CAMERA_DRIVER_H

#include <Arduino.h>
#include <esp_camera.h>
#include "camera_config.h"

// Camera operating modes with different pixel formats and resolutions
enum class CameraMode {
    PREVIEW_RGB565,  // RGB565 format for real-time TFT preview
    PHOTO_JPEG,      // JPEG format for high-resolution photo capture
    VIDEO_JPEG,      // JPEG format for video recording
    STREAM_JPEG,     // JPEG format for WiFi streaming
    PREVIEW_JPEG     // JPEG format for real-time TFT preview（TJpgDec 解码，省 PSRAM 带宽/发热）
};

// Initialize camera hardware and configure for specified mode
bool cameraInit(CameraMode mode = CameraMode::PREVIEW_RGB565);

// Deinitialize camera hardware and release resources
void cameraDeinit();

// Switch camera between different operating modes (requires reinitialization)
bool cameraSwitchMode(CameraMode newMode);

// Capture a single frame from camera (blocking call)
camera_fb_t* cameraCaptureFrame();

// Return frame buffer to camera driver after processing
void cameraReturnFrame(camera_fb_t *fb);

// Apply image parameters (brightness, contrast, saturation, white balance)
void cameraApplySettings(int brightness, int contrast, int saturation, int wbMode);

// 曝光/快门控制：mode 0=自动 1=手动（手动时锁定 aec_value 快门值与 agc_gain 增益）
void cameraApplyExposure(int exposureMode, int shutterValue, int gainValue);

// 场景优化算法：0=标准 1=夜间增强（低照度补偿）2=逆光补偿
void cameraApplyScene(int sceneMode);

// Set photo capture resolution
void cameraSetFrameSize(framesize_t size);

// Enable/disable horizontal mirroring
void cameraSetHmirror(bool enable);

// Enable/disable vertical flipping
void cameraSetVflip(bool enable);

// Get current frame size setting
framesize_t cameraGetCurrentFrameSize();

// Get current camera operating mode
CameraMode cameraGetCurrentMode();

// OV2640 硬件窗口变焦（实验）：按当前帧尺寸做中心裁剪
// 返回 true=已用硬件窗口实现变焦（调用方不要再做软件裁剪）
// 返回 false=不支持/未启用（调用方请回退软件裁剪）
bool cameraApplySensorZoom(int zoom);

// 按配置返回预览应使用的相机模式：previewJpeg=on → PREVIEW_JPEG，否则 PREVIEW_RGB565
// 供其它模块在“回到预览模式”时选择模式（如拍完照、推流结束）
CameraMode cameraPreviewMode();

// Lock camera to prevent concurrent access during capture
void cameraLock();

// Unlock camera to allow access after capture completes
void cameraUnlock();

// Check if camera is currently locked
bool cameraIsLocked();

#endif
