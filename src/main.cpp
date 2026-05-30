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

// ============ RADIO MODULE SELECTION ============
// เลือก module โดย uncomment อันที่ใช้ (เลือกได้อันเดียว)
//#define USE_SX1262          // SX1262 module (command-based SPI, DIO1+BUSY)
#define USE_RFM95         // RFM95/SX1276 module (register-based SPI, DIO0)

// ============ PIN DEFINITIONS ============
#define STATUS_LED   15   // P0.15

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
#define THIS_NODE_ADDRESS  116
#define BROADCAST_ADDRESS  0xFF
#define MAX_MESSAGE_LEN    251
#define TX_INTERVAL        30000  // 30 seconds

// Crypto key (must match STM32 nodes)
static const char* CRYPTO_KEY = "1234567890000000";
#define CRYPTO_KEY_LEN 16

// ============ RELAY / MULTI-HOP CONFIG ============
#define MAX_HOPS              3
#define RELAY_MEMORY_SIZE     10
#define MESSAGE_QUEUE_SIZE    5
#define RSSI_RELAY_THRESHOLD  -100
#define RELAY_CACHE_TTL       300000  // 5 minutes

// ============ BLE GPS CONFIG ============
#define BLE_GPS_SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define BLE_GPS_CHAR_UUID           "12345678-1234-1234-1234-123456789abd"
#define MAX_TRACKED_NODES           8
#define NODE_POSITION_TTL           120000  // 2 min stale timeout
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
    bool inUse;
} messageQueue[MESSAGE_QUEUE_SIZE];

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

    char line[25];

    // Row 0: Node ID + Battery + BLE/GPS status
    display.setCursor(0, 0);
    snprintf(line, sizeof(line), "N:%-3d %.1fV %s%s",
             THIS_NODE_ADDRESS, batteryVoltage,
             ble_connected ? "B+" : "B-",
             gpsValid ? " GPS" : "");
    display.print(line);

    // Row 1: Sensor + TX count
    display.setCursor(0, 10);
    snprintf(line, sizeof(line), "%dC %d%% TX#%d %s",
             temperature, humidity, txSequenceNum,
             bme_ok ? "" : "(s)");
    display.print(line);

    // Separator
    display.drawLine(0, 19, 127, 19, SSD1306_WHITE);

    // Check if we have GPS and a nearest node to display
    NodePosition* nearest = nullptr;
    if (gpsValid) nearest = findNearestNode();

    if (nearest && gpsValid) {
        double dist = haversineDistance(myLat, myLon, nearest->lat, nearest->lon);
        float brng = bearingTo(myLat, myLon, nearest->lat, nearest->lon);

        // Row 2: Nearest node info
        display.setCursor(0, 22);
        if (dist < 1000) {
            snprintf(line, sizeof(line), "N%d %.0fm %s R:%d",
                     nearest->nodeId, dist, bearingToCompass(brng), nearest->rssi);
        } else {
            char distStr[8];
            dtostrf(dist / 1000.0, 1, 1, distStr);
            snprintf(line, sizeof(line), "N%d %skm %s R:%d",
                     nearest->nodeId, distStr, bearingToCompass(brng), nearest->rssi);
        }
        display.print(line);

        // Draw directional arrow (right side)
        float relBearing = brng;
        if (myHeading >= 0) {
            relBearing = fmod(brng - myHeading + 360.0f, 360.0f);
        }
        drawArrow(112, 42, 10, relBearing);
        // Compass ring
        display.drawCircle(112, 42, 12, SSD1306_WHITE);
        // N marker on ring
        if (myHeading >= 0) {
            float nRad = -myHeading * DEG_TO_RAD;
            int nx = 112 + (int)(14 * sin(nRad));
            int ny = 42 - (int)(14 * cos(nRad));
            display.setCursor(nx - 2, ny - 3);
            display.print("N");
        }

        // Rows 3-5: debug log (fewer lines when showing arrow)
        for (int i = 0; i < 3; i++) {
            int idx = (dbgIdx + DBG_LINES - 3 + i) % DBG_LINES;
            if (dbgBuf[idx][0]) {
                display.setCursor(0, 33 + i * 10);
                display.print(dbgBuf[idx]);
            }
        }
    } else {
        // No GPS/nearest: show full debug log
        for (int i = 0; i < DBG_LINES; i++) {
            int idx = (dbgIdx + i) % DBG_LINES;
            if (dbgBuf[idx][0]) {
                display.setCursor(0, 22 + i * 9);
                display.print(dbgBuf[idx]);
            }
        }
    }

    display.display();
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
void addToMessageQueue(const uint8_t* packet, uint8_t len) {
    for (int i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
        if (!messageQueue[i].inUse) {
            memcpy(messageQueue[i].data, packet, len);
            messageQueue[i].len = len;
            uint32_t delayMs = 1000 + random(2000);
            messageQueue[i].sendTime = millis() + delayMs;
            messageQueue[i].inUse = true;
            dbgf("RELAY Q +%lums", delayMs);
            return;
        }
    }
    dbg("RELAY Q full!");
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
    packet[3] = 0x00;  // hop count = 0 (origin)
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
    if (!(irqFlags & 0x0002) || (irqFlags & 0x0040)) {
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
#endif

#ifdef USE_RFM95
    uint8_t irqFlags = rfm95ReadReg(RFM95_REG_IRQ_FLAGS);
    if (!(irqFlags & RFM95_IRQ_RX_DONE) || (irqFlags & RFM95_IRQ_CRC_ERROR)) {
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
    // RSSI = -RegPktRssi/2  (approximation for HF port)
    rssi = -((int16_t)rfm95ReadReg(RFM95_REG_PKT_RSSI)) + (-157);
    // More accurate: rssi = -157 + regValue (for HF band >862MHz)
    rssi = (int16_t)rfm95ReadReg(RFM95_REG_PKT_RSSI) - 157;
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

    // ---- Relay logic ----
    // Guard: skip if this is our own data (echo from another relay)
    // Check both payload N: field AND RadioHead FROM header
    if (srcNode == THIS_NODE_ADDRESS || rhFrom == THIS_NODE_ADDRESS) {
        dbgf("RX: own echo N:%d skip", srcNode);
    } else if (srcNode == 0) {
        dbg("RX: bad parse skip");
    } else {
        if (isMessageDuplicate(srcNode, seqNum)) {
            dbg("RX: dup skip");
        } else {
            addToRelayCache(srcNode, seqNum);

            if (rssi <= RSSI_RELAY_THRESHOLD && hopCount < MAX_HOPS) {
                uint8_t relayPkt[rxLen];
                memcpy(relayPkt, packet, rxLen);
                relayPkt[0] = BROADCAST_ADDRESS;
                relayPkt[1] = THIS_NODE_ADDRESS;
                relayPkt[2] = rhMsgId++;
                relayPkt[3] = (hopCount + 1) & 0x0F;
                addToMessageQueue(relayPkt, rxLen);
                dbgf("FWD N:%d T:%d H:%d%%", srcNode, rxTemp, rxHum);
                dbgf(" H:%d>%d R:%d", hopCount, hopCount + 1, rssi);
            } else if (hopCount >= MAX_HOPS) {
                dbgf("RX: maxhop %d N:%d", hopCount, srcNode);
            } else {
                dbgf("RX: no relay R:%d", rssi);
            }
        }
    }

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

    Serial.begin(115200);
    delay(2000);
#ifdef USE_SX1262
    Serial.println(F("\n=== nRF52840 LoRa P2P Node 116 (SX1262+BLE) ==="));
#endif
#ifdef USE_RFM95
    Serial.println(F("\n=== nRF52840 LoRa P2P Node 116 (RFM95+BLE) ==="));
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
        display.println(F("Node 116"));
        display.setTextSize(1);
#ifdef USE_SX1262
        display.println(F("SX1262+BLE init..."));
#endif
#ifdef USE_RFM95
        display.println(F("RFM95+BLE init..."));
#endif
        display.display();
    }

    dbg("=== Node 116 ===");
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

    // TX every 30s
    if (lora_ok && millis() - lastTx >= TX_INTERVAL) {
        lastTx = millis();
        sendPacket();
    }
}
