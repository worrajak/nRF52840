---
id: 20260726004
tags: [nrf52840, sx1262, low-power, lora, oled, rx]
---

# SX1262 Low Power — RX และ OLED เปิดตลอด

> บันทึกของชุด **Node 119 / SX1262** ที่ยืนยันบนบอร์ดแล้ว
> config ปัจจุบันใน `platformio.ini` กลับมาเป็น Node 119 / SX1262 แล้ว —
> ดู [[Node116_RFM95_Build]]

## สถานะที่ยืนยันแล้ว (ชุด 119)

- Board/Node: fakeTec nRF52840, Node ID `119`
- Radio: RA-01SH/SX1262 (`USE_SX1262`)
- LoRa initialization: `OK`
- Frequency: 923 MHz
- ส่งสถานะของตัวเองทุก `120000 ms` หรือ 2 นาที
- SX1262 อยู่ Continuous RX เพื่อรอ packet จาก node ข้างเคียง
- OLED เปิดและ refresh ทุก 1 วินาที
- ปุ่ม SW2/P1.00 ยังสลับ UI 3 หน้า

## รูปแบบประหยัดพลังงาน

ตั้งใน `platformio.ini` (ค่าของชุด 119):

```ini
custom_node_id = 119
custom_radio = USE_SX1262
custom_low_power = 1
```

`custom_low_power` ไม่ผูกกับรุ่นวิทยุ ใช้กับ RFM95 ได้เหมือนกัน

Firmware เรียก `delay(10)` ท้าย loop เพื่อปล่อยให้ FreeRTOS idle task พา
nRF52840 เข้า System-ON idle ช่วงสั้น ๆ โดยไม่สั่ง SX1262 sleep และไม่ปิด OLED
DIO1 สามารถปลุก CPU เมื่อรับสัญญาณ ส่วนการอ่านปุ่มจะกลับมาทำต่อภายในประมาณ
10 ms

หน้าที่ 3 แสดง:

```text
Power:IDLE RX-ON
```

นี่เป็นโหมดตรวจสอบระยะแรก ไม่ใช่โหมดกินไฟต่ำสุด เพราะ SX1262 Continuous RX,
OLED และ BLE ยังใช้พลังงานอยู่

## บทเรียนจาก Radio FAIL

ค่า Version `A2/C2` เกิดจากนำ firmware สำหรับ RFM95/SX1276 ไปลงบนบอร์ด
SX1262 ไม่ใช่ความเสียหายของ RFM95 driver หรือผลจาก low-power เมื่อเลือก
`USE_SX1262` ให้ตรงกับฮาร์ดแวร์แล้ว LoRa แสดง `OK`

ก่อน build ต้องตรวจรุ่นชิปจริงเสมอ:

- SX1262/RA-01SH → `USE_SX1262`
- RFM95/SX1276 → `USE_RFM95`

UF2 ล่าสุด:

```text
firmware_node_119_sx1262_ra01sh.uf2
```

## Links

[[00_MOC]] · [[PIO_Node_ID_Radio_Config]] · [[Node116_RFM95_Build]] · [[FakeTec_Button_UI_Pin]]
