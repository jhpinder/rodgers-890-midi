#include <MIDIUSB.h>

#define POWER_DETECT 2
#define RECEIVE_MIDI_LIMIT_PIN 3

#define ICLK 4
#define ISTB 5
#define I0 6  // swell
#define I1 7  // great
#define I2 8  // pedal
#define I3 9  // drawknobs
#define I4 10 // choir

#define OCLK 11
#define OSTB 12
#define O6 13

#define ISTB_DELAY 1 // microseconds
#define ICLK_DELAY 1

#define OSTB_DELAY 1
#define OCLK_DELAY 1

#define INPUT_CHAIN_SIZE 96

#define NOTE_ON_VELOCITY 64
#define NOTE_OFF_VELOCITY 64

#define NOTE_ON 0x90
#define NOTE_OFF 0x80

#define SWELL_CHANNEL 0
#define GREAT_CHANNEL 1
#define PEDAL_CHANNEL 2
#define STOP_TAB_CHANNEL 3
#define CHOIR_CHANNEL 4

#define MIDI_CC_NUM 7
#define ANALOG_DIFF 10

byte currentInputChain[5][96];
byte previousInputChain[5][96];
// 0 = swell, 1 = great, 2 = pedal, 3 = stops, 4 = choir

byte lampChainState[152];

int previousAnalogInputs[4];
int currentAnalogInputs[4];

struct midiAddr
{
  byte channel;
  byte note;
};

struct chainAddr
{
  byte number;
  byte value;
};

void setup()
{
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
  pinMode(RECEIVE_MIDI_LIMIT_PIN, INPUT_PULLUP);
  pinMode(POWER_DETECT, INPUT);
}

void loop()
{
  midiEventPacket_t rx;
  do
  {
    rx = MidiUSB.read();
    if (rx.header != 0)
    {
      receiveLampMIDI(rx);
    }
  } while (rx.header != 0);

  updateLamps();
  shiftInputs();
  scanAnalogInputs();
  handleInputChanges();
  MidiUSB.flush();
}

void updateLamps()
{
  for (int i = sizeof(lampChainState) - 1; i >= 0; i--)
  {
    // output chain
    digitalWrite(O6, lampChainState[i]);
    // clock
    delayMicroseconds(OCLK_DELAY);
    digitalWrite(OCLK, HIGH);
    delayMicroseconds(OCLK_DELAY);
    digitalWrite(OCLK, LOW);
  }
  // strobe
  delayMicroseconds(OCLK_DELAY);
  digitalWrite(OSTB, HIGH);
  delayMicroseconds(OSTB_DELAY);
  digitalWrite(OSTB, LOW);
}

void handleInputChanges()
{
  // switches
  for (int input_number = 0; input_number < INPUT_CHAIN_SIZE; input_number++)
  {
    for (int chain = 0; chain < 5; chain++)
    {
      if (currentInputChain[chain][input_number] != previousInputChain[chain][input_number])
      {
        if (chain == SWELL_CHANNEL && input_number == 95 && currentInputChain[chain][input_number]) {
          if (lampChainState[141]) {
            noteOn(SWELL_CHANNEL, 127, 64);
          } else {
            noteOn(SWELL_CHANNEL, 99, 64);
          }
        } else {
          // input is different than last scan
          midiAddr midiToSend = chainToMidi({chain, input_number});
          if (currentInputChain[chain][input_number])
          {
            // input turned on, send midi note on
            noteOn(midiToSend.channel, midiToSend.note, NOTE_ON_VELOCITY);
          }
          else
          {
            // input turned off, send midi note off
            noteOff(midiToSend.channel, midiToSend.note, NOTE_OFF_VELOCITY);
          }
        }
      }
      previousInputChain[chain][input_number] = currentInputChain[chain][input_number];
    }
  }
  // analog inputs
  for (int i = 0; i < 4; i++) {
    int diff = currentAnalogInputs[i] - previousAnalogInputs[i];
    if (diff > ANALOG_DIFF || diff < -ANALOG_DIFF) {
      // map 0-1023 to 0-127
      sendCC(i, MIDI_CC_NUM, map(currentAnalogInputs[i], 0, 1023, 0, 127));
      previousAnalogInputs[i] = currentAnalogInputs[i];
    }
  }
}

void shiftInputs()
{
  // pulse strobe to store data into registers
  digitalWrite(ISTB, HIGH);
  delayMicroseconds(ISTB_DELAY);
  digitalWrite(ISTB, LOW);
  delayMicroseconds(ICLK_DELAY);

  for (int i = INPUT_CHAIN_SIZE - 1; i >= 0; i--)
  {
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
    delayMicroseconds(ICLK_DELAY);
  }
}

void receiveLampMIDI(midiEventPacket_t rx)
{
  chainAddr chainToChange = midiToChain({rx.byte1 & 0x0F, rx.byte2});
  lampChainState[chainToChange.value] = ((rx.byte1 >> 4) & 0x0F) == 0x09;

  if (chainToChange.number == STOP_TAB_CHANNEL)
  {
    previousInputChain[STOP_TAB_CHANNEL][chainToChange.value] = rx.byte1 & 0xF0 == NOTE_ON;
  }
}

void noteOn(byte channel, byte pitch, byte velocity)
{
  midiEventPacket_t noteOn = {0x09, 0x90 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOn);
}

void noteOff(byte channel, byte pitch, byte velocity)
{
  midiEventPacket_t noteOff = {0x08, 0x80 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOff);
}

void sendCC(byte channel, byte number, byte value) {
  MidiUSB.sendMIDI({ 0x0B, 0xB0 | channel, number, value });
}

midiAddr chainToMidi(chainAddr chain)
{
  midiAddr result = {-1, -1};

  if (chain.number == SWELL_CHANNEL || chain.number == GREAT_CHANNEL || chain.number == CHOIR_CHANNEL)
  {
    result.channel = chain.number;
    result.note = chain.value + 4;
  }
  else if (chain.number == PEDAL_CHANNEL)
  {
    if (chain.value == 47) {
      result.channel = GREAT_CHANNEL;
      result.note = 19;
    } else {
      result.channel = chain.number;
      result.note = chain.value - 28;
    }
  }
  else if (chain.number == STOP_TAB_CHANNEL)
  {
    result.note = chain.value;
    result.channel = chain.number;
  }

  return result;
}

chainAddr midiToChain(midiAddr midiEvent)
{
  chainAddr result = {-1, -1};

  if (midiEvent.channel == SWELL_CHANNEL || midiEvent.channel == GREAT_CHANNEL || midiEvent.channel == CHOIR_CHANNEL)
  {    
    result.number = STOP_TAB_CHANNEL;
    switch (midiEvent.channel)
    {
    case SWELL_CHANNEL:
      if (midiEvent.note >= 19 && midiEvent.note <= 26)
      {
        result.value = midiEvent.note + 85;
      }
      else if (midiEvent.note >= 28 && midiEvent.note <= 30)
      {
        result.value = midiEvent.note + 83;
      }
      else if (midiEvent.note == 99)
      {
        result.value = 141;
      }
      break;
    case GREAT_CHANNEL:
      if (midiEvent.note >= 11 && midiEvent.note <= 15)
      {
        result.value = midiEvent.note + 103;
      }
      else if (midiEvent.note >= 19 && midiEvent.note <= 24)
      {
        result.value = midiEvent.note + 101;
      }
      else if (midiEvent.note >= 27 && midiEvent.note <= 31)
      {
        result.value = midiEvent.note + 100;
      }
      break;
    case CHOIR_CHANNEL:
      if (midiEvent.note >= 13 && midiEvent.note <= 14)
      {
        result.value = midiEvent.note + 119;
      }
      else if (midiEvent.note >= 18 && midiEvent.note <= 23)
      {
        result.value = midiEvent.note + 116;
      }
      else if (midiEvent.note >= 24 && midiEvent.note <= 27)
      {
        result.value = midiEvent.note + 119;
      }
      else if (midiEvent.note >= 29 && midiEvent.note <= 32)
      {
        result.value = midiEvent.note + 118;
      }
      break;
    default:
      break;
    }
  } else if (midiEvent.channel == STOP_TAB_CHANNEL) {
    result.value = midiEvent.note + 8;
    result.number = STOP_TAB_CHANNEL;
  }

  return result;
}

void scanAnalogInputs() {
  currentAnalogInputs[0] = analogRead(A0);
  currentAnalogInputs[1] = analogRead(A1);
  currentAnalogInputs[2] = analogRead(A2);
  currentAnalogInputs[3] = analogRead(A3);
}

