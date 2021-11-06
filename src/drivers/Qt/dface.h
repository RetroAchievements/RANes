#include "common/args.h"
#include "common/config.h"

#include "Qt/input.h"

extern CFGSTRUCT DriverConfig[];
extern ARGPSTRUCT DriverArgs[];

void DoDriverArgs(void);

int InitSound();
void WriteSound(int32 *Buffer, int Count);
int KillSound(void);
uint32 GetMaxSound(void);
uint32 GetWriteSound(void);
void FCEUD_MuteSoundOutput(bool value);

void SilenceSound(int s); /* DOS and SDL */

int InitJoysticks(void);
int KillJoysticks(void);
int AddJoystick( int which );
int RemoveJoystick( int which );
int FindJoystickByInstanceID( int which );
uint32 *GetJSOr(void);

int InitVideo(FCEUGI *gi);
int KillVideo(void);
void CalcVideoDimensions(void);
void BlitScreen(uint8 *XBuf);
void LockConsole(void);
void UnlockConsole(void);
void ToggleFS();		/* SDL */

int LoadGame(const char *path, bool silent);
//int CloseGame(void);

void Giggles(int);
void DoFun(void);

int FCEUD_NetworkConnect(void);

