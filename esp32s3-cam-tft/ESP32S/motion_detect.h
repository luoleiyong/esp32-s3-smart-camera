/**
 * Motion Detection Interface
 * 
 * Provides frame-by-frame motion detection using RGB565 pixel comparison.
 * Analyzes color channel differences between consecutive frames with
 * configurable threshold and consecutive frame validation.
 */

#ifndef MOTION_DETECT_H
#define MOTION_DETECT_H

#include <Arduino.h>
#include <esp_camera.h>

// Initialize motion detection with saved configuration
void motionInit();

// Analyze frame for motion (returns true when motion detected)
bool motionDetect(camera_fb_t *frame);

// Configure motion detection parameters
void motionSetThreshold(int threshold);
int motionGetThreshold();
void motionSetEnabled(bool enabled);
bool motionIsEnabled();

#endif
