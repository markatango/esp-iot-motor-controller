/**
 * @file main.h
 * @brief Header file for ESP32 ChickenCoop Multi-threaded Monitor
 * 
 * This file contains all configuration constants, pin definitions,
 * global variable declarations, and function prototypes for the
 * three-task monitoring system (Time Sync, I/O Monitor, Voltage Monitor).
 */

#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "broker_config.h"  // Broker configurations
#include "client.h"         // Client certificate configurations
#include "io.h"             // I/O pin definitions
#include "secrets.h"       // WiFi and MQTT credentials

// =============================================================================
// CONFIGURATION SECTION - Modify these values for your setup
// =============================================================================

// WiFi Configuration
extern const char* ssid;
extern const char* password;

// Broker and Client Selection
extern const char* selected_broker;   // Name from broker_config.h
extern const char* selected_client;   // Name from client.h

// MQTT Topics
extern const char* topic_time;
extern const char* topic_io_state;
extern const char* topic_io_control;
extern const char* topic_voltage;

// NTP Server Configuration
extern const char* ntp_server;
extern const long gmt_offset_sec;        // Timezone offset in seconds
extern const int daylight_offset_sec;    // Daylight saving offset in seconds

// =============================================================================
// PIN DEFINITIONS
// =============================================================================

// Digital Input Pins (6 inputs)
extern const int INPUT_PINS[NUM_DIGITAL_INPUTS];

// Digital Output Pins (4 outputs)
extern const int OUTPUT_PINS[NUM_DIGITAL_OUTPUTS];

// Analog Input Pins (ADC1 channels - WiFi safe)
extern const int BATTERY_VOLTAGE_PIN;    // GPIO36 - ADC1_CH0 (VP)
extern const int SOLAR_VOLTAGE_PIN;      // GPIO39 - ADC1_CH3 (VN)

// =============================================================================
// VOLTAGE MONITORING CONFIGURATION
// =============================================================================

// Voltage Divider Ratios
// Battery: (R1+R2)/R2 for your resistor values (e.g., 47K+10K = 5.7)
// Solar: (R1+R2)/R2 for your resistor values (e.g., 100K+11K = 10.09)
extern const float BATTERY_VOLTAGE_RATIO;
extern const float SOLAR_VOLTAGE_RATIO;

// ADC Configuration
extern const float ADC_REFERENCE_VOLTAGE;  // ESP32 ADC reference (3.3V)
extern const int ADC_RESOLUTION;           // 12-bit ADC (0-4095)
extern const int ADC_SAMPLES;              // Number of samples to average

// =============================================================================
// TIMING CONFIGURATION
// =============================================================================

// Debounce delay for digital inputs (milliseconds)
extern const unsigned long DEBOUNCE_DELAY;

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

// I/O State Arrays
extern uint8_t input_states[NUM_DIGITAL_INPUTS];
extern uint8_t output_states[NUM_DIGITAL_OUTPUTS];
extern uint8_t last_input_states[NUM_DIGITAL_INPUTS];
extern uint8_t last_output_states[NUM_DIGITAL_OUTPUTS];

// Voltage Readings
extern float battery_voltage;
extern float solar_voltage;

// Debounce State
extern unsigned long last_debounce_time[NUM_DIGITAL_INPUTS];
extern uint8_t last_reading[NUM_DIGITAL_INPUTS];

// Time Tracking
extern time_t current_time;
extern time_t last_published_time;

// FreeRTOS Task Handles
extern TaskHandle_t time_task_handle;
extern TaskHandle_t io_task_handle;
extern TaskHandle_t voltage_task_handle;

// FreeRTOS Semaphore Handles (Mutexes)
extern SemaphoreHandle_t mqtt_mutex;
extern SemaphoreHandle_t io_state_mutex;
extern SemaphoreHandle_t voltage_mutex;

// Network Clients
extern WiFiClientSecure espClient;
extern PubSubClient mqtt_client;

// =============================================================================
// FUNCTION PROTOTYPES
// =============================================================================

// Setup Functions
/**
 * @brief Initialize WiFi connection
 */
void setup_wifi();

/**
 * @brief Configure digital I/O pins
 */
void setup_pins();

/**
 * @brief Configure ADC for voltage monitoring
 */
void setup_adc();

/**
 * @brief Initialize NTP time synchronization
 */
void setup_ntp();

/**
 * @brief Configure SSL/TLS certificates for MQTT
 * @param broker Broker configuration with server certificate
 * @param client Client configuration with certificates and keys
 */
void setup_ssl(const BrokerConfig* broker, const ClientConfig* client);

// MQTT Functions
/**
 * @brief Reconnect to MQTT broker if connection is lost
 */
void reconnect_mqtt();

/**
 * @brief MQTT message callback handler
 * @param topic MQTT topic that received a message
 * @param payload Message payload
 * @param length Payload length
 */
void callback(char* topic, byte* payload, unsigned int length);

// FreeRTOS Task Functions
/**
 * @brief Time synchronization task (runs on Core 0)
 * @param parameter Task parameter (unused)
 */
void time_sync_task(void* parameter);

/**
 * @brief I/O monitoring task (runs on Core 1)
 * @param parameter Task parameter (unused)
 */
void io_monitor_task(void* parameter);

/**
 * @brief Voltage monitoring task (runs on Core 0)
 * @param parameter Task parameter (unused)
 */
void voltage_monitor_task(void* parameter);

// Input/Output Functions
/**
 * @brief Read and debounce digital inputs
 */
void read_inputs();

/**
 * @brief Read battery and solar voltages from ADC
 */
void read_voltages();

/**
 * @brief Check if I/O state has changed since last check
 * @return true if state changed, false otherwise
 */
bool has_state_changed();

// ADC Functions
/**
 * @brief Read ADC voltage with multiple sample averaging
 * @param pin ADC pin to read
 * @param samples Number of samples to average
 * @return Average voltage reading
 */
float read_adc_voltage(int pin, int samples);

// MQTT Publishing Functions
/**
 * @brief Publish I/O state to MQTT
 */
void publish_io_state();

/**
 * @brief Publish current time to MQTT
 */
void publish_time();

/**
 * @brief Publish voltage readings to MQTT
 */
void publish_voltage();

#endif // MAIN_H