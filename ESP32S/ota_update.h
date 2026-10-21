/**
 * OTA Update Interface
 * 
 * Provides Over-The-Air firmware update capability via ArduinoOTA.
 * Allows remote firmware uploads over WiFi without USB connection.
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>

// Initialize OTA service with hostname and event handlers
void otaInit();

// Handle incoming OTA update requests (call from main loop)
void otaHandle();

#endif
