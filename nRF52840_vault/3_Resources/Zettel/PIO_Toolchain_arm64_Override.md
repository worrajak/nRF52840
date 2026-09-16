---
id: 20260721001
tags: [platformio, toolchain, apple-silicon, n-able, build, rosetta]
r1_block: [1]
r1_state: doing
r1_role: evidence
---

# PlatformIO n-able Toolchain — x86 → arm64 บน Apple Silicon

## ปัญหา

build บน Mac Apple Silicon (arm64) ล้มทันทีบรรทัดแรก:

```
sh: .../toolchain-gccarmnoneeabi@1.90301.200702/bin/arm-none-eabi-g++: Bad CPU type in executable
*** [...variant.cpp.o] Error 126
```

platform **n-able** (`platform-n-able-pro-micro-nrf52840`) pin toolchain แคบ
`"toolchain-gccarmnoneeabi": ">=1.80301.0,<1.100301.0"` (GCC 8–9) — เวอร์ชันนั้นมีแต่ binary **x86_64** ไม่มี arm64 → รันไม่ได้ถ้าไม่มี Rosetta

> ไม่ใช่ปัญหา setting ใน folder — platformio.ini เหมือนเดิมทุกอย่าง ตัวบล็อกคือ toolchain ระดับเครื่อง

## แก้ (ที่ใช้) — override เป็น toolchain arm64

`toolchain-gccarmnoneeabi` เวอร์ชัน arm64-native ติดตั้งอยู่แล้ว (`@1.120301.0` = GCC 12) → force ใน `platformio.ini`:

```ini
platform_packages =
  platformio/toolchain-gccarmnoneeabi@1.120301.0
```

- contained ใน platformio.ini · reversible · ไม่ต้อง sudo/Rosetta
- เช็ค arch: `file ~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++` → ต้องเห็น `arm64`
- ✅ build ผ่าน: Flash 23.0% · RAM 10.6%

## ทางเลือกสำรอง — Rosetta 2

```bash
softwareupdate --install-rosetta --agree-to-license
```

รัน toolchain x86 เดิม (ชัวร์ compatibility) แต่เปลี่ยนระดับระบบ + ยอมรับ license

## หมายเหตุ

- GCC 12 ใหม่กว่าที่ n-able ทดสอบ (GCC 9) → build clean แต่ควรทดสอบ runtime BLE/LoRa จริง
- port FireWild DePIN (`node_nrf52840`) ใช้ fix เดียวกัน + ต้อง exclude tinycrypt ecc เพิ่ม (เพราะเพิ่ม micro-ecc secp256k1 → uECC ชน) — project นี้ไม่มี micro-ecc จึงไม่ต้อง

## Links

[[00_MOC]] · [[Software_SPI_Bitbang_nRF52840]] · [[_project-brief]]
