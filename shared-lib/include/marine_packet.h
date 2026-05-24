#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────────────────────
// Marine Sensor BLE Packet Definition
// All sensors use this common 23-byte packet format
// Transmitted as BLE Manufacturer Specific Data
// ─────────────────────────────────────────────────────────────

// BLE Company ID — 0xFFFF for development
// Register with Bluetooth SIG before production
#define COMPANY_ID 0xFFFF

// Nexus product marker — 0x4E = ASCII 'N' for Nexus
// Always written as byte 15 (first byte of sensor payload union)
// Provides two-factor device identification alongside COMPANY_ID
// to filter out rogue 0xFFFF devices from third-party hardware.
#define NEXUS_MARKER 0x4E

// Firmware version — high nibble major, low nibble minor
// 0x10 = v1.0
#define FIRMWARE_VERSION 0x10 // v1.0

// ─────────────────────────────────────────────────────────────
// Event Types
// 0x01-0x03 = Bilge Pump Monitor
// 0x10-0x13 = Water Ingress Sensor
// ─────────────────────────────────────────────────────────────
typedef enum {
    // Bilge Pump Monitor events
    EVENT_PUMP_RUNNING      = 0x01,  // Pump active — sent every 60s
    EVENT_PUMP_RUN_COMPLETE = 0x02,  // Pump stopped — full run summary
    EVENT_BILGE_HEARTBEAT   = 0x03,  // Daily health check

    // Water Ingress Sensor events
    EVENT_WATER_DETECTED    = 0x10,  // Water first detected
    EVENT_WATER_PRESENT     = 0x11,  // Water still present — sent every 60s
    EVENT_WATER_CLEARED     = 0x12,  // Water gone — event summary
    EVENT_WATER_HEARTBEAT   = 0x13,  // Daily health check
} EventType;

// ─────────────────────────────────────────────────────────────
// Packet Structure — 23 bytes total
// Fits within 31-byte BLE advertisement payload limit
// ─────────────────────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint16_t company_id;       // Bytes 0-1:  BLE manufacturer ID
    uint32_t device_id;        // Bytes 2-5:  nRF52840 hardware UID (32-bit)
    uint8_t  event_type;       // Byte  6:    EventType enum value
    uint8_t  sequence_number;  // Byte  7:    0-255 wrapping counter
    uint32_t elapsed_seconds;  // Bytes 8-11: Seconds since first boot
    uint16_t battery_mv;       // Bytes 12-13: Battery voltage in millivolts
    uint8_t  firmware_version; // Byte  14:   High nibble=major, low=minor

    // Bytes 15-22: Sensor-specific data (8 bytes)
    // Byte 15 is ALWAYS nexus_marker = NEXUS_MARKER (0x4E)
    // This provides two-factor identification alongside COMPANY_ID
    union {

        // Bilge monitor — PUMP_RUNNING
        // nexus_marker + elapsed_secs + field_now + field_max = 1+2+2+2 = 7 bytes + 1 reserved
        struct __attribute__((packed)) {
            uint8_t  nexus_marker;       // Byte 15: Always 0x4E
            uint16_t elapsed_secs;       // Bytes 16-17: Seconds pump has been running
            uint16_t field_strength_now; // Bytes 18-19: Current TMAG5273 Z-axis reading
            uint16_t field_strength_max; // Bytes 20-21: Peak since pump started
            uint8_t  reserved;           // Byte 22: Reserved for future use
        } pump_running;

        // Bilge monitor — PUMP_RUN_COMPLETE
        // nexus_marker + duration + field_avg + field_max = 1+2+2+2 = 7 bytes + 1 reserved
        struct __attribute__((packed)) {
            uint8_t  nexus_marker;       // Byte 15: Always 0x4E
            uint16_t duration_secs;      // Bytes 16-17: Total run duration in seconds
            uint16_t field_strength_avg; // Bytes 18-19: Average over entire run
            uint16_t field_strength_max; // Bytes 20-21: Peak over entire run
            uint8_t  reserved;           // Byte 22: Reserved for future use
        } pump_complete;

        // Water sensor — WATER_PRESENT
        // nexus_marker + temperature_c + elapsed_secs = 1+1+2 = 4 bytes + 4 reserved
        struct __attribute__((packed)) {
            uint8_t  nexus_marker;   // Byte 15: Always 0x4E
            int8_t   temperature_c;  // Byte 16: Die temp in degrees C (signed)
            uint16_t elapsed_secs;   // Bytes 17-18: Seconds water has been present
            uint8_t  reserved[4];    // Bytes 19-22: Reserved
        } water_present;

        // Water sensor — WATER_CLEARED
        // nexus_marker + temperature_c + duration_secs = 1+1+2 = 4 bytes + 4 reserved
        struct __attribute__((packed)) {
            uint8_t  nexus_marker;   // Byte 15: Always 0x4E
            int8_t   temperature_c;  // Byte 16: Die temp at time of clearing (signed)
            uint16_t duration_secs;  // Bytes 17-18: Total wet duration in seconds
            uint8_t  reserved[4];    // Bytes 19-22: Reserved
        } water_cleared;

        // Heartbeat and WATER_DETECTED — nexus_marker + temperature only
        struct __attribute__((packed)) {
            uint8_t nexus_marker;  // Byte 15: Always 0x4E
            int8_t  temperature_c; // Byte 16: Die temp in degrees C (signed)
            uint8_t reserved[6];   // Bytes 17-22: Reserved
        } simple;

        // Raw access to all 8 sensor payload bytes
        uint8_t raw[8];
    } data;

} MarinePacket;

// Verify packet is exactly 23 bytes at compile time
static_assert(sizeof(MarinePacket) == 23, "MarinePacket must be 23 bytes");

// ─────────────────────────────────────────────────────────────
// Timing Constants
// ─────────────────────────────────────────────────────────────
#define HEARTBEAT_INTERVAL_SEC      (24 * 60 * 60)  // 24 hours
#define RUNNING_REPORT_INTERVAL_SEC 60               // 60 seconds
#define DFU_WINDOW_SEC              10               // OTA update window
#define TX_REPEAT_COUNT             3                // Transmit each packet 3x
#define TX_REPEAT_DELAY_MS          10               // 10ms between repeats
#define I2C_BOOT_DELAY_MS           2                // Mandatory I2C settling
#define I2C_RETRY_COUNT             3                // Retries on I2C failure

// ─────────────────────────────────────────────────────────────
// Pin Definitions — matches PCB netlist
// ─────────────────────────────────────────────────────────────
#define PIN_TMAG_INT    5  // P0.05 — TMAG5273 interrupt
#define PIN_ELECTRODE_B 3  // P0.03 — Sense (water sensor)

// ─────────────────────────────────────────────────────────────
// Battery ADC
// ─────────────────────────────────────────────────────────────
#define BATTERY_ADC_PIN  A0    // Analog pin for battery voltage divider
#define BATTERY_MV_FULL  3600  // Li-SOCl2 nominal voltage
#define BATTERY_MV_LOW   3200  // Alert threshold