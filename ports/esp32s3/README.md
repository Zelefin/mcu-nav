# ESP32-S3 Port Placeholder

This directory is reserved for a future ESP32-S3 host port of the navigation
brain. The portable core must remain independent from ESP-IDF, FreeRTOS, UART
drivers, and board-specific headers.

Expected responsibilities for this port:

- Adapt GNSS driver samples into `nav_event_t`.
- Adapt radio-coprocessor UART/SPI frames into `nav_event_t`.
- Forward structured logs and CSV streams to the selected debug sink.
- Provide a timer source and persistent configuration storage.

The ESP8285/SX1280 radio firmware is not implemented in this repository.
