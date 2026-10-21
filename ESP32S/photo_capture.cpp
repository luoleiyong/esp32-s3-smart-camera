/**
 * Photo Capture Implementation
 * 
 * Handles complete photo capture workflow including camera mode switching,
 * warmup capture, image capture, and SD card file writing.
 * Uses timestamp-based filenames or sequential counter if NTP not synced.
 */

#include "photo_capture.h"
#include "camera_driver.h"
#include "config_manager.h"
#include "photo_upload.h"
#include "tft_display.h"
#include <time.h>
#include <TJpg_Decoder.h>
#include "img_converters.h"
#include <esp_task_wdt.h>

// ===== 统一后处理：数字变焦裁剪 + 可选时间水印（共用一次解码-重编码）=====

static uint8_t *cropRgb = nullptr;    // 裁剪区 RGB888（由 TJpgDec 回调直接写入）
static int cropW = 0, cropH = 0;
static int cropSrcX = 0, cropSrcY = 0;

// RGB565 → RGB888 位扩展（自己按位算，不依赖任何库的字节序约定）
static inline void rgb565ToRgb888(uint16_t v, uint8_t *p) {
    uint8_t r5 = (v >> 11) & 0x1F;
    uint8_t g6 = (v >> 5) & 0x3F;
    uint8_t b5 = v & 0x1F;
    p[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
    p[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
    p[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
}

/**
 * TJpgDec 解码回调：只把落在裁剪窗口内的块按位扩展成 RGB888 写入 cropRgb，
 * 窗口外的块直接丢弃——无需解码整幅图，内存只需裁剪区大小（省内存特性保留）。
 * 直接输出 RGB888 可省掉中间的 RGB565 裁剪缓冲，避免 UXGA 全幅（水印）时
 * 两份大缓冲同时占用 PSRAM。进入前已 setSwapBytes(false)，data 为数值型 RGB565。
 */
static bool zoomCropOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *data) {
    const uint16_t blockW = w;                  // 源块行宽（按行定位用）
    int16_t dx = x - cropSrcX;
    int16_t dy = y - cropSrcY;

    if (dx >= cropW || dy >= cropH) return 1;   // 完全在裁剪窗口外

    int16_t sx = 0, sy = 0;
    if (dx < 0) { sx = -dx; w -= sx; dx = 0; }
    if (dy < 0) { sy = -dy; h -= sy; dy = 0; }
    if (dx + w > cropW) w = cropW - dx;
    if (dy + h > cropH) h = cropH - dy;
    if (w <= 0 || h <= 0) return 1;

    for (int16_t j = 0; j < h; j++) {
        const uint16_t *srow = data + (int32_t)(sy + j) * blockW + sx;
        uint8_t *drow = cropRgb + ((int32_t)(dy + j) * cropW + dx) * 3;
        for (int16_t i = 0; i < w; i++) rgb565ToRgb888(srow[i], drow + i * 3);
    }
    return 1;
}

// ===== 时间戳水印：8×16 ASCII 点阵（仅包含水印所需字符）=====
// 每字符 16 字节，每字节低 8 位对应一行（bit7 最左）
static const uint8_t FONT_DIGITS[10][16] = {
    {0x00,0x00,0x00,0x18,0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0x00}, // 0
    {0x00,0x00,0x00,0x18,0x18,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00}, // 1
    {0x00,0x00,0x00,0x3C,0x66,0x66,0x06,0x0C,0x18,0x30,0x60,0x66,0x7E,0x00,0x00,0x00}, // 2
    {0x00,0x00,0x00,0x3C,0x66,0x66,0x06,0x0C,0x0C,0x06,0x66,0x66,0x3C,0x00,0x00,0x00}, // 3
    {0x00,0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x0C,0x00,0x00,0x00}, // 4
    {0x00,0x00,0x00,0x7E,0x60,0x60,0x60,0x7C,0x06,0x06,0x06,0x66,0x3C,0x00,0x00,0x00}, // 5
    {0x00,0x00,0x00,0x1C,0x30,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00}, // 6
    {0x00,0x00,0x00,0x7E,0x66,0x06,0x06,0x0C,0x0C,0x18,0x18,0x18,0x18,0x00,0x00,0x00}, // 7
    {0x00,0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x3C,0x66,0x66,0x66,0x3C,0x00,0x00,0x00}, // 8
    {0x00,0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x3E,0x06,0x06,0x0C,0x38,0x00,0x00,0x00}, // 9
};
static const uint8_t FONT_DASH[16]  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00}; // '-'
static const uint8_t FONT_COLON[16] = {0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x00,0x00,0x18,0x18,0x18,0x00,0x00,0x00,0x00}; // ':'
static const uint8_t FONT_SPACE[16] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}; // ' '
static const uint8_t FONT_X[16]     = {0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00,0x00,0x00,0x00}; // 'x'

// 取字符点阵指针（不支持的字符按空格处理）
static const uint8_t *fontGlyph(char c) {
    if (c >= '0' && c <= '9') return FONT_DIGITS[c - '0'];
    if (c == '-') return FONT_DASH;
    if (c == ':') return FONT_COLON;
    if (c == 'x') return FONT_X;
    return FONT_SPACE;
}

// 写一个 RGB888 像素（越界忽略）
static inline void putPixelRGB888(uint8_t *rgb, int w, int h, int x, int y,
                                  uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint8_t *p = rgb + ((int32_t)y * w + x) * 3;
    p[0] = r; p[1] = g; p[2] = b;
}

/**
 * 在 RGB888 缓冲区 (x,y) 处绘制 8×16 文本：白字 + 1px 黑色描边（明暗背景都可辨认）
 * 只支持水印用到的字符集：数字、'-'、':'、空格、'x'
 */
static void drawTextRGB888(uint8_t *rgb, int w, int h, int x, int y, const char *s) {
    // 第一遍把字形向外膨胀 1px 画黑（描边），第二遍覆盖白色字形
    for (int pass = 0; pass < 2; pass++) {
        int cx = x;
        for (const char *p = s; *p; p++) {
            const uint8_t *g = fontGlyph(*p);
            for (int row = 0; row < 16; row++) {
                uint8_t bits = g[row];
                if (!bits) continue;
                for (int col = 0; col < 8; col++) {
                    if (!(bits & (0x80 >> col))) continue;
                    if (pass == 0) {
                        for (int oy = -1; oy <= 1; oy++)
                            for (int ox = -1; ox <= 1; ox++)
                                putPixelRGB888(rgb, w, h, cx + col + ox, y + row + oy, 0, 0, 0);
                    } else {
                        putPixelRGB888(rgb, w, h, cx + col, y + row, 255, 255, 255);
                    }
                }
            }
            cx += 8;
        }
    }
}

/**
 * 统一照片后处理通道：数字变焦中心裁剪 + 可选时间水印，共用一次解码-重编码
 * - cropZoom<=1 且无水印：直接写原图（不进入解码，保持现有拍摄速度）
 * - 否则：解码（只保留裁剪区）→ 画时间戳 → RGB888 → fmt2jpg → 写文件
 * cropZoom 决定裁剪；labelZoom 仅用于水印倍率标注（硬件变焦时为真实倍数）
 * 任一步失败返回 false，由调用方回退保存原始 JPEG
 */
static bool postProcessJpeg(const uint8_t *jpg, size_t len, int cropZoom, int labelZoom,
                            bool watermark, const char *filename, int quality) {
    // 时间水印需要有效本地时间；取不到就跳过水印（避免烧录错误时间）
    struct tm timeinfo;
    bool wm = watermark && getLocalTime(&timeinfo);
    if (watermark && !wm) Serial.println("[POST] watermark skipped: local time unavailable");

    // cropZoom 决定是否裁剪；labelZoom 只用于水印倍率标注
    // （硬件窗口变焦时传感器已裁剪，cropZoom=1，但标注仍显示真实倍数）
    int z = constrain(cropZoom, 1, 4);
    int lz = constrain(labelZoom, 1, 4);

    if (z <= 1 && !wm) {
        File f = SD_MMC.open(filename, FILE_WRITE);
        bool saved = false;
        if (f) {
            saved = (f.write(jpg, len) == len);
            f.close();
        }
        return saved;
    }

    uint16_t imgW = 0, imgH = 0;
    if (TJpgDec.getJpgSize(&imgW, &imgH, jpg, len) != JDR_OK) return false;

    cropW = imgW / z;                    // 裁剪区尺寸（保持与预览一致的中心裁剪）
    cropH = imgH / z;
    cropSrcX = (imgW - cropW) / 2;
    cropSrcY = (imgH - cropH) / 2;
    if (cropW < 16 || cropH < 16) return false;

    cropRgb = (uint8_t *)ps_malloc((size_t)cropW * cropH * 3);
    if (!cropRgb) {
        Serial.println("[POST] PSRAM alloc failed");
        return false;
    }

    TJpgDec.setSwapBytes(false);   // 回调拿到数值型 RGB565，可直接位运算换算
    TJpgDec.setJpgScale(1);        // 全分辨率解码（只有裁剪区被保留）
    TJpgDec.setCallback(zoomCropOutput);
    JRESULT r = TJpgDec.drawJpg(0, 0, jpg, len);
    TJpgDec.setCallback(tftJpgOutput);   // 归还解码回调，避免影响图库浏览
    if (r != JDR_OK) {
        Serial.printf("[POST] decode failed: %d\n", (int)r);
        free(cropRgb); cropRgb = nullptr;
        return false;
    }

    // 右下角时间水印：YYYY-MM-DD HH:MM:SS（变焦时追加倍率），距边 4px
    if (wm) {
        char text[32];
        strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &timeinfo);
        if (lz > 1) {
            char zoomStr[8];
            snprintf(zoomStr, sizeof(zoomStr), " x%d", lz);
            strncat(text, zoomStr, sizeof(text) - strlen(text) - 1);
        }
        int tw = (int)strlen(text) * 8;
        int tx = cropW - tw - 4;
        int ty = cropH - 16 - 4;
        if (tx < 0) tx = 0;
        if (ty < 0) ty = 0;
        drawTextRGB888(cropRgb, cropW, cropH, tx, ty, text);
    }

    uint8_t *outJpg = nullptr;
    size_t outLen = 0;
    bool ok = fmt2jpg(cropRgb, (size_t)cropW * cropH * 3, cropW, cropH,
                      PIXFORMAT_RGB888, quality, &outJpg, &outLen);
    free(cropRgb); cropRgb = nullptr;
    if (!ok || !outJpg) {
        Serial.println("[POST] re-encode failed");
        if (outJpg) free(outJpg);
        return false;
    }

    File f = SD_MMC.open(filename, FILE_WRITE);
    bool saved = false;
    if (f) {
        saved = (f.write(outJpg, outLen) == outLen);
        f.close();
    }
    Serial.printf("[POST] crop=%dx label=%dx wm=%d %dx%d -> %u bytes, saved=%d\n",
                  z, lz, (int)wm, cropW, cropH, (unsigned)outLen, (int)saved);
    free(outJpg);
    return saved;
}

/**
 * 主图保存一次：优先后处理通道（变焦/水印），失败回退直存原始 JPEG
 * openFailed 输出是否为 SD_MMC.open() 失败（用于重挂载重试后区分错误提示与返回语义）
 */
static bool saveMainImage(const uint8_t *jpg, size_t len, int cropZoom, int labelZoom,
                          bool watermark, const char *filename, int quality, bool *openFailed) {
    if (postProcessJpeg(jpg, len, cropZoom, labelZoom, watermark, filename, quality)) return true;

    Serial.println("[PHOTO] Post-process FAILED, fallback to original");
    File file = SD_MMC.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("[PHOTO] File open FAILED!");
        *openFailed = true;
        return false;
    }
    size_t written = file.write(jpg, len);
    file.close();
    bool ok = (written == len);
    Serial.printf("[PHOTO] Written: %u/%u bytes\n", written, len);
    if (!ok) Serial.println("[PHOTO] Write size mismatch!");
    return ok;
}

// ===== 图库缩略图：128×96，存 /thumb/，供 TFT 图库快速渲染 =====
#define THUMB_W 128
#define THUMB_H 96

static uint16_t *thumbSrcBuf = nullptr;   // 1/8 缩放解码后的源图（RGB565）
static int thumbSrcW = 0, thumbSrcH = 0;

// 把 1/8 缩放解码出的整幅 RGB565 块拷入 thumbSrcBuf
static bool thumbFullOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *data) {
    if (x >= thumbSrcW || y >= thumbSrcH) return 1;
    const uint16_t blockW = w;
    int16_t sx = 0, sy = 0;
    if (x < 0) { sx = -x; w -= sx; x = 0; }
    if (y < 0) { sy = -y; h -= sy; y = 0; }
    if (x + w > thumbSrcW) w = thumbSrcW - x;
    if (y + h > thumbSrcH) h = thumbSrcH - y;
    if (w <= 0 || h <= 0) return 1;
    for (int16_t j = 0; j < h; j++) {
        const uint16_t *s = data + (int32_t)(sy + j) * blockW + sx;
        uint16_t *d = thumbSrcBuf + (int32_t)(y + j) * thumbSrcW + x;
        for (int16_t i = 0; i < w; i++) d[i] = s[i];
    }
    return 1;
}

/**
 * 为已保存照片生成 128×96 缩略图，写入 /thumb/<同名>
 * 以最大缩放档(1/8)解码省内存；失败只打日志，绝不影响主照片保存
 */
static void generateThumbnail(const char *filename) {
    File f = SD_MMC.open(filename, FILE_READ);
    if (!f) return;
    size_t len = f.size();
    uint8_t *jpg = (uint8_t *)ps_malloc(len);
    if (!jpg) { f.close(); return; }
    f.read(jpg, len);
    f.close();

    uint16_t imgW = 0, imgH = 0;
    if (TJpgDec.getJpgSize(&imgW, &imgH, jpg, len) != JDR_OK) {
        free(jpg);
        return;
    }
    thumbSrcW = (imgW + 7) / 8;
    thumbSrcH = (imgH + 7) / 8;
    thumbSrcBuf = (uint16_t *)ps_malloc((size_t)thumbSrcW * thumbSrcH * 2);
    if (!thumbSrcBuf) {
        free(jpg);
        return;
    }

    TJpgDec.setSwapBytes(false);
    TJpgDec.setJpgScale(8);
    TJpgDec.setCallback(thumbFullOutput);
    JRESULT r = TJpgDec.drawJpg(0, 0, jpg, len);
    TJpgDec.setCallback(tftJpgOutput);
    TJpgDec.setJpgScale(1);   // 复位缩放档，避免影响后续整幅解码
    free(jpg);
    if (r != JDR_OK) {
        Serial.printf("[THUMB] decode failed: %d\n", (int)r);
        free(thumbSrcBuf); thumbSrcBuf = nullptr;
        return;
    }

    // 中心裁出 4:3 区域，再最近邻缩放到 128×96
    int cw = thumbSrcW;
    int ch = thumbSrcW * THUMB_H / THUMB_W;
    if (ch > thumbSrcH) { ch = thumbSrcH; cw = thumbSrcH * THUMB_W / THUMB_H; }
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    int cx = (thumbSrcW - cw) / 2;
    int cy = (thumbSrcH - ch) / 2;

    uint8_t *rgb = (uint8_t *)ps_malloc((size_t)THUMB_W * THUMB_H * 3);
    if (!rgb) {
        free(thumbSrcBuf); thumbSrcBuf = nullptr;
        return;
    }
    for (int dy = 0; dy < THUMB_H; dy++) {
        int sy = cy + dy * ch / THUMB_H;
        if (sy >= thumbSrcH) sy = thumbSrcH - 1;
        const uint16_t *srow = thumbSrcBuf + (int32_t)sy * thumbSrcW + cx;
        uint8_t *drow = rgb + (int32_t)dy * THUMB_W * 3;
        for (int dx = 0; dx < THUMB_W; dx++) {
            int sx = dx * cw / THUMB_W;
            if (sx >= cw) sx = cw - 1;
            rgb565ToRgb888(srow[sx], drow + dx * 3);
        }
    }
    free(thumbSrcBuf); thumbSrcBuf = nullptr;

    uint8_t *outJpg = nullptr;
    size_t outLen = 0;
    if (!fmt2jpg(rgb, (size_t)THUMB_W * THUMB_H * 3, THUMB_W, THUMB_H,
                 PIXFORMAT_RGB888, 8, &outJpg, &outLen)) {
        free(rgb);
        Serial.println("[THUMB] re-encode failed");
        return;
    }
    free(rgb);

    SD_MMC.mkdir("/thumb");    // 目录已存在时返回 false，忽略
    String name = filename;
    if (name.startsWith("/")) name = name.substring(1);
    String thumbPath = "/thumb/" + name;
    File t = SD_MMC.open(thumbPath, FILE_WRITE);
    bool ok = false;
    if (t) {
        ok = (t.write(outJpg, outLen) == outLen);
        t.close();
    }
    free(outJpg);
    Serial.printf("[THUMB] %s saved=%d\n", thumbPath.c_str(), (int)ok);
}

/**
 * 切回预览模式并重新套用用户设置
 * 相机每次 cameraInit 都会把传感器重置为默认值（亮度/白平衡/镜像/曝光全部丢失），
 * 所以拍照结束后必须整组重放一遍
 */
static void restorePreviewMode() {
    cameraSwitchMode(cameraPreviewMode());   // 按配置回到 JPEG 或 RGB565 预览
    DeviceConfig cfg = configGet();
    cameraApplySettings(cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode);
    cameraApplyScene(cfg.sceneMode);
    cameraApplyExposure(cfg.exposureMode, cfg.shutterValue, cfg.gainValue);
    cameraSetHmirror(cfg.hmirror);
    cameraSetVflip(cfg.vflip);
}

/**
 * 自拍倒计时
 * 调用方须在 pauseForCapture() 之前调用本函数：此时预览任务仍在运行，画面持续刷新。
 * 倒计时画在顶部标题栏（预览任务不会覆盖标题栏），逐秒可见；delay 放在 tft 调用之外。
 */
void photoSelfTimer() {
    int sec = configGet().shotDelaySec;
    if (sec <= 0) return;                       // 未开启倒计时：无任何额外延迟
    Serial.printf("[PHOTO] Self-timer %d sec\n", sec);
    for (int s = sec; s > 0; s--) {
        char buf[16];
        snprintf(buf, sizeof(buf), "倒计时 %d", s);
        tftDrawTitleBar(buf);
        esp_task_wdt_reset();                   // 逐秒喂狗（主循环受任务看门狗保护）
        delay(1000);
    }
    tftDrawTitleBar("智能相机");                 // 倒计时结束恢复默认标题
}

/**
 * Capture photo and save to SD card
 * Workflow: switch to JPEG mode -> warmup capture -> real capture -> write to SD -> restore preview mode
 * Returns true if photo successfully saved
 */
bool photoCapture(const char *customPath) {
    Serial.println("[PHOTO] === START ===");

    DeviceConfig cfg = configGet();

    Serial.println("[PHOTO] Switching to JPEG mode...");
    if (!cameraSwitchMode(CameraMode::PHOTO_JPEG)) {
        Serial.println("[PHOTO] Mode switch FAILED!");
        tftShowMessage("Cam Err!", ST77XX_RED, 2000);
        restorePreviewMode();
        return false;
    }
    Serial.println("[PHOTO] Mode switch OK");

    delay(100);

    // Apply camera settings before capture
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_wb_mode(s, cfg.wbMode);   // 沿用用户设置（默认 0=自动）；原先强制"晴天"会让室内照片偏绿
        s->set_brightness(s, cfg.brightness);
        s->set_contrast(s, cfg.contrast);
        s->set_saturation(s, cfg.saturation);
    }
    // 场景优化与曝光/快门也要重放：cameraInit 会把传感器重置回默认值
    cameraApplyScene(cfg.sceneMode);
    cameraApplyExposure(cfg.exposureMode, cfg.shutterValue, cfg.gainValue);
    delay(150);

    // 数字变焦倍数（1~4）：硬件窗口变焦与后处理软件裁剪共用
    const int zoom = constrain(cfg.zoomLevel, 1, 4);

    // OV2640 硬件窗口变焦（实验，默认关闭）：在重放设置之后、预热帧之前应用，
    // 让窗口寄存器随预热帧一起参与 AWB/AE 收敛。
    // 返回 true=传感器已裁剪 -> 后处理 cropZoom=1 不再重复裁剪（仍用 zoom 做水印标注）；
    // 返回 false=不支持/未启用 -> 维持软件裁剪，行为与现状一致。
    bool hwZoomApplied = cameraApplySensorZoom(zoom);
    const int cropZoom = hwZoomApplied ? 1 : zoom;
    Serial.printf("[PHOTO] HW zoom applied=%d (cropZoom=%d labelZoom=%d)\n",
                  (int)hwZoomApplied, cropZoom, zoom);

    // OV2640 的 AWB/AE 需要几帧才收敛，首帧容易偏绿/偏暗，先连拍 3 帧丢弃
    Serial.println("[PHOTO] Warmup capture x3...");
    esp_task_wdt_reset();   // 连拍前喂狗
    for (int i = 0; i < 3; i++) {
        camera_fb_t *tempFb = cameraCaptureFrame();
        if (tempFb) {
            Serial.printf("[PHOTO] Warmup %d: %u bytes, fmt=%d\n", i + 1, tempFb->len, tempFb->format);
            cameraReturnFrame(tempFb);
        } else {
            Serial.println("[PHOTO] Warmup frame NULL");
        }
        esp_task_wdt_reset();
        delay(80);
    }
    delay(100);

    // 抓取最终帧
    // 防抖开启时连拍 3 帧选最清晰的一张：同一场景下 JPEG 码流越长＝保留的高频
    // 细节越多＝越清晰，用码流长度做清晰度代理可以免解码完成选优，
    // 把手抖造成的糊帧自然剔除（数字防抖，硬件 OIS 本项目不具备）
    const int shots = cfg.antiShake ? 3 : 1;
    uint8_t *bestBuf = nullptr;      // 最优帧的 JPEG 数据（自己 malloc 的副本）
    size_t bestLen = 0;

    for (int i = 0; i < shots; i++) {
        Serial.printf("[PHOTO] Shot %d/%d...\n", i + 1, shots);
        esp_task_wdt_reset();   // 连拍逐帧喂狗
        camera_fb_t *fb = cameraCaptureFrame();
        if (!fb) {
            Serial.println("[PHOTO] Capture FAILED - NULL frame!");
            delay(50);
            continue;
        }
        Serial.printf("[PHOTO] Captured: %u bytes, fmt=%d\n", fb->len, fb->format);

        if (fb->format == PIXFORMAT_JPEG && fb->len > bestLen) {
            uint8_t *copy = (uint8_t *)ps_malloc(fb->len);
            if (copy) {
                memcpy(copy, fb->buf, fb->len);
                if (bestBuf) free(bestBuf);
                bestBuf = copy;
                bestLen = fb->len;
            }
        }
        cameraReturnFrame(fb);
        if (i + 1 < shots) delay(80);   // 拉开一点间隔，让手抖在不同帧上体现出差异
    }

    if (!bestBuf || bestLen == 0) {
        Serial.println("[PHOTO] No valid frame captured!");
        tftShowMessage("Cap Err!", ST77XX_RED, 2000);
        restorePreviewMode();
        return false;
    }

    // Generate filename: timestamp-based or sequential counter
    char filename[64];
    if (customPath) {
        strncpy(filename, customPath, sizeof(filename) - 1);
    } else {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            strftime(filename, sizeof(filename), "/photo_%Y%m%d_%H%M%S.jpg", &timeinfo);
        } else {
            static unsigned long photoCounter = 0;
            photoCounter++;
            snprintf(filename, sizeof(filename), "/photo_%lu.jpg", photoCounter);
        }
    }

    Serial.printf("[PHOTO] Saving to: %s\n", filename);

    // 统一后处理：数字变焦裁剪 / 时间水印
    // （cropZoom=1 且无水印时直存原图，保持现有拍摄速度）
    if (cropZoom > 1 || cfg.watermark) {
        tftShowMessage("处理中...", UI_ACCENT, 0);
    }
    bool openFailed = false;
    esp_task_wdt_reset();   // 后处理（含 SD 写入）前喂狗
    bool saved = saveMainImage(bestBuf, bestLen, cropZoom, zoom, cfg.watermark, filename,
                               cfg.photoQuality, &openFailed);
    esp_task_wdt_reset();

    // 运行自愈：主图保存失败（open 失败或写入字节数不符）时重挂载 SD 并重试一次
    if (!saved) {
        Serial.println("[PHOTO] Main image save FAILED, remount SD and retry...");
        esp_task_wdt_reset();
        SD_MMC.end();
        delay(100);
        SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
        SD_MMC.begin("/sdcard", true);
        esp_task_wdt_reset();
        openFailed = false;
        saved = saveMainImage(bestBuf, bestLen, cropZoom, zoom, cfg.watermark, filename,
                              cfg.photoQuality, &openFailed);
        Serial.printf("[PHOTO] Retry after remount: saved=%d\n", (int)saved);
        esp_task_wdt_reset();
    }

    if (!saved) {
        free(bestBuf);
        if (openFailed) tftShowMessage("SD Err!", ST77XX_RED, 2000);
        restorePreviewMode();
        return !openFailed;   // 保持既有语义：仅 open 失败返回 false
    }

    // 主照片已保存：生成 128×96 缩略图（失败只打日志，绝不影响主图）
    generateThumbnail(filename);

    free(bestBuf);

    Serial.println("[PHOTO] Restoring preview mode...");
    restorePreviewMode();

    // 拍照后自动上传（默认关闭）：uploadAuto=false 或地址为空时整段跳过，
    // 无任何网络动作、无额外延迟，与旧行为完全一致；失败只提示不影响拍照返回值
    if (cfg.uploadAuto && cfg.uploadUrl[0]) {
        tftShowMessage("上传中...", UI_ACCENT, 0);
        esp_task_wdt_reset();   // 上传可能耗时，先喂狗
        bool up = photoUploadFile(filename);
        esp_task_wdt_reset();
        tftShowMessage(up ? "上传成功" : "上传失败",
                       up ? ST77XX_GREEN : ST77XX_RED, 1500);
    }

    Serial.println("[PHOTO] === DONE ===");
    return true;
}

/**
 * List all .jpg files on SD card
 * Populates list array with filenames, sets count to number found
 */
void photoList(String *list, int *count, int maxCount) {
    *count = 0;
    File root = SD_MMC.open("/");
    File file = root.openNextFile();

    while (file && *count < maxCount) {
        if (!file.isDirectory()) {
            String name = file.name();
            if (name.endsWith(".jpg")) {
                list[*count] = name;
                (*count)++;
            }
        }
        file = root.openNextFile();
    }
}

/**
 * Delete photo file from SD card
 */
bool photoDelete(const char *filename) {
    String path = "/" + String(filename);
    return SD_MMC.remove(path);
}

/**
 * Check if photo file exists on SD card
 */
bool photoExists(const char *filename) {
    String path = "/" + String(filename);
    File f = SD_MMC.open(path, FILE_READ);
    if (f) {
        f.close();
        return true;
    }
    return false;
}
