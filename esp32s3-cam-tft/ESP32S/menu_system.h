/**
 * Menu System Interface
 * 
 * Provides on-device menu navigation via joystick input with Chinese font support.
 * Implements a state machine with multiple menu screens for camera settings,
 * photo gallery, motion detection, timelapse, and WiFi info.
 */

#ifndef MENU_SYSTEM_H
#define MENU_SYSTEM_H

#include <Arduino.h>
#include "chinese_font.h"

// Menu system states representing different screens
enum class MenuState {
    PREVIEW,           // Camera preview mode (default)
    MAIN_MENU,         // Main menu with 8 options
    CAMERA_SETTINGS,   // Brightness, contrast, saturation, white balance
    RESOLUTION,        // Photo resolution selection
    QUALITY,           // JPEG quality selection
    GALLERY,           // Photo browsing
    MOTION_SETTINGS,   // Motion detection enable/threshold
    TIMELAPSE_SETTINGS, // Timelapse enable/interval
    WIFI_INFO,         // WiFi connection details
    ABOUT              // Device info (removed, handled as main menu item)
};

extern MenuState currentMenuState;
extern int menuSelection;

// Initialize menu system in preview state
void menuInit();

// Handle joystick input and menu state transitions (call from main loop)
void menuHandleNavigation();

// Enter main menu from preview mode
void menuEnterMain();

// Exit menu and return to camera preview
void menuExit();

// Get current menu state
MenuState menuGetCurrent();

#endif
