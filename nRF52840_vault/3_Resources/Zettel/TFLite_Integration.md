---
id: 20260729005
tags: [lora, ml, tflite, inference, neural-network, nrf52840]
---

# TFLite Integration — ML Inference บน nRF52840

> **สถานะ:** ✅ Implemented ใน `include/rssi_classifier.h` + `src/main.cpp` 2026-07-29
> **แก้ 2026-07-30 — ML default OFF + pipeline retrain ใช้งานได้จริง** (เจอตอน port ไป FireWild_DePIN)
> พบ 3 อย่างที่ทำให้ ML mode ใช้จริงไม่ได้:
> 1. **น้ำหนักชุดแรกไม่แยกอะไรเลย** — replay บน host ได้ prob 0.506–0.678 **ทุก input (≥0.5 = relay หมด)** และทิศกลับด้าน (rssi −47 ใกล้ → 0.506 · −110 ไกล → 0.647) → ตอน `mlMode=true` แปลว่า **adaptive threshold ไม่มีโอกาสทำงานเลย** ในบิลด์ default → เปลี่ยนเป็น `mlMode = false` (rule เป็นตัวจริง · เปิด ML ด้วย `MLON` หลังตรวจตัวเลข)
> 2. **`node` feature std=1e-8** → `normalizeInput` หารด้วย floor 1e-6 → เปลี่ยน `custom_node_id` เป็น 116/119 (บรรทัดเดียวใน .ini) ทำให้ป้อน (116−117)/1e-6 = **−1,000,000** เข้า layer 1 → ReLU อิ่มตัว inference มั่ว → แก้: std<1e-6 ⇒ `norm=0` (feature ไม่มี variance = ไม่มีข้อมูล)
> 3. **retrain ไม่มีผลกับ firmware** — `train_model.py` เขียน TFLite byte array ลง `rssi_classifier.h` ที่ CWD ซึ่งไม่มีโค้ดไหนอ้างถึง (firmware คอมไพล์ `include/`) → แก้: เขียน `include/rssi_classifier.h` ตรง ๆ (path อิงรีโป ไม่อิง CWD) + atomic write
> retrain ด้วย `rssi_log.csv` เดิมหลังแก้ → **acc 97.6% · prob 0.000–0.996 · strong 0.000 / weak 0.728 (ทิศถูก)** · ไม่ต้องมี TensorFlow/sklearn แล้ว (numpy พอ)

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

ชุดปัจจุบัน (regen 2026-07-30 จาก `rssi_log.csv` 42 origin rows) — ค่าอยู่ในหัวไฟล์ที่ generate:

| Feature | Mean | Std |
|---------|------|-----|
| rssi | -57.000 | 19.973 |
| src | 109.714 | 3.283 |
| from | 109.714 | 3.283 |
| node | 117.0 | **0.0 → ถูก zero ทิ้ง** (คงที่ตอน log = ไม่มีข้อมูล) |

> ⚠️ ห้ามใส่ std เล็ก ๆ (1e-8) ให้ feature ที่คงที่ — ให้ใส่ 0 แล้ว firmware จะบังคับ `norm=0`
> `src`/`from` = node id → โมเดลเรียน "ใครส่ง" ไม่ใช่ "สัญญาณแรงแค่ไหน" · dataset ใหญ่ขึ้นควรพิจารณาตัดออก เหลือฟีเจอร์เชิงสัญญาณ

## Training Pipeline

```
rssi_log.csv  (เก็บด้วย tools/rssi_logger.py — ต้องมี ≥10 origin rows)
  → python3 tools/train_model.py rssi_log.csv     (numpy เท่านั้น · ไม่ต้อง TF/sklearn)
    → เทรน MLP 4→8→4→1 (Adam + class weight) แล้ว "ตรวจก่อนปล่อย":
        · prob ต้องคร่อม 0.5 ทั้งสองฝั่ง (ไม่ใช่ตัดสินทางเดียว)
        · สัญญาณแรงต้อง relay น้อยกว่าสัญญาณอ่อน (ทิศไม่กลับ)
      ไม่ผ่าน → exit 3 ไม่เขียนไฟล์ (กันน้ำหนักเสียหลุดขึ้นบอร์ดซ้ำรอบเดิม)
    → เขียน include/rssi_classifier.h (atomic) → pio run
```

## ไฟล์สำคัญ

| ไฟล์ | คำอธิบาย |
|------|---------|
| `include/rssi_classifier.h` | ⭐ **ตัวที่ firmware คอมไพล์** — weights + forward pass (generated) |
| `tools/train_model.py` | training pipeline (numpy) — เขียน `include/` ให้เลย |
| `rssi_log.csv` | 86 samples from field test (42 origin rows ใช้เทรน) |
| `rssi_classifier.h` (root) | ⚠️ **stale ไม่ถูกคอมไพล์** — ของเก่าจาก pipeline ที่พัง · ลบได้ (สำเนาอยู่ `3_Resources/Refs/`) |

## Serial Commands

| Command | การทำงาน |
|---------|---------|
| `MLON` | เปิด ML mode (**ไม่ใช่ default แล้ว** — default = rule) |
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
