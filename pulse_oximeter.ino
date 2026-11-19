/*
  Pulse & SpO2 Monitor - Educational Prototype (NOT a medical device)
  Board : Arduino Uno / Nano
  Parts : MAX30102 (I2C), SSD1306 OLED 128x64 (I2C), active buzzer

  Wiring:
    MAX30102  VIN -> 3.3V (or 5V per your module), GND -> GND, SDA -> A4, SCL -> A5
    OLED      VCC -> 5V/3.3V, GND -> GND, SDA -> A4, SCL -> A5
    Buzzer    (+) -> D8, (-) -> GND

  Libraries (Arduino IDE > Library Manager):
    - SparkFun MAX3010x Pulse and Proximity Sensor Library
    - Adafruit SSD1306
    - Adafruit GFX Library
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

// ---------- Settings ----------
#define SCREEN_W      128
#define SCREEN_H      64
#define OLED_ADDR     0x3C
#define BUZZER_PIN    8

const int32_t  SPO2_LOW_LIMIT = 90;    // alarm if SpO2 below this (%)
const int32_t  HR_LOW_LIMIT   = 50;    // alarm if heart rate below this (bpm)
const int32_t  HR_HIGH_LIMIT  = 120;   // alarm if heart rate above this (bpm)
const uint32_t FINGER_THRESHOLD = 50000; // IR value below this = no finger

// ---------- Objects ----------
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
MAX30105 sensor;

// ---------- Sample buffers ----------
#define BUFFER_SIZE 100
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
  uint16_t irBuffer[BUFFER_SIZE];   // Uno/Nano have little RAM
  uint16_t redBuffer[BUFFER_SIZE];
#else
  uint32_t irBuffer[BUFFER_SIZE];
  uint32_t redBuffer[BUFFER_SIZE];
#endif

int32_t spo2;
int8_t  validSPO2;
int32_t heartRate;
int8_t  validHeartRate;

void showMessage(const char* line1, const char* line2 = "") {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(line1);
  display.println(line2);
  display.display();
}

void showReadings(bool fingerPresent, bool alarm) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (!fingerPresent) {
    display.setTextSize(1);
    display.setCursor(0, 24);
    display.println(F("Place finger on"));
    display.println(F("the sensor..."));
    display.display();
    return;
  }

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Pulse & SpO2"));
  if (alarm) display.print(F("  ALARM!"));

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(F("HR:"));
  if (validHeartRate) { display.print(heartRate); display.print(F(" bpm")); }
  else                { display.print(F("--")); }

  display.setCursor(0, 44);
  display.print(F("O2:"));
  if (validSPO2) { display.print(spo2); display.print(F(" %")); }
  else           { display.print(F("--")); }

  display.display();
}

bool checkAlarm() {
  bool alarm = false;
  if (validSPO2 && spo2 < SPO2_LOW_LIMIT) alarm = true;
  if (validHeartRate && (heartRate < HR_LOW_LIMIT || heartRate > HR_HIGH_LIMIT)) alarm = true;
  return alarm;
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED not found. Check wiring / address."));
    while (true);
  }

  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    showMessage("MAX30102 not found", "Check wiring");
    Serial.println(F("MAX30102 not found. Check wiring."));
    while (true);
  }

  // ledBrightness, sampleAverage, ledMode (2 = Red + IR), sampleRate, pulseWidth, adcRange
  sensor.setup(60, 4, 2, 100, 411, 4096);

  showMessage("Starting...", "Place finger");
  delay(1500);

  // Fill the first 100 samples
  for (int i = 0; i < BUFFER_SIZE; i++) {
    while (!sensor.available()) sensor.check();
    redBuffer[i] = sensor.getRed();
    irBuffer[i]  = sensor.getIR();
    sensor.nextSample();
  }
  maxim_heart_rate_and_oxygen_saturation(irBuffer, BUFFER_SIZE, redBuffer,
                                         &spo2, &validSPO2, &heartRate, &validHeartRate);
}

void loop() {
  // Drop the oldest 25 samples, keep the newest 75
  for (int i = 25; i < BUFFER_SIZE; i++) {
    redBuffer[i - 25] = redBuffer[i];
    irBuffer[i - 25]  = irBuffer[i];
  }

  // Take 25 new samples
  for (int i = 75; i < BUFFER_SIZE; i++) {
    while (!sensor.available()) sensor.check();
    redBuffer[i] = sensor.getRed();
    irBuffer[i]  = sensor.getIR();
    sensor.nextSample();
  }

  bool fingerPresent = irBuffer[BUFFER_SIZE - 1] > FINGER_THRESHOLD;

  maxim_heart_rate_and_oxygen_saturation(irBuffer, BUFFER_SIZE, redBuffer,
                                         &spo2, &validSPO2, &heartRate, &validHeartRate);

  bool alarm = fingerPresent && checkAlarm();
  if (alarm) tone(BUZZER_PIN, 1000, 200);

  showReadings(fingerPresent, alarm);

  if (fingerPresent) {
    Serial.print(F("HR="));   Serial.print(validHeartRate ? heartRate : -1);
    Serial.print(F(" bpm, SpO2=")); Serial.print(validSPO2 ? spo2 : -1);
    Serial.println(F(" %"));
  }
}
