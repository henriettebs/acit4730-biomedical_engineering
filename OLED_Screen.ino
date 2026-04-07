#include "Nicla_System.h"
#include "Arduino_BHY2.h"
#include <ArduinoBLE.h>
#include "Nicla_OLED.h"

// --- BLE CONFIGURATION ---
// Service and Characteristic UUIDs (Must match Python)
BLEService myNiclaService("19b10000-0000-537e-4f6c-d104768a1214");
// Added BLERead to Notify to make it easier for Python to discover
BLEStringCharacteristic myDataChar("19b10001-e8f2-537e-4f6c-d104768a1214", BLERead | BLENotify, 512); 
// --- DATE CHARACTERISTIC (written by Python on connect) ---
BLEStringCharacteristic dateChar("19b10002-e8f2-537e-4f6c-d104768a1214", BLEWrite, 16);

// Day tracking
String lastKnownDate = "";   // e.g. "2026-03-24"
String currentDate   = "";

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
bool tempInitialized = false;

// Global accelerometer tracking
AccelData accelData = {0, 0, 0, 0};

// Accel buffer for variance-based wrist detection
const int ACCEL_VM_SAMPLES = 75;
float vm_buffer[ACCEL_VM_SAMPLES] = {0};
int vm_buffer_pos = 0;
bool vm_buffer_full = false;

// --- WORN TIME TRACKING ---
uint32_t wornSeconds = 0;          // Total worn seconds today
uint32_t lastWornTick = 0;         // millis() timestamp of last worn-time update
uint32_t lastConnectedMs = 0;      // To detect long disconnects (new day heuristic)
const uint32_t NEW_DAY_GAP_MS = 6UL * 60 * 60 * 1000; // 6 hours gap = treat as new day
// --- VOTE-BASED DEBOUNCE ---
const int VOTE_WINDOW        = 40;
const float WEAR_THRESHOLD   = 0.70;
const float REMOVE_THRESHOLD = 0.30;
bool wornHistory[40] = {false};
int voteIndex = 0;

float getWornScore() {
  int count = 0;
  for (int i = 0; i < VOTE_WINDOW; i++) {
    if (wornHistory[i]) count++;
  }
  return (float)count / VOTE_WINDOW;
}

// Step-counter smoothing
uint32_t lastStepCounterValue = 0;
uint32_t stepTotal = 0;
const uint32_t STEP_MAX_DELTA_PER_INTERVAL = 500; // ignore spikes larger than this

// Worn-detection based on temperature rise
const float TEMP_RISE_DEGREES = 0.05;      // °C increase to consider "worn"
const float TEMP_SINK_DEGREES = -0.05;
const uint32_t TEMP_RISE_WINDOW_MS = 2*60*1000; // Time window in ms
float tempWindowStart = 0;
uint32_t tempWindowStartMs = 0;
uint32_t lastTempRiseDetectedMs = 0; // Timestamp when temp rise was last detected
bool tempRiseCooldownActive = false; // Flag to track if we're in cooldown period


// Handles cooldown logic after a temperature-rise detection.
// Must be called before updateTempLog() in the loop so min_delta doesn't get updated first.
bool handleTempRiseCooldown(float currentTemp) {
  if (!tempRiseCooldownActive) {
    return false;
  }
  
  float delta = tempLog.min_delta;


  if (delta <= TEMP_SINK_DEGREES) {
    tempRiseCooldownActive = false;
    tempWindowStartMs = 0; // Reset detection window for the next wear event
    tempLog.min_delta = 0.0;
    return false;
  }

  // Still not below baseline, continue treating as worn.
  return true;
}

// Function to update temperature log
void updateTempLog(float newTemp) {
  float tempMaxDelta = 0;
  float tempMinDelta = 0;

  // Initialize min and max with first reading
  if (!tempInitialized) {
    tempLog.current_temp = newTemp;
    tempLog.max_delta = 0;
    tempLog.min_delta = 0;
    tempInitialized = true;
  } else if (abs(newTemp - tempLog.current_temp) >= 5) {
    tempLog.current_temp = tempLog.current_temp;
  } else {
    if (newTemp - tempLog.current_temp > 0) {
      tempMaxDelta =(newTemp - tempLog.current_temp); // Update the window start point to current temp on new max
      tempLog.max_delta = tempLog.max_delta > tempMaxDelta ? tempLog.max_delta : tempMaxDelta;
    }
    if (newTemp - tempLog.current_temp < 0) {
      tempMinDelta =(newTemp - tempLog.current_temp);

      tempLog.min_delta = tempLog.min_delta < tempMinDelta ? tempLog.min_delta : tempMinDelta;
    }
  }
   tempLog.current_temp = newTemp;
}



// Returns true if temperature rise is detected (start of a "worn" period)
bool checkTempRise(float currentTemp) {
  uint32_t now = millis();

  if (tempWindowStartMs == 0) {
    // First reading -- establish the reference point
    tempWindowStart = currentTemp;
    tempWindowStartMs = now;
    return false;
  }
  float delta = tempLog.max_delta;
  uint32_t elapsed = now - tempWindowStartMs;

  // If we're still within the window, check if the rise is large enough
  if (elapsed <= TEMP_RISE_WINDOW_MS && delta >= TEMP_RISE_DEGREES) {
    lastTempRiseDetectedMs = now;  // Save the detection timestamp
    tempRiseCooldownActive = true; // Enter cooldown phase
    tempLog.max_delta = 0.0;
    return true;
  }

  // If the window has passed, reset the reference point
  if (elapsed >= TEMP_RISE_WINDOW_MS) {
    tempWindowStart = currentTemp;
    tempWindowStartMs = now;
  }

  return false;
}

// Function to update accelerometer data and detect wrist wear using variance
bool updateAccelData() {
  accelData.x = accel.x() / 1000.0;
  accelData.y = accel.y() / 1000.0;
  accelData.z = accel.z() / 1000.0;
  float vm_magnitude = sqrt(accelData.x*accelData.x + accelData.y*accelData.y + accelData.z*accelData.z);

  // Push into circular buffer
  vm_buffer[vm_buffer_pos++] = vm_magnitude;
  if (vm_buffer_pos >= ACCEL_VM_SAMPLES) {
    vm_buffer_pos = 0;
    vm_buffer_full = true;
  }

  // Only compute variance once buffer has filled at least once
  if (!vm_buffer_full) {
    return false;
  }

  float sum = 0;
  for (int i = 0; i < ACCEL_VM_SAMPLES; i++) {
    sum += vm_buffer[i];
  }
  float mean = sum / ACCEL_VM_SAMPLES;

  float variance = 0;
  for (int i = 0; i < ACCEL_VM_SAMPLES; i++) {
    float diff = vm_buffer[i] - mean;
    variance += diff * diff;
  }
  variance /= (ACCEL_VM_SAMPLES - 1);

  float sd_vm = sqrt(variance);
  
  // Store the standard deviation as the meaningful metric
  accelData.vm = sd_vm;

  // Worn-on-wrist check: moderate variance indicates wrist movement
  // - too low: stationary (table)
  // - too high: extreme shaking (unlikely for wrist wear)
  return (sd_vm > 0.025) && (sd_vm < 2.5);
}

const int GALVANIC_WINDOW = 10;
bool galvanicHistory[GALVANIC_WINDOW] = {false};
int galvanicIndex = 0;

bool updateGalvanic() {
  int skinTouch = analogRead(A0);
  return (skinTouch > 400);
}

bool getSmoothedGalvanic() {
  int count = 0;
  for (int i = 0; i < GALVANIC_WINDOW; i++) {
    if (galvanicHistory[i]) count++;
  }
  return count >= 7; // 70% of last 10 readings must be true
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

// --- SENSOR LATCH TIMERS ---
const uint32_t LATCH_DURATION_MS = 5UL * 60 * 1000; // 5 minutes
uint32_t activeLatchUntil  = 0;
uint32_t motionLatchUntil  = 0;
uint32_t tempRiseLatchUntil  = 0;


// Returns true if latch is still active, extends it if sensor is currently true
bool updateLatch(bool sensorValue, uint32_t &latchUntil) {
  uint32_t now = millis();
  if (sensorValue) {
    //Serial.println("SensorValue: "); Serial.print(sensorValue);
    latchUntil = now + LATCH_DURATION_MS; // Extend latch on every positive reading
    return true;
  }
  return (now < latchUntil); // Return true if still within latch window
}

// Helper: format seconds as "Xh Ym" string
String formatWornTime(uint32_t seconds) {
  uint32_t hours   = seconds / 3600;
  uint32_t minutes = (seconds % 3600) / 60;
  uint32_t secs    = seconds % 60;
  return String(hours) + "h " + String(minutes) + "m " + String(secs) + "s";
}

void setup() {
  Serial.begin(115200);
  
  // Initialize Nicla Hardware
  nicla::begin();
  nicla::leds.begin();
  initOLED();
  
  // Initialize Sensor Hub
  /*if (!BHY2.begin(NICLA_STANDALONE)) {
    Serial.println("Failed to init BHY2 Hub!");
    while(1);
  }*/
  
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
  myNiclaService.addCharacteristic(dateChar);
  BLE.addService(myNiclaService);
  BLE.advertise();

  Serial.println("Nicla is Online. Waiting for Python connection...");
  nicla::leds.setColor(blue); // Blue = Advertising
}

void loop() {
  BHY2.update();
  BLEDevice central = BLE.central();
  if(central && central.connected()) {
    //Serial.println("Connected");
    nicla::leds.setColor(green);
    //Serial.print("Connected");
    // Check if Python wrote a new date
    if (dateChar.written()) {
        currentDate = dateChar.value();
        Serial.print("Date received: "); Serial.println(currentDate);

        // New day detected → reset worn time
        if (lastKnownDate != "" && currentDate != lastKnownDate) {
            Serial.println("New day — resetting worn time.");
            wornSeconds = 0;
        }
        lastKnownDate = currentDate;
    }

    wornHistory[voteIndex % VOTE_WINDOW] = isWorn;
    voteIndex++;
    float wornScore = getWornScore();
    uint32_t now = millis();

    // --- Compute isWorn once per loop ---
    bool motionRaw   = updateAccelData();
    float currentTemp = temp.value();
    galvanicHistory[galvanicIndex % GALVANIC_WINDOW] = updateGalvanic();
    galvanicIndex++;
    bool galvanicRaw = getSmoothedGalvanic();
    bool tempRiseRaw = handleTempRiseCooldown(currentTemp);
    //Serial.print("TempRiseRaw cooldown: "); Serial.println(tempRiseRaw);
    if (!tempRiseRaw) {
      tempRiseRaw = checkTempRise(currentTemp);
      //Serial.print("TempRiseRaw warmup: "); Serial.println(tempRiseRaw);
    }

    // Galvanic without latch — immediate removal detection
    bool galvanicWorn = getSmoothedGalvanic(); // raw, no latch

    // Motion and temp latched — tolerates stillness and slow temp
    bool motionWorn   = updateLatch(motionRaw,   motionLatchUntil);
    bool tempRiseWorn = updateLatch(tempRiseRaw, tempRiseLatchUntil);

    isWorn = (motionWorn && galvanicWorn) || (tempRiseWorn && galvanicWorn);

    //Serial.print("\nisWorn yes (1)"); Serial.print(isWorn); 
    //Serial.print("Motion"); Serial.print(motionRaw); Serial.print(", temp"); Serial.println(tempRiseRaw);

    switch(currentState) {
      case IDLE:
        if(wornScore >= WEAR_THRESHOLD) {
          currentState = ACTIVE;
          activeLatchUntil = millis() + LATCH_DURATION_MS; // Latch for 5 min from entry
          lastWornTick = millis();
          Serial.print("STATE → ACTIVE (score: "); Serial.print(wornScore); Serial.println(")");
        }
        break;

      case ACTIVE:
        if (isWorn) activeLatchUntil = millis() + LATCH_DURATION_MS;
        bool latchActive = (millis() < activeLatchUntil);
      
        if(!latchActive && wornScore < REMOVE_THRESHOLD) {
          currentState = IDLE;
          lastWornTick = 0; // Reset tick reference when device removed
          Serial.println("STATE → IDLE: Device removed.");
        } else {
            // --- Worn-time accumulation ---
            uint32_t now = millis();
            if (lastWornTick == 0) lastWornTick = now; // First tick after becoming worn
            uint32_t tickDelta = now - lastWornTick;
            if (tickDelta >= 1000) { // Accumulate in 1-second steps
              wornSeconds += tickDelta / 1000;
              lastWornTick += (tickDelta / 1000) * 1000; // Keep sub-second remainder
            }
            // Periodic data gathering
              if (millis() - lastLog >= LOG_INTERVAL) {
                lastLog = millis();
                updateTempLog(currentTemp);
                uint32_t steps = updateStepTotal();
                String timeWorn = formatWornTime(wornSeconds);

                updateDisplay(steps, galvanicWorn, isWorn, timeWorn);

                String dataString = String(steps) + ", {" +
                                    String(tempLog.max_delta, 3) + "," +
                                    String(tempLog.current_temp, 3) + "," +
                                    String(tempLog.min_delta, 3) + "}," +
                                    String(tempRiseWorn) + "," +
                                    String(accelData.vm, 2) + "," +
                                    String(galvanicWorn) + "," +
                                    (isWorn ? "WORN" : "NOT_WORN") + "," +
                                    formatWornTime(wornSeconds); //new
                
                myDataChar.writeValue(dataString);
                Serial.println("Sent: " + dataString);
              }
          }
        break;
      }
    }
  //delay(1000);
}
