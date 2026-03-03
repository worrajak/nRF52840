/*
 * nRF52840 LoRa P2P Node — Low-Power TX-Only
 * Node ID: 115
 *
 * Protocol: RadioHead-compatible headers + XOR encryption + CRC16
 * Packet: [TO=0xFF][FROM=115][ID][FLAGS=0x00][encrypted_payload+CRC16]
 * Payload: N:115|S:seq|T:temp|H:hum|B:bat
 *
 * Power: ~0.2 mA average (deep sleep between TX via FreeRTOS tickless idle)
 * SPI: software bit-bang using nrf_gpio_* (n-able PINS_COUNT=34 bug)
 */

#include <Arduino.h>
#include <nrf_gpio.h>

// ============ PIN DEFINITIONS ============
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
#define TX_INTERVAL        60000  // 60 seconds

// Crypto key (must match STM32 nodes)
static const char* CRYPTO_KEY = "1234567890000000";
#define CRYPTO_KEY_LEN 16

// ============ GLOBALS ============
uint16_t batteryMV = 3300;
bool lora_ok = false;
uint8_t txSequenceNum = 0;
uint8_t rhMsgId = 0;

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

void rfm95WriteFifo(const uint8_t* data, uint8_t len) {
    gpioClear(RFM95_CS);
    delayMicroseconds(5);
    swSpiTransfer(0x00 | 0x80);
    for (uint8_t i = 0; i < len; i++) {
        swSpiTransfer(data[i]);
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
    rfm95WriteReg(0x01, 0x80);  // LoRa + Sleep
    delay(10);

    rfm95WriteReg(0x0E, 0x00);  // TX FIFO base
    rfm95WriteReg(0x0F, 0x00);  // RX FIFO base

    // Frequency: 923 MHz
    uint64_t frf = ((uint64_t)923000000 << 19) / 32000000;
    rfm95WriteReg(0x06, (frf >> 16) & 0xFF);
    rfm95WriteReg(0x07, (frf >> 8) & 0xFF);
    rfm95WriteReg(0x08, frf & 0xFF);

    rfm95WriteReg(0x1D, 0x72);  // BW=125kHz, CR=4/5
    rfm95WriteReg(0x1E, 0x74);  // SF7, CRC on

    // Preamble: 8 symbols
    rfm95WriteReg(0x20, 0x00);
    rfm95WriteReg(0x21, 0x08);

    rfm95WriteReg(0x39, 0x12);  // Sync word
    rfm95WriteReg(0x09, 0x8C);  // TX power: 12 dBm, PA_BOOST
    rfm95WriteReg(0x23, 0xFF);  // Max payload length
    rfm95WriteReg(0x24, 0x00);  // No frequency hopping

    return true;
}

void setModeSleep() {
    rfm95WriteReg(0x01, 0x80);  // LoRa + Sleep (~0.2 µA)
}

// ============ LORA INIT ============
bool initLoRa() {
    // Configure GPIO
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
    gpioClear(RFM95_RST);
    delay(10);
    gpioSet(RFM95_RST);
    delay(50);

    uint8_t version = rfm95ReadReg(0x42);
    Serial.print(F("LoRa ver: 0x"));
    Serial.println(version, HEX);

    if (version != 0x12) {
        Serial.println(F("LoRa: SPI FAIL!"));
        return false;
    }

    configureLoRaModem();
    setModeSleep();  // Start in sleep mode

    Serial.println(F("LoRa: OK"));
    return true;
}

// ============ LORA TX (RadioHead compatible) ============
void sendPacket() {
    // Build plaintext payload
    char payload[80];
    snprintf(payload, sizeof(payload), "N:%d|S:%d|T:%d|H:%d|B:%d",
             THIS_NODE_ADDRESS, txSequenceNum,
             0, 0, batteryMV);  // T/H = 0 (no sensor)

    int payloadLen = strlen(payload);

    // Encrypt
    uint8_t encrypted[payloadLen + 2];
    xorEncrypt(payload, payloadLen, encrypted);

    // CRC16 on encrypted data
    uint16_t crc = calculateCRC16(encrypted, payloadLen);
    encrypted[payloadLen] = (crc >> 8) & 0xFF;
    encrypted[payloadLen + 1] = crc & 0xFF;
    uint8_t encLen = payloadLen + 2;

    // RadioHead header (4 bytes) + encrypted payload + CRC
    uint8_t totalLen = 4 + encLen;
    uint8_t packet[totalLen];
    packet[0] = BROADCAST_ADDRESS;
    packet[1] = THIS_NODE_ADDRESS;
    packet[2] = rhMsgId++;
    packet[3] = 0x00;
    memcpy(&packet[4], encrypted, encLen);

    // TX
    setModeSleep();
    rfm95WriteReg(0x0D, 0x00);      // FIFO ptr
    rfm95WriteReg(0x22, totalLen);   // Payload length
    rfm95WriteFifo(packet, totalLen);
    rfm95WriteReg(0x01, 0x83);      // LoRa + TX

    // Wait for TxDone
    uint32_t txStart = millis();
    bool sent = false;
    while (millis() - txStart < 2000) {
        uint8_t irq = rfm95ReadReg(0x12);
        if (irq & 0x08) {
            rfm95WriteReg(0x12, 0xFF);  // Clear IRQ
            sent = true;
            break;
        }
        delay(1);
    }

    // Back to sleep
    setModeSleep();

    Serial.print(F("TX S:"));
    Serial.print(txSequenceNum);
    Serial.println(sent ? F(" OK") : F(" FAIL"));

    txSequenceNum++;
}

// ============ BATTERY ============
uint16_t readBatteryMV() {
    analogReadResolution(12);
    uint16_t raw = analogRead(BATTERY_PIN);
    return (uint16_t)(raw * REAL_VBAT_MV_PER_LSB);
}

// ============ SETUP ============
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println(F("\n=== nRF52840 LoRa Node 115 (Low Power) ==="));

    // Enable low-power mode
    NRF_POWER->TASKS_LOWPWR = 1;

    // LoRa init
    lora_ok = initLoRa();
    if (!lora_ok) {
        Serial.println(F("LoRa FAILED — halting"));
        while (1) delay(60000);
    }

    Serial.println(F("Ready. TX every 60s."));
}

// ============ LOOP ============
void loop() {
    batteryMV = readBatteryMV();
    sendPacket();

    // Deep sleep via FreeRTOS tickless idle (RTC1 + __WFE)
    // CPU draws ~2-5 µA, RFM95 in sleep ~0.2 µA
    delay(TX_INTERVAL);
}
