#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClient.h>          // For plain MQTT
#include <WiFiClientSecure.h>    // For SSL MQTT
#include "broker_config.h"  // Our broker dictionary
#include "secrets.h"  // Include your secrets header for WiFi and MQTT credentials
#include "io.h"  // Include your IO header for LED control
#include "main.h"   // Include your main header for function declarations

// #include "../certs/data-dancer.com.pem.h"


// ====================================================================
// BROKER SELECTION - Just change this line to switch brokers!
// ====================================================================
#define SELECTED_BROKER "data-dancer.com"  
#define USE_SSL true  // true for SSL, false for plain MQTT
 
// // WiFi credentials
const char *ssid = SSID;             // Replace with your WiFi name
const char *password = PASSWORD;   // Replace with your WiFi password

// Current broker (will be loaded from dictionary)
const BrokerConfig* currentBroker = nullptr;

// MQTT clients - we need BOTH types
WiFiClient plainClient;              // For plain MQTT (port 1883)
WiFiClientSecure sslClient;          // For SSL MQTT (port 8883)

// PubSubClient - we'll set the client type in setup()
#if USE_SSL
    PubSubClient mqtt_client(sslClient);   // Use SSL client
#else
    PubSubClient mqtt_client(plainClient); // Use plain client
#endif

// // MQTT Broker settings
const char *mqtt_broker = MQTT_BROKER;
const char *mqtt_topic = MQTT_TOPIC;
const char *mqtt_username = MQTT_USERNAME;
const char *mqtt_password = MQTT_PASSWORD;
const int mqtt_port = MQTT_PORT;

void setup() {
    Serial.begin(115200);
    delay(1000);
    setupIO();
    setLEDsTo(false);
  
    Serial.println("\n🚀 ESP32 MQTT Client with Broker Dictionary");
    Serial.println("=============================================");
    
    // Load broker configuration from dictionary
    currentBroker = getBrokerConfig(SELECTED_BROKER);
    
    if (currentBroker == nullptr) {
        Serial.printf("❌ ERROR: Broker '%s' not found!\n", SELECTED_BROKER);
        listBrokers();  // Show available brokers
        while(1) delay(1000);  // Stop execution
    }
    
    // Display selected broker
    printBrokerInfo();

    connectToWiFi();

    // Set Root CA certificate
    // esp_client.setCACert(currentBroker->server_cert);
    // esp_client.setInsecure();

    #if USE_SSL
        sslClient.setCACert(currentBroker->server_cert);
        Serial.println("🔑 SSL certificate loaded");
        // Optional: sslClient.setInsecure(); // Skip verification for testing
    #endif
    
    // Configure MQTT - use appropriate port
    #if USE_SSL
        int port = currentBroker->port_ssl;
        Serial.printf("🔐 Using SSL on port %d\n", port);
    #else
        int port = currentBroker->port_plain;
        Serial.printf("📡 Using plain MQTT on port %d\n", port);
    #endif

    mqtt_client.setServer(currentBroker->url, port);
    mqtt_client.setKeepAlive(60);
    mqtt_client.setCallback(mqttCallback);

    connectToMQTT();
}

void printBrokerInfo() {
    Serial.printf("📡 Broker: %s\n", currentBroker->name);
    Serial.printf("🌐 URL: %s\n", currentBroker->url);
    Serial.printf("🔐 SSL: %s\n", USE_SSL ? "Enabled" : "Disabled");
    
    if (strlen(currentBroker->username) > 0) {
        Serial.printf("👤 Username: %s\n", currentBroker->username);
    } else {
        Serial.println("🔓 Auth: None");
    }
    Serial.println();
}

void connectToWiFi() {
    Serial.println("📶 Connecting to WiFi...");
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
        Serial.printf("🔄 Attempt %d: %s...\n", attempts, client_id.c_str());
        
        bool connected = false;
        
        // Connect with or without credentials
        if (strlen(currentBroker->username) > 0) {
            connected = mqtt_client.connect(
                client_id.c_str(),
                currentBroker->username,
                currentBroker->password
            );
        } else {
            connected = mqtt_client.connect(client_id.c_str());
        }
        
        if (connected) {
            Serial.println("✅ Connected!");
            
            // Subscribe and publish
            String topic = mqtt_topic;
            mqtt_client.subscribe(topic.c_str());
            Serial.printf("📡 Subscribed: %s\n", topic.c_str());
            
            String msg = "ESP32 connected to " + String(currentBroker->name) + 
                        (USE_SSL ? " (SSL)" : " (Plain)");
            // mqtt_client.publish(topic.c_str(), msg.c_str());
            // Serial.println("📤 Message sent");
            
            break;
        } else {
            Serial.printf("❌ Failed, rc=%d\n", mqtt_client.state());
            if (attempts < maxAttempts) {
                delay(3000);
            }
        }
    }
}

void mqttCallback(char *topic, unsigned char * payload, unsigned int length) {
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    
    Serial.printf("\n📨 [%s]: %s\n", topic, message);
    
    processResponse(topic, message);
}

void scanControlls() {
    
}

void processResponse(const char *topic, const char *payload) {
    Serial.printf("Processing response for topic: %s\n", topic);
    Serial.printf("Payload: %s\n", payload);
    
    if (strcmp(payload, "ON") == 0) {
        setLEDTo(UPI_LED, 1);   // Turn on LED
        mqtt_client.publish(mqtt_topic, "OFF");
    } else if (strcmp(payload, "OFF") == 0) {
        setLEDTo(UPI_LED, 0);  // Turn off LED
        mqtt_client.publish(mqtt_topic, "ON");
    } else if (strcmp(payload, "HELLO") == 0) {
        Serial.println("Hello command received.  (broker.emqx.io compatibility thing)");
        Serial.printf("Sending 'ON' command to continue the cycle, using topic: %s\n", mqtt_topic);
        mqtt_client.publish(mqtt_topic, "ON"); // Respond with "ON" to continue the cycle
    } else {
        Serial.println("Unknown command received.");
    }
    delay(1000);  // Delay to allow for processing
}


void loop() {
    if (!mqtt_client.connected()) {
        connectToMQTT();
    }
    mqtt_client.loop();
}
// This code is designed to run on an ESP32 and connect to a WiFi network and an MQTT broker.