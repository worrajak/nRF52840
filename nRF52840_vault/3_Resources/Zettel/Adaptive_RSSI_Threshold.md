---
id: 20260729002
tags: [lora, relay, rssi, adaptive, ema, ml]
---

# Adaptive RSSI Threshold (EMA) — ปรับ threshold อัตโนมัติตามสภาพแวดล้อม

> **สถานะ:** ✅ Implemented ใน `main.cpp` 2026-07-29

## แนวคิด

แทนที่จะใช้ `RSSI_SOURCE_CLOSE = -100` แบบตายตัว → ใช้ **Exponential Moving Average (EMA)** 
ปรับ threshold ตามค่า RSSI จริงที่ node ได้ยิน

## สูตร

```
α = 3/10 = 0.3
new_threshold = (1-α) × old_threshold + α × sample_rssi
```

- α = 0.3 → ตอบสนองเร็วพอสมควร แต่ยัง smooth
- Clamp: [-120, -80] — ไม่ aggressive หรือ permissive เกินไป

## ข้อดี

- ปรับอัตโนมัติเมื่อสภาพแวดล้อมเปลี่ยน (กลางวัน/กลางคืน, ฝนตก, มีสิ่งกีดขวางใหม่)
- ไม่ต้อง calibrate threshold ล่วงหน้า
- ใช้ RAM แค่ 6 bytes (int16_t + uint32_t)
- **Fallback เมื่อ ML OFF** — ถ้าใช้ ML mode (default) threshold ยังคง update แต่ไม่ใช้ตัดสินใจ

## Links

- [[TFLite_Integration]] — ML mode (default) ใช้ NN แทน threshold
- [[RSSI_Forwarding_Delay]]
- [[_project-brief]]

## Code

```cpp
#define RSSI_EMA_ALPHA_NUM    3
#define RSSI_EMA_ALPHA_DEN    10

int16_t rssiSourceClose = -100;  // initial

void updateRssiThreshold(int16_t rssi) {
    rssiSampleCount++;
    if (rssiSampleCount == 1) {
        rssiSourceClose = rssi;
    } else {
        int32_t ema = (int32_t)rssiSourceClose * (RSSI_EMA_ALPHA_DEN - RSSI_EMA_ALPHA_NUM)
                    + (int32_t)rssi * RSSI_EMA_ALPHA_NUM;
        rssiSourceClose = (int16_t)(ema / RSSI_EMA_ALPHA_DEN);
    }
    if (rssiSourceClose > -80) rssiSourceClose = -80;
    if (rssiSourceClose < -120) rssiSourceClose = -120;
}
```

## Links

- [[_project-brief]]
- [[RSSI_Forwarding_Delay]]
- [[IS_ACK_Relay_Cancel_On_Heard]]