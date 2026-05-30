# Matter Protocol — ทำไมถึงใหญ่, เป้าหมายที่แท้จริง, และอนาคต

วันที่: 2026-05-13

---

## 1. ปัญหาที่ Matter ถูกสร้างมาเพื่อแก้

### สภาพ Smart Home ก่อน Matter (2015–2021)

```
Apple    → HomeKit    (WiFi/BLE)    ─┐
Google   → Works with Google (WiFi) ─┤  ไม่คุยกันเลย
Amazon   → Alexa      (WiFi/Zigbee) ─┤  ต่างคนต่างทำ
Samsung  → SmartThings (Zigbee)     ─┘

ผู้ใช้ซื้อหลอดไฟ Philips Hue (Zigbee)
→ ทำงานกับ Alexa ได้ ✓
→ ทำงานกับ Google Home ได้ ✓
→ ทำงานกับ Apple Home ไม่ได้ ✗  (ต้องซื้อ Hub เพิ่ม)
→ ทำงานกับ SmartThings ไม่ได้ ✗

ผู้ใช้ต้องถามก่อนซื้อทุกครั้ง: "Works with Alexa? Works with Google?"
```

### ปัญหาที่ใหญ่กว่า: Cloud Dependency

```
2019: Nest (Google) ปิด Revolv Hub → อุปกรณ์ $300 กลายเป็นขยะทันที
2020: Wink Hub ล้มละลาย → บ้านอัจฉริยะหลายหมื่นหลังใช้ไม่ได้
2021: SmartThings ปิด Groovy → automation ทั้งหมดพัง
2023: Amazon Echo รุ่นเก่า → ถูกปิดฟีเจอร์บางอย่างผ่าน update

ปัญหา: อุปกรณ์ใช้งานได้ก็ต่อเมื่อ cloud ของบริษัทนั้นยังอยู่
         internet หลุด 1 วินาที → เปิดไฟในบ้านตัวเองไม่ได้
```

### Security ที่น่ากลัว

```
2016: Mirai Botnet
  → กล้อง CCTV และ router IoT 600,000+ ตัวถูก hack
  → ใช้โจมตี DDoS ขนาดใหญ่ที่สุดในประวัติศาสตร์ (1.2 Tbps)
  → เหตุ: อุปกรณ์ใช้ username/password default "admin:admin"
           ไม่มี encryption, ไม่มี device authentication

ก่อน Matter: อุปกรณ์ IoT ส่วนใหญ่
  ✗ ไม่มี end-to-end encryption
  ✗ ไม่มี device attestation (ใครก็แอบ join network ได้)
  ✗ firmware update ไม่มีการ verify signature
  ✗ communication ผ่าน cloud → ข้อมูลส่วนตัวรั่วออกง่าย
```

---

## 2. เป้าหมาย 4 ข้อที่ Matter ถูกออกแบบมา

### 2.1 Interoperability — ซื้อครั้งเดียว ใช้ได้ทุก ecosystem

```
Matter device 1 ตัว:
  ✅ Apple Home
  ✅ Google Home
  ✅ Amazon Alexa
  ✅ Samsung SmartThings
  ✅ Home Assistant
  ✅ ทุก app ที่รองรับ Matter

ไม่ต้องถามว่า "works with" อีกต่อไป
```

### 2.2 Local Control — ทำงานได้โดยไม่ต้อง internet

```
Before Matter:
  กด switch → cloud (California) → กลับมา → หลอดไฟติด (~300ms)
  internet หลุด → ทำอะไรไม่ได้

Matter:
  กด switch → local network → หลอดไฟติด (~20ms)
  internet หลุด → บ้านยังทำงานได้ปกติ

ทำได้เพราะ: Matter ใช้ mDNS discovery + local IPv6
             devices คุยกันตรงๆ ใน LAN ไม่ผ่าน cloud
```

### 2.3 Security by Default — ไม่ optional

```
ทุก Matter device ต้องมี:
  Device Attestation Certificate (DAC)
    → chip ผลิตออกมาพร้อม certificate จาก manufacturer
    → ไม่มี certificate → join network ไม่ได้

  PASE (Passcode Authenticated Session Establishment)
    → commissioning ใช้ pairing code (เหมือน PIN Bluetooth)
    → ป้องกัน MITM attack

  CASE (Certificate Authenticated Session Establishment)
    → การสื่อสารหลัง commission ใช้ mutual TLS
    → ทั้งสองฝั่ง verify กัน

  End-to-end encryption (AES-128-CCM)
    → ทุก message encrypt

  Signed firmware updates
    → อัปเดต firmware ต้องมี signature → ไม่มีใครฝัง malware ได้
```

### 2.4 Reliability — อุปกรณ์ต้องอยู่ได้นาน

```
Matter spec กำหนดว่า:
  - Manufacturer ต้อง support OTA update ≥ 3 ปี
  - Local API ต้องทำงานได้โดยไม่พึ่ง cloud
  - ถ้าบริษัทล้ม → device ยังใช้งานได้ผ่าน local control

Apple เพิ่ม: ถ้า manufacturer ไม่ support แล้ว
              Apple Home ยังคง control device ผ่าน local ได้
```

---

## 3. ทำไม Matter ถึงกิน Memory มาก

### Matter ไม่ใช่ Protocol เบาๆ — มันคือ "Full Computer Network Stack"

```
Zigbee (2004):         ~128 KB Flash,  ~32 KB RAM
Z-Wave (2001):         ~256 KB Flash,  ~64 KB RAM
Matter (2022):         ~600 KB Flash, ~150 KB RAM

ต่างกัน 4-5 เท่า — ทำไม?
```

### Layer by Layer ของ Matter Stack

```
Layer                    Flash    RAM     เหตุผล
─────────────────────────────────────────────────────────
Mbed TLS (crypto)       ~120 KB  ~30 KB  ECDSA, AES-128, SHA-256,
                                          HKDF, TLS 1.3
                                          → Security mandate ทำให้จำเป็น

OpenThread              ~200 KB  ~40 KB  Full IPv6 mesh
                                          6LoWPAN, routing, mesh mgmt
                                          → ต้องการ IPv6 เพื่อ local control

Matter Core             ~150 KB  ~40 KB  Device models, attribute DB,
                                          Interaction Model, Commissioning
                                          → Interoperability ต้องการ common model

mDNS / DNS-SD           ~40 KB   ~15 KB  Device discovery ใน local network
                                          → Local control ต้องการ

Device Attestation      ~30 KB   ~10 KB  Certificate chain, DAC, PAI, PAA
                                          → Security mandate

App + Drivers           ~60 KB   ~15 KB  application logic ของ device เอง
─────────────────────────────────────────────────────────
รวม                    ~600 KB  ~150 KB
```

### เปรียบเทียบกับ Protocol อื่น

```
MQTT (our system):       ~10 KB Flash,   ~5 KB RAM   (client only)
LoRa custom protocol:    ~15 KB Flash,   ~3 KB RAM
Zigbee:                  ~128 KB Flash,  ~32 KB RAM
Matter:                  ~600 KB Flash, ~150 KB RAM

Matter ใหญ่กว่า LoRa custom ถึง 40× เพราะ:
  LoRa: เราเลือก security เอง (XOR+CRC เบาๆ)
  Matter: security เป็น non-negotiable ต้องครบทุกชั้น
```

### Security คือต้นทุนที่แพงที่สุด

```
AES-128 encryption: ง่าย, เล็ก (~5 KB)
แต่ Matter ต้องการมากกว่านั้น:

Device Attestation (PKI):
  nRF52840 มาจาก factory พร้อม certificate chain:
    PAA (Product Attestation Authority)  → CSA root CA
    PAI (Product Attestation Intermediate) → Nordic CA
    DAC (Device Attestation Certificate) → ใน chip ทุกตัว
    
  ตอน commission:
    phone verify ว่า DAC → PAI → PAA chain valid
    → ถ้าไม่ valid → device ไม่ได้รับอนุญาตเข้า network
    → ป้องกัน counterfeit devices

  โค้ดสำหรับ certificate verification: ~30 KB
  
TLS 1.3 handshake:
  ECDH key exchange (P-256 curve): ~20 KB code
  HKDF key derivation: ~5 KB
  แต่ละ session ใช้ RAM ~2 KB

→ ถ้าตัด security ออก Matter ก็ไม่ใช่ Matter อีกต่อไป
```

---

## 4. Target Device ที่ Matter ออกแบบมาให้ — ไม่ใช่ Sensor Node

```
Matter ออกแบบสำหรับ:               ไม่ใช่:
  Smart bulb (WiFi chip ใน lamp)     Bare metal sensor node
  Smart lock (แบตก้อนใหญ่)          Ultra-low-power LoRa node
  Thermostat (powered)              Industrial sensor (Modbus)
  Smart plug (ต่อไฟตลอด)            Long-range outdoor node
  Security camera (powered)         Battery < 1,000 mAh

อุปกรณ์เหล่านี้:
  ✓ มีไฟ AC หรือแบตก้อนใหญ่ → ไม่กังวล power
  ✓ Flash 2-8 MB, RAM 256-512 KB → พอสำหรับ Matter
  ✓ อยู่ในบ้าน ระยะ < 10 m → Thread range พอ
  ✓ ผู้ใช้ทั่วไป → ต้องการ interop กับ Apple/Google/Amazon
```

---

## 5. Matter Thread Sleepy End Device — พยายามแก้ปัญหา Power

```
CSA รู้ว่า battery device มีปัญหา → สร้าง Sleepy End Device (SED)

ทำงานอย่างไร:
  Node sleep ส่วนใหญ่
  ตื่นทุก poll interval → ถาม parent node ว่ามี message ไหม
  ถ้ามี → ตื่นรับ-ตอบ → กลับ sleep

Power consumption:
  SED ทั่วไป:       ~5-15 µA average (poll ทุก 1-10 วินาที)
  LoRa node (ours): ~2-5 µA average (sleep 60 วินาที)

ปัญหา:
  SED ยังต้องการ parent node (router) ที่ active ตลอด
  Parent คือ Thread router อีกตัวที่ไม่ sleep
  ต้องมี infrastructure หนาแน่น (router ทุก 30m)
  
LoRa node ของเรา:
  ไม่ต้องการ infrastructure กลาง
  STM32 sleep → wakeup → TX → sleep  จบ
  ง่ายกว่า, range ไกลกว่า มาก
```

---

## 6. อนาคตของ Matter

### Roadmap ที่ประกาศแล้ว

```
Matter 1.0 (Oct 2022) — Foundation
  Lights, Switches, Plugs, Locks, Thermostats, Blinds, Bridges

Matter 1.1 (May 2023)
  Bug fixes, Better commissioning, Enhanced security

Matter 1.2 (Oct 2023) — Expanding
  Robot Vacuums, Refrigerators, Air Purifiers
  Smoke & CO Alarms, Air Quality Sensors ← ใกล้ระบบเรามากขึ้น

Matter 1.3 (May 2024) — Energy & Water
  Energy Management (solar, batteries, EV charging)
  Water Heaters, Water Valves ← ใกล้ระบบเรา
  Electric Vehicle Supply Equipment (EVSE)

Matter 1.4 (Nov 2024)
  Enhanced device types, Better battery reporting
  Improved Bridge capabilities

Matter 2.0+ (2025–2026, คาดการณ์)
  Environmental sensors (soil, outdoor)? ← ยังไม่มี official
  Building automation bridge (BACnet, KNX, Modbus)?
  Matter Compact (reduced footprint)? ← ถกเถียงอยู่
  Cellular / satellite support?
```

### Tension ที่ยังแก้ไม่ได้

```
ปัญหา 1: Footprint vs Security
  ยิ่ง secure ยิ่งใหญ่
  CSA ยังไม่มีแผน "Matter Lite" อย่างเป็นทางการ
  เพราะ: ถ้าตัด security → ผิดวัตถุประสงค์ที่ตั้งไว้

ปัญหา 2: Certification ราคาแพง
  ต้องส่ง device ไป test lab ($5,000-$20,000 per product)
  ยากสำหรับ startup หรือ DIY maker
  → ทำให้ ecosystem เติบโตช้ากว่าที่ควร

ปัญหา 3: Thread infrastructure
  ต้องมี Border Router (HomePod, Nest Hub) ก่อนถึงใช้ Thread ได้
  คนที่ไม่มี Apple/Google hub → ใช้ Matter over WiFi แทน
  WiFi version → ต้องการ power มากกว่า
```

### ทิศทางที่น่าสนใจ: Matter Bridge ขยายตัว

```
Matter Bridge (เราวางแผนทำบน ESP32) กำลังเป็น standard approach:

Smart Home Hub ผู้ผลิตต่างๆ กำลังทำ Bridge ไปยัง:
  Zigbee → Matter Bridge (Philips Hue, IKEA)
  Z-Wave → Matter Bridge (Aeotec)
  KNX    → Matter Bridge (กำลังพัฒนา)
  BACnet → Matter Bridge (กำลังพัฒนา)
  Modbus → Matter Bridge (ยังไม่มี official แต่คนกำลังทำ)

→ ระบบของเรา (LoRa → Matter Bridge บน ESP32) เดินถูกทางครับ
→ เป็น pattern ที่ industry กำลังทำกันอยู่
```

---

## 7. เปรียบเทียบ Philosophy — Matter vs LoRa vs Modbus

```
                Matter          LoRa            Modbus
─────────────────────────────────────────────────────────────
เป้าหมาย       Consumer IoT    LPWAN IoT       Industrial
               Smart Home      Outdoor/Field   Factory/Field

Priority       Interoperability Range           Determinism
               Security        Low Power       Reliability
               UX              Cost            Simplicity

Range          30–100m/hop     2–15 km         1200m (RS-485)
               (Thread mesh)

Power          Medium–High     Ultra-low       N/A (powered)
               (WiFi)          (µA sleep)
               Low (Thread SED)

Security       ★★★★★          ★★☆☆☆          ★☆☆☆☆
               (PKI, TLS)     (custom, XOR)  (no encryption)

Footprint      ~600 KB Flash   ~15 KB          ~5 KB (client)

Ecosystem      Consumer devices Field sensors   Industrial sensors

Interop        Apple/Google/   Custom gateway  PLC, SCADA
               Amazon native
```

---

## 8. ทำไมระบบของเราถึงเลือก LoRa ถูกต้อง

```
Use case เรา:
  ✓ Outdoor / Field deployment (km range)
  ✓ Battery powered, solar, หรือ power-limited
  ✓ Environmental monitoring (ไม่ใช่ consumer smart home)
  ✓ Industrial-grade sensor (Modbus, 4-20mA)
  ✓ Self-hosted backend (InfluxDB, Grafana)
  ✓ ต้องการ customize protocol ได้เต็มที่
  ✓ Cost-sensitive (sensor node ราคาต่ำ)

Matter เหมาะถ้า:
  - อุปกรณ์ในบ้าน (indoor, < 10m)
  - ต้องการ Apple/Google/Amazon integration out-of-the-box
  - User เป็น consumer ทั่วไป (ไม่ใช่ engineer)
  - มีไฟฟ้า AC หรือแบตขนาดใหญ่

→ ระบบเราอยู่คนละ domain กับ Matter
→ ถูกต้องที่ใช้ LoRa + MQTT + InfluxDB + Grafana

Matter จะเข้ามาเสริมระบบเราได้ตรงจุดเดียว:
  ESP32 Gateway → Matter Bridge → Apple Home / Google Home
  ให้ consumer user เห็นข้อมูล sensor บน phone ง่ายขึ้น
  โดยไม่ต้องเปิด Grafana
```

---

## 9. สรุปภาพรวม

```
Matter ใหญ่เพราะ:
  Security + Interoperability + Local control
  ไม่ใช่ design flaw — เป็น intentional trade-off
  ของใหญ่ที่ตั้งใจให้ใหญ่เพื่อแก้ปัญหาที่ใหญ่

Matter ถูกสร้างมาเพื่อ:
  แก้ fragmentation ของ smart home ecosystem
  ทำให้ consumer ไม่ต้องถามว่า "works with X?"
  ทำให้ local control เป็นค่า default ไม่ใช่ optional
  ทำให้ security เป็น non-negotiable

อนาคต Matter:
  ขยาย device types → Energy, Water, Building automation
  Bridge pattern กำลัง dominant
  Footprint ลดยาก (security cost คงที่)
  
ระบบ LoRa Mesh ของเรา:
  อยู่คนละ domain กับ Matter โดยธรรมชาติ
  เชื่อมกัน "ที่ gateway" ผ่าน Matter Bridge บน ESP32 เท่านั้น
  ไม่จำเป็นต้อง rewrite ใดๆ บน nRF52840 หรือ STM32
```
