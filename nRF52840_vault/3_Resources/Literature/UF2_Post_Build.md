---
source: copy_uf2.py
fetched: 2026-07-30
topic: uf2, post-build, platformio, firmware-deployment
used_in: "[[UF2_Post_Build_nRF52840]]"
---

# UF2 Post-Build Script

## Overview

PlatformIO extra_script that converts Intel HEX → real nRF52840 UF2 bootloader format after each build.

## Features

- Converts `.elf` → Intel HEX → UF2
- Names file as `firmware_node_<ID>_<radio>.uf2`
- Copies to project root for easy drag-and-drop flashing

## Links
- [[UF2_Post_Build_nRF52840]] — zettel
- [[PIO_Node_ID_Radio_Config]]
- [[_project-brief]]
