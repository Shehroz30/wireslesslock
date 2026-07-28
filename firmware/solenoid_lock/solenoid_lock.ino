// Solenoid control for wireslesslock (ESP32-S3-WROOM-1)
// CTRL_P -> R18 -> Q1 (AO3400A) gate, low-side switch sinking the
// push-pull solenoid (L3) to GND. GPIO high = solenoid energized.
// R17 gate pulldown keeps it off at boot; D4 is the flyback diode.

const int SOLENOID_PIN = 39; // CTRL_P

// ponytail: no duty-cycle limiting in hardware, so a held-open pin can
// overheat the coil; keep unlock a timed pulse, add PWM hold-current
// limiting only if the lock needs to stay open longer than this.
const unsigned long UNLOCK_PULSE_MS = 300;

void setup() {
  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW); // de-energized
}

void unlock() {
  digitalWrite(SOLENOID_PIN, HIGH);
  delay(UNLOCK_PULSE_MS);
  digitalWrite(SOLENOID_PIN, LOW);
}

void loop() {
  // call unlock() from a button/BLE/GPS-trigger handler
}
