---
id: 20260729001
tags: [lora, relay, ack, cancel-on-heard, nrf52840]
---

# IS_ACK Relay + Cancel-on-Heard — ACK ย้อนกลับ + Cancel relay ซ้ำ

> **สถานะ:** ✅ Implemented ใน `main.cpp` 2026-07-29

## แนวคิดหลัก

สองกลไกที่ทำงานร่วมกัน:
1. **IS_ACK relay** — ACK จาก GW hop กลับผ่าน relay chain ถึงต้นทาง
2. **Cancel-on-heard** — ถ้า relay node ได้ยิน relay อื่น forward packet เดียวกัน → cancel ของตัวเอง

## FLAGS Layout

```
[3:0] hop_count     — 0=origin, 1..3=relay hops
[4]   FLAG_IS_ACK   — 0x10: packet นี้คือ ACK
[5]   FLAG_ACK_REQ  — 0x20: origin ต้องการ ACK
```

## IS_ACK Relay Flow

```
[STM32] N:1|S:5|...  (FLAGS=0x20, ack_req=1)
  → [Relay A] FWD (FLAGS=0x21, hop=1, ack_req preserved)
    → [Relay B] FWD (FLAGS=0x22, hop=2, ack_req preserved)
      → [ESP32 GW] รับ → ส่ง MQTT → ส่ง ACK
        ← [GW] ACK:N:1|S:5|GW:1  (FLAGS=0x30, is_ack=1, hop=0)
          → [Relay B] FWD (FLAGS=0x31, hop=1, is_ack preserved)
            → [Relay A] FWD (FLAGS=0x32, hop=2, is_ack preserved)
              → [STM32] รับ ACK → รู้ว่าถึง GW แล้ว
```

## Cancel-on-Heard: ทำงาน 2 ทาง

### 1. Data packet cancel
```
[Relay A] ได้ยิน packet → queue ไว้ 700ms
[Relay B] ได้ยิน packet เดียวกัน RSSI แรงกว่า → queue 200ms
[Relay B] ส่ง forward → [Relay A] ได้ยิน → cancelFromQueue() → A ไม่ส่ง
```
→ มี relay เดียวส่ง = traffic น้อยลง

### 2. ACK cancel
```
[Relay A] queue data N:1 S:5 ไว้ 1500ms
[GW] ส่ง ACK:N:1|S:5 → [Relay A] ได้ยิน → cancelFromQueue(1, 5)
```
→ ACK ถึง = ไม่ต้อง relay data ต่อ (เพราะ data ถึง GW แล้ว)

## Code Pattern

```cpp
// cancelFromQueue() — ทำงานเมื่อ RX packet
void cancelFromQueue(uint8_t srcNode, uint8_t seqNum) {
    for (int i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
        if (messageQueue[i].inUse &&
            messageQueue[i].srcNode == srcNode &&
            messageQueue[i].seqNum == seqNum) {
            messageQueue[i].inUse = false;
            lastRelayStatus = "CANCEL";
        }
    }
}

// FLAGS preservation — relay ไม่ทับ bits 4-5
relayPkt[3] = ((hopCount + 1) & FLAG_HOP_MASK) |
              (rhFlags & ~FLAG_HOP_MASK);
```

## ข้อควรระวัง

- ACK กับ data packet ของ(src, seq) เดียวกัน → ใช้ **duplicate cache แยกกัน** ผ่าน `dupSrc`/`dupSeq` (ACK ใช้ ackSrcNode สำหรับ dup check)
- ACK ถึงต้นทาง (ackSrcNode == THIS_NODE) → **หยุด** ไม่ relay ต่อ
- ACK malformed (ackSrcNode == 0) → **DROP** ไม่ relay
- ACK format: `ACK:<src>|S:<seq>|GW:<gw_id>` — ต้องตรงกับ format ที่ GW ส่ง

## ML Mode Compatibility

ML mode (default) ใช้ neural network ตัดสิน relay เฉพาะ **hop=0 (origin packets)** เท่านั้น
→ ACK relay (hop>0) และ cancel-on-heard ทำงานเหมือนเดิมทุกประการ

```
ML mode:  origin packet → ML decide relay/not
          ACK packet   → relay ต่อตาม rule (hop>0)
Rule mode: origin packet → threshold decide relay/not
           ACK packet   → relay ต่อตาม rule (hop>0)
```

## Links

- [[TFLite_Integration]] — ML mode details
- [[RSSI_Forwarding_Delay]] — delay + cancel mechanism
- [[_project-brief]]

## Links

- Source: [[_project-brief#Phase 4 — nRF52840 Relay ปรับ]]
- ใช้ใน: [[Architecture_LoRa_Mesh]]
- Related: [[RSSI_Forwarding_Delay]], [[Multi_GW_ACK_Dedup]]