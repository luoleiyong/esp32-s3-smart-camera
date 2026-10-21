/**
 * Web Server Interface
 * 
 * Provides HTTP web interface for camera control, live streaming,
 * photo gallery management, and remote settings configuration.
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>

extern WebServer webServer;

// Initialize web server and start client handling task on Core 0
void webServerInit();

// Handle incoming HTTP client requests (deprecated, runs in task now)
void webServerHandleClient();

// Start MJPEG live streaming (for remote trigger)
void webServerStartStream();

// Stop MJPEG live streaming
void webServerStopStream();

// Check if camera is currently streaming
bool webServerIsStreaming();

#endif
