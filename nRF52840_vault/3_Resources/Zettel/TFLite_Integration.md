---
id: 20260729005
tags: [lora, ml, tflite, inference, neural-network, nrf52840]
---

# TFLite Integration — ML Inference บน nRF52840

> **สถานะ:** ✅ Implemented ใน `include/rssi_classifier.h` + `src/main.cpp` 2026-07-29

## ภาพรวม

แทนที่ rule-based relay decision (`if (hop==0 && rssi >= threshold)`) ด้วย **neural network inference** ที่เทรนจากข้อมูล RSSI จริงในสนาม

## ทำไมไม่ใช้ TFLite Micro Runtime

| Approach | Flash | RAM | Complexity |
|----------|-------|-----|-----------|
| TFLite Micro runtime | ~+50KB | ~+20KB | สูง (interpreter, arena) |
| **Lightweight C inference** (เรา) | **~+320 bytes** | **~+320 bytes** | ต่ำ (แค่ matrix multiply) |

โมเดลเล็กมาก (3 Dense layers) → เขียน forward pass ด้วยมือตรง ๆ ดีกว่า

## Model Architecture

```
Input: [rssi, src, from, node]  (4 floats)
  ↓ Normalize: (x - mean) / std
  ↓ Dense(4→8, ReLU)    — W1[4×8], b1[8]
  ↓ Dense(8→4, ReLU)    — W2[8×4], b2[4]
  ↓ Dense(4→1, Sigmoid) — W3[4], b3[1]
Output: probability (0.0–1.0)
```

## Normalization Constants

| Feature | Mean | Std |
|---------|------|-----|
| rssi | -59.068 | 21.694 |
| src | 104.727 | 23.077 |
| from | 104.727 | 23.077 |
| node | 117.0 | 1e-8 (constant) |

## Training Pipeline

```
rssi_log.csv (86 samples, 4 nodes: 108, 116, 117, 118)
  → tools/train_model.py
    → Decision Tree (if-else chain, for reference)
    → TFLite model (rssi_classifier.h, 2388 bytes)
      → Manually extract weights → include/rssi_classifier.h
```

## ไฟล์สำคัญ

| ไฟล์ | คำอธิบาย |
|------|---------|
| `include/rssi_classifier.h` | Weights + inference engine (lightweight C) |
| `tools/train_model.py` | Python training pipeline |
| `rssi_log.csv` | 86 samples from field test |
| `rssi_classifier.h` (root) | TFLite model binary (2388 bytes) |

## Serial Commands

| Command | การทำงาน |
|---------|---------|
| `MLON` | เปิด ML mode (default) |
| `MLOFF` | กลับไปใช้ rule-based (adaptive threshold) |
| `MLTOGGLE` | สลับโหมด |
| `STATS` | ดูสถานะ + threshold + ML probability |

## OLED Display

### หน้า 1 (THIS NODE) — ML ON
```
1/3 THIS NODE
Node ID:117 LoRa:OK
Temp:25C (SIM)
Hum :50%
TX#:12 Pwr:+12dBm
Last TX:SENT
ML:85% Th:-95
```

### หน้า 1 — ML OFF
```
...
Rule Th:-95
```

### หน้า 2 (LAST RX / RELAY) — ML ON
```
ML:85% Path:92%
```

## ข้อจำกัดปัจจุบัน

- ฝึกจาก 86 samples เท่านั้น → accuracy อาจยังไม่ดีพอในสภาพแวดล้อมใหม่
- `node` feature เป็น constant (117) → ไม่มีประโยชน์ในทางปฏิบัติ
- ต้องเก็บข้อมูลเพิ่ม + retrain เพื่อให้โมเดลแม่นยำขึ้น

## วิธี Retrain

```bash
# 1. เก็บข้อมูลเพิ่ม
python3 tools/rssi_logger.py /dev/tty.usbmodemXXXX

# 2. Train ใหม่
python3 tools/train_model.py rssi_log.csv

# 3. ก็อปปี้ weights จาก rssi_classifier.h → include/rssi_classifier.h
```

## Links

- [[RSSI_Data_Logging]] — ข้อมูลที่ใช้ train
- [[Adaptive_RSSI_Threshold]] — fallback เมื่อ ML OFF
- [[Path_Success_Rate_Bayesian]] — ใช้คู่กับ ML
- [[_project-brief]]
