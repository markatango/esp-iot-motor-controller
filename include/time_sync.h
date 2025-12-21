
/**
 * @file time_sync.h
 * @brief Time Synchronization Module
 * 
 * Handles NTP time synchronization and time management
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ====================================================================
// TIME CONFIGURATION
// ====================================================================

// NTP configuration
extern const char* ntp_server;
extern const long gmt_offset_sec;
extern const int daylight_offset_sec;

// Current time
extern time_t current_time;

// ====================================================================
// TIME FUNCTIONS
// ====================================================================

/**
 * @brief Initialize NTP time synchronization
 */
void time_sync_init();

/**
 * @brief Get current time info
 * @param timeinfo Pointer to tm struct to fill
 * @return true if time is valid, false otherwise
 */
bool time_get_local(struct tm* timeinfo);

/**
 * @brief Format current time as string
 * @param buffer Buffer to write formatted time
 * @param size Size of buffer
 * @param format strftime format string
 * @return Number of characters written
 */
size_t time_format(char* buffer, size_t size, const char* format);

/**
 * @brief FreeRTOS task for time synchronization
 * @param parameter Task parameter (unused)
 */
void time_sync_task(void* parameter);

#endif // TIME_SYNC_H
