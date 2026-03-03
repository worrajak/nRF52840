#ifndef LORA_CONFIG_H
#define LORA_CONFIG_H

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
};

// ============= LoRa Frequency Bands =============
// Choose appropriate frequency for your region
#define FREQ_US_915     915.0    // USA, Australia
#define FREQ_EU_868     868.0    // Europe
#define FREQ_ASIA_433   433.0    // Asia (often used for long range)
#define FREQ_ASIA_470   470.0    // Asia ISM Band
#define FREQ_JAPAN_928  928.0    // Japan

// ============= LoRa Spreading Factors =============
// SF7-SF12 (higher SF = longer range but slower data rate)
#define SF_FAST         7        // 122 bps data rate
#define SF_BALANCED     9        // 47 bps data rate
#define SF_LONG_RANGE   12       // 14 bps data rate (maximum range)

// ============= LoRa Bandwidths =============
#define BW_62500        62500    // 62.5 kHz
#define BW_125000       125000   // 125 kHz (recommended)
#define BW_250000       250000   // 250 kHz
#define BW_500000       500000   // 500 kHz

// ============= Packet Structure =============
typedef struct {
    uint32_t packetId;
    float temperature;
    float humidity;
    float pressure;
    int8_t rssi;
    float snr;
    uint32_t timestamp;
} LoRaPacket_t;

// ============= Helper Functions =============
inline String floatToString(float value, int decimals) {
    String result = String(value, decimals);
    return result;
}

inline void ledBlink(int pin, int count, int duration) {
    for (int i = 0; i < count; i++) {
        digitalWrite(pin, LOW);
        delay(duration);
        digitalWrite(pin, HIGH);
        delay(duration);
    }
}

inline void ledPulse(int pin, int count) {
    for (int i = 0; i < count; i++) {
        for (int brightness = 0; brightness < 255; brightness += 5) {
            analogWrite(pin, brightness);
            delay(2);
        }
        for (int brightness = 255; brightness > 0; brightness -= 5) {
            analogWrite(pin, brightness);
            delay(2);
        }
    }
}

#endif // LORA_CONFIG_H
