---
id: 20260726005
tags: [nrf52840, rfm95, sx1276, node-116, build, uf2, low-power]
---

# Node 116 / RFM95 — Build ปัจจุบัน

## Config ที่ใช้อยู่จริงใน `platformio.ini`

```ini
custom_node_id  = 116
custom_radio    = USE_RFM95
custom_low_power = 1
```

เป็นการสลับจากชุด Node 119 / SX1262 ที่บันทึกไว้ใน
[[SX1262_Low_Power_RX_Always_On]] มาทดสอบบอร์ดที่ติดโมดูล RFM95/SX1276

## UF2 ที่มีอยู่

| ไฟล์ | ขนาด | blocks | เวลา build |
|---|---|---|---|
| `firmware_node_116_rfm95.uf2` | 366,080 | 715 | 2026-07-26 13:20 |
| `firmware_node_116_sx1262_ra01sh.uf2` | 367,104 | 717 | 2026-07-26 12:04 |
| `firmware_node_119_sx1262_ra01sh.uf2` | 367,104 | 717 | 2026-07-26 12:06 |

`firmware.uf2` และ `firmware_node_116.uf2` ที่ project root คือสำเนาของ build
ล่าสุด (RFM95) ทั้งคู่ ตอนหยิบไฟล์ไปลงบอร์ดควรใช้ชื่อเต็มที่ระบุ radio เสมอ
เพราะชื่อสั้นถูกเขียนทับทุกครั้งที่ `pio run`

ทุกไฟล์หารด้วย 512 ลงตัว จึงเป็น UF2 จริงตาม [[UF2_Post_Build_nRF52840]]

## ข้อควรระวังเมื่อสลับ radio

Firmware หนึ่งไฟล์รองรับ radio เดียว ถ้าเอา UF2 ของ SX1262 ไปลงบอร์ด RFM95
(หรือกลับกัน) จะเห็นอาการ Radio FAIL พร้อมค่า Version แปลก ๆ เช่น `A2` / `C2`
ซึ่งไม่ใช่ฮาร์ดแวร์เสีย ให้ตรวจว่าชิปบนบอร์ดตรงกับ `custom_radio` แล้ว build ใหม่

| ชิปบนบอร์ด | ต้องใช้ |
|---|---|
| RFM95 / SX1276 (DIO0, ไม่มี BUSY) | `USE_RFM95` |
| RA-01SH / HT-RA62 (SX1262, DIO1 + BUSY) | `USE_SX1262` |

## สถานะการทดสอบ

- build ผ่าน และได้ UF2 ครบทั้งสาม path
- ยังไม่มีบันทึกผลบนบอร์ดจริงของชุด 116/RFM95 (LoRa `OK`/`FAIL`, RSSI, ระยะ)
- `custom_low_power = 1` เหมือนเดิม คือ System-ON idle โดย RX และ OLED ยังเปิด
  จึงคาดว่าพฤติกรรมด้านพลังงานเทียบเท่าชุด 119 ต่างกันแค่ตัวโมดูลวิทยุ

## Links

[[00_MOC]] · [[PIO_Node_ID_Radio_Config]] · [[SX1262_Low_Power_RX_Always_On]] · [[UF2_Post_Build_nRF52840]]
