# 4HPico DSP Sketches

Arduino Pico sketches for the 4HPico DSP Eurorack module https://github.com/rheslip/2HPico-Eurorack-Module-Hardware

May 6 2026 - added Reverb, Delay, Braids and SMF Drums sketches. see the usage comments at the top of each sketch


You must have Arduino 2.xx installed with the Pico board support package https://github.com/earlephilhower/arduino-pico

The 4HPico requires an RP2350 processor since most DSP apps are fairly compute intensive. The DaisySP library uses floating point calculations extensively and will run very poorly on an RP2040. Some sketches require overclocking - check the comments in the source code.

Dependencies:

4HPico library from https://github.com/rheslip/4HPico-DSP-Sketches/tree/main/lib/4HPicolib - install it in your Arduino/Libraries directory

Adafruit Neopixel library

Some sketches use my fork of ElectroSmith's DaisySP library https://github.com/rheslip/DaisySP_Teensy