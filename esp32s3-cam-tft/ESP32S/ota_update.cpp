/**
 * OTA (Over-The-Air) Firmware Update
 * 
 * Implements remote firmware updates via WiFi using ArduinoOTA.
 * Registers callbacks for OTA lifecycle events (start, progress, complete, error).
 * Uses mDNS hostname for device discovery on the network.
 */

#include "ota_update.h"
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "camera_config.h"

/**
 * Initialize OTA update service with mDNS hostname and event callbacks
 * Sets up handlers for OTA lifecycle: start, end, progress, and error events
 */
void otaInit() {
    ArduinoOTA.setHostname(MDNS_HOSTNAME);

    ArduinoOTA.onStart([]() {
        Serial.println("OTA Update Start");
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA Update End");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r", (progress * 100) / total);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();
    Serial.println("OTA Ready");
}

/**
 * Process incoming OTA update requests
 * Must be called periodically from main loop or dedicated task
 */
void otaHandle() {
    ArduinoOTA.handle();
}
