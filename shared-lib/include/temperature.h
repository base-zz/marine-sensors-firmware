#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────────────────────
// nRF52840 Die Temperature Sensor
//
// The nRF52840 has a built-in temperature sensor accessible
// via the TEMP peripheral. It reads in units of 0.25 degrees C
// so we divide by 4 to get whole degrees.
//
// Accuracy: +/- 5 degrees C — adequate for trend logging.
// Cost: negligible — a few microseconds, no extra components.
// ─────────────────────────────────────────────────────────────

void temperature_init();
int8_t temperature_read_c();