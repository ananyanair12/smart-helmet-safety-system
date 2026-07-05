/*
 * ============================================================================
 *  Smart Helmet for Safety Applications
 *  Firmware — Arduino Nano
 * ----------------------------------------------------------------------------
 *  Detects rider impairment (alcohol intoxication and drowsiness) in real
 *  time using an MQ-3 alcohol sensor and an MPU6050 gyroscope/accelerometer,
 *  and drives an internal buzzer plus external red/yellow LEDs to warn the
 *  rider and surrounding traffic.
 *
 *  Hardware:
 *    - Arduino Nano
 *    - MQ-3 alcohol sensor    -> A0
 *    - MPU6050 (I2C)          -> A4 (SDA), A5 (SCL)
 *    - Buzzer                -> D9  (via NPN transistor)
 *    - Red LED (alcohol)     -> D11
 *    - Yellow LED (drowsy)   -> D12
 *
 *  Project: EPICS346 — Smart Helmet for Safety Applications
 *  Author:  Ananya Nair (22MIM10061), VIT Bhopal University
 * ============================================================================
 */

#include <Wire.h>
#include <MPU6050.h>

// ---------------------------------------------------------------------------
// Pin configuration
// ---------------------------------------------------------------------------
#define MQ3_PIN        A0
#define BUZZER_PIN     9
#define RED_LED_PIN    11
#define YELLOW_LED_PIN 12

// ---------------------------------------------------------------------------
// Thresholds (calibrated per the project report, Section 3.2)
// ---------------------------------------------------------------------------
#define BAC_THRESHOLD          0.08f   // Legal intoxication limit
#define ALCOHOL_WARMUP_MS      20000   // MQ-3 sensor warm-up period
#define ALCOHOL_SAMPLE_MS      500     // Sensor poll interval
#define ALCOHOL_SAFE_SAMPLES   60      // 30s of clean readings before clearing alert

#define GYRO_SAMPLE_MS         100     // Sensor poll interval
#define DROWSY_CONFIRM_SAMPLES 15      // 1.5s of sustained tilt before alerting
#define DROWSY_ALERT_HOLD_MS   10000   // Keep alert on for 10s after last detection
#define DROWSY_ESCALATE_COUNT  3       // Occurrences...
#define DROWSY_ESCALATE_WINDOW 120000  // ...within this window trigger escalation

// Drowsiness angle bands (degrees, normalized 0-360)
#define PITCH_FORWARD_MIN 319
#define PITCH_FORWARD_MAX 360
#define ROLL_RIGHT_MIN     40
#define ROLL_RIGHT_MAX     70
#define ROLL_LEFT_MIN     310
#define ROLL_LEFT_MAX     340

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
MPU6050 mpu;

int16_t ax, ay, az, gx, gy, gz;

// Alcohol detection state
int alcoholReadings[10] = {0};
uint8_t alcoholIndex = 0;
bool alcoholDetected = false;
uint16_t alcoholSafeCounter = 0;
unsigned long lastAlcoholSample = 0;

// Drowsiness detection state
bool isDrowsinessAlertActive = false;
uint16_t drowsinessCounter = 0;
uint8_t drowsinessOccurrences = 0;
unsigned long lastDrowsinessTime = 0;
unsigned long firstDrowsinessTime = 0;
unsigned long lastGyroSample = 0;

// Buzzer pattern state (non-blocking)
unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;

void setup() {
  Serial.begin(9600);
  Wire.begin();

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);

  runStartupSelfTest();

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println(F("WARNING: MPU6050 connection failed. Check wiring."));
  }

  Serial.println(F("Smart Helmet initializing..."));
  Serial.println(F("Warming up MQ-3 alcohol sensor (20s)..."));
  delay(ALCOHOL_WARMUP_MS);

  firstDrowsinessTime = millis();
  Serial.println(F("Smart Helmet ready."));
  beep(100); delay(100); beep(100); // two short beeps = ready
}

void loop() {
  unsigned long now = millis();

  if (now - lastAlcoholSample >= ALCOHOL_SAMPLE_MS) {
    lastAlcoholSample = now;
    checkAlcoholLevel();
  }

  if (now - lastGyroSample >= GYRO_SAMPLE_MS) {
    lastGyroSample = now;
    checkDrowsiness();
  }

  updateAlerts();
}

// ---------------------------------------------------------------------------
// Alcohol detection (MQ-3), per report Section 3.2
// ---------------------------------------------------------------------------
void checkAlcoholLevel() {
  int sensorValue = analogRead(MQ3_PIN);

  // 10-sample moving average filter to reduce noise
  alcoholReadings[alcoholIndex] = sensorValue;
  alcoholIndex = (alcoholIndex + 1) % 10;

  long sum = 0;
  for (int i = 0; i < 10; i++) sum += alcoholReadings[i];
  int filteredValue = sum / 10;

  // Calibrated conversion to estimated BAC%
  float bacPercentage = (filteredValue * 0.0008f) - 0.05f;
  if (bacPercentage < 0) bacPercentage = 0;

  if (bacPercentage >= BAC_THRESHOLD) {
    alcoholDetected = true;
    alcoholSafeCounter = 0;
  } else if (alcoholDetected) {
    alcoholSafeCounter++;
    if (alcoholSafeCounter >= ALCOHOL_SAFE_SAMPLES) {
      alcoholDetected = false;
      alcoholSafeCounter = 0;
    }
  }

  Serial.print(F("BAC%: "));
  Serial.println(bacPercentage, 3);
}

// ---------------------------------------------------------------------------
// Drowsiness detection (MPU6050), per report Section 3.2
// ---------------------------------------------------------------------------
void checkDrowsiness() {
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  float accelX = ax / 16384.0f;
  float accelY = ay / 16384.0f;
  float accelZ = az / 16384.0f;

  float pitch = atan2(accelY, sqrt(accelX * accelX + accelZ * accelZ)) * 180.0f / PI;
  float roll  = atan2(-accelX, accelZ) * 180.0f / PI;

  pitch = (pitch < 0) ? pitch + 360 : pitch;
  roll  = (roll  < 0) ? roll  + 360 : roll;

  bool tiltMatchesDrowsyPattern =
      (pitch >= PITCH_FORWARD_MIN && pitch <= PITCH_FORWARD_MAX) ||
      (roll  >= ROLL_RIGHT_MIN    && roll  <= ROLL_RIGHT_MAX)    ||
      (roll  >= ROLL_LEFT_MIN     && roll  <= ROLL_LEFT_MAX);

  if (tiltMatchesDrowsyPattern) {
    drowsinessCounter++;
    if (drowsinessCounter >= DROWSY_CONFIRM_SAMPLES) {
      isDrowsinessAlertActive = true;
      drowsinessOccurrences++;
      lastDrowsinessTime = millis();
    }
  } else {
    drowsinessCounter = 0;
    if (isDrowsinessAlertActive && (millis() - lastDrowsinessTime) > DROWSY_ALERT_HOLD_MS) {
      isDrowsinessAlertActive = false;
    }
  }

  // Escalation: 3+ drowsiness episodes within a 2-minute window
  if (drowsinessOccurrences >= DROWSY_ESCALATE_COUNT &&
      (millis() - firstDrowsinessTime) < DROWSY_ESCALATE_WINDOW) {
    // Handled in updateAlerts() via escalated buzzer cadence
  }

  if ((millis() - firstDrowsinessTime) > DROWSY_ESCALATE_WINDOW) {
    drowsinessOccurrences = 0;
    firstDrowsinessTime = millis();
  }

  Serial.print(F("Pitch: ")); Serial.print(pitch);
  Serial.print(F("  Roll: ")); Serial.println(roll);
}

// ---------------------------------------------------------------------------
// Alert management: alcohol takes priority over drowsiness (report 3.2)
// ---------------------------------------------------------------------------
void updateAlerts() {
  unsigned long now = millis();

  if (alcoholDetected) {
    // Priority 1: continuous buzzer + solid red LED
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, HIGH);
  } else if (isDrowsinessAlertActive) {
    // Priority 2: intermittent buzzer + blinking yellow LED
    digitalWrite(RED_LED_PIN, LOW);

    bool escalated = drowsinessOccurrences >= DROWSY_ESCALATE_COUNT;
    unsigned long onTime  = escalated ? 150 : 300;
    unsigned long offTime = escalated ? 100 : 200;

    if (buzzerState && (now - lastBuzzerToggle >= onTime)) {
      buzzerState = false;
      lastBuzzerToggle = now;
    } else if (!buzzerState && (now - lastBuzzerToggle >= offTime)) {
      buzzerState = true;
      lastBuzzerToggle = now;
    }
    digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
    digitalWrite(YELLOW_LED_PIN, buzzerState ? HIGH : LOW);
  } else {
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
void beep(unsigned int ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

void runStartupSelfTest() {
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(YELLOW_LED_PIN, HIGH);
  beep(150);
  delay(300);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
}
