/**
 * @file main.cpp
 * @brief ESP32 ChickenCoop Multi-threaded Monitor - Main Implementation
 * 
 * Three-task monitoring system:
 * - Time Sync Task (Core 0): NTP synchronization and time publishing
 * - I/O Monitor Task (Core 1): Digital I/O monitoring and control
 * - Voltage Monitor Task (Core 0): Battery and solar voltage monitoring
 * 
 * MQTT Connection: SSL/TLS with configurable broker and client certificates
 */

#include "main.h"


// =============================================================================
// CONFIGURATION CONSTANTS - Update these for your setup
// =============================================================================


// Broker and Client Selection
// Choose from broker_config.h and client.h
const char* selected_broker = "data-dancer.com";      // Options: "data-dancer.com", "broker.emqx.io", "192.168.1.41"
const char* selected_client = "data-dancer-client";   // Options: "data-dancer-client", "emqx-client", "local-client", "no-client-cert"

// MQTT Client ID
const char* mqtt_client_id = "ESP32_ChickenCoop_x0x01";  // Unique client ID

// MQTT Topics
const char* topic_time = "chickencoop/time";
const char* topic_io_state = "chickencoop/io/state";
const char* topic_io_control = "chickencoop/io/control";
const char* topic_voltage = "chickencoop/voltages";

// NTP Server settings
const char* ntp_server = "pool.ntp.org";
const long gmt_offset_sec = -8 * 3600;  // Adjust for your timezone
const int daylight_offset_sec = 0;

// Digital I/O pin definitions
const int INPUT_PINS[NUM_DIGITAL_INPUTS] = {UPLIM_SW, DNLIM_SW, UPS_SW, DNS_SW, UPI_SW, DNI_SW};
const int OUTPUT_PINS[NUM_DIGITAL_OUTPUTS] = {A, B, C, D, WIFI_LED};

const char* INPUT_NAMES[NUM_DIGITAL_INPUTS] = {
    "UPLIM",  // Upper limit switch
    "DNLIM",  // Down limit switch
    "UPS",    // Upper position sensor
    "DNS",    // Down position sensor
    "UPI",    // Upper input
    "DNI"     // Down input
};

const char* OUTPUT_NAMES[NUM_DIGITAL_OUTPUTS] = {
    "A",
    "B",
    "C",
    "D",
    "WIFI_LED"
};

// Voltage monitoring pins (ADC1 channels to avoid WiFi conflicts)
const int BATTERY_VOLTAGE_PIN = ADC1_CH6;  // ADC1_CH6 (VP)
const int SOLAR_VOLTAGE_PIN = ADC1_CH7;    // ADC1_CH7 (VN)

// Voltage divider ratios (adjust based on your resistor values)
// For 0-15V battery: Use voltage divider to bring to 0-3.3V (e.g., 47K + 10K)
// For 0-30V solar: Use voltage divider to bring to 0-3.3V (e.g., 100K + 11K)
const float BATTERY_VOLTAGE_RATIO = 5.45;  // (R1+R2)/R2 for battery divider
const float SOLAR_VOLTAGE_RATIO = 10.09;   // (R1+R2)/R2 for solar divider

// ADC settings
const float ADC_REFERENCE_VOLTAGE = 3.3;
const int ADC_RESOLUTION = 4095;  // 12-bit ADC
const int ADC_SAMPLES = 10;       // Number of samples to average

// Debounce settings
const unsigned long DEBOUNCE_DELAY = 50;

// Wifi reconnect variables
unsigned long last_wifi_check = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000;  // Check every 30 seconds

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

// State tracking
uint8_t input_states[NUM_DIGITAL_INPUTS] = {0};
uint8_t output_states[NUM_DIGITAL_OUTPUTS] = {0};
uint8_t last_input_states[NUM_DIGITAL_INPUTS] = {0};
uint8_t last_output_states[NUM_DIGITAL_OUTPUTS] = {0};

// Voltage tracking
float battery_voltage = 0.0;
float solar_voltage = 0.0;

// Debounce state
unsigned long last_debounce_time[NUM_DIGITAL_INPUTS] = {0};
uint8_t last_reading[NUM_DIGITAL_INPUTS] = {0};

// Time tracking
time_t current_time = 0;
time_t last_published_time = 0;

// FreeRTOS handles
TaskHandle_t time_task_handle = NULL;
TaskHandle_t io_task_handle = NULL;
TaskHandle_t voltage_task_handle = NULL;
SemaphoreHandle_t mqtt_mutex = NULL;
SemaphoreHandle_t io_state_mutex = NULL;
SemaphoreHandle_t voltage_mutex = NULL;

WiFiClientSecure espClient;
PubSubClient mqtt_client(espClient);

// =============================================================================
// SETUP FUNCTION
// =============================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nESP32 Multi-threaded ChickenCoop Monitor Starting...");
  
  // List available configurations
  listBrokers();
  listClients();
  
  // Get selected broker and client configurations from secrets.h
  const BrokerConfig* broker = getBrokerConfig(SELECTED_BROKER);
  const ClientConfig* client = getClientConfig(SELECTED_CLIENT);
  
  if (broker == nullptr) {
    Serial.printf("❌ ERROR: Broker '%s' not found!\n", SELECTED_BROKER);
    Serial.println("Available brokers:");
    listBrokers();
    while(1) delay(1000);
  }
  
  if (client == nullptr) {
    Serial.printf("❌ ERROR: Client '%s' not found!\n", SELECTED_CLIENT);
    Serial.println("Available clients:");
    listClients();
    while(1) delay(1000);
  }
  
  // Validate client config
  if (!validateClientConfig(client)) {
    Serial.println("❌ ERROR: Invalid client configuration!");
    while(1) delay(1000);
  }
  
  // Print selected configurations
  Serial.println("\n🔧 Selected Configuration:");
  Serial.println("==========================");
  printBrokerInfo(broker);
  printClientInfo(client);
  
  // Create mutexes
  mqtt_mutex = xSemaphoreCreateMutex();
  io_state_mutex = xSemaphoreCreateMutex();
  voltage_mutex = xSemaphoreCreateMutex();
  
  if (mqtt_mutex == NULL || io_state_mutex == NULL || voltage_mutex == NULL) {
    Serial.println("Failed to create mutexes!");
    while(1) delay(1000);
  }
  
  setup_pins();
  setup_wifi();  // Now tries multiple networks
  setup_adc();
  setup_ntp();
  setup_ssl(broker, client);
  
  mqtt_client.setServer(broker->url, broker->port_ssl);
  mqtt_client.setCallback(callback);
  
  // Initial state read
  read_inputs();
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    last_input_states[i] = input_states[i];
  }
  for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
    last_output_states[i] = output_states[i];
  }
  
  // Initial voltage read
  read_voltages();
  
  // Create tasks on different cores
  xTaskCreatePinnedToCore(
    time_sync_task,      // Task function
    "TimeSync",          // Task name
    4096,                // Stack size
    NULL,                // Parameters
    1,                   // Priority
    &time_task_handle,   // Task handle
    0                    // Core 0
  );
  
  xTaskCreatePinnedToCore(
    io_monitor_task,     // Task function
    "IOMonitor",         // Task name
    4096,                // Stack size
    NULL,                // Parameters
    1,                   // Priority
    &io_task_handle,     // Task handle
    1                    // Core 1
  );
  
  xTaskCreatePinnedToCore(
    voltage_monitor_task, // Task function
    "VoltageMonitor",     // Task name
    4096,                 // Stack size
    NULL,                 // Parameters
    1,                    // Priority
    &voltage_task_handle, // Task handle
    0                     // Core 0
  );
  
  Serial.println("All tasks created successfully");
  Serial.printf("Time Sync Task on Core 0\n");
  Serial.printf("I/O Monitor Task on Core 1\n");
  Serial.printf("Voltage Monitor Task on Core 0\n");
}

// Wifi monitoring and reconnection
void check_wifi_connection() {
  if (millis() - last_wifi_check < WIFI_CHECK_INTERVAL) {
    return;  // Not time to check yet
  }
  
  last_wifi_check = millis();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠️  WiFi connection lost! Attempting to reconnect...");
    digitalWrite(WIFI_LED, LOW);  // Indicate disconnection
    setup_wifi();  // Try to reconnect to any available network
  }
}

// =============================================================================
// MAIN LOOP
// =============================================================================

void loop() {
  
  // Make sure we're connected to WiFi
  check_wifi_connection();

  // Main loop handles MQTT connection
  if (!mqtt_client.connected()) {
    reconnect_mqtt();
  }
  
  if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
    mqtt_client.loop();
    xSemaphoreGive(mqtt_mutex);
  }
  
  delay(10);
}

// =============================================================================
// SETUP FUNCTIONS IMPLEMENTATION
// =============================================================================

void setup_wifi() {
  delay(10);
  Serial.println("\n====================================");
  Serial.println("WiFi Connection Manager");
  Serial.println("====================================");
  
  // List available networks
  Serial.printf("\n📶 Configured WiFi Networks (%d):\n", NUM_WIFI_NETWORKS);
  for (int i = 0; i < NUM_WIFI_NETWORKS; i++) {
    Serial.printf("   %d. %s", i + 1, WIFI_NETWORKS[i].ssid);
    if (WIFI_NETWORKS[i].description != nullptr && strlen(WIFI_NETWORKS[i].description) > 0) {
      Serial.printf(" - %s", WIFI_NETWORKS[i].description);
    }
    Serial.println();
  }
  Serial.println();
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  // Try each network in order
  for (int i = 0; i < NUM_WIFI_NETWORKS; i++) {
    const char* ssid = WIFI_NETWORKS[i].ssid;
    const char* password = WIFI_NETWORKS[i].password;
    
    Serial.printf("🔄 Attempting connection %d/%d: %s\n", i + 1, NUM_WIFI_NETWORKS, ssid);
    
    WiFi.begin(ssid, password);
    digitalWrite(WIFI_LED, LOW);  // Indicate attempting connection
    
    // Try to connect for 10 seconds
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi connected!");
      Serial.printf("   SSID: %s\n", ssid);
      Serial.printf("   IP Address: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("   Signal Strength: %d dBm\n", WiFi.RSSI());
      Serial.printf("   MAC Address: %s\n", WiFi.macAddress().c_str());
      Serial.println("====================================\n");
      digitalWrite(WIFI_LED, HIGH);  // Indicate successful connection
      return;  // Success! Exit function
    } else {
      Serial.println(" ❌ Failed");
      
      // Print reason for failure
      switch (WiFi.status()) {
        case WL_NO_SSID_AVAIL:
          Serial.println("   Reason: Network not found");
          break;
        case WL_CONNECT_FAILED:
          Serial.println("   Reason: Connection failed (wrong password?)");
          break;
        case WL_CONNECTION_LOST:
          Serial.println("   Reason: Connection lost");
          break;
        case WL_DISCONNECTED:
          Serial.println("   Reason: Disconnected");
          break;
        default:
          Serial.printf("   Reason: Unknown (status %d)\n", WiFi.status());
          break;
      }
    }
  }
  
  // If we get here, all networks failed
  Serial.println("\n❌ ERROR: Could not connect to any WiFi network!");
  Serial.println("   Please check:");
  Serial.println("   1. WiFi credentials in secrets.h");
  Serial.println("   2. WiFi network is in range");
  Serial.println("   3. WiFi network is operational");
  Serial.println("\n⏸️  System halted. Reboot to retry.");
  
  while (1) {
    delay(1000);  // Halt system
  }
}

void setup_pins() {
  // Configure input pins
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    pinMode(INPUT_PINS[i], INPUT_PULLUP);
    last_reading[i] = digitalRead(INPUT_PINS[i]);
  }
  
  // Configure output pins
  for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
    pinMode(OUTPUT_PINS[i], OUTPUT);
    digitalWrite(OUTPUT_PINS[i], LOW);
    output_states[i] = LOW;
  }
  
  Serial.println("Digital I/O pins configured");
}

void setup_adc() {
  // Configure ADC for voltage monitoring
  pinMode(BATTERY_VOLTAGE_PIN, INPUT);
  pinMode(SOLAR_VOLTAGE_PIN, INPUT);
  
  // Set ADC attenuation to measure up to ~3.3V
  // ADC_11db allows measurement up to ~3.3V (actually ~3.9V with non-linearity)
  analogSetAttenuation(ADC_11db);
  
  // Set ADC resolution to 12 bits (0-4095)
  analogReadResolution(12);
  
  Serial.println("ADC configured for voltage monitoring");
  Serial.printf("Battery voltage pin: GPIO%d\n", BATTERY_VOLTAGE_PIN);
  Serial.printf("Solar voltage pin: GPIO%d\n", SOLAR_VOLTAGE_PIN);
}

void setup_ntp() {
  Serial.println("Configuring NTP time...");
  configTime(gmt_offset_sec, daylight_offset_sec, ntp_server);
  
  // Wait for time to be set
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) {
    Serial.println("Waiting for NTP time sync...");
    delay(1000);
    retry++;
  }
  
  if (retry < 10) {
    Serial.println("Time synchronized with NTP");
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  } else {
    Serial.println("Failed to sync with NTP server");
  }
}

void setup_ssl(const BrokerConfig* broker, const ClientConfig* client) {
  Serial.println("\n🔐 Configuring SSL/TLS...");
  Serial.println("========================");
  
  // Set CA certificate to verify the broker (use client's CA if available, else broker's cert)
  if (client->ca_cert != nullptr) {
    espClient.setCACert(client->ca_cert);
    Serial.println("✅ CA Certificate set from client config");
  } else if (broker->server_cert != nullptr) {
    espClient.setCACert(broker->server_cert);
    Serial.println("✅ CA Certificate set from broker config");
  } else {
    Serial.println("⚠️  WARNING: No CA certificate - connection will be insecure!");
  }
  
  // Set client certificate and key for mutual authentication (if provided)
  if (client->cert != nullptr && client->key != nullptr) {
    espClient.setCertificate(client->cert);
    espClient.setPrivateKey(client->key);
    Serial.println("✅ Client certificate and key configured");
    Serial.println("🔑 Mutual TLS (mTLS) enabled");
  } else {
    Serial.println("ℹ️  No client certificate - server verification only");
  }
  
  Serial.printf("\n📡 Connecting to: %s:%d (SSL)\n", broker->url, broker->port_ssl);
  Serial.println();
}

// =============================================================================
// MQTT FUNCTIONS IMPLEMENTATION
// =============================================================================

void reconnect_mqtt() {
  // Get broker configuration
  const BrokerConfig* broker = getBrokerConfig(SELECTED_BROKER);
  
  if (broker == nullptr) {
    Serial.println("❌ ERROR: Broker configuration not found!");
    return;
  }
  
  while (!mqtt_client.connected()) {
    Serial.print("Attempting MQTT connection to ");
    Serial.print(broker->name);
    Serial.print("...");
    
    if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
      bool connected = false;
      
      // Connect with or without username/password
      if (strlen(broker->username) > 0) {
        connected = mqtt_client.connect(mqtt_client_id, broker->username, broker->password);
      } else {
        connected = mqtt_client.connect(mqtt_client_id);
      }
      
      if (connected) {
        Serial.println("connected");
        mqtt_client.subscribe(topic_io_control);
        
        // Publish initial states
        xSemaphoreGive(mqtt_mutex);
        publish_io_state();
        publish_time();
        publish_voltage();
      } else {
        Serial.print("failed, rc=");
        Serial.print(mqtt_client.state());
        Serial.println(" retrying in 5 seconds");
        xSemaphoreGive(mqtt_mutex);
        delay(5000);
      }
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  Serial.println(message);
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  if (strcmp(topic, topic_io_control) == 0) {
    if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
      if (doc.containsKey("outputs")) {
        JsonArray outputs = doc["outputs"];
        
        for (int i = 0; i < NUM_DIGITAL_OUTPUTS && i < outputs.size(); i++) {
          if (!outputs[i].isNull()) {
            uint8_t value = outputs[i].as<uint8_t>();
            digitalWrite(OUTPUT_PINS[i], value);
            output_states[i] = value;
            Serial.printf("Output %d set to %d\n", i, value);
          }
        }
      }
      
      if (doc.containsKey("output")) {
        int output_num = doc["output"];
        uint8_t value = doc["value"];
        
        if (output_num >= 0 && output_num < NUM_DIGITAL_OUTPUTS) {
          digitalWrite(OUTPUT_PINS[output_num], value);
          output_states[output_num] = value;
          Serial.printf("Output %d set to %d\n", output_num, value);
        }
      }
      
      xSemaphoreGive(io_state_mutex);
      publish_io_state();
    }
  }
}

// =============================================================================
// FREERTOS TASK IMPLEMENTATIONS
// =============================================================================

void time_sync_task(void* parameter) {
  Serial.println("Time Sync Task started on core " + String(xPortGetCoreID()));
  
  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t frequency = pdMS_TO_TICKS(1000); // 1 second
  
  while (1) {
    vTaskDelayUntil(&last_wake_time, frequency);
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      time(&current_time);
      
      // Publish time every minute
      if (difftime(current_time, last_published_time) >= 60) {
        publish_time();
        last_published_time = current_time;
      }
    }
  }
}

void io_monitor_task(void* parameter) {
  Serial.println("I/O Monitor Task started on core " + String(xPortGetCoreID()));
  
  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t frequency = pdMS_TO_TICKS(10); // 10ms
  
  while (1) {
    vTaskDelayUntil(&last_wake_time, frequency);
    
    if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
      read_inputs();
    //   for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    //     Serial.printf("Input %d state: %d\n", i, input_states[i]);  
    //   }
      
      if (has_state_changed()) {
        // Update last states before releasing mutex
        for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
          last_input_states[i] = input_states[i];
        }
        for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
          last_output_states[i] = output_states[i];
        }
        
        xSemaphoreGive(io_state_mutex);
        publish_io_state();
      } else {
        xSemaphoreGive(io_state_mutex);
      }
    }
  }
}

void voltage_monitor_task(void* parameter) {
  Serial.println("Voltage Monitor Task started on core " + String(xPortGetCoreID()));
  
  TickType_t last_wake_time = xTaskGetTickCount();
//   const TickType_t frequency = pdMS_TO_TICKS(60000); // 60 seconds
  const TickType_t frequency = pdMS_TO_TICKS(500); // half second
  
  while (1) {
    vTaskDelayUntil(&last_wake_time, frequency);
    
    if (xSemaphoreTake(voltage_mutex, portMAX_DELAY) == pdTRUE) {
      read_voltages();
      xSemaphoreGive(voltage_mutex);
    }
    
    publish_voltage();
  }
}

// =============================================================================
// INPUT/OUTPUT FUNCTIONS IMPLEMENTATION
// =============================================================================

void read_inputs() {
  unsigned long current_time = millis();
  
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    uint8_t reading = digitalRead(INPUT_PINS[i]);
    
    if (reading != last_reading[i]) {
      last_debounce_time[i] = current_time;
    }
    
    if ((current_time - last_debounce_time[i]) > DEBOUNCE_DELAY) {
      if (reading != input_states[i]) {
        input_states[i] = reading;
      }
    }
    
    last_reading[i] = reading;
  }
}

float read_adc_voltage(int pin, int samples) {
  long sum = 0;
  
  // Take multiple samples and average
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(100); // Small delay between samples
  }
  
  float avg_reading = (float)sum / samples;
  
  // Convert ADC reading to voltage
  float voltage = (avg_reading / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;
  
  return voltage;
}

void read_voltages() {
  // Read battery voltage
  float battery_adc_voltage = read_adc_voltage(BATTERY_VOLTAGE_PIN, ADC_SAMPLES);
  battery_voltage = battery_adc_voltage * BATTERY_VOLTAGE_RATIO;
  
  // Read solar voltage
  float solar_adc_voltage = read_adc_voltage(SOLAR_VOLTAGE_PIN, ADC_SAMPLES);
  solar_voltage = solar_adc_voltage * SOLAR_VOLTAGE_RATIO;
  
  // Constrain to reasonable ranges (sanity check)
  battery_voltage = constrain(battery_voltage, 0.0, 20.0);
  solar_voltage = constrain(solar_voltage, 0.0, 35.0);
  
  Serial.printf("Battery: %.2fV (ADC: %.3fV), Solar: %.2fV (ADC: %.3fV)\n", 
                battery_voltage, battery_adc_voltage, solar_voltage, solar_adc_voltage);
}

bool has_state_changed() {
  for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    if (input_states[i] != last_input_states[i]) {
      return true;
    }
  }
  
  for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
    if (output_states[i] != last_output_states[i]) {
      return true;
    }
  }
  
  return false;
}

// =============================================================================
// MQTT PUBLISHING FUNCTIONS IMPLEMENTATION
// =============================================================================

void publish_io_state() {
  StaticJsonDocument<512> doc;
  
  if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
    // JsonArray inputs = doc.createNestedArray("inputs");
    // for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    //   inputs.add(input_states[i]);
    // }
    
    // JsonArray outputs = doc.createNestedArray("outputs");
    // for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
    //   outputs.add(output_states[i]);
    // }
    
    // JsonArray input_pins = doc.createNestedArray("input_pins");
    // for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
    //   input_pins.add(INPUT_PINS[i]);
    // }
    
    // JsonArray output_pins = doc.createNestedArray("output_pins");
    // for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
    //   output_pins.add(OUTPUT_PINS[i]);
    // }

    // Add inputs directly to root object
    for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
      doc[INPUT_NAMES[i]] = (bool)input_states[i];
    }
    
    // Add outputs directly to root object
    for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
      doc[OUTPUT_NAMES[i]] = (bool)output_states[i];
    }
    
    xSemaphoreGive(io_state_mutex);
  }
  
  doc["timestamp"] = millis();
  time(&current_time);
  doc["unix_time"] = current_time;
  
  char buffer[512];
  serializeJson(doc, buffer);
  
  if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
    if (mqtt_client.publish(topic_io_state, buffer, true)) {
      Serial.println("I/O State published");
      Serial.println(buffer);
    } else {
      Serial.println("Failed to publish I/O state");
    }
    xSemaphoreGive(mqtt_mutex);
  }
}

void publish_time() {
  StaticJsonDocument<256> doc;
  
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char time_string[64];
    strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    doc["time"] = time_string;
    doc["unix_time"] = current_time;
    doc["year"] = timeinfo.tm_year + 1900;
    doc["month"] = timeinfo.tm_mon + 1;
    doc["day"] = timeinfo.tm_mday;
    doc["hour"] = timeinfo.tm_hour;
    doc["minute"] = timeinfo.tm_min;
    doc["second"] = timeinfo.tm_sec;
    
    char buffer[256];
    serializeJson(doc, buffer);
    
    if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
      if (mqtt_client.publish(topic_time, buffer, true)) {
        Serial.println("Time published");
      } else {
        Serial.println("Failed to publish time");
      }
      xSemaphoreGive(mqtt_mutex);
    }
  }
}

void publish_voltage() {
  StaticJsonDocument<256> doc;
  
  if (xSemaphoreTake(voltage_mutex, portMAX_DELAY) == pdTRUE) {
    doc["batteryVoltage"] = serialized(String(battery_voltage, 2));
    doc["solarVoltage"] = serialized(String(solar_voltage, 2));
    xSemaphoreGive(voltage_mutex);
  }
  
  time(&current_time);
  doc["unix_time"] = current_time;
  doc["timestamp"] = millis();
  
  // Add battery status indicators
  if (xSemaphoreTake(voltage_mutex, portMAX_DELAY) == pdTRUE) {
    float batt_v = battery_voltage;
    float solar_v = solar_voltage;
    xSemaphoreGive(voltage_mutex);
    
    // Battery status (typical for 12V lead-acid)
    if (batt_v > 12.6) {
      doc["battery_status"] = "full";
    } else if (batt_v > 12.0) {
      doc["battery_status"] = "good";
    } else if (batt_v > 11.5) {
      doc["battery_status"] = "low";
    } else {
      doc["battery_status"] = "critical";
    }
    
    // Solar status
    doc["solar_charging"] = (solar_v > batt_v + 0.5);
  }
  
  char buffer[256];
  serializeJson(doc, buffer);
  
  if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
    if (mqtt_client.publish(topic_voltage, buffer, true)) {
      Serial.println("Voltage published:");
      Serial.println(buffer);
    } else {
      Serial.println("Failed to publish voltage");
    }
    xSemaphoreGive(mqtt_mutex);
  }
}