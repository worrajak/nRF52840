---
id: 20260730006
tags: [lora, adaptive, tx-power, rssi, nrf52840, nbiot]
r1_block: [1]
r1_state: blocked
r1_role: confound
---

# Adaptive Radio TX Power — ปรับกำลังส่งตาม RSSI

> **สถานะ:** ✅ Implemented ใน `src/main.cpp` 2026-07-30

## แนวคิด

ปรับ **TX power** ตาม RSSI ที่ node ได้ยินล่าสุด — ถ้าสัญญาณดีก็ลดกำลัง (ประหยัดแบต), ถ้าสัญญาณอ่อนก็เพิ่มกำลัง (เพิ่มโอกาสถึง)

## ตารางปรับ

| RSSI | TX Power | ความหมาย |
|------|----------|---------|
| ≥ -75 dBm | **7 dBm** | ใกล้มาก → ประหยัดสุด |
| -75 ถึง -90 | **10 dBm** | ปานกลาง |
| -90 ถึง -105 | **14 dBm** | ไกล → เพิ่มกำลัง |
| < -105 dBm | **17 dBm** | ไกลมาก → สุดกำลัง |

## ข้อกำหนด กสทช.

920-925 MHz: ≤ 500 mW EIRP (~27 dBm)  
เราใช้สูงสุด 17 dBm (~50 mW) — ปลอดภัย เหลือเผื่อ antenna gain

## SF คงที่

**SF = 7 (default)** — ถ้าเปลี่ยน SF จะทำให้ RX ไม่ตรงกับ node อื่น
→ ปรับเฉพาะ TX power อย่างเดียว

> 💡 **สำหรับป่าไม้ภาคเหนือ:** เปลี่ยนเป็น SF10 จะได้ระยะ ~1.5 km ในป่า
> ต้องแก้ `configureLoRaModem()` ทั้ง SX1262 และ RFM95 ให้ตรงกันทุก node

## Serial Commands

| คำสั่ง | การทำงาน |
|-------|---------|
| `ADAPTIVE` | สลับ ON/OFF |
| `ADAPTON` | เปิด (default) |
| `ADAPTOFF` | ปิด → ใช้ 12 dBm คงที่ |
| `STATS` | แสดงสถานะ + power ปัจจุบัน |

## OLED แสดง

- หน้า 1: `Radio:ADAPT SF:7` (หรือ `Radio:FIXED SF:7`)
- หน้า 2: `TX:+14dBm SF:7 RX:-95`

## Adaptive Logic (ปัจจุบัน)

```
TX ครั้งนี้:
├─ เงียบ > 5 นาที? → ส่ง 17 dBm (silence timeout)
└─ มี activity → ใช้ RSSI-based power (7/10/14/17 dBm)
```

### ACK Feedback (รอ Phase 1 — ESP32 Gateway)
เมื่อมี Gateway แล้ว จะเพิ่ม logic:
- ACK กลับ → ลด power ตาม RSSI
- ไม่มี ACK → คงหรือเพิ่ม power
- Probe ทุก 10th TX → ส่งแรงสุดประกาศตัว

## Links
- [[RSSI_Forwarding_Delay]] — RSSI-based delay (แนวคิดเดียวกัน)
- [[Adaptive_RSSI_Threshold]] — EMA threshold (อีก adaptive mechanism)
- [[_project-brief]]
