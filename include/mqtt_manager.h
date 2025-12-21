/**
 * @file mqtt_manager.h
 * @brief MQTT Client Manager
 * 
 * Handles MQTT connection, SSL/TLS, publishing, and subscriptions
 */

#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include "broker_config.h"
#include "client.h"

// ====================================================================
// MQTT CONFIGURATION
// ====================================================================

// MQTT Topics
extern const char* TOPIC_TIME;
extern const char* TOPIC_IO_STATE;
extern const char* TOPIC_IO_CONTROL;
extern const char* TOPIC_VOLTAGE;
extern const char* TOPIC_STATUS_REQUEST;

// MQTT Client
extern WiFiClientSecure esp_client;
extern PubSubClient mqtt_client;
extern SemaphoreHandle_t mqtt_mutex;
extern volatile bool io_publish_needed;

// ====================================================================
// MQTT FUNCTIONS
// ====================================================================

/**
 * @brief Initialize MQTT manager
 * Creates mutex and sets up client
 */
void mqtt_init();

/**
 * @brief Configure SSL/TLS certificates
 * @param broker Broker configuration
 * @param client Client certificate configuration
 */
void mqtt_setup_ssl(const BrokerConfig* broker, const ClientConfig* client);

/**
 * @brief Connect to MQTT broker
 * @param broker_name Name of broker from broker_config.h
 * @param client_name Name of client from client.h
 * @param client_id MQTT client ID
 * @return true if connected, false otherwise
 */
bool mqtt_connect(const char* broker_name, const char* client_name, const char* client_id);

/**
 * @brief Reconnect to MQTT broker (blocking until connected)
 * @param broker_name Name of broker from broker_config.h
 * @param client_name Name of client from client.h
 * @param client_id MQTT client ID
 */
void mqtt_reconnect(const char* broker_name, const char* client_name, const char* client_id);

/**
 * @brief Check if MQTT client is connected
 * @return true if connected, false otherwise
 */
bool mqtt_is_connected();

/**
 * @brief MQTT loop - call regularly to process messages
 * Thread-safe (uses mqtt_mutex)
 */
void mqtt_loop();

/**
 * @brief Publish I/O state to MQTT
 */
void mqtt_publish_io_state();

/**
 * @brief Publish voltage readings to MQTT
 */
void mqtt_publish_voltage();

/**
 * @brief Publish time to MQTT
 */
void mqtt_publish_time();

/**
 * @brief MQTT callback for received messages
 * @param topic Topic string
 * @param payload Message payload
 * @param length Payload length
 */
void mqtt_callback(char* topic, byte* payload, unsigned int length);

/**
 * @brief Set custom MQTT callback
 * Allows application to provide its own callback function
 * @param callback Callback function pointer
 */
void mqtt_set_callback(void (*callback)(char*, byte*, unsigned int));

#endif // MQTT_MANAGER_H
