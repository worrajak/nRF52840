# nRF52840 LoRa P2P Node

LoRa P2P sensor node on **Pro Micro nRF52840 (NiceNano)** compatible with STM32 LoRa P2P network.

## Features

- **Dual radio support**: SX1262 and RFM95/SX1276 — switch via `#define` at compile time
- RadioHead-compatible packet format (interoperable with STM32 nodes using RH_RF95)
- **Multi-hop relay** with duplicate detection, echo prevention, and message queue
- **BLE GPS**: receive GPS coordinates from phone app, include in LoRa payload
- **Node tracking**: OLED shows distance/direction to other nodes with compass arrow
- XOR encryption + CRC16 (matches STM32 network protocol)
- BME280 temperature/humidity sensor (auto-detect, simulated fallback)
- SSD1306 OLED display (128x64, I2C) with status, GPS info, and debug log
- Software bit-bang SPI for LoRa module (bypasses n-able core PINS_COUNT=34 bug)
- Battery voltage monitoring
- Broadcast TX every 30 seconds, continuous RX

## Hardware

### Board
- Pro Micro nRF52840 / NiceNano v2
- Framework: [n-able Arduino core](https://github.com/LeeorNahum/platform-n-able-pro-micro-nrf52840)

### Pin Assignments

| Function | Pin | GPIO | Notes |
|----------|-----|------|-------|
| **OLED SDA** | P1.04 | 36 | I2C Data |
| **OLED SCL** | P0.11 | 11 | I2C Clock |
| **LoRa MISO** | P0.02 | 2 | SPI Master In |
| **LoRa SCK** | P1.11 | 43 | SPI Clock (Port 1!) |
| **LoRa MOSI** | P1.15 | 47 | SPI Master Out (Port 1!) |
| **LoRa CS** | P1.13 | 45 | SPI Chip Select (Port 1!) |
| **LoRa IRQ** | P0.29 | 29 | DIO0 (RFM95) or DIO1 (SX1262) |
| **LoRa RST** | P0.09 | 9 | Module reset |
| **LoRa BUSY** | P0.03 | 3 | SX1262 only |
| **Status LED** | P0.15 | 15 | Onboard LED |
| **Battery** | P0.31 | 31 | ADC battery voltage |

> **Important**: SCK(43), CS(45), MOSI(47) are Port 1 GPIOs (>= 32).
> The n-able Arduino core has `PINS_COUNT=34`, so `digitalWrite()`/`pinMode()` silently fail for these pins.
> This firmware uses `nrf_gpio_*` SDK functions directly to bypass this limitation.

### Wiring Diagram

```
Pro Micro nRF52840          RFM95W / SX1262
┌──────────┐              ┌──────────┐
│    P0.02 ├──────────────┤ MISO     │
│    P1.11 ├──────────────┤ SCK      │
│    P1.15 ├──────────────┤ MOSI     │
│    P1.13 ├──────────────┤ NSS (CS) │
│    P0.29 ├──────────────┤ DIO0/DIO1│
│    P0.09 ├──────────────┤ RST      │
│    P0.03 ├──────────────┤ BUSY     │  ← SX1262 only
│      3V3 ├──────────────┤ VCC      │
│      GND ├──────────────┤ GND      │
└──────────┘              └──────────┘

Pro Micro nRF52840          SSD1306 OLED
┌──────────┐              ┌──────────┐
│    P1.04 ├──────────────┤ SDA      │
│    P0.11 ├──────────────┤ SCL      │
│      3V3 ├──────────────┤ VCC      │
│      GND ├──────────────┤ GND      │
└──────────┘              └──────────┘
```

## LoRa Configuration

| Parameter | Value |
|-----------|-------|
| Frequency | 923 MHz |
| Spreading Factor | 7 |
| Bandwidth | 125 kHz |
| Coding Rate | 4/5 |
| Sync Word | 0x12 |
| TX Power | 12 dBm (PA_BOOST) |
| Preamble | 8 symbols |
| Node ID | 116 (configurable) |

## Protocol

### Packet Structure (over-the-air)

```
[Preamble][SyncWord][Length][ RadioHead Header (4 bytes) ][ Encrypted Payload ][ CRC16 ][ HW CRC ]
                            [ TO ][ FROM ][ ID ][ FLAGS    ]
                            [0xFF][ 116  ][ seq][ hop_count]
```

- **TO**: 0xFF (broadcast) or destination node ID
- **FROM**: sender node ID (116) — relay changes this to relay node ID
- **ID**: sequence number (auto-increment)
- **FLAGS**: lower 4 bits = hop count (0 = origin, incremented per relay)

### Payload Format

Plaintext before encryption:
```
N:116|S:0|T:28|H:65|B:3300|LA:13.736717|LO:100.523186
```

| Field | Description |
|-------|-------------|
| N | Original source node ID (preserved through relay) |
| S | Sequence number |
| T | Temperature (°C, from BME280 or simulated) |
| H | Humidity (%, from BME280 or simulated) |
| B | Battery voltage (mV) |
| LA | Latitude (from BLE GPS, optional) |
| LO | Longitude (from BLE GPS, optional) |

### Relay / Multi-Hop

- Relay triggers when RSSI <= -100 dBm and hop count < 3
- Duplicate detection: source node + sequence number cache (TTL 5 min)
- Echo prevention: skip packets where payload `N:` or header `FROM` matches own ID
- Queued forwarding with random delay (1-3s) to reduce collisions
- OLED shows: `FWD N:42 T:30 H:60%` + hop/RSSI details

### Encryption

- XOR with key `"1234567890000000"` (16 bytes, repeating)
- CRC16 (polynomial 0xA001, init 0xFFFF) on encrypted data
- CRC appended as 2 bytes (MSB first)

## Build & Upload

```bash
# Build
pio run

# Upload
pio run --target upload

# Serial monitor
pio device monitor --baud 115200
```

## Known Issues (n-able Arduino Core)

### PINS_COUNT = 34 Bug
The n-able variant defines `PINS_COUNT = 34`. All Arduino GPIO functions (`digitalWrite`, `pinMode`, etc.) silently ignore pins >= 34. This affects all Port 1 GPIOs used by SPI (SCK=43, CS=45, MOSI=47).

**Workaround**: Use `nrf_gpio_*` SDK functions directly:
```cpp
#include <nrf_gpio.h>
nrf_gpio_cfg_output(pin);   // instead of pinMode(pin, OUTPUT)
nrf_gpio_pin_set(pin);      // instead of digitalWrite(pin, HIGH)
nrf_gpio_pin_clear(pin);    // instead of digitalWrite(pin, LOW)
nrf_gpio_pin_read(pin);     // instead of digitalRead(pin)
```

### OLED Init Pitfalls
- Do NOT use `Wire.setClock(400000)` -- causes SSD1306 instability
- Do NOT call `i2cDevicePresent()` before `display.begin()` -- corrupts I2C bus
- Must have `delay(2000)` after `Serial.begin()` for power stabilization

## Radio Module Selection

Both **SX1262** and **RFM95 (SX1276)** are supported in a single firmware. Switch by editing the `#define` at the top of `src/main.cpp`:

```cpp
// เลือก module โดย uncomment อันที่ใช้ (เลือกได้อันเดียว)
//#define USE_SX1262          // SX1262 module (command-based SPI, DIO1+BUSY)
#define USE_RFM95         // RFM95/SX1276 module (register-based SPI, DIO0)
```

| | RFM95 (SX1276) | SX1262 |
|---|---|---|
| SPI protocol | Register-based | Command-based |
| Interrupt pin | DIO0 | DIO1 |
| BUSY pin | Not needed | Required (P0.03) |
| Max TX power | +20 dBm | +22 dBm |

## Project Structure

```
├── src/
│   └── main.cpp              # Main firmware (SX1262/RFM95 + BLE GPS + relay)
├── platformio.ini             # PlatformIO build config
└── README.md
```

## STM32 Compatibility

This node is fully compatible with the STM32 LoRa P2P network:
- Same LoRa parameters (923MHz, SF7, BW125kHz, CR4/5, Sync 0x12)
- Same RadioHead header format (4-byte header)
- Same XOR encryption with shared key
- Same CRC16 algorithm and byte order
- Relay preserves original payload — STM32 gateway sees original `N:` source

## License

MIT
