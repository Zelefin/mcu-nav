#include "BoardPins.h"
#include "ControlChannel.h"
#include "HealthStatus.h"
#include "Logger.h"

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nav/nav_events.h"
#include "nav/nav_gnss.h"
#include "nav/nav_nmea.h"

namespace {
static constexpr uart_port_t kGpsUart = UART_NUM_2;
static constexpr uint32_t kGpsBaud = 115200;
static constexpr uint32_t kNoByteFailMs = 5000;
static constexpr uint32_t kNoSentenceWarnMs = 5000;
static constexpr uint32_t kReportIntervalMs = 5000;
static constexpr uint32_t kParserWarnIntervalMs = 5000;

GpsHealthStatus status;
uint32_t lastByteMs = 0;
uint32_t lastSentenceMs = 0;
uint32_t lastFixMs = 0;
uint32_t lastParserWarnMs = 0;

uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void publishGpsStatus(HealthState state) {
  status.state = state;
  HealthStatus::setGps(status);
}

void updateStatusFromSample(const nav_gnss_sample_t &sample, uint32_t timestampMs) {
  status.nmeaSeen = true;
  status.validSentenceCount++;
  status.fixType = sample.fix_type;
  status.satelliteCount = sample.satellites;
  status.hdopCenti = sample.hdop_centi;
  status.latE7 = sample.position.lat_e7;
  status.lonE7 = sample.position.lon_e7;
  status.altMm = sample.position.alt_mm;
  lastSentenceMs = timestampMs;

  if (nav_gnss_sample_is_usable(&sample)) {
    status.fixSeen = true;
    lastFixMs = timestampMs;
  }
}

void preserveKnownAltitude(nav_gnss_sample_t *sample) {
  if (sample == nullptr || !sample->valid || sample->position.alt_mm != 0 || status.altMm == 0) {
    return;
  }
  if (sample->satellites == 0 && sample->hdop_centi == 0) {
    sample->position.alt_mm = status.altMm;
  }
}

uint8_t currentNodeId() {
  NodeConfig config = {};
  if (!ControlChannel::getConfig(&config)) {
    return 0u;
  }
  return config.nodeId;
}

void injectGnssSample(const nav_gnss_sample_t &sample, uint32_t timestampMs) {
  nav_event_t event = {};
  event.type = NAV_EVT_LOCAL_GNSS_SAMPLE;
  event.timestamp_ms = timestampMs;
  event.data.local_gnss = sample;
  ControlChannel::handleEvent(&event);
}

void handleParserResult(nav_nmea_result_t result, nav_gnss_sample_t *sample) {
  const uint32_t currentMs = nowMs();
  if (result == NAV_NMEA_RESULT_NONE) {
    return;
  }

  if (result == NAV_NMEA_RESULT_SAMPLE && sample != nullptr) {
    preserveKnownAltitude(sample);
    nav_gnss_sample_set_time(sample, currentMs);
    sample->node_id = currentNodeId();
    updateStatusFromSample(*sample, currentMs);
    injectGnssSample(*sample, currentMs);
    return;
  }

  if ((currentMs - lastParserWarnMs) >= kParserWarnIntervalMs) {
    lastParserWarnMs = currentMs;
    Logger::warnf("GPS", "NMEA parser result=%s", nav_nmea_result_to_string(result));
  }
}
}  // namespace

extern "C" void GpsHealthTask(void *) {
  Logger::infof("GPS",
                "UART started at %lu baud RX=%d TX=%d",
                static_cast<unsigned long>(kGpsBaud),
                BoardPins::gpsRx,
                BoardPins::gpsTx);

  uart_config_t uartConfig = {};
  uartConfig.baud_rate = kGpsBaud;
  uartConfig.data_bits = UART_DATA_8_BITS;
  uartConfig.parity = UART_PARITY_DISABLE;
  uartConfig.stop_bits = UART_STOP_BITS_1;
  uartConfig.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uartConfig.source_clk = UART_SCLK_DEFAULT;

  esp_err_t err = uart_param_config(kGpsUart, &uartConfig);
  if (err == ESP_OK) {
    err = uart_set_pin(kGpsUart, BoardPins::gpsTx, BoardPins::gpsRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  }
  if (err == ESP_OK) {
    err = uart_driver_install(kGpsUart, 2048, 0, 0, nullptr, 0);
  }
  if (err != ESP_OK) {
    publishGpsStatus(HealthState::Fail);
    Logger::failf("GPS", "UART init failed, esp_err=%d", err);
    vTaskDelete(nullptr);
  }

  nav_nmea_parser_t parser;
  nav_nmea_parser_init(&parser);
  uint32_t lastReportMs = 0;
  uint32_t previousChars = 0;
  uint32_t previousSentences = 0;

  for (;;) {
    uint8_t buffer[128];
    const int bytesRead = uart_read_bytes(kGpsUart, buffer, sizeof(buffer), pdMS_TO_TICKS(20));
    if (bytesRead > 0) {
      const uint32_t currentMs = nowMs();
      status.bytesSeen = true;
      status.bytesReceived += static_cast<uint32_t>(bytesRead);
      lastByteMs = currentMs;
      for (int i = 0; i < bytesRead; ++i) {
        nav_gnss_sample_t sample = {};
        const nav_nmea_result_t result = nav_nmea_parser_push(&parser, buffer[i], &sample);
        handleParserResult(result, &sample);
      }
    }

    const uint32_t currentMs = nowMs();
    if ((currentMs - lastReportMs) >= kReportIntervalMs) {
      lastReportMs = currentMs;

      const uint32_t charCount = status.bytesReceived;
      const uint32_t sentenceCount = status.validSentenceCount;
      const bool newChars = (charCount != previousChars);
      const bool newSentences = (sentenceCount != previousSentences);
      previousChars = charCount;
      previousSentences = sentenceCount;

      if (!status.bytesSeen || (lastByteMs != 0 && (currentMs - lastByteMs) > kNoByteFailMs)) {
        publishGpsStatus(HealthState::Fail);
        Logger::failf("GPS", "No GPS bytes received in the last %lu seconds", static_cast<unsigned long>(kNoByteFailMs / 1000));
      } else if (!status.nmeaSeen || (lastSentenceMs != 0 && (currentMs - lastSentenceMs) > kNoSentenceWarnMs)) {
        publishGpsStatus(HealthState::Warn);
        Logger::warnf("GPS",
                      "GPS bytes are present but valid NMEA samples are not confirmed yet chars=%lu new_chars=%s",
                      static_cast<unsigned long>(charCount),
                      newChars ? "yes" : "no");
      } else if (lastFixMs == 0 || (currentMs - lastFixMs) > kNoSentenceWarnMs) {
        publishGpsStatus(HealthState::Warn);
        Logger::warnf("GPS",
                      "NMEA ok, waiting for 3D fix sentences=%lu new_sentences=%s sats=%lu hdop_centi=%u",
                      static_cast<unsigned long>(sentenceCount),
                      newSentences ? "yes" : "no",
                      static_cast<unsigned long>(status.satelliteCount),
                      static_cast<unsigned>(status.hdopCenti));
      } else {
        publishGpsStatus(HealthState::Ok);
        Logger::okf("GPS",
                    "GNSS fix ok lat_e7=%ld lon_e7=%ld alt_mm=%ld fix_type=%d sats=%lu hdop_centi=%u age_ms=%lu usage=%s",
                    static_cast<long>(status.latE7),
                    static_cast<long>(status.lonE7),
                    static_cast<long>(status.altMm),
                    static_cast<int>(status.fixType),
                    static_cast<unsigned long>(status.satelliteCount),
                    static_cast<unsigned>(status.hdopCenti),
                    static_cast<unsigned long>(currentMs - lastFixMs),
                    ControlChannel::isGpsEnabled() ? "enabled" : "disabled");
      }
    }
  }
}
