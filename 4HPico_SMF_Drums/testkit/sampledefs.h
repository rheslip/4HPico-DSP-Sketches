// sample structure built by wav2header based on wav2sketch by Paul Stoffregen

struct sample_t {
  const int16_t * samplearray; // pointer to sample array
  uint32_t samplesize; // size of the sample array
  uint32_t sampleindex; // current sample array index when playing. index at last sample= not playing
  uint8_t MIDINOTE;  // MIDI note on that plays this sample
  uint8_t play_volume; // play volume 0-127
  char sname[20];        // sample name
} sample[] = {

	Silence,	// pointer to sample array
	Silence_SIZE,	// size of the sample array
	Silence_SIZE,	//sampleindex. if at end of sample array sound is not playing
	0,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Silence",	// sample name

	Kick01,	// pointer to sample array
	Kick01_SIZE,	// size of the sample array
	Kick01_SIZE,	//sampleindex. if at end of sample array sound is not playing
	36,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Kick01",	// sample name

	Clap01,	// pointer to sample array
	Clap01_SIZE,	// size of the sample array
	Clap01_SIZE,	//sampleindex. if at end of sample array sound is not playing
	39,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Clap01",	// sample name

	Snare,	// pointer to sample array
	Snare_SIZE,	// size of the sample array
	Snare_SIZE,	//sampleindex. if at end of sample array sound is not playing
	40,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Snare",	// sample name

	Clhat01,	// pointer to sample array
	Clhat01_SIZE,	// size of the sample array
	Clhat01_SIZE,	//sampleindex. if at end of sample array sound is not playing
	42,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Clhat01",	// sample name

	Oh50,	// pointer to sample array
	Oh50_SIZE,	// size of the sample array
	Oh50_SIZE,	//sampleindex. if at end of sample array sound is not playing
	46,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Oh50",	// sample name

};
