/*
If the red led is constantly on, the wiring for your display is broken.
made for the 128×64 version.
*/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define LED 15 //Set a definiton on pin P0.15 called "LED".

#define MY_SDA 17  // ตรงกับ P0.17
#define MY_SCL 20  // ตรงกับ P0.20

Adafruit_SSD1306 display(128, 64, &Wire, -1);

float readBatteryVoltage() {
  // The volatile keyword is a type qualifier in C/C++ that tells the compiler a variable's value might change in ways that the compiler cannot detect from the code alone. 
  // Essentially, it says: "Don't optimize access to this variable because its value might change unexpectedly."
  volatile uint32_t raw_value = 0;
  // Configure SAADC
  NRF_SAADC->ENABLE = 1;
  NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_12bit;
  
  NRF_SAADC->CH[0].CONFIG = 
    (SAADC_CH_CONFIG_GAIN_Gain1_4 << SAADC_CH_CONFIG_GAIN_Pos) |
    (SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos) |
    (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos);
  
  NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_VDDHDIV5;
  NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC;
  
  // Sample
  NRF_SAADC->RESULT.PTR = (uint32_t)&raw_value;
  NRF_SAADC->RESULT.MAXCNT = 1;
  NRF_SAADC->TASKS_START = 1;
  while (!NRF_SAADC->EVENTS_STARTED);
  NRF_SAADC->EVENTS_STARTED = 0;
  NRF_SAADC->TASKS_SAMPLE = 1;
  while (!NRF_SAADC->EVENTS_END);
  NRF_SAADC->EVENTS_END = 0;
  NRF_SAADC->TASKS_STOP = 1;
  while (!NRF_SAADC->EVENTS_STOPPED);
  NRF_SAADC->EVENTS_STOPPED = 0;
  NRF_SAADC->ENABLE = 0;

  // Force explicit double-precision calculations
  double raw_double = (double)raw_value;
  double step1 = raw_double * 2.4;
  double step2 = step1 / 4095.0;
  double vddh = 5.0 * step2;

  return (float)vddh;
}

void setup() {
  pinMode(LED, OUTPUT); //Set the LED to output mode.
  digitalWrite(LED, LOW); //Set the LED to low

  // Set custom I2C pins before begin()
  Wire.setPins(MY_SDA, MY_SCL);
  Wire.begin();
  Serial.println("I2C Bus initialized at P0.17 (SDA) and P0.20 (SCL)");

  // Initialize the display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while(1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); //place where the text will be drawn
  display.println("By ICantMakeThings");
  display.setCursor(0, 30);
  display.println("And cat5e-inv");
  display.display();
  delay(2000);
  display.clearDisplay();
}

void loop() {
  float voltage = readBatteryVoltage();
  
  // Calculate battery percentage (2.8V = 0%, 3.3V = 100%)
  uint8_t batteryPercent = (uint8_t)((voltage - 2.8) / (3.3 - 2.8) * 100);
  batteryPercent = constrain(batteryPercent, 0, 100);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Line 1: Voltage
  display.setCursor(5, 15);
  display.print("Voltage: ");
  display.print(voltage, 2);
  display.print("V");
  
  // Line 2: Battery percentage
  display.setCursor(5, 30);
  display.print("Level: ");
  display.print(batteryPercent);
  display.print("%");
  
  // Line 3: Status
  display.setCursor(5, 45);
  display.print("Status: ");
  if(batteryPercent > 80) {
    display.print("GOOD");
  } else if(batteryPercent > 50) {
    display.print("OK");
  } else if(batteryPercent > 20) {
    display.print("LOW");
  } else {
    display.print("CRITICAL");
  }
  
  // Line 4: Device info
  display.setCursor(5, 60);
  display.print("nRF52840");
  
  display.display();
  delay(1000);
}