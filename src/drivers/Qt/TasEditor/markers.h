// Specification file for Markers class
#pragma once
#include <stdint.h>
#include <vector>
#include <string>

#include "fceu.h"
#define MAX_NOTE_LEN 100

class MARKERS
{
public:
	MARKERS();

	void save(EMUFILE *os);
	bool load(EMUFILE *is);
	bool skipLoad(EMUFILE *is);

	void compressData(void);
	bool isAalreadyCompressed(void);
	void resetCompressedStatus(void);

	// saved data
	std::vector<std::string> notes;		// Format: 0th - note for intro (Marker 0), 1st - note for Marker1, 2nd - note for Marker2, ...
	// not saved data
	std::vector<int> markersArray;		// Format: 0th = Marker number (id) for frame 0, 1st = Marker number for frame 1, ...

private:
	// also saved data
	std::vector<uint8_t> compressedMarkersArray;

	bool alreadyCompressed;			// to compress only once
};
