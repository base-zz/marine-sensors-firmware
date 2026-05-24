#ifdef __INTELLISENSE__
#include <cstdint>
#include <cstring>

struct BLEAdvertisingClass {
    void clearData() {}
    void addManufacturerData(const void*, uint8_t) {}
    void setType(uint8_t) {}
    void start(uint32_t) {}
    void stop() {}
};

struct BluefruitClass {
    BLEAdvertisingClass Advertising;
    void begin() {}
    void setTxPower(int8_t) {}
    void setName(const char*) {}
};

extern BluefruitClass Bluefruit;

struct HardwareSerial {
    void print(const char*, int = 0) {}
    void print(unsigned long, int = 0) {}
    void println(const char*) {}
};

extern HardwareSerial Serial;

static inline unsigned long millis() { return 0; }
static inline void delay(unsigned long) {}

static constexpr uint8_t BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED = 0;
static constexpr uint8_t BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED = 1;
#else
#include <Arduino.h>
#include <bluefruit.h>
#endif

#include "ble_advertiser.h"
#include "device_identity.h"
#include "battery.h"

static uint32_t boot_time_sec = 0;

void ble_init() {
    Bluefruit.begin();
    Bluefruit.setTxPower(0);
    Bluefruit.setName("MarineSensor");
    boot_time_sec = millis() / 1000;
}

void packet_init(MarinePacket* packet, EventType event_type) {
    memset(packet, 0, sizeof(MarinePacket));
    packet->company_id       = COMPANY_ID;
    packet->device_id        = get_device_id();
    packet->event_type       = (uint8_t)event_type;
    packet->sequence_number  = next_sequence_number();
    packet->elapsed_seconds  = (millis() / 1000) - boot_time_sec;
    packet->battery_mv       = battery_read_mv();
    packet->firmware_version = FIRMWARE_VERSION;
}

void ble_transmit(MarinePacket* packet) {
    Bluefruit.Advertising.clearData();
    Bluefruit.Advertising.addManufacturerData(packet, sizeof(MarinePacket));
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);

    for (int i = 0; i < TX_REPEAT_COUNT; i++) {
        Bluefruit.Advertising.start(0);
        delay(TX_REPEAT_DELAY_MS);
        Bluefruit.Advertising.stop();
        if (i < TX_REPEAT_COUNT - 1) {
            delay(TX_REPEAT_DELAY_MS);
        }
    }

#ifdef DEBUG_SERIAL
    Serial.print("TX event=0x");
    Serial.print(packet->event_type, HEX);
    Serial.print(" seq=");
    Serial.print(packet->sequence_number);
    Serial.print(" bat=");
    Serial.print(packet->battery_mv);
    Serial.println("mV");
#endif
}

void ble_start_dfu_window() {
#ifdef DEBUG_SERIAL
    Serial.println("DFU window open — 10 seconds");
#endif
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.start(DFU_WINDOW_SEC);
    delay(DFU_WINDOW_SEC * 1000);
    Bluefruit.Advertising.stop();
#ifdef DEBUG_SERIAL
    Serial.println("DFU window closed");
#endif
}

void ble_stop() {
    Bluefruit.Advertising.stop();
}