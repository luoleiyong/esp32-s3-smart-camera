/**
 * HTML Pages Interface
 * 
 * Provides web interface HTML content generation for the camera's web server.
 * Generates responsive HTML pages with mobile-friendly design for camera control,
 * live streaming, photo gallery, and settings configuration.
 */

#ifndef HTML_PAGES_H
#define HTML_PAGES_H

#include <Arduino.h>

// Generate root page HTML with dashboard buttons and SD card info
String htmlGetRootPage(float totalMB, float usedMB);

// Generate settings page HTML with camera parameters form
String htmlGetConfigPage(int brightness, int contrast, int saturation, int wbMode,
    int frameSize, int quality, bool hmirror, bool vflip,
    bool motionEnabled, int motionThreshold,
    bool timelapseEnabled, int timelapseInterval);

// Generate gallery page HTML header with SD card usage stats
String htmlGetGalleryHeader(float totalMB, float usedMB, float freeMB);

// Generate HTML card for a single photo with preview, download, and delete buttons
String htmlGetPhotoCard(const String &name);

// Generate empty gallery message when no photos exist
String htmlGetEmptyGallery();

// Generate gallery page HTML footer and closing tags
String htmlGetGalleryFooter();

#endif
