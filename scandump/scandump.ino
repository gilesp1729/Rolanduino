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
  unsigned long first_ms;     // Millis count when note first pressed
  bool playing;               // The note is playing
} Note;

Note notes[88];

// Note offset of bottom note, to make middle C appear as note 60 per Midi
// specs.
int key_offset = 21;

// Scan the keybpard using 8 T-lines in turn. If the results differ from the
// last scan, return true.
bool scan(void)
{
  bool rc = false;
  int t, i = 0;

  for (t = 0; t < 8; t++)
  {
    PORTF = (unsigned char)(~(1 << t));

    // Delay a bit for clock settling in the chip and propagation delay in wiring.
    delayMicroseconds(100);

    keys[i++] = PINL;
    keys[i++] = PINC;
    keys[i++] = PINA;
  }

  // Check for any changes and remember them.
  for (i = 0; i < 24; i++)
  {
    if (keys[i] != last_keys[i])
    {
      rc = true;
      // last_keys[i] = keys[i]; // NOT HERE - wait till processed
    }
  }
  return rc;
}

// Dump the raw key bytes to serial.
void dump(void)
{
  for (int i = 0; i < 24; i++)
  {
    if (i % 3 == 0) Serial.print(" ");
    Serial.print(keys[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
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

      // If no change then there is nothing to do
      if (bit_pair == last_bit_pair)
        continue;

      // A key has been pressed and the PM line is low.
      // Remember the millis count
      if (bit_pair == 0b01)
        notes[note].first_ms = millis();

      // The key has gone all the way down.
      // The travel time is millis() - first_ms
      // This is where the note will be started.
      // In practice the travel times are from 100-300ms.

      // If somehow we have missed the PM line going down
      // (i.e straight from 11 to 00) then the first_ms
      // will be invalid. Handle this somehow.

      // The midi note is (note + key_offset) and its key velocity
      // is 1-127 mapped from (approx) 500-100ms.
      if (bit_pair == 0b00)
      {
        Serial.print("Note ");
        Serial.print(note + key_offset);
        Serial.print(" ms ");
        Serial.print(notes[note].first_ms);
        Serial.print(" ");
        Serial.println(millis() - notes[note].first_ms);
        notes[note].playing = true;
      }

      // The note has been released. Clear its first_ms counter.
      if (bit_pair == 0b11 && last_bit_pair != 0b11 && notes[note].playing)
      {
        notes[note].first_ms = 0;
        notes[note].playing = false;
        Serial.print("Note released ");
        Serial.println(note + key_offset);
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
    notes[i].first_ms = 0;
    notes[i].playing = false;
    // also do frequency here unless synth can do it for me
  }

  // Set up last_keys so we don't get spurious releases at the start
  for (int i = 0; i < 24; i++)
    last_keys[i] = 0xFF;
}

void loop(void)
{
  if (scan())
  {
    //dump();
    process_scan();
  }
}