/**
 * Motion Detection Implementation
 * 
 * Implements frame-by-frame motion detection by comparing RGB565 pixel values
 * between consecutive frames. Samples every 4th pixel for performance,
 * requires MOTION_MIN_FRAMES consecutive detections to trigger.
 */

#include "motion_detect.h"
#include "camera_config.h"
#include "config_manager.h"

// Motion detection configuration and state
static int motionThreshold = MOTION_THRESHOLD;
static bool motionEnabled = false;
static uint16_t *prevFrame = nullptr;
static int prevWidth = 0;
static int prevHeight = 0;
static int motionFrameCount = 0;

/**
 * Initialize motion detection with saved settings from configuration
 */
void motionInit() {
    DeviceConfig cfg = configGet();
    motionThreshold = cfg.motionThreshold;
    motionEnabled = cfg.motionEnabled;
}

/**
 * Detect motion by comparing current frame with previous frame
 * Algorithm:
 * 1. Extract R, G, B components from RGB565 pixels
 * 2. Calculate absolute differences per channel
 * 3. Sum channel differences and compare against threshold
 * 4. Require 2%+ changed pixels for MOTION_MIN_FRAMES consecutive frames
 * Returns true when motion is confirmed
 */
bool motionDetect(camera_fb_t *frame) {
    if (!motionEnabled) return false;
    if (!frame || frame->format != PIXFORMAT_RGB565) return false;

    uint16_t *currentFrame = (uint16_t *)frame->buf;
    int width = frame->width;
    int height = frame->height;
    int totalPixels = width * height;

    // Allocate previous frame buffer on first run or resolution change
    if (!prevFrame || prevWidth != width || prevHeight != height) {
        if (prevFrame) free(prevFrame);
        prevFrame = (uint16_t *)ps_malloc(totalPixels * 2);
        if (!prevFrame) return false;
        memcpy(prevFrame, currentFrame, totalPixels * 2);
        prevWidth = width;
        prevHeight = height;
        return false;
    }

    int changedPixels = 0;
    int step = 4;

    for (int i = 0; i < totalPixels; i += step) {
        uint16_t prev = prevFrame[i];
        uint16_t curr = currentFrame[i];

        int8_t pr = (prev >> 11) & 0x1F;
        int8_t pg = (prev >> 5) & 0x3F;
        int8_t pb = prev & 0x1F;

        int8_t cr = (curr >> 11) & 0x1F;
        int8_t cg = (curr >> 5) & 0x3F;
        int8_t cb = curr & 0x1F;

        int16_t dr = abs(pr - cr);
        int16_t dg = abs(pg - cg);
        int16_t db = abs(pb - cb);

        if ((dr + dg + db) > motionThreshold) {
            changedPixels++;
        }
    }

    memcpy(prevFrame, currentFrame, totalPixels * 2);

    int sampledPixels = totalPixels / step;
    float changeRatio = (float)changedPixels / sampledPixels * 100.0f;

    if (changeRatio > 2.0f) {
        motionFrameCount++;
        if (motionFrameCount >= MOTION_MIN_FRAMES) {
            motionFrameCount = 0;
            return true;
        }
    } else {
        motionFrameCount = 0;
    }

    return false;
}

// Motion detection configuration functions
void motionSetThreshold(int threshold) {
    motionThreshold = threshold;
}

int motionGetThreshold() {
    return motionThreshold;
}

void motionSetEnabled(bool enabled) {
    motionEnabled = enabled;
    if (!enabled && prevFrame) {
        free(prevFrame);
        prevFrame = nullptr;
    }
}

bool motionIsEnabled() {
    return motionEnabled;
}
