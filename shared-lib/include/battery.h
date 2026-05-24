#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────────────────────
// Battery Voltage Measurement
// Uses nRF52840 internal SAADC
// Returns voltage in millivolts
// ─────────────────────────────────────────────────────────────

void     battery_init();
uint16_t battery_read_mv();
bool     battery_is_low();