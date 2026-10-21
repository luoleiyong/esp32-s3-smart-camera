/**
 * Camera Driver Implementation
 * 
 * Handles low-level camera initialization, configuration, and frame management.
 * Uses ESP-IDF camera driver with support for multiple operating modes.
 */

#include "camera_driver.h"
#include "config_manager.h"   // cameraPreviewMode() 读取 previewJpeg 配置

// Current camera state tracking
static CameraMode currentCameraMode = CameraMode::PREVIEW_RGB565;
static framesize_t currentFrameSize = PREVIEW_FRAME_SIZE;
static volatile bool cameraLocked = false;

/**
 * Populate camera configuration structure based on operating mode
 * Configures GPIO pins, pixel format, frame size, and buffer count
 */
static void fillCameraConfig(camera_config_t &config, CameraMode mode) {
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    // Configure mode-specific settings
    switch (mode) {
        case CameraMode::PREVIEW_RGB565:
            config.pixel_format = PIXFORMAT_RGB565;
            config.frame_size = PREVIEW_FRAME_SIZE;
            // Single buffer + on-demand capture: the sensor only outputs a frame
            // when requested, which cuts power consumption and heat noticeably
            config.fb_count = 1;
            config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
            break;
        case CameraMode::PHOTO_JPEG:
            config.pixel_format = PIXFORMAT_JPEG;
            config.frame_size = currentFrameSize;
            config.jpeg_quality = PHOTO_QUALITY;
            config.fb_count = 1;
            break;
        case CameraMode::VIDEO_JPEG:
            config.pixel_format = PIXFORMAT_JPEG;
            config.frame_size = FRAMESIZE_240X240;
            config.jpeg_quality = 12;
            config.fb_count = 2;
            break;
        case CameraMode::STREAM_JPEG:
            config.pixel_format = PIXFORMAT_JPEG;
            config.frame_size = FRAMESIZE_SVGA;
            config.jpeg_quality = 12;
            config.fb_count = 2;
            break;
        case CameraMode::PREVIEW_JPEG:
            // 传感器直接输出小尺寸 JPEG，由 TJpgDec 解码到屏幕：
            // 相比 RGB565 大幅降低 PSRAM 带宽与发热；
            // 单缓冲 + 按需抓帧，省电策略与 RGB565 预览一致
            config.pixel_format = PIXFORMAT_JPEG;
            config.frame_size = PREVIEW_FRAME_SIZE;
            config.jpeg_quality = PREVIEW_QUALITY;
            config.fb_count = 1;
            config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
            break;
    }
}

/**
 * Initialize camera hardware with specified operating mode
 * Applies default image settings after successful initialization
 */
bool cameraInit(CameraMode mode) {
    camera_config_t config;
    fillCameraConfig(config, mode);

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return false;
    }

    currentCameraMode = mode;

    // Apply default image sensor settings
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
        s->set_special_effect(s, 0);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_wb_mode(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 0);
        s->set_ae_level(s, 2);       // 自动曝光目标再亮一档（-2~2），改善画面偏暗
        s->set_aec_value(s, 300);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)6);   // 最大增益 128X（原 8X），暗光下 AGC 才加得上去
        s->set_bpc(s, 0);
        s->set_wpc(s, 1);
        s->set_raw_gma(s, 1);
        s->set_lenc(s, 1);
        s->set_hmirror(s, 0);
        s->set_vflip(s, 0);
        s->set_dcw(s, 1);
        s->set_colorbar(s, 0);
    }

    return true;
}

/**
 * Deinitialize camera and release hardware resources
 */
void cameraDeinit() {
    esp_camera_deinit();
}

/**
 * Switch camera operating mode by deinitializing and reinitializing
 * Returns true if mode switch successful
 */
bool cameraSwitchMode(CameraMode newMode) {
    if (newMode == currentCameraMode) return true;
    cameraDeinit();
    delay(100);
    bool ok = cameraInit(newMode);
    if (ok) {
        delay(100);
    }
    return ok;
}

/**
 * Capture single frame from camera (blocking)
 * Returns frame buffer pointer or NULL on failure
 */
camera_fb_t* cameraCaptureFrame() {
    return esp_camera_fb_get();
}

/**
 * Return frame buffer to camera driver pool after processing
 */
void cameraReturnFrame(camera_fb_t *fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

/**
 * Apply image parameters to camera sensor
 * Parameters: brightness (-2 to 2), contrast (-2 to 2), 
 * saturation (-2 to 2), white balance mode (0-4)
 */
void cameraApplySettings(int brightness, int contrast, int saturation, int wbMode) {
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, brightness);
        s->set_contrast(s, contrast);
        s->set_saturation(s, saturation);
        s->set_wb_mode(s, wbMode);
    }
}

/**
 * Set photo capture frame size
 */
void cameraSetFrameSize(framesize_t size) {
    currentFrameSize = size;
}

/**
 * Enable/disable horizontal mirroring
 */
void cameraSetHmirror(bool enable) {
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_hmirror(s, enable);
}

/**
 * Enable/disable vertical flipping
 */
void cameraSetVflip(bool enable) {
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_vflip(s, enable);
}

/**
 * 曝光/快门控制
 *
 * 自动模式：打开 AEC/AGC，由传感器自行决定曝光时间与增益。
 * 手动模式：关闭 AEC/AGC 后写入固定值——
 *   aec_value（快门/曝光时间，0-1200，越小曝光越短：可冻结运动、减少手抖模糊）
 *   agc_gain （模拟增益，0-30，曝光不足时靠增益提亮，但噪点会变多）
 * 注意：相机的模式切换（cameraInit）会把传感器重置为默认值，
 * 因此每次切模式/改设置后都要重新调用本函数。
 */
void cameraApplyExposure(int exposureMode, int shutterValue, int gainValue) {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;

    if (exposureMode == 1) {
        s->set_exposure_ctrl(s, 0);   // 关闭自动曝光
        s->set_gain_ctrl(s, 0);       // 关闭自动增益
        s->set_aec_value(s, constrain(shutterValue, 0, 1200));
        s->set_agc_gain(s, constrain(gainValue, 0, 30));
    } else {
        s->set_exposure_ctrl(s, 1);
        s->set_gain_ctrl(s, 1);
    }
}

/**
 * 场景优化算法（传感器参数配方）
 *
 * 只调整曝光相关参数，不动亮度/对比度/饱和度（那是用户手动设置的），避免互相覆盖。
 * 标准档与相机初始化默认值一致，因此切回标准不会带来画质回退。
 * 说明：OV2640 未实现 set_sharpness / set_denoise（esp32-camera 的 ov2640.c 直接返回 -1），
 * 所以这里用 OV2640 真正支持的开关：
 *   标准    ：AE 目标 +2、增益上限 128X、关闭 AEC2
 *   夜间增强：叠加 AEC2 低照度补偿 + 坏点校正（暗光下热噪点更少）
 *   逆光补偿：降低 AE 目标、限制增益上限，避免高光过曝而主体发黑
 */
void cameraApplyScene(int sceneMode) {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;

    switch (sceneMode) {
        case 1:   // 夜间增强
            s->set_ae_level(s, 2);
            s->set_aec2(s, 1);
            s->set_gainceiling(s, GAINCEILING_128X);
            s->set_bpc(s, 1);
            break;
        case 2:   // 逆光补偿
            s->set_ae_level(s, -1);
            s->set_aec2(s, 1);
            s->set_gainceiling(s, GAINCEILING_32X);
            s->set_bpc(s, 0);
            break;
        default:  // 标准
            s->set_ae_level(s, 2);
            s->set_aec2(s, 0);
            s->set_gainceiling(s, GAINCEILING_128X);
            s->set_bpc(s, 0);
            break;
    }
}

/**
 * Get current frame size setting
 */
framesize_t cameraGetCurrentFrameSize() {
    return currentFrameSize;
}

/**
 * Get current camera operating mode
 */
CameraMode cameraGetCurrentMode() {
    return currentCameraMode;
}

/**
 * OV2640 硬件窗口变焦（实验特性）
 *
 * 用传感器自身的窗口裁剪实现变焦：零 CPU 开销、照片与 MJPEG 推流都能带变焦，
 * 且照片是“全分辨率裁剪”而非软件解码后再重编码。
 *
 * esp32-camera 的 ov2640.c 里 set_res_raw 参数被“挪用”为窗口设置：
 *   startX = 档位预设（0=UXGA、1=SVGA、2=CIF），startY/endX/endY/scale/binning 忽略；
 *   offsetX/offsetY = 窗口起点，totalX/totalY = 窗口尺寸，outputX/outputY = 输出尺寸。
 * 内部把 max_x/max_y/w/h 各除以 4 写入寄存器，因此窗口尺寸与偏移必须是 4 的倍数
 * （社区实践建议 8 的倍数，否则易出现绿/紫边）。
 *
 * 返回 true = 已用硬件窗口实现变焦（调用方不要再做软件裁剪）
 * 返回 false = 不支持/未启用（调用方请回退软件裁剪）
 */
bool cameraApplySensorZoom(int zoom) {
    // 默认关闭或无需变焦：不做任何传感器操作，保证 hwZoom=false 时行为与既有完全一致
    if (!configGet().hwZoom || zoom <= 1) return false;

    sensor_t *s = esp_camera_sensor_get();
    if (!s || !s->set_res_raw) {
        Serial.println("[HWZOOM] sensor/set_res_raw unavailable -> software");
        return false;
    }

    // 当前帧尺寸（cameraInit 已写入传感器状态）映射到 OV2640 档位预设：
    // 仅支持 4:3 的 UXGA/SVGA/QVGA/CIF，其余（VGA/XGA/SXGA 等）回退软件裁剪
    framesize_t fs = s->status.framesize;
    int preset;
    switch (fs) {
        case FRAMESIZE_UXGA: preset = 0; break;   // UXGA 档
        case FRAMESIZE_SVGA: preset = 1; break;   // SVGA 档
        case FRAMESIZE_QVGA: preset = 2; break;   // QVGA 走 CIF 档 + 缩放
        case FRAMESIZE_CIF:  preset = 2; break;   // CIF 档
        default:
            Serial.printf("[HWZOOM] zoom=%d framesize=%d not a 4:3 preset -> software\n",
                          zoom, (int)fs);
            return false;
    }

    // 以 resolution[] 的有效区（输出宽高）为裁剪基准
    int effW = resolution[fs].width;
    int effH = resolution[fs].height;

    // 参数自洽检查：有效区必须接近 4:3（CIF 的 400x296 略有偏差，留 5% 容差）
    if (effW <= 0 || effH <= 0 || abs(effW * 3 - effH * 4) > effH * 4 / 20) {
        Serial.printf("[HWZOOM] zoom=%d bad aspect %dx%d -> software\n", zoom, effW, effH);
        return false;
    }

    // 窗口 = 有效区 / zoom，向下取整到 8 的倍数；居中放置，偏移同样对齐到 8 的倍数
    int winW = (effW / zoom) & ~7;
    int winH = (effH / zoom) & ~7;
    int offX = ((effW - winW) / 2) & ~7;
    int offY = ((effH - winH) / 2) & ~7;

    // 越界保护
    if (winW <= 0 || winH <= 0 || offX < 0 || offY < 0 ||
        offX + winW > effW || offY + winH > effH) {
        Serial.printf("[HWZOOM] zoom=%d invalid window=%dx%d@(%d,%d) -> software\n",
                      zoom, winW, winH, offX, offY);
        return false;
    }

    // outW/outH 取当前帧尺寸的输出宽高，保证推流/照片分辨率不变
    int outW = effW, outH = effH;
    int ret = s->set_res_raw(s, preset, 0, 0, 0, offX, offY,
                             winW, winH, outW, outH, false, false);
    bool ok = (ret == 0);
    Serial.printf("[HWZOOM] zoom=%d win=%dx%d@(%d,%d) out=%dx%d preset=%d -> %s\n",
                  zoom, winW, winH, offX, offY, outW, outH, preset,
                  ok ? "hw" : "software");
    return ok;
}

/**
 * 按配置返回预览应使用的相机模式
 * 默认关闭（previewJpeg=false）时返回 PREVIEW_RGB565，行为与既有完全一致
 */
CameraMode cameraPreviewMode() {
    return configGet().previewJpeg ? CameraMode::PREVIEW_JPEG : CameraMode::PREVIEW_RGB565;
}

/**
 * Lock camera to prevent concurrent access
 */
void cameraLock() {
    cameraLocked = true;
}

/**
 * Unlock camera to allow access
 */
void cameraUnlock() {
    cameraLocked = false;
}

/**
 * Check if camera is currently locked
 */
bool cameraIsLocked() {
    return cameraLocked;
}
