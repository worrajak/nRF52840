---
id: 20260726003
tags: [platformio, nrf52840, node-id, sx1262, rfm95, uf2, build]
---

# PlatformIO Config — เลือก Node ID และ Radio ก่อน Build

## เป้าหมาย

บอร์ดแต่ละตัวมี Node ID และโมดูลวิทยุต่างกัน จึงย้ายค่าที่ต้องเปลี่ยนบ่อย
ออกจาก `src/main.cpp` มาไว้ใน `platformio.ini` จุดเดียว

## การตั้งค่า

ค่าที่อยู่ใน `platformio.ini` ตอนนี้ (2026-07-26 build 13:20 — ดู [[Node116_RFM95_Build]]):

```ini
; Node ID: ใช้ได้ 1–254
custom_node_id = 116

; เลือกอย่างใดอย่างหนึ่ง
custom_radio = USE_RFM95

; System-ON idle โดย LoRa RX/OLED ยังเปิด
custom_low_power = 1
```

ตัวเลือก radio:

| `custom_radio` | Hardware | Interface |
|---|---|---|
| `USE_SX1262` | RA-01SH / HT-RA62 ที่ใช้ SX1262 | command-based SPI, DIO1 + BUSY |
| `USE_RFM95` | RFM95 / SX1276 | register-based SPI, DIO0, ไม่มี BUSY |

> RA-01 รุ่นธรรมดาที่ใช้ SX1278/433 MHz ไม่ใช่ RA-01SH/SX1262 และยังไม่รองรับ
> เพราะ network นี้ตั้งไว้ที่ 923 MHz

`build_flags` ส่งค่าทั้งสองเข้า compiler:

```ini
build_flags =
  -DNODE_ID=${this.custom_node_id}
  -D${this.custom_radio}
  -DLOW_POWER_IDLE=${this.custom_low_power}
```

## Validation ใน Firmware

- ถ้าไม่ได้ build ผ่าน PlatformIO จะ fallback เป็น Node ID 119 และ SX1262
- Node ID ต้องอยู่ในช่วง `1..254`
- `0` และ `255` ถูกสงวนไว้
- ถ้ากำหนด `USE_SX1262` และ `USE_RFM95` พร้อมกัน compiler จะหยุดด้วย error

Node ID ถูกใช้ร่วมกันทั้ง:

- LoRa RadioHead header และ payload `N:`
- OLED
- BLE device name (`LoRa-N<ID>`)
- Serial diagnostic

## ขั้นตอน Build หลายบอร์ด

1. แก้ `custom_node_id`
2. เลือก `custom_radio`
3. รัน `pio run`
4. ใช้ UF2 ที่มี ID และ radio ตรงกับบอร์ด

ตัวอย่าง output:

```text
firmware_node_116_sx1262_ra01sh.uf2
firmware_node_117_rfm95.uf2
```

สคริปต์ยังสร้าง `firmware.uf2` และ `firmware_node_<ID>.uf2` เพื่อความสะดวก
แต่ไฟล์ชื่อเต็มปลอดภัยกว่าสำหรับจัดเก็บ firmware ของหลายบอร์ด

## Links

[[00_MOC]] · [[Node116_RFM95_Build]] · [[UF2_Post_Build_nRF52840]] · [[FakeTec_Button_UI_Pin]]
