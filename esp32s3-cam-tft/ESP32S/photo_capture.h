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

// Capture photo and save to SD card (generates timestamp filename if path is NULL)
bool photoCapture(const char *customPath = nullptr);

// List all photos on SD card into array (maxCount limits entries)
void photoList(String *list, int *count, int maxCount);

// Delete photo by filename
bool photoDelete(const char *filename);

// Check if photo file exists on SD card
bool photoExists(const char *filename);

#endif
