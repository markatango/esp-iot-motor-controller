#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
// #include <WiFiClient.h>

#include "secrets.h"  // Include your secrets header for WiFi and MQTT credentials
#include "io.h"  // Include your IO header for LED control
#include "main.h"   // Include your main header for function declarations
 

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


// Root CA Certificate
// Load DigiCert Global Root G2, which is used by EMQX Public Broker: broker.emqx.io
// const char *ca_cert = R"EOF(
// -----BEGIN CERTIFICATE-----
// MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
// MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
// d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
// MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
// MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
// b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
// 9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
// 2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
// 1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
// q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
// tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
// vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
// BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
// 5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
// 1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
// NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
// Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
// 8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
// pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
// MrY=
// -----END CERTIFICATE-----
// )EOF";

// Load DigiCert Global Root CA ca_cert, which is used by EMQX Cloud Serverless Deployment

// const char* ca_cert = R"EOF(-----BEGIN CERTIFICATE-----
// MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
// MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
// d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
// QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
// MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
// b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
// 9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
// CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
// nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
// 43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
// T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
// gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
// BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
// TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
// DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
// hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
// 06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
// PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
// YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
// CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
// -----END CERTIFICATE-----
// )EOF";

// Load broker pem, which is used by chickencoop_ubuntu server
const char* ca_cert = R"EOF(-----BEGIN CERTIFICATE-----
MIID2DCCAsCgAwIBAgIUXfzn5h2tz3iEnng6BYmGxDsVq5swDQYJKoZIhvcNAQEL
BQAweDELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAkNBMRYwFAYDVQQHDA1TYW4gRnJh
bmNpc2NvMRQwEgYDVQQKDAtNUVRUIEJyb2tlcjEXMBUGA1UECwwOSW9UIERlcGFy
dG1lbnQxFTATBgNVBAMMDDE5Mi4xNjguMS40MTAeFw0yNTA5MjYyMzE1NThaFw0y
NjA5MjYyMzE1NThaMHgxCzAJBgNVBAYTAlVTMQswCQYDVQQIDAJDQTEWMBQGA1UE
BwwNU2FuIEZyYW5jaXNjbzEUMBIGA1UECgwLTVFUVCBCcm9rZXIxFzAVBgNVBAsM
DklvVCBEZXBhcnRtZW50MRUwEwYDVQQDDAwxOTIuMTY4LjEuNDEwggEiMA0GCSqG
SIb3DQEBAQUAA4IBDwAwggEKAoIBAQCjmwQOWEMiQd1uNIgjSITQK11VCWPsi/Vn
j8MO29pZVFBIgZzTNOoA0f7dHI2RduyiKSCE3Jn5aWm1E2j+63hdjojrVJXiWAR8
mxhTdYxxZam5F4omBoiBv0CLDCbKqtTk8Ezis6zZxGLMz7gziAlqFr0A6ysWbnmW
a+uABmbfEQOCo4iNWO+icjdDl8bJSZ2GApM4FY3EDYtqkm0gHGJMrEGSP3TcjNJw
b+KwzfxR/LlFYmt5uB552TihpmXOFWrchcEB04tk0N0ysySQp79muwi6s8zrhvfE
MgoL3EohdYR4C+t+61LC4qzkSRrj4Vvrskks8jGWm0i/H9SnvRGhAgMBAAGjWjBY
MC4GA1UdEQQnMCWCDDE5Mi4xNjguMS40MYIJbG9jYWxob3N0hwR/AAABhwTAqAEp
MA4GA1UdDwEB/wQEAwIFoDAWBgNVHSUBAf8EDDAKBggrBgEFBQcDATANBgkqhkiG
9w0BAQsFAAOCAQEAVCCBJBR0uGQN5uM1hKdaw+ehHG4zzeN5cKWalx5nET++jIEY
tpSK9Bv9cKEXWLZry4HiX/dcxd5RVxAWuMa/cgx20T3Tp/ibpuX3aA0sbwhq5NUU
0M/uCI8nFy0Wgi+7+Rgy1l7kC2UO6CBTJMC2OkdqRonA/Qc0etqRYbmktgE9X2ug
sQR2Ei+qQa4XjNMJnG7OIPOEBHLyNRw2exxn3MHxNuWkw2NCe8yKapH8Kkti779S
3yanBrXQb67lOv6D1Mi2CIcHsU0FV8gEXXiEr40hAYcTd/H+7tRTupkVq3bXD57t
iQOsBYdCWzbZqVs93SsYpRvjtEz2PRV+YbxmAw==
-----END CERTIFICATE-----
)EOF";


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