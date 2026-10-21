/**
 * Photo Capture Interface
 * 
 * Provides photo capture functionality with SD card storage.
 * Supports timestamp-based filename generation, photo listing,
 * and deletion operations.
 */

#ifndef PHOTO_CAPTURE_H
#define PHOTO_CAPTURE_H

#include <Arduino.h>
#include <esp_camera.h>
#include <SD_MMC.h>

// 自拍倒计时：必须在 pauseForCapture() 之前调用（此时预览任务仍在刷新画面）
// shotDelaySec=0 时无任何延迟直接返回；否则逐秒在顶部标题栏显示剩余秒数
void photoSelfTimer();

// Capture photo and save to SD card (generates timestamp filename if path is NULL)
bool photoCapture(const char *customPath = nullptr);

// List all photos on SD card into array (maxCount limits entries)
void photoList(String *list, int *count, int maxCount);

// Delete photo by filename
bool photoDelete(const char *filename);

// Check if photo file exists on SD card
bool photoExists(const char *filename);

#endif
