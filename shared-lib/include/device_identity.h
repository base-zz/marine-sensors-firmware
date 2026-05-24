#pragma once

#include <stdint.h>

uint32_t get_device_id();
void     init_sequence_number();
uint8_t  next_sequence_number();