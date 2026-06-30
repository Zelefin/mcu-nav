#include <RadioLib.h>
#include <stdio.h>
#include <string.h>

#include "EspIdfRadioLibHal.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
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
static constexpr uint32_t MasterArmingDelayMs = 15000;
static constexpr uint32_t SlaveListenWindowMs = 10000;
static constexpr uint32_t SlaveArmingDelayMs = 5000;

#ifndef FORCE_RANGING_ROLE
#define FORCE_RANGING_ROLE 0
#endif

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

EspIdfRadioLibHal radio_hal(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI);
Module radio_module(&radio_hal, PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
SX1280 radio(&radio_module);

Role role = Role::Master;
uint32_t attempt_id = 0;
uint32_t next_master_exchange_ms = 0;
uint32_t slave_listen_start_ms = 0;
uint32_t next_slave_arming_log_ms = 0;

uint32_t millis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void delayMs(uint32_t ms) {
  if (ms == 0) {
    return;
  }
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (ticks == 0) {
    ticks = 1;
  }
  vTaskDelay(ticks);
}

void flushLog() {
  fflush(stdout);
}

const char *roleName(Role value) {
  return value == Role::Slave ? "slave" : "master";
}

bool isValidPin(int pin) {
  return pin >= 0 && pin < GPIO_NUM_MAX;
}

int readPin(int pin) {
  if (!isValidPin(pin)) {
    return 0;
  }
  return gpio_get_level(static_cast<gpio_num_t>(pin));
}

void configureInputPin(int pin, bool pull_up) {
  if (!isValidPin(pin)) {
    return;
  }

  gpio_config_t config = {};
  config.pin_bit_mask = (1ULL << pin);
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
}

void configureOutputPin(int pin) {
  if (!isValidPin(pin)) {
    return;
  }

  gpio_config_t config = {};
  config.pin_bit_mask = (1ULL << pin);
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
}

void appendFlag(char *buffer, size_t buffer_size, const char *flag) {
  if (buffer_size == 0) {
    return;
  }
  const size_t used = strlen(buffer);
  if (used >= buffer_size - 1) {
    return;
  }
  if (used > 0) {
    strncat(buffer, " ", buffer_size - strlen(buffer) - 1);
  }
  strncat(buffer, flag, buffer_size - strlen(buffer) - 1);
}

void formatIrqFlags(uint16_t irq, char *buffer, size_t buffer_size) {
  if (buffer_size == 0) {
    return;
  }
  buffer[0] = '\0';
  if (irq & RADIOLIB_SX128X_IRQ_RANGING_SLAVE_REQ_VALID) {
    appendFlag(buffer, buffer_size, "slave_req_valid");
  }
  if (irq & RADIOLIB_SX128X_IRQ_RANGING_MASTER_TIMEOUT) {
    appendFlag(buffer, buffer_size, "master_timeout");
  }
  if (irq & RADIOLIB_SX128X_IRQ_RANGING_MASTER_RES_VALID) {
    appendFlag(buffer, buffer_size, "master_result_valid");
  }
  if (irq & RADIOLIB_SX128X_IRQ_RANGING_SLAVE_REQ_DISCARD) {
    appendFlag(buffer, buffer_size, "slave_req_discard");
  }
  if (irq & RADIOLIB_SX128X_IRQ_RANGING_SLAVE_RESP_DONE) {
    appendFlag(buffer, buffer_size, "slave_response_done");
  }
  if (irq & RADIOLIB_SX128X_IRQ_RX_TX_TIMEOUT) {
    appendFlag(buffer, buffer_size, "rx_tx_timeout");
  }
}

void setStatusLed(bool on) {
  if (isValidPin(BoardPins::status_led)) {
    gpio_set_level(static_cast<gpio_num_t>(BoardPins::status_led), on ? 1 : 0);
  }
}

void setupStatusLed() {
  if (isValidPin(BoardPins::status_led)) {
    configureOutputPin(BoardPins::status_led);
    setStatusLed(false);
  }
}

#if FORCE_RANGING_ROLE == 0
void blinkStatusDuringRoleWindow(uint32_t now_ms, uint32_t *last_toggle_ms, bool *led_on) {
  if (!isValidPin(BoardPins::status_led)) {
    return;
  }
  if ((now_ms - *last_toggle_ms) >= 100) {
    *led_on = !*led_on;
    setStatusLed(*led_on);
    *last_toggle_ms = now_ms;
  }
}
#endif

Role selectRole() {
#if FORCE_RANGING_ROLE == 1
  printf("role_select selected=master reason=build_flag\n");
  flushLog();
  return Role::Master;
#elif FORCE_RANGING_ROLE == 2
  printf("role_select selected=slave reason=build_flag\n");
  flushLog();
  return Role::Slave;
#else
  printf("\n");
  printf("[esp32s3-ranging] role window: press BOOT/GPIO%d within 5s for slave\n",
         BoardPins::boot_select);
  flushLog();

  configureInputPin(BoardPins::boot_select, true);
  const uint32_t started_ms = millis();
  uint32_t last_toggle_ms = started_ms;
  bool led_on = false;

  while ((millis() - started_ms) < RoleSelectWindowMs) {
    if (readPin(BoardPins::boot_select) == 0) {
      printf("role_select selected=slave reason=boot_button\n");
      flushLog();
      setStatusLed(false);
      delayMs(250);
      return Role::Slave;
    }

    blinkStatusDuringRoleWindow(millis(), &last_toggle_ms, &led_on);
    delayMs(10);
  }

  setStatusLed(false);
  printf("role_select selected=master reason=timeout\n");
  flushLog();
  return Role::Master;
#endif
}

bool waitBusyLow(uint32_t timeout_ms) {
  const uint32_t started_ms = millis();
  while (readPin(BoardPins::lora_busy) != 0) {
    if ((millis() - started_ms) >= timeout_ms) {
      return false;
    }
    delayMs(2);
  }
  return true;
}

void printPinMap() {
  printf("pin_map sck=%d miso=%d mosi=%d cs=%d rst=%d busy=%d dio1=%d boot_select=%d status_led=%d\n",
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
  printf("ranging_profile freq_mhz=%.1f bandwidth_khz=%.1f sf=%u cr=4/%u address=0x%08lX calibration_sf7_bw1625=13528 uncorrected_m_primary=true\n",
         static_cast<double>(RangingFrequencyMhz),
         static_cast<double>(RangingBandwidthKhz),
         RangingSpreadingFactor,
         RangingCodingRate,
         static_cast<unsigned long>(RangingAddress));
  flushLog();
}

void runPreRadioArmingDelay(Role selected_role) {
  const uint32_t delay_ms = selected_role == Role::Master ? MasterArmingDelayMs : SlaveArmingDelayMs;
  const uint32_t start_ms = millis();
  uint32_t next_log_ms = start_ms;

  printf("%s_pre_radio_arming start delay_ms=%lu\n",
         roleName(selected_role),
         static_cast<unsigned long>(delay_ms));
  flushLog();

  while ((millis() - start_ms) < delay_ms) {
    if ((int32_t)(millis() - next_log_ms) >= 0) {
      printf("%s_pre_radio_arming remaining_ms=%lu\n",
             roleName(selected_role),
             static_cast<unsigned long>(delay_ms - (millis() - start_ms)));
      flushLog();
      next_log_ms = millis() + 1000;
    }
    delayMs(20);
  }
}

void fatalRadioInit(int16_t state) {
  for (;;) {
    printf("radio_init_failed error=%d note=\"SX1280 not ready; check wiring, power, and SPI pins\"\n",
           state);
    flushLog();
    setStatusLed(true);
    delayMs(80);
    setStatusLed(false);
    delayMs(920);
  }
}

void initRadio() {
  configureInputPin(BoardPins::lora_busy, false);
  configureInputPin(BoardPins::lora_dio1, false);

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
  printf("radio_init role=%s ok=%s error=%d\n",
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
  while (readPin(BoardPins::lora_dio1) == 0) {
    if ((millis() - started_ms) >= timeout_ms) {
      return false;
    }
    delayMs(2);
  }
  return true;
}

void logMasterFailure(uint32_t elapsed_ms, int16_t error, uint16_t irq, const char *note) {
  char flags[96];
  formatIrqFlags(irq, flags, sizeof(flags));
  printf("range_result ok=false role=master attempt=%lu elapsed_ms=%lu error=%d irq=0x%04X flags=\"%s\" note=\"%s\"\n",
         static_cast<unsigned long>(attempt_id),
         static_cast<unsigned long>(elapsed_ms),
         error,
         irq,
         flags,
         note);
  flushLog();
}

void runMasterExchange() {
  attempt_id++;
  const uint32_t started_ms = millis();

  printf("master_exchange start role=master attempt=%lu timeout_ms=%lu\n",
         static_cast<unsigned long>(attempt_id),
         static_cast<unsigned long>(MasterHostTimeoutMs));
  flushLog();

  if (!waitBusyLow(1000)) {
    logMasterFailure(millis() - started_ms,
                     RADIOLIB_ERR_SPI_CMD_TIMEOUT,
                     radio.getIrqStatus(),
                     "BUSY stayed high before ranging; check radio wiring");
    return;
  }

  (void)radio.clearIrqFlags(RADIOLIB_SX128X_IRQ_ALL);
  int16_t state = radio.startRanging(true, RangingAddress, RangingCalibration);
  if (state != RADIOLIB_ERR_NONE) {
    logMasterFailure(millis() - started_ms,
                     state,
                     radio.getIrqStatus(),
                     "startRanging failed; check radio configuration");
    return;
  }

  if (!waitDio1High(MasterHostTimeoutMs)) {
    const uint16_t irq = radio.getIrqStatus();
    logMasterFailure(millis() - started_ms,
                     RADIOLIB_ERR_RANGING_TIMEOUT,
                     irq,
                     "ranging timeout; check slave role, power, wiring, address, and RF profile");
    (void)radio.finishRanging();
    return;
  }

  const uint16_t irq = radio.getIrqStatus();
  state = radio.finishRanging();
  if (state != RADIOLIB_ERR_NONE) {
    logMasterFailure(millis() - started_ms,
                     state,
                     irq,
                     "finishRanging failed after DIO1 event");
    return;
  }

  const int32_t raw_reg = radio.getRangingResultRaw();
  const float uncorrected_m = radio.getRangingResult();
  const float rssi_dbm = radio.getRSSI();
  const float snr_db = radio.getSNR();
  const uint32_t elapsed_ms = millis() - started_ms;

  char flags[96];
  formatIrqFlags(irq, flags, sizeof(flags));
  printf("range_result ok=true role=master attempt=%lu uncorrected_m=%.2f raw_reg=%ld rssi_dbm=%.1f snr_db=%.1f elapsed_ms=%lu irq=0x%04X flags=\"%s\"\n",
         static_cast<unsigned long>(attempt_id),
         static_cast<double>(uncorrected_m),
         static_cast<long>(raw_reg),
         static_cast<double>(rssi_dbm),
         static_cast<double>(snr_db),
         static_cast<unsigned long>(elapsed_ms),
         irq,
         flags);
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

  printf("slave_listen start role=slave timeout_ms=%lu\n",
         static_cast<unsigned long>(SlaveListenWindowMs));
  flushLog();

  (void)radio.clearIrqFlags(RADIOLIB_SX128X_IRQ_ALL);
  int16_t state = radio.startRanging(false, RangingAddress, RangingCalibration);
  if (state != RADIOLIB_ERR_NONE) {
    const uint16_t irq = radio.getIrqStatus();
    char flags[96];
    formatIrqFlags(irq, flags, sizeof(flags));
    printf("slave_listen ok=false elapsed_ms=%lu error=%d irq=0x%04X flags=\"%s\" note=\"startRanging failed\"\n",
           static_cast<unsigned long>(millis() - started_ms),
           state,
           irq,
           flags);
    flushLog();
    delayMs(500);
    return;
  }

  uint32_t next_log_ms = started_ms;
  while (readPin(BoardPins::lora_dio1) == 0 &&
         (millis() - started_ms) < SlaveListenWindowMs) {
    if ((int32_t)(millis() - next_log_ms) >= 0) {
      const uint16_t irq = radio.getIrqStatus();
      char flags[96];
      formatIrqFlags(irq, flags, sizeof(flags));
      printf("slave_listen alive elapsed_ms=%lu remaining_ms=%lu busy=%d dio1=%d irq=0x%04X flags=\"%s\"\n",
             static_cast<unsigned long>(millis() - started_ms),
             static_cast<unsigned long>(SlaveListenWindowMs - (millis() - started_ms)),
             readPin(BoardPins::lora_busy),
             readPin(BoardPins::lora_dio1),
             irq,
             flags);
      flushLog();
      next_log_ms = millis() + 1000;
    }
    delayMs(20);
  }

  if (readPin(BoardPins::lora_dio1) == 0) {
    const uint16_t irq = radio.getIrqStatus();
    (void)radio.finishRanging();
    char flags[96];
    formatIrqFlags(irq, flags, sizeof(flags));
    printf("slave_listen ok=false elapsed_ms=%lu error=%d irq=0x%04X flags=\"%s\" note=\"no master request observed\"\n",
           static_cast<unsigned long>(millis() - started_ms),
           RADIOLIB_ERR_RANGING_TIMEOUT,
           irq,
           flags);
    flushLog();
    return;
  }

  const uint16_t irq = radio.getIrqStatus();
  state = radio.finishRanging();
  char flags[96];
  formatIrqFlags(irq, flags, sizeof(flags));
  printf("slave_response ok=%s elapsed_ms=%lu error=%d irq=0x%04X flags=\"%s\"\n",
         state == RADIOLIB_ERR_NONE ? "true" : "false",
         static_cast<unsigned long>(millis() - started_ms),
         state,
         irq,
         flags);
  flushLog();
}

void setupApp() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  delayMs(1500);

  setupStatusLed();
  printf("[esp32s3-ranging] booting\n");
  printf("board=%s build=\"%s %s\"\n", BoardPins::board_name, __DATE__, __TIME__);
  flushLog();
  printPinMap();

  role = selectRole();
  printf("[esp32s3-ranging] role=%s\n", roleName(role));
  flushLog();
  printProfile();
  runPreRadioArmingDelay(role);
  initRadio();

  if (role == Role::Master) {
    next_master_exchange_ms = millis();
    printf("master_ready\n");
    flushLog();
  } else {
    slave_listen_start_ms = millis();
    next_slave_arming_log_ms = millis();
    printf("slave_ready note=\"serial monitor is optional; master logs are primary evidence\"\n");
    flushLog();
  }
}

void loopApp() {
  if (role == Role::Slave) {
    if ((int32_t)(millis() - slave_listen_start_ms) < 0) {
      if ((int32_t)(millis() - next_slave_arming_log_ms) >= 0) {
        printf("slave_arming remaining_ms=%ld\n",
               static_cast<long>(slave_listen_start_ms - millis()));
        flushLog();
        next_slave_arming_log_ms = millis() + 1000;
      }
      delayMs(20);
      return;
    }
    serviceSlave();
    return;
  }
  serviceMaster();
  delayMs(10);
}
}  // namespace

extern "C" void app_main() {
  setupApp();
  for (;;) {
    loopApp();
  }
}
