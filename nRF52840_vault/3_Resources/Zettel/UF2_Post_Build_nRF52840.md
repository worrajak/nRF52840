---
id: 20260726002
tags: [nrf52840, platformio, uf2, build, bootloader]
---

# PlatformIO Post-build — สร้าง UF2 จริงสำหรับ nRF52840

## ปัญหาเดิม

`copy_uf2.py` เดิมเพียง copy Intel HEX แล้วเปลี่ยนนามสกุลเป็น `.uf2`
ไฟล์จึงขึ้นต้นด้วย `:020000...` ไม่ใช่ UF2 และลากเข้า bootloader ไม่ได้

## วิธีแก้

เพิ่ม post-build script ใน `platformio.ini`:

```ini
extra_scripts = post:copy_uf2.py
```

สคริปต์ใหม่ parse Intel HEX และสร้าง UF2 ขนาด block 512 bytes โดยใช้:

| ค่า | Value |
|---|---|
| UF2 start magic | `0x0A324655`, `0x9E5D5157` |
| Payload/block | 256 bytes |
| Family flag | `0x00002000` |
| nRF52840 family ID | `0xADA52840` |
| UF2 end magic | `0x0AB16F30` |

เลือก family ID จาก Meshtastic nRF52840 UF2 ที่ใช้งานกับบอร์ดนี้

## ผลลัพธ์

ทุกครั้งที่รัน:

```bash
pio run
```

จะสร้าง:

```
firmware.uf2
.pio/build/pro_micro_nrf52840/firmware.uf2
```

build วันที่ 2026-07-26:

- build success
- application start `0x26000`
- UF2 family `0xADA52840`
- RAM 10.6%
- Flash 22.5%

## Validation

ไฟล์ UF2 ที่ถูกต้องต้อง:

- ขนาดหาร 512 ลงตัว
- magic ต้น/ท้ายถูกต้อง
- family ID เป็น `0xADA52840`
- ไม่ใช่ข้อความ Intel HEX ที่ถูก rename

## Links

[[00_MOC]] · [[PIO_Toolchain_arm64_Override]] · [[FakeTec_Button_UI_Pin]]
