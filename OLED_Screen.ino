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
String currentTime = ""; // Variable used to check if warning should blink
// Streak tracking
int currentStreak = 0;
int longestStreak = 0;
// Battery
int oldBatteryLevel = 0;
int batteryLevel = 0;
// Sensor latch timers
const uint32_t LATCH_DURATION_MS = 5UL * 60 * 1000; // 5 minutes
uint32_t activeLatchUntil  = 0;


// --- SENSORS ---
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
uint32_t lastTempCheck = 0;
bool dateReceived = false;
bool timeReceived = false;
bool streakReceived = false;
bool dailyTargetReceived = false;
bool bleValuesInitalized = false;

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
const int TEMP_SAMPLES = 12;
const float TEMP_DELTA_THRESHOLD_ON = 0.1;
const float TEMP_DELTA_THRESHOLD_OFF = -0.16;
TempLog tempLog = {0, 0, 0};
CircularBuffer<float,TEMP_SAMPLES> temp_buff;
bool tempRiseActive = false; // True = temp rose 0.2 degrees, waiting for fall
bool tempRiseWorn = false;

// Global accelerometer tracking
const int ACCEL_SAMPLES = 75;
const float ACCEL_THRESHOLD_LOW = 0.05;
const float ACCEL_THRESHOLD_HIGH = 1.5;
AccelData accelData = {0, 0, 0, 0};
CircularBuffer<float,ACCEL_SAMPLES> accel_buff;
bool motionWorn = false;
float accelMean = 0;

// Skin contact tracking
const int GALVANIC_WINDOW = 10;
const int GALVANIC_ON_THRESHOLD = 8; // 80% must be true to activate
const int GALVANIC_OFF_THRESHOLD = 3; // Below 30% to deactivate
const int GALVANIC_SKIN_THRESHOLD = 300; // Calibrate on hardware
bool galvanicSmoothed = false; // State persistence
CircularBuffer<int,10> galvanic_buff;

// Step-counter smoothing
uint32_t lastStepCounterValue = 0;
uint32_t stepTotal = 0;
const uint32_t STEP_MAX_DELTA_PER_INTERVAL = 100; // ignore spikes larger than this

// Worn time tracking
uint32_t wornSeconds = 0;          // Total worn seconds today
uint32_t lastWornTick = 0;         // millis() timestamp of last worn-time update
int goalHours = 0; // Value retrieved from BLE connection

// Votebased debounce
const int WORN_WINDOW = 40;
const float WEAR_THRESHOLD = 0.70; // Worn score needs to be over 70% to trigger worn
const float REMOVE_THRESHOLD = 0.30; // Worn score needs to fall below 30% to trigger not worn

CircularBuffer<bool,WORN_WINDOW> worn_buff;

// --- WORN SCORE ---
float getWornScore() {
  int count = 0;
  for (int i = 0; i < WORN_WINDOW; i++) {
    if (worn_buff[i]) {
      count++;
    }
  }
  return (float)count / WORN_WINDOW;
}

// --- TEMPERATURE ---
bool updateTempState(float currentTemp) {
  tempLog.current_temp = currentTemp;
  temp_buff.unshift(currentTemp);

  String tempValues = "";
  if(!temp_buff.isFull()) {
    return false;
  }

  float delta = temp_buff.first() - temp_buff.last();
  Serial.print("First - last: "); Serial.print(temp_buff.first()); Serial.print(" - "); Serial.print(temp_buff.last()); Serial.print("= DELTA: "); Serial.println(delta);
  if (!tempRiseActive) {
    if (delta >= TEMP_DELTA_THRESHOLD_ON) { // Found valid rise!
      tempRiseActive = true;
      tempLog.max_delta = delta;
      return true; // Now active
    }
    return false;
  }

  if (tempRiseActive) {
    if (delta <= TEMP_DELTA_THRESHOLD_OFF) { // Found the fall
      tempRiseActive = false;
      tempLog.min_delta = delta;
      return false;
    }
    return true; // Still active
  }
  return false;
  delay(5000);
}
// --- ACCELEROMETER ---
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
  accelMean = mean;

  float variance = 0;
  for (int i = 0; i < size; i++) {
    float diff = accel_buff[i] - mean;
    variance += diff * diff;
  }
  variance /= (size - 1);

  float sd_vm = sqrt(variance);
  accelData.vm = sd_vm;

  return (sd_vm > ACCEL_THRESHOLD_LOW) && (sd_vm < ACCEL_THRESHOLD_HIGH);
}
// --- GALVANIC ---
int updateGalvanic() {
  int rawValue = analogRead(A0); // Reads from one of two analog pins on NSME, NIcla boards ADC converts to 10 bits (default)
  bool isContact = (rawValue > GALVANIC_SKIN_THRESHOLD);

  galvanic_buff.unshift(isContact);
  int count = 0;
  for (int i = 0; i < GALVANIC_WINDOW; i++) {
    if(galvanic_buff[i]) count++;
  }

  // Hysteresis: need 80% to turn ON, less than 30% to turn OFF
  if (galvanicSmoothed && count <= GALVANIC_OFF_THRESHOLD) {
    galvanicSmoothed = false;
  } else if (!galvanicSmoothed && count >= GALVANIC_ON_THRESHOLD) {
    galvanicSmoothed = true;
  }
  return rawValue;
}

bool getSmoothedGalvanic() {
  return galvanicSmoothed;
}
// --- SMOOTH STEP COUNTER ---
uint32_t updateStepTotal() {
  uint32_t rawSteps = (uint32_t)stepCounter.value();
  uint32_t deltaSteps = 0;

  if (lastStepCounterValue != 0) {
    if (rawSteps >= lastStepCounterValue) {
      deltaSteps = rawSteps - lastStepCounterValue;
    } else {
      deltaSteps = rawSteps;
    }
  }
  lastStepCounterValue = rawSteps;
  if (deltaSteps <= STEP_MAX_DELTA_PER_INTERVAL) { // Ignore large spikes
    stepTotal += deltaSteps;
  }
  return stepTotal;
}
// --- WARNING DISPLAYED ---
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

    if(wornSeconds == 0 && currentHour >= 16 && currentHour < 20) { // Only show warning if orthosis has not been worn, and it's between 4PM and 7PM
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
// --- BATTERY ---
void updateBatteryLevel() {
  batteryLevel = nicla::getBatteryVoltagePercentage();

  if (batteryLevel != oldBatteryLevel) {       
    oldBatteryLevel = batteryLevel;             
  }
}

// --- BLE CONNECTION VALUES ---
void getBLEConnectionValues() {
  if (!dateReceived && dateChar.written()) {
    currentDate = dateChar.value();
    dateReceived = true;
  }
  if (!timeReceived && timeChar.written()) {
    currentTime = timeChar.value();
    timeReceived = true;
  }
  if (!streakReceived && streakChars.written()) {
    String streakData = streakChars.value();
    int commaIndex = streakData.indexOf(',');
    if (commaIndex > 0) {
      currentStreak = streakData.substring(0, commaIndex).toInt();
      longestStreak = streakData.substring(commaIndex + 1).toInt();
      streakReceived = true;
    }
  }
  if (!dailyTargetReceived && dailyTargetChar.written()){
    String goalReceived = dailyTargetChar.value();
    goalHours = goalReceived.toInt();
    dailyTargetReceived = true;
  }
  if(dateReceived && timeReceived && streakReceived && dailyTargetReceived) {
    bleValuesInitalized = true;
  }
}

// --- HELPER FUNCTIONS ---
// Helper function: format seconds as "Xh Ym Zs" string
String formatWornTime(uint32_t seconds) {
  uint32_t hours   = seconds / 3600;
  uint32_t minutes = (seconds % 3600) / 60;
  uint32_t secs    = seconds % 60;
  return String(hours) + "h " + String(minutes) + "m " + String(secs) + "s";
}

// Helper function: Get hour value from time string
int parseHourFromTimeChar() {
  String timeString = currentTime;

  if(timeString.length() >= 2){
    return timeString.substring(0,2).toInt();
  }
  return -1; // Error
}
// Helper function: Clear all data as reset for a new day
void clearDataForNewDay() {
  wornSeconds = 0;
  stepTotal = 0;
  lastStepCounterValue = 0;
  tempLog = {0, 0, 0};
  accelData = {0, 0, 0, 0};
  temp_buff.clear();
  accel_buff.clear();
  galvanic_buff.clear();
  worn_buff.clear();
  clearDataOnDisconnect();
}

// Helper function: Clear data on BLE disconnect
void clearDataOnDisconnect() {
  dateReceived = false;
  timeReceived = false;
  streakReceived = false;
  dailyTargetReceived = false;
  bleValuesInitalized = false;
}

// --- SETUP ---
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
  nicla::setBatteryNTCEnabled(false);  // Battery doesn't have an NTC thermistor connected
  nicla::enableCharging(100);  // enable the battery charger for a 100 mA charging current
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
  myNiclaService.addCharacteristic(dailyTargetChar);
  BLE.addService(myNiclaService);
  BLE.advertise();

  nicla::leds.setColor(blue);
}
// --- LOOP ---
void loop() {
  BHY2.update();
  BLEDevice central = BLE.central();

  if(central && central.connected()) {
    nicla::leds.setColor(green);
    if(!bleValuesInitalized) {
      getBLEConnectionValues();
      updateBatteryLevel();
    }
    // New day detected → reset worn time
    if (currentDate != "" && lastKnownDate != "" && currentDate != lastKnownDate) {
      clearDataForNewDay();
      lastKnownDate = currentDate;
    }
    uint32_t nowTemp = millis();
    if (nowTemp - lastTempCheck >= LOG_INTERVAL){
      lastTempCheck = nowTemp;
      float currentTemp = temp.value();
      tempRiseWorn = updateTempState(currentTemp);
    }
    
    motionWorn = updateAccelData();
    int galvanicScore = updateGalvanic();
    bool galvanicWorn = getSmoothedGalvanic();

    uint32_t steps = updateStepTotal();
    String timeWorn = formatWornTime(wornSeconds);

    worn_buff.unshift(isWorn);
    float wornScore = getWornScore();

    isWorn = (motionWorn && galvanicWorn) || (tempRiseWorn && galvanicWorn);

    switch(currentState) {
      case IDLE:
        if(wornScore >= WEAR_THRESHOLD) {
          currentState = ACTIVE;
          activeLatchUntil = millis() + LATCH_DURATION_MS; // Latch for 5 min from entry
          lastWornTick = millis();
        } else {
          checkAndBlinkWarning();
          int skinRaw = analogRead(A0);
          displayTroubleShoot(tempRiseWorn, tempLog.current_temp, motionWorn, accelData.vm, galvanicWorn, skinRaw);
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
            // Worn-time accumulation
            uint32_t now = millis();
            if (lastWornTick == 0) lastWornTick = now; // First tick after becoming worn
            uint32_t tickDelta = now - lastWornTick;
            if (tickDelta >= 1000) {
              wornSeconds += tickDelta / 1000;
              lastWornTick += (tickDelta / 1000) * 1000;
            }
            uint32_t steps = updateStepTotal();
            String timeWorn = formatWornTime(wornSeconds);
            updateDisplay(steps, isWorn, timeWorn, wornSeconds, goalHours, currentStreak, longestStreak, oldBatteryLevel);
            // Periodic data gathering
              if (now - lastLog >= LOG_INTERVAL) {
                lastLog = now;
                dateReceived = false;

                String dataString = String(steps) + ", {" +
                                    String(tempLog.max_delta, 3) + "," +
                                    String(tempLog.current_temp, 3) + "," +
                                    String(tempLog.min_delta, 3) + "}," +
                                    String(tempRiseWorn) + "," +
                                    String(accelData.vm, 3) + "," +
                                    String(accelMean) + "," +
                                    String(motionWorn) + "," +
                                    String(galvanicScore) + "," +
                                    String(galvanicWorn) + "," +
                                    (isWorn ? "WORN" : "NOT_WORN") + "," +
                                    timeWorn;
                
                myDataChar.writeValue(dataString);
              }
          }
        break;
      }
    } else {
      BLE.advertise();
      nicla::leds.setColor(blue);
      clearDisplay();
      clearDataOnDisconnect();
    }
}
