---
id: 20260609001
tags: [lora, relay, rssi, mesh-routing]
---

# RSSI-Based Forwarding Delay — Best Relay ส่งก่อนเสมอ

> **สถานะ:** ✅ Implemented ใน `main.cpp` 2026-07-29 — ดู [[_project-brief#Phase 4 — nRF52840 Relay ปรับ]]

## แนวคิดหลัก

แทนที่จะใช้ random delay เหมือนกันทุก node → ให้ node ที่รับสัญญาณแรงกว่า delay น้อยกว่า
→ node ที่ดีที่สุด (RSSI สูงสุด) จะส่งก่อนเสมอ โดยอัตโนมัติ

## ตาราง Delay

| RSSI | Delay | ความหมาย |
|------|-------|---------|
| ≥ -75 dBm | 200 ms | relay ดีมาก → ส่งเกือบทันที |
| -75 ถึง -90 | 700 ms | relay ดี |
| -90 ถึง -105 | 1500 ms | relay อ่อน |
| < -105 dBm | 3000 ms | relay อ่อนมาก → รอนานสุด |

## Relay Decision Logic

### Rule mode (MLOFF)
```
hop=0 (origin packet):
  ├─ RSSI ≥ adaptive threshold → source ใกล้ → Gateway ได้ยินเอง → NO-RELAY ❌
  └─ RSSI < adaptive threshold → source ไกล → Gateway ไม่น่าได้ยิน → RELAY ✅

hop>0 (relayed packet):
  └─ relay ต่อตาม RSSI delay ปกติ (200/700/1500/3000ms) ✅
```

### ML mode (MLON — default)
```
hop=0 (origin):
  ├─ ML probability ≥ 50% → RELAY ✅
  └─ ML probability < 50% → NO-RELAY ❌

hop>0 (relayed):
  └─ relay ต่อตาม RSSI delay ปกติ ✅
```

### เหตุผล (Rule mode)
- ถ้า relay node ได้ยิน origin (hop=0) ด้วย RSSI ≥ adaptive threshold แสดงว่า origin อยู่ใกล้
  → Gateway ก็น่าจะได้ยินด้วย → ไม่ต้อง relay
- ถ้า relay node ได้ยิน origin (hop=0) ด้วย RSSI < adaptive threshold แสดงว่า origin อยู่ไกล
  → Gateway อาจไม่ได้ยิน → ต้อง relay
- Packet ที่ relay มาแล้ว (hop>0) → relay ต่อตาม delay ปกติ

### ML mode
- ใช้ neural network 3-layer (4→8→4→1) ที่เทรนจากข้อมูล RSSI จริง
- ตัดสินใจจาก 4 features: [rssi, src, from, node]
- สลับ mode ได้ real-time ผ่าน Serial: MLON/MLOFF/MLTOGGLE
- ดูรายละเอียดเพิ่มเติม: [[TFLite_Integration]]

## Cancel-on-Heard

ระหว่างรอ delay ถ้าได้ยิน relay อื่นส่ง packet เดียวกัน → **cancel ทันที**
→ ป้องกัน relay ซ้ำโดยอัตโนมัติโดยไม่ต้อง routing table

**เพิ่มเติม:** ACK ที่ GW ส่งกลับก็ cancel data packet ที่ค้าง queue ด้วย
(ACK:`<src>|S:<seq>|GW:<gw_id>` → `cancelFromQueue(src, seq)`)

## ข้อดีของ approach นี้

- ❌ ไม่ต้องมี routing table
- ❌ ไม่ต้องมี HELLO packet / neighbor discovery  
- ❌ ไม่ต้องเพิ่ม packet type ใหม่
- ✅ self-organizing: best relay ชนะโดยธรรมชาติ
- ✅ deploy ง่าย: ทุก node logic เหมือนกัน
- ✅ ACK จาก GW ก็ cancel relay ที่ค้าง → ลด traffic ซ้ำ

## Code Pattern (ของจริงใน `main.cpp`)

```cpp
// rssiToDelayMs() — คำนวณ delay จาก RSSI
static uint32_t rssiToDelayMs(int16_t rssi) {
    if      (rssi >= -75)  return 200;
    else if (rssi >= -90)  return 700;
    else if (rssi >= -105) return 1500;
    else                   return 3000;
}

// Relay decision logic
if (hopCount < MAX_HOPS) {
    bool shouldRelay = true;
    // hop=0 (origin): relay only if RSSI < -100 (source far)
    if (hopCount == 0 && rssi >= RSSI_SOURCE_CLOSE) {
        shouldRelay = false;  // NO-RELAY — gateway heard it directly
    }
    if (shouldRelay) {
        addToMessageQueue(relayPkt, len, srcNode, seqNum, rssi);
    }
}

// cancelFromQueue() — cancel ถ้าได้ยิน relay อื่น
cancelFromQueue(srcNode, seqNum);
```

## Links

- Source: [[Roadmap_LoRa_Mesh]]
- ใช้ใน: [[Architecture_LoRa_Mesh]], [[_project-brief]]
- Related: [[Multi_GW_ACK_Dedup]]
