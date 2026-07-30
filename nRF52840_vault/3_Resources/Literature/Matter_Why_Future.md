---
source: MATTER_WHY_AND_FUTURE.md
fetched: 2026-06-09
topic: matter, smart-home-history, security, local-control, roadmap
used_in: "[[Matter_Protocol_Analysis]]"
---

# Matter — ทำไมถึงพัฒนา + อนาคต

## ปัญหาก่อน Matter (ก่อน 2022)

| ปัญหา | ตัวอย่าง |
|------|---------|
| Ecosystem fragmentation | Apple HomeKit / Google Home / Amazon Alexa ไม่คุยกัน |
| Cloud dependency | Revolv/Wink ปิด → อุปกรณ์ทุกชิ้นใช้ไม่ได้ทันที |
| Security ต่ำ | Mirai botnet 2016: กล้อง IP 600,000 ตัวถูก hijack |
| Vendor lock-in | ย้ายแบรนด์ไม่ได้โดยไม่ซื้อใหม่ทั้งหมด |

## 4 เป้าหมายหลักของ Matter

1. **Interoperability** — อุปกรณ์ต่างแบรนด์คุยกันได้
2. **Local Control** — ทำงานโดยไม่ต้องมี internet
3. **Security by Default** — TLS + PKI ทุก device บังคับ
4. **Reliability** — ไม่พัง + vendor ปิดตัวก็ยังใช้ได้

## ทำไม Memory เยอะ — Layer Breakdown

| Layer | Flash | RAM | ทำอะไร |
|-------|-------|-----|---------|
| Mbed TLS (crypto) | ~120 KB | ~30 KB | TLS handshake, ECDSA, AES-CCM |
| OpenThread | ~200 KB | ~40 KB | Full IPv6 mesh (routing, fragmentation) |
| Matter Core | ~150 KB | ~40 KB | Device models, clusters, interactions |
| mDNS / DNS-SD | ~40 KB | ~15 KB | Device discovery บน network |
| Device Attestation | ~30 KB | ~10 KB | Certificate chain, ป้องกัน counterfeit |
| **รวม** | **~540 KB** | **~135 KB** | — |

> แต่ละ layer มีเหตุผล ไม่สามารถตัดออกได้

## Matter Roadmap

| Version | ปี | Feature ใหม่ |
|---------|-----|-------------|
| 1.0 | 2022 | Lighting, Locks, Sensors, Switches |
| 1.1 | 2023 | Refrigerators, AC, Washing machines |
| 1.2 | 2023 | Energy monitoring, Dishwashers |
| 1.3 | 2024 | Energy Management (solar, battery) |
| 1.4 | 2024 | EV chargers, Water management |
| 1.5+ | 2025+ | Building automation, Industrial IoT |

## Philosophy Comparison

| | Matter | LoRa Mesh | Modbus |
|--|--------|-----------|--------|
| Range | 30–100m (Thread) | 2–15 km | 1200m |
| Primary use | Smart Home | Field IoT | Industrial |
| Internet needed | No (local) | No | No |
| Power | Medium | Low | Low-Medium |
| Best for | Consumer devices | Outdoor mesh | Sensor networks |

## สรุป: ทำไม LoRa ถูกต้องสำหรับโปรเจกต์นี้

ระบบ outdoor environmental monitoring ที่ต้องการ km-range → LoRa คือ right tool
Matter เป็น consumer smart home layer → เพิ่มได้ผ่าน ESP32 Bridge ในอนาคต

## Links

- [[Matter_Protocol_Analysis]]
- [[Architecture_LoRa_Mesh]]
