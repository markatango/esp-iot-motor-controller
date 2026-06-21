/**
 * @file state_machine.h
 * @brief Chicken Coop Door State Machine
 *
 * Seven-state machine: START → OPENING → OPEN → CLOSING → CLOSED
 *                      plus MOT_ERROR and LIM_ERROR safety states.
 * All GPIO access is delegated to io_monitor for thread safety.
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "io_monitor.h"

typedef enum {
    SM_STATE_START,
    SM_STATE_OPENING,
    SM_STATE_OPEN,
    SM_STATE_CLOSING,
    SM_STATE_CLOSED,
    SM_STATE_MOT_ERROR,
    SM_STATE_LIM_ERROR
} CoopState;

extern CoopState         coop_state;
extern SemaphoreHandle_t sm_mutex;
extern volatile bool     g_sm_freeze;  // set on error entry, cleared on START

void        sm_init();
void        sm_trigger_open();   // signal OPEN event (WEB or CRON)
void        sm_trigger_close();  // signal CLOSE event (WEB or CRON)
void        sm_trigger_reset();  // signal CLR_ERROR event (WEB)
const char* sm_state_name(CoopState state);
const char* sm_get_state_string();
void        state_machine_task(void* parameter);

#endif // STATE_MACHINE_H
