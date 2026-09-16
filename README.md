# nRF52840 LoRa Mesh Node

LoRa mesh sensor node on **Pro Micro nRF52840 (NiceNano)**, interoperable with the STM32 LoRa P2P network.

> **Protocol contract**: [MESH_PROTOCOL.md](MESH_PROTOCOL.md) is the single source of truth shared by
> this repo and `2025-12-30_STM32_LoRaMesh`. Change one side and you must change the doc and the other side.

## Features

- **Dual radio**: SX1262 (RA-01SH / HT-RA62) and RFM95/SX1276 — selected in `platformio.ini`, one line
- RadioHead-compatible 4-byte header (interoperable with STM32 nodes using `RHDatagram`)
- **Multi-hop relay** (MAX_HOPS=3) with duplicate cache, echo prevention, cancel-on-heard, RSSI-ranked delay
- **VIA trace**: every relay appends `|V:<node>,<rssi>` so the gateway reconstructs the full path with per-hop RSSI
- **ML relay decision**: 4→8→4→1 neural net (`include/rssi_classifier.h`), trained from `rssi_log.csv`
- **Adaptive TX power (7–17 dBm) and SF (7–12)** based on last heard RSSI
- **BLE GPS**: receive coordinates from a phone app, include in the LoRa payload
- **Node tracking**: OLED shows distance/direction to other nodes with a compass arrow
- XOR encryption + CRC16 (Modbus) — matches the STM32 network
- BME280 temperature/humidity (auto-detect, simulated fallback), battery voltage monitoring
- SSD1306 OLED (128x64, I2C) with status, GPS info, and debug log
- Software bit-bang SPI for the LoRa module (bypasses the n-able core `PINS_COUNT=34` bug)
- **Low-power idle** (`custom_low_power=1`): System-ON idle while LoRa RX and OLED stay on
- **Measurement build** (`include/meas.h`): CSV propagation logging with frozen TX power — see below
- Broadcast TX every 30 seconds, continuous RX

## Configuration — all in `platformio.ini`

```ini
custom_node_id  = 115           ; 1–254, compiled into NODE_ID
custom_radio    = USE_SX1262    ; or USE_RFM95
custom_low_power = 1            ; 0 = CPU runs free, 1 = System-ON idle
```

Nothing has to be edited inside `src/main.cpp` to change node ID, radio, or power mode.
nRF nodes in this network use IDs **115–119**; STM32 nodes use 102–105.

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
| Frequency | 923.000 MHz |
| Spreading Factor | 7 (adaptive 7–12 when enabled) |
| Bandwidth | 125 kHz |
| Coding Rate | 4/5 |
| Sync Word | RFM95/SX1276 `0x12` · SX1262 `0x1424` (equivalent public sync) |
| TX Power | adaptive 7–17 dBm (STM32 side is fixed 10 dBm) |
| Preamble | 8 symbols |
| Hardware CRC | enabled |
| Node ID | `custom_node_id` in `platformio.ini` |

### Adaptive TX power / SF

| Last heard RSSI | TX power | SF |
|---|---|---|
| ≥ −75 dBm | 7 dBm | 7 |
| −75 to −90 | 10 dBm | 8 |
| −90 to −105 | 14 dBm | 10 |
| < −105 | 17 dBm | 12 |

After 5 minutes of silence (`SILENCE_TIMEOUT_MS`) the node falls back to maximum power.
Adaptation stays within the interop envelope — it does not change frequency, BW, CR, or sync word.

## Protocol (summary — full contract in [MESH_PROTOCOL.md](MESH_PROTOCOL.md))

### Packet Structure

```
| TO (0xFF) | FROM | ID | FLAGS | XOR(payload) | CRC16 |
     1B       1B    1B    1B        n B           2B
```

- **TO**: always `0xFF` (broadcast)
- **FROM**: the node transmitting *this hop* — not the original source
- **ID**: 8-bit counter of the transmitting node, incremented on every packet including relays
- **FLAGS**: bits 0–3 hop count · bit 4 `FLAG_IS_ACK` · bit 5 `FLAG_ACK_REQ` · bits 6–7 reserved by RadioHead

> STM32 must **not** use `RHReliableDatagram` — `sendtoWait()` overwrites `FLAGS` and destroys the hop count.

### Payload Format

```
N:115|S:0|T:28|H:65|B:3300|LA:13.736717|LO:100.523186|V:116,-103|V:104,-98
```

| Field | Description |
|-------|-------------|
| N | Original source node ID (preserved through relay) |
| S | Sequence number of the source (0–255 wrap) |
| SIM:1 | STM32 only — BME280 did not answer, values are simulated |
| T / H / B | Temperature °C / humidity % / battery mV (integers) |
| LA / LO | Latitude / longitude from BLE GPS (6 decimals, optional) |
| V | VIA trace: relay node ID + RSSI it heard the previous hop at |

VIA grows ~11 bytes per hop; payload is capped at 200 bytes (`MAX_PAYLOAD_LEN`).
Appending VIA changes the payload, so each relay **re-encrypts and recomputes CRC16**.

### Relay Rules

1. `FROM == self` or `N: == self` → `DROP-ECHO`
2. ACK addressed to us → `ACK-RCVD`, not forwarded
3. Source unparseable → `DROP-PARSE`
4. `(src, seq)` seen within 5 min → `DUP-SKIP`, **and cancel our own queued relay** (cancel-on-heard)
5. `hop >= 3` → `DROP-HOP`
6. Decision: **ML mode** runs `mlPredictRelay()` (relay when p ≥ 0.5) · **rule mode** skips when `hop == 0 && rssi >= rssiSourceClose`
7. Otherwise queue with an RSSI-ranked delay: 200 / 700 / 1500 / 3000 ms (strongest signal transmits first)

Duplicate cache: 10 slots, TTL 5 min, key `(src, seq)`.
Adaptive threshold: `rssiSourceClose` starts at −100 dBm, EMA α=0.3 over CRC-valid packets, clamped −80…−120 dBm.
OLED shows e.g. `FWD N:42 T:30 H:60%` plus hop/RSSI detail.

### Encryption

- XOR with key `"1234567890000000"` (16 bytes, repeating) — obfuscation, not real security
- CRC16 Modbus (poly `0xA001`, init `0xFFFF`) over the **ciphertext**, appended big-endian

## Build & Upload

```bash
pio run                                     # normal build
pio run --target upload
pio device monitor --baud 115200
```

> **Apple Silicon**: the n-able platform pins an x86_64 `arm-none-eabi` toolchain (GCC 9.3.1), which fails with
> "Bad CPU type" without Rosetta. `platformio.ini` overrides it with the arm64-native
> `toolchain-gccarmnoneeabi@1.120301.0` (GCC 12) — no system changes needed.

## ML Relay Decision

The relay decision can be made by a small neural net instead of the RSSI rule:

```
Dense(4→8, ReLU) → Dense(8→4, ReLU) → Dense(4→1, sigmoid)
input raw[4] = [rssi, src, from, node]   output p ≥ 0.5 → relay
```

Retraining workflow:

```bash
python3 tools/rssi_logger.py            # capture serial → rssi_log.csv
python3 tools/train_model.py rssi_log.csv   # writes include/rssi_classifier.h
pio run --target upload
```

`train_model.py` trains with numpy only (no TensorFlow/sklearn) and writes **the header the firmware
actually compiles**, `include/rssi_classifier.h`, with the 4-input ABI in the firmware's order.
Features that were constant during logging get `std = 0` so the firmware zeroes them instead of
amplifying a one-off node ID to ±1e6 and saturating every ReLU.

## Measurement Build (propagation campaign)

`env:pro_micro_nrf52840_meas` adds `-DFW_MEAS_MODE` and changes three things — the normal build is untouched:

1. reads **SNR and SignalRSSI** from `GetPacketStatus` (previously read and discarded)
2. logs **every TX and every RX** as CSV to serial, including lost frames and CRC failures
3. **freezes TX power**, since adaptive power makes RSSI-vs-distance a feedback loop instead of path loss

```bash
pio run -e pro_micro_nrf52840_meas --target upload
pio device monitor --baud 115200
```

Serial commands: `RUN <id>` · `POS <id>` · `SF <5-12>` · `PWR <-9..22>` · `PL <19-250>` ·
`SFLIST 7,9,12` · `SEND <n> [gap_ms]` · `CYCLE <dwell_ms> <frames>` · `SYNC` · `NOISE [n]` ·
`QUIET <0|1>` · `MEAS?` · `HELP`.
The log schema matches the FireWild_DePIN `_meas` build, so the same analysis scripts work on both.
`SEND` and `PL` print a 1% duty-cycle warning when the requested gap is too short.

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

## Radio Module Comparison

| | RFM95 (SX1276) | SX1262 |
|---|---|---|
| SPI protocol | Register-based | Command-based |
| Interrupt pin | DIO0 | DIO1 |
| BUSY pin | Not needed | Required (P0.03) |
| Sync word | `0x12` | `0x1424` |
| Max TX power | +20 dBm | +22 dBm |

## Project Structure

```
├── src/main.cpp                # Firmware: radio, relay, VIA trace, BLE GPS, OLED
├── include/
│   ├── lora_config.h           # PHY parameters
│   ├── rssi_classifier.h       # Generated NN weights (do not edit by hand)
│   └── meas.h                  # Measurement mode (only under -DFW_MEAS_MODE)
├── tools/
│   ├── rssi_logger.py          # Serial → rssi_log.csv
│   └── train_model.py          # CSV → include/rssi_classifier.h
├── nRF52840_vault/             # Obsidian vault (PARA): notes, datasets, build logs
├── MESH_PROTOCOL.md            # Shared protocol contract with the STM32 repo
├── ble_gps_sender.html         # Phone-side BLE GPS sender
├── platformio.ini              # Node ID / radio / power mode / build envs
└── README.md
```

## STM32 Compatibility

Fully compatible with the STM32 LoRa mesh:
- Same PHY (923 MHz, SF7, BW125 kHz, CR4/5, equivalent sync word, HW CRC on)
- Same 4-byte RadioHead header and FLAGS layout
- Same XOR key, same CRC16 algorithm and byte order
- Relay preserves the original `N:` source and appends VIA, so the gateway sees both origin and path

## License

MIT
