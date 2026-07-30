---
source: MATTER_PROTOCOL_DISCUSSION.md
fetched: 2026-06-09
topic: matter, thread, openthread, home-assistant, smart-home
used_in: "[[_project-brief]]"
---

# Matter Protocol — การวิเคราะห์ความเป็นไปได้บน nRF52840

## สรุปคำตอบ: ใช้ Matter บน nRF52840 ได้ แต่ไม่ควรทำตอนนี้

## Framework Conflict ⚠️

| Matter ต้องการ | เราใช้อยู่ |
|---------------|-----------|
| nRF Connect SDK | Arduino (n-able) |
| Zephyr RTOS | Arduino `loop()` |
| CMake | PlatformIO + Arduino |
| ~600 KB Flash | ~235 KB (ปัจจุบัน) |
| ~150 KB RAM | ~91 KB |

→ ไม่ compatible → ต้อง rewrite ทั้งหมดด้วย Zephyr

## Thread Range vs LoRa Range

```
LoRa SF12: ══════════════════════════════ 15 km (โล่ง)
LoRa SF7:  ════════════════ 2–3 km (เมือง)
Thread:    ███ 30–100 m (mesh hop)
```

**ปัญหา:** Thread ต้องการ node ทุก ~30m → ไม่เหมาะกับ outdoor/field

## Thread Border Router

Matter over Thread ต้องการ Border Router เพิ่ม:
- Apple HomePod mini / Apple TV 4K ✅
- Google Nest Hub 2nd gen ✅
- Raspberry Pi + HAT ✅
- ต้องต่อ internet ตลอดเวลา + อยู่ในระยะ Thread จากทุก node

## ตัวเลือกเปรียบเทียบ

| Option | ความยาก | ผลลัพธ์ |
|--------|---------|---------|
| Matter บน nRF52840 (Zephyr) | 🔴 ยากมาก | LoRa หายไป |
| Matter Bridge บน ESP32 | 🟡 ปานกลาง | LoRa คงอยู่ + Matter |
| MQTT → Home Assistant | 🟢 ง่าย | ได้ smart home ทันที |

## แนะนำ: Home Assistant (ระยะสั้น)

```yaml
# docker-compose เพิ่มแค่นี้:
homeassistant:
  image: homeassistant/home-assistant:stable
  ports: ["8123:8123"]

# configuration.yaml
mqtt:
  sensor:
    - name: "Node 32 Temperature"
      state_topic: "lora/32/data"
      value_template: "{{ value_json.temp_c }}"
```

→ MQTT ที่มีอยู่ทำงานได้เลย → Apple Home / Google Home / Alexa ได้ทันที

## Matter Bridge บน ESP32 (ถ้าต้องการ native Matter)

```
ESP32 Gateway + RadioLib (LoRa) + esp-matter SDK
→ Bridged Node device type
→ แต่ละ LoRa node = 1 Matter endpoint
→ Apple Home เห็นเป็น Temperature/Humidity Sensor
```

Framework: ESP-IDF + esp-matter (ไม่ใช่ Arduino) → เขียนใหม่

## Roadmap

| ระยะ | งาน | เวลา |
|------|-----|------|
| สั้น | MQTT → Home Assistant | < 1 วัน |
| กลาง | ESP32 Matter Bridge (ESP-IDF) | 2–4 สัปดาห์ |
| ยาว | nRF5340 (dual core, 512KB RAM) | hardware ใหม่ |

## Links

- [[_project-brief|← Project Brief]]
- [[Matter_Why_Future]]
- [[Architecture_LoRa_Mesh]]
