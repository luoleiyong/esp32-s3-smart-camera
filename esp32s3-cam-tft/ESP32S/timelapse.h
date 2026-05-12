/**
 * Timelapse Photography Interface
 * 
 * Provides interval-based photo capture for timelapse sequences.
 * Uses timestamp-based filenames with sequential numbering (tl_00000.jpg).
 */

#ifndef TIMELAPSE_H
#define TIMELAPSE_H

#include <Arduino.h>

// Initialize timelapse with saved configuration
void timelapseInit();

// Start timelapse capture with specified interval (minimum 5 seconds)
void timelapseStart(unsigned long intervalMs);

// Stop timelapse capture
void timelapseStop();

// Check if timelapse is currently running
bool timelapseIsRunning();

// Check interval timer and capture photo if due (call from main loop)
void timelapseUpdate();

// Get current timelapse interval in milliseconds
unsigned long timelapseGetInterval();

// Get number of photos captured in current timelapse session
int timelapseGetCount();

#endif
