#include "StatusLed.h"

#include <cstdint>

#include "BoardPins.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr uint32_t kPulseMs = 40u;
constexpr uint32_t kTaskPeriodMs = 10u;
constexpr uint32_t kWs2812ResolutionHz = 10000000u;
constexpr uint8_t kRgbPulseGrb[3] = {24u, 0u, 0u};
constexpr uint8_t kRgbOffGrb[3] = {0u, 0u, 0u};

portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t gTask = nullptr;
bool gReady = false;
bool gLedOn = false;
uint32_t gPulseUntilMs = 0u;

rmt_channel_handle_t gRgbChannel = nullptr;
rmt_encoder_handle_t gRgbEncoder = nullptr;

uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }

bool elapsed(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

bool plainLedAvailable() {
  return BoardPins::statusLed >= 0 && BoardPins::statusLed < GPIO_NUM_MAX;
}

bool rgbLedAvailable() {
  return BoardPins::statusRgbLed >= 0 && BoardPins::statusRgbLed < GPIO_NUM_MAX;
}

uint64_t gpioMask(int pin) { return 1ULL << static_cast<unsigned>(pin); }

void setPlainLed(bool on) {
  if (!plainLedAvailable()) {
    return;
  }
  const int level = BoardPins::statusLedActiveHigh == on ? 1 : 0;
  gpio_set_level(static_cast<gpio_num_t>(BoardPins::statusLed), level);
}

bool initRgbLed() {
  if (!rgbLedAvailable()) {
    return false;
  }

  rmt_tx_channel_config_t channelConfig = {};
  channelConfig.gpio_num = static_cast<gpio_num_t>(BoardPins::statusRgbLed);
  channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  channelConfig.resolution_hz = kWs2812ResolutionHz;
  channelConfig.mem_block_symbols = 64;
  channelConfig.trans_queue_depth = 2;
  channelConfig.flags.with_dma = false;
  if (rmt_new_tx_channel(&channelConfig, &gRgbChannel) != ESP_OK) {
    gRgbChannel = nullptr;
    return false;
  }

  rmt_bytes_encoder_config_t encoderConfig = {};
  encoderConfig.bit0.duration0 = 3;
  encoderConfig.bit0.level0 = 1;
  encoderConfig.bit0.duration1 = 9;
  encoderConfig.bit0.level1 = 0;
  encoderConfig.bit1.duration0 = 9;
  encoderConfig.bit1.level0 = 1;
  encoderConfig.bit1.duration1 = 3;
  encoderConfig.bit1.level1 = 0;
  encoderConfig.flags.msb_first = true;
  if (rmt_new_bytes_encoder(&encoderConfig, &gRgbEncoder) != ESP_OK) {
    gRgbEncoder = nullptr;
    gRgbChannel = nullptr;
    return false;
  }

  if (rmt_enable(gRgbChannel) != ESP_OK) {
    gRgbEncoder = nullptr;
    gRgbChannel = nullptr;
    return false;
  }

  return true;
}

void setRgbLed(bool on) {
  if (gRgbChannel == nullptr || gRgbEncoder == nullptr) {
    return;
  }

  rmt_transmit_config_t txConfig = {};
  txConfig.loop_count = 0;
  txConfig.flags.eot_level = 0;
  txConfig.flags.queue_nonblocking = true;

  const uint8_t *payload = on ? kRgbPulseGrb : kRgbOffGrb;
  (void)rmt_transmit(gRgbChannel, gRgbEncoder, payload, 3, &txConfig);
}

void setLed(bool on) {
  if (gRgbChannel != nullptr) {
    setRgbLed(on);
    return;
  }
  setPlainLed(on);
}

void statusLedTask(void *) {
  for (;;) {
    bool turnOff = false;
    const uint32_t now = nowMs();
    portENTER_CRITICAL(&gLock);
    if (gLedOn && elapsed(now, gPulseUntilMs)) {
      gLedOn = false;
      turnOff = true;
    }
    portEXIT_CRITICAL(&gLock);

    if (turnOff) {
      setLed(false);
    }
    vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
  }
}
}  // namespace

namespace StatusLed {
void begin() {
  if (gReady || (!plainLedAvailable() && !rgbLedAvailable())) {
    return;
  }

  const bool haveRgbLed = initRgbLed();
  const bool havePlainLed = !haveRgbLed && plainLedAvailable();
  if (havePlainLed) {
    gpio_config_t config = {};
    config.pin_bit_mask = gpioMask(BoardPins::statusLed);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&config);
  }
  if (!haveRgbLed && !havePlainLed) {
    return;
  }

  gReady = true;
  setLed(false);
  xTaskCreate(statusLedTask, "StatusLedTask", 2048, nullptr, 1, &gTask);
}

void pulse() {
  if (!gReady) {
    return;
  }

  bool turnOn = false;
  const uint32_t pulseUntilMs = nowMs() + kPulseMs;
  portENTER_CRITICAL(&gLock);
  gPulseUntilMs = pulseUntilMs;
  if (!gLedOn) {
    gLedOn = true;
    turnOn = true;
  }
  portEXIT_CRITICAL(&gLock);

  if (turnOn) {
    setLed(true);
  }
}
}  // namespace StatusLed
