---
source: ROADMAP.md
fetched: 2026-06-09
topic: roadmap, codebase-survey, relay-logic, rssi-routing
used_in: "[[_project-brief]]"
---

# Roadmap — สำรวจสถานะและแนวทางพัฒนา

## สิ่งที่มีแล้ว (Codebase ปัจจุบัน)

### Radio / Hardware ✅
- SX1262 + RFM95 dual driver, compile-time select
- Software SPI bit-bang (แก้ bug PINS_COUNT=34 ของ n-able core)
- 923 MHz, SF7, BW125, CR4/5, TX 12 dBm
- OLED SSD1306, BME280, Battery monitor
- BLE GPS รับพิกัดจากมือถือ (NimBLE)

### Protocol / Networking ✅
- RadioHead-compatible header `[TO][FROM][ID][FLAGS]`
- XOR encrypt + CRC16 (compat กับ STM32 เดิม)
- Multi-hop relay, hop count ใน FLAGS
- MAX_HOPS=3, Duplicate detection (RelayCache 10 entries, TTL 5 min)
- Message queue + **RSSI-based forwarding delay** (200/700/1500/3000ms)
- Cancel-on-heard (`cancelFromQueue`) — data + ACK
- IS_ACK relay ย้อนกลับ — FLAGS bits 4-5 preserved, ACK payload relayed กลับ
- Origin packet ส่งด้วย `FLAG_ACK_REQ`

### Navigation ✅
- Haversine distance + bearing, NodePosition table (8 entries)
- OLED compass arrow

## สิ่งที่ขาด (ก่อนเริ่ม Mesh Project)

| Feature | สถานะ |
|---------|-------|
| RSSI-based forwarding delay | ✅ **Implemented 2026-07-29** — 200/700/1500/3000ms ตาม RSSI |
| Best-relay selection | ✅ RSSI delay → best relay ส่งก่อนเสมอ |
| IS_ACK relay ย้อนกลับ | ✅ FLAGS bits 4-5 preserved, ACK payload relayed กลับ, หยุดที่ต้นทาง |
| Cancel-on-heard | ✅ `cancelFromQueue()` — data + ACK |
| ACK dedup key (src+seq+gw_id) | ❌ ยัง (Node-RED side) |
| Neighbor RSSI table | ❌ ยัง (future — Option B) |

## การเปลี่ยนแปลงล่าสุด (2026-07-29)

- `addToMessageQueue()` รับ `rssi` → `rssiToDelayMs()` แทน `random(1000,3000)`
- `cancelFromQueue(srcNode, seqNum)` ใหม่ — cancel ถ้าได้ยิน relay อื่นหรือ ACK
- `FLAG_IS_ACK + FLAG_ACK_REQ` constants ใหม่ — FLAGS ไม่ถูกทับตอน relay
- `sendPacket()` ส่งด้วย `FLAG_ACK_REQ` → GW รู้ว่าต้อง ACK
- ACK payload (`ACK:<src>|S:<seq>|GW:<gw_id>`) ถูก parse, cancel data queue, relay กลับ
- ACK ถึงต้นทาง (ackSrcNode == THIS_NODE) → หยุด ไม่ส่งต่อ
- ACK malformed → DROP ไม่ relay

## Relay Options ที่พิจารณา

| Option | แนวคิด | เลือก |
|--------|---------|-------|
| A — RSSI delay | ดี → delay สั้น, cancel ถ้าได้ยิน relay ก่อน | ✅ **เลือก** |
| B — Neighbor table | HELLO packet, unicast next-hop | future |
| C — 2-level threshold | -80/-100 dBm threshold | ผสมใน A |

**ตัดสินใจ:** ใช้ Option A — ไม่ต้องมี routing table, ไม่เพิ่ม packet type ใหม่

## RSSI Delay Table (ตัดสินใจแล้ว)

| RSSI | Delay |
|------|-------|
| ≥ -75 dBm | 200 ms |
| -75 ถึง -90 | 700 ms |
| -90 ถึง -105 | 1500 ms |
| < -105 | 3000 ms |

## Links

- [[_project-brief|← Project Brief]]
- [[Architecture_LoRa_Mesh]]
- [[RSSI_Forwarding_Delay]]
