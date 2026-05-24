#include "device_identity.h"

// Prefer the vendor header when available; otherwise provide a minimal
// fallback so this translation unit can be parsed/compiled in non-NRF
// environments (e.g. editor static analysis).
#if defined(__has_include)
#  if __has_include(<nrf.h>)
#    include <nrf.h>
#  elif __has_include("nrf.h")
#    include "nrf.h"
#  else
#    include <stdint.h>

// Minimal fallback definitions used only when the Nordic header is not
// available. These allow getting a device id to compile; on real hardware
// the vendor-provided nrf.h will be used instead.
typedef struct {
    volatile uint32_t DEVICEID[2];
} NRF_FICR_Type;

// Provide a mock instance so code links in non-target builds. The real
// symbol will be provided by the vendor headers/startup when building for
// the device.
static NRF_FICR_Type _mock_nrf_ficr = {{0}};
NRF_FICR_Type* NRF_FICR = &_mock_nrf_ficr;
#  endif
#else
#  include <nrf.h>
#endif

uint32_t get_device_id() {
    return NRF_FICR->DEVICEID[0];
}

constexpr uint32_t RETENTION_MAGIC = 0xDEADBEEF;

static uint8_t  seq_number __attribute__((section(".non_init")));
static uint32_t seq_magic  __attribute__((section(".non_init")));

void init_sequence_number() {
    if (seq_magic != RETENTION_MAGIC) {
        seq_number = 0;
        seq_magic  = RETENTION_MAGIC;
    }
}

uint8_t next_sequence_number() {
    seq_number++;
    return seq_number;
}