/**
 * @file state_machine.cpp
 * @brief State Machine Implementation - Integrated with I/O Monitor
 */

#include "state_machine.h"

// ====================================================================
// CONFIGURATION
// ====================================================================

// Pin name configuration (using io_monitor pin names)
// Change these to match your hardware setup
const char* SM_INPUT_A_NAME = "UPI";    // Input monitored in State A
const char* SM_INPUT_B_NAME = "UPLIM";    // Input monitored in State B
const char* SM_OUTPUT_NAME = "C";       // Output pin controlled by state machine

// Timing configuration
unsigned long SM_TIMEOUT_MS = 5000;  // 5 second timeout in State B

// ====================================================================
// GLOBAL VARIABLES
// ====================================================================

// State machine status
StateMachineState current_state = STATE_A;
unsigned long state_b_start_time = 0;
bool sm_output_active = false;
SemaphoreHandle_t sm_mutex = NULL;

// Previous input states for edge detection
uint8_t sm_prev_input_a = LOW;
uint8_t sm_prev_input_b = LOW;

// ====================================================================
// FUNCTIONS
// ====================================================================

void sm_init() {
  Serial.println("\n🔄 Initializing State Machine...");
  
  // Validate that pin names exist in io_monitor
  if (io_get_input_by_name(SM_INPUT_A_NAME) < 0) {
    Serial.printf("❌ ERROR: Input A '%s' not found in io_monitor!\n", SM_INPUT_A_NAME);
    Serial.println("Available inputs:");
    for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
      Serial.printf("   - %s\n", INPUT_NAMES[i]);
    }
    while(1) delay(1000);
  }
  
  if (io_get_input_by_name(SM_INPUT_B_NAME) < 0) {
    Serial.printf("❌ ERROR: Input B '%s' not found in io_monitor!\n", SM_INPUT_B_NAME);
    Serial.println("Available inputs:");
    for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
      Serial.printf("   - %s\n", INPUT_NAMES[i]);
    }
    while(1) delay(1000);
  }
  
  if (io_get_output_by_name(SM_OUTPUT_NAME) < 0) {
    Serial.printf("❌ ERROR: Output '%s' not found in io_monitor!\n", SM_OUTPUT_NAME);
    Serial.println("Available outputs:");
    for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
      Serial.printf("   - %s\n", OUTPUT_NAMES[i]);
    }
    while(1) delay(1000);
  }
  
  Serial.printf("   Input A: %s\n", SM_INPUT_A_NAME);
  Serial.printf("   Input B: %s\n", SM_INPUT_B_NAME);
  Serial.printf("   Output: %s\n", SM_OUTPUT_NAME);
  Serial.printf("   Timeout: %lu ms\n", SM_TIMEOUT_MS);
  
  // Create mutex
  sm_mutex = xSemaphoreCreateMutex();
  if (sm_mutex == NULL) {
    Serial.println("❌ Failed to create state machine mutex!");
    while(1) delay(1000);
  }
  
  // Initialize state
  current_state = STATE_A;
  sm_output_active = false;
  
  // Read initial input states (thread-safe)
  if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
    sm_prev_input_a = io_get_input_by_name(SM_INPUT_A_NAME);
    sm_prev_input_b = io_get_input_by_name(SM_INPUT_B_NAME);
    xSemaphoreGive(io_state_mutex);
  }
  
  Serial.println("✅ State Machine initialized in STATE_A");
}

bool sm_check_input_a_transition() {
  bool transition = false;
  
  // Read current state from io_monitor (thread-safe)
  if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
    int current_state = io_get_input_by_name(SM_INPUT_A_NAME);
    
    // Detect LOW to HIGH transition
    if (current_state == HIGH && sm_prev_input_a == LOW) {
      transition = true;
    }
    
    sm_prev_input_a = current_state;
    xSemaphoreGive(io_state_mutex);
  }
  
  return transition;
}

bool sm_check_input_b_transition() {
  bool transition = false;
  
  // Read current state from io_monitor (thread-safe)
  if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
    int current_state = io_get_input_by_name(SM_INPUT_B_NAME);
    
    // Detect LOW to HIGH transition
    if (current_state == HIGH && sm_prev_input_b == LOW) {
      transition = true;
    }
    
    sm_prev_input_b = current_state;
    xSemaphoreGive(io_state_mutex);
  }
  
  return transition;
}

void sm_set_output(uint8_t state) {
  // Set output via io_monitor (thread-safe)
  if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
    // Find output index
    int output_idx = -1;
    for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
      if (strcmp(OUTPUT_NAMES[i], SM_OUTPUT_NAME) == 0) {
        output_idx = i;
        break;
      }
    }
    
    if (output_idx >= 0) {
      io_set_output(output_idx, state);
      sm_output_active = (state == HIGH);
      Serial.printf("🔄 State Machine output '%s' set to %s\n", 
                    SM_OUTPUT_NAME, 
                    state == HIGH ? "HIGH" : "LOW");
    }
    
    xSemaphoreGive(io_state_mutex);
  }
}

bool sm_check_timeout() {
  unsigned long elapsed = millis() - state_b_start_time;
  return (elapsed >= SM_TIMEOUT_MS);
}

void sm_enter_state_a() {
  if (xSemaphoreTake(sm_mutex, portMAX_DELAY) == pdTRUE) {
    current_state = STATE_A;
    xSemaphoreGive(sm_mutex);
  }
  
  sm_set_output(LOW);
  Serial.println("🔄 State Machine → STATE_A");
}

void sm_enter_state_b() {
  if (xSemaphoreTake(sm_mutex, portMAX_DELAY) == pdTRUE) {
    current_state = STATE_B;
    state_b_start_time = millis();
    xSemaphoreGive(sm_mutex);
  }
  
  sm_set_output(HIGH);
  Serial.printf("🔄 State Machine → STATE_B (timeout in %lu ms)\n", SM_TIMEOUT_MS);
}

void sm_process_state_a() {
  // Monitor Input A for LOW to HIGH transition
  if (sm_check_input_a_transition()) {
    Serial.printf("🔄 Input %s asserted (LOW → HIGH)\n", SM_INPUT_A_NAME);
    sm_enter_state_b();
  }
}

void sm_process_state_b() {
  // Monitor Input B for LOW to HIGH transition
  if (sm_check_input_b_transition()) {
    Serial.printf("🔄 Input %s asserted (LOW → HIGH)\n", SM_INPUT_B_NAME);
    sm_enter_state_a();
    return;
  }
  
  // Check for timeout
  if (sm_check_timeout()) {
    Serial.printf("🔄 Timeout occurred (%lu ms elapsed)\n", SM_TIMEOUT_MS);
    sm_enter_state_a();
    return;
  }
}

const char* sm_get_state_string() {
  const char* state_str;
  
  if (xSemaphoreTake(sm_mutex, portMAX_DELAY) == pdTRUE) {
    switch(current_state) {
      case STATE_A:
        state_str = "STATE_A";
        break;
      case STATE_B:
        state_str = "STATE_B";
        break;
      default:
        state_str = "UNKNOWN";
        break;
    }
    xSemaphoreGive(sm_mutex);
  } else {
    state_str = "ERROR";
  }
  
  return state_str;
}

void sm_get_status(StateMachineState* state, bool* output_active, unsigned long* time_remaining_ms) {
  if (xSemaphoreTake(sm_mutex, portMAX_DELAY) == pdTRUE) {
    if (state != nullptr) {
      *state = current_state;
    }
    
    if (output_active != nullptr) {
      *output_active = sm_output_active;
    }
    
    if (time_remaining_ms != nullptr) {
      if (current_state == STATE_B) {
        unsigned long elapsed = millis() - state_b_start_time;
        if (elapsed < SM_TIMEOUT_MS) {
          *time_remaining_ms = SM_TIMEOUT_MS - elapsed;
        } else {
          *time_remaining_ms = 0;
        }
      } else {
        *time_remaining_ms = 0;
      }
    }
    
    xSemaphoreGive(sm_mutex);
  }
}

void state_machine_task(void* parameter) {
  Serial.println("🔄 State Machine Task started on core " + String(xPortGetCoreID()));
  
  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t frequency = pdMS_TO_TICKS(50);  // Run every 50ms (less frequent than io_monitor's 10ms)
  
  while (1) {
    StateMachineState state;
    
    if (xSemaphoreTake(sm_mutex, portMAX_DELAY) == pdTRUE) {
      state = current_state;
      xSemaphoreGive(sm_mutex);
    } else {
      vTaskDelayUntil(&last_wake_time, frequency);
      continue;
    }
    
    // Process current state
    switch(state) {
      case STATE_A:
        sm_process_state_a();
        break;
        
      case STATE_B:
        sm_process_state_b();
        break;
    }
    
    vTaskDelayUntil(&last_wake_time, frequency);
  }
}