/**
 * Web Server Implementation
 * 
 * Implements HTTP endpoints for camera control, MJPEG live streaming,
 * photo gallery management, and settings configuration. Runs client
 * handling on Core 0 as a separate FreeRTOS task.
 */

#include "web_server.h"
#include "camera_driver.h"
#include "photo_capture.h"
#include "photo_upload.h"
#include "video_record.h"
#include "config_manager.h"
#include "html_pages.h"
#include "system_stats.h"
#include <SD_MMC.h>
#include <WiFi.h>
#include <Update.h>

WebServer webServer(80);
static TaskHandle_t webTaskHandle = NULL;
static volatile bool streaming = false;
static CameraMode prevCameraMode = CameraMode::PREVIEW_RGB565;
static bool previewNotice = false;   // 保存设置后是否提示"预览模式重启后生效"

/**
 * Handle root page request
 * Displays main dashboard with SD card usage info
 */
static void handleRoot() {
    uint64_t totalBytes = SD_MMC.totalBytes();
    uint64_t usedBytes = SD_MMC.usedBytes();
    float totalMB = totalBytes / (1024.0 * 1024.0);
    float usedMB = usedBytes / (1024.0 * 1024.0);
    webServer.send(200, "text/html", htmlGetRootPage(totalMB, usedMB));
}

/**
 * Handle gallery page request
 * Lists all photos on SD card with thumbnail cards
 */
static void handleGallery() {
    uint64_t totalBytes = SD_MMC.totalBytes();
    uint64_t usedBytes = SD_MMC.usedBytes();
    uint64_t freeBytes = totalBytes - usedBytes;
    float totalMB = totalBytes / (1024.0 * 1024.0);
    float usedMB = usedBytes / (1024.0 * 1024.0);
    float freeMB = freeBytes / (1024.0 * 1024.0);

    String html = htmlGetGalleryHeader(totalMB, usedMB, freeMB);
    File root = SD_MMC.open("/");
    File file = root.openNextFile();
    int photoCount = 0;

    while (file) {
        if (!file.isDirectory()) {
            String name = file.name();
            if (name.endsWith(".jpg")) {
                photoCount++;
                html += htmlGetPhotoCard(name);
            }
        }
        file = root.openNextFile();
    }

    if (photoCount == 0) html += htmlGetEmptyGallery();
    html += htmlGetGalleryFooter();
    webServer.send(200, "text/html", html);
}

/**
 * Serve individual photo file as JPEG stream
 */
static void handlePhoto() {
    String filename = webServer.uri().substring(7);
    if (filename.length() == 0) { webServer.send(400); return; }

    File file = SD_MMC.open("/" + filename, FILE_READ);
    if (file) {
        webServer.streamFile(file, "image/jpeg");
        file.close();
    } else {
        webServer.send(404);
    }
}

/**
 * Serve individual recorded AVI file as video stream
 */
static void handleVideo() {
    String filename = webServer.uri().substring(7);   // 去掉 "/video/"
    if (filename.length() == 0) { webServer.send(400); return; }

    File file = SD_MMC.open("/" + filename, FILE_READ);
    if (file) {
        webServer.streamFile(file, "video/x-msvideo");
        file.close();
    } else {
        webServer.send(404);
    }
}

/**
 * Handle photo deletion request
 */
static void handleDelete() {
    String filename = webServer.uri().substring(8);
    if (SD_MMC.remove("/" + filename)) {
        SD_MMC.remove("/thumb/" + filename);   // 同步清理缩略图，避免 /thumb 残留
        webServer.send(200);
    } else {
        webServer.send(500);
    }
}

/**
 * Handle manual photo upload request（GET /upload?f=<文件名>）
 * 从 SD 读取该照片并 POST 到配置的上传地址，返回纯文本结果 OK / FAIL:原因
 */
static void handleUpload() {
    if (!webServer.hasArg("f")) { webServer.send(400, "text/plain", "FAIL:no file"); return; }
    String name = webServer.arg("f");
    name.replace("/", "");   // 去掉路径分隔符，防目录穿越
    if (name.length() == 0 || !photoExists(name.c_str())) {
        webServer.send(404, "text/plain", "FAIL:not found");
        return;
    }
    bool ok = photoUploadFile(("/" + name).c_str());
    webServer.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL:upload");
}

/**
 * Handle web OTA update page（GET /update）
 */
static void handleUpdatePage() {
    webServer.send(200, "text/html", htmlGetUpdatePage());
}

/**
 * OTA 收尾：先发响应再重启（留 500ms 让响应发完）
 */
static void handleUpdateDone() {
    webServer.send(200, "text/html", Update.hasError()
        ? "<meta charset='UTF-8'>升级失败，请重试"
        : "<meta charset='UTF-8'>升级成功，设备重启中...");
    delay(500);
    ESP.restart();
}

/**
 * OTA 数据流：webServer.upload() 逐块到来，直接写入 flash，不做整包缓冲
 */
static void handleUpdateUpload() {
    HTTPUpload &u = webServer.upload();
    if (u.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA] Start: %s\n", u.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Serial.printf("[OTA] begin failed: %s\n", Update.errorString());
        }
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (Update.write(u.buf, u.currentSize) != u.currentSize) {
            Serial.printf("[OTA] write failed: %s\n", Update.errorString());
        }
    } else if (u.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[OTA] Success: %u bytes\n", (unsigned)u.totalSize);
        else Serial.printf("[OTA] end failed: %s\n", Update.errorString());
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        Serial.println("[OTA] Aborted");
    }
}

extern TaskHandle_t cameraTaskHandle;
extern void pauseForCapture();
extern void resumeAfterCapture();
extern volatile bool cameraPaused;

/**
 * Handle device-side recording request (GET/POST /record?action=start|stop)
 * 缺省 action 视为切换；返回纯文本 REC / STOP / FAIL:原因。
 * 与推流、拍照互斥，避免争抢相机。
 */
static void handleRecord() {
    String action = webServer.hasArg("action") ? webServer.arg("action") : "";
    bool wantStart = (action == "start") || (action.length() == 0 && !videoRecordIsActive());

    if (!wantStart) {   // 停止
        if (!videoRecordIsActive()) { webServer.send(409, "text/plain", "FAIL:未在录像"); return; }
        videoRecordStop();
        webServer.send(200, "text/plain", "STOP");
        return;
    }

    if (webServerIsStreaming()) { webServer.send(409, "text/plain", "FAIL:正在推流"); return; }
    if (videoRecordIsActive()) { webServer.send(200, "text/plain", "REC"); return; }   // 已在录像
    if (!videoRecordStart()) { webServer.send(500, "text/plain", "FAIL:启动失败"); return; }
    webServer.send(200, "text/plain", "REC");
}

/**
 * Handle remote photo capture request
 * Pauses preview, captures photo, then resumes preview
 */
static void handleCapture() {
    if (videoRecordIsActive()) { webServer.send(409, "text/plain", "FAIL:正在录像"); return; }
    photoSelfTimer();   // 自拍倒计时（0=关），必须在 pauseForCapture() 之前
    pauseForCapture();
    bool ok = photoCapture();
    resumeAfterCapture();
    if (ok) statsOnPhoto();
    webServer.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
}

/**
 * Handle MJPEG live stream request
 * Switches camera to STREAM_JPEG mode and continuously sends JPEG frames
 * Uses multipart/x-mixed-replace content type for browser compatibility
 */
static void handleStream() {
    if (videoRecordIsActive()) {
        webServer.send(503, "text/plain", "正在录像，请先停止录像");
        return;
    }
    Serial.println("[STREAM] Connected");
    streaming = true;
    cameraPaused = true;
    cameraLock();
    delay(150);

    prevCameraMode = cameraGetCurrentMode();
    Serial.printf("[STREAM] Mode %d -> STREAM_JPEG\n", (int)prevCameraMode);

    if (!cameraSwitchMode(CameraMode::STREAM_JPEG)) {
        Serial.println("[STREAM] Switch FAILED");
        cameraUnlock(); cameraPaused = false; streaming = false;
        webServer.send(500);
        return;
    }

    // 推流同样沿用用户的画质与曝光设置（切模式会把传感器重置为默认值）
    DeviceConfig cfg = configGet();
    cameraApplySettings(cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode);
    cameraApplyScene(cfg.sceneMode);
    cameraApplyExposure(cfg.exposureMode, cfg.shutterValue, cfg.gainValue);
    // 硬件窗口变焦（实验，hwZoom 开启时）：让推流画面也带变焦；
    // 返回 false（帧尺寸不支持等）则保持整幅画面，不做任何软件裁剪
    if (cfg.hwZoom && cfg.zoomLevel > 1) {
        cameraApplySensorZoom(cfg.zoomLevel);
    }

    WiFiClient c = webServer.client();
    c.setNoDelay(true);   // 关闭 Nagle：帧数据不等小包合并，降低延迟、提高推流帧率
    c.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n");

    int n = 0;
    unsigned long t0 = millis();

    while (streaming && c.connected()) {
        camera_fb_t *fb = cameraCaptureFrame();
        if (!fb) { delay(10); continue; }
        if (fb->format != PIXFORMAT_JPEG) { cameraReturnFrame(fb); delay(10); continue; }

        // 帧头先拼成一段再一次写出（原先 4 次 print 会拆成多个 TCP 小包）
        char header[96];
        int hlen = snprintf(header, sizeof(header),
                            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                            (unsigned)fb->len);
        c.write((const uint8_t *)header, (size_t)hlen);
        c.write((const uint8_t *)fb->buf, fb->len);
        c.write((const uint8_t *)"\r\n", 2);

        cameraReturnFrame(fb); n++;

        if (n % 30 == 0) {
            Serial.printf("[STREAM] %d frames %.1ffps\n", n, n * 1000.0f / (millis() - t0));
        }
        delay(5);
    }

    c.stop();
    unsigned long dt = millis() - t0;
    Serial.printf("[STREAM] End: %d frames %.1ffps\n", n, n * 1000.0f / dt);

    cameraSwitchMode(cameraPreviewMode());   // 按配置回到 JPEG 或 RGB565 预览
    // 切回预览模式同样会重置传感器，重新套用用户设置（否则推流结束后画质设置会丢失）
    cfg = configGet();
    cameraApplySettings(cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode);
    cameraApplyScene(cfg.sceneMode);
    cameraApplyExposure(cfg.exposureMode, cfg.shutterValue, cfg.gainValue);
    cameraUnlock();
    cameraPaused = false;
    streaming = false;
}

/**
 * Handle settings page request
 * Displays current configuration in editable form
 */
static void handleSettingsPage() {
    DeviceConfig cfg = configGet();
    bool notice = previewNotice;   // 一次性提示：读取后即清除
    previewNotice = false;
    webServer.send(200, "text/html", htmlGetConfigPage(
        cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode,
        (int)cfg.photoFrameSize, cfg.photoQuality,
        cfg.hmirror, cfg.vflip,
        cfg.zoomLevel, cfg.exposureMode, cfg.shutterValue, cfg.gainValue,
        cfg.antiShake, cfg.sceneMode, cfg.shotDelaySec, cfg.watermark,
        cfg.previewJpeg, cfg.hwZoom, cfg.uploadUrl, cfg.uploadAuto,
        cfg.videoFps, notice
    ));
}

/**
 * 读取整型表单字段，字段缺失或为空时返回默认值
 */
static int argIntOr(const char *name, int def) {
    if (!webServer.hasArg(name)) return def;
    String v = webServer.arg(name);
    if (v.length() == 0) return def;
    return v.toInt();
}

/**
 * Handle settings save request
 * Updates configuration and applies changes to camera hardware
 */
static void handleSaveSettings() {
    if (webServer.hasArg("brightness")) {
        configSetCameraParams(
            webServer.arg("brightness").toInt(),
            webServer.arg("contrast").toInt(),
            webServer.arg("saturation").toInt(),
            webServer.arg("wbMode").toInt()
        );
    }
    if (webServer.hasArg("frameSize")) {
        configSetPhotoParams(
            (framesize_t)webServer.arg("frameSize").toInt(),
            webServer.arg("quality").toInt()
        );
    }
    // 复选框：未勾选时浏览器根本不提交该字段，因此不能再用 hasArg 包起来判断，
    // 否则开关一旦打开就永远关不掉（表现就是"点了没反应"）
    configSetFlip(
        webServer.arg("hmirror") == "1",
        webServer.arg("vflip") == "1"
    );

    // 图像增强：数字变焦 / 曝光模式与快门增益 / 手持防抖 / 场景优化
    // （字段缺失时沿用当前值，避免表单不带某字段时被清零）
    DeviceConfig old = configGet();
    int zoomLevel = constrain(argIntOr("zoomLevel", old.zoomLevel), 1, 4);
    int exposureMode = constrain(argIntOr("exposureMode", old.exposureMode), 0, 1);
    int shutterValue = constrain(argIntOr("shutterValue", old.shutterValue), 0, 1200);
    int gainValue = constrain(argIntOr("gainValue", old.gainValue), 0, 30);
    int sceneMode = constrain(argIntOr("sceneMode", old.sceneMode), 0, 2);
    configSetImage(zoomLevel, exposureMode, shutterValue, gainValue,
                   webServer.arg("antiShake") == "1", sceneMode);

    // 自拍倒计时：只接受 0/2/5/10，其余就近取值（用 argIntOr 兼容字段缺失）
    int shotDelay = constrain(argIntOr("shotDelay", old.shotDelaySec), 0, 10);
    if (shotDelay <= 1) shotDelay = 0;
    else if (shotDelay <= 3) shotDelay = 2;
    else if (shotDelay <= 7) shotDelay = 5;
    else shotDelay = 10;
    configSetShotDelay(shotDelay);
    configSetWatermark(webServer.arg("watermark") == "1");   // 复选框未勾选时不提交该字段

    // 高级功能：JPEG 预览 / 硬件窗口变焦 / 照片上传地址与自动上传 / 录像帧率
    bool newPreviewJpeg = webServer.arg("previewJpeg") == "1";
    bool newHwZoom = webServer.arg("hwZoom") == "1";
    String upUrl = webServer.arg("uploadUrl");
    bool newUploadAuto = webServer.arg("uploadAuto") == "1";
    int vFps = argIntOr("videoFps", old.videoFps);
    if (vFps != 5 && vFps != 10 && vFps != 15) vFps = 10;
    configSetAdvanced(newPreviewJpeg, newHwZoom, upUrl.c_str(), newUploadAuto, vFps);

    // 预览模式/硬件变焦改变：不影响当前会话，标注待下次进入预览/重启后生效
    if (old.previewJpeg != newPreviewJpeg || old.hwZoom != newHwZoom) previewNotice = true;

    configSave();
    cameraSwitchMode(cameraPreviewMode());   // 按配置切回 JPEG 或 RGB565 预览
    DeviceConfig cfg = configGet();
    cameraApplySettings(cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode);
    cameraApplyScene(cfg.sceneMode);
    cameraApplyExposure(cfg.exposureMode, cfg.shutterValue, cfg.gainValue);
    cameraSetHmirror(cfg.hmirror);
    cameraSetVflip(cfg.vflip);

    webServer.sendHeader("Location", "/config");
    webServer.send(302);
}

/**
 * 相机模式枚举名（供 /status 页面展示）
 */
static const char *cameraModeName(CameraMode m) {
    switch (m) {
    case CameraMode::PREVIEW_RGB565: return "PREVIEW_RGB565";
    case CameraMode::PREVIEW_JPEG:   return "PREVIEW_JPEG";
    case CameraMode::PHOTO_JPEG:     return "PHOTO_JPEG";
    case CameraMode::VIDEO_JPEG:     return "VIDEO_JPEG";
    case CameraMode::STREAM_JPEG:    return "STREAM_JPEG";
    }
    return "UNKNOWN";
}

/**
 * Handle runtime status page request (/status)
 * 只做轻量读取：不切相机模式、不读 SD 大文件，避免拖慢网页任务
 */
static void handleStatus() {
    bool sdMounted = (SD_MMC.totalBytes() > 0);
    uint64_t totalBytes = SD_MMC.totalBytes();
    uint64_t usedBytes = SD_MMC.usedBytes();
    const float MB = 1024.0f * 1024.0f;

    // 录像文件区块：遍历 SD 根目录列出最多 20 个 video_*.avi，给下载链接
    String videoHtml;
    if (sdMounted) {
        File root = SD_MMC.open("/");
        File file = root.openNextFile();
        int n = 0;
        while (file && n < 20) {
            if (!file.isDirectory()) {
                String nm = file.name();
                int slash = nm.lastIndexOf('/');     // 兼容 name() 是否带路径
                if (slash >= 0) nm = nm.substring(slash + 1);
                if (nm.startsWith("video_") && nm.endsWith(".avi")) {
                    videoHtml += htmlGetVideoRow(nm);
                    n++;
                }
            }
            file = root.openNextFile();
        }
    }
    if (videoHtml.length() == 0) videoHtml = F("<div class='st'><span>暂无录像文件</span><span class='sv'>--</span></div>");

    webServer.send(200, "text/html", htmlGetStatusPage(
        ESP.getFreeHeap(),
        ESP.getFreePsram(),
        sdMounted,
        totalBytes / MB,
        usedBytes / MB,
        WiFi.RSSI(),
        cameraModeName(cameraGetCurrentMode()),
        statsGetFps(),
        temperatureRead(),
        statsGetUptimeSec(),
        statsGetPhotoCount(),
        videoHtml
    ));
}

/**
 * FreeRTOS task for handling HTTP client requests
 * Runs on Core 0 to offload web server from main loop
 */
static void webTaskFunc(void *parameter) {
    for (;;) {
        webServer.handleClient();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/**
 * Initialize web server with all route handlers
 * Creates FreeRTOS task for client handling on Core 0
 */
void webServerInit() {
    webServer.on("/", handleRoot);
    webServer.on("/list", handleGallery);
    webServer.on("/status", handleStatus);
    webServer.on("/stream", handleStream);
    webServer.on("/record", handleRecord);                        // 机内录像 开始/停止
    webServer.on("/capture", HTTP_POST, handleCapture);
    webServer.on("/config", HTTP_GET, handleSettingsPage);
    webServer.on("/config", HTTP_POST, handleSaveSettings);
    webServer.on("/upload", handleUpload);                        // 手动上传图库照片
    webServer.on("/update", HTTP_GET, handleUpdatePage);          // 网页 OTA 升级页
    webServer.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    webServer.onNotFound([]() {
        String uri = webServer.uri();
        if (uri.startsWith("/photo/")) handlePhoto();
        else if (uri.startsWith("/video/")) handleVideo();
        else if (uri.startsWith("/delete/")) handleDelete();
        else webServer.send(404);
    });
    webServer.begin();

    xTaskCreatePinnedToCore(webTaskFunc, "WebTask", 8192, NULL, 2, &webTaskHandle, 0);
    Serial.println("Web:80 Core0");
}

/**
 * Deprecated: Web server now runs in dedicated task
 */
void webServerHandleClient() {}

void webServerStartStream() { streaming = true; }
void webServerStopStream() { streaming = false; }
bool webServerIsStreaming() { return streaming; }
