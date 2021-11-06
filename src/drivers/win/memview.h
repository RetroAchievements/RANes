#ifndef MEMVIEW_H
#define MEMVIEW_H


void DoMemView();
void KillMemView();
void UpdateMemoryView(int draw_all);
void UpdateColorTable();
void ChangeMemViewFocus(int newEditingMode, int StartOffset,int EndOffset);
void UpdateCaption();

void ApplyPatch(int addr,int size, uint8* data);
void UndoLastPatch();

void SetHexEditorAddress(int gotoaddress);

int GetMaxSize(int editingMode);

extern HWND hMemView, hMemFind;
extern int EditingMode;

extern char* EditString[4];

struct COLORMENU {
	char* text;
	HBITMAP bitmap;
	int *r, *g, *b;
};
bool ChangeColor(HWND hwnd, COLORMENU* item);
BOOL OpColorMenu(HWND hwnd, HMENU menu, COLORMENU* item, int pos, int id, BOOL (WINAPI *opMenu)(HMENU hmenu, UINT item, BOOL byPos, LPCMENUITEMINFO info));
#define InsertColorMenu(hwnd, menu, item, pos, id) OpColorMenu(hwnd, menu, item, pos, id, InsertMenuItem)
#define ModifyColorMenu(hwnd, menu, item, pos, id) OpColorMenu(hwnd, menu, item, pos, id, SetMenuItemInfo)
#define GetHexColorMenu(hwnd) (GetSubMenu(GetSubMenu(GetMenu(hwnd), HIGHLIGHTING_SUBMENU_POS), HEXEDITOR_COLOR_SUBMENU_POS))
#define GetCdlColorMenu(hwnd) (GetSubMenu(GetSubMenu(GetMenu(hwnd), HIGHLIGHTING_SUBMENU_POS), CDLOGGER_COLOR_SUBMENU_POS))
#endif