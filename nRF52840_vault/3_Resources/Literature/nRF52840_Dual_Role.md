---
source: NRF52840_AS_SENSOR_NODE.md
fetched: 2026-06-09
topic: nrf52840, memory-budget, dual-role, modbus, uart, hardware-bug
used_in: "[[_project-brief]]"
---

# nRF52840 Dual Role — Relay + Sensor Node

## Memory Budget (หลังเพิ่ม dual role)

| Component | Flash | RAM |
|-----------|-------|-----|
| LoRa relay logic (ปัจจุบัน) | ~235 KB (23%) | ~91 KB (36%) |
| + Modbus RS-485 driver | +15 KB | +5 KB |
| + Sensor read + process | +30 KB | +10 KB |
| **รวมทั้งหมด** | ~280 KB (**27%**) | ~106 KB (**41%**) |
| **เหลือ** | **744 KB (73%)** | **150 KB (59%)** |

> nRF52840: 1 MB Flash, 256 KB RAM — มีเหลือมากพอ

## PINS_COUNT=34 Bug — สิ่งที่ได้รับผลกระทบ / ไม่ได้รับ

| Feature | ผลกระทบ | หมายเหตุ |
|---------|---------|---------|
| Hardware SPI | ❌ ใช้ไม่ได้ | bug ส่งผลโดยตรง |
| Software SPI (bit-bang) | ✅ ใช้ได้ | workaround ที่ใช้อยู่ |
| UART / Serial1 (UARTE1) | ✅ ใช้ได้ | ไม่ได้รับผลกระทบ |
| I2C / Wire | ✅ ใช้ได้ | |
| ADC / analogRead | ✅ ใช้ได้ | battery monitor ทำงาน |
| NimBLE | ✅ ใช้ได้ | GPS via BLE ทำงาน |

## Available Peripherals สำหรับ Sensor

```
UARTE1 (Serial1): P0.17 TX, P0.20 RX → Modbus RS-485
I2C (Wire):       P0.19 SCL, P0.20 SDA → BME280 ใช้อยู่แล้ว
ADC (SAADC):      7 channels ว่างอยู่ → 4-20mA, soil moisture
GPIO:             ว่างอีกหลายตัว → 1-Wire DS18B20
```

## Wiring Dual Role

```
nRF52840 Pro Micro
├── SX1262/RFM95  → SPI (software bit-bang)     [relay]
├── BME280         → I2C P0.19/P0.20             [sensor existing]
├── MAX485         → UARTE1 P0.17/P0.20/P0.07    [Modbus]
├── Battery        → ADC                          [monitor]
└── OLED           → I2C (shared)                 [display]
```

## Modbus Code (nRF52840)

```cpp
#define MODBUS_TX_PIN  17   // P0.17
#define MODBUS_RX_PIN  20   // P0.20  (shared กับ I2C SDA — ต้องระวัง)
#define MODBUS_DE_PIN   7   // P0.07

Serial1.begin(9600, SERIAL_8N1);

// Send:
digitalWrite(MODBUS_DE_PIN, HIGH);  // TX enable
Serial1.write(frame, len);
Serial1.flush();
digitalWrite(MODBUS_DE_PIN, LOW);   // RX mode
```

> ⚠️ P0.20 ใช้ร่วมกับ I2C SDA — ถ้า share ต้องไม่ใช้พร้อมกัน

## BLE Local Access

เพิ่มประโยชน์: ช่าง field สามารถ connect BLE เพื่อดูค่า sensor live
โดยไม่ต้องเข้าถึง cloud/server ทำให้ debug ง่ายขึ้นในสนาม

## Links

- [[_project-brief|← Project Brief]]
- [[Sensor_Protocol_Design]]
- [[Architecture_LoRa_Mesh]]
