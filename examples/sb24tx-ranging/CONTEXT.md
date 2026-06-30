# nav-mcu

This context defines the language for the navigation MCU firmware, its radio
boundary, and hardware bring-up artifacts.

## Language

**Hardware bring-up example**:
A repository-local example used to validate wiring, chip behavior, and a narrow
hardware capability before production ownership is settled.
_Avoid_: Production firmware, radio firmware home

**Ranging master**:
The SX1280 ranging role that initiates a ranging exchange and owns the readable
distance result.
_Avoid_: Initiator, laptop module

**Ranging slave**:
The SX1280 ranging role that waits for a matching ranging request and sends the
automatic ranging response.
_Avoid_: Responder, powered-only module

**Ranging address**:
The 32-bit SX1280 address embedded in a ranging request that decides which
ranging slave may answer the exchange.
_Avoid_: Node ID, peer ID, frame sequence

**Corrected range**:
The human-facing distance after applying short-range compensation to the raw
SX1280 ranging result.
_Avoid_: Raw range, register result

**Ranging exchange**:
One master-initiated SX1280 request/response attempt that ends in either a
readable master result or a reported failure.
_Avoid_: Sample window, navigation range result

**RF front-end path**:
The board-level transmit or receive signal path selected outside the SX1280
chip before a ranging exchange.
_Avoid_: Ranging role, distance correction
