/**
 * Joystick Input Interface
 * 
 * Provides functions for reading analog joystick position (X/Y axes)
 * and digital button state. Supports edge detection and repeat-on-hold
 * functionality for menu navigation.
 */

#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <Arduino.h>
#include "camera_config.h"

// Initialize joystick GPIO pins and calibrate center position
void joystickInit();

// Read raw joystick values from analog and digital pins
void joystickRead();

// Check joystick direction states (continuous)
bool joystickUp();
bool joystickDown();
bool joystickLeft();
bool joystickRight();
bool joystickPressed();

// Check joystick edge detection (triggers on press with repeat-on-hold)
bool joystickUpEdge();
bool joystickDownEdge();
bool joystickLeftEdge();
bool joystickRightEdge();
bool joystickPressedEdge();

// 摇杆按下已持续时长（毫秒）；未按住时返回 0（用于长按删除等手势判定）
unsigned long joystickPressedHoldMs();

#endif
