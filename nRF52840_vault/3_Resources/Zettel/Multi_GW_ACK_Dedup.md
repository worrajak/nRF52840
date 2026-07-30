---
id: 20260609002
tags: [gateway, mqtt, dedup, ack, collision]
---

# Multi-Gateway ACK Dedup — ป้องกัน Packet ซ้ำหลาย GW

## ปัญหา

ถ้ามีหลาย ESP32 Gateway รับ LoRa packet เดียวกัน:
1. **MQTT duplicates** — ทั้งสอง GW publish MQTT → Node-RED ได้ 2 records สำหรับ data เดียวกัน
2. **ACK collision** — ทั้งสอง GW ส่ง ACK กลับพร้อมกัน → ชนกัน ไม่มีใครได้รับ

## แก้ MQTT Duplicates — Node-RED Dedup

```javascript
const key = `${node}:${seq}`;   // unique key
const WINDOW = 60_000;           // 60 วินาที

if (seen[key]) return null;      // drop dup
seen[key] = Date.now();
return msg;                      // ผ่านแค่อันแรก
```

→ GW ทั้งสอง publish ได้เลย แต่ Node-RED ทิ้ง duplicate

## แก้ ACK Collision — GW ID × Base Delay

```cpp
#define ACK_BASE_DELAY  (THIS_GW_ID * 200)
// GW1: ส่ง ACK หลัง 200ms
// GW2: ส่ง ACK หลัง 400ms

// GW2: ถ้าได้ยิน ACK ของ GW1 ก่อนหมด 400ms → cancel
if (heard_ack_for(src, seq)) cancelMyACK(src, seq);
```

→ GW1 ส่งก่อนเสมอ, GW2 cancel อัตโนมัติถ้าได้ยิน GW1

## กฎสำคัญ

- GW ID ต้อง unique และเรียงต่อกัน (1, 2, 3, ...)
- ช่องว่าง 200ms ต้องมากกว่า airtime LoRa (~70ms ที่ SF7 50B)
- Node-RED dedup window = 60s (ครอบคลุม clock skew ระหว่าง GW)

## Links

- Source: [[Architecture_LoRa_Mesh]]
- Related: [[RSSI_Forwarding_Delay]]
- ใช้ใน: [[_project-brief]]
