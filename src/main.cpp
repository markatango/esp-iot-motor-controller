#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
// #include <WiFiClient.h>

#include "secrets.h"  // Include your secrets header for WiFi and MQTT credentials
#include "io.h"  // Include your IO header for LED control
#include "main.h"   // Include your main header for function declarations

#include "../certs/data-dancer.com.pem.h"
 
// // WiFi credentials
const char *ssid = SSID;             // Replace with your WiFi name
const char *password = PASSWORD;   // Replace with your WiFi password

// MQTT Broker settings
const char *mqtt_broker = MQTT_BROKER;
const char *mqtt_topic = MQTT_TOPIC;
const char *mqtt_username = MQTT_USERNAME;
const char *mqtt_password = MQTT_PASSWORD;
const int mqtt_port = MQTT_PORT;

// WiFi and MQTT client initialization for secure connection
WiFiClientSecure esp_client;
PubSubClient mqtt_client(esp_client);


// For non-tls connections, you can use the regular WiFiClient
// WiFiClient espClient;
// PubSubClient mqtt_client(espClient);


void setup() {
    Serial.begin(115200);
    connectToWiFi();

    // Set Root CA certificate
    esp_client.setCACert(ca_cert);
    // esp_client.setInsecure();

    mqtt_client.setServer(mqtt_broker, mqtt_port);
    mqtt_client.setKeepAlive(60);
    mqtt_client.setCallback(mqttCallback);
    connectToMQTT();
}

void connectToWiFi() {
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");
}

void connectToMQTT() {
    Serial.printf("Connecting to MQTT Broker at %s:%d\n", mqtt_broker, mqtt_port);
    Serial.printf("🔍 DEBUG: Attempting connection to %s:%d\n", mqtt_broker, mqtt_port);
    Serial.printf("🔍 DEBUG: ESP32 IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("🔍 DEBUG: Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    while (!mqtt_client.connected()) {
        String client_id = "esp32-client-" + String(WiFi.macAddress());
        Serial.printf("Connecting to MQTT Broker as %s...\n", client_id.c_str());
        if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("Connected to MQTT broker");
        
            mqtt_client.subscribe(mqtt_topic);
            Serial.printf("Subscribed to topic: %s\n", mqtt_topic);
            mqtt_client.publish(mqtt_topic, "ON");  // Publish message upon connection
        } else {
            Serial.print("Failed to connect to MQTT broker, rc=");
            Serial.print(mqtt_client.state());
            Serial.println(" Retrying in 5 seconds.");
            delay(5000);
        }
    }
}

void mqttCallback(char *topic, unsigned char * payload, unsigned int length) {
    Serial.print("Message received on topic: ");
    Serial.println(topic);
    Serial.print("Message: ");
    char * str_payload = (char *) payload;
    str_payload[length] = '\0';  // Null-terminate the payload to make it a valid C string
    Serial.println(str_payload);
    Serial.println("\n-----------------------");

    processResponse(topic, str_payload);
}

void processResponse(const char *topic, const char *payload) {
    Serial.printf("Processing response for topic: %s\n", topic);
    Serial.printf("Payload: %s\n", payload);
    
    if (strcmp(payload, "ON") == 0) {
        led(1);  // Turn on LED
        mqtt_client.publish(mqtt_topic, "OFF");
    } else if (strcmp(payload, "OFF") == 0) {
        led(0);  // Turn off LED
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