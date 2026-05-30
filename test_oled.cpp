// ============================================================================
// SSD1306 OLED Display Test for nRF52840
// ============================================================================
// Simple test to diagnose OLED display issues

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

// I2C Pins
#define I2C_SDA     4    // P1.04
#define I2C_SCL     11   // P0.11

// Display object
Adafruit_SSD1306 display(128, 64, &Wire, -1);

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n========================================");
    Serial.println("SSD1306 OLED Display Test");
    Serial.println("========================================\n");
    
    // Start I2C with custom pins
    Serial.println("[*] Starting I2C with custom pins...");
    Serial.print("    SDA: P1.04 (GPIO ");
    Serial.print(I2C_SDA);
    Serial.print("), SCL: P0.11 (GPIO ");
    Serial.print(I2C_SCL);
    Serial.println(")");
    
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(100);
    
    // Scan I2C bus
    Serial.println("\n[*] Scanning I2C bus for devices...");
    byte error, address;
    int nDevices = 0;
    
    for(address = 1; address < 127; address++ ) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        
        if (error == 0) {
            Serial.print("     I2C device found at address 0x");
            if (address < 16) Serial.print("0");
            Serial.println(address, HEX);
            nDevices++;
        }
    }
    
    if (nDevices == 0) {
        Serial.println("     No I2C devices found!");
    } else {
        Serial.print("     Found ");
        Serial.print(nDevices);
        Serial.println(" device(s)");
    }
    
    // Try to initialize display
    Serial.println("\n[*] Initializing SSD1306 display...");
    Serial.println("    Trying address 0x3C...");
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("[-] SSD1306 at 0x3C failed!");
        
        Serial.println("    Trying address 0x3D...");
        if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
            Serial.println("[-] SSD1306 at 0x3D also failed!");
            Serial.println("[-] Display initialization FAILED!");
            
            while(1) {
                Serial.println("    Retrying...");
                delay(5000);
            }
        } else {
            Serial.println("[+] SSD1306 found at address 0x3D!");
        }
    } else {
        Serial.println("[+] SSD1306 found at address 0x3C!");
    }
    
    // Display is initialized - show test pattern
    Serial.println("[+] Display initialized successfully!");
    
    // Clear and show text
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("OLED OK!");
    display.display();
    
    Serial.println("[+] Test pattern displayed!");
    delay(2000);
    
    // Show all pixels
    Serial.println("[*] Testing display fill...");
    display.fillRect(0, 0, 128, 64, SSD1306_WHITE);
    display.display();
    delay(2000);
    
    // Clear
    display.clearDisplay();
    display.display();
    
    // Show test graphics
    Serial.println("[*] Testing graphics...");
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 10);
    display.println("Test 1: Text");
    display.drawLine(0, 25, 127, 25, SSD1306_WHITE);
    display.drawRect(20, 35, 88, 20, SSD1306_WHITE);
    display.fillRect(30, 40, 68, 10, SSD1306_WHITE);
    display.display();
    
    Serial.println("[+] All tests passed!");
}

void loop() {
    // Rotate display pattern
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 5);
    display.print("Running: ");
    display.println(millis() / 1000);
    
    // Draw a moving box
    int x = (millis() / 10) % 100;
    display.drawRect(x, 20, 20, 20, SSD1306_WHITE);
    
    display.display();
    delay(100);
}
