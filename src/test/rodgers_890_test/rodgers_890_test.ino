#define POWER_DETECT 2
#define RECEIVE_MIDI_LIMIT_PIN 3

#define ICLK 4
#define ISTB 5
#define I0 6   // swell
#define I1 7   // great
#define I2 8   // pedal
#define I3 9   // drawknobs
#define I4 10  // choir

#define OCLK 11
#define OSTB 12
#define O6 13

#define ISTB_DELAY 1  // Microseconds
#define ICLK_DELAY 1

#define OSTB_DELAY 1
#define OCLK_DELAY 1

int inputPins[] = { I0, I1, I2, I3, I4, POWER_DETECT };
int inputPullupPins[] = { RECEIVE_MIDI_LIMIT_PIN };
int outputPins[] = { ICLK, ISTB, OCLK, OSTB, O6 };
bool cycleMode;

void setup() {
  for (int pin : inputPins) {
    pinMode(pin, INPUT);
  }
  for (int pin : inputPullupPins) {
    pinMode(pin, INPUT_PULLUP);
  }
  for (int pin : outputPins) {
    pinMode(pin, OUTPUT);
  }
}

void loop() {
  cycleMode = !digitalRead(RECEIVE_MIDI_LIMIT_PIN);
  if (cycleMode) {
    for (int pin : outputPins) {
      digitalWrite(pin, HIGH);
      delay(250);
      digitalWrite(pin, LOW);
      delay(250);
    }
  } else {
    digitalWrite(ISTB, HIGH);
    delayMicroseconds(ISTB_DELAY);
    digitalWrite(ISTB, LOW);

    digitalWrite(OSTB, HIGH);
    delayMicroseconds(OSTB_DELAY);
    digitalWrite(OSTB, LOW);

    digitalWrite(ICLK, HIGH);
    delayMicroseconds(ICLK_DELAY);
    digitalWrite(ICLK, LOW);

    digitalWrite(OCLK, HIGH);
    delayMicroseconds(OCLK_DELAY);
    digitalWrite(OCLK, LOW);
    delay(1000);
  }
}
