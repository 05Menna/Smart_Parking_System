// ===========================================
//  SMART PARKING SYSTEM - WOKWI FIXED VERSION
// ===========================================
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// ===============================
//   PARKING SLOTS - IR SENSORS (ANALOG)
// ===============================
#define IR_SLOT1 36   // VP - ADC1_CH0
#define IR_SLOT2 39   // VN - ADC1_CH3
#define IR_SLOT3 35   // ADC1_CH6
#define IR_SLOT4 4   // ADC1_CH7

// ===============================
//   GATE IR SENSORS (ANALOG)
// ===============================
#define IR_BEFORE_GATE 32   // ADC1_CH4
#define IR_AFTER_GATE 33    // ADC1_CH5
// ===============================
//   RGB LEDs FOR PARKING SLOTS (PWM)
// ===============================
#define RGB1_RED  27
#define RGB1_GREEN 15
#define RGB2_RED  14
#define RGB2_GREEN 18
#define RGB3_RED  19
#define RGB3_GREEN 23
#define RGB4_RED  25
#define RGB4_GREEN 26

// ===============================
//   CORNER LEDs (DIGITAL OUTPUTS)
// ===============================
#define CORNER_LED1 1 //TX0
#define CORNER_LED2 3 //RX0
#define CORNER_LED3 16 //RX2
#define CORNER_LED4 17  //TX2

// ===============================
//   ENVIRONMENTAL SENSORS & ACTUATORS
// ===============================
#define DIGITAL_LDR 13    // Digital LDR Input
#define RAIN_SENSOR  34   // Analog Rain Sensor - CHANGED FROM 27 TO BETTER PIN
#define GATE_SERVO   2    // Servo PWM
#define CEILING_SERVO 5   // Servo PWM
#define BUZZER_PIN   12   // PWM Buzzer

// ===============================
//   LCD I2C PINS
// ===============================
#define SDA 21   // Already used by I2C (shared OK)
#define SCL 22


// ===========================================
// NETWORK & MQTT CONFIGURATION
// ===========================================
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "858a3089681b477597400a3b5c9ba46b.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "IOT2025";
const char* mqtt_password = "IOTtraining2025";
// ===========================================
// OBJECT DECLARATIONS
// ===========================================
WiFiClientSecure secureClient;
PubSubClient client(secureClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo gateServo;
Servo ceilingServo;

// ===========================================
// SYSTEM CONFIGURATION CONSTANTS
// ===========================================
const int IR_THRESHOLD = 500;           // Threshold for IR sensors (analog) - NOT USED for PIR
const int RAIN_THRESHOLD = 500;         // Threshold for rain sensor (analog)
const int SENSOR_READ_INTERVAL = 500;   // Read sensors every 500ms
const int MQTT_PUBLISH_INTERVAL = 2000; // Publish data every 2 seconds
const int LCD_UPDATE_INTERVAL = 1000;   // Update LCD every 1 second
const int GATE_AUTO_CLOSE_TIME = 3000; // Auto close gate after 10 seconds
const int GATE_WAIT_TIME = 3000;        // Wait time for vehicle passage
const int BUZZER_DURATION = 2000;       // Buzzer active duration
const int SENSOR_DEBOUNCE = 300;        // Debounce time for sensors

// PIR/Digital sensor configuration
const bool USING_PIR_SENSORS = true;    // Set to true for PIR sensors, false for analog IR
const bool PIR_ACTIVE_HIGH = true;      // PIR gives HIGH when motion detected

// ===========================================
// PARKING SLOT STRUCTURE & VARIABLES
// ===========================================
struct ParkingSlot {
  int id;
  int irPin;              // Analog pin for IR sensor
  int redLedPin;          // Red LED pin
  int greenLedPin;        // Green LED pin
  bool isOccupied;        // Current occupancy status
  bool lastOccupiedState; // Previous state for change detection
  int sensorValue;        // Raw analog sensor value
  int filteredValue;      // Filtered sensor value for stability
  unsigned long lastUpdate;       // Last change timestamp
  unsigned long lastSensorRead;   // Last sensor read time
  bool sensorStable;      // Is sensor reading stable?
  int stableCount;        // Counter for stable readings
  bool manualOverride;    // MQTT manual control flag
};

// Initialize parking slots array (Fixed GPIO pins)
ParkingSlot slots[4] = {
  {1, IR_SLOT1, RGB1_RED, RGB1_GREEN, false, false, 4095, 4095, 0, 0, false, 0, false},
  {2, IR_SLOT2, RGB2_RED, RGB2_GREEN, false, false, 4095, 4095, 0, 0, false, 0, false},
  {3, IR_SLOT3, RGB3_RED, RGB3_GREEN, false, false, 4095, 4095, 0, 0, false, 0, false},
  {4, IR_SLOT4, RGB4_RED, RGB4_GREEN, false, false, 4095, 4095, 0, 0, false, 0, false}
};

// ===========================================
// GATE CONTROL STATE MACHINE
// ===========================================
enum GateState {
  GATE_CLOSED,              // Gate Closed - Rest state
  GATE_OPENING_FOR_ENTRY,   // Gate Opening to enter
  GATE_OPENING_FOR_EXIT,    // Gate Opening to exit
  GATE_OPEN_WAITING,        // Gate is opened and waiting for car to pass
  GATE_AUTO_CLOSING         // Gate is automatically closed
};

GateState currentGateState = GATE_CLOSED;

// Gate sensor variables with filtering (Fixed initial values)
bool vehicleDetectedBefore = false;
bool vehicleDetectedAfter = false;
bool lastVehicleBeforeState = false;
bool lastVehicleAfterState = false;
int beforeGateSensorValue = 4095;  // Initialize to high value (no vehicle)
int afterGateSensorValue = 4095;   // Initialize to high value (no vehicle)
int beforeGateFiltered = 4095;     // Initialize to high value (no vehicle)
int afterGateFiltered = 4095;      // Initialize to high value (no vehicle)

// ===========================================
// SYSTEM STATUS VARIABLES
// ===========================================
bool gateOpen = false;
bool ceilingClosed = false;
bool nightMode = false;
bool lastNightModeState = false;
bool nightModeManualOverride = false;
bool rainDetected = false;
bool lastRainState = false;
int availableSlots = 4;
bool buzzerActive = false;
unsigned long lastParkingFullCheck = 0; // FIX: Add this to prevent gate lockout

// ===========================================
// TIMING VARIABLES
// ===========================================
unsigned long lastSensorRead = 0;
unsigned long lastMQTTPublish = 0;
unsigned long lastLCDUpdate = 0;
unsigned long gateStateChangeTime = 0;
unsigned long buzzerStartTime = 0;
unsigned long lastEnvironmentCheck = 0;
unsigned long systemStartTime = 0;

// ===========================================
// MQTT CALLBACK FUNCTION
// ===========================================
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.printf("📨 MQTT Received [%s]: %s\n", topic, message.c_str());
  handleMQTTCommand(String(topic), message);
}

// ===========================================
// SETUP FUNCTION
// ===========================================
void setup() {
  Serial.begin(115200);
  systemStartTime = millis();

  Serial.println("🚗 Smart Parking System Starting...");
  Serial.println("==================================");

  // 1. Initialize GPIO pins
  initializeGPIOPins();

  // 2. Initialize LCD
  initializeLCD();

  // 3. Initialize Servos
  initializeServos();

  // 4. Initialize WiFi
  initializeWiFi();

  // 5. Initialize MQTT
  initializeMQTT();

  // 6. System ready
  Serial.println("✅ System Ready! All components initialized.");
  Serial.println("==================================");
  displayMessage("System Ready!", "All slots free");

  publishAllSensorData();  
}

// ===========================================
// INITIALIZATION FUNCTIONS
// ===========================================

void initializeGPIOPins() {
  Serial.println("🔧 Initializing GPIO pins...");

  // Initialize parking slot pins
  for (int i = 0; i < 4; i++) {
    if (USING_PIR_SENSORS) {
      pinMode(slots[i].irPin, INPUT);     // PIR sensors - digital input
      Serial.printf("   Slot %d: PIR=Pin%d (Digital)", slots[i].id, slots[i].irPin);
    } else {
      pinMode(slots[i].irPin, INPUT);     // IR sensors - analog input
      Serial.printf("   Slot %d: IR=Pin%d (Analog)", slots[i].id, slots[i].irPin);
    }

    pinMode(slots[i].redLedPin, OUTPUT);
    pinMode(slots[i].greenLedPin, OUTPUT);

    // Initially show all slots as available (green ON, red OFF)
    digitalWrite(slots[i].redLedPin, LOW);
    digitalWrite(slots[i].greenLedPin, HIGH);

    // Initialize sensor values based on sensor type
    if (USING_PIR_SENSORS) {
      slots[i].sensorValue = 4095;      // PIR not triggered = high analog equivalent
      slots[i].filteredValue = 4095;
    } else {
      slots[i].sensorValue = 4095;      // Analog no obstacle = high value
      slots[i].filteredValue = 4095;
    }

    Serial.printf(", Red=Pin%d, Green=Pin%d ✅\n", slots[i].redLedPin, slots[i].greenLedPin);
  }

  // Initialize gate sensors
  if (USING_PIR_SENSORS) {
    pinMode(IR_BEFORE_GATE, INPUT);   // PIR - digital
    pinMode(IR_AFTER_GATE, INPUT);    // PIR - digital
    Serial.println("   Gate sensors: PIR (Digital)");
  } else {
    pinMode(IR_BEFORE_GATE, INPUT);   // IR - analog
    pinMode(IR_AFTER_GATE, INPUT);    // IR - analog
    Serial.println("   Gate sensors: IR (Analog)");
  }

  // Initialize corner LEDs for night mode (avoid input-only pins)
  pinMode(CORNER_LED3, OUTPUT);  // Pin 32
  pinMode(CORNER_LED4, OUTPUT);  // Pin 33
  digitalWrite(CORNER_LED3, LOW);
  digitalWrite(CORNER_LED4, LOW);

  // Initialize environmental sensors
  pinMode(DIGITAL_LDR, INPUT);        // Digital sensor
  pinMode(RAIN_SENSOR, INPUT);        // Analog sensor

  // Initialize buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.printf("✅ GPIO pins initialized successfully (PIR Mode: %s)\n",
                USING_PIR_SENSORS ? "ON" : "OFF");

  // Add initial sensor reading delay
  delay(2000);

  // Take initial sensor readings
  if (USING_PIR_SENSORS) {
    Serial.println("📡 Initial PIR sensor readings:");
    for (int i = 0; i < 4; i++) {
      bool pirState = digitalRead(slots[i].irPin);
      Serial.printf("   Slot %d PIR: %s\n", slots[i].id, pirState ? "HIGH (Motion)" : "LOW (No Motion)");
    }

    bool beforePIR = digitalRead(IR_BEFORE_GATE);
    bool afterPIR = digitalRead(IR_AFTER_GATE);
    Serial.printf("   Gate PIRs - Before: %s, After: %s\n",
                  beforePIR ? "HIGH" : "LOW", afterPIR ? "HIGH" : "LOW");
  } else {
    Serial.println("📡 Initial IR sensor readings:");
    for (int i = 0; i < 4; i++) {
      int initialReading = analogRead(slots[i].irPin);
      slots[i].sensorValue = initialReading;
      slots[i].filteredValue = initialReading;
      Serial.printf("   Slot %d IR: %d\n", slots[i].id, initialReading);
    }

    beforeGateFiltered = analogRead(IR_BEFORE_GATE);
    afterGateFiltered = analogRead(IR_AFTER_GATE);
    Serial.printf("   Gate IRs - Before: %d, After: %d\n", beforeGateFiltered, afterGateFiltered);
  }
}

void initializeLCD() {
  Serial.println("📱 Initializing LCD display...");
  Wire.begin(SDA, SCL);
  lcd.init();
  lcd.backlight();
  displayMessage("Smart Parking", "Initializing...");
  Serial.println("✅ LCD initialized successfully");
}

void initializeServos() {
  Serial.println("🔧 Initializing servo motors...");
  gateServo.attach(GATE_SERVO);
  ceilingServo.attach(CEILING_SERVO);

  // Set initial positions
  gateServo.write(180);     // Gate closed (180 degrees)
  ceilingServo.write(0);  // Ceiling open (0 degrees)

  delay(1000); // Allow servos to reach position
  Serial.println("✅ Servos initialized - Gate closed, Ceiling open");
}

void initializeWiFi() {
  Serial.print("📶 Connecting to WiFi");
  displayMessage("Connecting WiFi", "Please wait...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("✅ WiFi connected! IP address: ");
  Serial.println(WiFi.localIP());
}

void initializeMQTT() {
  Serial.println("📡 Setting up MQTT connection...");
  secureClient.setInsecure(); // Skip certificate validation
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(512);  // ADD THIS LINE - Increase MQTT buffer
  client.setKeepAlive(60);

  connectToMQTT();
  Serial.println("✅ MQTT setup completed");
}

// ===========================================
// MAIN LOOP - ORGANIZED BY FUNCTIONALITY
// ===========================================
void loop() {
  // 1. Maintain MQTT connection
  maintainMQTTConnection();

  // 2. Read and process all sensors (with timing control)
  if (millis() - lastSensorRead >= SENSOR_READ_INTERVAL) {
    readAllSensors();
    lastSensorRead = millis();
  }

  // 3. Handle gate control logic
  handleGateControl();

  // 4. Handle parking slot changes
  handleParkingSlots();

  // 5. Handle environmental conditions
  handleEnvironmentalSensors();

  // 6. Handle system outputs (buzzer, LCD)
  handleSystemOutputs();

  // 7. Publish data to MQTT (with timing control)
  if (millis() - lastMQTTPublish >= MQTT_PUBLISH_INTERVAL) {
    publishSystemOverview();
    publishEnvironmentalData();
    lastMQTTPublish = millis();
  }
}

// ===========================================
// SENSOR READING FUNCTIONS
// ===========================================

void readAllSensors() {
  if (USING_PIR_SENSORS) {
    // Read gate PIR sensors (digital)
    bool newBeforeState = digitalRead(IR_BEFORE_GATE);
    bool newAfterState = digitalRead(IR_AFTER_GATE);

    // For PIR: HIGH = motion detected (vehicle present)
    vehicleDetectedBefore = PIR_ACTIVE_HIGH ? newBeforeState : !newBeforeState;
    vehicleDetectedAfter = PIR_ACTIVE_HIGH ? newAfterState : !newAfterState;

    // Store digital values as analog equivalents for compatibility
    beforeGateFiltered = vehicleDetectedBefore ? 0 : 4095;
    afterGateFiltered = vehicleDetectedAfter ? 0 : 4095;

    // Read parking slot PIR sensors (all digital)
    for (int i = 0; i < 4; i++) {
      // FIX: Skip sensor reading if slot is under manual override
      if (slots[i].manualOverride) {
        continue; // Skip automatic sensor reading for manually controlled slots
      }

      bool newSensorState = digitalRead(slots[i].irPin);

      // For PIR: HIGH = motion/presence detected (slot occupied)
      bool isOccupied = PIR_ACTIVE_HIGH ? newSensorState : !newSensorState;

      // Convert to analog values for compatibility with existing logic
      int analogEquivalent = isOccupied ? 0 : 4095;

      // Use stronger filtering to prevent flickering for PIR sensors
      // PIR sensors can be noisy, so use more aggressive filtering
      slots[i].filteredValue = (slots[i].filteredValue * 7 + analogEquivalent) / 8;
      slots[i].sensorValue = analogEquivalent;
      slots[i].lastSensorRead = millis();

      // Debug output every 5 seconds
      static unsigned long lastPIRDebug = 0;
      if (millis() - lastPIRDebug > 5000) {
        Serial.printf("PIR Slot %d: Digital=%s, Analog=%d, Filtered=%d, Occupied=%s, Override=%s\n",
                      slots[i].id,
                      newSensorState ? "HIGH" : "LOW",
                      analogEquivalent,
                      slots[i].filteredValue,
                      slots[i].isOccupied ? "YES" : "NO",
                      slots[i].manualOverride ? "YES" : "NO");

        if (i == 3) lastPIRDebug = millis(); // Reset after last slot
      }
    }

  } else {
    // Original analog IR sensor code
    int newBeforeValue = analogRead(IR_BEFORE_GATE);
    int newAfterValue = analogRead(IR_AFTER_GATE);

    beforeGateFiltered = (beforeGateFiltered * 7 + newBeforeValue) / 8;
    afterGateFiltered = (afterGateFiltered * 7 + newAfterValue) / 8;

    vehicleDetectedBefore = (beforeGateFiltered < IR_THRESHOLD);
    vehicleDetectedAfter = (afterGateFiltered < IR_THRESHOLD);

    for (int i = 0; i < 4; i++) {
      // Skip sensor reading if slot is under manual override
      if (slots[i].manualOverride) {
        continue;
      }

      int newSensorValue = analogRead(slots[i].irPin);
      slots[i].filteredValue = (slots[i].filteredValue * 7 + newSensorValue) / 8;
      slots[i].sensorValue = newSensorValue;
      slots[i].lastSensorRead = millis();
    }
  }

  // Read environmental sensors (always analog)
  readEnvironmentalSensors();
}

void readEnvironmentalSensors() {
  // LDR is DIGITAL (only digital sensor in the system)
  bool newLDRState = digitalRead(DIGITAL_LDR);
  if (newLDRState != lastNightModeState) {
    nightMode = newLDRState;
    lastNightModeState = newLDRState;
  }

  // Rain sensor is ANALOG
  int rainValue = analogRead(RAIN_SENSOR);
  bool newRainState = (rainValue > RAIN_THRESHOLD);
  if (newRainState != lastRainState) {
    rainDetected = newRainState;
    lastRainState = newRainState;
  }
}

// ===========================================
// GATE CONTROL STATE MACHINE
// ===========================================

void handleGateControl() {
  // Detect sensor edge changes (vehicle entering/leaving sensor zones)
  bool vehicleEnteredBefore = vehicleDetectedBefore && !lastVehicleBeforeState;
  bool vehicleLeftBefore = !vehicleDetectedBefore && lastVehicleBeforeState;
  bool vehicleEnteredAfter = vehicleDetectedAfter && !lastVehicleAfterState;
  bool vehicleLeftAfter = !vehicleDetectedAfter && lastVehicleAfterState;

  // Debug gate sensors
  static unsigned long lastGateDebug = 0;
  if (millis() - lastGateDebug > 2000) {
    Serial.printf("🚪 Gate Debug - Before: %d (%s), After: %d (%s), State: %s\n",
                  beforeGateFiltered, vehicleDetectedBefore ? "DETECTED" : "CLEAR",
                  afterGateFiltered, vehicleDetectedAfter ? "DETECTED" : "CLEAR",
                  getGateStateString().c_str());
    lastGateDebug = millis();
  }

  // Update previous states
  lastVehicleBeforeState = vehicleDetectedBefore;
  lastVehicleAfterState = vehicleDetectedAfter;

  // Gate State Machine Logic
  switch (currentGateState) {

    case GATE_CLOSED:
      // 🚗 Vehicle wants to ENTER from street (only count actual available slots from sensors)
      if (vehicleEnteredBefore) {
        int realAvailableSlots = countRealAvailableSlots();
        if (realAvailableSlots > 0) {
          Serial.printf("🔓 Vehicle requesting ENTRY - Opening gate (Real available: %d)\n", realAvailableSlots);
          changeGateState(GATE_OPENING_FOR_ENTRY);
          controlGate(true, "entry_request");
          displayMessage("Welcome!", "Gate Opening...");
          lastParkingFullCheck = 0; // FIX: Reset the parking full check timer
        } else {
          // FIX: Only show parking full message if we haven't shown it recently
          if (millis() - lastParkingFullCheck > 10000) { // 10 seconds cooldown
            Serial.println("⚠️ Parking FULL - Entry denied");
            activateBuzzer("parking_full");
            displayMessage("PARKING FULL!", "Try later");
            lastParkingFullCheck = millis();
          }
        }
      }
      // 🚗 Vehicle wants to EXIT from inside
      else if (vehicleEnteredAfter) {
        Serial.println("🔓 Vehicle requesting EXIT - Opening gate");
        changeGateState(GATE_OPENING_FOR_EXIT);
        controlGate(true, "exit_request");
        displayMessage("Goodbye!", "Gate Opening...");
      }
      break;

    case GATE_OPENING_FOR_ENTRY:
      // ✅ Vehicle passed through (detected inside) - DON'T change available slots here
      if (vehicleEnteredAfter) {
        Serial.println("✅ Vehicle ENTERED successfully - Gate will close");
        changeGateState(GATE_CLOSED);
        controlGate(false, "entry_completed");
        displayMessage("Entry Complete", "Welcome!");
        // Note: availableSlots will be updated by slot sensors, not gate logic
      }
      // ⏱️ Timeout - vehicle didn't enter
      else if (millis() - gateStateChangeTime > GATE_AUTO_CLOSE_TIME) {
        Serial.println("⏱️ Entry TIMEOUT - Closing gate");
        changeGateState(GATE_CLOSED);
        controlGate(false, "entry_timeout");
      }
      break;

    case GATE_OPENING_FOR_EXIT:
      // 🚗 Vehicle moving out (left inside sensor, approaching outside)
      if (vehicleLeftAfter && vehicleEnteredBefore) {
        Serial.println("🚗 Vehicle EXITING - Waiting for complete exit");
        changeGateState(GATE_OPEN_WAITING);
      }
      // ⏱️ Timeout - close gate
      else if (millis() - gateStateChangeTime > GATE_AUTO_CLOSE_TIME) {
        Serial.println("⏱️ Exit TIMEOUT - Closing gate");
        changeGateState(GATE_CLOSED);
        controlGate(false, "exit_timeout");
      }
      break;

    case GATE_OPEN_WAITING:
      // ✅ Vehicle completely exited (left outside sensor) - DON'T change available slots here
      if (vehicleLeftBefore) {
        Serial.println("✅ Vehicle EXITED completely - Gate closing");
        changeGateState(GATE_CLOSED);
        controlGate(false, "exit_completed");
        displayMessage("Exit Complete", "Goodbye!");
        // Note: availableSlots will be updated by slot sensors, not gate logic
      }
      // ⏱️ Timeout - assume vehicle exited
      else if (millis() - gateStateChangeTime > GATE_WAIT_TIME) {
        Serial.println("⏱️ Exit completion TIMEOUT - Closing gate");
        changeGateState(GATE_CLOSED);
        controlGate(false, "exit_assumed");
      }
      break;
  }
}

void changeGateState(GateState newState) {
  currentGateState = newState;
  gateStateChangeTime = millis();
}

// ===========================================
// PARKING SLOTS MANAGEMENT
// ===========================================

void handleParkingSlots() {
  static unsigned long lastSlotDebug = 0;

  for (int i = 0; i < 4; i++) {
    // FIX: Skip processing if slot is under manual override
    if (slots[i].manualOverride) {
      continue; // Don't change manually controlled slots
    }

    bool currentOccupancy;

    if (USING_PIR_SENSORS) {
      // FIX: Use better threshold for PIR sensors - more stable detection
      currentOccupancy = (slots[i].filteredValue < 1000); // More conservative threshold
    } else {
      // For IR sensors: use original threshold
      currentOccupancy = (slots[i].filteredValue < IR_THRESHOLD);
    }

    // Debug slot sensors every 3 seconds
    if (millis() - lastSlotDebug > 3000) {
      if (USING_PIR_SENSORS) {
        bool rawPIR = digitalRead(slots[i].irPin);
        Serial.printf("🅿️ PIR Slot %d: Raw=%s, Converted=%d, Filtered=%d, State=%s, Stable=%d, Override=%s\n",
                      slots[i].id,
                      rawPIR ? "HIGH" : "LOW",
                      slots[i].sensorValue,
                      slots[i].filteredValue,
                      slots[i].isOccupied ? "OCCUPIED" : "FREE",
                      slots[i].stableCount,
                      slots[i].manualOverride ? "YES" : "NO");
      } else {
        Serial.printf("🅿️ IR Slot %d: Raw=%d, Filtered=%d, State=%s, Stable=%d, Override=%s\n",
                      slots[i].id, slots[i].sensorValue, slots[i].filteredValue,
                      slots[i].isOccupied ? "OCCUPIED" : "FREE", slots[i].stableCount,
                      slots[i].manualOverride ? "YES" : "NO");
      }
    }

    // Check for state change with debouncing
    if (currentOccupancy != slots[i].lastOccupiedState) {
      slots[i].stableCount++;

      // FIX: Increase stability requirement for better debouncing
      int stabilityRequired = USING_PIR_SENSORS ? 8 : 10; // More stable readings required

      if (slots[i].stableCount >= stabilityRequired) {
        // State change confirmed
        bool previousState = slots[i].isOccupied;
        slots[i].isOccupied = currentOccupancy;
        slots[i].lastOccupiedState = currentOccupancy;
        slots[i].lastUpdate = millis();
        slots[i].stableCount = 0;

        // Update RGB LEDs immediately
        updateSlotLEDs(i);

        Serial.printf("🔄 Slot %d STATE CHANGE: %s → %s (%s: %s)\n",
                      slots[i].id,
                      previousState ? "OCCUPIED" : "FREE",
                      currentOccupancy ? "OCCUPIED" : "FREE",
                      USING_PIR_SENSORS ? "PIR" : "IR",
                      USING_PIR_SENSORS ? (digitalRead(slots[i].irPin) ? "HIGH" : "LOW") :
                      String(slots[i].filteredValue).c_str());

        // Publish individual slot update
        publishSlotUpdate(slots[i]);
      }
    } else {
      // State is stable, reset counter
      slots[i].stableCount = 0;
    }
  }

  if (millis() - lastSlotDebug > 3000) {
    lastSlotDebug = millis();
  }

  // Update available slots count based on actual slot states
  int newAvailableSlots = countRealAvailableSlots();
  if (newAvailableSlots != availableSlots) {
    Serial.printf("📊 Available slots CORRECTED: %d → %d\n", availableSlots, newAvailableSlots);
    availableSlots = newAvailableSlots;
  }
}

// Helper function to count actual available slots from sensor readings
int countRealAvailableSlots() {
  int count = 0;
  for (int i = 0; i < 4; i++) {
    if (!slots[i].isOccupied) {
      count++;
    }
  }
  return count;
}

void updateSlotLEDs(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= 4) return;

  // Add debugging to see what's happening with LEDs
  Serial.printf("🔴🟢 Updating Slot %d LEDs: %s\n",
                slots[slotIndex].id,
                slots[slotIndex].isOccupied ? "RED (Occupied)" : "GREEN (Free)");

  if (slots[slotIndex].isOccupied) {
    // Slot occupied: Red ON, Green OFF
    digitalWrite(slots[slotIndex].redLedPin, HIGH);
    digitalWrite(slots[slotIndex].greenLedPin, LOW);
  } else {
    // Slot free: Red OFF, Green ON
    digitalWrite(slots[slotIndex].redLedPin, LOW);
    digitalWrite(slots[slotIndex].greenLedPin, HIGH);
  }

  // Small delay to ensure GPIO changes take effect
  delay(10);
}

// ===========================================
// ENVIRONMENTAL CONTROL FUNCTIONS
// ===========================================

void handleEnvironmentalSensors() {
  // Handle Night Mode (corner lighting)
  handleNightMode();

  // Handle Rain Detection (ceiling control)
  handleRainDetection();
}

void handleNightMode() {
  static bool previousNightMode = false;

  // Only read sensor if not in manual override mode
  if (!nightModeManualOverride) {
    bool newLDRState = digitalRead(DIGITAL_LDR);
    if (newLDRState != lastNightModeState) {
      nightMode = newLDRState;
      lastNightModeState = newLDRState;
    }
  }

  if (nightMode != previousNightMode) {
    previousNightMode = nightMode;

    if (nightMode) {
      Serial.println("🌙 NIGHT MODE activated - Corner lights ON");
      digitalWrite(CORNER_LED3, HIGH);  // Pin 32
      digitalWrite(CORNER_LED4, HIGH);  // Pin 33
      displayMessage("Night Mode", "Lights ON");
    } else {
      Serial.println("☀️ DAY MODE activated - Corner lights OFF");
      digitalWrite(CORNER_LED3, LOW);   // Pin 32
      digitalWrite(CORNER_LED4, LOW);   // Pin 33
    }

    // Publish night mode status
    String mode = nightModeManualOverride ? "manual" : "automatic";
    publishMessage("parking/environment/lighting",
                   "{\"night_mode\":" + String(nightMode ? "true" : "false") +
                   ",\"mode\":\"" + mode + "\"" +
                   ",\"corner_lights\":\"" + String(nightMode ? "on" : "off") +
                   "\",\"timestamp\":" + String(millis()) + "}");
  }
}
void handleRainDetection() {
  static bool previousRainState = false;

  if (rainDetected != previousRainState) {
    previousRainState = rainDetected;

    if (rainDetected) {
      Serial.println("🌧️ RAIN detected - Closing ceiling");
      activateBuzzer("rain_detected");
      controlCeiling(true, "rain_protection");
      displayMessage("Rain Detected!", "Ceiling Closed");
    } else {
      Serial.println("☀️ Rain STOPPED - Opening ceiling");
      controlCeiling(false, "rain_stopped");
      displayMessage("Rain Stopped", "Ceiling Open");
    }

    // Publish rain status
    int rainValue = analogRead(RAIN_SENSOR);
    publishMessage("parking/environment/weather",
                   "{\"rain_detected\":" + String(rainDetected ? "true" : "false") +
                   ",\"rain_sensor_value\":" + String(rainValue) +
                   ",\"ceiling_status\":\"" + String(rainDetected ? "closed" : "open") +
                   "\",\"timestamp\":" + String(millis()) + "}");
  }
}

// ===========================================
// ACTUATOR CONTROL FUNCTIONS
// ===========================================

void controlGate(bool open, String source) {
  if (open && !gateOpen) {
    Serial.println("🔓 Opening gate: " + source);
    gateServo.write(180); // Open position
    gateOpen = true;

    /*publishMessage("parking/actuators/gate",
                   "{\"status\":\"open\",\"source\":\"" + source +
                   "\",\"available_slots\":" + String(availableSlots) +
                   ",\"timestamp\":" + String(millis()) + "}");*/

  } else if (!open && gateOpen) {
    Serial.println("🔒 Closing gate: " + source);
    gateServo.write(90);  // Closed position
    gateOpen = false;

    /*publishMessage("parking/actuators/gate",
                   "{\"status\":\"closed\",\"source\":\"" + source +
                   "\",\"available_slots\":" + String(availableSlots) +
                   ",\"timestamp\":" + String(millis()) + "}");*/
  }
  publishGateStatus();               
}

void controlCeiling(bool close, String source) {
  if (close && !ceilingClosed) {
    Serial.println("☂️ Closing ceiling: " + source);
    ceilingServo.write(180); // Closed position
    ceilingClosed = true;

    publishMessage("parking/actuators/ceiling",
                   "{\"status\":\"closed\",\"source\":\"" + source +
                   "\",\"timestamp\":" + String(millis()) + "}");

  } else if (!close && ceilingClosed) {
    Serial.println("🌤️ Opening ceiling: " + source);
    ceilingServo.write(0);  // Open position
    ceilingClosed = false;

    publishMessage("parking/actuators/ceiling",
                   "{\"status\":\"open\",\"source\":\"" + source +
                   "\",\"timestamp\":" + String(millis()) + "}");
  }
}

// ===========================================
// SYSTEM OUTPUT HANDLERS
// ===========================================

void handleSystemOutputs() {
  // Handle buzzer timing
  handleBuzzer();

  // Update LCD display periodically
  if (millis() - lastLCDUpdate >= LCD_UPDATE_INTERVAL) {
    updateLCDDisplay();
    lastLCDUpdate = millis();
  }
}

void activateBuzzer(String reason) {
  if (!buzzerActive) {
    Serial.println("🔔 Buzzer activated: " + reason);
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerActive = true;
    buzzerStartTime = millis();

    publishMessage("parking/actuators/buzzer",
                   "{\"status\":\"active\",\"reason\":\"" + reason +
                   "\",\"timestamp\":" + String(millis()) + "}");
  }
}

void handleBuzzer() {
  if (buzzerActive && (millis() - buzzerStartTime >= BUZZER_DURATION)) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;

    publishMessage("parking/actuators/buzzer",
                   "{\"status\":\"inactive\",\"timestamp\":" + String(millis()) + "}");
  }
}

void updateLCDDisplay() {
  // Only update LCD if not showing temporary messages
  if (currentGateState == GATE_CLOSED && !buzzerActive) {
    String line1 = "Smart Parking";
    String line2 = "Free: " + String(availableSlots) + "/4";

    if (nightMode) {
      line2 += " N";
    }
    if (rainDetected) {
      line2 += " R";
    }

    displayMessage(line1, line2);
  }
}

void displayMessage(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, 16)); // Limit to 16 characters
  lcd.setCursor(0, 1);
  lcd.print(line2.substring(0, 16)); // Limit to 16 characters
}

// ===========================================
// MQTT CONNECTION & MESSAGE HANDLING
// ===========================================

void maintainMQTTConnection() {
  if (!client.connected()) {
    connectToMQTT();
  }
  client.loop();
}

void connectToMQTT() {
  while (!client.connected()) {
    Serial.println("📡 Connecting to MQTT...");

    if (client.connect("SmartParkingESP32", mqtt_user, mqtt_password)) {
      Serial.println("✅ MQTT Connected to HiveMQ Cloud");
      subscribeToTopics();

      // Announce system online
      publishMessage("parking/system/status",
                     "{\"status\":\"online\",\"uptime\":" +
                     String((millis() - systemStartTime) / 1000) +
                     ",\"timestamp\":" + String(millis()) + "}");

    } else {
      Serial.printf("❌ MQTT connection failed, state: %d. Retrying in 5s...\n", client.state());
      delay(5000);
    }
  }
}

void subscribeToTopics() {
  // Control topics
  client.subscribe("parking/control/gate");
  client.subscribe("parking/control/ceiling");
  client.subscribe("parking/control/nightmode");
  client.subscribe("parking/control/buzzer");
  client.subscribe("parking/control/system");
  client.subscribe("parking/control/slot/+");

  Serial.println("📡 Subscribed to all control topics");
}

void handleMQTTCommand(String topic, String message) {
  Serial.printf("🔧 Processing MQTT command: Topic=%s, Message=%s\n", topic.c_str(), message.c_str());

  // FIX: Improve MQTT command parsing - handle both JSON and simple commands
  DynamicJsonDocument doc(200);
  String command = message; // Default to raw message

  // Try to parse as JSON first
  DeserializationError error = deserializeJson(doc, message);
  if (!error) {
    // JSON parsing successful, extract action
    command = doc["action"] | message;
    Serial.printf("📋 JSON parsed successfully, action: %s\n", command.c_str());
  } else {
    Serial.printf("📋 Using raw message as command: %s\n", command.c_str());
  }

  if (topic == "parking/control/gate") {
    if (command == "open") controlGate(true, "mqtt_manual");
    else if (command == "close") controlGate(false, "mqtt_manual");
    else Serial.printf("❌ Unknown gate command: %s\n", command.c_str());
  }
  else if (topic == "parking/control/ceiling") {
    if (command == "open") controlCeiling(false, "mqtt_manual"); // FIX: open = false (servo position 0)
    else if (command == "close") controlCeiling(true, "mqtt_manual"); // FIX: close = true (servo position 180)
    else Serial.printf("❌ Unknown ceiling command: %s\n", command.c_str());
  }
  else if (topic == "parking/control/buzzer") {
    if (command == "activate") {
      activateBuzzer("mqtt_manual");
    }
    else if (command == "stop" || command == "deactivate") {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerActive = false;
      Serial.println("🔇 Buzzer stopped via MQTT");
      publishMessage("parking/actuators/buzzer",
                     "{\"status\":\"stopped_manual\",\"timestamp\":" + String(millis()) + "}");
    }
    else {
      Serial.printf("❌ Unknown buzzer command: %s (valid: activate, stop)\n", command.c_str());
    }
  }
  else if (topic == "parking/control/nightmode") {
    if (command == "on" || command == "enable") {
      nightMode = true;
      nightModeManualOverride = true;
      digitalWrite(CORNER_LED3, HIGH);
      digitalWrite(CORNER_LED4, HIGH);
      Serial.println("🌙 Night mode MANUALLY enabled via MQTT");
      publishMessage("parking/environment/lighting",
                     "{\"night_mode\":true,\"mode\":\"manual\",\"corner_lights\":\"on\",\"timestamp\":" + String(millis()) + "}");
    }
    else if (command == "off" || command == "disable") {
      nightMode = false;
      nightModeManualOverride = true;
      digitalWrite(CORNER_LED3, LOW);
      digitalWrite(CORNER_LED4, LOW);
      Serial.println("☀️ Night mode MANUALLY disabled via MQTT");
      publishMessage("parking/environment/lighting",
                     "{\"night_mode\":false,\"mode\":\"manual\",\"corner_lights\":\"off\",\"timestamp\":" + String(millis()) + "}");
    }
    else if (command == "auto" || command == "automatic") {
      nightModeManualOverride = false;
      Serial.println("🔄 Night mode returned to AUTOMATIC control via MQTT");
      publishMessage("parking/environment/lighting",
                     "{\"mode\":\"automatic\",\"message\":\"returned_to_sensor_control\",\"timestamp\":" + String(millis()) + "}");
    }
    else {
      Serial.printf("❌ Unknown night mode command: %s (valid: on, off, auto)\n", command.c_str());
    }
  }
  else if (topic == "parking/control/system") {
    if (command == "reset") {
      Serial.println("🔄 System reset requested via MQTT");
      publishMessage("parking/system/status", "{\"status\":\"restarting\",\"timestamp\":" + String(millis()) + "}");
      delay(1000);
      ESP.restart();
    }
    else if (command == "status") {
      Serial.println("📊 System status requested via MQTT");
      publishAllSensorData(); // Send immediate status update
    }
    else Serial.printf("❌ Unknown system command: %s\n", command.c_str());
  }
  else if (topic.startsWith("parking/control/slot/")) {
    // FIX: Improve slot control parsing
    String slotNumStr = topic.substring(topic.lastIndexOf('/') + 1);
    int slotNum = slotNumStr.toInt();

    Serial.printf("🅿️ Slot control: Slot=%d, Command=%s\n", slotNum, command.c_str());

    if (slotNum >= 1 && slotNum <= 4) {
      if (command == "force_occupied" || command == "occupied") {
        forceSlotStatus(slotNum, true);
        Serial.printf("✅ Slot %d manually set to OCCUPIED\n", slotNum);
      }
      else if (command == "force_free" || command == "free") {
        forceSlotStatus(slotNum, false);
        Serial.printf("✅ Slot %d manually set to FREE\n", slotNum);
      }
      else if (command == "auto" || command == "automatic") {
        // FIX: Add option to return slot to automatic control
        if (slotNum >= 1 && slotNum <= 4) {
          int index = slotNum - 1;
          slots[index].manualOverride = false;
          Serial.printf("🔄 Slot %d returned to AUTOMATIC control\n", slotNum);

          // Publish update
          publishSlotUpdate(slots[index]);
        }
      }
      else {
        Serial.printf("❌ Unknown slot command: %s (valid: occupied, free, auto)\n", command.c_str());
      }
    } else {
      Serial.printf("❌ Invalid slot number: %d (valid: 1-4)\n", slotNum);
    }
  }
  else {
    Serial.printf("❌ Unknown MQTT topic: %s\n", topic.c_str());
  }
}

// ===========================================
// MQTT PUBLISHING FUNCTIONS
// ===========================================

void publishAllSensorData() {
  publishSystemOverview();
  publishEnvironmentalData();
  publishGateStatus();

  // Publish individual slot data
  for (int i = 0; i < 4; i++) {
    publishSlotUpdate(slots[i]);
  }
}

void publishSystemOverview() {
  DynamicJsonDocument doc(300);  // Reduced from 400

  // Essential system info only
  doc["available_slots"] = availableSlots;
  doc["gate_open"] = gateOpen;
  doc["ceiling_closed"] = ceilingClosed;
  doc["night_mode"] = nightMode;
  doc["rain_detected"] = rainDetected;
  doc["buzzer_active"] = buzzerActive;
  doc["gate_sensor_before"] = vehicleDetectedBefore;
  doc["gate_sensor_after"] = vehicleDetectedAfter;
  doc["rain_sensor_value"] = analogRead(RAIN_SENSOR);

  String jsonString;
  serializeJson(doc, jsonString);
  publishMessage("parking/system/overview", jsonString);
}

void publishSlotUpdate(ParkingSlot slot) {
  DynamicJsonDocument doc(300);

  doc["slot_id"] = slot.id;
  doc["is_occupied"] = slot.isOccupied;
  doc["sensor_raw"] = slot.sensorValue;
  doc["sensor_filtered"] = slot.filteredValue;
  doc["last_changed"] = slot.lastUpdate;
  doc["stable_readings"] = slot.stableCount;
  doc["led_status"] = slot.isOccupied ? "red" : "green";
  doc["manual_override"] = slot.manualOverride; // FIX: Add manual override status

  String jsonString;
  serializeJson(doc, jsonString);
  publishMessage("parking/sensors/slots/slot" + String(slot.id), jsonString);
}

void publishEnvironmentalData() {
  DynamicJsonDocument doc(250);

  // Lighting conditions
  doc["light_level"] = digitalRead(DIGITAL_LDR) ? "bright" : "dark";
  doc["night_mode"] = nightMode;
  doc["corner_lights"] = nightMode ? "on" : "off";

  // Weather conditions
  int rainValue = analogRead(RAIN_SENSOR);
  doc["rain_sensor_value"] = rainValue;
  doc["rain_detected"] = rainDetected;
  doc["rain_threshold"] = RAIN_THRESHOLD;


  String jsonString;
  serializeJson(doc, jsonString);
  publishMessage("parking/sensors/environment", jsonString);
}

void publishGateStatus() {
  DynamicJsonDocument doc(300);

  doc["status"] = gateOpen ? "open" : "closed";
  doc["state"] = getGateStateString();
  doc["vehicle_before_gate"] = vehicleDetectedBefore;
  doc["vehicle_after_gate"] = vehicleDetectedAfter;
  doc["sensor_before_value"] = beforeGateFiltered;
  doc["sensor_after_value"] = afterGateFiltered;
  doc["auto_close_timeout"] = GATE_AUTO_CLOSE_TIME;
  doc["state_duration"] = millis() - gateStateChangeTime;

  String jsonString;
  serializeJson(doc, jsonString);
  publishMessage("parking/actuators/gate/detailed", jsonString);
}

void publishMessage(String topic, String message) {
  if (client.connected()) {
    // Check message size
    if (message.length() > 400) {
      Serial.printf("⚠️ Message too large (%d bytes) for %s - truncating\n", message.length(), topic.c_str());
      message = message.substring(0, 400);
    }

    bool success = client.publish(topic.c_str(), message.c_str());
    if (success) {
      Serial.printf("📤 Published to %s (%d bytes)\n", topic.c_str(), message.length());
    } else {
      Serial.printf("❌ Failed to publish to %s (%d bytes) - buffer issue?\n", topic.c_str(), message.length());
    }
  } else {
    Serial.println("❌ MQTT not connected - message not sent");
  }
}
// ===========================================
// UTILITY FUNCTIONS
// ===========================================

String getGateStateString() {
  switch (currentGateState) {
    case GATE_CLOSED: return "closed";
    case GATE_OPENING_FOR_ENTRY: return "opening_entry";
    case GATE_OPENING_FOR_EXIT: return "opening_exit";
    case GATE_OPEN_WAITING: return "waiting";
    case GATE_AUTO_CLOSING: return "auto_closing";
    default: return "unknown";
  }
}

void forceSlotStatus(int slotNum, bool occupied) {
  if (slotNum >= 1 && slotNum <= 4) {
    int index = slotNum - 1;

    // FIX: Set manual override flag and update slot status
    slots[index].isOccupied = occupied;
    slots[index].lastOccupiedState = occupied;
    slots[index].lastUpdate = millis();
    slots[index].stableCount = 0;
    slots[index].manualOverride = true; // Enable manual override

    // Update LEDs immediately
    updateSlotLEDs(index);

    Serial.printf("🔧 Slot %d MANUALLY set to: %s (Override enabled)\n",
                  slotNum, occupied ? "🔴 OCCUPIED" : "🟢 FREE");

    // Publish update
    publishSlotUpdate(slots[index]);

    // Recalculate available slots
    availableSlots = countRealAvailableSlots();

    Serial.printf("📊 Available slots updated to: %d/4\n", availableSlots);
  } else {
    Serial.printf("❌ Invalid slot number: %d (valid: 1-4)\n", slotNum);
  }
}

// ===========================================
// DEBUG & MONITORING FUNCTIONS
// ===========================================

void printSystemStatus() {
  Serial.println("\n=================== SYSTEM STATUS ===================");
  Serial.printf("Uptime: %d seconds\n", (millis() - systemStartTime) / 1000);
  Serial.printf("Free Memory: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("WiFi Status: %s (RSSI: %d dBm)\n",
                WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected",
                WiFi.RSSI());
  Serial.printf("MQTT Status: %s\n", client.connected() ? "Connected" : "Disconnected");

  Serial.println("\n--- PARKING STATUS ---");
  Serial.printf("Available Slots: %d/4\n", availableSlots);
  Serial.printf("Occupancy Rate: %.1f%%\n", ((4.0 - availableSlots) / 4.0) * 100);

  Serial.println("\n--- SLOT DETAILS ---");
  for (int i = 0; i < 4; i++) {
    Serial.printf("Slot %d: %s (Raw: %d, Filtered: %d, Override: %s)\n",
                  slots[i].id,
                  slots[i].isOccupied ? "🔴 OCCUPIED" : "🟢 FREE",
                  slots[i].sensorValue,
                  slots[i].filteredValue,
                  slots[i].manualOverride ? "YES" : "NO");
  }

  Serial.println("\n--- GATE STATUS ---");
  Serial.printf("Gate: %s (%s)\n", gateOpen ? "OPEN" : "CLOSED", getGateStateString().c_str());
  Serial.printf("Vehicle Before Gate: %s (Value: %d)\n",
                vehicleDetectedBefore ? "YES" : "NO", beforeGateFiltered);
  Serial.printf("Vehicle After Gate: %s (Value: %d)\n",
                vehicleDetectedAfter ? "YES" : "NO", afterGateFiltered);

  Serial.println("\n--- ENVIRONMENT ---");
  Serial.printf("Night Mode: %s\n", nightMode ? "ON (Dark)" : "OFF (Bright)");
  Serial.printf("Rain Detected: %s (Value: %d)\n",
                rainDetected ? "YES" : "NO", analogRead(RAIN_SENSOR));
  Serial.printf("Ceiling: %s\n", ceilingClosed ? "CLOSED" : "OPEN");

  Serial.println("=====================================================\n");
}