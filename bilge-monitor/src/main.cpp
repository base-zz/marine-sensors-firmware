// ─────────────────────────────────────────────────────────────
// Marine Bilge Pump Monitor — Main Firmware
// Nordic nRF52840 via Raytac MDBT50Q-1MV2
// Adafruit nRF52 Arduino core / PlatformIO
//
// Sleep: System ON low-power (~4.8uA total)
// Wake:  TMAG5273 INT pin GPIO sense on P0.05
// ─────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <Wire.h>
#include <nrf.h>
#include "marine_packet.h"
#include "device_identity.h"
#include "battery.h"
#include "ble_advertiser.h"
#include "temperature.h"

// ─────────────────────────────────────────────────────────────
// TMAG5273 I2C Configuration
// ─────────────────────────────────────────────────────────────
#define TMAG5273_ADDR 0x35
#define TMAG5273_REG_RESULT 0x02  // Z-axis result register
#define TMAG5273_REG_CONFIG1 0x01 // Configuration register 1
#define TMAG5273_REG_CONFIG2 0x03 // Configuration register 2

// Wake-on-threshold mode value — duty cycles at ~1uA
#define TMAG5273_WAKE_MODE 0x02

// Detection threshold — SET THIS FROM BENCH TESTING
// Start conservative. Validate with Rule 500 pump at groove distance.
// Target: 30-40% of weakest pump peak Z-axis reading.
#define TMAG5273_THRESHOLD 500 // PLACEHOLDER — calibrate on bench

// ─────────────────────────────────────────────────────────────
// Run Tracking — Retention RAM
// Survives System OFF sleep
// ─────────────────────────────────────────────────────────────
#define RUN_MAGIC 0xCAFEBABE

static uint32_t run_start_sec __attribute__((section(".non_init")));
static uint16_t run_field_avg   __attribute__((section(".non_init")));  
static uint16_t run_field_max __attribute__((section(".non_init")));
static uint16_t run_sample_count __attribute__((section(".non_init")));
static uint32_t run_magic __attribute__((section(".non_init")));

void init_run_tracking()
{
    if (run_magic != RUN_MAGIC)
    {
        run_start_sec = 0;
        run_field_avg = 0;
        run_field_max = 0;
        run_sample_count = 0;
        run_magic = RUN_MAGIC;
    }
}

void reset_run_tracking()
{
    run_start_sec    = millis() / 1000;
    run_field_avg    = 0;
    run_field_max    = 0;
    run_sample_count = 0;
}

// ─────────────────────────────────────────────────────────────
// TMAG5273 Functions
// ─────────────────────────────────────────────────────────────

bool tmag5273_init()
{
    // Mandatory 2ms delay before first I2C transaction
    // nRF52840 I2C peripheral needs settling time after boot
    delay(I2C_BOOT_DELAY_MS);

    Wire.begin();

    // Attempt to communicate with TMAG5273 — 3x retry
    for (int attempt = 0; attempt < I2C_RETRY_COUNT; attempt++)
    {
        Wire.beginTransmission(TMAG5273_ADDR);
        uint8_t err = Wire.endTransmission();
        if (err == 0)
        {
            return true; // TMAG5273 responding
        }
        delay(I2C_BOOT_DELAY_MS);
    }

#ifdef DEBUG_SERIAL
    Serial.println("TMAG5273 not responding — false trigger");
#endif
    return false;
}

uint16_t tmag5273_read_z()
{
    // Read Z-axis magnetic field magnitude
    Wire.beginTransmission(TMAG5273_ADDR);
    Wire.write(TMAG5273_REG_RESULT);
    Wire.endTransmission(false);
    Wire.requestFrom(TMAG5273_ADDR, 2);

    if (Wire.available() < 2)
        return 0xFFFF;

    uint16_t high = Wire.read();
    uint16_t low = Wire.read();
    return (high << 8) | low;
}

bool tmag5273_rearm()
{
    // Re-arm wake-on-threshold mode before returning to System OFF
    // TMAG5273 resets on power cycle — must reconfigure every wake
    Wire.beginTransmission(TMAG5273_ADDR);
    Wire.write(TMAG5273_REG_CONFIG1);
    Wire.write(TMAG5273_WAKE_MODE);
    uint8_t err = Wire.endTransmission();
    return (err == 0);
}

// ─────────────────────────────────────────────────────────────
// PMOS Buffer Cap Control
// Assert LOW before BLE TX — connects tantalum caps to V_REG
// Assert HIGH immediately after — disconnects caps
// R2 trickle keeps caps charged during sleep — do NOT remove R2
// ─────────────────────────────────────────────────────────────

void pmos_connect()
{
    digitalWrite(PIN_PMOS_CTRL, LOW);
    delay(1); // Brief settling time
}

void pmos_disconnect()
{
    digitalWrite(PIN_PMOS_CTRL, HIGH);
}


// ─────────────────────────────────────────────────────────────
// RTC Heartbeat Timer
// Uses nRF52840 internal RTC1 to track time between heartbeats
// RTC1 runs at 32768Hz / (prescaler+1)
// Prescaler 4095 → 8Hz tick rate
// 24 hours = 24 * 60 * 60 * 8 = 691200 ticks
// RTC counter is 24-bit → max ~24.2 days. Sufficient.
// ─────────────────────────────────────────────────────────────

#define RTC_PRESCALER           4095
#define RTC_HZ                  8
#define HEARTBEAT_TICKS         (24UL * 60 * 60 * RTC_HZ)

#define HEARTBEAT_MAGIC         0xBEEFCAFE

static uint32_t last_heartbeat_tick __attribute__((section(".non_init")));
static uint32_t heartbeat_magic     __attribute__((section(".non_init")));

void init_heartbeat_timer() {
    // Start RTC1
    NRF_RTC1->PRESCALER   = RTC_PRESCALER;
    NRF_RTC1->TASKS_CLEAR = 1;
    NRF_RTC1->TASKS_START = 1;

    // Initialize retention RAM on first power-on
    if (heartbeat_magic != HEARTBEAT_MAGIC) {
        last_heartbeat_tick = NRF_RTC1->COUNTER;
        heartbeat_magic     = HEARTBEAT_MAGIC;
    }
}

bool heartbeat_due() {
    uint32_t now     = NRF_RTC1->COUNTER;
    uint32_t elapsed = (now - last_heartbeat_tick) & 0x00FFFFFF; // 24-bit wrap
    return elapsed >= HEARTBEAT_TICKS;
}

void update_heartbeat_tick() {
    last_heartbeat_tick = NRF_RTC1->COUNTER;
}

// ─────────────────────────────────────────────────────────────
// System OFF Sleep
// Configures GPIO DETECT on P0.05 for TMAG5273 INT wake
// This is the ONLY valid wake mechanism from System OFF
// ─────────────────────────────────────────────────────────────

void enter_sleep()
{
#ifdef DEBUG_SERIAL
    Serial.println("Entering System ON low-power sleep");
    Serial.flush();
    delay(10);
#endif

    // Ensure PMOS is disconnected
    pmos_disconnect();

    // Configure TMAG5273 INT pin for GPIO sense wake
    nrf_gpio_cfg_sense_input(
        PIN_TMAG_INT,
        NRF_GPIO_PIN_PULLUP,
        NRF_GPIO_PIN_SENSE_LOW
    );

    // Clear pending PORT events to prevent spurious wake
    NRF_GPIOTE->EVENTS_PORT = 0;

    // System ON low-power sleep
    // CPU halts, LFCLK and RTC keep running
    // Wakes on TMAG5273 INT going LOW
    __WFE();
    __SEV();
    __WFE();
}

// ─────────────────────────────────────────────────────────────
// Packet Transmission Helpers
// ─────────────────────────────────────────────────────────────

void transmit_pump_running(uint16_t field_now)
{
    MarinePacket packet;
    packet_init(&packet, EVENT_PUMP_RUNNING);

    uint32_t elapsed = (millis() / 1000) - run_start_sec;
    uint16_t field_avg = run_sample_count > 0
                             ? (uint16_t)(run_field_avg)
                             : field_now;

    packet.data.pump_running.elapsed_secs = (uint16_t)elapsed;
    packet.data.pump_running.field_strength_now = field_now;
    packet.data.pump_running.field_strength_avg = field_avg;
    packet.data.pump_running.field_strength_max = run_field_max;

    pmos_connect();
    ble_transmit(&packet);
    pmos_disconnect();
}

void transmit_pump_complete()
{
    MarinePacket packet;
    packet_init(&packet, EVENT_PUMP_RUN_COMPLETE);

    uint32_t duration = (millis() / 1000) - run_start_sec;
    uint16_t field_avg = run_sample_count > 0
                             ? (uint16_t)(run_field_avg)
                             : 0;

    packet.data.pump_complete.duration_secs = (uint16_t)duration;
    packet.data.pump_complete.field_strength_avg = field_avg;
    packet.data.pump_complete.field_strength_max = run_field_max;
    packet.data.pump_complete.reserved = 0;

    pmos_connect();
    ble_transmit(&packet);
    pmos_disconnect();
}

void transmit_heartbeat()
{
    MarinePacket packet;
    packet_init(&packet, EVENT_BILGE_HEARTBEAT);
    packet.data.simple.temperature_c = temperature_read_c();

    pmos_connect();
    ble_transmit(&packet);
    pmos_disconnect();

    // Open DFU window after heartbeat
    // Allows Nexus to push firmware updates
    ble_start_dfu_window();
}

// ─────────────────────────────────────────────────────────────
// Main State Machine
// ─────────────────────────────────────────────────────────────

void setup()
{
#ifdef DEBUG_SERIAL
    Serial.begin(115200);
    delay(500);
    Serial.println("=== Bilge Monitor Boot ===");
#endif

    // Initialize retention RAM — must be first
    init_sequence_number();
    init_run_tracking();
    init_heartbeat_timer();
    
    // ── READ RESET REASON ─────────────────────────────────────
    // Distinguish System OFF GPIO DETECT wake from other resets
    // Bit 16 = System OFF wake (legitimate pump or RTC wake)
    // Bit 0  = PIN reset, Bit 3 = soft reset, Bit 2 = watchdog
    uint32_t reset_reason = NRF_POWER->RESETREAS;
    NRF_POWER->RESETREAS = 0xFFFFFFFF;  // Clear all flags

    bool woke_from_system_off = (reset_reason & 0x00010000);

    // Initialize hardware
    pinMode(PIN_PMOS_CTRL, OUTPUT);
    pmos_disconnect();

    battery_init();
    temperature_init();
    ble_init();

    // ── UNEXPECTED RESET ─────────────────────────────────────
    if (!woke_from_system_off)
    {
        // Not a System OFF wake — brownout, watchdog, PIN reset etc
        // Send heartbeat so Nexus knows the device restarted
        // Then re-arm and sleep
#ifdef DEBUG_SERIAL
        Serial.println("Non-SystemOFF reset — sending heartbeat");
        Serial.flush();
        delay(10);
#endif
        transmit_heartbeat();
        if (tmag5273_init()) tmag5273_rearm();
        enter_sleep();
        return;
    }

    // ── TMAG5273 INIT ─────────────────────────────────────────
    if (!tmag5273_init())
    {
        // TMAG5273 not responding — do NOT attempt rearm
        // Sleep and wait for next wake — avoids battery-killing loop
#ifdef DEBUG_SERIAL
        Serial.println("TMAG5273 failed — sleeping without rearm");
        Serial.flush();
        delay(10);
#endif
        enter_sleep();
        return;
    }

    // ── READ FIELD STRENGTH ───────────────────────────────────
    uint16_t field = tmag5273_read_z();

    if (field == 0xFFFF)
    {
        // I2C read failed — error sentinel, not a valid reading
        // Treat as false trigger
        tmag5273_rearm();
        enter_sleep();
        return;
    }

#ifdef DEBUG_SERIAL
    Serial.print("Field strength: ");
    Serial.println(field);
#endif

    // ── BELOW THRESHOLD — HEARTBEAT ──────────────────────────
    if (field < TMAG5273_THRESHOLD)
    {
        // Below threshold — false trigger or wake with no pump activity
        // Send heartbeat if 24 hours have passed
        if (heartbeat_due()) {
            transmit_heartbeat();
            update_heartbeat_tick();
        }
        tmag5273_rearm();
        enter_sleep();
        return;
    }

    // ── PUMP IS RUNNING ───────────────────────────────────────
#ifdef DEBUG_SERIAL
    Serial.println("Pump detected — monitoring");
#endif

    reset_run_tracking();
    transmit_pump_running(field);

    // Running average — no overflow possible
    run_sample_count++;
    run_field_avg = run_field_avg + (field - run_field_avg) / run_sample_count;
    if (field > run_field_max) run_field_max = field;

    uint32_t last_report_ms = millis();

// ── MONITORING LOOP ───────────────────────────────────────
    while (true)
    {
        delay(1000);

        field = tmag5273_read_z();

        // Skip bad reads — don't update stats or break on error
        if (field == 0xFFFF) continue;

        // Update running average — no overflow
        run_sample_count++;
        run_field_avg = run_field_avg + (field - run_field_avg) / run_sample_count;
        if (field > run_field_max) run_field_max = field;

        // Pump stopped
        if (field < TMAG5273_THRESHOLD)
        {
#ifdef DEBUG_SERIAL
            Serial.println("Pump stopped");
#endif
            break;
        }

        // Send heartbeat if 24 hours due even during long pump run
        if (heartbeat_due()) {
            transmit_heartbeat();
            update_heartbeat_tick();
        }

        // Report every 60 seconds
        if ((millis() - last_report_ms) >= (RUNNING_REPORT_INTERVAL_SEC * 1000UL))
        {
            transmit_pump_running(field);
            last_report_ms = millis();
        }
    }

    // ── PUMP STOPPED ──────────────────────────────────────────
    transmit_pump_complete();
    tmag5273_rearm();
    enter_sleep();
}

void loop()
{
    // Never reached — setup() ends with enter_sleep()
    // After System OFF wake, nRF52840 reboots and calls setup() again
}