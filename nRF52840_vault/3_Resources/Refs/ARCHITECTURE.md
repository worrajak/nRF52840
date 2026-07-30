# LoRa Mesh Network — System Architecture & Planning

วันที่: 2026-05-10  
สถานะ: **FINAL — พร้อม implement**

---

## 1. ภาพรวมระบบ (Complete Stack)

```
┌─────────────────────────────────────────────────────────────────────┐
│  SENSOR LAYER  (STM32 + RFM95, Arduino IDE)                         │
│  [Node 0x20]  [Node 0x21]  [Node 0x22]                             │
│  coords: hardcode  TX ทุก 60s  รอ ACK 10s + retry 3               │
└──────┬──────────────────┬──────────────────┬────────────────────────┘
       │ hop=0            │ hop=0            │ hop=0
       ▼                  ▼                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│  RELAY LAYER  (nRF52840, PlatformIO)                                │
│  [Relay 0x10]                  [Relay 0x11]                         │
│  RSSI-based delay — best relay ส่งก่อน, คนอื่น cancel              │
│  forward ทั้ง data และ ACK ย้อนกลับ                                 │
└────────────────────────┬────────────────────────────────────────────┘
                         │ hop++
              ┌──────────┴──────────┐
              ▼                     ▼
┌─────────────────────┐  ┌─────────────────────┐
│  ESP32 GW #1 (0x01) │  │  ESP32 GW #2 (0x02) │
│  RadioLib            │  │  RadioLib            │
│  WiFiManager         │  │  WiFiManager         │
│  ACK delay: 200ms    │  │  ACK delay: 400ms    │
└──────────┬──────────┘  └──────────┬──────────┘
           │ MQTT TLS / 8883         │ MQTT TLS / 8883
           └──────────┬─────────────┘
                      │ (ทั้งสอง publish — dedup ที่ Node-RED)
                      ▼
          ┌────────────────────────┐
          │  HiveMQ Cloud          │
          │  MQTT Broker (TLS)     │
          └────────────┬───────────┘
                       │ subscribe
                       ▼
          ┌────────────────────────┐   Docker Compose
          │  Node-RED              │◄──────────────────┐
          │  dedup → enrich        │                   │
          │  → InfluxDB v2 write   │  ┌────────────────┴──────┐
          └────────────┬───────────┘  │  Self-hosted (Docker) │
                       │ Line Protocol │                       │
                       ▼               │  influxdb:2.7  :8086  │
          ┌────────────────────────┐   │  grafana:latest :3000 │
          │  InfluxDB v2           │   │  nodered:latest :1880 │
          │  Org: lora-iot         │   └───────────────────────┘
          │  Bucket: lora_sensor   │
          └────────────┬───────────┘
                       │ Flux query
                       ▼
          ┌────────────────────────┐
          │  Grafana (self-hosted) │
          │  Time series / Geomap  │
          │  Alerts                │
          └────────────────────────┘

  ── ACK ย้อนกลับ ────────────────────────────────────────────────►
  GW1 ACK delay=200ms → ส่งก่อน → GW2 ได้ยิน → cancel
  Relay forward ACK → STM32 รับ → mark delivered
```

---

## 2. Decisions สุดท้าย (ปิดแล้วทุกข้อ)

| หัวข้อ | ตัดสินใจ |
|---|---|
| ESP32 radio driver | RadioLib |
| Radio module select | `#define USE_SX1262 / USE_RFM95 / USE_SX1276` |
| MQTT broker | HiveMQ Cloud (TLS port 8883) |
| Multiple gateway dedup | Server-side ที่ Node-RED (node, seq) 60s window |
| Multiple gateway ACK | GW ID × 200ms base delay + cancel ถ้าได้ยิน GW อื่น |
| STM32 GPS | Hardcode `MY_LAT / MY_LON` ต่อ node (ไม่มี GPS hardware) |
| ESP32 WiFi config | WiFiManager + custom MQTT params บันทึก NVS |
| Time-series DB | InfluxDB v2 |
| Dashboard | Grafana self-hosted (Docker) |
| Backend flow | Node-RED (Docker) |
| OTA | ไม่ใช้ |
| Encryption | XOR + CRC16 (เหมือนเดิม, compat STM32) |

---

## 3. Node Roles & Hardware

| Role | Hardware | Radio | Framework | SPI |
|---|---|---|---|---|
| Sensor Node | STM32 (F103/F411) | RFM95 | Arduino IDE + STM32duino | Hardware SPI |
| Relay Node | nRF52840 Pro Micro | SX1262 หรือ RFM95 | PlatformIO | Software bit-bang |
| Gateway | ESP32 DevKit | RFM95 / SX1262 / SX1276 | PlatformIO | Hardware SPI |

---

## 4. Address Map

```
0x01–0x0E   ESP32 Gateway     (GW1=0x01, GW2=0x02, ...)
0x10–0x1F   nRF52840 Relay    (Relay1=0x10, Relay2=0x11, ...)
0x20–0x7F   STM32 Sensor      (Node1=0x20, Node2=0x21, ...)
0xFF        Broadcast
```

---

## 5. Packet Format

```
[TO: 1B][FROM: 1B][MSG_ID: 1B][FLAGS: 1B][Payload: XOR+CRC16]

FLAGS:
  bits[3:0] = hop_count    (0=origin, relay เพิ่มทีละ 1, max=3)
  bit [4]   = is_ack       (0=data, 1=ACK)
  bit [5]   = ack_request  (1=ขอ ACK กลับจาก gateway)
  bits[7:6] = reserved

Macros:
  GET_HOP(f)  = (f) & 0x0F
  INC_HOP(f)  = ((f) & 0xF0) | ((GET_HOP(f) + 1) & 0x0F)
  IS_ACK(f)   = (f) & 0x10
  WANT_ACK(f) = (f) & 0x20
```

---

## 6. Payload Format & Shared Code

### 6.1 Data Packet
```
N:<src>|S:<seq>|T:<temp_c>|H:<hum_pct>|B:<bat_mv>|LA:<lat>|LO:<lon>
ตัวอย่าง: N:32|S:157|T:28|H:65|B:3820|LA:13.756300|LO:100.501800
```

### 6.2 ACK Packet
```
ACK:<src>|S:<seq>|GW:<gw_id>
ตัวอย่าง: ACK:32|S:157|GW:1
```

### 6.3 `shared/lora_protocol.h`
```cpp
#define ADDR_BROADCAST   0xFF
#define MAX_HOPS         3
#define LORA_FREQ        923.0
#define LORA_BW          125.0
#define LORA_SF          7
#define LORA_CR          5
#define LORA_SYNC        0x12
#define LORA_TX_POWER    12
#define LORA_PREAMBLE    8

#define FLAG_HOP_MASK    0x0F
#define FLAG_IS_ACK      0x10
#define FLAG_ACK_REQUEST 0x20

#define GET_HOP(f)   ((f) & FLAG_HOP_MASK)
#define INC_HOP(f)   (((f) & ~FLAG_HOP_MASK) | ((GET_HOP(f)+1) & FLAG_HOP_MASK))
#define IS_ACK(f)    ((f) & FLAG_IS_ACK)
#define WANT_ACK(f)  ((f) & FLAG_ACK_REQUEST)
```

### 6.4 `shared/lora_crypto.h`
```cpp
#define CRYPTO_KEY     "1234567890000000"
#define CRYPTO_KEY_LEN 16

inline void xorEncrypt(const char* in, int len, uint8_t* out) {
    for (int i = 0; i < len; i++)
        out[i] = (uint8_t)in[i] ^ (uint8_t)CRYPTO_KEY[i % CRYPTO_KEY_LEN];
}
inline void xorDecrypt(const uint8_t* in, int len, char* out) {
    for (int i = 0; i < len; i++)
        out[i] = (char)(in[i] ^ (uint8_t)CRYPTO_KEY[i % CRYPTO_KEY_LEN]);
    out[len] = '\0';
}
inline uint16_t crc16(const uint8_t* data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}
```

### 6.5 `shared/lora_payload.h`
```cpp
inline int buildDataPayload(char* buf, int bufLen, uint8_t nodeId, uint8_t seq,
    int16_t temp, uint8_t hum, uint16_t bat_mv, double lat, double lon) {
    return snprintf(buf, bufLen,
        "N:%d|S:%d|T:%d|H:%d|B:%d|LA:%.6f|LO:%.6f",
        nodeId, seq, temp, hum, bat_mv, lat, lon);
}
inline int buildACKPayload(char* buf, int bufLen,
    uint8_t ack_src, uint8_t ack_seq, uint8_t gw_id) {
    return snprintf(buf, bufLen, "ACK:%d|S:%d|GW:%d", ack_src, ack_seq, gw_id);
}
inline int parseIntField(const char* payload, const char* key) {
    const char* p = strstr(payload, key);
    return p ? atoi(p + strlen(key)) : -1;
}
inline double parseFloatField(const char* payload, const char* key) {
    const char* p = strstr(payload, key);
    return p ? atof(p + strlen(key)) : 0.0;
}
```

---

## 7. STM32 Sensor Node

### 7.1 Config ต่อ Node (`config.h`)
```cpp
#define MY_NODE_ID      0x20      // เปลี่ยนตาม node: 0x20, 0x21, ...
#define MY_LAT          13.756300 // hardcode พิกัด
#define MY_LON          100.501800

#define TX_INTERVAL_MS  60000     // 1 นาที
#define ACK_TIMEOUT_MS  10000     // 10 วินาที
#define MAX_RETRY       3
```

### 7.2 State Machine
```
IDLE → READ_SENSOR → TX(hop=0,ack_req=1) → WAIT_ACK(10s)
                         ▲                      │         │
                         │ retry (seq เดิม)      │ ACK ok  │ timeout
                         └──── RETRY_CHECK ◄─────┘         │
                               (count < 3?)                 │
                                    │ no                    │
                                    ▼                       ▼
                               failed++              DELIVERED++
                                    └──────────► SLEEP(60s) ◄────┘
```

### 7.3 ACK Detection
```cpp
// รับ packet ขณะอยู่ใน WAIT_ACK:
if (IS_ACK(pkt[3])) {
    // parse: ACK:<src>|S:<seq>|GW:<gw>
    int ack_src = parseIntField(decrypted, "ACK:");
    int ack_seq = parseIntField(decrypted, "S:");
    if (ack_src == MY_NODE_ID && ack_seq == currentSeq)
        ackReceived = true;  // ไม่สนใจ GW ไหน รับแรกที่มาถึงก็พอ
}
```

---

## 8. nRF52840 Relay Node

### 8.1 RSSI-based Forwarding Delay
```
รับ packet → filter → คำนวณ delay → รอ → ส่งต่อ (หรือ cancel)

Filter (ทิ้ง):
  hop_count >= MAX_HOPS(3)
  dup cache hit (src+seq สำหรับ data, src+seq+gw_id สำหรับ ACK)
  FROM == ตัวเอง

Delay ตาม RSSI:
  rssi >= -75    →  200 ms
  -75 to -90     →  700 ms
  -90 to -105    → 1500 ms
  < -105         → 3000 ms

ระหว่างรอ delay:
  ถ้าได้ยิน packet เดียวกันซ้ำ → cancel (มี relay ที่ดีกว่าส่งแล้ว)

ส่งต่อ:
  FROM = ตัวเอง, hop_count++ (INC_HOP), broadcast
  ทำเหมือนกันกับ ACK packet (IS_ACK=1)
```

### 8.2 สิ่งที่ต้องเพิ่มใน code ปัจจุบัน
```
มีแล้ว ✓:   hop_count, dup cache (src+seq), message queue, echo guard
เพิ่ม  ✗:   IS_ACK relay, RSSI-based delay, cancel-on-heard, ACK dedup key
```

---

## 9. ESP32 Gateway

### 9.1 Radio — RadioLib + Compile-time Selection

```cpp
//#define USE_SX1262
#define USE_RFM95
//#define USE_SX1276    // driver เดียวกับ RFM95

// ESP32 SPI pins (DevKit — ปรับได้)
#define LORA_SCK   18
#define LORA_MISO  19
#define LORA_MOSI  23
#define LORA_CS     5
#define LORA_RST   14

#ifdef USE_SX1262
  #define LORA_DIO1  26
  #define LORA_BUSY  27
  SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
#else
  #define LORA_DIO0  26
  SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, -1);
#endif

// Init — ทุก param ตรงกับ nRF52840:
SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
radio.begin(923.0, 125.0, 7, 5, 0x12, 12, 8);
radio.setCRC(true);
radio.startReceive();
```

> **ทำไม RadioLib compat กับ nRF52840 bit-bang ได้ทันที:**  
> RadioLib ไม่เพิ่ม header ใดๆ ส่งแค่ raw bytes → bytes เดิมทุก bit  
> Sync word `0x12` — RadioLib แปลง SX1262 เป็น `0x1224` อัตโนมัติ

### 9.2 WiFiManager

```cpp
#include <WiFiManager.h>

// Custom MQTT parameters (บันทึกลง NVS)
WiFiManagerParameter p_host("mqtt_host", "MQTT Host", "x.hivemq.cloud", 64);
WiFiManagerParameter p_port("mqtt_port", "MQTT Port", "8883", 6);
WiFiManagerParameter p_user("mqtt_user", "MQTT User", "", 32);
WiFiManagerParameter p_pass("mqtt_pass", "MQTT Pass", "", 32);

void setup() {
    WiFiManager wm;
    wm.addParameter(&p_host);
    wm.addParameter(&p_port);
    wm.addParameter(&p_user);
    wm.addParameter(&p_pass);
    wm.setSaveConfigCallback(saveConfig);

    char apName[20];
    snprintf(apName, sizeof(apName), "LoRa-GW-%d", THIS_GW_ID);

    // ถ้า connect ไม่ได้ใน 30s → เปิด AP mode
    wm.setConfigPortalTimeout(30);
    wm.autoConnect(apName);
}

void saveConfig() {
    Preferences prefs;
    prefs.begin("gw", false);
    prefs.putString("host", p_host.getValue());
    prefs.putInt("port",    atoi(p_port.getValue()));
    prefs.putString("user", p_user.getValue());
    prefs.putString("pass", p_pass.getValue());
    prefs.end();
}
```

### 9.3 MQTT TLS (HiveMQ Cloud)

```cpp
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

WiFiClientSecure tlsClient;
PubSubClient     mqtt(tlsClient);

void initMQTT() {
    tlsClient.setInsecure();   // skip cert verify (พอสำหรับ HiveMQ Cloud)
    // หรือ tlsClient.setCACert(ISRG_ROOT_X1);  ถ้าต้องการ verify
    mqtt.setServer(mqtt_host, 8883);
    mqtt.setBufferSize(1024);
}
```

### 9.4 ACK Timing (ป้องกัน collision หลาย GW)

```cpp
#define THIS_GW_ID       1           // เปลี่ยนต่อ gateway
#define ACK_BASE_DELAY   (THIS_GW_ID * 200)   // ms: GW1=200, GW2=400, ...

struct ACKEntry {
    uint8_t  ack_src;
    uint8_t  ack_seq;
    uint32_t sendAt;      // millis() ที่จะส่ง
    bool     cancelled;
};
ACKEntry ackQueue[4];

// เมื่อ gateway รับ data packet:
void scheduleACK(uint8_t src, uint8_t seq) {
    for (auto& e : ackQueue) {
        if (!e.cancelled && e.ack_src == 0) {
            e = { src, seq, millis() + ACK_BASE_DELAY, false };
            return;
        }
    }
}

// เมื่อ gateway รับ ACK packet จาก GW อื่น:
void checkCancelACK(uint8_t ack_src, uint8_t ack_seq) {
    for (auto& e : ackQueue) {
        if (e.ack_src == ack_src && e.ack_seq == ack_seq)
            e.cancelled = true;   // GW อื่นส่งก่อนแล้ว
    }
}
```

### 9.5 Offline MQTT Buffer

```cpp
struct MQTTEntry { char topic[64]; char payload[512]; bool inUse; };
MQTTEntry mqttBuf[20];

void publishOrBuffer(const char* topic, const char* payload) {
    if (mqtt.connected()) {
        mqtt.publish(topic, payload);
        return;
    }
    for (auto& e : mqttBuf) {
        if (!e.inUse) {
            strlcpy(e.topic, topic, sizeof(e.topic));
            strlcpy(e.payload, payload, sizeof(e.payload));
            e.inUse = true;
            return;
        }
    }
    // buffer เต็ม → ทิ้ง packet เก่าสุด (circular overwrite — implement ถ้าต้องการ)
}

void flushBuffer() {
    for (auto& e : mqttBuf) {
        if (e.inUse && mqtt.publish(e.topic, e.payload))
            e.inUse = false;
    }
}
```

### 9.6 MQTT Topics & JSON

```
lora/<node_id>/data      ← sensor data
lora/<node_id>/ack       ← delivery confirmation
lora/gw/<gw_id>/status   ← heartbeat ทุก 60s
```

**lora/32/data:**
```json
{
  "node": 32, "seq": 157, "gw_id": 1,
  "temp_c": 28.5, "hum_pct": 65, "bat_mv": 3820,
  "rssi": -87, "snr": 8.5, "hops": 2, "relayed_by": 16,
  "lat": 13.756300, "lon": 100.501800,
  "ts": 1746873600
}
```

**lora/gw/1/status:**
```json
{ "gw_id": 1, "uptime_s": 3600, "wifi_rssi": -55, "ts": 1746873600 }
```

---

## 10. Node-RED

### 10.1 Flow

```
[MQTT in: lora/+/data]  → [dedup] → [enrich] → [InfluxDB v2 out: lora_sensor]
[MQTT in: lora/+/ack]              →            [InfluxDB v2 out: lora_delivery]
[MQTT in: lora/gw/+/status]        →            [InfluxDB v2 out: lora_gateway]
```

### 10.2 Dedup Function
```javascript
const WINDOW = 60000;
const seen   = flow.get('seen') || {};
const d      = msg.payload;
const key    = `${d.node}:${d.seq}`;
const now    = Date.now();

for (const k in seen) if (now - seen[k] > WINDOW) delete seen[k];

if (seen[key]) {
    node.warn(`dedup drop node=${d.node} seq=${d.seq} gw=${d.gw_id}`);
    flow.set('seen', seen);
    return null;
}
seen[key] = now;
flow.set('seen', seen);
return msg;
```

### 10.3 Enrich Function
```javascript
const d = msg.payload;
d.bat_pct     = Math.max(0, Math.min(100, Math.round((d.bat_mv - 3000) / 12)));
d.hop_quality = ['direct','good','fair','poor'][Math.min(d.hops, 3)];
d.received_ms = Date.now();
return msg;
```

### 10.4 InfluxDB v2 Node (node-red-contrib-influxdb)

```
node: influxdb out
  version:      2.0
  url:          http://influxdb:8086      (Docker internal)
  token:        <admin token>
  organisation: lora-iot
  bucket:       lora_sensor

msg.payload ส่งเป็น array of points:
[
  {
    measurement: "lora_sensor",
    tags:   { node_id: String(d.node), gw_id: String(d.gw_id), hop_quality: d.hop_quality },
    fields: { temp_c: d.temp_c, hum_pct: d.hum_pct, bat_mv: d.bat_mv, bat_pct: d.bat_pct,
              rssi: d.rssi, snr: d.snr, hops: d.hops, lat: d.lat, lon: d.lon, seq: d.seq },
    timestamp: new Date(d.ts * 1000)
  }
]
```

---

## 11. InfluxDB v2

### 11.1 Concepts เปรียบเทียบ v1 → v2

| v1 | v2 |
|---|---|
| database | bucket |
| username/password | token |
| n/a | organization |
| InfluxQL | Flux (+ InfluxQL compat mode) |
| /write endpoint | /api/v2/write |

### 11.2 Initial Setup (ทำครั้งแรกผ่าน Docker env)

```
Organization : lora-iot
Bucket       : lora_sensor   retention 30d
Admin Token  : (generate ใน UI หรือ env)
```

### 11.3 Schema

**Measurement: `lora_sensor`**
```
Tags (indexed):
  node_id      "32"
  gw_id        "1"
  hop_quality  "fair"

Fields:
  temp_c    float
  hum_pct   float
  bat_mv    int
  bat_pct   int
  rssi      int
  snr       float
  hops      int
  lat       float
  lon       float
  seq       int
```

**Measurement: `lora_delivery`**
```
Tags:   node_id, gw_id
Fields: seq (int)
```

**Measurement: `lora_gateway`**
```
Tags:   gw_id
Fields: uptime_s (int), wifi_rssi (int)
```

### 11.4 Retention + Downsampling

```
Bucket: lora_sensor     → 30d  (raw, full resolution)
Bucket: lora_sensor_1h  → 365d (hourly average — Flux task)

Flux task (สร้างใน InfluxDB UI):
  option task = { name: "downsample_1h", every: 1h }
  from(bucket: "lora_sensor")
    |> range(start: -1h)
    |> filter(fn: (r) => r._measurement == "lora_sensor")
    |> aggregateWindow(every: 1h, fn: mean, createEmpty: false)
    |> to(bucket: "lora_sensor_1h", org: "lora-iot")
```

### 11.5 Flux Query ตัวอย่าง (สำหรับ Grafana)

**Temperature 24h:**
```flux
from(bucket: "lora_sensor")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "lora_sensor"
         and r._field == "temp_c"
         and r.node_id == "${node_id}")
  |> aggregateWindow(every: ${interval}, fn: mean)
```

**Battery ทุก node (latest value):**
```flux
from(bucket: "lora_sensor")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "lora_sensor"
         and r._field == "bat_pct")
  |> last()
  |> group(columns: ["node_id"])
```

**Node สุดท้ายที่เห็น + พิกัด (สำหรับ Geomap):**
```flux
from(bucket: "lora_sensor")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "lora_sensor"
         and (r._field == "lat" or r._field == "lon" or r._field == "rssi"))
  |> last()
  |> pivot(rowKey: ["node_id"], columnKey: ["_field"], valueColumn: "_value")
```

---

## 12. Grafana (Self-hosted)

### 12.1 Data Source

```
Type:           InfluxDB
Query Language: Flux
URL:            http://influxdb:8086    (Docker internal network)
Organization:   lora-iot
Token:          <admin token>
Default Bucket: lora_sensor
```

### 12.2 Dashboard Panels

```
Row 1: Overview
  [Stat] Nodes online (last 10m)     [Stat] Packets today
  [Stat] Gateways online             [Stat] Failed deliveries today

Row 2: Per-Node (repeat by $node_id variable)
  [Time series] Temperature & Humidity (24h)
  [Gauge]       Battery %  (color: green>50, yellow>25, red<25)
  [Stat]        Last RSSI / Hops / Last seen

Row 3: Network Quality
  [Time series] RSSI ทุก node (overlay, 24h)
  [Bar chart]   Hop distribution per node
  [Table]       node_id | temp | hum | bat% | rssi | hops | last_seen | hop_quality

Row 4: Map
  [Geomap] Node positions
    lat/lon จาก InfluxDB (latest per node)
    color by: bat_pct (green/yellow/red)
    tooltip: node_id, temp, bat%, rssi

Row 5: Gateway
  [Time series] Gateway WiFi RSSI (per gw_id)
  [Time series] Gateway uptime
```

### 12.3 Variables

```
$node_id    = query tag values "node_id" from lora_sensor
$gw_id      = query tag values "gw_id"   from lora_sensor
$interval   = custom: 1m, 5m, 15m, 1h
```

### 12.4 Alert Rules

```
battery_low:     bat_pct < 25  ติดต่อกัน 5 นาที
node_offline:    ไม่มี data ใน 10 นาที  (no data → alert)
rssi_poor:       rssi < -110  ติดต่อกัน 3 ครั้ง
gateway_offline: ไม่มี lora_gateway record ใน 3 นาที
```

---

## 13. Docker Compose

### 13.1 `docker-compose.yml`

```yaml
version: "3.8"

services:

  influxdb:
    image: influxdb:2.7
    container_name: influxdb
    restart: unless-stopped
    ports:
      - "8086:8086"
    volumes:
      - ./data/influxdb:/var/lib/influxdb2
      - ./data/influxdb-config:/etc/influxdb2
    environment:
      DOCKER_INFLUXDB_INIT_MODE:         setup
      DOCKER_INFLUXDB_INIT_USERNAME:     admin
      DOCKER_INFLUXDB_INIT_PASSWORD:     changeme_influx
      DOCKER_INFLUXDB_INIT_ORG:          lora-iot
      DOCKER_INFLUXDB_INIT_BUCKET:       lora_sensor
      DOCKER_INFLUXDB_INIT_RETENTION:    30d
      DOCKER_INFLUXDB_INIT_ADMIN_TOKEN:  changeme_token_long_random_string

  grafana:
    image: grafana/grafana:latest
    container_name: grafana
    restart: unless-stopped
    ports:
      - "3000:3000"
    volumes:
      - ./data/grafana:/var/lib/grafana
    environment:
      GF_SECURITY_ADMIN_PASSWORD: changeme_grafana
      GF_USERS_ALLOW_SIGN_UP:     "false"
    depends_on:
      - influxdb

  nodered:
    image: nodered/node-red:latest
    container_name: nodered
    restart: unless-stopped
    ports:
      - "1880:1880"
    volumes:
      - ./data/nodered:/data
    depends_on:
      - influxdb
    environment:
      TZ: Asia/Bangkok

networks:
  default:
    name: lora-iot
```

### 13.2 First Start

```bash
mkdir -p data/influxdb data/influxdb-config data/grafana data/nodered
docker compose up -d

# ตรวจสอบ
docker compose ps
docker compose logs influxdb   # ดู setup complete
```

### 13.3 ติดตั้ง Node-RED Nodes

```bash
# เข้า container
docker exec -it nodered bash

# ติดตั้ง nodes ที่ต้องการ
cd /data
npm install node-red-contrib-influxdb   # InfluxDB v2
npm install node-red-contrib-mqtt       # (มาพร้อม Node-RED แล้ว)

# restart
docker restart nodered
```

### 13.4 Port Summary

```
localhost:8086   InfluxDB UI + API
localhost:3000   Grafana
localhost:1880   Node-RED
```

### 13.5 Backup

```bash
# InfluxDB backup
docker exec influxdb influx backup /var/lib/influxdb2/backup \
  --token changeme_token_long_random_string
docker cp influxdb:/var/lib/influxdb2/backup ./backup/influxdb

# Grafana dashboards
docker cp grafana:/var/lib/grafana/grafana.db ./backup/
```

---

## 14. File/Folder Structure

```
project-root/
│
├── shared/                         ← copy เข้าทุก platform
│   ├── lora_protocol.h             ← address, FLAGS, constants, macros
│   ├── lora_crypto.h               ← xorEncrypt, xorDecrypt, crc16
│   └── lora_payload.h              ← build/parse payload helpers
│
├── stm32_sensor/                   ← STM32, Arduino IDE
│   ├── stm32_sensor.ino
│   ├── radio_rfm95.h               ← HW SPI RFM95 driver
│   └── config.h                    ← MY_NODE_ID, MY_LAT, MY_LON
│
├── nrf52840_relay/                 ← nRF52840, PlatformIO
│   ├── src/main.cpp
│   ├── include/
│   │   ├── lora_config.h           ← #define USE_SX1262/RFM95, pins
│   │   └── (copy shared/*.h)
│   └── platformio.ini
│
├── esp32_gateway/                  ← ESP32, PlatformIO
│   ├── src/main.cpp
│   ├── include/
│   │   ├── radio_config.h          ← #define USE_SX1262/RFM95/SX1276, pins
│   │   ├── gw_config.h             ← THIS_GW_ID, ACK_BASE_DELAY
│   │   └── (copy shared/*.h)
│   └── platformio.ini
│
└── docker/                         ← Server stack
    ├── docker-compose.yml
    └── data/                       ← mount points (gitignore ไว้)
        ├── influxdb/
        ├── grafana/
        └── nodered/
```

### platformio.ini — ESP32 Gateway

```ini
[env:esp32_gateway]
platform  = espressif32
board     = esp32dev
framework = arduino

lib_deps =
  jgromes/RadioLib @ ^6.6.0
  tzapu/WiFiManager @ ^2.0.17
  knolleary/PubSubClient @ ^2.8
  bblanchon/ArduinoJson @ ^7.1

build_flags =
  -DTHIS_GW_ID=1
  -DUSE_RFM95
```

---

## 15. LoRa Parameters — ต้องตรงกันทุก Node

| Parameter | Value | หมายเหตุ |
|---|---|---|
| Frequency | 923.000 MHz | Thailand ISM band |
| SF | 7 | ~2–3 km ในเมือง |
| Bandwidth | 125 kHz | |
| Coding Rate | 4/5 (CR=5) | |
| Sync Word | 0x12 | private, compat SX1262↔SX1276 |
| TX Power | 12 dBm | |
| Preamble | 8 symbols | |
| CRC | enabled | PHY CRC + Protocol CRC16 |
| Airtime ~50B | ~70 ms | |
| Min TX interval | 30s | duty cycle 1% safe |

---

## 16. ลำดับการพัฒนา

```
Phase 1 — ESP32 Gateway                     ← ทดสอบกับ nRF52840 ปัจจุบันได้เลย
  RadioLib init → รับ LoRa จาก nRF52840
  decrypt + parse → publish MQTT (HiveMQ)
  ส่ง ACK กลับ
  Node-RED: subscribe → write InfluxDB v2
  Grafana: dashboard เบื้องต้น (time series + table)

Phase 2 — Multi-Gateway
  Gateway #2 (THIS_GW_ID=2)
  ทดสอบ MQTT dedup ที่ Node-RED
  ทดสอบ ACK cancel (GW1 ก่อน, GW2 cancel)
  WiFiManager ใส่เข้าไป

Phase 3 — STM32 Sensor Node
  TX packet (hop=0, ack_req=1, LA/LO hardcode)
  WAIT_ACK + retry state machine
  ทดสอบ STM32 → ESP32 โดยตรง (ไม่มี relay ก่อน)
  Grafana Geomap แสดง node position

Phase 4 — nRF52840 Relay ปรับ
  เพิ่ม IS_ACK relay (ย้อนกลับ)
  RSSI-based delay แทน random delay
  cancel queue entry ถ้าได้ยิน relay ก่อน
  ทดสอบ: STM32 → Relay → GW → MQTT → ACK ย้อน STM32

Phase 5 — Tune & Harden
  ทดสอบ 2–3 hops จริง
  ทดสอบ GW offline failover
  InfluxDB downsampling task
  Grafana alert rules + notification
  docker compose on production server
```
