/**
 * ESP32-S3 Smart Camera - Main Entry Point
 * 
 * This is the main Arduino sketch that orchestrates all camera subsystems:
 * - Camera preview and capture
 * - TFT display and UI rendering
 * - Joystick input handling
 * - WiFi connectivity and web server
 * - Image enhancement: digital zoom, manual shutter/exposure, anti-shake,
 *   scene optimization (night / backlight)
 * - Menu navigation system
 * 
 * Architecture: Dual-core design with camera preview running on Core 1
 * and main control loop running on Core 0 for optimal performance.
 */

#include "camera_config.h"
#include "camera_driver.h"
#include "tft_display.h"
#include "joystick.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "ota_update.h"
#include "photo_capture.h"
#include "video_record.h"
#include "menu_system.h"
#include "led_status.h"
#include "system_stats.h"
#include <SD_MMC.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

// FreeRTOS task handle for camera preview task (runs on Core 1)
TaskHandle_t cameraTaskHandle = NULL;
volatile bool cameraPaused = false;
static volatile bool btnPressed = false;
static volatile unsigned long btnPressTime = 0;
static volatile bool capturing = false;

// ===== 实体键手势状态机（单击/双击/长按，非阻塞判定）=====
static bool btnDown = false;                    // 上一轮采样到的按键电平（按下=LOW）
static unsigned long btnDownTime = 0;           // 本次按下起始时刻
static bool longPressFired = false;             // 本次按住是否已触发长按（保证只触发一次）
static bool pendingClick = false;               // 已松开一次，等待双击窗口结束判定单击
static unsigned long pendingClickTime = 0;      // 第一次松开时刻（双击窗口起点）
static bool consumedPress = false;              // 本次按压已被双击/长按消费，松开时不再视为单击

static const unsigned long CLICK_MIN_MS   = 30;   // 去抖下限：短于此时长视为抖动
static const unsigned long LONG_PRESS_MS  = 600;  // 长按阈值（按住 ≥600ms）
static const unsigned long DOUBLE_CLICK_MS = 400; // 双击窗口（第一次松开→第二次按下）

void startPreviewAndCamera();   // 前置声明：供预览任务自愈时调用

/**
 * Hardware interrupt handler for physical button (IO0)
 * Records button press event and timestamp for debouncing in main loop
 */
void IRAM_ATTR onButtonPress() {
    if (digitalRead(BUTTON_PIN) == LOW && !capturing) {
        btnPressed = true;
        btnPressTime = millis();
    }
}

/**
 * Core 1 Task: Camera Preview
 * Continuously renders camera frames to TFT display when in preview mode.
 * 连续 3 次取帧失败时自愈（重新初始化相机），两次自愈间隔 ≥10 秒避免重启风暴；
 * 任务已订阅看门狗，每轮循环与重初始化前喂狗。
 */
static void cameraTask(void *parameter) {
    esp_task_wdt_add(NULL);   // 订阅当前预览任务到任务看门狗
    static int failCount = 0;                 // 连续取帧失败次数
    static unsigned long lastHealTime = 0;    // 上次自愈时刻（节流用）

    for (;;) {
        esp_task_wdt_reset();   // 每轮循环喂狗（单轮耗时远小于看门狗超时）

        if (currentMenuState == MenuState::PREVIEW && !cameraPaused && !cameraIsLocked()) {
            camera_fb_t *fb = cameraCaptureFrame();
            if (fb) {
                failCount = 0;
                statsOnFrame();                   // 统计预览帧率（1 秒滑动窗口）
                if (fb->format == PIXFORMAT_JPEG) {
                    tftDrawPreviewFrameJPEG(fb);  // JPEG 预览：TJpgDec 解码（内部加锁/变焦）
                } else {
                    tftDrawPreviewFrame(fb);      // RGB565 预览：内部处理数字变焦与字节序
                }
                cameraReturnFrame(fb);
            } else {
                failCount++;
                // 连续 3 次取帧失败且距上次自愈 ≥10 秒 → 执行一次自愈
                if (failCount >= 3 && (millis() - lastHealTime >= 10000)) {
                    Serial.println("[CAM] 3 consecutive frame failures, restarting camera");
                    tftShowMessage("相机重启", UI_ACCENT, 0);
                    esp_task_wdt_reset();         // 重初始化前喂狗
                    cameraDeinit();               // 先释放，否则 esp_camera_init 会因"已初始化"失败
                    vTaskDelay(pdMS_TO_TICKS(100));
                    startPreviewAndCamera();      // 重新初始化并重放用户设置
                    esp_task_wdt_reset();
                    lastHealTime = millis();
                    failCount = 0;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(PREVIEW_INTERVAL_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/**
 * Start dual-core task distribution
 * Creates camera preview task pinned to Core 1
 */
static void startDualCore() {
    xTaskCreatePinnedToCore(cameraTask, "CameraTask", 8192, NULL, 1, &cameraTaskHandle, 1);
}

/**
 * Stop camera preview and deinitialize camera hardware
 * Used when entering menu system to free resources
 */
void stopPreviewAndCamera() {
    cameraPaused = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    cameraDeinit();
    Serial.println("[CAM] Deinit for menu");
}

/**
 * Reinitialize camera for preview mode with saved configuration
 * Applies user settings (brightness, contrast, etc.) from persistent storage
 */
void startPreviewAndCamera() {
    cameraInit(cameraPreviewMode());   // 按配置选择 JPEG/RGB565 预览模式
    DeviceConfig cfg = configGet();
    cameraApplySettings(cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode);
    cameraApplyScene(cfg.sceneMode);
    cameraApplyExposure(cfg.exposureMode, cfg.shutterValue, cfg.gainValue);
    cameraSetHmirror(cfg.hmirror);
    cameraSetVflip(cfg.vflip);
    cameraPaused = false;
    Serial.println("[CAM] Reinit for preview");
}

/**
 * Pause camera preview and lock frame buffer before photo capture
 * Ensures exclusive access to camera hardware during capture
 */
void pauseForCapture() {
    cameraPaused = true;
    cameraLock();
    vTaskDelay(pdMS_TO_TICKS(150));
}

/**
 * Resume camera preview after photo capture completes
 */
void resumeAfterCapture() {
    cameraUnlock();
    cameraPaused = false;
}

/**
 * 双击动作：在 1x / 2x 之间切换数字变焦并持久化
 * 预览帧每帧读取 configGet().zoomLevel，因此写入配置后即时生效
 */
static void toggleZoom() {
    DeviceConfig cfg = configGet();
    int newZoom = (cfg.zoomLevel >= 2) ? 1 : 2;
    configSetImage(newZoom, cfg.exposureMode, cfg.shutterValue,
                   cfg.gainValue, cfg.antiShake, cfg.sceneMode);
    configSave();
    tftShowMessage(newZoom >= 2 ? "变焦 2x" : "变焦 1x", UI_ACCENT, 800);
}

/**
 * 执行一次完整的按键拍照流程（单击触发）
 * 停推流 → 自拍倒计时 → 暂停预览 → 快门动画 → 拍照 → 提示 → 恢复预览
 */
static void doCaptureFlow() {
    capturing = true;
    Serial.println("[BTN] Capture");
    esp_task_wdt_reset();   // 长耗时的拍照/写卡前喂狗

    if (webServerIsStreaming()) {
        Serial.println("[BTN] Stop stream first...");
        webServerStopStream();
        delay(800);
    }

    photoSelfTimer();   // 自拍倒计时（0=关，期间预览照常刷新），必须在 pauseForCapture 之前
    pauseForCapture();

    tftShutterAnimation();
    bool ok = photoCapture();

    if (ok) {
        Serial.println("[BTN] Photo saved OK!");
        tftShowPhotoSaved("OK!");
        statsOnPhoto();                    // 累计拍照数 +1
        ledStatusSet(LedState::CAPTURE);   // 拍照成功后才白闪一次（曝光期间保持基础状态）
    } else {
        Serial.println("[BTN] Photo FAILED!");
        tftShowMessage("Fail!", ST77XX_RED, 2000);
        ledStatusSet(LedState::ERROR);     // 拍照失败：红闪提示
    }

    tftDrawUIFrame();
    tftUpdateStatusBar(wifiIsConnected(), wifiGetIP().c_str());

    resumeAfterCapture();
    capturing = false;
}

/**
 * 实体键手势状态机（非阻塞，由主循环周期调用）
 * 单击=拍照、双击=切 1x/2x 变焦、长按=进入/退出菜单；
 * 三种手势互不误触发：长按与双击会"吃掉"待判定的单击，且 capturing 期间忽略新手势。
 */
static void handleButtonGestures() {
    // 拍照/倒计时期间忽略新手势，并同步电平避免结束后误判
    if (capturing) {
        btnPressed = false;
        btnDown = (digitalRead(BUTTON_PIN) == LOW);
        if (btnDown) btnDownTime = millis();
        longPressFired = false;
        pendingClick = false;
        consumedPress = false;
        return;
    }

    // 录像期间忽略按键手势（避免与录像抢相机、误触拍照）
    if (videoRecordIsActive()) {
        btnPressed = false;
        btnDown = (digitalRead(BUTTON_PIN) == LOW);
        if (btnDown) btnDownTime = millis();
        longPressFired = false;
        pendingClick = false;
        consumedPress = false;
        return;
    }

    unsigned long now = millis();
    bool down = (digitalRead(BUTTON_PIN) == LOW);

    // 单击超时：第一次松开后 400ms 内无第二次按下 → 执行拍照
    if (pendingClick && (now - pendingClickTime >= DOUBLE_CLICK_MS)) {
        pendingClick = false;
        doCaptureFlow();
        btnDown = (digitalRead(BUTTON_PIN) == LOW);   // 同步按键状态，避免残留造成误判
        if (btnDown) btnDownTime = millis();
        longPressFired = false;
        return;
    }

    // 按下沿：ISR 锁存兜底 + 轮询采样兜底，避免两次轮询之间漏掉快速点按
    bool pressEdge = false;
    if (btnPressed) { btnPressed = false; if (!btnDown) pressEdge = true; }
    if (down && !btnDown) pressEdge = true;

    if (pressEdge) {
        btnDown = true;
        btnDownTime = now;
        longPressFired = false;
        // 第二次按下落在双击窗口内 → 双击（立即触发，并消费本次按压）
        if (pendingClick && (now - pendingClickTime < DOUBLE_CLICK_MS)) {
            pendingClick = false;
            consumedPress = true;
            Serial.println("[BTN] Double click -> zoom");
            toggleZoom();
        }
    } else if (down) {
        // 长按：按住 ≥600ms 触发一次，松开前不重复触发
        if (!longPressFired && (now - btnDownTime >= LONG_PRESS_MS)) {
            longPressFired = true;
            pendingClick = false;     // 吃掉待判定的单击，避免长按后又误拍照
            consumedPress = true;
            Serial.println("[BTN] Long press -> menu");
            if (currentMenuState == MenuState::PREVIEW) menuEnterMain();
            else menuExit();
        }
    } else if (btnDown) {
        // 松开沿：仅当既非长按、也未被双击消费时，才把本次按压作为单击候选
        unsigned long hold = now - btnDownTime;
        btnDown = false;
        if (!longPressFired && !consumedPress && hold >= CLICK_MIN_MS) {
            pendingClick = true;
            pendingClickTime = now;
        }
        longPressFired = false;
        consumedPress = false;
    }
}

/**
 * Arduino Setup Function
 * Initializes all hardware subsystems and starts dual-core operation
 */
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== ESP32-S3 Smart Camera ===");

    // 初始化板载 RGB LED（WS2812）状态指示，替代原先的一次性熄灭调用
    ledStatusInit();

    // 锁定最高主频，避免被降频运行
    setCpuFrequencyMhz(240);
    Serial.printf("CPU: %d MHz, PSRAM: %.1f MB\n",
                  getCpuFrequencyMhz(), ESP.getPsramSize() / 1024.0f / 1024.0f);

    // Configure hardware button interrupt
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

    // Initialize configuration storage and joystick input
    configInit();
    joystickInit();
    statsInit();   // 运行统计（帧率/拍照数/运行时长）清零

    // Initialize camera hardware in preview mode（按配置选择 JPEG/RGB565）
    if (!cameraInit(cameraPreviewMode())) {
        Serial.println("Camera FAIL");
        ledStatusSet(LedState::ERROR);   // 红灯提示相机初始化失败
        ledStatusUpdate();               // 死循环前至少刷新一次，让红灯可见
        while (1);
    }

    // Apply saved camera settings from persistent storage
    DeviceConfig cfg = configGet();
    cameraApplySettings(cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode);
    cameraApplyScene(cfg.sceneMode);
    cameraApplyExposure(cfg.exposureMode, cfg.shutterValue, cfg.gainValue);
    cameraSetFrameSize(cfg.photoFrameSize);
    cameraSetHmirror(cfg.hmirror);
    cameraSetVflip(cfg.vflip);

    // Initialize TFT display
    if (!tftInit()) { Serial.println("TFT FAIL"); while (1); }
    // 先点亮界面并启动预览：这样即使 WiFi 连接很慢（甚至连不上），
    // 屏幕也立刻有 UI 和实时画面，不会再出现"开机一直白屏"的观感
    tftDrawUIFrame();
    tftUpdateStatusBar(wifiIsConnected(), wifiGetIP().c_str());
    startDualCore();

    // Initialize SD card storage (SD_MMC 1-bit mode)
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    if (!SD_MMC.begin("/sdcard", true)) {
        tftShowMessage("SD Err!", ST77XX_RED, 3000);
    }

    // Initialize WiFi connectivity and web server
    bool wifiOk = wifiInit();
    if (wifiOk) {
        Serial.printf("WiFi: %s\n", wifiGetIP().c_str());
        webServerInit();
        otaInit();
    } else if (wifiGetState() == WiFiState::AP_MODE) {
        Serial.printf("AP: %s\n", wifiGetIP().c_str());
    }

    // 启动流程收尾（预览任务已在 TFT 初始化后启动）
    tftUpdateStatusBar(wifiIsConnected(), wifiGetIP().c_str());   // WiFi 连上后刷新状态栏
    menuInit();
    Serial.println("Ready!");
}

/**
 * Arduino Main Loop
 * Handles menu navigation, OTA updates, status bar refresh, and button capture
 */
void loop() {
    menuHandleNavigation();

    if (wifiIsConnected()) {
        otaHandle();
    } else if (wifiGetState() == WiFiState::AP_MODE) {
        wifiHandleAPClient();
    }

    if (currentMenuState == MenuState::PREVIEW) {
        static unsigned long lastStatusUpdate = 0;
        if (millis() - lastStatusUpdate > 1000) {
            tftUpdateStatusBar(wifiIsConnected(), wifiGetIP().c_str());
            lastStatusUpdate = millis();
        }
    }

    // 实体键手势判定：单击拍照 / 双击切 1x-2x 变焦 / 长按进出菜单（非阻塞状态机）
    handleButtonGestures();

    // 板载 LED：按联网情况维护基础状态（一次性提示播放期间不会被覆盖）
    if (wifiIsConnected()) {
        ledStatusSet(LedState::IDLE);
    } else if (wifiGetState() == WiFiState::AP_MODE) {
        ledStatusSet(LedState::AP_MODE);
    } else {
        ledStatusSet(LedState::NO_WIFI);
    }
    ledStatusUpdate();   // 非阻塞刷新（仅 millis() 判时）

    // 机内录像：到帧间隔抓一帧写 AVI（未录像时立即返回，不影响既有行为）
    videoRecordUpdate();

    // Yield: without this the loop spins Core 1 at 100% and heats up
    delay(5);
}
