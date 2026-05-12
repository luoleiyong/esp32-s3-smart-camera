/**
 * Timelapse Photography Implementation
 * 
 * Handles interval-based photo capture using millis() timing.
 * Enforces minimum 5-second interval to ensure reliable SD card writes.
 * Photos are saved with sequential numbering: tl_00000.jpg, tl_00001.jpg, etc.
 */

#include "timelapse.h"
#include "photo_capture.h"
#include "camera_driver.h"
#include "config_manager.h"
#include "camera_config.h"

// Timelapse state tracking
static bool tlRunning = false;
static unsigned long tlInterval = TIMELAPSE_MIN_INTERVAL_MS;
static unsigned long tlLastCapture = 0;
static int tlCount = 0;

/**
 * Initialize timelapse with saved configuration
 * Sets interval but does not start capture automatically
 */
void timelapseInit() {
    DeviceConfig cfg = configGet();
    tlInterval = cfg.timelapseInterval;
    tlRunning = false;
    tlCount = 0;
}

/**
 * Start timelapse capture with specified interval
 * Enforces minimum interval of 5 seconds (TIMELAPSE_MIN_INTERVAL_MS)
 */
void timelapseStart(unsigned long intervalMs) {
    tlInterval = max(intervalMs, (unsigned long)TIMELAPSE_MIN_INTERVAL_MS);
    tlRunning = true;
    tlLastCapture = millis();
    tlCount = 0;
    Serial.printf("Timelapse started, interval: %lu ms\n", tlInterval);
}

/**
 * Stop timelapse capture and log total count
 */
void timelapseStop() {
    tlRunning = false;
    Serial.printf("Timelapse stopped, captured: %d\n", tlCount);
}

/**
 * Check if timelapse is currently active
 */
bool timelapseIsRunning() {
    return tlRunning;
}

/**
 * Check interval timer and capture photo when interval elapsed
 * Must be called from main loop (non-blocking)
 */
void timelapseUpdate() {
    if (!tlRunning) return;

    if (millis() - tlLastCapture >= tlInterval) {
        tlLastCapture = millis();

        char path[64];
        snprintf(path, sizeof(path), "/tl_%05d.jpg", tlCount);

        if (photoCapture(path)) {
            tlCount++;
            Serial.printf("Timelapse frame %d captured\n", tlCount);
        }
    }
}

/**
 * Get current timelapse interval in milliseconds
 */
unsigned long timelapseGetInterval() {
    return tlInterval;
}

/**
 * Get total photo count captured in current timelapse session
 */
int timelapseGetCount() {
    return tlCount;
}
