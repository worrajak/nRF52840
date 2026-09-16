---
source: MESH_PROTOCOL.md + การหารือ 2026-08-01
url: ../../../MESH_PROTOCOL.md
fetched: 2026-08-01
topic: interop nRF52840 <-> STM32
used_in: VIA_Trace_Reencrypt
---

# ทำ nRF52840 คุยกับ STM32 ได้ — สรุปสิ่งที่เปลี่ยน

> โปรเจกต์พี่น้อง: `../../2025-12-30_STM32_LoRaMesh/` (STM32_LoRaMesh_vault)
> โครงการจริงที่ใช้งาน: DaaS Wildfire สัญญา บพข. C05F690131 (`FireWildPMUC_vault`)

## สรุปการเปรียบเทียบ

RF ตรงกันหมดตั้งแต่แรก — 923 MHz · SF7 · BW125 · CR4:5 · preamble 8 ·
sync word (RFM95 `0x12` = SX1262 `0x1424` public) · XOR key เดียวกัน · CRC16 Modbus เหมือนกัน ·
Node ID ไม่ชน (STM32 102–105 · nRF 115–119)

**ปัญหาอยู่ที่ hop counter อยู่คนละที่:**

| | เก็บ hop ที่ไหน |
|---|---|
| nRF52840 | header byte `[3]` nibble ล่าง (`FLAG_HOP_MASK 0x0F`) ✅ |
| STM32 (เดิม) | ข้อความ `HOP:<n>` ใน payload ❌ |

ผลจริง: packet จาก nRF ไปถึง STM32 ไม่มี `HOP:` → STM32 อ่านเป็น 0 ตลอด → `MAX_HOPS`
ไม่เคยทำงาน · ทางกลับกัน STM32 ใช้ `RHReliableDatagram::sendtoWait()` ซึ่งเขียน FLAGS = 0
เสมอ → nRF ก็อ่าน hop ได้ 0 ตลอด

**ตัดสินใจ: ยก STM32 มาตามแบบ nRF** (nRF ใหม่กว่า มี ML/RSSI log/BLE ที่พึ่ง hop ใน header อยู่แล้ว)
STM32 เปลี่ยนไปใช้ `RHDatagram` เพื่อคุม FLAGS เอง

**จุดที่โชคดี:** RadioHead นิยาม `RH_FLAGS_APPLICATION_SPECIFIC = 0x0f` (nibble ล่าง)
และสงวน `0xf0` ให้ตัวเอง — ตรงกับที่ nRF เลือกใช้ `FLAG_HOP_MASK 0x0F` พอดี
ส่วน `FLAG_IS_ACK 0x10` / `FLAG_ACK_REQ 0x20` อยู่ในโซนสงวนของ RadioHead แต่ไม่ชนกับ
`RH_FLAGS_ACK`(0x80) / `RH_FLAGS_RETRY`(0x40) ที่ใช้จริง

## สิ่งที่เปลี่ยนในฝั่ง nRF52840

| # | เปลี่ยน | เหตุผล |
|---|---------|--------|
| 1 | relay เปลี่ยนจาก `memcpy` header อย่างเดียว → decrypt + ต่อ VIA + XOR ใหม่ + CRC16 ใหม่ | → [[VIA_Trace_Reencrypt]] |
| 2 | parse `SIM:1` | STM32 ใส่มาเมื่อ BME280 ไม่ตอบ (ค่าจำลอง ไม่ใช่ค่าวัดจริง) แสดงใน RX line |
| 3 | เพิ่ม `VIA_ENTRY_MAX_LEN` / `MAX_PAYLOAD_LEN` ให้ตรงกับ STM32 | กัน payload ล้น |

ที่ nRF **ไม่ต้องแก้** เพราะทำถูกอยู่แล้ว: hop ใน header · delay ตาม RSSI ·
cancel-on-heard · EMA threshold · dup cache TTL 5 นาที · queue 5 ช่อง

## รูปแบบ packet ร่วม

```
[TO=0xFF][FROM=relay_id][ID=msg_id][FLAGS=hop|ack_bits][XOR payload][CRC16]

payload: N:<src>|S:<seq>[|SIM:1]|T:<c>|H:<%>|B:<mV>[|LA:<lat>|LO:<lon>][|V:<relay>,<rssi>]...
```

## ผล build

nRF52840: 189,668 / 815,104 ไบต์ (23.3%) · RAM 33,764 / 248,832 (13.6%)
STM32: 62,100 / 65,536 ไบต์ (94.8%) ⚠️ เหลือ ~3.4 KB

**ยังไม่ทดสอบบนฮาร์ดแวร์จริง** — compile ผ่านอย่างเดียว

## ข้อควรระวังเวลาแก้ฝั่ง STM32

STM32F103C8 แฟลชเต็มเกือบหมด — ห้ามใช้ `String::toFloat()` / `atof` / `strtod`
ดึง `_strtod_l` + `__ieee754_pow` + `__gethex` เข้ามา **~8.5 KB** ทำให้ link ไม่ผ่าน
(รายละเอียด → `STM32_LoRaMesh_vault/3_Resources/Zettel/zettel-stm32f103-flash-strtod.md`)

## ผลจำลองที่กระทบการออกแบบ

จำลองโหลด 25–400 node พบว่า **ความหนาแน่น gateway คือตัวชี้ขาด ไม่ใช่โหลด**
และ **CAD/LBT คือการแก้ firmware ที่ให้ผลมากสุด** — ตอนนี้ `sendRawPacket()` ยิงตรง
ไม่มี LBT เลย (ฝั่ง STM32 `isChannelClear()` ก็ `return true;` ตายตัว)

รายละเอียดเต็ม → `FireWildPMUC_vault/3_Resources/Literature/LoRa-Mesh-Load-Simulation.md`
simulator → `../../2025-12-30_STM32_LoRaMesh/sim/lora_mesh_sim.py`

## Links

- [[VIA_Trace_Reencrypt]]
- [[Architecture_LoRa_Mesh]]
- [[STM32_to_nRF52840_Mapping]]
- [[IS_ACK_Relay_Cancel_On_Heard]]
- [[RSSI_Forwarding_Delay]]
- [[Adaptive_RSSI_Threshold]]
- [[Multi_GW_ACK_Dedup]]
