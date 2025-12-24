/**
 * @file main.cpp
 * @brief ESP32 ChickenCoop Monitor - Modular Architecture
 * 
 * Main orchestration file that initializes and coordinates all modules:
 * - voltage_monitor: Battery and solar voltage monitoring
 * - time_sync: NTP time synchronization
 * - io_monitor: Digital I/O monitoring and control
 * - mqtt_manager: MQTT communication and publishing
 */

#include <Arduino.h>
#include <WiFi.h>

#include "voltage_monitor.h"
#include "time_sync.h"
#include "io_monitor.h"
#include "state_machine.h"
#include "mqtt_manager.h"
#include "broker_config.h"
#include "client.h"
#include "secrets.h"
// #include "io.h"

// ====================================================================
// WIFI MANAGEMENT
// ====================================================================

void setup_wifi() {
  delay(10);
  Serial.println("\n====================================");
  Serial.println("WiFi Connection Manager");
  Serial.println("====================================");
  
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.disconnect();
  delay(100);
  
  Serial.printf("\n📶 Trying %d configured networks...\n\n", NUM_WIFI_NETWORKS);
  
  // Try each network in order
  for (int i = 0; i < NUM_WIFI_NETWORKS; i++) {
    const char* ssid = WIFI_NETWORKS[i].ssid;
    const char* password = WIFI_NETWORKS[i].password;
    
    Serial.printf("🔄 Attempt %d/%d: %s", i + 1, NUM_WIFI_NETWORKS, ssid);
    if (WIFI_NETWORKS[i].description != nullptr && strlen(WIFI_NETWORKS[i].description) > 0) {
      Serial.printf(" (%s)", WIFI_NETWORKS[i].description);
    }
    Serial.println();
    
    WiFi.begin(ssid, password);
    
    // Wait up to 10 seconds
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(" ✅");
      Serial.println("\n✅ WiFi Connected!");
      Serial.printf("   SSID: %s\n", WiFi.SSID().c_str());
      Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("   Signal: %d dBm\n", WiFi.RSSI());
      Serial.println("====================================\n");
      
      int wifi_led = io_set_output_by_name("WIFI_LED", HIGH);  // Turn on WiFi LED

      Serial.printf("✅ WiFi LED (GPIO %d) turned on", OUTPUT_PINS[wifi_led]);
      return;
    } else {
      Serial.println(" ❌");
      WiFi.disconnect();
      delay(100);
    }
  }
  
  // All failed
  Serial.println("\n❌ Could not connect to any network!");
  while(1) delay(1000);
}

void check_wifi_connection() {
  static unsigned long last_check = 0;
  const unsigned long CHECK_INTERVAL = 30000;  // 30 seconds
  
  if (millis() - last_check < CHECK_INTERVAL) {
    return;
  }
  
  last_check = millis();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠️  WiFi connection lost! Attempting to reconnect...");
    setup_wifi();
  }
}

// ====================================================================
// SETUP
// ====================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n");
  Serial.println("╔════════════════════════════════════════════════╗");
  Serial.println("║   ESP32 ChickenCoop Monitor - Modular Design  ║");
  Serial.println("╚════════════════════════════════════════════════╝");
  Serial.println();
  
  // List available configurations
  listBrokers();
  listClients();
  
  // Get selected broker and client from secrets.h
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
  
  if (!validateClientConfig(client)) {
    Serial.println("❌ ERROR: Invalid client configuration!");
    while(1) delay(1000);
  }
  
  // Print selected configurations
  Serial.println("\n🔧 Selected Configuration:");
  Serial.println("==========================");
  printBrokerInfo(broker);
  printClientInfo(client);
  
  // Initialize WiFi
  io_init(); // Need to initialize I/O before WiFi
  setup_wifi();
  
  // Initialize modules
  voltage_init();
  time_sync_init();
  mqtt_init();
  sm_init();
  
  // Connect to MQTT broker
  if (!mqtt_connect(SELECTED_BROKER, SELECTED_CLIENT, MQTT_CLIENT_ID)) {
    Serial.println("⚠️  Initial MQTT connection failed, will retry in loop");
  }
  
  // Create FreeRTOS tasks
  Serial.println("\n🔧 Creating FreeRTOS Tasks:");
  Serial.println("============================");
  
  xTaskCreatePinnedToCore(
    time_sync_task,
    "TimeSync",
    4096,
    NULL,
    1,
    NULL,
    0  // Core 0
  );
  Serial.println("✅ Time Sync Task created (Core 0)");
  
  xTaskCreatePinnedToCore(
    io_monitor_task,
    "IOMonitor",
    4096,
    NULL,
    2,
    NULL,
    1  // Core 1
  );
  Serial.println("✅ I/O Monitor Task created (Core 1)");
  
  xTaskCreatePinnedToCore(
    voltage_monitor_task,
    "VoltageMonitor",
    4096,
    NULL,
    1,
    NULL,
    0  // Core 0
  );
  Serial.println("✅ Voltage Monitor Task created (Core 0)");


  xTaskCreatePinnedToCore(
    state_machine_task,
    "StateMachine",
    4096,
    NULL,
    2,
    NULL,
    1  // Core 1
  );
  Serial.println("✅ State Machine Task created (Core 1)");
  
  Serial.println("\n✅ Setup complete! System running...\n");
  Serial.println("════════════════════════════════════════════════\n");
}

// ====================================================================
// LOOP
// ====================================================================

void loop() {
  // Check WiFi connection
  check_wifi_connection();
  
  // Check MQTT connection and reconnect if needed
  if (!mqtt_is_connected()) {
    Serial.println("⚠️  MQTT disconnected, reconnecting...");
    mqtt_reconnect(SELECTED_BROKER, SELECTED_CLIENT, MQTT_CLIENT_ID);
  }
  
  // Process MQTT messages
  mqtt_loop();
  
  // Publish I/O state when it changes
  static unsigned long last_io_check = 0;
  const unsigned long IO_CHECK_INTERVAL = 100;  // Check every 100ms
  
  if (millis() - last_io_check > IO_CHECK_INTERVAL) {
    last_io_check = millis();
    
    if (io_publish_needed) {

        io_publish_needed = false;
        mqtt_publish_io_state();
        Serial.println("📤 I/O state changed - published");
    } 
  }
  
  // Publish voltage readings every 60 seconds
  static unsigned long last_voltage_publish = 0;
  const unsigned long VOLTAGE_PUBLISH_INTERVAL = 60000;  // 60 seconds
  
  if (millis() - last_voltage_publish > VOLTAGE_PUBLISH_INTERVAL) {
    last_voltage_publish = millis();
    mqtt_publish_voltage();
    Serial.println("📤 Voltage published (periodic)");
  }
  
  // Publish time every 60 seconds (optional)
  static unsigned long last_time_publish = 0;
  const unsigned long TIME_PUBLISH_INTERVAL = 60000;  // 60 seconds
  
  if (millis() - last_time_publish > TIME_PUBLISH_INTERVAL) {
    last_time_publish = millis();
    mqtt_publish_time();
    Serial.println("📤 Time published (periodic)");
  }
  
  delay(10);
}
