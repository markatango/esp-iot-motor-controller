#ifndef MAIN_H
#define MAIN_H      

#include <WiFi.h>
#include <PubSubClient.h>

void connectToWiFi();
void connectToMQTT();
void mqttCallback(char *topic, unsigned char *payload, unsigned int length);
void processResponse(const char *topic, const char *payload);
void printBrokerInfo();

// Configure static IP
IPAddress local_IP(10, 0, 0, 200);      // Choose an unused IP in your network
IPAddress gateway(10, 0, 0, 1);         // Your router IP
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);       // Google DNS
IPAddress secondaryDNS(8, 8, 4, 4);     // Optional
#endif 
