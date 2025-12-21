/**
 * @file broker_config.h
 * @brief MQTT Broker Configuration with SSL/TLS Certificates
 * 
 * Contains broker definitions with their server certificates and connection details.
 * Supports multiple brokers with easy switching.
 */

#ifndef BROKER_CONFIG_H
#define BROKER_CONFIG_H

// ====================================================================
// BROKER CERTIFICATES - Declarations only (extern)
// ====================================================================
// Actual certificate data is defined in broker_config.cpp

extern const char* dataDancerCert;  // Certificate for data-dancer.com
extern const char* emqxCert;        // Certificate for broker.emqx.io
extern const char* localCert;       // Certificate for local broker

// ====================================================================
// BROKER CONFIGURATION STRUCTURE
// ====================================================================

/**
 * Structure to hold broker configuration
 */
struct BrokerConfig {
    const char* name;           // Friendly name for display
    const char* url;            // Broker hostname or IP
    const char* server_cert;    // Server's SSL certificate
    const char* username;       // MQTT username (empty = no auth)
    const char* password;       // MQTT password
    int port_plain;            // Plain MQTT port (usually 1883)
    int port_ssl;              // SSL MQTT port (usually 8883)
};

// ====================================================================
// BROKER CONFIGURATIONS ARRAY
// ====================================================================

/**
 * Array of broker configurations
 * Add or modify brokers here as needed
 */
const BrokerConfig BROKER_CONFIGS[] = {
    {
        "data-dancer.com",     // name
        "data-dancer.com",     // url
        dataDancerCert,        // certificate
        "",                    // username (empty = no auth)
        "",                    // password
        1883,                  // plain MQTT port
        8883                   // SSL MQTT port
    },
    {
        "broker.emqx.io",      // name
        "broker.emqx.io",      // url
        emqxCert,              // certificate
        "emqx",                // username
        "public",              // password
        1883,                  // plain MQTT port
        8883                   // SSL MQTT port
    },
    {
        "192.168.1.41",        // name
        "192.168.1.41",        // url (local broker)
        localCert,             // certificate
        "",                    // username
        "",                    // password
        1883,                  // plain MQTT port
        8883                   // SSL MQTT port
    }
    // Add more brokers here as needed
};

// Number of brokers in the array
const int NUM_BROKERS = sizeof(BROKER_CONFIGS) / sizeof(BROKER_CONFIGS[0]);

// ====================================================================
// ACCESS FUNCTIONS - Declarations only
// ====================================================================

/**
 * Get broker config by name
 * @param brokerName Name of the broker to find
 * @return Pointer to BrokerConfig or nullptr if not found
 */
const BrokerConfig* getBrokerConfig(const char* brokerName);

/**
 * Get broker config by index
 * @param index Index in the BROKER_CONFIGS array
 * @return Pointer to BrokerConfig or nullptr if index out of range
 */
const BrokerConfig* getBrokerByIndex(int index);

/**
 * List all available broker configurations
 */
void listBrokers();

/**
 * Print broker configuration details
 * @param config Pointer to BrokerConfig to print
 */
void printBrokerInfo(const BrokerConfig* config);

#endif // BROKER_CONFIG_H
