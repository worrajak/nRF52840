---
source: tools/train_model.py
fetched: 2026-07-30
topic: ml, training, tflite, decision-tree, rssi-classifier
used_in: "[[TFLite_Integration]]"
---

# ML Training Pipeline

## Overview

Python script that reads RSSI log CSV → trains a tiny MLP → **writes `include/rssi_classifier.h`** (the header the firmware compiles: weights + hand-written forward pass).

> **แก้ 2026-07-30:** เดิม script เขียน TFLite byte array ลง `rssi_classifier.h` ที่ CWD ซึ่งไม่มีโค้ดไหนอ้างถึง → **retrain แล้ว firmware ไม่เปลี่ยนพฤติกรรมเลย** · ตอนนี้เขียน `include/` ตรง (path อิงรีโป · atomic) + บังคับ 4 features ตาม ABI ของ `mlPredictRelay()` (เดิมถ้า origin sample น้อยจะสลับไปใช้ 5 features มี `hop` = ไม่ตรงกับ firmware)

## Training Strategy

Two strategies:
1. **Origin packets (hop=0)** — "should I relay?" using decision column
2. **Full dataset** — "is this packet being relayed?" using flags

## Models

| Model | Algorithm | Output |
|-------|-----------|--------|
| Neural Network | Dense(4→8)→Dense(8→4)→Dense(4→1) Sigmoid · Adam + class weight (numpy) | inline weights ใน `include/rssi_classifier.h` (~320 B บนบอร์ด) |

## Features

`['rssi', 'src', 'from', 'node']` — z-score · **ต้องเป็น 4 ตัวนี้ตามลำดับนี้** (ABI ของ `mlPredictRelay(raw[4])`)
feature ที่คงที่ตอน log → emit `std = 0` → firmware บังคับ `norm = 0` (อย่าใส่ 1e-8 แล้วให้หารด้วย floor — ทำ node id อื่นระเบิดเป็น ±1e6)

## Release gate (กันน้ำหนักเสียขึ้นบอร์ด)

script จะ **ไม่เขียนไฟล์** (exit 3) ถ้า:
1. prob ตกอยู่ฝั่งเดียวของ 0.5 ทั้งหมด → โมเดลไม่ได้ตัดสินอะไร
2. สัญญาณแรงถูก relay มากกว่าสัญญาณอ่อน → ทิศกลับด้าน
(ทั้งสองข้อคือสิ่งที่น้ำหนักชุดแรกเป็น — ดู [[TFLite_Integration]])

## Usage

```bash
python3 tools/rssi_logger.py /dev/tty.usbmodemXXXX > rssi_log.csv   # ต้องบิลด์ด้วย ML_RSSI_LOG/STATS
python3 tools/train_model.py rssi_log.csv    # → include/rssi_classifier.h
pio run                                       # firmware รับน้ำหนักใหม่
```

ผลรอบล่าสุด (2026-07-30, dataset เดิม 42 origin rows): acc 97.6% · prob 0.000–0.996 · strong 0.000 / weak 0.728

## Dependencies
- numpy, pandas (**ไม่ต้องมี tensorflow / scikit-learn อีกแล้ว**)

## Links
- [[TFLite_Integration]] — firmware inference
- [[RSSI_Data_Logging]] — data collection
- [[_project-brief]]
