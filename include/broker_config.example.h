/**
 * @file broker_config.example.h
 * @brief MQTT Broker Configuration — Template / Example
 *
 * Copy this file to include/broker_config.h and fill in your own values.
 * broker_config.h is excluded from version control (.gitignore).
 */

#ifndef BROKER_CONFIG_H
#define BROKER_CONFIG_H

// ====================================================================
// BROKER CERTIFICATES — Declarations
// Actual certificate strings are defined in broker_config.cpp
// ====================================================================

extern const char* yourRemoteBrokerCert;  // CA cert for your remote broker
extern const char* emqxCert;              // CA cert for broker.emqx.io
extern const char* localCert;             // CA cert for your local broker

// ====================================================================
// BROKER CONFIGURATION STRUCTURE
// ====================================================================

struct BrokerConfig {
    const char* name;        // Friendly name used to select the broker at runtime
    const char* url;         // Broker hostname or IP address
    const char* server_cert; // CA certificate that signed the broker's server cert
    const char* username;    // MQTT username (empty string = no auth)
    const char* password;    // MQTT password
    int port_plain;          // Plain MQTT port (typically 1883)
    int port_ssl;            // SSL/TLS MQTT port (typically 8883)
};

// ====================================================================
// BROKER CONFIGURATIONS
// Add, remove, or edit entries to match your environment.
// The name string is what you pass to mqtt_connect() / SELECTED_BROKER.
// ====================================================================

const BrokerConfig BROKER_CONFIGS[] = {
    {
        "your-remote-broker",       // name  — matches SELECTED_BROKER in secrets.h
        "your.broker.hostname",     // url
        yourRemoteBrokerCert,       // CA certificate
        "",                         // username  (empty = no auth)
        "",                         // password
        1883,                       // plain MQTT port
        8883                        // SSL MQTT port
    },
    {
        "broker.emqx.io",           // name  — public test broker
        "broker.emqx.io",           // url
        emqxCert,                   // CA certificate
        "your_emqx_username",       // username
        "your_emqx_password",       // password
        1883,
        8883
    },
    {
        "local-broker",             // name
        "192.168.x.x",             // url  — your local broker IP or hostname
        localCert,                  // CA certificate
        "",                         // username
        "",                         // password
        1883,
        8883
    }
    // Add more brokers here as needed
};

const int NUM_BROKERS = sizeof(BROKER_CONFIGS) / sizeof(BROKER_CONFIGS[0]);

// ====================================================================
// ACCESS FUNCTIONS — implemented in broker_config.cpp
// ====================================================================

const BrokerConfig* getBrokerConfig(const char* name);
const BrokerConfig* getBrokerByIndex(int index);
void listBrokers();
void printBrokerInfo(const BrokerConfig* config);

#endif // BROKER_CONFIG_H
