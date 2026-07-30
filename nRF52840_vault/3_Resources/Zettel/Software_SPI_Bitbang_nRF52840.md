---
id: 20260609003
tags: [nrf52840, spi, bug, workaround, n-able]
---

# Software SPI Bit-bang — Workaround สำหรับ n-able PINS_COUNT Bug

## Bug ที่พบ

n-able Arduino core สำหรับ nRF52840 มี `PINS_COUNT = 34` แทนที่จะเป็นจำนวน pin จริง
→ Hardware SPI library map pin ผิด → SX1262/RFM95 init ล้มเหลว

## Workaround: nrf_gpio bit-bang

```cpp
// ใช้ nrf_gpio_* functions โดยตรง แทน SPI library
void spiWrite(uint8_t data) {
    for (int i = 7; i >= 0; i--) {
        nrf_gpio_pin_write(SCK_PIN, 0);
        nrf_gpio_pin_write(MOSI_PIN, (data >> i) & 1);
        nrf_gpio_pin_write(SCK_PIN, 1);
    }
}
```

## สิ่งที่ได้รับผลกระทบจาก Bug

- ❌ Hardware SPI (`SPI.begin()`) — ใช้ไม่ได้
- ✅ Software SPI bit-bang — ใช้ได้ (workaround นี้)
- ✅ UART (Serial, Serial1) — ไม่ได้รับผลกระทบ
- ✅ I2C (Wire) — ไม่ได้รับผลกระทบ
- ✅ ADC (analogRead) — ไม่ได้รับผลกระทบ
- ✅ NimBLE — ไม่ได้รับผลกระทบ

## ผลกระทบต่อ Performance

Software bit-bang ช้ากว่า hardware SPI แต่ LoRa ใช้ SPI แค่ตอน init + send/receive
→ ไม่มีผลกระทบต่อ LoRa throughput จริง (bottleneck คือ airtime LoRa ไม่ใช่ SPI)

## Links

- Source: [[nRF52840_Dual_Role]]
- ใช้ใน: [[Roadmap_LoRa_Mesh]]
