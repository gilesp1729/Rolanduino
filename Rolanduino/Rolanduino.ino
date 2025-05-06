// Synthesiser/piano based on an old Roland keyboard and a fork of
// DLehenbauer's excellent synth library. Runs on Arduino Mega2560.
// (the synth will run on Uno as well, but there are not enough pins to scan
// the keyboard)

// Scanning for Roland piano keyboards, such as the HP-237Re. There are many
// models of these old keyboards but the scanning details are usually quite
// similar.

// The keyboard is wired in groups of 8 keys (11 groups = 88 keys in total)
// The 8 T-lines are normally high, but each is pulled low in turn to sense
// each of the 8 notes in each group. Each group has a half-press and full-press
// line, the timing between them is used to sense key velocity.
// In the service manuals they are called SM and PM lines
// (I don't know what this stands for)
// Some older keyboards have break and make lines instead.

// So there are 11 groups * 2 bits = 22 bits = 3 bytes reported
// back from each scan, for a total of 24 bytes from 8 scans.

// The PM/SM lines are wired as follows on the Mega to
// the ports on the heel of the board (the double row connector) while the
// T lines are connected to port F. There are a lot of wires!

//  bit     7       6       5       4       3       2       1       0
//        -----------------------------------------------------------------
//  PORTL |  PM3  |  SM3  |  PM2  |  SM2  |  PM1  |  SM1  |  PM0  |  SM0  |
//        -----------------------------------------------------------------
//        -----------------------------------------------------------------
//  PORTC |  PM7  |  SM7  |  PM6  |  SM6  |  PM5  |  SM5  |  PM4  |  SM4  |
//        -----------------------------------------------------------------
//        -----------------------------------------------------------------
//  PORTA |       |       | PM10  | SM10  |  PM9  |  SM9  |  PM8  |  SM8  |
//        -----------------------------------------------------------------
//        -----------------------------------------------------------------
//  PORTF |  T7   |  T6   |  T5   |  T4   |  T3   |  T2   |  T1   |  T0   |
//(output)-----------------------------------------------------------------


// The default instrument is instrument 0 (grand piano)
// Selection of instruments from the synth is by pressing and holding the
// bottom note on the keyboard for 1 second. While holding, playing any other note
// will select an instrument from the synth's internal table. Not all of them
// produce unique or correct sounds and these have been skipped. The percussion
// have also been skipped for now.

// The instruments are arranged as follows by octave:

//            ----------------------------------------------------------------------------------
// Midi note  | 24       | 36       | 48        | 60       | 72       | 84       | 96      108 |
// Octave     | C1       | C2       | C3        | C4       | C5       | C6       | C7       C8 |
//            | Organs   | Brass    | Woodwinds | Pianos   | Pianos   | Strings  | Effects     |
//            ----------------------------------------------------------------------------------
// Synth inst     16-23      56-67      68-77      0-15                  24-46       52-55
//   (from instruments_generated.h)

// When in selection mode, the note you hear will always be in the Middle C octave.


#include <stdint.h>

// MIDI synth from midi-sound-library
#include "midisynth.h"

MidiSynth synth;


// Key bytes: 2 bits for each key, in key/group order:
// T0 = the low bit pair of all 11 groups
// T1 = the next bit pair of all 11 groups
// and so on. There are two unused bits at the top of each group.
unsigned char keys[24];

// Old key bytes for comparison.
unsigned char last_keys[24];

// Struct for each note on the keyboard.
typedef struct Note
{
  uint32_t  first_tick;   // Tick count (50us) when note first pressed
  bool      playing;      // The note is playing
} Note;

Note notes[88];

// Note offset of bottom note, to make middle C appear as note 60 per Midi
// specs.
int key_offset = 21;

// For instrument selection
static int inst = 0, curr_inst = 0;
static bool new_inst = false;

// Global most recent first_tick to use when a transition to a half-pressed
// key is missed
static uint32_t last_first_tick = 0;

// Timer to detect and enter selection mode
static bool selection_mode = false;
static uint32_t last_note_0_time = 0;

// The selection table is indexed by keyboard note (0-88) and maps
// to a synth instrument number. If an entry is -1 no new selection
// will be made.

// Octaves of the selection table
int selection[88] =
{
  // Unused notes at bottom of keyboard
  -1, -1, -1,

  // C1 octave: Organs
  16, 17, 18, 19, 20, 21, 22, 23, -1,-1, -1, -1,

  // C2 octave: Brass
  56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67,

  // C3 octave: Woodwinds
  68, 69, 70, 71, 72, 73, 74, 75, 76, 77, -1, -1,

  // C4  and C5 octave: Pianos and related keyoards
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
  12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1,   // Too much - try and get back an octave for strings

  // C6 octave: Strings
  24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,  // we've run out of space here

  // C7 octave: Special effects (voice ooh/ahh sounds)
  52, 53, 54, 55, -1, -1, -1, -1, -1, -1, -1, -1,

  // That final C8
  -1
};



// Scan the keyboard using 8 T-lines in turn. If the results differ from the
// last scan, return true.
bool scan(void)
{
  bool rc = false;
  int t, i = 0;

  for (t = 0; t < 8; t++)
  {
    PORTF = (unsigned char)(~(1 << t));

    // Delay by ~100us for clock settling in the chip and propagation delay in wiring.
    synth.delayByCount(2);

    keys[i++] = PINL;
    keys[i++] = PINC;
    keys[i++] = PINA;
  }

  // Check for any changes and remember them.
  for (i = 0; i < 24; i++)
  {
    if (keys[i] != last_keys[i])
      rc = true;
  }
  return rc;
}


// Run through the bit pairs of each key and process into a note
// and frequency. Only look for key bit pairs that have changed since
// the last scan.
void process_scan()
{
  for (int t = 0; t < 8; t++)
  {
    // Starting byte in key array of this T-line
    int start_byte = t * 3;

    // For each bit pair (SMx/PMx) within the T-line.
    // i.e. bits (bp, bp+1) of byte (bp / 8).
    for (int bp = 0; bp < 22; bp += 2)
    {
      int byte = start_byte + (bp >> 3);
      int shift = bp & 7;
      int bit_pair = (keys[byte] >> shift) & 0b11;
      int last_bit_pair = (last_keys[byte] >> shift) & 0b11;
      int note = (bp << 2) + t;

      // Check hold-down of bottom note if we want to use it for
      // instrument selection. Note 0 must be held for 1 second.
      if (last_note_0_time != 0 && synth.getDelayCount() - last_note_0_time > 20000)
      {
        if (!selection_mode)
          Serial.println("Selection mode");
        selection_mode = new_inst = true;

        // If in selection mode we are always playing a note from the
        // Middle C octave (note + key_offset = 60-71) and which
        // instrument is selected depends on the note played.
        // Note 0 must not be modified (it's tested for later)
        if (note > 0)
        {
          int key = (note + key_offset) % 12;
          int middle_c = 60 - key_offset;

          // Look up the instrument from the table. An entry of -1
          // will not play or select anything.
          if (selection[note] == -1)
            continue;
          inst = selection[note];

          // Modify the note to be in the Middle C octave.
          note = middle_c + key;
        }
      }

      // If no change then there is nothing more to do
      if (bit_pair == last_bit_pair)
        continue;

      // A key has been pressed and the PM line is low.
      // Remember the tick count both per note ad globally.
      // We have to handle repeated 01 -> 00 transitions
      // without going back to 11 (key fully released)
      if (bit_pair == 0b01)
        notes[note].first_tick = last_first_tick = synth.getDelayCount();

      // The key has gone all the way down.
      // This is where the note will be started.
      // In practice the travel times are from ~40-150ms
      // or 800-3000 ticks.

      // If somehow we have missed the PM line going down
      // (i.e straight from 11 to 00) then the first_tick
      // will be invalid and the last_first_tick is used instead.

      // The midi note is (note + key_offset) .

      if (bit_pair == 0b00)
      {
        uint32_t ticks;
        int vel;

        // But first, see if we have had a change of instrument,
        // or we are in selection mode
        if (new_inst && inst != curr_inst)
        {
          char name[80];

          Instruments::getInstrumentName(inst, name);
          Serial.print(inst);
          Serial.print(": ");
          Serial.println(name);
          synth.midiProgramChange(1, inst);
          curr_inst = inst;
          inst = 0;     // ready for next instrument change
          new_inst = false;
        }

        //Serial.print("Note ");
        //Serial.println(note + key_offset);

        // Time the transition from 01 to 00. If the 11-01 transition
        // was missed, use the last_first_tick as a backup.
        if (notes[note].first_tick != 0)
        {
          ticks = synth.getDelayCount() - notes[note].first_tick;
          //Serial.print("Ticks ");
        }
        else
        {
          ticks = synth.getDelayCount() - last_first_tick;
          //Serial.print("Ticks* ");
        }

        notes[note].playing = true;
        vel = 170 - (ticks >> 4);   // approximate mapping to 32-127
        if (vel < 32)
          vel = 32;
         else if (vel > 127)
          vel = 127;

        //Serial.print(ticks);
        //Serial.print(" vel ");
        //Serial.println(vel);
        synth.midiNoteOn(1, note + key_offset, (uint8_t)vel);

        // If Note 0 is currently playing, remember the first time it
        // was pressed (in case we are going into selection mode)
        if (!selection_mode && note == 0)
          last_note_0_time = synth.getDelayCount();
      }

      // The note has been released. Clear its first_tick counter.
      if (bit_pair == 0b11 && last_bit_pair != 0b11 && notes[note].playing)
      {
        notes[note].first_tick = 0;
        notes[note].playing = false;
        //Serial.print("Note released ");
        //Serial.println(note + key_offset);
        synth.midiNoteOff(1, note + key_offset);

        // Note 0 has been released; turn off selection mode.
        if (note == 0)
        {
          last_note_0_time = 0;
          selection_mode = new_inst = false;
          Serial.println("Selection mode exited");
        }
      }
    }
  }

  // Update last_keys array now that we have processed the changes
  for (int i = 0; i < 24; i++)
    last_keys[i] = keys[i];
}

void setup(void)
{
  // We are dumping, so set the serial port up.
  Serial.begin(9600);
  while (!Serial)
    ;

  // Use DDR register to set all bits of the port to OUTPUT.
  DDRF = 0xFF;    // set port F to all output

  // Set all bits of the ports to INPUT_PULLUP.
  PORTL = 0xFF;
  PORTC = 0xFF;
  PORTA = 0xFF;

  // Set up notes
  for (int i = 0; i < 88; i++)
  {
    notes[i].first_tick = 0;
    notes[i].playing = false;
  }

  // Set up last_keys so we don't get spurious releases at the start
  for (int i = 0; i < 24; i++)
    last_keys[i] = 0xFF;

  synth.begin();                          // Start synth sample/mixing on Timer2 ISR
  sei();                                  // Begin processing interrupts.
}

void loop(void)
{
  // Scan the keyboard and process any new notes.
  if (scan())
    process_scan();

  // Look on the serial monitor for changes of instrument.
  // Accept a 2 or 3 digit number here by building up digits.
  if (Serial.available())
  {
    int key = Serial.read();
    if (key >= '0' && key <= '9')
    {
      inst = (inst * 10) + (key - '0');
      new_inst = true;
    }
  }
}