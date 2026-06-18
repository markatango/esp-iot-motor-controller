/**
 * @file mqtt_manager.cpp
 * @brief MQTT Client Manager Implementation
 */

#include "mqtt_manager.h"
#include "voltage_monitor.h"
#include "time_sync.h"
#include "io_monitor.h"
#include "cron_scheduler.h"
#include "state_machine.h"



// ====================================================================
// CONFIGURATION
// ====================================================================

// MQTT Topics
const char* TOPIC_TIME = "chickencoop/time";
const char* TOPIC_IO_STATE = "chickencoop/io/state";
const char* TOPIC_IO_CONTROL = "chickencoop/io/command";
const char* TOPIC_VOLTAGE = "chickencoop/voltages";
const char* TOPIC_STATUS_REQUEST = "chickencoop/status/request";
const char* TOPIC_SCHEDULE = "chickencoop/schedule";

// ====================================================================
// GLOBAL VARIABLES
// ====================================================================

WiFiClientSecure esp_client;
PubSubClient mqtt_client(esp_client);
SemaphoreHandle_t mqtt_mutex = NULL;
volatile bool status_request_pending = false;


// ====================================================================
// FUNCTIONS
// ====================================================================

void mqtt_init() {
  Serial.println("\n📡 Initializing MQTT manager...");
  
  // Create mutex
  mqtt_mutex = xSemaphoreCreateMutex();
  if (mqtt_mutex == NULL) {
    Serial.println("❌ Failed to create MQTT mutex!");
    while(1) delay(1000);
  }
  
  // Increase packet buffer beyond the 256-byte default to accommodate full I/O state payload
  mqtt_client.setBufferSize(1024);

  // Set callback
  mqtt_client.setCallback(mqtt_callback);

  Serial.println("✅ MQTT manager initialized");
}

void mqtt_setup_ssl(const BrokerConfig* broker, const ClientConfig* client) {
  Serial.println("\n🔐 Configuring SSL/TLS...");
  Serial.println("========================");
  
  if (broker == nullptr || client == nullptr) {
    Serial.println("❌ ERROR: Invalid broker or client configuration!");
    return;
  }
  
  // Set CA certificate to verify the broker
  if (client->ca_cert != nullptr) {
    esp_client.setCACert(client->ca_cert);
    Serial.println("✅ CA Certificate set from client config");
  } else if (broker->server_cert != nullptr) {
    esp_client.setCACert(broker->server_cert);
    Serial.println("✅ CA Certificate set from broker config");
  } else {
    Serial.println("⚠️  WARNING: No CA certificate - connection will be insecure!");
  }
  
  // Set client certificate and key for mutual authentication
  if (client->cert != nullptr && client->key != nullptr) {
    esp_client.setCertificate(client->cert);
    esp_client.setPrivateKey(client->key);
    Serial.println("✅ Client certificate and key configured");
    Serial.println("🔑 Mutual TLS (mTLS) enabled");
  } else {
    Serial.println("ℹ️  No client certificate - server verification only");
  }
  
  Serial.printf("\n📡 Connecting to: %s:%d (SSL)\n", broker->url, broker->port_ssl);
  Serial.println();
}

bool mqtt_connect(const char* broker_name, const char* client_name, const char* client_id) {
  const BrokerConfig* broker = getBrokerConfig(broker_name);
  const ClientConfig* client = getClientConfig(client_name);
  
  if (broker == nullptr) {
    Serial.printf("❌ ERROR: Broker '%s' not found!\n", broker_name);
    return false;
  }
  
  if (client == nullptr) {
    Serial.printf("❌ ERROR: Client '%s' not found!\n", client_name);
    return false;
  }
  
  // Setup SSL/TLS
  mqtt_setup_ssl(broker, client);
  
  // Configure MQTT server
  mqtt_client.setServer(broker->url, broker->port_ssl);
  
  Serial.print("🔄 Attempting MQTT connection to ");
  Serial.print(broker->name);
  Serial.print("...");
  
  bool connected = false;
  
  if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
    // Connect with or without username/password
    if (strlen(broker->username) > 0) {
      connected = mqtt_client.connect(client_id, broker->username, broker->password);
    } else {
      connected = mqtt_client.connect(client_id);
    }
    
    if (connected) {
      Serial.println("connected ✅");
      
      // Subscribe to topics
      mqtt_client.subscribe(TOPIC_IO_CONTROL);
      mqtt_client.subscribe(TOPIC_STATUS_REQUEST);
      mqtt_client.subscribe(TOPIC_SCHEDULE);
      
      Serial.println("📥 Subscribed to:");
      Serial.printf("   - %s\n", TOPIC_IO_CONTROL);
      Serial.printf("   - %s\n", TOPIC_STATUS_REQUEST);
      Serial.printf("   - %s\n", TOPIC_SCHEDULE);
      
      xSemaphoreGive(mqtt_mutex);
      
      // Read and publish initial state
      Serial.println("📤 Publishing initial states...");
      
      if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
        io_read_inputs();
        xSemaphoreGive(io_state_mutex);
      }
      
      mqtt_publish_io_state();
      mqtt_publish_voltage();
      mqtt_publish_time();
      
      Serial.println("✅ Initial state published - server is synchronized");
    } else {
      Serial.print("failed ❌, rc=");
      Serial.println(mqtt_client.state());
      xSemaphoreGive(mqtt_mutex);
    }
  }
  
  return connected;
}

void mqtt_reconnect(const char* broker_name, const char* client_name, const char* client_id) {
  // Rate-limit to one attempt every 10 seconds so the main loop is never blocked
  static unsigned long last_attempt_ms = 0;
  if (millis() - last_attempt_ms < 10000) return;
  last_attempt_ms = millis();

  const BrokerConfig* broker = getBrokerConfig(broker_name);
  if (broker == nullptr) {
    Serial.println("❌ ERROR: Broker configuration not found!");
    return;
  }

  Serial.print("🔄 Attempting MQTT reconnect to ");
  Serial.print(broker->name);
  Serial.print("...");

  if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) != pdTRUE) return;

  bool connected = (strlen(broker->username) > 0)
      ? mqtt_client.connect(client_id, broker->username, broker->password)
      : mqtt_client.connect(client_id);

  if (connected) {
    Serial.println(" connected ✅");
    mqtt_client.subscribe(TOPIC_IO_CONTROL, 1);
    mqtt_client.subscribe(TOPIC_STATUS_REQUEST, 1);
    mqtt_client.subscribe(TOPIC_SCHEDULE, 1);
    xSemaphoreGive(mqtt_mutex);

    if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
      io_read_inputs();
      xSemaphoreGive(io_state_mutex);
    }
    mqtt_publish_io_state();
    mqtt_publish_voltage();
    mqtt_publish_time();
    Serial.println("✅ Reconnected and state published");
  } else {
    Serial.printf(" failed ❌ rc=%d (will retry in 10s)\n", mqtt_client.state());
    xSemaphoreGive(mqtt_mutex);
  }
}

bool mqtt_is_connected() {
  if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
    bool connected = mqtt_client.connected();
    xSemaphoreGive(mqtt_mutex);
    return connected;
  }
  return false;
}

void mqtt_loop() {
  if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
    mqtt_client.loop();
    xSemaphoreGive(mqtt_mutex);
  }
}

void mqtt_publish_io_state() {
  static CoopState prev_sm_state = SM_STATE_START;

  // Snapshot current SM state under its mutex
  CoopState cur_sm_state = prev_sm_state;
  if (xSemaphoreTake(sm_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    cur_sm_state = coop_state;
    xSemaphoreGive(sm_mutex);
  }

  StaticJsonDocument<640> doc;
  Serial.printf("\n📤 Publishing I/O state... SM: %s → %s\n",
                sm_state_name(prev_sm_state), sm_state_name(cur_sm_state));

  if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
      doc[INPUT_NAMES[i]] = (bool)input_states[i];
    }
    for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
      doc[OUTPUT_NAMES[i]] = (bool)output_states[i];
    }
    xSemaphoreGive(io_state_mutex);
  }

  doc["sm_state_prev"] = sm_state_name(prev_sm_state);
  doc["sm_state"]      = sm_state_name(cur_sm_state);
  doc["timestamp"]     = millis();
  doc["unix_time"]     = current_time;
  doc["online"]        = true;

  char buffer[640];
  serializeJson(doc, buffer);

  if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
    if (mqtt_client.publish(TOPIC_IO_STATE, buffer, true)) {
      Serial.println("📤 I/O State published");
    } else {
      Serial.println("❌ Failed to publish I/O state");
    }
    xSemaphoreGive(mqtt_mutex);
  }

  prev_sm_state = cur_sm_state;
}

void mqtt_publish_voltage() {
  StaticJsonDocument<256> doc;
  
  if (xSemaphoreTake(voltage_mutex, portMAX_DELAY) == pdTRUE) {
    doc["batteryVoltage"] = serialized(String(battery_voltage, 2));
    doc["solarVoltage"] = serialized(String(solar_voltage, 2));
    
    float batt_v = battery_voltage;
    float solar_v = solar_voltage;
    xSemaphoreGive(voltage_mutex);
    
    // Battery status
    doc["batteryStatus"] = voltage_get_battery_status(batt_v);
    doc["solarStatus"] = voltage_is_solar_charging(solar_v, batt_v);
  }
  
  doc["unix_time"] = current_time;
  doc["timestamp"] = millis();
  
  char buffer[256];
  serializeJson(doc, buffer);
  
  if (xSemaphoreTake(mqtt_mutex, portMAX_DELAY) == pdTRUE) {
    if (mqtt_client.publish(TOPIC_VOLTAGE, buffer, true)) {
      // Serial.println("📤 Voltage published");
    } else {
      Serial.println("❌ Failed to publish voltage");
    }
    xSemaphoreGive(mqtt_mutex);
  }
}

void mqtt_publish_time() {
  StaticJsonDocument<256> doc;
  
  struct tm timeinfo;
  if (time_get_local(&timeinfo)) {
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
      if (mqtt_client.publish(TOPIC_TIME, buffer, true)) {
        Serial.println("📤 Time published");
      } else {
        Serial.println("❌ Failed to publish time");
      }
      xSemaphoreGive(mqtt_mutex);
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📥 Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  Serial.println(message);
  
  // Handle status request
  if (strcmp(topic, TOPIC_STATUS_REQUEST) == 0) {
    Serial.println("📥 Status request received");
    
    // Read current I/O state
    if (xSemaphoreTake(io_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      io_read_inputs();
      xSemaphoreGive(io_state_mutex);
    }
    status_request_pending = true;

    // // Publish all states
    // mqtt_publish_io_state();
    // mqtt_publish_voltage();
    // mqtt_publish_time();
    
    Serial.println("✅ Status published in response to request");
    return;
  }

  // Handle schedule times
  if (strcmp(topic, TOPIC_SCHEDULE) == 0) {
    // StaticJsonDocument<256> doc;
    // DeserializationError error = deserializeJson(doc, message);
    applyScheduleFromMQTT(message, length);  // ← this line is missing
    return;
    // if (error) {
    //   Serial.print("❌ JSON parsing failed: ");
    //   Serial.println(error.c_str());
    //   return;
    // }
    
    // if (xSemaphoreTake(time_and_schedule_mutex, portMAX_DELAY) == pdTRUE) {
      
    //   const char * open_time_str = nullptr;
    //    if (doc.containsKey("open_time")) {
    //     open_time_str = doc["open_time"];
    //     Serial.print("Received open_time: ");
    //     Serial.println(open_time_str);
    //   }

    //   if (doc.containsKey("close_time")) {
    //     const char * close_time_str = nullptr;
    //     close_time_str = doc["close_time"];
    //     Serial.print("Received close_time: ");
    //     Serial.println(close_time_str);
    //   }
      
    //   xSemaphoreGive(time_and_schedule_mutex);
    // }
  }
  
  // Handle I/O control
  if (strcmp(topic, TOPIC_IO_CONTROL) == 0) {
    // Accept plain-text payload ("open"/"close") or JSON ({"command":"open"})
    const char* command = message;
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, message) == DeserializationError::Ok && doc.containsKey("command")) {
      command = doc["command"];
    }

    Serial.printf("📥 IO command: %s\n", command);

    if (strcmp(command, "open") == 0) {
      sm_trigger_open();
    } else if (strcmp(command, "close") == 0) {
      sm_trigger_close();
    } else {
      Serial.printf("❌ Unknown IO command: %s\n", command);
    }
    return;
  }

}

void mqtt_set_callback(void (*callback)(char*, byte*, unsigned int)) {
  mqtt_client.setCallback(callback);
}
