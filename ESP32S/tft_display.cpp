/**
 * TFT Display Implementation (White Theme)
 *
 * 硬件：1.44 寸 ST7735S，128×128
 * 白底界面渲染：摄像头预览、U8g2 中文字体、标题栏/状态栏、
 * JPEG 解码与图库浏览。中文渲染方式参照 SmartTargetV2 项目。
 */

#include "tft_display.h"
#include "camera_driver.h"
#include "config_manager.h"
#include <WiFi.h>
#include <SD_MMC.h>

// TFT 显示实例（CS、DC、RST 引脚）
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

// SPI 总线互斥量：相机任务（Core 1）与 Arduino loop（Core 0）同时写屏，用于防止撕裂/花屏
static SemaphoreHandle_t tftMutex = nullptr;

// U8g2 中文字体实例：正文（12px）与标题（14px）
static U8G2_FOR_ADAFRUIT_GFX u8g2_cn;
static U8G2_FOR_ADAFRUIT_GFX u8g2_cn_large;

// JPEG 输出偏移
int16_t display_offset_x = 0;
int16_t display_offset_y = 0;

/**
 * TJpgDec 解码回调：按偏移绘制解码块，并处理越界裁剪
 * 支持负偏移（居中裁切）：从源块内跳过被裁掉的行列，避免错位
 */
bool tftJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
    int16_t pos_x = x + display_offset_x;
    int16_t pos_y = y + display_offset_y;

    if (pos_x >= TFT_WIDTH || pos_y >= TFT_HEIGHT) return 1;

    const uint16_t src_w = w;      // 源块行宽（按行定位用）
    int16_t src_x = 0, src_y = 0;  // 源块内起始列/行

    if (pos_x < 0) { src_x = -pos_x; w -= src_x; pos_x = 0; }
    if (pos_y < 0) { src_y = -pos_y; h -= src_y; pos_y = 0; }
    if (pos_x + w > TFT_WIDTH) w = TFT_WIDTH - pos_x;
    if (pos_y + h > TFT_HEIGHT) h = TFT_HEIGHT - pos_y;

    if (w <= 0 || h <= 0) return 1;

    if (src_x == 0 && src_y == 0 && w == src_w) {
        // 无水平裁剪，整块直接绘制
        tft.drawRGBBitmap(pos_x, pos_y, bitmap, w, h);
    } else {
        // 有裁剪：逐行按源行宽定位，保证像素不错位
        for (int16_t j = 0; j < h; j++) {
            tft.drawRGBBitmap(pos_x, pos_y + j, bitmap + (src_y + j) * src_w + src_x, w, 1);
        }
    }
    return 1;
}

/**
 * RGB565 字节序转换（小端转大端）
 */
uint16_t swapRGB565(uint16_t color) {
    return ((color & 0xFF) << 8) | ((color >> 8) & 0xFF);
}

/**
 * 整块 RGB565 缓冲区字节序转换
 *
 * 相机输出为逐像素大端（高字节在前），显示端需要先转成小端数值，
 * 再由 Adafruit SPITFT 在发送时完成 SPI 大端转换（一次正好）。
 * 帧缓冲位于 PSRAM，访存是主要开销：这里一次 32 位读写交换两个像素，
 * 相比逐像素 __builtin_bswap16 减少一半访存与循环次数。
 * 放入 IRAM 避免每次调用从 Flash 取指。
 */
IRAM_ATTR void swapRGB565Buffer(uint16_t *buffer, int32_t length) {
    int32_t i = 0;
    for (; i + 1 < length; i += 2) {
        uint32_t pair = (uint32_t)buffer[i] | ((uint32_t)buffer[i + 1] << 16);
        pair = __builtin_bswap32(pair);
        buffer[i]     = (uint16_t)(pair >> 16);   // 交换后高 16 位为第一个像素
        buffer[i + 1] = (uint16_t)pair;
    }
    for (; i < length; i++) {                     // 奇数长度尾部
        buffer[i] = __builtin_bswap16(buffer[i]);
    }
}

/**
 * 初始化 TFT：背光、SPI、ST7735S、U8g2 中文字体
 * 背景统一为白色
 */
bool tftInit() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    delay(100);

    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_RST, OUTPUT);

    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_RST, HIGH);
    delay(100);

    digitalWrite(TFT_RST, LOW);
    delay(100);
    digitalWrite(TFT_RST, HIGH);
    delay(100);

    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.initR(INITR_144GREENTAB);   // 1.44 寸 ST7735S，128×128
    tftMutex = xSemaphoreCreateMutex();   // 创建 SPI 总线互斥量
    tft.setSPISpeed(TFT_SPI_FREQ_HZ);   // 27MHz（见 camera_config.h，异常时回退 20MHz）
    tft.setRotation(0);
    tft.fillScreen(UI_BG_COLOR);

    // 初始化中文字体（文泉驿 GB2312）
    u8g2_cn.begin(tft);
    u8g2_cn.setFont(u8g2_font_wqy12_t_gb2312a);
    u8g2_cn.setFontMode(1);

    u8g2_cn_large.begin(tft);
    u8g2_cn_large.setFont(u8g2_font_wqy14_t_gb2312a);
    u8g2_cn_large.setFontMode(1);

    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tftJpgOutput);

    return true;
}

/**
 * 以白底清屏
 */
void tftClear() {
    tft.fillScreen(UI_BG_COLOR);
}

/**
 * 绘制中文/UTF-8 文本，y 为文本顶部坐标
 */
void tftDrawCN(int x, int y, const char *text, uint16_t fg, bool large) {
    U8G2_FOR_ADAFRUIT_GFX &u = large ? u8g2_cn_large : u8g2_cn;
    u.setForegroundColor(fg);
    u.setCursor(x, y + u.getFontAscent());
    u.print(text);
}

/**
 * 以屏幕宽度居中绘制中文/UTF-8 文本
 */
void tftDrawCNCenter(int y, const char *text, uint16_t fg, bool large) {
    U8G2_FOR_ADAFRUIT_GFX &u = large ? u8g2_cn_large : u8g2_cn;
    int16_t w = u.getUTF8Width(text);
    tftDrawCN((TFT_WIDTH - w) / 2, y, text, fg, large);
}

/**
 * 绘制顶部标题栏：深色底 + 白色居中标题 + 分隔线
 */
void tftDrawTitleBar(const char *title) {
    tft.fillRect(0, 0, TFT_WIDTH, UI_TITLE_H, UI_TITLE_BG);
    tftDrawCNCenter(1, title, UI_BG_COLOR, true);
    tft.drawFastHLine(0, UI_TITLE_H, TFT_WIDTH, UI_DIVIDER);
}

/**
 * 状态栏内容缓存：内容与上次相同就跳过重绘；
 * tftDrawUIFrame / tftDrawGalleryInfo 覆盖状态栏后需清空缓存
 */
static char statusWifiText[16] = "";
static char statusTimeBuf[12] = "";

/**
 * 绘制预览界面框：标题栏、预览区、底部状态栏
 */
void tftDrawUIFrame() {
    xSemaphoreTake(tftMutex, portMAX_DELAY);
    tft.fillScreen(UI_BG_COLOR);
    tftDrawTitleBar("智能相机");

    // 底部状态栏（占位内容，使状态栏缓存失效，等待下一次刷新填入真实值）
    statusWifiText[0] = '\0';
    statusTimeBuf[0] = '\0';
    tft.fillRect(0, UI_STATUS_Y, TFT_WIDTH, UI_STATUS_H, UI_DIVIDER);
    tftDrawCN(4, UI_STATUS_Y + 2, "WiFi: --", UI_HINT_COLOR);
    tftDrawCN(78, UI_STATUS_Y + 2, "--:--:--", UI_TEXT_COLOR);
    xSemaphoreGive(tftMutex);
}

/**
 * 刷新状态栏：WiFi 状态（左）与时间（右）
 * 128×128 屏幕放不下 IP，IP 只在“网络信息”菜单页显示
 */
void tftUpdateStatusBar(bool wifiConnected, const char *ipStr) {
    (void)ipStr;

    // 先算好要显示的内容，与上次完全相同就直接返回：
    // 省掉每秒一次的整条状态栏填充 + 中文渲染 + SPI 传输
    const char *wifiText = wifiConnected ? "WiFi已连接" : "WiFi未连接";
    char timeBuf[12] = "--:--:--";
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }

    if (strcmp(statusWifiText, wifiText) == 0 && strcmp(statusTimeBuf, timeBuf) == 0) {
        return;
    }
    strncpy(statusWifiText, wifiText, sizeof(statusWifiText) - 1);
    strncpy(statusTimeBuf, timeBuf, sizeof(statusTimeBuf) - 1);
    statusWifiText[sizeof(statusWifiText) - 1] = '\0';
    statusTimeBuf[sizeof(statusTimeBuf) - 1] = '\0';

    xSemaphoreTake(tftMutex, portMAX_DELAY);
    tft.fillRect(0, UI_STATUS_Y, TFT_WIDTH, UI_STATUS_H, UI_DIVIDER);
    tftDrawCN(4, UI_STATUS_Y + 2, wifiText, wifiConnected ? 0x07E0 : 0xF800);
    tftDrawCN(78, UI_STATUS_Y + 2, timeBuf, UI_TEXT_COLOR);
    xSemaphoreGive(tftMutex);
}

/**
 * 绘制指定预览帧（RGB565）：调用方无需再持锁，本函数内部已加锁
 * fb 为空或非 RGB565 格式时直接返回
 * zoomLevel > 1 时做数字变焦：中心裁剪 1/zoom 区域并最近邻放大到整个预览区
 */
void tftDrawPreviewFrame(camera_fb_t *fb) {
    if (!fb || fb->format != PIXFORMAT_RGB565) return;

    int zoom = configGet().zoomLevel;
    if (zoom < 1) zoom = 1;
    if (zoom > 4) zoom = 4;

    xSemaphoreTake(tftMutex, portMAX_DELAY);

    static bool firstDraw = true;
    if (firstDraw) {
        tft.fillRect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, UI_BG_COLOR);
        firstDraw = false;
    }

    if (zoom == 1) {
        int16_t img_w = fb->width;
        int16_t img_h = fb->height;

        if (img_w > PREVIEW_W) img_w = PREVIEW_W;
        if (img_h > PREVIEW_H) img_h = PREVIEW_H;

        int16_t x = PREVIEW_X + (PREVIEW_W - img_w) / 2;
        int16_t y = PREVIEW_Y + (PREVIEW_H - img_h) / 2;

        // 取源图居中部分（128×128 → 中间 96 行）
        int16_t src_y = (fb->height - img_h) / 2;
        uint16_t *rgb_buffer = (uint16_t *)fb->buf + (int32_t)src_y * fb->width;

        swapRGB565Buffer(rgb_buffer, (int32_t)img_w * img_h);
        tft.drawRGBBitmap(x, y, rgb_buffer, img_w, img_h);
    } else {
        // 数字变焦：裁剪缓冲一次分配（24KB，放在 PSRAM）
        static uint16_t *zoomBuf = nullptr;
        if (!zoomBuf) {
            zoomBuf = (uint16_t *)ps_malloc((size_t)PREVIEW_W * PREVIEW_H * sizeof(uint16_t));
        }
        if (zoomBuf) {
            int srcW = PREVIEW_W / zoom;    // 裁剪宽度（放大倍数即 1/zoom）
            int srcH = PREVIEW_H / zoom;
            if (srcW < 1) srcW = 1;
            if (srcH < 1) srcH = 1;
            if (srcW > fb->width) srcW = fb->width;
            if (srcH > fb->height) srcH = fb->height;
            int srcX = (fb->width - srcW) / 2;    // 居中裁剪
            int srcY = (fb->height - srcH) / 2;

            const uint16_t *src = (const uint16_t *)fb->buf;
            for (int dy = 0; dy < PREVIEW_H; dy++) {
                // 每行只需算一次源行地址；PREVIEW_W 是 2 的幂，列映射可用移位
                const uint16_t *srow = src + (int32_t)(srcY + dy * srcH / PREVIEW_H) * fb->width + srcX;
                uint16_t *drow = zoomBuf + (int32_t)dy * PREVIEW_W;
                for (int dx = 0; dx < PREVIEW_W; dx++) {
                    // 相机帧缓冲是逐像素大端，这里顺便完成字节序转换
                    drow[dx] = __builtin_bswap16(srow[(dx * srcW) >> 7]);
                }
            }
            tft.drawRGBBitmap(PREVIEW_X, PREVIEW_Y, zoomBuf, PREVIEW_W, PREVIEW_H);
        }
    }

    xSemaphoreGive(tftMutex);
}

// ===== JPEG 预览数字变焦：只保留裁剪窗口内的解码块 =====
// 回调由 TJpgDec 在解码时逐块调用，禁止在此取 tftMutex（父函数已持锁）
static uint16_t *jpgZoomCropBuf = nullptr;   // 裁剪窗口像素（数值型 RGB565）
static int jpgCropW = 0, jpgCropH = 0;       // 裁剪窗口尺寸
static int jpgCropSrcX = 0, jpgCropSrcY = 0; // 裁剪窗口在源图中的起点

/**
 * TJpgDec 解码回调：只把落在裁剪窗口内的块写入 jpgZoomCropBuf，窗口外直接丢弃，
 * 无需缓存整幅图。进入前已 setSwapBytes(false)，data 为数值型 RGB565。
 * 块边界裁剪与按源块行宽定位的写法参照 photo_capture.cpp 的 zoomCropOutput。
 */
static bool tftJpgZoomCropOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *data) {
    const uint16_t blockW = w;                       // 源块行宽（按行定位用）
    int16_t dx = x - jpgCropSrcX;
    int16_t dy = y - jpgCropSrcY;
    if (dx >= jpgCropW || dy >= jpgCropH) return 1;  // 完全在裁剪窗口外

    int16_t sx = 0, sy = 0;
    if (dx < 0) { sx = -dx; w -= sx; dx = 0; }
    if (dy < 0) { sy = -dy; h -= sy; dy = 0; }
    if (dx + w > jpgCropW) w = jpgCropW - dx;
    if (dy + h > jpgCropH) h = jpgCropH - dy;
    if (w <= 0 || h <= 0) return 1;

    for (int16_t j = 0; j < h; j++) {
        const uint16_t *srow = data + (int32_t)(sy + j) * blockW + sx;
        uint16_t *drow = jpgZoomCropBuf + (int32_t)(dy + j) * jpgCropW + dx;
        for (int16_t i = 0; i < w; i++) drow[i] = srow[i];
    }
    return 1;
}

/**
 * 绘制指定预览帧（JPEG）：传感器输出 JPEG，用 TJpgDec 解码到预览区
 * 调用方无需再持锁，本函数内部已加锁；fb 为空或非 JPEG 格式直接返回。
 * zoomLevel > 1 时做数字变焦：只解码中心裁剪窗口，再最近邻放大到整个预览区，
 * 与 RGB565 预览的变焦视角保持一致。
 */
void tftDrawPreviewFrameJPEG(camera_fb_t *fb) {
    if (!fb || fb->format != PIXFORMAT_JPEG) return;

    int zoom = configGet().zoomLevel;
    if (zoom < 1) zoom = 1;
    if (zoom > 4) zoom = 4;

    xSemaphoreTake(tftMutex, portMAX_DELAY);

    if (zoom == 1) {
        // 只保留中间的 128×96 窗口：传感器 JPEG 是 128×128，若按偏移整幅绘制，
        // 底部 16 行会压到状态栏上；与变焦路径共用"只写裁剪窗口"的回调，
        // 取景与 RGB565 预览一致（都取中间 96 行）
        if (!jpgZoomCropBuf) {
            jpgZoomCropBuf = (uint16_t *)ps_malloc((size_t)PREVIEW_W * PREVIEW_H * sizeof(uint16_t));
        }
        if (jpgZoomCropBuf) {
            jpgCropW = PREVIEW_W;
            jpgCropH = PREVIEW_H;
            jpgCropSrcX = 0;
            jpgCropSrcY = ((int)fb->height - PREVIEW_H) / 2;   // 128×128 → 中间 96 行
            TJpgDec.setSwapBytes(false);
            TJpgDec.setJpgScale(1);
            TJpgDec.setCallback(tftJpgZoomCropOutput);
            TJpgDec.drawJpg(0, 0, fb->buf, fb->len);
            TJpgDec.setCallback(tftJpgOutput);   // 归还回调，避免影响图库浏览
            tft.drawRGBBitmap(PREVIEW_X, PREVIEW_Y, jpgZoomCropBuf, PREVIEW_W, PREVIEW_H);
        }
    } else {
        // 变焦缓冲惰性分配：与 zoom==1 共用同一块裁剪缓冲，
        // 统一按最大窗口（128×96=24KB）分配，避免先跑哪条路径导致缓冲大小不一致
        static uint16_t *zoomOutBuf = nullptr;
        if (!jpgZoomCropBuf) {
            jpgZoomCropBuf = (uint16_t *)ps_malloc((size_t)PREVIEW_W * PREVIEW_H * sizeof(uint16_t));
        }
        if (!zoomOutBuf) {
            zoomOutBuf = (uint16_t *)ps_malloc((size_t)PREVIEW_W * PREVIEW_H * sizeof(uint16_t));
        }
        if (jpgZoomCropBuf && zoomOutBuf) {
            jpgCropW = PREVIEW_W / zoom;      // 中心裁剪窗口尺寸
            jpgCropH = PREVIEW_H / zoom;
            if (jpgCropW < 1) jpgCropW = 1;
            if (jpgCropH < 1) jpgCropH = 1;
            jpgCropSrcX = ((int)fb->width - jpgCropW) / 2;
            jpgCropSrcY = ((int)fb->height - jpgCropH) / 2;

            TJpgDec.setSwapBytes(false);      // 回调拿到数值型 RGB565
            TJpgDec.setJpgScale(1);           // 全分辨率解码（窗口外的块被回调丢弃）
            TJpgDec.setCallback(tftJpgZoomCropOutput);
            TJpgDec.drawJpg(0, 0, fb->buf, fb->len);
            TJpgDec.setCallback(tftJpgOutput);   // 归还回调，避免影响图库浏览

            // 最近邻放大：裁剪窗口 → 128×96（回调已给数值型 RGB565，无需再换字节序）
            for (int dy = 0; dy < PREVIEW_H; dy++) {
                const uint16_t *srow = jpgZoomCropBuf + (int32_t)(dy * jpgCropH / PREVIEW_H) * jpgCropW;
                uint16_t *drow = zoomOutBuf + (int32_t)dy * PREVIEW_W;
                for (int dx = 0; dx < PREVIEW_W; dx++) {
                    drow[dx] = srow[dx * jpgCropW / PREVIEW_W];
                }
            }
            tft.drawRGBBitmap(PREVIEW_X, PREVIEW_Y, zoomOutBuf, PREVIEW_W, PREVIEW_H);
        }
    }

    xSemaphoreGive(tftMutex);
}

/**
 * 显示摄像头预览帧（RGB565）：内部取帧后调用 tftDrawPreviewFrame 绘制
 * （tftDrawPreviewFrame 内部已加锁，本函数无需再加锁）
 */
void tftShowPreviewRGB565() {
    camera_fb_t *fb = cameraCaptureFrame();
    if (!fb) return;

    tftDrawPreviewFrame(fb);

    cameraReturnFrame(fb);
}

/**
 * 显示摄像头预览帧（JPEG，TJpgDec 解码）
 * 取帧后委托 tftDrawPreviewFrameJPEG 绘制（内部已加锁并处理数字变焦）
 */
void tftShowPreviewJPEG() {
    camera_fb_t *fb = cameraCaptureFrame();
    if (!fb) return;

    tftDrawPreviewFrameJPEG(fb);

    cameraReturnFrame(fb);
}

/**
 * 快门闪烁动画：预览区短暂白闪作为拍照反馈
 */
void tftShutterAnimation() {
    xSemaphoreTake(tftMutex, portMAX_DELAY);
    tft.fillRect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, UI_BG_COLOR);
    xSemaphoreGive(tftMutex);
    delay(60);   // 延时放在解锁之后，避免长时间占锁
}

/**
 * 居中显示提示信息（白底 + 彩色边框）
 */
void tftShowMessage(const char *msg, uint16_t color, int delayMs) {
    xSemaphoreTake(tftMutex, portMAX_DELAY);
    tft.fillRect(8, 48, 112, 32, UI_BG_COLOR);
    tft.drawRect(8, 48, 112, 32, color);
    tftDrawCNCenter(58, msg, color);
    xSemaphoreGive(tftMutex);
    if (delayMs > 0) delay(delayMs);   // 延时放在解锁之后，避免长时间占锁
}

/**
 * 显示拍照已保存提示（3 秒后自动消失）
 */
void tftShowPhotoSaved(const char *filename) {
    xSemaphoreTake(tftMutex, portMAX_DELAY);
    tft.fillRect(8, 40, 112, 48, UI_BG_COLOR);
    tft.drawRect(8, 40, 112, 48, UI_ACCENT);
    tftDrawCNCenter(46, "已保存", 0x07E0, true);
    tftDrawCNCenter(70, filename, UI_TEXT_COLOR);
    xSemaphoreGive(tftMutex);
    delay(3000);   // 延时放在解锁之后，避免长时间占锁
}

/**
 * 绘制图库浏览信息（当前张数/总数与左右键提示）
 */
void tftDrawGalleryInfo(int current, int total) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d", current + 1, total);
    xSemaphoreTake(tftMutex, portMAX_DELAY);
    statusWifiText[0] = '\0';   // 状态栏被图库信息覆盖，缓存失效
    statusTimeBuf[0] = '\0';
    tft.fillRect(0, UI_STATUS_Y, TFT_WIDTH, UI_STATUS_H, UI_DIVIDER);
    tftDrawCN(6, UI_STATUS_Y + 2, buf, UI_TEXT_COLOR);
    // 128px 宽放不下"左右浏览 长按删除"整句，这里保留最关键的删除提示
    tftDrawCN(64, UI_STATUS_Y + 2, "长按删除", UI_TEXT_COLOR);
    xSemaphoreGive(tftMutex);
}

/**
 * 显示 SD 卡中指定照片：优先用 /thumb 缩略图（128×96，正好铺满内容区），
 * 无缩略图的历史照片回退为整幅解码（按尺寸选缩放倍数、居中显示）
 */
void tftDisplayPhoto(int index, const String &filename) {
    // 内容区：标题栏与状态栏之间（128×96）
    const int16_t AREA_X = 0;
    const int16_t AREA_Y = UI_TITLE_H;
    const int16_t AREA_W = TFT_WIDTH;
    const int16_t AREA_H = UI_STATUS_Y - UI_TITLE_H;

    tft.fillRect(AREA_X, AREA_Y, AREA_W, AREA_H, UI_BG_COLOR);

    fs::FS &fs = SD_MMC;

    // 目录名归一化：photoList 的条目可能带前导 '/'
    String base = filename;
    if (base.startsWith("/")) base = base.substring(1);

    // 优先缩略图：尺寸就是内容区大小，1:1 直接绘制
    File thumb = fs.open("/thumb/" + base, FILE_READ);
    if (thumb) {
        size_t tsize = thumb.size();
        uint8_t *tbuf = (uint8_t *)ps_malloc(tsize);
        if (tbuf) {
            thumb.read(tbuf, tsize);
            thumb.close();
            display_offset_x = AREA_X;
            display_offset_y = AREA_Y;
            TJpgDec.setSwapBytes(false);
            TJpgDec.setCallback(tftJpgOutput);
            TJpgDec.setJpgScale(1);
            TJpgDec.drawJpg(0, 0, tbuf, tsize);
            free(tbuf);
            return;
        }
        thumb.close();   // 内存不足则回退整幅解码
    }

    File file = fs.open("/" + base);
    if (!file) {
        tftDrawCN(AREA_X + 10, AREA_Y + 40, "文件读取失败", 0xF800);
        return;
    }

    size_t fileSize = file.size();
    uint8_t *jpgBuffer = (uint8_t *)ps_malloc(fileSize);
    if (!jpgBuffer) {
        tftDrawCN(AREA_X + 10, AREA_Y + 40, "内存不足", 0xF800);
        file.close();
        return;
    }

    file.read(jpgBuffer, fileSize);
    file.close();

    // 读取实际 JPEG 尺寸，选能装进内容区的最小缩放倍数（1/2/4/8）
    uint16_t img_w = 0, img_h = 0;
    if (TJpgDec.getJpgSize(&img_w, &img_h, jpgBuffer, fileSize) != JDR_OK) {
        img_w = 0;
        img_h = 0;
    }

    uint8_t scale = 8;
    if (img_w && img_h) {
        for (uint8_t s = 1; s <= 8; s <<= 1) {
            if (img_w / s <= AREA_W && img_h / s <= AREA_H) {
                scale = s;
                break;
            }
        }
    }

    // 缩放后尺寸（读不到尺寸时按内容区大小处理）
    int16_t dw = img_w ? img_w / scale : AREA_W;
    int16_t dh = img_h ? img_h / scale : AREA_H;

    // 内容区内居中；画面过高时顶端对齐，避免覆盖标题栏
    int16_t off_x = AREA_X + (AREA_W - dw) / 2;
    int16_t off_y = AREA_Y + (AREA_H - dh) / 2;
    if (off_y < AREA_Y) off_y = AREA_Y;

    display_offset_x = off_x;
    display_offset_y = off_y;
    TJpgDec.setSwapBytes(false);        // 数值型 RGB565（字节序转换交给 Adafruit 内部完成）
    TJpgDec.setCallback(tftJpgOutput);  // 显式设置回调：拍照变焦会临时换用其它回调
    TJpgDec.setJpgScale(scale);
    TJpgDec.drawJpg(0, 0, jpgBuffer, fileSize);

    free(jpgBuffer);
}