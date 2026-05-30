// ============================================================================
// nRF52840 LoRa P2P Sensor Node (Adapted from STM32 LoRa P2P)
// ============================================================================
// Hardware: Adafruit Feather nRF52840 + RFM95 LoRa Module + BME280
// Serial: USB CDC @ 115200 baud
// BLE: Bluefruit BLE Service for status display
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// BLE Includes
#include <bluefruit.h>

#define SEALEVELPRESSURE_HPA (1013.25)

// ============= GPIO Pin Configuration for nRF52840 =============
// Arduino Pin = nRF52840 GPIO (Port + Offset)
// Port 0: pins 0-31 (add 0)
// Port 1: pins 32-63 (add 32)
// Native notation: P0.xx or P1.xx
//
// RFM95 LoRa Module Pins (SPI)
#define RFM95_CS    13   // P1.13 (32+13=45 in native, but Arduino uses 13)
#define RFM95_INT   29   // P0.29 - Interrupt (DIO0)
#define RFM95_RST   9    // P0.09 - Reset
#define RFM95_DIO1  10   // P0.10 - DIO1/IRQ

// SPI Pins
#define SPI_MOSI    15   // P1.15 (32+15=47 in native, but Arduino uses 15)
#define SPI_MISO    2    // P0.02 - SPI Data In (MISO)
#define SPI_SCK     11   // P1.11 (32+11=43 in native, but Arduino uses 11)

// I2C Pins for BME280
#define I2C_SDA     4    // P1.04 (32+4=36 in native, but Arduino uses 4)
#define I2C_SCL     11   // P0.11 - I2C Clock

// LED and Status
#define LED_STATUS  14   // P0.14 - Status LED
#define LED_RX      27   // P0.27 - RX Indicator
#define LED_TX      28   // P0.28 - TX Indicator
#define LED_P0_15   15   // P0.15 - LED P0.15

// ============= LoRa Configuration Structure (from STM32) =============
struct LoRaConfig {
    long freq = 923E6;           // Frequency: 923 MHz (923,000,000 Hz)
    int spreadingFactor = 7;     // SF: 7 (fast mode)
    long bandwidth = 125000;     // BW: 125 kHz
    int codingRate = 5;          // CR: 5 (4/5)
    int txPower = 12;            // TX Power: 12 dBm (balanced)
    int syncWord = 0x12;         // Sync Word: 0x12
} loraConfig;

// ============= Global Objects =============
Adafruit_BME280 bme280;
Adafruit_SSD1306 display(128, 64, &Wire, -1);  // 128x64 OLED display on I2C
uint32_t packetCounter = 0;
unsigned long lastTxTime = 0;
unsigned long lastSensorRead = 0;
unsigned long lastDisplayUpdate = 0;

// ============= Configuration Constants =============
const unsigned long SENSOR_READ_INTERVAL = 5000;  // 5 seconds
const int TX_INTERVAL = 30;  // 30 seconds between transmissions

// Sensor data variables
float temperature = 0;
float humidity = 0;
float pressure = 0;
uint16_t vbat_mv = 3300;  // Battery voltage in mV

// Node Address Configuration
#define THIS_NODE_ADDRESS 104      // This node ID (1-255)
#define BROADCAST_ADDRESS 0xFF     // Broadcast to all nodes (255)

// ============= BLE Service and Characteristics =============
BLEService loraService("18D0");  // Custom LoRa Service UUID

// BLE Characteristics for Status and Errors
BLECharacteristic systemStatusChar("18D0");    // System Status
BLECharacteristic sensorStatusChar("18D1");    // Sensor Init Status
BLECharacteristic loraStatusChar("18D2");      // LoRa Init Status
BLECharacteristic sensorDataChar("18D3");      // Sensor Data (String)
BLECharacteristic loraSignalChar("18D4");      // RSSI/SNR (String)
BLECharacteristic packetCountChar("18D5");     // Packet Counter (String)
BLECharacteristic loraConfigChar("18D6");      // LoRa Config (String)
BLECharacteristic batteryChar("18D7");         // Battery Voltage (String)
BLECharacteristic errorLogChar("18D8");        // Error Log (String)

bool ble_connected = false;
bool bme280_ok = false;
bool lora_ok = false;

// ============= Function Declarations =============
void initializeLoRa();
void initializeBME280();
void initializeSSD1306();
void initializeBLE();
void setupBLECharacteristics();
void connectCallback(uint16_t conn_handle);
void disconnectCallback(uint16_t conn_handle, uint8_t reason);
void readData();
void transmitData();
void receiveData();
void updateBLEStatus();
void updateBLEStatus(const char* module, const char* status);
void updateDisplay();
void displaySensorData();
void printLoRaConfig();
void setLoRaFrequency(long freq);
void setSpreadingFactor(int sf);
void setBandwidth(long bw);
void setCodingRate(int cr);
void setTXPower(int power);
uint16_t readSupplyVoltage();
void displayInitStatus(const char* module, const char* status);

// ============= Setup =============
void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n========================================");
    Serial.println("nRF52840 LED Blink Test");
    Serial.println("========================================\n");
    
    // Initialize GPIO
    pinMode(LED_STATUS, OUTPUT);
    pinMode(LED_RX, OUTPUT);
    pinMode(LED_TX, OUTPUT);
    pinMode(LED_P0_15, OUTPUT);
    digitalWrite(LED_STATUS, LOW);
    digitalWrite(LED_RX, LOW);
    digitalWrite(LED_TX, LOW);
    digitalWrite(LED_P0_15, HIGH);  // Turn on LED_P0_15
    
    Serial.println("[+] LED TEST MODE");
    Serial.println("LED_STATUS (Pin 14): Blinking");
    Serial.println("LED_RX (Pin 27): Blinking");
    Serial.println("LED_TX (Pin 28): Blinking");
    
    lastTxTime = millis();
}

// ============= Main Loop =============
void loop() {
    // Blink LED_STATUS
    digitalWrite(LED_STATUS, HIGH);
    Serial.println("LED_STATUS ON");
    delay(2000);
    
    digitalWrite(LED_STATUS, LOW);
    Serial.println("LED_STATUS OFF");
    delay(2000);
    
    // Blink LED_RX
    digitalWrite(LED_RX, HIGH);
    Serial.println("LED_RX ON");
    delay(2000);
    
    digitalWrite(LED_RX, LOW);
    Serial.println("LED_RX OFF");
    delay(2000);
    
    // Blink LED_TX
    digitalWrite(LED_TX, HIGH);
    Serial.println("LED_TX ON");
    delay(2000);
    
    digitalWrite(LED_TX, LOW);
    Serial.println("LED_TX OFF");
    delay(2000);
}

// ============= Initialize LoRa (from STM32 config) =============
void initializeLoRa() {
    // Setup SPI pins
    SPI.setPins(SPI_MISO, SPI_MOSI, SPI_SCK);
    
    // Configure LoRa
    LoRa.setPins(RFM95_CS, RFM95_RST, RFM95_INT);
    
    // Initialize with frequency in MHz
    if (!LoRa.begin(loraConfig.freq / 1000000.0)) {
        Serial.println("[-] LoRa init failed!");
        lora_ok = false;
        updateBLEStatus("LoRa", "FAILED_INIT");
        while (1) {
            digitalWrite(LED_STATUS, HIGH);
            delay(100);
            digitalWrite(LED_STATUS, LOW);
            delay(100);
        }
    }
    
    // Set LoRa Parameters from STM32 config
    LoRa.setSpreadingFactor(loraConfig.spreadingFactor);
    LoRa.setSignalBandwidth(loraConfig.bandwidth);
    LoRa.setCodingRate4(loraConfig.codingRate - 4);  // Convert CR 5-8 to 1-4
    LoRa.setSyncWord(loraConfig.syncWord);
    LoRa.setTxPower(loraConfig.txPower);
    LoRa.enableCrc();
    
    Serial.println("[+] LoRa initialized successfully!");
    lora_ok = true;
    updateBLEStatus("LoRa", "OK");
}

// ============= Initialize BLE =============
void initializeBLE() {
    // Initialize Bluefruit with maximum connections = 1
    Bluefruit.begin();
    Bluefruit.setTxPower(4);    // Check bluefruit.h for supported values
    Bluefruit.setName("LoRa-Node-104");
    Bluefruit.Periph.setConnectCallback(connectCallback);
    Bluefruit.Periph.setDisconnectCallback(disconnectCallback);
    
    // Setup the LoRa Service
    setupBLECharacteristics();
    
    // Start advertising
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(loraService);
    Bluefruit.Advertising.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
    Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
    
    Serial.println("[+] BLE initialized successfully!");
    Serial.println("    Device Name: LoRa-Node-104");
    Serial.println("    Advertising started...");
}

// ============= Setup BLE Characteristics =============
void setupBLECharacteristics() {
    // Setup and start the LoRa Service
    loraService.begin();
    
    // System Status Characteristic (Read + Notify)
    systemStatusChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    systemStatusChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    systemStatusChar.setMaxLen(60);
    systemStatusChar.begin();
    systemStatusChar.write((uint8_t*)"INITIALIZING", 12);
    
    // Sensor Status Characteristic (Read + Notify) - ERROR STATUS
    sensorStatusChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    sensorStatusChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    sensorStatusChar.setMaxLen(60);
    sensorStatusChar.begin();
    sensorStatusChar.write((uint8_t*)"INITIALIZING", 12);
    
    // LoRa Status Characteristic (Read + Notify) - ERROR STATUS
    loraStatusChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    loraStatusChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    loraStatusChar.setMaxLen(60);
    loraStatusChar.begin();
    loraStatusChar.write((uint8_t*)"INITIALIZING", 12);
    
    // Sensor Data Characteristic (Read + Notify)
    sensorDataChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    sensorDataChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    sensorDataChar.setMaxLen(100);
    sensorDataChar.begin();
    sensorDataChar.write((uint8_t*)"T:-- H:-- P:--", 14);
    
    // LoRa Signal Characteristic (Read + Notify)
    loraSignalChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    loraSignalChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    loraSignalChar.setMaxLen(40);
    loraSignalChar.begin();
    loraSignalChar.write((uint8_t*)"RSSI:-- SNR:--", 14);
    
    // Packet Count Characteristic (Read + Notify)
    packetCountChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    packetCountChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    packetCountChar.setMaxLen(40);
    packetCountChar.begin();
    packetCountChar.write((uint8_t*)"TX:0 RX:0", 9);
    
    // LoRa Config Characteristic (Read)
    loraConfigChar.setProperties(CHR_PROPS_READ);
    loraConfigChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    loraConfigChar.setMaxLen(80);
    loraConfigChar.begin();
    loraConfigChar.write((uint8_t*)"923MHz SF7 BW125 CR5 Pwr12", 27);
    
    // Battery Characteristic (Read + Notify)
    batteryChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    batteryChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    batteryChar.setMaxLen(40);
    batteryChar.begin();
    batteryChar.write((uint8_t*)"Batt: 3300mV", 12);
    
    // Error Log Characteristic (Read + Notify)
    errorLogChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    errorLogChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    errorLogChar.setMaxLen(100);
    errorLogChar.begin();
    errorLogChar.write((uint8_t*)"No errors", 9);
}

// ============= BLE Connection Callback =============
void connectCallback(uint16_t conn_handle) {
    Serial.println("[+] BLE Client Connected!");
    ble_connected = true;
    digitalWrite(LED_STATUS, HIGH);
}

// ============= BLE Disconnection Callback =============
void disconnectCallback(uint16_t conn_handle, uint8_t reason) {
    Serial.println("[-] BLE Client Disconnected");
    ble_connected = false;
    digitalWrite(LED_STATUS, LOW);
}

// ============= Update BLE Status Characteristics =============
void updateBLEStatus() {
    // Create buffer for data
    char buf[100];
    
    // Update Sensor Data
    snprintf(buf, sizeof(buf), "T:%.1f H:%.1f P:%.1f", temperature, humidity, pressure);
    sensorDataChar.notify((uint8_t*)buf, strlen(buf));
    
    // Update Packet Count
    snprintf(buf, sizeof(buf), "TX:%lu RX:--", packetCounter);
    packetCountChar.notify((uint8_t*)buf, strlen(buf));
    
    // Update Battery
    snprintf(buf, sizeof(buf), "Batt: %umV", vbat_mv);
    batteryChar.notify((uint8_t*)buf, strlen(buf));
}

// ============= Update BLE Status (Error/Status Notifications) =============
void updateBLEStatus(const char* module, const char* status) {
    if (!ble_connected) return;
    
    char buf[80];
    
    if (strcmp(module, "System") == 0) {
        snprintf(buf, sizeof(buf), "SYS: %s", status);
        systemStatusChar.notify((uint8_t*)buf, strlen(buf));
        Serial.print("[BLE] System: ");
        Serial.println(status);
    } 
    else if (strcmp(module, "Sensor") == 0) {
        snprintf(buf, sizeof(buf), "SENSOR: %s", status);
        sensorStatusChar.notify((uint8_t*)buf, strlen(buf));
        Serial.print("[BLE] Sensor: ");
        Serial.println(status);
        
        // Log error if failed
        if (strstr(status, "FAILED") != NULL) {
            snprintf(buf, sizeof(buf), "ERROR: Sensor %s", status);
            errorLogChar.notify((uint8_t*)buf, strlen(buf));
        }
    } 
    else if (strcmp(module, "LoRa") == 0) {
        snprintf(buf, sizeof(buf), "LORA: %s", status);
        loraStatusChar.notify((uint8_t*)buf, strlen(buf));
        Serial.print("[BLE] LoRa: ");
        Serial.println(status);
        
        // Log error if failed
        if (strstr(status, "FAILED") != NULL) {
            snprintf(buf, sizeof(buf), "ERROR: LoRa %s", status);
            errorLogChar.notify((uint8_t*)buf, strlen(buf));
        }
    }
}

// ============= Print LoRa Configuration (from STM32) =============
void printLoRaConfig() {
    Serial.print(F("\n[LoRa] "));
    Serial.print(loraConfig.freq / 1000000);
    Serial.print(F("MHz SF"));
    Serial.print(loraConfig.spreadingFactor);
    Serial.print(F(" BW"));
    Serial.print(loraConfig.bandwidth / 1000);
    Serial.print(F("kHz CR"));
    Serial.print(loraConfig.codingRate);
    Serial.print(F(" Pwr"));
    Serial.print(loraConfig.txPower);
    Serial.print(F("dBm Node:"));
    Serial.print(THIS_NODE_ADDRESS);
    Serial.println(F(" [BROADCAST]\n"));
}

// ============= Set LoRa Frequency (from STM32) =============
void setLoRaFrequency(long freq) {
    loraConfig.freq = freq;
    Serial.print(F("[*] LoRa Frequency set to: "));
    Serial.print(freq / 1000000);
    Serial.println(F(" MHz"));
    LoRa.setFrequency(freq / 1000000.0);
}

// ============= Set Spreading Factor (from STM32) =============
void setSpreadingFactor(int sf) {
    if (sf >= 7 && sf <= 12) {
        loraConfig.spreadingFactor = sf;
        Serial.print(F("[*] Spreading Factor set to: SF"));
        Serial.println(sf);
        LoRa.setSpreadingFactor(sf);
    } else {
        Serial.println(F("[-] Invalid SF! Use 7-12"));
    }
}

// ============= Set Bandwidth (from STM32) =============
void setBandwidth(long bw) {
    loraConfig.bandwidth = bw;
    Serial.print(F("[*] Bandwidth set to: "));
    Serial.print(bw / 1000);
    Serial.println(F(" kHz"));
    LoRa.setSignalBandwidth(bw);
}

// ============= Set Coding Rate (from STM32) =============
void setCodingRate(int cr) {
    if (cr >= 5 && cr <= 8) {
        loraConfig.codingRate = cr;
        Serial.print(F("[*] Coding Rate set to: "));
        Serial.println(cr);
        LoRa.setCodingRate4(cr - 4);
    } else {
        Serial.println(F("[-] Invalid CR! Use 5-8"));
    }
}

// ============= Set TX Power (from STM32) =============
void setTXPower(int power) {
    if (power >= 5 && power <= 20) {
        loraConfig.txPower = power;
        Serial.print(F("[*] TX Power set to: "));
        Serial.print(power);
        Serial.println(F(" dBm"));
        LoRa.setTxPower(power);
    } else {
        Serial.println(F("[-] Invalid TX Power! Use 5-20 dBm"));
    }
}

// ============= Initialize BME280 =============
void initializeBME280() {
    if (!bme280.begin(0x76)) {  // Default I2C address: 0x76 (or 0x77)
        Serial.println("[-] BME280 not found!");
        bme280_ok = false;
        updateBLEStatus("Sensor", "FAILED_NOT_FOUND");
        return;
    }
    
    Serial.println("[+] BME280 initialized successfully!");
    bme280_ok = true;
    updateBLEStatus("Sensor", "OK");
    
    // Sensor sampling configuration
    bme280.setSampling(Adafruit_BME280::MODE_NORMAL,
                       Adafruit_BME280::SAMPLING_X2,
                       Adafruit_BME280::SAMPLING_X16,
                       Adafruit_BME280::SAMPLING_X1,
                       Adafruit_BME280::FILTER_OFF);
}

// ============= Read Sensor Data (from STM32) =============
void readData() {
    // Read temperature
    temperature = bme280.readTemperature();
    
    // Read humidity
    humidity = bme280.readHumidity();
    
    // Read pressure
    pressure = bme280.readPressure() / 100.0F;
    
    // Read supply voltage
    vbat_mv = readSupplyVoltage();
}

// ============= Display Sensor Data =============
void displaySensorData() {
    Serial.println("\n[*] Sensor Data:");
    Serial.print("    Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");
    
    Serial.print("    Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
    
    Serial.print("    Pressure: ");
    Serial.print(pressure);
    Serial.println(" hPa");
    
    Serial.print("    Altitude: ");
    Serial.print(bme280.readAltitude(SEALEVELPRESSURE_HPA));
    Serial.println(" m");
    
    Serial.print("    Battery: ");
    Serial.print(vbat_mv);
    Serial.println(" mV");
}

// ============= Initialize SSD1306 Display =============
void initializeSSD1306() {
    Serial.println("[*] Configuring I2C pins...");
    Serial.print("    SDA: P1.04 (GPIO ");
    Serial.print(I2C_SDA);
    Serial.print("), SCL: P0.11 (GPIO ");
    Serial.print(I2C_SCL);
    Serial.println(")");
    
    // Note: Wire.begin() should be called in setup before this
    delay(100);
    
    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    Serial.println("[*] Initializing SSD1306 at address 0x3C...");
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("[-] SSD1306 at 0x3C FAILED!");
        Serial.println("[*] Trying address 0x3D...");
        
        if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
            Serial.println("[-] SSD1306 at 0x3D also FAILED!");
            Serial.println("[-] SSD1306 allocation failed!");
            Serial.println("    Check I2C connections!");
            Serial.println("    Check display address!");
            return;
        } else {
            Serial.println("[+] SSD1306 found at address 0x3D!");
        }
    } else {
        Serial.println("[+] SSD1306 found at address 0x3C!");
    }
    
    Serial.println("[+] SSD1306 initialized successfully!");
    
    // Show startup message
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 15);
    display.println("nRF52840");
    display.setCursor(20, 35);
    display.println("LoRa Node");
    display.display();
    delay(1500);
}

// ============= Update Display =============
void updateDisplay() {
    if (display.height() == 0) return;  // Display not initialized
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    // Line 1: Node ID
    display.print("N:");
    display.print(THIS_NODE_ADDRESS);
    display.print(" Pkt:");
    display.println(packetCounter);
    
    // Line 2: Temperature
    display.print("T:");
    display.print(temperature, 1);
    display.print("C ");
    
    // Line 3: Humidity
    display.print("H:");
    display.println(humidity, 0);
    
    // Line 4: Pressure
    display.print("P:");
    display.print(pressure, 0);
    display.print(" hPa");
    
    // Line 5: Battery
    display.print(" V:");
    display.println(vbat_mv);
    
    // Line 6: LoRa
    display.print("LoRa: ");
    if (lora_ok) {
        display.print("OK SF");
        display.print(loraConfig.spreadingFactor);
    } else {
        display.print("FAIL");
    }
    display.print(" BLE:");
    display.println(ble_connected ? "Y" : "N");
    
    display.display();
}

// ============= Display Initialization Status =============
void displayInitStatus(const char* module, const char* status) {
    if (display.height() == 0) return;  // Display not initialized
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("INIT");
    display.println("");
    display.setTextSize(1);
    display.print("MOD: ");
    display.println(module);
    display.print("ST: ");
    display.println(status);
    
    display.display();
}

// ============= Read Supply Voltage (from STM32) =============
uint16_t readSupplyVoltage() {
    // Nominal voltage for nRF52840 (typically 3.3V from USB or battery)
    // For actual voltage measurement, add a voltage divider on an ADC pin
    return 3300;  // 3.3V nominal (3300 mV)
}

// ============= Transmit Data (from STM32) =============
void transmitData() {
    digitalWrite(LED_TX, LOW);  // Turn on TX LED
    
    // Build packet string similar to STM32 format
    // Format: NODE:ID|SEQ:count|TEMP:value|HUM:value|PRES:value|VBAT:value|RSSI:signal
    
    String packet = "";
    packet += "NODE:";
    packet += THIS_NODE_ADDRESS;
    packet += "|SEQ:";
    packet += packetCounter;
    packet += "|TEMP:";
    packet += temperature;
    packet += "|HUM:";
    packet += humidity;
    packet += "|PRES:";
    packet += pressure;
    packet += "|VBAT:";
    packet += vbat_mv;
    
    // Increment sequence number
    packetCounter++;
    
    // Send LoRa packet
    LoRa.beginPacket();
    LoRa.print(packet);
    LoRa.endPacket();
    
    // Print transmission info
    Serial.println("\n[TX] LoRa Packet Transmitted:");
    Serial.println("     " + packet);
    Serial.print("     Packet #: ");
    Serial.println(packetCounter - 1);
    
    digitalWrite(LED_TX, HIGH);  // Turn off TX LED
    delay(100);
}

// ============= Receive Data (from STM32) =============
void receiveData() {
    digitalWrite(LED_RX, LOW);  // Turn on RX LED
    
    String packet = "";
    while (LoRa.available()) {
        packet += (char)LoRa.read();
    }
    
    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();
    
    Serial.println("\n[RX] LoRa Packet Received:");
    Serial.println("     Data: " + packet);
    Serial.print("     RSSI: ");
    Serial.print(rssi);
    Serial.println(" dBm");
    Serial.print("     SNR: ");
    Serial.print(snr);
    Serial.println(" dB");
    
    digitalWrite(LED_RX, HIGH);  // Turn off RX LED
    delay(100);
}