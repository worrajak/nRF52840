---
source: README_LoRa_P2P.md
fetched: 2026-06-09
topic: nrf52840, lora-p2p, bme280, rfm95, original-codebase
used_in: "[[Roadmap_LoRa_Mesh]]"
---

# LoRa P2P Original — nRF52840 Starting Point

## ระบบเดิม (ก่อน Mesh)

Point-to-Point LoRa บน nRF52840 Adafruit Feather
adapted มาจาก STM32 LoRa P2P

## Hardware (Original Setup)

| Module | Pins | Notes |
|--------|------|-------|
| RFM95 CS | P0.24 | Hardware SPI |
| RFM95 INT | P0.25 | |
| RFM95 RST | P0.26 | |
| BME280 SDA | P0.20 | I2C 0x76/0x77 |
| BME280 SCL | P0.19 | |

## Original Packet Format

```
NODE:id|SEQ:counter|TEMP:temp|HUM:humidity|PRES:pressure|VBAT:battery
```

ตัวอย่าง: `NODE:104|SEQ:42|TEMP:24.56|HUM:45.32|PRES:1013.25|VBAT:3300`

> **หมายเหตุ:** Format นี้ถูก upgrade ใน Mesh project เป็น `N:<src>|S:<seq>|T:<temp>|H:<hum>|B:<bat_mv>|LA:<lat>|LO:<lon>` + RadioHead header + XOR+CRC16

## LoRa Config (เหมือนกัน Mesh)

```cpp
923E6 Hz, SF7, BW125kHz, CR4/5, TX12dBm, Sync 0x12
```

## Libraries (Original)

- LoRa (sandeepmistry) — ถูกเปลี่ยนเป็น RadioLib ใน ESP32 Gateway
- Adafruit BME280
- n-able Arduino core

## Links

- [[Roadmap_LoRa_Mesh]]
- [[Architecture_LoRa_Mesh]]
