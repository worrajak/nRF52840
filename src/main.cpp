/*
 * nRF52840 LoRa P2P Node — Compatible with STM32 LoRa P2P Network
 * Node ID: 115
 *
 * Protocol: RadioHead-compatible headers + XOR encryption + CRC16
 * Packet: [TO=0xFF][FROM=115][ID][FLAGS=0x00][encrypted_payload+CRC16]
 * Payload: N:115|S:seq|T:temp|H:hum|B:bat
 *
 * OLED init: matched to working test_oled.bak pattern
 * SPI: software bit-bang using nrf_gpio_* (n-able PINS_COUNT=34 bug)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <stdarg.h>
#include <nrf_gpio.h>
#include "lora_config.h"

// ============ PIN DEFINITIONS ============
#define STATUS_LED   15   // P0.15

// OLED I2C
#define OLED_SDA     36   // P1.04
#define OLED_SCL     11   // P0.11

// RFM95 LoRa SPI — raw GPIO numbers (nrf_gpio_* functions)
#define RFM95_MISO   2    // P0.02
#define RFM95_SCK    43   // P1.11
#define RFM95_MOSI   47   // P1.15
#define RFM95_CS     45   // P1.13
#define RFM95_DIO0   29   // P0.29
#define RFM95_RST    9    // P0.09

// Battery
#define BATTERY_PIN  31   // P0.31
#define VBAT_MV_PER_LSB     0.73242188F
#define VBAT_DIVIDER_COMP   1.73F
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

// ============ NETWORK CONFIG ============
#define THIS_NODE_ADDRESS  115
#define BROADCAST_ADDRESS  0xFF
#define MAX_MESSAGE_LEN    251
#define TX_INTERVAL        30000  // 30 seconds

// Crypto key (must match STM32 nodes)
static const char* CRYPTO_KEY = "1234567890000000";
#define CRYPTO_KEY_LEN 16

// ============ GLOBALS ============
Adafruit_SSD1306 display(128, 64, &Wire, -1);

float batteryVoltage = 0;
uint16_t batteryMV = 3300;
bool oled_ok = false;
bool lora_ok = false;

// LoRa state
volatile bool lora_rx_flag = false;
uint8_t rxbuf[MAX_MESSAGE_LEN];
uint8_t txSequenceNum = 0;
uint8_t rhMsgId = 0;  // RadioHead message ID

// Last RX data for display
struct {
    uint8_t nodeId;
    uint8_t seqNum;
    int16_t rssi;
    bool hasData;
} lastRx = {0, 0, 0, false};

// ============ DEBUG OUTPUT SYSTEM ============
#define DBG_LINES 6
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

void updateOLED() {
    if (!oled_ok) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Line 1: Node ID + battery + uptime
    char line[DBG_COLS];
    display.setCursor(0, 0);
    snprintf(line, sizeof(line), "N:%d V:%.2f %lus",
             THIS_NODE_ADDRESS, batteryVoltage, millis() / 1000);
    display.print(line);

    // Debug log (6 lines)
    for (int i = 0; i < DBG_LINES; i++) {
        int idx = (dbgIdx + i) % DBG_LINES;
        if (dbgBuf[idx][0]) {
            display.setCursor(0, 10 + i * 9);
            display.print(dbgBuf[idx]);
        }
    }

    display.display();
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
        gpioWrite(RFM95_MOSI, (data >> i) & 1);
        delayMicroseconds(2);
        gpioSet(RFM95_SCK);
        delayMicroseconds(2);
        if (gpioRead(RFM95_MISO)) recv |= (1 << i);
        gpioClear(RFM95_SCK);
        delayMicroseconds(2);
    }
    return recv;
}

// ============ RFM95 REGISTER ACCESS ============
uint8_t rfm95ReadReg(uint8_t addr) {
    gpioClear(RFM95_CS);
    delayMicroseconds(5);
    swSpiTransfer(addr & 0x7F);
    uint8_t value = swSpiTransfer(0x00);
    delayMicroseconds(5);
    gpioSet(RFM95_CS);
    delayMicroseconds(5);
    return value;
}

void rfm95WriteReg(uint8_t addr, uint8_t value) {
    gpioClear(RFM95_CS);
    delayMicroseconds(5);
    swSpiTransfer(addr | 0x80);
    swSpiTransfer(value);
    delayMicroseconds(5);
    gpioSet(RFM95_CS);
    delayMicroseconds(5);
}

// Write burst to FIFO
void rfm95WriteFifo(const uint8_t* data, uint8_t len) {
    gpioClear(RFM95_CS);
    delayMicroseconds(5);
    swSpiTransfer(0x00 | 0x80);  // FIFO register with write bit
    for (uint8_t i = 0; i < len; i++) {
        swSpiTransfer(data[i]);
    }
    delayMicroseconds(5);
    gpioSet(RFM95_CS);
    delayMicroseconds(5);
}

// Read burst from FIFO
void rfm95ReadFifo(uint8_t* data, uint8_t len) {
    gpioClear(RFM95_CS);
    delayMicroseconds(5);
    swSpiTransfer(0x00);  // FIFO register read
    for (uint8_t i = 0; i < len; i++) {
        data[i] = swSpiTransfer(0x00);
    }
    delayMicroseconds(5);
    gpioSet(RFM95_CS);
    delayMicroseconds(5);
}

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

// ============ LORA MODEM CONFIG ============
bool configureLoRaModem() {
    // MUST set LoRa mode first! (reg 0x01 bit 7)
    // Without this, registers 0x1D/0x1E/0x39 are FSK registers, not LoRa!
    rfm95WriteReg(0x01, 0x80);  // LoRa + Sleep
    delay(10);

    // FIFO base addresses
    rfm95WriteReg(0x0E, 0x00);  // TX FIFO base
    rfm95WriteReg(0x0F, 0x00);  // RX FIFO base

    // Frequency: 923 MHz
    uint64_t frf = ((uint64_t)923000000 << 19) / 32000000;
    rfm95WriteReg(0x06, (frf >> 16) & 0xFF);
    rfm95WriteReg(0x07, (frf >> 8) & 0xFF);
    rfm95WriteReg(0x08, frf & 0xFF);

    // ModemConfig1: BW=125kHz(0x70), CR=4/5(0x02), Implicit=0
    rfm95WriteReg(0x1D, 0x72);

    // ModemConfig2: SF7(0x70), CRC on(0x04)
    rfm95WriteReg(0x1E, 0x74);

    // Preamble: 8 symbols (matches RadioHead default)
    rfm95WriteReg(0x20, 0x00);
    rfm95WriteReg(0x21, 0x08);

    // Sync word: 0x12 (matches STM32 config)
    rfm95WriteReg(0x39, 0x12);

    // TX power: 12 dBm, PA_BOOST
    rfm95WriteReg(0x09, 0x8C);  // PA_BOOST + OutputPower=12

    // Max payload length
    rfm95WriteReg(0x23, 0xFF);

    // Disable frequency hopping
    rfm95WriteReg(0x24, 0x00);

    // DIO0 = RxDone (00), DIO1 = RxTimeout (00)
    rfm95WriteReg(0x40, 0x00);

    return true;
}

void setModeRx() {
    rfm95WriteReg(0x01, 0x85);  // LoRa + RX continuous
    delay(10);
}

void setModeTx() {
    rfm95WriteReg(0x01, 0x83);  // LoRa + TX
}

void setModeSleep() {
    rfm95WriteReg(0x01, 0x80);  // LoRa + Sleep
}

// DIO0 interrupt
void dio0_isr() {
    lora_rx_flag = true;
}

// ============ LORA INIT ============
bool initLoRa() {
    dbg("LoRa: init...");
    updateOLED();

    // Configure GPIO using nrf_gpio_*
    gpioOutput(RFM95_CS);
    gpioSet(RFM95_CS);
    gpioInput(RFM95_DIO0);
    gpioOutput(RFM95_RST);
    gpioSet(RFM95_RST);
    delay(10);

    // Software SPI pins
    gpioOutput(RFM95_MOSI);
    gpioOutput(RFM95_SCK);
    gpioInput(RFM95_MISO);
    gpioClear(RFM95_MOSI);
    gpioClear(RFM95_SCK);
    delay(100);

    // Reset RFM95
    dbg("LoRa: reset...");
    updateOLED();
    gpioClear(RFM95_RST);
    delay(10);
    gpioSet(RFM95_RST);
    delay(50);

    // Read version
    uint8_t version = rfm95ReadReg(0x42);
    dbgf("LoRa ver: 0x%02X", version);
    updateOLED();

    if (version != 0x12) {
        dbg("LoRa: SPI FAIL!");
        updateOLED();
        return false;
    }

    // Configure modem
    if (!configureLoRaModem()) {
        dbg("LoRa: config FAIL!");
        updateOLED();
        return false;
    }

    // Start RX
    setModeRx();
    attachInterrupt(digitalPinToInterrupt(RFM95_DIO0), dio0_isr, RISING);

    dbgf("LoRa: OK N:%d", THIS_NODE_ADDRESS);
    updateOLED();
    return true;
}

// ============ LORA TX (RadioHead compatible) ============
void sendPacket() {
    if (!lora_ok) return;

    // Build plaintext payload
    char payload[80];
    snprintf(payload, sizeof(payload), "N:%d|S:%d|T:%d|H:%d|B:%d",
             THIS_NODE_ADDRESS, txSequenceNum,
             (int)(batteryVoltage * 10),  // placeholder temp (battery*10)
             50,                          // placeholder humidity
             batteryMV);

    int payloadLen = strlen(payload);

    // Encrypt payload
    uint8_t encrypted[payloadLen + 2];  // +2 for CRC
    xorEncrypt(payload, payloadLen, encrypted);

    // Calculate CRC16 on encrypted data
    uint16_t crc = calculateCRC16(encrypted, payloadLen);
    encrypted[payloadLen] = (crc >> 8) & 0xFF;      // CRC high byte
    encrypted[payloadLen + 1] = crc & 0xFF;          // CRC low byte
    uint8_t encLen = payloadLen + 2;

    // Build full packet: RadioHead header (4 bytes) + encrypted payload + CRC
    uint8_t totalLen = 4 + encLen;
    uint8_t packet[totalLen];
    packet[0] = BROADCAST_ADDRESS;     // TO
    packet[1] = THIS_NODE_ADDRESS;     // FROM
    packet[2] = rhMsgId++;             // ID
    packet[3] = 0x00;                  // FLAGS (no ACK for broadcast)
    memcpy(&packet[4], encrypted, encLen);

    // Set TX mode
    setModeSleep();
    rfm95WriteReg(0x0D, 0x00);         // FIFO ptr to base
    rfm95WriteReg(0x22, totalLen);      // Payload length
    rfm95WriteFifo(packet, totalLen);   // Write to FIFO
    setModeTx();                        // Start TX

    // Wait for TxDone
    uint32_t txStart = millis();
    bool sent = false;
    while (millis() - txStart < 2000) {
        uint8_t irq = rfm95ReadReg(0x12);
        if (irq & 0x08) {  // TxDone
            rfm95WriteReg(0x12, 0xFF);  // Clear all IRQ
            sent = true;
            break;
        }
        delay(10);
    }

    // Back to RX
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

    uint8_t irqFlags = rfm95ReadReg(0x12);

    // Check RxDone (bit 6) and no CRC error (bit 5)
    if ((irqFlags & 0x40) && !(irqFlags & 0x20)) {
        // Get packet info
        uint8_t rxLen = rfm95ReadReg(0x13);    // Number of bytes received
        uint8_t fifoAddr = rfm95ReadReg(0x10); // Start of last packet in FIFO
        rfm95WriteReg(0x0D, fifoAddr);         // Set FIFO pointer

        if (rxLen < 7 || rxLen > MAX_MESSAGE_LEN) {  // 4 header + 1 data + 2 CRC minimum
            rfm95WriteReg(0x12, 0xFF);
            setModeRx();
            return;
        }

        // Read packet from FIFO
        uint8_t packet[rxLen];
        rfm95ReadFifo(packet, rxLen);

        // RSSI
        int16_t rssi = rfm95ReadReg(0x1A) - 157;

        // Parse RadioHead header
        uint8_t rhTo    = packet[0];
        uint8_t rhFrom  = packet[1];
        uint8_t rhId    = packet[2];
        uint8_t rhFlags = packet[3];

        // Check if addressed to us or broadcast
        if (rhTo != BROADCAST_ADDRESS && rhTo != THIS_NODE_ADDRESS) {
            rfm95WriteReg(0x12, 0xFF);
            setModeRx();
            return;
        }

        // Encrypted data starts at byte 4
        uint8_t dataLen = rxLen - 4;

        // Need at least 3 bytes (1 data + 2 CRC)
        if (dataLen < 3) {
            rfm95WriteReg(0x12, 0xFF);
            setModeRx();
            return;
        }

        // Verify CRC16 (last 2 bytes are CRC)
        uint8_t encLen = dataLen - 2;
        uint16_t receivedCRC = ((uint16_t)packet[4 + encLen] << 8) | packet[4 + encLen + 1];
        uint16_t calcCRC = calculateCRC16(&packet[4], encLen);

        if (receivedCRC != calcCRC) {
            dbg("RX: CRC fail!");
            rfm95WriteReg(0x12, 0xFF);
            setModeRx();
            return;
        }

        // Decrypt
        char decrypted[encLen + 1];
        xorDecrypt(&packet[4], encLen, decrypted);

        // Parse source node and sequence from payload
        uint8_t srcNode = 0;
        uint8_t seqNum = 0;
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

        // Update last RX info
        lastRx.nodeId = srcNode;
        lastRx.seqNum = seqNum;
        lastRx.rssi = rssi;
        lastRx.hasData = true;

        // Display on OLED
        dbgf("RX N:%d S:%d R:%d", srcNode, seqNum, rssi);

        // Serial detail
        Serial.print(F("[RX from:"));
        Serial.print(rhFrom);
        Serial.print(F("] "));
        Serial.println(decrypted);
    }

    rfm95WriteReg(0x12, 0xFF);  // Clear all IRQ
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

    // 1. Serial first (proven working pattern)
    Serial.begin(115200);
    delay(2000);
    Serial.println(F("\n=== nRF52840 LoRa P2P Node 115 ==="));

    // 2. I2C
    Wire.setPins(OLED_SDA, OLED_SCL);
    Wire.begin();
    delay(100);

    // 3. OLED — direct display.begin
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
        display.println(F("Node 115"));
        display.setTextSize(1);
        display.println(F("LoRa P2P init..."));
        display.display();
    }

    dbg("=== Node 115 ===");
    dbg(oled_ok ? "OLED: OK" : "OLED: FAIL!");
    updateOLED();

    scanI2C();
    updateOLED();

    // 4. LoRa init
    lora_ok = initLoRa();
    if (!lora_ok) {
        dbg("LoRa: FAILED!");
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

    // Battery every 2s
    if (millis() - lastBat >= 2000) {
        lastBat = millis();
        batteryVoltage = readBattery();
        batteryMV = (uint16_t)(batteryVoltage * 1000);
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
