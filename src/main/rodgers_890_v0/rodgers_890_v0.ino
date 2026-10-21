// rodgers_890_v0 — Rodgers 890 console firmware, release v0.3
// Release builds: see ../release/platformio.ini (Leonardo env + USB name).
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

// The CPU-board edge connector brings out four ADC inputs (AD0-AD3, shared
// 870B/890/900B design), but the 890 pedal assembly wires only three pots:
// Swell, Great, Crescendo. AD3 is the spare Choir-expression channel --
// unwired on this console it floats and spams CC7 on MIDI ch 4 (1-based).
// Shoes therefore emit CC7 on ch0-2 only. To reuse the spare (e.g. aux
// input), bump ANALOG_INPUT_COUNT back to 4 and re-add the analogRead(A3).
#define ANALOG_INPUT_COUNT 3

#define MIDI_ADDR_NONE 0xFF      // midiToChain() "no mapping" sentinel
#define MIDI_IDLE_MS 5           // ms of MIDI-in silence before strobing lamps
#define LAMP_STROBE_INTERVAL 250 // ms: force a lamp re-strobe even mid-burst

// --- Crescendo bar + tutti indicator -------------------------------------------
// Lamp bits 0-7 are the crescendo indicator board (6112-719, a 12-element LED
// array: 8 green crescendo + 4 red tutti segments) sitting at the head of the
// lamp chain -- that is exactly why the drawknob map starts at +8. Confirmed
// by the factory OUTPUT CHAIN(6) LAMPS listing (service manual, Data Chains
// section) -- each bit drives a GROUP of segments:
//   bit0->seg1,2   bit1->seg1-3   bit2->seg1-4   bit3->seg5
//   bit4->seg5,6   bit5->seg5-7   bit6->seg5-8   bit7->all 4 red tutti segments
// So a level is a bitmask pattern, not a per-segment thermometer, and the
// clean fills reachable are 0,2,3,4,5,6,7,8 (no single-segment state).
//
// By design the bar is HOST-driven: the Leonardo never reads the crescendo
// shoe ADC for it (the OEM CPU did -- AD0-3 are labeled "Crescendo-expression
// inputs" on schematic 1779 -- we forward the shoe as a raw CC and the host
// owns the level). Inbound:
//   - crescendo: CC CRESCENDO_CC_NUM on CRESCENDO_CHANNEL, value 0-127 ->
//     segments lit via crescBarPattern[].
//   - tutti: Note On/Off (TUTTI_CHANNEL, TUTTI_NOTE) -> lampChainState[7].
//     The tutti piston has no lamp of its own -- the 4 red segments ARE its
//     indicator -- so the echo note is consumed and never hits midiToChain.
// TODO(bench): listing is confirmed for the 890 -- still verify segment order
// on this console (bit order vs physical bar direction).
#define CRESCENDO_CHANNEL 5 // unused channel; shoes emit CC7 on ch0-2
#define CRESCENDO_CC_NUM 20
#define TUTTI_CHANNEL GREAT_CHANNEL
#define TUTTI_NOTE 35 // measured from the console: 91 23 40 on the wire
#define SET_CHANNEL GREAT_CHANNEL
#define SET_NOTE 27 // set keeps Note Off: it is held while capturing

// Piston/toe-stud contacts occupy the low bits of each input chain and are
// all momentary: they emit Note On on press and nothing on release. Chain 3
// (latching drawknobs) and key/pedal-note bits are unaffected.
#define MANUAL_PISTON_BITS 32 // bits 0-31 -> notes 4-35 on chains 0,1,4
#define PEDAL_PISTON_BITS 64  // bits 0-63 -> toe studs on chain 2

// Index = number of bar segments lit (0-8); value = lamp bits 0-6 pattern.
// A 1-segment fill isn't displayable -- it shows segments 1,2 anyway.
const byte crescBarPattern[9] = {
  0x00, // 0
  0x01, // 1 (falls through to 2-segment display)
  0x01, // 2: bit0 -> seg 1,2
  0x02, // 3: bit1 -> seg 1,2,3
  0x04, // 4: bit2 -> seg 1-4
  0x0C, // 5: bit2+3 -> seg 1-5
  0x14, // 6: bit2+4 -> seg 1-6
  0x24, // 7: bit2+5 -> seg 1-7
  0x44, // 8: bit2+6 -> seg 1-8 (full)
};
#define TUTTI_LAMP 7 // drives all four red segments

// --- Power keep-alive (output chain 5, "power supply control") ----------------
// Uncomment POWER_KEEPALIVE only when the S5 driver bodge is wired (parts and
// wiring in docs/power-keepalive.md): spare GPIO -> ~1k -> 2N7000 gate (or
// 2N3904 + 4.7k base resistor), drain/collector -> TP30 (S5), source/emitter
// -> GND (TP48), and a 2.2k pullup TP46 (+12V) -> TP30.
//
// OCLK/OSTB are shared by every output chain, so chain-5 data can ONLY be
// shifted on the same clock edges as the lamp frame -- a dedicated S5-only
// strobe would latch zeros into the lamp chain. updateLamps() therefore
// shifts one 160-bit frame (890 OUTPUT CHAIN(5) listing): all zeros = every
// chain-5 device off, except bit ~66 -- "power supply control" (PS), driving
// the airsound-board transistor that feeds the PSU keep-alive pump -- which
// toggles every strobe. If firmware hangs, strobes stop, the pump dies, and
// the organ powers down: the OEM watchdog behavior is preserved.
//
// The driver inverts: pin HIGH sinks the line = data 0 on the wire, pin LOW
// lets the pullup raise it = data 1.
//
// Bit 66 is from OCR of the chain listing (could be +/-1); verify which
// transistor actually toggles at pad PS on the bench before relying on it.
// S5_FRAME_BITS only has to be >= the physical chain length: positions are
// set by the LAST bits shifted, so extra leading zeros just fall off the far
// end. CAUTION: "all zeros" semantics (stops/mutes/keyers off, expression
// DACs zeroed) are inferred from the listing -- bench-verify before amps.
//#define POWER_KEEPALIVE
#define S5_PIN A4         // deadbug driver gate/base
#define S5_FRAME_BITS 160 // 890 OUTPUT CHAIN(5) listing
#define S5_POWER_BIT 66   // "power supply control" (PS), trem block
#define S5_STROBE_MS 8    // keep-alive cadence -> ~60 Hz square wave on bit 66

#ifdef POWER_KEEPALIVE
bool s5PowerBit = false; // toggled once per output strobe -> AC on bit 66
#endif

byte currentInputChain[5][96];
byte previousInputChain[5][96];
// 0 = swell, 1 = great, 2 = pedal, 3 = stops, 4 = choir

byte lampChainState[152];

int previousAnalogInputs[ANALOG_INPUT_COUNT];
int currentAnalogInputs[ANALOG_INPUT_COUNT];

unsigned long prevMillis = 0;       // last time an inbound MIDI packet was seen
unsigned long lastLampStrobeMs = 0; // last time updateLamps() strobed the chain

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

void pumpMidi();
void handleMidiPacket(midiEventPacket_t rx);

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
#ifdef POWER_KEEPALIVE
  pinMode(S5_PIN, OUTPUT);
#endif
  pinMode(RECEIVE_MIDI_LIMIT_PIN, INPUT_PULLUP);
  pinMode(POWER_DETECT, INPUT);

  //  Serial.begin(312500);
}

void loop()
{
  shiftInputs();
  scanAnalogInputs();

  pumpMidi();

#ifdef POWER_KEEPALIVE
  // The keep-alive pump needs continuous edges, not just lamp updates: force
  // a re-strobe on its own cadence even while inbound MIDI keeps prevMillis
  // fresh and suppresses the idle path.
  const bool keepAliveDue = millis() - lastLampStrobeMs > S5_STROBE_MS;
#else
  const bool keepAliveDue = false;
#endif
  if (millis() - prevMillis > MIDI_IDLE_MS ||
      millis() - lastLampStrobeMs > LAMP_STROBE_INTERVAL ||
      keepAliveDue)
  {
    updateLamps();
  }

  handleInputChanges();
  MidiUSB.flush();
}

// Drain everything the USB-MIDI OUT endpoint currently holds. Called not only
// once per loop but repeatedly from inside shiftInputs()/scanAnalogInputs()/
// updateLamps() so the 32U4's 64-byte (16-event) MIDI FIFO never overflows
// during a large inbound burst (e.g. Cancel clearing many stops at once). An
// overflow makes the host retry back-to-back, which a marginal USB clock/analog
// path can turn into dropped Note Offs -> stops that don't cancel.
void pumpMidi()
{
  midiEventPacket_t rx;
  while ((rx = MidiUSB.read()).header != 0)
  {
    handleMidiPacket(rx);
    prevMillis = millis();
  }
}

void updateLamps()
{
  // Shift out from a snapshot: the pumpMidi() calls below write lampChainState[],
  // so working from a copy keeps this strobe self-consistent; any change that
  // lands mid-strobe is picked up on the next strobe.
  byte lamps[sizeof(lampChainState)];
  memcpy(lamps, lampChainState, sizeof(lampChainState));

#ifdef POWER_KEEPALIVE
  // Chain 5 wants all zeros except the toggling power bit, and its data rides
  // the same OCLK edges as the lamps: hold the wire at data-0 (pin HIGH) for
  // every position except index 66 -- two extra digitalWrite()s per frame.
  s5PowerBit = !s5PowerBit;
  digitalWrite(S5_PIN, HIGH);
  const int frameBits = S5_FRAME_BITS;
#else
  const int frameBits = sizeof(lamps);
#endif

  for (int i = frameBits - 1; i >= 0; i--)
  {
    // output chain
    digitalWrite(O6, i < (int)sizeof(lamps) ? lamps[i] : LOW);
#ifdef POWER_KEEPALIVE
    if (i == S5_POWER_BIT)
    {
      digitalWrite(S5_PIN, s5PowerBit ? LOW : HIGH); // wire = bit 66 value
    }
    else if (i == S5_POWER_BIT - 1)
    {
      digitalWrite(S5_PIN, HIGH); // back to data-0 for the rest of the frame
    }
#endif
    // clock
    delayMicroseconds(OCLK_DELAY);
    digitalWrite(OCLK, HIGH);
    delayMicroseconds(OCLK_DELAY);
    digitalWrite(OCLK, LOW);
    delayMicroseconds(OCLK_DELAY);

    if ((i & 0x0F) == 0)
    {
      pumpMidi();
    }
  }
  // strobe
  digitalWrite(OSTB, HIGH);
  delayMicroseconds(OSTB_DELAY);
  digitalWrite(OSTB, LOW);

  lastLampStrobeMs = millis();
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
        if (chain == SWELL_CHANNEL && input_number == 95 && currentInputChain[chain][input_number])
        {
          if (lampChainState[141])
          {
            noteOn(SWELL_CHANNEL, 127, 64);
          }
          else
          {
            noteOn(SWELL_CHANNEL, 99, 64);
          }
        }
        else
        {
          // input is different than last scan
          midiAddr midiToSend = chainToMidi({chain, input_number});
          if (currentInputChain[chain][input_number])
          {
            // input turned on, send midi note on
            noteOn(midiToSend.channel, midiToSend.note, NOTE_ON_VELOCITY);
          }
          else if (chain == STOP_TAB_CHANNEL ||
                   input_number >= (chain == PEDAL_CHANNEL ? PEDAL_PISTON_BITS : MANUAL_PISTON_BITS) ||
                   (midiToSend.channel == SET_CHANNEL && midiToSend.note == SET_NOTE))
          {
            // input turned off, send midi note off -- keys, latching tabs,
            // and the held SET piston all report their release; pistons and
            // toe studs do not
            noteOff(midiToSend.channel, midiToSend.note, NOTE_OFF_VELOCITY);
          }
        }
      }
      previousInputChain[chain][input_number] = currentInputChain[chain][input_number];
    }
  }
  // analog inputs
  for (int i = 0; i < ANALOG_INPUT_COUNT; i++)
  {
    int diff = currentAnalogInputs[i] - previousAnalogInputs[i];
    if (diff > ANALOG_DIFF || diff < -ANALOG_DIFF)
    {
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

    // Keep servicing USB-MIDI mid-scan so an inbound burst can't overflow the
    // endpoint while this ~3 ms loop runs.
    if ((i & 0x07) == 0)
    {
      pumpMidi();
    }
  }
}

// Apply one inbound USB-MIDI packet to the lamp state. Ignores anything that is
// not a Note On / Note Off, and refuses to act when midiToChain() can't map the
// channel/note (it returns MIDI_ADDR_NONE) or maps out of range -- both of those
// previously caused out-of-bounds writes past lampChainState[] that could
// silently corrupt prevMillis and the analog input state.
//
// This ONLY writes lampChainState[]. It must not write previousInputChain[]:
// Hauptwerk owns stop state, the console just mirrors it, and the stop switches
// latch (the 4021 bit stays set from "pull" until "push"). So:
//   - a physical pull already lands current==previous==on -> no echo to suppress;
//   - a piston/preset never moves currentInputChain[] -> no echo to suppress;
//   - forcing previousInputChain[] to the commanded value here was what made
//     Cancel fight itself: midiToChain() returns a LAMP index (note + 8) for the
//     stop-tab channel, so "Note Off ch3 N" was zeroing previousInputChain[3]
//     [N+8]; on the next scan a still-latched knob at input N+8 read as a 0->1
//     change and Leonardo re-sent "Note On ch3 N+8", turning that stop back on.
//     Dense registrations have many such N / N+8 pairs, which is why a big Cancel
//     left stops set and a second press was needed.
void handleMidiPacket(midiEventPacket_t rx)
{
  byte status = rx.byte1 & 0xF0;
  byte channel = rx.byte1 & 0x0F;

  // Crescendo bar: a CC whose value sets the segment count via the factory
  // group pattern. Only writes lamp bits 0-6 -- bit 7 (tutti) stays untouched.
  if (status == 0xB0 && channel == CRESCENDO_CHANNEL && rx.byte2 == CRESCENDO_CC_NUM)
  {
    byte segmentsLit = ((unsigned int)rx.byte3 * 8) / 127;
    byte pattern = crescBarPattern[segmentsLit];
    for (byte i = 0; i < 7; i++)
    {
      lampChainState[i] = (pattern >> i) & 1;
    }
    return;
  }

  if (status != NOTE_ON && status != NOTE_OFF)
  {
    return;
  }

  // Note On with velocity 0 is a Note Off.
  bool stopOn = (status == NOTE_ON) && (rx.byte3 != 0);

  // Tutti: one lamp bit drives all four red segments -- the piston has no
  // lamp of its own, so the note is consumed here and never falls through
  // to midiToChain (an earlier default collided with General 8's lamp).
  if (channel == TUTTI_CHANNEL && rx.byte2 == TUTTI_NOTE)
  {
    lampChainState[TUTTI_LAMP] = stopOn;
    return;
  }

  chainAddr chainToChange = midiToChain({channel, rx.byte2});
  if (chainToChange.number == MIDI_ADDR_NONE ||
      chainToChange.value >= sizeof(lampChainState))
  {
    return;
  }

  lampChainState[chainToChange.value] = stopOn;
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

void sendCC(byte channel, byte number, byte value)
{
  MidiUSB.sendMIDI({0x0B, 0xB0 | channel, number, value});
}

midiAddr chainToMidi(chainAddr chain)
{
  midiAddr result = {MIDI_ADDR_NONE, MIDI_ADDR_NONE};

  if (chain.number == SWELL_CHANNEL || chain.number == GREAT_CHANNEL || chain.number == CHOIR_CHANNEL)
  {
    result.channel = chain.number;
    result.note = chain.value + 4;
  }
  else if (chain.number == PEDAL_CHANNEL)
  {
    if (chain.value == 47)
    {
      result.channel = GREAT_CHANNEL;
      result.note = 19;
    }
    else
    {
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
  chainAddr result = {MIDI_ADDR_NONE, MIDI_ADDR_NONE};

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
  }
  else if (midiEvent.channel == STOP_TAB_CHANNEL)
  {
    result.value = midiEvent.note + 8;
    result.number = STOP_TAB_CHANNEL;
  }
  else if (midiEvent.channel == PEDAL_CHANNEL)
  {
    // No lamps on the pedal channel; leave result unmapped.
  }

  return result;
}

void scanAnalogInputs()
{
  currentAnalogInputs[0] = analogRead(A0);
  pumpMidi();
  currentAnalogInputs[1] = analogRead(A1);
  pumpMidi();
  currentAnalogInputs[2] = analogRead(A2);
  pumpMidi();
  // A3 (AD3 edge finger 22) is the spare Choir-expression input -- unwired
  // on the 890, so it is neither read nor reported.
}
