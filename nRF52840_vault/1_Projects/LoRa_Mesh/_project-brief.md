# LoRa Mesh Network — Project Brief

> **เป้าหมาย:** ระบบ IoT mesh ส่งข้อมูล sensor จาก STM32 ผ่าน nRF52840 relay ไปยัง ESP32 gateway → MQTT → InfluxDB/Grafana
> **Deadline:** —
> **สถานะ:** ✅ Phase 4 เสร็จ — RSSI delay + cancel-on-heard + IS_ACK relay + ML/TFLite implemented 2026-07-29

---

## Architecture Overview

```
STM32 Sensor (0x20–0x7F)
  → LoRa 923MHz SF7
    → nRF52840 Relay (0x10–0x1F)  [RSSI-based forwarding]
      → ESP32 Gateway (0x01–0x0E) [RadioLib + WiFiManager]
        → HiveMQ Cloud (MQTT TLS)
          → Node-RED → InfluxDB v2 → Grafana
```

---

## Development Phases Checklist

### Phase 1 — ESP32 Gateway
- [ ] `shared/lora_protocol.h` — address map, FLAGS, constants
- [ ] `shared/lora_crypto.h` — XOR encrypt, CRC16
- [ ] `shared/lora_payload.h` — build/parse payload helpers
- [ ] `esp32_gateway/platformio.ini`
- [ ] `esp32_gateway/src/main.cpp` — RadioLib + WiFiManager + MQTT + ACK
- [ ] `docker/docker-compose.yml` — InfluxDB + Grafana + Node-RED
- [ ] Node-RED flow (dedup → enrich → InfluxDB)
- [ ] Grafana dashboard เบื้องต้น

### Phase 2 — Multi-Gateway
- [ ] Gateway #2 (THIS_GW_ID=2)
- [ ] ทดสอบ MQTT dedup ที่ Node-RED
- [ ] ทดสอบ ACK cancel

### Phase 3 — STM32 Sensor Node
- [ ] TX packet (hop=0, ack_req=1)
- [ ] WAIT_ACK + retry state machine
- [ ] Grafana Geomap

### Phase 4 — nRF52840 Relay ปรับ
- [x] ปุ่ม fakeTec SW2/P1.00 สลับ OLED UI 3 หน้า
- [x] PlatformIO post-build สร้าง nRF52840 UF2 จริง
- [x] ตั้ง Node ID และเลือก SX1262/RA-01SH หรือ RFM95 จาก `platformio.ini`
- [x] Node 119/SX1262 low-power ระยะแรก: System-ON idle, RX+OLED เปิด, TX ทุก 5 นาที
- [x] Build ชุด Node 116/RFM95 ด้วย config เดียวกัน (ยังไม่ได้บันทึกผลบนบอร์ด)
- [x] **IS_ACK relay (ย้อนกลับ)** — FLAGS bits 4-5 preserved, ACK payload relayed กลับ, หยุดที่ต้นทาง
- [x] **RSSI-based delay แทน random delay** — 200/700/1500/3000ms ตาม RSSI
- [x] **Cancel queue entry ถ้าได้ยิน relay ก่อน** — cancelFromQueue() + ACK ก็ cancel data ที่ค้าง
- [x] **Adaptive RSSI threshold (EMA)** — α=0.3, clamp [-120, -80]
- [x] **Path success rate (Bayesian)** — Laplace smoothing 8 entries
- [x] **RSSI data logging** — 512 samples circular buffer, CRC fail flag
- [x] **ML training pipeline** — tools/train_model.py, Decision Tree + TFLite
- [x] **TFLite integration** — lightweight C inference (3-layer NN), no TFLite Micro runtime
- [x] **ML mode toggle** — Serial: MLON/MLOFF/MLTOGGLE, OLED แสดง ML probability

### Phase 5 — Tune & Harden
- [ ] ทดสอบ 2–3 hops จริง
- [ ] InfluxDB downsampling task
- [ ] Grafana alert rules

---

## Key Decisions

| หัวข้อ | ตัดสินใจ |
|--------|---------|
| ESP32 radio driver | RadioLib |
| Radio module | `#define USE_SX1262 / USE_RFM95 / USE_SX1276` |
| MQTT broker | HiveMQ Cloud (TLS 8883) |
| LoRa params | 923 MHz, SF7, BW125, CR4/5, sync 0x12, 12dBm |
| Address map | GW: 0x01–0x0E, Relay: 0x10–0x1F, Sensor: 0x20–0x7F |
| Encryption | XOR + CRC16 Modbus (key: `1234567890000000`) |
| Database | InfluxDB v2 (org: lora-iot, bucket: lora_sensor) |
| Dashboard | Grafana self-hosted (Docker) |
| OTA | ไม่ใช้ |
| Node 119 power mode | nRF52840 System-ON idle; SX1262 Continuous RX และ OLED เปิด; TX ทุก 2 นาที |
| Build ปัจจุบัน | `custom_node_id = 116`, `custom_radio = USE_RFM95`, `custom_low_power = 1` ([[Node116_RFM95_Build]]) |
| Relay forward delay | RSSI-based: 200/700/1500/3000ms ([[RSSI_Forwarding_Delay]]) |
| Relay forward delay | RSSI-based: 200/700/1500/3000ms ([[RSSI_Forwarding_Delay]]) |
| Cancel-on-heard | ✅ implemented — `cancelFromQueue()` สำหรับ data + ACK |
| IS_ACK relay | ✅ FLAGS bits 4-5 preserved, ACK payload relayed กลับ, หยุดที่ต้นทาง |
| Adaptive threshold | ✅ EMA α=0.3, clamp [-120, -80] ([[Adaptive_RSSI_Threshold]]) |
| Path success rate | ✅ Bayesian Laplace smoothing ([[Path_Success_Rate_Bayesian]]) |
| RSSI data logging | ✅ 512 samples, CRC fail flag ([[RSSI_Data_Logging]]) |
| ML inference | ✅ 3-layer NN (4→8→4→1), lightweight C, no TFLite runtime ([[TFLite_Integration]]) |
| ML toggle | ✅ Serial MLON/MLOFF/MLTOGGLE, OLED แสดง ML probability |

---

## RSSI-based Forwarding Delay

| RSSI | Delay |
|------|-------|
| ≥ -75 | 200 ms |
| -75 ถึง -90 | 700 ms |
| -90 ถึง -105 | 1500 ms |
| < -105 | 3000 ms |

## Relay Decision Logic

### Rule mode (MLOFF)
```
hop=0 (origin):
  ├─ RSSI ≥ adaptive threshold → source ใกล้ → Gateway ได้ยินเอง → NO-RELAY
  └─ RSSI < adaptive threshold → source ไกล → ต้อง relay

hop>0 (relayed):
  └─ relay ต่อตาม RSSI delay ปกติ
```

### ML mode (MLON — default)
```
hop=0 (origin):
  ├─ ML probability ≥ 50% → relay
  └─ ML probability < 50% → NO-RELAY

hop>0 (relayed):
  └─ relay ต่อตาม RSSI delay ปกติ
```

ML model: 3-layer Dense(4→8→4→1) Sigmoid, trained from 86 field samples
Features: [rssi, src, from, node] normalized

---

## Resource Usage (nRF52840)

| Resource | Usage |
|----------|-------|
| RAM | 33,748 / 248,832 bytes (13.6%) |
| Flash | 188,092 / 815,104 bytes (23.1%) |
| RSSI log buffer | 512 × 12 = 6,144 bytes |
| ML weights | 4 × (32+8+32+4+4+1) = ~320 bytes |

---

## Links

- [[00_MOC|← MOC]]
- อ้างอิง: ARCHITECTURE.md (ไฟล์ทำงาน ข้างนอก vault)
