# Mesh Protocol — STM32 + nRF52840 (สัญญาร่วม)

เอกสารนี้คือ **สัญญาเดียว** ที่ทั้งสอง firmware ต้องทำตาม ถ้าแก้ฝั่งใดฝั่งหนึ่ง ต้องแก้เอกสารนี้และอีกฝั่งพร้อมกัน

| repo | ไฟล์หลัก | Node ID |
|---|---|---|
| `2025-12-30_STM32_LoRaMesh` | `src/STM32_LoRa_P2P.ino` | 102–105 (build flag `NODE_ID`) |
| `2026-01-16_nRF52840` | `src/main.cpp` | 115–119 (`custom_node_id` ใน platformio.ini) |

---

## 1. ชั้นวิทยุ (ต้องตรงกันเป๊ะ ไม่งั้นไม่ได้ยินกัน)

| พารามิเตอร์ | ค่า |
|---|---|
| ความถี่ | 923.000 MHz |
| Spreading Factor | 7 |
| Bandwidth | 125 kHz |
| Coding Rate | 4/5 |
| Preamble | 8 symbol |
| Sync word | SX1276/RFM95 = `0x12` · SX1262 = `0x1424` (public — เทียบเท่ากัน) |
| Header | explicit |
| Hardware CRC | เปิด |
| TX power | STM32 คงที่ 10 dBm · nRF adaptive 7–17 dBm (ไม่กระทบ interop) |

## 2. โครง packet

```
+--------+----------+--------+---------+---------------------+--------+
| TO     | FROM     | ID     | FLAGS   | XOR(payload)        | CRC16  |
| 1 byte | 1 byte   | 1 byte | 1 byte  | n bytes             | 2 bytes|
+--------+----------+--------+---------+---------------------+--------+
```

- `TO` = `0xFF` (broadcast) เสมอ
- `FROM` = node ที่ยิงทอดนี้ (**ไม่ใช่ต้นทางเดิม** — ต้นทางอยู่ใน payload `N:`)
- `ID` = ตัวนับ 8 บิตของ node ที่ยิง เพิ่มขึ้นทุก packet รวม relay
- `FLAGS` = ดูข้อ 3
- CRC16 = Modbus (poly `0xA001`, init `0xFFFF`) คำนวณบน **ciphertext** ต่อท้ายแบบ big-endian

4 byte แรกเป็น header ของ RadioHead — ฝั่ง STM32 ให้ `RHDatagram` ใส่ให้, ฝั่ง nRF สร้างเองด้วยมือ

> ⚠️ STM32 **ห้ามใช้ `RHReliableDatagram`** — `sendtoWait()` เขียนทับ `FLAGS` เสมอ ทำให้ hop count หาย

## 3. FLAGS byte

| บิต | ชื่อ | ความหมาย |
|---|---|---|
| 0–3 | `FLAG_HOP_MASK` = `0x0F` | hop count 0–15 (ใช้จริง 0–3) |
| 4 | `FLAG_IS_ACK` = `0x10` | packet นี้เป็น ACK |
| 5 | `FLAG_ACK_REQ` = `0x20` | ต้นทางขอ ACK |
| 6–7 | สงวน | RadioHead ใช้ `RH_FLAGS_RETRY`(0x40) / `RH_FLAGS_ACK`(0x80) — **ห้ามแตะ** |

nibble ล่างตรงกับ `RH_FLAGS_APPLICATION_SPECIFIC` ของ RadioHead พอดี จึงปลอดภัย

**hop count อยู่ใน header ไม่ใช่ payload** — อ่านได้ก่อน decrypt และไม่กิน airtime
ต้นทางส่ง hop=0 · ทุกครั้งที่ทวน relay ทำ `hop+1` และคงบิต 4–5 เดิมไว้

## 4. การเข้ารหัส

XOR ซ้ำคีย์ กุญแจ `"1234567890000000"` (16 ไบต์) — `out[i] = in[i] ^ key[i % 16]`
กันคนอ่านผ่าน ๆ เท่านั้น ไม่ใช่ security จริง ทั้งสองฝั่งใช้คีย์เดียวกัน

## 5. Payload (หลัง decrypt)

### ข้อมูลเซนเซอร์
```
N:<src>|S:<seq>[|SIM:1]|T:<temp_C>|H:<hum_%>|B:<mV>[|LA:<lat>|LO:<lon>][|V:<relay>,<rssi>]...
```

| field | ผู้ส่ง | ความหมาย |
|---|---|---|
| `N:` | ทั้งคู่ | **ต้นทางเดิม** ไม่เปลี่ยนตอน relay |
| `S:` | ทั้งคู่ | sequence number ของต้นทาง (0–255 วน) |
| `SIM:1` | STM32 | ค่าจำลอง BME280 ไม่ตอบ |
| `T: H: B:` | ทั้งคู่ | จำนวนเต็ม · `B:` หน่วย mV |
| `LA: LO:` | nRF | GPS จากมือถือผ่าน BLE ทศนิยม 6 ตำแหน่ง |
| `V:` | ทั้งคู่ | VIA trace — ดูข้อ 6 |

### ACK (จาก gateway — ยังไม่ได้สร้าง)
```
ACK:<src>|S:<seq>|GW:<gw_id>
```
`FLAG_IS_ACK` ต้องเซ็ตด้วย · relay ทวน ACK ต่อโดยใช้ `(src, seq)` จากใน ACK ทำ dedup

## 6. VIA trace — เส้นทางเต็ม + RSSI รายทอด

ทุกครั้งที่ node ทวน ให้ **ต่อท้าย** payload:
```
|V:<relay_node_id>,<rssi_ที่ได้ยิน_hop_ก่อนหน้า>
```

ตัวอย่างเดินทาง 3 ทอด:
```
ต้นทาง 102 ส่ง (hop=0):
    N:102|S:45|T:28|H:65|B:3900

nRF 116 ได้ยินที่ -103 ทวน (hop=1):
    N:102|S:45|T:28|H:65|B:3900|V:116,-103

STM32 104 ได้ยิน 116 ที่ -98 ทวน (hop=2):
    N:102|S:45|T:28|H:65|B:3900|V:116,-103|V:104,-98
```
gateway ถอดได้: `102 --(-103)--> 116 --(-98)--> 104 --> GW`

- payload เปลี่ยน จึงต้อง **encrypt ใหม่ + คำนวณ CRC16 ใหม่** ทุกทอด
- เพดาน payload 200 ไบต์ (`MAX_PAYLOAD_LEN`) — ถ้าเต็ม ส่งต่อโดยไม่ต่อ trace แล้ว log ไว้
- โตประมาณ 11 ไบต์ต่อทอด · MAX_HOPS=3 คิดเป็น +33 ไบต์

## 7. กฎการทวน (relay) — เหมือนกันทั้งสองฝั่ง

ลำดับการตัดสิน:

1. `FROM == ตัวเอง` หรือ `N: == ตัวเอง` → `DROP-ECHO`
2. เป็น ACK และ ACK ส่งถึงเรา → `ACK-RCVD` (ไม่ทวนต่อ)
3. parse ต้นทางไม่ออก → `DROP-PARSE`
4. `(src, seq)` เคยเห็นใน 5 นาที → `DUP-SKIP` **และยกเลิกคิวทวนของตัวเอง** (cancel-on-heard)
5. `hop >= MAX_HOPS(3)` → `DROP-HOP`
6. `hop == 0` และ `rssi >= rssiSourceClose` → `NO-RELAY` (ต้นทางใกล้อยู่แล้ว)
   - nRF โหมด ML ใช้ neural net ตัดสินแทนกฎข้อนี้
7. นอกนั้น → เข้าคิว ทวนตามหน่วงเวลาข้อ 8

**Duplicate cache**: 10 ช่อง · TTL 5 นาที · key = `(src, seq)` (ACK ใช้ `(ack_src, ack_seq)`)

**Adaptive threshold (EMA)**: `rssiSourceClose` เริ่ม −100 dBm ปรับด้วย α=0.3 จากทุก packet ที่ CRC ผ่าน จำกัดช่วง −80 ถึง −120 dBm

## 8. หน่วงเวลาก่อนทวน (ตาม RSSI)

ตัวที่ได้ยินชัดสุด = ใกล้ต้นทางสุด = relay ที่ดีสุด → ยิงก่อน
ตัวที่ยิงทีหลังจะได้ยินคนแรกทวนแล้วยกเลิกคิวตัวเอง (ข้อ 7.4)

| RSSI | หน่วง |
|---|---|
| ≥ −75 dBm | 200 ms |
| −75 ถึง −90 | 700 ms |
| −90 ถึง −105 | 1500 ms |
| < −105 | 3000 ms |

คิว 5 ช่อง ทั้งสองฝั่ง

## 9. ค่าที่ต่างกันได้ (ไม่กระทบ interop)

| | STM32 | nRF52840 |
|---|---|---|
| TX interval ของตัวเอง | **120 วินาที** (`TX_INTERVAL_SEC`) | **120 วินาที** |
| TX power | คงที่ 10 dBm | adaptive 7–17 dBm |
| relay decision | กฎ RSSI + EMA | ML neural net (สลับเป็นกฎได้) |
| ของเพิ่ม | — | RSSI log 512 ตัวอย่าง, BLE GPS, path stats |

interval ตรงกันที่ 120 วินาที · การชนกันกันด้วย jitter สุ่ม 0–500 ms ก่อนยิง + CAD ตรวจช่องว่าง
ถ้าต้องประหยัดพลังงานฝั่ง STM32 override ได้: `-D TX_INTERVAL_SEC=300`

## 10. ข้อควรระวังตอนแก้โค้ด

- **STM32 flash เหลือน้อย** — 62100/65536 ไบต์ (94.8%) เหลือ ~3.4 KB
  - ห้ามใช้ `String::toFloat()` / `atof` / `strtod` — ดึง `_strtod_l`+`__gethex`+`__ieee754_pow` มา **~8.5 KB**
  - ถ้าต้องเพิ่มโค้ดอีก: บอร์ด blue pill ส่วนใหญ่มีแฟลชจริง 128 KB (โค้ดตรวจให้ตอน boot ดู `check_flash_size()`) เปลี่ยน `board = genericSTM32F103CB` ได้ **แต่ต้องยืนยันจาก serial ก่อนว่าชิปตัวนั้นรายงาน 128 KB จริง**
- ห้ามใส่ `N:` ซ้ำใน payload — `sanitizePayload()` ฝั่ง STM32 จะตัดทิ้งตั้งแต่ตัวที่สอง
- `FROM` ในเฮดเดอร์คือ hop ล่าสุด ไม่ใช่ต้นทาง — อย่าเอาไปใช้แทน `N:`
