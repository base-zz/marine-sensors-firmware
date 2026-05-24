#include "temperature.h"
#include "nrf.h"

void temperature_init() {}

int8_t temperature_read_c() {
    NRF_TEMP->TASKS_START = 1;
    while (NRF_TEMP->EVENTS_DATARDY == 0) {}
    NRF_TEMP->EVENTS_DATARDY = 0;
    int32_t raw = NRF_TEMP->TEMP;
    NRF_TEMP->TASKS_STOP = 1;
    return (int8_t)(raw / 4);
}