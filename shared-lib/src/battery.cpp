#include "battery.h"
#include "marine_packet.h"
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────
// Battery Voltage Measurement
//
// The nRF52840 SAADC reads the battery voltage through a
// resistor divider on the PCB. The Adafruit nRF52 core
// exposes this via standard analogRead().
//
// Li-SOCl2 discharge curve is very flat — voltage stays near
// 3.6V for most of battery life then drops sharply near end.
// We report millivolts so Nexus can plot the curve over time.
// ─────────────────────────────────────────────────────────────

// ADC reference voltage in millivolts
// nRF52840 internal reference is 0.6V with 1/6 gain = 3.6V range
// #define ADC_REF_MV          3600
// #define ADC_SAMPLES         4       // Average multiple readings

constexpr uint16_t ADC_REF_MV  = 3600; 
constexpr uint8_t  ADC_SAMPLES = 4;     // Average multiple readings

void battery_init() {
    // Set ADC resolution
    analogReadResolution(10);   // 10-bit = 0-1023
}

uint16_t battery_read_mv() {
    // Take multiple readings and average to reduce noise
    uint32_t total = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        total += analogRead(BATTERY_ADC_PIN);
        delay(1);
    }
    uint32_t avg = total / ADC_SAMPLES;

    // Convert ADC reading to millivolts
    // mv = (reading / max_reading) * reference_mv
    uint16_t mv = (uint16_t)((avg * ADC_REF_MV) / 1024);

    return mv;
}

bool battery_is_low() {
    return battery_read_mv() < BATTERY_MV_LOW;
}