/**
 * @file broker_config.example.cpp
 * @brief MQTT Broker Configuration — Template / Example
 *
 * Copy this file to src/broker_config.cpp and fill in your own values.
 * broker_config.cpp is excluded from version control (.gitignore).
 *
 * Each broker entry needs the CA certificate that signed the broker's
 * server certificate (PEM format, including the BEGIN/END lines).
 */

#include "broker_config.h"
#include <Arduino.h>
#include <string.h>

// ====================================================================
// CA CERTIFICATES
// Paste the PEM-encoded CA certificate for each broker below.
// This is the certificate that signed your broker's server certificate.
// It is NOT the broker's private key and NOT the client certificate.
// ====================================================================

// CA certificate for your remote cloud broker
const char* yourRemoteBrokerCert = R"EOF(-----BEGIN CERTIFICATE-----
... (paste your remote broker CA certificate here) ...
-----END CERTIFICATE-----
)EOF";

// CA certificate for broker.emqx.io (public test broker — DigiCert root)
const char* emqxCert = R"EOF(-----BEGIN CERTIFICATE-----
... (paste emqx CA certificate here) ...
-----END CERTIFICATE-----
)EOF";

// CA certificate for your local broker (e.g. Raspberry Pi or NAS)
const char* localCert = R"EOF(-----BEGIN CERTIFICATE-----
... (paste local broker CA certificate here) ...
-----END CERTIFICATE-----
)EOF";


// ====================================================================
// ACCESS FUNCTIONS IMPLEMENTATION
// ====================================================================

const BrokerConfig* getBrokerConfig(const char* brokerName) {
    for (int i = 0; i < NUM_BROKERS; i++) {
        if (strcmp(BROKER_CONFIGS[i].name, brokerName) == 0) {
            return &BROKER_CONFIGS[i];
        }
    }
    return nullptr;
}

const BrokerConfig* getBrokerByIndex(int index) {
    if (index >= 0 && index < NUM_BROKERS) {
        return &BROKER_CONFIGS[index];
    }
    return nullptr;
}

void listBrokers() {
    Serial.println("\n📡 Available MQTT Brokers:");
    Serial.println("====================================");

    for (int i = 0; i < NUM_BROKERS; i++) {
        Serial.printf("\n🔹 %s\n", BROKER_CONFIGS[i].name);
        Serial.printf("   URL: %s\n", BROKER_CONFIGS[i].url);
        Serial.printf("   Ports: %d (plain), %d (SSL)\n",
                     BROKER_CONFIGS[i].port_plain,
                     BROKER_CONFIGS[i].port_ssl);

        if (strlen(BROKER_CONFIGS[i].username) > 0) {
            Serial.printf("   🔐 Auth: Username/Password\n");
        } else {
            Serial.printf("   🔓 Auth: None\n");
        }

        if (BROKER_CONFIGS[i].server_cert != nullptr) {
            Serial.println("   ✅ Server Certificate: Present");
        } else {
            Serial.println("   ❌ Server Certificate: None");
        }
    }
    Serial.println();
}

void printBrokerInfo(const BrokerConfig* config) {
    if (config == nullptr) {
        Serial.println("❌ Invalid broker configuration");
        return;
    }

    Serial.println("📡 Broker Configuration:");
    Serial.println("========================");
    Serial.printf("   Name: %s\n", config->name);
    Serial.printf("   URL: %s\n", config->url);
    Serial.printf("   Plain Port: %d\n", config->port_plain);
    Serial.printf("   SSL Port: %d\n", config->port_ssl);

    if (strlen(config->username) > 0) {
        Serial.printf("   Username: %s\n", config->username);
        Serial.println("   Password: ********");
    } else {
        Serial.println("   Authentication: None");
    }

    if (config->server_cert != nullptr) {
        Serial.println("   ✅ Server Certificate: Present");
    } else {
        Serial.println("   ⚠️  Server Certificate: Missing (insecure)");
    }

    Serial.println();
}
