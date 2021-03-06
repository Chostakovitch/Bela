/*
 ____  _____ _        _    
| __ )| ____| |      / \   
|  _ \|  _| | |     / _ \  
| |_) | |___| |___ / ___ \ 
|____/|_____|_____/_/   \_\

The platform for ultra-low latency audio and sensor processing

http://bela.io

A project of the Augmented Instruments Laboratory within the
Centre for Digital Music at Queen Mary University of London.
http://www.eecs.qmul.ac.uk/~andrewm

(c) 2016 Augmented Instruments Laboratory: Andrew McPherson,
	Astrid Bin, Liam Donovan, Christian Heinrichs, Robert Jack,
	Giulio Moro, Laurel Pardue, Victor Zappi. All rights reserved.

The Bela software is distributed under the GNU Lesser General Public License
(LGPL 3.0), available here: https://www.gnu.org/licenses/lgpl-3.0.txt
*/

#include <iostream>
#include <cstdlib>
#include <libgen.h>
#include <signal.h>
#include <string>
#include <getopt.h>
#include <libraries/sndfile/sndfile.h>				// to load audio files

#include <Bela.h>
#include "SampleData.h"

using namespace std;

// Load samples from file
int initFile(string file, SampleData *smp)//float *& smp)
{
	SNDFILE *sndfile ;
	SF_INFO sfinfo ;
	sfinfo.format = 0;
	if (!(sndfile = sf_open (file.c_str(), SFM_READ, &sfinfo))) {
		cout << "Couldn't open file " << file << endl;
		return 1;
	}

	int numChan = sfinfo.channels;
	if(numChan != 1)
	{
		cout << "Error: " << file << " is not a mono file" << endl;
		return 1;
	}

	smp->sampleLen = sfinfo.frames * numChan;
	smp->samples = new float[smp->sampleLen];
	if(smp == NULL){
		cout << "Could not allocate buffer" << endl;
		return 1;
	}

	int subformat = sfinfo.format & SF_FORMAT_SUBMASK;
	int readcount = sf_read_float(sndfile, smp->samples, smp->sampleLen);

	// Pad with zeros in case we couldn't read whole file
	for(int k = readcount; k <smp->sampleLen; k++)
		smp->samples[k] = 0;

	if (subformat == SF_FORMAT_FLOAT || subformat == SF_FORMAT_DOUBLE) {
		double	scale ;
		int 	m ;

		sf_command (sndfile, SFC_CALC_SIGNAL_MAX, &scale, sizeof (scale)) ;
		if (scale < 1e-10)
			scale = 1.0 ;
		else
			scale = 32700.0 / scale ;
		cout << "File samples scale = " << scale << endl;

		for (m = 0; m < smp->sampleLen; m++)
			smp->samples[m] *= scale;
	}

	sf_close(sndfile);

	return 0;
}


// Handle Ctrl-C by requesting that the audio rendering stop
void interrupt_handler(int var)
{
	//rt_task_delete ((RT_TASK *) &gTriggerSamplesTask);
	gShouldStop = true;
}

// Print usage information
void usage(const char * processName)
{
	cerr << "Usage: " << processName << " [options]" << endl;

	Bela_usage();

	cerr << "   --file [-f] filename:    Name of the file to load (default is \"sample.wav\")\n";
	cerr << "   --help [-h]:             Print this menu\n";
}

int main(int argc, char *argv[])
{
	BelaInitSettings* settings = Bela_InitSettings_alloc();	// Standard audio settings
	string fileName;			// Name of the sample to load

	SampleData sampleData;		// User define structure to pass data retrieved from file to render function
	sampleData.samples = 0;
	sampleData.sampleLen = -1;


	struct option customOptions[] =
	{
		{"help", 0, NULL, 'h'},
		{"file", 1, NULL, 'f'},
		{NULL, 0, NULL, 0}
	};

	// Set default settings
	Bela_defaultSettings(settings);
	settings->setup = setup;
	settings->render = render;
	settings->cleanup = cleanup;

	// Parse command-line arguments
	while (1) {
		int c = Bela_getopt_long(argc, argv, "hf:", customOptions, settings);
		if (c < 0)
			break;
		int ret = -1;
		switch (c) {
			case 'h':
				usage(basename(argv[0]));
				ret = 0;
				break;
			case 'f':
				fileName = string((char *)optarg);
				break;
			default:
				usage(basename(argv[0]));
				ret = 1;
				break;
		}
		if(ret >= 0)
		{
			Bela_InitSettings_free(settings);
			return ret;
		}
	}

	if(fileName.empty()){
		fileName = "sample.wav";
	}

	cout << "Loading file " << fileName << endl;
	cout << "You can load a custom file with `--file [-f] filename'" << endl;

	// Load file
	if(initFile(fileName, &sampleData) != 0)
	{
		cout << "Error: unable to load samples " << endl;
		return 1;
	}

	if(settings->verbose)
		cout << "File contains " << sampleData.sampleLen << " samples" << endl;


	// Initialise the PRU audio device
	if(Bela_initAudio(settings, &sampleData) != 0) {
		Bela_InitSettings_free(settings);	
		cout << "Error: unable to initialise audio" << endl;
		return 1;
	}
	Bela_InitSettings_free(settings);

	// Start the audio device running
	if(Bela_startAudio()) {
		cout << "Error: unable to start real-time audio" << endl;
		return 1;
	}

	// Set up interrupt handler to catch Control-C and SIGTERM
	signal(SIGINT, interrupt_handler);
	signal(SIGTERM, interrupt_handler);

	// Run until told to stop
	while(!gShouldStop) {
		usleep(100000);
	}

	// Stop the audio device
	Bela_stopAudio();

	// Clean up any resources allocated for audio
	Bela_cleanupAudio();

	// All done!
	return 0;
}
