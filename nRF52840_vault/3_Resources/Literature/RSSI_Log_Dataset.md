---
source: rssi_log.csv
fetched: 2026-07-30
topic: dataset, rssi-log, ml-training-data, field-test
used_in: "[[TFLite_Integration]]"
r1_block: [2]
r1_state: blocked
r1_role: confound
---

# RSSI Log Dataset

## Overview

86 samples from field test with 4 nodes (108, 116, 117, 118). Used to train the ML relay classifier.

## Columns

| Column | Description |
|--------|-------------|
| ts_ms | Timestamp (ms since boot) |
| node | THIS_NODE (who logged) |
| src | Original source node |
| from | Sender (relay or origin) |
| hop | Hop count (0=origin) |
| rssi | Signal strength (dBm) |
| snr | Signal-to-noise ratio (dB) |
| decision | RELAY_STATUS enum |
| flags | ACK/RELAYED/CRC_FAIL flags |

## Stats

- Range: RSSI -47 to -95 dBm
- 4 nodes: 108, 116, 117, 118
- Origin packets (hop=0) with clear decision: subset used for training

## Links
- [[TFLite_Integration]] — model trained from this data
- [[RSSI_Data_Logging]] — data collection mechanism
- [[_project-brief]]
