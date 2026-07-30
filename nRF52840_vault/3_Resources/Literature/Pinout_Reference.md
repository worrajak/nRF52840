---
source: pinout.md
fetched: 2026-07-30
topic: pinout, nice-nano, nrf52840, gpio-reference
used_in: "[[_project-brief]]"
---

# Nice!Nano Pinout Reference

## Physical Layout

| Label | GPIO | nRF52840 | Function |
|-------|------|----------|----------|
| D0 | 8 | P0.08 | UART RX |
| D1 | 6 | P0.06 | UART TX |
| D2 | 17 | P0.17 | GPIO |
| D3 | 20 | P0.20 | GPIO |
| D4 | 13 | P0.13 | GPIO |
| D5 | 24 | P0.24 | GPIO |
| D6 | 9 | P0.09 | GPIO |
| D7 | 10 | P0.10 | GPIO |
| D8 | 30 | P0.30 | GPIO |
| D9 | 31 | P0.31 | Battery ADC |
| F4 | 2 | P0.02 | LoRa MISO |
| F5 | 29 | P0.29 | LoRa DIO0 |
| F6 | 23 | P0.23 | GPIO |
| F7 | 22 | P0.22 | GPIO |
| F8 | 19 | P0.19 | GPIO |
| F9 | 11 | P0.11 | OLED SCL |
| F10 | 27 | P0.27 | GPIO |

## Special Pins
- LED (Red): P0.15 (GPIO 15)
- I2C: SDA=P0.10, SCL=P0.09 (pull-up included)
- SPI (if used): MOSI=P0.08, MISO=P0.06, SCK=P0.27

## Links
- [[SCHEMATIC_PIN_ANALYSIS]]
- [[STM32_TO_NRF52840_MAPPING]]
- [[_project-brief]]
