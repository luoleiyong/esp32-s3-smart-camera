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
// 追加：JPEG 预览 / 硬件变焦 / 上传地址与自动上传 / 录像帧率；
// previewNotice=true 时在页面顶部提示"预览模式变更重启后生效"
String htmlGetConfigPage(int brightness, int contrast, int saturation, int wbMode,
    int frameSize, int quality, bool hmirror, bool vflip,
    int zoomLevel, int exposureMode, int shutterValue, int gainValue,
    bool antiShake, int sceneMode, int shotDelaySec, bool watermark,
    bool previewJpeg, bool hwZoom, const char *uploadUrl, bool uploadAuto,
    int videoFps, bool previewNotice);

// Generate web OTA firmware update page（/update）
String htmlGetUpdatePage();

// Generate runtime status page HTML（/status）with heap / PSRAM / SD / RSSI /
// camera mode / fps / temperature / uptime / photo count
// videoHtml 为"录像文件"区块内容（由调用方遍历 SD 生成）
String htmlGetStatusPage(uint32_t freeHeap, uint32_t freePsram,
    bool sdMounted, float sdTotalMB, float sdUsedMB, int rssi,
    const char *cameraMode, float fps, float tempC,
    unsigned long uptimeSec, uint32_t photoCount, const String &videoHtml);

// Generate one recorded-video row（文件名 + 下载链接），供 /status 录像文件区块使用
String htmlGetVideoRow(const String &name);

// Generate gallery page HTML header with SD card usage stats
String htmlGetGalleryHeader(float totalMB, float usedMB, float freeMB);

// Generate HTML card for a single photo with preview, download, and delete buttons
String htmlGetPhotoCard(const String &name);

// Generate empty gallery message when no photos exist
String htmlGetEmptyGallery();

// Generate gallery page HTML footer and closing tags
String htmlGetGalleryFooter();

#endif
