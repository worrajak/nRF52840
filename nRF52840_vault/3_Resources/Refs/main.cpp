/*
 * nRF52840 LoRa P2P Node — Relay Hop + LoRa (SX1262/RFM95) + BLE GPS
 * Node ID: 116
 *
 * Protocol: RadioHead-compatible headers + XOR encryption + CRC16
 * Packet: [TO=0xFF][FROM=116][ID][FLAGS=hop_count][encrypted_payload+CRC16]
 * Payload: N:116|S:seq|T:temp|H:hum|B:bat|LA:lat|LO:lon
 *
 * BLE: phone sends GPS coordinates → node includes in LoRa payload
 * OLED: status + distance/direction to other nodes
 * SPI: software bit-bang using nrf_gpio_* (n-able PINS_COUNT=34 bug)
 * LoRa: SX1262 (command-based SPI) or RFM95/SX1276 (register-based SPI)
 * BME280: optional — uses simulated values if not connected
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <stdarg.h>
#include <nrf_gpio.h>
#include <NimBLEDevice.h>
#include <math.h>
#include "rssi_classifier.h"

// ============ RADIO MODULE SELECTION ============
// PlatformIO defines one of these from custom_radio in platformio.ini.
// Fallback keeps direct Arduino builds usable.
#if !defined(USE_SX1262) && !defined(USE_RFM95)
#define USE_SX1262
#endif
#if defined(USE_SX1262) && defined(USE_RFM95)
#error "Select only one radio: USE_SX1262 or USE_RFM95"
#endif

// ============ PIN DEFINITIONS ============
#define STATUS_LED   15   // P0.15
#define UI_BUTTON_PIN 32  // P1.00 / fakeTec D6 (BTN), active LOW
#define UI_PAGE_COUNT 3
#define BUTTON_DEBOUNCE_MS 40
#define LORA_TX_POWER_DBM 12
#define CPU_IDLE_SLEEP_MS 10  // Yield to FreeRTOS System-ON idle; radio/OLED stay on
#ifndef LOW_POWER_IDLE
#define LOW_POWER_IDLE 0
#endif

// OLED I2C
#define OLED_SDA     36   // P1.04
#define OLED_SCL     11   // P0.11

// LoRa SPI — raw GPIO numbers (nrf_gpio_* functions)
// ปรับ pin ตามการต่อสายของแต่ละบอร์ด
#define LORA_MISO     2    // P0.02
#define LORA_SCK      43   // P1.11
#define LORA_MOSI     47   // P1.15
#define LORA_CS       45   // P1.13
#define LORA_RST      9    // P0.09

#ifdef USE_SX1262
#define LORA_IRQ      29   // P0.29 — SX1262 DIO1
#define LORA_BUSY     3    // P0.03 — SX1262 BUSY pin
#endif

#ifdef USE_RFM95
#define LORA_IRQ      29   // P0.29 — RFM95 DIO0
// RFM95 ไม่มี BUSY pin
#endif

// Battery
#define BATTERY_PIN  31   // P0.31
#define VBAT_MV_PER_LSB     0.73242188F
#define VBAT_DIVIDER_COMP   1.73F
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

// ============ NETWORK CONFIG ============
#ifndef NODE_ID
#define NODE_ID 119  // fallback when not built by PlatformIO
#endif
#if NODE_ID < 1 || NODE_ID > 254
#error "NODE_ID must be in range 1..254 (0 and 255 are reserved)"
#endif
#define THIS_NODE_ADDRESS  NODE_ID
#define BROADCAST_ADDRESS  0xFF
#define MAX_MESSAGE_LEN    251
#define TX_INTERVAL        120000UL  // Send this node's status every 2 minutes

// Crypto key (must match STM32 nodes)
static const char* CRYPTO_KEY = "1234567890000000";
#define CRYPTO_KEY_LEN 16

// ============ RELAY / MULTI-HOP CONFIG ============
#define MAX_HOPS              3
#define RELAY_MEMORY_SIZE     10
#define MESSAGE_QUEUE_SIZE    5
#define RSSI_RELAY_THRESHOLD  -100
#define RELAY_CACHE_TTL       300000  // 5 minutes

// FLAGS bits in RadioHead header byte [3]
#define FLAG_HOP_MASK         0x0F
#define FLAG_IS_ACK           (1 << 4)
#define FLAG_ACK_REQ          (1 << 5)

// RSSI-based forwarding delay table (best relay sends first)
#define RSSI_DELAY_GOOD       200    // ≥ -75 dBm
#define RSSI_DELAY_FAIR       700    // -75 to -90
#define RSSI_DELAY_WEAK       1500   // -90 to -105
#define RSSI_DELAY_POOR       3000   // < -105
#define RSSI_SOURCE_CLOSE_INIT -100  // initial threshold
#define RSSI_EMA_ALPHA_NUM    3     // EMA numerator   (α = 0.3)
#define RSSI_EMA_ALPHA_DEN    10    // EMA denominator

// ML mode toggle
bool mlMode = true;
uint8_t mlProbPct = 50;  // last ML probability in % (0-100)

// Path success rate (Bayesian)
#define PATH_STATS_SIZE       8
#define BAYES_PRIOR           1     // Laplace smoothing prior

// ============ BLE GPS CONFIG ============
#define BLE_GPS_SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define BLE_GPS_CHAR_UUID           "12345678-1234-1234-1234-123456789abd"
#define BLE_DATA_CHAR_UUID          "12345678-1234-1234-1234-123456789abe"
#define MAX_TRACKED_NODES           8
#define NODE_POSITION_TTL           120000  // 2 min stale timeout

// ============ RSSI DATA LOGGING (for ML training) ============
#define RSSI_LOG_SIZE            512   // circular buffer, ~8KB RAM
#define RSSI_LOG_FLAG_ACK        0x80
#define RSSI_LOG_FLAG_RELAYED    0x40
#define RSSI_LOG_FLAG_CRC_FAIL   0x10  // CRC failed, header-only log

struct RssiSample {
    uint32_t timestamp;   // ms since boot
    uint8_t  nodeId;      // THIS_NODE (who logged this)
    uint8_t  srcNode;     // original source (from payload or header)
    uint8_t  rhFrom;      // who actually sent this packet (relay or origin)
    uint8_t  hopCount;    // 0-3
    int16_t  rssi;        // dBm
    int16_t  snr;         // dB (if available)
    uint8_t  decision;    // RELAY_STATUS_* enum
    uint8_t  flags;       // ACK/RELAYED/CRC_FAIL
} __attribute__((packed));

#define RELAY_STATUS_IDLE     0
#define RELAY_STATUS_QUEUED   1
#define RELAY_STATUS_SENT     2
#define RELAY_STATUS_CANCEL   3
#define RELAY_STATUS_NO_RELAY 4
#define RELAY_STATUS_DROP_DUP 5
#define RELAY_STATUS_DROP_Q   6
#define RELAY_STATUS_DROP_HOP 7
#define RELAY_STATUS_DROP_CRC 8
#define RELAY_STATUS_DROP_ECHO 9
#define RELAY_STATUS_ACK_RCVD 10

RssiSample rssiLog[RSSI_LOG_SIZE];
uint16_t rssiLogHead = 0;
uint16_t rssiLogCount = 0;
#ifndef DEG_TO_RAD
#define DEG_TO_RAD  0.017453292519943295
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG  57.29577951308232
#endif
#define EARTH_RADIUS_M              6371000.0

// ============ GLOBALS ============
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_BME280 bme;

float batteryVoltage = 0;
uint16_t batteryMV = 3300;
bool oled_ok = false;
bool lora_ok = false;
bool bme_ok = false;
bool ble_ok = false;
bool ble_connected = false;
int16_t temperature = 25;  // simulated default
int16_t humidity = 50;     // simulated default

// GPS from phone via BLE
double myLat = 0.0;
double myLon = 0.0;
float myHeading = -1;  // -1 = no heading available
bool gpsValid = false;
uint32_t gpsLastUpdate = 0;

// Tracked node positions (from received LoRa packets)
struct NodePosition {
    uint8_t nodeId;
    double lat;
    double lon;
    int16_t rssi;
    uint32_t lastSeen;
    bool valid;
} trackedNodes[MAX_TRACKED_NODES];

// LoRa state
volatile bool lora_rx_flag = false;
uint8_t txSequenceNum = 0;
uint8_t rhMsgId = 0;

// OLED UI and latest radio activity
uint8_t uiPage = 0;
bool lastOwnTxOk = false;
bool ownTxAttempted = false;
bool lastRxValid = false;
uint8_t lastRxNode = 0;
uint8_t lastRxFrom = 0;
uint8_t lastRxHop = 0;
int16_t lastRxRssi = 0;
int16_t lastRxTemp = 0;
int16_t lastRxHum = 0;
uint16_t lastRxBat = 0;
char lastRelayStatus[11] = "IDLE";

// Relay: duplicate detection cache
struct RelayCache {
    uint8_t sourceNode;
    uint8_t sequenceNum;
    uint32_t timestamp;
} relayMemory[RELAY_MEMORY_SIZE];
uint8_t relayCacheIndex = 0;

// Relay: message queue for delayed forwarding
struct MessageQueueEntry {
    uint8_t data[MAX_MESSAGE_LEN + 4];
    uint8_t len;
    uint32_t sendTime;
    uint8_t srcNode;   // for cancel-on-heard
    uint8_t seqNum;    // for cancel-on-heard
    bool inUse;
} messageQueue[MESSAGE_QUEUE_SIZE];

// Adaptive RSSI threshold (EMA)
int16_t rssiSourceClose = RSSI_SOURCE_CLOSE_INIT;
uint32_t rssiSampleCount = 0;

// Path success rate (Bayesian)
struct PathStat {
    uint8_t nodeId;
    uint32_t successCount;  // heard relay/ACK back = success
    uint32_t totalCount;    // total packets from this source
} pathStats[PATH_STATS_SIZE];

// ============ DEBUG OUTPUT SYSTEM ============
#define DBG_LINES 5
#define DBG_COLS  22

char dbgBuf[DBG_LINES][DBG_COLS];
uint8_t dbgIdx = 0;

void dbg(const char* msg) {
    Serial.println(msg);
    strncpy(dbgBuf[dbgIdx], msg, DBG_COLS - 1);
    dbgBuf[dbgIdx][DBG_COLS - 1] = '\0';
    dbgIdx = (dbgIdx + 1) % DBG_LINES;
}

void dbgf(const char* fmt, ...) {
    char tmp[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    dbg(tmp);
}

// Forward declarations for GPS functions defined later
double haversineDistance(double lat1, double lon1, double lat2, double lon2);
float bearingTo(double lat1, double lon1, double lat2, double lon2);
const char* bearingToCompass(float bearing);
NodePosition* findNearestNode();
uint8_t pathSuccessProbability(uint8_t nodeId);

// dtostrf polyfill for ARM GCC
#ifndef dtostrf
static char* dtostrf(double val, signed char width, unsigned char prec, char* s) {
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%%d.%df", width, prec);
    sprintf(s, fmt, val);
    return s;
}
#endif

// Draw arrow pointing in a direction (relative bearing)
void drawArrow(int cx, int cy, int r, float angleDeg) {
    float rad = angleDeg * DEG_TO_RAD;
    // Arrow tip
    int tx = cx + (int)(r * sin(rad));
    int ty = cy - (int)(r * cos(rad));
    // Arrow tail
    int bx = cx - (int)((r * 0.4) * sin(rad));
    int by = cy + (int)((r * 0.4) * cos(rad));
    // Arrow wings
    float wingAngle = 0.5f;
    int w1x = cx - (int)((r * 0.5) * sin(rad - wingAngle));
    int w1y = cy + (int)((r * 0.5) * cos(rad - wingAngle));
    int w2x = cx - (int)((r * 0.5) * sin(rad + wingAngle));
    int w2y = cy + (int)((r * 0.5) * cos(rad + wingAngle));
    display.drawLine(tx, ty, w1x, w1y, SSD1306_WHITE);
    display.drawLine(tx, ty, w2x, w2y, SSD1306_WHITE);
    display.drawLine(bx, by, tx, ty, SSD1306_WHITE);
}

void updateOLED() {
    if (!oled_ok) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    char line[24];
    display.setCursor(0, 0);
    snprintf(line, sizeof(line), "%d/3 ", uiPage + 1);
    display.print(line);

    if (uiPage == 0) {
        display.println(F("THIS NODE"));
        display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
        display.setCursor(0, 13);
        snprintf(line, sizeof(line), "Node ID:%d LoRa:%s", THIS_NODE_ADDRESS,
                 lora_ok ? "OK" : "FAIL");
        display.println(line);
        snprintf(line, sizeof(line), "Temp:%dC %s", temperature,
                 bme_ok ? "" : "(SIM)");
        display.println(line);
        snprintf(line, sizeof(line), "Hum :%d%%", humidity);
        display.println(line);
        snprintf(line, sizeof(line), "TX#:%d Pwr:%+ddBm", txSequenceNum,
                 LORA_TX_POWER_DBM);
        display.println(line);
        snprintf(line, sizeof(line), "Last TX:%s",
                 !ownTxAttempted ? "--" : (lastOwnTxOk ? "SENT" : "DROP"));
        display.println(line);
        if (mlMode) {
            snprintf(line, sizeof(line), "ML:%d%% Th:%d", mlProbPct, rssiSourceClose);
        } else {
            snprintf(line, sizeof(line), "Rule Th:%d", rssiSourceClose);
        }
        display.println(line);
    } else if (uiPage == 1) {
        display.println(F("LAST RX / RELAY"));
        display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
        display.setCursor(0, 13);
        if (!lastRxValid) {
            display.println(F("Waiting for packet..."));
            display.println(F("RX RSSI: --"));
            display.println(F("Hop:-- From:--"));
            display.println(F("Relay: IDLE"));
        } else {
            snprintf(line, sizeof(line), "ID:%d From:%d", lastRxNode, lastRxFrom);
            display.println(line);
            snprintf(line, sizeof(line), "Hop:%d RX:%ddBm", lastRxHop, lastRxRssi);
            display.println(line);
            snprintf(line, sizeof(line), "T:%dC H:%d%%", lastRxTemp, lastRxHum);
            display.println(line);
            snprintf(line, sizeof(line), "Relay:%s", lastRelayStatus);
            display.println(line);
            if (lastRxNode != 0) {
                uint8_t prob = pathSuccessProbability(lastRxNode);
                if (mlMode) {
                    snprintf(line, sizeof(line), "ML:%d%% Path:%d%%", mlProbPct, prob);
                } else {
                    snprintf(line, sizeof(line), "Path:%d%% Th:%d", prob, rssiSourceClose);
                }
            } else {
                if (mlMode) {
                    snprintf(line, sizeof(line), "ML:%d%% Th:%d", mlProbPct, rssiSourceClose);
                } else {
                    snprintf(line, sizeof(line), "Thresh:%ddBm", rssiSourceClose);
                }
            }
            display.println(line);
            snprintf(line, sizeof(line), "TX:%+ddBm RX:%ddBm",
                     LORA_TX_POWER_DBM, lastRxRssi);
            display.println(line);
        }
    } else {
        display.println(F("DEVICE STATUS"));
        display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
        display.setCursor(0, 13);
        snprintf(line, sizeof(line), "Battery: %.2fV", batteryVoltage);
        display.println(line);
        snprintf(line, sizeof(line), "LoRa:%s OLED:%s", lora_ok ? "OK" : "ERR",
                 oled_ok ? "OK" : "ERR");
        display.println(line);
        snprintf(line, sizeof(line), "BLE:%s %s", ble_ok ? "OK" : "ERR",
                 ble_connected ? "LINK" : "WAIT");
        display.println(line);
        snprintf(line, sizeof(line), "BME:%s GPS:%s", bme_ok ? "OK" : "SIM",
                 gpsValid ? "FIX" : "NO");
        display.println(line);
#if LOW_POWER_IDLE
        display.println(F("Power:IDLE RX-ON"));
#else
        display.println(F("Power:RUN RX-ON"));
#endif
        snprintf(line, sizeof(line), "Uptime:%lus", millis() / 1000UL);
        display.println(line);
    }

    display.display();
}

void handleUIButton() {
    static bool rawState = false;
    static bool stableState = false;
    static uint32_t changedAt = 0;
    bool pressed = (nrf_gpio_pin_read(UI_BUTTON_PIN) == 0);

    if (pressed != rawState) {
        rawState = pressed;
        changedAt = millis();
    }
    if ((millis() - changedAt) >= BUTTON_DEBOUNCE_MS &&
        stableState != rawState) {
        stableState = rawState;
        if (stableState) {
            uiPage = (uiPage + 1) % UI_PAGE_COUNT;
            Serial.print(F("[UI] Button pressed, page "));
            Serial.println(uiPage + 1);
            updateOLED();
        }
    }
}

// ============ GPS MATH ============
double haversineDistance(double lat1, double lon1, double lat2, double lon2) {
    double dLat = (lat2 - lat1) * DEG_TO_RAD;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
               sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return EARTH_RADIUS_M * c;
}

// Returns bearing in degrees (0=N, 90=E, 180=S, 270=W)
float bearingTo(double lat1, double lon1, double lat2, double lon2) {
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    double y = sin(dLon) * cos(lat2 * DEG_TO_RAD);
    double x = cos(lat1 * DEG_TO_RAD) * sin(lat2 * DEG_TO_RAD) -
               sin(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * cos(dLon);
    float brng = (float)(atan2(y, x) * RAD_TO_DEG);
    return fmod(brng + 360.0f, 360.0f);
}

const char* bearingToCompass(float bearing) {
    static const char* dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
    int idx = (int)((bearing + 22.5f) / 45.0f) % 8;
    return dirs[idx];
}

// Find nearest tracked node with valid GPS
NodePosition* findNearestNode() {
    NodePosition* nearest = nullptr;
    double minDist = 999999999.0;
    uint32_t now = millis();
    for (int i = 0; i < MAX_TRACKED_NODES; i++) {
        if (!trackedNodes[i].valid) continue;
        if ((now - trackedNodes[i].lastSeen) > NODE_POSITION_TTL) continue;
        if (trackedNodes[i].lat == 0.0 && trackedNodes[i].lon == 0.0) continue;
        double d = haversineDistance(myLat, myLon, trackedNodes[i].lat, trackedNodes[i].lon);
        if (d < minDist) {
            minDist = d;
            nearest = &trackedNodes[i];
        }
    }
    return nearest;
}

void updateTrackedNode(uint8_t nodeId, double lat, double lon, int16_t rssi) {
    // Find existing or oldest slot
    int slot = -1;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < MAX_TRACKED_NODES; i++) {
        if (trackedNodes[i].nodeId == nodeId && trackedNodes[i].valid) {
            slot = i;
            break;
        }
        if (!trackedNodes[i].valid) {
            slot = i;
            break;
        }
        if (trackedNodes[i].lastSeen < oldest) {
            oldest = trackedNodes[i].lastSeen;
            slot = i;
        }
    }
    if (slot < 0) slot = 0;
    trackedNodes[slot].nodeId = nodeId;
    trackedNodes[slot].lat = lat;
    trackedNodes[slot].lon = lon;
    trackedNodes[slot].rssi = rssi;
    trackedNodes[slot].lastSeen = millis();
    trackedNodes[slot].valid = true;
}

// ============ BLE GPS SERVICE ============
class GpsCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) {
        std::string val = pChar->getValue();
        // Expected format: "lat,lon" or "lat,lon,heading"
        // e.g. "13.7563,100.5018" or "13.7563,100.5018,45.2"
        if (val.length() < 5) return;

        char buf[64];
        strncpy(buf, val.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char* tok = strtok(buf, ",");
        if (!tok) return;
        double lat = atof(tok);

        tok = strtok(NULL, ",");
        if (!tok) return;
        double lon = atof(tok);

        tok = strtok(NULL, ",");
        if (tok) {
            myHeading = atof(tok);
        }

        if (lat != 0.0 && lon != 0.0) {
            myLat = lat;
            myLon = lon;
            gpsValid = true;
            gpsLastUpdate = millis();
            dbgf("BLE GPS:%.4f,%.4f", myLat, myLon);
            updateOLED();
        }
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
        ble_connected = true;
        dbg("BLE: connected");
        updateOLED();
    }
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
        ble_connected = false;
        dbg("BLE: disconnected");
        NimBLEDevice::startAdvertising();
        updateOLED();
    }
};

static GpsCallbacks gpsCallbacks;
static ServerCallbacks serverCallbacks;

// BLE Data characteristic callback — triggers DATADUMP on write
class DataCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) {
        std::string val = pChar->getValue();
        if (val == "DUMP") {
            // Build CSV string and set as characteristic value
            // Max BLE MTU is ~512, so we chunk
            char buf[256];
            int pos = 0;
            uint16_t count = (rssiLogCount < RSSI_LOG_SIZE) ? rssiLogCount : RSSI_LOG_SIZE;
            uint16_t start = (rssiLogCount < RSSI_LOG_SIZE) ? 0 :
                             (rssiLogHead % RSSI_LOG_SIZE);
            pos += snprintf(buf + pos, sizeof(buf) - pos, "ts,node,src,from,hop,rssi,snr,decision,flags\n");
            for (uint16_t i = 0; i < count && pos < 240; i++) {
                uint16_t idx = (start + i) % RSSI_LOG_SIZE;
                RssiSample* s = &rssiLog[idx];
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                                "%lu,%u,%u,%u,%u,%d,%d,%u,%02X\n",
                                s->timestamp, s->nodeId, s->srcNode, s->rhFrom,
                                s->hopCount, s->rssi, s->snr, s->decision, s->flags);
            }
            pChar->setValue((uint8_t*)buf, pos);
            dbgf("BLE: sent %d bytes RSSI log", pos);
        } else if (val == "CLEAR") {
            rssiLogHead = 0;
            rssiLogCount = 0;
            pChar->setValue("CLEARED");
            dbg("BLE: RSSI log cleared");
        }
    }
};
static DataCallbacks dataCallbacks;

bool initBLE() {
    dbg("BLE: starting...");
    updateOLED();

    char name[20];
    snprintf(name, sizeof(name), "LoRa-N%d", THIS_NODE_ADDRESS);

    if (!NimBLEDevice::init(name)) {
        dbg("BLE: init FAIL");
        return false;
    }
    NimBLEDevice::setPower(3);  // +3 dBm

    // Disable bonding/pairing — prevents "peer remove pairing" errors
    NimBLEDevice::setSecurityAuth(false, false, false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::deleteAllBonds();

    dbgf("BLE: name=%s", name);

    NimBLEServer* pServer = NimBLEDevice::createServer();
    if (!pServer) {
        dbg("BLE: server FAIL");
        return false;
    }
    pServer->setCallbacks(&serverCallbacks);

    NimBLEService* pService = pServer->createService(BLE_GPS_SERVICE_UUID);
    NimBLECharacteristic* pChar = pService->createCharacteristic(
        BLE_GPS_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pChar->setCallbacks(&gpsCallbacks);

    // RSSI data export characteristic (READ + WRITE for trigger)
    NimBLECharacteristic* pDataChar = pService->createCharacteristic(
        BLE_DATA_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    pDataChar->setCallbacks(&dataCallbacks);
    pDataChar->setValue("READY");

    pService->start();

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->setName(name);
    pAdv->addServiceUUID(BLE_GPS_SERVICE_UUID);
    pAdv->enableScanResponse(true);
    if (!pAdv->start()) {
        dbg("BLE: adv FAIL");
        return false;
    }

    dbg("BLE: advertising");
    return true;
}

// ============ nRF GPIO HELPERS ============
static inline void gpioSet(uint32_t pin)   { nrf_gpio_pin_set(pin); }
static inline void gpioClear(uint32_t pin) { nrf_gpio_pin_clear(pin); }
static inline void gpioWrite(uint32_t pin, bool val) {
    if (val) nrf_gpio_pin_set(pin); else nrf_gpio_pin_clear(pin);
}
static inline uint32_t gpioRead(uint32_t pin) { return nrf_gpio_pin_read(pin); }
static inline void gpioOutput(uint32_t pin) { nrf_gpio_cfg_output(pin); }
static inline void gpioInput(uint32_t pin) {
    nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_NOPULL);
}

// ============ SOFTWARE SPI (bit-bang) ============
uint8_t swSpiTransfer(uint8_t data) {
    uint8_t recv = 0;
    for (int i = 7; i >= 0; i--) {
        gpioWrite(LORA_MOSI, (data >> i) & 1);
        delayMicroseconds(2);
        gpioSet(LORA_SCK);
        delayMicroseconds(2);
        if (gpioRead(LORA_MISO)) recv |= (1 << i);
        gpioClear(LORA_SCK);
        delayMicroseconds(2);
    }
    return recv;
}

// ============================================================
//  RADIO DRIVER — SX1262 (command-based SPI)
// ============================================================
#ifdef USE_SX1262

bool sx1262WaitBusy(uint32_t timeoutMs = 1000) {
    uint32_t start = millis();
    while (gpioRead(LORA_BUSY)) {
        if (millis() - start > timeoutMs) return false;
        delayMicroseconds(100);
    }
    return true;
}

void sx1262WriteCommand(uint8_t opcode, const uint8_t* params, uint8_t numParams) {
    sx1262WaitBusy();
    gpioClear(LORA_CS);
    delayMicroseconds(5);
    swSpiTransfer(opcode);
    for (uint8_t i = 0; i < numParams; i++) {
        swSpiTransfer(params[i]);
    }
    delayMicroseconds(5);
    gpioSet(LORA_CS);
    delayMicroseconds(5);
}

void sx1262ReadCommand(uint8_t opcode, uint8_t* result, uint8_t numBytes) {
    sx1262WaitBusy();
    gpioClear(LORA_CS);
    delayMicroseconds(5);
    swSpiTransfer(opcode);
    swSpiTransfer(0x00);  // NOP status byte
    for (uint8_t i = 0; i < numBytes; i++) {
        result[i] = swSpiTransfer(0x00);
    }
    delayMicroseconds(5);
    gpioSet(LORA_CS);
    delayMicroseconds(5);
}

void sx1262WriteRegister(uint16_t addr, uint8_t value) {
    sx1262WaitBusy();
    gpioClear(LORA_CS);
    delayMicroseconds(5);
    swSpiTransfer(0x0D);  // WriteRegister opcode
    swSpiTransfer((addr >> 8) & 0xFF);
    swSpiTransfer(addr & 0xFF);
    swSpiTransfer(value);
    delayMicroseconds(5);
    gpioSet(LORA_CS);
    delayMicroseconds(5);
}

uint8_t sx1262ReadRegister(uint16_t addr) {
    sx1262WaitBusy();
    gpioClear(LORA_CS);
    delayMicroseconds(5);
    swSpiTransfer(0x1D);  // ReadRegister opcode
    swSpiTransfer((addr >> 8) & 0xFF);
    swSpiTransfer(addr & 0xFF);
    swSpiTransfer(0x00);  // NOP status
    uint8_t val = swSpiTransfer(0x00);
    delayMicroseconds(5);
    gpioSet(LORA_CS);
    delayMicroseconds(5);
    return val;
}

void sx1262WriteBuffer(uint8_t offset, const uint8_t* data, uint8_t len) {
    sx1262WaitBusy();
    gpioClear(LORA_CS);
    delayMicroseconds(5);
    swSpiTransfer(0x0E);  // WriteBuffer opcode
    swSpiTransfer(offset);
    for (uint8_t i = 0; i < len; i++) {
        swSpiTransfer(data[i]);
    }
    delayMicroseconds(5);
    gpioSet(LORA_CS);
    delayMicroseconds(5);
}

void sx1262ReadBuffer(uint8_t offset, uint8_t* data, uint8_t len) {
    sx1262WaitBusy();
    gpioClear(LORA_CS);
    delayMicroseconds(5);
    swSpiTransfer(0x1E);  // ReadBuffer opcode
    swSpiTransfer(offset);
    swSpiTransfer(0x00);  // NOP status
    for (uint8_t i = 0; i < len; i++) {
        data[i] = swSpiTransfer(0x00);
    }
    delayMicroseconds(5);
    gpioSet(LORA_CS);
    delayMicroseconds(5);
}

void sx1262SetStandby() {
    uint8_t p = 0x00;  // STDBY_RC
    sx1262WriteCommand(0x80, &p, 1);
}

void sx1262ClearIrqStatus() {
    uint8_t p[2] = {0xFF, 0xFF};
    sx1262WriteCommand(0x02, p, 2);
}

uint16_t sx1262GetIrqStatus() {
    uint8_t buf[2];
    sx1262ReadCommand(0x12, buf, 2);
    return ((uint16_t)buf[0] << 8) | buf[1];
}

#endif // USE_SX1262

// ============================================================
//  RADIO DRIVER — RFM95/SX1276 (register-based SPI)
// ============================================================
#ifdef USE_RFM95

// RFM95 register addresses
#define RFM95_REG_FIFO          0x00
#define RFM95_REG_OPMODE        0x01
#define RFM95_REG_FRF_MSB       0x06
#define RFM95_REG_FRF_MID       0x07
#define RFM95_REG_FRF_LSB       0x08
#define RFM95_REG_PA_CONFIG     0x09
#define RFM95_REG_OCP           0x0B
#define RFM95_REG_LNA           0x0C
#define RFM95_REG_FIFO_ADDR_PTR 0x0D
#define RFM95_REG_FIFO_TX_BASE  0x0E
#define RFM95_REG_FIFO_RX_BASE  0x0F
#define RFM95_REG_FIFO_RX_CURR  0x10
#define RFM95_REG_IRQ_FLAGS     0x12
#define RFM95_REG_RX_NB_BYTES   0x13
#define RFM95_REG_PKT_SNR       0x19
#define RFM95_REG_PKT_RSSI      0x1A
#define RFM95_REG_MODEM_CFG1    0x1D
#define RFM95_REG_MODEM_CFG2    0x1E
#define RFM95_REG_SYMB_TIMEOUT  0x1F
#define RFM95_REG_PREAMBLE_MSB  0x20
#define RFM95_REG_PREAMBLE_LSB  0x21
#define RFM95_REG_PAYLOAD_LEN   0x22
#define RFM95_REG_MODEM_CFG3    0x26
#define RFM95_REG_SYNC_WORD     0x39
#define RFM95_REG_DIO_MAPPING1  0x40
#define RFM95_REG_VERSION       0x42
#define RFM95_REG_PA_DAC        0x4D

// OpMode values (LoRa mode = bit 7 set)
#define RFM95_MODE_SLEEP        0x80
#define RFM95_MODE_STANDBY      0x81
#define RFM95_MODE_TX           0x83
#define RFM95_MODE_RX_CONT      0x85

// IRQ flags
#define RFM95_IRQ_TX_DONE       0x08
#define RFM95_IRQ_RX_DONE       0x40
#define RFM95_IRQ_CRC_ERROR     0x20

void rfm95WriteReg(uint8_t addr, uint8_t value) {
    gpioClear(LORA_CS);
    delayMicroseconds(5);
    swSpiTransfer(addr | 0x80);  // bit 7 = write
    swSpiTransfer(value);
    delayMicroseconds(5);
    gpioSet(LORA_CS);
    delayMicroseconds(5);
}

uint8_t rfm95ReadReg(uint8_t addr) {
    gpioClear(LORA_CS);
    delayMicroseconds(5);
    swSpiTransfer(addr & 0x7F);  // bit 7 = 0 for read
    uint8_t val = swSpiTransfer(0x00);
    delayMicroseconds(5);
    gpioSet(LORA_CS);
    delayMicroseconds(5);
    return val;
}

void rfm95WriteBurst(uint8_t addr, const uint8_t* data, uint8_t len) {
    gpioClear(LORA_CS);
    delayMicroseconds(5);
    swSpiTransfer(addr | 0x80);
    for (uint8_t i = 0; i < len; i++) {
        swSpiTransfer(data[i]);
    }
    delayMicroseconds(5);
    gpioSet(LORA_CS);
    delayMicroseconds(5);
}

void rfm95ReadBurst(uint8_t addr, uint8_t* data, uint8_t len) {
    gpioClear(LORA_CS);
    delayMicroseconds(5);
    swSpiTransfer(addr & 0x7F);
    for (uint8_t i = 0; i < len; i++) {
        data[i] = swSpiTransfer(0x00);
    }
    delayMicroseconds(5);
    gpioSet(LORA_CS);
    delayMicroseconds(5);
}

#endif // USE_RFM95

// ============ ENCRYPTION (XOR — matches STM32) ============
void xorEncrypt(const char* plaintext, int len, uint8_t* output) {
    for (int i = 0; i < len; i++) {
        output[i] = plaintext[i] ^ CRYPTO_KEY[i % CRYPTO_KEY_LEN];
    }
}

void xorDecrypt(const uint8_t* ciphertext, int len, char* output) {
    for (int i = 0; i < len; i++) {
        output[i] = ciphertext[i] ^ CRYPTO_KEY[i % CRYPTO_KEY_LEN];
    }
    output[len] = '\0';
}

// ============ CRC16 (matches STM32) ============
uint16_t calculateCRC16(const uint8_t* data, uint8_t length) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// ============ MODEM MODES ============
#ifdef USE_SX1262
void setModeRx() {
    sx1262ClearIrqStatus();
    uint8_t p[3] = {0xFF, 0xFF, 0xFF};  // continuous RX
    sx1262WriteCommand(0x82, p, 3);
}
void setModeSleep() {
    uint8_t p = 0x00;
    sx1262WriteCommand(0x84, &p, 1);
}
#endif

#ifdef USE_RFM95
void setModeRx() {
    rfm95WriteReg(RFM95_REG_IRQ_FLAGS, 0xFF);  // clear IRQ
    rfm95WriteReg(RFM95_REG_FIFO_ADDR_PTR, 0x00);
    rfm95WriteReg(RFM95_REG_OPMODE, RFM95_MODE_RX_CONT);
}
void setModeSleep() {
    rfm95WriteReg(RFM95_REG_OPMODE, RFM95_MODE_SLEEP);
}
#endif

// DIO interrupt handler (SX1262=DIO1, RFM95=DIO0)
void lora_isr() {
    lora_rx_flag = true;
}

// ============ RELAY: DUPLICATE DETECTION ============
bool isMessageDuplicate(uint8_t sourceNode, uint8_t seqNum) {
    uint32_t now = millis();
    for (int i = 0; i < RELAY_MEMORY_SIZE; i++) {
        if (relayMemory[i].sourceNode == sourceNode &&
            relayMemory[i].sequenceNum == seqNum &&
            (now - relayMemory[i].timestamp) < RELAY_CACHE_TTL) {
            return true;
        }
    }
    return false;
}

void addToRelayCache(uint8_t sourceNode, uint8_t seqNum) {
    relayMemory[relayCacheIndex].sourceNode = sourceNode;
    relayMemory[relayCacheIndex].sequenceNum = seqNum;
    relayMemory[relayCacheIndex].timestamp = millis();
    relayCacheIndex = (relayCacheIndex + 1) % RELAY_MEMORY_SIZE;
}

// ============ RELAY: MESSAGE QUEUE ============
// RSSI-based forwarding delay: best relay (highest RSSI) sends first
static uint32_t rssiToDelayMs(int16_t rssi) {
    if      (rssi >= -75)  return RSSI_DELAY_GOOD;
    else if (rssi >= -90)  return RSSI_DELAY_FAIR;
    else if (rssi >= -105) return RSSI_DELAY_WEAK;
    else                   return RSSI_DELAY_POOR;
}

void addToMessageQueue(const uint8_t* packet, uint8_t len,
                       uint8_t srcNode, uint8_t seqNum, int16_t rssi) {
    for (int i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
        if (!messageQueue[i].inUse) {
            memcpy(messageQueue[i].data, packet, len);
            messageQueue[i].len = len;
            messageQueue[i].srcNode = srcNode;
            messageQueue[i].seqNum = seqNum;
            uint32_t delayMs = rssiToDelayMs(rssi);
            messageQueue[i].sendTime = millis() + delayMs;
            messageQueue[i].inUse = true;
            strncpy(lastRelayStatus, "QUEUED", sizeof(lastRelayStatus));
            dbgf("RELAY Q N:%d S:%d +%lums RSSI:%d",
                 srcNode, seqNum, delayMs, rssi);
            return;
        }
    }
    strncpy(lastRelayStatus, "DROP-Q", sizeof(lastRelayStatus));
    dbg("RELAY Q full!");
}

// Cancel-on-heard: if we hear another relay forward the same packet, cancel ours
void cancelFromQueue(uint8_t srcNode, uint8_t seqNum) {
    for (int i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
        if (messageQueue[i].inUse &&
            messageQueue[i].srcNode == srcNode &&
            messageQueue[i].seqNum == seqNum) {
            messageQueue[i].inUse = false;
            strncpy(lastRelayStatus, "CANCEL", sizeof(lastRelayStatus));
            dbgf("RELAY CANCEL N:%d S:%d (heard other relay)", srcNode, seqNum);
            return;
        }
    }
}

// ============ ADAPTIVE RSSI THRESHOLD (EMA) ============
// Exponential Moving Average: adjusts RSSI_SOURCE_CLOSE based on ambient noise
// α = RSSI_EMA_ALPHA_NUM / RSSI_EMA_ALPHA_DEN (e.g. 3/10 = 0.3)
void updateRssiThreshold(int16_t rssi) {
    rssiSampleCount++;
    if (rssiSampleCount == 1) {
        rssiSourceClose = rssi;
    } else {
        // EMA: new = (1-α)*old + α*sample
        int32_t ema = (int32_t)rssiSourceClose * (RSSI_EMA_ALPHA_DEN - RSSI_EMA_ALPHA_NUM)
                    + (int32_t)rssi * RSSI_EMA_ALPHA_NUM;
        rssiSourceClose = (int16_t)(ema / RSSI_EMA_ALPHA_DEN);
    }
    // Clamp: never go above -80 (too aggressive) or below -120 (too permissive)
    if (rssiSourceClose > -80) rssiSourceClose = -80;
    if (rssiSourceClose < -120) rssiSourceClose = -120;
}

// ============ PATH SUCCESS RATE (Bayesian) ============
// Tracks how often packets from each source node get relayed/ACKed
// Uses Laplace smoothing: P(success) = (success + 1) / (total + 2)

PathStat* getPathStat(uint8_t nodeId) {
    // Find existing or empty slot
    int slot = -1;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < PATH_STATS_SIZE; i++) {
        if (pathStats[i].nodeId == nodeId) return &pathStats[i];
        if (pathStats[i].totalCount < oldest) {
            oldest = pathStats[i].totalCount;
            slot = i;
        }
    }
    if (slot < 0) slot = 0;
    pathStats[slot].nodeId = nodeId;
    pathStats[slot].successCount = 0;
    pathStats[slot].totalCount = 0;
    return &pathStats[slot];
}

void recordPathEvent(uint8_t nodeId, bool success) {
    PathStat* ps = getPathStat(nodeId);
    if (success) {
        ps->successCount++;
    } else {
        ps->totalCount++;  // total event (packet seen)
    }
}

// Record that we saw a packet from this source (total count)
void recordPathSeen(uint8_t nodeId) {
    PathStat* ps = getPathStat(nodeId);
    ps->totalCount++;
}

// Record that this source's packet was successfully relayed/ACKed
void recordPathSuccess(uint8_t nodeId) {
    PathStat* ps = getPathStat(nodeId);
    ps->successCount++;
    // Ensure totalCount >= successCount
    if (ps->successCount > ps->totalCount) ps->totalCount = ps->successCount;
}

// Returns probability 0-100 that a packet from this node will reach GW
uint8_t pathSuccessProbability(uint8_t nodeId) {
    PathStat* ps = getPathStat(nodeId);
    // Laplace smoothed: (success + 1) / (total + 2)
    uint32_t num = (ps->successCount + BAYES_PRIOR) * 100;
    uint32_t den = ps->totalCount + (BAYES_PRIOR * 2);
    return (uint8_t)(num / den);
}

// ============ RSSI DATA LOGGING ============
void logRssiSample(uint8_t srcNode, uint8_t rhFrom, uint8_t hopCount, int16_t rssi,
                   int16_t snr, uint8_t decision, uint8_t flags) {
    rssiLog[rssiLogHead].timestamp = millis();
    rssiLog[rssiLogHead].nodeId = THIS_NODE_ADDRESS;
    rssiLog[rssiLogHead].srcNode = srcNode;
    rssiLog[rssiLogHead].rhFrom = rhFrom;
    rssiLog[rssiLogHead].hopCount = hopCount;
    rssiLog[rssiLogHead].rssi = rssi;
    rssiLog[rssiLogHead].snr = snr;
    rssiLog[rssiLogHead].decision = decision;
    rssiLog[rssiLogHead].flags = flags;
    rssiLogHead = (rssiLogHead + 1) % RSSI_LOG_SIZE;
    if (rssiLogCount < 0xFFFF) rssiLogCount++;
}

// Update last log entry's decision from lastRelayStatus string
void updateLastLogDecision() {
    uint16_t prevIdx = (rssiLogHead == 0) ? RSSI_LOG_SIZE - 1 : rssiLogHead - 1;
    if (strcmp(lastRelayStatus, "QUEUED") == 0)        rssiLog[prevIdx].decision = RELAY_STATUS_QUEUED;
    else if (strcmp(lastRelayStatus, "SENT") == 0)      rssiLog[prevIdx].decision = RELAY_STATUS_SENT;
    else if (strcmp(lastRelayStatus, "CANCEL") == 0)    rssiLog[prevIdx].decision = RELAY_STATUS_CANCEL;
    else if (strcmp(lastRelayStatus, "NO-RELAY") == 0)  rssiLog[prevIdx].decision = RELAY_STATUS_NO_RELAY;
    else if (strcmp(lastRelayStatus, "DROP-DUP") == 0)  rssiLog[prevIdx].decision = RELAY_STATUS_DROP_DUP;
    else if (strcmp(lastRelayStatus, "DROP-Q") == 0)    rssiLog[prevIdx].decision = RELAY_STATUS_DROP_Q;
    else if (strcmp(lastRelayStatus, "DROP-HOP") == 0)  rssiLog[prevIdx].decision = RELAY_STATUS_DROP_HOP;
    else if (strcmp(lastRelayStatus, "DROP-CRC") == 0)  rssiLog[prevIdx].decision = RELAY_STATUS_DROP_CRC;
    else if (strcmp(lastRelayStatus, "DROP-ECHO") == 0) rssiLog[prevIdx].decision = RELAY_STATUS_DROP_ECHO;
    else if (strcmp(lastRelayStatus, "ACK-RCVD") == 0)  rssiLog[prevIdx].decision = RELAY_STATUS_ACK_RCVD;
}

// Dump RSSI log as CSV via Serial
// Format: ts_ms,srcNode,hopCount,rssi,snr,flags
void dumpRssiLog() {
    uint16_t count = (rssiLogCount < RSSI_LOG_SIZE) ? rssiLogCount : RSSI_LOG_SIZE;
    uint16_t start = (rssiLogCount < RSSI_LOG_SIZE) ? 0 :
                     (rssiLogHead % RSSI_LOG_SIZE);
    Serial.println(F("=== RSSI LOG DUMP ==="));
    Serial.println(F("ts_ms,node,src,from,hop,rssi,snr,decision,flags"));
    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (start + i) % RSSI_LOG_SIZE;
        RssiSample* s = &rssiLog[idx];
        Serial.print(s->timestamp);
        Serial.print(F(","));
        Serial.print(s->nodeId);
        Serial.print(F(","));
        Serial.print(s->srcNode);
        Serial.print(F(","));
        Serial.print(s->rhFrom);
        Serial.print(F(","));
        Serial.print(s->hopCount);
        Serial.print(F(","));
        Serial.print(s->rssi);
        Serial.print(F(","));
        Serial.print(s->snr);
        Serial.print(F(","));
        Serial.print(s->decision);
        Serial.print(F(","));
        Serial.println(s->flags, HEX);
    }
    Serial.println(F("=== END ==="));
}

// Handle Serial commands
void handleSerialCommand() {
    if (Serial.available() <= 0) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "DATADUMP") {
        dumpRssiLog();
    } else if (cmd == "DATACLEAR") {
        rssiLogHead = 0;
        rssiLogCount = 0;
        dbg("RSSI log cleared");
    } else if (cmd == "STATS") {
        dbgf("RSSI samples: %u", rssiLogCount);
        dbgf("EMA threshold: %d dBm", rssiSourceClose);
        dbgf("ML mode: %s", mlMode ? "ON" : "OFF");
        dbgf("ML last prob: %d%%", mlProbPct);
    } else if (cmd == "MLTOGGLE") {
        mlMode = !mlMode;
        dbgf("ML mode: %s", mlMode ? "ON" : "OFF");
    } else if (cmd == "MLON") {
        mlMode = true;
        dbg("ML mode: ON");
    } else if (cmd == "MLOFF") {
        mlMode = false;
        dbg("ML mode: OFF");
    }
}

void sendRawPacket(const uint8_t* packet, uint8_t len) {
#ifdef USE_SX1262
    sx1262SetStandby();
    delay(2);
    uint8_t pktParams[6] = {0x00, 0x08, 0x00, len, 0x01, 0x00};
    sx1262WriteCommand(0x8C, pktParams, 6);
    sx1262WriteBuffer(0x00, packet, len);
    sx1262ClearIrqStatus();
    uint8_t txP[3] = {0x00, 0x00, 0x00};
    sx1262WriteCommand(0x83, txP, 3);
    uint32_t txStart = millis();
    bool sent = false;
    while (millis() - txStart < 2000) {
        uint16_t irq = sx1262GetIrqStatus();
        if (irq & 0x0001) {
            sx1262ClearIrqStatus();
            sent = true;
            break;
        }
        delay(1);
    }
#endif
#ifdef USE_RFM95
    rfm95WriteReg(RFM95_REG_OPMODE, RFM95_MODE_STANDBY);
    delay(2);
    rfm95WriteReg(RFM95_REG_FIFO_ADDR_PTR, 0x00);
    rfm95WriteBurst(RFM95_REG_FIFO, packet, len);
    rfm95WriteReg(RFM95_REG_PAYLOAD_LEN, len);
    rfm95WriteReg(RFM95_REG_IRQ_FLAGS, 0xFF);
    rfm95WriteReg(RFM95_REG_OPMODE, RFM95_MODE_TX);
    uint32_t txStart = millis();
    bool sent = false;
    while (millis() - txStart < 2000) {
        if (rfm95ReadReg(RFM95_REG_IRQ_FLAGS) & RFM95_IRQ_TX_DONE) {
            rfm95WriteReg(RFM95_REG_IRQ_FLAGS, 0xFF);
            sent = true;
            break;
        }
        delay(1);
    }
#endif
    setModeRx();
    strncpy(lastRelayStatus, sent ? "SENT" : "DROP-TX",
            sizeof(lastRelayStatus));
    dbg(sent ? "RELAY TX OK" : "RELAY TX timeout!");
    updateOLED();
}

void processMessageQueue() {
    uint32_t now = millis();
    for (int i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
        if (messageQueue[i].inUse && now >= messageQueue[i].sendTime) {
            sendRawPacket(messageQueue[i].data, messageQueue[i].len);
            messageQueue[i].inUse = false;
        }
    }
}

// ============ MODEM CONFIG ============
#ifdef USE_SX1262
bool configureLoRaModem() {
    uint8_t pktType = 0x01;
    sx1262WriteCommand(0x8A, &pktType, 1);
    // 923 MHz: freq_raw = freq_hz * 2^25 / 32MHz
    uint32_t frf = (uint32_t)((923000000.0 / 32000000.0) * (1 << 25));
    uint8_t freqParams[4] = {
        (uint8_t)((frf >> 24) & 0xFF), (uint8_t)((frf >> 16) & 0xFF),
        (uint8_t)((frf >> 8) & 0xFF),  (uint8_t)(frf & 0xFF)
    };
    sx1262WriteCommand(0x86, freqParams, 4);
    uint8_t paConfig[4] = {0x04, 0x07, 0x00, 0x01};  // +22 dBm capable
    sx1262WriteCommand(0x95, paConfig, 4);
    uint8_t txParams[2] = {12, 0x04};  // 12 dBm, ramp 200us
    sx1262WriteCommand(0x8E, txParams, 2);
    uint8_t modParams[4] = {0x07, 0x04, 0x01, 0x00};  // SF7, BW125, CR4/5
    sx1262WriteCommand(0x8B, modParams, 4);
    uint8_t pktParams[6] = {0x00, 0x08, 0x00, 0xFF, 0x01, 0x00};
    sx1262WriteCommand(0x8C, pktParams, 6);
    uint8_t bufBase[2] = {0x00, 0x80};
    sx1262WriteCommand(0x8F, bufBase, 2);
    uint8_t irqParams[8] = {0x02, 0x03, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00};
    sx1262WriteCommand(0x08, irqParams, 8);
    uint8_t calFreq[2] = {0xE1, 0xE9};
    sx1262WriteCommand(0x98, calFreq, 2);
    // Sync word 0x1424 = public (compatible with SX1276 sync=0x12)
    sx1262WriteRegister(0x0740, 0x14);
    sx1262WriteRegister(0x0741, 0x24);
    return true;
}
#endif

#ifdef USE_RFM95
bool configureLoRaModem() {
    // 923 MHz: frf = freq / 61.035 Hz step
    uint32_t frf = (uint32_t)(923000000.0 / 61.03515625);
    rfm95WriteReg(RFM95_REG_FRF_MSB, (frf >> 16) & 0xFF);
    rfm95WriteReg(RFM95_REG_FRF_MID, (frf >> 8) & 0xFF);
    rfm95WriteReg(RFM95_REG_FRF_LSB, frf & 0xFF);
    // PA config: PA_BOOST, power=12 dBm (OutputPower = 12 - 2 = 10 → reg 0x8A)
    rfm95WriteReg(RFM95_REG_PA_CONFIG, 0x80 | (12 - 2));
    rfm95WriteReg(RFM95_REG_OCP, 0x2B);  // OCP 100mA
    rfm95WriteReg(RFM95_REG_LNA, 0x23);  // LNA max gain
    // Modem config: BW=125kHz(0x70), CR=4/5(0x02), explicit header(0x00)
    rfm95WriteReg(RFM95_REG_MODEM_CFG1, 0x72);
    // SF=7(0x70), CRC on(0x04)
    rfm95WriteReg(RFM95_REG_MODEM_CFG2, 0x74);
    // LDRO off, AGC auto
    rfm95WriteReg(RFM95_REG_MODEM_CFG3, 0x04);
    // Preamble = 8
    rfm95WriteReg(RFM95_REG_PREAMBLE_MSB, 0x00);
    rfm95WriteReg(RFM95_REG_PREAMBLE_LSB, 0x08);
    // Sync word 0x12 (public, compatible with SX1262 0x1424)
    rfm95WriteReg(RFM95_REG_SYNC_WORD, 0x12);
    // DIO0 = RxDone (00), DIO1 = RxTimeout (01)
    rfm95WriteReg(RFM95_REG_DIO_MAPPING1, 0x00);
    // FIFO base addresses
    rfm95WriteReg(RFM95_REG_FIFO_TX_BASE, 0x00);
    rfm95WriteReg(RFM95_REG_FIFO_RX_BASE, 0x00);
    return true;
}
#endif

// ============ LORA INIT ============
bool initLoRa() {
    // Common SPI pin setup
    gpioOutput(LORA_CS);
    gpioSet(LORA_CS);
    gpioInput(LORA_IRQ);
    gpioOutput(LORA_RST);
    gpioSet(LORA_RST);
    gpioOutput(LORA_MOSI);
    gpioOutput(LORA_SCK);
    gpioInput(LORA_MISO);
    gpioClear(LORA_MOSI);
    gpioClear(LORA_SCK);

#ifdef USE_SX1262
    dbg("SX1262: init...");
    updateOLED();
    gpioInput(LORA_BUSY);
    delay(10);

    // Reset
    dbg("SX1262: reset...");
    updateOLED();
    gpioClear(LORA_RST);
    delay(20);
    gpioSet(LORA_RST);
    delay(50);

    if (!sx1262WaitBusy(2000)) {
        dbg("SX1262: BUSY timeout!");
        updateOLED();
        return false;
    }

    sx1262SetStandby();
    delay(10);

    // Verify chip
    uint8_t pktType = 0x01;
    sx1262WriteCommand(0x8A, &pktType, 1);
    delay(5);
    uint8_t syncH = sx1262ReadRegister(0x0740);
    dbgf("SX1262 sync: 0x%02X", syncH);
    updateOLED();

    if (syncH == 0x00 || syncH == 0xFF) {
        dbg("SX1262: not found!");
        updateOLED();
        return false;
    }

    configureLoRaModem();
    setModeRx();
    attachInterrupt(digitalPinToInterrupt(LORA_IRQ), lora_isr, RISING);
    dbgf("SX1262: OK N:%d", THIS_NODE_ADDRESS);
#endif

#ifdef USE_RFM95
    dbg("RFM95: init...");
    updateOLED();
    delay(10);

    // Reset
    dbg("RFM95: reset...");
    updateOLED();
    gpioClear(LORA_RST);
    delay(10);
    gpioSet(LORA_RST);
    delay(10);

    // Set sleep mode first, then LoRa mode
    rfm95WriteReg(RFM95_REG_OPMODE, 0x00);  // FSK sleep
    delay(10);
    rfm95WriteReg(RFM95_REG_OPMODE, RFM95_MODE_SLEEP);  // LoRa sleep
    delay(10);

    // Verify chip — version should be 0x12
    uint8_t ver = rfm95ReadReg(RFM95_REG_VERSION);
    dbgf("RFM95 ver: 0x%02X", ver);
    updateOLED();

    if (ver != 0x12) {
        dbg("RFM95: not found!");
        updateOLED();
        return false;
    }

    rfm95WriteReg(RFM95_REG_OPMODE, RFM95_MODE_STANDBY);
    delay(10);

    configureLoRaModem();
    setModeRx();
    attachInterrupt(digitalPinToInterrupt(LORA_IRQ), lora_isr, RISING);
    dbgf("RFM95: OK N:%d", THIS_NODE_ADDRESS);
#endif

    updateOLED();
    return true;
}

// ============ LORA TX (RadioHead compatible) ============
void sendPacket() {
    if (!lora_ok) return;

    char payload[120];
    if (gpsValid) {
        // Include GPS coordinates (6 decimal places ≈ 0.11m precision)
        char latStr[12], lonStr[12];
        dtostrf(myLat, 1, 6, latStr);
        dtostrf(myLon, 1, 6, lonStr);
        snprintf(payload, sizeof(payload), "N:%d|S:%d|T:%d|H:%d|B:%d|LA:%s|LO:%s",
                 THIS_NODE_ADDRESS, txSequenceNum,
                 temperature, humidity, batteryMV, latStr, lonStr);
    } else {
        snprintf(payload, sizeof(payload), "N:%d|S:%d|T:%d|H:%d|B:%d",
                 THIS_NODE_ADDRESS, txSequenceNum,
                 temperature, humidity, batteryMV);
    }

    int payloadLen = strlen(payload);

    uint8_t encrypted[payloadLen + 2];
    xorEncrypt(payload, payloadLen, encrypted);

    uint16_t crc = calculateCRC16(encrypted, payloadLen);
    encrypted[payloadLen] = (crc >> 8) & 0xFF;
    encrypted[payloadLen + 1] = crc & 0xFF;
    uint8_t encLen = payloadLen + 2;

    uint8_t totalLen = 4 + encLen;
    uint8_t packet[totalLen];
    packet[0] = BROADCAST_ADDRESS;
    packet[1] = THIS_NODE_ADDRESS;
    packet[2] = rhMsgId++;
    packet[3] = FLAG_ACK_REQ;  // hop=0, ack_req=1 (origin)
    memcpy(&packet[4], encrypted, encLen);

    // Standby → Buffer → TX
    bool sent = false;
#ifdef USE_SX1262
    sx1262SetStandby();
    delay(2);
    uint8_t pktParams[6] = {0x00, 0x08, 0x00, (uint8_t)totalLen, 0x01, 0x00};
    sx1262WriteCommand(0x8C, pktParams, 6);
    sx1262WriteBuffer(0x00, packet, totalLen);
    sx1262ClearIrqStatus();
    uint8_t txP[3] = {0x00, 0x00, 0x00};
    sx1262WriteCommand(0x83, txP, 3);
    { uint32_t txStart = millis();
    while (millis() - txStart < 2000) {
        uint16_t irq = sx1262GetIrqStatus();
        if (irq & 0x0001) { sx1262ClearIrqStatus(); sent = true; break; }
        delay(1);
    } }
#endif
#ifdef USE_RFM95
    rfm95WriteReg(RFM95_REG_OPMODE, RFM95_MODE_STANDBY);
    delay(2);
    rfm95WriteReg(RFM95_REG_FIFO_ADDR_PTR, 0x00);
    rfm95WriteBurst(RFM95_REG_FIFO, packet, totalLen);
    rfm95WriteReg(RFM95_REG_PAYLOAD_LEN, totalLen);
    rfm95WriteReg(RFM95_REG_IRQ_FLAGS, 0xFF);
    rfm95WriteReg(RFM95_REG_OPMODE, RFM95_MODE_TX);
    { uint32_t txStart = millis();
    while (millis() - txStart < 2000) {
        if (rfm95ReadReg(RFM95_REG_IRQ_FLAGS) & RFM95_IRQ_TX_DONE) {
            rfm95WriteReg(RFM95_REG_IRQ_FLAGS, 0xFF);
            sent = true; break;
        }
        delay(1);
    } }
#endif
    setModeRx();

    if (sent) {
        dbgf("TX S:%d OK", txSequenceNum);
    } else {
        dbg("TX: timeout!");
    }

    ownTxAttempted = true;
    lastOwnTxOk = sent;
    txSequenceNum++;
    updateOLED();
}

// ============ LORA RX (RadioHead compatible) ============
void receivePacket() {
    if (!lora_ok || !lora_rx_flag) return;
    lora_rx_flag = false;

    uint8_t rxLen = 0;
    int16_t rssi = 0;
    uint8_t packet[MAX_MESSAGE_LEN + 4];

#ifdef USE_SX1262
    uint16_t irqFlags = sx1262GetIrqStatus();
    bool sx1262CrcFail = (irqFlags & 0x0040);
    if (!(irqFlags & 0x0002)) {
        sx1262ClearIrqStatus();
        setModeRx();
        return;
    }
    uint8_t rxStatus[2];
    sx1262ReadCommand(0x13, rxStatus, 2);
    rxLen = rxStatus[0];
    uint8_t rxStartAddr = rxStatus[1];
    if (rxLen < 7 || rxLen > MAX_MESSAGE_LEN) {
        sx1262ClearIrqStatus(); setModeRx(); return;
    }
    sx1262ReadBuffer(rxStartAddr, packet, rxLen);
    uint8_t pktStatus[3];
    sx1262ReadCommand(0x14, pktStatus, 3);
    rssi = -(int16_t)pktStatus[0] / 2;
    if (sx1262CrcFail) {
        // Log CRC-failed packet with RSSI only
        logRssiSample(0, 0, 0, rssi, 0, RELAY_STATUS_DROP_CRC, RSSI_LOG_FLAG_CRC_FAIL);
        sx1262ClearIrqStatus();
        setModeRx();
        return;
    }
#endif

#ifdef USE_RFM95
    uint8_t irqFlags = rfm95ReadReg(RFM95_REG_IRQ_FLAGS);
    bool rfm95CrcFail = (irqFlags & RFM95_IRQ_CRC_ERROR);
    if (!(irqFlags & RFM95_IRQ_RX_DONE)) {
        rfm95WriteReg(RFM95_REG_IRQ_FLAGS, 0xFF);
        setModeRx();
        return;
    }
    rxLen = rfm95ReadReg(RFM95_REG_RX_NB_BYTES);
    if (rxLen < 7 || rxLen > MAX_MESSAGE_LEN) {
        rfm95WriteReg(RFM95_REG_IRQ_FLAGS, 0xFF); setModeRx(); return;
    }
    rfm95WriteReg(RFM95_REG_FIFO_ADDR_PTR, rfm95ReadReg(RFM95_REG_FIFO_RX_CURR));
    rfm95ReadBurst(RFM95_REG_FIFO, packet, rxLen);
    rssi = (int16_t)rfm95ReadReg(RFM95_REG_PKT_RSSI) - 157;
    if (rfm95CrcFail) {
        // Log CRC-failed packet with RSSI only
        logRssiSample(0, 0, 0, rssi, 0, RELAY_STATUS_DROP_CRC, RSSI_LOG_FLAG_CRC_FAIL);
        rfm95WriteReg(RFM95_REG_IRQ_FLAGS, 0xFF);
        setModeRx();
        return;
    }
#endif

    // Parse RadioHead header
    uint8_t rhTo    = packet[0];
    uint8_t rhFrom  = packet[1];
    uint8_t rhFlags = packet[3];
    uint8_t hopCount = rhFlags & 0x0F;

    if (rhTo != BROADCAST_ADDRESS && rhTo != THIS_NODE_ADDRESS) {
        setModeRx();
        return;
    }

    // Log RSSI sample BEFORE CRC check — catches weak-signal packets too
    // Use rhFrom from header as source (payload not yet decrypted)
    {
        uint8_t logFlags = 0;
        if (hopCount > 0) logFlags |= RSSI_LOG_FLAG_RELAYED;
        logRssiSample(rhFrom, rhFrom, hopCount, rssi, 0,
                       RELAY_STATUS_IDLE, logFlags);
    }

    uint8_t dataLen = rxLen - 4;
    if (dataLen < 3) {
        setModeRx();
        return;
    }

    // Verify CRC16
    uint8_t encLen = dataLen - 2;
    uint16_t receivedCRC = ((uint16_t)packet[4 + encLen] << 8) | packet[4 + encLen + 1];
    uint16_t calcCRC = calculateCRC16(&packet[4], encLen);

    if (receivedCRC != calcCRC) {
        // Mark the last log entry as CRC_FAIL
        uint16_t prevIdx = (rssiLogHead == 0) ? RSSI_LOG_SIZE - 1 : rssiLogHead - 1;
        rssiLog[prevIdx].flags |= RSSI_LOG_FLAG_CRC_FAIL;
        strncpy(lastRelayStatus, "DROP-CRC", sizeof(lastRelayStatus));
        dbg("RX: CRC fail!");
        setModeRx();
        updateOLED();
        return;
    }

    // Decrypt
    char decrypted[encLen + 1];
    xorDecrypt(&packet[4], encLen, decrypted);

    // Parse source node + sequence + sensor values
    uint8_t srcNode = 0;
    uint8_t seqNum = 0;
    int16_t rxTemp = 0;
    int16_t rxHum = 0;
    uint16_t rxBat = 0;
    String msg = String(decrypted);

    int nPos = msg.indexOf("N:");
    if (nPos != -1) {
        int pipe = msg.indexOf("|", nPos);
        if (pipe != -1) srcNode = msg.substring(nPos + 2, pipe).toInt();
    }
    int sPos = msg.indexOf("S:");
    if (sPos != -1) {
        int pipe = msg.indexOf("|", sPos);
        if (pipe != -1) seqNum = msg.substring(sPos + 2, pipe).toInt();
    }
    int tPos = msg.indexOf("T:");
    if (tPos != -1) {
        int pipe = msg.indexOf("|", tPos);
        if (pipe != -1) rxTemp = msg.substring(tPos + 2, pipe).toInt();
    }
    int hPos = msg.indexOf("H:");
    if (hPos != -1) {
        int pipe = msg.indexOf("|", hPos);
        if (pipe != -1) rxHum = msg.substring(hPos + 2, pipe).toInt();
    }
    int bPos = msg.indexOf("B:");
    if (bPos != -1) {
        int pipe = msg.indexOf("|", bPos);
        if (pipe != -1) rxBat = msg.substring(bPos + 2, pipe).toInt();
        else rxBat = msg.substring(bPos + 2).toInt();
    }

    // Parse GPS if present
    double rxLat = 0.0, rxLon = 0.0;
    int laPos = msg.indexOf("LA:");
    if (laPos != -1) {
        int pipe = msg.indexOf("|", laPos);
        if (pipe != -1) rxLat = msg.substring(laPos + 3, pipe).toFloat();
        else rxLat = msg.substring(laPos + 3).toFloat();
    }
    int loPos = msg.indexOf("LO:");
    if (loPos != -1) {
        int pipe = msg.indexOf("|", loPos);
        if (pipe != -1) rxLon = msg.substring(loPos + 3, pipe).toFloat();
        else rxLon = msg.substring(loPos + 3).toFloat();
    }

    // Track node position
    if (srcNode != 0 && srcNode != THIS_NODE_ADDRESS) {
        updateTrackedNode(srcNode, rxLat, rxLon, rssi);
    }

    lastRxValid = true;
    lastRxNode = srcNode;
    lastRxFrom = rhFrom;
    lastRxHop = hopCount;
    lastRxRssi = rssi;
    lastRxTemp = rxTemp;
    lastRxHum = rxHum;
    lastRxBat = rxBat;

    dbgf("RX N:%d S:%d R:%d H:%d", srcNode, seqNum, rssi, hopCount);
    dbgf(" T:%d H:%d%% B:%dmV", rxTemp, rxHum, rxBat);
    if (rxLat != 0.0 && rxLon != 0.0 && gpsValid) {
        double dist = haversineDistance(myLat, myLon, rxLat, rxLon);
        float brng = bearingTo(myLat, myLon, rxLat, rxLon);
        if (dist < 1000) {
            dbgf(" N%d:%.0fm %s", srcNode, dist, bearingToCompass(brng));
        } else {
            dbgf(" N%d:%.1fkm %s", srcNode, dist / 1000.0, bearingToCompass(brng));
        }
    }
    Serial.print(F("[RX from:"));
    Serial.print(rhFrom);
    Serial.print(F(" src:"));
    Serial.print(srcNode);
    Serial.print(F(" hop:"));
    Serial.print(hopCount);
    Serial.print(F(" rssi:"));
    Serial.print(rssi);
    Serial.print(F("] "));
    Serial.println(decrypted);

    // ---- Parse ACK payload for relay/cancel ----
    // ACK format: "ACK:<src>|S:<seq>|GW:<gw_id>"
    bool isAck = msg.startsWith("ACK:");
    uint8_t ackSrcNode = 0;
    uint8_t ackSeqNum = 0;

    // Update previous log entry with ACK flag if applicable
    // (the sample was already logged before CRC check)
    {
        uint16_t prevIdx = (rssiLogHead == 0) ? RSSI_LOG_SIZE - 1 : rssiLogHead - 1;
        rssiLog[prevIdx].srcNode = srcNode;  // update src from decrypted payload
        if (isAck) rssiLog[prevIdx].flags |= RSSI_LOG_FLAG_ACK;
    }

    if (isAck) {
        // ACK:<src> — extract the original source node
        int colon = msg.indexOf(":");
        int pipe = msg.indexOf("|", colon);
        if (pipe != -1) {
            ackSrcNode = msg.substring(colon + 1, pipe).toInt();
        } else {
            ackSrcNode = msg.substring(colon + 1).toInt();
        }
        int sPos = msg.indexOf("S:");
        if (sPos != -1) {
            int sPipe = msg.indexOf("|", sPos);
            if (sPipe != -1) ackSeqNum = msg.substring(sPos + 2, sPipe).toInt();
            else ackSeqNum = msg.substring(sPos + 2).toInt();
        }
        // Cancel any pending forward of the original data packet
        if (ackSrcNode != 0) {
            cancelFromQueue(ackSrcNode, ackSeqNum);
        }
    }

    // ---- Cancel-on-heard: if this packet is in our forward queue, cancel ----
    // (for non-ACK packets — ACKs cancel via ackSrcNode/ackSeqNum above)
    if (!isAck) {
        cancelFromQueue(srcNode, seqNum);
    }

    // ---- Adaptive RSSI threshold ----
    // Feed every valid RX RSSI into EMA to adapt RSSI_SOURCE_CLOSE
    updateRssiThreshold(rssi);

    // ---- Path success tracking ----
    // Record that we saw a packet from this source
    if (srcNode != 0 && srcNode != THIS_NODE_ADDRESS) {
        recordPathSeen(srcNode);
    }

    // If we hear a relay (hop>0) or ACK for a source, that's a success signal
    if (hopCount > 0 && !isAck && srcNode != 0 && srcNode != THIS_NODE_ADDRESS) {
        // This packet was relayed by someone → source's path is working
        recordPathSuccess(srcNode);
    }
    if (isAck && ackSrcNode != 0) {
        // ACK means the data reached GW → source's path succeeded
        recordPathSuccess(ackSrcNode);
    }

    // ---- Relay logic ----
    // Guard: skip if this is our own data (echo from another relay)
    if (srcNode == THIS_NODE_ADDRESS || rhFrom == THIS_NODE_ADDRESS) {
        strncpy(lastRelayStatus, "DROP-ECHO", sizeof(lastRelayStatus));
        dbgf("RX: own echo N:%d skip", srcNode);
    } else if (isAck && ackSrcNode == THIS_NODE_ADDRESS) {
        // ACK is for us — don't relay further
        strncpy(lastRelayStatus, "ACK-RCVD", sizeof(lastRelayStatus));
        dbgf("RX: ACK for us N:%d S:%d", ackSrcNode, ackSeqNum);
    } else if (isAck && ackSrcNode == 0) {
        // Malformed ACK — can't determine original source
        strncpy(lastRelayStatus, "DROP-ACK", sizeof(lastRelayStatus));
        dbg("RX: bad ACK skip");
    } else if (srcNode == 0 && !isAck) {
        strncpy(lastRelayStatus, "DROP-PARSE", sizeof(lastRelayStatus));
        dbg("RX: bad parse skip");
    } else {
        // For ACKs: use (ackSrcNode, ackSeqNum) for duplicate check
        // to avoid collision with the original data packet's cache entry
        uint8_t dupSrc = isAck ? ackSrcNode : srcNode;
        uint8_t dupSeq = isAck ? ackSeqNum : seqNum;

        if (isMessageDuplicate(dupSrc, dupSeq)) {
            strncpy(lastRelayStatus, "DROP-DUP", sizeof(lastRelayStatus));
            dbg("RX: dup skip");
        } else {
            addToRelayCache(dupSrc, dupSeq);

            if (hopCount < MAX_HOPS) {
                // Decision logic:
                //   ML mode: predictRelay() uses trained neural network
                //   Rule mode: hop=0 (origin) → relay only if RSSI < adaptive threshold
                bool shouldRelay = true;
                
                if (mlMode) {
                    // ML-based decision using 3-layer neural network
                    float raw[4] = { (float)rssi, (float)srcNode, (float)rhFrom, (float)THIS_NODE_ADDRESS };
                    float prob = mlPredictRelay(raw);
                    mlProbPct = (uint8_t)(prob * 100.0f);
                    if (prob < 0.5f) {
                        shouldRelay = false;
                        strncpy(lastRelayStatus, "NO-RELAY", sizeof(lastRelayStatus));
                        dbgf("ML: skip R:%d prob:%d%% N:%d", rssi, mlProbPct, srcNode);
                    } else {
                        dbgf("ML: relay R:%d prob:%d%% N:%d", rssi, mlProbPct, srcNode);
                    }
                } else {
                    // Rule-based: source close → skip relay
                    if (hopCount == 0 && rssi >= rssiSourceClose) {
                        shouldRelay = false;
                        strncpy(lastRelayStatus, "NO-RELAY", sizeof(lastRelayStatus));
                        dbgf("RX: src close R:%d thresh:%d N:%d skip relay",
                             rssi, rssiSourceClose, srcNode);
                    }
                }

                if (shouldRelay) {
                    uint8_t relayPkt[rxLen];
                    memcpy(relayPkt, packet, rxLen);
                    relayPkt[0] = BROADCAST_ADDRESS;
                    relayPkt[1] = THIS_NODE_ADDRESS;
                    relayPkt[2] = rhMsgId++;
                    // Preserve FLAGS bits 4-5 (IS_ACK, ACK_REQ) while incrementing hop
                    relayPkt[3] = ((hopCount + 1) & FLAG_HOP_MASK) |
                                  (rhFlags & ~FLAG_HOP_MASK);
                    addToMessageQueue(relayPkt, rxLen, dupSrc, dupSeq, rssi);
                    dbgf("FWD N:%d S:%d H:%d>%d R:%d%s",
                         dupSrc, dupSeq, hopCount, hopCount + 1, rssi,
                         isAck ? " ACK" : "");
                }
            } else {
                strncpy(lastRelayStatus, "DROP-HOP", sizeof(lastRelayStatus));
                dbgf("RX: maxhop %d N:%d", hopCount, srcNode);
            }
        }
    }

// Record final decision in RSSI log
    updateLastLogDecision();

#ifdef USE_SX1262
    sx1262ClearIrqStatus();
#endif
#ifdef USE_RFM95
    rfm95WriteReg(RFM95_REG_IRQ_FLAGS, 0xFF);
#endif
    setModeRx();
    updateOLED();
}

// ============ BATTERY ============
float readBattery() {
    analogReadResolution(12);
    uint16_t raw = analogRead(BATTERY_PIN);
    return (raw * REAL_VBAT_MV_PER_LSB) / 1000.0f;
}

// ============ I2C SCAN ============
void scanI2C() {
    uint8_t count = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            dbgf("I2C: 0x%02X found", addr);
            count++;
        }
    }
    dbgf("I2C: %d device(s)", count);
}

// ============ SETUP ============
void setup() {
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    nrf_gpio_cfg_input(UI_BUTTON_PIN, NRF_GPIO_PIN_PULLUP);

    Serial.begin(115200);
    delay(2000);
#ifdef USE_SX1262
    Serial.print(F("\n=== nRF52840 LoRa P2P Node "));
    Serial.print(THIS_NODE_ADDRESS);
    Serial.println(F(" (SX1262+BLE) ==="));
#endif
#ifdef USE_RFM95
    Serial.print(F("\n=== nRF52840 LoRa P2P Node "));
    Serial.print(THIS_NODE_ADDRESS);
    Serial.println(F(" (RFM95+BLE) ==="));
#endif

    // I2C
    Wire.setPins(OLED_SDA, OLED_SCL);
    Wire.begin();
    delay(100);

    // OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
            Serial.println(F("[-] OLED FAILED"));
        } else {
            oled_ok = true;
        }
    } else {
        oled_ok = true;
    }

    if (oled_ok) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.print(F("Node "));
        display.println(THIS_NODE_ADDRESS);
        display.setTextSize(1);
#ifdef USE_SX1262
        display.println(F("SX1262+BLE init..."));
#endif
#ifdef USE_RFM95
        display.println(F("RFM95+BLE init..."));
#endif
        display.display();
    }

    dbgf("=== Node %d ===", THIS_NODE_ADDRESS);
    dbg(oled_ok ? "OLED: OK" : "OLED: FAIL!");
    updateOLED();

    scanI2C();
    updateOLED();

    // BME280 init
    if (bme.begin(0x76)) {
        bme_ok = true;
    } else if (bme.begin(0x77)) {
        bme_ok = true;
    }
    if (bme_ok) {
        bme.setSampling(Adafruit_BME280::MODE_FORCED,
                        Adafruit_BME280::SAMPLING_X1,
                        Adafruit_BME280::SAMPLING_NONE,
                        Adafruit_BME280::SAMPLING_X1,
                        Adafruit_BME280::FILTER_OFF);
        dbg("BME280: OK");
    } else {
        dbg("BME280: sim mode");
    }
    updateOLED();

    // BLE init (must be before LoRa — NimBLE needs early radio init)
    ble_ok = initBLE();
    dbg(ble_ok ? "BLE: OK" : "BLE: FAIL!");
    updateOLED();

    // LoRa init
    lora_ok = initLoRa();
    if (!lora_ok) {
#ifdef USE_SX1262
        dbg("SX1262: FAILED!");
#endif
#ifdef USE_RFM95
        dbg("RFM95: FAILED!");
#endif
    }
    updateOLED();

    dbg("Ready.");
    updateOLED();

    digitalWrite(STATUS_LED, HIGH);
    delay(1000);
}

// ============ LOOP ============
void loop() {
    static unsigned long lastBat = 0;
    static unsigned long lastDisplay = 0;
    static unsigned long lastTx = 0;

    handleUIButton();
    handleSerialCommand();

    // RX has priority
    receivePacket();

    // Process relay queue
    processMessageQueue();

    // Battery + BME280 every 2s
    if (millis() - lastBat >= 2000) {
        lastBat = millis();
        batteryVoltage = readBattery();
        batteryMV = (uint16_t)(batteryVoltage * 1000);
        if (bme_ok) {
            bme.takeForcedMeasurement();
            temperature = (int16_t)bme.readTemperature();
            humidity = (int16_t)bme.readHumidity();
        }
    }

    // Update OLED every 1s
    if (millis() - lastDisplay >= 1000) {
        lastDisplay = millis();
        updateOLED();
    }

    // TX every 2 minutes
    if (lora_ok && millis() - lastTx >= TX_INTERVAL) {
        lastTx = millis();
        sendPacket();
    }

#if LOW_POWER_IDLE
    // This blocks only the application loop task. The nRF52840 enters
    // System-ON idle while SX1262 remains in continuous RX and OLED stays on.
    // DIO1 wakes the CPU; button polling resumes within 10 ms.
    delay(CPU_IDLE_SLEEP_MS);
#else
    yield();
#endif
}
