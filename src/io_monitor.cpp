/**
 * @file io_monitor.cpp
 * @brief I/O Monitoring Implementation
 */

#include "io_monitor.h"

// ====================================================================
// CONFIGURATION
// ====================================================================

// Pin counts
const int NUM_DIGITAL_INPUTS = 6;
const int NUM_DIGITAL_OUTPUTS = 7;

// Input pin definitions
const int INPUT_PINS[NUM_DIGITAL_INPUTS] = {14,27,13,12,22,23};

// Output pin definitions
const int OUTPUT_PINS[NUM_DIGITAL_OUTPUTS] = {4,5,18,19,25,33,2};

// Pin names
const char* INPUT_NAMES[NUM_DIGITAL_INPUTS] = {
    "UPLIM",   // Upper limit switch
    "DNLIM",   // Down limit switch
    "UPS",     // Upper position sensor
    "DNS",     // Down position sensor
    "CLR_ERROR", // Error clear input
    "DNI"      // Down input
};

const char* OUTPUT_NAMES[NUM_DIGITAL_OUTPUTS] = {
    "Error",
    "B",
    "C",
    "D",
    "Mup",
    "Mdn",
    "WIFI_LED"
};

// Debounce configuration
const unsigned long DEBOUNCE_DELAY = 50;  // 50ms

// ====================================================================
// GLOBAL VARIABLES
// ====================================================================

uint8_t input_states[NUM_DIGITAL_INPUTS] = {0};
uint8_t output_states[NUM_DIGITAL_OUTPUTS] = {0};
uint8_t prev_input_states[NUM_DIGITAL_INPUTS] = {0};
unsigned long last_debounce_times[NUM_DIGITAL_INPUTS] = {0};
SemaphoreHandle_t io_state_mutex = NULL;
volatile bool io_publish_needed = false;

// ====================================================================
// FUNCTIONS
// ====================================================================

void io_init() {
  Serial.println("\n🔌 Initializing I/O pins...");
  
  // Configure input pins
  Serial.printf("   Inputs (%d pins):\n", NUM_DIGITAL_INPUTS);
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    pinMode(INPUT_PINS[i], INPUT_PULLUP);
    Serial.printf("      %s: GPIO%d\n", INPUT_NAMES[i], INPUT_PINS[i]);
  }
  
  // Prime input_states[] from actual pin readings so the SM sees correct
  // values on its first cycle before the IO monitor task has run.
  // Also prime prev_input_states[] to the same values so the first
  // io_read_inputs() call doesn't falsely trigger a debounce timer reset.
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    input_states[i]      = digitalRead(INPUT_PINS[i]);
    prev_input_states[i] = input_states[i];
  }

  // Configure output pins
  Serial.printf("   Outputs (%d pins):\n", NUM_DIGITAL_OUTPUTS);
  for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
    pinMode(OUTPUT_PINS[i], OUTPUT);
    digitalWrite(OUTPUT_PINS[i], LOW);
    Serial.printf("      %s: GPIO %d\n", OUTPUT_NAMES[i], OUTPUT_PINS[i]);
  }
  
  // Create mutex
  io_state_mutex = xSemaphoreCreateMutex();
  if (io_state_mutex == NULL) {
    Serial.println("❌ Failed to create I/O state mutex!");
    while(1) delay(1000);
  }
  
  Serial.println("✅ I/O pins initialized");
}

void io_read_inputs() {
  unsigned long current_time = millis();
  
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    uint8_t reading = digitalRead(INPUT_PINS[i]);
    
    // Debounce logic
    if (reading != prev_input_states[i]) {
      last_debounce_times[i] = current_time;
    }
    
    if ((current_time - last_debounce_times[i]) > DEBOUNCE_DELAY) {
      if (reading != input_states[i]) {
        input_states[i] = reading;
      }
    }
    
    prev_input_states[i] = reading;
  }
}

bool io_has_state_changed() {
  static uint8_t last_states[NUM_DIGITAL_INPUTS] = {0};
  bool changed = false;
  
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    if (input_states[i] != last_states[i]) {
      changed = true;
      last_states[i] = input_states[i];
    }
  }
  
  return changed;
}

bool io_set_output(int output_num, uint8_t value) {
  if (output_num < 0 || output_num >= NUM_DIGITAL_OUTPUTS) {
    return false;
  }
  
  digitalWrite(OUTPUT_PINS[output_num], value);
  output_states[output_num] = value;
  
  Serial.printf("🔌 Output %s (GPIO%d) set to %d\n", 
                OUTPUT_NAMES[output_num], 
                OUTPUT_PINS[output_num], 
                value);
  
  return true;
}

void io_set_outputs(const uint8_t* values, int count) {
  int num_to_set = (count < NUM_DIGITAL_OUTPUTS) ? count : NUM_DIGITAL_OUTPUTS;
  
  for (int i = 0; i < num_to_set; i++) {
    io_set_output(i, values[i]);
  }
}

int io_get_input_by_name(const char* name) {
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    if (strcmp(INPUT_NAMES[i], name) == 0) {
      return input_states[i];
    }
  }
  return -1;  // Not found
}

int io_get_output_by_name(const char* name) {
  for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
    if (strcmp(OUTPUT_NAMES[i], name) == 0) {
      return output_states[i];
    }
  }
  return -1;  // Not found
}

int io_set_output_by_name(const char* name, const uint8_t value) {
  for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
    if (strcmp(OUTPUT_NAMES[i], name) == 0) {
      io_set_output(i, value);  // updates output_states[] and drives GPIO
      return i;
    }
  }
  return -1;  // Not found
}


void io_monitor_task(void* parameter) {
  Serial.println("🔌 I/O Monitor Task started on core " + String(xPortGetCoreID()));
  
  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t frequency = pdMS_TO_TICKS(10);  // 10ms fixed interval
  
  while (1) {
      if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
          io_read_inputs();
          
          if (io_has_state_changed()) {
              Serial.print("🔌 I/O State changed: ");
              io_publish_needed = true;
              Serial.printf("Set true on cpu core %d\n", (int)xPortGetCoreID());
              for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
                  Serial.printf("%s=%d ", INPUT_NAMES[i], input_states[i]);
              }
              Serial.println();
          }
          
          xSemaphoreGive(io_state_mutex);
      }
      
      vTaskDelayUntil(&last_wake_time, frequency);  // ✅ Precise timing
  }
}