#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClient.h>          // For plain MQTT
#include <WiFiClientSecure.h>    // For SSL MQTT
#include "broker_config.h"       // Broker dictionary
#include "client.h"              // Client certificate dictionary 
#include "secrets.h"             // WiFi and MQTT credentials
#include "io.h"                  // LED control
#include "main.h"                // Function declarations

// ====================================================================
// CONFIGURATION - Change these lines to switch brokers and clients!
// ====================================================================
#define SELECTED_BROKER "data-dancer.com"
#define SELECTED_CLIENT "data-dancer-client"  // or "no-client-cert" for server-only verification
#define USE_SSL true  // true for SSL, false for plain MQTT
// ====================================================================

// WiFi credentials
const char *ssid = SSID;           
const char *password = PASSWORD;   

// Current broker and client (loaded from dictionaries)
const BrokerConfig* currentBroker = nullptr;
const ClientConfig* currentClient = nullptr;

// MQTT clients - we need BOTH types
WiFiClient plainClient;              // For plain MQTT (port 1883)
WiFiClientSecure sslClient;          // For SSL MQTT (port 8883)

// PubSubClient - we'll set the client type in setup()
#if USE_SSL
    PubSubClient mqtt_client(sslClient);   // Use SSL client
#else
    PubSubClient mqtt_client(plainClient); // Use plain client
#endif

// MQTT topic from secrets.h
const char *mqtt_topic = MQTT_TOPIC;

void setup() {
    // Use static IP (optional)
    // Set values in main.h

    // Uncomment the following lines to enable static IP configuration
    
    // Serial.println("Configuring static IP...");
    // if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    //     Serial.println("Static IP configuration failed!");
    // }

    
 
    Serial.begin(115200);
    WiFi.begin();
    delay(1000);
    setupIO();
    // setLEDsTo(true);
    mapSwToLed();
  
    Serial.println("\n🚀 ESP32 MQTT Client with Separated Certificate Management");
    Serial.println("============================================================");
    
    // ================================================================
    // LOAD BROKER CONFIGURATION
    // ================================================================
    currentBroker = getBrokerConfig(SELECTED_BROKER);
    
    if (currentBroker == nullptr) {
        Serial.printf("❌ ERROR: Broker '%s' not found!\n", SELECTED_BROKER);
        Serial.println("\n📋 Available brokers:");
        listBrokers();
        while(1) delay(1000);  // Stop execution
    }
    
    // ================================================================
    // LOAD CLIENT CONFIGURATION
    // ================================================================
    currentClient = getClientConfig(SELECTED_CLIENT);
    
    if (currentClient == nullptr) {
        Serial.printf("❌ ERROR: Client config '%s' not found!\n", SELECTED_CLIENT);
        Serial.println("\n📋 Available client configs:");
        listClients();
        while(1) delay(1000);  // Stop execution
    }
    
    // Validate client configuration
    if (!validateClientConfig(currentClient)) {
        Serial.println("❌ ERROR: Invalid client configuration!");
        while(1) delay(1000);  // Stop execution
    }
    
    // Display configurations
    printBrokerInfo();
    printClientInfo(currentClient);

    // Connect to WiFi
    connectToWiFi();

    // ================================================================
    // CONFIGURE SSL/TLS
    // ================================================================
    #if USE_SSL
        Serial.println("🔐 Configuring SSL/TLS...");
        
        // 1. Set CA certificate for broker verification
        //    Priority: Use client's CA if available, otherwise use broker's CA
        const char* caToUse = (currentClient->ca_cert != nullptr) 
                              ? currentClient->ca_cert 
                              : currentBroker->server_cert;
        
        if (caToUse != nullptr) {
            sslClient.setCACert(caToUse);
            if (currentClient->ca_cert != nullptr) {
                Serial.println("   ✅ Using client's CA certificate for broker verification");
            } else {
                Serial.println("   ✅ Using broker's CA certificate for broker verification");
            }
        } else {
            Serial.println("   ⚠️  No CA certificate available - verification may fail!");
        }
        
        // 2. Set client certificate and key (if available for mTLS)
        if (currentClient->cert != nullptr && currentClient->key != nullptr) {
            Serial.println("   🔑 Loading client certificate for mutual TLS...");
            
            // Set client certificate
            sslClient.setCertificate(currentClient->cert);
            Serial.println("      ✅ Client certificate loaded");
            
            // Set client private key
            sslClient.setPrivateKey(currentClient->key);
            Serial.println("      ✅ Client private key loaded");
            
            Serial.println("   🔒 Mutual TLS (mTLS) enabled - client will authenticate to broker");
        } else {
            Serial.println("   🔓 No client certificate - server verification only");
        }
        
        // Optional: Skip certificate verification for testing
        // WARNING: Only use this for testing with self-signed certificates!
        // sslClient.setInsecure();
        // Serial.println("   ⚠️  Certificate verification DISABLED (insecure mode)");
        
        Serial.println("✅ SSL/TLS configuration complete\n");
    #endif
    
    // ================================================================
    // CONFIGURE MQTT
    // ================================================================
    #if USE_SSL
        int port = currentBroker->port_ssl;
        Serial.printf("🔐 Using SSL/TLS on port %d\n", port);
    #else
        int port = currentBroker->port_plain;
        Serial.printf("📡 Using plain MQTT on port %d\n", port);
    #endif

    mqtt_client.setServer(currentBroker->url, port);
    mqtt_client.setKeepAlive(60);
    mqtt_client.setBufferSize(512);  // Default is 256
    mqtt_client.setCallback(mqttCallback);

    connectToMQTT();
}

void printBrokerInfo() {
    Serial.println("📡 Broker Configuration:");
    Serial.println("========================");
    Serial.printf("   Name: %s\n", currentBroker->name);
    Serial.printf("   URL: %s\n", currentBroker->url);
    Serial.printf("   SSL: %s\n", USE_SSL ? "Enabled" : "Disabled");
    
    // Show username/password authentication
    if (strlen(currentBroker->username) > 0) {
        Serial.printf("   👤 Username: %s\n", currentBroker->username);
        Serial.println("   🔑 Password: ********");
    } else {
        Serial.println("   🔓 Username/Password: None");
    }
    
    Serial.println();
}

void connectToWiFi() {
    Serial.println("📶 Connecting to WiFi...");
    WiFi.enableIpV6();
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ WiFi connected!");
        Serial.printf("📍 IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("📡 Signal: %d dBm\n", WiFi.RSSI());
    } else {
        Serial.println("\n❌ WiFi connection failed!");
        while(1) delay(1000);
    }
}

void connectToMQTT() {
    Serial.println("\n🔗 Connecting to MQTT Broker...");
    
    int attempts = 0;
    const int maxAttempts = 10;
    
    while (!mqtt_client.connected() && attempts < maxAttempts) {
        attempts++;
        
        String client_id = "esp32-client-" + String(WiFi.macAddress());
        Serial.printf("🔄 Attempt %d/%d: Client ID: %s\n", 
                     attempts, maxAttempts, client_id.c_str());
        
        bool connected = false;
        
        // Connect with or without username/password credentials
        if (strlen(currentBroker->username) > 0) {
            Serial.println("   Authenticating with username/password...");
            connected = mqtt_client.connect(
                client_id.c_str(),
                currentBroker->username,
                currentBroker->password
            );
        } else {
            Serial.println("   Connecting without username/password...");
            connected = mqtt_client.connect(client_id.c_str());
        }
        
        if (connected) {
            Serial.println("✅ MQTT Connected!");
            
            #if USE_SSL
                // Confirm authentication method used
                if (currentClient->cert != nullptr && currentClient->key != nullptr) {
                    Serial.println("🔒 Mutual TLS (client certificate) authentication successful!");
                } else {
                    Serial.println("🔓 Server verification successful (no client cert)");
                }
            #endif
            
            // Subscribe to topics
            String topic = mqtt_topic;
            if (mqtt_client.subscribe(topic.c_str())) {
                Serial.printf("📡 Subscribed: %s\n", topic.c_str());
            } else {
                Serial.printf("⚠️  Failed to subscribe to: %s\n", topic.c_str());
            }
            
            // Send connection announcement
            String msg = "ESP32 connected to " + String(currentBroker->name);
            #if USE_SSL
                msg += " (SSL";
                if (currentClient->cert != nullptr) {
                    msg += " + mTLS";
                }
                msg += ")";
            #else
                msg += " (Plain)";
            #endif
            
            if (mqtt_client.publish(topic.c_str(), msg.c_str())) {
                Serial.printf("📤 Published: %s\n", msg.c_str());
            }

            break;
            
        } else {
            int errorCode = mqtt_client.state();
            Serial.printf("❌ Failed, error code: %d\n", errorCode);
            
            // Decode error codes
            switch(errorCode) {
                case -4:
                    Serial.println("   Connection timeout");
                    break;
                case -3:
                    Serial.println("   Connection lost");
                    break;
                case -2:
                    Serial.println("   Connect failed");
                    #if USE_SSL
                        if (currentClient->cert != nullptr) {
                            Serial.println("   💡 Hint: Check client certificate validity and matching");
                        }
                        Serial.println("   💡 Hint: Verify CA certificate matches broker");
                    #endif
                    break;
                case -1:
                    Serial.println("   Disconnected");
                    break;
                case 1:
                    Serial.println("   Bad protocol");
                    break;
                case 2:
                    Serial.println("   Bad client ID");
                    break;
                case 3:
                    Serial.println("   Server unavailable");
                    break;
                case 4:
                    Serial.println("   Bad credentials");
                    if (strlen(currentBroker->username) > 0) {
                        Serial.println("   💡 Hint: Check username/password");
                    }
                    break;
                case 5:
                    Serial.println("   Not authorized");
                    #if USE_SSL
                        if (currentClient->cert != nullptr) {
                            Serial.println("   💡 Hint: Broker may require different client certificate");
                        } else {
                            Serial.println("   💡 Hint: Broker may require client certificate");
                        }
                    #endif
                    break;
                default:
                    Serial.printf("   Unknown error: %d\n", errorCode);
            }
            
            if (attempts < maxAttempts) {
                Serial.println("   Retrying in 3 seconds...");
                delay(3000);
            }
        }
    }
    
    if (!mqtt_client.connected()) {
        Serial.println("\n❌ Failed to connect after maximum attempts");
        Serial.println("💡 Troubleshooting tips:");
        Serial.println("   1. Check broker URL and port");
        Serial.println("   2. Verify WiFi connection");
        #if USE_SSL
            Serial.println("   3. Verify CA certificate matches broker");
            if (currentClient->cert != nullptr) {
                Serial.println("   4. Check client certificate is valid and matches broker requirements");
                Serial.println("   5. Ensure client private key matches certificate");
            }
        #endif
        if (strlen(currentBroker->username) > 0) {
            Serial.println("   6. Verify username and password");
        }
        
        Serial.println("\n📋 Review your configuration:");
        Serial.printf("   Broker: %s\n", SELECTED_BROKER);
        Serial.printf("   Client: %s\n", SELECTED_CLIENT);
    }
}

void mqttCallback(char *topic, unsigned char *payload, unsigned int length) {
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    
    Serial.printf("\n📨 [%s]: %s\n", topic, message);
    processResponse(topic, message);
}

void processResponse(const char *topic, const char *payload) {
    Serial.printf("Processing response for topic: %s\n", topic);
    Serial.printf("Payload: %s\n", payload);
    
    if (strcmp(payload, "swChanged") == 0) {
        mapSwToLed();
        printSwStates();
    } else if (strcmp(payload, "status") == 0) {
        // Report client status
        String status = "ESP32 Online - Broker: ";
        status += currentBroker->name;
        status += " | Client: ";
        status += currentClient->name;
        
        #if USE_SSL
            status += " (SSL";
            if (currentClient->cert != nullptr) {
                status += "+mTLS)";
            } else {
                status += ")";
            }
        #else
            status += " (Plain)";
        #endif
        
        mqtt_client.publish(mqtt_topic, status.c_str());
    } else if (strcmp(payload, "list-config") == 0) {
        // List available configurations
        String response = "Brokers: ";
        response += String(NUM_BROKERS);
        response += " | Clients: ";
        response += String(NUM_CLIENTS);
        mqtt_client.publish(mqtt_topic, response.c_str());
    }
}

void loop() {
    // Check connection and reconnect if necessary
    if (!mqtt_client.connected()) {
        Serial.println("⚠️  Connection lost, reconnecting...");
        connectToMQTT();
    }
    
    mqtt_client.loop();
    
    // Read switch states and publish changes
    readSwitches();
    if (hasSwChanged()) {
        Serial.println("🔘 Switch state changed!");
        if (mqtt_client.publish(mqtt_topic, "swChanged")) {
            Serial.println("   ✅ Published switch change");
        } else {
            Serial.println("   ❌ Failed to publish switch change");
        }
    }
    
    delay(100);  // Small delay to avoid overwhelming the loop
}
