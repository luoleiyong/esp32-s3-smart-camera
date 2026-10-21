/**
 * System Runtime Statistics
 *
 * 采集预览帧率（1 秒滑动窗口）、累计拍照数与运行时长，供网页 /status 状态页读取。
 * statsOnFrame() 由预览任务每帧调用；读取函数可从其它任务（如网页任务）安全调用。
 */

#ifndef SYSTEM_STATS_H
#define SYSTEM_STATS_H

#include <Arduino.h>

// 初始化统计数据（记录起始时刻）
void statsInit();

// 预览任务每成功绘制一帧时调用（仅用 millis() 判时，非阻塞）
void statsOnFrame();

// 拍照成功后调用，累计拍照数 +1
void statsOnPhoto();

// 当前预览帧率（1 秒滑动窗口估计值）
float statsGetFps();

// 自启动以来的运行时长（秒）
unsigned long statsGetUptimeSec();

// 累计成功拍照数
uint32_t statsGetPhotoCount();

#endif