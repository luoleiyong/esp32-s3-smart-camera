/**
 * Video Record Implementation
 *
 * 把 CameraMode::VIDEO_JPEG（240×240、q12、双缓冲）产出的 JPEG 帧，
 * 按 AVI(MJPEG) 规范封装写入 SD 卡：
 *   RIFF('AVI ') → LIST('hdrl'){ avih, LIST('strl'){ strh, strf } }
 *                → LIST('movi'){ 每帧 00dc 块 } → idx1 索引表
 * 帧率取 configGet().videoFps（5/10/15）。索引表放 PSRAM，录像开始时按最大时长预留。
 */

#include "video_record.h"
#include "camera_driver.h"
#include "config_manager.h"
#include "tft_display.h"
#include <SD_MMC.h>
#include <FS.h>
#include <esp_task_wdt.h>
#include <time.h>

extern volatile bool cameraPaused;   // 定义在 ESP32S.ino：暂停预览任务

#define VR_WIDTH         240          // 与 CameraMode::VIDEO_JPEG 的分辨率一致
#define VR_HEIGHT        240
#define VR_MAX_MINUTES   10           // 单次录像最长分钟数（索引表预留上限）
#define VR_MIN_FREE_BYTES (2 * 1024 * 1024)   // SD 剩余低于 2MB 时自动停止
#define VR_IDX_ENTRY     8            // 每帧索引 8 字节：movi 内偏移 + 长度

// ===== 运行状态 =====
static volatile bool vrActive = false;   // 是否正在录像（供其它核安全读取）
static volatile bool vrReady = false;    // 文件头已就绪，可开始写帧
static volatile bool vrWriting = false;  // 正在写帧（停止时等待其收尾，避免两核同时写同一文件）
static File vrFile;
static uint8_t *vrIndex = nullptr;       // PSRAM 索引表
static uint32_t vrMaxFrames = 0;         // 索引表容量（帧）
static uint32_t vrFrameCount = 0;        // 已写入帧数
static int vrFrameIntervalMs = 100;      // 帧间隔 = 1000/fps
static unsigned long vrLastFrameMs = 0;
static char vrFileName[40] = "";

// ===== AVI 头部回填位置 =====
static uint32_t vrRiffSizePos = 0;       // RIFF 总长度
static uint32_t vrFramesPos = 0;         // avih.dwTotalFrames
static uint32_t vrLengthPos = 0;         // strh.dwLength
static uint32_t vrMoviSizePos = 0;       // LIST('movi') 长度
static uint32_t vrMoviOrigin = 0;        // 'movi' fourcc 位置（idx1 偏移原点）

// ===== 小端写入辅助（ESP32 为小端，可直接写内存）=====
static inline void vrWrU32(uint32_t v) { vrFile.write((const uint8_t *)&v, 4); }
static inline void vrWrU16(uint16_t v) { vrFile.write((const uint8_t *)&v, 2); }
static inline void vrWrTag(const char *t) { vrFile.write((const uint8_t *)t, 4); }

// 切模式会重置传感器，须重放用户设置（与 photo_capture.cpp 的 restorePreviewMode 一致）
static void vrApplyUserSettings() {
    DeviceConfig cfg = configGet();
    cameraApplySettings(cfg.brightness, cfg.contrast, cfg.saturation, cfg.wbMode);
    cameraApplyScene(cfg.sceneMode);
    cameraApplyExposure(cfg.exposureMode, cfg.shutterValue, cfg.gainValue);
    cameraSetHmirror(cfg.hmirror);
    cameraSetVflip(cfg.vflip);
}

// 停止录像后回到预览模式并重放用户设置
static void vrRestorePreview() {
    cameraSwitchMode(cameraPreviewMode());
    vrApplyUserSettings();
}

// 生成文件名：有 NTP 用时间戳，否则用 /video_NNN.avi 递增编号（跳过已存在）
static void vrBuildFileName() {
    struct tm ti;
    if (getLocalTime(&ti)) {
        strftime(vrFileName, sizeof(vrFileName), "/video_%Y%m%d_%H%M%S.avi", &ti);
        return;
    }
    for (int i = 1; i < 10000; i++) {
        snprintf(vrFileName, sizeof(vrFileName), "/video_%d.avi", i);
        if (!SD_MMC.exists(vrFileName)) return;
    }
    snprintf(vrFileName, sizeof(vrFileName), "/video_tmp.avi");
}

/**
 * 写入 AVI 头部（含占位字段与回填位置记录）
 * 布局：RIFF/Avi → LIST/hdrl → avih → LIST/strl → strh → strf → LIST/movi
 */
static bool vrWriteHeader(int fps) {
    uint32_t usecs = (uint32_t)(1000000UL / (uint32_t)fps);

    vrWrTag("RIFF"); vrRiffSizePos = vrFile.position(); vrWrU32(0); vrWrTag("AVI ");

    // LIST 'hdrl'
    vrWrTag("LIST"); uint32_t hdrlSizePos = vrFile.position(); vrWrU32(0); vrWrTag("hdrl");
    uint32_t hdrlStart = hdrlSizePos + 4;

    // avih：MainAVIHeader（56 字节）
    vrWrTag("avih"); vrWrU32(56);
    vrWrU32(usecs); vrWrU32(0); vrWrU32(0); vrWrU32(0x00000010);   // 建议 microsec/frame、flags=AVIF_HASINDEX
    vrFramesPos = vrFile.position(); vrWrU32(0);                    // dwTotalFrames 占位
    vrWrU32(0); vrWrU32(1); vrWrU32(0);                             // initialFrames、streams=1、suggestedBuffer
    vrWrU32(VR_WIDTH); vrWrU32(VR_HEIGHT);
    for (int i = 0; i < 4; i++) vrWrU32(0);                         // dwReserved[4]

    // LIST 'strl'
    vrWrTag("LIST"); uint32_t strlSizePos = vrFile.position(); vrWrU32(0); vrWrTag("strl");
    uint32_t strlStart = strlSizePos + 4;

    // strh：AVIStreamHeader（56 字节）
    vrWrTag("strh"); vrWrU32(56);
    vrWrTag("vids"); vrWrTag("MJPG"); vrWrU32(0);
    vrWrU16(0); vrWrU16(0);
    vrWrU32(0); vrWrU32(1); vrWrU32((uint32_t)fps); vrWrU32(0);     // scale=1, rate=fps
    vrLengthPos = vrFile.position(); vrWrU32(0);                    // dwLength 占位
    vrWrU32(0); vrWrU32(0); vrWrU32(0);
    vrWrU16(0); vrWrU16(0); vrWrU16(VR_WIDTH); vrWrU16(VR_HEIGHT);  // rcFrame

    // strf：BITMAPINFOHEADER（40 字节）
    vrWrTag("strf"); vrWrU32(40);
    vrWrU32(40); vrWrU32(VR_WIDTH); vrWrU32(VR_HEIGHT);
    vrWrU16(1); vrWrU16(24); vrWrTag("MJPG");
    vrWrU32((uint32_t)VR_WIDTH * VR_HEIGHT * 3);
    vrWrU32(0); vrWrU32(0); vrWrU32(0); vrWrU32(0);

    // 回填 strl / hdrl 长度（LIST 长度 = size 字段之后的字节数）
    uint32_t strlEnd = vrFile.position();
    if (!vrFile.seek(strlSizePos)) return false; vrWrU32(strlEnd - strlStart);
    if (!vrFile.seek(hdrlSizePos)) return false; vrWrU32(strlEnd - hdrlStart);
    if (!vrFile.seek(strlEnd)) return false;

    // LIST 'movi'：长度与内容占位，录像过程中逐帧追加
    vrWrTag("LIST"); vrMoviSizePos = vrFile.position(); vrWrU32(0);
    vrMoviOrigin = vrFile.position();   // 'movi' fourcc 位置，idx1 偏移以此为原点（首帧偏移=4）
    vrWrTag("movi");
    return true;
}

bool videoRecordStart() {
    if (vrActive) return false;

    DeviceConfig cfg = configGet();
    int fps = cfg.videoFps;
    if (fps != 5 && fps != 10 && fps != 15) fps = 10;
    vrFrameIntervalMs = 1000 / fps;

    // 索引表按最长录像时长预留：fps(最大 15)×60×10 帧 ≈ 90KB 上限
    vrMaxFrames = (uint32_t)fps * 60 * VR_MAX_MINUTES;
    vrIndex = (uint8_t *)ps_malloc((size_t)vrMaxFrames * VR_IDX_ENTRY);
    if (!vrIndex) {
        Serial.println("[VID] index alloc failed");
        return false;
    }
    vrFrameCount = 0;
    vrReady = false;
    vrBuildFileName();

    esp_task_wdt_reset();
    // 先置为"录像中"：让按键/拍照/推流立即互斥，避免启动期间的窄窗口冲突
    vrActive = true;
    cameraPaused = true;
    cameraLock();
    vTaskDelay(pdMS_TO_TICKS(150));   // 等预览任务让出相机

    if (!cameraSwitchMode(CameraMode::VIDEO_JPEG)) {
        Serial.println("[VID] mode switch failed");
        vrActive = false;
        cameraUnlock(); cameraPaused = false;
        free(vrIndex); vrIndex = nullptr;
        return false;
    }
    vrApplyUserSettings();

    vrFile = SD_MMC.open(vrFileName, FILE_WRITE);
    if (!vrFile) {
        Serial.println("[VID] open failed");
        vrActive = false;
        vrRestorePreview(); cameraUnlock(); cameraPaused = false;
        free(vrIndex); vrIndex = nullptr;
        return false;
    }
    if (!vrWriteHeader(fps)) {
        Serial.println("[VID] header write failed");
        vrFile.close(); SD_MMC.remove(vrFileName);
        vrActive = false;
        vrRestorePreview(); cameraUnlock(); cameraPaused = false;
        free(vrIndex); vrIndex = nullptr;
        return false;
    }

    vrLastFrameMs = millis();
    vrReady = true;
    Serial.printf("[VID] start %s fps=%d maxFrames=%u\n", vrFileName, fps, vrMaxFrames);
    return true;
}

void videoRecordUpdate() {
    if (!vrActive || !vrReady) return;

    unsigned long now = millis();
    if (now - vrLastFrameMs < (unsigned long)vrFrameIntervalMs) return;

    // 帧数到顶：自动收尾
    if (vrFrameCount >= vrMaxFrames) {
        Serial.println("[VID] max frames reached, auto stop");
        tftShowMessage("录像已满", UI_ACCENT, 0);
        videoRecordStop();
        return;
    }
    // 剩余空间不足：自动收尾（每 30 帧查一次，避免频繁读 FAT）
    if (vrFrameCount % 30 == 0) {
        uint64_t total = SD_MMC.totalBytes();
        if (total > 0 && (total - SD_MMC.usedBytes()) < VR_MIN_FREE_BYTES) {
            Serial.println("[VID] SD almost full, auto stop");
            tftShowMessage("磁盘已满", ST77XX_RED, 0);
            videoRecordStop();
            return;
        }
    }

    esp_task_wdt_reset();   // 写卡前喂狗（主循环受任务看门狗保护）
    vrWriting = true;

    camera_fb_t *fb = cameraCaptureFrame();
    if (!fb) { vrWriting = false; return; }
    if (fb->format != PIXFORMAT_JPEG) { cameraReturnFrame(fb); vrWriting = false; return; }

    uint32_t chunkPos = vrFile.position();          // '00dc' 块起点
    uint32_t idxOffset = chunkPos - vrMoviOrigin;   // 相对 movi 原点的偏移
    uint32_t len = (uint32_t)fb->len;
    uint8_t pad = (len & 1) ? 1 : 0;                // 奇数长度补 1 字节 0（2 字节对齐）

    bool ok = (vrFile.write((const uint8_t *)"00dc", 4) == 4);
    if (ok) ok = (vrFile.write((const uint8_t *)&len, 4) == 4);
    if (ok) ok = (vrFile.write(fb->buf, len) == len);
    if (ok && pad) { uint8_t z = 0; ok = (vrFile.write(&z, 1) == 1); }
    cameraReturnFrame(fb);

    if (!ok) {
        Serial.println("[VID] write failed, stop");
        vrWriting = false;               // 先解除占用，再收尾（避免自锁）
        videoRecordStop();
        return;
    }

    // 记录索引：movi 内偏移 + JPEG 长度（不含填充字节）
    uint8_t *e = vrIndex + (size_t)vrFrameCount * VR_IDX_ENTRY;
    memcpy(e, &idxOffset, 4);
    memcpy(e + 4, &len, 4);
    vrFrameCount++;
    vrLastFrameMs = millis();
    vrWriting = false;                   // 索引记录完毕才解除占用（停止时据此等待）
}

void videoRecordStop() {
    if (!vrActive) return;
    vrActive = false;                    // 先置位，令主循环不再进入写帧
    vrReady = false;
    while (vrWriting) vTaskDelay(pdMS_TO_TICKS(1));   // 等待可能正在进行的帧写收尾
    esp_task_wdt_reset();

    bool failed = false;

    // 追加 idx1 索引表：'idx1' + size + 每帧 16 字节（'00dc', flags=0x10, 偏移, 长度）
    uint32_t moviEnd = vrFile.position();
    uint32_t idxSize = vrFrameCount * 16;
    if (vrFile.write((const uint8_t *)"idx1", 4) == 4 &&
        vrFile.write((const uint8_t *)&idxSize, 4) == 4) {
        for (uint32_t i = 0; i < vrFrameCount; i++) {
            uint8_t *e = vrIndex + (size_t)i * VR_IDX_ENTRY;
            uint32_t off, len, flags = 0x10;
            memcpy(&off, e, 4);
            memcpy(&len, e + 4, 4);
            if (vrFile.write((const uint8_t *)"00dc", 4) != 4 ||
                vrFile.write((const uint8_t *)&flags, 4) != 4 ||
                vrFile.write((const uint8_t *)&off, 4) != 4 ||
                vrFile.write((const uint8_t *)&len, 4) != 4) { failed = true; break; }
        }
    } else {
        failed = true;
    }

    // 回填长度/帧数：无论是否失败都写成与实际字节一致，绝不留下损坏的头部
    uint32_t finalSize = vrFile.position();
    if (vrFile.seek(vrRiffSizePos)) { uint32_t v = finalSize - 8; vrFile.write((const uint8_t *)&v, 4); }
    if (vrFile.seek(vrFramesPos))   { uint32_t v = vrFrameCount;  vrFile.write((const uint8_t *)&v, 4); }
    if (vrFile.seek(vrLengthPos))   { uint32_t v = vrFrameCount;  vrFile.write((const uint8_t *)&v, 4); }
    if (vrFile.seek(vrMoviSizePos)) { uint32_t v = moviEnd - vrMoviOrigin; vrFile.write((const uint8_t *)&v, 4); }
    vrFile.close();

    // 一帧都没写成：删除空文件，避免残留无效 AVI
    if (vrFrameCount == 0) {
        SD_MMC.remove(vrFileName);
        failed = true;
    } else {
        Serial.printf("[VID] saved %s frames=%u failed=%d\n", vrFileName, vrFrameCount, (int)failed);
    }

    free(vrIndex); vrIndex = nullptr;
    uint32_t written = vrFrameCount;
    vrFrameCount = 0;

    // 恢复预览：切回配置的预览模式并重放用户设置
    vrRestorePreview();
    cameraUnlock();
    cameraPaused = false;

    if (failed) tftShowMessage("录像失败", ST77XX_RED, 2000);
    else Serial.printf("[VID] === DONE %u frames ===\n", written);
}

bool videoRecordIsActive() { return vrActive; }

const char *videoRecordFileName() { return vrActive ? vrFileName : ""; }