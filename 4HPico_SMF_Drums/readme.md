**Standard MIDI file Drum Machine for the 4HPico DSP module**

This one is pretty cool if I do say so myself. Grids is nice but has a fixed selection of 32 step drum patterns that it creates variations on. With SMF Drums you load MIDI drum loops to the onboard file system and it plays them back synced to the external clock input. MIDI files, drum samples, levels, and panning can be selected with the encoder and OLED display menu system. Loops can be as long as you want - the limitation is how much flash and RAM the RP2350 module has for storage. Fortunately MIDI files are very compact, often only a few hundred bytes for a 4-8 bar loop.

Its quite easy to create new drum kits - process is the same as the 2HPico Grids drum machine but it will make usage a lot easier if you adhere to the naming conventions in the docs.


There are thousands of free MIDI drum loops available. A great site for these is https://drum-patterns.com/

See the comments at the top of the file for instructions on how to load MIDI files, drum samples and usage of the module.

May 6 2026 - initial release. This one was a lot of work! Encounted some bugs/anomolies in the Arduino Pico libraries and getting playback to sync accurately to external clock took lots of time. Playback is now reliably syncing to external clock using interrupts for clock timing and MIDI playback.


Some explanation of how it works:

The FATFSUSB subsystem is used to load files on a file partition in the RP2350 module's flash. FATFSUSB makes the RP2350 module look like a flash drive to the host computer. You must reserve the file partition in the Arduino tools menu - 256k will hold a lot of MIDI files.
The filesystem is set up as four banks of MIDI files to help with organization - this also helps the UI a lot on the 2HPico version of the code. On power up the code loads all of the MIDI files into RAM - not strictly needed but it makes switching files a bit faster and simplifies the menu code. When you select a file in the menu system the code parses the MIDI file into a list of note on messages and event times.

Like all the other 2HPico sketches both CPU cores are used - the first core in the RP2350 handles the UI and MIDI playback, and the second handles audio playback. The first core sends MIDI messages to the second core through the interprocessor FIFO which is a fast and generally safe way to communicate between cores.

Syncing MIDI playback to the external clock is a bit tricky. A clock interrupt measures the external clock period. The MIDI file provides a pulse per quarter note value, and the timing between note on events is based on this PPQN value. A bit of math converts clock period, PPQN and event time into microseconds between events, which is what the note interrupt uses to schedule itself.

There were a number of issues that had to be worked out - handling the reset input, clock starting and stopping and syncing the overall MIDI loop playback. The reset line is level sensitive and when high it just keeps resetting the internal clock counter and a couple of other flags. When reset goes low and clock starts, playback begins. The clock interrupt kicks off the first note Alarm interrupt which in most cases happens immediately i.e. there is usually a drum hit on the first beat.

Playback of MIDI notes is done in the note Alarm interrupt. Once its running this interrupt sends MIDI messages to the second core and schedules the next note on event as another Alarm interrupt. Note off events are not used - we just let the drum samples play till they end or playback is restarted by another note on. 

Playback of notes continues till the last note on event in the MIDI loop. When the last note on event in the loop is sent, the note interrupt handler stops rescheduling itself and raises a loop sync flag. When the clock interrupt handler sees the loop sync flag active it waits for the clock counter to roll over modulo 16 (1 bar), then it starts the playback loop again. The implication is that all MIDI loops should be a multiple of 16 clocks. Other time signatures could probably be handled by changing the clock counter modulus, but so far it works with every drum loop I've tried.

Seems simple but it took a lot of head scratching and experimenting to get note playback to stay in perfect sync. I initially tried to do the clock measurement and playback in loop() but the latency builds up over time and it would fall just a bit out of sync after 16 or 20 bars. Interrupts made the note event and clock timing much more precise.I initially tried to do the clock measurement and playback in loop() but the latency builds up over time and it would fall just a bit out of sync after 16 or 20 bars. Interrupts made the note event and clock timing much more precise. Its actually not perfect because some interrupt latency does accumulate but its very small relative to the external clock period. Testing shows it stays in sync to 250bpm or so. There are other considerations as well e.g. the 2nd core has to poll the interprocessor FIFO very frequently so it doesn't fill and block the first core while its processing the playback interrupt.

Then there was the issue of mapping MIDI notes to samples. If you don't map samples automatically what gets played back is generally a cacophony and it can be quite hard to map the samples by ear based on the pattern you hear. Hence the recommended use the General MIDI drum files and naming samples with their General MIDI note numbers - see the docs at the top of the sketch for how to do this. When the sketch makes the initial sample selections for you it makes using it so much easier!

This is probably the most complex Arduino app I've ever written in terms of critical real time processing.