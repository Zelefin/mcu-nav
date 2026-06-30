#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#include "board_pins.h"

namespace {
static constexpr uint32_t SerialBaud = 115200;
static constexpr uint32_t RoleSelectWindowMs = 5000;
static constexpr float RangingFrequencyMhz = 2445.0f;
static constexpr float RangingBandwidthKhz = 1625.0f;
static constexpr uint8_t RangingSpreadingFactor = 7;
static constexpr uint8_t RangingCodingRate = 5;
static constexpr uint8_t RangingSyncWord = RADIOLIB_SX128X_SYNC_WORD_PRIVATE;
static constexpr int8_t RangingTxPowerDbm = 2;
static constexpr uint16_t RangingPreambleLength = 12;
static constexpr uint32_t RangingAddress = 0x53423234UL;  // "SB24"
static constexpr uint32_t MasterHostTimeoutMs = 350;
static constexpr uint32_t MasterExchangePeriodMs = 500;
static constexpr uint32_t SlaveListenWindowMs = 10000;

// AN1200.29 defaults; SF7/BW1625 uses value 13528.
uint16_t RangingCalibration[3][6] = {
    {10299, 10271, 10244, 10242, 10230, 10246},
    {11486, 11474, 11453, 11426, 11417, 11401},
    {13308, 13493, 13528, 13515, 13430, 13376},
};

enum class Role {
  Master,
  Slave,
};

SX1280 radio = new Module(PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
Role role = Role::Master;
uint32_t attempt_id = 0;
uint32_t next_master_exchange_ms = 0;

const char *roleName(Role value) {
  return value == Role::Slave ? "slave" : "master";
}

void flushLog() {
  Serial.flush();
}

void setStatusLed(bool on) {
  if (BoardPins::status_led >= 0) {
    digitalWrite(BoardPins::status_led, on ? HIGH : LOW);
  }
}

void setupStatusLed() {
  if (BoardPins::status_led >= 0) {
    pinMode(BoardPins::status_led, OUTPUT);
    setStatusLed(false);
  }
}

void blinkStatusDuringRoleWindow(uint32_t now_ms, uint32_t *last_toggle_ms, bool *led_on) {
  if (BoardPins::status_led < 0) {
    return;
  }
  if ((now_ms - *last_toggle_ms) >= 100) {
    *led_on = !*led_on;
    setStatusLed(*led_on);
    *last_toggle_ms = now_ms;
  }
}

Role selectRole() {
  Serial.println();
  Serial.printf("[esp32s3-ranging] role window: press BOOT/GPIO%d within 5s for slave\r\n",
                BoardPins::boot_select);
  flushLog();

  pinMode(BoardPins::boot_select, INPUT_PULLUP);
  const uint32_t started_ms = millis();
  uint32_t last_toggle_ms = started_ms;
  bool led_on = false;

  while ((millis() - started_ms) < RoleSelectWindowMs) {
    if (digitalRead(BoardPins::boot_select) == LOW) {
      Serial.println("role_select selected=slave reason=boot_button");
      flushLog();
      setStatusLed(false);
      delay(250);
      return Role::Slave;
    }

    blinkStatusDuringRoleWindow(millis(), &last_toggle_ms, &led_on);
    delay(10);
  }

  setStatusLed(false);
  Serial.println("role_select selected=master reason=timeout");
  flushLog();
  return Role::Master;
}

bool waitBusyLow(uint32_t timeout_ms) {
  const uint32_t started_ms = millis();
  while (digitalRead(BoardPins::lora_busy) == HIGH) {
    if ((millis() - started_ms) >= timeout_ms) {
      return false;
    }
    delay(2);
  }
  return true;
}

void printPinMap() {
  Serial.printf("pin_map sck=%d miso=%d mosi=%d cs=%d rst=%d busy=%d dio1=%d boot_select=%d status_led=%d\r\n",
                BoardPins::lora_sck,
                BoardPins::lora_miso,
                BoardPins::lora_mosi,
                BoardPins::lora_cs,
                BoardPins::lora_rst,
                BoardPins::lora_busy,
                BoardPins::lora_dio1,
                BoardPins::boot_select,
                BoardPins::status_led);
  flushLog();
}

void printProfile() {
  Serial.printf("ranging_profile freq_mhz=%.1f bandwidth_khz=%.1f sf=%u cr=4/%u address=0x%08lX calibration_sf7_bw1625=13528 uncorrected_m_primary=true\r\n",
                static_cast<double>(RangingFrequencyMhz),
                static_cast<double>(RangingBandwidthKhz),
                RangingSpreadingFactor,
                RangingCodingRate,
                static_cast<unsigned long>(RangingAddress));
  flushLog();
}

void fatalRadioInit(int16_t state) {
  Serial.printf("radio_init_failed error=%d note=\"SX1280 not ready; check wiring, power, and SPI pins\"\r\n",
                state);
  flushLog();
  for (;;) {
    setStatusLed(true);
    delay(80);
    setStatusLed(false);
    delay(80);
  }
}

void initRadio() {
  pinMode(BoardPins::lora_busy, INPUT);
  pinMode(BoardPins::lora_dio1, INPUT);
  SPI.begin(BoardPins::lora_sck, BoardPins::lora_miso, BoardPins::lora_mosi, BoardPins::lora_cs);

  if (!waitBusyLow(1000)) {
    fatalRadioInit(RADIOLIB_ERR_SPI_CMD_TIMEOUT);
  }

  int16_t state = radio.begin(RangingFrequencyMhz,
                              RangingBandwidthKhz,
                              RangingSpreadingFactor,
                              RangingCodingRate,
                              RangingSyncWord,
                              RangingTxPowerDbm,
                              RangingPreambleLength);
  Serial.printf("radio_init role=%s ok=%s error=%d\r\n",
                roleName(role),
                state == RADIOLIB_ERR_NONE ? "true" : "false",
                state);
  flushLog();
  if (state != RADIOLIB_ERR_NONE) {
    fatalRadioInit(state);
  }
}

bool waitDio1High(uint32_t timeout_ms) {
  const uint32_t started_ms = millis();
  while (digitalRead(BoardPins::lora_dio1) == LOW) {
    if ((millis() - started_ms) >= timeout_ms) {
      return false;
    }
    delay(2);
  }
  return true;
}

void logMasterFailure(uint32_t elapsed_ms, int16_t error, const char *note) {
  Serial.printf("range_result ok=false role=master attempt=%lu elapsed_ms=%lu error=%d note=\"%s\"\r\n",
                static_cast<unsigned long>(attempt_id),
                static_cast<unsigned long>(elapsed_ms),
                error,
                note);
  flushLog();
}

void runMasterExchange() {
  attempt_id++;
  const uint32_t started_ms = millis();

  if (!waitBusyLow(1000)) {
    logMasterFailure(millis() - started_ms,
                     RADIOLIB_ERR_SPI_CMD_TIMEOUT,
                     "BUSY stayed high before ranging; check radio wiring");
    return;
  }

  int16_t state = radio.startRanging(true, RangingAddress, RangingCalibration);
  if (state != RADIOLIB_ERR_NONE) {
    logMasterFailure(millis() - started_ms,
                     state,
                     "startRanging failed; check radio configuration");
    return;
  }

  if (!waitDio1High(MasterHostTimeoutMs)) {
    logMasterFailure(millis() - started_ms,
                     RADIOLIB_ERR_RANGING_TIMEOUT,
                     "ranging timeout; check slave role, power, wiring, address, and RF profile");
    (void)radio.finishRanging();
    return;
  }

  state = radio.finishRanging();
  if (state != RADIOLIB_ERR_NONE) {
    logMasterFailure(millis() - started_ms,
                     state,
                     "finishRanging failed after DIO1 event");
    return;
  }

  const int32_t raw_reg = radio.getRangingResultRaw();
  const float uncorrected_m = radio.getRangingResult();
  const float rssi_dbm = radio.getRSSI();
  const float snr_db = radio.getSNR();
  const uint32_t elapsed_ms = millis() - started_ms;

  Serial.printf("range_result ok=true role=master attempt=%lu uncorrected_m=%.2f raw_reg=%ld rssi_dbm=%.1f snr_db=%.1f elapsed_ms=%lu\r\n",
                static_cast<unsigned long>(attempt_id),
                static_cast<double>(uncorrected_m),
                static_cast<long>(raw_reg),
                static_cast<double>(rssi_dbm),
                static_cast<double>(snr_db),
                static_cast<unsigned long>(elapsed_ms));
  flushLog();
}

void serviceMaster() {
  if ((int32_t)(millis() - next_master_exchange_ms) < 0) {
    return;
  }
  runMasterExchange();
  next_master_exchange_ms = millis() + MasterExchangePeriodMs;
}

void serviceSlave() {
  const uint32_t started_ms = millis();

  int16_t state = radio.startRanging(false, RangingAddress, RangingCalibration);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("slave_listen ok=false elapsed_ms=%lu error=%d note=\"startRanging failed\"\r\n",
                  static_cast<unsigned long>(millis() - started_ms),
                  state);
    flushLog();
    delay(500);
    return;
  }

  if (!waitDio1High(SlaveListenWindowMs)) {
    (void)radio.finishRanging();
    Serial.printf("slave_listen ok=false elapsed_ms=%lu error=%d note=\"no master request observed\"\r\n",
                  static_cast<unsigned long>(millis() - started_ms),
                  RADIOLIB_ERR_RANGING_TIMEOUT);
    flushLog();
    return;
  }

  state = radio.finishRanging();
  Serial.printf("slave_response ok=%s elapsed_ms=%lu error=%d\r\n",
                state == RADIOLIB_ERR_NONE ? "true" : "false",
                static_cast<unsigned long>(millis() - started_ms),
                state);
  flushLog();
}
}  // namespace

void setup() {
  Serial.begin(SerialBaud);
  delay(1500);

  setupStatusLed();
  Serial.println("[esp32s3-ranging] booting");
  Serial.printf("board=%s build=\"%s %s\"\r\n", BoardPins::board_name, __DATE__, __TIME__);
  flushLog();
  printPinMap();

  role = selectRole();
  Serial.printf("[esp32s3-ranging] role=%s\r\n", roleName(role));
  flushLog();
  printProfile();
  initRadio();

  if (role == Role::Master) {
    next_master_exchange_ms = millis();
  } else {
    Serial.println("slave_ready note=\"serial monitor is optional; master logs are primary evidence\"");
    flushLog();
  }
}

void loop() {
  if (role == Role::Slave) {
    serviceSlave();
    return;
  }
  serviceMaster();
  delay(10);
}
