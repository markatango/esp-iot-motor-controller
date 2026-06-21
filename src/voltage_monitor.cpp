/**
 * @file voltage_monitor.cpp
 * @brief Voltage Monitoring Implementation
 */

#include "voltage_monitor.h"
#include "state_machine.h"

// ====================================================================
// CONFIGURATION
// ====================================================================

// ADC pins
const int BATTERY_ADC_PIN = 34;  // VP (GPIO36)
const int SOLAR_ADC_PIN = 35;    // VN (GPIO39)

// Voltage divider ratios
// Battery: 47kΩ + 10kΩ = 57kΩ total, 10kΩ to ADC
// Ratio = (47 + 10) / 10 = 5.7
const float BATTERY_VOLTAGE_RATIO = 5.7;

// Solar: 100kΩ + 11kΩ = 111kΩ total, 11kΩ to ADC
// Ratio = (100 + 11) / 11 = 10.09
const float SOLAR_VOLTAGE_RATIO = 10.09;

// ADC configuration
const int ADC_SAMPLES = 10;
const int ADC_RESOLUTION = 4095;  // 12-bit ADC

// ====================================================================
// GLOBAL VARIABLES
// ====================================================================

float battery_voltage = 0.0;
float solar_voltage = 0.0;
SemaphoreHandle_t voltage_mutex = NULL;

// ====================================================================
// FUNCTIONS
// ====================================================================

void voltage_init() {
  Serial.println("\n⚡ Initializing voltage monitoring...");
  
  // Configure ADC pins
  pinMode(BATTERY_ADC_PIN, INPUT);
  pinMode(SOLAR_ADC_PIN, INPUT);
  
  // Set ADC attenuation for 0-3.3V range
  analogSetAttenuation(ADC_11db);  // Allows up to ~3.3V input
  
  // Create mutex
  voltage_mutex = xSemaphoreCreateMutex();
  if (voltage_mutex == NULL) {
    Serial.println("❌ Failed to create voltage mutex!");
    while(1) delay(1000);
  }
  
  Serial.printf("   Battery ADC: GPIO%d (ratio: %.2f)\n", BATTERY_ADC_PIN, BATTERY_VOLTAGE_RATIO);
  Serial.printf("   Solar ADC: GPIO%d (ratio: %.2f)\n", SOLAR_ADC_PIN, SOLAR_VOLTAGE_RATIO);
  Serial.println("✅ Voltage monitoring initialized");
}

float voltage_read_adc(int pin, int samples) {
  uint32_t sum = 0;
  
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  
  float average = (float)sum / samples;
  float voltage = (average / ADC_RESOLUTION) * 3.3;  // Convert to voltage
  
  return voltage;
}

void voltage_read_all() {
  // Read ADC values with averaging
  float battery_adc = voltage_read_adc(BATTERY_ADC_PIN, ADC_SAMPLES);
  float solar_adc = voltage_read_adc(SOLAR_ADC_PIN, ADC_SAMPLES);
  
  // Apply voltage divider ratios
  float batt_v = battery_adc * BATTERY_VOLTAGE_RATIO;
  float sol_v = solar_adc * SOLAR_VOLTAGE_RATIO;
  
  // Update global variables (thread-safe)
  if (xSemaphoreTake(voltage_mutex, portMAX_DELAY) == pdTRUE) {
    battery_voltage = batt_v;
    solar_voltage = sol_v;
    xSemaphoreGive(voltage_mutex);
  }
}

const char* voltage_get_battery_status(float voltage) {
  if (voltage > 12.6) {
    return "full";
  } else if (voltage > 12.0) {
    return "good";
  } else if (voltage > 11.5) {
    return "low";
  } else {
    return "critical";
  }
}

bool voltage_is_solar_charging(float solar_v, float battery_v) {
  return (solar_v > battery_v + 0.5);
}

void voltage_monitor_task(void* parameter) {
  Serial.println("⚡ Voltage Monitor Task started on core " + String(xPortGetCoreID()));
  
  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t frequency = pdMS_TO_TICKS(60000);  // Read every 60 seconds
  
  while (1) {
    voltage_read_all();

    if (!g_sm_freeze) {
      if (xSemaphoreTake(voltage_mutex, portMAX_DELAY) == pdTRUE) {
        Serial.printf("⚡ Battery: %.2fV (%s) | Solar: %.2fV | Charging: %s\n",
                      battery_voltage,
                      voltage_get_battery_status(battery_voltage),
                      solar_voltage,
                      voltage_is_solar_charging(solar_voltage, battery_voltage) ? "Yes" : "No");
        xSemaphoreGive(voltage_mutex);
      }
    }

    vTaskDelayUntil(&last_wake_time, frequency);
  }
}
