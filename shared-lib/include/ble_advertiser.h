#pragma once

#include "marine_packet.h"

void ble_init();
void ble_transmit(MarinePacket* packet);
void ble_start_dfu_window();
void ble_stop();
void packet_init(MarinePacket* packet, EventType event_type);