/**
 * Onboard LED Status Indicator
 *
 * 非阻塞状态机实现：ledStatusUpdate() 由主循环周期调用，
 * 内部只读 millis() 决定亮度/亮灭，不含任何 delay。
 *
 * 一次性提示（CAPTURE / ERROR）播放期间，主循环设置的基础状态只被记录，
 * 不会打断正在播放的提示；提示结束后自动回到最新的基础状态。
 */

#include "led_status.h"
#include "camera_config.h"

// 基础状态：由主循环按联网情况设置（IDLE / AP_MODE / NO_WIFI）
static LedState s_baseState = LedState::IDLE;
// 当前实际显示的状态（可能是基础状态，也可能是正在播放的一次性提示）
static LedState s_curState = LedState::IDLE;
// 是否有一次性提示正在播放（播放期间不允许被基础状态覆盖）
static bool s_oneShotActive = false;
// 当前状态的起始时刻，用于计算呼吸/闪烁相位与提示时长
static unsigned long s_stateStart = 0;

// 统一输出到板载 WS2812
static inline void show(uint8_t r, uint8_t g, uint8_t b) {
    neopixelWrite(LED_RGB_PIN, r, g, b);
}

void ledStatusInit() {
    s_baseState = LedState::IDLE;
    s_curState = LedState::IDLE;
    s_oneShotActive = false;
    s_stateStart = millis();
    show(0, 0, 0);   // 接管板载 LED，初始熄灭（随后由 update 进入呼吸）
}

void ledStatusSet(LedState st) {
    if (st == LedState::CAPTURE || st == LedState::ERROR) {
        // 一次性提示：立即开始播放（重复触发则重新计时）
        s_curState = st;
        s_oneShotActive = true;
        s_stateStart = millis();
        return;
    }

    // 基础状态：始终记录；若一次性提示正在播放则仅记录，不打断
    s_baseState = st;
    if (!s_oneShotActive && s_curState != st) {
        s_curState = st;
        s_stateStart = millis();
    }
}

void ledStatusUpdate() {
    unsigned long now = millis();
    unsigned long elapsed = now - s_stateStart;

    switch (s_curState) {
    case LedState::IDLE: {
        // 暗绿呼吸：亮度 0..40，约 2 秒一个周期（三角波，先升后降）
        const unsigned long period = 2000;
        unsigned long t = elapsed % period;
        uint8_t level;
        if (t < period / 2) {
            level = (uint8_t)(40UL * t / (period / 2));
        } else {
            level = (uint8_t)(40UL * (period - t) / (period / 2));
        }
        show(0, level, 0);
        break;
    }

    case LedState::CAPTURE: {
        // 白色高亮闪 150ms，随后回到基础状态
        show(255, 255, 255);
        if (elapsed >= 150) {
            s_oneShotActive = false;
            s_curState = s_baseState;
            s_stateStart = now;
        }
        break;
    }

    case LedState::AP_MODE: {
        // 蓝色慢闪：500ms 亮 / 500ms 灭
        if ((elapsed % 1000) < 500) show(0, 0, 255);
        else show(0, 0, 0);
        break;
    }

    case LedState::NO_WIFI: {
        // 橙色慢闪：500ms 亮 / 500ms 灭
        if ((elapsed % 1000) < 500) show(255, 120, 0);
        else show(0, 0, 0);
        break;
    }

    case LedState::ERROR: {
        // 红色快闪：150ms 周期（亮 75ms / 灭 75ms），持续 3 秒后回到基础状态
        if ((elapsed % 150) < 75) show(255, 0, 0);
        else show(0, 0, 0);
        if (elapsed >= 3000) {
            s_oneShotActive = false;
            s_curState = s_baseState;
            s_stateStart = now;
        }
        break;
    }
    }
}