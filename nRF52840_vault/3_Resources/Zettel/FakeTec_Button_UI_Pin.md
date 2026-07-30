---
id: 20260726001
tags: [nrf52840, faketec, button, oled, ui, pinout]
---

# fakeTec v5 Button + OLED UI — BTN คือ P1.00

## Pin ที่ถูกต้อง

ปุ่ม programmable บน fakeTec v5 Rev.B คือ `SW2`:

```
SW2 → net BTN → Nice!Nano D6 → nRF52840 P1.00 → raw GPIO 32
SW2 อีกขา → GND
R7 10k → +3.3V (external pull-up)
C4 100nF → GND (hardware debounce)
```

ใน firmware ใช้:

```cpp
#define UI_BUTTON_PIN 32  // P1.00, active LOW

nrf_gpio_cfg_input(UI_BUTTON_PIN, NRF_GPIO_PIN_PULLUP);
bool pressed = (nrf_gpio_pin_read(UI_BUTTON_PIN) == 0);
```

> อย่าใช้ `PIN_BUTTON1 = 34 / P1.02` จาก generic `pro_micro_nrf52840`
> variant เพราะเป็น mapping ของบอร์ดทั่วไป ไม่ตรงกับปุ่ม `BTN` บน fakeTec PCB

## วิธีที่ยืนยัน mapping

ไล่ลำดับขาจาก `schematics_rev_B.pdf`:

| fakeTec/Nice!Nano net | nRF52840 raw GPIO |
|---|---:|
| SDA | 36 / P1.04 |
| SCL | 11 / P0.11 |
| BTN (ขาถัดจาก SCL) | **32 / P1.00** |

การเดาเป็น GPIO 34 ทำให้ปุ่มไม่ตอบสนอง แม้ build ผ่าน

## UI ปัจจุบัน

กดสั้นพร้อม software debounce 40 ms เพื่อวน `1 → 2 → 3 → 1`:

1. **THIS NODE** — ID, LoRa, temperature, humidity, TX count,
   TX power และผล TX ล่าสุด
2. **LAST RX / RELAY** — source ID, previous hop, hop count, RX RSSI,
   sensor และผล relay (`QUEUED`, `SENT`, `NO-RELAY`, `DROP-*`)
3. **DEVICE STATUS** — battery, LoRa, OLED, BLE, BME280, GPS และ uptime

Serial diagnostic เมื่อกด:

```text
[UI] Button pressed, page 2
```

## RSSI กับ TX Power

node รู้ค่า RSSI ของ packet ที่ตัวเอง **รับ** แต่ไม่รู้ RSSI ที่ปลายทางวัด
จาก packet ที่ตัวเองส่ง หากไม่มี ACK/report กลับมา ดังนั้นหน้า UI ใช้:

- `RX` = RSSI ที่วัดจาก packet ขาเข้า
- `TX` = configured transmit power (`+12 dBm`)

## Links

[[00_MOC]] · [[Software_SPI_Bitbang_nRF52840]] · [[UF2_Post_Build_nRF52840]]
