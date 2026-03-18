#include "Nicla_System.h"
#include "Arduino_BHY2.h"
#include <ArduinoBLE.h>

// --- BLE CONFIGURATION ---
// Service and Characteristic UUIDs (Must match Python)
BLEService myNiclaService("19b10000-0000-537e-4f6c-d104768a1214");
// Added BLERead to Notify to make it easier for Python to discover
BLEStringCharacteristic myDataChar("19b10001-e8f2-537e-4f6c-d104768a1214", BLERead | BLENotify, 512); 

// Sensors
SensorXYZ accel(SENSOR_ID_ACC);
Sensor temp(SENSOR_ID_TEMP); 
Sensor stepCounter(SENSOR_ID_STC);

// Data Tracking
const uint32_t LOG_INTERVAL = 2000; // Sent every 2 seconds for testing
uint32_t lastLog = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize Nicla Hardware
  nicla::begin();
  nicla::leds.begin();
  
  // Initialize Sensor Hub
  if (!BHY2.begin(NICLA_STANDALONE)) {
    Serial.println("Failed to init BHY2 Hub!");
    while(1);
  }
  
  // Start Individual Sensors
  accel.begin();
  temp.begin();
  stepCounter.begin();

  // Initialize BLE
  if (!BLE.begin()) {
    Serial.println("- BLE failed to start");
    nicla::leds.setColor(red);
    while (1);
  }

  BLE.setLocalName("NiclaBio");
  BLE.setAdvertisedService(myNiclaService);
  myNiclaService.addCharacteristic(myDataChar);
  BLE.addService(myNiclaService);
  BLE.advertise();

  Serial.println("Nicla is Online. Waiting for Python connection...");
  nicla::leds.setColor(blue); // Blue = Advertising
}

void loop() {
  // Always update the sensor hub
  BHY2.update();

  // Look for a connection from Python
  BLEDevice central = BLE.central();

  if (central) {
    Serial.print("Connected to: ");
    Serial.println(central.address());
    nicla::leds.setColor(green); // Green = Connected

    while (central.connected()) {
      // Keep sensors updating while connected
      BHY2.update();

      if (millis() - lastLog >= LOG_INTERVAL) {
        lastLog = millis();
        
        // 1. Gather Data
        float x = accel.x() / 1000.0;
        float y = accel.y() / 1000.0;
        float z = accel.z() / 1000.0;
        float currentTemp = temp.value();
        uint32_t steps = (uint32_t)stepCounter.value();

        // 2. Simple Wear Detection (Motion check)
        float vm = sqrt(x*x + y*y + z*z);
        bool is_worn = (vm > 1.02 || vm < 0.98);

        // 3. Construct CSV: "Steps,Temp,Status"
        String dataString = String(steps) + "," + 
                            String(currentTemp, 2) + "," + 
                            String(is_worn ? "WORN" : "NOT_WORN");
        
        // 4. Send to Python
        myDataChar.writeValue(dataString);
        Serial.println(">>> Sent: " + dataString);
      }
    }

    // Reset when disconnected
    Serial.println("Disconnected.");
    nicla::leds.setColor(blue);
  }
}