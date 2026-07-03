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
R Heslip Chorus+Delay for 4HPico July 2026
same as the delay sketch but chorus effect added before the delay - subtle but useful

// Stereo Chorus+Delay using the Pico-Audio framework which is a port of PJRC's Teensy Audio
// runs OK at 150 Mhz - very low CPU usage

Top Jacks - audio inputs

Middle jacks - not used - should probably add some modulation inputs

Bottom Jacks - audio outputs

Menu Parameters:
  Ch.Level - chorus level 
  Delay MS - delay time (both channels) in milliseconds
  Delay FB - % feedback from delay back to input for repeats
  PingPong - % cross feedback from delay back to other input for stereo ping pong effect
  Mix - dry/wet mix
  Level - signal output level

*/

#include "4HPico.h"
#include <I2S.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Monospaced_12.h"
#include <pico-audio.h>
#include "clickencoder.h"
#include <Wire.h>
#include <SPI.h>

#define DEBUG   // comment out to remove debug code

enum UISTATES {RUN,DORMANT,WAIT_BUTTON_RELEASE};
int16_t UI_state=RUN; // initial UI state

#define DEBOUNCE_CYCLES 100 // counter to debounce button release

#define DISPLAY_BLANK_MS 300000
int32_t displaytimer; // display blanking timer

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

// Pico Audio lib patch setup
// sample rate is fixed at 48k in the Pico Audio library
// you can change it by editing the definitions in AudioStream.h
// BE AWARE - this sketch uses a lot of the Pico 2 internal RAM. increasing sample rate will chew up proportionately more memory



// GUItool: begin automatically generated code
AudioInputI2S            i2s1;           //xy=88,260
AudioEffectChorus        chorus2;        //xy=268,479
AudioEffectChorus        chorus1;        //xy=285,115
AudioMixer4              mixer2;         //xy=545,435
AudioMixer4              mixer1;         //xy=573,191
AudioEffectDelay         delay2;         //xy=686,652
AudioEffectDelay         delay1;         //xy=822,75
AudioMixer4              mixeroutR;      //xy=903,405
AudioMixer4              mixeroutL;      //xy=905,276
AudioOutputI2S           i2s2;           //xy=1193,255
AudioConnection          patchCord1(i2s1, 0, mixer1, 0);
AudioConnection          patchCord2(i2s1, 0, mixeroutL, 0);
AudioConnection          patchCord3(i2s1, 0, chorus1, 0);
AudioConnection          patchCord4(i2s1, 1, mixer2, 0);
AudioConnection          patchCord5(i2s1, 1, mixeroutR, 0);
AudioConnection          patchCord6(i2s1, 1, chorus2, 0);
AudioConnection          patchCord7(chorus2, 0, mixer2, 3);
AudioConnection          patchCord8(chorus1, 0, mixer1, 3);
AudioConnection          patchCord9(mixer2, delay2);
AudioConnection          patchCord10(mixer2, 0, mixeroutR, 1);
AudioConnection          patchCord11(mixer1, delay1);
AudioConnection          patchCord12(mixer1, 0, mixeroutL, 1);
AudioConnection          patchCord13(delay2, 0, mixer2, 1);
AudioConnection          patchCord14(delay2, 0, mixer1, 2);
AudioConnection          patchCord15(delay1, 0, mixer1, 1);
AudioConnection          patchCord16(delay1, 0, mixer2, 2);
AudioConnection          patchCord17(mixeroutR, 0, i2s2, 1);
AudioConnection          patchCord18(mixeroutL, 0, i2s2, 0);
// GUItool: end automatically generated code

// Number of samples in each delay line
#define CHORUS_DELAY_LENGTH (16*AUDIO_BLOCK_SAMPLES)
// Allocate the delay lines for left and right channels
int16_t l_delayline[CHORUS_DELAY_LENGTH];
int16_t r_delayline[CHORUS_DELAY_LENGTH];

// number of "voices" in the chorus which INCLUDES the original voice
int16_t n_chorus = 2;

int16_t delaytime0 = 500;
int16_t delayfeedback = 100;
int16_t crossfeedback = 100;
int16_t choruslevel = 0;
int16_t wetdrymix = 100;
int16_t outputlevel = 800;

// menu callback to update all settings
// parameters passed by menusystem not used because we are updating everything on every change
void updatepatch(int16_t dummy1, int16_t dummy2) {
    // delay tap connects through a
  // mixer to the right channel output

  delay1.delay(0, delaytime0);
  delay2.delay(0, delaytime0);
  mixer1.gain(0, 0.8);  // dry input signal
  mixer1.gain(1,(float)delayfeedback/1000);  // delayed signal feedback
  mixer1.gain(2,(float)crossfeedback/1000);  // ping pong feedback
  mixer1.gain(3,(float)choruslevel/1000);  // chorus level
  mixer2.gain(0, 0.8);  // dry input signal
  mixer2.gain(1,(float)delayfeedback/1000);  // delayed signal feedback
  mixer2.gain(2,(float)crossfeedback/1000);  //  ping pong feedback
  mixer2.gain(3,(float)choruslevel/1000);  // chorus level
  mixeroutL.gain(1,(float)wetdrymix/1000*(float)outputlevel/1000); // wet signal gain
  mixeroutR.gain(1,(float)wetdrymix/1000*(float)outputlevel/1000);
  mixeroutL.gain(0,(1.0f-(float)wetdrymix/1000)*(float)outputlevel/1000); // dry signal gain
  mixeroutR.gain(0,(1.0f-(float)wetdrymix/1000)*(float)outputlevel/1000);
  chorus1.voices(n_chorus);
  chorus2.voices(n_chorus);
}

#include "menusystem.h"

void setup() {

  Serial.begin(115200);
// init IO ports
  pinMode(ENCA_IN, INPUT_PULLUP);  // menu encoder and switch
  pinMode(ENCB_IN, INPUT_PULLUP);    
  pinMode(ENCSW_IN, INPUT_PULLUP); 

// set up I2C pins 
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();

// set up timer interrupt 
  alarm_in_us(TIMER_MICROS);

  analogReadResolution(AD_BITS); // set up for max resolution

  // give the audio library some memory.  We'll be able
  // to see how much it actually uses, which can be used
  // to reduce this to the minimum necessary.
  AudioMemory(1000);  // determined by watching the audio stats when delay is at max
  updatepatch(0,0); // set initial values

	i2s2.begin(BCLK,WS,MCLK,I2S_DATAIN,I2S_DATA); // set up I2S for PCM5102A and PCM1808 which requires MCLK and a data input pin

  chorus1.begin(r_delayline,CHORUS_DELAY_LENGTH,n_chorus); // set up chorus effect L & R
  chorus2.begin(r_delayline,CHORUS_DELAY_LENGTH,n_chorus);

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
  display.printf(" 4HPico\nCh+Delay"); // power on message
  display.display();
  delay(3000);
  displaytimer=millis(); // reset display blanking timer
  drawmenu(0); // show first menu item
  menuenc.getValue(); // clear any initial input
}


int count = 0;
int speed = 60;


// first core handles Pico Audio processing under interrupts
void loop() {

  // print a summary of the current & maximum usage

  Serial.print("all=");
  Serial.print(AudioProcessorUsage());
  Serial.print(",");
  Serial.print(AudioProcessorUsageMax());
  Serial.print("    ");
  Serial.print("Memory: ");
  Serial.print(AudioMemoryUsage());
  Serial.print(",");
  Serial.print(AudioMemoryUsageMax());
  Serial.print("    ");

  Serial.println();
  delay(speed);

}

// second core setup
// second core is dedicated to UI
void setup1() {
  delay (1000); // wait for main core to start up peripherals
}

void loop1() {
  static int16_t debouncecounter;
  int16_t encvalue;

  if ((millis()-displaytimer) > DISPLAY_BLANK_MS) {
    UI_state=DORMANT;  // 
    blankdisplay(); // protect the OLED from burnin
  } 

// UI state machine
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
}

