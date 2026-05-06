/* Copyright Rich Heslip 2026
//
// Plaits Library Copyright Emilie Gillete and Mark Washeim
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

 ** Mutable Instrument Braids for 4HPico **

This version uses the ArduinoMI library for Braids and DaisySP for the ADSR

 R Heslip April  2026

Runs very well at 150Mhz - lots of CPU left for more signal processing

top left jack - Timbre input 
top right jack - Color input 
middle left jack - V/Octave CV input
middle  right jack - gate input
bottom jacks - left and right audio out

Menu parameters 
Model - Braids model - there are 45 synthesis models 

Timbre - Braids Timbre setting, added to Timbre CV input

Color - Braids Timbre setting, added to Color CV input

Pitch - sets pitch by MIDI note number ie 60=Middle C

FineFreq - adjusts pitch +-1 semitone

Attack - ADSR attack 0-100 - exponential range approx. 0-10 seconds

Decay - ADSR decay 0-100 - exponential range approx. 0-10 seconds

Sustain - - ADSR sustain level 0-100 

Release - ADSR release 0-100 - exponential range approx. 0-10 seconds

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
// plaits dsp
#include <STMLIB.h>
#include <BRAIDS.h>

#ifndef _BV
#define _BV(bit) (1 << (bit)) 
#endif

#define MONITOR_CPU1  // define to enable 2nd core monitoring
//#define DEBUG   // comment out to remove debug code


#define SAMPLERATE 48000
#define GATE TRIGGER    // semantics - ADSR is generally used with a gate signal

uint32_t gatetimer;  // timers

bool gate;

#define GATE_DELAY 5  // gate debounce time + delay to allow CV to settle

I2S i2s(INPUT_PULLUP); // both input and output

#include "daisysp.h"
// including the source files is a pain but that way you compile in only the modules you need
// DaisySP statically allocates memory and some modules e.g. reverb use a lot of ram
#include "control/adsr.cpp"
Adsr      env;

enum UISTATES {RUN,DORMANT,WAIT_BUTTON_RELEASE};
int16_t UI_state=RUN; // initial UI state

#define DEBOUNCE_CYCLES 100 // counter to debounce button release

int32_t displaytimer ; // display blanking timer

ClickEncoder menuenc(ENCA_IN,ENCB_IN,ENCSW_IN,ENCDIVIDE); // menu encoder object

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define CVIN_VOLT 580.6  // a/d count per volt - **** adjust this value to calibrate V/octave input

// braids library stuff
#define     BLOCK_SIZE          32      // --> macro_oscillator.h !

struct Unit {
  braids::MacroOscillator *osc;
  braids::Quantizer   *quantizer;
  braids::SignatureWaveshaper *ws;
  int16_t     buffer[BLOCK_SIZE];
  uint8_t     sync_buffer[BLOCK_SIZE];
} voices[1];

// initialize voice parameters
void initVoices() {
  voices[0].osc = new braids::MacroOscillator;
  voices[0].osc->Init(SAMPLERATE);
  voices[0].osc->set_pitch((48 << 7));
  voices[0].osc->set_shape((braids::MacroOscillatorShape)0);
  voices[0].ws = new braids::SignatureWaveshaper;
  voices[0].ws->Init(123774);   // RH where did this magic number come from?
  voices[0].quantizer = new braids::Quantizer;
  voices[0].quantizer->Init();
  voices[0].quantizer->Configure(braids::scales[0]);
  memset(voices[0].buffer, 0, sizeof(int16_t)*BLOCK_SIZE);
  memset(voices[0].sync_buffer, 0, sizeof(voices[0].sync_buffer));
}

// timer routines
static void alarm_in_us(uint32_t delay_us) {
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
  irq_set_enabled(ALARM_IRQ, true);
  alarm_in_us_arm(delay_us);
}

static void alarm_in_us_arm(uint32_t delay_us) {
  uint64_t target = timer_hw->timerawl + delay_us;
  timer_hw->alarm[ALARM_NUM] = (uint32_t) target;
}

// timer interrupt handler
//handles the menu encoder
static void alarm_irq(void) {
  menuenc.service(); // handle the menu encoder
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM); // clear IRQ flag
  alarm_in_us_arm(TIMER_MICROS);  // reschedule interrupt
}

// menu parameters are always 16 bit ints to simplify the menu system
// they must be converted to other types as needed in the sketch
int16_t shape=0;
int16_t timbre=0;  // set initial values
int16_t color=0;
int16_t pitch=60;
int16_t finefreq=0;
int16_t attack=10;
int16_t decay=10;
int16_t sustain=50;
int16_t release=10;

// menu callback functions
void setshape(void) {
  voices[0].osc->set_shape((braids::MacroOscillatorShape)shape);
}

void settimbre_color(void){
  voices[0].osc->set_parameters(map(timbre,0,1000,0,32767),map(color,0,1000,0,32767));
}

void setattack(void) {
  env.SetTime(ADSR_SEG_ATTACK, pow(mapf(attack,0,100,0,2.1),3)); // up to 10 seconds per segment, exponential pot response
}

void setdecay(void){ 
  env.SetTime(ADSR_SEG_DECAY, pow(mapf(decay,0,100,0,2.1),3));
}

void setsustain(void) {
  env.SetSustainLevel(mapf(sustain,0,100,0,1));
}

void setrelease(void) {
  env.SetTime(ADSR_SEG_RELEASE, pow(mapf(release,0,100,0,2.1),3)); 
}

#include "menusystem.h"  // has to come after display and encoder objects creation, menu callbacks

void setup() {
  Serial.begin(115200);

// init IO ports
  pinMode(ENCA_IN, INPUT_PULLUP);  // menu encoder and switch
  pinMode(ENCB_IN, INPUT_PULLUP);    
  pinMode(ENCSW_IN, INPUT_PULLUP); 
#ifdef MONITOR_CPU1 // for monitoring 2nd core CPU usage
  pinMode(CPU_USE,OUTPUT); // hi = CPU busy
#endif 

// set up I2C pins 
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();

  analogReadResolution(AD_BITS); // set up for max resolution

// init the DaisySP ADSR 
  env.Init(SAMPLERATE);
  setattack();
  setdecay();
  setsustain();
  setrelease();

  // init the braids voices
  initVoices();

// set up I2S for 32 bits in and out
// PCM1808 is 24 bit only but I could not get 24 bit I2S working. 32 bits is little if any extra overhead
  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK); // Note: LRCLK = BCLK + 1
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();

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
  display.printf(" 4HPico\n Braids"); // power on message
  display.display();
  delay(3000);
  displaytimer=millis(); // reset display blanking timer
  drawmenu(0); // show first menu item

  // set up timer interrupt 
  alarm_in_us(TIMER_MICROS);
}

// first Pico core does UI etc - not super time critical
void loop() {
  static int16_t debouncecounter;
  int16_t encvalue;

  if ((millis()-displaytimer) > DISPLAY_BLANK_MS) {
    UI_state=DORMANT;  // 
    blankdisplay(); // protect the OLED from burnin
  } 

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

  if (analogRead(GATE) < 1000) {  // if gate input is active, tell core 1 to process ADSR **** temporary mod for the proto
 // if (!digitalRead(GATE)) {  // if gate input is active, tell core 1 to process ADSR
    if (((millis()-gatetimer) > GATE_DELAY) && !gate) {  
      gate=1;  
      float cv=(AD_RANGE-sampleCV2()); // CV in is inverted 
      // pitch seems to be in MIDI notes with a 7 bit fractional part
      voices[0].osc->set_pitch((int16_t)(cv/CVIN_VOLT*12*128)+pitch*128+finefreq); // ~ 7 octave range
      voices[0].osc->Strike();
    }
  }
  else {
    gatetimer=millis();
    gate=0;   
  }
}



// second core dedicated to DSP

void setup1() {
  delay (200); // wait for main core to start up perhipherals
}

void loop1() {

  float envelope;
  int32_t left,right;

// not using I2S in in this sketch but read the data anyway
// these calls will stall if not data is available
  left=i2s.read();    // input is mono but we still have to read both channels
  right=i2s.read();

  voices[0].osc->Render(voices[0].sync_buffer, voices[0].buffer, BLOCK_SIZE);

#ifdef MONITOR_CPU1  
  digitalWrite(CPU_USE,0); // low - CPU not busy
#endif

  for (size_t i = 0; i < BLOCK_SIZE; i++) {
       // write samples to DMA buffer - this is a blocking call so it stalls when buffer is full
      envelope=env.Process(gate);
      i2s.write((int32_t)(voices[0].buffer[i]*envelope)<<16); // left
	    i2s.write((int32_t)(voices[0].buffer[i]*envelope)<<16); // right
  }

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE,1); // hi = CPU busy
#endif
}
