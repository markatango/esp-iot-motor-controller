/**
 * @file time_sync.cpp
 * @brief Time Synchronization Implementation
 */

#include "time_sync.h"
#include "cron_scheduler.h"

// ====================================================================
// CONFIGURATION
// ====================================================================

const char* ntp_server = "pool.ntp.org";
const long gmt_offset_sec = -8 * 3600;  // PST (UTC-8)
const int daylight_offset_sec = 3600;    // 1 hour for DST

// ====================================================================
// GLOBAL VARIABLES
// ====================================================================

time_t current_time = 0;
SemaphoreHandle_t time_and_schedule_mutex = NULL;

// ====================================================================
// FUNCTIONS
// ====================================================================

void time_sync_init() {
  Serial.println("\n🕐 Initializing time synchronization...");
  Serial.printf("   NTP Server: %s\n", ntp_server);
  Serial.printf("   GMT Offset: %ld seconds (%.1f hours)\n", gmt_offset_sec, gmt_offset_sec / 3600.0);
  
  configTime(gmt_offset_sec, daylight_offset_sec, ntp_server);

  // Create mutex
  time_and_schedule_mutex = xSemaphoreCreateMutex();
  if (time_and_schedule_mutex == NULL) {
    Serial.println("❌ Failed to create time and schedule mutex!");
    while(1) delay(1000);
  }
  
  // Wait for time to be set
  Serial.print("   Waiting for NTP sync");
  int attempts = 0;
  while (time(&current_time) < 100000 && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (current_time < 100000) {
    Serial.println("⚠️  Failed to sync with NTP server");
  } else {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char time_str[64];
      strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
      Serial.printf("✅ Time synchronized: %s\n", time_str);
      ntp_setSynced(true);
    }
  }
}

bool time_get_local(struct tm* timeinfo) {
  return getLocalTime(timeinfo);
}

size_t time_format(char* buffer, size_t size, const char* format) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    return strftime(buffer, size, format, &timeinfo);
  }
  return 0;
}

void time_sync_task(void* parameter) {
  Serial.println("🕐 Time Sync Task started on core " + String(xPortGetCoreID()));
  
  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t frequency = pdMS_TO_TICKS(60000);  // Update every 60 seconds
  
  while (1) {
    // Update current time
    if (xSemaphoreTake(time_and_schedule_mutex, portMAX_DELAY) == pdTRUE) {
      time(&current_time);
      
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        if (!ntp_isSynced() && current_time > 1577836800L) {
          ntp_setSynced(true);
        }
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Serial.printf("🕐 Time: %s (Unix: %ld)\n", time_str, current_time);
      } else {
        Serial.println("⚠️  Failed to get local time");
      }
      xSemaphoreGive(time_and_schedule_mutex);
    }
    
    vTaskDelayUntil(&last_wake_time, frequency);
  }
}
