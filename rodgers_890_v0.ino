#include <MIDIUSB.h>

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

#define ISTB_DELAY 1
#define ICLK_DELAY 1

#define OSTB_DELAY 1
#define OCLK_DELAY 1

#define INPUT_CHAIN_SIZE 96

#define NOTE_ON_VELOCITY 64
#define NOTE_OFF_VELOCITY 64

#define NOTE_ON 0x90
#define NOTE_OFF 0x80

#define STOPS_CRESC_OFFSET 8

byte currentInputChain[5][96];
byte previousInputChain[5][96];
// 0 = swell, 1 = great, 2 = pedal, 3 = stops, 4 = choir

byte lampChainState[152];

int previousMicros;

void setup() {
  pinMode(ICLK, OUTPUT);
  pinMode(ISTB, OUTPUT);
  pinMode(I0, INPUT);
  pinMode(I1, INPUT);
  pinMode(I2, INPUT);
  pinMode(I3, INPUT);
  pinMode(I4, INPUT);
  pinMode(OCLK, OUTPUT);
  pinMode(OSTB, OUTPUT);
  pinMode(O6, OUTPUT);
}

void loop() {
  midiEventPacket_t rx;
  do {
    if (micros() - previousMicros < 10000) {
      rx = MidiUSB.read();
      if (rx.header != 0) {
        receiveLampMIDI(rx);
        // send back the received MIDI command
        // placeholder
        // MidiUSB.sendMIDI(rx);
        // MidiUSB.flush();
      } else {
        updateLamps();
      }
    } else {
      break;
    }
  } while (rx.header != 0);

  shiftInputs();
  handleInputChanges();
  MidiUSB.flush();
  previousMicros = micros();
}

void updateLamps() {
  for (int i = 0; i < 152; i++) {
    // output chain
    digitalWrite(O6, lampChainState[i]);
    // clock
    digitalWrite(OCLK, HIGH);
    delayMicroseconds(ICLK_DELAY);
    digitalWrite(OCLK, LOW);
  }
  // strobe
  digitalWrite(OSTB, HIGH);
  delayMicroseconds(OSTB_DELAY);
  digitalWrite(OSTB, LOW);
}

void handleInputChanges() {
  for (int input_number = 0; input_number < INPUT_CHAIN_SIZE; input_number++) {
    for (int chain = 0; chain < 5; chain++) {
      if (currentInputChain[chain][input_number] != previousInputChain[chain][input_number]) {
        // input is different than last scan
        if (currentInputChain[chain][input_number]) {
          // input turned on, send midi note on
          noteOn(chain, input_number, NOTE_ON_VELOCITY);
        } else {
          // input turned off, send midi note off
          noteOff(chain, input_number, NOTE_OFF_VELOCITY);
        }
      }
      previousInputChain[chain][input_number] = currentInputChain[chain][input_number];
    }
  }
}

void shiftInputs() {
  // pulse strobe to store data into registers
  digitalWrite(ISTB, HIGH);
  delayMicroseconds(ISTB_DELAY);
  digitalWrite(ISTB, LOW);

  for (int i = 0; i < INPUT_CHAIN_SIZE; i++) {
    // I0
    currentInputChain[0][i] = digitalRead(I0);
    // I1
    currentInputChain[1][i] = digitalRead(I1);
    // I2
    currentInputChain[2][i] = digitalRead(I2);
    // I3
    currentInputChain[3][i] = digitalRead(I3);
    // I4
    currentInputChain[4][i] = digitalRead(I4);

    // clock
    digitalWrite(ICLK, HIGH);
    delayMicroseconds(ICLK_DELAY);
    digitalWrite(ICLK, LOW);
  }
}

void receiveLampMIDI(midiEventPacket_t rx) {
  // check if the channel is 0x03
  if (rx.byte1 & 0x0F != 0x03) {
    return;
  }

  byte note = rx.byte2;
  lampChainState[note] = rx.byte1 & 0xF0 == NOTE_ON;
  if (rx.byte1 >= 8) {
    previousInputChain[3][note - 8] = rx.byte1 & 0xF0 == NOTE_ON;
  }
}

void noteOn(byte channel, byte pitch, byte velocity) {
  if (channel == 3) {
    pitch += STOPS_CRESC_OFFSET;
  }
  midiEventPacket_t noteOn = { 0x09, 0x90 | channel, pitch, velocity };
  MidiUSB.sendMIDI(noteOn);
}

void noteOff(byte channel, byte pitch, byte velocity) {
  if (channel == 3) {
    pitch += STOPS_CRESC_OFFSET;
  }
  midiEventPacket_t noteOff = { 0x08, 0x80 | channel, pitch, velocity };
  MidiUSB.sendMIDI(noteOff);
}
