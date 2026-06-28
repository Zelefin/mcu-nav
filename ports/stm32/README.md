# STM32 Port Placeholder

This directory is reserved for a future STM32 host port of the navigation core.
The core library must not include STM32 HAL headers or depend on RTOS objects.

Expected responsibilities for this port:

- Convert board GNSS/radio/timer callbacks into core events.
- Transport structured logs to UART, SWO, storage, or a telemetry bridge.
- Keep all HAL, linker, startup, and board configuration files outside `core/`.
