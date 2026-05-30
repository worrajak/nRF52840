# nRF52840 LoRa Mesh — สำรวจสถานะและแนวทางพัฒนา

วันที่: 2026-05-10

---

## 1. สิ่งที่มีแล้วใน Codebase ปัจจุบัน

### 1.1 Radio / Hardware
| Feature | สถานะ |
|---|---|
| SX1262 driver (command-based SPI) | ✅ มีใน `src/main.cpp` |
| RFM95/SX1276 driver (register-based SPI) | ✅ มีใน `src/main.cpp` |
| Compile-time radio selection (`#define USE_SX1262 / USE_RFM95`) | ✅ |
| Software SPI bit-bang (แก้ bug PINS_COUNT=34 ของ n-able) | ✅ |
| 923 MHz, SF7, BW125, CR4/5, TX 12 dBm | ✅ |
| OLED SSD1306 แสดงสถานะ | ✅ |
| BME280 อุณหภูมิ/ความชื้น (หรือ simulate ถ้าไม่มีฮาร์ดแวร์) | ✅ |
| Battery voltage monitoring | ✅ |
| BLE GPS รับพิกัดจากมือถือ (NimBLE) | ✅ |

### 1.2 Protocol / Networking
| Feature | สถานะ |
|---|---|
| RadioHead-compatible packet header `[TO][FROM][ID][FLAGS]` | ✅ |
| XOR encryption + CRC16 (compat กับ STM32 nodes เดิม) | ✅ |
| Broadcast P2P (`TO=0xFF`) | ✅ |
| Multi-hop relay — ส่งต่อ packet ที่รับมา | ✅ |
| Hop count ใน FLAGS byte (4 bits ล่าง) | ✅ |
| ป้องกัน loop ด้วย `MAX_HOPS = 3` | ✅ |
| Duplicate detection (RelayCache 10 entries, TTL 5 นาที) | ✅ |
| Message queue + random delay 1–3s (ป้องกัน collision) | ✅ |
| Echo guard (ไม่ relay ของตัวเอง) | ✅ |

### 1.3 Navigation / Awareness
| Feature | สถานะ |
|---|---|
| Haversine distance + bearing calculation | ✅ |
| Track ตำแหน่ง nodes อื่น (NodePosition table 8 entries) | ✅ |
| OLED แสดงระยะ/ทิศทาง node ที่ใกล้ที่สุด | ✅ |
| Compass arrow บน OLED | ✅ |

---

## 2. สิ่งที่ "ยังขาด" หรือยังไม่ได้ทำ

### 2.1 RSSI-based Smart Relay (จุดสำคัญ)

**สถานะปัจจุบัน:** โค้ดใช้ `RSSI_RELAY_THRESHOLD = -100` dBm เป็นเพียง "gate":
```
if (rssi <= -100 && hopCount < MAX_HOPS) → relay
```
คือ relay เมื่อสัญญาณจากผู้ส่งอ่อน ซึ่งสมเหตุสมผล แต่ยังมีปัญหาว่า:
- **ทุก node** ที่รับ packet ได้และผ่าน threshold จะ relay พร้อมกัน
- random delay ช่วยลด collision แต่ไม่ได้เลือก "relay ที่ดีที่สุด"

**สิ่งที่ยังขาด:**
- ❌ Neighbor RSSI table (ไม่รู้ว่า node ไหนได้ยิน node ไหนแรงแค่ไหน)
- ❌ Best-relay selection (เลือก relay ที่มี RSSI ดีที่สุดไปยัง destination)
- ❌ Route discovery / Route reply (RREQ/RREP)
- ❌ Directed unicast relay (ยังเป็น broadcast ทั้งหมด)
- ❌ SNR ถูกอ่านแต่ไม่นำมาใช้ใน routing decision

### 2.2 อื่นๆ ที่น่าสนใจ
- ❌ Adaptive SF/BW (ปรับ spreading factor ตามระยะ/คุณภาพสัญญาณ)
- ❌ ACK / Confirmed delivery
- ❌ Node ID แบบ dynamic (hardcode อยู่ที่ `THIS_NODE_ADDRESS = 116`)
- ❌ Unicast addressing (ส่งหาเป้าหมายโดยตรง ไม่ต้อง broadcast ทุก hop)

---

## 3. แนวทางที่ควรพัฒนาต่อ

### Option A — RSSI-Based Forwarding Delay (ง่ายที่สุด, ไม่ต้องมี routing table)

**แนวคิด:** แทนที่จะใช้ random delay เดียวกัน ให้ node ที่มี RSSI ดีกว่า delay น้อยกว่า
```
forward_delay = base_delay + map(-50 ↔ -120 dBm → 0 ↔ 3000 ms)
```
- Node ที่ได้ยินชัด (RSSI สูง) จะ relay ก่อน
- Node อื่นที่ยังไม่ได้ส่งก็จะได้ยิน relay แรกแล้ว cancel ตัวเอง

**ข้อดี:** ไม่ต้องเพิ่ม packet type ใหม่ ไม่ต้อง routing table  
**ข้อเสีย:** ยังเป็น broadcast, ยังมีโอกาส relay ซ้ำ

---

### Option B — Neighbor Advertisement + Route Selection (สมบูรณ์กว่า)

**Phase 1: Neighbor Discovery**
- แต่ละ node broadcast "HELLO packet" ทุก N วินาที
- format: `[TYPE=HELLO][FROM=nodeId][SEQ][payload=RSSI ของ neighbors ที่ได้ยิน]`
- ทุก node สร้าง **Neighbor Table**:
  ```
  neighbor_id | rssi_from_me | rssi_i_heard_them | last_seen
  ```

**Phase 2: Smart Relay Decision**
- เมื่อรับ data packet มา → เช็ค neighbor table
- หา relay path ที่ RSSI ดีที่สุดไปยัง destination (หรือ gateway)
- ส่งแบบ unicast ไปยัง next-hop นั้น

**Phase 3: Route Reply (optional)**
- Destination ส่ง RREP กลับบอก path ที่ดีที่สุด
- คล้าย AODV lite

**ข้อดี:** ประหยัด airtime, ลด collision  
**ข้อเสีย:** complex, ต้องเพิ่ม packet type ใหม่, ต้องใช้ RAM เพิ่ม

---

### Option C — Opportunistic Relay พร้อม RSSI Threshold แบบ 2 ระดับ (กึ่งกลาง)

```
ถ้า rssi_ถึงฉัน > -80 dBm  → ฉันอาจเป็น good relay → delay สั้น (500ms)
ถ้า rssi_ถึงฉัน = -80~-100  → relay ปานกลาง → delay กลาง (1500ms)
ถ้า rssi_ถึงฉัน < -100      → relay อ่อน → delay นาน (3000ms) หรือ skip
```
- ใช้ RSSI ที่รับได้ตอน RX เป็น metric เดียว (ไม่ต้อง routing table)
- ถ้ามี relay ดีกว่าส่งก่อน → node อื่นได้ยินแล้ว cancel ออกจาก queue

---

## 4. สรุปสถานะ Relay Logic ปัจจุบัน (ความเข้าใจผิด)

> **หมายเหตุสำคัญ:** `RSSI_RELAY_THRESHOLD = -100` หมายความว่า relay เมื่อ RSSI ≤ -100 (สัญญาณอ่อน)  
> นี่หมายถึง "ถ้า node ต้นทางไกลมาก (สัญญาณอ่อน) → ค่อย relay" ซึ่งถูกต้องเชิงตรรกะ  
> แต่ยังไม่ได้เลือก "best relay node" — ทุก node ที่ผ่าน threshold จะ relay ทั้งหมด

---

## 5. คำถามที่ต้องตัดสินใจก่อนลงมือ

1. **โปรเจคนี้มี gateway หรือไม่?** (มี node เป้าหมายที่ชัดเจน หรือทุก node ควรได้รับทุก packet)
2. **ต้องการ confirmed delivery / ACK ไหม?** (ถ้าต้องการ → ต้องมี unicast + ACK)
3. **Network size คาดหวังกี่ nodes?** (< 10 → opportunistic ok, > 10 → ควรมี routing table)
4. **ต้องการ low-power mode ไหม?** (มี `.bak` ของ low-power TX อยู่แล้ว)
5. **จะ hardcode Node ID หรือ auto-assign?** (ตอนนี้ hardcode 116)
6. **ต้องการ inter-op กับ STM32 nodes เดิมไหม?** (ถ้าใช่ → packet format ต้องเหมือนเดิม)

---

## 6. ไฟล์ที่น่าสนใจใน Project

| ไฟล์ | รายละเอียด |
|---|---|
| `src/main.cpp` | Main code — Node 116, SX1262/RFM95 dual, BLE GPS, relay |
| `main_rfm95.bak` | Node 117 backup — RFM95 only, ไม่มี BLE |
| `main_sx1262.bak` | Node 116 backup — SX1262 only, ไม่มี BLE (เก่ากว่า) |
| `src/main_sx1262_relay_hop6l_v1.1.bak` | SX1262 relay hop 6L version |
| `src/main_relay_hop.bak` | Relay hop prototype |
| `src/main_rx_always_on_relay.bak` | RX always-on relay variant |
| `src/main_rx_relay_ble_v1.1.bak` | RX relay + BLE version 1.1 |
| `include/lora_config.h` | LoRa config struct, frequency definitions, packet struct |
| `platformio.ini` | Build config — n-able platform, NimBLE, SSD1306, BME280 |

---

## 7. แนวทางสำหรับโปรเจคใหม่ (Proposed Next Steps)

### ขั้นที่ 1: Clean Architecture
- แยก radio driver ออกเป็น `radio_sx1262.cpp` / `radio_rfm95.cpp`
- แยก relay logic ออกเป็น `relay.cpp`
- แยก packet protocol เป็น `protocol.h`
- ทำให้ Node ID configure ได้ (flash storage หรือ compile flag)

### ขั้นที่ 2: RSSI-based Smart Relay (เลือก Option A หรือ C ก่อน)
- เพิ่ม forwarding delay แปรผันตาม RSSI
- เพิ่ม "cancel relay if already heard relay from better node"
- Log RSSI ของแต่ละ relay สำหรับ debug

### ขั้นที่ 3: Neighbor Table (Option B — ถ้าต้องการ)
- เพิ่ม HELLO packet type
- Neighbor table ใน RAM
- Smart next-hop selection

### ขั้นที่ 4: Gateway / Sink node
- Node พิเศษที่รับ packet แล้วส่งต่อไป UART/BLE/WiFi
- แสดง network topology บน PC/phone
