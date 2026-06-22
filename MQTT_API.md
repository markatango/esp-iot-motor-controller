# Chicken Coop MQTT API

This document is the authoritative interface contract between the two components of the chicken coop system:

| Component | Repository |
|-----------|------------|
| ESP32 firmware | [esp-iot-motor-controller](https://github.com/markatango/esp-iot-motor-controller) |
| Ubuntu web server | [chickencoop_ubuntu](https://github.com/markatango/chickencoop_ubuntu) |

When this interface changes in a way that breaks backward compatibility, both repositories should be tagged with the same version number (e.g., `v1.1`) on the same day.

---

## Protocol

- **Protocol:** MQTT 3.1.1
- **Transport:** SSL/TLS port 8883, mutual authentication (mTLS)
- **Broker:** Configured per-deployment in `broker_config.h` / `broker_config.cpp`

---

## Topic Reference

| Topic | Publisher | Subscriber | QoS | Retained |
|-------|-----------|------------|:---:|:--------:|
| `chickencoop/io/state` | ESP32 | Server | 0 | Yes |
| `chickencoop/voltages` | ESP32 | Server | 0 | Yes |
| `chickencoop/time` | ESP32 | Server | 0 | Yes |
| `chickencoop/door/operation_time` | ESP32 | Server | 0 | Yes |
| `chickencoop/io/command` | Server | ESP32 | 1 | No |
| `chickencoop/status/request` | Server | ESP32 | 1 | No |
| `chickencoop/schedule` | Server | ESP32 | 1 | Yes |

---

## ESP32 Publishes

### `chickencoop/io/state`

Published on every input change, on MQTT connect, and in response to a status request.

**QoS:** 0 &nbsp;&nbsp; **Retained:** Yes

```json
{
  "UPLIM": false,
  "DNLIM": true,
  "UPS": false,
  "DNS": false,
  "CLR_ERROR": false,
  "DNI": false,
  "Error": true,
  "B": false,
  "C": false,
  "D": false,
  "Mup": false,
  "Mdn": false,
  "WIFI_LED": true,
  "sm_state_prev": "CLOSING",
  "sm_state": "CLOSED",
  "timestamp": 123456,
  "unix_time": 1781993219,
  "online": true
}
```

| Field | Type | Description |
|-------|------|-------------|
| `UPLIM` … `DNI` | bool | Debounced input states — `true` = switch active/closed |
| `Error` … `WIFI_LED` | bool | Output states |
| `sm_state` | string | Current FSM state (see values below) |
| `sm_state_prev` | string | Previous FSM state |
| `unix_time` | int | UTC Unix timestamp |
| `timestamp` | int | `millis()` at publish time |
| `online` | bool | Always `true` when present; absence of retained message implies offline |

**`sm_state` values:** `START`, `OPEN`, `CLOSED`, `OPENING`, `CLOSING`, `MOT_ERROR`, `LIM_ERROR`

---

### `chickencoop/voltages`

Published every 60 seconds, on MQTT connect, and on status request. Suppressed during error freeze.

**QoS:** 0 &nbsp;&nbsp; **Retained:** Yes

```json
{
  "batteryVoltage": "12.45",
  "solarVoltage": "13.20",
  "batteryStatus": "good",
  "solarStatus": true,
  "unix_time": 1781993219,
  "timestamp": 123456
}
```

| Field | Type | Description |
|-------|------|-------------|
| `batteryVoltage` | string | Volts, 2 decimal places |
| `solarVoltage` | string | Volts, 2 decimal places |
| `batteryStatus` | string | `"full"`, `"good"`, `"low"`, or `"critical"` |
| `solarStatus` | bool | `true` if solar voltage > battery + 0.5 V (panel is charging) |

---

### `chickencoop/time`

Published every 60 seconds, on MQTT connect, and on status request. Suppressed during error freeze.

**QoS:** 0 &nbsp;&nbsp; **Retained:** Yes

```json
{
  "time": "2026-06-21 09:01:00",
  "unix_time": 1782043260,
  "year": 2026,
  "month": 6,
  "day": 21,
  "hour": 9,
  "minute": 1,
  "second": 0
}
```

All values are in the configured local timezone (default: US Pacific with DST).

---

### `chickencoop/door/operation_time`

Published after each completed open or close cycle (motor stopped by limit switch, not by timeout).

**QoS:** 0 &nbsp;&nbsp; **Retained:** Yes

```json
{
  "duration": "14.23",
  "door": "main",
  "direction": "open",
  "timestamp": "2026-06-21T16:01:14Z"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `duration` | string | Motor run time in seconds, 2 decimal places |
| `door` | string | Always `"main"` (single door) |
| `direction` | string | `"open"` or `"close"` |
| `timestamp` | string | ISO 8601 UTC timestamp |

---

## Server Publishes

### `chickencoop/io/command`

Send a door control command to the ESP32.

**QoS:** 1 &nbsp;&nbsp; **Retained:** No

Accepts plain text or JSON:

```
open
close
reset
```

```json
{"command": "open"}
```

| Command | Effect |
|---------|--------|
| `open` | Starts motor up; SM transitions to `OPENING` |
| `close` | Starts motor down; SM transitions to `CLOSING` |
| `reset` | Clears `MOT_ERROR` or `LIM_ERROR`, returns SM to `START` |

Commands other than `reset` are ignored while the SM is in `MOT_ERROR` or `LIM_ERROR`.

---

### `chickencoop/status/request`

Ask the ESP32 to immediately publish a full snapshot: I/O state, voltages, and time.

**QoS:** 1 &nbsp;&nbsp; **Retained:** No

**Payload:** any — convention is `"1"` or `"request"`

---

### `chickencoop/schedule`

Set the daily door schedule. The server should publish this with `retain=true` so the ESP32 receives it automatically after each reconnect.

**QoS:** 1 &nbsp;&nbsp; **Retained:** Yes (server must set retain flag)

```json
{
  "open_time": "09:01",
  "close_time": "21:00",
  "open_enabled": true,
  "close_enabled": true,
  "timezone": "PST8PDT,M3.2.0,M11.1.0"
}
```

| Field | Type | Required | Description |
|-------|------|:--------:|-------------|
| `open_time` | string | Yes | Local time `"HH:MM"` for daily open |
| `close_time` | string | Yes | Local time `"HH:MM"` for daily close |
| `open_enabled` | bool | Yes | `false` disables the open cron without clearing the time |
| `close_enabled` | bool | Yes | `false` disables the close cron without clearing the time |
| `timezone` | string | No | POSIX TZ string — applied before scheduling. Default: `"PST8PDT,M3.2.0,M11.1.0"` |

The `timezone` field is applied via `setenv("TZ", …) + tzset()` immediately on receipt. `open_time` and `close_time` are always interpreted in local time with correct DST.

---

## Behavioral Notes

### Error freeze

While the SM is in `MOT_ERROR` or `LIM_ERROR`, the ESP32 suspends periodic voltage and time publishes. I/O state is still published when inputs change. Send `reset` to `chickencoop/io/command` to clear the error.

### NTP sync and cron scheduling

The cron scheduler does not fire commands until the system clock is confirmed valid (Unix timestamp > 2020-01-01). If NTP has not synced by the scheduled fire time, the scheduler polls every 60 seconds until the clock is valid, then arms for the **next** daily occurrence of the target time — the missed event is not retroactively fired.

### Startup schedule recovery

The ESP32 stores the schedule only in RAM. On power cycle, the schedule is lost until the broker delivers the retained `chickencoop/schedule` message on reconnect. Keep the schedule published with `retain=true` on the broker.
