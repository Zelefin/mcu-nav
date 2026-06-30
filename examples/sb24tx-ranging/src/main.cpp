// ===========================================================================
// SX1280 ranging bring-up for SpeedyBee Nano 2.4G (ESP8285 + SX1280)
//
// Single image:
//   - press bind button during the first 5 seconds after boot for slave role
//   - otherwise the board becomes master and logs every ranging exchange
// ===========================================================================
#include <Arduino.h>
#include <math.h>

#include "SX1280.h"
#include "board_pins.h"

static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t ROLE_SELECT_WINDOW_MS = 5000;
static constexpr uint32_t RANGING_FREQUENCY_HZ = 2445000000UL;
static constexpr uint32_t RANGING_ADDRESS = 0x53423234UL;  // "SB24"
static constexpr uint16_t RANGING_CALIBRATION = 13528;     // SF7/BW1600 table.
static constexpr float SHORT_RANGE_RAW_BIAS_M = 6.2f;      // Empirical SB24TX v0 bias.
static constexpr uint16_t MASTER_EXCHANGE_TIMEOUT_MS = 150;
static constexpr uint32_t MASTER_HOST_TIMEOUT_MS = 350;
static constexpr uint32_t MASTER_EXCHANGE_PERIOD_MS = 500;
static constexpr uint16_t MASTER_PA_RX_SWITCH_US = 3000;

enum Role {
  ROLE_MASTER,
  ROLE_SLAVE,
};

enum PaMode {
  PA_OFF,
  PA_RX,
  PA_TX,
};

static SX1280 radio(PIN_RADIO_NSS, PIN_RADIO_RST, PIN_RADIO_BUSY);
static Role role = ROLE_MASTER;
static uint32_t attempt_id = 0;
static uint32_t next_master_exchange_ms = 0;
static uint32_t last_master_led_ms = 0;
static bool master_led_on = false;

static const char *roleName(Role value) {
  return value == ROLE_SLAVE ? "slave" : "master";
}

static const char *paName(PaMode value) {
  switch (value) {
    case PA_RX:
      return "rx";
    case PA_TX:
      return "tx";
    default:
      return "off";
  }
}

static void setLed(bool on) {
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

static void setPaMode(PaMode mode) {
  switch (mode) {
    case PA_RX:
      digitalWrite(PIN_PA_RXEN, HIGH);
      digitalWrite(PIN_PA_TXEN, LOW);
      break;
    case PA_TX:
      digitalWrite(PIN_PA_RXEN, LOW);
      digitalWrite(PIN_PA_TXEN, HIGH);
      break;
    default:
      digitalWrite(PIN_PA_RXEN, LOW);
      digitalWrite(PIN_PA_TXEN, LOW);
      break;
  }
}

static float correctedMeters(float biased_raw_m) {
  if (biased_raw_m <= 18.5f) {
    return expf((biased_raw_m + 2.4917f) / 7.2262f);
  }
  return biased_raw_m;
}

static void fatalBlink(const __FlashStringHelper *message) {
  Serial.println(message);
  setPaMode(PA_OFF);
  while (true) {
    setLed(true);
    delay(80);
    setLed(false);
    delay(80);
  }
}

static Role selectRole() {
  Serial.println();
  Serial.println(F("[sb24tx-ranging] role window: press bind button within 5s for slave"));

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  uint32_t started_ms = millis();
  uint32_t last_blink_ms = 0;
  bool led = false;

  while ((uint32_t)(millis() - started_ms) < ROLE_SELECT_WINDOW_MS) {
    if (digitalRead(PIN_BUTTON) == LOW) {
      Serial.println(F("role_select selected=slave reason=button"));
      setLed(false);
      delay(250);
      return ROLE_SLAVE;
    }

    if ((uint32_t)(millis() - last_blink_ms) >= 100) {
      led = !led;
      setLed(led);
      last_blink_ms = millis();
    }
    yield();
  }

  setLed(false);
  Serial.println(F("role_select selected=master reason=timeout"));
  return ROLE_MASTER;
}

static bool initRadio(Role selected_role) {
  SX1280::RangingRole sx_role = selected_role == ROLE_SLAVE
                                    ? SX1280::RANGING_ROLE_SLAVE
                                    : SX1280::RANGING_ROLE_MASTER;
  setPaMode(selected_role == ROLE_SLAVE ? PA_RX : PA_OFF);

  bool ok = radio.beginRanging(sx_role, RANGING_FREQUENCY_HZ, RANGING_ADDRESS,
                               RANGING_CALIBRATION);
  Serial.print(F("radio_init role="));
  Serial.print(roleName(selected_role));
  Serial.print(F(" ok="));
  Serial.print(ok ? F("true") : F("false"));
  Serial.print(F(" status=0x"));
  Serial.print(radio.getStatus(), HEX);
  Serial.print(F(" freq_hz="));
  Serial.print(RANGING_FREQUENCY_HZ);
  Serial.print(F(" address=0x"));
  Serial.print(RANGING_ADDRESS, HEX);
  Serial.print(F(" calibration="));
  Serial.println(RANGING_CALIBRATION);

  return ok;
}

static void startSlaveListen() {
  setPaMode(PA_RX);
  radio.startSlaveListen();
  Serial.print(F("slave_listen pa="));
  Serial.print(paName(PA_RX));
  Serial.println(F(" mode=continuous_rx"));
}

static void printIrqFlags(uint16_t irq) {
  if (irq & SX1280::IRQ_RANGING_MASTER_RESULT_VALID) {
    Serial.print(F("master_result_valid "));
  }
  if (irq & SX1280::IRQ_RANGING_MASTER_TIMEOUT) {
    Serial.print(F("master_timeout "));
  }
  if (irq & SX1280::IRQ_RANGING_SLAVE_REQUEST_VALID) {
    Serial.print(F("slave_request_valid "));
  }
  if (irq & SX1280::IRQ_RANGING_SLAVE_RESPONSE_DONE) {
    Serial.print(F("slave_response_done "));
  }
  if (irq & SX1280::IRQ_RANGING_SLAVE_REQUEST_DISCARD) {
    Serial.print(F("slave_request_discard "));
  }
  if (irq & SX1280::IRQ_RX_TX_TIMEOUT) {
    Serial.print(F("rx_tx_timeout "));
  }
}

static void runMasterExchange() {
  attempt_id++;

  setPaMode(PA_TX);
  uint32_t started_ms = millis();
  radio.startMasterExchange(MASTER_EXCHANGE_TIMEOUT_MS);
  delayMicroseconds(MASTER_PA_RX_SWITCH_US);
  setPaMode(PA_RX);

  uint16_t irq = 0;
  while ((uint32_t)(millis() - started_ms) < MASTER_HOST_TIMEOUT_MS) {
    irq = radio.getIrqStatus();
    if (irq & (SX1280::IRQ_RANGING_MASTER_RESULT_VALID |
               SX1280::IRQ_RANGING_MASTER_TIMEOUT |
               SX1280::IRQ_RX_TX_TIMEOUT)) {
      break;
    }
    delay(2);
    yield();
  }

  uint32_t elapsed_ms = millis() - started_ms;
  if (irq & SX1280::IRQ_RANGING_MASTER_RESULT_VALID) {
    SX1280::PacketStatus status = radio.readPacketStatus();
    int32_t raw_result = radio.readRangingResultRaw();
    float raw_m = radio.rawRangingMeters(raw_result);
    float biased_raw_m = raw_m + SHORT_RANGE_RAW_BIAS_M;
    float corrected_m = correctedMeters(biased_raw_m);

    Serial.print(F("range_result ok=true attempt="));
    Serial.print(attempt_id);
    Serial.print(F(" corrected_m="));
    Serial.print(corrected_m, 2);
    Serial.print(F(" raw_m="));
    Serial.print(raw_m, 2);
    Serial.print(F(" biased_raw_m="));
    Serial.print(biased_raw_m, 2);
    Serial.print(F(" raw_bias_m="));
    Serial.print(SHORT_RANGE_RAW_BIAS_M, 2);
    Serial.print(F(" raw_reg="));
    Serial.print(raw_result);
    Serial.print(F(" rssi_dbm="));
    Serial.print(status.rssi_dbm);
    Serial.print(F(" snr_db="));
    Serial.print(status.snr_db, 1);
    Serial.print(F(" elapsed_ms="));
    Serial.print(elapsed_ms);
    Serial.print(F(" pa_start="));
    Serial.print(paName(PA_TX));
    Serial.print(F(" pa_result="));
    Serial.print(paName(PA_RX));
    Serial.print(F(" pa_rx_switch_us="));
    Serial.println(MASTER_PA_RX_SWITCH_US);
  } else {
    Serial.print(F("range_result ok=false attempt="));
    Serial.print(attempt_id);
    Serial.print(F(" elapsed_ms="));
    Serial.print(elapsed_ms);
    Serial.print(F(" irq=0x"));
    Serial.print(irq, HEX);
    Serial.print(F(" flags=\""));
    printIrqFlags(irq);
    Serial.print(F("\" pa_start="));
    Serial.print(paName(PA_TX));
    Serial.print(F(" pa_result="));
    Serial.print(paName(PA_RX));
    Serial.print(F(" note=\"timeout or missing response; check slave power and RF front-end path\""));
    Serial.println();
  }

  radio.clearIrqStatus();
  setPaMode(PA_OFF);
}

static void serviceMasterLed() {
  if ((uint32_t)(millis() - last_master_led_ms) >= 1000) {
    master_led_on = !master_led_on;
    setLed(master_led_on);
    last_master_led_ms = millis();
  }
}

static void serviceSlaveLed() {
  uint32_t phase = millis() % 2000;
  bool on = phase < 90 || (phase >= 220 && phase < 310);
  setLed(on);
}

static void serviceSlave() {
  serviceSlaveLed();
  uint16_t irq = radio.getIrqStatus();
  uint16_t terminal = SX1280::IRQ_RANGING_SLAVE_RESPONSE_DONE |
                      SX1280::IRQ_RANGING_SLAVE_REQUEST_DISCARD |
                      SX1280::IRQ_RX_TX_TIMEOUT;

  if (irq & (terminal | SX1280::IRQ_RANGING_SLAVE_REQUEST_VALID)) {
    Serial.print(F("slave_irq irq=0x"));
    Serial.print(irq, HEX);
    Serial.print(F(" flags=\""));
    printIrqFlags(irq);
    Serial.print(F("\" pa="));
    Serial.println(paName(PA_RX));
  }

  if (irq & terminal) {
    startSlaveListen();
  } else if (irq & SX1280::IRQ_RANGING_SLAVE_REQUEST_VALID) {
    radio.clearIrqStatus(SX1280::IRQ_RANGING_SLAVE_REQUEST_VALID);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(250);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_PA_RXEN, OUTPUT);
  pinMode(PIN_PA_TXEN, OUTPUT);
  setLed(false);
  setPaMode(PA_OFF);

  role = selectRole();

  Serial.print(F("[sb24tx-ranging] role="));
  Serial.print(roleName(role));
  Serial.println(F(" profile=sf7_bw1625_cr45 corrected_m_primary=true"));

  if (!initRadio(role)) {
    fatalBlink(F("radio_init_failed error=\"SX1280 not responding\""));
  }

  if (role == ROLE_SLAVE) {
    startSlaveListen();
  } else {
    next_master_exchange_ms = millis();
  }
}

void loop() {
  if (role == ROLE_SLAVE) {
    serviceSlave();
    delay(10);
    return;
  }

  serviceMasterLed();
  if ((int32_t)(millis() - next_master_exchange_ms) >= 0) {
    runMasterExchange();
    next_master_exchange_ms = millis() + MASTER_EXCHANGE_PERIOD_MS;
  }
}
