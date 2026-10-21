/**
 * System Runtime Statistics Implementation
 *
 * 帧率用 1 秒滑动窗口统计：把窗口内累计帧数在窗口结束时换算为 fps。
 * 计数器均为 32 位对齐标量，XTensa 上单次读写是原子的，
 * 因此预览任务（Core 1）与网页任务（Core 0）可无锁并发访问。
 */

#include "system_stats.h"

// 当前统计窗口内已绘制的帧数
static volatile uint32_t s_framesInWindow = 0;
// 当前统计窗口的起始时刻（毫秒）
static volatile unsigned long s_windowStart = 0;
// 最近一次窗口结束时算出的帧率
static volatile float s_fps = 0.0f;
// 累计成功拍照数
static volatile uint32_t s_photoCount = 0;

void statsInit() {
    s_framesInWindow = 0;
    s_windowStart = millis();
    s_fps = 0.0f;
    s_photoCount = 0;
}

void statsOnFrame() {
    unsigned long now = millis();
    s_framesInWindow++;
    unsigned long dt = now - s_windowStart;
    if (dt >= 1000) {
        s_fps = s_framesInWindow * 1000.0f / (float)dt;   // 1 秒滑动窗口
        s_framesInWindow = 0;
        s_windowStart = now;
    }
}

void statsOnPhoto() {
    s_photoCount++;
}

float statsGetFps() {
    return s_fps;
}

unsigned long statsGetUptimeSec() {
    return millis() / 1000UL;
}

uint32_t statsGetPhotoCount() {
    return s_photoCount;
}