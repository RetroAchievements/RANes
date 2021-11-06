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
// HexEditor.cpp
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include <SDL.h>
#include <QHeaderView>
#include <QScrollBar>
#include <QSpinBox>
#include <QPainter>
#include <QMenuBar>
#include <QFileDialog>
#include <QFontDialog>
#include <QColorDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QActionGroup>
#include <QSettings>

#include "../../types.h"
#include "../../fceu.h"
#include "../../cheat.h"
#include "../../debug.h"
#include "../../driver.h"
#include "../../version.h"
#include "../../movie.h"
#include "../../palette.h"
#include "../../fds.h"
#include "../../ppu.h"
#include "../../cart.h"
#include "../../ines.h"
#include "../common/configSys.h"

#include "Qt/main.h"
#include "Qt/dface.h"
#include "Qt/input.h"
#include "Qt/config.h"
#include "Qt/keyscan.h"
#include "Qt/fceuWrapper.h"
#include "Qt/HexEditor.h"
#include "Qt/CheatsConf.h"
#include "Qt/SymbolicDebug.h"
#include "Qt/ConsoleDebugger.h"
#include "Qt/ConsoleUtilities.h"
#include "Qt/ConsoleWindow.h"

static bool memNeedsCheck = false;
static HexBookMarkManager_t hbm;
static std::list <HexEditorDialog_t*> winList;
static const char *memViewNames[] = { "CPU", "PPU", "OAM", "ROM", NULL };

static int getROM( unsigned int offset);
static int writeMem( int mode, unsigned int addr, int value );
//----------------------------------------------------------------------------
struct  romEditEntry_t
{
	int       addr;
	int       size;
	uint8_t  *data;

	romEditEntry_t(void)
	{
		addr = -1; size = 0; data = NULL;
	}

	~romEditEntry_t(void)
	{
		if ( data != NULL )
		{
			free(data); data = NULL;
		}
	}
};

struct  romEditList_t
{
	uint8_t  *modMem;
	int       modMemSize;

	std::list <romEditEntry_t*> undoList;
	//std::list <romEditEntry_t*> redoList; // TODO

	romEditList_t(void)
	{
		modMem = NULL;
		modMemSize = 0;
	}

	~romEditList_t(void)
	{
		clear();
	}

	void clear(void)
	{
		while ( !undoList.empty() )
		{
			delete undoList.back();

			undoList.pop_back();
		}
		if ( modMem != NULL )
		{
			free(modMem); modMem = NULL; modMemSize = 0;
		}
	}	

	void applyPatch( int addr, int data )
	{
		uint8_t u8;

		u8 = data;

		applyPatch( addr, &u8, 1 );
	}

	void applyPatch( int addr, uint8_t *data, int size )
	{
		int ofs;
		romEditEntry_t *entry;

		if ( GameInfo == NULL )
		{
			return;
		}
		if ( modMem == NULL )
		{
			modMemSize = 16 + CHRsize[0] + PRGsize[0];

			modMem = (uint8_t*)malloc( modMemSize );

			if ( modMem == NULL )
			{
				printf("Error: Failed to allocate ROM modification memory buffer\n");
				return;
			}
			memset( modMem, 0, modMemSize );
		}
		entry = new romEditEntry_t();
		entry->addr = addr;
		entry->size = size;
		entry->data = (uint8_t*)malloc(sizeof(uint8_t)*size);

		for (int i = 0; i < size; i++)
		{
			ofs = addr+i;

			entry->data[i] = getROM(ofs);

			writeMem( QHexEdit::MODE_NES_ROM, ofs, data[i] );

			modMem[ofs]++;
		}
		undoList.push_back( entry );
	}

	int undoPatch(void)
	{
		int ofs, ret = -1;
		romEditEntry_t *entry;

		if ( undoList.empty() )
		{
			return ret;
		}
		entry = undoList.back();  undoList.pop_back();

		ret = entry->addr;

		for (int i=0; i<entry->size; i++)
		{
			ofs = entry->addr + i;

			writeMem( QHexEdit::MODE_NES_ROM, ofs, entry->data[i] );

			if ( modMem )
			{	
				if ( (ofs >= 0) && (ofs < modMemSize) )
				{
					if ( modMem[ofs] > 0 )
					{
						modMem[ofs]--;
					}
				}
			}
		}
		delete entry;

		return ret;
	}

	bool isModified( int addr )
	{
		if ( modMem == NULL )
		{
			return false;
		}
		if ( addr < 0 )
		{
			return false;
		}
		else if ( addr >= modMemSize )
		{
			return false;
		}
		return modMem[addr] ? true : false;
	}

	size_t undoQueueSize(void)
	{
		return undoList.size();
	}
};

static romEditList_t romEditList;
//----------------------------------------------------------------------------
static int getRAM( unsigned int i )
{
	return GetMem(i);
}
//----------------------------------------------------------------------------
static int getPPU( unsigned int i )
{
	if (GameInfo == NULL )
	{
		return 0;
	}
	i &= 0x3FFF;
	if (i < 0x2000)return VPage[(i) >> 10][(i)];
	//NSF PPU Viewer crash here (UGETAB) (Also disabled by 'MaxSize = 0x2000')
	if (GameInfo->type == GIT_NSF)
		return 0;
	else
	{
		if (i < 0x3F00)
			return vnapage[(i >> 10) & 0x3][i & 0x3FF];
		return READPAL_MOTHEROFALL(i & 0x1F);
	}
	return 0;
}
//----------------------------------------------------------------------------
static int getOAM( unsigned int i )
{
	return SPRAM[i & 0xFF];
}
//----------------------------------------------------------------------------
static int getROM( unsigned int offset)
{
	if (GameInfo == NULL )
	{
		return 0;
	}
	if (offset < 16)
	{
		return *((unsigned char *)&head+offset);
	}
	else if (offset < (16+PRGsize[0]) )
	{
		return PRGptr[0][offset-16];
	}
	else if (offset < (16+PRGsize[0]+CHRsize[0]) )
	{
		return CHRptr[0][offset-16-PRGsize[0]];
	}
	return -1;
}
//----------------------------------------------------------------------------
static void PalettePoke(uint32 addr, uint8 data)
{
	data = data & 0x3F;
	addr = addr & 0x1F;
	if ((addr & 3) == 0)
	{
		addr = (addr & 0xC) >> 2;
		if (addr == 0)
		{
			PALRAM[0x00] = PALRAM[0x04] = PALRAM[0x08] = PALRAM[0x0C] = data;
		}
		else
		{
			UPALRAM[addr-1] = data;
		}
	}
	else
	{
		PALRAM[addr] = data;
	}
}
//----------------------------------------------------------------------------
static int writeMem( int mode, unsigned int addr, int value )
{
	value = value & 0x000000ff;

	switch ( mode )
	{
		default:
      case QHexEdit::MODE_NES_RAM:
		{
			if ( addr < 0x8000 )
			{
				writefunc wfunc;
            
				wfunc = GetWriteHandler (addr);
            
				if (wfunc)
				{
					wfunc ((uint32) addr,
					       (uint8) (value & 0x000000ff));
				}
			}
			else
			{
				fprintf( stdout, "Error: Writing into RAM addresses >= 0x8000 is unsafe. Operation Denied.\n");
			}
		}
		break;
      case QHexEdit::MODE_NES_PPU:
		{
			addr &= 0x3FFF;
			if (addr < 0x2000)
			{
				VPage[addr >> 10][addr] = value; //todo: detect if this is vrom and turn it red if so
			}
			if ((addr >= 0x2000) && (addr < 0x3F00))
			{
				vnapage[(addr >> 10) & 0x3][addr & 0x3FF] = value; //todo: this causes 0x3000-0x3f00 to mirror 0x2000-0x2f00, is this correct?
			}
			if ((addr >= 0x3F00) && (addr < 0x3FFF))
			{
				PalettePoke(addr, value);
			}
		}
		break;
      case QHexEdit::MODE_NES_OAM:
		{
			addr &= 0xFF;
			SPRAM[addr] = value;
		}
		break;
      case QHexEdit::MODE_NES_ROM:
		{
			if (addr < 16)
			{
				fprintf( stdout, "You can't edit ROM header here, however you can use iNES Header Editor to edit the header if it's an iNES format file.");
			}
			else if ( (addr >= 16) && (addr < PRGsize[0]+16) )
			{
			  	*(uint8 *)(GetNesPRGPointer(addr-16)) = value;
			}
			else if ( (addr >= PRGsize[0]+16) && (addr < CHRsize[0]+PRGsize[0]+16) )
			{
				*(uint8 *)(GetNesCHRPointer(addr-16-PRGsize[0])) = value;
			}
		}
		break;
	}

	hexEditorRequestUpdateAll();

   return 0;
}
//----------------------------------------------------------------------------

static int convToXchar( int i )
{
   int c = 0;

	if ( (i >= 0) && (i < 10) )
	{
      c = i + '0';
	}
	else if ( i < 16 )
	{
		c = (i - 10) + 'A';
	}
	return c;
}
//----------------------------------------------------------------------------

static int convFromXchar( int i )
{
   int c = 0;

   i = ::toupper(i);

	if ( (i >= '0') && (i <= '9') )
	{
      c = i - '0';
	}
	else if ( (i >= 'A') && (i <= 'F') )
	{
		c = (i - 'A') + 10;
	}
	return c;
}

//----------------------------------------------------------------------------
memBlock_t::memBlock_t( void )
{
	buf = NULL;
	_size = 0;
	_maxLines = 0;
	memAccessFunc = NULL;
}
//----------------------------------------------------------------------------

memBlock_t::~memBlock_t(void)
{
	if ( buf != NULL )
	{
		::free( buf ); buf = NULL;
	}
	_size = 0;
   _maxLines = 0;
}

//----------------------------------------------------------------------------
int memBlock_t::reAlloc( int newSize )
{
	if ( newSize == 0 )
	{
		return 0;
	}
	if ( _size == newSize )
	{
		return 0;
	}

	if ( buf != NULL )
	{
		::free( buf ); buf = NULL;
	}
	_size = 0;
	_maxLines = 0;

	buf = (struct memByte_t *)malloc( newSize * sizeof(struct memByte_t) );

	if ( buf != NULL )
	{
		_size = newSize;
		init();

      if ( (_size % 16) )
	   {
	   	_maxLines = (_size / 16) + 1;
	   }
	   else
	   {
	   	_maxLines = (_size / 16);
	   }
	}
	return (buf == NULL);
}
//----------------------------------------------------------------------------
void memBlock_t::setAccessFunc( int (*newMemAccessFunc)( unsigned int offset) )
{
	memAccessFunc = newMemAccessFunc;
}
//----------------------------------------------------------------------------
void memBlock_t::init(void)
{
	for (int i=0; i<_size; i++)
	{
		buf[i].data  = memAccessFunc(i);
		buf[i].color = 0;
		buf[i].actv  = 0;
		//buf[i].draw  = 1;
	}
}
//----------------------------------------------------------------------------
HexBookMark::HexBookMark(void)
{
	addr = 0;
	mode = 0;
	desc[0] = 0;
}
//----------------------------------------------------------------------------
HexBookMark::~HexBookMark(void)
{

}
//----------------------------------------------------------------------------
HexBookMarkManager_t::HexBookMarkManager_t(void)
{

}
//----------------------------------------------------------------------------
HexBookMarkManager_t::~HexBookMarkManager_t(void)
{
	removeAll();
}
//----------------------------------------------------------------------------
void HexBookMarkManager_t::removeAll(void)
{
	HexBookMark *b;

	while ( !ls.empty() )
	{
		b = ls.front();

		delete b;

		ls.pop_front();
	}
	v.clear();
}
//----------------------------------------------------------------------------
int HexBookMarkManager_t::addBookMark( int addr, int mode, const char *desc )
{
	HexBookMark *b;

	b = new HexBookMark();
	b->addr = addr;
	b->mode = mode;

	if ( desc )
	{
		strncpy( b->desc, desc, 63 );
	}

	b->desc[63] = 0;

	ls.push_back(b);

	updateVector();

	return 0;
}
//----------------------------------------------------------------------------
void HexBookMarkManager_t::updateVector(void)
{
	std::list <HexBookMark*>::iterator it;

	v.clear();

	for (it=ls.begin(); it!=ls.end(); it++)
	{
		v.push_back( *it );
	}

	return;
}
//----------------------------------------------------------------------------
int HexBookMarkManager_t::size(void)
{
	return ls.size();
}
//----------------------------------------------------------------------------
HexBookMark *HexBookMarkManager_t::getBookMark( int index )
{
	if ( index < 0 )
	{
		return NULL;
	}
	else if ( index >= (int)v.size() )
	{
		return NULL;
	}
	return v[index];
}
//----------------------------------------------------------------------------
int HexBookMarkManager_t::loadFromFile(void)
{
	int i,j,mode,addr;
	FILE *fp;
	QDir dir;
	char line[256], fd[256], baseFile[512];
	const char *romFile = getRomFile();
	const char *baseDir = FCEUI_GetBaseDirectory();
	std::string path;

	if ( romFile == NULL )
	{
		return -1;
	}
	path = std::string(baseDir) + "/bookmarks/";

	dir.mkpath( QString::fromStdString(path) );

	getFileBaseName( romFile, baseFile );

	path += std::string(baseFile) + ".txt";

	fp = ::fopen( path.c_str(), "r");

	if ( fp == NULL )
	{
		return -1;
	}

	while ( ::fgets( line, sizeof(line), fp ) != NULL )
	{
		i=0; j=0;
		//printf("%s\n", line );
		//
		while ( isspace(line[i]) ) i++;

		while ( isalpha(line[i]) )
		{
			fd[j] = line[i]; i++; j++;
		}
		fd[j] = 0;

		mode = -1;

		for (j=0; j<4; j++)
		{
			if ( strcmp( fd, memViewNames[j] ) == 0 )
			{
				mode = j; break;
			}
		}
		if ( mode < 0 ) continue;

		while ( isspace(line[i]) ) i++;
		if ( line[i] == ':' ) i++;
		while ( isspace(line[i]) ) i++;

		j=0;
		while ( isxdigit(line[i]) )
		{
			fd[j] = line[i]; i++; j++;
		}
		fd[j] = 0;

		addr = strtol( fd, NULL, 16 );

		while ( isspace(line[i]) ) i++;
		if ( line[i] == ':' ) i++;
		while ( isspace(line[i]) ) i++;

		j=0;
		while ( line[i] ) 
		{
			fd[j] = line[i]; i++; j++;
		}
		fd[j] = 0;
		j--;

		while ( j >= 0 )
		{
			if ( isspace( fd[j] ) )
			{
				fd[j] = 0; 
			}
			else
			{
				break;
			}
			j--;
		}

		addBookMark( addr, mode, fd );
	}
	::fclose(fp);

	return 0;
}
//----------------------------------------------------------------------------
int HexBookMarkManager_t::saveToFile(void)
{
	FILE *fp;
	QDir dir;
	char baseFile[512];
	const char *romFile = getRomFile();
	const char *baseDir = FCEUI_GetBaseDirectory();
	std::string path;

	if ( romFile == NULL )
	{
		return -1;
	}
	path = std::string(baseDir) + "/bookmarks/";

	dir.mkpath( QString::fromStdString(path) );

	getFileBaseName( romFile, baseFile );

	path += std::string(baseFile) + ".txt";

	fp = ::fopen( path.c_str(), "w");

	if ( fp == NULL )
	{
		return -1;
	}

	for (int i=0; i<v.size(); i++)
	{
		fprintf( fp, "%s:%08X:%s\n", 
				memViewNames[ v[i]->mode ], v[i]->addr, v[i]->desc );
	}
	::fclose(fp);

	return 0;
}
//----------------------------------------------------------------------------
HexBookMarkMenuAction::HexBookMarkMenuAction(QString desc, QWidget *parent)
	: QAction( desc, parent )
{
	bm = NULL; qedit = NULL;
}
//----------------------------------------------------------------------------
HexBookMarkMenuAction::~HexBookMarkMenuAction(void)
{
	//printf("Hex Bookmark Menu Action Deleted\n");
}
//----------------------------------------------------------------------------
void HexBookMarkMenuAction::activateCB(void)
{
	//printf("Activate Bookmark: %p \n", bm );
	qedit->setMode( bm->mode );
	qedit->setAddr( bm->addr );
}
//----------------------------------------------------------------------------
HexEditorCharTable_t::HexEditorCharTable_t(void)
{
	extAsciiEnable  = true;
	customMapLoaded = false;

	resetAscii();
}
//----------------------------------------------------------------------------
HexEditorCharTable_t::~HexEditorCharTable_t(void)
{
}
//----------------------------------------------------------------------------
void HexEditorCharTable_t::resetAscii(void)
{
	for (int i=0; i<256; i++)
	{
		rmap[i] = i;

		if (i > 127)
		{	// Extended Ascii
			if ( extAsciiEnable )
			{
				map[i] = i;
			}
			else
			{
				map[i] = '.';
			}
		}
		else
		{       // Normal Ascii
			if ( isprint(i) )
			{
				map[i] = i;
			}
			else
			{
				map[i] = '.';
			}
		}
	}
	customMapLoaded = false;
}
//----------------------------------------------------------------------------
int HexEditorCharTable_t::loadFromFile( const char *filepath )
{
	int i,j,hexValue,mapValue,retVal, lineNum = 0;
	FILE *fp;
	char line[256];
	char tk[64];
	char errMsg[256];
	int  tmpMap[256];
	int  tmpMapR[256];

	retVal = 0;
	errMsg[0] = 0;

	fp = ::fopen( filepath, "r");

	if ( fp == NULL )
	{
		return -1;
	}
	for (i=0; i<256; i++)
	{
		tmpMap[i]  = -1;
		tmpMapR[i] = -1;
	}

	while ( fgets( line, sizeof(line), fp ) != 0 )
	{
		lineNum++;

		if ( retVal != 0 )
		{
			break;
		}
		//printf("%s\n", line);

		i=0;
		while ( isspace(line[i]) ) i++;

		if ( line[i] == '#' )
		{	// Comment line, skip it
			line[i] = 0; continue;
		}

		while ( isspace(line[i]) ) i++;

		j=0;
		while ( isxdigit(line[i]) )
		{
			tk[j] = line[i]; i++; j++;
		}
		tk[j] = 0;

		if ( j == 0 )
		{	// Line did not start with a hex character, skip it
			continue;
		}
		hexValue = strtol( tk, NULL, 16 );

		if ( hexValue > 255 )
		{
			sprintf( errMsg, "Error: Line %i: Hex Value 0x%X exceeds 0xFF \n", lineNum, hexValue );
			retVal = -1;
			continue;
		}

		while ( isspace(line[i]) ) i++;

		if ( line[i] != '=' )
		{
			sprintf( errMsg, "Error: Line %i: Expected assignment operator '=' but got '%c' \n", lineNum, line[i] );
			retVal = -1;
			continue;
		}
		i++;

		while ( isspace(line[i]) ) i++;

		mapValue = 0;

		if (line[i] == '0')
		{	// Hex Value Remap or '0'
			i++;
			if ( line[i] == 'x')
			{
				i++; j=0;
				while ( isxdigit(line[i]) )
				{
					tk[j] = line[i]; i++; j++;
				}
				tk[j] = 0;
				
				mapValue = strtol( tk, NULL, 16 );
			}
			else
			{
				mapValue = '0';
			}
		}
		else if (line[i] == '$')
		{	// Hex Value Remap or '$'
			i++;
			if ( isxdigit(line[i]) )
			{
				j=0;
				while ( isxdigit(line[i]) )
				{
					tk[j] = line[i]; i++; j++;
				}
				tk[j] = 0;
				
				mapValue = strtol( tk, NULL, 16 );
			}
			else
			{
				mapValue = '$';
			}
		}
		else
		{	// Hex to Character Mapping
			mapValue = line[i];
		}

		if ( mapValue > 255 )
		{
			sprintf( errMsg, "Error: Line %i: Map Value 0x%X exceeds 0xFF \n", lineNum, mapValue );
			retVal = -1;
			continue;
		}

		tmpMap[ hexValue ] = mapValue;

		if ( mapValue < 256 )
		{
			tmpMapR[ mapValue ] = hexValue;
		}
	}

	if ( retVal == 0 )
	{
		memcpy(  map, tmpMap , sizeof( map) );
		memcpy( rmap, tmpMapR, sizeof(rmap) );
		customMapLoaded = true;
	}
	else
	{
		if ( errMsg[0] != 0 )
		{
			if ( consoleWindow != NULL )
			{
				consoleWindow->QueueErrorMsgWindow(errMsg);
			}
		}
	}
	
	::fclose(fp);
	return retVal;
}
//----------------------------------------------------------------------------
HexEditorFindDialog_t::HexEditorFindDialog_t(QWidget *parent)
	: QDialog( parent )
{
	QVBoxLayout *mainLayout, *vbox;
	QHBoxLayout *hbox;
	QPushButton *nextBtn;
	QGroupBox   *dirGroup, *typeGroup;
	
	QDialog::setWindowTitle( tr("Find") );

	this->parent = (HexEditorDialog_t*)parent;

	mainLayout = new QVBoxLayout();
	hbox       = new QHBoxLayout();

	searchBox = new QLineEdit();
	nextBtn   = new QPushButton( tr("Find Next") );
	dirGroup  = new QGroupBox( tr("Direction") );
	typeGroup = new QGroupBox( tr("Type") );

	hbox->addWidget( new QLabel( tr("Find What:") ) );
	hbox->addWidget( searchBox );
	hbox->addWidget( nextBtn   );

	nextBtn->setDefault(true);

	mainLayout->addLayout( hbox );

	hbox   = new QHBoxLayout();
	hbox->addWidget( dirGroup  );
	hbox->addWidget( typeGroup );

	mainLayout->addLayout( hbox );

	vbox   = new QVBoxLayout();
	upBtn  = new QRadioButton( tr("Up") );
	dnBtn  = new QRadioButton( tr("Down") );

	dnBtn->setChecked(true);

	vbox->addWidget( upBtn );
	vbox->addWidget( dnBtn );

	dirGroup->setLayout( vbox );

	vbox   = new QVBoxLayout();
	hexBtn = new QRadioButton( tr("Hex") );
	txtBtn = new QRadioButton( tr("Text") );

	vbox->addWidget( hexBtn );
	vbox->addWidget( txtBtn );

	hexBtn->setChecked(true);

	typeGroup->setLayout( vbox );

	setLayout( mainLayout );

	connect( nextBtn, SIGNAL(clicked(void)), this, SLOT(runSearch(void)) );
}
//----------------------------------------------------------------------------
HexEditorFindDialog_t::~HexEditorFindDialog_t(void)
{
	parent->findDialog = NULL;
}
//----------------------------------------------------------------------------
void HexEditorFindDialog_t::closeEvent(QCloseEvent *event)
{
	printf("Hex Editor Close Window Event\n");
	done(0);
	deleteLater();
	event->accept();
}
//----------------------------------------------------------------------------
void HexEditorFindDialog_t::closeWindow(void)
{
	//printf("Close Window\n");
	done(0);
	deleteLater();
}
//----------------------------------------------------------------------------
void HexEditorFindDialog_t::runSearch(void)
{
	int i=0;
	unsigned char v;
	std::string s = searchBox->text().toStdString();
	std::vector <unsigned char> varray;

	if ( s.size() == 0 )
	{
		return;
	}
	//printf("Run Search: '%s'\n", s.c_str() );

	if ( hexBtn->isChecked() )
	{
		i=0;
		while ( s[i] != 0 )
		{
			while ( isspace(s[i]) ) i++;
			v = 0;

			if ( isxdigit(s[i]) )
			{
				v = convFromXchar(s[i]) << 4; i++;
			}
			else 
			{
				return;
			}

			if ( isxdigit(s[i]) )
			{
				v |= convFromXchar(s[i]); i++;
			}
			else 
			{
				return;
			}
			varray.push_back(v);

			while ( isspace(s[i]) ) i++;
		}
	}
	else
	{
		i=0;
		while ( s[i] != 0 )
		{
			v = s[i];
			varray.push_back(v);
			i++;
		}
	}
	fceuWrapperLock();
	parent->editor->findPattern( varray, upBtn->isChecked() );
	fceuWrapperUnLock();
}
//----------------------------------------------------------------------------
HexEditorDialog_t::HexEditorDialog_t(QWidget *parent)
	: QDialog( parent, Qt::Window )
{
	//QVBoxLayout *mainLayout;
	QGridLayout *grid;
	QMenuBar *menuBar;
	QMenu *fileMenu, *editMenu, *viewMenu, *colorMenu, *subMenu;
	QAction *saveROM, *closeAct;
	QAction *act, *actHlgt, *actHlgtRV;
	ColorMenuItem *actColorFG, *actColorBG, *actRowColColor, *actAltColColor;
	QActionGroup *group;
	int opt, useNativeMenuBar, refreshRateOpt;
	QSettings settings;

	QDialog::setWindowTitle( tr("Hex Editor") );

	resize( 512, 512 );

	menuBar = new QMenuBar(this);

	grid   = new QGridLayout(this);
	editor = new QHexEdit(this);
	vbar   = new QScrollBar( Qt::Vertical, this );
	hbar   = new QScrollBar( Qt::Horizontal, this );

	// This is needed for menu bar to show up on MacOS
	g_config->getOption( "SDL.UseNativeMenuBar", &useNativeMenuBar );

	menuBar->setNativeMenuBar( useNativeMenuBar ? true : false );
	//-----------------------------------------------------------------------
	// Menu 
	//-----------------------------------------------------------------------
	// File
	fileMenu = menuBar->addMenu(tr("&File"));

	// File -> Save ROM
	saveROM = new QAction(tr("&Save ROM"), this);
	saveROM->setShortcut(QKeySequence::Save);
	saveROM->setStatusTip(tr("Save ROM File"));
	connect(saveROM, SIGNAL(triggered()), this, SLOT(saveRomFile(void)) );
	
	fileMenu->addAction(saveROM);

	// File -> Save ROM As
	saveROM = new QAction(tr("Save ROM &As"), this);
	saveROM->setShortcut(QKeySequence::SaveAs);
	saveROM->setStatusTip(tr("Save ROM File As"));
	connect(saveROM, SIGNAL(triggered()), this, SLOT(saveRomFileAs(void)) );
	
	fileMenu->addAction(saveROM);

	// File -> Load Table
	loadTableAct = new QAction(tr("&Load TBL File"), this);
	//loadTableAct->setShortcut(QKeySequence::Open);
	loadTableAct->setStatusTip(tr("Load Table from File"));
	connect(loadTableAct, SIGNAL(triggered()), this, SLOT(loadTableFromFile(void)) );
	
	fileMenu->addAction(loadTableAct);

	// File -> Unload Table
	unloadTableAct = new QAction(tr("&Unload TBL File"), this);
	//unloadTableAct->setShortcut(QKeySequence::Open);
	unloadTableAct->setStatusTip(tr("Unload Table"));
	connect(unloadTableAct, SIGNAL(triggered()), this, SLOT(unloadTable(void)) );
	
	fileMenu->addAction(unloadTableAct);

	// File -> Goto Address
	gotoAddrAct = new QAction(tr("&Goto Address"), this);
	gotoAddrAct->setShortcut(QKeySequence(tr("Ctrl+A")));
	gotoAddrAct->setStatusTip(tr("Goto Address"));
	connect(gotoAddrAct, SIGNAL(triggered()), this, SLOT(openGotoAddrDialog(void)) );

	fileMenu->addAction(gotoAddrAct);

	fileMenu->addSeparator();

	// File -> Close
	closeAct = new QAction(tr("&Close"), this);
	closeAct->setShortcut(QKeySequence::Close);
	closeAct->setStatusTip(tr("Close Window"));
	connect(closeAct, SIGNAL(triggered()), this, SLOT(closeWindow(void)) );
	
	fileMenu->addAction(closeAct);

	// Edit
	editMenu = menuBar->addMenu(tr("&Edit"));

	// Edit -> Undo
	undoEditAct = new QAction(tr("&Undo"), this);
	undoEditAct->setShortcut(QKeySequence::Undo);
	undoEditAct->setStatusTip(tr("Undo Edit"));
	undoEditAct->setEnabled(false);
	connect(undoEditAct, SIGNAL(triggered()), this, SLOT(undoRomPatch(void)) );
	
	editMenu->addAction(undoEditAct);
	editMenu->addSeparator();

	// Edit -> Copy
	act = new QAction(tr("&Copy"), this);
	act->setShortcut(QKeySequence(tr("Ctrl+C")));
	act->setStatusTip(tr("Copy"));
	connect(act, SIGNAL(triggered()), this, SLOT(copyToClipboard(void)) );
	
	editMenu->addAction(act);

	// Edit -> Paste
	act = new QAction(tr("&Paste"), this);
	act->setShortcut(QKeySequence(tr("Ctrl+V")));
	act->setStatusTip(tr("Paste"));
	connect(act, SIGNAL(triggered()), this, SLOT(pasteFromClipboard(void)) );
	
	editMenu->addAction(act);
	editMenu->addSeparator();

	// Edit -> Find
	act = new QAction(tr("&Find"), this);
	act->setShortcut(QKeySequence(tr("Ctrl+F")));
	act->setStatusTip(tr("Find"));
	connect(act, SIGNAL(triggered()), this, SLOT(openFindDialog(void)) );

	editMenu->addAction(act);

	// View
	viewMenu = menuBar->addMenu(tr("&View"));

	group   = new QActionGroup(this);

	group->setExclusive(true);

	// View -> RAM
	viewRAM = new QAction(tr("&CPU"), this);
	//viewRAM->setShortcuts(QKeySequence::Open);
	viewRAM->setStatusTip(tr("View CPU"));
	viewRAM->setCheckable(true);
	connect(viewRAM, SIGNAL(triggered()), this, SLOT(setViewRAM(void)) );

	group->addAction(viewRAM);
	viewMenu->addAction(viewRAM);

	// View -> PPU
	viewPPU = new QAction(tr("&PPU"), this);
	//viewPPU->setShortcuts(QKeySequence::Open);
	viewPPU->setStatusTip(tr("View PPU"));
	viewPPU->setCheckable(true);
	connect(viewPPU, SIGNAL(triggered()), this, SLOT(setViewPPU(void)) );

	group->addAction(viewPPU);
	viewMenu->addAction(viewPPU);

	// View -> OAM
	viewOAM = new QAction(tr("&OAM"), this);
	//viewOAM->setShortcuts(QKeySequence::Open);
	viewOAM->setStatusTip(tr("View OAM"));
	viewOAM->setCheckable(true);
	connect(viewOAM, SIGNAL(triggered()), this, SLOT(setViewOAM(void)) );

	group->addAction(viewOAM);
	viewMenu->addAction(viewOAM);

	// View -> ROM
	viewROM = new QAction(tr("&ROM"), this);
	//viewROM->setShortcuts(QKeySequence::Open);
	viewROM->setStatusTip(tr("View ROM"));
	viewROM->setCheckable(true);
	connect(viewROM, SIGNAL(triggered()), this, SLOT(setViewROM(void)) );

	group->addAction(viewROM);
	viewMenu->addAction(viewROM);

	viewRAM->setChecked(true); // Set default view

	viewMenu->addSeparator();

	// View -> Font
	act = new QAction(tr("Set &Font..."), this);
	//act->setShortcuts(QKeySequence::Open);
	act->setStatusTip(tr("Change Font"));
	//act->setCheckable(true);
	connect(act, SIGNAL(triggered()), this, SLOT(changeFontRequest(void)) );

	viewMenu->addAction(act);

	viewMenu->addSeparator();

	// View -> Refresh Rate
	subMenu = viewMenu->addMenu( tr("Re&fresh Rate") );
	group   = new QActionGroup(this);

	group->setExclusive(true);

	g_config->getOption("SDL.HexEditRefreshRate", &refreshRateOpt);

	// View -> Refresh Rate -> 5 Hz
	act = new QAction(tr("5 Hz"), this);
	act->setCheckable(true);
	act->setChecked( refreshRateOpt == 5 );
	connect(act, SIGNAL(triggered()), this, SLOT(setViewRefresh5Hz(void)) );

	group->addAction(act);
	subMenu->addAction(act);

	// View -> Refresh Rate -> 10 Hz
	act = new QAction(tr("10 Hz"), this);
	act->setCheckable(true);
	act->setChecked( refreshRateOpt == 10 );
	connect(act, SIGNAL(triggered()), this, SLOT(setViewRefresh10Hz(void)) );

	group->addAction(act);
	subMenu->addAction(act);

	// View -> Refresh Rate -> 20 Hz
	act = new QAction(tr("20 Hz"), this);
	act->setCheckable(true);
	act->setChecked( refreshRateOpt == 20 );
	connect(act, SIGNAL(triggered()), this, SLOT(setViewRefresh20Hz(void)) );

	group->addAction(act);
	subMenu->addAction(act);

	// View -> Refresh Rate -> 30 Hz
	act = new QAction(tr("30 Hz"), this);
	act->setCheckable(true);
	act->setChecked( refreshRateOpt == 30 );
	connect(act, SIGNAL(triggered()), this, SLOT(setViewRefresh30Hz(void)) );

	group->addAction(act);
	subMenu->addAction(act);

	// View -> Refresh Rate -> 60 Hz
	act = new QAction(tr("60 Hz"), this);
	act->setCheckable(true);
	act->setChecked( refreshRateOpt == 60 );
	connect(act, SIGNAL(triggered()), this, SLOT(setViewRefresh60Hz(void)) );

	group->addAction(act);
	subMenu->addAction(act);

	// Color Menu
	colorMenu = menuBar->addMenu(tr("&Color"));

	// Color -> Highlight Activity
	g_config->getOption("SDL.HexEditActivityHlgt", &opt);
	editor->setHighlightActivity( opt );

	actHlgt = new QAction(tr("Highlight &Activity"), this);
	//actHlgt->setShortcuts(QKeySequence::Open);
	actHlgt->setStatusTip(tr("Highlight Activity"));
	actHlgt->setCheckable(true);
	actHlgt->setChecked(opt);
	connect(actHlgt, SIGNAL(triggered(bool)), this, SLOT(actvHighlightCB(bool)) );

	colorMenu->addAction(actHlgt);

	// Color -> Highlight Reverse Video
	g_config->getOption("SDL.HexEditReverseVideo", &opt);
	editor->setHighlightReverseVideo( opt );

	actHlgtRV = new QAction(tr("Highlight &Reverse Video"), this);
	//actHlgtRV->setShortcuts(QKeySequence::Open);
	actHlgtRV->setStatusTip(tr("Highlight Reverse Video"));
	actHlgtRV->setCheckable(true);
	actHlgtRV->setChecked(opt);
	connect(actHlgtRV, SIGNAL(triggered(bool)), this, SLOT(actvHighlightRVCB(bool)) );

	colorMenu->addAction(actHlgtRV);

	// Color -> Highlight Reverse Video
	g_config->getOption("SDL.HexEditRowColumnHlgt", &opt);
	editor->setRowColHlgtEna( opt );

	rolColHlgtAct = new QAction(tr("Highlight &Cursor Row/Column"), this);
	//rolColHlgtAct->setShortcuts(QKeySequence::Open);
	rolColHlgtAct->setStatusTip(tr("Highlight Cursor Row/Column"));
	rolColHlgtAct->setCheckable(true);
	rolColHlgtAct->setChecked(opt);
	connect(rolColHlgtAct, SIGNAL(triggered(bool)), this, SLOT(rolColHlgtChanged(bool)) );

	colorMenu->addAction(rolColHlgtAct);

	// Color -> Highlight Reverse Video
	g_config->getOption("SDL.HexEditAltnColumnColor", &opt);
	editor->setAltColHlgtEna( opt );

	altColHlgtAct = new QAction(tr("&Alternating Column Colors"), this);
	//altColHlgtAct->setShortcuts(QKeySequence::Open);
	altColHlgtAct->setStatusTip(tr("&Alternating Column Colors"));
	altColHlgtAct->setCheckable(true);
	altColHlgtAct->setChecked(opt);
	connect(altColHlgtAct, SIGNAL(triggered(bool)), this, SLOT(altColHlgtChanged(bool)) );

	colorMenu->addAction(altColHlgtAct);

	colorMenu->addSeparator();

	// Color -> ForeGround Color
	actColorFG = new ColorMenuItem( tr("&ForeGround Color"), "SDL.HexEditFgColor", this);
	
	colorMenu->addAction(actColorFG);

	// Color -> BackGround Color
	actColorBG = new ColorMenuItem( tr("&BackGround Color"), "SDL.HexEditBgColor", this);
	
	colorMenu->addAction(actColorBG);

	// Color -> Cursor Row/Column Color
	actRowColColor = new ColorMenuItem( tr("&Cursor Row/Column Color"), "SDL.HexEditCursorColorRC", this);
	
	colorMenu->addAction(actRowColColor);

	// Color -> Alternate Column Color
	actAltColColor = new ColorMenuItem( tr("&Alternate Column Color"), "SDL.HexEditAltColColor", this);
	
	colorMenu->addAction(actAltColColor);

	// Bookmarks Menu
	bookmarkMenu = menuBar->addMenu(tr("&Bookmarks"));

	//-----------------------------------------------------------------------
	// Menu End 
	//-----------------------------------------------------------------------
	//mainLayout = new QVBoxLayout();


	grid->setMenuBar( menuBar );

	grid->addWidget( editor, 0, 0 );
	grid->addWidget( vbar  , 0, 1 );
	grid->addWidget( hbar  , 1, 0 );
	//mainLayout->addLayout( grid );

	setLayout( grid );

	hbar->setMinimum(0);
	hbar->setMaximum(100);
	vbar->setMinimum(0);
	vbar->setMaximum( 0x1000 / 16 );

	editor->setScrollBars( hbar, vbar );
	
	   actColorFG->connectColor( &editor->fgColor );
	   actColorBG->connectColor( &editor->bgColor );
	actRowColColor->connectColor( &editor->rowColHlgtColor );
	actAltColColor->connectColor( &editor->altColHlgtColor );

	//connect( vbar, SIGNAL(sliderMoved(int)), this, SLOT(vbarMoved(int)) );
	connect( hbar, SIGNAL(valueChanged(int)), this, SLOT(hbarChanged(int)) );
	connect( vbar, SIGNAL(valueChanged(int)), this, SLOT(vbarChanged(int)) );

	findDialog = NULL;

	editor->memModeUpdate();

	periodicTimer  = new QTimer( this );

	connect( periodicTimer, &QTimer::timeout, this, &HexEditorDialog_t::updatePeriodic );

	//printf("Refresh Rate: %i\n", 1000 / refreshRateOpt );
	periodicTimer->start( 1000 / refreshRateOpt  );

	// Lock the mutex before adding a new window to the list,
	// we want to be sure that the emulator is not iterating the list
	// when we change it.
	fceuWrapperLock();
	winList.push_back(this);
	fceuWrapperUnLock();

	populateBookmarkMenu();

	FCEUI_CreateCheatMap();

	unloadTableAct->setEnabled( editor->charTable.customMapLoaded );

	restoreGeometry(settings.value("hexEditor/geometry").toByteArray());
}
//----------------------------------------------------------------------------
HexEditorDialog_t::~HexEditorDialog_t(void)
{
	std::list <HexEditorDialog_t*>::iterator it;
	  
	//printf("Hex Editor Deleted\n");
	periodicTimer->stop();

	// Lock the emulation thread mutex to ensure
	// that the emulator is not attempting to update memory values
	// for window while we are destroying it or editing the window list.
	fceuWrapperLock();

	for (it = winList.begin(); it != winList.end(); it++)
	{
		if ( (*it) == this )
		{
			winList.erase(it);
			//printf("Removing Hex Edit Window\n");
			break;
		}
	}
	fceuWrapperUnLock();
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::setWindowTitle(void)
{
	const char *modeString;
	char stmp[128];

	modeString = memViewNames[ editor->getMode() ];

	sprintf( stmp, "Hex Editor - %s: 0x%04X", modeString, editor->getAddr() );

	QDialog::setWindowTitle( tr(stmp) );

}
//----------------------------------------------------------------------------
void HexEditorDialog_t::removeAllBookmarks(void)
{
	int ret;
	QMessageBox mbox(this);

	mbox.setWindowTitle( tr("Bookmarks") );
	mbox.setText( tr("Remove All Bookmarks?") );
	mbox.setIcon( QMessageBox::Question );
	mbox.setStandardButtons( QMessageBox::Cancel | QMessageBox::Ok );

	ret = mbox.exec();

	//printf("Ret: %i \n", ret );
	if ( ret == QMessageBox::Ok )
	{
		hbm.removeAll();

		populateBookmarkMenu();
	}
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::populateBookmarkMenu(void)
{
	QAction *act;
	HexBookMarkMenuAction *hAct;

	bookmarkMenu->clear();

	// Bookmarks -> Remove All Bookmarks
	act = new QAction(tr("&Remove All Bookmarks"), bookmarkMenu);
	//act->setShortcuts(QKeySequence::Open);
	act->setStatusTip(tr("Remove All Bookmarks"));
	connect(act, SIGNAL(triggered(void)), this, SLOT(removeAllBookmarks(void)) );
	
	bookmarkMenu->addAction(act);
	bookmarkMenu->addSeparator();

	for (int i=0; i<hbm.size(); i++)
	{
		HexBookMark *b = hbm.getBookMark(i);

		if ( b )
		{
			//printf("%p  %p  \n", b, editor );
			hAct = new HexBookMarkMenuAction(tr(b->desc), bookmarkMenu);
			bookmarkMenu->addAction(hAct);
			hAct->bm = b; hAct->qedit = editor;
			connect(hAct, SIGNAL(triggered(void)), hAct, SLOT(activateCB(void)) );
		}
	}
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::closeEvent(QCloseEvent *event)
{
	QSettings settings;
	//printf("Hex Editor Close Window Event\n");
	settings.setValue("hexEditor/geometry", saveGeometry());
	done(0);
	deleteLater();
	event->accept();
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::closeWindow(void)
{
	QSettings settings;
	//printf("Close Window\n");
	settings.setValue("hexEditor/geometry", saveGeometry());
	done(0);
	deleteLater();
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::vbarMoved(int value)
{
	//printf("VBar Moved: %i\n", value);
	editor->setLine( value );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::vbarChanged(int value)
{
	//printf("VBar Changed: %i\n", value);
	editor->setLine( value );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::hbarChanged(int value)
{
	//printf("HBar Changed: %i\n", value);
	editor->setHorzScroll( value );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::gotoAddress( int newAddr )
{
	editor->setAddr( newAddr );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::saveRomFile(void)
{
	romEditList.clear();
	iNesSave();
	//UpdateColorTable();
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::saveRomFileAs(void)
{
	int ret, useNativeFileDialogVal;
	QString filename;
	QFileDialog  dialog(this, tr("Save ROM To File") );

	dialog.setFileMode(QFileDialog::AnyFile);

	dialog.setNameFilter(tr("NES Files (*.nes *.NES) ;; All files (*)"));

	dialog.setViewMode(QFileDialog::List);
	dialog.setFilter( QDir::AllEntries | QDir::AllDirs | QDir::Hidden );
	dialog.setLabelText( QFileDialog::Accept, tr("Save") );
	dialog.setDefaultSuffix( tr(".nes") );

	// Check config option to use native file dialog or not
	g_config->getOption ("SDL.UseNativeFileDialog", &useNativeFileDialogVal);

	dialog.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogVal);

	ret = dialog.exec();

	if ( ret )
	{
		QStringList fileList;
		fileList = dialog.selectedFiles();

		if ( fileList.size() > 0 )
		{
			filename = fileList[0];
		}
	}

	if ( filename.isNull() )
	{
	   return;
	}
	qDebug() << "selected file path : " << filename.toUtf8();

	iNesSaveAs( filename.toStdString().c_str() );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::loadTableFromFile(void)
{
	int ret, useNativeFileDialogVal;
	QString filename;
	QFileDialog  dialog(this, tr("Load Table From File") );

	dialog.setFileMode(QFileDialog::ExistingFile);

	dialog.setNameFilter(tr("TBL Files (*.tbl *.TBL) ;; All files (*)"));

	dialog.setViewMode(QFileDialog::List);
	dialog.setFilter( QDir::AllEntries | QDir::AllDirs | QDir::Hidden );
	dialog.setLabelText( QFileDialog::Accept, tr("Load") );
	dialog.setDefaultSuffix( tr(".tbl") );

	// Check config option to use native file dialog or not
	g_config->getOption ("SDL.UseNativeFileDialog", &useNativeFileDialogVal);

	dialog.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogVal);

	ret = dialog.exec();

	if ( ret )
	{
		QStringList fileList;
		fileList = dialog.selectedFiles();

		if ( fileList.size() > 0 )
		{
			filename = fileList[0];
		}
	}

	if ( filename.isNull() )
	{
	   return;
	}
	qDebug() << "selected file path : " << filename.toUtf8();

	editor->charTable.loadFromFile( filename.toStdString().c_str() );

	unloadTableAct->setEnabled( editor->charTable.customMapLoaded );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::unloadTable(void)
{
	editor->charTable.resetAscii();

	unloadTableAct->setEnabled( editor->charTable.customMapLoaded );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::setViewRAM(void)
{
	editor->setMode( QHexEdit::MODE_NES_RAM );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::setViewPPU(void)
{
	editor->setMode( QHexEdit::MODE_NES_PPU );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::setViewOAM(void)
{
	editor->setMode( QHexEdit::MODE_NES_OAM );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::setViewROM(void)
{
	editor->setMode( QHexEdit::MODE_NES_ROM );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::changeFontRequest(void)
{
	editor->changeFontRequest();
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::setViewRefresh5Hz(void)
{
	g_config->setOption("SDL.HexEditRefreshRate", 5);
	periodicTimer->stop();
	periodicTimer->start(200);
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::setViewRefresh10Hz(void)
{
	g_config->setOption("SDL.HexEditRefreshRate", 10);
	periodicTimer->stop();
	periodicTimer->start(100);
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::setViewRefresh20Hz(void)
{
	g_config->setOption("SDL.HexEditRefreshRate", 20);
	periodicTimer->stop();
	periodicTimer->start(50);
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::setViewRefresh30Hz(void)
{
	g_config->setOption("SDL.HexEditRefreshRate", 30);
	periodicTimer->stop();
	periodicTimer->start(33);
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::setViewRefresh60Hz(void)
{
	g_config->setOption("SDL.HexEditRefreshRate", 60);
	periodicTimer->stop();
	periodicTimer->start(16);
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::actvHighlightCB(bool enable)
{
	g_config->setOption("SDL.HexEditActivityHlgt", enable);

	//printf("Highlight: %i \n", enable );
	editor->setHighlightActivity( enable );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::actvHighlightRVCB(bool enable)
{
	g_config->setOption("SDL.HexEditReverseVideo", enable);

	//printf("Highlight: %i \n", enable );
	editor->setHighlightReverseVideo( enable );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::rolColHlgtChanged(bool enable)
{
	g_config->setOption("SDL.HexEditRowColumnHlgt", enable);

	//printf("Highlight: %i \n", enable );
	editor->setRowColHlgtEna( enable );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::altColHlgtChanged(bool enable)
{
	g_config->setOption("SDL.HexEditAltnColumnColor", enable);

	//printf("Highlight: %i \n", enable );
	editor->setAltColHlgtEna( enable );
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::openDebugSymbolEditWindow( int addr )
{
	int ret, bank;
	debugSymbol_t *sym;
	SymbolEditWindow win(this);

	if ( addr < 0x8000 )
	{
	   bank = -1;
	}
	else
	{
	   bank = getBank( addr );
	}

  	sym = debugSymbolTable.getSymbolAtBankOffset( bank, addr );

	win.setAddr( addr );

	win.setSym( sym );

	ret = win.exec();

	if ( ret == QDialog::Accepted )
	{
		updateAllDebuggerWindows();
	}
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::updatePeriodic(void)
{
	//printf("Update Periodic\n");
	
	undoEditAct->setEnabled( romEditList.undoQueueSize() > 0 );

	if ( fceuWrapperTryLock(0) )
	{
		memNeedsCheck = false;

		editor->checkMemActivity();

		fceuWrapperUnLock();
	}
	else
	{
		memNeedsCheck = true;
	}

	editor->memModeUpdate();

	editor->update();

	setWindowTitle();

	switch ( editor->getMode() )
	{
		case QHexEdit::MODE_NES_RAM:
			if ( !viewRAM->isChecked() )
			{
				viewRAM->setChecked(true);
			}
		break;
		case QHexEdit::MODE_NES_PPU:
			if ( !viewPPU->isChecked() )
			{
				viewPPU->setChecked(true);
			}
		break;
		case QHexEdit::MODE_NES_OAM:
			if ( !viewOAM->isChecked() )
			{
				viewOAM->setChecked(true);
			}
		break;
		case QHexEdit::MODE_NES_ROM:
			if ( !viewROM->isChecked() )
			{
				viewROM->setChecked(true);
			}
		break;
	}
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::undoRomPatch(void)
{
	int addr = romEditList.undoPatch();

	if ( addr >= 0 )
	{
		editor->setMode( QHexEdit::MODE_NES_ROM );
		editor->setAddr( addr );
	}
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::openFindDialog(void)
{
	if ( findDialog == NULL )
	{
		findDialog = new HexEditorFindDialog_t(this);

		findDialog->show();
	}
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::openGotoAddrDialog(void)
{
   editor->openGotoAddrDialog();
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::copyToClipboard(void)
{
   editor->loadHighlightToClipboard();
}
//----------------------------------------------------------------------------
void HexEditorDialog_t::pasteFromClipboard(void)
{
   editor->pasteFromClipboard();
}
//----------------------------------------------------------------------------
QHexEdit::QHexEdit(QWidget *parent)
	: QWidget( parent )
{
	QPalette pal;
	std::string fontString, colorString;

	this->parent = (HexEditorDialog_t*)parent;
	this->setFocusPolicy(Qt::StrongFocus);

	g_config->getOption("SDL.HexEditFont", &fontString);

	if ( fontString.size() > 0 )
	{
		font.fromString( QString::fromStdString( fontString ) );
	}
	else
	{
		font.setFamily("Courier New");
		font.setStyle( QFont::StyleNormal );
		font.setStyleHint( QFont::Monospace );
	}

	g_config->getOption("SDL.HexEditBgColor", &colorString);
	bgColor.setNamedColor( colorString.c_str() );
	g_config->getOption("SDL.HexEditFgColor", &colorString);
	fgColor.setNamedColor( colorString.c_str() );

	g_config->getOption("SDL.HexEditCursorColorRC", &colorString);
	rowColHlgtColor.setNamedColor( colorString.c_str() );
	rolColHlgtEna   = false;

	g_config->getOption("SDL.HexEditAltColColor", &colorString);
	altColHlgtColor.setNamedColor( colorString.c_str() );
	altColHlgtEna   = false;

	pal = this->palette();
	pal.setColor(QPalette::Base      , bgColor );
	pal.setColor(QPalette::Window    , bgColor );
	pal.setColor(QPalette::WindowText, fgColor );

	this->setPalette(pal);

	calcFontData();

	setMinimumWidth( pxLineWidth );

	memAccessFunc = getRAM;
	viewMode    = MODE_NES_RAM;
	lineOffset  = 0;
	cursorPosX  = 0;
	cursorPosY  = 0;
	cursorAddr  = 0;
	cursorBlink = true;
	cursorBlinkCount = 0;
	maxLineOffset = 0;
	editAddr  = -1;
	editValue =  0;
	editMask  =  0;
	reverseVideo = true;
	actvHighlightEnable = true;
	total_instructions_lp = 0;
	pxLineXScroll = 0;

	frzRamAddr = -1;
	frzRamVal = 0;
	frzRamMode = 0;
	frzIdx = 0;

	wheelPixelCounter = 0;

	highLightColor[ 0].setRgb( 0x00, 0x00, 0x00 );
	highLightColor[ 1].setRgb( 0x35, 0x40, 0x00 );
	highLightColor[ 2].setRgb( 0x18, 0x52, 0x18 );
	highLightColor[ 3].setRgb( 0x34, 0x5C, 0x5E );
	highLightColor[ 4].setRgb( 0x00, 0x4C, 0x80 );
	highLightColor[ 5].setRgb( 0x00, 0x03, 0xBA );
	highLightColor[ 6].setRgb( 0x38, 0x00, 0xD1 );
	highLightColor[ 7].setRgb( 0x72, 0x12, 0xB2 );
	highLightColor[ 8].setRgb( 0xAB, 0x00, 0xBA );
	highLightColor[ 9].setRgb( 0xB0, 0x00, 0x6F );
	highLightColor[10].setRgb( 0xC2, 0x00, 0x37 );
	highLightColor[11].setRgb( 0xBA, 0x0C, 0x00 );
	highLightColor[12].setRgb( 0xC9, 0x2C, 0x00 );
	highLightColor[13].setRgb( 0xBF, 0x53, 0x00 );
	highLightColor[14].setRgb( 0xCF, 0x72, 0x00 );
	highLightColor[15].setRgb( 0xC7, 0x8B, 0x3C );

	for (int i=0; i<HIGHLIGHT_ACTIVITY_NUM_COLORS; i++)
	{
		float red, green, blue, avg, grayScale;

		red   = highLightColor[i].redF();
		green = highLightColor[i].greenF();
		blue  = highLightColor[i].blueF();

		avg = (red + green + blue) / 3.0;

		if ( avg >= 0.5 )
		{
			grayScale = 0.0;
		}
		else
		{
			grayScale = 1.0;
		}
		rvActvTextColor[i].setRgbF( grayScale, grayScale, grayScale );
	}

	updateRequested = false;
	mouseLeftBtnDown = false;

	txtHlgtAnchorChar = -1;
	txtHlgtAnchorLine = -1;
	txtHlgtStartChar = -1;
	txtHlgtStartLine = -1;
	txtHlgtStartAddr = -1;
	txtHlgtEndChar = -1;
	txtHlgtEndLine = -1;
	txtHlgtEndAddr = -1;

	clipboard = QGuiApplication::clipboard();
}
//----------------------------------------------------------------------------
QHexEdit::~QHexEdit(void)
{

}
//----------------------------------------------------------------------------
void QHexEdit::calcFontData(void)
{
	 this->setFont(font);
    QFontMetrics metrics(font);
#if QT_VERSION > QT_VERSION_CHECK(5, 11, 0)
    pxCharWidth = metrics.horizontalAdvance(QLatin1Char('2'));
#else
    pxCharWidth = metrics.width(QLatin1Char('2'));
#endif
	pxCharHeight  = metrics.height();
	pxLineSpacing = metrics.lineSpacing() * 1.25;
	pxLineLead    = pxLineSpacing - pxCharHeight;
	pxXoffset     = pxCharWidth;
	pxYoffset     = pxLineSpacing * 2.0;
	pxHexOffset   = pxXoffset + (7*pxCharWidth);
	pxHexAscii    = pxHexOffset + (16*3*pxCharWidth) + (pxCharWidth);
	pxLineWidth   = pxHexAscii + (17*pxCharWidth);
    //_pxGapAdr = _pxCharWidth / 2;
    //_pxGapAdrHex = _pxCharWidth;
    //_pxGapHexAscii = 2 * _pxCharWidth;
    pxCursorHeight = pxCharHeight;
    //_pxSelectionSub = _pxCharHeight / 5;
	 viewLines   = (viewHeight - pxLineSpacing) / pxLineSpacing;
}
//----------------------------------------------------------------------------
void QHexEdit::changeFontRequest(void)
{
	bool ok = false;

	QFont selFont = QFontDialog::getFont( &ok, font, this, tr("Select Font"), QFontDialog::MonospacedFonts );

	if ( ok )
	{
		font = selFont;

		calcFontData();

		//printf("Font Changed to: '%s'\n", font.toString().toStdString().c_str() );

		g_config->setOption("SDL.HexEditFont", font.toString().toStdString().c_str() );
	}
}
//----------------------------------------------------------------------------
void QHexEdit::setHighlightActivity( int enable )
{
	actvHighlightEnable = enable;
}
//----------------------------------------------------------------------------
void QHexEdit::setHighlightReverseVideo( int enable )
{
	reverseVideo = enable;
}
//----------------------------------------------------------------------------
void QHexEdit::setForeGroundColor( QColor fg )
{
	QPalette pal;

	pal = this->palette();
	pal.setColor(QPalette::WindowText, fg );

	this->setPalette(pal);
}
//----------------------------------------------------------------------------
void QHexEdit::setBackGroundColor( QColor bg )
{
	QPalette pal;

	pal = this->palette();
	pal.setColor(QPalette::Window    , bg );

	this->setPalette(pal);
}
//----------------------------------------------------------------------------
void QHexEdit::setMode( int mode )
{
	if ( viewMode != mode )
	{
		viewMode = mode;
		memModeUpdate();
		clearHighlight();
	}
}
//----------------------------------------------------------------------------
void QHexEdit::setLine( int newLineOffset )
{
	lineOffset = newLineOffset;
}
//----------------------------------------------------------------------------
void QHexEdit::setAddr( int newAddr )
{
	int addr;
	lineOffset = newAddr / 16;

	if ( lineOffset < 0 )
	{
		lineOffset = 0;
	}
	else if ( lineOffset >= maxLineOffset )
	{
		lineOffset = maxLineOffset;
	}

	addr = 16*lineOffset;

	cursorPosX = 2*((newAddr - addr)%16);
	cursorPosY =    (newAddr - addr)/16;

	vbar->setValue( lineOffset );
}
//----------------------------------------------------------------------------
void QHexEdit::setHorzScroll( int value )
{
	float f;
	//printf("Value: %i \n", value);

	if ( viewWidth >= pxLineWidth )
	{
		f = 0.0;
	}
	else
	{
		f = 0.010f * (float)value * (float)(pxLineWidth - viewWidth);
	}

	pxLineXScroll = (int)f;
}
//----------------------------------------------------------------------------
void QHexEdit::setScrollBars( QScrollBar *h, QScrollBar *v )
{
	hbar = h; vbar = v;
}
//----------------------------------------------------------------------------
void QHexEdit::resizeEvent(QResizeEvent *event)
{
	viewWidth  = event->size().width();
	viewHeight = event->size().height();

	//printf("QHexEdit Resize: %ix%i\n", viewWidth, viewHeight );

	viewLines = (viewHeight - pxLineSpacing) / pxLineSpacing;

	maxLineOffset = mb.numLines() - viewLines + 1;

	if ( viewWidth >= pxLineWidth )
	{
		pxLineXScroll = 0;
		hbar->setMaximum(0);
		hbar->hide();
	}
	else
	{
		hbar->setPageStep(viewWidth);
		hbar->setMaximum(pxLineWidth - viewWidth);
		hbar->show();
		pxLineXScroll = hbar->value();
	}
	vbar->setMaximum( maxLineOffset );
	vbar->setPageStep( (3*viewLines)/4 );

}
//----------------------------------------------------------------------------
void QHexEdit::openGotoAddrDialog(void)
{
	int ret;
	char stmp[128];
	QDialog dialog(this);
	QLabel *lbl;
	QSpinBox *sbox;
	QVBoxLayout *vbox;
	QHBoxLayout *hbox;
	QPushButton *okButton, *cancelButton;

	sprintf( stmp, "Specify Address [ 0x0 -> 0x%X ]", mb.size()-1 );

	vbox = new QVBoxLayout();
	hbox = new QHBoxLayout();
	lbl  = new QLabel( tr(stmp) );

	okButton     = new QPushButton( tr("Go") );
	cancelButton = new QPushButton( tr("Cancel") );

	okButton->setIcon( style()->standardIcon( QStyle::SP_DialogApplyButton ) );
	cancelButton->setIcon( style()->standardIcon( QStyle::SP_DialogCancelButton ) );

	connect(     okButton, SIGNAL(clicked(void)), &dialog, SLOT(accept(void)) );
	connect( cancelButton, SIGNAL(clicked(void)), &dialog, SLOT(reject(void)) );

	sbox = new QSpinBox();
	sbox->setRange(0x0000, mb.size()-1);
	sbox->setDisplayIntegerBase(16);
	sbox->setValue( 0 );

	QFont font = sbox->font();
	font.setCapitalization(QFont::AllUppercase);
	sbox->setFont(font);

	hbox->addWidget( cancelButton );
	hbox->addWidget(     okButton );

	vbox->addWidget( lbl  );
	vbox->addWidget( sbox );
	vbox->addLayout( hbox );

	dialog.setLayout( vbox );

	dialog.setWindowTitle( tr("Goto Address") );

	okButton->setDefault(true);

	ret = dialog.exec();

	if ( QDialog::Accepted == ret )
	{
		int addr;
	
		addr = sbox->value();
	
		parent->gotoAddress(addr);
	}
}
//----------------------------------------------------------------------------
void QHexEdit::resetCursor(void)
{
	cursorBlink = true;
	cursorBlinkCount = 0;
	editAddr = -1;
	editValue = 0;
	editMask  = 0;
}
//----------------------------------------------------------------------------
void QHexEdit::clearHighlight(void)
{
	txtHlgtAnchorChar = -1;
	txtHlgtAnchorLine = -1;
	txtHlgtStartChar = -1;
	txtHlgtStartLine = -1;
	txtHlgtStartAddr = -1;
	txtHlgtEndChar = -1;
	txtHlgtEndLine = -1;
	txtHlgtEndAddr = -1;
}
//----------------------------------------------------------------------------
void QHexEdit::loadClipboard( const char *txt )
{
	//printf("Load Clipboard: '%s'\n", txt );
	clipboard->setText( tr(txt), QClipboard::Clipboard );

	if ( clipboard->supportsSelection() )
	{
		clipboard->setText( tr(txt), QClipboard::Selection );
	}
}
//----------------------------------------------------------------------------
void QHexEdit::pasteFromClipboard(void)
{
	int i, nbytes=0, val, addr;
	std::string s = clipboard->text().toStdString();
	const char *c;
	unsigned char *buf;

	fceuWrapperLock();

	//printf("Paste: '%s'\n", s.c_str() );

	addr = cursorAddr;

	c = s.c_str();

	if ( s.size() == 0 )
	{
		return;
	}
	buf = (unsigned char*)malloc( s.size() );

	if ( buf == NULL )
	{
		return;
	}
	memset( buf, 0, s.size() );

	i=0; nbytes = 0;
	while ( c[i] != 0 )
	{
		while ( isspace(c[i]) ) i++;

		val = 0;

		if ( isxdigit(c[i]) )
		{
			val = convFromXchar(c[i]) << 4; i++;
		}
		else 
		{
			break;
		}

		if ( isxdigit(c[i]) )
		{
			val |= convFromXchar(c[i]); i++;
		}
		else 
		{
			break;
		}
		buf[ nbytes ] = val;

		nbytes++;
	}
	if ( nbytes > 0 )
	{
		if ( viewMode == QHexEdit::MODE_NES_ROM )
		{
			romEditList.applyPatch( addr, buf, nbytes );
		}
		for (i=0; i<nbytes; i++)
		{
			writeMem( viewMode, addr+i, buf[i] );
		}
	}
	free(buf);

	fceuWrapperUnLock();
}
//----------------------------------------------------------------------------
void QHexEdit::loadHighlightToClipboard(void)
{
	int a, startAddr, endAddr;
	std::string s;
	char c[8];

	fceuWrapperLock();

	startAddr = (txtHlgtStartLine*16) + txtHlgtStartChar;
	endAddr   = (txtHlgtEndLine  *16) + txtHlgtEndChar;

	for (a=startAddr; a<=endAddr; a++)
	{
		sprintf( c, "%02X ", memAccessFunc(a) );

		s.append(c);
	}
	fceuWrapperUnLock();

	loadClipboard( s.c_str() );
}
//----------------------------------------------------------------------------
int QHexEdit::findPattern( std::vector <unsigned char> &varray, int dir )
{
	int addr, inc, match;

	inc   = dir ? -1 : 1;
	addr  = cursorAddr;
	match = 0;

	//printf("Looking for pattern %zi\n", varray.size() );

	while ( !match )
	{
		addr = (addr + inc);

		if ( addr < 0 )
		{
			addr = mb.size() - 1;
		}
		else if ( addr >= mb.size() )
		{
			addr = 0;
		}

		if ( addr == cursorAddr )
		{
			return -1;
		}
		match = 1;
		for (int i=0; i<varray.size(); i++)
		{
			if ( (addr+i) >= mb.size() )
			{
				match = 0; break;
			}
			if ( memAccessFunc(addr+i) != varray[i] )
			{
				match = 0; break;
			}
		}
	}

	if ( match )
	{
		int endAddr = addr + varray.size() - 1;
		//printf("Found Match at $%04X\n", addr );
		txtHlgtStartChar = (addr%16);
		txtHlgtStartLine = (addr/16);
		txtHlgtStartAddr = addr;
		txtHlgtEndChar = (endAddr%16);
		txtHlgtEndLine = (endAddr/16);
		txtHlgtEndAddr = (endAddr);
		cursorAddr     = addr;
		cursorPosX     = txtHlgtStartChar*2;

		if ( txtHlgtStartLine < lineOffset )
		{
			lineOffset = txtHlgtStartLine;
			vbar->setValue( lineOffset );
		}
		else if ( txtHlgtStartLine >= (lineOffset+viewLines-3) )
		{
			lineOffset = txtHlgtStartLine - viewLines + 3;

			if ( lineOffset >= maxLineOffset )
			{
			   lineOffset = maxLineOffset;
			}
			vbar->setValue( lineOffset );
		}
		cursorPosY = txtHlgtStartLine - lineOffset;
	}
	return 0;
}
//----------------------------------------------------------------------------
QPoint QHexEdit::convPixToCursor( QPoint p )
{
	QPoint c(0,0);

	//printf("Pos: %ix%i \n", p.x(), p.y() );
	
	p.setX( p.x() + pxLineXScroll );

	if ( p.x() < pxHexOffset )
	{
		c.setX(0);
	}
	else if ( (p.x() >= pxHexOffset) && (p.x() < pxHexAscii) )
	{
		float px = ( (float)p.x() - (float)pxHexOffset) / (float)(pxCharWidth);
		float ox = (px/3.0);
		float rx = fmodf(px,3.0);

		if ( rx >= 2.50 )
		{
			c.setX( 2*( (int)ox + 1 ) );
		}
		else
		{
			//if ( rx >= 1.0 )
			//{
			//	c.setX( 2*( (int)ox ) + 1 );
			//}
			//else
			//{
				c.setX( 2*( (int)ox ) );
			//}
		}
	}
	else
	{
		c.setX( 32 + (p.x() - pxHexAscii) / pxCharWidth );
	}
	if ( c.x() >= 48 )
	{
		c.setX( 47 );
	}

	if ( p.y() < pxYoffset )
	{
		c.setY( 0 );
	}
	else
	{
		float ly = ( (float)pxLineLead / (float)pxLineSpacing );
		float py = ( (float)p.y() -  (float)pxLineSpacing) /  (float)pxLineSpacing;
		float ry = fmod( py, 1.0 );

		if ( ry < ly )
		{
			c.setY( ((int)py) - 1 );
		}
		else
		{
			c.setY( (int)py );
		}
	}
	if ( c.y() < 0 )
	{
		c.setY(0);
	}
	else if ( c.y() >= viewLines )
	{
		c.setY( viewLines - 1 );
	}
	//printf("c: %ix%i \n", cx, cy );
	//

	return c;
}
//----------------------------------------------------------------------------
int QHexEdit::convPixToAddr( QPoint p )
{
	int a,addr;
	QPoint c = convPixToCursor(p);

	//printf("Cursor: %ix%i\n", c.x(), c.y() );
	
	if ( c.x() < 32 )
	{
		a = (c.x() / 2);

		addr = 16*(lineOffset + c.y()) + a;
	}
	else
	{
		a = (c.x()-32);

		addr = 16*(lineOffset + c.y()) + a;
	}
	return addr;
}
//----------------------------------------------------------------------------
void QHexEdit::keyPressEvent(QKeyEvent *event)
{
	//printf("Hex Window Key Press: 0x%x \n", event->key() );
	
	if (event->matches(QKeySequence::MoveToNextChar))
	{
		if ( cursorPosX < 32 )
		{
			if ( cursorPosX % 2 )
			{
				cursorPosX++;
			}
			else 
			{
				cursorPosX += 2;
			}
		}
		else
		{
			cursorPosX++;
		}
		if ( cursorPosX >= 48  )
		{
			cursorPosX = 47;
		}
		resetCursor();
		update();
		event->accept();
	}
	else if (event->matches(QKeySequence::MoveToPreviousChar))
	{
		if ( cursorPosX < 33 )
		{
			if ( cursorPosX % 2 )
			{
				cursorPosX -= 3;
			}
			else 
			{
				cursorPosX -= 2;
			}
		}
		else
		{
			cursorPosX--;
		}
		if ( cursorPosX < 0 )
		{
			cursorPosX = 0;
		}
		resetCursor();
		update();
		event->accept();
	}
	else if (event->matches(QKeySequence::MoveToEndOfLine))
	{
		cursorPosX = 47;
		resetCursor();
		update();
		event->accept();
	}
	else if (event->matches(QKeySequence::MoveToStartOfLine))
	{
		cursorPosX = 0;
		resetCursor();
		update();
		event->accept();
	}
	else if (event->matches(QKeySequence::MoveToPreviousLine))
	{
		cursorPosY--;

		if ( cursorPosY < 0 )
		{
			lineOffset--;

			if ( lineOffset < 0 )
			{
				lineOffset = 0;
			}
			cursorPosY = 0;

			vbar->setValue( lineOffset );
		}
		resetCursor();
		update();
		event->accept();
	}
	else if (event->matches(QKeySequence::MoveToNextLine))
	{
		cursorPosY++;

		if ( cursorPosY >= viewLines )
		{
			lineOffset++;

			if ( lineOffset >= maxLineOffset )
			{
			   lineOffset = maxLineOffset;
			}
			cursorPosY = viewLines-1;

			vbar->setValue( lineOffset );
		}
		resetCursor();
		update();
		event->accept();

	}
	else if (event->matches(QKeySequence::MoveToNextPage))
	{
		lineOffset += ( (3 * viewLines) / 4);
		
		if ( lineOffset >= maxLineOffset )
		{
		   lineOffset = maxLineOffset;
		}
		vbar->setValue( lineOffset );
	     	resetCursor();
		update();
		event->accept();
	}
	else if (event->matches(QKeySequence::MoveToPreviousPage))
	{
		lineOffset -= ( (3 * viewLines) / 4);

		if ( lineOffset < 0 )
		{
		   lineOffset = 0;
		}
		vbar->setValue( lineOffset );
		resetCursor();
		update();
		event->accept();
	}
	else if (event->matches(QKeySequence::MoveToEndOfDocument))
	{
		lineOffset = maxLineOffset;
		vbar->setValue( lineOffset );
	     	resetCursor();
		update();
		event->accept();
	}
	else if (event->matches(QKeySequence::MoveToStartOfDocument))
	{
		lineOffset = 0;
		vbar->setValue( lineOffset );
		resetCursor();
		update();
		event->accept();
	}
	else if (Qt::ControlModifier == event->modifiers())
	{
		if ( event->key() == Qt::Key_A )
		{
			openGotoAddrDialog();
			event->accept();
		}
		else if ( event->key() == Qt::Key_L )
		{
			frzRamAddr = ctxAddr = cursorAddr;
			frzRamToggle();
			event->accept();
		}
	}
	else if (event->key() == Qt::Key_Tab && (cursorPosX < 32) )
	{  // switch from hex to ascii edit
		cursorPosX = 32 + (cursorPosX / 2);
		update();
		event->accept();
	}
	else if (event->key() == Qt::Key_Backtab  && (cursorPosX >= 32) )
	{  // switch from ascii to hex edit
	   cursorPosX = 2 * (cursorPosX - 32);
		update();
		event->accept();
	}
	else
	{
		int key;
		if ( cursorPosX >= 32 )
		{  // Edit Area is ASCII
			key = (int)event->text()[0].toLatin1();

			if ( (key >= 0) && (key < 256) )
			{
				if ( charTable.rmap[key] != -1 )
				{
					key = charTable.rmap[key];

					int offs = (cursorPosX-32);
					int addr = 16*(lineOffset+cursorPosY) + offs;
					fceuWrapperLock();
					if ( viewMode == QHexEdit::MODE_NES_ROM )
					{
						romEditList.applyPatch( addr, key );
					}
					writeMem( viewMode, addr, key );
					fceuWrapperUnLock();
				
					editAddr  = -1;
					editValue =  0;
					editMask  =  0;
					update();
					event->accept();
				}
			}
		}
		else
		{  // Edit Area is Hex
		   key = int(event->text()[0].toUpper().toLatin1());
		
		   if ( ::isxdigit( key ) )
		   {
			int offs, nibbleValue, nibbleIndex;
			
			offs = (cursorPosX / 2);
			nibbleIndex = (cursorPosX % 2);
			
			editAddr = 16*(lineOffset+cursorPosY) + offs;
			
			nibbleValue = convFromXchar( key );
			
			if ( nibbleIndex )
			{
				nibbleValue = editValue | nibbleValue;
				
				fceuWrapperLock();
				if ( viewMode == QHexEdit::MODE_NES_ROM )
				{
					romEditList.applyPatch( editAddr, nibbleValue );
				}
				writeMem( viewMode, editAddr, nibbleValue );
				fceuWrapperUnLock();
				
				editAddr  = -1;
				editValue =  0;
				editMask  =  0;
			}
			else
			{
			   editValue = (nibbleValue << 4);
			   editMask  = 0x00f0;
			}
			cursorPosX++;
		
			if ( cursorPosX >= 32 )
			{
			   cursorPosX = 0;
			}
			update();
			event->accept();
		   }
		}
		//printf("Key: %c  %i \n", key, key);
	}
}
//----------------------------------------------------------------------------
void QHexEdit::keyReleaseEvent(QKeyEvent *event)
{
   //printf("Hex Window Key Release: 0x%x \n", event->key() );
}
//----------------------------------------------------------------------------
bool QHexEdit::textIsHighlighted(void)
{
	bool set = false;

	if ( txtHlgtStartLine == txtHlgtEndLine )
	{
		set = (txtHlgtStartChar != txtHlgtEndChar);
	}
	else
	{
		set = true;
	}
	return set;
}
//----------------------------------------------------------------------------
void QHexEdit::setHighlightEndCoord( int x, int y )
{

	if ( txtHlgtAnchorLine < y )
	{
		txtHlgtStartLine = txtHlgtAnchorLine;
		txtHlgtStartChar = txtHlgtAnchorChar;
		txtHlgtEndLine   = y;
		txtHlgtEndChar   = x;
	}
	else if ( txtHlgtAnchorLine > y )
	{
		txtHlgtStartLine = y;
		txtHlgtStartChar = x;
		txtHlgtEndLine   = txtHlgtAnchorLine;
		txtHlgtEndChar   = txtHlgtAnchorChar;
	}
	else
	{
		txtHlgtStartLine = txtHlgtAnchorLine;
		txtHlgtEndLine   = txtHlgtAnchorLine;

		if ( txtHlgtAnchorChar < x )
		{
			txtHlgtStartChar = txtHlgtAnchorChar;
			txtHlgtEndChar   = x;
		}
		else if ( txtHlgtAnchorChar > x )
		{
			txtHlgtStartChar = x;
			txtHlgtEndChar   = txtHlgtAnchorChar;
		}
		else
		{
			txtHlgtStartChar = txtHlgtAnchorChar;
			txtHlgtEndChar   = txtHlgtAnchorChar;
		}
	}
	txtHlgtStartAddr = (txtHlgtStartLine*16) + txtHlgtStartChar;
	txtHlgtEndAddr   = (txtHlgtEndLine  *16) + txtHlgtEndChar;

	//printf(" (%i,%i) -> (%i,%i) \n", txtHlgtStartChar, txtHlgtStartLine, txtHlgtEndChar, txtHlgtEndLine );
	return;
}
//----------------------------------------------------------------------------
void QHexEdit::mouseMoveEvent(QMouseEvent * event)
{
	//int line;
	//QPoint c = convPixToCursor( event->pos() );
	int addr = convPixToAddr( event->pos() );

	//line = lineOffset + c.y();

	//printf("Move c: %ix%i \n", c.x(), c.y() );

	if ( mouseLeftBtnDown )
	{
		//printf("Left Button Move: (%i,%i)\n", c.x(), c.y() );
		setHighlightEndCoord( addr % 16, addr / 16 );
		update();
	}
}
//----------------------------------------------------------------------------
void QHexEdit::mousePressEvent(QMouseEvent * event)
{
	int addr;
	QPoint c = convPixToCursor( event->pos() );
	addr     = convPixToAddr( event->pos() );

	//line = lineOffset + c.y();
	//printf("c: %ix%i \n", c.x(), c.y() );

	if ( event->button() == Qt::LeftButton )
	{
		cursorPosX = c.x();
		cursorPosY = c.y();
		resetCursor();
		mouseLeftBtnDown = true;

		txtHlgtAnchorChar = addr % 16;
		txtHlgtAnchorLine = addr / 16;
		setHighlightEndCoord( txtHlgtAnchorChar, txtHlgtAnchorLine );
		update();
	}

}
//----------------------------------------------------------------------------
void QHexEdit::mouseReleaseEvent(QMouseEvent * event)
{
	//int line;
	//QPoint c = convPixToCursor( event->pos() );
	int addr   = convPixToAddr( event->pos() );

	//line = lineOffset + c.y();
	//printf("c: %ix%i \n", c.x(), c.y() );

	if ( event->button() == Qt::LeftButton )
	{
		mouseLeftBtnDown = false;

		setHighlightEndCoord( addr % 16, addr / 16 );

		if ( textIsHighlighted() )
		{
			loadHighlightToClipboard();
		}
		update();
	}

}
//----------------------------------------------------------------------------
void QHexEdit::wheelEvent(QWheelEvent *event)
{

	QPoint numPixels  = event->pixelDelta();
	QPoint numDegrees = event->angleDelta();

	if (!numPixels.isNull()) 
	{
		wheelPixelCounter -= numPixels.y();
	   //printf("numPixels: (%i,%i) \n", numPixels.x(), numPixels.y() );
	} 
	else if (!numDegrees.isNull()) 
	{
		//QPoint numSteps = numDegrees / 15;
		//printf("numSteps: (%i,%i) \n", numSteps.x(), numSteps.y() );
		//printf("numDegrees: (%i,%i)  %i\n", numDegrees.x(), numDegrees.y(), pxLineSpacing );
		wheelPixelCounter -= (pxLineSpacing * numDegrees.y()) / (15*8);
	}
	//printf("Wheel Event: %i\n", wheelPixelCounter);

	if ( wheelPixelCounter >= pxLineSpacing )
	{
		lineOffset += (wheelPixelCounter / pxLineSpacing);

		if ( lineOffset > maxLineOffset )
		{
			lineOffset = maxLineOffset;
		}
		vbar->setValue( lineOffset );

		wheelPixelCounter = wheelPixelCounter % pxLineSpacing;
	}
	else if ( wheelPixelCounter <= -pxLineSpacing )
	{
		lineOffset += (wheelPixelCounter / pxLineSpacing);

		if ( lineOffset < 0 )
		{
			lineOffset = 0;
		}
		vbar->setValue( lineOffset );

		wheelPixelCounter = wheelPixelCounter % pxLineSpacing;
	}

	 event->accept();
}
//----------------------------------------------------------------------------
void QHexEdit::contextMenuEvent(QContextMenuEvent *event)
{
	QAction *act;
	QMenu menu(this);
	int addr;
	char stmp[128];

	QPoint c = convPixToCursor( event->pos() );
	cursorPosX = c.x();
	cursorPosY = c.y();
	resetCursor();

	ctxAddr = addr = convPixToAddr( event->pos() );
	//printf("contextMenuEvent\n");

	switch ( viewMode )
	{
		case MODE_NES_RAM:
		{
			QMenu *subMenu;

			act = new QAction(tr("Add &Symbolic Debug Name"), &menu);
			menu.addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(addDebugSym(void)) );

			subMenu = menu.addMenu(tr("&Freeze/Unfreeze Address"));

			if ( frzRamAddrValid( addr ) )
			{
				act = new QAction(tr("&Toggle State"), &menu);
				act->setShortcut( QKeySequence(tr("Ctrl+L")));
				subMenu->addAction(act);
				connect( act, SIGNAL(triggered(void)), this, SLOT(frzRamToggle(void)) );

				act = new QAction(tr("&Freeze"), &menu);
				subMenu->addAction(act);
				connect( act, SIGNAL(triggered(void)), this, SLOT(frzRamSet(void)) );

				act = new QAction(tr("&Unfreeze"), &menu);
				subMenu->addAction(act);
				connect( act, SIGNAL(triggered(void)), this, SLOT(frzRamUnset(void)) );

				subMenu->addSeparator();
			}

			act = new QAction(tr("Unfreeze &All"), &menu);
			subMenu->addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(frzRamUnsetAll(void)) );

			sprintf( stmp, "Add &Read Breakpoint for Address $%04X", addr );
			act = new QAction(tr(stmp), &menu);
			menu.addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(addRamReadBP(void)) );

			sprintf( stmp, "Add &Write Breakpoint for Address $%04X", addr );
			act = new QAction(tr(stmp), &menu);
			menu.addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(addRamWriteBP(void)) );

			sprintf( stmp, "Add &Execute Breakpoint for Address $%04X", addr );
			act = new QAction(tr(stmp), &menu);
			menu.addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(addRamExecuteBP(void)) );

			if ( addr > 0x6000 )
			{
				int romAddr = GetNesFileAddress(addr);

				if ( romAddr >= 0 )
				{
					jumpToRomValue = romAddr;
					sprintf( stmp, "&Go Here in ROM File: (%08X)", romAddr );
					act = new QAction(tr(stmp), &menu);
					menu.addAction(act);
					connect( act, SIGNAL(triggered(void)), this, SLOT(jumpToROM(void)) );
				}
			}

			act = new QAction(tr("Add Book&mark"), &menu);
			menu.addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(addBookMarkCB(void)) );
		}
		break;
		case MODE_NES_PPU:
		{
			sprintf( stmp, "Add &Read Breakpoint for Address $%04X", addr );
			act = new QAction(tr(stmp), &menu);
			menu.addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(addPpuReadBP(void)) );

			sprintf( stmp, "Add &Write Breakpoint for Address $%04X", addr );
			act = new QAction(tr(stmp), &menu);
			menu.addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(addPpuWriteBP(void)) );

			act = new QAction(tr("Add Book&mark"), &menu);
			menu.addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(addBookMarkCB(void)) );
		}
		break;
		case MODE_NES_OAM:
		{
			act = new QAction(tr("Add Book&mark"), &menu);
			menu.addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(addBookMarkCB(void)) );
		}
		break;
		case MODE_NES_ROM:
		{
			act = new QAction(tr("Add Book&mark"), &menu);
			menu.addAction(act);
			connect( act, SIGNAL(triggered(void)), this, SLOT(addBookMarkCB(void)) );
		}
		break;
	}

   menu.exec(event->globalPos());

}
//----------------------------------------------------------------------------
void QHexEdit::setRowColHlgtEna(bool val)
{
	rolColHlgtEna = val;
}
//----------------------------------------------------------------------------
void QHexEdit::setAltColHlgtEna(bool val)
{
	altColHlgtEna = val;
}
//----------------------------------------------------------------------------
void QHexEdit::addBookMarkCB(void)
{
	int ret;
	char stmp[64];
	QInputDialog dialog(this);

	switch ( viewMode )
	{
		default:
		case MODE_NES_RAM:
			sprintf( stmp, "CPU %04X", ctxAddr );
		break;
		case MODE_NES_PPU:
			sprintf( stmp, "PPU %04X", ctxAddr );
		break;
		case MODE_NES_OAM:
			sprintf( stmp, "OAM %04X", ctxAddr );
		break;
		case MODE_NES_ROM:
			sprintf( stmp, "ROM %04X", ctxAddr );
		break;
	}

	dialog.setWindowTitle( tr("Add Bookmark") );
	dialog.setLabelText( tr("Specify New Bookmark Description") );
	dialog.setOkButtonText( tr("Add") );
	dialog.setTextValue( tr(stmp) );

	ret = dialog.exec();
	
	if ( QDialog::Accepted == ret )
	{
	     	hbm.addBookMark( ctxAddr, viewMode, dialog.textValue().toStdString().c_str() );
	     	parent->populateBookmarkMenu();
	}
}
//----------------------------------------------------------------------------
static int RamFreezeCB(char *name, uint32 a, uint8 v, int compare,int s,int type, void *data)
{
	return ((QHexEdit*)data)->FreezeRam( name, a, v, compare, s, type );
}	
//----------------------------------------------------------------------------
int QHexEdit::FreezeRam( const char *name, uint32_t a, uint8_t v, int c, int s, int type )
{

	//if ( c >= 0 )
	//{
	//	printf("$%04X?%02X:%02X   %i: %s\n", a, c, v, s, name );
	//}
	//else
	//{
	//	printf("$%04X:%02X   %i: %s\n", a, v, s, name );
	//}

	if ( a == frzRamAddr )
	{
		switch ( frzRamMode )
		{
			case 0: // Toggle

				if ( s )
				{
					FCEUI_DelCheat( frzIdx );
					frzRamAddr = -1;
					return 0;
				}
			break;
			case 1: // Freeze

				if ( s )
				{
					// Already Set so there is nothing further to do
					frzRamAddr = -1;
					return 0;
				}
			break;
			case 2: // Unfreeze
				if ( s )
				{
					FCEUI_DelCheat( frzIdx );
					frzRamAddr = -1;
					return 0;
				}
			break;
			default:
			case 3: // Unfreeze All Handled Below
				// Nothing to do
			break;
		}
	}

	if ( frzRamMode == 3 )
	{
		if ( s )
		{
			FCEUI_DelCheat( frzIdx );
		}
	}

	frzIdx++;

	return 1;
}
//----------------------------------------------------------------------------
bool QHexEdit::frzRamAddrValid( int addr )
{
	if ( addr < 0 )
	{
		return false;
	}

	if ( (addr < 0x2000) || ( (addr >= 0x6000) && (addr <= 0x7FFF) ) )
	{
		return true;
	}
	return false;
}
//----------------------------------------------------------------------------
void QHexEdit::frzRamSet(void)
{
	frzIdx = 0;
	frzRamMode = 1;
	frzRamAddr = ctxAddr;

	if ( !frzRamAddrValid( frzRamAddr ) )
	{
		return;
	}

	fceuWrapperLock();
	FCEUI_ListCheats( RamFreezeCB, this);

	if ( (frzRamAddr >= 0) && (FrozenAddressCount < 256) )
	{
		FCEUI_AddCheat("", frzRamAddr, GetMem(frzRamAddr), -1, 1);
	}
	updateCheatDialog();
	fceuWrapperUnLock();
}
//----------------------------------------------------------------------------
void QHexEdit::frzRamUnset(void)
{
	frzIdx = 0;
	frzRamMode = 2;
	frzRamAddr = ctxAddr;

	if ( !frzRamAddrValid( frzRamAddr ) )
	{
		return;
	}
	fceuWrapperLock();
	FCEUI_ListCheats( RamFreezeCB, this);
	updateCheatDialog();
	fceuWrapperUnLock();
}
//----------------------------------------------------------------------------
void QHexEdit::frzRamUnsetAll(void)
{
	frzIdx = 0;
	frzRamMode = 3;
	frzRamAddr = ctxAddr;

	if ( !frzRamAddrValid( frzRamAddr ) )
	{
		return;
	}
	fceuWrapperLock();
	FCEU_DeleteAllCheats();
	updateCheatDialog();
	fceuWrapperUnLock();
}
//----------------------------------------------------------------------------
void QHexEdit::frzRamToggle(void)
{
	frzIdx = 0;
	frzRamMode = 0;
	frzRamAddr = ctxAddr;

	if ( !frzRamAddrValid( frzRamAddr ) )
	{
		return;
	}
	fceuWrapperLock();
	FCEUI_ListCheats( RamFreezeCB, this);

	if ( (frzRamAddr >= 0) && (FrozenAddressCount < 256) )
	{
		FCEUI_AddCheat("", frzRamAddr, GetMem(frzRamAddr), -1, 1);
	}
	updateCheatDialog();
	fceuWrapperUnLock();
}
//----------------------------------------------------------------------------
void QHexEdit::addDebugSym(void)
{
	parent->openDebugSymbolEditWindow( ctxAddr );
}
//----------------------------------------------------------------------------
void QHexEdit::addRamReadBP(void)
{
	int retval, type;
	char cond[64], name[64];

	type = BT_C | WP_R;

	cond[0] = 0;
	name[0] = 0;

	if ( ctxAddr >= 0x8000 )
	{
		sprintf(cond, "K==#%02X", getBank(ctxAddr));
	}

	retval = NewBreak( name, ctxAddr, -1, type, cond, numWPs, true);

	if ( (retval == 1) || (retval == 2) )
	{
		printf("Breakpoint Add Failed\n");
	}
	else
	{
		numWPs++;
	}
}
//----------------------------------------------------------------------------
void QHexEdit::addRamWriteBP(void)
{
	int retval, type;
	char cond[64], name[64];

	type = BT_C | WP_W;

	cond[0] = 0;
	name[0] = 0;

	if ( ctxAddr >= 0x8000 )
	{
		sprintf(cond, "K==#%02X", getBank(ctxAddr));
	}

	retval = NewBreak( name, ctxAddr, -1, type, cond, numWPs, true);

	if ( (retval == 1) || (retval == 2) )
	{
		printf("Breakpoint Add Failed\n");
	}
	else
	{
		numWPs++;
	}
}
//----------------------------------------------------------------------------
void QHexEdit::addRamExecuteBP(void)
{
	int retval, type;
	char cond[64], name[64];

	type = BT_C | WP_X;

	cond[0] = 0;
	name[0] = 0;

	if ( ctxAddr >= 0x8000 )
	{
		sprintf(cond, "K==#%02X", getBank(ctxAddr));
	}

	retval = NewBreak( name, ctxAddr, -1, type, cond, numWPs, true);

	if ( (retval == 1) || (retval == 2) )
	{
		printf("Breakpoint Add Failed\n");
	}
	else
	{
		numWPs++;
	}
}
//----------------------------------------------------------------------------
void QHexEdit::addPpuReadBP(void)
{
	int retval, type;
	char cond[64], name[64];

	type = BT_P | WP_R;

	cond[0] = 0;
	name[0] = 0;

	retval = NewBreak( name, ctxAddr, -1, type, cond, numWPs, true);

	if ( (retval == 1) || (retval == 2) )
	{
		printf("Breakpoint Add Failed\n");
	}
	else
	{
		numWPs++;
	}
}
//----------------------------------------------------------------------------
void QHexEdit::addPpuWriteBP(void)
{
	int retval, type;
	char cond[64], name[64];

	type = BT_P | WP_W;

	cond[0] = 0;
	name[0] = 0;

	retval = NewBreak( name, ctxAddr, -1, type, cond, numWPs, true);

	if ( (retval == 1) || (retval == 2) )
	{
		printf("Breakpoint Add Failed\n");
	}
	else
	{
		numWPs++;
	}
}
//----------------------------------------------------------------------------
void QHexEdit::jumpToROM(void)
{
	setMode( MODE_NES_ROM );

	maxLineOffset = mb.numLines() - viewLines + 1;

	if ( lineOffset > maxLineOffset )
	{
		lineOffset = maxLineOffset;
	}
	setAddr( jumpToRomValue );
}
//----------------------------------------------------------------------------
void QHexEdit::requestUpdate(void)
{
	updateRequested = true;
}
//----------------------------------------------------------------------------
// Calling of checkMemActivity must always be synchronized with the emulation
// thread as calling GetMem while the emulation is executing can mess up certain
// registers (especially controller registers $4016 and $4017)
int QHexEdit::checkMemActivity(void)
{
	int c;

	// Don't perform memory activity checks when:
	// 1. In ROM View Mode
	// 2. The simulation is not cycling (paused)

	if ( !updateRequested )
	{
		if ( ( viewMode == MODE_NES_ROM ) ||
		      ( total_instructions_lp == total_instructions ) )
		{
			return -1;
		}
	}

	for (int i=0; i<mb.size(); i++)
	{
		c = memAccessFunc(i);

		if ( c != mb.buf[i].data )
		{
			mb.buf[i].actv  = 15;
			mb.buf[i].data  = c;
			//mb.buf[i].draw  = 1;
		}
		else
		{
			if ( mb.buf[i].actv > 0 )
			{
				//mb.buf[i].draw = 1;
				mb.buf[i].actv--;
			}
		}
	}
	total_instructions_lp = total_instructions;
	updateRequested = false;

   return 0;
}
//----------------------------------------------------------------------------
int QHexEdit::getRomAddrColor( int addr, QColor &fg, QColor &bg )
{
	int temp_offset;
	QColor color, oppColor; 
			
	fg = fgColor;
	bg = bgColor;

	if ( reverseVideo )
	{
		color    = bgColor;
		oppColor = fgColor;
	}
	else
	{
		color    = fgColor;
		oppColor = bgColor;
	}

	if ( viewMode != MODE_NES_ROM )
	{
		return -1;
	}
	if ( memAccessFunc )
	{
		mb.buf[addr].data = memAccessFunc(addr);
	}
	else
	{
		mb.buf[addr].data = 0;
	}

	if ( (txtHlgtStartAddr != txtHlgtEndAddr) && (addr >= txtHlgtStartAddr) && (addr <= txtHlgtEndAddr) )
	{
		fg.setRgb( 255, 255, 255 ); // white
		bg.setRgb(   0,   0, 255 ); // blue
		return 0;
	}
	if ( romEditList.isModified( addr ) )
	{
		fg.setRgb( 255, 255, 255 ); // white
		bg.setRgb( 255,   0,   0 ); // red
		return 0;
	}
	if (cdloggerdataSize == 0)
	{
		return -1;
	}
	temp_offset = addr - 16;

	if (temp_offset >= 0)
	{
		if ((unsigned int)temp_offset < cdloggerdataSize)
		{
			// PRG
			if ((cdloggerdata[temp_offset] & 3) == 3)
			{
				// the byte is both Code and Data - green
				color.setRgb(0, 190, 0);
			}
			else if ((cdloggerdata[temp_offset] & 3) == 1)
			{
				// the byte is Code - dark-yellow
				color.setRgb(160, 140, 0);
				oppColor.setRgb( 0, 0, 0 );
			}
			else if ((cdloggerdata[temp_offset] & 3) == 2)
			{
				// the byte is Data - blue/cyan
				if (cdloggerdata[temp_offset] & 0x40)
				{
					// PCM data - cyan
					color.setRgb(0, 130, 160);
				}
				else
				{
					// non-PCM data - blue
					color.setRgb(0, 0, 210);
				}
			}
		}
		else
		{
			temp_offset -= cdloggerdataSize;
			if (((unsigned int)temp_offset < cdloggerVideoDataSize))
			{
				// CHR
				if ((cdloggervdata[temp_offset] & 3) == 3)
				{
					// the byte was both rendered and read programmatically - light-green
					color.setRgb(5, 255, 5);
				}
				else if ((cdloggervdata[temp_offset] & 3) == 1)
				{
					// the byte was rendered - yellow
					color.setRgb(210, 190, 0);
					oppColor.setRgb( 0, 0, 0 );
				}
				else if ((cdloggervdata[temp_offset] & 3) == 2)
				{
					// the byte was read programmatically - light-blue
					color.setRgb(15, 15, 255);
				}
			}
		}
	}

	if ( reverseVideo )
	{
		bg = color;
		fg = oppColor;
	}
	else
	{
		fg = color;
		bg = oppColor;
	}

	return 0;
}
//----------------------------------------------------------------------------
void QHexEdit::memModeUpdate(void)
{
	int memSize;

	switch ( getMode() )
	{
		default:
		case MODE_NES_RAM:
			memAccessFunc = getRAM;
			memSize       = 0x10000;
		break;
		case MODE_NES_PPU:
			memAccessFunc = getPPU;
			if ( GameInfo )
			{
				memSize = (GameInfo->type == GIT_NSF ? 0x2000 : 0x4000);
			}
			else
			{
				memSize = 0x4000;
			}
		break;
		case MODE_NES_OAM:
			memAccessFunc = getOAM;
			memSize       = 0x100;
		break;
		case MODE_NES_ROM:

			if ( GameInfo != NULL )
			{
				memAccessFunc = getROM;
				memSize       = 16 + CHRsize[0] + PRGsize[0];
			}
			else
			{  // No Game Loaded!!! Get out of Function
				memAccessFunc = NULL;
				memSize = 0;
				return;
			}
		break;
	}

	if ( memSize != mb.size() )
	{
		mb.setAccessFunc( memAccessFunc );

		if ( mb.reAlloc( memSize ) )
		{
			printf("Error: Failed to allocate memview buffer size\n");
			return;
		}
		maxLineOffset = mb.numLines() - viewLines + 1;

		//vbar->setMaximum( memSize / 16 );
		vbar->setMaximum( maxLineOffset );
		vbar->setPageStep( (3*viewLines)/4 );
	}
}
//----------------------------------------------------------------------------
void QHexEdit::paintEvent(QPaintEvent *event)
{
	int x, y, w, h, row, col, nrow, addr;
	int c, cx, cy, ca, l, recty;
	int pxCharWidth3;
	int colHlgtStart = -1, colHlgtEnd = -1;
	char txt[32];
       	QString asciiTxt;
	QPainter painter(this);
	QColor white("white"), black("black"), blue("blue");
	bool txtHlgtSet;

	pxCharWidth3 = 3*pxCharWidth;

	painter.setFont(font);
	w = event->rect().width();
	h = event->rect().height();

	viewWidth  = w;
	viewHeight = h;
	//painter.fillRect( 0, 0, w, h, QColor("white") );

	nrow = (h - pxLineSpacing) / pxLineSpacing;

	if ( nrow < 1 ) nrow = 1;

	viewLines = nrow;

	if ( cursorPosY >= viewLines )
	{
		cursorPosY = viewLines-1;
	}
	//printf("Draw Area: %ix%i \n", event->rect().width(), event->rect().height() );
	//
	maxLineOffset = mb.numLines() - nrow + 1;

	if ( maxLineOffset < 0 )
	{
		maxLineOffset = 0;
	}

	if ( lineOffset < 0 )
	{
		lineOffset = 0;
	}
	if ( lineOffset > maxLineOffset )
	{
		lineOffset = maxLineOffset;
	}
	
	painter.fillRect( 0, 0, w, h, bgColor );

	if ( cursorBlinkCount >= 5 )
	{
		cursorBlink = !cursorBlink;
		cursorBlinkCount = 0;
	} 
	else
  	{
		cursorBlinkCount++;
	}

	cy = pxYoffset + (pxLineSpacing*cursorPosY) - pxCursorHeight + pxLineLead;

	if ( cursorPosX < 32 )
	{
		int a = (cursorPosX / 2);
		int r = (cursorPosX % 2);
		cx = pxHexOffset + (a*pxCharWidth3) + (r*pxCharWidth) - pxLineXScroll;

		ca = 16*(lineOffset + cursorPosY) + a;
	}
	else
	{
		int a = (cursorPosX-32);
		cx = pxHexAscii + (a*pxCharWidth) - pxLineXScroll;

		ca = 16*(lineOffset + cursorPosY) + a;
	}
	cursorAddr = ca;

	if ( altColHlgtEna )
	{
		for (int i=0; i<16; i++)
		{
			if ( (i % 2) == 0 )
			{	
				painter.fillRect( pxHexOffset - (0.5*pxCharWidth) + (i*pxCharWidth3) - pxLineXScroll , 0, (3*pxCharWidth), viewHeight, altColHlgtColor );
			}
		}

	}

	if ( rolColHlgtEna )
	{
		//painter.fillRect( 0 , cy - pxLineLead, viewWidth, pxLineSpacing, rowColHlgtColor );
		painter.fillRect( 0 , cy - pxLineLead/2, viewWidth, pxLineSpacing, rowColHlgtColor );
		//painter.fillRect( 0 , cy, viewWidth, pxLineSpacing, rowColHlgtColor );

		if ( cursorPosX < 32 )
		{
			painter.fillRect( cx - (0.5*pxCharWidth) , 0, (pxCharWidth3), viewHeight, rowColHlgtColor );
		}
		else
		{
			painter.fillRect( cx, 0, (pxCharWidth), viewHeight, rowColHlgtColor );
		}
	}

	painter.setPen( fgColor );

	addr = lineOffset * 16;
	y = pxYoffset;

	txtHlgtSet = textIsHighlighted();

	for ( row=0; row < nrow; row++)
	{
		colHlgtStart = -1; colHlgtEnd = -1;

		l = lineOffset + row;
		x = pxXoffset - pxLineXScroll;


		painter.setPen( fgColor );
		sprintf( txt, "%06X", addr );
		painter.drawText( x, y, tr(txt) );

		x = pxHexOffset - pxLineXScroll;
		recty = pxYoffset + (pxLineSpacing*row) - pxCursorHeight + pxLineLead - (pxLineLead/2); // This must be this way to avoid rounding error

		if ( txtHlgtSet && (l >= txtHlgtStartLine) && (l <= txtHlgtEndLine) )
		{
			int hlgtXs, hlgtXe, hlgtXd;

			if ( l == txtHlgtStartLine )
			{
				hlgtXs = txtHlgtStartChar*3;
				colHlgtStart = txtHlgtStartChar;
			}
			else
			{
				hlgtXs = 0;
				colHlgtStart = 0;
			}

			if ( l == txtHlgtEndLine )
			{
				hlgtXe = (txtHlgtEndChar+1)*3;
				colHlgtEnd = txtHlgtEndChar+1;
			}
			else
			{
				hlgtXe = 16*3;
				colHlgtEnd = 16;
			}
			hlgtXd = hlgtXe - hlgtXs;

			x = pxHexOffset - pxLineXScroll;

			painter.fillRect( x + (hlgtXs*pxCharWidth), recty, hlgtXd*pxCharWidth, pxLineSpacing, blue );

			if ( l == txtHlgtStartLine )
			{
				hlgtXs = txtHlgtStartChar;
			}
			else
			{
				hlgtXs = 0;
			}

			if ( l == txtHlgtEndLine )
			{
				hlgtXe = (txtHlgtEndChar+1);
			}
			else
			{
				hlgtXe = 16;
			}
			hlgtXd = hlgtXe - hlgtXs;

			x = pxHexAscii - pxLineXScroll;

			painter.fillRect( x + (hlgtXs*pxCharWidth), recty, hlgtXd*pxCharWidth, pxLineSpacing, blue );
		}

		x = pxHexOffset - pxLineXScroll;

		for (col=0; col<16; col++)
		{
			if ( addr < mb.size() )
			{
				c =  mb.buf[addr].data;

				asciiTxt.clear();

				if ( charTable.map[c] >= 0x20 )
				{
					asciiTxt += QChar(charTable.map[c]);
				}
				else
				{
					asciiTxt += QChar('.');
				}

				if ( addr == editAddr )
				{  // Set a cell currently being editting to red text
					painter.setPen( QColor("red") );
					txt[0] = convToXchar( (editValue >> 4) & 0x0F );
					txt[1] = convToXchar( c & 0x0F );
					txt[2] = 0;
					painter.drawText( x, y, tr(txt) );

					if ( cursorBlink && (ca == addr) )
					{
						painter.fillRect( cx , cy, pxCharWidth, pxCursorHeight, fgColor );

						painter.setPen( bgColor );

						if ( cursorPosX < 32 )
						{
							if ( cursorPosX % 2 )
							{
								txt[0] = convToXchar( c & 0x0F );
								txt[1] = 0;
							}
							else
							{
								txt[0] = convToXchar( (editValue >> 4) & 0x0F );
								txt[1] = 0;
							}
							painter.drawText( cx, y, tr(txt) );
						}
						else
						{
							painter.drawText( pxHexAscii + (col*pxCharWidth) - pxLineXScroll, y, asciiTxt );
						}
					}
				        painter.setPen( fgColor );
				} 
				else
				{
					if ( txtHlgtSet && (col >= colHlgtStart) && (col < colHlgtEnd) )
					{
						// Background is already colored by highlight
						painter.setPen( fgColor );
					}
					else if ( viewMode == MODE_NES_ROM )
					{
						QColor romBgColor, romFgColor;
					  
						getRomAddrColor( addr, romFgColor, romBgColor );

						if ( reverseVideo )
						{
							painter.setPen( romFgColor );
							painter.fillRect( x - (0.5*pxCharWidth) , recty, pxCharWidth3, pxLineSpacing, romBgColor );
							painter.fillRect( pxHexAscii + (col*pxCharWidth) - pxLineXScroll, recty, pxCharWidth, pxLineSpacing, romBgColor );
						}
						else
						{
							painter.setPen( romFgColor );
						}
					}
					else if ( viewMode == MODE_NES_RAM )
					{
						if ( FCEUI_FindCheatMapByte( addr ) )
						{
							if ( reverseVideo )
							{
								painter.setPen( white );
								painter.fillRect( x - (0.5*pxCharWidth) , recty, pxCharWidth3, pxLineSpacing, blue );
								painter.fillRect( pxHexAscii + (col*pxCharWidth) - pxLineXScroll, recty, pxCharWidth, pxLineSpacing, blue );
							}
							else
							{
								painter.setPen( blue );
							}
						}
						else if ( actvHighlightEnable && (mb.buf[addr].actv > 0) )
						{
							if ( reverseVideo )
							{
								painter.setPen( rvActvTextColor[ mb.buf[addr].actv ] );
								painter.fillRect( x - (0.5*pxCharWidth) , recty, pxCharWidth3, pxLineSpacing, highLightColor[ mb.buf[addr].actv ] );
								painter.fillRect( pxHexAscii + (col*pxCharWidth) - pxLineXScroll, recty, pxCharWidth, pxLineSpacing, highLightColor[ mb.buf[addr].actv ] );
							}
							else
							{
								painter.setPen( highLightColor[ mb.buf[addr].actv ] );
							}
						}
						else 
						{
							painter.setPen( fgColor );
						}
					}
					else if ( actvHighlightEnable && (mb.buf[addr].actv > 0) )
					{
						if ( reverseVideo )
						{
							painter.setPen( rvActvTextColor[ mb.buf[addr].actv ] );
							painter.fillRect( x - (0.5*pxCharWidth) , recty, pxCharWidth3, pxLineSpacing, highLightColor[ mb.buf[addr].actv ] );
							painter.fillRect( pxHexAscii + (col*pxCharWidth) - pxLineXScroll, recty, pxCharWidth, pxLineSpacing, highLightColor[ mb.buf[addr].actv ] );
						}
						else
						{
							painter.setPen( highLightColor[ mb.buf[addr].actv ] );
						}
					}
					else
					{
						painter.setPen( fgColor );
					}
					txt[0] = convToXchar( (c >> 4) & 0x0F );
					txt[1] = convToXchar( c & 0x0F );
					txt[2] = 0;

					painter.drawText( x, y, tr(txt) );
					//painter.drawText( pxHexAscii + (col*pxCharWidth) - pxLineXScroll, y, tr(asciiTxt) );
					painter.drawText( pxHexAscii + (col*pxCharWidth) - pxLineXScroll, y, asciiTxt );

					if ( cursorBlink && (ca == addr) )
					{
						painter.fillRect( cx , cy, pxCharWidth, pxCursorHeight, fgColor );

						painter.setPen( bgColor );

						if ( cursorPosX < 32 )
						{
							if ( cursorPosX % 2 )
							{
								txt[0] = convToXchar( c & 0x0F );
								txt[1] = 0;
							}
							else
							{
								txt[0] = convToXchar( (c >> 4) & 0x0F );
								txt[1] = 0;
							}
							painter.drawText( cx, y, tr(txt) );
						}
						else
						{
							painter.drawText( pxHexAscii + (col*pxCharWidth) - pxLineXScroll, y, asciiTxt );
						}
					}
				}
			}
			x += (pxCharWidth3);
			addr++;
		}

		y += pxLineSpacing;
	}

	painter.setPen( fgColor );
	for (x=0; x<16; x++)
	{
		txt[0] = '0';
		txt[1] = convToXchar( x );
		txt[2] = 0;
		painter.drawText( pxHexOffset - pxLineXScroll + (x * pxCharWidth3), pxLineSpacing, txt );
	}
	painter.drawLine( pxHexOffset - (pxCharWidth/2) - pxLineXScroll, 0, pxHexOffset - (pxCharWidth/2) - pxLineXScroll, h );
	painter.drawLine( pxHexAscii  - (pxCharWidth/2) - pxLineXScroll, 0, pxHexAscii  - (pxCharWidth/2) - pxLineXScroll, h );
	painter.drawLine( 0, pxLineSpacing + (pxLineLead), w, pxLineSpacing + (pxLineLead) );

}
//----------------------------------------------------------------------------
void hexEditorLoadBookmarks(void)
{
	std::list <HexEditorDialog_t*>::iterator it;

	hbm.removeAll();
	hbm.loadFromFile();

	for (it = winList.begin(); it != winList.end(); it++)
	{
		(*it)->populateBookmarkMenu();
	}
}
//----------------------------------------------------------------------------
void hexEditorSaveBookmarks(void)
{
	std::list <HexEditorDialog_t*>::iterator it;

	hbm.saveToFile();
	hbm.removeAll();

	for (it = winList.begin(); it != winList.end(); it++)
	{
		(*it)->populateBookmarkMenu();
	}
}
//----------------------------------------------------------------------------
void hexEditorRequestUpdateAll(void)
{
	std::list <HexEditorDialog_t*>::iterator it;

	for (it = winList.begin(); it != winList.end(); it++)
	{
		(*it)->editor->requestUpdate();
	}
}
//----------------------------------------------------------------------------
int hexEditorNumWindows(void)
{
	return winList.size();
}
//----------------------------------------------------------------------------
int hexEditorOpenFromDebugger( int mode, int addr )
{
	HexEditorDialog_t *win = NULL;

	if ( winList.size() > 0 )
	{
		win = winList.front();
	}

	if ( win == NULL )
	{
		win = new HexEditorDialog_t(consoleWindow);

		win->show();
	}

	win->editor->setMode( mode );
	win->editor->setAddr( addr );

	return 0;
}
//----------------------------------------------------------------------------
// This function must be called from within the emulation thread
void hexEditorUpdateMemoryValues( bool force )
{
	std::list <HexEditorDialog_t*>::iterator it;

	if ( !memNeedsCheck && !force )
	{
		return;
	}

	for (it = winList.begin(); it != winList.end(); it++)
	{
		(*it)->editor->checkMemActivity();
	}
	memNeedsCheck = false;
}
//----------------------------------------------------------------------------
