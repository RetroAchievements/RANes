#include "retroachievements.h"

#include "fceu.h"

#include "drivers\win\common.h"

#include "RA_BuildVer.h"

int FDS_GameId = 0;

static void CauseUnpause()
{
}

static void CausePause()
{
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
}

static void LoadROM(const char* sFullPath)
{
}

void RA_Init()
{
	// initialize the DLL
	RA_Init(hAppWnd, RA_FCEUX, RANES_VERSION);
	RA_SetConsoleID(NES);

	// provide callbacks to the DLL
	RA_InstallSharedFunctions(NULL, CauseUnpause, CausePause, RebuildMenu, GetEstimatedGameTitle, ResetEmulator, LoadROM);

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
