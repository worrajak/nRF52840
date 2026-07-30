---
source: ARCHITECTURE.md
fetched: 2026-06-09
topic: system-architecture, esp32-gateway, mqtt, influxdb, docker
used_in: "[[_project-brief]]"
---

# Architecture — LoRa Mesh Network (Full Stack)

## Stack Overview

```
STM32 Sensor → LoRa → nRF52840 Relay → LoRa → ESP32 Gateway
  → HiveMQ Cloud (MQTT TLS 8883)
  → Node-RED (dedup + enrich)
  → InfluxDB v2 (lora-iot org, lora_sensor bucket)
  → Grafana (self-hosted Docker)
```

## Address Map

| Range | Role |
|-------|------|
| 0x01–0x0E | ESP32 Gateway |
| 0x10–0x1F | nRF52840 Relay |
| 0x20–0x7F | STM32 Sensor |
| 0xFF | Broadcast |

## Packet Format

```
[TO: 1B][FROM: 1B][MSG_ID: 1B][FLAGS: 1B][XOR+CRC16 Payload]

FLAGS bits:
  [3:0] hop_count    (max 3)
  [4]   is_ack       — FLAG_IS_ACK (0x10)
  [5]   ack_request  — FLAG_ACK_REQ (0x20)  ← origin sets this
  [6:7] reserved
```

## FLAGS Constants (ใน `main.cpp`)

```cpp
#define FLAG_HOP_MASK         0x0F
#define FLAG_IS_ACK           (1 << 4)   // 0x10
#define FLAG_ACK_REQ          (1 << 5)   // 0x20
```

## Relay Logic (nRF52840)

```
RX → parse ACK → cancelFromQueue() ถ้า ACK → cancel own data queue
  → cancelFromQueue() ถ้า data packet → cancel ถ้า relay อื่นส่งก่อน
  → duplicate check → addToRelayCache()
  → FLAGS preserved: (hop+1 & 0x0F) | (rhFlags & ~0x0F)
  → addToMessageQueue() with RSSI-based delay
       ≥ -75:   200ms
       -75:-90: 700ms
       -90:-105:1500ms
       < -105:  3000ms
  → Decision: hop=0 + RSSI≥-100 → NO-RELAY (source close, GW heard it)
               hop=0 + RSSI<-100 → RELAY (source far)
               hop>0 → always relay with RSSI delay
  → on send time: forward
```
```

## Payload Formats

```
Data:  N:<src>|S:<seq>|T:<temp_c>|H:<hum>|B:<bat_mv>|LA:<lat>|LO:<lon>
ACK:   ACK:<src>|S:<seq>|GW:<gw_id>
```

## LoRa Parameters (ต้องตรงกันทุก node)

| Param | Value |
|-------|-------|
| Frequency | 923.0 MHz |
| SF | 7 |
| BW | 125 kHz |
| CR | 4/5 |
| Sync Word | 0x12 |
| TX Power | 12 dBm |
| Preamble | 8 symbols |
| CRC | enabled |

## ESP32 Gateway — Key Code

```cpp
// RadioLib init (compat กับ nRF52840 bit-bang)
radio.begin(923.0, 125.0, 7, 5, 0x12, 12, 8);
radio.setCRC(true);

// ACK timing (ป้องกัน collision หลาย GW)
#define ACK_BASE_DELAY  (THIS_GW_ID * 200)  // GW1=200ms, GW2=400ms

// MQTT topics
// lora/<node_id>/data
// lora/<node_id>/ack
// lora/gw/<gw_id>/status
```

## Node-RED Dedup

```javascript
// key = node:seq, window = 60s
const key = `${d.node}:${d.seq}`;
if (seen[key]) return null;  // drop dup
seen[key] = now;
```

## Docker Compose Ports

| Service | Port |
|---------|------|
| InfluxDB | 8086 |
| Grafana | 3000 |
| Node-RED | 1880 |

## Encryption

XOR + CRC16 Modbus (poly 0xA001)
Key: `"1234567890000000"` (16 bytes)

## Development Phases

| Phase | งาน | สถานะ |
|-------|-----|-------|
| 1 | ESP32 Gateway + Docker stack | ⬜ ยังไม่เริ่ม |
| 2 | Multi-Gateway dedup/ACK | ⬜ |
| 3 | STM32 Sensor Node | ⬜ |
| 4 | nRF52840 Relay (RSSI-based delay + cancel-on-heard + IS_ACK) | ✅ **Implemented 2026-07-29** |
| 5 | Tune & Harden | ⬜ |

## Links

- [[_project-brief|← Project Brief]]
- [[RSSI_Forwarding_Delay]] — atomic idea
- [[Multi_GW_ACK_Dedup]] — atomic idea
