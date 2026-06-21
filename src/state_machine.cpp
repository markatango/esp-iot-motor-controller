/**
 * @file state_machine.cpp
 * @brief Chicken Coop Door State Machine
 *
 * Implements the state transition table in chickencoop state transition table.txt.
 *
 * Inputs read from io_monitor:
 *   UPLIM, DNLIM  — limit switches (1 = activated)
 *   UPS, DNS      — up/down command switches (1 = active, > = LOW→HIGH edge)
 *
 * Outputs driven via io_monitor:
 *   Mup, Mdn      — motor up / motor down
 *
 * External events injected by MQTT and CRON callers:
 *   ev_open, ev_close  — set via sm_trigger_open() / sm_trigger_close()
 */

#include "state_machine.h"
#include "mqtt_manager.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

static const unsigned long MOTOR_TIMEOUT_MS = 30000UL;  // safety cutoff

static const char* IN_UPLIM     = "UPLIM";
static const char* IN_DNLIM     = "DNLIM";
static const char* IN_UPS       = "UPS";
static const char* IN_DNS       = "DNS";
static const char* IN_CLR_ERROR = "CLR_ERROR";
static const char* OUT_MUP  = "Mup";
static const char* OUT_MDN  = "Mdn";
static const char* OUT_ERR  = "Error";  // active-low error LED (GPIO4)

// ============================================================================
// GLOBAL STATE
// ============================================================================

CoopState         coop_state = SM_STATE_START;
SemaphoreHandle_t sm_mutex   = NULL;
volatile bool     g_sm_freeze = false;

// Event flags — written by external callers, consumed once per SM cycle
static volatile bool ev_open  = false;
static volatile bool ev_close = false;
static volatile bool ev_reset = false;

// Previous input values for LOW→HIGH edge detection
static uint8_t prev_ups       = LOW;
static uint8_t prev_dns       = LOW;
static uint8_t prev_clr_error = LOW;

// Timestamp when the running motor was started (for timeout)
static unsigned long motor_start_ms = 0;

// ============================================================================
// INPUT SNAPSHOT  (one mutex lock keeps all readings consistent)
// ============================================================================

struct SMInputs {
    uint8_t uplim, dnlim, ups, dns;
    bool    ups_rise;       // LOW→HIGH edge this cycle
    bool    dns_rise;       // LOW→HIGH edge this cycle
    bool    clr_error_rise; // LOW→HIGH edge this cycle
    uint8_t motor_up, motor_down;
    bool    ev_open, ev_close;
};

static SMInputs read_inputs() {
    SMInputs in = {};
    if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
        // All four switches are active-LOW (INPUT_PULLUP): invert to logical active-HIGH
        in.uplim      = (io_get_input_by_name(IN_UPLIM) == LOW) ? HIGH : LOW;
        in.dnlim      = (io_get_input_by_name(IN_DNLIM) == LOW) ? HIGH : LOW;
        in.ups        = (io_get_input_by_name(IN_UPS)   == LOW) ? HIGH : LOW;
        in.dns        = (io_get_input_by_name(IN_DNS)   == LOW) ? HIGH : LOW;
        in.motor_up   = (uint8_t)io_get_output_by_name(OUT_MUP);
        in.motor_down = (uint8_t)io_get_output_by_name(OUT_MDN);
        uint8_t clr = (io_get_input_by_name(IN_CLR_ERROR) == LOW) ? HIGH : LOW;
        in.ups_rise       = (in.ups == HIGH && prev_ups       == LOW);
        in.dns_rise       = (in.dns == HIGH && prev_dns       == LOW);
        in.clr_error_rise = (clr == HIGH && prev_clr_error == LOW) || ev_reset;
        ev_reset = false;
        prev_ups       = in.ups;
        prev_dns       = in.dns;
        prev_clr_error = clr;
        xSemaphoreGive(io_state_mutex);
    }
    // Consume event flags; volatile write is sufficient for single-writer/single-reader
    in.ev_open  = ev_open;   ev_open  = false;
    in.ev_close = ev_close;  ev_close = false;
    return in;
}

// ============================================================================
// MOTOR CONTROL
// ============================================================================

static void set_motor_up() {
    motor_start_ms = millis();
    if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
        io_set_output_by_name(OUT_MUP, HIGH);
        io_set_output_by_name(OUT_MDN, LOW);
        xSemaphoreGive(io_state_mutex);
    }
    Serial.println("🔼 Motor UP started");
}

static void set_motor_down() {
    motor_start_ms = millis();
    if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
        io_set_output_by_name(OUT_MUP, LOW);
        io_set_output_by_name(OUT_MDN, HIGH);
        xSemaphoreGive(io_state_mutex);
    }
    Serial.println("🔽 Motor DOWN started");
}

static void stop_motors() {
    if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
        io_set_output_by_name(OUT_MUP, LOW);
        io_set_output_by_name(OUT_MDN, LOW);
        xSemaphoreGive(io_state_mutex);
    }
    Serial.println("⏹ Motors stopped");
}

static void set_error_led(bool on) {
    // Active-low: drive LOW to illuminate, HIGH to extinguish
    if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
        io_set_output_by_name(OUT_ERR, on ? LOW : HIGH);
        xSemaphoreGive(io_state_mutex);
    }
}

static bool motor_timed_out() {
    return (millis() - motor_start_ms) >= MOTOR_TIMEOUT_MS;
}

// ============================================================================
// ERROR DIAGNOSTICS
// ============================================================================

static void dump_io_state() {
    if (xSemaphoreTake(io_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        Serial.println("❌ [FREEZE] Could not read I/O state for dump");
        return;
    }
    Serial.println("=== I/O STATE AT ERROR ENTRY ===");
    Serial.print("  Inputs  (raw): ");
    for (int i = 0; i < NUM_DIGITAL_INPUTS; i++) {
        Serial.printf("%s=%d ", INPUT_NAMES[i], input_states[i]);
    }
    Serial.println();
    Serial.print("  Outputs (raw): ");
    for (int i = 0; i < NUM_DIGITAL_OUTPUTS; i++) {
        Serial.printf("%s=%d ", OUTPUT_NAMES[i], output_states[i]);
    }
    Serial.println();
    Serial.println("================================");
    xSemaphoreGive(io_state_mutex);
}

// ============================================================================
// STATE TRANSITION HELPER
// ============================================================================

static void do_transition(CoopState next) {
    if (next == SM_STATE_MOT_ERROR || next == SM_STATE_LIM_ERROR) {
        g_sm_freeze = true;
    } else if (next == SM_STATE_START) {
        g_sm_freeze = false;
    }
    if (xSemaphoreTake(sm_mutex, portMAX_DELAY) == pdTRUE) {
        coop_state = next;
        xSemaphoreGive(sm_mutex);
    }
    Serial.printf("🚪 Coop SM → %s\n", sm_state_name(next));
}

// ============================================================================
// SAFETY CHECK  (ANY STATE rows — evaluated every cycle before state logic)
// Returns true if a safety fault was triggered; caller must skip state logic.
// ============================================================================

static bool check_safety(const SMInputs& in) {
    if (in.motor_up && in.motor_down) {
        Serial.println("⚠️ MOT ERROR: both motors active simultaneously!");
        stop_motors();
        dump_io_state();
        set_error_led(true);
        do_transition(SM_STATE_MOT_ERROR);
        return true;
    }
    if (in.uplim && in.dnlim) {
        Serial.println("⚠️ LIM ERROR: both limit switches active!");
        stop_motors();
        dump_io_state();
        set_error_led(true);
        do_transition(SM_STATE_LIM_ERROR);
        return true;
    }
    return false;
}

// ============================================================================
// STATE PROCESSORS
// ============================================================================

static void process_start(const SMInputs& in) {
    // Startup recovery: single limit switch already active → skip to known state
    if (in.uplim && !in.dnlim && !in.motor_up && !in.motor_down) {
        do_transition(SM_STATE_OPEN);
        return;
    }
    if (in.dnlim && !in.uplim && !in.motor_up && !in.motor_down) {
        do_transition(SM_STATE_CLOSED);
        return;
    }

    // Recognise motor already running (e.g. after unexpected restart)
    if (!in.uplim && in.motor_up)   { do_transition(SM_STATE_OPENING); return; }
    if (!in.dnlim && in.motor_down) { do_transition(SM_STATE_CLOSING); return; }

    if (!in.motor_up && !in.motor_down) {
        // Start motor UP: ups LOW→HIGH edge, CRON OPEN, or WEB OPEN
        if (!in.uplim && (in.ups_rise || in.ev_open)) {
            set_motor_up();
            return;
        }
        // Start motor DOWN: dns LOW→HIGH edge, CRON CLOSE, or WEB CLOSE
        if (!in.dnlim && (in.dns_rise || in.ev_close)) {
            set_motor_down();
            return;
        }
    }
}

static void process_opening(const SMInputs& in) {
    // Upper limit hit while motoring up → door is OPEN
    if (in.uplim && in.motor_up) {
        float duration_s = (millis() - motor_start_ms) / 1000.0f;
        stop_motors();
        do_transition(SM_STATE_OPEN);
        mqtt_publish_door_operation(duration_s, "open");
        return;
    }
    if (motor_timed_out()) {
        Serial.printf("⚠️ OPENING timeout (%lus)!\n", MOTOR_TIMEOUT_MS / 1000);
        stop_motors();
        dump_io_state();
        set_error_led(true);
        do_transition(SM_STATE_MOT_ERROR);
    }
}

static void process_open(const SMInputs& in) {
    // Reality check: DNLIM asserted with no motor means the door is physically closed
    // but the SM state is stale — snap to CLOSED so commands can be acted on.
    if (in.dnlim && !in.motor_up && !in.motor_down) {
        Serial.println("⚠️ OPEN state but DNLIM active — correcting to CLOSED");
        do_transition(SM_STATE_CLOSED);
        return;
    }

    // Recognise motor already going down (recover from unexpected restart)
    if (!in.dnlim && in.motor_down) { do_transition(SM_STATE_CLOSING); return; }

    // dns=1 (level), CRON CLOSE, or WEB CLOSE triggers close; ups=0 guards conflicting command
    if (!in.dnlim && !in.motor_up && !in.motor_down && !in.ups &&
        (in.dns || in.ev_close)) {
        set_motor_down();
    }
}

static void process_closing(const SMInputs& in) {
    // Lower limit hit while motoring down → door is CLOSED
    // Note: row 15 in the table has motor columns transposed; corrected here: mdn=1
    if (in.dnlim && in.motor_down) {
        float duration_s = (millis() - motor_start_ms) / 1000.0f;
        stop_motors();
        do_transition(SM_STATE_CLOSED);
        mqtt_publish_door_operation(duration_s, "close");
        return;
    }
    if (motor_timed_out()) {
        Serial.printf("⚠️ CLOSING timeout (%lus)!\n", MOTOR_TIMEOUT_MS / 1000);
        stop_motors();
        dump_io_state();
        set_error_led(true);
        do_transition(SM_STATE_MOT_ERROR);
    }
}

static void process_closed(const SMInputs& in) {
    // Reality check: UPLIM asserted with no motor means the door is physically open
    // but the SM state is stale — snap to OPEN so commands can be acted on.
    if (in.uplim && !in.motor_up && !in.motor_down) {
        Serial.println("⚠️ CLOSED state but UPLIM active — correcting to OPEN");
        do_transition(SM_STATE_OPEN);
        return;
    }

    // Recognise motor already going up (recover from unexpected restart)
    if (!in.uplim && in.motor_up) { do_transition(SM_STATE_OPENING); return; }

    // ups=1 (level), CRON OPEN, or WEB OPEN triggers open; dns=0 guards conflicting command
    if (!in.uplim && !in.motor_up && !in.motor_down && !in.dns &&
        (in.ups || in.ev_open)) {
        set_motor_up();
    }
}

static void process_mot_error(const SMInputs& in) {
    // CLR_ERROR LOW→HIGH edge with either motor confirmed off → reset to START
    if (in.clr_error_rise && (!in.motor_up || !in.motor_down)) {
        stop_motors();
        set_error_led(false);
        do_transition(SM_STATE_START);
    }
}

static void process_lim_error(const SMInputs& in) {
    // CLR_ERROR LOW→HIGH edge with at least one limit no longer active → reset to START
    if (in.clr_error_rise && (!in.uplim || !in.dnlim)) {
        stop_motors();
        set_error_led(false);
        do_transition(SM_STATE_START);
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void sm_init() {
    Serial.println("\n🚪 Initializing Coop State Machine...");

    sm_mutex = xSemaphoreCreateMutex();
    if (!sm_mutex) {
        Serial.println("❌ Failed to create sm_mutex!");
        while (1) delay(1000);
    }

    if (xSemaphoreTake(io_state_mutex, portMAX_DELAY) == pdTRUE) {
        // Store logical (inverted) values so edge detection is correct from cycle 1
        prev_ups       = (io_get_input_by_name(IN_UPS)       == LOW) ? HIGH : LOW;
        prev_dns       = (io_get_input_by_name(IN_DNS)       == LOW) ? HIGH : LOW;
        prev_clr_error = (io_get_input_by_name(IN_CLR_ERROR) == LOW) ? HIGH : LOW;
        io_set_output_by_name(OUT_ERR, HIGH);  // active-low: HIGH = extinguished at boot
        xSemaphoreGive(io_state_mutex);
    }

    coop_state = SM_STATE_START;
    Serial.println("✅ Coop State Machine initialized → START");
}

void sm_trigger_open() {
    ev_open = true;
    Serial.println("📥 SM: open event queued");
}

void sm_trigger_close() {
    ev_close = true;
    Serial.println("📥 SM: close event queued");
}

void sm_trigger_reset() {
    ev_reset = true;
    Serial.println("📥 SM: reset event queued");
}

const char* sm_state_name(CoopState state) {
    switch (state) {
        case SM_STATE_START:     return "START";
        case SM_STATE_OPENING:   return "OPENING";
        case SM_STATE_OPEN:      return "OPEN";
        case SM_STATE_CLOSING:   return "CLOSING";
        case SM_STATE_CLOSED:    return "CLOSED";
        case SM_STATE_MOT_ERROR: return "MOT_ERROR";
        case SM_STATE_LIM_ERROR: return "LIM_ERROR";
        default:                 return "UNKNOWN";
    }
}

const char* sm_get_state_string() {
    CoopState state;
    if (xSemaphoreTake(sm_mutex, portMAX_DELAY) == pdTRUE) {
        state = coop_state;
        xSemaphoreGive(sm_mutex);
    } else {
        return "ERROR";
    }
    return sm_state_name(state);
}

void state_machine_task(void* parameter) {
    Serial.println("🚪 Coop SM Task started on core " + String(xPortGetCoreID()));

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(50);

    while (1) {
        SMInputs in = read_inputs();

        if (!check_safety(in)) {
            CoopState state;
            if (xSemaphoreTake(sm_mutex, portMAX_DELAY) == pdTRUE) {
                state = coop_state;
                xSemaphoreGive(sm_mutex);
            } else {
                vTaskDelayUntil(&last_wake, period);
                continue;
            }

            switch (state) {
                case SM_STATE_START:     process_start(in);     break;
                case SM_STATE_OPENING:   process_opening(in);   break;
                case SM_STATE_OPEN:      process_open(in);      break;
                case SM_STATE_CLOSING:   process_closing(in);   break;
                case SM_STATE_CLOSED:    process_closed(in);    break;
                case SM_STATE_MOT_ERROR: process_mot_error(in); break;
                case SM_STATE_LIM_ERROR: process_lim_error(in); break;
            }
        }

        vTaskDelayUntil(&last_wake, period);
    }
}
