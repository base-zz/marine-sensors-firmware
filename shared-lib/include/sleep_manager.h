#pragma once

#include <Arduino.h>

enum class WakeReason : uint8_t {
    UNKNOWN      = 0x00,
    PUMP_TRIGGER = 0x01,  // Woke up from TMAG5273 interrupt pin falling edge
    RTC_TICK     = 0x02   // Woke up from the 1-minute internal RTC alarm
};

/**
 * @brief Configures the internal hardware RTC1 peripheral to issue a low-power interrupt
 * exactly every 60 seconds. Bypasses and preserves standard Arduino millis() tracking.
 */
void sleep_manager_init(void);

/**
 * @brief Places the nRF52840 into a System ON Low-Power sleep state.
 * Instruction execution halts here until a GPIO event or the 1-minute RTC tick fires.
 * @return WakeReason The verified source that forced the processor to resume execution.
 */
WakeReason sleep_enter_low_power(void);

/**
 * @brief Evaluates whether the background timer tracking has reached a full 24-hour period.
 * @return true if 1440 minutes (24 hours) have completely elapsed since the last reset.
 */
bool sleep_check_heartbeat_timeout(void);

/**
 * @brief Resets the 24-hour retention counter back to zero following a successful broadcast.
 */
void sleep_reset_heartbeat_timer(void);