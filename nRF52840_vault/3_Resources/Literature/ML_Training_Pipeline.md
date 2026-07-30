---
source: tools/train_model.py
fetched: 2026-07-30
topic: ml, training, tflite, decision-tree, rssi-classifier
used_in: "[[TFLite_Integration]]"
---

# ML Training Pipeline

## Overview

Python script that reads RSSI log CSV → trains a TinyML classifier → outputs TFLite model for firmware embedding.

## Training Strategy

Two strategies:
1. **Origin packets (hop=0)** — "should I relay?" using decision column
2. **Full dataset** — "is this packet being relayed?" using flags

## Models

| Model | Algorithm | Output |
|-------|-----------|--------|
| Decision Tree | CART, max_depth=3, min_samples_leaf=5 | If-else chain (reference) |
| Neural Network | Dense(8)→Dense(4)→Dense(1) Sigmoid | TFLite model (~2.4KB) |

## Features

`['rssi', 'src', 'from', 'node']` — normalized with z-score

## Usage

```bash
python3 tools/train_model.py rssi_log.csv
# → rssi_classifier.h (2388 bytes)
```

## Dependencies
- tensorflow, pandas, numpy, scikit-learn

## Links
- [[TFLite_Integration]] — firmware inference
- [[RSSI_Data_Logging]] — data collection
- [[_project-brief]]
