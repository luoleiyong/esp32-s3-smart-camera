/**
 * Menu System Implementation
 * 
 * Implements a state machine-based menu system with Chinese font rendering.
 * Handles joystick navigation (Up/Down for selection, Left/Right for value adjustment,
 * Press for enter/confirm). Uses partial screen redraws for smooth UI transitions.
 * 
 * Menu hierarchy:
 * - Preview (default) -> joystick press -> Main Menu
 *   - Camera Settings (brightness, contrast, saturation, white balance)
 *   - Resolution (UXGA, SXGA, XGA, SVGA, QVGA)
 *   - Quality (Ultra Q5 to Very Low Q20)
 *   - Gallery (browse photos)
 *   - Motion Settings (enable/disable, threshold)
 *   - Timelapse Settings (enable/disable, interval)
 *   - WiFi Info (IP, RSSI, stream status)
 *   - Back (return to preview)
 */

#include "menu_system.h"
#include "joystick.h"
#include "tft_display.h"
#include "camera_driver.h"
#include "config_manager.h"
#include "photo_capture.h"
#include "motion_detect.h"
#include "timelapse.h"
#include "wifi_manager.h"
#include "web_server.h"
#include <WiFi.h>
#include <SD_MMC.h>

extern void stopPreviewAndCamera();
extern void startPreviewAndCamera();

// Global menu state
MenuState currentMenuState = MenuState::PREVIEW;
int menuSelection = 0;
static int menuItemsCount = 0;
static bool menuNeedsRedraw = true;

// Gallery photo listing (up to 50 entries)
static String photoListArr[50];
static int photoCount = 0;
static int currentPhotoIndex = 0;

/**
 * Initialize menu system in preview state
 */
void menuInit() {
    currentMenuState = MenuState::PREVIEW;
    menuSelection = 0;
}

/**
 * Enter main menu from preview mode
 * Stops camera preview to free resources for menu rendering
 */
void menuEnterMain() {
    stopPreviewAndCamera();
    currentMenuState = MenuState::MAIN_MENU;
    menuSelection = 0;
    menuItemsCount = 8;
    menuNeedsRedraw = true;
}

/**
 * Exit menu and return to camera preview
 * Restarts camera and redraws UI frame
 */
void menuExit() {
    currentMenuState = MenuState::PREVIEW;
    menuNeedsRedraw = true;
    tft.fillScreen(ST77XX_BLACK);
    startPreviewAndCamera();
    tftDrawUIFrame();
    tftUpdateStatusBar(wifiIsConnected(), wifiGetIP().c_str());
}

/**
 * Get current menu state
 */
MenuState menuGetCurrent() {
    return currentMenuState;
}

// Chinese character index arrays for menu items
static const ChineseCharIndex ITEM0[] = {CHAR_XIANG, CHAR_JI, CHAR_SHE, CHAR_ZHI};    // 相机设置
static const ChineseCharIndex ITEM1[] = {CHAR_FEN, CHAR_BIAN, CHAR_LV};               // 分辨率
static const ChineseCharIndex ITEM2[] = {CHAR_ZHAO, CHAR_PIAN, CHAR_ZHI2, CHAR_LIANG}; // 照片质量
static const ChineseCharIndex ITEM3[] = {CHAR_TU, CHAR_KU};                           // 图库
static const ChineseCharIndex ITEM4[] = {CHAR_YUN, CHAR_DONG, CHAR_JIAN, CHAR_CE};    // 运动检测
static const ChineseCharIndex ITEM5[] = {CHAR_YAN, CHAR_SHI, CHAR_SHE2, CHAR_YING};   // 延时摄影
static const ChineseCharIndex ITEM6[] = {CHAR_WANG, CHAR_LUO, CHAR_XIN, CHAR_XI};     // 网络信息
static const ChineseCharIndex ITEM7[] = {CHAR_FAN, CHAR_HUI};                         // 返回

static const ChineseCharIndex* menuCharArrays[] = {ITEM0, ITEM1, ITEM2, ITEM3, ITEM4, ITEM5, ITEM6, ITEM7};
static const int menuCharCounts[] = {4, 3, 4, 2, 4, 4, 4, 2};

/**
 * Draw a single menu item with Chinese characters centered on screen
 */
static void drawMenuItem(int idx, int y, uint16_t color) {
    int charCount = menuCharCounts[idx];
    int totalWidth = charCount * 17;
    int startX = (240 - totalWidth) / 2;
    drawChineseWord(startX, y, menuCharArrays[idx], charCount, color);
}

/**
 * Draw complete main menu screen with title and 8 items
 */
static void drawMainMenu() {
    if (!menuNeedsRedraw) return;

    tft.fillScreen(ST77XX_BLACK);

    const ChineseCharIndex menuTitle[] = {CHAR_CAI, CHAR_DAN};  // 菜单
    drawChineseWord(103, 8, menuTitle, 2, ST77XX_WHITE);
    tft.drawFastHLine(0, 32, 240, ST77XX_WHITE);

    for (int i = 0; i < 8; i++) {
        int y = 42 + i * 24;
        if (i == menuSelection) {
            tft.fillRoundRect(8, y - 2, 224, 20, 4, 0x18E3);
            drawMenuItem(i, y, ST77XX_WHITE);
        } else {
            drawMenuItem(i, y, ST77XX_CYAN);
        }
    }

    menuNeedsRedraw = false;
}

/**
 * Partial redraw for main menu selection change (unselected -> selected)
 */
static void updateMainMenuSelection(int oldSel, int newSel) {
    int yOld = 42 + oldSel * 24;
    tft.fillRoundRect(8, yOld - 2, 224, 20, 4, ST77XX_BLACK);
    drawMenuItem(oldSel, yOld, ST77XX_CYAN);

    int yNew = 42 + newSel * 24;
    tft.fillRoundRect(8, yNew - 2, 224, 20, 4, 0x18E3);
    drawMenuItem(newSel, yNew, ST77XX_WHITE);
}

/**
 * Draw submenu title with centered Chinese characters and divider line
 */
static void drawSubTitle(const ChineseCharIndex *chars, int count) {
    int totalWidth = count * 17;
    int startX = (240 - totalWidth) / 2;
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    drawChineseWord(startX, 8, chars, count, ST77XX_WHITE);
    tft.drawFastHLine(0, 32, 240, ST77XX_WHITE);
}

/**
 * Draw a single settings row with label and value
 * Highlights selected row with rounded rectangle background
 */
static void drawSettingRow(int row, int y, const char *label, const char *value, int maxRows) {
    if (row == menuSelection) {
        tft.fillRoundRect(8, y - 3, 224, 22, 4, 0x18E3);
        tft.setTextColor(ST77XX_WHITE);
    } else {
        tft.setTextColor(ST77XX_CYAN);
    }
    tft.setTextSize(1);
    tft.setCursor(16, y);
    tft.print(label);
    tft.setCursor(180, y);
    tft.print(value);
}

/**
 * Draw camera settings submenu with brightness, contrast, saturation, white balance
 */
static void drawCameraSettings() {
    DeviceConfig cfg = configGet();
    const ChineseCharIndex title[] = {CHAR_XIANG, CHAR_JI, CHAR_SHE, CHAR_ZHI};
    drawSubTitle(title, 4);

    drawSettingRow(0, 48, "Brightness", String(cfg.brightness).c_str(), 4);
    drawSettingRow(1, 78, "Contrast", String(cfg.contrast).c_str(), 4);
    drawSettingRow(2, 108, "Saturation", String(cfg.saturation).c_str(), 4);
    drawSettingRow(3, 138, "White Bal", String(cfg.wbMode).c_str(), 4);

    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(16, 210);
    tft.print("L/R Adjust | Press Back");
}

/**
 * Draw resolution selection submenu
 */
static void drawResolutionMenu() {
    DeviceConfig cfg = configGet();
    const ChineseCharIndex title[] = {CHAR_FEN, CHAR_BIAN, CHAR_LV};
    drawSubTitle(title, 3);

    const char *resolutions[] = {
        "UXGA 1600x1200", "SXGA 1280x1024", "XGA 1024x768",
        "SVGA 800x600", "QVGA 320x240"
    };
    framesize_t sizes[] = {FRAMESIZE_UXGA, FRAMESIZE_SXGA, FRAMESIZE_XGA, FRAMESIZE_SVGA, FRAMESIZE_QVGA};

    for (int i = 0; i < 5; i++) {
        int y = 48 + i * 28;
        if (i == menuSelection) {
            tft.fillRoundRect(8, y - 3, 224, 22, 4, 0x18E3);
            tft.setTextColor(ST77XX_WHITE);
        } else {
            tft.setTextColor(sizes[i] == cfg.photoFrameSize ? ST77XX_GREEN : ST77XX_CYAN);
        }
        tft.setTextSize(1);
        tft.setCursor(16, y);
        tft.print(resolutions[i]);
    }
}

/**
 * Draw JPEG quality selection submenu
 */
static void drawQualityMenu() {
    DeviceConfig cfg = configGet();
    const ChineseCharIndex title[] = {CHAR_ZHI2, CHAR_LIANG};
    drawSubTitle(title, 2);

    const char *qualities[] = {"Ultra (Q5)", "High (Q8)", "Medium (Q10)", "Low (Q15)", "Very Low (Q20)"};
    int qualityValues[] = {5, 8, 10, 15, 20};

    for (int i = 0; i < 5; i++) {
        int y = 48 + i * 28;
        if (i == menuSelection) {
            tft.fillRoundRect(8, y - 3, 224, 22, 4, 0x18E3);
            tft.setTextColor(ST77XX_WHITE);
        } else {
            tft.setTextColor(qualityValues[i] == cfg.photoQuality ? ST77XX_GREEN : ST77XX_CYAN);
        }
        tft.setTextSize(1);
        tft.setCursor(16, y);
        tft.print(qualities[i]);
    }
}

/**
 * Draw gallery with photo browser
 */
static void drawGallery() {
    const ChineseCharIndex title[] = {CHAR_TU, CHAR_KU};
    drawSubTitle(title, 2);

    photoList(photoListArr, &photoCount, 50);

    if (photoCount == 0) {
        tft.setTextColor(ST77XX_YELLOW);
        tft.setTextSize(1);
        tft.setCursor(40, 100);
        tft.println("No photos");
        tft.setCursor(30, 120);
        tft.println("Press IO0 to take!");
    } else {
        if (currentPhotoIndex >= photoCount) currentPhotoIndex = photoCount - 1;
        tftDisplayPhoto(currentPhotoIndex, photoListArr[currentPhotoIndex]);
        tftDrawGalleryInfo(currentPhotoIndex, photoCount);
    }
}

/**
 * Draw motion detection settings (enable/disable, threshold)
 */
static void drawMotionSettings() {
    DeviceConfig cfg = configGet();
    const ChineseCharIndex title[] = {CHAR_YUN, CHAR_DONG, CHAR_JIAN, CHAR_CE};
    drawSubTitle(title, 4);

    drawSettingRow(0, 48, "Enabled", cfg.motionEnabled ? "ON" : "OFF", 2);
    drawSettingRow(1, 78, "Threshold", String(cfg.motionThreshold).c_str(), 2);

    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(16, 210);
    tft.print("L/R Adjust | Press Back");
}

/**
 * Draw timelapse settings (enable/disable, interval)
 */
static void drawTimelapseSettings() {
    DeviceConfig cfg = configGet();
    const ChineseCharIndex title[] = {CHAR_YAN, CHAR_SHI, CHAR_SHE2, CHAR_YING};
    drawSubTitle(title, 4);

    drawSettingRow(0, 48, "Enabled", cfg.timelapseEnabled ? "ON" : "OFF", 2);
    char buf[16];
    snprintf(buf, sizeof(buf), "%lus", cfg.timelapseInterval / 1000);
    drawSettingRow(1, 78, "Interval", buf, 2);

    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(16, 210);
    tft.print("L/R Adjust | Press Back");
}

/**
 * Draw WiFi info screen with IP, RSSI, stream status
 */
static void drawWiFiInfo() {
    const ChineseCharIndex title[] = {CHAR_WANG, CHAR_LUO, CHAR_XIN, CHAR_XI};
    drawSubTitle(title, 4);

    tft.setTextSize(1);
    int y = 48;

    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(16, y);
    tft.print("IP: ");
    tft.setTextColor(ST77XX_WHITE);
    tft.println(wifiGetIP());
    y += 22;

    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(16, y);
    tft.print("RSSI: ");
    tft.setTextColor(ST77XX_WHITE);
    tft.print(WiFi.RSSI());
    tft.println(" dBm");
    y += 22;

    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(16, y);
    tft.print("Stream: ");
    tft.setTextColor(webServerIsStreaming() ? ST77XX_GREEN : ST77XX_RED);
    tft.println(webServerIsStreaming() ? "Active" : "Idle");
    y += 28;

    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(16, y);
    tft.println("Web Record:");
    y += 18;
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(16, y);
    tft.println("Browser -> Record");
    y += 18;
    tft.setCursor(16, y);
    tft.println("Save as WebM");

    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(16, 215);
    tft.print("Press to go back");
}

/**
 * Return to main menu from any submenu
 */
static void goBackToMain() {
    currentMenuState = MenuState::MAIN_MENU;
    menuSelection = 0;
    menuNeedsRedraw = true;
    drawMainMenu();
}

// Navigation helper functions for submenu handling
namespace NavHelper {

void enterSubMenu(MenuState state) {
    currentMenuState = state;
    menuSelection = 0;
}

bool handleUpDown(int maxIndex, void (*renderFn)()) {
    if (joystickUpEdge() && menuSelection > 0) {
        menuSelection--;
        if (renderFn) renderFn();
        return true;
    }
    if (joystickDownEdge() && menuSelection < maxIndex) {
        menuSelection++;
        if (renderFn) renderFn();
        return true;
    }
    return false;
}

bool adjustValue(int& value, int minVal, int maxVal) {
    if (joystickLeftEdge() && value > minVal) {
        value--;
        return true;
    }
    if (joystickRightEdge() && value < maxVal) {
        value++;
        return true;
    }
    return false;
}

} // namespace NavHelper

// ========== State Handler Functions ==========

/**
 * Preview state: press joystick to enter main menu
 */
static void handlePreview() {
    if (joystickPressedEdge()) {
        menuEnterMain();
    }
}

/**
 * Main menu: navigate items and enter submenus
 */
static void handleMainMenu() {
    drawMainMenu();

    if (joystickUpEdge() && menuSelection > 0) {
        int old = menuSelection;
        menuSelection--;
        updateMainMenuSelection(old, menuSelection);
        return;
    }
    if (joystickDownEdge() && menuSelection < 7) {
        int old = menuSelection;
        menuSelection++;
        updateMainMenuSelection(old, menuSelection);
        return;
    }
    if (!joystickPressedEdge()) return;

    switch (menuSelection) {
        case 0:
            NavHelper::enterSubMenu(MenuState::CAMERA_SETTINGS);
            drawCameraSettings();
            break;
        case 1:
            NavHelper::enterSubMenu(MenuState::RESOLUTION);
            drawResolutionMenu();
            break;
        case 2:
            NavHelper::enterSubMenu(MenuState::QUALITY);
            drawQualityMenu();
            break;
        case 3:
            currentMenuState = MenuState::GALLERY;
            currentPhotoIndex = 0;
            drawGallery();
            break;
        case 4:
            NavHelper::enterSubMenu(MenuState::MOTION_SETTINGS);
            drawMotionSettings();
            break;
        case 5:
            NavHelper::enterSubMenu(MenuState::TIMELAPSE_SETTINGS);
            drawTimelapseSettings();
            break;
        case 6:
            currentMenuState = MenuState::WIFI_INFO;
            drawWiFiInfo();
            break;
        case 7:
            menuExit();
            break;
    }
}

/**
 * Camera settings: adjust brightness, contrast, saturation, white balance
 */
static void handleCameraSettings() {
    if (NavHelper::handleUpDown(3, drawCameraSettings)) return;

    DeviceConfig cfg = configGet();
    int vals[] = {cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode};
    int mins[] = {-2, -2, -2, 0};
    int maxs[] = {2, 2, 2, 4};

    if (NavHelper::adjustValue(vals[menuSelection], mins[menuSelection], maxs[menuSelection])) {
        configSetCameraParams(vals[0], vals[1], vals[2], vals[3]);
        cameraApplySettings(vals[0], vals[1], vals[2], vals[3]);
        configSave();
        drawCameraSettings();
        return;
    }

    if (joystickPressedEdge()) {
        goBackToMain();
    }
}

/**
 * Resolution selection: choose from 5 resolution presets
 */
static void handleResolution() {
    if (NavHelper::handleUpDown(4, drawResolutionMenu)) return;

    if (joystickPressedEdge()) {
        framesize_t sizes[] = {FRAMESIZE_UXGA, FRAMESIZE_SXGA, FRAMESIZE_XGA, FRAMESIZE_SVGA, FRAMESIZE_QVGA};
        configSetPhotoParams(sizes[menuSelection], configGet().photoQuality);
        cameraSetFrameSize(sizes[menuSelection]);
        configSave();
        goBackToMain();
    }
}

/**
 * Quality selection: choose JPEG quality from 5 presets
 */
static void handleQuality() {
    if (NavHelper::handleUpDown(4, drawQualityMenu)) return;

    if (joystickPressedEdge()) {
        int qualities[] = {5, 8, 10, 15, 20};
        configSetPhotoParams(configGet().photoFrameSize, qualities[menuSelection]);
        configSave();
        goBackToMain();
    }
}

/**
 * Gallery: browse saved photos with Left/Right navigation
 */
static void handleGallery() {
    if (photoCount > 0) {
        if (joystickLeftEdge() && currentPhotoIndex > 0) {
            currentPhotoIndex--;
            drawGallery();
            return;
        }
        if (joystickRightEdge() && currentPhotoIndex < photoCount - 1) {
            currentPhotoIndex++;
            drawGallery();
            return;
        }
    }

    if (joystickPressedEdge()) {
        goBackToMain();
    }
}

/**
 * Motion detection settings: toggle enable/disable, adjust threshold
 */
static void handleMotionSettings() {
    if (NavHelper::handleUpDown(1, drawMotionSettings)) return;

    DeviceConfig cfg = configGet();

    if (joystickLeftEdge() || joystickRightEdge()) {
        if (menuSelection == 0) {
            configSetMotion(!cfg.motionEnabled, cfg.motionThreshold);
            motionSetEnabled(!cfg.motionEnabled);
        } else {
            int delta = joystickLeftEdge() ? -5 : 5;
            int newVal = cfg.motionThreshold + delta;
            if (newVal >= 5 && newVal <= 50) {
                configSetMotion(cfg.motionEnabled, newVal);
                motionSetThreshold(newVal);
            }
        }
        configSave();
        drawMotionSettings();
        return;
    }

    if (joystickPressedEdge()) {
        goBackToMain();
    }
}

/**
 * Timelapse settings: toggle enable/disable, adjust interval
 */
static void handleTimelapseSettings() {
    if (NavHelper::handleUpDown(1, drawTimelapseSettings)) return;

    DeviceConfig cfg = configGet();

    if (joystickLeftEdge() || joystickRightEdge()) {
        if (menuSelection == 0) {
            configSetTimelapse(!cfg.timelapseEnabled, cfg.timelapseInterval);
            if (!cfg.timelapseEnabled) timelapseStart(cfg.timelapseInterval);
            else timelapseStop();
        } else {
            long delta = joystickLeftEdge() ? -5000 : 5000;
            unsigned long newInterval = cfg.timelapseInterval + delta;
            if (newInterval >= TIMELAPSE_MIN_INTERVAL_MS) {
                configSetTimelapse(cfg.timelapseEnabled, newInterval);
            }
        }
        configSave();
        drawTimelapseSettings();
        return;
    }

    if (joystickPressedEdge()) {
        goBackToMain();
    }
}

/**
 * WiFi info: display connection details (read-only)
 */
static void handleWiFiInfo() {
    if (joystickPressedEdge()) {
        goBackToMain();
    }
}

/**
 * About page handler (placeholder)
 */
static void handleAbout() {
    if (joystickPressedEdge()) {
        goBackToMain();
    }
}

// ========== State Handler Dispatch Table ==========

using StateHandler = void (*)();
static StateHandler stateHandlers[] = {
    handlePreview,           // PREVIEW
    handleMainMenu,          // MAIN_MENU
    handleCameraSettings,    // CAMERA_SETTINGS
    handleResolution,        // RESOLUTION
    handleQuality,           // QUALITY
    handleGallery,           // GALLERY
    handleMotionSettings,    // MOTION_SETTINGS
    handleTimelapseSettings, // TIMELAPSE_SETTINGS
    handleWiFiInfo,          // WIFI_INFO
    handleAbout,             // ABOUT
};

/**
 * Main navigation handler (call from main loop)
 * Reads joystick input and dispatches to current state handler
 */
void menuHandleNavigation() {
    joystickRead();
    stateHandlers[static_cast<int>(currentMenuState)]();
}
