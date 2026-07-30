---
id: 20260729003
tags: [lora, relay, path, bayesian, probability]
---

# Path Success Rate (Bayesian) — วัดโอกาสที่ packet จะถึง Gateway

> **สถานะ:** ✅ Implemented ใน `main.cpp` 2026-07-29

## แนวคิด

ใช้ **Bayesian probability + Laplace smoothing** เพื่อคำนวณโอกาสที่ packet 
จาก source node ใด ๆ จะถึง Gateway โดยดูจาก:
- **totalCount** — จำนวน packet ที่เราเห็นจาก source นี้
- **successCount** — จำนวน packet ที่ถูก relay ต่อ (hop>0) หรือมี ACK กลับมา

## สูตร

```
P(success) = (successCount + 1) / (totalCount + 2) × 100
```

Laplace smoothing ป้องกัน division by zero และ bias ตอนมีข้อมูลน้อย

## ข้อดี

- เรียนรู้จากประวัติจริง — node ที่ relay สำเร็จบ่อย → probability สูง
- ใช้ RAM แค่ 8 entries × 10 bytes = 80 bytes
- ไม่ต้องใช้ TFLite / Neural Network

## OLED Display

หน้า 1 (LAST RX / RELAY) แสดง:
```
ML:85% Path:92%
```

- `ML:85%` = ML confidence (เฉพาะ ML mode)
- `Path:92%` = โอกาสที่ packet จาก node นี้จะถึง Gateway
- `Th:-97` = adaptive RSSI threshold ปัจจุบัน (เฉพาะ rule mode)

## Links

- [[TFLite_Integration]] — ML mode ใช้คู่กับ Path success
- [[Adaptive_RSSI_Threshold]]
- [[_project-brief]]

## Code

```cpp
struct PathStat {
    uint8_t nodeId;
    uint32_t successCount;
    uint32_t totalCount;
} pathStats[PATH_STATS_SIZE];  // 8 entries

uint8_t pathSuccessProbability(uint8_t nodeId) {
    PathStat* ps = getPathStat(nodeId);
    uint32_t num = (ps->successCount + 1) * 100;
    uint32_t den = ps->totalCount + 2;
    return (uint8_t)(num / den);
}
```

## Links

- [[_project-brief]]
- [[Adaptive_RSSI_Threshold]]
- [[RSSI_Forwarding_Delay]]