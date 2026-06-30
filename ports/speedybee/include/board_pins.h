// ---------------------------------------------------------------------------
// SpeedyBee Nano 2.4G (ESP8285 + SX1280) pin map
//
// Source of truth: ExpressLRS/targets  ->  RX/Generic 2400 PA.json
//   { "product_name": "SpeedyBee Nano 2.4GHz RX", "platform": "esp8285" }
//
// The SX1280 hangs off the ESP8285 hardware SPI bus (HSPI):
//   SCK=GPIO14  MISO=GPIO12  MOSI=GPIO13  (NSS controlled manually on GPIO15)
// ---------------------------------------------------------------------------
#pragma once

// --- SX1280 SPI / control lines ---
#define PIN_RADIO_NSS    15   // SPI chip-select (active low)
#define PIN_RADIO_SCK    14   // SPI clock  (HSPI)
#define PIN_RADIO_MOSI   13   // SPI MOSI   (HSPI)
#define PIN_RADIO_MISO   12   // SPI MISO   (HSPI)
#define PIN_RADIO_RST     2   // SX1280 reset (active low)
#define PIN_RADIO_BUSY    5   // SX1280 BUSY  (high = busy)
#define PIN_RADIO_DIO1    4   // SX1280 DIO1 interrupt (unused for scanning)

// --- PA / LNA RF front-end switch (this is the "PA" board variant) ---
// Drive RXEN high + TXEN low to route the antenna through the LNA into the
// radio. This MUST be enabled or the scan only sees the chip's own noise floor.
#define PIN_PA_RXEN       9
#define PIN_PA_TXEN      10

// --- UI / misc ---
#define PIN_LED          16   // status LED
#define PIN_BUTTON        0   // bind button == BOOT strap (low at reset = flash)

// --- UART0 (debug / scan output goes here) ---
#define PIN_UART_RX       3
#define PIN_UART_TX       1
