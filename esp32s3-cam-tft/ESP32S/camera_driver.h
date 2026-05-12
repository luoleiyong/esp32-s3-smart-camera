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
    STREAM_JPEG      // JPEG format for WiFi streaming
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

// Lock camera to prevent concurrent access during capture
void cameraLock();

// Unlock camera to allow access after capture completes
void cameraUnlock();

// Check if camera is currently locked
bool cameraIsLocked();

#endif
