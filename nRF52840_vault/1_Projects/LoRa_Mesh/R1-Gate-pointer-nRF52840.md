---
name: R1-Gate-pointer-nRF52840
description: ตัวชี้ไป R1-Gate ต้นฉบับ + ของกับดักในบอร์ดที่ต้องปิดก่อนวัด
type: pointer
r1_block: [1, 2]
r1_state: blocked
r1_role: gate
---

# R1 Gate — ส่วนที่ vault นี้รับผิดชอบ

นิยาม R1 ทั้งหมดอยู่ที่ **`Sensors_LoRa_Propagation_vault`** ที่เดียว — [[R1-Gate]] (เปิดผ่าน `~/ObsidianHub`)

## ⚠️ ของในโน้ตชุดนี้ที่ต้องปิดก่อนวัด

โน้ตพวกนี้เขียนไว้ว่าเป็น **ฟีเจอร์ที่ดี** สำหรับ production — สำหรับ R1 มันคือ **confound**

| โน้ต | ทำไมต้องปิด |
|---|---|
| [[Adaptive_Radio_TX_Power]] | กำลังส่งแกว่ง 7→17 dBm ตาม RSSI · **นี่คือสาเหตุที่ชุดข้อมูลเก่าฟิตได้ PLE 3.04 และย้อนกลับหา EIRP ได้ −10.4 dBm ซึ่งเป็นไปไม่ได้ทางกายภาพ** (ค่าที่สมเหตุผลคือ +6…+22) |
| [[Adaptive_RSSI_Threshold]] | เกณฑ์รับปรับเอง PDR เลยไม่ใช่ฟังก์ชันของระยะอย่างเดียว |
| [[RSSI_Log_Dataset]] | 86 ตัวอย่างชุดเก่า เก็บใต้เงื่อนไขกำลังส่งไม่คงที่ — **ห้ามปนกับ R1** |

ตรึงด้วย `MEAS_FREEZE_POWER` ใน build `_meas`

## ก้อน 1 — เฟิร์มแวร์

| ต้องได้ | สถานะ |
|---|---|
| คอมไพล์ผ่าน `node_nrf52840_meas` + `gateway_esp32_meas` (tree FireWild_DePIN) | ✅ 2026-08-26 |
| คอมไพล์ผ่าน `pio run -e pro_micro_nrf52840_meas` (tree แล็บ ตัวนี้) | ❌ ยังไม่ได้ทำ |
| bench test 7 ข้อ · `snr_db` ต้องไม่เป็น 0 | ❌ |

เป็นคนละ tree กัน — ผ่านอันแรกไม่ได้แปลว่าผ่านอันนี้

toolchain บน Apple Silicon: [[PIO_Toolchain_arm64_Override]] · ตั้งค่า build: [[PlatformIO_Build_Config]]

> งานเฟิร์มแวร์จริงทำใน session แยก โน้ตนี้เก็บแค่ "ต้องได้อะไรถึงผ่าน"
