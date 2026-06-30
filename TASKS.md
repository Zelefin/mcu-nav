# TASKS — Фаза 2: реальні драйвери → `core/`

Продовження переархітектури. Фази 0, 1, 3, 4 завершено й верифіковано (див.
нижче "Контекст"). Лишилась **Фаза 2**: під'єднати справжні радіо/GPS-драйвери,
щоб годувати навігаційне ядро реальними даними замість `nav_mock`.

## Ціль

На платі зараз сусіди беруться з `nav_mock` (інжекція синтетичних подій). Треба:

1. **GPS → подія ядра.** Драйвер GPS читає NMEA по UART і віддає
   `NAV_EVT_LOCAL_GNSS_SAMPLE` у `core/`.
2. **Радіо TX/TDMA.** У TX-слоті (`nav_tdma_action_at` → `NAV_TDMA_TX_BEACON`)
   запакувати власну телеметрію через `nav_telemetry_encode_beacon` і передати по
   SX1280. У ranging-слоті (`NAV_TDMA_RANGE_PEER`) запустити апаратний
   SX1280 ranging engine, а не оцінку дистанції з RSSI чи packet timing.
3. **Радіо RX → подія ядра.** Прийняті байти SX1280 → `nav_telemetry_decode_event`
   → `nav_core_handle_event` (телеметрія сусідів / ranging-результати).
4. **Pair ranges для control-app.** Ranging payload має нести `from_id`/`to_id`,
   щоб нода могла показувати відстані між будь-якою парою вузлів, включно з
   third-party парами, де локальна нода не є endpoint.
5. **Замінити mock на реальні дані** як основне джерело; `nav_mock` лишити як
   опційний debug-режим (тогл уже є в control-app/ControlChannel).

Після цього: реальна нода будує network view і трилатерується з фізичних
сусідів; SpeedyBee використовується для дебагу ranging без GPS.

## Що вже є (база для рефактору)

- **ESP-IDF радіо**: [src/RadioHealth.cpp](src/RadioHealth.cpp) — ініт SX1280
  через RadioLib + [include/EspIdfRadioLibHal.h](include/EspIdfRadioLibHal.h),
  TX health-пакетів, RX по DIO1-переривання. Звідси брати ініт/SPI/IRQ.
- **ESP-IDF GPS**: [src/GpsHealth.cpp](src/GpsHealth.cpp) — UART2 NMEA-читання
  й парс GGA/RMC. Переюзати UART-частину; для парсингу взяти портативний
  [core/src/nav_nmea.c](core/src/nav_nmea.c) (`nav_nmea_*` → `nav_gnss_sample_t`).
- **Control/ядро**: [src/ControlChannel.cpp](src/ControlChannel.cpp) володіє
  `nav_system_t gNav`, `nav_mock_t gMock`, мьютексом і emitter/reader-тасками.
  Сюди підключати реальні джерела подій (під тим самим мьютексом).
- **OTA-кодек**: [core/include/nav/nav_telemetry.h](core/include/nav/nav_telemetry.h)
  — `nav_telemetry_encode_beacon`, `nav_telemetry_encode_range_result`,
  `nav_telemetry_decode_event`.
- **Розклад**: [core/include/nav/nav_tdma.h](core/include/nav/nav_tdma.h) —
  `nav_tdma_action_at(now_ms)` → TX_BEACON / RANGE_PEER / LISTEN.
- **ESP32-S3 ranging приклад**: [examples/esp32s3-ranging](examples/esp32s3-ranging)
  — self-contained ESP-IDF/RadioLib приклад з `startRanging(true/false, ...)`,
  `finishRanging()`, `getRangingResultRaw()` і `getRangingResult()`. Це головний
  reference для ESP32 ranging-slot інтеграції.
- **SpeedyBee радіо**: [ports/speedybee/lib/SX1280](ports/speedybee/lib/SX1280) +
  референс-прошивка ranging [ports/speedybee/reference/ranging_main.cpp](ports/speedybee/reference/ranging_main.cpp)
  (готовий ranging-обмін master/slave, калібрування, bias-корекція).

## Кроки

### 2.1 GPS-адаптер (ESP-IDF)
- Винести UART-читання з `GpsHealth.cpp` у драйвер, що подає сирі байти в
  `nav_nmea`-парсер; на валідний fix формувати `NAV_EVT_LOCAL_GNSS_SAMPLE` і
  слати `nav_core_handle_event(&gNav, ...)` (під мьютексом ControlChannel).
- Коли `gConfig.gpsEnabled == false` — GPS-події ігнорувати (нода трилатерується).
- Висота: при валідному GPS — реальна; інакше fallback на `gConfig.altitudeMm`
  (логіка вже у ControlChannel `feedAltitude`).
- Перевірка (host): юніт на `nav_nmea` вже є; на платі — лог `GPS fix` + поле
  координат у control-app.

### 2.2 Радіо-абстракція + TX
- Ввести тонкий інтерфейс радіо (наприклад `RadioLink`): `init()`, `send(bytes,len)`,
  `poll(rx_buf, &len, &rssi, &snr)`. ESP-IDF-реалізація поверх RadioLib/HAL з
  `RadioHealth.cpp`; SpeedyBee-реалізація поверх `lib/SX1280`.
- TDMA-таска: щотіку `nav_tdma_action_at(now)`; на `TX_BEACON` —
  `nav_telemetry_encode_beacon(власна_телеметрія)` → `RadioLink::send`.
- Власна телеметрія: зібрати `nav_peer_telemetry_t` для локальної ноди зі snapshot
  (позиція, режим, gnss_valid, packet_seq++).

### 2.3 Радіо RX → події
- У `poll()` отримані кадри → `nav_telemetry_decode_event(bytes,len,now,rssi,snr,&ev)`
  → `nav_core_handle_event(&gNav,&ev)` (під мьютексом).
- `NAV_STATUS_NOT_IMPLEMENTED`/`BAD_FRAME` — тихо ігнорувати (лог trace).

### 2.4 Ranging
- На `NAV_TDMA_RANGE_PEER(peer)` — ініціювати two-way ranging через SX1280
  ranging engine. Для ESP32 брати процедуру з
  `examples/esp32s3-ranging/src/main.cpp`: scheduled initiator працює як
  ranging master (`startRanging(true, ...)`), peer у цьому слоті слухає як
  ranging slave (`startRanging(false, ...)`), після DIO1/timeout викликається
  `finishRanging()`.
- Успішний master result (`getRangingResultRaw()` / `getRangingResult()`, після
  обраної calibration/correction політики) → `nav_range_result_t` →
  `NAV_EVT_RANGE_RESULT`. Невдача/timeout/BUSY/engine error →
  `NAV_EVT_RANGE_FAIL`.
- Перед hardware TDMA інтеграцією розширити range/result/fail payload з
  implicit-local `peer_id` до endpoint-bearing `from_id`/`to_id`: `from_id` =
  scheduled initiator / SX1280 ranging master, `to_id` = scheduled peer /
  SX1280 ranging slave.
- RSSI/SNR з ranging exchange логувати і передавати як diagnostics, але не
  використовувати як distance source.
- Узгодити RF-профіль (freq/SF/BW/калібрування) між усіма нодами; винести в
  build_flags (як у `platformio.ini`).

### 2.5 Pair-range store + control-app
- Додати окреме сховище pair-range observations, не змішуючи його з
  `nav_peer_state_t` anchor fields. Локальні endpoint ranges можуть оновлювати
  peer table для solver; third-party ranges мають лишатися network-health даними.
- Розширити serial JSON для control-app: додати `ranges[]` або малу матрицю з
  `from_id`, `to_id`, `range_mm`, freshness/age, validity, RSSI/SNR,
  `request_id`, `range_fail_reason`.
- Оновити `control-app/index.html`, щоб бачити A-B/A-C/B-C range health на
  кожній ноді, навіть коли локальна нода не є endpoint конкретного range.
- В одному changeset з C API зміною оновити `docs/replay_csv.md`, replay
  fixtures і replay/telemetry/serial-json тести, щоб `from_id`/`to_id` були
  replayable та deterministic.

### 2.6 Членство мережі (TDMA members)
- Поки що задати членів статично з конфіга (список node_id). Пізніше —
  автовиявлення з прийнятої телеметрії. `nav_tdma_set_members(...)`.

### 2.7 Mock як опційний режим
- Залишити `nav_mock` під `gConfig.mockEnabled`; коли увімкнено — інжектити
  замість/поверх радіо (для дебагу однієї плати). За замовчуванням на ESP32 — off
  (реальне радіо), на SpeedyBee — за потреби.

## Опційно (рефактор структури, низький пріоритет)
- Перенести ESP-IDF `src/` + `include/` у `ports/esp32/` (план у
  [velvet-roaming-neumann.md](.claude/plans/velvet-roaming-neumann.md)). Зараз
  ESP-IDF-нода живе в корені `src/` — працює, але асиметрично зі SpeedyBee.

## Верифікація
- Host: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
  (бейзлайн — 5 наявних падінь: matplotlib-plot + `test_radio_navigation`; не
  погіршувати).
- Збірка 3 env: `pio run -e nodemcu-32s`, `-e esp32-s3-devkitc-1`, `-e speedybee`
  (через `~/.platformio/penv/bin/pio`).
- На залізі: 2 ESP32 поряд → у control-app взаємно видно ноди, ranging у метрах,
  телеметрія оновлюється; 3+ ESP32 → кожна нода бачить pair ranges між іншими
  нодами; з GPS — координати, без GPS — трилатерація. 2 SpeedyBee → дебаг
  ranging без GPS.

## Контекст: що вже зроблено (попередні сесії)
- **Фаза 0**: видалено `ports/stm32`, `examples/sb24tx`; ADR 0002 superseded;
  `docs/radio_protocol.md` переписано на on-device OTA; оновлено CONTEXT/README/
  CONTRIBUTING.
- **Фаза 1**: `nav_mock`, `nav_serial_json`, `nav_telemetry`, `nav_tdma`,
  `nav_trilat_distance_m`; оновлено `ports/posix` demo; тести
  `test_mock/serial_json/telemetry/tdma` — зелені.
- **Config/persist/control**: `NodeConfig` (NVS), `ControlChannel` (USB-serial
  JSON) у ESP-IDF; компіл-верифіковано на обох ESP32.
- **Фаза 3**: `ports/speedybee` (Arduino/ESP8266) на спільному `core/`, EEPROM;
  `env:speedybee` у `platformio.ini`; збірка успішна.
- **Фаза 4**: `control-app/index.html` (Web Serial, network view, тоглі GPS/mock,
  rename, altitude).
- Рішення архітектури — у [.claude/plans/velvet-roaming-neumann.md](.claude/plans/velvet-roaming-neumann.md).

> Примітка: у попередніх сесіях зміни **не комітились** — усе в робочому дереві.
