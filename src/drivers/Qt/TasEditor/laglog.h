// Specification file for LagLog class
#pragma once
#include <stdint.h>
#include <vector>

enum LAG_FLAG_VALUES
{
	LAGGED_NO = 0,
	LAGGED_YES = 1,
	LAGGED_UNKNOWN = 2
};

class LAGLOG
{
public:
	LAGLOG(void);
	void reset(void);

	void compressData(void);
	bool isAlreadyCompressed(void);
	void resetCompressedStatus(void);

	void save(EMUFILE *os);
	bool load(EMUFILE *is);
	bool skipLoad(EMUFILE *is);

	void invalidateFromFrame(int frame);

	void setLagInfo(int frame, bool lagFlag);
	void eraseFrame(int frame, int numFrames = 1);
	void insertFrame(int frame, bool lagFlag, int numFrames = 1);

	int getSize(void);
	int getLagInfoAtFrame(int frame);

	int findFirstChange(LAGLOG& theirLog);

private:
	// saved data
	std::vector<uint8_t> compressedLagLog;

	// not saved data
	std::vector<uint8_t> lagLog;
	bool alreadyCompressed;			// to compress only once
};
