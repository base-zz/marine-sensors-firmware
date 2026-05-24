#include "sleep_manager.h"

// ─────────────────────────────────────────────────────────────
// Non-Initialized Retention RAM Blocks
// ─────────────────────────────────────────────────────────────
// Placed in ".non_init" memory space so variables bypass the 
// standard Adafruit bootloader clearing cycle during warm wake events.
static uint16_t m_minutes_since_heartbeat __attribute__((section(".non_init")));
static uint32_t m_sleep_canary            __attribute__((section(".non_init")));

// #define SLEEP_CANARY_VAL    0x7BEEF707
// #define MINUTES_IN_24H      1440
// #define TMAG_INT_PIN        5 // Maps to P0.05 on your physical layout

// A 32-bit hex value needs a uint32_t
constexpr uint32_t SLEEP_CANARY_VAL = 0x7BEEF707;

// 1440 easily fits inside a standard 16-bit integer (max 65,535)
constexpr uint16_t MINUTES_IN_24H   = 1440;

// Pin numbers are small and never change, an 8-bit integer is perfect
constexpr uint8_t  TMAG_INT_PIN     = 5;

static volatile bool m_rtc_tick_occurred = false;

// Hardware Vector Hook for Nordic RTC1 Events
extern "C" void RTC1_IRQHandler(void) {
    if (NRF_RTC1->EVENTS_COMPARE[0] == 1) {
        NRF_RTC1->EVENTS_COMPARE[0] = 0; // Clear the interrupt flag
        NRF_RTC1->TASKS_CLEAR = 1;       // Reset hardware counter register to 0
        m_rtc_tick_occurred = true;
    }
}

void sleep_manager_init(void) {
    // Check safety canary to initialize variables if this is a cold boot (battery insertion)
    if (m_sleep_canary != SLEEP_CANARY_VAL) {
        m_sleep_canary = SLEEP_CANARY_VAL;
        m_minutes_since_heartbeat = 0;
    }

    // Configure the TMAG5273 interrupt line with standard internal pull-up
    pinMode(TMAG_INT_PIN, INPUT_PULLUP);

    // ─────────────────────────────────────────────────────────────
    // Hardware RTC1 Register Configuration (60-Second Tick)
    // ─────────────────────────────────────────────────────────────
    // Prescaler formula: frequency = 32768 / (PRESCALER + 1)
    // A prescaler value of 31 provides an explicit 1024Hz internal clock rate.
    NRF_RTC1->PRESCALER = 31; 
    
    // 1024Hz * 60 seconds = 61440 counter target cycles
    NRF_RTC1->CC[0] = 61440; 
    
    // Direct the RTC peripheral to assert an interrupt vector on Compare 0 events
    NRF_RTC1->INTENSET = RTC_INTENSET_COMPARE0_Msk;
    
    // Enable the event vector inside the Nested Vectored Interrupt Controller (NVIC)
    NVIC_SetPriority(RTC1_IRQn, 3);
    NVIC_EnableIRQ(RTC1_IRQn);
    
    // Fire the clock task
    NRF_RTC1->TASKS_START = 1;
}

WakeReason sleep_enter_low_power(void) {
    m_rtc_tick_occurred = false;

    // Clear underlying ARM Cortex internal system event flags to prevent immediate false wakeups
    __SEV(); 
    __WFE();

    // Fall into a controlled Wait-For-Event polling loop
    while (!m_rtc_tick_occurred && digitalRead(TMAG_INT_PIN) == HIGH) {
        // Enforce Nordic DCDC subsystem power routing optimization
        NRF_POWER->TASKS_LOWPWR = 1;
        __WFE(); 
    }

    // Evaluate exactly why execution expanded past the sleep block
    if (m_rtc_tick_occurred) {
        m_rtc_tick_occurred = false;
        m_minutes_since_heartbeat++; // Safely log background time progression
        return WakeReason::RTC_TICK;
    }

    if (digitalRead(TMAG_INT_PIN) == LOW) {
        return WakeReason::PUMP_TRIGGER;
    }

    return WakeReason::UNKNOWN;
}

bool sleep_check_heartbeat_timeout(void) {
    return (m_minutes_since_heartbeat >= MINUTES_IN_24H);
}

void sleep_reset_heartbeat_timer(void) {
    m_minutes_since_heartbeat = 0;
}