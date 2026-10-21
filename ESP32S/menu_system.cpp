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
 *   - Lens Settings (digital zoom, scene optimization, anti-shake)
 *   - Exposure Settings (auto/manual, shutter, gain)
 *   - WiFi Info (IP, RSSI, stream status)
 *   - Back (return to preview)
 */

#include "menu_system.h"
#include "joystick.h"
#include "tft_display.h"
#include "camera_driver.h"
#include "config_manager.h"
#include "photo_capture.h"
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

// 图库长按删除的手势状态（区分按下/释放、避免长按期间重复触发）
static bool galleryPressActive = false;   // 是否处于一次有效的按下过程
static bool galleryLongFired = false;     // 本次按住是否已触发长按删除

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
    tftClear();
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

// UTF-8 menu item labels
static const char *menuItems[] = {
    "相机设置", "分辨率", "照片质量", "图库",
    "变焦防抖", "曝光设置", "网络信息", "返回"
};

// Main menu item vertical positions
#define MENU_ITEM_Y0   17
#define MENU_ITEM_DY   14
#define MENU_ITEM_H    14

/**
 * Draw a single menu item centered on screen
 */
static void drawMenuItem(int idx, int y, uint16_t color) {
    tftDrawCNCenter(y, menuItems[idx], color);
}

/**
 * Draw complete main menu screen with title and 8 items
 */
static void drawMainMenu() {
    if (!menuNeedsRedraw) return;

    tft.fillScreen(UI_BG_COLOR);
    tftDrawTitleBar("菜单");

    for (int i = 0; i < 8; i++) {
        int y = MENU_ITEM_Y0 + i * MENU_ITEM_DY;
        if (i == menuSelection) {
            tft.fillRoundRect(4, y - 1, 120, MENU_ITEM_H, 3, UI_SELECT_BG);
            drawMenuItem(i, y, UI_ACCENT);
        } else {
            drawMenuItem(i, y, UI_TEXT_COLOR);
        }
    }

    menuNeedsRedraw = false;
}

/**
 * Partial redraw for main menu selection change (unselected -> selected)
 */
static void updateMainMenuSelection(int oldSel, int newSel) {
    int yOld = MENU_ITEM_Y0 + oldSel * MENU_ITEM_DY;
    tft.fillRoundRect(4, yOld - 1, 120, MENU_ITEM_H, 3, UI_BG_COLOR);
    drawMenuItem(oldSel, yOld, UI_TEXT_COLOR);

    int yNew = MENU_ITEM_Y0 + newSel * MENU_ITEM_DY;
    tft.fillRoundRect(4, yNew - 1, 120, MENU_ITEM_H, 3, UI_SELECT_BG);
    drawMenuItem(newSel, yNew, UI_ACCENT);
}

/**
 * Clear screen and draw submenu title bar
 */
static void drawSubTitle(const char *title) {
    tft.fillScreen(UI_BG_COLOR);
    tftDrawTitleBar(title);
}

/**
 * Draw a single settings row with label and value
 * Highlights selected row with rounded rectangle background
 */
static void drawSettingRow(int row, int y, const char *label, const char *value) {
    if (row == menuSelection) {
        tft.fillRoundRect(4, y - 3, 120, 17, 3, UI_SELECT_BG);
        tftDrawCN(8, y, label, UI_ACCENT);
        tftDrawCN(100, y, value, UI_ACCENT);
    } else {
        tftDrawCN(8, y, label, UI_TEXT_COLOR);
        tftDrawCN(100, y, value, UI_TEXT_COLOR);
    }
}

/**
 * Draw the bottom status bar with a hint message
 */
static void drawStatusHint(const char *hint) {
    tft.fillRect(0, UI_STATUS_Y, TFT_WIDTH, UI_STATUS_H, UI_DIVIDER);
    tftDrawCNCenter(UI_STATUS_Y + 2, hint, UI_TEXT_COLOR);
}

/**
 * Draw camera settings submenu with brightness, contrast, saturation, white balance
 */
static void drawCameraSettings() {
    DeviceConfig cfg = configGet();
    drawSubTitle("相机设置");

    drawSettingRow(0, 24, "亮度", String(cfg.brightness).c_str());
    drawSettingRow(1, 46, "对比度", String(cfg.contrast).c_str());
    drawSettingRow(2, 68, "饱和度", String(cfg.saturation).c_str());
    drawSettingRow(3, 90, "白平衡", String(cfg.wbMode).c_str());

    drawStatusHint("左右调节 按下返回");
}

/**
 * Draw resolution selection submenu
 */
static void drawResolutionMenu() {
    DeviceConfig cfg = configGet();
    drawSubTitle("分辨率");

    const char *resolutions[] = {
        "UXGA 1600x1200", "SXGA 1280x1024", "XGA 1024x768",
        "SVGA 800x600", "QVGA 320x240"
    };
    framesize_t sizes[] = {FRAMESIZE_UXGA, FRAMESIZE_SXGA, FRAMESIZE_XGA, FRAMESIZE_SVGA, FRAMESIZE_QVGA};

    for (int i = 0; i < 5; i++) {
        int y = 22 + i * 18;
        if (i == menuSelection) {
            tft.fillRoundRect(4, y - 3, 120, 17, 3, UI_SELECT_BG);
            tftDrawCN(8, y, resolutions[i], UI_ACCENT);
        } else {
            tftDrawCN(8, y, resolutions[i], sizes[i] == cfg.photoFrameSize ? 0x07E0 : UI_TEXT_COLOR);
        }
    }
}

/**
 * Draw JPEG quality selection submenu
 */
static void drawQualityMenu() {
    DeviceConfig cfg = configGet();
    drawSubTitle("照片质量");

    const char *qualities[] = {"Ultra (Q5)", "High (Q8)", "Medium (Q10)", "Low (Q15)", "Very Low (Q20)"};
    int qualityValues[] = {5, 8, 10, 15, 20};

    for (int i = 0; i < 5; i++) {
        int y = 22 + i * 18;
        if (i == menuSelection) {
            tft.fillRoundRect(4, y - 3, 120, 17, 3, UI_SELECT_BG);
            tftDrawCN(8, y, qualities[i], UI_ACCENT);
        } else {
            tftDrawCN(8, y, qualities[i], qualityValues[i] == cfg.photoQuality ? 0x07E0 : UI_TEXT_COLOR);
        }
    }
}

/**
 * Draw gallery with photo browser
 */
static void drawGallery() {
    drawSubTitle("图库");

    photoList(photoListArr, &photoCount, 50);

    if (photoCount == 0) {
        tftDrawCNCenter(48, "暂无照片", UI_HINT_COLOR);
        tftDrawCNCenter(68, "按下 IO0 拍照", UI_HINT_COLOR);
    } else {
        if (currentPhotoIndex >= photoCount) currentPhotoIndex = photoCount - 1;
        tftDisplayPhoto(currentPhotoIndex, photoListArr[currentPhotoIndex]);
        tftDrawGalleryInfo(currentPhotoIndex, photoCount);
    }
}

/**
 * Draw lens settings (digital zoom, scene optimization, anti-shake)
 */
static void drawLensSettings() {
    DeviceConfig cfg = configGet();
    drawSubTitle("变焦防抖");

    const char *zooms[] = {"1x", "2x", "3x", "4x"};
    const char *scenes[] = {"标准", "夜间", "逆光"};
    drawSettingRow(0, 24, "变焦", zooms[constrain(cfg.zoomLevel, 1, 4) - 1]);
    drawSettingRow(1, 46, "场景", scenes[constrain(cfg.sceneMode, 0, 2)]);
    drawSettingRow(2, 68, "防抖", cfg.antiShake ? "开" : "关");

    drawStatusHint("左右调节 按下返回");
}

/**
 * 倒计时秒数 → 显示文本（0/其它=关）
 */
static const char *shotDelayText(int sec) {
    if (sec == 2) return "2s";
    if (sec == 5) return "5s";
    if (sec == 10) return "10s";
    return "关";
}

/**
 * Draw exposure settings (auto/manual, shutter, gain, self-timer, watermark)
 * 5 行纵向间距收紧为 22/40/58/76/94，避免与底部状态栏（UI_STATUS_Y=112）重叠
 */
static void drawExposureSettings() {
    DeviceConfig cfg = configGet();
    drawSubTitle("曝光设置");

    char buf[16];
    drawSettingRow(0, 22, "模式", cfg.exposureMode == 0 ? "自动" : "手动");
    snprintf(buf, sizeof(buf), "%d", cfg.shutterValue);
    drawSettingRow(1, 40, "快门", buf);
    snprintf(buf, sizeof(buf), "%d", cfg.gainValue);
    drawSettingRow(2, 58, "增益", buf);
    drawSettingRow(3, 76, "倒计时", shotDelayText(cfg.shotDelaySec));
    drawSettingRow(4, 94, "水印", cfg.watermark ? "开" : "关");

    drawStatusHint("左右调节 按下返回");
}

/**
 * Draw WiFi info screen with IP, RSSI, stream status
 */
static void drawWiFiInfo() {
    drawSubTitle("网络信息");

    char buf[48];

    // 标签 + 值（标签 x=6，值 x=42）
    tftDrawCN(6, 20, "IP", UI_HINT_COLOR);
    tftDrawCN(42, 20, wifiGetIP().c_str(), UI_TEXT_COLOR);

    snprintf(buf, sizeof(buf), "%d dBm", WiFi.RSSI());
    tftDrawCN(6, 36, "信号", UI_HINT_COLOR);
    tftDrawCN(42, 36, buf, UI_TEXT_COLOR);

    tftDrawCN(6, 52, "推流", UI_HINT_COLOR);
    tftDrawCN(42, 52, webServerIsStreaming() ? "推流中" : "空闲",
              webServerIsStreaming() ? 0x07E0 : 0xF800);

    // 网页录制说明
    tftDrawCN(6, 68, "网页录制", UI_HINT_COLOR);
    tftDrawCN(6, 84, "浏览器点录制", UI_TEXT_COLOR);
    tftDrawCN(6, 100, "保存 WebM", UI_TEXT_COLOR);

    drawStatusHint("按下返回");
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
            NavHelper::enterSubMenu(MenuState::LENS_SETTINGS);
            drawLensSettings();
            break;
        case 5:
            NavHelper::enterSubMenu(MenuState::EXPOSURE_SETTINGS);
            drawExposureSettings();
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
 * 删除当前照片及其缩略图，并刷新图库列表与计数
 */
static void galleryDeleteCurrent() {
    if (photoCount == 0) return;
    String base = photoListArr[currentPhotoIndex];
    if (base.startsWith("/")) base = base.substring(1);   // photoDelete 会拼上根路径

    tftShowMessage("删除中...", UI_ACCENT, 0);
    photoDelete(base.c_str());          // 主图
    SD_MMC.remove("/thumb/" + base);    // 同名缩略图，避免残留
    drawGallery();                      // 重新列目录并刷新计数
    tftShowMessage("已删除", 0x07E0, 1000);
}

/**
 * Gallery: browse saved photos (Left/Right); long-press (>=1s) deletes current photo
 */
static void handleGallery() {
    bool pressed = joystickPressed();

    // 只在"新的一次按下"时开始手势判定：进入图库那次持续按压（已在主菜单消耗）不会误判
    if (joystickPressedEdge()) {
        galleryPressActive = true;
        galleryLongFired = false;
    }

    if (galleryPressActive) {
        if (pressed && !galleryLongFired && photoCount > 0 && joystickPressedHoldMs() >= 1000) {
            galleryLongFired = true;    // 长按删除，按住期间只触发一次
            galleryDeleteCurrent();
            return;
        }
        if (!pressed) {                 // 释放
            galleryPressActive = false;
            if (!galleryLongFired) {    // 短按 -> 返回主菜单
                goBackToMain();
                return;
            }
            galleryLongFired = false;
        }
    }

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
}

/**
 * Lens settings: digital zoom, scene optimization, anti-shake
 * （菜单状态下相机已 deinit，改动写入配置，退出菜单时由 startPreviewAndCamera 统一应用）
 */
static void handleLensSettings() {
    if (NavHelper::handleUpDown(2, drawLensSettings)) return;

    DeviceConfig cfg = configGet();
    int zoom = constrain(cfg.zoomLevel, 1, 4);
    int scene = constrain(cfg.sceneMode, 0, 2);
    bool shake = cfg.antiShake;
    bool changed = false;

    if (joystickLeftEdge() || joystickRightEdge()) {
        int delta = joystickLeftEdge() ? -1 : 1;
        if (menuSelection == 0) {
            zoom = constrain(zoom + delta, 1, 4);   // 预览每帧读取配置，改完立即生效
        } else if (menuSelection == 1) {
            scene = constrain(scene + delta, 0, 2);
        } else {
            shake = !shake;
        }
        changed = true;
    }

    if (changed) {
        configSetImage(zoom, cfg.exposureMode, cfg.shutterValue, cfg.gainValue, shake, scene);
        configSave();
        drawLensSettings();
        return;
    }

    if (joystickPressedEdge()) {
        goBackToMain();
    }
}

/**
 * Exposure settings: auto/manual mode, shutter, gain, self-timer, watermark
 */
static void handleExposureSettings() {
    if (NavHelper::handleUpDown(4, drawExposureSettings)) return;

    DeviceConfig cfg = configGet();
    bool changed = false;

    if (joystickLeftEdge() || joystickRightEdge()) {
        int delta = joystickLeftEdge() ? -1 : 1;
        if (menuSelection == 0) {
            int mode = (cfg.exposureMode == 0) ? 1 : 0;                        // 自动 <-> 手动
            configSetImage(cfg.zoomLevel, mode, cfg.shutterValue, cfg.gainValue, cfg.antiShake, cfg.sceneMode);
        } else if (menuSelection == 1) {
            int shutter = constrain(cfg.shutterValue + delta * 50, 0, 1200);   // 快门步进 50
            configSetImage(cfg.zoomLevel, cfg.exposureMode, shutter, cfg.gainValue, cfg.antiShake, cfg.sceneMode);
        } else if (menuSelection == 2) {
            int gain = constrain(cfg.gainValue + delta, 0, 30);                // 增益步进 1
            configSetImage(cfg.zoomLevel, cfg.exposureMode, cfg.shutterValue, gain, cfg.antiShake, cfg.sceneMode);
        } else if (menuSelection == 3) {
            // 倒计时在 0 → 2 → 5 → 10 → 0 间循环
            int cur = cfg.shotDelaySec;
            int next;
            if (delta > 0) next = (cur == 0) ? 2 : (cur == 2) ? 5 : (cur == 5) ? 10 : 0;
            else           next = (cur == 0) ? 10 : (cur == 10) ? 5 : (cur == 5) ? 2 : 0;
            configSetShotDelay(next);
        } else {
            configSetWatermark(!cfg.watermark);                                // 水印 开/关
        }
        changed = true;
    }

    if (changed) {
        configSave();
        drawExposureSettings();
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
    handleLensSettings,      // LENS_SETTINGS
    handleExposureSettings,  // EXPOSURE_SETTINGS
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
