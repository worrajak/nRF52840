---
source: ble_gps_sender.html
fetched: 2026-07-30
topic: ble, gps, web-bluetooth, phone, pwa
used_in: "[[_project-brief]]"
---

# BLE GPS Sender — Web App

## Overview

PWA-style web app that sends GPS coordinates from phone browser to nRF52840 via Web Bluetooth API.

## How It Works

1. Phone browser → Web Bluetooth → connect to nRF52840
2. Reads phone GPS (Geolocation API)
3. Writes `N:<nodeId>|LA:<lat>|LO:<lon>` to BLE characteristic
4. nRF52840 includes GPS in LoRa payload

## BLE Service

- Service UUID: `12345678-1234-1234-1234-123456789abc`
- GPS Char UUID: `12345678-1234-1234-1234-123456789abd`

## Links
- [[_project-brief]]
