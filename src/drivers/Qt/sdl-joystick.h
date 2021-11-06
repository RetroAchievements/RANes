// sdl-joystick.h

#ifndef __SDL_JOYSTICK_H__
#define __SDL_JOYSTICK_H__

#include <string>

#include "Qt/main.h"
#include "Qt/input.h"
#include "Qt/sdl.h"

#define MAX_JOYSTICKS 32

struct nesGamePadMap_t
{
	char guid[64];
	char name[128];
	char os[64];

	struct {
		char btn[GAMEPAD_NUM_BUTTONS][32];
	} conf[4];

	nesGamePadMap_t(void);
	~nesGamePadMap_t(void);

	void clearMapping(void);
	int parseMapping(const char *text);
};

struct jsDev_t
{
	SDL_Joystick *js;
	SDL_GameController *gc;

	jsDev_t(void);
	//~jsDev_t(void);

	void init(int idx);
	int close(void);
	SDL_Joystick *getJS(void);
	bool isGameController(void);
	bool isConnected(void);
	void print(void);
	int bindPort(int idx);
	int unbindPort(int idx);
	int getBindPorts(void);
	const char *getName(void);
	const char *getGUID(void);

private:
	int devIdx;
	int portBindMask;
	std::string guidStr;
	std::string name;
};

class GamePad_t
{
public:
	static const int NUM_CONFIG = 4;

	ButtConfig bmap[NUM_CONFIG][GAMEPAD_NUM_BUTTONS];

	char bmapState[GAMEPAD_NUM_BUTTONS];

	GamePad_t(void);
	~GamePad_t(void);

	int init(int port, const char *guid, const char *profile = NULL);
	const char *getGUID(void);

	int loadDefaults(void);
	int loadProfile(const char *name, const char *guid = NULL);

	int getDeviceIndex(void) { return devIdx; }
	int setDeviceIndex(int devIdx);
	int setMapping(const char *map);
	int setMapping(nesGamePadMap_t *map);

	int createProfile(const char *name);
	int getMapFromFile(const char *filename, nesGamePadMap_t *gpm);
	int getDefaultMap(const char *guid = NULL);
	int saveMappingToFile(const char *filename, const char *txtMap);
	int saveCurrentMapToFile(const char *filename);
	int deleteMapping(const char *name);
	int loadHotkeyMapFromFile(const char *name);
	int convText2ButtConfig( const char *txt, ButtConfig *bmap );
	int deleteHotKeyMappings(void);

	std::list <gamepad_function_key_t*> gpKeySeqList;
private:
	int devIdx;
	int portNum;
};

extern GamePad_t GamePad[4];

jsDev_t *getJoystickDevice(int devNum);

#endif
