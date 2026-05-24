#pragma once

#include "marine_packet.h"

void ble_init();
void ble_transmit(MarinePacket* packet);
void ble_start_dfu_window();
void ble_stop();
void packet_init(MarinePacket* packet, EventType event_type);

// Override elapsed_seconds in packet_init() with a real RTC-based uptime.
// Call once after init_uptime() in setup() — bilge monitor only.
// The water sensor does NOT call this — it uses millis() which is valid
// because it never fully reboots (System ON sleep).
void ble_set_uptime_override(uint32_t seconds);