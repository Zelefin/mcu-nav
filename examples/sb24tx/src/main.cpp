// ===========================================================================
// 2.4 GHz band-load scanner for the SpeedyBee Nano 2.4G (ESP8285 + SX1280)
//
// Steps the SX1280 across the 2.4 GHz ISM band, reads the instantaneous RSSI
// of the receiver front-end at each step, and streams a live ASCII waterfall
// over UART0 (115200 baud) so you can see which channels are busy.
//
// This is NOT a calibrated spectrum analyzer: readings are relative receiver
// RSSI, useful for spotting WiFi/BT/video-TX congestion and finding quiet
// channels, not for absolute power measurement.
// ===========================================================================
#include <Arduino.h>
#include "board_pins.h"
#include "SX1280.h"

// ---- Scan configuration ----------------------------------------------------
static constexpr uint32_t F_START_MHZ = 2400;   // band start
static constexpr uint32_t F_STOP_MHZ  = 2483;   // band stop (ISM edge 2483.5)
static constexpr uint32_t F_STEP_MHZ  = 1;      // bin width (match RX bandwidth)
static constexpr uint16_t SETTLE_US   = 300;    // RX settle time after retune
static constexpr uint8_t  SAMPLES     = 4;      // peak-hold samples per bin

static constexpr int NUM_BINS =
    (F_STOP_MHZ - F_START_MHZ) / F_STEP_MHZ + 1;

// Display dynamic range (dBm). Quieter than FLOOR -> blank, busier -> '@'.
static constexpr float RSSI_FLOOR = -100.0f;
static constexpr float RSSI_CEIL  = -40.0f;

// ----------------------------------------------------------------------------
SX1280 radio(PIN_RADIO_NSS, PIN_RADIO_RST, PIN_RADIO_BUSY);

static float bins[NUM_BINS];

static char levelChar(float rssi) {
  static const char ramp[] = " .:-=+*#%@";   // 10 levels
  const int steps = sizeof(ramp) - 2;
  float t = (rssi - RSSI_FLOOR) / (RSSI_CEIL - RSSI_FLOOR);
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return ramp[(int)(t * steps + 0.5f)];
}

// Tune to one bin and return its peak RSSI.
static float scanBin(uint32_t freqMhz) {
  radio.setStandby(SX1280::STDBY_RC);
  radio.setFrequencyHz((uint32_t)freqMhz * 1000000UL);
  radio.startRx();
  delayMicroseconds(SETTLE_US);

  float peak = -200.0f;
  for (uint8_t i = 0; i < SAMPLES; i++) {
    float r = radio.readRssiDbm();
    if (r > peak) peak = r;
    delayMicroseconds(50);
  }
  return peak;
}

static void printScale() {
  Serial.println();
  Serial.print(F("  2.4 GHz band load  ["));
  Serial.print(F_START_MHZ);
  Serial.print(F(" - "));
  Serial.print(F_STOP_MHZ);
  Serial.print(F(" MHz, "));
  Serial.print(NUM_BINS);
  Serial.print(F(" x "));
  Serial.print(F_STEP_MHZ);
  Serial.println(F(" MHz bins]"));
  Serial.println(F("  legend: ' '=quiet  . : - = + * # % @ =busy"));

  // Frequency tick row: mark every 10 MHz.
  Serial.print(F("  "));
  for (int i = 0; i < NUM_BINS; i++) {
    uint32_t f = F_START_MHZ + i * F_STEP_MHZ;
    Serial.print((f % 10 == 0) ? '|' : ' ');
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n[SpeedyBee Nano 2.4G] SX1280 band scanner"));

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  // Put the PA/LNA front-end into receive: RXEN high, TXEN low.
  pinMode(PIN_PA_RXEN, OUTPUT);
  pinMode(PIN_PA_TXEN, OUTPUT);
  digitalWrite(PIN_PA_RXEN, HIGH);
  digitalWrite(PIN_PA_TXEN, LOW);

  // 1 MHz bins -> ~1.2 MHz receiver bandwidth is a good match.
  if (!radio.begin(SX1280::GFSK_BR_1_000_BW_1_2)) {
    Serial.println(F("ERROR: SX1280 not responding (check SPI wiring)."));
    while (true) {                 // fast blink = init failure
      digitalWrite(PIN_LED, !digitalRead(PIN_LED));
      delay(100);
    }
  }
  radio.enableHighSensitivity();
  Serial.print(F("SX1280 OK, status=0x"));
  Serial.println(radio.getStatus(), HEX);

  printScale();
}

void loop() {
  static uint16_t sweeps = 0;

  // --- one full sweep across the band ---
  int peakIdx = 0;
  for (int i = 0; i < NUM_BINS; i++) {
    bins[i] = scanBin(F_START_MHZ + i * F_STEP_MHZ);
    if (bins[i] > bins[peakIdx]) peakIdx = i;
  }

  // --- render one waterfall row ---
  if (++sweeps % 24 == 0) printScale();
  Serial.print(F("  "));
  for (int i = 0; i < NUM_BINS; i++) Serial.print(levelChar(bins[i]));
  Serial.print(F("  peak "));
  Serial.print(F_START_MHZ + peakIdx * F_STEP_MHZ);
  Serial.print(F(" MHz @ "));
  Serial.print(bins[peakIdx], 0);
  Serial.println(F(" dBm"));

  digitalWrite(PIN_LED, !digitalRead(PIN_LED));   // heartbeat
}
