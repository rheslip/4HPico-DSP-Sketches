
// Copyright 2026 Rich Heslip
//
// Author: Rich Heslip 
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
/*
Reverb example for 4HPico hardware 
R Heslip  April 2026

Uses most of the Pico2's memory but its reasonably light on CPU - can run 44khz sampling at 150mhz

Top Jacks - left and right Audio inputs 

Bottom Jacks - left and right  Audio out

Menu Parameters

Time - Reverb Time

Filter - Reverb Filter cutoff

Mix - Blends reverb signal with dry signal

Level - Output level

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

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring

//#define SAMPLERATE 11025 
//#define SAMPLERATE 22050  // 
#define SAMPLERATE 44100  // run at higher sample rate - works OK at 150mhz


I2S i2s(INPUT_PULLUP); // both input and output

#include "daisysp.h"

// including the source files is a pain but that way you compile in only the modules you need
// DaisySP statically allocates memory and some modules e.g. reverb use a lot of ram
#include "effects/reverbsc.cpp"

float samplerate=SAMPLERATE;  // for DaisySP
daisysp::ReverbSc reverb;

#define CV_VOLT 580.6  // a/d counts per volt - trim for V/octave

enum UISTATES {RUN,DORMANT,WAIT_BUTTON_RELEASE};
int16_t UI_state=RUN; // initial UI state

#define DEBOUNCE_CYCLES 100 // counter to debounce button release

int32_t displaytimer ; // display blanking timer

ClickEncoder menuenc(ENCA_IN,ENCB_IN,ENCSW_IN,ENCDIVIDE); // menu encoder object

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

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
int16_t reverbfeedback=50;  // set initial values
int16_t reverblpf=5050;
int16_t reverbmix=50;
int16_t reverblevel=75;

// menu callback functions
void setreverbfeedback(void) {
  reverb.SetFeedback(mapf(reverbfeedback,0,100,0,0.96)); // 100% feedback distorts
}

void setreverblpf(void) {
  reverb.SetLpFreq((float)reverblpf);
}

#include "menusystem.h"  // has to come after display and encoder objects creation



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

// set up timer interrupt 
  alarm_in_us(TIMER_MICROS);

  analogReadResolution(AD_BITS); // set up for max resolution

// set up I2S for 32 bits in and out
// PCM1808 is 24 bit only but I could not get 24 bit I2S working. 32 bits is little if any extra overhead
  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK); // Note: LRCLK = BCLK + 1
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(22050);
  i2s.begin();

  reverb.Init(samplerate);
  reverb.SetFeedback(mapf(reverbfeedback,0,100,0,0.96)); // initial settings
  reverb.SetLpFreq(mapf(reverblpf,0,100,1000,samplerate/2));

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
  display.printf(" 4HPico\n Reverb"); // power on message
  display.display();
  delay(3000);
  displaytimer=millis(); // reset display blanking timer
  drawmenu(0); // show first menu item
  menuenc.getValue(); // clear any initial input
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
   //  encvalue=menuenc.getValue();
   //  if (encvalue) Serial.printf("%d\n",encvalue);
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
}


// second core setup
// second core is dedicated to sample processing
void setup1() {
delay (1000); // wait for main core to start up peripherals
}

// process audio samples
void loop1(){
  float sigL,sigR,outL,outR,mix,level;
  int32_t left,right;

// these calls will stall if not data is available
  left=i2s.read();    // input is mono but we still have to read both channels
  right=i2s.read();

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE,1); // hi = CPU busy
#endif

  sigL=left*DIV_16; // convert input to float for DaisySP
  sigR=right*DIV_16; 

  mix=(float)(reverbmix)/100; // convert menu values to float
  level=(float)(reverblevel)/100;

  reverb.Process(sigL, sigR, &outL, &outR); 

 // sigL=(sigL*(1-mix)+ outL*mix)*level; // full dry to full wet signal mix
//  sigR=(sigR*(1-mix)+ outR*mix)*level;

  sigL=(sigL+ outL*mix)*level; // add reverb to dry signal - I think this sounds better
  sigR=(sigR+ outR*mix)*level;

  left=(int32_t)(sigL*MULT_16); // convert output back to int32
  right=(int32_t)(sigR*MULT_16); // convert output back to int32

#ifdef MONITOR_CPU1  
  digitalWrite(CPU_USE,0); // low - CPU not busy
#endif
// these calls will stall if buffer is full
	i2s.write(left); // left passthru
	i2s.write(right); // right passthru

}
