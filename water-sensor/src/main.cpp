// ─────────────────────────────────────────────────────────────
// Marine Water Ingress Sensor — Main Firmware
// Nordic nRF52840 via Raytac MDBT50Q-1MV2
// Adafruit nRF52 Arduino core / PlatformIO
//
// Sleep: System ON low-power (~1.8uA total)
// Wake:  RTC alarm every 10 seconds
// ─────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <nrf.h>
#include "marine_packet.h"
#include "device_identity.h"
#include "battery.h"
#include "ble_advertiser.h"
#include "temperature.h"
#include "water_config.h"

// // ─────────────────────────────────────────────────────────────
// // Electrode Pin Configuration
// // AC differential excitation — mandatory to prevent electrolysis
// // Both pins driven OUTPUT LOW during sleep — not Hi-Z
// // ─────────────────────────────────────────────────────────────
// #define PIN_ELECTRODE_A     2       // P0.02 — excitation drive
// #define PIN_ELECTRODE_B     3       // P0.03 — sense + reverse drive

// // ─────────────────────────────────────────────────────────────
// // Wet Detection Threshold
// // SET FROM BENCH TESTING
// // Test with distilled water, tap water, salt water
// // Must NOT trigger on condensation alone
// // ─────────────────────────────────────────────────────────────
// #define WET_THRESHOLD       512     // PLACEHOLDER — calibrate on bench

// // ─────────────────────────────────────────────────────────────
// // RTC Configuration
// // RTC1 — 32768Hz / (prescaler+1)
// // Prescaler 4095 → 8Hz tick rate
// // 10 seconds = 80 ticks
// // 24 hours = 691200 ticks
// // ─────────────────────────────────────────────────────────────
// #define RTC_PRESCALER               4095
// #define RTC_HZ                      8
// #define SENSE_INTERVAL_TICKS        (10 * RTC_HZ)       // 10 seconds
// #define HEARTBEAT_TICKS             (24UL * 60 * 60 * RTC_HZ)
// #define WATER_REPORT_TICKS          (60 * RTC_HZ)       // 60 seconds

// // ─────────────────────────────────────────────────────────────
// // State — Retention RAM
// // Survives System ON sleep
// // ─────────────────────────────────────────────────────────────
// #define STATE_MAGIC     0xA5B6C7D8

// ─────────────────────────────────────────────────────────────
// Electrode Pin Configuration- defined in marine_packet.h to be used in other files
// AC differential excitation — mandatory to prevent electrolysis
// Both pins driven OUTPUT LOW during sleep — not Hi-Z
// ─────────────────────────────────────────────────────────────
// constexpr uint8_t PIN_ELECTRODE_A     = 2;       // P0.02 — excitation drive
// constexpr uint8_t PIN_ELECTRODE_B     = 3;       // P0.03 — sense + reverse drive

// ─────────────────────────────────────────────────────────────
// Wet Detection Threshold
// ─────────────────────────────────────────────────────────────
constexpr uint16_t WET_THRESHOLD      = 512;     // Calibrate on bench (fits in 16-bit)

// ─────────────────────────────────────────────────────────────
// RTC Configuration
// ─────────────────────────────────────────────────────────────
constexpr uint16_t RTC_PRESCALER      = 4095;
constexpr uint8_t  RTC_HZ             = 8;

// The compiler automatically calculates these math operations at compile-time
constexpr uint8_t  SENSE_INTERVAL_TICKS = 10 * RTC_HZ;       // 80 ticks (fits in 8-bit)
constexpr uint32_t HEARTBEAT_TICKS      = 24UL * 60 * 60 * RTC_HZ; // 691,200 ticks (needs 32-bit)
constexpr uint16_t WATER_REPORT_TICKS   = 60 * RTC_HZ;       // 480 ticks (needs 16-bit)

// ─────────────────────────────────────────────────────────────
// State — Retention RAM
// ─────────────────────────────────────────────────────────────
constexpr uint32_t STATE_MAGIC        = 0xA5B6C7D8; // Another excellent 32-bit Hexsignature

static uint32_t state_magic         __attribute__((section(".non_init")));
static uint8_t  is_wet              __attribute__((section(".non_init")));
static uint32_t wet_start_tick      __attribute__((section(".non_init")));
static uint32_t last_report_tick    __attribute__((section(".non_init")));
static uint32_t last_heartbeat_tick __attribute__((section(".non_init")));

void init_state() {
    if (state_magic != STATE_MAGIC) {
        is_wet              = 0;
        wet_start_tick      = 0;
        last_report_tick    = 0;
        last_heartbeat_tick = 0;
        state_magic         = STATE_MAGIC;
    }
}

// ─────────────────────────────────────────────────────────────
// RTC Functions
// ─────────────────────────────────────────────────────────────

void rtc_init() {
    NRF_RTC0->PRESCALER   = RTC_PRESCALER;
    NRF_RTC0->TASKS_CLEAR = 1;
    NRF_RTC0->TASKS_START = 1;
}

uint32_t rtc_now() {
    return NRF_RTC0->COUNTER;
}

uint32_t rtc_elapsed(uint32_t since) {
    return (rtc_now() - since) & 0x00FFFFFF;  // 24-bit wrap
}

// ─────────────────────────────────────────────────────────────
// Electrode Control
// ─────────────────────────────────────────────────────────────

void electrodes_sleep() {
    // Both pins OUTPUT LOW during sleep
    // Zero differential voltage = zero electrolysis
    // Prevents floating input cross-talk current
    nrf_gpio_cfg_output(PIN_ELECTRODE_A);
    nrf_gpio_cfg_output(PIN_ELECTRODE_B);
    nrf_gpio_pin_clear(PIN_ELECTRODE_A);
    nrf_gpio_pin_clear(PIN_ELECTRODE_B);
}

uint16_t electrodes_sense() {
    // AC differential excitation — two phase measurement
    // Phase 1: A HIGH, B LOW → current flows E1 to E2
    nrf_gpio_cfg_output(PIN_ELECTRODE_A);
    nrf_gpio_cfg_output(PIN_ELECTRODE_B);
    nrf_gpio_pin_set(PIN_ELECTRODE_A);
    nrf_gpio_pin_clear(PIN_ELECTRODE_B);
    delay(1);
    uint16_t phase1 = analogRead(PIN_ELECTRODE_B);

    // Phase 2: A LOW, B HIGH → current flows E2 to E1 (REVERSED)
    nrf_gpio_pin_clear(PIN_ELECTRODE_A);
    nrf_gpio_pin_set(PIN_ELECTRODE_B);
    delay(1);
    uint16_t phase2 = analogRead(PIN_ELECTRODE_A);

    // Return both pins to OUTPUT LOW before sleep
    electrodes_sleep();

    // Average both phases — cancels DC offset artifacts
    return (phase1 + phase2) / 2;
}

// ─────────────────────────────────────────────────────────────
// Sleep
// System ON low-power — CPU halts, RTC keeps running
// Wakes on RTC compare event via software interrupt
// ─────────────────────────────────────────────────────────────

void enter_sleep() {
#ifdef DEBUG_SERIAL
    Serial.println("Sleeping");
    Serial.flush();
    delay(5);
#endif

    // Ensure electrodes are LOW before sleep
    electrodes_sleep();

uint32_t next_wake = (rtc_now() + SENSE_INTERVAL_TICKS) & 0x00FFFFFF;
    NRF_RTC0->CC[0]            = next_wake;
    NRF_RTC0->EVTENSET         = RTC_EVTENSET_COMPARE0_Msk;
    NRF_RTC0->INTENSET         = RTC_INTENSET_COMPARE0_Msk;
    NRF_RTC0->EVENTS_COMPARE[0] = 0;

    NVIC_SetPriority(RTC0_IRQn, 7);
    NVIC_ClearPendingIRQ(RTC0_IRQn);
    NVIC_EnableIRQ(RTC0_IRQn);

    __WFE();
    __SEV();
    __WFE();

    NVIC_DisableIRQ(RTC0_IRQn);
    NRF_RTC0->INTENCLR         = RTC_INTENCLR_COMPARE0_Msk;
    NRF_RTC0->EVENTS_COMPARE[0] = 0;
}

// RTC1 interrupt handler — just wake, do nothing
extern "C" void RTC0_IRQHandler() {
    NRF_RTC0->EVENTS_COMPARE[0] = 0;
}

// ─────────────────────────────────────────────────────────────
// Packet Transmission Helpers
// ─────────────────────────────────────────────────────────────

void transmit_water_detected() {
    MarinePacket packet;
    packet_init(&packet, EVENT_WATER_DETECTED);
    packet.data.simple.temperature_c = temperature_read_c();
    ble_transmit(&packet);
}

void transmit_water_present() {
    MarinePacket packet;
    packet_init(&packet, EVENT_WATER_PRESENT);
    packet.data.water_present.temperature_c = temperature_read_c();
    packet.data.water_present.elapsed_secs  =
        (uint16_t)(rtc_elapsed(wet_start_tick) / RTC_HZ);
    ble_transmit(&packet);
}

void transmit_water_cleared() {
    MarinePacket packet;
    packet_init(&packet, EVENT_WATER_CLEARED);
    packet.data.water_cleared.temperature_c = temperature_read_c();
    packet.data.water_cleared.duration_secs =
        (uint16_t)(rtc_elapsed(wet_start_tick) / RTC_HZ);
    ble_transmit(&packet);
}

void transmit_heartbeat() {
    MarinePacket packet;
    packet_init(&packet, EVENT_WATER_HEARTBEAT);
    packet.data.simple.temperature_c = temperature_read_c();
    ble_transmit(&packet);

    // Open DFU window after heartbeat
    ble_start_dfu_window();
}

// ─────────────────────────────────────────────────────────────
// Main State Machine
// ─────────────────────────────────────────────────────────────

void setup() {
#ifdef DEBUG_SERIAL
    Serial.begin(115200);
    delay(500);
    Serial.println("=== Water Sensor Boot ===");
#endif

    // Initialize retention RAM — must be first
    init_sequence_number();
    init_state();

    // Initialize hardware
    battery_init();
    temperature_init();
    ble_init();
    analogReadResolution(10);

    // Start RTC
    rtc_init();

    // Electrodes LOW during init
    electrodes_sleep();

    // ── MAIN SENSE LOOP ───────────────────────────────────────
    while (true) {

        // Read conductivity via AC differential excitation
        uint16_t conductivity = electrodes_sense();
        bool currently_wet = (conductivity > WET_THRESHOLD);

#ifdef DEBUG_SERIAL
        Serial.print("Conductivity: ");
        Serial.print(conductivity);
        Serial.print(" Wet: ");
        Serial.println(currently_wet ? "YES" : "NO");
#endif

        // ── WET PATH ─────────────────────────────────────────
        if (currently_wet) {
            if (!is_wet) {
                // Transition dry → wet
                is_wet           = 1;
                wet_start_tick   = rtc_now();
                last_report_tick = rtc_now();
                transmit_water_detected();
            } else {
                // Already wet — send WATER_PRESENT every 60 seconds
                if (rtc_elapsed(last_report_tick) >= WATER_REPORT_TICKS) {
                    transmit_water_present();
                    last_report_tick = rtc_now();
                }
            }
        }

        // ── DRY PATH ─────────────────────────────────────────
        else {
            if (is_wet) {
                // Transition wet → dry
                is_wet = 0;
                transmit_water_cleared();
            }

            // Send heartbeat if 24 hours have passed
            if (rtc_elapsed(last_heartbeat_tick) >= HEARTBEAT_TICKS) {
                transmit_heartbeat();
                last_heartbeat_tick = rtc_now();
            }
        }

        // Sleep until next sense cycle
        enter_sleep();
    }
}

void loop() {
    // Never reached — main loop is inside setup()
}