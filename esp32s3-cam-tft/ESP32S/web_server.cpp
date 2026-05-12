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
#include "config_manager.h"
#include "html_pages.h"
#include <SD_MMC.h>
#include <WiFi.h>

WebServer webServer(80);
static TaskHandle_t webTaskHandle = NULL;
static volatile bool streaming = false;
static CameraMode prevCameraMode = CameraMode::PREVIEW_RGB565;

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
 * Handle photo deletion request
 */
static void handleDelete() {
    String filename = webServer.uri().substring(8);
    if (SD_MMC.remove("/" + filename)) {
        webServer.send(200);
    } else {
        webServer.send(500);
    }
}

extern TaskHandle_t cameraTaskHandle;
extern void pauseForCapture();
extern void resumeAfterCapture();
extern volatile bool cameraPaused;

/**
 * Handle remote photo capture request
 * Pauses preview, captures photo, then resumes preview
 */
static void handleCapture() {
    pauseForCapture();
    bool ok = photoCapture();
    resumeAfterCapture();
    webServer.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
}

/**
 * Handle MJPEG live stream request
 * Switches camera to STREAM_JPEG mode and continuously sends JPEG frames
 * Uses multipart/x-mixed-replace content type for browser compatibility
 */
static void handleStream() {
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

    WiFiClient c = webServer.client();
    c.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n");

    int n = 0;
    unsigned long t0 = millis();

    while (streaming && c.connected()) {
        camera_fb_t *fb = cameraCaptureFrame();
        if (!fb) { delay(10); continue; }
        if (fb->format != PIXFORMAT_JPEG) { cameraReturnFrame(fb); delay(10); continue; }

        c.print("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ");
        c.print(fb->len);
        c.print("\r\n\r\n");
        c.write((const char*)fb->buf, fb->len);
        c.print("\r\n");

        cameraReturnFrame(fb); n++;

        if (n % 30 == 0) {
            Serial.printf("[STREAM] %d frames %.1ffps\n", n, n * 1000.0f / (millis() - t0));
        }
        delay(5);
    }

    c.stop();
    unsigned long dt = millis() - t0;
    Serial.printf("[STREAM] End: %d frames %.1ffps\n", n, n * 1000.0f / dt);

    cameraSwitchMode(prevCameraMode);
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
    webServer.send(200, "text/html", htmlGetConfigPage(
        cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode,
        (int)cfg.photoFrameSize, cfg.photoQuality,
        cfg.hmirror, cfg.vflip,
        cfg.motionEnabled, cfg.motionThreshold,
        cfg.timelapseEnabled, cfg.timelapseInterval
    ));
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
    if (webServer.hasArg("hmirror")) {
        configSetFlip(
            webServer.arg("hmirror") == "1",
            webServer.arg("vflip") == "1"
        );
    }
    if (webServer.hasArg("motionEnabled")) {
        configSetMotion(
            webServer.arg("motionEnabled") == "1",
            webServer.arg("motionThreshold").toInt()
        );
    }
    if (webServer.hasArg("timelapseEnabled")) {
        configSetTimelapse(
            webServer.arg("timelapseEnabled") == "1",
            webServer.arg("timelapseInterval").toInt()
        );
    }
    configSave();
    cameraSwitchMode(CameraMode::PREVIEW_RGB565);
    DeviceConfig cfg = configGet();
    cameraApplySettings(cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode);
    cameraSetHmirror(cfg.hmirror);
    cameraSetVflip(cfg.vflip);

    webServer.sendHeader("Location", "/config");
    webServer.send(302);
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
    webServer.on("/stream", handleStream);
    webServer.on("/capture", HTTP_POST, handleCapture);
    webServer.on("/config", HTTP_GET, handleSettingsPage);
    webServer.on("/config", HTTP_POST, handleSaveSettings);
    webServer.onNotFound([]() {
        String uri = webServer.uri();
        if (uri.startsWith("/photo/")) handlePhoto();
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
