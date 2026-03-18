#include "Nicla_System.h"
#include "Arduino_BHY2.h"
#include <ArduinoBLE.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- WORKING BLE SETUP ---
BLEService myNiclaService("19b10000-0000-537e-4f6c-d104768a1214");
BLEStringCharacteristic myDataChar("19b10001-e8f2-537e-4f6c-d104768a1214", BLENotify, 512); 

// Sensors
SensorActivity activity(SENSOR_ID_AR);
Sensor stepCounter(SENSOR_ID_STC); 
SensorXYZ accel(SENSOR_ID_ACC);
Sensor temp(SENSOR_ID_TEMP); 
Sensor pressure(SENSOR_ID_BARO);

// Your Original Data Structure
struct CompleteLog {
  uint32_t t_minutes;
  uint32_t steps;
  uint16_t sd_vm_x100;
  float temp_c;
  uint8_t overall_worn;
};

const size_t MAX_RECORDS = 64; 
CompleteLog dataLog[MAX_RECORDS];
size_t writeIdx = 0;
bool bufferFull = false;
const uint32_t LOG_INTERVAL = 1 * 60 * 1000; // Currently 1 min for testing

// Accelorometer Setup
#define N_SAMPLES 100 
float vm_buffer[N_SAMPLES];
int buf_idx = 0;
bool vm_buffer_full = false;

const char* activityLabels[] = {"Still", "Walk", "Run", "Bike", "Car", "Tilt", "InVeh", "", "S-Start", "W-Start", "R-Start", "B-Start", "C-Start", "T-Start", "V-Start", ""};

int currentScreen = 0;
unsigned long lastSwitch = 0;
const unsigned long SCREEN_INTERVAL = 3000;

void setup() {
  Serial.begin(115200);
  
  nicla::begin();
  nicla::leds.begin();
  BHY2.begin(NICLA_STANDALONE);
  
  stepCounter.begin();
  accel.begin();
  temp.begin();
  pressure.begin();
  activity.begin();

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED failed"));
  }
  display.clearDisplay();
  display.display();

  // --- START BLE ---
  if (!BLE.begin()) {
    while (1);
  }
  BLE.setLocalName("NiclaBio");
  BLE.setAdvertisedService(myNiclaService);
  myNiclaService.addCharacteristic(myDataChar);
  BLE.addService(myNiclaService);
  BLE.advertise();
}

void loop() {
  BHY2.update();

  // 1. Calculate Vector Magnitude (Your Logic)
  float x = accel.x() / 1000.0;
  float y = accel.y() / 1000.0;
  float z = accel.z() / 1000.0;
  float vm = sqrt(x*x + y*y + z*z);
  vm_buffer[buf_idx] = vm;
  buf_idx = (buf_idx + 1) % N_SAMPLES;
  if (buf_idx == 0) vm_buffer_full = true;

  // 2. Logging & BLE Transmission
  static uint32_t lastLog = 0;
  if (millis() - lastLog >= LOG_INTERVAL) {
    lastLog = millis();
    
    // Calculate Standard Deviation for Wear Detection
    float sum = 0;
    for(int i=0; i<N_SAMPLES; i++) sum += vm_buffer[i];
    float mean = sum / N_SAMPLES;
    float var = 0;
    for(int i=0; i<N_SAMPLES; i++) var += pow(vm_buffer[i] - mean, 2);
    float sd_vm = sqrt(var / N_SAMPLES);
    
    bool overall_worn = (sd_vm > 0.01); // Simple threshold

    // Store in Log
    dataLog[writeIdx] = { (millis()/60000), (uint32_t)stepCounter.value(), (uint16_t)(sd_vm*100), temp.value(), (uint8_t)overall_worn };
    
    // Format the CSV string to send to Python
    String dataString = String(dataLog[writeIdx].t_minutes) + "," + 
                        String(dataLog[writeIdx].steps) + "," + 
                        String(dataLog[writeIdx].temp_c) + "," + 
                        String(overall_worn ? "WORN" : "NOT_WORN");
    
    // Send over Bluetooth
    if (BLE.connected()) {
        myDataChar.writeValue(dataString);
        Serial.println("Sent: " + dataString);
    }

    writeIdx = (writeIdx + 1) % MAX_RECORDS;
    if(writeIdx == 0) bufferFull = true;
  }

  // 3. Screen Switching
  if (millis() - lastSwitch >= SCREEN_INTERVAL) {
    lastSwitch = millis();
    currentScreen = (currentScreen + 1) % 4;
    updateDisplay();
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  
  if (currentScreen == 1) {
    display.println("Steps:");
    display.setTextSize(2);
    display.println((int)stepCounter.value());
  } else if (currentScreen == 2) {
    display.println("Temp:");
    display.print(temp.value()); display.println(" C");
  } else {
    display.println("Status:");
    display.println(BLE.connected() ? "BLE Connected" : "BLE Waiting...");
  }
  display.display();
}