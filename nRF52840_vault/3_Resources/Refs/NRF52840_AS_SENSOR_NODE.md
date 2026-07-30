# nRF52840 — Dual Role: Relay + Sensor Node

วันที่: 2026-05-13

---

## 1. ทำไม nRF52840 ทำได้มากกว่า STM32F103

```
STM32F103 (Blue Pill):   72 MHz Cortex-M3,  64–128 KB Flash,   20 KB RAM
STM32F411:              100 MHz Cortex-M4F, 512 KB Flash,     128 KB RAM
nRF52840 (Pro Micro):    64 MHz Cortex-M4F,   1 MB Flash,     256 KB RAM  ← เยอะมาก
```

nRF52840 มี RAM มากกว่า STM32F103 ถึง **12×** และ Flash มากกว่า **8–16×**  
แม้จะใช้ NimBLE (BLE stack) ซึ่งกินทรัพยากรเยอะ ก็ยังเหลือมากพอ

---

## 2. Memory Budget (nRF52840 ปัจจุบัน + dual role)

```
Flash (1024 KB):
  NimBLE stack          ~150 KB
  LoRa custom driver    ~ 15 KB   (software SPI bit-bang + radio logic)
  Relay + app logic     ~ 40 KB
  OLED + BME280         ~ 20 KB
  BLE GPS               ~ 10 KB
  ─────────────────────────────
  ใช้ไปแล้ว             ~235 KB  (23%)
  ยังเหลือ              ~789 KB  (77%)

  ถ้าเพิ่ม Modbus + multi-sensor:
  Modbus RTU library    ~ 10 KB
  Sensor drivers        ~ 15 KB
  ─────────────────────────────
  รวม                   ~260 KB  (25%) ← ยังเหลือ 75%

RAM (256 KB):
  NimBLE runtime        ~ 60 KB
  Stack + globals       ~ 25 KB
  Relay cache/queue     ~  3 KB
  Tracked nodes         ~  2 KB
  OLED buffer           ~  1 KB
  ─────────────────────────────
  ใช้ไปแล้ว             ~ 91 KB  (36%)
  ยังเหลือ              ~165 KB  (64%)

  ถ้าเพิ่ม Modbus + sensor buffers:
  Modbus frame buffer   ~  1 KB
  Sensor value cache    ~  2 KB
  ─────────────────────────────
  รวม                   ~ 94 KB  (37%) ← ยังเหลือ 63%
```

**สรุป: มีทรัพยากรเหลือมากพอสำหรับ dual role อย่างสบาย**

---

## 3. Peripheral ที่มีและ Bug ที่ต้องระวัง

### 3.1 PINS_COUNT=34 Bug — กระทบแค่ Hardware SPI เท่านั้น

```
n-able core ตั้ง PINS_COUNT=34 (ทั้งที่ nRF52840 มี 48 GPIO จริง)
→ ทำให้ hardware SPI pin mapping ผิด → บังคับ Software SPI bit-bang สำหรับ LoRa

สิ่งที่ "ไม่ได้รับผล" จาก bug นี้:
  ✓ Hardware UART (UARTE0, UARTE1) — ใช้ Serial, Serial1
  ✓ Hardware I2C  (TWI0, TWI1)    — ใช้ Wire (ใช้อยู่แล้ว: OLED + BME280)
  ✓ Hardware ADC  (SAADC)         — ใช้ analogRead() (ใช้อยู่แล้ว: battery)
  ✓ GPIO bit-bang                 — ใช้ nrf_gpio_* ได้เต็มที่
  ✗ Hardware SPI                  — bug! → ใช้ software SPI แทน (LoRa)
```

### 3.2 Peripheral Inventory

```
ใช้อยู่แล้ว:
  Software SPI (GPIO)  → LoRa radio (SX1262 หรือ RFM95)
  UARTE0 / Serial      → USB Debug output
  TWI0 / Wire          → OLED SSD1306 + BME280
  SAADC ch7 (P0.31)    → Battery voltage
  NimBLE               → BLE GPS from phone

ว่างอยู่ พร้อมใช้:
  UARTE1 / Serial1     → Modbus RS-485 (MAX485) หรือ SDI-12
  TWI1 / Wire1         → I2C sensor เพิ่มเติม (address pool แยก)
  SAADC ch0–ch6        → 7 channels ADC ว่าง (4-20mA, analog sensor)
  GPIO (P0.xx/P1.xx)   → 1-Wire DS18B20, rain gauge pulse, MAX485 DE/RE
```

### 3.3 Pin ที่ว่างบน nRF52840 Pro Micro

```
P0.02  (GPIO 2)   → ADC ch0    — analog sensor
P0.03  (GPIO 3)   → ADC ch1    — analog sensor
P0.04  (GPIO 4)   → ADC ch2    — analog sensor
P0.05  (GPIO 5)   → ADC ch3    — analog sensor
P0.06  (GPIO 6)   → GPIO       — 1-Wire DS18B20 / rain pulse
P0.07  (GPIO 7)   → GPIO       — MAX485 DE/RE control
P0.08  (GPIO 8)   → GPIO       — spare
P0.13  (GPIO 13)  → GPIO       — spare
P0.17  (GPIO 17)  → UART1 TX   — Modbus RS-485 (via MAX485)
P0.20  (GPIO 20)  → UART1 RX   — Modbus RS-485 (via MAX485)
P1.09  (GPIO 41)  → GPIO/ADC   — spare
```

---

## 4. Dual Role Architecture

```
nRF52840 ทำสองอย่างพร้อมกัน:

Role 1 — RELAY (เหมือนเดิม):
  รับ LoRa packet จาก STM32 → relay ต่อไปยัง gateway
  RSSI-based forwarding delay
  Duplicate detection

Role 2 — LOCAL SENSOR NODE (เพิ่มใหม่):
  อ่าน sensor ที่ต่ออยู่กับตัวเอง (Modbus / I2C / ADC)
  ส่ง data packet ของตัวเองผ่าน LoRa ทุก 60 วินาที
  Address: 0x10–0x1F (relay range) → Gateway รู้ว่ามาจาก relay node

Role 3 — BLE LOCAL ACCESS (มีอยู่แล้ว):
  ส่ง sensor data ผ่าน BLE ให้ phone อ่านได้โดยตรง
  ไม่ต้องผ่าน LoRa → MQTT ถ้าต้องการข้อมูล real-time
```

### ข้อดีของ Dual Role

```
1. ลด hardware count
   relay node ที่ติดตั้งอยู่แล้ว → เพิ่ม sensor ได้โดยไม่ต้องซื้อ node ใหม่

2. strategic placement ได้ประโยชน์คู่
   ตำแหน่งที่ relay ดี → มักเป็นตำแหน่งที่ sensor coverage ดีด้วย

3. BLE local access
   technician เดินเข้ามาใกล้ → เปิดโทรศัพท์อ่านค่า sensor ได้ทันที
   ไม่ต้องรอ cycle 60 วินาทีผ่าน LoRa → MQTT → Grafana

4. LoRa RX ยังทำงานปกติ
   Modbus polling ใช้เวลา < 200ms ต่อรอบ
   LoRa RX เป็น interrupt-driven → ไม่พลาด packet ระหว่าง Modbus polling
```

---

## 5. Protocol ที่ nRF52840 รองรับได้

### 5.1 I2C (ใช้อยู่แล้ว — Wire)

```cpp
// Wire ปัจจุบัน: OLED + BME280 บน I2C address pool เดิม
// เพิ่ม sensor ได้เลยถ้า address ไม่ชน

// ตัวอย่าง sensor เพิ่มบน Wire เดิม:
#include <Adafruit_SHT31.h>    // SHT31 อุณหภูมิ/ความชื้น แม่นยำกว่า (0x44)
#include <Adafruit_BH1750.h>   // BH1750 แสง lux (0x23)
#include <Adafruit_INA226.h>   // INA226 กระแส/แรงดัน (0x40)
#include <Adafruit_ADS1X15.h>  // ADS1115 16-bit ADC 4ch (0x48) ← สำหรับ 4-20mA
```

### 5.2 Hardware ADC — 4-20mA และ Analog Sensor

```cpp
// SAADC — 12-bit, ใช้ analogRead() ตรงๆ
// ใช้อยู่แล้วสำหรับ battery (P0.31 = ch7)

// 4-20mA ผ่าน 249Ω shunt resistor:
// 4mA × 249Ω = 0.996V  → ADC = ~1228/4095
// 20mA × 249Ω = 4.98V  → ADC = ~4090/4095  (แต่ nRF52840 VDD=3.3V ต้อง scale)
// แนะนำ: ใช้ ADS1115 (I2C, 16-bit, รองรับ PGA gain) แทน SAADC โดยตรง
//         เพราะ ADS1115 รับ input ได้ถึง ±6.144V ด้วย PGA

// Soil moisture analog (0-3.3V):
float readSoilMoisture() {
    analogReadResolution(12);
    uint16_t raw = analogRead(SOIL_PIN);   // 0–4095
    return map(raw, 400, 3000, 100, 0);    // calibrate ตาม sensor
}
```

### 5.3 Hardware UART → Modbus RTU RS-485 ← สำคัญที่สุด

```cpp
// Serial1 (UARTE1) — ไม่ได้รับผลจาก PINS_COUNT bug
#define MODBUS_TX_PIN  17   // P0.17
#define MODBUS_RX_PIN  20   // P0.20
#define MODBUS_DE_PIN   7   // P0.07 → MAX485 DE+RE

void initModbus() {
    Serial1.begin(9600, SERIAL_8N1);        // Modbus RTU มักใช้ 9600
    pinMode(MODBUS_DE_PIN, OUTPUT);
    digitalWrite(MODBUS_DE_PIN, LOW);       // RX mode เริ่มต้น
}

// ส่ง Modbus request (Master TX)
uint8_t modbusRequest(uint8_t slaveAddr, uint8_t func,
                      uint16_t regAddr, uint16_t regCount,
                      uint8_t* response, uint8_t respLen) {
    uint8_t req[8];
    req[0] = slaveAddr;
    req[1] = func;                          // 0x03 = Read Holding Registers
    req[2] = (regAddr >> 8) & 0xFF;
    req[3] = regAddr & 0xFF;
    req[4] = (regCount >> 8) & 0xFF;
    req[5] = regCount & 0xFF;
    uint16_t crc = crc16modbus(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    digitalWrite(MODBUS_DE_PIN, HIGH);      // TX mode
    delayMicroseconds(100);
    Serial1.write(req, 8);
    Serial1.flush();
    delayMicroseconds(100);
    digitalWrite(MODBUS_DE_PIN, LOW);       // RX mode

    // รอ response
    uint32_t start = millis();
    uint8_t idx = 0;
    while (millis() - start < 200 && idx < respLen) {
        if (Serial1.available())
            response[idx++] = Serial1.read();
    }
    return idx;
}
```

### 5.4 1-Wire — DS18B20

```cpp
// Software 1-Wire บน GPIO — ไม่ได้รับผลจาก bug
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_PIN  6   // P0.06

OneWire          oneWire(ONE_WIRE_PIN);
DallasTemperature ds(&oneWire);

// อ่านอุณหภูมิทุก sensor บน bus:
void readDS18B20() {
    ds.requestTemperatures();              // ~750ms conversion
    int count = ds.getDeviceCount();
    for (int i = 0; i < count; i++) {
        float t = ds.getTempCByIndex(i);
        // เก็บลง array
    }
}
```

### 5.5 Interrupt Input — Rain Gauge / Flow / Wind Pulse

```cpp
// GPIO interrupt — ทำงานปกติ ไม่มี bug
#define RAIN_PULSE_PIN  13   // P0.13
volatile uint32_t rainPulseCount = 0;

void IRAM_ATTR rainISR() { rainPulseCount++; }

void initRainGauge() {
    pinMode(RAIN_PULSE_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(RAIN_PULSE_PIN),
                    rainISR, FALLING);
}
// 1 pulse = 0.2794 mm rain (tipping bucket standard)
float getRainMM() { return rainPulseCount * 0.2794f; }
```

---

## 6. Modbus Sensor Polling ไม่รบกวน LoRa RX

```
LoRa RX:      interrupt-driven (lora_rx_flag)
              → lora_isr() ทำงานทันทีที่รับ packet
              → ไม่ต้องรอ main loop

Modbus poll:  ทำใน main loop ทุก 60 วินาที
              → request → รอ response ≤ 200ms → อ่านค่า

Timeline:
  t=0s:       Modbus poll เริ่ม
  t=0.05s:    request ถึง sensor (9600 baud, 8 bytes ≈ 9ms)
  t=0.10s:    sensor ตอบกลับ
  t=0.20s:    Modbus poll จบ (worst case)

  ← ถ้า LoRa packet มาระหว่างนี้ → lora_isr() set flag ทันที
  ← receivePacket() จะทำงานใน loop รอบถัดไป (< 1ms หลัง Modbus จบ)
  ← airtime LoRa = 70ms → packet ไม่หายแน่นอน

สรุป: ไม่มี conflict ระหว่าง Modbus polling กับ LoRa RX
```

---

## 7. Payload ของ nRF52840 เมื่อทำ Dual Role

```
Node address: 0x10 (relay range) แต่ส่ง sensor data ของตัวเอง
Gateway แยกแยะได้:
  - FROM = 0x10 → รู้ว่าเป็น relay node ที่ส่งข้อมูลของตัวเอง
  - FROM = 0x10 แต่ payload N:32 → รู้ว่าเป็น relay ส่งต่อของ node 32

Payload ของตัวเอง:
  N:16|S:5|T:29|H:70|B:3850|LA:13.7580|LO:100.5030|SM:45|PH:6.8|WL:1200

MQTT topic ที่ gateway publish:
  lora/16/data  ← node_id=16 ระบุว่าเป็น relay node ส่งข้อมูลตัวเอง
```

---

## 8. BLE Local Access — ประโยชน์เพิ่มเติม

```
เมื่อ nRF52840 มี sensor data ของตัวเอง:

BLE Characteristic เพิ่มเติม:
  UUID: 12345678-1234-1234-1234-123456789abe   ← sensor data (notify)
  Format: JSON หรือ CSV
  Update rate: ทุก 5 วินาที (เร็วกว่า LoRa interval)

ประโยชน์:
  1. Technician เดินเข้ามา → เปิด phone → อ่านค่า sensor ได้ทันที
  2. ไม่ต้องรอ LoRa → MQTT → Grafana (ใช้เวลา ~5-10 วินาที)
  3. Calibrate sensor on-site โดยไม่ต้องเปิด Grafana
  4. Debug ระบบได้ง่ายขึ้น

BLE notify payload (ตัวอย่าง):
  {"T":29,"H":70,"SM":45,"PH":6.8,"WL":1200,"BAT":3850,"RSSI":-75}
```

---

## 9. สรุปเปรียบเทียบ Role ของแต่ละ Node

| Node | Hardware | Role หลัก | Sensor protocol | BLE |
|---|---|---|---|---|
| **STM32** | F103/F411 | Sensor only | I2C, UART, RS-485 Modbus, ADC | ไม่มี |
| **nRF52840** | Pro Micro | **Relay + Sensor** | I2C, UART Modbus, ADC, 1-Wire | ✅ |
| **ESP32** | DevKit | Gateway | RadioLib LoRa | ได้ถ้าต้องการ |

---

## 10. Sensor ที่แนะนำสำหรับ nRF52840 Node

### ต่อ I2C (Wire — ตรงๆ ไม่ต้องเพิ่ม chip)
```
BME280      อุณหภูมิ/ความชื้น/ความดัน   (มีแล้ว)
SHT31       อุณหภูมิ/ความชื้น แม่นยำ   0x44
ADS1115     ADC 4ch 16-bit (4-20mA)    0x48  ← แนะนำมากสำหรับ field
BH1750      แสง (lux)                  0x23
INA226      กระแส/แรงดัน               0x40
SCD41       CO2 (NDIR)                 0x62
```

### ต่อ Serial1 + MAX485 (Modbus RS-485)
```
Jxct Soil 7-in-1   moisture + temp + EC + pH + N + P + K
Jxct Water pH      pH น้ำ
Jxct DO            Dissolved Oxygen
Eastron SDM120     Power meter (ไฟฟ้า)
Pressure Modbus    ระดับน้ำ แม่นยำ
```

### ต่อ GPIO (ง่าย ราคาถูก)
```
DS18B20     อุณหภูมิ กันน้ำ (1-Wire)
Rain gauge  tipping bucket pulse
Wind speed  anemometer pulse
```

---

## 11. Wiring Diagram — nRF52840 Dual Role

```
nRF52840 Pro Micro
         ┌─────────────────────┐
P0.15 ───┤ STATUS LED          │
P1.04 ───┤ OLED SDA (I2C)      ├── OLED ──┐
P0.11 ───┤ OLED SCL (I2C)      │          ├── Wire (I2C bus)
         │                     │          ├── BME280
P0.04 ───┤ ADS1115 SDA         │          └── ADS1115 (ADC)
P0.05 ───┤ ADS1115 SCL         │
         │                     │   ADS1115 → A0: 4-20mA sensor #1
P0.17 ───┤ Serial1 TX          ├── MAX485 DI
P0.20 ───┤ Serial1 RX          ├── MAX485 RO
P0.07 ───┤ MAX485 DE/RE        ├── MAX485 DE
         │                     │   MAX485 A/B → Modbus sensor bus
P0.06 ───┤ 1-Wire              ├── DS18B20(s) (4.7kΩ pull-up to 3.3V)
P0.13 ───┤ Rain pulse          ├── Rain gauge (INPUT_PULLUP)
         │                     │
P0.31 ───┤ Battery ADC         │   (ใช้อยู่แล้ว)
P1.13 ───┤ LoRa CS (SW SPI)   ├── RFM95/SX1262
P1.11 ───┤ LoRa SCK (SW SPI)  │   (ใช้อยู่แล้ว)
P1.15 ───┤ LoRa MOSI          │
P0.02 ───┤ LoRa MISO          │
P0.09 ───┤ LoRa RST           │
P0.29 ───┤ LoRa IRQ           │
         └─────────────────────┘
```
