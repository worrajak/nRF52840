# 🗺️ Map of Content — nRF52840 LoRa Mesh

> vault: `nRF52840_vault/` | สร้าง: 2026-06-09 | framework: PARA + Zettelkasten

---

## 🚀 Projects (Active)

| Project | คำอธิบาย | สถานะ |
|---------|---------|-------|
| [[_project-brief\|LoRa Mesh Network]] | STM32 → nRF52840 relay → ESP32 GW → MQTT → InfluxDB/Grafana | 🟢 Phase 4 เสร็จ (ML/TFLite integrated) |

---

## 📚 Literature Notes

| Note | หัวข้อ | สำคัญ |
|------|-------|-------|
| [[Wildfire_Reward_DePIN_Design]] | 🔥 Blockchain/Token reward: Proof-of-Maintenance dual-sig + 6-pillar DePIN + SkillChain reuse + custodial-C (Tron) | ⭐⭐⭐ |
| [[Architecture_LoRa_Mesh]] | Full stack: packet format, address map, Docker, MQTT, Grafana | ⭐⭐⭐ |
| [[Roadmap_LoRa_Mesh]] | สำรวจ codebase เดิม + สิ่งที่ขาด + options | ⭐⭐⭐ |
| [[Sensor_Protocol_Design]] | Modbus vs SDI-12 vs 4-20mA vs I2C — decision tree | ⭐⭐ |
| [[nRF52840_Dual_Role]] | Dual role relay+sensor, memory budget, PINS_COUNT bug | ⭐⭐ |
| [[Matter_Protocol_Analysis]] | Matter feasibility: framework conflict, Thread range, Home Assistant | ⭐⭐ |
| [[Matter_Why_Future]] | ประวัติ Matter, memory breakdown per layer, roadmap 1.0→1.5 | ⭐ |
| [[LoRa_P2P_Original]] | จุดเริ่มต้น: P2P codebase เดิม, pin assignments, original format | ⭐ |
| [[Schematic_Pin_Analysis]] | วิเคราะห์ schematic vs code pin conflicts | ⭐⭐ |
| [[STM32_to_nRF52840_Mapping]] | STM32→nRF52840 pin/function mapping | ⭐⭐ |
| [[Pinout_Reference]] | Nice!Nano pinout reference table | ⭐⭐ |
| [[PlatformIO_Build_Config]] | Build config: node ID, radio, Apple Silicon fix | ⭐⭐⭐ |
| [[ML_Training_Pipeline]] | tools/train_model.py — Decision Tree + TFLite | ⭐⭐⭐ |
| [[BLE_GPS_Sender]] | Web app: phone GPS → nRF52840 via BLE | ⭐ |
| [[UF2_Post_Build]] | Post-build script: Intel HEX → UF2 | ⭐ |
| [[RSSI_Log_Dataset]] | 86 samples field data for ML training | ⭐⭐⭐ |

---

## 💡 Zettel (Atomic Ideas)

| Zettel | แนวคิด |
|--------|-------|
| [[RSSI_Forwarding_Delay]] | Best relay ส่งก่อนเสมอ โดยไม่ต้อง routing table |
| [[IS_ACK_Relay_Cancel_On_Heard]] | ACK relay ย้อนกลับ + cancel relay ซ้ำเมื่อได้ยิน relay อื่น |
| [[Adaptive_RSSI_Threshold]] | EMA ปรับ RSSI threshold อัตโนมัติตามสภาพแวดล้อม |
| [[Path_Success_Rate_Bayesian]] | Bayesian probability วัดโอกาส packet ถึง Gateway |
| [[RSSI_Data_Logging]] | เก็บ RSSI log 512 samples → ดึงผ่าน Serial → train ML |
| [[TFLite_Integration]] | ML inference บน nRF52840 — 3-layer NN แทน rule-based |
| [[Multi_GW_ACK_Dedup]] | หลาย GW ไม่ชนกัน: GW_ID × 200ms + Node-RED dedup |
| [[Software_SPI_Bitbang_nRF52840]] | Workaround สำหรับ n-able PINS_COUNT=34 bug |
| [[PIO_Toolchain_arm64_Override]] | 🍎 Apple Silicon: n-able toolchain x86 → override arm64 (ไม่ต้อง Rosetta) — build ผ่าน |
| [[FakeTec_Button_UI_Pin]] | ปุ่ม SW2/D6 คือ P1.00 GPIO 32 + OLED UI 3 หน้า |
| [[UF2_Post_Build_nRF52840]] | สร้าง UF2 จริงอัตโนมัติหลัง PlatformIO build |
| [[PIO_Node_ID_Radio_Config]] | ตั้ง Node ID และเลือก SX1262/RA-01SH หรือ RFM95 จาก platformio.ini |
| [[SX1262_Low_Power_RX_Always_On]] | Node 119: System-ON idle โดย SX1262 RX และ OLED เปิดตลอด ส่งทุก 5 นาที |
| [[Node116_RFM95_Build]] | 🔧 Build ปัจจุบัน: Node 116 + RFM95 + low-power · ตาราง UF2 · กับดักสลับ radio ผิดรุ่น |

---

## 📁 Refs (ไฟล์อ้างอิง Hardware)

| ไฟล์ | คำอธิบาย |
|------|---------|
| [[pinout]] | nRF52840 Pro Micro pinout |
| [[SCHEMATIC_PIN_ANALYSIS]] | วิเคราะห์ schematic + pin conflicts |
| [[STM32_TO_NRF52840_MAPPING]] | ตาราง mapping pin STM32 → nRF52840 |

---

## 🗄️ Archives

| รายการ | เหตุผลที่ archive |
|--------|----------------|
| [[MESHTASTIC_PIN_ADAPTATION]] | Meshtastic path ถูกยกเลิก → ใช้ Custom firmware |
| [[QUICK_START_MESHTASTIC]] | ไม่เกี่ยวกับ Mesh LoRa project นี้ |
| [[UPLOAD_INSTRUCTIONS]] | UF2 upload สำหรับ Meshtastic |

---

## 📚 Literature Notes (Full List)

| Note | Source File |
|------|-------------|
| [[Architecture_LoRa_Mesh]] | `ARCHITECTURE.md` |
| [[Roadmap_LoRa_Mesh]] | `ROADMAP.md` |
| [[Sensor_Protocol_Design]] | `SENSOR_PROTOCOL_DESIGN.md` |
| [[nRF52840_Dual_Role]] | `NRF52840_AS_SENSOR_NODE.md` |
| [[Matter_Protocol_Analysis]] | `MATTER_PROTOCOL_DISCUSSION.md` |
| [[Matter_Why_Future]] | `MATTER_WHY_AND_FUTURE.md` |
| [[LoRa_P2P_Original]] | `README_LoRa_P2P.md` |
| [[Schematic_Pin_Analysis]] | `SCHEMATIC_PIN_ANALYSIS.md` |
| [[STM32_to_nRF52840_Mapping]] | `STM32_TO_NRF52840_MAPPING.md` |
| [[Pinout_Reference]] | `pinout.md` |
| [[PlatformIO_Build_Config]] | `platformio.ini` |
| [[ML_Training_Pipeline]] | `tools/train_model.py` |
| [[BLE_GPS_Sender]] | `ble_gps_sender.html` |
| [[UF2_Post_Build]] | `copy_uf2.py` |
| [[RSSI_Log_Dataset]] | `rssi_log.csv` |

---

## Quick Navigation

```
เริ่มต้น:   [[_project-brief]]          ← checklist Phase 1–5
สถาปัตยกรรม: [[Architecture_LoRa_Mesh]] ← packet format, code snippets
แนวคิดหลัก: [[RSSI_Forwarding_Delay]]   ← relay mechanism
            [[IS_ACK_Relay_Cancel_On_Heard]] ← ACK relay + cancel
            [[TFLite_Integration]]       ← ML inference (Phase 4)
            [[Multi_GW_ACK_Dedup]]       ← multi-gateway strategy
```

---

## Phase Status

| Phase | งาน | สถานะ |
|-------|-----|-------|
| 1 | ESP32 Gateway + Docker stack | ⬜ ยังไม่เริ่ม |
| 2 | Multi-Gateway dedup/ACK | ⬜ |
| 3 | STM32 Sensor Node | ⬜ |
| 4 | nRF52840 Relay (RSSI-based delay + cancel-on-heard + IS_ACK relay) | ✅ **Implemented 2026-07-29** |
| 5 | Tune & Harden | ⬜ |
