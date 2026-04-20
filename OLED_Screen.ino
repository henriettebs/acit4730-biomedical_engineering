#include "Nicla_System.h" // Nicla System library for built-in functions on NSME
#include "Arduino_BHY2.h" // To collect data from sensors under BHY2, BME688 (Temperature) and BHI260AP (Accelerometer)
#include <ArduinoBLE.h> // To connect on BLE with Python
#include "Nicla_OLED.h" // To run OLED commands in Nicla_OLED.cpp
#include <CircularBuffer.hpp> 

// --- BLE CONFIGURATION ---
BLEService myNiclaService("19b10000-0000-537e-4f6c-d104768a1214"); // Service and Characteristic UUIDs (Must match Python)
// --- DATA CHARACTERISTICS (written by Python on connect) ---
BLEStringCharacteristic myDataChar("19b10001-e8f2-537e-4f6c-d104768a1214", BLERead | BLENotify, 512); // Added BLERead to Notify to make it easier for Python to discover
BLEStringCharacteristic dateChar("19b10002-e8f2-537e-4f6c-d104768a1214", BLEWrite, 16);
BLEStringCharacteristic timeChar("19b10003-e8f2-537e-4f6c-d104768a1214", BLEWrite, 8); // "HH:MM:SS"
BLEStringCharacteristic streakChars("19b10004-e8f2-537e-4f6c-d104768a1214", BLEWrite, 16); // Format: [currentStreak, longestStreak]
BLEUnsignedCharCharacteristic batteryLevelChar("2A19", BLERead | BLENotify); 
BLEStringCharacteristic dailyTargetChar("19b10005-e8f2-537e-4f6c-d104768a1214", BLEWrite, 2); 

// Day tracking
String lastKnownDate = "";   // Variable used in loop for clearing locally stored data on new day
String currentDate   = ""; // Variable used in loop for comparing if locally stored date is matching date sent from Python
String currentTime = ""; // UNSURE!! 
// Streak tracking
int currentStreak = 0;
int longestStreak = 0;
// Battery
int oldBatteryLevel = 0;
int batteryLevel = 0;
float voltage = 0.0;
bool runsOnBattery = false;
// --- SENSOR LATCH TIMERS ---
const uint32_t LATCH_DURATION_MS = 5UL * 60 * 1000; // 5 minutes
uint32_t activeLatchUntil  = 0;

// Sensors
SensorXYZ accel(SENSOR_ID_ACC);
Sensor temp(SENSOR_ID_TEMP); 
Sensor stepCounter(SENSOR_ID_STC);

// --- STATE MACHINE ---
enum DeviceState {
  IDLE,
  ACTIVE
};

DeviceState currentState = IDLE;
bool isWorn = false;

// Data Tracking
const uint32_t LOG_INTERVAL = 1*5*1000; // Sent every 5 seconds for testing
uint32_t lastLog = 0;

struct TempLog {
  float max_delta;
  float current_temp;
  float min_delta;
};

struct AccelData {
  float x;
  float y;
  float z;
  float vm; // Vector magnitude
};

// Global temperature tracking
TempLog tempLog = {0, 0, 0};
CircularBuffer<float,12> temp_buff;
bool tempInitialized = false;
const int TEMP_SAMPLES = 12;
const float TEMP_DELTA_THRESHOLD = 0.2;
bool tempRiseActive = false; // True = temp rose 0.2 degrees, waiting for fall
bool tempRiseWorn = false;

// Global accelerometer tracking
AccelData accelData = {0, 0, 0, 0};
CircularBuffer<float,150> accel_buff;
bool motionWorn = false;


// Step-counter smoothing
uint32_t lastStepCounterValue = 0;
uint32_t stepTotal = 0;
const uint32_t STEP_MAX_DELTA_PER_INTERVAL = 100; // ignore spikes larger than this

// --- WORN TIME TRACKING ---
uint32_t wornSeconds = 0;          // Total worn seconds today
uint32_t lastWornTick = 0;         // millis() timestamp of last worn-time update
int goalHours = 0; 

// --- VOTE-BASED DEBOUNCE ---
const int VOTE_WINDOW = 40;
const float WEAR_THRESHOLD = 0.70;
const float REMOVE_THRESHOLD = 0.30;
bool wornHistory[40] = {false};
int voteIndex = 0;

float getWornScore() {
  int count = 0;
  String wornHistoryValues = "";
  for (int i = 0; i < VOTE_WINDOW; i++) {
    if (wornHistory[i]) {
      count++;
      wornHistoryValues += wornHistory[i];
      wornHistoryValues += ", ";
    }
  }
  Serial.print("Worn history: "); Serial.println(wornHistoryValues);
  return (float)count / VOTE_WINDOW;
}

bool updateTempState(float currentTemp) {
  uint32_t now = millis();
  tempLog.current_temp = currentTemp;
  float delta = 0;

  Serial.print("New currentTemp: "); Serial.print(now); Serial.print(" - "); Serial.println(currentTemp);

  temp_buff.unshift(currentTemp);

  String tempValues = "";
  if(!temp_buff.isFull()) {
    return false;
  }

  for(int i = 0; i < TEMP_SAMPLES; i++) {
    tempValues += String(temp_buff[i]);
    tempValues += ", ";
  }
  Serial.print("Temp buffer "); Serial.println(tempValues);

  delta = temp_buff.first() - temp_buff.last();
  Serial.print("DELTA: "); Serial.println(delta);

  if (!tempRiseActive) {
    // Found valid rise!
    if (delta >= TEMP_DELTA_THRESHOLD) {
      tempRiseActive = true;
      tempLog.max_delta = delta;
      return true; // Now active
    }
    return false;
  }

  // --- FALL DETECTION (active, waiting for fall) ---
  if (tempRiseActive) {
    // Found the fall
    if (delta <= -TEMP_DELTA_THRESHOLD) {
      tempRiseActive = false;
      tempLog.min_delta = delta;
      return false;
    }
    return true; // Still active
  }
  return false;
}

bool updateAccelData() {
  accelData.x = accel.x() / 1000.0;
  accelData.y = accel.y() / 1000.0;
  accelData.z = accel.z() / 1000.0;
  float vm_magnitude = sqrt(accelData.x*accelData.x + accelData.y*accelData.y + accelData.z*accelData.z);

  accel_buff.unshift(vm_magnitude);

  if(!accel_buff.isFull()){
    return false;
  }

  float sum = 0;
  int size = accel_buff.size();
  for (int i = 0; i < size; i++) {
    sum += accel_buff[i];
  }
  float mean = sum / size;

  float variance = 0;
  for (int i = 0; i < size; i++) {
    float diff = accel_buff[i] - mean;
    variance += diff * diff;
  }
  variance /= (size - 1);

  float sd_vm = sqrt(variance);
  Serial.print("sd_vm: "); Serial.print(sd_vm); Serial.print(", mean: "); Serial.println(mean);
  
  // Store the standard deviation as the meaningful metric
  accelData.vm = sd_vm;

  // Worn-on-wrist check: moderate variance indicates wrist movement
  // - too low: stationary (table)
  // - too high: extreme shaking (unlikely for wrist wear)
  return (sd_vm > 0.025) && (sd_vm < 1.5);
}

const int GALVANIC_WINDOW = 10;
const int GALVANIC_ON_THRESHOLD = 8; // 80% must be true to activate
const int GALVANIC_OFF_THRESHOLD = 3; // Below 30% to deactivate
const int GALVANIC_SKIN_THRESHOLD = 300; // Calibrate on hardware

bool galvanicHistory[GALVANIC_WINDOW] = {false};
int galvanicIndex = 0;
bool galvanicSmoothed = false; // State persistence

CircularBuffer<int,10> galvanic_buff;

int updateGalvanic() {
  int rawValue = analogRead(A0); // Reads from one of two analog pins on NSME, NIcla boards ADC converts to 10 bits (default)
  bool isContact = (rawValue > GALVANIC_SKIN_THRESHOLD);

  galvanicHistory[galvanicIndex % GALVANIC_WINDOW] = isContact;
  galvanicIndex++;

  int count = 0;
  for (int i = 0; i < GALVANIC_WINDOW; i++) {
    if(galvanicHistory[i]) count++;
  }

  // Hysteresis: need 80% to turn ON, only 30% to turn OFF
  if (galvanicSmoothed && count < GALVANIC_OFF_THRESHOLD) {
    galvanicSmoothed = false;
  } else if (!galvanicSmoothed && count >= GALVANIC_ON_THRESHOLD) {
    galvanicSmoothed = true;
  }
  return rawValue;
}

bool getSmoothedGalvanic() {
  return galvanicSmoothed;
}

// Read and smooth step counter values, ignoring large spikes
uint32_t updateStepTotal() {
  uint32_t rawSteps = (uint32_t)stepCounter.value();
  uint32_t deltaSteps = 0;

  if (lastStepCounterValue != 0) {
    if (rawSteps >= lastStepCounterValue) {
      deltaSteps = rawSteps - lastStepCounterValue;
    } else {
      // Counter wrapped or reset; treat the new value as delta
      deltaSteps = rawSteps;
    }
  }

  lastStepCounterValue = rawSteps;
  // Ignore absurd spikes
  if (deltaSteps <= STEP_MAX_DELTA_PER_INTERVAL) {
    stepTotal += deltaSteps;
  }
  return stepTotal;
}

// Helper: format seconds as "Xh Ym Zs" string
String formatWornTime(uint32_t seconds) {
  uint32_t hours   = seconds / 3600;
  uint32_t minutes = (seconds % 3600) / 60;
  uint32_t secs    = seconds % 60;
  return String(hours) + "h " + String(minutes) + "m " + String(secs) + "s";
}

void checkAndBlinkWarning() {
  uint32_t now = millis();
  int currentHour = parseHourFromTimeChar();

  static uint32_t lastBlinkCheck = 0;
  static bool blinkState = false;
  static uint32_t blinkUntil = 0;
  static uint32_t pauseBlinkUntil = 0;

  if (now < pauseBlinkUntil){
    return;
  }

  if (now - lastBlinkCheck > 3000) {
    lastBlinkCheck = now;

    if(wornSeconds == 0 && currentHour >= 16 && currentHour <= 19) { // Only show warning if orthosis has not been worn, and it's between 4PM and 8PM

      if(blinkUntil == 0) {
        blinkUntil = now + (5 * 60 * 1000); // 5 minutes blink
      }

      if (now < blinkUntil) {
        blinkState = !blinkState;
        blinkState ? displayWarningScreen() : clearDisplay();
      } else {
        pauseBlinkUntil = now + (15 * 60 * 1000); // 15 minutes pause
        blinkUntil = 0;
        int pauseSeconds = pauseBlinkUntil / 1000;
        String pauseUntil = formatWornTime(pauseSeconds);
        clearDisplay();
      }
      
    } else {
      blinkUntil = 0;
      pauseBlinkUntil = 0;
    }
  }
}

int parseHourFromTimeChar() {
  String timeString = timeChar.value();

  if(timeString.length() >= 2){
    return timeString.substring(0,2).toInt();
  }
  return -1; // Error
}

void updateBatteryLevel() {
  batteryLevel = nicla::getBatteryVoltagePercentage();  // this command return the battery percentage
  voltage = nicla::getCurrentBatteryVoltage();
  runsOnBattery = nicla::runsOnBattery();

  if (batteryLevel != oldBatteryLevel) {       // if the battery level has changed
    batteryLevelChar.writeValue(batteryLevel);  // and update the battery level characteristic
    oldBatteryLevel = batteryLevel;             // save the level for next comparison
  }
}

void clearDataForNewDay() {
  wornSeconds = 0;
  stepTotal = 0;
  lastStepCounterValue = 0;
  tempLog = {0, 0, 0};
  accelData = {0, 0, 0, 0};
  temp_buff.clear();
  accel_buff.clear();
}

void setup() {
  Serial.begin(115200);
  uint32_t serialStart = millis();
  while (!Serial && (millis() - serialStart < 100)) {  // 100ms MAX wait
    delay(1);
  }
  
  if (!Serial) {
    Serial.println("Running headless (battery mode)");
  }
  // Initialize Nicla Hardware
  nicla::begin();
  nicla::leds.begin();
  nicla::setBatteryNTCEnabled(false);  // Set to false if your battery doesn't have an NTC thermistor.
  nicla::enableCharging(100);  // enable the battery charger and define the charging current in mA
  initOLED(); // OLED setup
  
  // Initialize Sensor Hub
  if (!BHY2.begin(NICLA_STANDALONE)) {
    Serial.println("Failed to init BHY2 Hub!");
    while(1);
  }
  
  // Start individual sensors
  accel.begin();
  temp.begin();
  stepCounter.begin();

  // Initialize BLE
  if (!BLE.begin()) {
    Serial.println("- BLE failed to start");
    while (1);
  }

  BLE.setLocalName("Nicla Sense ME");
  BLE.setAdvertisedService(myNiclaService);
  myNiclaService.addCharacteristic(myDataChar);
  myNiclaService.addCharacteristic(dateChar);
  myNiclaService.addCharacteristic(timeChar);
  myNiclaService.addCharacteristic(streakChars);
  myNiclaService.addCharacteristic(batteryLevelChar);
  batteryLevelChar.writeValue(oldBatteryLevel);        // set initial value for this characteristic
  myNiclaService.addCharacteristic(dailyTargetChar);
  BLE.addService(myNiclaService);
  BLE.advertise();

  nicla::leds.setColor(blue); // Blue = Advertising
}

void loop() {
  BHY2.update();
  BLEDevice central = BLE.central();

  if(central && central.connected()) {
    nicla::leds.setColor(green);
    // Retrieve and save values from Python connection
    if (dateChar.written()) {
        currentDate = dateChar.value();
    }
    if (timeChar.written()) {
      currentTime = timeChar.value();
    }
    if (streakChars.written()) {
      String streakData = streakChars.value();
      int commaIndex = streakData.indexOf(',');
      if (commaIndex > 0) {
        currentStreak = streakData.substring(0, commaIndex).toInt();
        longestStreak = streakData.substring(commaIndex + 1).toInt();
      }
    }
    if (dailyTargetChar.written()){
      String goalReceived = dailyTargetChar.value();
      goalHours = goalReceived.toInt();
    }

    updateBatteryLevel();
    // New day detected → reset worn time
    if (currentDate != "" && lastKnownDate != "" && currentDate != lastKnownDate) {
      clearDataForNewDay();
      lastKnownDate = currentDate;
    }

    wornHistory[voteIndex % VOTE_WINDOW] = isWorn;
    voteIndex++;
    float wornScore = getWornScore();
    uint32_t now = millis();

    motionWorn   = updateAccelData();

    int galvanicScore = updateGalvanic();
    bool galvanicWorn = getSmoothedGalvanic(); 


    if (lastWornTick == 0) lastWornTick = now; // First tick after becoming worn
    uint32_t tickDelta = now - lastWornTick;
    if (tickDelta >= 1000) { // Accumulate in 1-second steps
              wornSeconds += tickDelta / 1000;
              lastWornTick += (tickDelta / 1000) * 1000; // Keep sub-second remainder
            }
            uint32_t steps = updateStepTotal();
            String timeWorn = formatWornTime(wornSeconds);
    if (millis() - lastLog >= LOG_INTERVAL) {
      lastLog = millis();
      float currentTemp = temp.value();

      tempRiseWorn = updateTempState(currentTemp);
      isWorn = (motionWorn && galvanicWorn) || (tempRiseWorn && galvanicWorn);
      String dataString = String(steps) + ", {" +
                          String(tempLog.max_delta, 3) + "," +
                          String(tempLog.current_temp, 3) + "," +
                          String(tempLog.min_delta, 3) + "}," +
                          String(tempRiseWorn) + "," +
                          String(accelData.vm, 3) + "," +
                          String(motionWorn) + "," +
                          String(galvanicScore) + "," +
                          String(galvanicWorn) + "," +
                          (isWorn ? "WORN" : "NOT_WORN") + "," +
                          timeWorn; //new
                
      myDataChar.writeValue(dataString);
    }

    switch(currentState) {
      case IDLE:
        if(wornScore >= WEAR_THRESHOLD) {
          currentState = ACTIVE;
          activeLatchUntil = millis() + LATCH_DURATION_MS; // Latch for 5 min from entry
          lastWornTick = millis();
        } else {
          nicla::leds.setColor(red);
          int skinRaw = analogRead(A0);
          displayTroubleShoot(tempRiseWorn, motionWorn, galvanicWorn, skinRaw);
          //checkAndBlinkWarning();
        }
        break;

      case ACTIVE:
        if (isWorn) activeLatchUntil = millis() + LATCH_DURATION_MS;
        bool latchActive = (millis() < activeLatchUntil);
      
        if(!latchActive && wornScore < REMOVE_THRESHOLD) {
          currentState = IDLE;
          lastWornTick = 0; // Reset tick reference when device removed
          clearDisplay();
        } else {
            // --- Worn-time accumulation ---
            uint32_t now = millis();
            /*if (lastWornTick == 0) lastWornTick = now; // First tick after becoming worn
            uint32_t tickDelta = now - lastWornTick;
            if (tickDelta >= 1000) { // Accumulate in 1-second steps
              wornSeconds += tickDelta / 1000;
              lastWornTick += (tickDelta / 1000) * 1000; // Keep sub-second remainder
            }
            uint32_t steps = updateStepTotal();
            String timeWorn = formatWornTime(wornSeconds);

            updateDisplay(steps, isWorn, timeWorn, wornSeconds, goalHours, currentStreak, longestStreak, oldBatteryLevel);
            // Periodic data gathering
              if (millis() - lastLog >= LOG_INTERVAL) {
                lastLog = millis();      

                String dataString = String(steps) + ", {" +
                                    String(tempLog.max_delta, 3) + "," +
                                    String(tempLog.current_temp, 3) + "," +
                                    String(tempLog.min_delta, 3) + "}," +
                                    String(tempRiseWorn) + "," +
                                    String(accelData.vm, 3) + "," +
                                    String(motionWorn) + "," +
                                    String(galvanicScore) + "," +
                                    String(galvanicWorn) + "," +
                                    (isWorn ? "WORN" : "NOT_WORN") + "," +
                                    timeWorn + "," + 
                                    oldBatteryLevel; //new
                
                myDataChar.writeValue(dataString);
              }*/
          }
        break;
      }
    } else {
      BLE.advertise();
      nicla::leds.setColor(blue);
      clearDisplay();
    }
}