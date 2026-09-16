// meas.h — measurement mode สำหรับแคมเปญวัด propagation (เล่ม Sensors)
//
// ⚠️ ทุกอย่างในไฟล์นี้อยู่ใต้ -DFW_MEAS_MODE
//    build ปกติ (env: pro_micro_nrf52840) พฤติกรรมไม่เปลี่ยนเลยแม้แต่นิดเดียว
//    build วัด      (env: pro_micro_nrf52840_meas) จึงจะเปิด
//
// แก้ 3 อย่างที่ทำให้ชุดข้อมูลเดิมใช้ fit โมเดลไม่ได้:
//   (1) SNR ไม่เคยถูกอ่าน — GetPacketStatus อ่านมา 3 ไบต์แต่ใช้ไบต์เดียว
//   (2) ไม่มี log ฝั่งส่ง  — เฟรมที่หายไม่มีใครนับ → PDR ไม่มีตัวหารจริง
//   (3) **กำลังส่งปรับอัตโนมัติตาม RSSI (7→17 dBm)** — ทำให้ RSSI-vs-distance
//       กลายเป็นลูปป้อนกลับ ไม่ใช่ path loss ล้วน · โหมดวัดต้องตรึงกำลังส่ง
//
// schema ของ log ตรงกับ build _meas ของ FireWild_DePIN → สคริปต์วิเคราะห์ใช้ร่วมกันได้
#ifndef FW_MEAS_H
#define FW_MEAS_H

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

#ifdef FW_MEAS_MODE
#define MEAS_FREEZE_POWER 1   // โหมดวัด: ตรึงกำลังส่ง ไม่ให้ adaptive แตะ

// ---- PHY ที่ใช้จริงในเฟิร์มแวร์นี้ (lora_config.h) ----
#define MEAS_BW_KHZ   125
#define MEAS_CR       5
#define MEAS_SF_DEF   7
#define MEAS_TX_DEF   12          // dBm — ตรึงไว้ระหว่างวัด ไม่ให้ adaptive แตะ
#define MEAS_PL_MIN   19
#define MEAS_PL_DEF   64
#define MEAS_SFLIST_N 4

// ---- hook ที่ main.cpp ต้อง implement ----
bool meas_hw_set_sf(uint8_t sf);
bool meas_hw_set_power(int8_t dbm);
int  meas_hw_rssi_inst();
bool meas_hw_send(uint8_t frame_len);

// ---- context ----
static char    g_meas_run[16]  = "R0";
static char    g_meas_pos[16]  = "P0";
static uint8_t g_meas_self     = 0;
static uint8_t g_meas_sf       = MEAS_SF_DEF;
static int8_t  g_meas_txdbm    = MEAS_TX_DEF;
static uint8_t g_meas_pl       = MEAS_PL_DEF;
static bool    g_meas_quiet    = false;   // พักทราฟฟิกปกติระหว่างวัด
static uint8_t g_meas_sflist[MEAS_SFLIST_N] = {7, 9, 12, 0};
static uint8_t g_meas_sfn      = 3;

static inline void meas_set_self(uint8_t s){ g_meas_self = s; }

static inline void meas_header(){
  static bool done=false; if(done) return; done=true;
  Serial.println(F("# FireWild measurement log v1 (dir=TX|RX|NOISE)"));
  Serial.println(F("dir,run,pos,ts_ms,self,peer,seq,type,hop,sf,bw_khz,cr,tx_dbm,pl,ok,rssi,snr_db,srssi"));
}

// SNR จากชิปเป็น quarter-dB (int8/4) — พิมพ์ทศนิยม 2 ตำแหน่งโดยไม่ใช้ float printf
// q4 = -3 ต้องได้ "-0.75" ไม่ใช่ "0.75" จึงแยกเครื่องหมายก่อนหาร
static inline void meas_fmt_snr(char* out, size_t n, int16_t q4){
  const char* sg = (q4<0) ? "-" : "";
  int16_t a = (q4<0) ? (int16_t)(-q4) : q4;
  snprintf(out, n, "%s%d.%02d", sg, a/4, (a%4)*25);
}

static inline void meas_log_tx(uint8_t peer, uint32_t seq, char type,
                               uint8_t hop, uint8_t pl, bool ok){
  meas_header();
  Serial.printf("TX,%s,%s,%lu,%u,%u,%lu,%c,%u,%u,%u,%u,%d,%u,%u,,,\n",
                g_meas_run, g_meas_pos, (unsigned long)millis(), g_meas_self, peer,
                (unsigned long)seq, type?type:'?', hop,
                g_meas_sf, MEAS_BW_KHZ, MEAS_CR, (int)g_meas_txdbm, pl, ok?1u:0u);
}

static inline void meas_log_rx(uint8_t peer, uint32_t seq, char type, uint8_t hop,
                               uint8_t pl, bool crc_ok, int rssi, int16_t snr_q4, int srssi){
  meas_header();
  char snr[12]; meas_fmt_snr(snr,sizeof(snr),snr_q4);
  Serial.printf("RX,%s,%s,%lu,%u,%u,%lu,%c,%u,%u,%u,%u,,%u,%u,%d,%s,%d\n",
                g_meas_run, g_meas_pos, (unsigned long)millis(), g_meas_self, peer,
                (unsigned long)seq, type?type:'?', hop,
                g_meas_sf, MEAS_BW_KHZ, MEAS_CR, pl, crc_ok?1u:0u, rssi, snr, srssi);
}

static inline void meas_log_noise(int rssi_dbm){
  meas_header();
  Serial.printf("NOISE,%s,%s,%lu,%u,,,,,%u,%u,%u,,,,%d,,\n",
                g_meas_run, g_meas_pos, (unsigned long)millis(), g_meas_self,
                g_meas_sf, MEAS_BW_KHZ, MEAS_CR, rssi_dbm);
}

// ---- ToA (BW125, CR4/5, CRC on, explicit header) — จำนวนเต็มล้วน ----
static inline uint32_t meas_toa_ms(uint8_t pl, uint8_t sf){
  if(sf<5 || sf>12) return 0;
  uint32_t tsym = (1UL<<sf)*8UL;                 // us, BW125
  uint32_t pre  = (49UL*tsym)/4UL;               // (8 + 4.25) * Tsym
  int32_t num = 8L*pl - 4L*sf + 28L + 16L;
  int32_t de  = (sf>=11) ? 1 : 0;
  int32_t den = 4L*((int32_t)sf - 2L*de);
  int32_t ns  = 0;
  if(num>0 && den>0) ns = ((num+den-1)/den)*5L;  // CR 4/5
  return ((pre + (uint32_t)(8L+ns)*tsym) + 999UL)/1000UL;
}

static inline void meas_duty_check(uint32_t gap_ms, uint8_t pl, uint8_t sf){
  uint32_t toa=meas_toa_ms(pl,sf), need=toa*99UL;
  Serial.printf("# toa=%lums sf=%u pl=%uB duty1%%_gap=%lums\n",
                (unsigned long)toa, sf, pl, (unsigned long)need);
  if(gap_ms<need)
    Serial.printf("# WARN gap=%lums < duty1%% (%lums) — รอบนี้ไม่ได้คุม 1%%\n",
                  (unsigned long)gap_ms,(unsigned long)need);
}

static inline bool meas_apply_sf(uint8_t sf){
  if(!meas_hw_set_sf(sf)) return false;
  g_meas_sf=sf; return true;
}
static inline bool meas_apply_power(int8_t dbm){
  if(!meas_hw_set_power(dbm)) return false;
  g_meas_txdbm=dbm; return true;
}

// ---- burst + SF cycle ----
static uint32_t g_burst_left=0, g_burst_gap=0, g_burst_next=0;
static uint32_t g_cyc_dwell=0, g_cyc_t0=0, g_cyc_frames=0, g_cyc_sent=0, g_cyc_next=0;
static uint8_t  g_cyc_idx=0xFF;

static inline void meas_tick(){
  uint32_t now=millis();
  if(g_cyc_dwell){
    uint8_t idx=(uint8_t)(((now-g_cyc_t0)/g_cyc_dwell)%g_meas_sfn);
    if(idx!=g_cyc_idx){
      g_cyc_idx=idx; g_cyc_sent=0;
      meas_apply_sf(g_meas_sflist[idx]);
      Serial.printf("# cycle slot=%u sf=%u\n", idx, g_meas_sflist[idx]);
      if(g_cyc_frames) g_cyc_next = now + meas_toa_ms(g_meas_pl,g_meas_sflist[idx])/2 + 20;
    }
    if(g_cyc_frames && g_cyc_sent<g_cyc_frames && (int32_t)(now-g_cyc_next)>=0){
      uint32_t toa=meas_toa_ms(g_meas_pl,g_meas_sflist[g_cyc_idx]);
      if(meas_hw_send(g_meas_pl)) g_cyc_sent++; else g_cyc_sent=g_cyc_frames;
      uint32_t slot=g_cyc_dwell/(g_cyc_frames+1);
      g_cyc_next = now + (slot>toa ? slot : toa+20);
    }
  }
  if(g_burst_left && (int32_t)(now-g_burst_next)>=0){
    meas_hw_send(g_meas_pl);
    g_burst_left--; g_burst_next=now+g_burst_gap;
    if(!g_burst_left) Serial.println(F("# burst done"));
  }
}

static inline void meas_cmd_line(char* l){
  if(!strncmp(l,"RUN ",4)){ strncpy(g_meas_run,l+4,sizeof(g_meas_run)-1); g_meas_run[sizeof(g_meas_run)-1]=0;
      Serial.printf("# run=%s\n",g_meas_run); }
  else if(!strncmp(l,"POS ",4)){ strncpy(g_meas_pos,l+4,sizeof(g_meas_pos)-1); g_meas_pos[sizeof(g_meas_pos)-1]=0;
      Serial.printf("# pos=%s\n",g_meas_pos); }
  else if(!strncmp(l,"SF ",3)){ int v=atoi(l+3);
      Serial.printf(meas_apply_sf((uint8_t)v)?"# sf=%d\n":"# ERR sf=%d (5..12)\n",v); }
  else if(!strncmp(l,"PWR ",4)){ int v=atoi(l+4);
      Serial.printf(meas_apply_power((int8_t)v)?"# tx_dbm=%d\n":"# ERR pwr=%d (-9..22)\n",v); }
  else if(!strncmp(l,"PL ",3)){ int v=atoi(l+3);
      if(v<MEAS_PL_MIN||v>250) Serial.printf("# ERR pl=%d (%d..250)\n",v,MEAS_PL_MIN);
      else { g_meas_pl=(uint8_t)v; Serial.printf("# pl=%u\n",g_meas_pl); meas_duty_check(0,g_meas_pl,g_meas_sf);} }
  else if(!strncmp(l,"SFLIST ",7)){ uint8_t n=0; const char* p=l+7;
      while(*p && n<MEAS_SFLIST_N){ int v=atoi(p); if(v>=5&&v<=12) g_meas_sflist[n++]=(uint8_t)v;
        while(*p && *p!=',') p++; if(*p==',') p++; }
      if(n){ g_meas_sfn=n; Serial.printf("# sflist n=%u\n",n);} else Serial.println(F("# ERR sflist")); }
  else if(!strncmp(l,"SEND ",5)){ const char* p=l+5; long c=atol(p);
      while(*p && *p!=' ') p++; if(*p==' ') p++; long g=atol(p);
      if(c<=0) Serial.println(F("# ERR send <count> [gap_ms]"));
      else { if(g<=0) g=(long)meas_toa_ms(g_meas_pl,g_meas_sf)*99L;
             meas_duty_check((uint32_t)g,g_meas_pl,g_meas_sf);
             g_burst_left=(uint32_t)c; g_burst_gap=(uint32_t)g; g_burst_next=millis();
             Serial.printf("# burst n=%ld gap=%ldms\n",c,g); } }
  else if(!strncmp(l,"CYCLE ",6)){ const char* p=l+6; long d=atol(p);
      while(*p && *p!=' ') p++; if(*p==' ') p++; long f=atol(p);
      if(d<=0){ g_cyc_dwell=0; Serial.println(F("# cycle off")); }
      else { g_cyc_dwell=(uint32_t)d; g_cyc_frames=(uint32_t)(f>0?f:0);
             g_cyc_t0=millis(); g_cyc_idx=0xFF;
             Serial.printf("# cycle dwell=%ldms frames/slot=%ld — กด SYNC ทั้งสองฝั่ง\n",d,f);
             if(g_cyc_frames){ uint32_t w=0,wsf=0;
               for(uint8_t i=0;i<g_meas_sfn;i++){ uint32_t t=meas_toa_ms(g_meas_pl,g_meas_sflist[i]);
                 if(t>w){w=t; wsf=g_meas_sflist[i];} }
               uint32_t need=w*g_cyc_frames+100UL;
               if((uint32_t)d<need)
                 Serial.printf("# WARN dwell %ldms ใส่ %ld เฟรมที่ SF%lu ไม่พอ — ต้อง >= %lums\n",
                               d,f,(unsigned long)wsf,(unsigned long)need); } } }
  else if(!strcmp(l,"SYNC")){ g_cyc_t0=millis(); g_cyc_idx=0xFF; g_cyc_sent=0;
      Serial.println(F("# sync t0=0")); }
  else if(!strncmp(l,"QUIET ",6)){ g_meas_quiet=(atoi(l+6)!=0);
      Serial.printf("# quiet=%d\n", g_meas_quiet?1:0); }
  else if(!strncmp(l,"NOISE",5)){ int n=(l[5]==' ')?atoi(l+6):10;
      if(n<1)n=1; if(n>200)n=200;
      for(int i=0;i<n;i++){ meas_log_noise(meas_hw_rssi_inst()); delay(10);} }
  else if(!strcmp(l,"MEAS?")){
      Serial.printf("# run=%s pos=%s self=%u sf=%u tx_dbm=%d pl=%uB toa=%lums bw=%u cr=%u cycle=%lums quiet=%d\n",
                    g_meas_run,g_meas_pos,g_meas_self,g_meas_sf,(int)g_meas_txdbm,g_meas_pl,
                    (unsigned long)meas_toa_ms(g_meas_pl,g_meas_sf),MEAS_BW_KHZ,MEAS_CR,
                    (unsigned long)g_cyc_dwell,g_meas_quiet?1:0); }
  else if(!strcmp(l,"HELP")){
      Serial.println(F("# RUN <id> | POS <id> | SF <5-12> | PWR <-9..22> | PL <19-250>"));
      Serial.println(F("# SFLIST 7,9,12 | SEND <n> [gap_ms] | CYCLE <dwell_ms> <frames> | SYNC"));
      Serial.println(F("# NOISE [n] | QUIET <0|1> | MEAS? | HELP")); }
}

static inline void meas_cmd_poll(){
  static char line[64]; static uint8_t n=0;
  while(Serial.available()){
    char c=(char)Serial.read();
    if(c=='\r') continue;
    if(c!='\n'){ if(n<sizeof(line)-1) line[n++]=c; continue; }
    line[n]=0; n=0; if(line[0]) meas_cmd_line(line);
  }
  meas_tick();
}

#else   // ---------- ปิด measurement mode: หายไปจาก build ทั้งหมด ----------
#define MEAS_FREEZE_POWER 0
#define meas_set_self(a)                do{}while(0)
#define meas_log_tx(a,b,c,d,e,f)        do{}while(0)
#define meas_log_rx(a,b,c,d,e,f,g,h,i)  do{}while(0)
#define meas_log_noise(a)               do{}while(0)
#define meas_cmd_poll()                 do{}while(0)
#define g_meas_quiet                    false
#endif  // FW_MEAS_MODE

#endif  // FW_MEAS_H
