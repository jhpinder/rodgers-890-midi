#include <Keyboard.h>
#define SWITCH_PIN 8
#define RELAY_PIN 4

boolean currentSwitchState;
boolean lastSwitchState;
boolean powerState;

void setup() {
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  currentSwitchState = readSwitchPin();
  lastSwitchState = currentSwitchState;
  powerState = false;
}

void loop() {
  if (currentSwitchState != lastSwitchState && currentSwitchState) {
    powerState ? turnOffRelay() : turnOnRelay();
  }
  lastSwitchState = currentSwitchState;
  delay(20);
  currentSwitchState = readSwitchPin();
}

void turnOnRelay() {
  powerState = true;
  digitalWrite(RELAY_PIN, HIGH);
  delay(2000);
}

void turnOffRelay() {
  powerState = false;
  digitalWrite(RELAY_PIN, LOW);
  delay(2000);
}

boolean readSwitchPin() {
  return !digitalRead(SWITCH_PIN);
}

