
// Copyright (c) 2026, Rich Heslip
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
/*
 R Heslip Drum Machine app for 4HPico eurack module March 2026
 Plays standard MIDI file drum patterns.

Notes:

The sketch is set up for standard MIDI files with GM mapped drums. It will work with non-GM mapping but it might be a little harder to map sample sounds to the individual drum tracks.

It must have an external clock running at 16 clocks per bar. MIDI patterns with odd time signatures will probably not work correctly. External reset can be used to sync the MIDI loop playback to a sequencer etc.
No attempt is made to handle files with "negative SMPTE format" timing - these seem to be pretty rare in practice.

You MUST compile this sketch with space allocated for a file system in the /Tools/Flash Size Arduino menu. I suggest 128-256k because all the MIDI files get loaded into RAM and the Pico 2 only has 520kb RAM.

To load MIDI files, hold the encoder button while powering up. The display will indicate "USB MODE". Connect the Pico's USB port to a host computer - it will appear as a flash drive. 

If this is the first time you have connected you should format the drive. You should only have to format the drive once - even if you change the code the flash drive should still be intact.

You MUST create four bank directories named "Bank1", "Bank2", "Bank3" and "Bank4". Drag and drop your MIDI files into these bank folders. There is a maximum of 32 MIDI files per bank. Any more than 32 files per bank will be ignored.
**IMPORTANT** eject the drive when finished loading files to ensure the file system does not get corrupted. Power the module off and back on for normal operation.

Drum sounds:

This one works the same way as the Grids sketch - put your drum samples in a subfolder of the sketch and run the wav2header22khz utility which creates a header file which you include in the sketch.
Wave2header22khz sorts the samples in alphabetical order. Its very helpful to have a silent sample and make sure its the first sample e.g. "00silence.wav". This allows tracks to be muted by turning the sample knob fully ccw.
You should also prefix the filenames with the GM note # of that sound e.g. "36Bassdrum.wav". The sketch will auto assign voices by mapping the GM note in the MIDI file to a sample with the same GM note number. 
You can have saveral samples with the same note # - it will assign the alphabetically first sample. Voices will always be assigned in ascending GM note order i.e. If there is a bass in the MIDI file it will always be on POT3 of the first page.
If none of your sample names match the GM notes in the MIDI file it will map the GM note to the first sample - which will be silence if you follow the suggested naming prefix. 
You can assign voices manually by using the menus. 

You need the updated version of wav2header22khz in this archive /resources which now supports assigning MIDI note number from the filename prefix. 

 top jack - clock input -
 left middle jack - clock input, advances on +ve edge. input clock must be at 1/16 note rate ie 16 clocks per bar for 4/4 time signature.
 right middle jack - reset input to sync with other sequencers - resets on +ve level
 bottom jacks - L & R audio out

 button - click to advance to next page
 - hold to manually reset to first step
 - hold when powering up to enter flash drive mode

Menus - rotate the encoder to scroll through menu items. Click to select - the menu will be underlined when selected
  Bank1-Bank4 - click to scroll thru the MIDI patterns in that bank, click to return to menu
  Sample 1- Sample10 - click to scroll thru sounds, click to return to menu
  Pan 1-10, Level 1-10 - set pan and level of the sample with the same number

Screen saver - the screen will go blank after 60 seconds of inactivity to avoid buring in the OLED. you can change this time in the #defines below

*/

#include "4HPico.h"
#include <I2S.h>
#include <math.h>
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Monospaced_12.h"
#include "Clickencoder.h"
#include "pico/multicore.h"
//#include "LittleFS.h"
#include <FatFS.h>
#include <FatFSUSB.h>
#include "midi-parser.h"

#ifndef _BV
#define _BV(bit) (1 << (bit)) 
#endif

#define MONITOR_CPU1  // define to enable 2nd core monitoring
//#define DEBUG   // comment out to remove debug code
//#define dumpMIDI  // define to dump MIDI file info 

//#define SAMPLERATE 11025 
#define SAMPLERATE 22050  // saves CPU cycles
//#define SAMPLERATE 44100

I2S DAC(OUTPUT);  // I2C output mode - use 16 bits for PCM5102. PCM1808 not used in this sketch

// constants for integer to float and float to integer conversion
#define MULT_16 2147483647
#define DIV_16 4.6566129e-10

#define CLOCKIN AIN0  // left middle jack is clock  
#define RESETIN AIN1   // right middle jack resets the sequencer  

#define DEBOUNCE_CYCLES 100 // counter to debounce encoder button release
#define CLKDELAYCOUNT 5
bool playing=0;
bool clocked=0;
bool resetedge=0;
bool loopsync=1;  // flags end of file and resync to clock needed
uint16_t clkdelay=CLKDELAYCOUNT;  // wait this many clocks after reset or stopped clock to measure clock again
uint32_t clockcounter;  // counts external clocks for syncing
uint32_t lastclock,edgetime; // clock related timers that use micros()
uint32_t clockperiod=125000; // start up at 120bpm
int16_t totalnotes=0; // number of note on events in the current file
int16_t notes=0; // note counter used in interrupt
//
int32_t clocktimer,displaytimer; // various timer counts

ClickEncoder menuenc(ENCA_IN,ENCB_IN,ENCSW_IN,ENCDIVIDE); // menu encoder object

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

enum UISTATES {RUN,DORMANT,WAIT_BUTTON_RELEASE};
int16_t UI_state=RUN; // initial UI state

int16_t ppqn=96;  // this changes - depends on the MIDI file

#define NUM_VOICES 10  // 
struct voice_t {
  int16_t sample;   // 
  uint32_t sampleindex; // current sample array index when playing. index at last sample = not playing  
  uint8_t velocity; //  0-127  
  int16_t level;   // mixer level
  int16_t pan;     // mixer pan
} voice[NUM_VOICES]; 

// we can have an arbitrary number of samples but you will run out of memory (or CPU) at some point
// sound sample files must be 22khz 16 bit signed PCM format - see the sample include files for examples
// sample files are compiled into arrays and stored in program flash
// if your sketch is too big you may have to fiddle with the flash partition scheme to enable more of the flash to be used for program space

// the header files can be auto generated by the wav2header utility
// put your 22khz or 44khz PCM wav files in a sample directory, run the utility and it will generate all the header files

// include only ONE sample header file
//#include "808samples/samples.h" // 808 sounds
//#include "Angular_Jungle_Set/samples.h"   // Jungle soundfont set - great!
//#include "Angular_Techno_Set/samples.h"   // Techno
//#include "Acoustic3/samples.h"   // acoustic drums 
//#include "Pico_kit/samples.h"   // assorted samples
//#include "testkit/samples.h"   // assorted samples
//#include "EDM_kits/samples.h"   // Techno, Pop, Trap, House
#include "EDM/samples.h"   // Techno, Pop, Trap, House
//#include "House/samples.h"   // House 808 909 style kit
//#include "Techno/samples.h"   // Techno

#define NUM_SAMPLES (sizeof(sample)/sizeof(sample_t))
#define NUM_BANKS 4
#define MAX_FILES_PER_BANK 32
#define MAX_FILENAME_LENGTH 8 // filename plus a nul terminator

// set the voice sample and update the voice info from the sample info
// a bit redundant but I don't want to change the sample structure - been using this same one for years
void setvoicesample(uint16_t v,uint16_t s) {
  voice[v].sample=s;
  rp2040.idleOtherCore();  // stop other core while we write sampleindex
  voice[v].sampleindex=sample[s].samplesize;
  rp2040.resumeOtherCore();
}

// initialize the voice structures
void init_voices(void){
  for (uint16_t i=0; i< NUM_VOICES;++i) {
    if (i==0) setvoicesample(i,1);  // set the first voice so it will play something
    else setvoicesample(i,0);   // rest are sample 0 which should be silence
    voice[i].velocity=0;
    voice[i].level=500;
    voice[i].pan=0;
  }
}

int16_t currentbank,currentfile,lastbank,lastfile;

struct bankinfo {
  uint8_t * buffer[MAX_FILES_PER_BANK];
  int32_t buffersize[MAX_FILES_PER_BANK];
  int16_t numfiles;
} patternbank[NUM_BANKS];

char * filenames[NUM_BANKS][MAX_FILES_PER_BANK]; // stores filenames of all the files

// load the files in a bank from a directory in the file system
int16_t readbank(int16_t bank, String dirName) {
  Dir dir = FatFS.openDir(dirName);
  patternbank[bank].numfiles=0;
  while (true) {
    if (!dir.next()) {
      // no more files
      break;
    }
   // Serial.print(dir.fileName());
    File MIDIfile;
    if(dir.fileSize()) {
      MIDIfile= dir.openFile("r");
     // Serial.print(MIDIfile.size());
     // Serial.print(" ");
      patternbank[bank].buffer[patternbank[bank].numfiles]=0; // null pointer means no file content 
      char temp[128]; // hopefully nobody uses filenames longer than this
      strcpy(temp,dir.fileName().c_str());  // get the filename as char
      temp[MAX_FILENAME_LENGTH]=0; // chop it off at 8 letters
      filenames[bank][patternbank[bank].numfiles]=(char *) malloc(MAX_FILENAME_LENGTH+1);
      strcpy(filenames[bank][patternbank[bank].numfiles] ,temp);
   //   Serial.printf("%s\n",filenames[bank][patternbank[bank].numfiles]);
      uint32_t index=0;

      if (patternbank[bank].buffer[patternbank[bank].numfiles]=(uint8_t *)malloc(MIDIfile.size()*sizeof(uint8_t))) { // allocate RAM to store the file
        while (MIDIfile.available()) patternbank[bank].buffer[patternbank[bank].numfiles][index++]=MIDIfile.read();
      //  Serial.printf(" Read %d bytes\n",index);
        patternbank[bank].buffersize[patternbank[bank].numfiles]=dir.fileSize();
        ++patternbank[bank].numfiles;
      }
      else {
        Serial.printf("memory allocation error\n");
        return -1; // error
      }
    }
    else {
      Serial.printf("error reading file %s\n",dir.fileName());
      return -1; // error
    }
  } 
  return patternbank[bank].numfiles;
}

void printDirectory(String dirName, int numTabs) {
  Dir dir = FatFS.openDir(dirName);
  while (true) {
    if (!dir.next()) {
      // no more files
      break;
    }
    for (int i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(dir.fileName());
    if (dir.isDirectory()) {
      Serial.println("/");
      printDirectory(dirName + "/" + dir.fileName(), numTabs + 1);
    } else {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.print(dir.fileSize(), DEC);
      time_t cr = dir.fileCreationTime();
      struct tm tmstruct;
      localtime_r(&cr, &tmstruct);
      Serial.printf("\t%d-%02d-%02d %02d:%02d:%02d\n", (tmstruct.tm_year) + 1900, (tmstruct.tm_mon) + 1, tmstruct.tm_mday, tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);
    }
  }
}

// midi parser stuff

#define MAXNOTES 2000
struct noteinfo {
  uint8_t note;
  uint8_t velocity;
  int64_t time;
} event[MAXNOTES];

uint8_t instrument[NUM_VOICES]; // maps MIDI note # to a voice
int8_t instrumentcount=0;

struct midi_parser parser;
enum midi_parser_status status;

void resetmidiparser(uint8_t * buffer, int32_t size) {
  parser.state = MIDI_PARSER_INIT;
  parser.size  = size;
  parser.in    = (const uint8_t*)buffer;
}

// Comparison function for instruments (uint_8) 
int uint8_compare(const void *a, const void *b) {
    uint8_t uint_a = *((uint8_t*)a);
    uint8_t uint_b = *((uint8_t*)b);

    if (uint_a < uint_b) return -1;
    if (uint_a > uint_b) return 1;
    return 0;
}

// find the MIDI instruments (GM notes) used in a MIDI file in memory
// save the note on events for processing
void scaninstruments(uint8_t * buffer, int32_t size) {
  bool voiceassigned;
  enum midi_parser_status status;
  int16_t notes,instruments;
  int64_t acctime=0;
  int64_t runningtime=0;
  resetmidiparser(buffer,size);
  instrumentcount=totalnotes=0;  // reset values so note interrupt handler doesn't use intermediate calculations
  for(int8_t i=0; i< NUM_VOICES; ++i) instrument[i]=255; // clear instrument assignments
  instruments=notes=0;    
  while (1) {  // scan the MIDI file to find GM instruments used
    status = midi_parse(&parser);
    if (status==MIDI_PARSER_EOB) break;
    if (status==MIDI_PARSER_HEADER) ppqn=parser.header.time_division;
    if (status==MIDI_PARSER_TRACK_MIDI) {
      acctime+=parser.vtime; // accumulate time for all events
      runningtime+=parser.vtime;
    }
    if ((status==MIDI_PARSER_TRACK_MIDI) && (parser.midi.status==9) && (parser.midi.param1 > 23) && (parser.midi.param1 <84)) { // note on event - filter out any trash notes
      event[notes].note=parser.midi.param1; // save note
      event[notes].velocity=parser.midi.param2; // save velocity
      event[notes].time=acctime; // save time of the next event
 //     Serial.printf("note on event %d  time=%ld runningtime=%ld\n",notes,(int32_t)acctime,(int32_t)runningtime);
      acctime=0;
      ++notes;
      voiceassigned=0;
      int16_t i;
      for (i=0; i< NUM_VOICES;++i) { // check if this instrument is assigned
        if (parser.midi.param1==instrument[i]) { // 
          voiceassigned=1;
          break;
        }
        if (instrument[i]==255) break;
      }
      if (!voiceassigned && (instruments < NUM_VOICES)) {
        instrument[i]=parser.midi.param1; // assign the instrument 
        ++instruments;
      }      
    }
  }
  qsort(instrument, sizeof(instrument)/sizeof(uint8_t), sizeof(uint8_t), uint8_compare); // sort instrument assignments into ascending order
  instrumentcount=instruments; // update the globals 
  totalnotes=notes;  // update the value read by the note interrupt
}

// reset the parser, scan the MIDI file for the new instrument set, and reset again so its ready to start from the beginning
// do this every time bank or pattern is changed
void resetandscan(uint8_t * buffer, int32_t size) {
  resetmidiparser(patternbank[currentbank].buffer[currentfile],patternbank[currentbank].buffersize[currentfile]); // set the parser to the current bank and file
  scaninstruments(patternbank[currentbank].buffer[currentfile],patternbank[currentbank].buffersize[currentfile]); // find instruments used in the MIDI file
}

// map samples to voices using their MIDI note number
void mapGMsamples (void){
  bool found;
  for (int8_t i=0; i<instrumentcount;++i) {
    found=0;
    for (int8_t j=0; j< NUM_SAMPLES;++j) {
      if (instrument[i]==sample[j].MIDINOTE) {
        voice[i].sample=j;
        found=1;
        break;
      }
    }
    if (!found) voice[i].sample=0; // if we can't find a sample with the same GM note# use sample 0 which should be silence
  }
}

// set up a new MIDI file for playing
void setupnewpattern(void) {
  noInterrupts();  // we might have a note interrupt pending
  if (patternbank[currentbank].numfiles>0) {
  resetandscan(patternbank[currentbank].buffer[currentfile],patternbank[currentbank].buffersize[currentfile]); // parse the new file to find what instruments are used
  mapGMsamples();
  }
  else { // this bank is empty so don't attempt to play it
    totalnotes=0;
  }

  clockcounter=playing=loopsync=notes=0; // stop playing, sync to clock
  clkdelay=CLKDELAYCOUNT;
//  note_alarm_in_us((uint32_t)(event[0].time));  // schedule first note interrupt  
  interrupts();
}

// menu variables - these must be int16
int16_t bank0file,bank1file,bank2file,bank3file;

// menu callbacks

// menu callback to set bank and file
void setbankfile(int16_t file, int16_t bank){
  currentbank=bank;   // bank is hard coded in menus so it will always be valid
  if (file >= patternbank[bank].numfiles) currentfile=patternbank[bank].numfiles-1; // make sure file number is valid
  else currentfile=file;
  setupnewpattern();
}

// menu callback to set sample on a voice
void updatesample(int16_t s,int16_t v) {
  setvoicesample(v,s);
}


#include "menusystem.h"  // has to come after display and encoder objects creation, menu callbacks

// **** interrupt code *****
// external clock input is measured and processed by an edge interrupt
// encoder is handled by a periodic alarm interrupt
// note on messages are processed by an alarm interrupt for precise timing

// clock interrupt handler
void clockhandler(void) {  // hardware inverts input so interrupt is rising edge of external clock
  edgetime=micros();
  if (clkdelay>0) --clkdelay; // delay clock measurement after clock is stopped to avoid bad measurements
  // ideally clock period should be averaged over time but the measurement is pretty accurate with an interrupt
  if (((edgetime - lastclock) < 1000000) && (clkdelay==0)) clockperiod=edgetime-lastclock; // handles case of stopped clock which would otherwise result in a very long clockperiod. 1000000 us ~ 15 BPM minimum
  lastclock=edgetime; 
  playing=1;   // starts play again if clock was stopped
  if ((clockcounter%16)==0) {
    if (!loopsync) {
      loopsync=1; // sync file playing to 16 clock bar rate
      note_alarm_in_us((uint32_t)(event[0].time+100));  // schedule first note interrupt with a small offset - Pico alarm timer isn't reliable if set to 0
      delayMicroseconds(100); // *** not sure why this is needed but startup after clock stop+reset is not reliable without it
    }
  }
  ++clockcounter;
}

// RP2040 timer code from https://github.com/raspberrypi/pico-examples/blob/master/timer/timer_lowlevel/timer_lowlevel.c
// Use alarm 0 for encoder timer interrupt
// use alarm 1 for note interrupt

#define ALARM_ENCODER 0
#define ALARM_ENCODER_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_ENCODER)
#define ALARM_NOTE 1
#define ALARM_NOTE_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NOTE)

static void encoder_alarm_in_us(uint32_t delay_us) {
  hw_set_bits(&timer_hw->inte, 1u << ALARM_ENCODER);
  irq_set_exclusive_handler(ALARM_ENCODER_IRQ, encoder_irq);
  irq_set_enabled(ALARM_ENCODER_IRQ, true);
  encoder_alarm_in_us_arm(delay_us);
}

static void encoder_alarm_in_us_arm(uint32_t delay_us) {
  uint64_t target = timer_hw->timerawl + delay_us;
  timer_hw->alarm[ALARM_ENCODER] = (uint32_t) target;
}

// encoder IRQ handler
static void encoder_irq(void) {
  menuenc.service(); // handle the menu encoder
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_ENCODER); // clear IRQ flag
  encoder_alarm_in_us_arm(TIMER_MICROS);  // reschedule interrupt
}

// note alarm interrupt code 

static void note_alarm_in_us(uint32_t delay_us) {
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NOTE);
  irq_set_exclusive_handler(ALARM_NOTE_IRQ, note_irq);
  irq_set_enabled(ALARM_NOTE_IRQ, true);
  note_alarm_in_us_arm(delay_us);
}

static void note_alarm_in_us_arm(uint32_t delay_us) {
  uint64_t target = timer_hw->timerawl + delay_us;
  timer_hw->alarm[ALARM_NOTE] = (uint32_t) target;
}

//  note IRQ handler
//  note on events are scheduled with the Pico alarm interrupt
// interprocessor FIFO is used to send MIDI note on messages to 2nd core which handles the audio processing
static void note_irq(void) {
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_NOTE); // clear pending IRQ flag
  if ((totalnotes==0) || (!playing)) return; // do not play if pattern is empty or not in playing mode
//  Serial.printf("event %d %u clkper=%d \n",notes,(uint32_t)event[notes].time,clockperiod);
  for (int16_t i=0; i< NUM_VOICES;++i) { // check if this GM percussion instrument is assigned
    if (event[notes].note==instrument[i]) {
      rp2040.fifo.push(((0x90 | i)<<24) | (event[notes].velocity <<16));  // tell other core to play this voice - this could block if FIFO is full
      break;
    }
  } 
  ++notes;
  // process any other note on events with this event time
  // best done here because the PICO alarm interrupt is flakey if set to 0
  while (event[notes].time==0 && (notes<totalnotes)) {
    for (int16_t i=0; i< NUM_VOICES;++i) { // check if this GM percussion instrument is assigned
      if (event[notes].note==instrument[i]) {
        rp2040.fifo.push(((0x90 | i)<<24) | (event[notes].velocity <<16));  // tell other core to play this voice - this could block if FIFO is full
        break;
      }
    } 
    ++notes;    
  }

  if (notes>=totalnotes) {  // last note was just played - wait for clock interrupt to sync and restart MIDI event playback
    notes=0; // reset play index 
    noInterrupts();
    loopsync=0; // sync to clock
    interrupts(); 
  }
  else {   // schedule next note on event interrupt
    uint32_t nextevent=(uint32_t)event[notes].time*clockperiod*4/ppqn;   // this expression converts MIDI file ticks to time in us. 16 clocks per bar is assumed
    note_alarm_in_us_arm(nextevent); 
  }
}

// error condition - loops forever
void fatalerror(const char * errorstring){
  Serial.printf("%s\n",errorstring);
  display.clearDisplay();
  display.setCursor(0,0);
  display.printf("%s",errorstring);
  updatedisplay();
  while (1); 
}

void setup() { 
  Serial.begin(115200);
//  while(!Serial) ;

// init IO ports
  pinMode(ENCA_IN, INPUT_PULLUP);  // menu encoder and switch
  pinMode(ENCB_IN, INPUT_PULLUP);    
  pinMode(ENCSW_IN, INPUT_PULLUP);
  pinMode(CLOCKIN, INPUT_PULLUP); // gate/trigger in used for clock
  pinMode(RESETIN, INPUT_PULLUP); // 2nd jack in used for reset 
#ifdef MONITOR_CPU1 // for monitoring 2nd core CPU usage
  pinMode(CPU_USE,OUTPUT); // hi = CPU busy
#endif 


// set up I2C pins 
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();

  analogReadResolution(AD_BITS); // set up for max resolution

    // set up OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while(1); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setFont(&Monospaced_bold_12); // 8x10 pixel font is more readable than the stock 5x7
  display.setTextSize(1);
	display.setTextColor(WHITE,BLACK); // foreground, background  
  display.setCursor(0,10);
  display.printf(" 4HPico\nSMFDrums"); // power on message
  display.display();
  delay(2000);
  displaytimer=millis(); // reset display blanking timer

// something strange going on here -FatFS.begin() crashes if the FS has not been set up already
// it should report a failure, not crash

  Serial.printf("starting FS\n");
  if (!FatFS.begin()) {
    Serial.print("Can't mount FS"); // start up filesystem
    fatalerror("No FS\nFound");
  }
  else Serial.print("mounted FS\n");

// also weird - FatFSUSB will fail on file modification activity unless its started without I2S
// no idea why but its flakey if I2S is running

  if(!digitalRead(ENCSW_IN)) { // button held on powerup, start in USB mode so files can be managed from host
    Serial.printf("starting FatFSUSB\n ");
    display.clearDisplay();
    display.setCursor(0,10);
    display.printf("USB MODE"); // show mode
    updatedisplay();
    FatFSUSB.begin(); 
    delay(2000); // TinyUSB seems to have a race condition, see https://github.com/hathach/tinyusb/discussions/1764
    Serial.printf("FatFSUSB started.\n");
    Serial.printf("Connect drive via USB to upload/erase files\n");
    while(1); // loop forever - USB files handled in background 
  }  
  else {
    printDirectory("/", 0); // do a directory dump
  }

  // read MIDI files from flash file system
  // *** if you want to customize the bank names change them here and in menusystem.h ***
  currentbank=currentfile=0;
  if (readbank(0, "/Bank1") < 0) fatalerror("can't read bank 1");
  if (readbank(1, "/Bank2") < 0) fatalerror("can't read bank 2");
  if (readbank(2, "/Bank3") < 0) fatalerror("can't read bank 3");
  if (readbank(3, "/Bank4") < 0) fatalerror("can't read bank 4");

  drawmenu(0); // show first menu item
  clocktimer=millis(); // initial clock measurement

  init_voices();  // initialize the voices

  setbankfile(currentfile, currentbank);  // set up the first pattern

// set up I2S for PCM5102 stereo DAC in 16 bit mode - input not needed
	DAC.setBCLK(BCLK);
	DAC.setDATA(I2S_DATA);
	DAC.setBitsPerSample(16);
	DAC.setBuffers(1, 128, 0); // DMA buffer - 32 bit L/R words
	//DAC.setLSBJFormat();  // needed for PT8211 which has funny timing
	DAC.begin(SAMPLERATE);

    // set up timer interrupt to handle encoder
  encoder_alarm_in_us(TIMER_MICROS);

 // set up clock interupt handler
  attachInterrupt(digitalPinToInterrupt(CLOCKIN), clockhandler, FALLING); // actually rising edge of clock - opamp inverts it
}


void loop() {
  static int16_t debouncecounter;
  int16_t encvalue;

  ClickEncoder::Button b=menuenc.getButton();
    switch (b) {
    case ClickButton::Clicked:
      break;
    case ClickButton::DoubleClicked:
      mapGMsamples();
      break;
    case ClickButton::Held:  // hold to reset sequencer
      break;
    default:
      break;
  }

  if ((millis()-displaytimer) > DISPLAY_BLANK_MS) {
    UI_state=DORMANT;  // 
    blankdisplay(); // protect the OLED from burnin
  } 

// this state machine handles display blanking and restore of display when encoder activity is detected
  switch (UI_state) {
    case RUN:
      domenus();  // call the menu state machine
      break;    
    case DORMANT:  // using menu encoder will start screen up again
      encvalue=menuenc.getValue();
      if (encvalue || !digitalRead(ENCSW_IN)) {
        updatedisplay(); // restore the display
        debouncecounter=DEBOUNCE_CYCLES;
        UI_state=WAIT_BUTTON_RELEASE;
      }
      break; 
    case WAIT_BUTTON_RELEASE:  // intermediate state - wait for button release so it doesn't mess up menus
      if (digitalRead(ENCSW_IN)) {
        -- debouncecounter;
        if (debouncecounter==0) UI_state=RUN;  // if button released move to run state
      }
      break;
    default:
      UI_state=RUN;
      break;
  }



/*
// edge triggered reset
  if (!digitalRead(RESETIN))  { // look for reset - reset input is inverted
    if (((millis()-resetdebouncetimer) > CLOCK_DEBOUNCE) && !resetedge) {  // true on rising edge
      resetedge=1;
      resetmidiparser(patternbank[currentbank].buffer[currentfile],patternbank[currentbank].buffersize[currentfile]); // reset the parser
      playing=loopsynced=0; // stop playing, sync to clock
      nextMIDIevent=micros();  // fake event to start the parser loop again
    }
  }
  else {
    resetedge=0;
    resetdebouncetimer=millis();
  }
*/

// reset when input is high
  if (!digitalRead(RESETIN))  { // look for reset - reset input is inverted
    noInterrupts();
    clockcounter=playing=loopsync=notes=0; // stop playing, sync to clock
    clkdelay=CLKDELAYCOUNT;
    interrupts();
  }

  noInterrupts();  // edgetime is set in the clock interrupt - make sure it doesn't change on us while we calculate the time since last clock
  uint32_t diff=micros()-edgetime;
  interrupts();

  if (((diff) > (clockperiod*3)/2) && playing) { // if last lock edge is 150% of the last measured clock period clock must be stopped
    noInterrupts();
    playing=0; // clock not running, stop playing
    interrupts();
  }

} // end loop()


// second core setup
// second core is dedicated to sample processing
void setup1() {
delay (1000); // wait for main core to start up peripherals
}

// process audio samples
void loop1(){
  int32_t samp;
  int32_t samplesumL=0;
  int32_t samplesumR=0;
  int32_t command,left,right;
  uint8_t message,velocity,track;

  // sample loop is set up so it never blocks so we can empty FIFO quickly
  // we don't want the other core blocking due to full FIFO - that would add a lot of latency to the note interrupt

// get note on messages from core1 thru the FIFO. 
// if both cores write the same variable (sampleindex in this case) it will mess up now and then so communicate MIDI messages with the interprocessor FIFO
// we don't care about note offs - just let the sample play thru
  while (rp2040.fifo.available()) { // get MIDI command, channel# = voice#
    command=rp2040.fifo.pop(); //
    track=(command>>24) & 0xf; 
    message= (command>>24) & 0xf0;  
    velocity=(command >>16) & 0x7f;

    switch (message) {
      case 0x90: // note on
        voice[track].velocity=velocity; // set velocity
        voice[track].sampleindex=0; // trigger sample for this track
        break;
      case 0x80: // note off
        break;
      default:
        break;
    }       
  }

// process next sample if there is room in the I2S output buffer
  if (DAC.availableForWrite() >=4) {  
    samp=0;
    for (int i=0; i< NUM_VOICES;++i) {  // look for voices that are playing, scale their volume, and add them up
      int s=voice[i].sample;
      if (voice[i].sampleindex < sample[s].samplesize) {  // if voice is playing, grab the next sample
        samp=(int32_t)(sample[s].samplearray[voice[i].sampleindex++]*voice[i].velocity);  // thats a mouthful!
      }
      samp=(samp>>7)*voice[i].level/1000;  // do mix level and adjust for play_volume multiply above
      samplesumL+=samp*map(voice[i].pan,-1000,1000,1000,0)/1000; // do L-R panning
      samplesumR+=samp*map(voice[i].pan,-1000,1000,0,1000)/1000;
    }

  //  if  (samplesum>32767) samplesum=32767; // clip if sample sum is too large
  //  if  (samplesum<-32767) samplesum=-32767;

    DAC.write((int16_t)(samplesumL)); // left
    DAC.write((int16_t)(samplesumR)); // right
  }

}




