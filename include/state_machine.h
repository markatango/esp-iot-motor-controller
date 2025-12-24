/**
 * @file state_machine.h
 * @brief State Machine Module - Integrated with I/O Monitor
 * 
 * Implements a two-state machine (State A and State B) with timer and input monitoring
 * Uses io_monitor module for all GPIO access to ensure thread safety
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "io_monitor.h"

// ====================================================================
// STATE MACHINE CONFIGURATION
// ====================================================================

// State enumeration
enum StateMachineState {
    STATE_A,
    STATE_B
};

// Pin name configuration (uses io_monitor pin names)
extern const char* SM_INPUT_A_NAME;   // Input monitored in State A (e.g., "UPI")
extern const char* SM_INPUT_B_NAME;   // Input monitored in State B (e.g., "DNI")
extern const char* SM_OUTPUT_NAME;    // Output controlled by state machine (e.g., "A")

// Timing configuration
extern unsigned long SM_TIMEOUT_MS;  // Timeout in milliseconds for State B

// State machine status
extern StateMachineState current_state;
extern unsigned long state_b_start_time;
extern bool sm_output_active;
extern SemaphoreHandle_t sm_mutex;

// Previous input states for edge detection
extern uint8_t sm_prev_input_a;
extern uint8_t sm_prev_input_b;

// ====================================================================
// STATE MACHINE FUNCTIONS
// ====================================================================

/**
 * @brief Initialize state machine
 * Validates pin names and initial state
 * NOTE: Call AFTER io_init()
 */
void sm_init();

/**
 * @brief Check if Input A had LOW to HIGH transition
 * Uses io_monitor's debounced input states
 * Thread-safe (uses io_state_mutex)
 * @return true if transition occurred, false otherwise
 */
bool sm_check_input_a_transition();

/**
 * @brief Check if Input B had LOW to HIGH transition
 * Uses io_monitor's debounced input states
 * Thread-safe (uses io_state_mutex)
 * @return true if transition occurred, false otherwise
 */
bool sm_check_input_b_transition();

/**
 * @brief Set the output pin state via io_monitor
 * Thread-safe (uses io_state_mutex)
 * @param state HIGH or LOW
 */
void sm_set_output(uint8_t state);

/**
 * @brief Check if State B timer has timed out
 * @return true if timeout occurred, false otherwise
 */
bool sm_check_timeout();

/**
 * @brief Transition to State A
 * Thread-safe (uses sm_mutex)
 */
void sm_enter_state_a();

/**
 * @brief Transition to State B
 * Thread-safe (uses sm_mutex)
 */
void sm_enter_state_b();

/**
 * @brief Process State A logic
 * Monitors input A for LOW to HIGH transition
 */
void sm_process_state_a();

/**
 * @brief Process State B logic
 * Monitors timer and input B for timeout or LOW to HIGH transition
 */
void sm_process_state_b();

/**
 * @brief Get current state as string
 * @return "STATE_A" or "STATE_B"
 */
const char* sm_get_state_string();

/**
 * @brief FreeRTOS task for state machine
 * @param parameter Task parameter (unused)
 */
void state_machine_task(void* parameter);

/**
 * @brief Get current state machine status
 * Thread-safe accessor function
 * @param state Pointer to store current state
 * @param output_active Pointer to store output status
 * @param time_remaining_ms Pointer to store remaining time in State B (0 if in State A)
 */
void sm_get_status(StateMachineState* state, bool* output_active, unsigned long* time_remaining_ms);

#endif // STATE_MACHINE_H