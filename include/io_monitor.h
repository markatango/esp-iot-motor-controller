/**
 * @file io_monitor.h
 * @brief I/O Monitoring Module
 * 
 * Handles digital input/output monitoring and control
 */

#ifndef IO_MONITOR_H
#define IO_MONITOR_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ====================================================================
// I/O CONFIGURATION
// ====================================================================

// Number of I/O pins
extern const int NUM_DIGITAL_INPUTS;
extern const int NUM_DIGITAL_OUTPUTS;

// Pin arrays
extern const int INPUT_PINS[];
extern const int OUTPUT_PINS[];

// Pin names
extern const char* INPUT_NAMES[];
extern const char* OUTPUT_NAMES[];

// I/O state arrays (protected by mutex)
extern uint8_t input_states[];
extern uint8_t output_states[];
extern uint8_t prev_input_states[];
extern SemaphoreHandle_t io_state_mutex;

// Debounce configuration
extern const unsigned long DEBOUNCE_DELAY;
extern unsigned long last_debounce_times[];
extern volatile bool io_publish_needed;

// ====================================================================
// I/O FUNCTIONS
// ====================================================================

/**
 * @brief Initialize I/O pins
 */
void io_init();

/**
 * @brief Read all input pins with debouncing
 */
void io_read_inputs();

/**
 * @brief Check if any input state has changed
 * @return true if state changed, false otherwise
 */
bool io_has_state_changed();

/**
 * @brief Set output pin state
 * @param output_num Output number (0-based index)
 * @param value Output value (HIGH/LOW or 1/0)
 * @return true if successful, false if invalid output_num
 */
bool io_set_output(int output_num, uint8_t value);

/**
 * @brief Set multiple outputs from array
 * @param values Array of output values
 * @param count Number of values (max NUM_DIGITAL_OUTPUTS)
 */
void io_set_outputs(const uint8_t* values, int count);

/**
 * @brief Get input state by name
 * @param name Input name (e.g., "UPLIM", "DNS")
 * @return Input state (0 or 1), or -1 if name not found
 */
int io_get_input_by_name(const char* name);

/**
 * @brief Get output state by name
 * @param name Output name
 * @return Output state (0 or 1), or -1 if name not found
 */
int io_get_output_by_name(const char* name);

/**
 * @brief Set output state by name
 * @param name Output name
 * @param value Output value (HIGH/LOW or 1/0)
 * @return output index if successful, -1 if name not found
 */
int io_set_output_by_name(const char* name, const uint8_t value);

/**
 * @brief FreeRTOS task for I/O monitoring
 * @param parameter Task parameter (unused)
 */
void io_monitor_task(void* parameter);

#endif // IO_MONITOR_H
