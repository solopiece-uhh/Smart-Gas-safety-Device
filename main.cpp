/*
  ============================================================
  SMART GAS SAFETY DEVICE
  Hack for Social Cause - National Technology Innovation Challenge
  PlatformIO project (VS Code)

  Features:
   1. Detects LPG gas leaks (MQ-6 sensor)
   2. Automatically shuts off the regulator valve (servo motor)
   3. Sounds a buzzer + flashes an LED
   4. Measures remaining gas using cylinder weight (load cell + HX711)
   5. Sends an SMS alert for leaks and low gas level (SIM800L, no internet needed)

  ------------------------------------------------------------
  ONE-TIME LOAD CELL CALIBRATION (do this before first real use):
   1. Wire the load cell + HX711 as below, nothing on the platform.
   2. Temporarily add this in setup(), upload, open Serial Monitor:
        scale.set_scale();
        scale.tare();
        Serial.println(scale.get_offset());
   3. Copy the printed number into LOADCELL_OFFSET below.
   4. Place a known weight on it, note scale.get_units(10), and
      compute LOADCELL_CALIBRATION = raw_reading / known_weight_kg.
   5. Remove the temporary calibration lines and upload this file.
  This way the scale reads correctly from every power-on, without
  ever taring again with the cylinder already sitting on it.
  ------------------------------------------------------------
*/

#include <Arduino.h>
#include <HX711.h>
#include <Servo.h>
#include <SoftwareSerial.h>

// ------------------- PIN CONFIGURATION -------------------
#define GAS_SENSOR_PIN   A0     // MQ-6 analog output
#define LOADCELL_DOUT    3      // HX711 data pin
#define LOADCELL_SCK     2      // HX711 clock pin
#define BUZZER_PIN       7
#define LED_PIN          8
#define SERVO_PIN        9
#define SIM800_RX_PIN    10     // Arduino RX <- SIM800L TX
#define SIM800_TX_PIN    11     // Arduino TX -> SIM800L RX (through a voltage divider, SIM800L is 3.3V logic)

// ------------------- OBJECTS -------------------
HX711 scale;
Servo valveServo;
SoftwareSerial sim800(SIM800_RX_PIN, SIM800_TX_PIN);

// ------------------- CONFIGURATION (edit these) -------------------
const int    GAS_LEAK_THRESHOLD   = 350;     // MQ-6 reading above this = leak; retest against your room's clean-air baseline
const float  FULL_CYLINDER_KG     = 14.2;    // net gas weight when cylinder is full
const float  EMPTY_CYLINDER_KG    = 15.5;    // empty cylinder tare weight
const float  LOW_GAS_PERCENT      = 15.0;    // send refill alert below this %
const float  LOADCELL_CALIBRATION = 420.0;   // replace with YOUR tested value (see calibration note above)
const long   LOADCELL_OFFSET      = 0;       // replace with YOUR printed offset (see calibration note above)
const char*  PHONE_NUMBER         = "+91XXXXXXXXXX"; // recipient number

// ------------------- STATE FLAGS -------------------
bool leakAlertSent   = false;
bool refillAlertSent = false;
bool valveShut       = false;

unsigned long lastCheckTime = 0;
const unsigned long CHECK_INTERVAL = 2000; // check every 2 seconds

// ------------------- FUNCTION DECLARATIONS -------------------
int   readGasSensor();
float readGasPercent();
void  handleLeakDetection(int gasReading);
void  handleLowGasLevel(float gasPercent);
void  sendSMS(const __FlashStringHelper* message);

// ============================================================
void setup() {
  Serial.begin(9600);
  sim800.begin(9600);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  valveServo.attach(SERVO_PIN);
  valveServo.write(0);    // valve OPEN (normal position)
  delay(500);
  valveServo.detach();    // power down servo to prevent jitter/current draw when idle

  scale.begin(LOADCELL_DOUT, LOADCELL_SCK);
  scale.set_scale(LOADCELL_CALIBRATION);
  scale.set_offset(LOADCELL_OFFSET); // fixed zero-point, set once during calibration
                                      // do NOT call scale.tare() here - the cylinder
                                      // is already on the platform at every boot

  delay(1000);
  Serial.println(F("System initialised. Monitoring started."));
}

// ============================================================
void loop() {
  if (millis() - lastCheckTime >= CHECK_INTERVAL) {
    lastCheckTime = millis();

    int   gasReading = readGasSensor();
    float gasPercent = readGasPercent();

    Serial.print(F("Gas sensor: "));
    Serial.print(gasReading);
    Serial.print(F(" | Gas level: "));
    Serial.print(gasPercent);
    Serial.println(F("%"));

    handleLeakDetection(gasReading);
    handleLowGasLevel(gasPercent);
  }
}

// ============================================================
int readGasSensor() {
  return analogRead(GAS_SENSOR_PIN);
}

float readGasPercent() {
  if (!scale.is_ready()) return 0.0;

  float totalWeight = scale.get_units(5); // average of 5 readings for stability
  float netGasWeight = totalWeight - EMPTY_CYLINDER_KG;
  float percent = (netGasWeight / FULL_CYLINDER_KG) * 100.0;

  return constrain(percent, 0.0, 100.0);
}

void handleLeakDetection(int gasReading) {
  if (gasReading > GAS_LEAK_THRESHOLD) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);

    if (!valveShut) {
      valveServo.attach(SERVO_PIN);
      valveServo.write(90); // rotate to shut the regulator knob
      delay(1000);
      valveServo.detach();
      valveShut = true;
    }

    if (!leakAlertSent) {
      sendSMS(F("ALERT: Gas leak detected! Regulator valve shut automatically."));
      leakAlertSent = true;
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    leakAlertSent = false;
    // Note: valve stays shut until manually reopened for safety,
    // even after the air clears.
  }
}

void handleLowGasLevel(float gasPercent) {
  if (gasPercent < LOW_GAS_PERCENT && !refillAlertSent) {
    sendSMS(F("Gas level is low (under 15%). Please book a refill soon."));
    refillAlertSent = true;
  }
  if (gasPercent > (LOW_GAS_PERCENT + 5.0)) {
    refillAlertSent = false; // reset once cylinder is replaced
  }
}

void sendSMS(const __FlashStringHelper* message) {
  sim800.println(F("AT+CMGF=1")); // set SMS text mode
  delay(200);
  sim800.print(F("AT+CMGS=\""));
  sim800.print(PHONE_NUMBER);
  sim800.println(F("\""));
  delay(200);
  sim800.println(message);
  delay(200);
  sim800.write(26); // Ctrl+Z, tells module to send the message
  delay(500);
}
