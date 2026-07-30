---
source: SENSOR_PROTOCOL_DESIGN.md
fetched: 2026-06-09
topic: sensor-protocol, modbus, rs485, sdi-12, 4-20ma, industrial
used_in: "[[_project-brief]]"
---

# Sensor Protocol Design — การเลือก Protocol สำหรับ Sensor

## สรุปการเปรียบเทียบ Protocol

| Protocol | ระยะทาง | Speed | Noise immunity | Power | ราคา sensor |
|----------|---------|-------|----------------|-------|-------------|
| **Modbus RTU (RS-485)** | 1200 m | 9600–115200 baud | ⭐⭐⭐⭐⭐ | ต่ำ | ถูก–ปานกลาง |
| SDI-12 | 61 m | 1200 baud | ⭐⭐⭐⭐ | ต่ำมาก | แพง (นักวิทยาศาสตร์) |
| 4-20mA | 1000 m | analog | ⭐⭐⭐⭐⭐ | ต่ำ | ปานกลาง |
| I2C | 1 m | 100–400 kHz | ⭐⭐ | ต่ำมาก | ถูก |
| 1-Wire | 100 m | 15 kbps | ⭐⭐⭐ | ต่ำ | ถูก |

## ตัดสินใจ: Modbus RTU เป็น Primary Protocol

**เหตุผล:**
- อุตสาหกรรมใช้มากที่สุดในโลก
- Sensor ราคาถูก หาง่าย (Jxct, RS485 soil sensors)
- nRF52840 รองรับ UARTE1 (Serial1) ได้เลย
- STM32 USART2 ใช้ได้ทันที

## Sensor ที่แนะนำ

| Sensor | Protocol | วัดอะไร | ราคา (THB) |
|--------|----------|---------|-----------|
| Jxct 7-in-1 Soil | RS485 Modbus | moisture, temp, EC, pH, N, P, K | ~2,500 |
| Generic RS485 temp/hum | RS485 Modbus | temp + humidity | ~500–800 |
| DS18B20 | 1-Wire | temperature only | ~50 |
| BME280 | I2C | temp + hum + pressure | ~150 |

## nRF52840 Modbus Wiring

```
nRF52840 UARTE1:
  TX  → P0.17 → MAX485 DI
  RX  → P0.20 → MAX485 RO
  DE  → P0.07 → MAX485 DE+RE

Serial1.begin(9600, SERIAL_8N1);
// TX: DE HIGH → write → flush → DE LOW
```

## STM32 UART Allocation

| UART | ใช้ทำอะไร |
|------|----------|
| USART1 | Debug serial |
| USART2 | Modbus RS-485 (MAX485) |
| USART3 | SDI-12 (ถ้าต้องการ) |

## Decision Tree

```
ต้องการ sensor:
  ├─ ราคาถูก + industrial → Modbus RS-485 ✅
  ├─ ต้องการ academic/precision → SDI-12
  ├─ analog current loop + noise → 4-20mA
  ├─ เพียงอุณหภูมิ → 1-Wire DS18B20
  └─ prototype ใกล้ๆ → I2C BME280
```

## Links

- [[_project-brief|← Project Brief]]
- [[nRF52840_Dual_Role]]
