#ifndef MAIN_H
#define MAIN_H      

void connectToWiFi();
void connectToMQTT();
void mqttCallback(char *topic, unsigned char *payload, unsigned int length);
void processResponse(const char *topic, const char *payload);

#endif 
