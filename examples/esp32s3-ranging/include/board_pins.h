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
#ifndef PIN_BOOT_SELECT
#error "PIN_BOOT_SELECT must be supplied by platformio.ini build_flags"
#endif
#ifndef PIN_STATUS_LED
#define PIN_STATUS_LED -1
#endif

namespace BoardPins {
static constexpr const char *board_name = BOARD_NAME;

static constexpr int lora_sck = PIN_LORA_SCK;
static constexpr int lora_miso = PIN_LORA_MISO;
static constexpr int lora_mosi = PIN_LORA_MOSI;
static constexpr int lora_cs = PIN_LORA_CS;
static constexpr int lora_rst = PIN_LORA_RST;
static constexpr int lora_busy = PIN_LORA_BUSY;
static constexpr int lora_dio1 = PIN_LORA_DIO1;

static constexpr int boot_select = PIN_BOOT_SELECT;
static constexpr int status_led = PIN_STATUS_LED;
}  // namespace BoardPins
