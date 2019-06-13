#include "retroachievements.h"

#include "fceu.h"
#include "movie.h"
#include "cheat.h"

#include "drivers\win\cdlogger.h"
#include "drivers\win\cheat.h"
#include "drivers\win\common.h"
#include "drivers\win\debugger.h"
#include "drivers\win\memview.h"
#include "drivers\win\memwatch.h"
#include "drivers\win\ntview.h"
#include "drivers\win\ram_search.h"
#include "drivers\win\taseditor.h"
#include "drivers\win\taseditor\taseditor_project.h"
#include "drivers\win\tracer.h"

#include "RA_BuildVer.h"

extern HWND hPPUView; // not in ppuview.h
extern void KillPPUView(); // not in ppuview.h
extern HWND hGGConv; // not in cheat.h
extern HWND hNTView; // not in ntview.h
extern HWND LuaConsoleHWnd; // not in fcelua.h

int FDS_GameId = 0;

static bool GameIsActive()
{
	return true;
}

static void CauseUnpause()
{
	FCEUI_SetEmulationPaused(false);
}

static void CausePause()
{
	FCEUI_SetEmulationPaused(true);
}

static int GetMenuItemIndex(HMENU hMenu, const char* pItemName)
{
	int nIndex = 0;
	char pBuffer[256];

	while (nIndex < GetMenuItemCount(hMenu))
	{
		if (GetMenuString(hMenu, nIndex, pBuffer, sizeof(pBuffer)-1, MF_BYPOSITION))
		{
			if (!strcmp(pItemName, pBuffer))
				return nIndex;
		}
		nIndex++;
	}

	return -1;
}

static void RebuildMenu() 
{
	HMENU hMainMenu = GetMenu(hAppWnd);
	if (!hMainMenu) 
		return;

	// if RetroAchievements submenu exists, destroy it
	int index = GetMenuItemIndex(hMainMenu, "&RetroAchievements");
	if (index >= 0)
		DeleteMenu(hMainMenu, index, MF_BYPOSITION);

	// append RetroAchievements menu
	AppendMenu (hMainMenu, MF_POPUP|MF_STRING, (UINT_PTR)RA_CreatePopupMenu(), TEXT("&RetroAchievements"));

	// repaint
	DrawMenuBar(hAppWnd);
}

static void GetEstimatedGameTitle(char* sNameOut)
{
	const char* ptr = GameInfo->filename;
	if (ptr)
		_splitpath_s(ptr, NULL, 0, NULL, 0, sNameOut, 64, NULL, 0);
}

static void ResetEmulator()
{
	// close the TAS editor
	if (FCEUMOV_Mode(MOVIEMODE_TASEDITOR))
	{
		// make sure the project isn't marked as modified, or exitTASEditor can be canceled
		extern TASEDITOR_PROJECT project;
		project.reset();
		
		exitTASEditor();
	}	

	// make sure we're not in the middle of playing a movie
	FCEUI_StopMovie();

	// force all layers to be visible
	FCEUI_SetRenderPlanes(true, true);

	// close debug windows
	CloseMemoryWatch();
	CloseRamWindows();
	if (hDebug)
		DebuggerExit();
	if (hPPUView)
		KillPPUView();
	if (hNTView)
		KillNTView();
	if (hMemView)
		KillMemView();
	if (hTracer)
		SendMessage(hTracer, WM_CLOSE, NULL, NULL);
	if (hCDLogger)
		SendMessage(hCDLogger, WM_CLOSE, NULL, NULL);
	if (hGGConv)
		SendMessage(hGGConv, WM_CLOSE, NULL, NULL);
	if (LuaConsoleHWnd)
		PostMessage(LuaConsoleHWnd, WM_CLOSE, 0, 0);

	// disable any active cheats
	FCEU_FlushGameCheats(0, 1);

	// reset speed
	extern int32 fps_scale_unpaused;
	if (fps_scale_unpaused != 256)
		FCEUD_SetEmulationSpeed(EMUSPEED_NORMAL);
	FCEUI_SetEmulationPaused(0);

	// reset emulator
	FCEUI_ResetNES();
}

static void LoadROM(const char* sFullPath)
{
	FCEUI_LoadGame(sFullPath, 0);
}

unsigned char ByteReader(unsigned int nOffs)
{
	if (GameInfo)
		return static_cast<unsigned char>(ARead[nOffs](nOffs));

	return 0;
}

void ByteWriter(unsigned int nOffs, unsigned int nVal)
{
	if (GameInfo)
		BWrite[nOffs](nOffs, static_cast<uint8>(nVal));
}

void RA_Init()
{
	// initialize the DLL
	RA_Init(hAppWnd, RA_FCEUX, RANES_VERSION);
	RA_SetConsoleID(NES);

	// provide callbacks to the DLL
	RA_InstallSharedFunctions(GameIsActive, CauseUnpause, CausePause, RebuildMenu, GetEstimatedGameTitle, ResetEmulator, LoadROM);

	// register the system memory
	RA_ClearMemoryBanks();
	RA_InstallMemoryBank(0, ByteReader, ByteWriter, 0x10000);

	// add a placeholder menu item and start the login process - menu will be updated when login completes
	RebuildMenu();
	RA_AttemptLogin(true);

	// ensure titlebar text matches expected format
	RA_UpdateAppTitle("");
}

void RA_IdentifyAndActivateGame()
{
	if (GameInfo->type == EGIT::GIT_FDS)
	{
		RA_ActivateGame(FDS_GameId);
	}
	else
	{
		// The file has been split into several buffers. rather than try to piece
		// it back together, just reload it into a single buffer.
		std::string fullname;
		if (GameInfo->archiveFilename)
		{
			fullname.append(GameInfo->archiveFilename);
			fullname.push_back('|');
		}
		fullname.append(GameInfo->filename);

		FCEUFILE *fp = FCEU_fopen(fullname.c_str(), 0, "rb", 0);
		if (fp)
		{
			uint64 size = FCEU_fgetsize(fp);
			uint8* buffer = new uint8[size];
			FCEU_fread(buffer, 1, size, fp);
			if (memcmp(&buffer, "NES\x1a", 4)) // if a header is found, ignore it
				RA_OnLoadNewRom(&buffer[16], size - 16);
			else
				RA_OnLoadNewRom(buffer, size);
			delete[] buffer;

			FCEU_fclose(fp);
		}
	}
}
