/**
 * Joystick Input Implementation
 * 
 * Handles analog joystick reading with calibration, deadzone filtering,
 * and edge detection with repeat-on-hold functionality for menu navigation.
 */

#include "joystick.h"

// Joystick raw values and state tracking
static int joystickVRX = 0;
static int joystickVRY = 0;
static int joystickSW = 1;
static unsigned long lastJoystickRead = 0;

// Calibration center values and deadzone threshold
static int joystickCenterVRX = 2048;
static int joystickCenterVRY = 2048;
static const int JOYSTICK_DEADZONE = 1000;

// Previous state tracking for edge detection
static bool lastUpState = false;
static bool lastDownState = false;
static bool lastLeftState = false;
static bool lastRightState = false;
static bool lastPressedState = false;

// Hold time tracking for repeat-on-hold functionality
static unsigned long upHoldTime = 0;
static unsigned long downHoldTime = 0;
static unsigned long leftHoldTime = 0;
static unsigned long rightHoldTime = 0;
static unsigned long pressedHoldTime = 0;

// Timing thresholds for repeat behavior
static unsigned long lastActionTime = 0;
static const unsigned long HOLD_THRESHOLD = 500;
static const unsigned long REPEAT_DELAY = 150;

/**
 * Initialize joystick GPIO pins and calibrate center position
 * Samples analog values 16 times to determine resting center point
 */
void joystickInit() {
    pinMode(JOYSTICK_VRX, INPUT);
    pinMode(JOYSTICK_VRY, INPUT);
    pinMode(JOYSTICK_SW, INPUT_PULLUP);

    delay(100);
    long sumX = 0, sumY = 0;
    for (int i = 0; i < 16; i++) {
        sumX += analogRead(JOYSTICK_VRX);
        sumY += analogRead(JOYSTICK_VRY);
        delay(10);
    }
    joystickCenterVRX = sumX / 16;
    joystickCenterVRY = sumY / 16;
}

/**
 * Read joystick values with 50ms rate limiting
 * Reads analog X/Y axes and digital button state
 */
void joystickRead() {
    if (millis() - lastJoystickRead < 50) return;
    lastJoystickRead = millis();

    joystickVRX = analogRead(JOYSTICK_VRX);
    joystickVRY = analogRead(JOYSTICK_VRY);
    joystickSW = digitalRead(JOYSTICK_SW);
}

// Check joystick direction states against deadzone threshold
bool joystickUp() { return joystickVRY < (joystickCenterVRY - JOYSTICK_DEADZONE); }
bool joystickDown() { return joystickVRY > (joystickCenterVRY + JOYSTICK_DEADZONE); }
bool joystickLeft() { return joystickVRX < (joystickCenterVRX - JOYSTICK_DEADZONE); }
bool joystickRight() { return joystickVRX > (joystickCenterVRX + JOYSTICK_DEADZONE); }
bool joystickPressed() { return joystickSW == LOW; }

/**
 * Edge detection with repeat-on-hold functionality
 * Triggers immediately on press, then repeats after HOLD_THRESHOLD
 * with REPEAT_DELAY between repeats if repeatable is true
 */
static bool edgeDetect(bool current, bool &lastState, unsigned long &holdTime, bool repeatable) {
    unsigned long now = millis();
    if (current) {
        if (!lastState) {
            // Initial press - trigger immediately
            holdTime = now;
            lastState = true;
            return true;
        } else if (repeatable && (now - holdTime > HOLD_THRESHOLD)) {
            // Repeat after hold threshold
            if (now - lastActionTime > REPEAT_DELAY) {
                return true;
            }
        }
    } else {
        lastState = false;
    }
    return false;
}

// Edge detection functions for each direction
bool joystickUpEdge() {
    bool result = edgeDetect(joystickUp(), lastUpState, upHoldTime, true);
    if (result) lastActionTime = millis();
    return result;
}

bool joystickDownEdge() {
    bool result = edgeDetect(joystickDown(), lastDownState, downHoldTime, true);
    if (result) lastActionTime = millis();
    return result;
}

bool joystickLeftEdge() {
    bool result = edgeDetect(joystickLeft(), lastLeftState, leftHoldTime, true);
    if (result) lastActionTime = millis();
    return result;
}

bool joystickRightEdge() {
    bool result = edgeDetect(joystickRight(), lastRightState, rightHoldTime, true);
    if (result) lastActionTime = millis();
    return result;
}

bool joystickPressedEdge() {
    bool result = edgeDetect(joystickPressed(), lastPressedState, pressedHoldTime, false);
    if (result) lastActionTime = millis();
    return result;
}
