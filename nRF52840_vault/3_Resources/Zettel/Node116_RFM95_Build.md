---
id: 20260730007
tags: [nrf52840, sx1262, node-119, build, uf2, adaptive-radio, ml]
---

# Node 119 / SX1262 — Build ปัจจุบัน (2026-07-30)

## Config ที่ใช้อยู่จริงใน `platformio.ini`

```ini
custom_node_id  = 119
custom_radio    = USE_SX1262
custom_low_power = 1
```

เปลี่ยนจาก Node 116 / RFM95 มาเป็น Node 119 / SX1262 สำหรับ field test

## Features ที่เพิ่มใน build นี้

| Feature | รายละเอียด |
|---------|-----------|
| ML mode default ON | `mlMode = true` — ใช้ neural network ตัดสิน relay |
| Adaptive TX power | 7/10/14/17 dBm ตาม RSSI, **SF10 fixed** (ป่าไม้) |
| OLED layout ใหม่ | T/H รวมบรรทัด, Radio:ADAPT แทนที่ Hum |

## UF2 ที่มีอยู่

| ไฟล์ | ขนาด | blocks |
|------|-------|--------|
| `firmware_node_119_sx1262_ra01sh.uf2` | ~367 KB | ~717 |

## ข้อควรระวัง

- ถ้าสลับ radio ต้อง build ใหม่ — firmware รองรับ radio เดียว
- Adaptive radio ปรับเฉพาะ TX power, SF คงที่ 7 (ไม่งั้น RX ไม่ตรง)
- ML mode default ON — ถ้าต้องการ rule-based ส่ง `MLOFF` ทาง Serial

## Links
- [[Adaptive_Radio_TX_Power]] — adaptive TX power
- [[TFLite_Integration]] — ML inference
- [[_project-brief]]

## Links

[[00_MOC]] · [[PIO_Node_ID_Radio_Config]] · [[SX1262_Low_Power_RX_Always_On]] · [[UF2_Post_Build_nRF52840]]
