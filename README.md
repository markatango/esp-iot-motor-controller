# ESP32 Chicken Coop Monitor

A modular, multi-threaded ESP32 firmware for automated chicken coop monitoring and control. The system monitors digital I/O (limit switches, position sensors), battery and solar voltages, synchronizes time via NTP, and communicates with a backend server over MQTT with SSL/TLS mutual authentication.

---

## Table of Contents

- [Features](#features)
- [Hardware](#hardware)
- [Software Architecture](#software-architecture)
- [Modules](#modules)
- [MQTT Interface](#mqtt-interface)
- [FreeRTOS Task Layout](#freertos-task-layout)
- [Getting Started](#getting-started)
- [Configuration](#configuration)
- [Security](#security)
- [Project Status](#project-status)

---

## Features

- **Modular C++ architecture** — each concern is a self-contained module with its own header and implementation file
- **Multi-threaded** via FreeRTOS — tasks pinned to specific cores with mutex-protected shared state
- **SSL/TLS mutual authentication** (mTLS) for MQTT — configurable broker and client certificate sets
- **Multi-network WiFi** — priority-ordered list of networks with automatic fallback
- **Digital I/O monitoring** — 6 debounced inputs and 5 outputs; state published to MQTT on change
- **Battery and solar voltage monitoring** — ADC readings with configurable voltage divider ratios
- **NTP time synchronization** — periodic updates, timezone-aware
- **Cron scheduler** — FreeRTOS timer-based scheduler for timed door open/close events, configured via MQTT
- **On-demand status** — server can request a full state snapshot at any time
- **State machine** *(placeholder, in development)* — framework for automated door control sequences

---

## Hardware

### ESP32 Pin Assignments

#### Digital Inputs (INPUT_PULLUP)

| Name | GPIO | Description |
|------|------|-------------|
| UPLIM | 14 | Upper limit switch |
| DNLIM | 27 | Lower limit switch |
| UPS | 13 | Upper position sensor |
| DNS | 12 | Down position sensor |
| UPI | 22 | Upper input |
| DNI | 23 | Down input |

#### Digital Outputs

| Name | GPIO | Description |
|------|------|-------------|
| A | 18 | Output A |
| B | 19 | Output B |
| C | 25 | Output C |
| D | 33 | Output D |
| WIFI_LED | 2 | WiFi connected indicator (onboard LED) |

#### Analog Inputs (ADC)

| Name | GPIO | Description | Voltage Divider Ratio |
|------|------|-------------|----------------------|
| Battery | 34 | Battery voltage | 5.7× (47kΩ + 10kΩ) |
| Solar | 35 | Solar panel voltage | 10.09× (100kΩ + 11kΩ) |

> **Note:** GPIO34 and GPIO35 are input-only pins on the ESP32 and are not suitable for use as outputs.

---

## Software Architecture

The firmware is organized as a set of independent modules. Each module owns its data, its mutex, and its FreeRTOS task. Modules communicate through well-defined, thread-safe accessor functions rather than direct access to each other's globals.

```
┌─────────────────────────────────────────────────────────┐
│                       main.cpp                          │
│   WiFi management, setup orchestration, main loop       │
└──────┬──────────┬──────────┬────────────┬──────────────┘
       │          │          │            │
┌──────▼──┐ ┌────▼────┐ ┌───▼────┐ ┌────▼──────────┐
│  io_    │ │voltage_ │ │ time_  │ │    mqtt_      │
│ monitor │ │ monitor │ │  sync  │ │   manager     │
└──────┬──┘ └────┬────┘ └───┬────┘ └────┬──────────┘
       │         │          │            │
       └─────────┴──────────┴────────────┘
                            │
                  ┌─────────▼──────────┐
                  │   cron_scheduler   │
                  └────────────────────┘
                            │
                  ┌─────────▼──────────┐
                  │   state_machine    │
                  │   (placeholder)    │
                  └────────────────────┘
```

### Thread Safety

All shared data is protected by FreeRTOS mutexes:

| Mutex | Defined In | Protects |
|-------|-----------|----------|
| `io_state_mutex` | io_monitor | `input_states[]`, `output_states[]`, GPIO operations |
| `voltage_mutex` | voltage_monitor | `battery_voltage`, `solar_voltage` |
| `mqtt_mutex` | mqtt_manager | MQTT client operations |
| `time_and_schedule_mutex` | time_sync | `current_time`, schedule data |
| `sm_mutex` | state_machine | `current_state`, `state_b_start_time` |
| `g_coop_mutex` | cron_scheduler | `g_coop_command` |
| `g_ntp_mutex` | cron_scheduler | `g_ntp_synced` flag |

**Mutex acquisition order** (when more than one is needed, always acquire in this order to prevent deadlock):

```
io_state_mutex → sm_mutex → voltage_mutex → time_and_schedule_mutex → mqtt_mutex
```

---

## Modules

### `io_monitor`
Manages all digital I/O. Configures input pins with pull-ups and output pins, implements 50ms debouncing on all inputs, and tracks state changes. Sets the `io_publish_needed` flag when any input changes so the main loop can publish via MQTT without the monitor task calling into the MQTT module.

**Key globals:** `input_states[]`, `output_states[]`, `io_state_mutex`, `io_publish_needed`

**Key functions:**
- `io_init()` — configure pins and create mutex
- `io_read_inputs()` — read and debounce all inputs (call while holding `io_state_mutex`)
- `io_has_state_changed()` — returns true and updates internal baseline if any input changed
- `io_set_output(int index, uint8_t value)` — set output by index
- `io_set_output_by_name(const char* name, uint8_t value)` — set output by name, returns index
- `io_get_input_by_name(const char* name)` — returns current debounced state, or -1
- `io_monitor_task()` — FreeRTOS task, Core 1, 10ms fixed interval via `vTaskDelayUntil`

---

### `voltage_monitor`
Reads battery and solar voltages through ADC pins using averaged samples and configurable voltage divider ratios. Uses 12-bit ADC with 11dB attenuation.

**Key globals:** `battery_voltage`, `solar_voltage`, `voltage_mutex`

**Key functions:**
- `voltage_init()` — configure ADC pins and create mutex
- `voltage_read_all()` — read and average both ADC channels, update globals
- `voltage_get_battery_status(float v)` — returns `"full"`, `"good"`, `"low"`, or `"critical"`
- `voltage_is_solar_charging(float solar_v, float battery_v)` — true if solar > battery + 0.5V
- `voltage_monitor_task()` — FreeRTOS task, Core 0, 60s fixed interval

---

### `time_sync`
Configures NTP time synchronization and maintains `current_time`. Notifies the cron scheduler when a valid sync is achieved. Timezone defaults to PST (UTC-8) with DST.

**Key globals:** `current_time`, `time_and_schedule_mutex`

**Key functions:**
- `time_sync_init()` — configure NTP, wait for first sync, notify cron scheduler
- `time_get_local(struct tm*)` — thread-safe local time query
- `time_format(char*, size_t, const char*)` — format current time using strftime
- `time_sync_task()` — FreeRTOS task, Core 0, 60s fixed interval

---

### `mqtt_manager`
Handles all MQTT operations: SSL/TLS setup, connection, reconnection, topic subscription, publishing, and the message callback. Uses `broker_config.h` and `client.h` for multi-broker and multi-certificate configuration.

**MQTT Topics:**

| Topic | Direction | Description |
|-------|-----------|-------------|
| `chickencoop/io/state` | Publish | Current I/O state (on change, on connect, on request) |
| `chickencoop/voltages` | Publish | Battery and solar voltages (every 60s, on connect, on request) |
| `chickencoop/time` | Publish | Current time (every 60s, on connect, on request) |
| `chickencoop/io/command` | Subscribe | Output control commands |
| `chickencoop/status/request` | Subscribe | Triggers full state publish |
| `chickencoop/schedule` | Subscribe | Sets cron open/close times |

**Key globals:** `mqtt_mutex`, `status_request_pending`

**Key functions:**
- `mqtt_init()` — create mutex, set callback
- `mqtt_connect(broker, client, id)` — SSL setup, connect, subscribe, publish initial state
- `mqtt_reconnect(broker, client, id)` — blocking reconnect with retry
- `mqtt_publish_io_state()` — publishes flat JSON of all I/O names and states
- `mqtt_publish_voltage()` — publishes battery/solar readings with status
- `mqtt_publish_time()` — publishes current timestamp
- `mqtt_loop()` — must be called regularly from main loop

**Published JSON examples:**

```json
// chickencoop/io/state
{
  "UPLIM": false, "DNLIM": true, "UPS": false, "DNS": false, "UPI": false, "DNI": false,
  "A": false, "B": false, "C": false, "D": false, "WIFI_LED": true,
  "timestamp": 123456, "unix_time": 1734586305, "online": true
}

// chickencoop/voltages
{
  "batteryVoltage": "12.45", "solarVoltage": "13.20",
  "batteryStatus": "good", "solarStatus": true,
  "unix_time": 1734586305, "timestamp": 123456
}

// chickencoop/time
{
  "time": "2025-06-15 10:30:00",
  "unix_time": 1734586305,
  "year": 2025, "month": 6, "day": 15, "hour": 10, "minute": 30, "second": 0
}
```

---

### `cron_scheduler`
FreeRTOS timer-based scheduler for timed door operations. Receives open and close times via MQTT, arms one-shot timers that fire at the specified UTC wall-clock times, and re-arms for the next day after firing. Guards against firing before NTP sync is valid.

**Commands written to `g_coop_command`:**

| Value | Meaning |
|-------|---------|
| 0 | Idle (no pending action) |
| 6 | Open door |
| 9 | Close door |
| -1 | Error sentinel |

**Schedule MQTT payload** (topic: `chickencoop/schedule`):

```json
{
  "open_time": "06:30",
  "close_time": "20:00",
  "open_enabled": true,
  "close_enabled": true
}
```

**Key functions:**
- `cronScheduler_init()` — initialize mutexes
- `ntp_setSynced(bool)` — call from time_sync once NTP is valid
- `ntp_isSynced()` — query sync status (also validates system clock)
- `applyScheduleFromMQTT(payload, length)` — parse JSON and arm timers
- `coopCommand_get()` / `coopCommand_set()` — thread-safe command accessors

---

### `state_machine` *(Placeholder — In Development)*

A two-state FSM framework intended to sequence the door motor. Uses only `io_monitor` functions for all GPIO access to ensure thread safety.

```
STATE_A (Idle)
    │  Input A: LOW → HIGH
    ▼
STATE_B (Active)
    │  Input B: LOW → HIGH  OR  timer expires
    ▼
STATE_A (Idle)
```

- **State A:** monitors a named input for a LOW→HIGH transition; on detection, sets a named output HIGH, starts a timeout timer, and enters State B
- **State B:** monitors a second named input and the timer; on either trigger, resets the output and returns to State A
- Configured by pin names (`SM_INPUT_A_NAME`, `SM_INPUT_B_NAME`, `SM_OUTPUT_NAME`) and timeout (`SM_TIMEOUT_MS`)
- Runs as a FreeRTOS task on Core 1 at 50ms intervals

> ⚠️ The state machine is a placeholder and has not been tested against real hardware. It compiles and the task runs but the motor control logic is not yet wired to the cron scheduler.

---

## FreeRTOS Task Layout

| Task | Function | Core | Priority | Interval |
|------|----------|------|----------|----------|
| I/O Monitor | `io_monitor_task` | 1 | 2 | 10ms |
| State Machine | `state_machine_task` | 1 | 2 | 50ms |
| Time Sync | `time_sync_task` | 0 | 1 | 60s |
| Voltage Monitor | `voltage_monitor_task` | 0 | 1 | 60s |
| Main Loop | Arduino `loop()` | 1 | — | ~10ms |

All periodic tasks use `vTaskDelayUntil()` for drift-free fixed-rate execution.

---

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP32 development board
- MQTT broker with SSL/TLS support (e.g., Mosquitto)

### Installation

1. Clone the repository:
   ```bash
   git clone https://github.com/yourusername/chickencoop-monitor.git
   cd chickencoop-monitor
   ```

2. Create the required configuration files (see [Configuration](#configuration)):
   ```
   include/secrets.h
   include/broker_config.h
   include/broker_config.cpp
   include/client.h
   include/client.cpp
   ```

3. Build and upload:
   ```bash
   pio run --target upload
   pio device monitor
   ```

---

## Configuration

### `secrets.h` *(not in repository)*

Create `include/secrets.h` from the template:

```cpp
#pragma once

struct WiFiCredentials {
    const char* ssid;
    const char* password;
    const char* description;
};

const WiFiCredentials WIFI_NETWORKS[] = {
    { "your_ssid_1",     "your_password_1", "Primary"  },
    { "your_ssid_2",     "your_password_2", "Backup"   },
};
const int NUM_WIFI_NETWORKS = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

const char* SELECTED_BROKER = "your-broker-name";
const char* SELECTED_CLIENT = "your-client-name";
const char* MQTT_CLIENT_ID  = "ESP32_ChickenCoop";
```

### `broker_config.h` / `broker_config.cpp` *(not in repository)*

Define one or more MQTT brokers. The `BrokerConfig` struct holds the broker name, URL, CA certificate, optional username/password, and port numbers. Example:

```cpp
// broker_config.h
struct BrokerConfig {
    const char* name;
    const char* url;
    const char* server_cert;  // CA certificate PEM
    const char* username;
    const char* password;
    int port_plain;
    int port_ssl;
};

extern const BrokerConfig BROKER_CONFIGS[];
extern const int NUM_BROKERS;

const BrokerConfig* getBrokerConfig(const char* name);
const BrokerConfig* getBrokerByIndex(int index);
void listBrokers();
void printBrokerInfo(const BrokerConfig* config);
```

```cpp
// broker_config.cpp
const char* dataDancerCert = R"EOF(
-----BEGIN CERTIFICATE-----
<YOUR CA CERTIFICATE HERE>
-----END CERTIFICATE-----
)EOF";

const BrokerConfig BROKER_CONFIGS[] = {
    { "your-broker", "your.broker.hostname", dataDancerCert, "", "", 1883, 8883 },
};
const int NUM_BROKERS = sizeof(BROKER_CONFIGS) / sizeof(BROKER_CONFIGS[0]);
```

### `client.h` / `client.cpp` *(not in repository)*

Define client certificates for mutual TLS. The `ClientConfig` struct holds the client certificate, private key, CA certificate, and a description. Example:

```cpp
// client.h
struct ClientConfig {
    const char* name;
    const char* cert;      // Client certificate PEM
    const char* key;       // Private key PEM
    const char* ca_cert;   // Optional: override broker CA
    const char* description;
};

extern const ClientConfig CLIENT_CONFIGS[];
extern const int NUM_CLIENTS;

const ClientConfig* getClientConfig(const char* name);
const ClientConfig* getClientByIndex(int index);
void listClients();
bool validateClientConfig(const ClientConfig* config);
void printClientInfo(const ClientConfig* config);
```

```cpp
// client.cpp
const char* myClientCert = R"EOF(
-----BEGIN CERTIFICATE-----
<YOUR CLIENT CERTIFICATE HERE>
-----END CERTIFICATE-----
)EOF";

const char* myClientKey = R"EOF(
-----BEGIN PRIVATE KEY-----
<YOUR PRIVATE KEY HERE>
-----END PRIVATE KEY-----
)EOF";

const ClientConfig CLIENT_CONFIGS[] = {
    { "your-client-name", myClientCert, myClientKey, nullptr, "My ESP32 client" },
};
const int NUM_CLIENTS = sizeof(CLIENT_CONFIGS) / sizeof(CLIENT_CONFIGS[0]);
```

### `.gitignore`

Ensure sensitive files are never committed:

```gitignore
include/secrets.h
include/broker_config.cpp
include/client.cpp
.pio/
```

### Adjustable Parameters

| Parameter | File | Default | Description |
|-----------|------|---------|-------------|
| `INPUT_PINS[]` | io_monitor.cpp | {14,27,13,12,22,23} | Input GPIO assignments |
| `OUTPUT_PINS[]` | io_monitor.cpp | {18,19,25,33,2} | Output GPIO assignments |
| `DEBOUNCE_DELAY` | io_monitor.cpp | 50ms | Input debounce time |
| `BATTERY_ADC_PIN` | voltage_monitor.cpp | 34 | Battery ADC GPIO |
| `SOLAR_ADC_PIN` | voltage_monitor.cpp | 35 | Solar ADC GPIO |
| `BATTERY_VOLTAGE_RATIO` | voltage_monitor.cpp | 5.7 | Voltage divider ratio |
| `SOLAR_VOLTAGE_RATIO` | voltage_monitor.cpp | 10.09 | Voltage divider ratio |
| `gmt_offset_sec` | time_sync.cpp | -28800 (PST) | Timezone offset |
| `SM_INPUT_A_NAME` | state_machine.cpp | "UPI" | State machine trigger input |
| `SM_INPUT_B_NAME` | state_machine.cpp | "UPLIM" | State machine stop input |
| `SM_OUTPUT_NAME` | state_machine.cpp | "C" | State machine output |
| `SM_TIMEOUT_MS` | state_machine.cpp | 5000ms | State B timeout |

---

## Security

- SSL/TLS mutual authentication (mTLS) is used for all MQTT connections
- Certificates are stored in separate `.cpp` files excluded from version control
- WiFi credentials are stored in `secrets.h`, also excluded from version control
- The `broker_config.h` and `client.h` interface headers are safe to commit — they contain no credentials
- Never commit `broker_config.cpp`, `client.cpp`, or `secrets.h`

---

## Project Status

| Module | Status |
|--------|--------|
| WiFi (multi-network) | ✅ Working |
| I/O Monitor | ✅ Working |
| Voltage Monitor | ✅ Working |
| Time Sync (NTP) | ✅ Working |
| MQTT Manager (mTLS) | ✅ Working |
| Cron Scheduler | ✅ Working (timer arming; pending integration with state machine) |
| State Machine | 🚧 Placeholder — compiles, not yet tested on hardware |
| Door motor control | 🚧 Not yet implemented |

---

## License

MIT License — see [LICENSE](LICENSE) for details.
