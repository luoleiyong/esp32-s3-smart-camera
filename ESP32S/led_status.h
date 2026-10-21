/**
 * Onboard LED Status Indicator
 *
 * 用板载 WS2812（GPIO48）以非阻塞方式表达设备状态：
 * 待机=暗绿呼吸、拍照=白闪、AP 模式=蓝慢闪、STA 未连接=橙慢闪、错误=红闪。
 * 所有刷新只依据 millis() 判时，绝不 delay，因此不会拖慢主循环。
 */

#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Arduino.h>

// LED 状态
enum class LedState {
    IDLE,      // 待机：暗绿呼吸
    CAPTURE,   // 拍照：白色高亮闪（一次性提示）
    AP_MODE,   // AP 配网：蓝色慢闪
    NO_WIFI,   // STA 未连接：橙色慢闪
    ERROR      // 错误：红色快闪（一次性提示，持续 3 秒）
};

// 初始化板载 LED，接管状态指示
void ledStatusInit();

// 切换状态；CAPTURE/ERROR 为一次性提示，播放完毕自动回到基础状态
void ledStatusSet(LedState st);

// 由主循环调用：仅用 millis() 判时，非阻塞、不 delay
void ledStatusUpdate();

#endif