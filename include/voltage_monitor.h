/**
 * @file voltage_monitor.h
 * @brief Voltage Monitoring Module
 * 
 * Handles battery and solar voltage monitoring using ADC
 */

#ifndef VOLTAGE_MONITOR_H
#define VOLTAGE_MONITOR_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ====================================================================
// VOLTAGE MONITORING CONFIGURATION
// ====================================================================

// ADC pins
extern const int BATTERY_ADC_PIN;
extern const int SOLAR_ADC_PIN;

// Voltage divider ratios (adjust based on your resistors)
extern const float BATTERY_VOLTAGE_RATIO;
extern const float SOLAR_VOLTAGE_RATIO;

// ADC configuration
extern const int ADC_SAMPLES;
extern const int ADC_RESOLUTION;

// Voltage readings (protected by mutex)
extern float battery_voltage;
extern float solar_voltage;
extern SemaphoreHandle_t voltage_mutex;

// ====================================================================
// VOLTAGE MONITORING FUNCTIONS
// ====================================================================

/**
 * @brief Initialize voltage monitoring (ADC setup)
 */
void voltage_init();

/**
 * @brief Read ADC voltage with averaging
 * @param pin ADC pin to read
 * @param samples Number of samples to average
 * @return Voltage reading in volts
 */
float voltage_read_adc(int pin, int samples);

/**
 * @brief Read both battery and solar voltages
 * Updates global battery_voltage and solar_voltage variables
 * Thread-safe (uses voltage_mutex)
 */
void voltage_read_all();

/**
 * @brief Get battery status string
 * @param voltage Battery voltage
 * @return Status string: "full", "good", "low", "critical"
 */
const char* voltage_get_battery_status(float voltage);

/**
 * @brief Check if solar is charging
 * @param solar_v Solar voltage
 * @param battery_v Battery voltage
 * @return true if solar voltage > battery voltage + threshold
 */
bool voltage_is_solar_charging(float solar_v, float battery_v);

/**
 * @brief FreeRTOS task for continuous voltage monitoring
 * @param parameter Task parameter (unused)
 */
void voltage_monitor_task(void* parameter);

#endif // VOLTAGE_MONITOR_H
