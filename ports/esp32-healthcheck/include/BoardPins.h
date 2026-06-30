#pragma once

#include <Arduino.h>

#ifndef BOARD_NAME
#error "BOARD_NAME must be supplied by platformio.ini build_flags"
#endif

#ifndef PIN_LORA_SCK
#error "PIN_LORA_SCK must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_LORA_MISO
#error "PIN_LORA_MISO must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_LORA_MOSI
#error "PIN_LORA_MOSI must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_LORA_CS
#error "PIN_LORA_CS must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_LORA_RST
#error "PIN_LORA_RST must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_LORA_BUSY
#error "PIN_LORA_BUSY must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_LORA_DIO1
#error "PIN_LORA_DIO1 must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_LORA_DIO2
#error "PIN_LORA_DIO2 must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_LORA_DIO3
#error "PIN_LORA_DIO3 must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_GPS_RX
#error "PIN_GPS_RX must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_GPS_TX
#error "PIN_GPS_TX must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_I2C_SDA
#error "PIN_I2C_SDA must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_I2C_SCL
#error "PIN_I2C_SCL must be supplied by platformio.ini build_flags"
#endif

#ifndef RADIO_FREQUENCY_MHZ
#define RADIO_FREQUENCY_MHZ 2445.0
#endif
#ifndef RADIO_BANDWIDTH_KHZ
#define RADIO_BANDWIDTH_KHZ 812.5
#endif
#ifndef RADIO_SPREADING_FACTOR
#define RADIO_SPREADING_FACTOR 7
#endif
#ifndef RADIO_CODING_RATE
#define RADIO_CODING_RATE 5
#endif
#ifndef RADIO_SYNC_WORD
#define RADIO_SYNC_WORD 0x12
#endif
#ifndef RADIO_TX_POWER_DBM
#define RADIO_TX_POWER_DBM 2
#endif
#ifndef RADIO_PREAMBLE_LEN
#define RADIO_PREAMBLE_LEN 12
#endif
#ifndef RADIO_TX_INTERVAL_MS
#define RADIO_TX_INTERVAL_MS 10000
#endif

namespace BoardPins {
static constexpr const char *boardName = BOARD_NAME;

static constexpr int loraSck = PIN_LORA_SCK;
static constexpr int loraMiso = PIN_LORA_MISO;
static constexpr int loraMosi = PIN_LORA_MOSI;
static constexpr int loraCs = PIN_LORA_CS;
static constexpr int loraRst = PIN_LORA_RST;
static constexpr int loraBusy = PIN_LORA_BUSY;
static constexpr int loraDio1 = PIN_LORA_DIO1;
static constexpr int loraDio2 = PIN_LORA_DIO2;
static constexpr int loraDio3 = PIN_LORA_DIO3;

static constexpr int gpsRx = PIN_GPS_RX;
static constexpr int gpsTx = PIN_GPS_TX;

static constexpr int i2cSda = PIN_I2C_SDA;
static constexpr int i2cScl = PIN_I2C_SCL;
}  // namespace BoardPins
