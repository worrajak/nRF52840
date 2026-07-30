---
source: platformio.ini
fetched: 2026-07-30
topic: platformio, build-config, node-id, radio-select, apple-silicon
used_in: "[[_project-brief]]"
---

# PlatformIO Build Configuration

## Key Settings

| Setting | Value | Description |
|---------|-------|-------------|
| `custom_node_id` | 117 | Node ID (1–254) |
| `custom_radio` | USE_RFM95 | USE_SX1262 or USE_RFM95 |
| `custom_low_power` | 1 | 0=normal, 1=System-ON idle |
| Platform | n-able pro_micro_nrf52840 | Custom fork |
| Framework | Arduino | nRF5 core |

## Apple Silicon Fix

Override toolchain to arm64 GCC 12.3.1 (avoid Rosetta):
```
platform_packages = platformio/toolchain-gccarmnoneeabi@1.120301.0
```

## Build Flags

```
-DNODE_ID=117 -DUSE_RFM95 -DLOW_POWER_IDLE=1
-DVARIANT_MCK=64000000ul -DUSE_TINYUSB -DUSBCON
-DCONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED
-DCONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED
```

## Dependencies
- Adafruit SSD1306, BME280 Library, Unified Sensor
- h2zero/NimBLE-Arduino @ 2.3.8

## Links
- [[PIO_Node_ID_Radio_Config]] — zettel
- [[PIO_Toolchain_arm64_Override]] — zettel
- [[_project-brief]]
