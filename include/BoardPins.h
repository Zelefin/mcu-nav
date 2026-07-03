#pragma once

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

#ifndef PIN_NOT_CONNECTED
#define PIN_NOT_CONNECTED (-1)
#endif

#if defined(TARGET_NODEMCU_32S) && TARGET_NODEMCU_32S
static_assert(PIN_LORA_SCK == 18, "Wrong NodeMCU-32S LORA SCK pin");
static_assert(PIN_LORA_MISO == 19, "Wrong NodeMCU-32S LORA MISO pin");
static_assert(PIN_LORA_MOSI == 23, "Wrong NodeMCU-32S LORA MOSI pin");
static_assert(PIN_LORA_CS == 5, "Wrong NodeMCU-32S LORA CS pin");
static_assert(PIN_LORA_RST == 27, "Wrong NodeMCU-32S LORA RST pin");
static_assert(PIN_LORA_BUSY == 26, "Wrong NodeMCU-32S LORA BUSY pin");
static_assert(PIN_LORA_DIO1 == 25, "Wrong NodeMCU-32S LORA DIO1 pin");
static_assert(PIN_LORA_DIO2 == PIN_NOT_CONNECTED || PIN_LORA_DIO2 == 33,
              "Wrong NodeMCU-32S LORA DIO2 pin; use -1 when not connected or GPIO33 when wired");
static_assert(PIN_LORA_DIO3 == PIN_NOT_CONNECTED || PIN_LORA_DIO3 == 32,
              "Wrong NodeMCU-32S LORA DIO3 pin; use -1 when not connected or GPIO32 when wired");
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
static constexpr int notConnected = PIN_NOT_CONNECTED;

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
}  // namespace BoardPins
