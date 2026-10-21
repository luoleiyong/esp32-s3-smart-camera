/**
 * WiFi Manager Interface
 * 
 * Provides WiFi connectivity management including station mode connection,
 * access point mode setup, mDNS service registration, and NTP time sync.
 * Includes captive portal for initial WiFi configuration.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "camera_config.h"

// WiFi connection states
enum class WiFiState {
    IDLE,
    CONNECTING,
    CONNECTED,
    AP_MODE
};

// Initialize WiFi (attempts STA connection, falls back to AP mode)
bool wifiInit();

// Connect to WiFi network with timeout
bool wifiConnect(const char *ssid, const char *password, int timeoutMs = 10000);

// Start access point mode with captive portal
void wifiStartAP();

// Handle incoming AP mode client requests (captive portal)
void wifiHandleAPClient();

// Get current WiFi state
WiFiState wifiGetState();

// Check if connected to WiFi network
bool wifiIsConnected();

// Get current IP address (STA or AP)
String wifiGetIP();

// Initialize NTP time synchronization
void wifiInitTime();

// Initialize mDNS/Bonjour service discovery
void wifiInitMDNS();

#endif
