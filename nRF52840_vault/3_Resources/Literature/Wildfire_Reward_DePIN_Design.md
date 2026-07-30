# Wildfire DePIN Reward — Proof-of-Maintenance + 6-Pillar (Design Discussion)

วันที่: 2026-07-09 (อัปเดต: + provisioning + zone/reward-fairness)
สถานะ: 🟡 design ก้าวหน้ามาก — decisions ล็อกเพิ่ม (ดูข้อ 10) · **workspace โค้ดสร้างแล้ว**: `FireWildPMUC/FireWild_DePIN/` (node+gateway firmware + Python server + Android webapp + Obsidian)
บริบท: ชั้น Blockchain/Token Reward ของโครงการ **DaaS Wildfire (FireWildPMUC, สัญญา บพข. C05F690131)** สร้างบน LoRa mesh ของ vault นี้ + reuse reward model จาก **SkillChain RMUTL**

---

## 1. เป้าหมาย

ให้ **จิตอาสา/ชุมชน** ได้ reward (เหรียญ → แลกของรางวัล/มูลค่าบาท) จากการช่วยดูแลระบบเฝ้าระวังไฟป่า — ครอบคลุมทั้งการเข้าไปบำรุงรักษาโหนดจริง และการทำให้ระบบ mesh เสถียร (uptime, hop, gateway, อินเทอร์เน็ต) แบบ **โปร่งใส ตรวจสอบได้บน blockchain** และ **กันโกงด้วย cryptographic proof**

## 2. ฐานระบบที่มีอยู่ (จาก vault นี้)

```
STM32/nRF52840 sensor → nRF52840 relay → ESP32 GW → MQTT(HiveMQ) → Node-RED → InfluxDB/Grafana
packet: [TO][FROM][MSG_ID][FLAGS][payload XOR+CRC16]  · FLAGS: hop(4bit)+is_ack+ack_req+reserved
MQTT: lora/<node>/data (seq,rssi,hops,bat,relayed_by) · lora/<node>/ack · lora/gw/<gw>/status (uptime_s,wifi_rssi)
```
- **nRF52840** = relay + sensor + BLE (NimBLE, รับ GPS จากมือถือได้แล้ว) + **CryptoCell CC310** (crypto accel) — เหมาะกับงานนี้ที่สุด [[nRF52840_Dual_Role]]
- Node-RED dedup (src+seq) มีแล้ว → นับ packet จริงต่อ gateway ได้

## 3. Proof-of-Maintenance (dual-signature ผ่าน BLE + mesh)

**ปัญหา:** ไม่มีอินเทอร์เน็ตที่หน้า node (ป่าลึก) → identity ผู้ดูแลต้องเดินทางผ่าน mesh · เป็น reward → กันโกงต้องแน่น

**ทางออก = 2 ลายเซ็น (dual-signature):**

| ลายเซ็น | ใครเซ็น | เซ็นอะไร | พิสูจน์ |
|---------|--------|---------|---------|
| maintainerSig | wallet ในแอป (secp256k1/Tron) | `nonce ‖ node_id` | identity (ถือ private key จริง) |
| nodeSig | node (CC310 ECDSA) | `{node_id, counter, maintainer_id, ts, status, maintainerSig}` | presence (มาจาก node นี้จริง) |

**Flow:**
```
[App] login→wallet · BLE ต่อ node · อ่านสถานะ(bat/rssi/health)
      node ให้ counter(monotonic)+nonce ผ่าน BLE
      wallet เซ็น(nonce‖node_id) → maintainerSig · ส่ง{maintainer_id,maintainerSig} → node
        ▼ BLE write
[nRF52840] build E, เซ็น E ด้วย node key (CC310) → nodeSig
      payload: M:<node>|C:<counter>|MID:<id>|ST:..|MSIG:64B|NSIG:64B · FLAGS bit6=is_maint
        ▼ hop (relay logic เดิม, packet ~140B, airtime ~170ms occasional)
[ESP32 GW] → MQTT lora/<node>/maint  (GW มีเน็ต)
        ▼
[Python server] verify nodeSig+maintainerSig + counter>last-seen (anti-replay offline) + rate-limit
        ▼ reward (custodial-C ดูข้อ 5)
```
- **Anti-replay offline:** monotonic counter ใน flash ของ node (ไม่พึ่ง server nonce) · server จำ counter ล่าสุด/node · counter เก่า = reject
- **node key curve:** CC310 native = P-256; maintainer = secp256k1 (บังคับ เพราะ Tron wallet) · จะให้ node ใช้ secp256k1 ด้วยก็ได้ผ่าน uECC (micro-ecc บน M4) — **ยังไม่ล็อก**
- ความเสี่ยงคงเหลือ: งัด node ดึง key ปลอม presence จากไกล → CC310/KMU ลด, ATECC608 ถ้าต้องการสูงกว่า

## 4. DePIN Reward Taxonomy — 6 เสา

| # | เสา | พิสูจน์ด้วย | ใครได้ | วัดจาก | กันโกง |
|---|-----|-----------|--------|--------|--------|
| 1 | **Proof-of-Maintenance** | dual-sig BLE visit | ช่าง/จิตอาสา | signed maintenance event | node+maintainer ECDSA + rate-limit |
| 2 | **Proof-of-Time (uptime/เสถียร)** | ส่ง data สม่ำเสมอ | เจ้าของ/ผู้ดูแลโหนด | ความต่อเนื่อง seq + gap/jitter ใน window | **signed liveness beacon + counter** |
| 3 | **Proof-of-Data** | ทุกครั้งที่ส่ง (status/event) | โหนด | packet จริงที่ส่งถึง (dedup) | seq/counter + signed |
| 4 | **Proof-of-Relay (hop)** | forward ให้เพื่อนไกล | relay ที่ TX จริง | packet delivered × hops (สะท้อนพลังงาน) | delivered+dedup, loop detect, rate-limit |
| 5 | **Proof-of-Gateway/Backhaul** | รับ→MQTT+เน็ตเสถียร | เจ้าของ gateway | gw uptime + packet forward + MQTT ต่อเนื่อง | forward signed packet จริง |
| 6 | **Internet subsidy** | ใช้เน็ตบ้านตัวเอง | บ้านที่ตั้ง gateway | gw online-time/data volume | ผูก gateway↔host wallet |

**ข่าวดี:** เสา 2/3/4/5 คำนวณได้จาก telemetry เดิม (seq, hops, relayed_by, gw uptime_s) → เพิ่มแค่ reward logic layer ทับ InfluxDB/MQTT ไม่ต้องรื้อ

### เสา 4 (relay) — จุดที่พลาดง่ายสุด ต้องระวัง
- **Attribution:** ต้องแบ่งให้ทุก relay บน path (ไม่ใช่แค่ `relayed_by` ตัวสุดท้าย) → เพิ่ม **relay-path field** (แต่ละ relay ต่อ ID 1 byte ตอน forward)
- **relay ทำงาน+เปลืองพลังงานมากกว่า leaf → เรตควรสูงกว่า** + ต้องบำรุงบ่อยกว่า (ได้ PoM เพิ่มด้วย)
- จ่ายเฉพาะ packet ที่ **delivered ถึง GW จริง** (forward แล้วหาย = ไม่จ่าย)
- relay ที่ cancel (ไม่ได้ TX) = ไม่ได้ (เสีย RX พลังงานน้อย)

### Anti-gaming: signed liveness beacon
เซ็นทุก packet = เปลือง airtime/แบต → **แนะนำ: node ส่ง signed beacon เป็นระยะ (1/ชม. หรือ 1/วัน) มี counter** เป็นฐาน PoT/PoRelay reward · data ปกติส่ง unsigned เดินระบบ

## 5. Reward Mechanism = Custodial-C (ล็อกแนวทางแล้ว)

```
signed events (mesh) → MQTT → Python server
  verify signatures + counter + rate-limit
  accrue เศษเล็กๆ ต่อ wallet ต่อ event/packet ใน off-chain ledger (Supabase)   ← per-packet micro-reward
  ทุก epoch: publish batch → IPFS + build Merkle → anchor root บน Tron (audit)
redeem: แลกของจาก catalog (burn/decrement) ตี peg X บาท/coin — OFF-chain, user ไม่ transact
```
- **ทำไม custodial-C:** (ก) relay/data reward = micro-payment ต่อ packet ที่ผู้รับแปรผันต่อ path → **จ่าย on-chain ต่อ packet เป็นไปไม่ได้** (gas) (ข) user "ไม่มี TRX" → ต้องไม่ให้ user ส่ง tx เลย (SkillChain พิสูจน์แล้ว) (ค) ยังได้ immutable audit trail จาก Merkle root anchor (USP โครงการ)
- **dual-sig ของเรา = แทน gate `release()=onlyOwner` ของ SkillChain** (เปลี่ยน "ความเชื่อใจ" เป็น "ยืนยันภาคสนามเข้ารหัส")

## 6. SkillChain Reuse Map (จาก skillchain-web3, แมปแล้ว)

| ชั้น | reuse จาก SkillChain | ปรับสำหรับไฟป่า |
|------|----------------------|-----------------|
| เหรียญรางวัล | **TRPBToken.sol** (TRC-20, mint/burn, decimals=6, peg comment 1:THB) | "เหรียญจิตอาสาป้องกันไฟป่า" peg X บาท (เก็บใน config table) |
| แต้มชื่อเสียง | **SkillCredit.sol** SBT (award/awardBatch/revoke, lifetimeEarned, non-transferable revert) | "แต้มผู้พิทักษ์ป่า" reason=PATROL/FIREBREAK/NODE_MAINT — **โอนไม่ได้ → เลี่ยง พ.ร.ก.สินทรัพย์ดิจิทัล 2561** |
| แบ่งจ่าย | **JobEscrow.sol** bps split (รวม=10000) | maintainer/relay/gateway/กองทุน/ผู้ประสานงาน |
| บัญชี | dual-ledger (Supabase truth + Tron mirror) + content_hash+on_chain_tx anchoring | accrue off-chain + Merkle anchor |
| ไม่มี TRX | custodial (backend hot wallet เซ็น+จ่าย gas ทุก tx) | จิตอาสาไม่ transact เลย |
| off-ramp | PromptPay + easyslip + admin confirm (Phase 3 ยังไม่ build) | ใช้ถ้าภายหลังอยากจ่ายเงินสด |

### ⚠️ ก่อน reuse โค้ด SkillChain ต้องแก้ (เจอตอนแมป)
- **RLS = allow_all** บน fee_config/escrow_records → ใครก็แก้สัดส่วน/สถานะได้ → ต้องล็อกสิทธิ์
- TRPB **mint ไม่มี cap** + peg เป็นแค่ comment → เก็บ X ใน config + สิทธิ์แก้แบบ multisig/role
- JobEscrow **ไม่มี reentrancy guard / ไม่เช็ค transfer return / ไม่มี SafeERC20** → harden ถ้าขยับเงินจริง
- doc ขัดกัน (split 90/5/5 vs code 85/5/5/5) → sync ก่อน
- **relay reward เป็น dynamic per-path split** → JobEscrow เป็น role คงที่ 4 ทาง ทำไม่ได้ → ต้อง accrue off-chain แทน

## 7. Economics

- **peg:** 1 coin = X บาท (**มติที่ประชุม**) — เก็บใน config table (ไม่ hardcode) + สิทธิ์แก้ auditable · custodial float หนุนมูลค่า
- **แหล่งเงิน pool:** แมปกับงบโครงการ — **จ้างเหมาบำรุงรักษาระบบ 300,000** + ค่าบริการเครือข่าย (internet subsidy) 60,000 (จาก BOM/สัญญา C05F690131)
- **⚠️ Emission model จำเป็น:** จ่าย uptime ทุกวัน × โหนด × gateway + relay per-packet สะสมเร็ว → ต้องคุมเรตไม่ให้ pool แห้ง

## 8. Roles / Registry (สูงสุด 5 บทบาท — คนละ wallet, คนละ reward stream)

- เจ้าของโหนด (PoT/PoData) · relay (PoRelay) · เจ้าของ gateway (PoGateway) · ช่าง/จิตอาสา (PoMaintenance) · บ้านที่ให้เน็ต (subsidy)
- ต้องมี **node_registry** (nodeId→pubkey, พิกัด, วันติดตั้ง, revoke) + **maintainer/host wallet registry** + onboarding/approval (reuse approval_logs ของ SkillChain)
- หมายเหตุ: nRF52840 dual-role → device เดียวอาจได้หลาย stream (uptime + data + relay + maintenance)

## 9. Gaps ที่ต้องสร้างใหม่ (SkillChain ไม่มี)

LoRa/MQTT ingestion → Python verifier · node/device registry · nonce/counter replay + rate-limit · **Merkle accrual/anchor** (SkillChain มีแค่ per-row hash) · reward rate schedule ต่อเสา · **relay-path recording** · prize catalog + redemption ledger · anti-collusion (GPS/ภาพ/สุ่มตรวจ กันช่างคุมโหนดเอง) · fee model (ถ้าเลือก pull-claim)

## 10. Decisions — ล็อกแล้ว ✅

- [x] ทำ maintenance reward via BLE + blockchain
- [x] identity ไปทาง **mesh** (ไม่มีเน็ตที่ node)
- [x] **dual-signature**: maintainer wallet เซ็น identity + node เซ็น presence (ECDSA)
- [x] reward mechanism = **C → custodial-C** (off-chain accrue + Merkle anchor + redeem off-chain, user ไม่ transact)
- [x] chain = **Tron** · มี Python server prototype แล้ว
- [x] user ไม่มี TRX (custodial, backend จ่าย gas)
- [x] 1 coin = X บาท ตามมติที่ประชุม · แลกของรางวัล
- [x] ใช้ **SkillChain reward model** (TRPB + SkillCredit SBT + bps split + dual-ledger)
- [x] เสา reward = 6 (PoMaintenance/PoTime/PoData/**PoRelay**/PoGateway/InternetSubsidy)
- [x] relay ได้ reward ด้วย (ทำงาน+เปลืองพลังงานมากกว่า → เรตสูงกว่า leaf)
- [x] anti-gaming: **signed liveness beacon เป็นระยะ** (ไม่เซ็นทุก packet)
- [x] **custodial-C ยืนยัน** (ไม่ทำ pull-claim — user ไม่ transact, redeem off-chain)
- [x] SkillChain reuse ยืนยัน: TRPBToken + SkillCredit(SBT) + JobEscrow bps + dual-ledger (แก้ RLS/cap/reentrancy ก่อน)
- [x] **Provisioning**: Android web app (Web Bluetooth) + BLE commission (self-test + birth-cert dual-sig) + GPS มือถือ
- [x] **อนุมัติ**: register ผ่านเน็ต → หัวหน้าคณะทำงานจิตอาสา/หัวหน้าโครงการ approve ขั้นสุดท้าย · PENDING=ไม่ record ไม่ reward · ACTIVE=นับ forward
- [x] **id uniqueness**: server assign lora_addr (ห้าม hardcode) + UNIQUE (addr ACTIVE / pubkey ทั้งระบบ) + runtime collision→re-address
- [x] **zone/group**: node→zone(กลุ่ม, หัวหน้ากลุ่ม, gateway ครอบคลุม), coverage วางจาก risk map (top-down ไม่ farm)
- [x] **reward = pillar × difficulty_coeff, cap ต่อ zone_budget** (equity: ป่าได้มาก/ที่โล่งน้อย, กัน over-install) · leader override capped+SLA-gated

## 11. Decisions — ยังเปิด ⬜ (ต้องตัดสินก่อน implement)

- [ ] **relay-path**: append ID 1 byte/hop (เบา) vs signed chain-of-custody (แน่น, +64B/hop หนัก)
- [ ] **เรต reward ต่อเสา** (coin/วัน uptime, coin/ครั้ง maintenance, coin/วัน gateway, relay ×เท่าไร, subsidy/เดือน) + emission
- [ ] **signing scope**: beacon 1/ชม. vs 1/วัน (กระทบแบต+กันโกง)
- [ ] **node key curve**: P-256 (CC310 native) vs secp256k1 (uECC) · key provision/rotate/revoke · secure element ATECC608?
- [ ] **2-token** (SBT แต้ม + เหรียญแลกได้) vs token เดียว
- [ ] **reuse ระดับไหน**: fork skillchain-web3 (Next.js+Supabase+contracts) vs เขียน backend Python ใหม่ + ยืมเฉพาะ contracts+schema
- [ ] peg X = เท่าไร + governance table + mint cap/reserve backing
- [ ] prize catalog: ใครจัด/ออกงบ/mapping มูลค่า
- [ ] anti-collusion independent attestation (GPS/ภาพ/สุ่มตรวจ) — กันช่าง+โหนดสมรู้กัน
- [ ] anchor birth-cert commission ขึ้น Tron (audit) — เอาไหม
- [ ] address scaling: > ~96 node → 2-byte addr / pubkey-hash id
- [ ] ค่าจริง (committee): difficulty coeff/tier · base_rate ต่อ pillar · TOTAL_POOL/emission · SLA thresholds · LEADER_OVERRIDE_BPS

## 12. Provisioning & Device Registry (2026-07-09)
- แอปเดียว (Android Web Bluetooth) ทำทั้ง **commission + maintenance** — iOS ไม่รองรับ Web Bluetooth
- commission: self-test (challenge→device เซ็น→verify) + จับ GPS มือถือ + **birth-cert dual-sig** (device+installer) → register PENDING
- อนุมัติ: หัวหน้าคณะทำงานจิตอาสา/หัวหน้าโครงการ (role-gated + approval_logs) → ACTIVE (ก่อนหน้า = DROP)
- schema: `FireWild_DePIN/shared/registry_schema.md`

## 13. Zone/Group + Reward-fairness (2026-07-09)
- โครง: project → สาย → **zone(กลุ่ม, หัวหน้ากลุ่ม, gateway)** → nodes
- `reward = Σ(base_rate×quality) × difficulty_coeff` · **cap ต่อ zone_budget** (pro-rata กัน over-install)
- `zone_budget = pool × (risk×difficulty×planned_nodes)/Σ` · leader override ≤10% SLA-gated
- **equity:** จ่ายตาม effort/ความยาก/ความเสี่ยง — ป่าได้มาก ที่โล่งได้น้อย
- สูตร: `FireWild_DePIN/shared/reward_model.md`

## 14. IP — Candidate D (อนุสิทธิบัตร)

กลไก **"Proof-of-Maintenance dual-signature (BLE-proximity + mesh-relayed, offline) + 6-pillar DePIN reward + signed-liveness anti-gaming + dynamic per-path relay micro-reward บน LoRa mesh เฝ้าระวังไฟป่า"** — ชุดนี้ใหม่และเฉพาะเจาะจงมาก แยกจาก B (อุปกรณ์โหนด) / C (Edge AI false-alarm) และผู้ใช้ (ฝั่ง Blockchain/Web3 = ผศ.ดร.วรจักร์ CoI2) เป็นผู้ประดิษฐ์ตัวจริง → **candidate D แข็งพอยื่นแยกได้**

## Links
- [[Architecture_LoRa_Mesh]] · [[nRF52840_Dual_Role]] · [[Roadmap_LoRa_Mesh]] · [[00_MOC]]
- **Implementation workspace:** `~/Dropbox/.../2026-04-21_FireWildPMUC/FireWild_DePIN/` — node+gateway firmware + Python server + Android webapp + shared/{protocol,registry,reward_model}.md
- FireWildPMUC: `~/Dropbox/.../2026-04-21_FireWildPMUC/` (สัญญา C05F690131 + petty patent B/C/D)
- SkillChain: `~/Dropbox/.../2026-03-27_SkillChain_RMUTL/skillchain-web3/` (contracts: TRPBToken/JobEscrow/SkillCredit)
