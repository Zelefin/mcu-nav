#pragma once

#include <RadioLib.h>

#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace EspIdfRadioLibHalInternal {
static void (*gInterruptCallbacks[GPIO_NUM_MAX])(void) = {};
static bool gIsrServiceInstalled = false;

static void IRAM_ATTR interruptThunk(void *arg) {
  const int pin = static_cast<int>(reinterpret_cast<intptr_t>(arg));
  if (pin >= 0 && pin < GPIO_NUM_MAX && gInterruptCallbacks[pin] != nullptr) {
    gInterruptCallbacks[pin]();
  }
}
}  // namespace EspIdfRadioLibHalInternal

class EspIdfRadioLibHal : public RadioLibHal {
 public:
  EspIdfRadioLibHal(int sck, int miso, int mosi)
      : RadioLibHal(kInput, kOutput, kLow, kHigh, kRising, kFalling),
        sck_(sck),
        miso_(miso),
        mosi_(mosi) {}

  void init() override {
    spiBegin();
  }

  void term() override {
    spiEnd();
  }

  void pinMode(uint32_t pin, uint32_t mode) override {
    if (pin == RADIOLIB_NC || pin >= GPIO_NUM_MAX) {
      return;
    }

    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << pin);
    config.mode = mode == kOutput ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&config);
  }

  void digitalWrite(uint32_t pin, uint32_t value) override {
    if (pin == RADIOLIB_NC || pin >= GPIO_NUM_MAX) {
      return;
    }
    gpio_set_level(static_cast<gpio_num_t>(pin), value == kHigh ? 1 : 0);
  }

  uint32_t digitalRead(uint32_t pin) override {
    if (pin == RADIOLIB_NC || pin >= GPIO_NUM_MAX) {
      return kLow;
    }
    return gpio_get_level(static_cast<gpio_num_t>(pin)) == 0 ? kLow : kHigh;
  }

  void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override {
    if (interruptNum == RADIOLIB_NC || interruptNum >= GPIO_NUM_MAX) {
      return;
    }

    using namespace EspIdfRadioLibHalInternal;
    if (!gIsrServiceInstalled) {
      const esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
      gIsrServiceInstalled = (err == ESP_OK || err == ESP_ERR_INVALID_STATE);
    }

    gInterruptCallbacks[interruptNum] = interruptCb;
    gpio_set_intr_type(static_cast<gpio_num_t>(interruptNum),
                       mode == kFalling ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(static_cast<gpio_num_t>(interruptNum),
                         interruptThunk,
                         reinterpret_cast<void *>(static_cast<intptr_t>(interruptNum)));
  }

  void detachInterrupt(uint32_t interruptNum) override {
    if (interruptNum == RADIOLIB_NC || interruptNum >= GPIO_NUM_MAX) {
      return;
    }

    using namespace EspIdfRadioLibHalInternal;
    gpio_isr_handler_remove(static_cast<gpio_num_t>(interruptNum));
    gpio_set_intr_type(static_cast<gpio_num_t>(interruptNum), GPIO_INTR_DISABLE);
    gInterruptCallbacks[interruptNum] = nullptr;
  }

  void delay(RadioLibTime_t ms) override {
    if (ms == 0) {
      return;
    }
    TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ticks == 0) {
      ticks = 1;
    }
    vTaskDelay(ticks);
  }

  void delayMicroseconds(RadioLibTime_t us) override {
    esp_rom_delay_us(us);
  }

  RadioLibTime_t millis() override {
    return static_cast<RadioLibTime_t>(esp_timer_get_time() / 1000ULL);
  }

  RadioLibTime_t micros() override {
    return static_cast<RadioLibTime_t>(esp_timer_get_time());
  }

  long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override {
    if (pin == RADIOLIB_NC || pin >= GPIO_NUM_MAX) {
      return 0;
    }

    const RadioLibTime_t start = micros();
    while (digitalRead(pin) == state) {
      if ((micros() - start) > timeout) {
        return 0;
      }
    }

    while (digitalRead(pin) != state) {
      if ((micros() - start) > timeout) {
        return 0;
      }
    }

    const RadioLibTime_t pulseStart = micros();
    while (digitalRead(pin) == state) {
      if ((micros() - pulseStart) > timeout) {
        return 0;
      }
    }

    return static_cast<long>(micros() - pulseStart);
  }

  void spiBegin() override {
    if (!busInitialized_) {
      spi_bus_config_t busConfig = {};
      busConfig.mosi_io_num = mosi_;
      busConfig.miso_io_num = miso_;
      busConfig.sclk_io_num = sck_;
      busConfig.quadwp_io_num = -1;
      busConfig.quadhd_io_num = -1;
      busConfig.max_transfer_sz = 512;

      const esp_err_t err = spi_bus_initialize(host_, &busConfig, SPI_DMA_CH_AUTO);
      busInitialized_ = (err == ESP_OK || err == ESP_ERR_INVALID_STATE);
    }

    if (busInitialized_ && spi_ == nullptr) {
      spi_device_interface_config_t deviceConfig = {};
      deviceConfig.clock_speed_hz = 2000000;
      deviceConfig.mode = 0;
      deviceConfig.spics_io_num = -1;
      deviceConfig.queue_size = 1;
      spi_bus_add_device(host_, &deviceConfig, &spi_);
    }
  }

  void spiBeginTransaction() override {}

  void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override {
    if (spi_ == nullptr || out == nullptr || len == 0) {
      if (in != nullptr && len > 0) {
        memset(in, 0, len);
      }
      return;
    }

    spi_transaction_t transaction = {};
    transaction.length = len * 8;
    transaction.tx_buffer = out;
    transaction.rx_buffer = in;

    const esp_err_t err = spi_device_transmit(spi_, &transaction);
    if (err != ESP_OK && in != nullptr) {
      memset(in, 0, len);
    }
  }

  void spiEndTransaction() override {}

  void spiEnd() override {
    if (spi_ != nullptr) {
      spi_bus_remove_device(spi_);
      spi_ = nullptr;
    }
  }

 private:
  static constexpr uint32_t kInput = 0;
  static constexpr uint32_t kOutput = 1;
  static constexpr uint32_t kLow = 0;
  static constexpr uint32_t kHigh = 1;
  static constexpr uint32_t kRising = 1;
  static constexpr uint32_t kFalling = 2;

  spi_host_device_t host_ = SPI2_HOST;
  int sck_;
  int miso_;
  int mosi_;
  bool busInitialized_ = false;
  spi_device_handle_t spi_ = nullptr;
};
