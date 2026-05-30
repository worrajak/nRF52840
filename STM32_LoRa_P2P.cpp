// ============================================================================
// STM32 LoRa P2P Sensor Node
// ============================================================================
// Hardware: STM32F103C8 (Blue Pill) + RFM95 LoRa Module
// Upload: ST-Link (SWD)
// Serial Ports:
//   - Serial1 (PA9=TX, PA10=RX) - Debug/Monitor @ 9600 baud
//   - Serial3 (PB10=TX, PB11=RX) - GPS Module (future use) @ 9600 baud
// BOOT0: Must be connected to GND (0) to run from Flash
// ============================================================================

#include <SPI.h>
#include <RH_RF95.h>
#include <RHReliableDatagram.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <RTClib.h>
#include <SdFat.h>

// STM32 HAL for ADC
#ifdef STM32F1xx
#include "stm32f1xx_hal.h"
#endif

#define SEALEVELPRESSURE_HPA (1013.25)

// Crypto Key (Must be 16 chars for AES-128)
const char* FIXED_CRYPTO_KEY = "1234567890000000";

// LoRa Configuration Structure (P2P)
struct LoRaConfig {
    long freq = 923E6;           // Frequency: 923 MHz
    int spreadingFactor = 7;     // SF: 7
    long bandwidth = 125000;     // BW: 125 kHz (125E3)
    int codingRate = 5;          // CR: 5
    int txPower = 12;            // TX Power: 12 dBm (balanced for range & power saving)
    int syncWord = 0x12;         // Sync Word: 0x12
} loraConfig;

// Hardware Serial Configuration
// Serial1 (PA9/PA10) - Debug/Monitor
// Serial3 (PB10/PB11) - GPS Module (future use)
HardwareSerial Serial1(PA10, PA9);   // RX, TX - Debug
HardwareSerial Serial3(PB11, PB10);  // RX, TX - GPS

// OLED display configuration
#define OLED_ADDRESS      0x3C  // Typical I2C address for SSD1306 OLED
bool oled_found = false;

// Sensor configuration
Adafruit_BME280 bme; // I2C #define BME280_ADDRESS (0x76)

// RTC DS1307 configuration (I2C Address: 0x68)
RTC_DS1307 rtc;
bool rtc_found = false;

// SD Card configuration (using Software SPI to avoid conflict with LoRa)
// CS (NSS) - PA4, MOSI - PA7, MISO - PA6, SCK - PA5
#define SD_CS_PIN PA4
SdFat sd;
FsFile logFile;
bool sd_found = false;
char logFilename[] = "DATA.CSV";

// Ping-Pong Buffer for data logging
#define BUFFER_SIZE 512  // Size of each buffer (256 bytes each)
#define MAX_RECORDS 16   // Max records per buffer (~32 bytes per record)

struct LogRecord {
    char timestamp[20];  // "YYYY-MM-DD HH:MM:SS"
    char type[10];       // "TRANSMIT", "RECEIVE"
    char data[60];       // Node ID, RSSI, Temp, etc.
};

struct PingPongBuffer {
    LogRecord records[MAX_RECORDS];
    uint8_t recordCount;
    bool full;
};

// Two buffers for ping-pong operation
PingPongBuffer bufferA, bufferB;
PingPongBuffer* activeBuffer = &bufferA;    // Current write buffer
PingPongBuffer* standbyBuffer = &bufferB;   // To be written to SD
bool bufferSwapNeeded = false;

float temperature = 0;
float humidity = 0;
uint16_t tempC_int = 0;
uint8_t hum_int = 0;
uint16_t vbat_int = 0;  // Battery voltage in mV

#define DEBUG   // ⚠️ Comment out to save additional power
#define SLEEP

#define led      PB5   // External LED
#define VBAT_CHANNEL 16  // STM32F103 Internal VBAT/2 monitor (ADC1_IN16)
#define VREF_CHANNEL 17  // STM32F103 Internal 1.2V Reference (ADC1_IN17) - for VDD measurement
#define VDD_SENSE_PIN PA0  // Voltage divider pin for measuring VDD (R1=22k, R2=10k from VDD to GND)

// LoRa Radio SPI2 pins (using SPI2 on PB13/PB14/PB15)
#define RFM95_CS   PB12  // NSS
#define RFM95_RST  PA8   // Reset
#define RFM95_INT  PB1   // DIO0

// SPI2 will be configured in setup()

// Create an instance of the radio driver
RH_RF95 rf95(RFM95_CS, RFM95_INT);

// Create a reliable datagram manager instance
RHReliableDatagram manager(rf95, 1);

// This node's address (0-255)
// ใช้เพื่อบอกว่าข้อความมาจาก node ไหน (ระบุตัวเอง)
#define THIS_NODE_ADDRESS 104

// Broadcast address - ส่งให้ทุก node รับได้ (0xFF = 255 = broadcast)
// ไม่ส่งไปหา node เฉพาะ แต่ "ตะโกน" ให้ทุกคนได้ยิน
#define BROADCAST_ADDRESS 0xFF  // RH_BROADCAST_ADDRESS

// Buffer for receiving messages
uint8_t rxbuf[RH_RF95_MAX_MESSAGE_LEN];
uint8_t rxlen = sizeof(rxbuf);

int txInterval = 30;  // seconds between transmissions (30 sec for testing)

// Collision Avoidance Variables
uint8_t retryCount = 0;
const uint8_t MAX_RETRIES = 5;
const uint16_t BASE_BACKOFF = 1000;  // 1 second base backoff in ms

// Sequence Number for this node (increments on each transmission)
uint8_t txSequenceNum = 0;

// Forward declarations
void initBME280();
void initDS1307();
void initSD();
void logDataToBuffer(const char* type, const char* data);
void writeBufferToSD();
void swapBuffers();
void logDataToSD(const char* dataType, const char* message);
void displayRTCTime();
void setup_vdd_sensor();
void check_flash_size();
void initOLED();
void printLoRaConfig();
void receiveData();
void transmitData();
void processMessageQueue();

// Relay Memory - Track received messages to avoid duplicate relay
#define RELAY_MEMORY_SIZE 10  // Track last 10 messages
struct RelayCache {
    uint8_t sourceNode;      // Which node sent the original message
    uint8_t sequenceNum;     // Sequence number of the message
    uint32_t timestamp;      // When we received it
} relayMemory[RELAY_MEMORY_SIZE];
uint8_t relayCacheIndex = 0;

// Message Queue for Delayed Relay (Store messages with random delay)
#define MESSAGE_QUEUE_SIZE 5  // Max 5 messages in queue
struct MessageQueue {
    uint8_t data[RH_RF95_MAX_MESSAGE_LEN];  // Message data
    uint8_t len;              // Message length
    uint32_t sendTime;        // When to send (timestamp)
    bool inUse;               // Whether this slot is used
} messageQueue[MESSAGE_QUEUE_SIZE];

uint8_t messageQueueIndex = 0;  // Current index in circular queue

// RX/TX Priority Control - Prevent collision by blocking TX during RX
volatile bool isRxBusy = false;        // Flag indicating receiver is active
uint32_t rxStartTime = 0;              // Timestamp when RX started
const uint32_t RX_HOLD_TIME = 50;      // Hold TX for 50ms after RX completes (safety margin)
const uint32_t RX_TIMEOUT = 5000;      // Max 5 seconds in RX state (failsafe)

// CRC16 calculation
uint16_t calculateCRC16(uint8_t* data, uint8_t length) {
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

struct {
  uint8_t Header1[2] = {0x01, 0x67};
  char temp[2];
  uint8_t Header2[2] = {0x01, 0x68};
  char humid[1];
  uint8_t Header3[2] = {0x01, 0x02};
  char vbat[2];  
  uint8_t Header4[2] = {0x01, 0x88};
  char gpsb[10];  
} mydata;

byte power;
byte rate2;

// GPS variables (placeholder)
float lat = 0;
float lon = 0;
float alt = 0;

#ifdef SLEEP
#include <stm32f1xx.h>

// Simple sleep functions using standard delay
uint32_t sleepTime;

void mdelay(int n, bool mode = false) {
  delay(n);  // Use standard Arduino delay
}

void msleep(uint32_t ms) {
  delay(ms);  // Use standard Arduino delay
}
#endif

unsigned long lastTxTime = 0;
bool txInProgress = false;

// I2C device scanner - checks if OLED or other I2C devices are available
bool checkI2CDevice(uint8_t address) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    return (error == 0); // Returns true if device found (no error)
}

// Initialize OLED display if available
void initOLED() {
    Wire.setSDA(PB7);
    Wire.setSCL(PB6);
    Wire.begin();
    delay(100);
    
    // Check for OLED at address 0x3C
    if (checkI2CDevice(OLED_ADDRESS)) {
        oled_found = true;
        Serial1.println(F("[OLED] OK"));
    } else {
        oled_found = false;
    }
}

// Print LoRa configuration to Serial
void printLoRaConfig() {
    Serial1.print(F("\n[LoRa] "));
    Serial1.print(loraConfig.freq / 1000000);
    Serial1.print(F("MHz SF"));
    Serial1.print(loraConfig.spreadingFactor);
    Serial1.print(F(" BW"));
    Serial1.print(loraConfig.bandwidth / 1000);
    Serial1.print(F("kHz CR"));
    Serial1.print(loraConfig.codingRate);
    Serial1.print(F(" Pwr"));
    Serial1.print(loraConfig.txPower);
    Serial1.print(F("dBm Node:"));
    Serial1.print(THIS_NODE_ADDRESS);
    Serial1.println(F(" [BROADCAST]"));
}

// Function to set LoRa frequency
void setLoRaFrequency(long freq) {
    loraConfig.freq = freq;
    Serial1.print(F("LoRa Frequency set to: "));
    Serial1.print(freq);
    Serial1.println(F(" Hz"));
    rf95.setFrequency(freq / 1000000.0);  // Convert to MHz
}

// Function to set Spreading Factor
void setSpreadingFactor(int sf) {
    if (sf >= 7 && sf <= 12) {
        loraConfig.spreadingFactor = sf;
        Serial1.print(F("Spreading Factor set to: SF"));
        Serial1.println(sf);
        rf95.setSpreadingFactor(sf);
    } else {
        Serial1.println(F("Invalid SF! Use 7-12"));
    }
}

// Function to set Bandwidth
void setBandwidth(long bw) {
    loraConfig.bandwidth = bw;
    Serial1.print(F("Bandwidth set to: "));
    Serial1.print(bw);
    Serial1.println(F(" Hz"));
    // Convert to bandwidth code (125kHz=7, 250kHz=8, 500kHz=9)
    uint8_t bwCode = 7;  // Default 125 kHz
    if (bw == 250000) bwCode = 8;
    else if (bw == 500000) bwCode = 9;
    rf95.setSignalBandwidth(bw);
}

// Function to set Coding Rate
void setCodingRate(int cr) {
    if (cr >= 5 && cr <= 8) {
        loraConfig.codingRate = cr;
        Serial1.print(F("Coding Rate set to: "));
        Serial1.println(cr);
        // RadioHead uses 4/(4+n) format, where n is 1-4 for CR 5-8
        rf95.setCodingRate4(cr - 4);
    } else {
        Serial1.println(F("Invalid CR! Use 5-8"));
    }
}

// Function to set TX Power
void setTXPower(int power) {
    if (power >= 5 && power <= 20) {
        loraConfig.txPower = power;
        Serial1.print(F("TX Power set to: "));
        Serial1.print(power);
        Serial1.println(F(" dBm"));
        rf95.setTxPower(power, false);  // false = PA_PIN not used
    } else {
        Serial1.println(F("Invalid TX Power! Use 5-20 dBm"));
    }
}

// Simple Encryption using XOR with key repetition (works with binary data)
void encrypt_data(const char* plaintext, int len, uint8_t* output) {
    int keyLen = strlen(FIXED_CRYPTO_KEY);
    
    for (int i = 0; i < len; i++) {
        output[i] = plaintext[i] ^ FIXED_CRYPTO_KEY[i % keyLen];  // XOR operation
    }
}

// Simple Decryption using XOR with key repetition (same operation as encryption)
void decrypt_data(const uint8_t* ciphertext, int len, char* output) {
    int keyLen = strlen(FIXED_CRYPTO_KEY);
    
    for (int i = 0; i < len; i++) {
        output[i] = ciphertext[i] ^ FIXED_CRYPTO_KEY[i % keyLen];  // XOR operation
    }
    output[len] = '\0';  // Null terminate
}

// Read Supply Voltage (VDD)
// Battery voltage measurement disabled - STM32duino does not support internal VREF
// Testing showed CH16 (VBAT) and CH17 (VREF) both return 0
// External voltage divider method available but not used
// Returns nominal 3.3V for compatibility
uint16_t readSupplyVoltage() {
    // Return nominal voltage without measurement
    return 3300;  // 3.3V nominal
    
    /* Voltage divider method (requires hardware):
       VDD --[22k]--+--[10k]--GND, PA0 to junction
       
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 4; i++) {
        sum += analogRead(VDD_SENSE_PIN);
        delay(2);
    }
    uint16_t adcValue = sum / 4;
    if (adcValue < 100) return 3300;
    uint16_t supplyMV = (adcValue * 10560UL) / 4096;
    if (supplyMV < 2700 || supplyMV > 3600) return 3300;
    return supplyMV;
    */
}

// Sensor monitoring
bool bme280_available = false;
uint32_t bme280_last_read = 0;
uint32_t bme280_last_error = 0;
const uint32_t BME280_TIMEOUT = 30000;  // 30 seconds timeout
const uint32_t BME280_RETRY_INIT = 60000;  // Retry init every 60 seconds
uint8_t bme280_error_count = 0;
const uint8_t BME280_MAX_ERRORS = 3;  // Max consecutive errors before retry

// Simulated data
bool using_simulated_data = false;
float sim_temp = 25.0;
float sim_humidity = 60.0;
uint16_t sim_battery = 120;

void readData() {
    // Try to read from BME280
    bool bme280_ok = false;
    uint32_t now = millis();
    
    // Check if we should retry BME280 initialization
    if (!bme280_available && (now - bme280_last_error) > BME280_RETRY_INIT) {
        initBME280();
    }
    
    if (bme280_available) {
        // Try to read sensor data with timeout protection
        uint32_t read_start = millis();
        temperature = bme.readTemperature();
        humidity = bme.readHumidity();
        uint32_t read_time = millis() - read_start;
        
        // Check for I2C communication timeout (excessive read time)
        if (read_time > 5000) {
            bme280_error_count++;
            Serial1.print(F("[SENSOR] BME280 slow read ("));
            Serial1.print(read_time);
            Serial1.println(F(" ms)"));
        } else {
            // Validate readings (sanity check)
            if (temperature > -50 && temperature < 100 && humidity >= 0 && humidity <= 100) {
                bme280_ok = true;
                bme280_last_read = now;
                bme280_error_count = 0;  // Reset error counter on success
                using_simulated_data = false;
            } else {
                // BME280 returned invalid data
                bme280_error_count++;
                Serial1.print(F("[SENSOR] BME280 invalid data: "));
                Serial1.print(temperature);
                Serial1.print(F(" C, "));
                Serial1.print(humidity);
                Serial1.println(F(" %"));
            }
        }
        
        // If too many consecutive errors, mark as unavailable and retry init
        if (bme280_error_count >= BME280_MAX_ERRORS) {
            bme280_available = false;
            bme280_last_error = now;
            Serial1.println(F("[SENSOR] BME280 errors - will retry init later"));
        }
        
        // Also check absolute timeout since last successful read
        if ((now - bme280_last_read) > BME280_TIMEOUT && bme280_last_read > 0) {
            bme280_available = false;
            bme280_last_error = now;
            bme280_error_count = 0;
            Serial1.println(F("[SENSOR] BME280 timeout - using simulated"));
        }
    }
    
    // If BME280 failed, use simulated data
    if (!bme280_ok) {
        using_simulated_data = true;
        
        // Generate realistic simulated values
        temperature = 22.0 + (random(-50, 50) / 100.0);  // 21.5-22.5°C
        humidity = 55.0 + (random(-100, 100) / 100.0);   // 54-56%
        sim_temp = temperature;
        sim_humidity = humidity;
    }
    
    tempC_int = temperature * 10;
    hum_int = humidity * 2;
    
    // Read supply voltage (returns nominal 3300mV - measurement disabled)
    vbat_int = readSupplyVoltage();
    sim_battery = vbat_int;
    
    mydata.temp[0] = tempC_int >> 8;
    mydata.temp[1] = tempC_int;
    mydata.humid[0] = hum_int;
    mydata.vbat[0] = vbat_int >> 8;
    mydata.vbat[1] = vbat_int;

#ifdef DEBUG
    if (using_simulated_data) {
        Serial1.print(F("[SIM] "));
    }
    Serial1.print(temperature);
    Serial1.print(F(" "));
    Serial1.print(humidity);
    Serial1.print(F(" "));
    Serial1.println(vbat_int);
#endif
}

void initBME280() {
    bme280_available = false;
    bme280_error_count = 0;
    
    // Try I2C address 0x77 first
    if (bme.begin(0x77)) {
        bme280_available = true;
        bme280_last_read = millis();
        bme280_last_error = 0;
        Serial1.println(F("[INIT] BME280 OK (0x77)"));
        return;
    }
    
    // Try alternative address 0x76
    if (bme.begin(0x76)) {
        bme280_available = true;
        bme280_last_read = millis();
        bme280_last_error = 0;
        Serial1.println(F("[INIT] BME280 OK (0x76)"));
        return;
    }
    
    // Check if Wire is properly initialized
    Serial1.println(F("[INIT] BME280 NOT FOUND - check I2C connections"));
    bme280_available = false;
    bme280_last_error = millis();
    using_simulated_data = true;
}

// Initialize DS1307 RTC
void initDS1307() {
    rtc_found = false;
    
    if (rtc.begin()) {
        rtc_found = true;
        Serial1.println(F("[INIT] DS1307 RTC OK"));
        
        // Check if RTC is running
        if (!rtc.isrunning()) {
            Serial1.println(F("[WARN] RTC not running - setting time"));
            // Set to compile time
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
        displayRTCTime();
        return;
    }
    
    Serial1.println(F("[INIT] DS1307 RTC NOT FOUND - check I2C connections"));
    rtc_found = false;
}

// Display current time from DS1307
void displayRTCTime() {
    if (!rtc_found) {
        return;
    }
    
    DateTime now = rtc.now();
    
    Serial1.print(F("[RTC] "));
    // Format: YYYY-MM-DD HH:MM:SS
    if (now.year() < 10) Serial1.print("0");
    Serial1.print(now.year());
    Serial1.print("-");
    if (now.month() < 10) Serial1.print("0");
    Serial1.print(now.month());
    Serial1.print("-");
    if (now.day() < 10) Serial1.print("0");
    Serial1.print(now.day());
    Serial1.print(" ");
    if (now.hour() < 10) Serial1.print("0");
    Serial1.print(now.hour());
    Serial1.print(":");
    if (now.minute() < 10) Serial1.print("0");
    Serial1.print(now.minute());
    Serial1.print(":");
    if (now.second() < 10) Serial1.print("0");
    Serial1.println(now.second());
}

// Initialize SD Card
void initSD() {
    sd_found = false;
    
    // Use Software SPI to avoid conflict with LoRa
    // CS=PA4, MOSI=PA7, MISO=PA6, SCK=PA5
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    
    // Try EIGHTH_SPEED first (slowest, most reliable), then QUARTER_SPEED
    if (sd.begin(SD_CS_PIN, SPI_EIGHTH_SPEED)) {
        sd_found = true;
        Serial1.println(F("[SD] OK (EIGHTH_SPEED)"));
    } else if (sd.begin(SD_CS_PIN, SPI_QUARTER_SPEED)) {
        sd_found = true;
        Serial1.println(F("[SD] OK (QUARTER_SPEED)"));
    } else {
        Serial1.println(F("[SD] NOT FOUND"));
        return;
    }
    
    // Write header if file is new
    if (!sd.exists(logFilename)) {
        logFile.open(logFilename, O_WRONLY | O_CREAT);
        if (logFile) {
            logFile.println(F("Timestamp,Type,Node,RSSI,Temp,Humidity,Battery,Message"));
            logFile.close();
        }
    }
}

// Add data to ping-pong buffer
void logDataToBuffer(const char* type, const char* data) {
    if (!rtc_found || activeBuffer->recordCount >= MAX_RECORDS) {
        return;
    }
    
    // Get current timestamp
    DateTime now = rtc.now();
    LogRecord* record = &activeBuffer->records[activeBuffer->recordCount];
    
    // Format timestamp: YYYY-MM-DD HH:MM:SS
    snprintf(record->timestamp, sizeof(record->timestamp),
             "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    
    // Copy type and data
    strncpy(record->type, type, sizeof(record->type) - 1);
    record->type[sizeof(record->type) - 1] = '\0';
    
    strncpy(record->data, data, sizeof(record->data) - 1);
    record->data[sizeof(record->data) - 1] = '\0';
    
    activeBuffer->recordCount++;
    
    // Check if buffer is full - if so, trigger swap
    if (activeBuffer->recordCount >= MAX_RECORDS) {
        activeBuffer->full = true;
        bufferSwapNeeded = true;
    }
}

// Swap active and standby buffers
void swapBuffers() {
    PingPongBuffer* temp = activeBuffer;
    activeBuffer = standbyBuffer;
    standbyBuffer = temp;
    activeBuffer->recordCount = 0;
    activeBuffer->full = false;
}

// Write buffer contents to SD card
void writeBufferToSD() {
    if (!sd_found || standbyBuffer->recordCount == 0) {
        return;
    }
    
    // Open log file in append mode
    logFile.open(logFilename, O_WRONLY | O_APPEND);
    if (!logFile) {
        Serial1.println(F("[SD] Failed to open file"));
        return;
    }
    
    // Write all records from standby buffer
    for (uint8_t i = 0; i < standbyBuffer->recordCount; i++) {
        LogRecord* record = &standbyBuffer->records[i];
        logFile.print(record->timestamp);
        logFile.print(",");
        logFile.print(record->type);
        logFile.print(",");
        logFile.println(record->data);
    }
    
    logFile.close();
    
    Serial1.print(F("[SD-LOG] Wrote "));
    Serial1.print(standbyBuffer->recordCount);
    Serial1.println(F(" records"));
    
    // Clear standby buffer
    standbyBuffer->recordCount = 0;
    standbyBuffer->full = false;
}

// Legacy function for compatibility
void logDataToSD(const char* dataType, const char* message) {
    // Now just calls the buffer-based logging
    char fullMsg[80];
    snprintf(fullMsg, sizeof(fullMsg), "%s,%s", dataType, message);
    logDataToBuffer(dataType, message);
}

void setup_vdd_sensor() {
    // Configure ADC reference voltage to VREF (3.3V internal)
    // This enables accurate measurement of internal VBAT
    // For STM32: use DEFAULT reference (VREF = VDDA = 3.3V)
    
    // Enable temperature sensor and VBAT for ADC
    // For STM32F103, we just need to start reading to enable VBAT channel
    delay(10);
}

// Check actual Flash size of STM32
void check_flash_size() {
    // Read Flash Size register at 0x1FFFF7E0
    uint16_t flash_size = *(__IO uint16_t*)(0x1FFFF7E0);
    
    Serial1.print(F("STM32 Flash Size: "));
    Serial1.print(flash_size);
    Serial1.println(F(" KB"));
    
    if (flash_size == 128) {
        Serial1.println(F("*** 128KB Flash detected! ***"));
        Serial1.println(F("You can use more memory by changing platformio.ini"));
    } else if (flash_size == 64) {
        Serial1.println(F("64KB Flash (official spec)"));
    } else {
        Serial1.print(F("Unusual flash size: "));
        Serial1.println(flash_size);
    }
}

// Exponential Backoff with Jitter (CSMA/CA-like)
uint16_t calculateBackoff() {
    // Exponential backoff: BASE_BACKOFF * 2^retryCount + random jitter
    uint16_t backoff = BASE_BACKOFF * (1 << retryCount);  // 2^retryCount
    uint16_t jitter = random(0, backoff);  // Random jitter (0 to backoff)
    uint16_t totalBackoff = backoff + jitter;
    
    Serial1.print(F("[BACKOFF] Retry "));
    Serial1.print(retryCount);
    Serial1.print(F(" - Waiting "));
    Serial1.print(totalBackoff);
    Serial1.println(F(" ms"));
    
    return totalBackoff;
}

// Channel Activity Detection - Check if channel is clear before transmit
bool isChannelClear() {
    // Temporarily disabled - use direct transmission for testing
    // CAD can be enabled later when system is stable
    
    // Option 1: Always return true (skip CAD)
    return true;
    
    /* Option 2: Use proper RSSI reading (uncomment when needed)
    // Read current RSSI properly
    int currentRSSI = rf95.rssiRead();
    
    Serial1.print(F("[CAD] Current RSSI: "));
    Serial1.println(currentRSSI);
    
    // If RSSI is stronger than -90 dBm, channel might be busy
    if (currentRSSI > -90) {
        Serial1.println(F("[CAD] Channel busy"));
        return false;
    }
    return true;
    */
}

// Check if message was already relayed (Duplicate Detection)
bool isMessageDuplicate(uint8_t sourceNode, uint8_t seqNum) {
    for (int i = 0; i < RELAY_MEMORY_SIZE; i++) {
        if (relayMemory[i].sourceNode == sourceNode && relayMemory[i].sequenceNum == seqNum) {
            // Check if it's still within the "remember" window (5 minutes)
            uint32_t ageInSeconds = (millis() - relayMemory[i].timestamp) / 1000;
            if (ageInSeconds < 300) {  // 5 minutes = 300 seconds
                return true;  // This is a duplicate
            }
        }
    }
    return false;  // Not a duplicate
}

// Add message to relay cache
void addToRelayCache(uint8_t sourceNode, uint8_t seqNum) {
    relayMemory[relayCacheIndex].sourceNode = sourceNode;
    relayMemory[relayCacheIndex].sequenceNum = seqNum;
    relayMemory[relayCacheIndex].timestamp = millis();
    
    // Move to next cache slot (circular buffer)
    relayCacheIndex = (relayCacheIndex + 1) % RELAY_MEMORY_SIZE;
}

// Add message to queue with random delay (1-3 seconds)
void addToMessageQueue(uint8_t* buf, uint8_t len) {
    // Find empty slot in queue
    for (int i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
        if (!messageQueue[i].inUse) {
            // Copy message to queue
            memcpy(messageQueue[i].data, buf, len);
            messageQueue[i].len = len;
            
            // Calculate send time: random 1-3 seconds from now
            uint32_t delayMs = 1000 + random(2000);  // 1000-3000 ms
            messageQueue[i].sendTime = millis() + delayMs;
            messageQueue[i].inUse = true;
            
            Serial1.print(F("[QUEUE] Added msg, delay: "));
            Serial1.print(delayMs);
            Serial1.println(F(" ms"));
            return;
        }
    }
    
    Serial1.println(F("[QUEUE] FULL - message dropped"));
}

// Sanitize payload - remove garbage characters and duplicate headers
String sanitizePayload(String input) {
    String clean = "";
    
    // Remove non-printable characters
    for (int i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c >= 32 && c <= 126) {  // Printable ASCII only
            clean += c;
        } else {
            break;  // Stop at first garbage character
        }
    }
    
    // Check for repeated "N:" header (concatenated messages)
    int firstN = clean.indexOf("N:");
    if (firstN != -1) {
        int secondN = clean.indexOf("N:", firstN + 2);
        if (secondN != -1) {
            // Truncate before second header
            clean = clean.substring(0, secondN);
        }
    }
    
    return clean;
}

// Process message queue - send delayed messages
void processMessageQueue() {
    uint32_t now = millis();
    
    for (int i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
        if (messageQueue[i].inUse && now >= messageQueue[i].sendTime) {
            // Time to send this message
            Serial1.print(F("[QUEUE] Sending queued message "));
            Serial1.println(i);
            
            // Send the message
            if (manager.sendtoWait(messageQueue[i].data, messageQueue[i].len, BROADCAST_ADDRESS)) {
                Serial1.println(F("[QUEUE] Sent OK"));
            } else {
                Serial1.println(F("[QUEUE] Send Failed"));
            }
            
            // Mark slot as unused
            messageQueue[i].inUse = false;
        }
    }
}

// Transmit data with Collision Avoidance and Sequence Number
void transmitData() {
    // CHECK RX PRIORITY: Don't transmit if receiver is busy
    if (isRxBusy) {
        Serial1.println(F("[TX-HOLD] RX busy..."));
        return;  // Skip TX, try again next loop
    }
    
    readData();
    
    // Display current date/time when sending data
    displayRTCTime();
    
    // Create payload string in simplified P2P format: N:NODE|S:SEQ|T:TEMP|H:HUM|B:BAT
    // This format is compatible with Node 108 (PZEM) for unified mesh
    String payload = "";
    payload += "N:";
    payload += THIS_NODE_ADDRESS;  // Node ID (104)
    payload += "|S:";
    payload += txSequenceNum;      // Sequence Number
    
    // Add BME280 temperature (as integer)
    payload += "|T:";
    payload += (int)temperature;
    
    // Add BME280 humidity (as integer)
    payload += "|H:";
    payload += (int)humidity;
    
    // Add battery voltage
    payload += "|B:";
    payload += vbat_int;
    
    // Debug: Show plaintext before encryption
    Serial1.print(F("[TX "));
    Serial1.print(THIS_NODE_ADDRESS);
    Serial1.print(F("] Plaintext: "));
    Serial1.println(payload);
    
    // Encrypt data using XOR
    int payloadLen = payload.length();
    uint8_t txbuf[payloadLen + 2];  // +2 for CRC
    encrypt_data(payload.c_str(), payloadLen, txbuf);
    
    // Debug: Show encrypted length
    Serial1.print(F("  Encrypted length: "));
    Serial1.println(payloadLen);
    
    // Calculate CRC16 checksum on encrypted data
    uint16_t crc = calculateCRC16(txbuf, payloadLen);
    
    // Append CRC16 (2 bytes)
    txbuf[payloadLen] = (crc >> 8) & 0xFF;      // High byte
    txbuf[payloadLen + 1] = crc & 0xFF;         // Low byte
    
    uint16_t totalLen = payloadLen + 2;
    
    // Retry logic with backoff
    retryCount = 0;
    bool txSuccess = false;
    
    while (retryCount <= MAX_RETRIES && !txSuccess) {
        // Random jitter before transmission (0-500ms)
        uint16_t preDelay = random(0, 500);
        delay(preDelay);
        
        // Check if channel is clear before sending
        if (isChannelClear()) {
            Serial1.print(F("  Sending..."));
            
            if (using_simulated_data) {
                Serial1.print(F(" [SIM]"));
            }
            Serial1.println();
            
            // Send to broadcast address (all nodes receive)
            if (manager.sendtoWait(txbuf, totalLen, BROADCAST_ADDRESS)) {
                // Success!
                Serial1.print(F("  [OK] Seq "));
                Serial1.println(txSequenceNum);
                
                // Log to buffer (non-blocking)
                char logMsg[100];
                snprintf(logMsg, sizeof(logMsg), "TX,%d,N/A,%d,%d,%s", 
                         THIS_NODE_ADDRESS, (int)temperature, (int)humidity, (using_simulated_data ? "SIM" : "BME"));
                logDataToBuffer("TRANSMIT", logMsg);
                
                // Blink LED 1 time for successful transmission
                digitalWrite(led, LOW);
                delay(100);
                digitalWrite(led, HIGH);
                
                txSuccess = true;
                txSequenceNum++;  // Increment for next transmission
                retryCount = 0;   // Reset for next transmission
            } else {
                // Timeout or no ACK - backoff and retry
                retryCount++;
                if (retryCount <= MAX_RETRIES) {
                    uint16_t backoffTime = calculateBackoff();
                    delay(backoffTime);
                } else {
                    Serial1.println(F("  [FAIL] Max retry"));
                }
            }
        } else {
            // Channel busy - wait and retry
            retryCount++;
            if (retryCount <= MAX_RETRIES) {
                uint16_t backoffTime = calculateBackoff();
                delay(backoffTime);
            } else {
                Serial1.println(F("  [FAIL] Ch busy"));
            }
        }
    }
    
    lastTxTime = millis();
}

// Receive and process incoming data
void receiveData() {
    if (manager.available()) {
        // Mark that we're receiving - this will block TX during RX
        isRxBusy = true;
        rxStartTime = millis();
        
        uint8_t len = sizeof(rxbuf);
        uint8_t from;
        
        if (manager.recvfromAck(rxbuf, &len, &from)) {
            int rssi = rf95.lastRssi();
            
            // Verify CRC16 checksum (last 2 bytes)
            if (len < 2) {
                isRxBusy = false;
                return;
            }
            
            uint16_t receivedCRC = ((uint16_t)rxbuf[len - 2] << 8) | rxbuf[len - 1];
            uint16_t calculatedCRC = calculateCRC16(rxbuf, len - 2);
            
            if (receivedCRC != calculatedCRC) {
                return;
            }
            
            // Decrypt the data (excluding CRC bytes)
            int dataLen = len - 2;  // Exclude CRC
            char decrypted[dataLen + 1];  // +1 for null terminator
            decrypt_data(rxbuf, dataLen, decrypted);
            
            // Sanitize payload - remove garbage and duplicate headers
            String decryptedStr = String(decrypted);
            String cleaned = sanitizePayload(decryptedStr);
            bool wasCleaned = (cleaned != decryptedStr);
            
            // If sanitization occurred, re-encrypt and update buffer
            if (wasCleaned) {
                Serial1.println(F("[SANITIZED]"));
                
                // Re-encrypt cleaned payload
                int cleanedLen = cleaned.length();
                encrypt_data(cleaned.c_str(), cleanedLen, rxbuf);
                
                // Recalculate CRC
                uint16_t newCRC = calculateCRC16(rxbuf, cleanedLen);
                
                // Update buffer with CRC
                rxbuf[cleanedLen] = (newCRC >> 8) & 0xFF;
                rxbuf[cleanedLen + 1] = newCRC & 0xFF;
                len = cleanedLen + 2;
                
                // Use cleaned payload
                strncpy(decrypted, cleaned.c_str(), dataLen);
                decrypted[cleaned.length()] = '\0';
                decryptedStr = cleaned;  // Update decryptedStr too
            }
            
            // Parse Node ID and Sequence Number from decrypted payload
            uint8_t sourceNode = 0;
            uint8_t seqNum = 0;
            bool isSim = false;
            
            // Extract Node ID
            int nPos = decryptedStr.indexOf("N:");
            if (nPos != -1) {
                int pipePos = decryptedStr.indexOf("|", nPos);
                if (pipePos != -1) {
                    String nodeStr = decryptedStr.substring(nPos + 2, pipePos);
                    sourceNode = nodeStr.toInt();
                }
            }
            
            // Extract Sequence Number
            int sPos = decryptedStr.indexOf("S:");
            if (sPos != -1) {
                int pipePos = decryptedStr.indexOf("|", sPos);
                if (pipePos != -1) {
                    String seqStr = decryptedStr.substring(sPos + 2, pipePos);
                    seqNum = seqStr.toInt();
                }
            }
            
            // Check if simulated data
            if (decryptedStr.indexOf("SIM|") != -1) {
                isSim = true;
            }
            
            // Display received data
            Serial1.print(F("[RX from:"));
            Serial1.print(from);
            Serial1.print(F(" src:"));
            Serial1.print(sourceNode);
            Serial1.print(F("] S"));
            Serial1.print(seqNum);
            if (isSim) {
                Serial1.print(F(" [SIM]"));
            }
            Serial1.print(F(" RSSI:"));
            Serial1.println(rssi);
            
            // Display decrypted payload
            Serial1.print(F("PAYLOAD: "));
            Serial1.println(decrypted);
            
            // Log to buffer (non-blocking)
            char logMsg[150];
            snprintf(logMsg, sizeof(logMsg), "RX,%d,%d,%d,,%s", 
                     sourceNode, from, rssi, (isSim ? "SIM" : "DATA"));
            logDataToBuffer("RECEIVE", logMsg);
            
            // Blink LED 2 times for received message
            for (int i = 0; i < 2; i++) {
                digitalWrite(led, LOW);
                delay(100);
                digitalWrite(led, HIGH);
                delay(100);
            }
            
            // Check for duplicate message
            if (isMessageDuplicate(sourceNode, seqNum)) {
                Serial1.println(F("[DUP-SKIP]"));
                return;
            }
            
            // Add to relay cache
            addToRelayCache(sourceNode, seqNum);
            
            // Forward logic: Forward if RSSI <= -100 (weak to medium signal)
            if (rssi > -100) {
                Serial1.println(F("[SKIP] RSSI>-100"));
            } else {
                // Add message to queue for delayed relay (1-3 seconds random)
                Serial1.print(F("[FWD "));
                Serial1.print(sourceNode);
                Serial1.print(F("] S"));
                Serial1.print(seqNum);
                if (isSim) {
                    Serial1.print(F(" [SIM]"));
                }
                Serial1.println();
                
                // Store message for delayed relay
                uint8_t fwdbuf[len];
                memcpy(fwdbuf, rxbuf, len);
                addToMessageQueue(fwdbuf, len);
            }
        } else {
            // recvfromAck failed
            isRxBusy = false;
        }
    } else {
        // No message available, clear RX busy flag after hold time
        if (isRxBusy && (millis() - rxStartTime) >= RX_HOLD_TIME) {
            isRxBusy = false;
        }
    }
    
    // Failsafe: If stuck in RX state too long, force clear
    if (isRxBusy && (millis() - rxStartTime) > RX_TIMEOUT) {
        Serial1.println(F("[RX-TIMEOUT] Force clear"));
        isRxBusy = false;
    }
}

void setup() {
    setup_vdd_sensor();
    
    // Initialize Serial first
    Serial1.begin(9600);  // 9600 baud, 8-N-1 (8 data bits, No parity, 1 stop bit)
    delay(1000);  // Wait for serial to initialize
    
    // Initialize LED
    pinMode(led, OUTPUT);
    digitalWrite(led, HIGH);  // LED off (active low)
    
    // Blink LED 2 times to indicate board is working
    for (int i = 0; i < 2; i++) {
        digitalWrite(led, LOW);   // LED on
        delay(100);
        digitalWrite(led, HIGH);  // LED off
        delay(100);
    }
    
    Serial1.println(F("\n\n==============================="));
    Serial1.println(F("STM32 LoRa P2P Sensor Node"));
    Serial1.println(F("===============================\n"));
    
    // Check Flash size
    check_flash_size();
    
    // Check for OLED display
    initOLED();
    
    // Initialize SPI with SPI2 pins (for LoRa)
    SPI.setMOSI(PB15);
    SPI.setMISO(PB14);
    SPI.setSCLK(PB13);
    SPI.begin();
    pinMode(RFM95_INT, INPUT);
    
    // Initialize LoRa radio
    pinMode(RFM95_RST, OUTPUT);
    digitalWrite(RFM95_RST, HIGH);
    delay(100);
    digitalWrite(RFM95_RST, LOW);
    delay(10);
    digitalWrite(RFM95_RST, HIGH);
    delay(100);
    
    if (!manager.init()) {
        Serial1.println(F("[ERR] LoRa init failed - check wiring"));
        while (1) {
            digitalWrite(led, LOW);
            delay(100);
            digitalWrite(led, HIGH);
            delay(100);
        };
    }
    Serial1.println(F("[LoRa] OK"));
    
    // Apply configuration
    rf95.setFrequency(loraConfig.freq / 1000000.0);  // Convert Hz to MHz
    rf95.setSpreadingFactor(loraConfig.spreadingFactor);
    rf95.setSignalBandwidth(loraConfig.bandwidth);
    rf95.setCodingRate4(loraConfig.codingRate - 4);
    rf95.setTxPower(loraConfig.txPower, false);
    
    // Print configuration
    printLoRaConfig();
    
    // Initialize BME280 sensor with custom I2C pins
    Wire.setSDA(PB7);
    Wire.setSCL(PB6);
    Wire.begin();
    delay(100);
    
    // Initialize BME280 sensor
    initBME280();
    
    // Initialize DS1307 RTC
    initDS1307();
    
    // Initialize SD Card
    initSD();
    
    lastTxTime = millis();
    
#ifdef DEBUG
    Serial1.println(F("Setup complete - entering main loop"));
#endif
}

void loop() {
    // *** RX HAS PRIORITY - Process incoming messages FIRST ***
    // Check for incoming messages (IMPORTANT: Keep this first for relay functionality)
    receiveData();
    
    // Process message queue - send delayed relay messages
    processMessageQueue();
    
    // Check if it's time to transmit (only if RX is not busy)
    if (millis() - lastTxTime >= txInterval * 1000) {
        transmitData();
    }
    
    // Ping-pong buffer management: Swap and flush when buffer is full
    if (bufferSwapNeeded) {
        // Don't write immediately; let it happen in background when not busy
        static unsigned long lastBufferCheck = 0;
        if (millis() - lastBufferCheck >= 500) {  // Check every 500ms
            swapBuffers();                          // Exchange active and standby buffers
            writeBufferToSD();                       // Flush standby buffer to SD in one operation
            lastBufferCheck = millis();
        }
    }
    
    // Display current time every 60 seconds
    static unsigned long lastTimeDisplay = 0;
    if (millis() - lastTimeDisplay >= 60000) {
        displayRTCTime();
        lastTimeDisplay = millis();
    }
    
    // Minimal delay - reduced to save power while still allowing RX interrupt
    delay(10);  // Very short delay to reduce power consumption
}

// GPS functions (stubbed out)
static void gpsdump() {
    // GPS dump function stub
}

static bool feedgps() {
    // GPS feed function placeholder
    return false;
}

