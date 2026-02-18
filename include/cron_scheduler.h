#pragma once

// ============================================================================
// cron_scheduler.h
//
// FreeRTOS-based cron scheduler for ESP32 chicken coop door control.
// Parses an MQTT JSON schedule payload and arms one-shot timers that
// fire at configured UTC wall-clock times.
//
// Usage:
//   1. Call cronScheduler_init() once from setup() before starting tasks.
//   2. Call ntp_setSynced(true) from your NTP sync callback.
//   3. Call applyScheduleFromMQTT() from your MQTT message handler.
//   4. Read/write g_coop_command exclusively via coopCommand_get/set().
//
// Commands written to g_coop_command:
//   0  — idle (no pending action)
//   6  — open door
//   9  — close door
//  -1  — error sentinel (returned by coopCommand_get on mutex failure)
// ============================================================================

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief  Initialize mutexes and internal state.
 *         Must be called from setup() before any tasks are started.
 * @return true on success, false if a FreeRTOS allocation failed.
 */
bool cronScheduler_init();

// ============================================================================
// Coop Command — thread-safe accessors
// ============================================================================

/**
 * @brief  Atomically write a value to g_coop_command.
 * @param  value    The command value to set.
 * @param  timeout  Mutex wait timeout (default 100 ms).
 * @return true on success, false on mutex timeout or uninitialized state.
 */
bool coopCommand_set(int value, TickType_t timeout = pdMS_TO_TICKS(100));

/**
 * @brief  Atomically read g_coop_command.
 * @param  timeout  Mutex wait timeout (default 100 ms).
 * @return The current command value, or -1 on error.
 */
int  coopCommand_get(TickType_t timeout = pdMS_TO_TICKS(100));

// ============================================================================
// NTP Sync Flag — thread-safe accessors
// ============================================================================

/**
 * @brief  Set the NTP sync flag. Call with true from your NTP callback.
 */
void ntp_setSynced(bool synced);

/**
 * @brief  Query whether NTP has synced.
 * @return true if ntp_setSynced(true) has been called and the system
 *         clock reads a post-2020 epoch.
 */
bool ntp_isSynced();

// ============================================================================
// MQTT Schedule Handler
// ============================================================================

/**
 * @brief  Parse a 'chickencoop/schedule' MQTT payload and arm cron timers.
 *
 * Expected JSON fields:
 *   open_time     (string)  "HH:MM" UTC — triggers command 6
 *   close_time    (string)  "HH:MM" UTC — triggers command 9
 *   open_enabled  (bool)
 *   close_enabled (bool)
 *
 * A timer fires only when g_coop_command == 0; otherwise it logs and
 * skips. After firing the timer re-arms for the same time the next day.
 *
 * @param  payload  Raw MQTT payload bytes (need not be null-terminated).
 * @param  length   Payload length in bytes.
 */
void applyScheduleFromMQTT(const char* payload, size_t length);
