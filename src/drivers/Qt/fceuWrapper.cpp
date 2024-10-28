/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2020 mjbudd77
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
// fceuWrapper.cpp
//
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <unzip.h>

#include <QFileInfo>
#include <QStyleFactory>
#include "Qt/main.h"
#include "Qt/throttle.h"
#include "Qt/config.h"
#include "Qt/dface.h"
#include "Qt/fceuWrapper.h"
#include "Qt/input.h"
#include "Qt/sdl.h"
#include "Qt/sdl-video.h"
#include "Qt/nes_shm.h"
#include "Qt/unix-netplay.h"
#include "Qt/AviRecord.h"
#include "Qt/HexEditor.h"
#include "Qt/CheatsConf.h"
#include "Qt/SymbolicDebug.h"
#include "Qt/CodeDataLogger.h"
#include "Qt/ConsoleDebugger.h"
#include "Qt/ConsoleWindow.h"
#include "Qt/ConsoleUtilities.h"
#include "Qt/TasEditor/TasEditorWindow.h"
#include "Qt/fceux_git_info.h"

#include "common/cheat.h"
#include "../../fceu.h"
#include "../../cheat.h"
#include "../../movie.h"
#include "../../state.h"
#include "../../profiler.h"
#include "../../version.h"

#ifdef _S9XLUA_H
#include "../../fceulua.h"
#endif

#include "common/os_utils.h"
#include "common/configSys.h"
#include "utils/timeStamp.h"
#include "../../oldmovie.h"
#include "../../types.h"

#ifdef CREATE_AVI
#include "../videolog/nesvideos-piece.h"
#endif

#ifdef _MSC_VER 
//not #if defined(_WIN32) || defined(_WIN64) because we have strncasecmp in mingw
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif
//*****************************************************************
// Define Global Variables to be shared with FCEU Core
//*****************************************************************
int  dendy = 0;
int eoptions=0;
int isloaded=0;
int pal_emulation=0;
int gametype = 0;
int closeFinishedMovie = 0;
int KillFCEUXonFrame = 0;

bool swapDuty = 0;
bool turbo = false;
bool pauseAfterPlayback = false;
bool suggestReadOnlyReplay = true;
bool showStatusIconOpt = true;
bool drawInputAidsEnable = true;
bool usePaletteForVideoBg = false;
unsigned int gui_draw_area_width   = 256;
unsigned int gui_draw_area_height  = 256;

// global configuration object
Config *g_config = NULL;

static int inited = 0;
static int noconfig=0;
static int frameskip=0;
static int periodic_saves = 0;
static int   mutexLocks = 0;
static int   mutexPending = 0;
static bool  emulatorHasMutex = 0;
unsigned int emulatorCycleCount = 0;

extern double g_fpsScale;

#ifdef CREATE_AVI
int mutecapture = 0;
#endif
//*****************************************************************
// Define Global Functions to be shared with FCEU Core
//*****************************************************************
//

// Message functions defined in MsgLogViewer.cpp
//void FCEUD_Message(const char *text)
//void FCEUD_PrintError(const char *errormsg)

/**
 * Opens a file, C++ style, to be read a byte at a time.
 */
FILE *FCEUD_UTF8fopen(const char *fn, const char *mode)
{
   FILE *fp = ::fopen(fn,mode);
	return(fp);
}

/**
 * Opens a file to be read a byte at a time.
 */
EMUFILE_FILE* FCEUD_UTF8_fstream(const char *fn, const char *m)
{
	std::ios_base::openmode mode = std::ios_base::binary;
	if(!strcmp(m,"r") || !strcmp(m,"rb"))
		mode |= std::ios_base::in;
	else if(!strcmp(m,"w") || !strcmp(m,"wb"))
		mode |= std::ios_base::out | std::ios_base::trunc;
	else if(!strcmp(m,"a") || !strcmp(m,"ab"))
		mode |= std::ios_base::out | std::ios_base::app;
	else if(!strcmp(m,"r+") || !strcmp(m,"r+b"))
		mode |= std::ios_base::in | std::ios_base::out;
	else if(!strcmp(m,"w+") || !strcmp(m,"w+b"))
		mode |= std::ios_base::in | std::ios_base::out | std::ios_base::trunc;
	else if(!strcmp(m,"a+") || !strcmp(m,"a+b"))
		mode |= std::ios_base::in | std::ios_base::out | std::ios_base::app;
    return new EMUFILE_FILE(fn, m);
	//return new std::fstream(fn,mode);
}

#if defined(MSVC)
 #ifdef _M_X64
   #define _MSVC_ARCH "x64"
 #else
   #define _MSVC_ARCH "x86"
 #endif
 #ifdef _DEBUG
  #define _MSVC_BUILD "debug"
 #else
  #define _MSVC_BUILD "release"
 #endif
 #define __COMPILER__STRING__ "msvc " _Py_STRINGIZE(_MSC_VER) " " _MSVC_ARCH " " _MSVC_BUILD
 #define _Py_STRINGIZE(X) _Py_STRINGIZE1((X))
 #define _Py_STRINGIZE1(X) _Py_STRINGIZE2 ## X
 #define _Py_STRINGIZE2(X) #X
 //re: http://72.14.203.104/search?q=cache:HG-okth5NGkJ:mail.python.org/pipermail/python-checkins/2002-November/030704.html+_msc_ver+compiler+version+string&hl=en&gl=us&ct=clnk&cd=5
#elif defined(__GNUC__)
 #define __COMPILER__STRING__ "gcc " __VERSION__
#elif defined(__clang__)
 #define __COMPILER__STRING__ "clang " __VERSION__
#else
 #define __COMPILER__STRING__ "unknown"
#endif

static const char *s_CompilerString = __COMPILER__STRING__;
/**
 * Returns the compiler string.
 */
const char *FCEUD_GetCompilerString(void)
{
	return s_CompilerString;
}

/**
 * Get the time in ticks.
 */
uint64
FCEUD_GetTime(void)
{
	uint64 t;

	if (FCEU::timeStampModuleInitialized())
	{
		FCEU::timeStampRecord ts;

		ts.readNew();

		t = ts.toCounts();
	}
	else
	{
		t = (double)SDL_GetTicks();

		t = t * 1e-3;
	}
	return t;
}

/**
 * Get the tick frequency in Hz.
 */
uint64
FCEUD_GetTimeFreq(void)
{
	// SDL_GetTicks() is in milliseconds
	uint64 f = 1000;

	if (FCEU::timeStampModuleInitialized())
	{
		f = FCEU::timeStampRecord::countFreq();
	}
	return f;
}

/**
 * Initialize all of the subsystem drivers: video, audio, and joystick.
 */
static int
DriverInitialize(FCEUGI *gi)
{
	if (InitVideo(gi) < 0)
	{
		return 0;
	}
	inited|=4;

	if (InitSound())
	{
		inited|=1;
	}

	if (InitJoysticks())
	{
		inited|=2;
	}

	int fourscore=0;
	g_config->getOption("SDL.FourScore", &fourscore);
	eoptions &= ~EO_FOURSCORE;
	if (fourscore)
	{
		eoptions |= EO_FOURSCORE;
	}

	InitInputInterface();
	return 1;
}

/**
 * Shut down all of the subsystem drivers: video, audio, and joystick.
 */
static void
DriverKill()
{
	if (!noconfig)
		g_config->save();

	KillJoysticks();

	if(inited&4)
		KillVideo();
	if(inited&1)
		KillSound();
	inited=0;
}

int LoadGameFromLua( const char *path )
{
	//printf("Load From Lua: '%s'\n", path);
	fceuWrapperUnLock();

	consoleWindow->emulatorThread->signalRomLoad(path);

	fceuWrapperLock();
	return 0;
}

/**
 * Reloads last game
 */
int reloadLastGame(void)
{
	std::string lastRom;
	g_config->getOption(std::string("SDL.LastOpenFile"), &lastRom);
	return LoadGame(lastRom.c_str(), false);
}

/**
 * Loads a game, given a full path/filename.  The driver code must be
 * initialized after the game is loaded, because the emulator code
 * provides data necessary for the driver code(number of scanlines to
 * render, what virtual input devices to use, etc.).
 */
int LoadGame(const char *path, bool silent)
{
	std::string fullpath;
	int gg_enabled, autoLoadDebug, autoOpenDebugger, autoInputPreset;

	if (isloaded){
		CloseGame();
	}

	QFileInfo fi( path );

	// Resolve absolute path to file
	if ( fi.exists() )
	{
		//printf("FI: '%s'\n", fi.absoluteFilePath().toStdString().c_str() );
		//printf("FI: '%s'\n", fi.canonicalFilePath().toStdString().c_str() );
		fullpath = fi.canonicalFilePath().toStdString();
	}
	else
	{
		fullpath.assign( path );
	}
//#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
//
//	// Resolve absolute path to file
//	if ( realpath( path, fullpath ) == NULL )
//	{
//		strcpy( fullpath, path );
//	}
//#else
//	strcpy( fullpath, path );
//#endif

	//printf("Fullpath: %zi '%s'\n", sizeof(fullpath), fullpath );

	// For some reason, the core of the emulator clears the state of 
	// the game genie option selection. So check the config each time
	// and re-enable the core game genie state if needed.
	g_config->getOption ("SDL.GameGenie", &gg_enabled);

	FCEUI_SetGameGenie (gg_enabled);

	// Set RAM Init Method Prior to Loading New Game
	g_config->getOption ("SDL.RamInitMethod", &RAMInitOption);

	// Load the game
	if(!FCEUI_LoadGame(fullpath.c_str(), 1, silent)) {
		return 0;
	}

	if ( consoleWindow )
	{
		consoleWindow->addRecentRom( fullpath.c_str() );
	}

	hexEditorLoadBookmarks();

	g_config->getOption( "SDL.AutoLoadDebugFiles", &autoLoadDebug );

	if ( autoLoadDebug )
	{
		loadGameDebugBreakpoints();
	}

	g_config->getOption( "SDL.AutoOpenDebugger", &autoOpenDebugger );

	if ( autoOpenDebugger && !debuggerWindowIsOpen() )
	{
		consoleWindow->openDebugWindow();
	}

	debugSymbolTable.loadGameSymbols();

	updateCheatDialog();

	CDLoggerROMChanged();

	int state_to_load;
	g_config->getOption("SDL.AutoLoadState", &state_to_load);
	if (state_to_load >= 0 && state_to_load < 10){
	    FCEUI_SelectState(state_to_load, 0);
	    FCEUI_LoadState(NULL, false);
	}

	g_config->getOption( "SDL.AutoInputPreset", &autoInputPreset );

	if ( autoInputPreset )
	{
		loadInputSettingsFromFile();
	}

	ParseGIInput(GameInfo);
	RefreshThrottleFPS();

	if(!DriverInitialize(GameInfo)) {
		return(0);
	}
	
	// set pal/ntsc
	int id, region, autoDetectPAL;
	g_config->getOption("SDL.PAL", &region);
	g_config->getOption("SDL.AutoDetectPAL", &autoDetectPAL);

	if ( autoDetectPAL )
	{
		id = FCEUI_GetCurrentVidSystem(NULL, NULL);

		if ( region == 2 )
		{	// Dendy mode:
			//   Run PAL Games as PAL
			//   Run NTSC Games as Dendy
			if ( id == 1 )
			{
				g_config->setOption("SDL.PAL", id);
				FCEUI_SetRegion(id);
			}
			else
			{
				FCEUI_SetRegion(region);
			}
		}
		else
		{	// Run NTSC games as NTSC and PAL games as PAL
			g_config->setOption("SDL.PAL", id);
			FCEUI_SetRegion(id);
		}
	}
	else
	{
		// If not Auto-detection of region,
		// Strictly enforce region GUI selection
		// Does not matter what type of game is 
		// loaded, the current region selection is used 
		FCEUI_SetRegion(region);
	}

	// Always re-calculate video dimensions after setting region.
	CalcVideoDimensions();

	// Force re-send of video settings to console viewer
	if ( consoleWindow )
	{
		consoleWindow->videoReset();
	}

	g_config->getOption("SDL.SwapDuty", &id);
	swapDuty = id;
	
	// Wave Recording done through menu or hotkeys
	//std::string filename;
	//g_config->getOption("SDL.Sound.RecordFile", &filename);
	//if (filename.size()) 
	//{
	//	if (!FCEUI_BeginWaveRecord(filename.c_str())) {
	//		g_config->setOption("SDL.Sound.RecordFile", "");
	//	}
	//}
	isloaded = 1;

	//FCEUD_NetworkConnect();
	return 1;
}

/**
 * Closes a game.  Frees memory, and deinitializes the drivers.
 */
int
CloseGame(void)
{
	std::string filename;

	if ( nes_shm )
	{	// Clear Screen on Game Close
		nes_shm->clear_pixbuf();
		nes_shm->blitUpdated = 1;
	}

	if (!isloaded) {
		return(0);
	}

	// If the emulation thread is stuck hanging at a breakpoint,
	// disable breakpoint debugging and wait for the thread to 
	// complete its frame. So that it is idle with a minimal call
	// stack when we close the ROM. After thread has completed the
	// frame, it is then safe to re-enable breakpoint debugging.
	if ( debuggerWaitingAtBreakpoint() )
	{
		bpDebugSetEnable(false);

		if ( fceuWrapperIsLocked() )
		{
			fceuWrapperUnLock();
			msleep(100);
			fceuWrapperLock();
		}
		else
		{
			msleep(100);
		}
		bpDebugSetEnable(true);
	}

	hexEditorSaveBookmarks();
	saveGameDebugBreakpoints();
	debuggerClearAllBreakpoints();
	debuggerClearAllBookmarks();

	debugSymbolTable.save();
	debugSymbolTable.clear();
	CDLoggerROMClosed();

	int state_to_save;
	g_config->getOption("SDL.AutoSaveState", &state_to_save);
	if (state_to_save < 10 && state_to_save >= 0){
	    FCEUI_SelectState(state_to_save, 0);
	    FCEUI_SaveState(NULL, false);
	}

	int autoInputPreset;
	g_config->getOption( "SDL.AutoInputPreset", &autoInputPreset );

	if ( autoInputPreset )
	{
		saveInputSettingsToFile();
	}

	if ( tasWin != NULL )
	{
		tasWin->requestWindowClose();
	}

	FCEUI_CloseGame();

	DriverKill();
	isloaded = 0;
	GameInfo = 0;

	g_config->getOption("SDL.Sound.RecordFile", &filename);
	if(filename.size()) {
		FCEUI_EndWaveRecord();
	}

	InputUserActiveFix();
	return(1);
}

int  fceuWrapperSoftReset(void)
{
	if ( isloaded )
	{
		ResetNES();
	}
	return 0;
}

int  fceuWrapperHardReset(void)
{
	if ( isloaded && GameInfo )
	{
		char romPath[2048];

		romPath[0] = 0;

		if ( GameInfo->archiveFilename )
		{
			strcpy( romPath, GameInfo->archiveFilename );
		}
		else if ( GameInfo->filename )
		{
			strcpy( romPath, GameInfo->filename );
		}

		if ( romPath[0] != 0 )
		{
			CloseGame();
			//printf("Loading: '%s'\n", romPath );
			LoadGame ( romPath );
		}
	}
	return 0;
}

int  fceuWrapperTogglePause(void)
{
	if ( isloaded )
	{
		FCEUI_ToggleEmulationPause();
	}
	return 0;
}

bool fceuWrapperGameLoaded(void)
{
	return (isloaded ? true : false);
}

void fceuWrapperRequestAppExit(void)
{
	if ( consoleWindow )
	{
		consoleWindow->requestClose();
	}
}

static const char *DriverUsage =
"Option         Value   Description\n"
"--pal          {0|1}   Use PAL timing.\n"
"--newppu       {0|1}   Enable the new PPU core. (WARNING: May break savestates)\n"
"--input(1,2)   d       Set which input device to emulate for input 1 or 2.\n"
"                         Devices:  gamepad zapper powerpad.0 powerpad.1\n"
"                         arkanoid\n"
"--input(3,4)   d       Set the famicom expansion device to emulate for\n"
"                       input(3, 4)\n"
"                          Devices: quizking hypershot mahjong toprider ftrainer\n"
"                          familykeyboard oekakids arkanoid shadow bworld\n"
"                          4player\n"
"--gamegenie    {0|1}   Enable emulated Game Genie.\n"
"--frameskip    x       Set # of frames to skip per emulated frame.\n"
"--xres         x       Set horizontal resolution for full screen mode.\n"
"--yres         x       Set vertical resolution for full screen mode.\n"
"--autoscale    {0|1}   Enable autoscaling in fullscreen. \n"
"--keepratio    {0|1}   Keep native NES aspect ratio when autoscaling. \n"
"--(x/y)scale   x       Multiply width/height by x. \n"
"                         (Real numbers >0 with OpenGL, otherwise integers >0).\n"
"--(x/y)stretch {0|1}   Stretch to fill surface on x/y axis (OpenGL only).\n"
"--fullscreen   {0|1}   Enable full screen mode.\n"
"--noframe      {0|1}   Hide title bar and window decorations.\n"
"--special      {1-4}   Use special video scaling filters\n"
"                         (1 = hq2x; 2 = Scale2x; 3 = NTSC 2x; 4 = hq3x;\n"
"                         5 = Scale3x; 6 = Prescale2x; 7 = Prescale3x; 8=Precale4x; 9=PAL)\n"
"--palette      f       Load custom global palette from file f.\n"
"--sound        {0|1}   Enable sound.\n"
"--soundrate    x       Set sound playback rate to x Hz.\n"
"--soundq      {0|1|2}  Set sound quality. (0 = Low 1 = High 2 = Very High)\n"
"--soundbufsize x       Set sound buffer size to x ms.\n"
"--volume      {0-256}  Set volume to x.\n"
"--soundrecord  f       Record sound to file f.\n"
"--playmov      f       Play back a recorded FCM/FM2/FM3 movie from filename f.\n"
"--pauseframe   x       Pause movie playback at frame x.\n"
"--fcmconvert   f       Convert fcm movie file f to fm2.\n"
"--ripsubs      f       Convert movie's subtitles to srt\n"
"--subtitles    {0|1}   Enable subtitle display\n"
"--fourscore    {0|1}   Enable fourscore emulation\n"
"--no-config    {0|1}   Use default config file and do not save\n"
"--net          s       Connect to server 's' for TCP/IP network play.\n"
"--port         x       Use TCP/IP port x for network play.\n"
"--user         x       Set the nickname to use in network play.\n"
"--pass         x       Set password to use for connecting to the server.\n"
"--netkey       s       Use string 's' to create a unique session for the\n"
"                       game loaded.\n"
"--players      x       Set the number of local players in a network play\n"
"                       session.\n"
"--rp2mic       {0|1}   Replace Port 2 Start with microphone (Famicom).\n"
"--4buttonexit {0|1}    exit the emulator when A+B+Select+Start is pressed\n"
"--loadstate {0-9|>9}   load from the given state when the game is loaded\n"
"--savestate {0-9|>9}   save to the given state when the game is closed\n"
"                         to not save/load automatically provide a number\n"
"                         greater than 9\n"
"--periodicsaves {0|1}  enable automatic periodic saving.  This will save to\n"
"                         the state passed to --savestate\n";

static void ShowUsage(const char *prog)
{
	int i,j;
	FCEUD_Message("Starting " FCEU_NAME_AND_VERSION "...\n");
	printf("\nUsage is as follows:\n%s <options> filename\n\n",prog);
	puts(DriverUsage);
#ifdef _S9XLUA_H
	puts ("--loadlua      f       Loads lua script from filename f.");
#endif
#ifdef CREATE_AVI
	puts ("--videolog     c       Calls mencoder to grab the video and audio streams to\n                         encode them. Check the documentation for more on this.");
	puts ("--mute        {0|1}    Mutes FCEUX while still passing the audio stream to\n                         mencoder during avi creation.");
#endif
	puts ("--style=KEY            Use Qt GUI Style based on supplied key. Available system style keys are:\n");

	QStringList styleList = QStyleFactory::keys();

	j=0;
	for (i=0; i<styleList.size(); i++)
	{
		printf("  %16s  ", styleList[i].toStdString().c_str() ); j++;

		if ( j >= 4 )
		{
			printf("\n"); j=0;
		}
	}
	printf("\n\n");
	printf(" Custom Qt stylesheets (.qss files) may be used by setting an\n");
	printf(" environment variable named FCEUX_QT_STYLESHEET equal to the \n");
	printf(" full (absolute) path to the qss file.\n");

	puts("");
	printf("Compiled with SDL version %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL );
	SDL_version v; 
	SDL_GetVersion(&v);
	printf("Linked with SDL version %d.%d.%d\n", v.major, v.minor, v.patch);
  	printf("Compiled with QT version %d.%d.%d\n", QT_VERSION_MAJOR, QT_VERSION_MINOR, QT_VERSION_PATCH );
	printf("git URL: %s\n", fceu_get_git_url() );
	printf("git Rev: %s\n", fceu_get_git_rev() );
	
}

// Pre-GUI initialization.
int  fceuWrapperPreInit( int argc, char *argv[] )
{
	for (int i=0; i<argc; i++)
	{
		if ( (strcmp(argv[i], "--help") == 0) || (strcmp(argv[i],"-h") == 0) )
		{
			ShowUsage(argv[0]);
			exit(0);
		}
		else if ( strcmp(argv[i], "--no-gui") == 0)
		{
			printf("Error: Qt/SDL version does not support --no-gui option.\n");
			exit(1);
		}
		else if ( strcmp(argv[i], "--version") == 0)
		{
			printf("%i.%i.%i\n", FCEU_VERSION_MAJOR, FCEU_VERSION_MINOR, FCEU_VERSION_PATCH);
			exit(0);
		}
	}
	return 0;
}

int  fceuWrapperInit( int argc, char *argv[] )
{
	int opt, error;
	std::string s;

	FCEUD_Message("Starting " FCEU_NAME_AND_VERSION "...\n");

	/* SDL_INIT_VIDEO Needed for (joystick config) event processing? */
	if (SDL_Init(SDL_INIT_VIDEO)) 
	{
		printf("Could not initialize SDL: %s.\n", SDL_GetError());
		exit(-1);
	}
	if ( SDL_SetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1" ) == SDL_FALSE )
	{
		printf("Error setting SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS\n");
	}

	// Initialize the configuration system
	g_config = InitConfig();

	if ( !g_config )
	{
		printf("Error: Could not initialize configuration system\n");
		exit(-1);
	}

	// initialize the infrastructure
	error = FCEUI_Initialize();

	if (error != 1) 
	{
		printf("Error: Initializing FCEUI\n");
		ShowUsage(argv[0]);
		//SDL_Quit();
		exit(-1);
	}

	int romIndex = g_config->parse(argc, argv);

	// This is here so that a default fceux.cfg will be created on first
	// run, even without a valid ROM to play.
	// Unless, of course, there's actually --no-config given
	// mbg 8/23/2008 - this is also here so that the inputcfg routines can have 
    // a chance to dump the new inputcfg to the fceux.cfg  in case you didnt 
    // specify a rom  filename
	g_config->getOption("SDL.NoConfig", &noconfig);

	if (!noconfig)
	{
		g_config->save();
	}

	// update the input devices
	UpdateInput(g_config);

	// check for a .fcm file to convert to .fm2
	g_config->getOption ("SDL.FCMConvert", &s);
	g_config->setOption ("SDL.FCMConvert", "");
	if (!s.empty())
	{
		int okcount = 0;
		std::string infname = s.c_str();
		// produce output filename
		std::string outname;
		size_t dot = infname.find_last_of (".");
		if (dot == std::string::npos)
			outname = infname + ".fm2";
		else
			outname = infname.substr(0,dot) + ".fm2";
	  
		MovieData md;
		EFCM_CONVERTRESULT result = convert_fcm (md, infname);

		if (result == FCM_CONVERTRESULT_SUCCESS) {
			okcount++;
        // *outf = new EMUFILE;
		EMUFILE_FILE* outf = FCEUD_UTF8_fstream (outname, "wb");
		md.dump (outf,false);
		delete outf;
		FCEUD_Message ("Your file has been converted to FM2.\n");
	}
	else {
		FCEUD_Message ("Something went wrong while converting your file...\n");
	}
	  
		DriverKill();
	  SDL_Quit();
	  return 0;
	}
	// If x/y res set to 0, store current display res in SDL.LastX/YRes
	int yres, xres;
	g_config->getOption("SDL.XResolution", &xres);
	g_config->getOption("SDL.YResolution", &yres);
	
	int autoResume;
	g_config->getOption("SDL.AutoResume", &autoResume);
	if(autoResume)
	{
		AutoResumePlay = true;
	}
	else
	{
		AutoResumePlay = false;
	}

	// Cheats
	g_config->getOption ("SDL.CheatsDisabled"     , &globalCheatDisabled);
	g_config->getOption ("SDL.CheatsDisableAutoLS", &disableAutoLSCheats);

	g_config->getOption ("SDL.DrawInputAids", &drawInputAidsEnable);

	// Initialize Autofire Pattern
	int autofireOnFrames=1, autofireOffFrames=1;
	g_config->getOption ("SDL.AutofireOnFrames" , &autofireOnFrames );
	g_config->getOption ("SDL.AutofireOffFrames", &autofireOffFrames);

	SetAutoFirePattern( autofireOnFrames, autofireOffFrames );

	// check to see if recording HUD to AVI is enabled
	int rh;
	g_config->getOption("SDL.RecordHUD", &rh);
	if( rh )
		FCEUI_SetAviEnableHUDrecording(true);
	else
		FCEUI_SetAviEnableHUDrecording(false);

	g_config->getOption("SDL.SuggestReadOnlyReplay"  , &suggestReadOnlyReplay);
	g_config->getOption("SDL.PauseAfterMoviePlayback", &pauseAfterPlayback);
	g_config->getOption("SDL.CloseFinishedMovie"     , &closeFinishedMovie);
	g_config->getOption("SDL.MovieBindSavestate"     , &bindSavestate);
	g_config->getOption("SDL.SubtitlesOnAVI"         , &subtitlesOnAVI);
	g_config->getOption("SDL.AutoMovieBackup"        , &autoMovieBackup);
	g_config->getOption("SDL.MovieFullSaveStateLoads", &fullSaveStateLoads);

	// check to see if movie messages are disabled
	int mm;
	g_config->getOption("SDL.MovieMsg", &mm);
	if( mm == 0)
		FCEUI_SetAviDisableMovieMessages(true);
	else
		FCEUI_SetAviDisableMovieMessages(false);
  
	g_config->getOption("SDL.AviVideoFormat", &opt);
	aviSetSelVideoFormat(opt);

	// check for a .fm2 file to rip the subtitles
	g_config->getOption("SDL.RipSubs", &s);
	g_config->setOption("SDL.RipSubs", "");
	if (!s.empty())
	{
		MovieData md;
		std::string infname;
		infname = s.c_str();
		FCEUFILE *fp = FCEU_fopen(s.c_str(), 0, "rb", 0);
		
		// load the movie and and subtitles
		extern bool LoadFM2(MovieData&, EMUFILE*, int, bool);
		LoadFM2(md, fp->stream, INT_MAX, false);
		LoadSubtitles(md); // fill subtitleFrames and subtitleMessages
		delete fp;
		
		// produce .srt file's name and open it for writing
		std::string outname;
		size_t dot = infname.find_last_of (".");
		if (dot == std::string::npos)
			outname = infname + ".srt";
		else
			outname = infname.substr(0,dot) + ".srt";
		FILE *srtfile;
		srtfile = fopen(outname.c_str(), "w");
		
		if (srtfile != NULL)
		{
			extern std::vector<int> subtitleFrames;
			extern std::vector<std::string> subtitleMessages;
			float fps = (md.palFlag == 0 ? 60.0988 : 50.0069); // NTSC vs PAL
			float subduration = 3; // seconds for the subtitles to be displayed
			for (size_t i = 0; i < subtitleFrames.size(); i++)
			{
				fprintf(srtfile, "%zi\n", i+1); // starts with 1, not 0
				double seconds, ms, endseconds, endms;
				seconds = subtitleFrames[i]/fps;
				if (i+1 < subtitleFrames.size()) // there's another subtitle coming after this one
				{
					if (subtitleFrames[i+1]-subtitleFrames[i] < subduration*fps) // avoid two subtitles at the same time
					{
						endseconds = (subtitleFrames[i+1]-1)/fps; // frame x: subtitle1; frame x+1 subtitle2
					} else {
						endseconds = seconds+subduration;
							}
				} else {
					endseconds = seconds+subduration;
				}
				ms = modf(seconds, &seconds);
				endms = modf(endseconds, &endseconds);
				// this is just beyond ugly, don't show it to your kids
				fprintf(srtfile,
				"%02.0f:%02d:%02d,%03d --> %02.0f:%02d:%02d,%03d\n", // hh:mm:ss,ms --> hh:mm:ss,ms
				floor(seconds/3600),	(int)floor(seconds/60   ) % 60, (int)floor(seconds)	% 60, (int)(ms*1000),
				floor(endseconds/3600), (int)floor(endseconds/60) % 60, (int)floor(endseconds) % 60, (int)(endms*1000));
				fprintf(srtfile, "%s\n\n", subtitleMessages[i].c_str()); // new line for every subtitle
			}
		fclose(srtfile);
		printf("%d subtitles have been ripped.\n", (int)subtitleFrames.size());
		} else {
		FCEUD_Message("Couldn't create output srt file...\n");
		}
	  
		DriverKill();
		SDL_Quit();
		return 0;
	}

	nes_shm = open_nes_shm();

	if ( nes_shm == NULL )
	{
		printf("Error: Failed to open NES Shared memory\n");
		return -1;
	}

	// update the emu core
	UpdateEMUCore(g_config);

	CalcVideoDimensions();

	#ifdef CREATE_AVI
	g_config->getOption("SDL.VideoLog", &s);
	g_config->setOption("SDL.VideoLog", "");
	if(!s.empty())
	{
		NESVideoSetVideoCmd(s.c_str());
		LoggingEnabled = 1;
		g_config->getOption("SDL.MuteCapture", &mutecapture);
	} else {
		mutecapture = 0;
	}
	#endif

	{
		int id;
		g_config->getOption("SDL.InputDisplay", &id);
		extern int input_display;
		input_display = id;
		// not exactly an id as an true/false switch; still better than creating another int for that
		g_config->getOption("SDL.SubtitleDisplay", &id); 
		movieSubtitles = id ? true : false;
	}

	// Emulation Timing Mechanism
	{
		int timingMode;

		g_config->getOption("SDL.EmuTimingMech", &timingMode);

		setTimingMode( timingMode );
	}
	
	// load the hotkeys from the config life
	setHotKeys();

	// Initialize the State Recorder
	{
		bool srEnable = false;
		bool srUseTimeMode = false;
		int srHistDurMin = 15;
		int srFramesBtwSnaps = 60;
		int srTimeBtwSnapsMin = 0;
		int srTimeBtwSnapsSec = 3;
		int srCompressionLevel = 0;
		int pauseOnLoadTime = 3;
		int pauseOnLoad = StateRecorderConfigData::TEMPORARY_PAUSE;

		g_config->getOption("SDL.StateRecorderEnable", &srEnable);
		g_config->getOption("SDL.StateRecorderTimingMode", &srUseTimeMode);
		g_config->getOption("SDL.StateRecorderHistoryDurationMin", &srHistDurMin);
		g_config->getOption("SDL.StateRecorderFramesBetweenSnaps", &srFramesBtwSnaps);
		g_config->getOption("SDL.StateRecorderTimeBetweenSnapsMin", &srTimeBtwSnapsMin);
		g_config->getOption("SDL.StateRecorderTimeBetweenSnapsSec", &srTimeBtwSnapsSec);
		g_config->getOption("SDL.StateRecorderCompressionLevel", &srCompressionLevel);
		g_config->getOption("SDL.StateRecorderPauseOnLoad", &pauseOnLoad);
		g_config->getOption("SDL.StateRecorderPauseDuration", &pauseOnLoadTime);

		StateRecorderConfigData srConfig;

		srConfig.historyDurationMinutes = srHistDurMin;
		srConfig.timingMode = srUseTimeMode ?
					StateRecorderConfigData::TIME : StateRecorderConfigData::FRAMES;
		srConfig.timeBetweenSnapsMinutes = static_cast<float>( srTimeBtwSnapsMin ) +
			                          ( static_cast<float>( srTimeBtwSnapsSec ) / 60.0f );
		srConfig.framesBetweenSnaps = srFramesBtwSnaps;
		srConfig.compressionLevel = srCompressionLevel;
		srConfig.loadPauseTimeSeconds = pauseOnLoadTime;
		srConfig.pauseOnLoad = static_cast<StateRecorderConfigData::PauseType>(pauseOnLoad);

		FCEU_StateRecorderSetEnabled( srEnable );
		FCEU_StateRecorderSetConfigData( srConfig );
	}

	// Rom Load
	if (romIndex >= 0)
	{
		QFileInfo fi( argv[romIndex] );

		// Resolve absolute path to file
		if ( fi.exists() )
		{
			std::string fullpath = fi.canonicalFilePath().toStdString().c_str();

			error = LoadGame( fullpath.c_str() );

			if (error != 1)
			{
				DriverKill();
				SDL_Quit();
				return -1;
			}
			g_config->setOption("SDL.LastOpenFile", fullpath.c_str() );
			g_config->save();
		}
		else
		{
			// File was not found
			return -1;
		}
	}

	aviRecordInit();

	// movie playback
	g_config->getOption("SDL.Movie", &s);
	g_config->setOption("SDL.Movie", "");
	if (s != "")
	{
		if(s.find(".fm2") != std::string::npos || s.find(".fm3") != std::string::npos)
		{
			static int pauseframe;
			char replayReadOnlySetting;
			g_config->getOption("SDL.PauseFrame", &pauseframe);
			g_config->setOption("SDL.PauseFrame", 0);

			if (suggestReadOnlyReplay)
			{
				replayReadOnlySetting = true;
			}
			else
			{
				replayReadOnlySetting = FCEUI_GetMovieToggleReadOnly();
			}
			FCEUI_printf("Playing back movie located at %s\n", s.c_str());
			FCEUI_LoadMovie(s.c_str(), replayReadOnlySetting, pauseframe ? pauseframe : false);
		}
		else
		{
		  FCEUI_printf("Sorry, I don't know how to play back %s\n", s.c_str());
		}
		g_config->getOption("SDL.MovieLength",&KillFCEUXonFrame);
		printf("KillFCEUXonFrame %d\n",KillFCEUXonFrame);
	}
	
    int save_state;
    g_config->getOption("SDL.PeriodicSaves", &periodic_saves);
    g_config->getOption("SDL.AutoSaveState", &save_state);
    if(periodic_saves && save_state < 10 && save_state >= 0){
        FCEUI_SelectState(save_state, 0);
    } else {
        periodic_saves = 0;
    }
	
#ifdef _S9XLUA_H
	// load lua script if option passed
	g_config->getOption("SDL.LuaScript", &s);
	g_config->setOption("SDL.LuaScript", "");
	if (s.size() > 0)
	{
		QFileInfo fi( s.c_str() );

		// Resolve absolute path to file
		if ( fi.exists() )
		{
			//printf("FI: '%s'\n", fi.absoluteFilePath().toStdString().c_str() );
			//printf("FI: '%s'\n", fi.canonicalFilePath().toStdString().c_str() );
			s = fi.canonicalFilePath().toStdString();
		}
//#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
//
//		// Resolve absolute path to file
//		char fullpath[2048];
//		if ( realpath( s.c_str(), fullpath ) != NULL )
//		{
//			printf("Fullpath: '%s'\n", fullpath );
//			s.assign( fullpath );
//		}
//#endif
		FCEU_LoadLuaCode(s.c_str());
	}
#endif
	
	g_config->getOption("SDL.NewPPU", &newppu);
	g_config->getOption("SDL.Frameskip", &frameskip);

	return 0;
}

int  fceuWrapperClose( void )
{
	CloseGame();

	// exit the infrastructure
	FCEUI_Kill();
	SDL_Quit();

	return 0;
}

int  fceuWrapperMemoryCleanup(void)
{
	FreeCDLog();

	close_nes_shm();

	if ( g_config )
	{
		delete g_config; g_config = NULL;
	}

	return 0;
}

/**
 * Update the video, audio, and input subsystems with the provided
 * video (XBuf) and audio (Buffer) information.
 */
void
FCEUD_Update(uint8 *XBuf,
	 int32 *Buffer,
	 int Count)
{
	int blitDone = 0;
	//extern int FCEUDnetplay;

	//#ifdef CREATE_AVI
	//if (LoggingEnabled == 2 || (eoptions&EO_NOTHROTTLE))
	//{
	//	if(LoggingEnabled == 2)
	//	{
	//		int16* MonoBuf = new int16[Count];
	//		int n;
	//		for(n=0; n<Count; ++n)
	//		{
	//			MonoBuf[n] = Buffer[n] & 0xFFFF;
	//		}
	//		NESVideoLoggingAudio
	//		(
	//		  MonoBuf, 
	//		  FSettings.SndRate, 16, 1,
	//		  Count
	//		);
	//		delete [] MonoBuf;
	//	}
	//	Count /= 2;
	//	if (inited & 1)
	//	{
	//		if (Count > GetWriteSound()) Count = GetWriteSound();

	//		if (!mutecapture)
	//		{
	//			if(Count > 0 && Buffer) WriteSound(Buffer,Count);   
	//		}
	//	}
	//	//if (inited & 2)
	//	//	FCEUD_UpdateInput();
	//  	if(XBuf && (inited & 4)) BlitScreen(XBuf);
	//  
	//	return;
	//}
	//#endif
	aviRecordAddAudioFrame( Buffer, Count );
	
	WriteSound(Buffer,Count);

	//int ocount = Count;
	// apply frame scaling to Count
	//Count = (int)(Count / g_fpsScale);
	//if (Count) 
	//{
	//	int32 can=GetWriteSound();
	//	static int uflow=0;
	//	int32 tmpcan;

	//	// don't underflow when scaling fps
	//	if(can >= GetMaxSound() && g_fpsScale==1.0) uflow=1;	/* Go into massive underflow mode. */

	//	if(can > Count) can=Count;
	//	else uflow=0;

	//	#ifdef CREATE_AVI
	//	if (!mutecapture)
	//	#endif
	//	  WriteSound(Buffer,can);

	//	//if(uflow) puts("Underflow");
	//	tmpcan = GetWriteSound();
	//	// don't underflow when scaling fps
	//	if (g_fpsScale>1.0 || ((tmpcan < Count*0.90) && !uflow)) 
	//	{
	//		if (XBuf && (inited&4) && !(NoWaiting & 2))
	//		{
	//			BlitScreen(XBuf); blitDone = 1;
	//		}
	//		Buffer+=can;
	//		Count-=can;
	//		if(Count) 
	//		{
	//			if(NoWaiting) 
	//			{
	//				can=GetWriteSound();
	//				if(Count>can) Count=can;
	//				#ifdef CREATE_AVI
	//				if (!mutecapture)
	//				#endif
	//				  WriteSound(Buffer,Count);
	//			}
	//		  	else
	//		  	{
	//				while(Count>0) 
	//				{
	//					#ifdef CREATE_AVI
	//					if (!mutecapture)
	//					#endif
	//					  WriteSound(Buffer,(Count<ocount) ? Count : ocount);
	//					Count -= ocount;
	//				}
	//			}
	//		}
	//	} //else puts("Skipped");
	//	//else if (!NoWaiting && FCEUDnetplay && (uflow || tmpcan >= (Count * 1.8))) 
	//	//{
	//	//	if (Count > tmpcan) Count=tmpcan;
	//	//	while(tmpcan > 0) 
	//	//	{
	//	//		//	printf("Overwrite: %d\n", (Count <= tmpcan)?Count : tmpcan);
	//	//		#ifdef CREATE_AVI
	//	//		if (!mutecapture)
	//	//		#endif
	//	//		  WriteSound(Buffer, (Count <= tmpcan)?Count : tmpcan);
	//	//		tmpcan -= Count;
	//	//	}
	//	//}
	//}
  	//else 
	//{
	//	if (XBuf && (inited&4)) 
	//	{
	//		BlitScreen(XBuf); blitDone = 1;
	//	}
	//}
	if ( !blitDone )
	{
		if (XBuf && (inited&4)) 
		{
			BlitScreen(XBuf); blitDone = 1;
		}
	}
	//FCEUD_UpdateInput();
}

static void DoFun(int frameskip, int periodic_saves)
{
	uint8 *gfx = 0;
	int32 *sound = 0;
	int32 ssize = 0;
	static int fskipc = 0;
	//static int opause = 0;

	// If TAS editor is engaged, check whether a seek frame is set.
	// If a seek is in progress, don't emulate past target frame.
	if ( tasWindowIsOpen() )
	{
		int runToFrameTarget;
	
		runToFrameTarget = PLAYBACK::getPauseFrame();

		if ( runToFrameTarget >= 0)
		{
			if ( currFrameCounter >= runToFrameTarget )
			{
				FCEUI_SetEmulationPaused(EMULATIONPAUSED_PAUSED);
				return;
			}
		}
	}
    //TODO peroidic saves, working on it right now
    if (periodic_saves && FCEUD_GetTime() % PERIODIC_SAVE_INTERVAL < 30){
        FCEUI_SaveState(NULL, false);
    }
#ifdef FRAMESKIP
	fskipc = (fskipc + 1) % (frameskip + 1);
#endif

	if (NoWaiting || turbo) 
	{
		gfx = 0;
	}
	FCEUI_Emulate(&gfx, &sound, &ssize, fskipc);
	FCEUD_Update(gfx, sound, ssize);

	//if(opause!=FCEUI_EmulationPaused()) 
	//{
	//	opause=FCEUI_EmulationPaused();
	//	SilenceSound(opause);
	//}
	
	emulatorCycleCount++;
}

static std::string lockFile;
static bool debugMutexLock = false;

void fceuWrapperLock(const char *filename, int line, const char *func)
{
	fceuWrapperLock();

	if ( debugMutexLock )
	{
		char txt[32];

		if ( mutexLocks > 1 )
		{
			printf("Recursive Lock:%i\n", mutexLocks );
			printf("Already Locked By: %s\n", lockFile.c_str() );
			printf("Requested By: %s:%i - %s\n", filename, line, func );
		}
		sprintf( txt, ":%i - ", line );
		lockFile.assign(filename);
		lockFile.append(txt);
		lockFile.append(func);
	}
}

void fceuWrapperLock(void)
{
	mutexPending++;
	if ( consoleWindow != NULL )
	{
		consoleWindow->mutex->lock();
	}
	mutexPending--;
	mutexLocks++;
}

bool fceuWrapperTryLock(const char *filename, int line, const char *func, int timeout)
{
	bool lockAcq = false;

	lockAcq = fceuWrapperTryLock( timeout );

	if ( lockAcq && debugMutexLock)
	{
		char txt[32];
		sprintf( txt, ":%i - ", line );
		lockFile.assign(filename);
		lockFile.append(txt);
		lockFile.append(func);
	}
	return lockAcq;
}

bool fceuWrapperTryLock(int timeout)
{
	bool lockAcq = false;

	mutexPending++;
	if ( consoleWindow != NULL )
	{
		lockAcq = consoleWindow->mutex->tryLock( timeout );
	}
	mutexPending--;

	if ( lockAcq )
	{
		mutexLocks++;
	}
	return lockAcq;
}

void fceuWrapperUnLock(void)
{
	if ( mutexLocks > 0 )
	{
		mutexLocks--;
		if ( consoleWindow != NULL )
		{
			consoleWindow->mutex->unlock();
		}
	}
	else
	{
		printf("Error: Mutex is Already UnLocked\n");
		//abort(); // Uncomment to catch a stack trace
	}
}

bool fceuWrapperIsLocked(void)
{
	return mutexLocks > 0;
}

int  fceuWrapperUpdate( void )
{
	bool lock_acq;
	static bool mutexLockFail = false;

	// If a request is pending, 
	// sleep to allow request to be serviced.
	if ( mutexPending > 0 )
	{
		msleep( 16 );
	}

	lock_acq = fceuWrapperTryLock( __FILE__, __LINE__, __func__ );

	if ( !lock_acq )
	{
		if ( GameInfo )
		{
			if ( !mutexLockFail )
			{
				printf("Warning: Emulator Thread Failed to Acquire Mutex - GUI has Lock\n");
			}
			mutexLockFail = true;
		}
		msleep( 16 );

		return -1;
	}
	mutexLockFail = false;
	emulatorHasMutex = 1;
 
	if ( GameInfo )
	{
		DoFun(frameskip, periodic_saves);
	
		hexEditorUpdateMemoryValues();

		if ( consoleWindow )
		{
			consoleWindow->emulatorThread->signalFrameFinished();
		}
		fceuWrapperUnLock();

		emulatorHasMutex = 0;

#ifdef __FCEU_PROFILER_ENABLE__
		FCEU_profiler_log_thread_activity();
#endif
		while ( SpeedThrottle() )
		{
			// Input device processing is in main thread
			// because to MAC OS X SDL2 requires it.
			//FCEUD_UpdateInput(); 
		}
	}
	else
	{
		fceuWrapperUnLock();

		emulatorHasMutex = 0;

#ifdef __FCEU_PROFILER_ENABLE__
		FCEU_profiler_log_thread_activity();
#endif
		msleep( 100 );
	}
	return 0;
}

static int minizip_ScanArchive( const char *filepath, ArchiveScanRecord &rec)
{
	int idx=0, ret;
	unzFile zf;
	unz_file_info fi;
	char filename[512];

	zf = unzOpen( filepath );

	if ( zf == NULL )
	{
		//printf("Error: Failed to open Zip File: '%s'\n", fname.c_str() );
		return -1;
	}
	rec.type = 0;

	ret = unzGoToFirstFile( zf );

	//printf("unzGoToFirstFile: %i \n", ret );

	while ( ret == 0 )
	{
		FCEUARCHIVEFILEINFO_ITEM item;

		unzGetCurrentFileInfo( zf, &fi, filename, sizeof(filename), NULL, 0, NULL, 0 );

		//printf("Filename: %u '%s' \n", fi.uncompressed_size, filename );

		item.name.assign( filename );
		item.size  = fi.uncompressed_size;
		item.index = idx; idx++;

		rec.files.push_back( item );

		ret = unzGoToNextFile( zf );

		//printf("unzGoToNextFile: %i \n", ret );
	}
	rec.numFilesInArchive = idx;

	unzClose( zf );

	return 0;
}

#ifdef _USE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>

static int libarchive_ScanArchive( const char *filepath, ArchiveScanRecord &rec)
{
	int r, idx=0;
	struct archive *a;
	struct archive_entry *entry;

	a = archive_read_new();

	if (a == nullptr)
	{
		return -1;
	}

	// Initialize decoders
	r = archive_read_support_filter_all(a);
	if (r)
	{
		archive_read_free(a);
		return -1;
	}

	// Initialize formats
	r = archive_read_support_format_all(a);
	if (r)
	{
		archive_read_free(a);
		return -1;
	}

	r = archive_read_open_filename(a, filepath, 10240);

	if (r)
	{
		archive_read_free(a);
		return -1;
	}
	rec.type = 1;

	while (1)
	{
		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_EOF)
		{
			break;
		}
		else if (r != ARCHIVE_OK)
		{
			printf("archive_read_next_header() %s\n", archive_error_string(a));
			break;
		}
		const char *filename = archive_entry_pathname(entry);

		FCEUARCHIVEFILEINFO_ITEM item;
		item.name.assign( filename );
		item.size  = archive_entry_size(entry);
		item.index = idx; idx++;

		rec.files.push_back( item );
	}
	rec.numFilesInArchive = idx;

	archive_read_free(a);

	return 0;
}
#endif

ArchiveScanRecord FCEUD_ScanArchive(std::string fname)
{
	int ret = -1;
	ArchiveScanRecord rec;
		
#ifdef _USE_LIBARCHIVE
	ret = libarchive_ScanArchive( fname.c_str(), rec );
#endif

	if (ret == -1)
	{
		minizip_ScanArchive( fname.c_str(), rec );
	}
	return rec;
}

static FCEUFILE* minizip_OpenArchive(ArchiveScanRecord& asr, std::string &fname, std::string *searchFile, int innerIndex )
{
	int ret, idx=0;
	FCEUFILE* fp = nullptr;
	void *tmpMem = nullptr;
	unzFile zf;
	unz_file_info fi;
	char filename[512];
	bool foundFile = false;

	zf = unzOpen( fname.c_str() );

	if ( zf == NULL )
	{
		//printf("Error: Failed to open Zip File: '%s'\n", fname.c_str() );
		return fp;
	}

	//printf("Searching for %s in %s \n", searchFile.c_str(), fname.c_str() );

	ret = unzGoToFirstFile( zf );

	//printf("unzGoToFirstFile: %i \n", ret );

	while ( ret == 0 )
	{
		unzGetCurrentFileInfo( zf, &fi, filename, sizeof(filename), NULL, 0, NULL, 0 );

		//printf("Filename: %u '%s' \n", fi.uncompressed_size, filename );

		if (searchFile)
		{
			if ( strcmp( searchFile->c_str(), filename ) == 0 )
			{
			   //printf("Found Filename: %u '%s' \n", fi.uncompressed_size, filename );
				foundFile = true; break;
			}
		}
		else if ((innerIndex != -1) && (idx == innerIndex))
		{
			foundFile = true; break;
		}

		ret = unzGoToNextFile( zf );

		//printf("unzGoToNextFile: %i \n", ret );
		idx++;
	}

	if ( !foundFile )
	{
		unzClose( zf );
		return fp;
	}

	tmpMem = ::malloc( fi.uncompressed_size );

	if ( tmpMem == NULL )
	{
		unzClose( zf );
		return fp;
	}
	//printf("Loading via minizip\n");

	EMUFILE_MEMORY* ms = new EMUFILE_MEMORY(fi.uncompressed_size);

	unzOpenCurrentFile( zf );
	unzReadCurrentFile( zf, tmpMem, fi.uncompressed_size );
	unzCloseCurrentFile( zf );

	ms->fwrite( tmpMem, fi.uncompressed_size );

	free( tmpMem );

	//if we extracted the file correctly
	fp = new FCEUFILE();
	fp->archiveFilename = fname;
	fp->filename = filename;
	fp->fullFilename = fp->archiveFilename + "|" + fp->filename;
	fp->archiveIndex = idx;
	fp->mode = FCEUFILE::READ;
	fp->size = fi.uncompressed_size;
	fp->stream = ms;
	fp->archiveCount = (int)asr.numFilesInArchive;
	ms->fseek(0,SEEK_SET); //rewind so that the rom analyzer sees a freshly opened file

	unzClose( zf );

	return fp;
}

#ifdef _USE_LIBARCHIVE
static FCEUFILE* libarchive_OpenArchive( ArchiveScanRecord& asr, std::string& fname, std::string *searchFile, int innerIndex)
{
	int r, idx=0;
	struct archive *a;
	struct archive_entry *entry;
	const char *filename = nullptr;
	bool foundFile = false;
	int fileSize = 0;
	FCEUFILE* fp = nullptr;

	a = archive_read_new();

	if (a == nullptr)
	{
		archive_read_free(a);
		return nullptr;
	}

	// Initialize decoders
	r = archive_read_support_filter_all(a);
	if (r)
	{
		archive_read_free(a);
		return nullptr;
	}

	// Initialize formats
	r = archive_read_support_format_all(a);
	if (r)
	{
		archive_read_free(a);
		return nullptr;
	}

	r = archive_read_open_filename(a, fname.c_str(), 10240);

	if (r)
	{
		archive_read_free(a);
		return nullptr;
	}

	while (1)
	{
		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_EOF)
		{
			break;
		}
		else if (r != ARCHIVE_OK)
		{
			printf("archive_read_next_header() %s\n", archive_error_string(a));
			break;
		}
		filename = archive_entry_pathname(entry);
		fileSize = archive_entry_size(entry);

		if (searchFile)
		{
			if (strcmp( filename, searchFile->c_str() ) == 0)
			{
				foundFile = true; break;
			}
		}
		else if ((innerIndex != -1) && (idx == innerIndex))
		{
			foundFile = true; break;
		}
		idx++;
	}

	if (foundFile && (fileSize > 0))
	{
		const void *buff;
		size_t size, totalSize = 0;
		#if ARCHIVE_VERSION_NUMBER >= 3000000
			int64_t offset;
		#else
			off_t offset;
		#endif

		//printf("Loading via libarchive\n");

		EMUFILE_MEMORY* ms = new EMUFILE_MEMORY(fileSize);

		while (1)
		{
			r = archive_read_data_block(a, &buff, &size, &offset);

			if (r == ARCHIVE_EOF)
			{
				break;
			}
			if (r != ARCHIVE_OK)
			{
				break;
			}
			//printf("Read: %p   Size:%zu   Offset:%llu\n", buff, size, (long long int)offset);
			ms->fwrite( buff, size );
			totalSize += size;
		}

		//if we extracted the file correctly
		fp = new FCEUFILE();
		fp->archiveFilename = fname;
		fp->filename = filename;
		fp->fullFilename = fp->archiveFilename + "|" + fp->filename;
		fp->archiveIndex = idx;
		fp->mode = FCEUFILE::READ;
		fp->size = totalSize;
		fp->stream = ms;
		fp->archiveCount = (int)asr.numFilesInArchive;
		ms->fseek(0,SEEK_SET); //rewind so that the rom analyzer sees a freshly opened file
	}

	archive_read_free(a);

	return fp;
}

#endif

FCEUFILE* FCEUD_OpenArchive(ArchiveScanRecord& asr, std::string& fname, std::string* innerFilename, int* userCancel)
{
	FCEUFILE* fp = nullptr;
	std::string searchFile;

	if ( innerFilename != NULL )
	{
		searchFile = *innerFilename;
	}
	else
	{
		std::vector <std::string> fileList;

		for (size_t i=0; i<asr.files.size(); i++)
		{
			char base[512], suffix[128];

			getFileBaseName( asr.files[i].name.c_str(), base, suffix );

			if ( (strcasecmp( suffix, ".nes" ) == 0) ||
			     (strcasecmp( suffix, ".nsf" ) == 0) ||
			     (strcasecmp( suffix, ".fds" ) == 0) ||
			     (strcasecmp( suffix, ".unf" ) == 0) ||
			     (strcasecmp( suffix, ".unif") == 0) )
			{
				fileList.push_back( asr.files[i].name );
			}
		}

		if ( fileList.size() > 1 )
		{
			if ( consoleWindow != NULL )
			{
				int sel = consoleWindow->showListSelectDialog( "Select ROM From Archive", fileList );

				if ( sel < 0 )
				{
					if ( userCancel )
					{
						*userCancel = 1;
					}
					return fp;
				}
				searchFile = fileList[sel];
			}
		}
		else if ( fileList.size() > 0 )
		{
			searchFile = fileList[0];
		}
	}

#ifdef _USE_LIBARCHIVE
	fp = libarchive_OpenArchive(asr, fname, &searchFile, -1 );
#endif

	if (fp == nullptr)
	{
		fp = minizip_OpenArchive(asr, fname, &searchFile, -1 );
	}
	return fp;
}

FCEUFILE* FCEUD_OpenArchive(ArchiveScanRecord& asr, std::string& fname, std::string* innerFilename)
{
	int userCancel = 0;

	return FCEUD_OpenArchive( asr, fname, innerFilename, &userCancel );
}

FCEUFILE* FCEUD_OpenArchiveIndex(ArchiveScanRecord& asr, std::string &fname, int innerIndex, int* userCancel)
{
	FCEUFILE* fp = nullptr;

#ifdef _USE_LIBARCHIVE
	fp = libarchive_OpenArchive( asr, fname, nullptr, innerIndex );
#endif
	if (fp == nullptr)
	{
		fp = minizip_OpenArchive(asr, fname, nullptr, innerIndex);
	}

	return fp;
}

FCEUFILE* FCEUD_OpenArchiveIndex(ArchiveScanRecord& asr, std::string &fname, int innerIndex)
{
	int userCancel = 0;

	return FCEUD_OpenArchiveIndex( asr, fname, innerIndex, &userCancel );
}

// dummy functions

#define DUMMY(__f) \
    void __f(void) {\
        printf("%s\n", #__f);\
        FCEU_DispMessage("Not implemented.",0);\
    }
DUMMY(FCEUD_HideMenuToggle)
DUMMY(FCEUD_MovieReplayFrom)
//DUMMY(FCEUD_AviRecordTo)
//DUMMY(FCEUD_AviStop)
//void FCEUI_AviVideoUpdate(const unsigned char* buffer) { }
//bool FCEUI_AviIsRecording(void) {return false;}
void FCEUI_UseInputPreset(int preset) { }
bool FCEUD_PauseAfterPlayback() { return pauseAfterPlayback; }

int FCEUD_ShowStatusIcon(void)
{
	return showStatusIconOpt;
}
void FCEUD_ToggleStatusIcon(void)
{
	showStatusIconOpt = !showStatusIconOpt;
}

bool FCEUD_ShouldDrawInputAids(void)
{
	return drawInputAidsEnable;
}

void FCEUD_TurboOn (void) { turbo = true; };
void FCEUD_TurboOff   (void) { turbo = false; };
void FCEUD_TurboToggle(void) { turbo = !turbo; };

