# ESP32 Chicken Coop Monitor

A modular, multi-threaded ESP32 firmware for automated chicken coop monitoring and control. The system monitors digital I/O (limit switches, command inputs), battery and solar voltages, synchronizes time via NTP, runs a cron-based door schedule, and communicates with a backend server over MQTT with SSL/TLS mutual authentication.

**Related repository:** [chickencoop_ubuntu](https://github.com/markatango/chickencoop_ubuntu) — the Ubuntu web server and UI that communicates with this firmware over MQTT. See [MQTT_API.md](MQTT_API.md) for the shared interface contract.

---

## Table of Contents

- [Features](#features)
- [Hardware](#hardware)
- [Software Architecture](#software-architecture)
- [Modules](#modules)
- [MQTT Interface](MQTT_API.md)
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
- **Digital I/O monitoring** — 6 debounced inputs (active-LOW, INPUT_PULLUP) and 7 outputs; state published to MQTT on change
- **Battery and solar voltage monitoring** — ADC readings with configurable voltage divider ratios
- **NTP time synchronization** — Pacific time with US DST rules; timezone overridable at runtime via MQTT schedule payload
- **Cron scheduler** — FreeRTOS timer-based scheduler for timed door open/close events, configured via MQTT; fires in local time with correct DST
- **Door state machine** — full FSM for automated door motor control with safety timeouts, limit switch validation, error detection, and error-freeze diagnostics
- **Door operation timing** — measures and publishes motor run duration on each open/close cycle
- **On-demand status** — server can request a full state snapshot at any time

---

## Hardware

### ESP32 Pin Assignments

#### Digital Inputs (INPUT_PULLUP, active-LOW)

All inputs use internal pull-ups and are active-LOW: a physical switch closure pulls the pin to GND (logical HIGH after inversion at the state machine boundary).

| Name | GPIO | Description |
| --- | --- | --- |
| UPLIM | 14 | Upper (open) limit switch |
| DNLIM | 27 | Lower (closed) limit switch |
| UPS | 13 | Up position sensor |
| DNS | 12 | Down position sensor |
| CLR_ERROR | 22 | Clear error input — rising edge clears MOT_ERROR or LIM_ERROR |
| DNI | 23 | Down input |

#### Digital Outputs

| Name | GPIO | Description | Active Level |
| --- | --- | --- | --- |
| Error | 4 | Error LED | LOW (illuminated when LOW) |
| B | 5 | Status LED | LOW (illuminated when LOW) |
| C | 18 | General purpose output | HIGH |
| D | 19 | General purpose output | HIGH |
| Mup | 25 | Motor UP relay | HIGH |
| Mdn | 33 | Motor DOWN relay | HIGH |
| WIFI_LED | 2 | WiFi connected indicator (onboard LED) | HIGH |

> **Note:** The Error and B LEDs are active-LOW. GPIO5 has an internal pull-up causing a brief flash on B at boot; this is normal hardware behavior.

#### Analog Inputs (ADC)

| Name | GPIO | Description | Voltage Divider Ratio |
| --- | --- | --- | --- |
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
                  │   (door FSM)       │
                  └────────────────────┘
```

### Thread Safety

All shared data is protected by FreeRTOS mutexes:

| Mutex | Defined In | Protects |
| --- | --- | --- |
| `io_state_mutex` | io_monitor | `input_states[]`, `output_states[]`, GPIO operations |
| `voltage_mutex` | voltage_monitor | `battery_voltage`, `solar_voltage` |
| `mqtt_mutex` | mqtt_manager | MQTT client operations |
| `time_and_schedule_mutex` | time_sync | `current_time` |
| `sm_mutex` | state_machine | `coop_state` |
| `g_coop_mutex` | cron_scheduler | `g_coop_command` |
| `g_ntp_mutex` | cron_scheduler | `g_ntp_synced` flag |

**Mutex acquisition order** (when more than one is needed, always acquire in this order to prevent deadlock):

```
io_state_mutex → sm_mutex → voltage_mutex → time_and_schedule_mutex → mqtt_mutex
```

---

## Modules

### `io_monitor`

Manages all digital I/O. Configures input pins with pull-ups and output pins, implements 50ms debouncing on all inputs, and tracks state changes. Primes `input_states[]` from actual `digitalRead()` at init time so the state machine never sees false "all switches active" from zero-initialization. Sets the `io_publish_needed` flag when any input changes so the main loop can publish via MQTT without the monitor task calling into the MQTT module.

**Key globals:** `input_states[]`, `output_states[]`, `io_state_mutex`, `io_publish_needed`

**Key functions:**

- `io_init()` — configure pins, create mutex, prime input state arrays from hardware
- `io_read_inputs()` — read and debounce all inputs (call while holding `io_state_mutex`)
- `io_has_state_changed()` — returns true and updates internal baseline if any input changed
- `io_set_output(int index, uint8_t value)` — set output by index, keeps `output_states[]` in sync
- `io_set_output_by_name(const char* name, uint8_t value)` — set output by name, returns index
- `io_get_input_by_name(const char* name)` — returns current debounced state, or -1
- `io_monitor_task()` — FreeRTOS task, Core 1, 10ms fixed interval via `vTaskDelayUntil`

---

### `voltage_monitor`

Reads battery and solar voltages through ADC pins using averaged samples and configurable voltage divider ratios. Uses 12-bit ADC with 11dB attenuation. Serial and MQTT output is suppressed when the state machine is in an error-freeze state (`g_sm_freeze`).

**Key globals:** `battery_voltage`, `solar_voltage`, `voltage_mutex`

**Key functions:**

- `voltage_init()` — configure ADC pins and create mutex
- `voltage_read_all()` — read and average both ADC channels, update globals
- `voltage_get_battery_status(float v)` — returns `"full"`, `"good"`, `"low"`, or `"critical"`
- `voltage_is_solar_charging(float solar_v, float battery_v)` — true if solar > battery + 0.5V
- `voltage_monitor_task()` — FreeRTOS task, Core 0, 60s fixed interval

---

### `time_sync`

Configures NTP time synchronization using a POSIX TZ string for correct local time and DST handling. The default TZ is `PST8PDT,M3.2.0,M11.1.0` (US Pacific with standard DST rules); this can be overridden at runtime when the MQTT schedule payload includes a `timezone` field. Notifies the cron scheduler when a valid sync is achieved. Serial output is suppressed during error-freeze.

**Key globals:** `current_time`, `time_and_schedule_mutex`

**Key functions:**

- `time_sync_init()` — call `configTzTime()`, wait up to 10s for first sync, notify cron scheduler
- `time_get_local(struct tm*)` — thread-safe local time query via `getLocalTime()`
- `time_format(char*, size_t, const char*)` — format current time using strftime
- `time_sync_task()` — FreeRTOS task, Core 0, 60s fixed interval; marks NTP synced regardless of freeze state

---

### `mqtt_manager`

Handles all MQTT operations: SSL/TLS setup, connection, reconnection, topic subscription, publishing, and the message callback. Uses `broker_config.h` and `client.h` for multi-broker and multi-certificate configuration. Periodic publish functions (`mqtt_publish_time`, `mqtt_publish_voltage`) return immediately when the state machine is in error-freeze.

**MQTT Topics:**

| Topic | Direction | Description |
| --- | --- | --- |
| `chickencoop/io/state` | Publish | Current I/O and SM state (on change, on connect, on request) |
| `chickencoop/voltages` | Publish | Battery and solar voltages (every 60s, on connect, on request) |
| `chickencoop/time` | Publish | Current time (every 60s, on connect, on request) |
| `chickencoop/door/operation_time` | Publish | Motor run duration after each open/close cycle |
| `chickencoop/io/command` | Subscribe | Door control commands: `open`, `close`, `reset` |
| `chickencoop/status/request` | Subscribe | Triggers full state publish |
| `chickencoop/schedule` | Subscribe | Sets cron open/close times and timezone |

**Key globals:** `mqtt_mutex`, `status_request_pending`

**Key functions:**

- `mqtt_init()` — create mutex, set callback, set packet buffer to 1024 bytes
- `mqtt_connect(broker, client, id)` — SSL setup, connect, subscribe, publish initial state
- `mqtt_reconnect(broker, client, id)` — rate-limited reconnect (10s between attempts)
- `mqtt_publish_io_state()` — publishes flat JSON of all I/O names, states, and SM state
- `mqtt_publish_voltage()` — publishes battery/solar readings with status
- `mqtt_publish_time()` — publishes current timestamp
- `mqtt_publish_door_operation(duration_s, direction)` — publishes motor run duration and direction
- `mqtt_loop()` — must be called regularly from main loop

**Published JSON examples:**

```json
// chickencoop/io/state
{
  "UPLIM": false, "DNLIM": true, "UPS": false, "DNS": false,
  "CLR_ERROR": false, "DNI": false,
  "Error": true, "B": false, "C": false, "D": false,
  "Mup": false, "Mdn": false, "WIFI_LED": true,
  "sm_state_prev": "CLOSING", "sm_state": "CLOSED",
  "timestamp": 123456, "unix_time": 1781993219, "online": true
}

// chickencoop/voltages
{
  "batteryVoltage": "12.45", "solarVoltage": "13.20",
  "batteryStatus": "good", "solarStatus": true,
  "unix_time": 1781993219, "timestamp": 123456
}

// chickencoop/time
{
  "time": "2026-06-21 09:01:00",
  "unix_time": 1782043260,
  "year": 2026, "month": 6, "day": 21, "hour": 9, "minute": 1, "second": 0
}

// chickencoop/door/operation_time
{
  "duration": "14.23", "door": "main", "direction": "open",
  "timestamp": "2026-06-21T16:01:14Z"
}
```

**IO control commands** (topic: `chickencoop/io/command`):

Accepts plain-text or JSON (`{"command": "open"}`):

| Command | Action |
| --- | --- |
| `open` | Queue open event in state machine |
| `close` | Queue close event in state machine |
| `reset` | Clear MOT_ERROR or LIM_ERROR, return to START |

---

### `cron_scheduler`

FreeRTOS timer-based scheduler for timed door operations. Receives open and close times (plus optional timezone) via MQTT, arms one-shot timers that fire at the specified local-time wall-clock times, and re-arms for the next day after firing. Handles the NTP-not-yet-synced case with 60-second polling, transitioning to exact timing once the clock is valid without firing a spurious command.

**Timer period computation:** Periods are computed as `(TickType_t)(secs) * (TickType_t)(configTICK_RATE_HZ)` — directly seconds-to-ticks — to avoid the uint32_t overflow that `pdMS_TO_TICKS(secs * 1000)` produces for periods longer than ~71 minutes.

**Schedule MQTT payload** (topic: `chickencoop/schedule`):

```json
{
  "open_time": "09:01",
  "close_time": "21:00",
  "open_enabled": true,
  "close_enabled": true,
  "timezone": "PST8PDT,M3.2.0,M11.1.0"
}
```

The `timezone` field is a POSIX TZ string. If present it is applied via `setenv("TZ", ...) + tzset()` before any time calculations, so `open_time`/`close_time` fire in the correct local time with DST.

**Key functions:**

- `cronScheduler_init()` — initialize mutexes
- `ntp_setSynced(bool)` — called from `time_sync` once NTP is valid
- `ntp_isSynced()` — query sync status (also validates system clock > 2020-01-01)
- `applyScheduleFromMQTT(payload, length)` — parse JSON, apply TZ, arm timers
- `coopCommand_get()` / `coopCommand_set()` — thread-safe command accessors

---

### `state_machine`

A full FSM for automated chicken coop door motor control. Runs as a FreeRTOS task on Core 1 at 50ms intervals. All GPIO access goes through `io_monitor` functions to maintain thread safety. On error entry, sets `g_sm_freeze` to silence periodic serial/MQTT output and dumps raw I/O pin states for diagnosis.

#### States

| State | Description |
| --- | --- |
| `START` | Boot state; performs startup recovery based on active limit switches |
| `OPEN` | Door is fully open (UPLIM active, no motor running) |
| `CLOSED` | Door is fully closed (DNLIM active, no motor running) |
| `OPENING` | Motor UP running; waiting for UPLIM |
| `CLOSING` | Motor DOWN running; waiting for DNLIM |
| `MOT_ERROR` | Motor ran to timeout without reaching the target limit switch |
| `LIM_ERROR` | Conflicting or impossible limit switch combination detected |

#### State Transitions

```
                  ┌──────────────────────────────────────────────────────────┐
                  │   Any state: both limits active → LIM_ERROR              │
                  └──────────────────────────────────────────────────────────┘

              ┌────────[ UPLIM only ]────────┐
              │                              ▼
         [START]──────[ DNLIM only ]──────►[CLOSED]──[close cmd]──►[CLOSING]──[DNLIM]──┐
              │                               ▲                          │               │
              └──[neither/both]──►[wait]      │                     [timeout]            │
                                             [CLOSED]◄──────────────────┘               │
                                                                    [MOT_ERROR]◄─────────┘

         [OPEN]──[open cmd]──►[OPENING]──[UPLIM]──►[OPEN]
           ▲                      │
           │                  [timeout]
           │                 [MOT_ERROR]
           │
           └──[DNLIM active, no motor]──►[CLOSED]   (reality check)

    [MOT_ERROR] or [LIM_ERROR] ──[CLR_ERROR↑ or MQTT reset]──► [START]
```

#### Inputs consumed by the state machine

Inputs are active-LOW with INPUT_PULLUP; the SM boundary inverts them so HIGH = switch closed = active.

| Input | Used For |
| --- | --- |
| UPLIM | Detect door fully open; stops motor in OPENING |
| DNLIM | Detect door fully closed; stops motor in CLOSING |
| UPS | Edge-detected up-position sensor |
| DNS | Edge-detected down-position sensor |
| CLR_ERROR | Rising-edge clears MOT_ERROR or LIM_ERROR |

#### Outputs driven by the state machine

| Output | When HIGH |
| --- | --- |
| Mup | Motor UP relay energised (door opening) |
| Mdn | Motor DOWN relay energised (door closing) |
| Error | Error LED illuminated (LOW = ON, active-LOW) |

#### Error freeze

When the SM enters `MOT_ERROR` or `LIM_ERROR`:

- `g_sm_freeze` is set — silences all periodic serial and MQTT output
- Raw I/O state (all inputs and outputs) is dumped to serial for post-mortem analysis
- The freeze lifts when the SM transitions back to `START` via CLR_ERROR or MQTT `reset`

#### Door operation timing

On each successful OPENING or CLOSING, the SM measures the elapsed motor run time and publishes it to `chickencoop/door/operation_time` via `mqtt_publish_door_operation()`.

#### Startup recovery

In `START`, the SM reads the physical limit switches:

- Only UPLIM active → transition directly to `OPEN`
- Only DNLIM active → transition directly to `CLOSED`
- Both or neither active → remain in `START` until a valid command arrives

**Key globals:** `coop_state`, `sm_mutex`, `g_sm_freeze`

**Key functions:**

- `sm_init()` — create mutex, initialize edge-detection history from live pin reads
- `sm_trigger_open()` — queue open event (called from cron or MQTT)
- `sm_trigger_close()` — queue close event (called from cron or MQTT)
- `sm_trigger_reset()` — queue reset event; same effect as CLR_ERROR rising edge
- `sm_get_state_string()` — thread-safe snapshot of current state name
- `state_machine_task()` — FreeRTOS task, Core 1, 50ms fixed interval

---

## FreeRTOS Task Layout

| Task | Function | Core | Priority | Interval |
| --- | --- | --- | --- | --- |
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

2. Create the required configuration files from the included examples (see [Configuration](#configuration)):

   | File to create | Copy from |
   | --- | --- |
   | `include/secrets.h` | *(no example — see template below)* |
   | `include/broker_config.h` | `include/broker_config.example.h` |
   | `src/broker_config.cpp` | `src/broker_config.example.cpp` |
   | `include/client.h` | *(no example — see template below)* |
   | `src/client.cpp` | *(no example — contains private key)* |

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
const char* myCACert = R"EOF(
-----BEGIN CERTIFICATE-----
<YOUR CA CERTIFICATE HERE>
-----END CERTIFICATE-----
)EOF";

const BrokerConfig BROKER_CONFIGS[] = {
    { "your-broker", "your.broker.hostname", myCACert, "", "", 1883, 8883 },
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
include/broker_config.h
src/broker_config.cpp
**/client.*
.pio/
```

### Adjustable Parameters

| Parameter | File | Default | Description |
| --- | --- | --- | --- |
| `INPUT_PINS[]` | io_monitor.cpp | {14,27,13,12,22,23} | Input GPIO assignments |
| `OUTPUT_PINS[]` | io_monitor.cpp | {4,5,18,19,25,33,2} | Output GPIO assignments |
| `DEBOUNCE_DELAY` | io_monitor.cpp | 50ms | Input debounce time |
| `BATTERY_ADC_PIN` | voltage_monitor.cpp | 34 | Battery ADC GPIO |
| `SOLAR_ADC_PIN` | voltage_monitor.cpp | 35 | Solar ADC GPIO |
| `BATTERY_VOLTAGE_RATIO` | voltage_monitor.cpp | 5.7 | Voltage divider ratio |
| `SOLAR_VOLTAGE_RATIO` | voltage_monitor.cpp | 10.09 | Voltage divider ratio |
| `DEFAULT_TZ` | time_sync.cpp | `"PST8PDT,M3.2.0,M11.1.0"` | POSIX TZ string (overridden by MQTT schedule) |
| `OPENING_TIMEOUT_MS` | state_machine.cpp | 30000ms | Max motor run time before MOT_ERROR |
| `CLOSING_TIMEOUT_MS` | state_machine.cpp | 30000ms | Max motor run time before MOT_ERROR |

---

## Security

- SSL/TLS mutual authentication (mTLS) is used for all MQTT connections
- WiFi credentials are stored in `include/secrets.h` — excluded from version control
- Broker CA certificates, broker URLs/IPs, and credentials are in `include/broker_config.h` and `src/broker_config.cpp` — both excluded from version control
- Client certificate and private key are in `include/client.h` and `src/client.cpp` — both excluded from version control
- Example/template versions of `broker_config.*` (with all sensitive values replaced by placeholders) are included in the repository as `broker_config.example.h` and `broker_config.example.cpp`
- Never commit `secrets.h`, `broker_config.h`, `broker_config.cpp`, `client.h`, or `client.cpp`

---

## Project Status

> **Beta** — The full door control system has been developed and tested against a hardware simulator. Physical integration with the chicken coop controller has not yet been performed. Behavior under real-world hardware conditions (mechanical faults, long cable runs, noisy limit switches) is untested.

| Module | Status | Notes |
| --- | --- | --- |
| WiFi (multi-network) | ✅ Working | |
| I/O Monitor | ✅ Working | |
| Voltage Monitor | ✅ Working | |
| Time Sync (NTP) | ✅ Working | Pacific time + DST via POSIX TZ string |
| MQTT Manager (mTLS) | ✅ Working | 1024-byte packet buffer |
| Cron Scheduler | ✅ Working | Local time with DST; NTP-sync safe; overflow-safe timer periods |
| Door State Machine | 🔶 Beta | Simulator-tested; pending physical hardware integration |
| Door motor control | 🔶 Beta | Full open/close/error/recovery cycle; simulator-tested |
| Error diagnostics | ✅ Working | Freeze-on-error with I/O state dump at error entry |

---

## License

MIT License — see [LICENSE](LICENSE) for details.
