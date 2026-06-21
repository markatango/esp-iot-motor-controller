// ============================================================================
// cron_scheduler.cpp
// ============================================================================

#include "cron_scheduler.h"
#include "state_machine.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>
#include <time.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// Internal Types
// ============================================================================

struct CronJob {
    TimerHandle_t timer;
    int           command;       // 6 = open, 9 = close
    uint8_t       target_hour;
    uint8_t       target_min;
    bool          enabled;
    bool          polling;       // true while using 60s NTP-wait polling; false in exact mode
    const char*   label;
};

// ============================================================================
// Module-Private State
// ============================================================================

static volatile int      g_coop_command = 0;
static SemaphoreHandle_t g_coop_mutex   = nullptr;

static volatile bool     g_ntp_synced   = false;
static SemaphoreHandle_t g_ntp_mutex    = nullptr;

static CronJob s_open_job  = { nullptr, 6, 0, 0, false, false, "OPEN"  };
static CronJob s_close_job = { nullptr, 9, 0, 0, false, false, "CLOSE" };

// ============================================================================
// Private Helpers — forward declarations
// ============================================================================

static bool     ntp_clockIsValid();
static bool     parseHHMM(const char* hhmm, uint8_t& hour, uint8_t& min);
static uint32_t secondsUntilNext(uint8_t hour, uint8_t min);
static void     armCronJob(CronJob& job, uint8_t hour, uint8_t min, bool enabled);
static void     cronCallback(TimerHandle_t xTimer);

// ============================================================================
// Public API — Initialization
// ============================================================================

bool cronScheduler_init() {
    g_coop_mutex = xSemaphoreCreateMutex();
    if (g_coop_mutex == nullptr) {
        Serial.println("[INIT] ERROR: failed to create g_coop_mutex.");
        return false;
    }

    g_ntp_mutex = xSemaphoreCreateMutex();
    if (g_ntp_mutex == nullptr) {
        Serial.println("[INIT] ERROR: failed to create g_ntp_mutex.");
        vSemaphoreDelete(g_coop_mutex);
        g_coop_mutex = nullptr;
        return false;
    }

    Serial.println("[INIT] cronScheduler initialized.");
    return true;
}

// ============================================================================
// Public API — Coop Command
// ============================================================================

bool coopCommand_set(int value, TickType_t timeout) {
    if (g_coop_mutex == nullptr) {
        Serial.println("[COOP_CMD] ERROR: mutex not initialized.");
        return false;
    }
    if (xSemaphoreTake(g_coop_mutex, timeout) != pdTRUE) {
        Serial.println("[COOP_CMD] ERROR: mutex timeout on set.");
        return false;
    }
    g_coop_command = value;
    xSemaphoreGive(g_coop_mutex);
    return true;
}

int coopCommand_get(TickType_t timeout) {
    if (g_coop_mutex == nullptr) {
        Serial.println("[COOP_CMD] ERROR: mutex not initialized.");
        return -1;
    }
    if (xSemaphoreTake(g_coop_mutex, timeout) != pdTRUE) {
        Serial.println("[COOP_CMD] ERROR: mutex timeout on get.");
        return -1;
    }
    int val = g_coop_command;
    xSemaphoreGive(g_coop_mutex);
    return val;
}

// ============================================================================
// Public API — NTP Sync Flag
// ============================================================================

void ntp_setSynced(bool synced) {
    if (g_ntp_mutex == nullptr) return;
    if (xSemaphoreTake(g_ntp_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_ntp_synced = synced;
        xSemaphoreGive(g_ntp_mutex);
        Serial.printf("[NTP] Sync flag set to: %s\n", synced ? "true" : "false");
    }
}

bool ntp_isSynced() {
    if (g_ntp_mutex == nullptr) return false;
    if (xSemaphoreTake(g_ntp_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool synced = g_ntp_synced;
    xSemaphoreGive(g_ntp_mutex);
    return synced && ntp_clockIsValid();
}

// ============================================================================
// Public API — MQTT Schedule Handler
// ============================================================================

void applyScheduleFromMQTT(const char* payload, size_t length) {
    if (!payload || length == 0) {
        Serial.println("[SCHEDULE] ERROR: empty payload.");
        return;
    }
    if (length > 512) {
        Serial.printf("[SCHEDULE] ERROR: payload too large (%d bytes).\n", length);
        return;
    }

    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.printf("[SCHEDULE] JSON parse error: %s\n", err.c_str());
        return;
    }

    if (!doc.containsKey("open_time")    || !doc.containsKey("close_time") ||
        !doc.containsKey("open_enabled") || !doc.containsKey("close_enabled")) {
        Serial.println("[SCHEDULE] ERROR: missing required fields.");
        return;
    }

    // Apply timezone before computing any local-time deltas
    if (doc.containsKey("timezone")) {
        const char* tz_str = doc["timezone"];
        if (tz_str && strlen(tz_str) > 0) {
            setenv("TZ", tz_str, 1);
            tzset();
            Serial.printf("[SCHEDULE] TZ set to: %s\n", tz_str);
        }
    }

    const char* open_time     = doc["open_time"];
    const char* close_time    = doc["close_time"];
    bool        open_enabled  = doc["open_enabled"]  | false;
    bool        close_enabled = doc["close_enabled"] | false;

    uint8_t oh = 0, om = 0, ch = 0, cm = 0;
    bool open_valid  = parseHHMM(open_time,  oh, om);
    bool close_valid = parseHHMM(close_time, ch, cm);

    if (!open_valid) {
        Serial.printf("[SCHEDULE] Invalid open_time '%s' — open job disabled.\n", open_time);
        open_enabled = false;
    }
    if (!close_valid) {
        Serial.printf("[SCHEDULE] Invalid close_time '%s' — close job disabled.\n", close_time);
        close_enabled = false;
    }
    if (open_valid && close_valid && open_enabled && close_enabled &&
        oh == ch && om == cm) {
        Serial.println("[SCHEDULE] WARNING: open_time and close_time are identical.");
    }

    Serial.printf("[SCHEDULE] Applying — open=%s(%s)  close=%s(%s)\n",
                  open_time,  open_enabled  ? "enabled" : "disabled",
                  close_time, close_enabled ? "enabled" : "disabled");

    armCronJob(s_open_job,  oh, om, open_enabled);
    armCronJob(s_close_job, ch, cm, close_enabled);
}

// ============================================================================
// Private — NTP Clock Hard Validation
// ============================================================================

static bool ntp_clockIsValid() {
    return (time(nullptr) > 1577836800L);  // post 2020-01-01 00:00:00 UTC
}

// ============================================================================
// Private — Parse "HH:MM"
// ============================================================================

static bool parseHHMM(const char* hhmm, uint8_t& hour, uint8_t& min) {
    if (!hhmm) {
        Serial.println("[PARSE] Null time string.");
        return false;
    }
    size_t len = strlen(hhmm);
    if (len != 5) {
        Serial.printf("[PARSE] Bad length (%d) for time string: '%s'\n", len, hhmm);
        return false;
    }
    if (hhmm[2] != ':') {
        Serial.printf("[PARSE] Missing colon in time string: '%s'\n", hhmm);
        return false;
    }
    for (int i : {0, 1, 3, 4}) {
        if (!isdigit((unsigned char)hhmm[i])) {
            Serial.printf("[PARSE] Non-digit at position %d in: '%s'\n", i, hhmm);
            return false;
        }
    }
    char buf[6];
    strncpy(buf, hhmm, 5);
    buf[5] = '\0';
    buf[2] = '\0';
    hour = (uint8_t)atoi(buf);
    min  = (uint8_t)atoi(buf + 3);
    if (hour > 23 || min > 59) {
        Serial.printf("[PARSE] Out-of-range values: hour=%d min=%d\n", hour, min);
        return false;
    }
    return true;
}

// ============================================================================
// Private — Milliseconds Until Next Wall-Clock Occurrence (UTC)
// ============================================================================

static uint32_t secondsUntilNext(uint8_t hour, uint8_t min) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);   // decompose in local time (respects TZ + DST)

    struct tm target = t;
    target.tm_hour  = hour;
    target.tm_min   = min;
    target.tm_sec   = 0;
    target.tm_isdst = -1;    // let mktime determine DST for the target moment

    time_t target_t = mktime(&target);
    if (target_t <= now) {
        target_t += 86400;
    }

    long diff = (long)(target_t - now);
    return (uint32_t)(diff > 0 ? diff : 1);
}

// ============================================================================
// Private — Timer Callback  (runs in FreeRTOS timer daemon task)
// ============================================================================

static void cronCallback(TimerHandle_t xTimer) {
    CronJob* job = (CronJob*)pvTimerGetTimerID(xTimer);

    // NTP guard — defer if clock is not yet valid
    if (!ntp_isSynced()) {
        Serial.printf("[CRON][%s] Clock not valid — action deferred. Retrying in 60s.\n",
                      job->label);
        job->polling = true;
        xTimerChangePeriod(xTimer, ((TickType_t)(60) * (TickType_t)(configTICK_RATE_HZ)), 0);
        xTimerStart(xTimer, 0);
        return;
    }

    // Get current local time for log messages
    time_t now_t = time(nullptr);
    struct tm now_tm;
    localtime_r(&now_t, &now_tm);
    char now_str[20];
    strftime(now_str, sizeof(now_str), "%H:%M:%S", &now_tm);

    // Polling-to-exact transition: NTP just became valid.
    // Do NOT fire the command — we don't know if the target time has passed.
    // Just re-arm for the next correct occurrence.
    if (job->polling) {
        job->polling = false;
        uint32_t secs = secondsUntilNext(job->target_hour, job->target_min);
        xTimerChangePeriod(xTimer, ((TickType_t)(secs) * (TickType_t)(configTICK_RATE_HZ)), 0);
        xTimerStart(xTimer, 0);
        Serial.printf("[CRON][%s] NTP synced at %s — transitioning to exact mode, fires in %lus (%02d:%02d local).\n",
                      job->label, now_str, secs, job->target_hour, job->target_min);
        return;
    }

    if (!job->enabled) {
        Serial.printf("[CRON][%s] Triggered at %s but job is disabled — no action.\n",
                      job->label, now_str);
    } else {
        // Record command for external readers
        if (xSemaphoreTake(g_coop_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_coop_command = job->command;
            xSemaphoreGive(g_coop_mutex);
        }

        // Snapshot SM state for diagnostics before triggering
        CoopState sm_state = SM_STATE_START;
        if (xSemaphoreTake(sm_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            sm_state = coop_state;
            xSemaphoreGive(sm_mutex);
        }
        Serial.printf("[CRON][%s] Triggered at %s — SM: %s\n",
                      job->label, now_str, sm_state_name(sm_state));

        if (strcmp(job->label, "OPEN") == 0) {
            sm_trigger_open();
        } else if (strcmp(job->label, "CLOSE") == 0) {
            sm_trigger_close();
        }
    }

    // Re-arm for the same time tomorrow
    uint32_t secs = secondsUntilNext(job->target_hour, job->target_min);
    xTimerChangePeriod(xTimer, ((TickType_t)(secs) * (TickType_t)(configTICK_RATE_HZ)), 0);
    xTimerStart(xTimer, 0);
    Serial.printf("[CRON][%s] Re-armed — next trigger in %lus (%02d:%02d local).\n",
                  job->label, secs, job->target_hour, job->target_min);
}

// ============================================================================
// Private — Arm a Single Cron Job
// ============================================================================

static void armCronJob(CronJob& job, uint8_t hour, uint8_t min, bool enabled) {
    job.target_hour = hour;
    job.target_min  = min;
    job.enabled     = enabled;

    // Tear down any existing timer cleanly.
    // portMAX_DELAY ensures the stop/delete commands actually reach the timer
    // daemon — a short timeout can silently fail, leaving a ghost timer that
    // fires alongside the newly created one (double-trigger symptom).
    if (job.timer != nullptr) {
        xTimerStop(job.timer, portMAX_DELAY);
        xTimerDelete(job.timer, portMAX_DELAY);
        job.timer = nullptr;
    }

    if (!enabled) {
        Serial.printf("[CRON][%s] Disabled — timer not created.\n", job.label);
        return;
    }

    TickType_t period;
    if (!ntp_isSynced()) {
        // Clock not ready — poll every 60 s; callback will re-arm correctly
        // once ntp_isSynced() returns true.
        Serial.printf("[CRON][%s] Clock not ready — polling every 60s until sync.\n",
                      job.label);
        period = ((TickType_t)(60) * (TickType_t)(configTICK_RATE_HZ));
        job.polling = true;
    } else {
        uint32_t secs = secondsUntilNext(hour, min);
        Serial.printf("[CRON][%s] Armed for %02d:%02d local — triggers in %lus.\n",
                      job.label, hour, min, secs);
        period = ((TickType_t)(secs) * (TickType_t)(configTICK_RATE_HZ));
        job.polling = false;
    }

    job.timer = xTimerCreate(
        job.label,
        period,
        pdFALSE,          // one-shot; callback re-arms
        (void*)&job,      // timer ID = CronJob pointer
        cronCallback
    );

    if (job.timer == nullptr) {
        Serial.printf("[CRON][%s] ERROR: xTimerCreate failed!\n", job.label);
        return;
    }

    if (xTimerStart(job.timer, pdMS_TO_TICKS(100)) != pdPASS) {
        Serial.printf("[CRON][%s] ERROR: xTimerStart failed!\n", job.label);
    }
}
