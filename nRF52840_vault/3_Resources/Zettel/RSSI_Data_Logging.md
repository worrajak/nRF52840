---
id: 20260729004
tags: [lora, rssi, logging, ml, training, data-collection]
r1_block: [4]
r1_state: doing
r1_role: evidence
---

# RSSI Data Logging — เก็บข้อมูล RSSI สำหรับ Train ML Model

> **สถานะ:** ✅ Implemented ใน `main.cpp` 2026-07-29

## ภาพรวม

เก็บตัวอย่าง RSSI ทุกครั้งที่ RX packet ลงใน **circular buffer 512 samples** 
ใน RAM ของ nRF52840 แล้วดึงออกมาผ่าน Serial หรือ BLE เพื่อ train ML model บน PC

## โครงสร้างข้อมูล

```cpp
struct RssiSample {
    uint32_t timestamp;   // ms since boot
    uint8_t  nodeId;      // THIS_NODE (who logged this)
    uint8_t  srcNode;     // original source
    uint8_t  rhFrom;      // who actually sent this packet
    uint8_t  hopCount;    // 0-3
    int16_t  rssi;        // dBm
    int16_t  snr;         // dB
    uint8_t  decision;    // RELAY_STATUS_* enum
    uint8_t  flags;       // ACK/RELAYED/CRC_FAIL
} __attribute__((packed));  // 14 bytes/sample

RssiSample rssiLog[512];   // 7,168 bytes RAM
```

## Flags

| Flag | Value | ความหมาย |
|------|-------|---------|
| `RSSI_LOG_FLAG_ACK` | 0x80 | packet นี้คือ ACK |
| `RSSI_LOG_FLAG_RELAYED` | 0x40 | packet นี้ถูก relay มา (hop>0) |
| `RSSI_LOG_FLAG_CRC_FAIL` | 0x10 | CRC fail — header-only log |

## Decision Status

| Status | Value | ความหมาย |
|--------|-------|---------|
| `RELAY_STATUS_QUEUED` | 1 | ถูก queue รอ relay |
| `RELAY_STATUS_SENT` | 2 | ส่ง relay สำเร็จ |
| `RELAY_STATUS_NO_RELAY` | 4 | ตัดสินใจไม่ relay |
| `RELAY_STATUS_CANCEL` | 3 | cancel เพราะได้ยิน relay อื่น |
| `RELAY_STATUS_DROP_DUP` | 5 | duplicate packet |
| `RELAY_STATUS_DROP_CRC` | 8 | CRC fail |

## วิธีดึงข้อมูล

### 1. ผ่าน Serial (ง่ายสุด)
```bash
# เชื่อมต่อ Serial monitor → พิมพ์ DATADUMP
# หรือใช้ Python script
python3 tools/rssi_logger.py /dev/tty.usbmodemXXXX -o rssi_log.csv
```

### 2. ผ่าน BLE
- เขียน `"DUMP"` → characteristic `BLE_DATA_CHAR_UUID`
- อ่าน characteristic ได้ CSV chunk
- เขียน `"CLEAR"` → ล้าง buffer

## วิธี Train Model

```bash
# 1. เก็บข้อมูล
python3 tools/rssi_logger.py /dev/tty.usbmodemXXXX

# 2. Train model
python3 tools/train_model.py rssi_log.csv

# Output:
#   - Decision tree rules (if-else chain, ไม่ต้องใช้ TFLite)
#   - rssi_classifier.h (TFLite model, ~2KB)
```

## ขนาด

- RAM: 512 × 14 = **7,168 bytes** (2.8% ของ 256KB)
- Flash: **~2KB** (code + buffer)
- 512 samples ที่ TX ทุก 2 นาที + RX จาก node อื่น = เก็บได้ ~2-4 ชั่วโมง

## Links

- [[_project-brief]]
- [[Adaptive_RSSI_Threshold]]
- [[Path_Success_Rate_Bayesian]]
- [[TFLite_Integration]] — ML model ที่ train จาก log นี้