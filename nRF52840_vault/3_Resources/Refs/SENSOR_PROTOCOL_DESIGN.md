# Sensor & Protocol Design — Discussion

วันที่: 2026-05-13  
บริบท: ต่อยอดจาก LoRa Mesh (STM32 → nRF52840 relay → ESP32 GW → MQTT → InfluxDB/Grafana)

---

## 1. ภาพรวม: Protocol Stack ที่เกี่ยวข้อง

```
┌─────────────────────────────────────────────────────────────────┐
│  SENSOR / FIELD LEVEL                                           │
│                                                                 │
│  [Sensor A]──I2C──┐                                            │
│  [Sensor B]──SPI──┤                                            │
│  [Sensor C]──RS485 (Modbus RTU)──┤                             │
│  [Sensor D]──4-20mA──ADC──────────┤                            │
│  [Sensor E]──SDI-12───────────────┤                            │
│  [Sensor F]──1-Wire───────────────┘                            │
│                    ▼                                            │
│            [STM32 Node]  ← ตัวรวบรวมข้อมูล (Aggregator)        │
└────────────────────┬────────────────────────────────────────────┘
                     │ LoRa (protocol ที่เราทำ)
                     ▼
           [...relay → ESP32 GW → MQTT → InfluxDB/Grafana...]
```

---

## 2. Protocol ระดับ Sensor ← → STM32

### 2.1 I2C

```
ข้อดี:
  - เดินสาย 2 เส้น (SDA, SCL) ต่อได้หลาย device บน bus เดียว
  - ใช้ address แยก device (0x00–0x7F)
  - library ครบ, sensor ราคาถูก
  - STM32 มี I2C hardware หลายตัว

ข้อจำกัด:
  - ระยะสั้น ≤ 1 เมตร (capacitance ของสาย)
  - ไม่ noise-immune ในสภาพแวดล้อม EMI สูง
  - ไม่มี differential signaling

เหมาะกับ:
  ✓ Sensor บนบอร์ดเดียวกัน (BME280, OLED, RTC)
  ✓ Module ที่อยู่ใกล้กัน
  ✗ Field sensor ที่อยู่ไกลหลายเมตร

ตัวอย่าง sensor:
  BME280      อุณหภูมิ/ความชื้น/ความดัน
  SHT31       อุณหภูมิ/ความชื้น (แม่นยำกว่า)
  BH1750      แสง (lux)
  ADS1115     ADC 4ch 16-bit (แปลง analog sensor)
  INA226      กระแส/แรงดัน (power monitoring)
  VL53L1X     ระยะ laser ToF (< 4m)
```

### 2.2 SPI

```
ข้อดี:
  - เร็วกว่า I2C มาก (10–50 MHz)
  - Full-duplex
  - ไม่มี address conflict

ข้อจำกัด:
  - ใช้ pin มาก (MISO, MOSI, SCK, CS ต่อ device)
  - ระยะสั้น (เหมือน I2C)

เหมาะกับ:
  ✓ Radio module (RFM95) — ใช้อยู่แล้ว
  ✓ SD card, display
  ✗ Field sensor ทั่วไป (ไม่ค่อยมี SPI sensor สำหรับ field)
```

### 2.3 UART / RS232

```
ข้อดี:
  - Simple point-to-point
  - STM32 มี UART หลายตัว
  - Sensor หลายชนิดใช้ (GPS, CO2, particle)

ข้อจำกัด:
  - RS232: ระยะ ~15m, ไม่ทน noise
  - Point-to-point (1:1 เท่านั้น)
  - ต้องใช้ UART แยกต่อ device

เหมาะกับ:
  ✓ GPS module (NMEA)
  ✓ CO2 sensor (MH-Z19)
  ✓ Particle sensor (PMS5003)
  ✗ Multi-sensor บน bus เดียว
```

### 2.4 1-Wire

```
ข้อดี:
  - สาย 1 เส้น + GND
  - ต่อหลาย sensor บน bus เดียว (address 64-bit unique)
  - ต่อระยะได้ ~100m (ถ้า pull-up ถูกต้อง)

ข้อจำกัด:
  - ช้า (~15 kbps)
  - Sensor มีจำกัด (ส่วนใหญ่เป็น temperature)
  - Timing sensitive

เหมาะกับ:
  ✓ DS18B20 — temperature sensor ราคาถูก ทนทาน กันน้ำ
  ✓ ต่อหลาย DS18B20 บน bus เดียว (เช่น วัดอุณหภูมิหลายจุด)
  ✗ Sensor ชนิดอื่น (ecosystem แคบ)

ตัวอย่างใช้งาน:
  วัดอุณหภูมิน้ำ/ดิน 10 จุด บน bus เดียว → STM32 อ่านทีละตัว
```

### 2.5 Analog (0–5V, 0–10V, 4–20mA) ← สำคัญมาก

```
ข้อดี:
  - Sensor ราคาถูกที่สุด (industrial grade)
  - Ecosystem ใหญ่มาก — sensor เกือบทุกชนิดมีแบบ analog
  - ง่ายต่อการ debug (วัดด้วย multimeter ได้)

4-20mA (Current Loop) — แนะนำเป็นพิเศษ:
  - ทน noise ดีมาก (current ไม่ได้รับผลจาก voltage drop)
  - ระยะ 300–1000 เมตร บนสาย 2 เส้น
  - ถ้าสายขาด → 0 mA → รู้ทันทีว่า fault (fail-safe)
  - STM32: ADC อ่าน voltage บน shunt resistor (250Ω → 1–5V = 4–20mA)

0–5V / 0–10V:
  - ง่ายกว่า 4-20mA แต่ noise-sensitive กว่า
  - ระยะ < 50m

เหมาะกับ:
  ✓ Pressure transducer (ระดับน้ำ, ความดัน) — ส่วนใหญ่เป็น 4-20mA
  ✓ pH sensor (ต้องการ signal conditioning)
  ✓ Soil moisture (resistive/capacitive แบบ analog)
  ✓ Flow sensor, turbidity, dissolved oxygen
  ✓ Sensor industrial grade ทุกชนิด

ข้อจำกัด:
  - ต้องการ ADC resolution ดี (12-bit STM32 = 4096 steps = ละเอียดพอ)
  - pH และบางตัวต้องการ isolation / signal conditioner
```

---

## 3. Protocol ระดับ Industrial ← → STM32

### 3.1 RS-485 (Physical Layer)

```
ก่อนอื่น: RS-485 เป็นแค่ physical layer (สาย + electrical spec)
           protocol ที่วิ่งบน RS-485 คือ Modbus RTU, Profibus, DMX, etc.

ข้อดี:
  - Differential signaling → ทน noise, EMI ได้ดีมาก
  - ระยะสูงสุด 1200 เมตร @ 100 kbps
  - Multi-drop: node 32 ตัว (มาตรฐาน) ถึง 256 ตัว (driver พิเศษ)
  - สาย 2 เส้น (A, B) + GND
  - ชิป: MAX485 / MAX3485 ราคา 10–30 บาท

STM32 → RS-485:
  UART TX/RX → MAX485 → สาย A/B → sensor
  + DE/RE pin control (half-duplex)
```

### 3.2 Modbus RTU — แนะนำเป็นอันดับ 1

```
คืออะไร:
  Protocol ที่ทำงานบน RS-485 (หรือ RS-232)
  พัฒนาโดย Modicon (1979) → standard de facto ของอุตสาหกรรม

ทำงานอย่างไร:
  Master (STM32) → ส่ง request → Slave (sensor) → ตอบ response
  ทุกอย่างผ่านสาย RS-485 เส้นเดียวกัน (bus)

ทำไมถึงแนะนำ:
  ✓ Sensor ecosystem ใหญ่ที่สุด — เกือบทุก industrial sensor มี Modbus
     - pH meter, conductivity, dissolved oxygen
     - Pressure, level, flow
     - Soil NPK, moisture, temperature
     - Weather station, pyranometer (solar)
     - Power meter, energy meter
     - CO2, VOC, dust
  ✓ Open standard — ไม่ต้องซื้อ license
  ✓ Simple มาก — เข้าใจได้ใน 1 วัน
  ✓ Library มีพร้อมทั้ง Arduino, STM32
  ✓ Sensor ราคาเริ่มต้น ~500 บาท (Chinese grade)
     จนถึง ~50,000 บาท (certified industrial)
  ✓ ต่อ sensor ได้ถึง 247 ตัวบน bus เดียว
  ✓ ระยะ 1200 เมตร

Frame Structure (RTU):
  [Address 1B][Function 1B][Data NB][CRC 2B]
  Function codes ที่ใช้บ่อย:
    0x03 = Read Holding Registers (อ่านค่า sensor)
    0x06 = Write Single Register  (config sensor)
    0x10 = Write Multiple Registers

ตัวอย่าง request อ่านค่า pH:
  [01][03][00 00][00 02][C4 0B]
  → slave 01, อ่าน 2 registers เริ่มจาก address 0x0000

STM32 implementation:
  UART2 → MAX485 → bus
  library: แนะนำเขียน request/response เองง่ายกว่า (Modbus RTU ไม่ซับซ้อน)
  หรือใช้: ArduinoRS485 + ArduinoModbus library
```

### 3.3 Modbus TCP

```
คืออะไร:
  Modbus RTU แต่วิ่งบน TCP/IP แทน RS-485
  ใช้ port 502

เหมาะกับ:
  ✓ ESP32 / Raspberry Pi / PC เป็น Master
  ✓ Sensor ที่มี Ethernet port
  ✗ STM32 ไม่มี Ethernet → ต้องใช้ module เพิ่ม (W5500)

ในระบบเรา: ไม่จำเป็นในตอนนี้ — RS-485 Modbus RTU เพียงพอ
```

### 3.4 CAN Bus

```
คืออะไร:
  Controller Area Network — พัฒนาโดย Bosch (1986) สำหรับรถยนต์
  ต่อมาใช้ใน industrial, aerospace, medical

ข้อดี:
  ✓ Robust มากที่สุดในกลุ่ม — Multi-master, collision detection
  ✓ Real-time, deterministic
  ✓ ทน EMI ดีมาก (differential)
  ✓ Error detection และ retry อัตโนมัติ (hardware level)
  ✓ STM32 มี CAN controller built-in (F103, F4xx)
  ✓ ระยะ: 40m @ 1Mbps, 500m @ 125kbps, 1000m @ 50kbps

ข้อจำกัด:
  ✗ Sensor ecosystem น้อยกว่า Modbus มาก
  ✗ ส่วนใหญ่ใช้ใน automotive / machine (ไม่ใช่ environmental sensor)
  ✗ Protocol ซับซ้อนกว่า (ต้องใช้ higher-level protocol: CANopen, J1939)
  ✗ ราคา transceiver สูงกว่า RS-485 เล็กน้อย (SN65HVD230 ~50 บาท)

เหมาะกับ:
  ✓ ระบบ machine monitoring (vibration, motor, hydraulic)
  ✓ Vehicle telemetry
  ✓ ระบบที่ต้องการ real-time และ safety-critical
  ✗ Environmental sensor ทั่วไป → ใช้ Modbus ง่ายกว่า

ข้อสรุป: CAN เหมาะถ้า STM32 node อยู่ใกล้กับเครื่องจักร
          แต่สำหรับ environmental monitoring → Modbus ดีกว่า
```

### 3.5 Profibus

```
คืออะไร:
  Process Field Bus — พัฒนาโดย Siemens (1987)
  Standard ยุโรป (EN 50170)

ข้อดี:
  ✓ Speed สูง (12 Mbps)
  ✓ Deterministic timing
  ✓ ใช้กว้างขวางใน factory automation (EU)

ข้อจำกัด:
  ✗ ซับซ้อนมาก — ต้องมี Profibus Master card (ราคาแพง)
  ✗ Sensor ราคาสูงกว่า Modbus มาก
  ✗ Ecosystem ผูกกับ Siemens / PLC เป็นหลัก
  ✗ STM32 implement ได้แต่ยากมาก
  ✗ ไม่มี open-source library ที่ดี

ข้อสรุป: ไม่แนะนำสำหรับระบบนี้ — ใช้ใน PLC/SCADA factory เท่านั้น
```

### 3.6 SDI-12 — น่าสนใจสำหรับ Environmental

```
คืออะไร:
  Serial Digital Interface at 1200 baud
  Standard สำหรับ environmental sensor โดยเฉพาะ (US Geological Survey)

ข้อดี:
  ✓ Single wire data + power (3.5–16V)
  ✓ Sensor ecosystem เฉพาะทาง environmental ดีมาก:
     - Soil moisture (METER/Decagon 5TM, 10HS)
     - Soil NPK, EC, pH
     - Water level, water quality
     - Weather station components
  ✓ ต่อ sensor ได้ 62 ตัวบน bus เดียว
  ✓ Low power (sensor sleep ระหว่างรอ)
  ✓ Certified สำหรับ scientific measurement

ข้อจำกัด:
  ✗ ช้า (1200 baud)
  ✗ Library บน STM32 มีน้อย (ต้องเขียนเอง หรือใช้ Arduino library ดัดแปลง)
  ✗ Sensor ราคาสูงกว่า Modbus (แต่แม่นยำกว่ามาก)

ตัวอย่าง sensor SDI-12:
  METER TEROS 12    ความชื้นดิน + อุณหภูมิ + EC  (~5,000 บาท)
  Apogee SQ-500     PAR / solar radiation
  Campbell CS451    Water level pressure

เหมาะกับ:
  ✓ งาน agriculture precision ที่ต้องการความแม่นยำสูง
  ✓ งานวิจัย/monitoring ระดับ scientific
  ✓ ถ้า budget ยอมได้และต้องการ certified data
```

### 3.7 HART (Highway Addressable Remote Transducer)

```
คืออะไร:
  Protocol ที่ฝังสัญญาณ digital บน loop 4-20mA
  อ่านค่า analog ได้ปกติ + อ่าน digital data เพิ่มเติมได้

ข้อดี:
  ✓ Backward compat กับระบบ 4-20mA เดิม
  ✓ Sensor information, diagnostics, calibration
  ✓ ใช้กว้างขวางใน oil & gas, chemical plant

ข้อจำกัด:
  ✗ ต้องการ HART modem chip (AD5700 ~200 บาท)
  ✗ ซับซ้อนกว่า Modbus
  ✗ Protocol licensing (ฟรีแต่ต้องขอ)

ข้อสรุป: ใช้เมื่อต้องการ interop กับระบบ 4-20mA เดิมที่มี HART
          ไม่จำเป็นสำหรับระบบใหม่
```

---

## 4. เปรียบเทียบรวม

| Protocol | Layer | ระยะ | Sensor Choices | ความซับซ้อน | ราคา Sensor | แนะนำสำหรับ |
|---|---|---|---|---|---|---|
| **I2C** | Physical | < 1m | ปานกลาง | ต่ำ | ถูก | Onboard sensor |
| **1-Wire** | Physical | ~100m | น้อย (DS18B20) | ต่ำ | ถูก | Multi-point temp |
| **4-20mA** | Analog | ~1000m | ใหญ่มาก | ต่ำ | ปานกลาง | Field sensor ทั่วไป |
| **RS-485** | Physical | 1200m | — | ต่ำ | — | Physical layer ของ Modbus |
| **Modbus RTU** | L2–L7 | 1200m | ใหญ่มาก ⭐ | ต่ำ–ปานกลาง | ถูก–ปานกลาง | **แนะนำหลัก** |
| **SDI-12** | Physical+L2 | 60m | ปานกลาง (enviro) | ปานกลาง | สูง | Precision agriculture |
| **CAN Bus** | Physical+L2 | 1000m | น้อย (industrial) | สูง | ปานกลาง | Machine monitoring |
| **Profibus** | L1–L7 | 1200m | ปานกลาง (factory) | สูงมาก | สูงมาก | PLC/SCADA เท่านั้น |
| **HART** | บน 4-20mA | 1000m | ปานกลาง | สูง | สูง | Retrofit ระบบเดิม |

---

## 5. สิ่งที่ควรตรวจวัดและ Protocol ที่เหมาะสม

### 5.1 Environmental / Agriculture

| Measurement | Sensor ตัวอย่าง | Protocol | ราคาประมาณ |
|---|---|---|---|
| **อุณหภูมิ/ความชื้น/ความดัน** | BME280 | I2C | 80 บาท |
| **อุณหภูมิ field (กันน้ำ)** | DS18B20 | 1-Wire | 50 บาท |
| **ความชื้นดิน (basic)** | Capacitive v1.2 | Analog | 30 บาท |
| **ความชื้นดิน (precision)** | METER TEROS 12 | SDI-12 | 5,000 บาท |
| **ความชื้น+EC+อุณหภูมิดิน** | Jxct Soil 3-in-1 | **Modbus RS485** | 800 บาท |
| **pH ดิน/น้ำ** | Jxct pH sensor | **Modbus RS485** | 1,500 บาท |
| **EC (Electrical Conductivity)** | Jxct EC sensor | **Modbus RS485** | 1,200 บาท |
| **ระดับน้ำ (pressure)** | Pressure transducer | **4-20mA** | 500–2,000 บาท |
| **ระดับน้ำ (ultrasonic)** | A02YYUW | UART | 300 บาท |
| **ระดับน้ำ (radar)** | Vega/Endress+Hauser | **Modbus RS485** | 5,000+ บาท |
| **N-P-K ดิน** | Jxct NPK sensor | **Modbus RS485** | 2,500 บาท |
| **ความขุ่นน้ำ (turbidity)** | Turbidity sensor | **Modbus RS485** | 2,000 บาท |
| **ออกซิเจนในน้ำ (DO)** | DO sensor | **Modbus RS485** | 3,000+ บาท |
| **แสง (PAR/lux)** | BH1750 | I2C | 50 บาท |
| **แสง (PAR scientific)** | Apogee SQ-500 | SDI-12 | 15,000 บาท |
| **CO2** | SCD41 | I2C | 800 บาท |
| **CO2 (NDIR field)** | Winsen MH-Z19 | UART | 400 บาท |
| **ฝน (rain gauge)** | Tipping bucket | Pulse/Interrupt | 500–2,000 บาท |
| **ลม (speed/direction)** | Anemometer | Pulse + Analog | 1,000–5,000 บาท |
| **ลม (ultrasonic)** | Ultrasonic anemometer | **Modbus RS485** | 5,000+ บาท |

### 5.2 Industrial / Machine Monitoring

| Measurement | Sensor ตัวอย่าง | Protocol | ราคาประมาณ |
|---|---|---|---|
| **การสั่นสะเทือน** | MPU6050 | I2C | 50 บาท |
| **การสั่นสะเทือน (industrial)** | MEMS vibration | **Modbus RS485** | 3,000+ บาท |
| **กระแส/แรงดัน** | PZEM-004T | UART/Modbus | 300 บาท |
| **กระแส (industrial)** | Eastron SDM630 | **Modbus RS485** | 3,000 บาท |
| **ความดัน gas/liquid** | Pressure transducer | **4-20mA** | 500–5,000 บาท |
| **อัตราการไหล** | Flow meter | Pulse / **Modbus** | 500–10,000 บาท |
| **gas detection (toxic)** | MQ series | Analog | 100 บาท |
| **gas detection (industrial)** | 4-20mA transmitter | **4-20mA** | 3,000+ บาท |

---

## 6. สถาปัตยกรรมที่แนะนำสำหรับระบบนี้

### 6.1 Multi-Protocol Aggregator บน STM32

```
[DS18B20 ×N]──── 1-Wire ────────────────┐
[BME280]────────── I2C ──────────────────┤
[ADS1115]────────── I2C ─────────────────┤─► [STM32 Node]──► LoRa
[4-20mA sensor]── ADC (via ADS1115) ────┤
[Modbus sensor ×N]─ RS485/UART2 ─────────┤
[SDI-12 sensor]─── UART3 (bit-bang) ────┘
```

**STM32 ทำหน้าที่ Modbus Master:**
- Poll แต่ละ slave ตามลำดับ (Round-robin)
- รวมค่าทุกตัวใน payload เดียว
- ส่ง LoRa ทุก 60 วินาที

### 6.2 Payload ขยาย (รองรับ multi-sensor)

```
N:<id>|S:<seq>|B:<bat_mv>|LA:<lat>|LO:<lon>|<sensor_data...>

sensor_data format (key:value คั่นด้วย |):
  T:<temp_c>       อุณหภูมิอากาศ (BME280)
  H:<hum_pct>      ความชื้นอากาศ (BME280)
  P:<press_hpa>    ความดัน (BME280)
  SM:<pct>         Soil moisture %
  ST:<temp_c>      Soil temperature
  EC:<us_cm>       Electrical conductivity
  PH:<value>       pH
  WL:<mm>          Water level (mm)
  DO:<mg_l>        Dissolved oxygen
  NP:<ppm>         Nitrogen
  PP:<ppm>         Phosphorus
  KP:<ppm>         Potassium
  CO:<ppm>         CO2
  LX:<lux>         Light (lux)
  RN:<mm>          Rain (mm accumulated)
  WS:<m_s>         Wind speed
  WD:<deg>         Wind direction

ตัวอย่าง payload ครบ:
  N:32|S:157|B:3820|LA:13.756300|LO:100.501800|T:28|H:65|SM:42|ST:27|PH:6.8|WL:1250
```

> Payload ยังอยู่ใน XOR + CRC16 เหมือนเดิม  
> LoRa SF7 BW125: max payload ~242 bytes — ยังเหลือเยอะ

### 6.3 RS-485 Wiring บน STM32

```
STM32 UART2:
  PA2 (TX) ──► MAX485 DI
  PA3 (RX) ◄── MAX485 RO
  PA4 (GPIO) ─► MAX485 DE+RE  (LOW=RX, HIGH=TX)

Termination:
  120Ω resistor ที่ปลาย bus ทั้งสองข้าง (ถ้า bus ยาว > 10m)

สายที่ใช้:
  Shielded twisted pair (STP) — Belden 9841 หรือ CAT5e
  Shield ต่อ GND ที่ Master เท่านั้น (ปลายเดียว)

Power สำหรับ Modbus sensor:
  12V หรือ 24V DC (sensor ส่วนใหญ่ต้องการ)
  → ต้องมี power supply แยกถ้า STM32 ใช้ battery
  → หรือเลือก sensor ที่ใช้ 5V/3.3V
```

---

## 7. กลยุทธ์การเลือก Protocol

### Decision Tree

```
ต้องการ sensor แบบไหน?
│
├─ Onboard / ใกล้มาก (< 50cm)
│    → I2C  (BME280, CO2, ADC)
│
├─ Temperature หลายจุด (< 100m)
│    → 1-Wire + DS18B20
│
├─ Field sensor ราคาถูก, ระยะ < 50m
│    → Analog 0-5V / 0-10V + ADC
│
├─ Field sensor ระยะไกล (> 50m) หรือ noise สูง
│    → 4-20mA Current Loop
│
├─ Industrial sensor (pH, EC, NPK, DO, level, etc.)
│    → Modbus RTU over RS-485  ← แนะนำ
│
├─ Environmental sensor ระดับ scientific
│    → SDI-12 (METER/Campbell)
│
├─ Machine monitoring, real-time, safety
│    → CAN Bus + CANopen
│
└─ ระบบ factory PLC (Siemens)
     → Profibus  (ไม่แนะนำสำหรับระบบนี้)
```

### สำหรับระบบนี้ โดยสรุป:

```
Priority 1 (ทำได้เลย):
  I2C    → BME280, CO2, ADC converter (ADS1115)
  Analog → ความชื้นดิน, ระดับน้ำ (4-20mA)

Priority 2 (เพิ่มได้ไม่ยาก):
  Modbus RTU RS-485 → pH, EC, NPK, DO, Modbus level sensor
  1-Wire → DS18B20 temperature array

Priority 3 (ถ้าต้องการ precision):
  SDI-12 → METER soil sensor

ไม่แนะนำตอนนี้:
  CAN Bus   → ถ้าไม่มี machine monitoring
  Profibus  → ไม่เกี่ยวกับระบบนี้
  HART      → ถ้าไม่มีระบบ 4-20mA เดิม
```

---

## 8. แนวทาง STM32 Node รองรับ Multi-Protocol

### 8.1 Hardware UART Allocation (STM32F411)

```
USART1  → Debug / Serial monitor   (PA9/PA10)
USART2  → RS-485 Modbus Master     (PA2/PA3 + PA4=DE/RE)
USART3  → SDI-12 (ถ้าต้องการ)      (PB10/PB11)
UART4   → GPS module (อนาคต)       (PC10/PC11)

I2C1    → BME280, ADS1115, OLED    (PB6/PB7)
I2C2    → อนาคต                    (PB10/PB11)

SPI1    → RFM95 (LoRa)             (PA5/PA6/PA7)
SPI2    → SD card (อนาคต)

GPIO    → 1-Wire DS18B20           (PB0)
GPIO    → Rain gauge pulse          (PB1, interrupt)
ADC1    → Battery voltage           (PA0)
ADC2    → 0-5V analog sensor        (PA1)
```

### 8.2 Polling Sequence ใน STM32

```cpp
// loop() ทุก 60 วินาที:
void collectAllSensors() {
    // 1. I2C sensors (เร็ว)
    readBME280();           // ~5ms
    readCO2();              // ~5ms

    // 2. ADC / 4-20mA (เร็ว)
    readWaterLevel();       // ~1ms
    readSoilMoistureAnalog(); // ~1ms

    // 3. Modbus sensors (ช้ากว่า — ต้องรอ response)
    readModbus(0x01, REG_PH,   2, &ph_value);     // ~50ms
    readModbus(0x02, REG_EC,   2, &ec_value);     // ~50ms
    readModbus(0x03, REG_NPK,  6, &npk_values);   // ~50ms
    readModbus(0x04, REG_DO,   2, &do_value);     // ~50ms

    // 4. 1-Wire (อาจช้าถ้ามีหลายตัว)
    readDS18B20Array();     // ~750ms per sensor (conversion time)

    // 5. Build payload & send LoRa
    buildPayload();
    sendLoRa();
}
```

---

## 9. ตัวอย่าง Modbus Sensor ที่หาซื้อได้ทั่วไป (ราคา field grade)

### เซนเซอร์ดิน

| Sensor | วัดอะไร | Protocol | ราคา | หมายเหตุ |
|---|---|---|---|---|
| Jxct Soil 7-in-1 | Moisture, Temp, EC, pH, N, P, K | RS485 Modbus | 2,500 บาท | ครบในตัวเดียว |
| DFROBOT SEN0308 | Soil moisture | I2C/Analog | 350 บาท | ง่าย, ราคาถูก |
| Jxct Soil pH | pH | RS485 Modbus | 1,500 บาท | ต้องการการ calibrate |

### เซนเซอร์น้ำ

| Sensor | วัดอะไร | Protocol | ราคา | หมายเหตุ |
|---|---|---|---|---|
| 316SS Pressure | ระดับน้ำ (0–5m) | 4-20mA | 800 บาท | ทน IP68, ไม่มีชิ้นส่วนเคลื่อนที่ |
| JSN-SR04T | ระดับน้ำ ultrasonic | UART | 200 บาท | ไม่ contact, กันน้ำ |
| Jxct Water pH | pH น้ำ | RS485 Modbus | 2,000 บาท | ต้อง maintain electrode |
| Jxct DO | Dissolved O2 | RS485 Modbus | 3,500 บาท | ต้องการ calibrate |
| Jxct Turbidity | ความขุ่น | RS485 Modbus | 2,500 บาท | |

### อุตสาหกรรม

| Sensor | วัดอะไร | Protocol | ราคา | หมายเหตุ |
|---|---|---|---|---|
| Eastron SDM120 | ไฟฟ้า 1 phase | RS485 Modbus | 1,200 บาท | kWh, V, A, PF |
| Eastron SDM630 | ไฟฟ้า 3 phase | RS485 Modbus | 3,000 บาท | |
| PZEM-004T | ไฟฟ้า 1 phase | UART/Modbus | 300 บาท | ราคาถูก, ไม่ robust |

---

## 10. Physical / Environmental Protection

### IP Rating สำหรับ field deployment

```
IP54  → กัน splash, กันฝุ่นบางส่วน (พอสำหรับในที่ร่ม)
IP65  → กัน jet water, กันฝุ่น     (ติดตั้งกลางแจ้งได้)
IP67  → จมน้ำ 1m/30min             (ใกล้แหล่งน้ำ)
IP68  → จมน้ำ > 1m                 (ใต้น้ำ)

แนะนำ:
  STM32 node enclosure : IP65 (กล่อง ABS + cable gland)
  Sensor ที่ดิน/น้ำ    : IP67 ขึ้นไป
  Connector           : M12 connector (ทน IP67, industrial standard)
```

### สายสัญญาณ

```
สั้น (< 10m):   สาย dupont / jumper (ใช้ใน lab เท่านั้น)
กลาง (10-100m): Shielded twisted pair (STP) 24AWG
ยาว (100-1200m): Belden 9841 (RS-485 dedicated) หรือ CAT5e STP
4-20mA:          2-wire shielded, ไม่ต้องเป็น twisted pair
```

---

## 11. สรุปคำแนะนำสำหรับระบบนี้

### ทันที (Phase 1–2 ของ LoRa mesh)
```
I2C    : BME280 (อุณหภูมิ/ความชื้น/ความดัน)  ← มีแล้ว
Analog : ความชื้นดิน capacitive (cheap, proof of concept)
4-20mA : ระดับน้ำ pressure transducer (เพิ่ม ADS1115 สำหรับ I2C ADC)
```

### ระยะกลาง (เพิ่ม Modbus RS-485)
```
เพิ่ม MAX485 chip บน STM32 node
เพิ่ม library Modbus RTU
เชื่อม sensor:
  - Jxct Soil 7-in-1 (moisture, temp, EC, pH, N, P, K)
  - Pressure sensor Modbus (ระดับน้ำ แม่นยำกว่า)
  - Power meter (Eastron SDM120)
```

### ระยะยาว (ถ้าต้องการ precision)
```
SDI-12 : METER TEROS 12 (soil moisture + temp + EC แม่นยำมาก)
CAN    : ถ้าขยายไปยัง machine monitoring
```

### ไม่แนะนำสำหรับระบบนี้
```
Profibus  → ecosystem ผูกกับ Siemens PLC, ราคาสูง, ซับซ้อน
HART      → ใช้เมื่อ retrofit ระบบ 4-20mA เดิมเท่านั้น
Profinet  → ต้องการ Ethernet infrastructure
```
