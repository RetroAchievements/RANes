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
//
// ppuViewer.cpp
//
#include <stdio.h>
#include <stdint.h>

#include <QDir>
#include <QMenu>
#include <QAction>
#include <QMenuBar>
#include <QPainter>
#include <QSettings>
#include <QFileDialog>
#include <QInputDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QTreeWidget>
#include <QActionGroup>
#include <QClipboard>
#include <QGuiApplication>

#include "../../types.h"
#include "../../fceu.h"
#include "../../cart.h"
#include "../../ppu.h"
#include "../../debug.h"
#include "../../palette.h"

#include "Qt/ppuViewer.h"
#include "Qt/main.h"
#include "Qt/dface.h"
#include "Qt/input.h"
#include "Qt/config.h"
#include "Qt/HexEditor.h"
#include "Qt/fceuWrapper.h"
#include "Qt/ConsoleWindow.h"
#include "Qt/ConsoleUtilities.h"
#include "Qt/PaletteEditor.h"
#include "Qt/ColorMenu.h"

#define PATTERNWIDTH          128
#define PATTERNHEIGHT         128
#define PATTERNBITWIDTH       PATTERNWIDTH*3

#define PALETTEWIDTH          16
#define PALETTEHEIGHT         2
#define PALETTEBITWIDTH       PALETTEWIDTH*3

static ppuViewerDialog_t *ppuViewWindow = NULL;
static spriteViewerDialog_t *spriteViewWindow = NULL;
static int PPUViewScanline = 0;
static int PPUViewSkip = 0;
static int PPUViewRefresh = 1;
static bool PPUView_maskUnusedGraphics = true;
static bool PPUView_invertTheMask = false;
static int PPUView_sprite16Mode[2] = { 0, 0 };
static int pindex[2] = { 0, 0 };
static uint8_t pallast[32+3] = { 0 }; // palette cache for change comparison
static uint8_t palcache[36] = { 0 }; //palette cache for drawing
static uint8_t chrcache0[0x1000] = {0}, chrcache1[0x1000] = {0}, logcache0[0x1000] = {0}, logcache1[0x1000] = {0}; //cache CHR, fixes a refresh problem when right-clicking
static uint8_t oam[256];
static bool	redrawWindow = true;

static void initPPUViewer(void);
static ppuPatternTable_t pattern0;
static ppuPatternTable_t pattern1;
static oamPatternTable_t oamPattern;
//----------------------------------------------------
int openPPUViewWindow( QWidget *parent )
{
	if ( ppuViewWindow != NULL )
	{
		ppuViewWindow->activateWindow();
		ppuViewWindow->raise();
		return -1;
	}
	initPPUViewer();

	ppuViewWindow = new ppuViewerDialog_t(parent);

	ppuViewWindow->show();

	return 0;
}
//----------------------------------------------------
int openOAMViewWindow( QWidget *parent )
{
	if ( spriteViewWindow != NULL )
	{
		spriteViewWindow->activateWindow();
		spriteViewWindow->raise();
		return -1;
	}
	initPPUViewer();

	spriteViewWindow = new spriteViewerDialog_t(parent);

	spriteViewWindow->show();

	return 0;
}
//----------------------------------------------------------------------------
static int conv2hex( int i )
{
	int h = 0;
	if ( i >= 10 )
	{
		h = 'A' + i - 10;
	}
	else
	{
		h = '0' + i;
	}
	return h;
}
//----------------------------------------------------
void setPPUSelPatternTile( int table, int x, int y )
{
	if ( ppuViewWindow == NULL )
	{
		return;
	}
	if ( table )
	{
		table = 1;
	}
	else
	{
		table = 0;
	}
	ppuViewWindow->patternView[ table ]->setTileCoord( x, y );
	ppuViewWindow->patternView[ table ]->updateSelTileLabel();
}
//----------------------------------------------------
static int exportActivePaletteACT( const char *filename )
{
	FILE *fp;
	int i=0, c, ret = 0, numBytes;
	unsigned char buf[768];

	fp = fopen( filename, "wb");

	if ( fp == NULL )
	{
		return -1;
	}
	memset( buf, 0, sizeof(buf) );

	i=0;
	for (int p=0; p<32; p++)
	{
		c = palcache[p];

		//printf("%i: %02X\n", p, c );

		if ( palo )
		{
			buf[i] = palo[c].r; i++;
			buf[i] = palo[c].g; i++;
			buf[i] = palo[c].b; i++;

			//printf("%i: %02X    #%02X%02X%02X  rgb( %3i,%3i,%3i)\n", p, c,
			//	palo[c].r, palo[c].g, palo[c].b, palo[c].r, palo[c].g, palo[c].b );
		}
	}

	numBytes = ::fwrite( buf, 1, 768, fp );

	if ( numBytes != 768 )
	{
		printf("Error Failed to Export Palette\n");
		ret = -1;
	}
	::fclose(fp);

	return ret;
}
//----------------------------------------------------
//----------------------------------------------------
ppuViewerDialog_t::ppuViewerDialog_t(QWidget *parent)
	: QDialog( parent, Qt::Window )
{
	QSettings    settings;
	QMenuBar    *menuBar;
	QVBoxLayout *mainLayout, *vbox;
	QVBoxLayout *patternVbox[2];
	QHBoxLayout *hbox, *hbox1, *hbox2;
	QGridLayout *grid;
	QActionGroup *group;
	QMenu *fileMenu, *viewMenu, *colorMenu, *optMenu, *subMenu;
	QAction *act;
	//char stmp[64];
	int useNativeMenuBar;
	ColorMenuItem *tileSelColorAct[2], *tileGridColorAct[2];

	ppuViewWindow = this;

	menuBar = new QMenuBar(this);

	// This is needed for menu bar to show up on MacOS
	g_config->getOption( "SDL.UseNativeMenuBar", &useNativeMenuBar );

	menuBar->setNativeMenuBar( useNativeMenuBar ? true : false );

	setWindowTitle( tr("PPU Viewer") );

	mainLayout = new QVBoxLayout();

	mainLayout->setMenuBar( menuBar );

	setLayout( mainLayout );

	hbox              = new QHBoxLayout();
	grid              = new QGridLayout;
	patternVbox[0]    = new QVBoxLayout();
	patternVbox[1]    = new QVBoxLayout();
	patternFrame[0]   = new QGroupBox( tr("Pattern Table 0") );
	patternFrame[1]   = new QGroupBox( tr("Pattern Table 1") );
	patternView[0]    = new ppuPatternView_t( 0, this);
	patternView[1]    = new ppuPatternView_t( 1, this);
	sprite8x16Cbox[0] = new QCheckBox( tr("Sprites 8x16 Mode") );
	sprite8x16Cbox[1] = new QCheckBox( tr("Sprites 8x16 Mode") );
	tileLabel[0]      = new QLabel( tr("Tile:") );
	tileLabel[1]      = new QLabel( tr("Tile:") );

	g_config->getOption("SDL.PPU_View1_8x16", &PPUView_sprite16Mode[0]);
	g_config->getOption("SDL.PPU_View2_8x16", &PPUView_sprite16Mode[1]);

	sprite8x16Cbox[0]->setChecked( PPUView_sprite16Mode[0] );
	sprite8x16Cbox[1]->setChecked( PPUView_sprite16Mode[1] );

	patternVbox[0]->addWidget( patternView[0], 100 );
	patternVbox[0]->addWidget( tileLabel[0], 1 );
	patternVbox[0]->addWidget( sprite8x16Cbox[0], 1 );
	patternVbox[1]->addWidget( patternView[1], 100 );
	patternVbox[1]->addWidget( tileLabel[1], 1 );
	patternVbox[1]->addWidget( sprite8x16Cbox[1], 1 );

	patternFrame[0]->setLayout( patternVbox[0] );
	patternFrame[1]->setLayout( patternVbox[1] );

	hbox->addWidget( patternFrame[0] );
	hbox->addWidget( patternFrame[1] );

	mainLayout->addLayout( hbox, 10 );
	mainLayout->addLayout( grid,  1 );

	maskUnusedCbox = new QCheckBox( tr("Mask unused Graphics (Code/Data Logger)") );
	invertMaskCbox = new QCheckBox( tr("Invert the Mask (Code/Data Logger)") );

	g_config->getOption("SDL.PPU_MaskUnused", &PPUView_maskUnusedGraphics);
	g_config->getOption("SDL.PPU_InvertMask", &PPUView_invertTheMask);

	maskUnusedCbox->setChecked( PPUView_maskUnusedGraphics );
	invertMaskCbox->setChecked( PPUView_invertTheMask );

	connect( maskUnusedCbox   , SIGNAL(stateChanged(int)), this, SLOT(maskUnusedGraphicsChanged(int)));
	connect( invertMaskCbox   , SIGNAL(stateChanged(int)), this, SLOT(invertMaskChanged(int)));
	connect( sprite8x16Cbox[0], SIGNAL(stateChanged(int)), this, SLOT(sprite8x16Changed0(int)));
	connect( sprite8x16Cbox[1], SIGNAL(stateChanged(int)), this, SLOT(sprite8x16Changed1(int)));

	hbox           = new QHBoxLayout();
	refreshSlider  = new QSlider( Qt::Horizontal );
	hbox->addWidget( new QLabel( tr("Refresh: More") ) );
	hbox->addWidget( refreshSlider );
	hbox->addWidget( new QLabel( tr("Less") ) );

	grid->addWidget( maskUnusedCbox, 0, 0, Qt::AlignLeft );
	grid->addWidget( invertMaskCbox, 1, 0, Qt::AlignLeft );
	grid->addLayout( hbox, 0, 1, Qt::AlignRight );

	hbox         = new QHBoxLayout();
	scanLineEdit = new QSpinBox();
	hbox->addWidget( new QLabel( tr("Display on Scanline:") ) );
	hbox->addWidget( scanLineEdit );
	grid->addLayout( hbox, 1, 1, Qt::AlignRight );

	vbox         = new QVBoxLayout();
	//paletteFrame = new QGroupBox( tr("Palettes: ---- Top Row: Background ---- Bottom Row: Sprites") );
	paletteFrame = new QGroupBox( tr("Palettes:") );
	//paletteView  = new ppuPalatteView_t(this);

	hbox1        = new QHBoxLayout();
	hbox2        = new QHBoxLayout();

	for (int i=0; i<8; i++)
	{
		tilePalView[i] = new tilePaletteView_t(this);

		if ( i < 4 )
		{
			hbox1->addWidget( tilePalView[i] );
		}
		else
		{
			hbox2->addWidget( tilePalView[i] );
		}
		tilePalView[i]->setIndex(i);
	}

	vbox->addLayout( hbox1, 1 );
	vbox->addLayout( hbox2, 1 );
	paletteFrame->setLayout( vbox );

	mainLayout->addWidget( paletteFrame,  1 );

	patternView[0]->setPattern( &pattern0 );
	patternView[1]->setPattern( &pattern1 );
	patternView[0]->setTileLabel( tileLabel[0] );
	patternView[1]->setTileLabel( tileLabel[1] );

	g_config->getOption("SDL.PPU_ViewScanLine", &PPUViewScanline);

	scanLineEdit->setRange( 0, 255 );
	scanLineEdit->setValue( PPUViewScanline );

	connect( scanLineEdit, SIGNAL(valueChanged(int)), this, SLOT(scanLineChanged(int)));

	g_config->getOption("SDL.PPU_ViewRefreshFrames", &PPUViewRefresh);

	refreshSlider->setMinimum( 0);
	refreshSlider->setMaximum(25);
	refreshSlider->setValue(PPUViewRefresh);

	connect( refreshSlider, SIGNAL(valueChanged(int)), this, SLOT(refreshSliderChanged(int)));

	cycleCount  = 0;
	PPUViewSkip = 100;
	
	FCEUD_UpdatePPUView( -1, 1 );

	//-----------------------------------------------------------------------
	// Menu 
	//-----------------------------------------------------------------------
	// File
	fileMenu = menuBar->addMenu(tr("&File"));

	// File -> Close
	act = new QAction(tr("&Close"), this);
	act->setShortcut(QKeySequence::Close);
	act->setStatusTip(tr("Close Window"));
	connect(act, SIGNAL(triggered()), this, SLOT(closeWindow(void)) );
	
	fileMenu->addAction(act);

	// View1
	viewMenu = menuBar->addMenu(tr("View&1"));

	// View1 -> Toggle Grid
	act = new QAction(tr("Toggle &Grid"), this);
	//act->setShortcut(QKeySequence::Open);
	//act->setCheckable(true);
	//act->setChecked( patternView[0]->getDrawTileGrid() );
	act->setStatusTip(tr("Toggle Grid"));
	connect( act, SIGNAL(triggered()), patternView[0], SLOT(toggleTileGridLines()) );
	
	viewMenu->addAction(act);

	// View1 -> Colors
	colorMenu = viewMenu->addMenu(tr("&Colors"));

	// View1 -> Colors -> Tile Selector
	tileSelColorAct[0] = new ColorMenuItem(tr("Tile &Selector"), "SDL.PPU_TileSelColor0", this);
	tileSelColorAct[0]->connectColor( &patternView[0]->selTileColor );
	
	colorMenu->addAction(tileSelColorAct[0]);

	// View1 -> Colors -> Tile Grid
	tileGridColorAct[0] = new ColorMenuItem(tr("Tile &Grid"), "SDL.PPU_TileGridColor0", this);
	tileGridColorAct[0]->connectColor( &patternView[0]->gridColor );
	
	colorMenu->addAction(tileGridColorAct[0]);

	// View2
	viewMenu = menuBar->addMenu(tr("View&2"));

	// View2 -> Toggle Grid
	act = new QAction(tr("Toggle &Grid"), this);
	//act->setShortcut(QKeySequence::Open);
	//act->setCheckable(true);
	//act->setChecked( patternView[1]->getDrawTileGrid() );
	act->setStatusTip(tr("Toggle Grid"));
	connect( act, SIGNAL(triggered()), patternView[1], SLOT(toggleTileGridLines()) );
	
	viewMenu->addAction(act);

	// View2 -> Colors
	colorMenu = viewMenu->addMenu(tr("&Colors"));

	// View2 -> Colors -> Tile Selector
	tileSelColorAct[1] = new ColorMenuItem(tr("Tile &Selector"), "SDL.PPU_TileSelColor1", this);
	tileSelColorAct[1]->connectColor( &patternView[1]->selTileColor );
	
	colorMenu->addAction(tileSelColorAct[1]);

	// View2 -> Colors -> Tile Grid
	tileGridColorAct[1] = new ColorMenuItem(tr("Tile &Grid"), "SDL.PPU_TileGridColor1", this);
	tileGridColorAct[1]->connectColor( &patternView[1]->gridColor );
	
	colorMenu->addAction(tileGridColorAct[1]);

	// Options
	optMenu = menuBar->addMenu(tr("&Options"));

	// Options -> Focus
	subMenu = optMenu->addMenu(tr("&Focus Policy"));
	group   = new QActionGroup(this);
	group->setExclusive(true);

	act = new QAction(tr("&Click"), this);
	act->setCheckable(true);
	act->setChecked( !patternView[0]->getHoverFocus() );
	group->addAction(act);
	subMenu->addAction(act);
	connect(act, SIGNAL(triggered()), this, SLOT(setClickFocus(void)) );

	act = new QAction(tr("&Hover"), this);
	act->setCheckable(true);
	act->setChecked( patternView[0]->getHoverFocus() );
	group->addAction(act);
	subMenu->addAction(act);
	connect(act, SIGNAL(triggered()), this, SLOT(setHoverFocus(void)) );

	//-----------------------------------------------------------------------
	// End Menu 
	//-----------------------------------------------------------------------

	updateTimer  = new QTimer( this );

	connect( updateTimer, &QTimer::timeout, this, &ppuViewerDialog_t::periodicUpdate );

	updateTimer->start( 33 ); // 30hz

	restoreGeometry(settings.value("ppuViewer/geometry").toByteArray());

	connect( this, SIGNAL(rejected(void)), this, SLOT(deleteLater(void)));
}

//----------------------------------------------------
ppuViewerDialog_t::~ppuViewerDialog_t(void)
{
	QSettings settings;

	updateTimer->stop();
	ppuViewWindow = NULL;

	//printf("PPU Viewer Window Deleted\n");
	settings.setValue("ppuViewer/geometry", saveGeometry());
}
//----------------------------------------------------
void ppuViewerDialog_t::closeEvent(QCloseEvent *event)
{
	QSettings settings;
	//printf("PPU Viewer Close Window Event\n");
	settings.setValue("ppuViewer/geometry", saveGeometry());
	done(0);
	deleteLater();
	event->accept();
}
//----------------------------------------------------
void ppuViewerDialog_t::closeWindow(void)
{
	QSettings settings;
	//printf("Close Window\n");
	settings.setValue("ppuViewer/geometry", saveGeometry());
	done(0);
	deleteLater();
}
//----------------------------------------------------
void ppuViewerDialog_t::periodicUpdate(void)
{
	cycleCount = (cycleCount + 1) % 4;

	if ( redrawWindow || (cycleCount == 0) )
	{
		this->update();
		redrawWindow = false;
	}
	patternView[0]->updateCycleCounter();
	patternView[1]->updateCycleCounter();

	if ( scanLineEdit->value() != PPUViewScanline )
	{
		scanLineEdit->setValue( PPUViewScanline );
	}
}
//----------------------------------------------------
void ppuViewerDialog_t::scanLineChanged(int value)
{
	PPUViewScanline = value;
	//printf("ScanLine: %i\n", PPUViewScanline );
	g_config->setOption("SDL.PPU_ViewScanLine", PPUViewScanline);
}
//----------------------------------------------------
void ppuViewerDialog_t::invertMaskChanged(int state)
{
	PPUView_invertTheMask = (state == Qt::Unchecked) ? 0 : 1;

	g_config->setOption("SDL.PPU_InvertMask", PPUView_invertTheMask);
}
//----------------------------------------------------
void ppuViewerDialog_t::maskUnusedGraphicsChanged(int state)
{
	PPUView_maskUnusedGraphics = (state == Qt::Unchecked) ? 0 : 1;

	g_config->setOption("SDL.PPU_MaskUnused", PPUView_maskUnusedGraphics);
}
//----------------------------------------------------
void ppuViewerDialog_t::sprite8x16Changed0(int state)
{
	PPUView_sprite16Mode[0] = (state == Qt::Unchecked) ? 0 : 1;

	g_config->setOption("SDL.PPU_View1_8x16", PPUView_sprite16Mode[0]);
}
//----------------------------------------------------
void ppuViewerDialog_t::sprite8x16Changed1(int state)
{
	PPUView_sprite16Mode[1] = (state == Qt::Unchecked) ? 0 : 1;

	g_config->setOption("SDL.PPU_View2_8x16", PPUView_sprite16Mode[1]);
}
//----------------------------------------------------
void ppuViewerDialog_t::refreshSliderChanged(int value)
{
	PPUViewRefresh = value;

	g_config->setOption("SDL.PPU_ViewRefreshFrames", PPUViewRefresh);
}
//----------------------------------------------------
void ppuViewerDialog_t::setClickFocus(void)
{
	patternView[0]->setHoverFocus(false);
	patternView[1]->setHoverFocus(false);
}
//----------------------------------------------------
void ppuViewerDialog_t::setHoverFocus(void)
{
	patternView[0]->setHoverFocus(true);
	patternView[1]->setHoverFocus(true);
}
//----------------------------------------------------
ppuPatternView_t::ppuPatternView_t( int patternIndexID, QWidget *parent)
	: QWidget(parent)
{
	this->setFocusPolicy(Qt::StrongFocus);
	this->setMouseTracking(true);
	patternIndex = patternIndexID;
	setMinimumWidth( 256 );
	setMinimumHeight( 256 );
	viewWidth = 256;
	viewHeight = 256;
	tileLabel = NULL;
	mode = 0;
	cycleCount   = 0;
	drawTileGrid = true;
	hover2Focus  = false;

	selTileColor.setRgb(255,255,255);
	gridColor.setRgb(128,128,128);
	selTile.setX(-1);
	selTile.setY(-1);

	if ( patternIndexID )
	{
		fceuLoadConfigColor("SDL.PPU_TileSelColor1"  , &selTileColor  );
		fceuLoadConfigColor("SDL.PPU_TileGridColor1" , &gridColor     );
		g_config->getOption("SDL.PPU_TileShowGrid1"  , &drawTileGrid );
	}
	else
	{
		fceuLoadConfigColor("SDL.PPU_TileSelColor0"  , &selTileColor  );
		fceuLoadConfigColor("SDL.PPU_TileGridColor0" , &gridColor     );
		g_config->getOption("SDL.PPU_TileShowGrid0"  , &drawTileGrid );
	}

	g_config->getOption("SDL.PPU_TileFocusPolicy", &hover2Focus );
}
//----------------------------------------------------
void ppuPatternView_t::setPattern( ppuPatternTable_t *p )
{
	pattern = p;
}
//----------------------------------------------------
void ppuPatternView_t::setTileLabel( QLabel *l )
{
	tileLabel = l;
}
//----------------------------------------------------
void ppuPatternView_t::setHoverFocus( bool h )
{
	hover2Focus = h;

	g_config->setOption("SDL.PPU_TileFocusPolicy", hover2Focus );
}
//----------------------------------------------------
void ppuPatternView_t::setTileCoord( int x, int y )
{
	selTile.setX(x);
	selTile.setY(y);
	cycleCount = 0;
}
//----------------------------------------------------
ppuPatternView_t::~ppuPatternView_t(void)
{

}
//----------------------------------------------------
QPoint ppuPatternView_t::convPixToTile( QPoint p )
{
	QPoint t(0,0);
	int x,y,w,h,i,j,ii,jj,rr;

	x = p.x(); y = p.y();

	w = pattern->w;
	h = pattern->h;

	i = w == 0 ? 0 : x / (w*8);
	j = h == 0 ? 0 : y / (h*8);

	if ( PPUView_sprite16Mode[ patternIndex ] )
	{
		rr = (j%2);
		jj =  j;

		if ( rr )
		{
			jj--;
		}

		ii = (i*2)+rr;

		if ( ii >= 16 )
		{
			ii = ii % 16;
			jj++;
		}
	}
	else
	{
		ii = i; jj = j;
	}
	//printf("(x,y) = (%i,%i) w=%i h=%i  $%X%X \n", x, y, w, h, jj, ii );

	t.setX(ii);
	t.setY(jj);

	return t;
}
//----------------------------------------------------
void ppuPatternView_t::resizeEvent(QResizeEvent *event)
{
	viewWidth  = event->size().width();
	viewHeight = event->size().height();

	pattern->w = viewWidth / 128;
  	pattern->h = viewHeight / 128;
}
//----------------------------------------------------
void ppuPatternView_t::keyPressEvent(QKeyEvent *event)
{
	//printf("Pattern View Key Press: 0x%x \n", event->key() );

	if ( event->key() == Qt::Key_Z )
	{
		mode = !mode;

		event->accept();
	}
	else if ( event->key() == Qt::Key_G )
	{
		drawTileGrid = !drawTileGrid;

		event->accept();
	}
	else if ( event->key() == Qt::Key_E )
	{
		openTileEditor();

		event->accept();
	}
	else if ( event->key() == Qt::Key_P )
	{
		pindex[ patternIndex ] = (pindex[ patternIndex ] + 1) % 9;
	
	     	PPUViewSkip = 100;
	
	     	FCEUD_UpdatePPUView( -1, 0 );

		event->accept();
	}
	else if ( event->key() == Qt::Key_F5 )
	{
		// Load Tile Viewport
		PPUViewSkip = 100;

		FCEUD_UpdatePPUView( -1, 1 );

		event->accept();
	}
	else if ( event->key() == Qt::Key_Up )
	{
		int x, y;

		y = selTile.y();
		x = selTile.x();

		if ( PPUView_sprite16Mode[ patternIndex ] )
		{
			if ( (x % 2) == 0 )
			{
				y -= 2;
				x++;
			}
			else
			{
				x--;
			}
		}
		else
		{
			y--;
		}
		if ( y < 0 )
		{
			y += 16;
		}
		selTile.setX(x);
		selTile.setY(y);

		cycleCount = 0;

		updateSelTileLabel();

		event->accept();
	}
	else if ( event->key() == Qt::Key_Down )
	{
		int x,y;

		y = selTile.y();
		x = selTile.x();

		if ( PPUView_sprite16Mode[ patternIndex ] )
		{
			if ( (x % 2) )
			{
				y += 2;
				x--;
			}
			else
			{
				x++;
			}
		}
		else
		{
			y++;
		}
		if ( y >= 16 )
		{
			y = 0;
		}
		selTile.setX(x);
		selTile.setY(y);

		cycleCount = 0;

		updateSelTileLabel();

		event->accept();
	}
	else if ( event->key() == Qt::Key_Left )
	{
		int x,y;

		x = selTile.x();
		y = selTile.y();

		if ( PPUView_sprite16Mode[ patternIndex ] )
		{
			x -= 2;

			if ( x < 0 )
			{
				if ( y % 2 )
				{
					y--;
				}
				else
				{
					y++;
				}
				x += 16;
			}
		}
		else
		{
			x--;
		}
		if ( x < 0 )
		{
			x = 15;
		}
		selTile.setX(x);
		selTile.setY(y);

		cycleCount = 0;

		updateSelTileLabel();

		event->accept();
	}
	else if ( event->key() == Qt::Key_Right )
	{
		int x,y;

		x = selTile.x();
		y = selTile.y();

		if ( PPUView_sprite16Mode[ patternIndex ] )
		{
			x += 2;

			if ( x >= 16 )
			{
				if ( y % 2 )
				{
					y--;
				}
				else
				{
					y++;
				}
				if ( x % 2 )
				{
					x = 1;
				}
				else
				{
					x = 0;
				}
			}
		}
		else
		{
			x++;
		}
		if ( x >= 16 )
		{
			x = 0;
		}
		selTile.setX(x);
		selTile.setY(y);

		cycleCount = 0;

		updateSelTileLabel();

		event->accept();
	}

}
//----------------------------------------------------
void ppuPatternView_t::updateSelTileLabel(void)
{
	char stmp[32];
	if ( (selTile.y() >= 0) && (selTile.x() >= 0) )
	{
		sprintf( stmp, "Tile: $%X%X", selTile.y(), selTile.x() );
	}
	else
	{
		strcpy( stmp, "Tile:");
	}
	tileLabel->setText( tr(stmp) );
}
//----------------------------------------------------
void ppuPatternView_t::mouseMoveEvent(QMouseEvent *event)
{
	if ( mode == 0 )
	{
		QPoint tile = convPixToTile( event->pos() );

		if ( (tile.x() < 16) && (tile.y() < 16) )
		{
		        if ( hover2Focus )
			{
				selTile = tile;

				cycleCount = 0;

				updateSelTileLabel();
			}
		}
	}
}
//----------------------------------------------------------------------------
void ppuPatternView_t::mousePressEvent(QMouseEvent * event)
{
	QPoint tile = convPixToTile( event->pos() );

	if ( event->button() == Qt::LeftButton )
	{
		if ( (tile.x() < 16) && (tile.y() < 16) )
		{
			selTile = tile;

			cycleCount = 0;

			updateSelTileLabel();
		}
	}
}
//----------------------------------------------------
void ppuPatternView_t::contextMenuEvent(QContextMenuEvent *event)
{
	QAction *act;
	QMenu menu(this);
	QMenu *subMenu;
	QActionGroup *group;
	QAction *paletteAct[9];
	char stmp[64];

	act = new QAction(tr("Open Tile &Editor"), &menu);
	act->setShortcut( QKeySequence(tr("E")));
	connect( act, SIGNAL(triggered(void)), this, SLOT(openTileEditor(void)) );
	menu.addAction( act );

	if ( mode )
	{
		sprintf( stmp, "Exit Tile &View: %X%X", selTile.y(), selTile.x() );
		
		act = new QAction(tr(stmp), &menu);
		act->setShortcut( QKeySequence(tr("Z")));
		connect( act, SIGNAL(triggered(void)), this, SLOT(exitTileMode(void)) );
		menu.addAction( act );
	}
	else
	{
		sprintf( stmp, "&View Tile: %X%X", selTile.y(), selTile.x() );
		
		act = new QAction(tr(stmp), &menu);
		act->setShortcut( QKeySequence(tr("Z")));
		connect( act, SIGNAL(triggered(void)), this, SLOT(showTileMode(void)) );
		menu.addAction( act );
	}

	act = new QAction(tr("Draw Tile &Grid Lines"), &menu);
	act->setCheckable(true);
	act->setChecked(drawTileGrid);
	act->setShortcut( QKeySequence(tr("G")));
	connect( act, SIGNAL(triggered(void)), this, SLOT(toggleTileGridLines(void)) );
	menu.addAction( act );

	act = new QAction(tr("Next &Palette"), &menu);
	act->setShortcut( QKeySequence(tr("P")));
	connect( act, SIGNAL(triggered(void)), this, SLOT(cycleNextPalette(void)) );
	menu.addAction( act );

	subMenu = menu.addMenu(tr("Palette &Select"));
	group   = new QActionGroup(&menu);

	group->setExclusive(true);

	for (int i=0; i<9; i++)
	{
	   char stmp[8];

	   sprintf( stmp, "&%i", i+1 );

	   paletteAct[i] = new QAction(tr(stmp), &menu);
	   paletteAct[i]->setCheckable(true);

	   group->addAction(paletteAct[i]);
		subMenu->addAction(paletteAct[i]);
      
	   paletteAct[i]->setChecked( pindex[ patternIndex ] == i );
	}

	connect( paletteAct[0], SIGNAL(triggered(void)), this, SLOT(selPalette0(void)) );
	connect( paletteAct[1], SIGNAL(triggered(void)), this, SLOT(selPalette1(void)) );
	connect( paletteAct[2], SIGNAL(triggered(void)), this, SLOT(selPalette2(void)) );
	connect( paletteAct[3], SIGNAL(triggered(void)), this, SLOT(selPalette3(void)) );
	connect( paletteAct[4], SIGNAL(triggered(void)), this, SLOT(selPalette4(void)) );
	connect( paletteAct[5], SIGNAL(triggered(void)), this, SLOT(selPalette5(void)) );
	connect( paletteAct[6], SIGNAL(triggered(void)), this, SLOT(selPalette6(void)) );
	connect( paletteAct[7], SIGNAL(triggered(void)), this, SLOT(selPalette7(void)) );
	connect( paletteAct[8], SIGNAL(triggered(void)), this, SLOT(selPalette8(void)) );
	
	menu.exec(event->globalPos());
}
//----------------------------------------------------
void ppuPatternView_t::toggleTileGridLines(void)
{
	drawTileGrid = !drawTileGrid;
	
	if ( patternIndex )
	{
	     g_config->setOption( "SDL.PPU_TileShowGrid1", drawTileGrid );
	}
	else
	{
	     g_config->setOption( "SDL.PPU_TileShowGrid0", drawTileGrid );
	}
}
//----------------------------------------------------
void ppuPatternView_t::showTileMode(void)
{
   mode = 1;
}
//----------------------------------------------------
void ppuPatternView_t::exitTileMode(void)
{
   mode = 0;
}
//----------------------------------------------------
void ppuPatternView_t::openTileEditor(void)
{
	ppuTileEditor_t *tileEditor;

	tileEditor = new ppuTileEditor_t( patternIndex, this );

	tileEditor->setTile( &selTile );

	tileEditor->show();
}
//----------------------------------------------------
void ppuPatternView_t::cycleNextPalette(void)
{
	pindex[ patternIndex ] = (pindex[ patternIndex ] + 1) % 9;

	PPUViewSkip = 100;

	FCEUD_UpdatePPUView( -1, 0 );
}
//----------------------------------------------------
void ppuPatternView_t::selPalette0(void)
{
   pindex[ patternIndex ] = 0;
}
//----------------------------------------------------
void ppuPatternView_t::selPalette1(void)
{
   pindex[ patternIndex ] = 1;
}
//----------------------------------------------------
void ppuPatternView_t::selPalette2(void)
{
   pindex[ patternIndex ] = 2;
}
//----------------------------------------------------
void ppuPatternView_t::selPalette3(void)
{
   pindex[ patternIndex ] = 3;
}
//----------------------------------------------------
void ppuPatternView_t::selPalette4(void)
{
   pindex[ patternIndex ] = 4;
}
//----------------------------------------------------
void ppuPatternView_t::selPalette5(void)
{
   pindex[ patternIndex ] = 5;
}
//----------------------------------------------------
void ppuPatternView_t::selPalette6(void)
{
   pindex[ patternIndex ] = 6;
}
//----------------------------------------------------
void ppuPatternView_t::selPalette7(void)
{
   pindex[ patternIndex ] = 7;
}
//----------------------------------------------------
void ppuPatternView_t::selPalette8(void)
{
   pindex[ patternIndex ] = 8;
}
//----------------------------------------------------
void ppuPatternView_t::updateCycleCounter(void)
{
	cycleCount = (cycleCount + 1) % 30;
}
//----------------------------------------------------
void ppuPatternView_t::paintEvent(QPaintEvent *event)
{
	int i,j,x,y,w,h,xx,yy,ii,jj,rr;
	QPainter painter(this);
	QPen pen;
	char showSelector;

	viewWidth  = event->rect().width();
	viewHeight = event->rect().height();

	pen = painter.pen();

	//printf("PPU PatternView %ix%i \n", viewWidth, viewHeight );

	pen.setWidth( 1 );
	pen.setColor( gridColor );
	painter.setPen( pen );

	w = viewWidth / 128;
  	h = viewHeight / 128;

	pattern->w = w;
	pattern->h = h;

	xx = 0; yy = 0;

	showSelector = (cycleCount < 20) && (selTile.x() >= 0) && (selTile.y() >= 0);

	if ( mode == 1 )
	{
		w = viewWidth / 8;
		h = viewHeight / 8;
	
		if ( w < h )
		{
		   h = w;
		}
		else
		{
		   w = h;
		}
		
		ii = selTile.x();
		jj = selTile.y();
		
		// Draw Tile Pixels as rectangles
		for (x=0; x < 8; x++)
		{
			yy = 0;

			for (y=0; y < 8; y++)
			{
				painter.fillRect( xx, yy, w, h, pattern->tile[jj][ii].pixel[y][x].color );
				yy += h;
			}
			xx += w;
		}

		if ( drawTileGrid )
		{
			// Draw Tile Pixel grid lines
			xx = 0; y = 8*h;
			
			for (x=0; x<9; x++)
			{
			           painter.drawLine( xx, 0 , xx, y ); xx += w;
			}
			yy = 0; x = 8*w;
			
			for (y=0; y<9; y++)
			{
			           painter.drawLine( 0, yy , x, yy ); yy += h;
			}
		}
	}
	else if ( PPUView_sprite16Mode[ patternIndex ] )
	{
		for (i=0; i<16; i++) //Columns
		{
			for (j=0; j<16; j++) //Rows
			{
				rr = (j%2);
				jj =  j;

				if ( rr )
				{
					jj--;
				}

				ii = (i*2)+rr;

				if ( ii >= 16 )
				{
					ii = ii % 16;
					jj++;
				}

				xx = (i*8)*w;
				yy = (j*8)*h;

				pattern->tile[jj][ii].x = xx;
				pattern->tile[jj][ii].y = yy;

				for (x=0; x < 8; x++)
				{
					yy = (j*8)*h;

					for (y=0; y < 8; y++)
					{
						painter.fillRect( xx, yy, w, h, pattern->tile[jj][ii].pixel[y][x].color );
						yy += h;
					}
					xx += w;
				}
			}
		}

		if ( drawTileGrid )
		{
			xx = 0; y = 128*h;

			for (i=0; i<16; i++) //Columns
			{
				painter.drawLine( xx, 0 , xx, y ); xx += (8*w);
			}

			yy = 0; x = 128*w;

			for (j=0; j<16; j++) //Rows
			{
				painter.drawLine( 0, yy , x, yy ); yy += (8*h);
			}
		}

		if ( showSelector )
		{
			xx = pattern->tile[ selTile.y() ][ selTile.x() ].x;
			yy = pattern->tile[ selTile.y() ][ selTile.x() ].y;

			pen.setWidth( 3 );
			pen.setColor( QColor(  0,  0,  0) );
			painter.setPen( pen );

			painter.drawRect( xx, yy, w*8, h*8 );

			pen.setWidth( 1 );
			pen.setColor( selTileColor );
			painter.setPen( pen );

			painter.drawRect( xx, yy, w*8, h*8 );
		}
	}
	else
	{
		for (i=0; i<16; i++) //Columns
		{
			for (j=0; j<16; j++) //Rows
			{
				xx = (i*8)*w;
				yy = (j*8)*h;

				pattern->tile[j][i].x = xx;
				pattern->tile[j][i].y = yy;

				for (x=0; x < 8; x++)
				{
					yy = (j*8)*h;

					for (y=0; y < 8; y++)
					{
						painter.fillRect( xx, yy, w, h, pattern->tile[j][i].pixel[y][x].color );
						yy += h;
					}
					xx += w;
				}
			}
		}

		if ( drawTileGrid )
		{
			xx = 0; y = 128*h;

			for (i=0; i<16; i++) //Columns
			{
				painter.drawLine( xx, 0 , xx, y ); xx += (8*w);
			}

			yy = 0; x = 128*w;

			for (j=0; j<16; j++) //Rows
			{
				painter.drawLine( 0, yy , x, yy ); yy += (8*h);
			}
		}

		if ( showSelector )
		{
			xx = pattern->tile[ selTile.y() ][ selTile.x() ].x;
			yy = pattern->tile[ selTile.y() ][ selTile.x() ].y;

			pen.setWidth( 3 );
			pen.setColor( QColor(  0,  0,  0) );
			painter.setPen( pen );

			painter.drawRect( xx, yy, w*8, h*8 );

			pen.setWidth( 1 );
			pen.setColor( selTileColor );
			painter.setPen( pen );

			painter.drawRect( xx, yy, w*8, h*8 );
		}
	}
}
//----------------------------------------------------
static int getPPU( unsigned int i )
{
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
//----------------------------------------------------
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
//----------------------------------------------------
static int writeMemPPU( unsigned int addr, int value )
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
	return 0;
}

//----------------------------------------------------
static void initPPUViewer(void)
{
	memset( pallast  , 0, sizeof(pallast)   );
	memset( palcache , 0, sizeof(palcache)  );
	memset( chrcache0, 0, sizeof(chrcache0) );
	memset( chrcache1, 0, sizeof(chrcache1) );
	memset( logcache0, 0, sizeof(logcache0) );
	memset( logcache1, 0, sizeof(logcache1) );
	memset( oam,       0, sizeof(oam)       );

	// forced palette (e.g. for debugging CHR when palettes are all-black)
	palcache[(8*4)+0] = 0x0F;
	palcache[(8*4)+1] = 0x00;
	palcache[(8*4)+2] = 0x10;
	palcache[(8*4)+3] = 0x20;

	pindex[0] = 0;
	pindex[1] = 0;

}
//----------------------------------------------------
static void DrawPatternTable( ppuPatternTable_t *pattern, uint8_t *table, uint8_t *log, uint8_t pal)
{
	int i,j,x,y,index=0;
	int p=0,tmp;
	uint8_t chr0,chr1,logs,shift;

	if (palo == NULL)
	{
		return;
	}

	pal <<= 2;
	for (i = 0; i < 16; i++)		//Columns
	{
		for (j = 0; j < 16; j++)	//Rows
		{
			//printf("Tile: %X%X  index:%04X   %04X\n", j,i,index, (i<<4)|(j<<8));
			//-----------------------------------------------
			for (y = 0; y < 8; y++)
			{
				chr0 = table[index];
				chr1 = table[index + 8];
				logs = log[index] & log[index + 8];
				tmp = 7;
				shift=(PPUView_maskUnusedGraphics && debug_loggingCD && (((logs & 3) != 0) == PPUView_invertTheMask))?3:0;
				for (x = 0; x < 8; x++)
				{
					p  =  (chr0 >> tmp) & 1;
					p |= ((chr1 >> tmp) & 1) << 1;

					pattern->tile[i][j].pixel[y][x].val = p;

					p = palcache[p | pal];
					tmp--;
					pattern->tile[i][j].pixel[y][x].color.setBlue( palo[p].b >> shift );
					pattern->tile[i][j].pixel[y][x].color.setGreen( palo[p].g >> shift );
					pattern->tile[i][j].pixel[y][x].color.setRed( palo[p].r >> shift );

					//printf("Tile: %X%X Pixel: (%i,%i)  P:%i RGB: (%i,%i,%i)\n", j, i, x, y, p,
					//	  pattern->tile[j][i].pixel[y][x].color.red(), 
					//	  pattern->tile[j][i].pixel[y][x].color.green(), 
					//	  pattern->tile[j][i].pixel[y][x].color.blue() );
				}
				index++;
			}
			index+=8;
			//------------------------------------------------
		}
	}
}
//----------------------------------------------------
static void drawSpriteTable(void)
{
	int j=0, y,x,yy,xx,p,tmp,idx,chr0,chr1,pal,t0,t1;
	uint8_t *chrcache;
	struct oamSpriteData_t *spr;

	if (palo == NULL)
	{
		return;
	}
	oamPattern.mode8x16 = (PPU[0] & 0x20) ? 1 : 0;

	for (int i=0; i<64; i++)
	{
		spr = &oamPattern.sprite[i];

		spr->y     = (oam[j]);
		spr->x     = (oam[j+3]);
		spr->pal   = (oam[j+2] & 0x03) | 0x04;
		spr->pri   = (oam[j+2] & 0x20) ? 1 : 0;
		spr->hFlip = (oam[j+2] & 0x40) ? 1 : 0;
		spr->vFlip = (oam[j+2] & 0x80) ? 1 : 0;

		if ( oamPattern.mode8x16 )
		{
			spr->bank  = (oam[j+1] & 0x01);
			spr->tNum  = (oam[j+1] & 0xFE);
		}
		else
		{
			spr->bank  = (PPU[0] & 0x08) ? 1 : 0;
			spr->tNum  = (oam[j+1]);
		}

		idx = spr->tNum << 4;

		if ( spr->bank )
		{
			chrcache = chrcache1;
			spr->chrAddr = 0x1000 + idx;
		}
		else
		{
			chrcache = chrcache0;
			spr->chrAddr = idx;
		}

		if ( oamPattern.mode8x16 && spr->vFlip )
		{
			t0 = 1; t1 = 0;
		}
		else
		{
			t0 = 0; t1 = 1;
		}
		//printf("OAM:$%02X  TileNum:$%02X  TileAddr:$%04X \n", i, spr->tNum, spr->chrAddr );

		pal = spr->pal * 4;

		for (yy = 0; yy < 8; yy++)
		{
			if ( spr->vFlip )
			{
				y = 7 - yy;
			}
			else
			{
				y = yy;
			}

			chr0 = chrcache[idx];
			chr1 = chrcache[idx + 8];
			tmp = 7;

			for (xx = 0; xx < 8; xx++)
			{
				if ( spr->hFlip )
				{
					x = 7 - xx;
				}
				else
				{
					x = xx;
				}

				p  =  (chr0 >> tmp) & 1;
				p |= ((chr1 >> tmp) & 1) << 1;

				spr->tile[t0].pixel[y][x].val = p;

				p = palcache[p | pal];
				tmp--;
				spr->tile[t0].pixel[y][x].color.setBlue( palo[p].b );
				spr->tile[t0].pixel[y][x].color.setGreen( palo[p].g );
				spr->tile[t0].pixel[y][x].color.setRed( palo[p].r );

				//printf("Tile: %X%X Pixel: (%i,%i)  P:%i RGB: (%i,%i,%i)\n", j, i, x, y, p,
				//	  pattern->tile[j][i].pixel[y][x].color.red(), 
				//	  pattern->tile[j][i].pixel[y][x].color.green(), 
				//	  pattern->tile[j][i].pixel[y][x].color.blue() );
			}
			idx++;
		}
		idx+=8;

		for (yy = 0; yy < 8; yy++)
		{
			if ( spr->vFlip )
			{
				y = 7 - yy;
			}
			else
			{
				y = yy;
			}
			chr0 = chrcache[idx];
			chr1 = chrcache[idx + 8];
			tmp = 7;

			for (xx = 0; xx < 8; xx++)
			{
				if ( spr->hFlip )
				{
					x = 7 - xx;
				}
				else
				{
					x = xx;
				}

				p  =  (chr0 >> tmp) & 1;
				p |= ((chr1 >> tmp) & 1) << 1;

				spr->tile[t1].pixel[y][x].val = p;

				p = palcache[p | pal];
				tmp--;
				spr->tile[t1].pixel[y][x].color.setBlue( palo[p].b );
				spr->tile[t1].pixel[y][x].color.setGreen( palo[p].g );
				spr->tile[t1].pixel[y][x].color.setRed( palo[p].r );

				//printf("Tile: %X%X Pixel: (%i,%i)  P:%i RGB: (%i,%i,%i)\n", j, i, x, y, p,
				//	  pattern->tile[j][i].pixel[y][x].color.red(), 
				//	  pattern->tile[j][i].pixel[y][x].color.green(), 
				//	  pattern->tile[j][i].pixel[y][x].color.blue() );
			}
			idx++;
		}
		idx+=8;

		//if ( oamPattern.mode8x16 )
		//{
		//	spr->chrAddr = 
		//}

		//printf("OAM:%i   (X,Y)=(%3i,%3i)   Bank:%i  Tile:%i\n", i, oam[j], oam[j+3], bank, tile );
		j += 4;
	}
}
//----------------------------------------------------
void FCEUD_UpdatePPUView(int scanline, int refreshchr)
{
	if ( (ppuViewWindow == NULL) && (spriteViewWindow == NULL) )
	{
		return;
	}
	if ( (scanline != -1) && (scanline != PPUViewScanline) )
	{
		return;
	}
	int x,i;

	if (refreshchr)
	{
		int i10, x10;
		for (i = 0, x=0x1000; i < 0x1000; i++, x++)
		{
			i10 = i>>10;
			x10 = x>>10;

			if ( VPage[i10] == NULL )
			{
				continue;
			}
			chrcache0[i] = VPage[i10][i];
			chrcache1[i] = VPage[x10][x];

			if (debug_loggingCD) 
			{
				if (cdloggerVideoDataSize)
				{
					int addr;
					addr = &VPage[i10][i] - CHRptr[0];
					if ((addr >= 0) && (addr < (int)cdloggerVideoDataSize))
						logcache0[i] = cdloggervdata[addr];
					addr = &VPage[x10][x] - CHRptr[0];
					if ((addr >= 0) && (addr < (int)cdloggerVideoDataSize))
						logcache1[i] = cdloggervdata[addr];
				}
			  	else
			  	{
					logcache0[i] = cdloggervdata[i];
					logcache1[i] = cdloggervdata[x];
				}
			}
		}
	}

	if (PPUViewSkip < PPUViewRefresh) 
	{
		PPUViewSkip++;
		return;
	}
	PPUViewSkip = 0;
	
	// update palette only if required
	if ( (palo != NULL) && ( (memcmp(pallast, PALRAM, 32) != 0) || (memcmp(pallast+32, UPALRAM, 3) != 0) ))
	{
		//printf("Updated PPU View Palette\n");
		memcpy(pallast, PALRAM, 32);
		memcpy(pallast+32, UPALRAM, 3);

		// cache palette content
		memcpy(palcache,PALRAM,32);
		palcache[0x10] = palcache[0x00];
		palcache[0x04] = palcache[0x14] = UPALRAM[0];
		palcache[0x08] = palcache[0x18] = UPALRAM[1];
		palcache[0x0C] = palcache[0x1C] = UPALRAM[2];
	}

	DrawPatternTable( &pattern0,chrcache0,logcache0,pindex[0]);
	DrawPatternTable( &pattern1,chrcache1,logcache1,pindex[1]);

	if ( spriteViewWindow != NULL )
	{
		memcpy( oam, SPRAM, 256 );

		drawSpriteTable();
	}
	redrawWindow = true;
}
//----------------------------------------------------
//-- Tile Palette View
//----------------------------------------------------
tilePaletteView_t::tilePaletteView_t(QWidget *parent)
	: QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	viewHeight = 32;
	viewWidth = viewHeight*4;
	setMinimumWidth( viewWidth );
	setMinimumHeight( viewHeight );

	boxWidth  = viewWidth/4;
	boxHeight = viewHeight;
	palIdx = 0;
	selBox = 0;
}
//----------------------------------------------------
tilePaletteView_t::~tilePaletteView_t(void)
{

}
//----------------------------------------------------
void tilePaletteView_t::setIndex( int val )
{
	palIdx = val;
}
//----------------------------------------------------
int  tilePaletteView_t::heightForWidth(int w) const
{
	return w/4;
}
//----------------------------------------------------
QSize tilePaletteView_t::minimumSizeHint(void) const
{
	return QSize(48,12);
}
//----------------------------------------------------
QSize tilePaletteView_t::maximumSizeHint(void) const
{
	return QSize(256,64);
}
//----------------------------------------------------
QSize tilePaletteView_t::sizeHint(void) const
{
	return QSize(128,32);
}
//----------------------------------------------------
QPoint tilePaletteView_t::convPixToCell( QPoint p )
{
	QPoint o;

	o.setX( p.x() / boxWidth );
	o.setY( 0 );

	return o;
}
//----------------------------------------------------
void tilePaletteView_t::mouseMoveEvent(QMouseEvent *event)
{
	QPoint cell = convPixToCell( event->pos() );

	selBox = cell.x();
}
//----------------------------------------------------
void tilePaletteView_t::contextMenuEvent(QContextMenuEvent *event)
{
	QAction *act;
	QMenu menu(this);
	QMenu *subMenu;
	char stmp[128];
	QPoint p;

	p = convPixToCell( event->pos() );

	selBox = p.x();

	act = new QAction(tr("Change Color"), &menu);
	//act->setCheckable(true);
	//act->setChecked(drawTileGrid);
	//act->setShortcut( QKeySequence(tr("G")));
	connect( act, SIGNAL(triggered(void)), this, SLOT(openColorPicker(void)) );
	menu.addAction( act );

	act = new QAction(tr("Export ACT"), &menu);
	//act->setShortcut( QKeySequence(tr("G")));
	connect( act, SIGNAL(triggered(void)), this, SLOT(exportPaletteFileDialog(void)) );
	menu.addAction( act );

	if ( palo )
	{
		int i;

		i = palcache[ (palIdx << 2) | selBox ];

		subMenu = menu.addMenu( tr("Copy Color to Clipboard") );

		sprintf( stmp, "Hex #%02X%02X%02X", palo[i].r, palo[i].g, palo[i].b );
		act = new QAction(tr(stmp), &menu);
		//act->setShortcut( QKeySequence(tr("G")));
		connect( act, SIGNAL(triggered(void)), this, SLOT(copyColor2ClipBoardHex(void)) );
		subMenu->addAction( act );

		sprintf( stmp, "rgb(%3i,%3i,%3i)", palo[i].r, palo[i].g, palo[i].b );
		act = new QAction(tr(stmp), &menu);
		//act->setShortcut( QKeySequence(tr("G")));
		connect( act, SIGNAL(triggered(void)), this, SLOT(copyColor2ClipBoardRGB(void)) );
		subMenu->addAction( act );
	}

	menu.exec(event->globalPos());
}
//----------------------------------------------------
void tilePaletteView_t::copyColor2ClipBoardHex(void)
{
	int p;
	char txt[64];
	QClipboard *clipboard = QGuiApplication::clipboard();

	if ( palo == NULL )
	{
		return;
	}
	p = palcache[ (palIdx << 2) | selBox ];

	sprintf( txt, "#%02X%02X%02X", palo[p].r, palo[p].g, palo[p].b );

	clipboard->setText( tr(txt), QClipboard::Clipboard );

	if ( clipboard->supportsSelection() )
	{
		clipboard->setText( tr(txt), QClipboard::Selection );
	}
}
//----------------------------------------------------
void tilePaletteView_t::copyColor2ClipBoardRGB(void)
{
	int p;
	char txt[64];
	QClipboard *clipboard = QGuiApplication::clipboard();

	if ( palo == NULL )
	{
		return;
	}
	p = palcache[ (palIdx << 2) | selBox ];

	sprintf( txt, "rgb(%3i,%3i,%3i)", palo[p].r, palo[p].g, palo[p].b );

	clipboard->setText( tr(txt), QClipboard::Clipboard );

	if ( clipboard->supportsSelection() )
	{
		clipboard->setText( tr(txt), QClipboard::Selection );
	}
}
//----------------------------------------------------
void tilePaletteView_t::exportPaletteFileDialog(void)
{
	int ret, useNativeFileDialogVal;
	QString filename;
	QFileDialog  dialog(this, tr("Export Palette To File") );
	const char *home;

	dialog.setFileMode(QFileDialog::AnyFile);

	dialog.setNameFilter(tr("Adobe Color Table Files (*.act *.ACT) ;; All files (*)"));

	dialog.setViewMode(QFileDialog::List);
	dialog.setFilter( QDir::AllEntries | QDir::AllDirs | QDir::Hidden );
	dialog.setLabelText( QFileDialog::Accept, tr("Export") );
	dialog.setDefaultSuffix( tr(".act") );

	home = ::getenv("HOME");

	if ( home )
	{
		dialog.setDirectory( tr(home) );
	}

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
	//qDebug() << "selected file path : " << filename.toUtf8();

	exportActivePaletteACT( filename.toStdString().c_str() );
}
//----------------------------------------------------
void tilePaletteView_t::openColorPicker(void)
{
	nesPalettePickerDialog *dialog;

	dialog = new nesPalettePickerDialog( (palIdx << 2) + selBox, this );

	dialog->show();
}
//----------------------------------------------------
void tilePaletteView_t::resizeEvent(QResizeEvent *event)
{
	viewWidth  = event->size().width();
	viewHeight = event->size().height();
}
//----------------------------------------------------
void tilePaletteView_t::paintEvent(QPaintEvent *event)
{
	int x,w,h,xx,yy,p,p2,i,j;
	QPainter painter(this);
	QColor color( 0, 0, 0);
	QColor  white(255,255,255), black(0,0,0);
	//QPen pen;
	//char showSelector;
	char c[4];

	//pen = painter.pen();

	viewWidth  = event->rect().width();
	viewHeight = event->rect().height();

	w = viewWidth  / 4;
  	h = viewHeight;

	//if ( w < h )
	//{
	//	h = w;
	//}
	//else
	//{
	//	w = h;
	//}

	boxWidth  = w;
	boxHeight = h;

	i = w / 4;
	j = h / 4;

	p2 = palIdx << 2;
	yy = 0;
	xx = 0;
	for (x=0; x < 4; x++)
	{
		if ( palo != NULL )
		{
			p = palcache[p2 | x];
			color.setBlue( palo[p].b );
			color.setGreen( palo[p].g );
			color.setRed( palo[p].r );

			c[0] = conv2hex( (p & 0xF0) >> 4 );
			c[1] = conv2hex(  p & 0x0F);
			c[2] =  0;
		}
		painter.fillRect( xx, yy, w, h, color );

		if ( qGray( color.red(), color.green(), color.blue() ) > 128 )
		{
			painter.setPen( black );
		}
		else
		{
			painter.setPen( white );
		}
		painter.drawText( xx+i, yy+h-j, tr(c) );

		painter.setPen( black );
		painter.drawRect( xx, yy, w-1, h-1 );
		painter.setPen( white );
		painter.drawRect( xx+1, yy+1, w-3, h-3 );
		xx += w;
	}
	
	//painter.setPen( black );
	//painter.drawLine( 0, 0   , w*4, 0 );
	//painter.drawLine( 0, h-1 , w*4, h-1 );
	//xx = 0;

	//for (int i=0; i < 5; i++)
	//{
	//	painter.drawLine( xx, 0 , xx, h );

	//	xx += w;
	//}
}
//----------------------------------------------------
//----------------------------------------------------
// PPU Tile Editor
//----------------------------------------------------
//----------------------------------------------------
ppuTileEditor_t::ppuTileEditor_t(int patternIndex, QWidget *parent)
	: QDialog( parent, Qt::Window )
{
	QVBoxLayout *mainLayout;
	QHBoxLayout *hbox;
	QMenuBar *menuBar;
	QMenu *fileMenu, *helpMenu;
	QAction *act;
	QSettings settings;
	int useNativeMenuBar;

	this->setFocusPolicy(Qt::StrongFocus);

	menuBar = new QMenuBar(this);

	// This is needed for menu bar to show up on MacOS
	g_config->getOption( "SDL.UseNativeMenuBar", &useNativeMenuBar );

	menuBar->setNativeMenuBar( useNativeMenuBar ? true : false );

	//-----------------------------------------------------------------------
	// Menu 
	//-----------------------------------------------------------------------
	// File
	fileMenu = menuBar->addMenu(tr("&File"));

	// File -> Close
	act = new QAction(tr("&Close"), this);
	act->setShortcut(QKeySequence::Close);
	act->setStatusTip(tr("Close Window"));
	connect(act, SIGNAL(triggered()), this, SLOT(closeWindow(void)) );
	
	fileMenu->addAction(act);

	// Help
	helpMenu = menuBar->addMenu(tr("&Help"));

	// Help -> Key Assignments
	act = new QAction(tr("Keys"), this);
	//act->setShortcut(QKeySequence::Open);
	act->setStatusTip(tr("View Key Descriptions"));
	connect(act, SIGNAL(triggered()), this, SLOT(showKeyAssignments(void)) );
	
	helpMenu->addAction(act);

	//-----------------------------------------------------------------------
	// End Menu 
	//-----------------------------------------------------------------------

	tileAddr = 0;
	palIdx = pindex[ patternIndex ];

	//ppuViewWindow = this;

	setWindowTitle( tr("PPU Tile Editor") );

	mainLayout = new QVBoxLayout();

	mainLayout->setMenuBar( menuBar );

	setLayout( mainLayout );

	//vbox = new QVBoxLayout();
	//hbox = new QHBoxLayout();

	tileView = new ppuTileView_t( patternIndex, this );

	if ( patternIndex )
	{
		tileView->setPattern( &pattern1 );
	}
	else
	{
		tileView->setPattern( &pattern0 );
	}
	tileView->setPaletteNES( palIdx );

	colorPicker = new ppuTileEditColorPicker_t();

	colorPicker->setPaletteNES( palIdx );

	tileIdxLbl = new QLabel();

	palSelBox = new QComboBox();
	palSelBox->addItem( tr("Tile 0"), 0 );
	palSelBox->addItem( tr("Tile 1"), 1 );
	palSelBox->addItem( tr("Tile 2"), 2 );
	palSelBox->addItem( tr("Tile 3"), 3 );
	palSelBox->addItem( tr("Sprite 0"), 4 );
	palSelBox->addItem( tr("Sprite 1"), 5 );
	palSelBox->addItem( tr("Sprite 2"), 6 );
	palSelBox->addItem( tr("Sprite 3"), 7 );
	palSelBox->addItem( tr("GrayScale"), 8 );

	palSelBox->setCurrentIndex( palIdx );

	connect(palSelBox, SIGNAL(currentIndexChanged(int)), this, SLOT(paletteChanged(int)) );

	mainLayout->addWidget( tileIdxLbl, 1 );
	mainLayout->addWidget( tileView, 100  );
	mainLayout->addWidget( colorPicker, 10  );

	hbox = new QHBoxLayout();
	hbox->addWidget( new QLabel( tr("Palette:") ), 1 );
	hbox->addWidget( palSelBox, 10 );

	mainLayout->addLayout( hbox, 1 );

	updateTimer  = new QTimer( this );

	connect( updateTimer, &QTimer::timeout, this, &ppuTileEditor_t::periodicUpdate );

	updateTimer->start( 100 ); // 10hz

	restoreGeometry(settings.value("ppuTileEditorWindow/geometry").toByteArray());
}
//----------------------------------------------------
ppuTileEditor_t::~ppuTileEditor_t(void)
{
	QSettings settings;
	updateTimer->stop();

	//printf("PPU Tile Editor Window Deleted\n");
	settings.setValue("ppuTileEditorWindow/geometry", saveGeometry());
}
//----------------------------------------------------
void ppuTileEditor_t::closeEvent(QCloseEvent *event)
{
	//printf("PPU Tile Editor Close Window Event\n");
	done(0);
	deleteLater();
	event->accept();
}
//----------------------------------------------------
void ppuTileEditor_t::closeWindow(void)
{
	//printf("Close Window\n");
	done(0);
	deleteLater();
}
//----------------------------------------------------
void ppuTileEditor_t::periodicUpdate(void)
{
	tileView->update();
	colorPicker->update();
}
//----------------------------------------------------
void ppuTileEditor_t::paletteChanged(int index)
{
	palIdx = index;

	tileView->setPaletteNES( palIdx );
	colorPicker->setPaletteNES( palIdx );
	//palSelBox->setCurrentIndex( palIdx );
}
//----------------------------------------------------
void ppuTileEditor_t::setTile( QPoint *t )
{
	if ( (t->x() < 16) && (t->y() < 16) )
	{
		int addr;
		char stmp[64];

		addr = tileView->getPatternIndex() ? 0x1000 : 0x0000;
		addr = addr + ( t->y() * 0x0100 );
		addr = addr + ( t->x() * 0x0010 );

		sprintf( stmp, "Tile Index: $%X%X   Address: $%04X", t->y(), t->x(), addr );
		tileIdxLbl->setText( tr(stmp) );

		tileView->setTile( t );
		tileAddr = addr;
	}
}
//----------------------------------------------------
void ppuTileEditor_t::setCellValue( int y, int x, int colorIndex )
{
	int a;
	unsigned char chr0, chr1, mask, val;

	chr0 = (colorIndex & 0x01) ? 1 : 0;
	chr1 = (colorIndex & 0x02) ? 1 : 0;

	a = tileAddr + y;

	mask = (0x01 << (7-x));

	val = getPPU( a );

	if ( chr0 )
	{
		val = val |  mask;
	}
	else
	{
		val = val & ~mask;
	}
	writeMemPPU( a, val );

	val = getPPU( a+8 );

	if ( chr1 )
	{
		val = val |  mask;
	}
	else
	{
		val = val & ~mask;
	}
	writeMemPPU( a+8, val );

	hexEditorRequestUpdateAll();
}
//----------------------------------------------------
void ppuTileEditor_t::showKeyAssignments(void)
{
	int i;
	QDialog *dialog;
	QVBoxLayout *mainLayout;
	QTreeWidget *tree;
	QTreeWidgetItem *item;
	const char *txt[] = 
	{ 
		"Up"   , "Move Selected Cell Up",
		"Down" , "Move Selected Cell Down",
		"Left" , "Move Selected Cell Left",
		"Right", "Move Selected Cell Right",
		"1"    , "Set Selected Cell to Color #1",
		"2"    , "Set Selected Cell to Color #2",
		"3"    , "Set Selected Cell to Color #3",
		"4"    , "Set Selected Cell to Color #4",
		"P"    , "Cycle to Next Tile Palette",
		"ESC"  , "Close Window",
		NULL
	};

	dialog = new QDialog(this);
	dialog->setWindowTitle("Tile Editor Key Descriptions");
	dialog->resize( 512, 512 );

	tree = new QTreeWidget();

	tree->setColumnCount(2);

	item = new QTreeWidgetItem();
	item->setText( 0, QString::fromStdString( "Key" ) );
	item->setText( 1, QString::fromStdString( "Description" ) );
	item->setTextAlignment( 0, Qt::AlignLeft);
	item->setTextAlignment( 1, Qt::AlignLeft);

	tree->setHeaderItem( item );

	tree->header()->setSectionResizeMode( QHeaderView::ResizeToContents );

	i=0;
	while ( txt[i] != NULL )
	{

		item = new QTreeWidgetItem();

		item->setText( 0, tr(txt[i]) ); i++;
		item->setText( 1, tr(txt[i]) ); i++;

		item->setTextAlignment( 0, Qt::AlignLeft);
		item->setTextAlignment( 1, Qt::AlignLeft);

		tree->addTopLevelItem( item );
	}
	mainLayout = new QVBoxLayout();

	mainLayout->addWidget( tree );

	dialog->setLayout( mainLayout );

	dialog->show();
}
//----------------------------------------------------
void ppuTileEditor_t::keyPressEvent(QKeyEvent *event)
{
	//printf("Tile Editor Key Press: 0x%x \n", event->key() );

	if ( (event->key() >= Qt::Key_1) && (event->key() <= Qt::Key_4) )
	{
		QPoint cell;
		int selColor = event->key() - Qt::Key_1;
		
		colorPicker->setColor( selColor );

		cell = tileView->getSelPix();

		setCellValue( cell.y(), cell.x(), selColor );

		PPUViewSkip = 100;
		FCEUD_UpdatePPUView( -1, 1 );

		event->accept();
	}
	else if ( event->key() == Qt::Key_P )
	{
		palIdx = (palIdx + 1) % 9;
		
		tileView->setPaletteNES( palIdx );
		colorPicker->setPaletteNES( palIdx );
		palSelBox->setCurrentIndex( palIdx );

		event->accept();
	}
	else if ( event->key() == Qt::Key_Up )
	{
		tileView->moveSelBoxUpDown(-1);

		event->accept();
	}
	else if ( event->key() == Qt::Key_Down )
	{
		tileView->moveSelBoxUpDown(1);

		event->accept();
	}
	else if ( event->key() == Qt::Key_Left )
	{
		tileView->moveSelBoxLeftRight(-1);

		event->accept();
	}
	else if ( event->key() == Qt::Key_Right )
	{
		tileView->moveSelBoxLeftRight(1);

		event->accept();
	}
	else if ( event->key() == Qt::Key_Escape )
	{
		closeWindow();

		event->accept();
	}

}
//----------------------------------------------------
//----------------------------------------------------
ppuTileView_t::ppuTileView_t( int patternIndexID, QWidget *parent)
	: QWidget(parent)
{
	this->setFocusPolicy(Qt::StrongFocus);
	this->setMouseTracking(true);
	patternIndex = patternIndexID;
	paletteIndex = 0;
	setMinimumWidth( 256 );
	setMinimumHeight( 256 );
	viewWidth = 256;
	viewHeight = 256;
	tileLabel = NULL;
	drawTileGrid = true;
}
//----------------------------------------------------
void ppuTileView_t::setPattern( ppuPatternTable_t *p )
{
	pattern = p;
}
//----------------------------------------------------
void ppuTileView_t::setPaletteNES( int palIndex )
{
	paletteIndex = palIndex << 2;
}
//----------------------------------------------------
void ppuTileView_t::setTile( QPoint *t )
{
	selTile = *t;
}
//----------------------------------------------------
void ppuTileView_t::setTileLabel( QLabel *l )
{
	tileLabel = l;
}
//----------------------------------------------------
void ppuTileView_t::moveSelBoxUpDown( int i )
{
	int y;

	y = selPix.y();

	y = y + (i%8);

	if ( y < 0 )
	{
		y = y + 8;
	}
	else if ( y >= 8 )
	{
		y = y - 8;
	}

	selPix.setY(y);

}
//----------------------------------------------------
void ppuTileView_t::moveSelBoxLeftRight( int i )
{
	int x;

	x = selPix.x();

	x = x + (i%8);

	if ( x < 0 )
	{
		x = x + 8;
	}
	else if ( x >= 8 )
	{
		x = x - 8;
	}

	selPix.setX(x);

}
//----------------------------------------------------
void ppuTileView_t::setSelCell( QPoint &p )
{
	selPix = p;
}
//----------------------------------------------------
ppuTileView_t::~ppuTileView_t(void)
{

}
//----------------------------------------------------
QPoint ppuTileView_t::convPixToCell( QPoint p )
{
	QPoint t(0,0);
	int x,y,w,h,i,j;

	x = p.x(); y = p.y();

	w = boxWidth;
	h = boxHeight;

	i = x / w;
	j = y / h;

	//printf("(x,y) = (%i,%i) w=%i h=%i  $%X%X \n", x, y, w, h, j, i );

	t.setX(i);
	t.setY(j);

	return t;
}
//----------------------------------------------------
void ppuTileView_t::resizeEvent(QResizeEvent *event)
{
	viewWidth  = event->size().width();
	viewHeight = event->size().height();

	boxWidth  = viewWidth / 8;
  	boxHeight = viewHeight / 8;
}
//----------------------------------------------------
void ppuTileView_t::keyPressEvent(QKeyEvent *event)
{
	//printf("Tile View Key Press: 0x%x \n", event->key() );

	//event->accept();
	event->ignore();
}
//----------------------------------------------------
void ppuTileView_t::mouseMoveEvent(QMouseEvent *event)
{
//	QPoint cell = convPixToCell( event->pos() );
}
//----------------------------------------------------------------------------
void ppuTileView_t::mousePressEvent(QMouseEvent * event)
{
	QPoint cell = convPixToCell( event->pos() );

	if ( event->button() == Qt::LeftButton )
	{
		// Set Cell
		setSelCell( cell );
	}
}
//----------------------------------------------------
void ppuTileView_t::contextMenuEvent(QContextMenuEvent *event)
{
//	QAction *act;
//	QMenu menu(this);
//   QMenu *subMenu;
//	QActionGroup *group;
//	QAction *paletteAct[9];
//   char stmp[64];
//
//   if ( mode )
//   {
//      sprintf( stmp, "Exit Tile View: %X%X", selTile.y(), selTile.x() );
//
//      act = new QAction(tr(stmp), &menu);
//      act->setShortcut( QKeySequence(tr("Z")));
//	   connect( act, SIGNAL(triggered(void)), this, SLOT(exitTileMode(void)) );
//      menu.addAction( act );
//
//      act = new QAction(tr("Draw Tile Grid Lines"), &menu);
//      act->setCheckable(true);
//      act->setChecked(drawTileGrid);
//      act->setShortcut( QKeySequence(tr("G")));
//	   connect( act, SIGNAL(triggered(void)), this, SLOT(toggleTileGridLines(void)) );
//      menu.addAction( act );
//   }
//   else
//   {
//      sprintf( stmp, "View Tile: %X%X", selTile.y(), selTile.x() );
//
//      act = new QAction(tr(stmp), &menu);
//      act->setShortcut( QKeySequence(tr("Z")));
//	   connect( act, SIGNAL(triggered(void)), this, SLOT(showTileMode(void)) );
//      menu.addAction( act );
//   }
//
//	act = new QAction(tr("Open Tile Editor"), &menu);
//   act->setShortcut( QKeySequence(tr("O")));
//	connect( act, SIGNAL(triggered(void)), this, SLOT(openTileEditor(void)) );
//   menu.addAction( act );
//
//   act = new QAction(tr("Next Palette"), &menu);
//   act->setShortcut( QKeySequence(tr("P")));
//	connect( act, SIGNAL(triggered(void)), this, SLOT(cycleNextPalette(void)) );
//   menu.addAction( act );
//
//	subMenu = menu.addMenu(tr("Palette Select"));
//	group   = new QActionGroup(&menu);
//
//	group->setExclusive(true);
//
//	for (int i=0; i<9; i++)
//	{
//	   char stmp[8];
//
//	   sprintf( stmp, "%i", i+1 );
//
//	   paletteAct[i] = new QAction(tr(stmp), &menu);
//	   paletteAct[i]->setCheckable(true);
//
//	   group->addAction(paletteAct[i]);
//		subMenu->addAction(paletteAct[i]);
//      
//	   paletteAct[i]->setChecked( pindex[ patternIndex ] == i );
//	}
//
//   connect( paletteAct[0], SIGNAL(triggered(void)), this, SLOT(selPalette0(void)) );
//   connect( paletteAct[1], SIGNAL(triggered(void)), this, SLOT(selPalette1(void)) );
//   connect( paletteAct[2], SIGNAL(triggered(void)), this, SLOT(selPalette2(void)) );
//   connect( paletteAct[3], SIGNAL(triggered(void)), this, SLOT(selPalette3(void)) );
//   connect( paletteAct[4], SIGNAL(triggered(void)), this, SLOT(selPalette4(void)) );
//   connect( paletteAct[5], SIGNAL(triggered(void)), this, SLOT(selPalette5(void)) );
//   connect( paletteAct[6], SIGNAL(triggered(void)), this, SLOT(selPalette6(void)) );
//   connect( paletteAct[7], SIGNAL(triggered(void)), this, SLOT(selPalette7(void)) );
//   connect( paletteAct[8], SIGNAL(triggered(void)), this, SLOT(selPalette8(void)) );
//
//   menu.exec(event->globalPos());
}
//----------------------------------------------------
void ppuTileView_t::paintEvent(QPaintEvent *event)
{
	int x,y,w,h,xx,yy,ii,jj;
	QPainter painter(this);
	QColor   color[4];
	QPen     pen;

	pen = painter.pen();

	viewWidth  = event->rect().width();
	viewHeight = event->rect().height();

	//printf("PPU TileView %ix%i \n", viewWidth, viewHeight );
	
	for (x=0; x < 4; x++)
	{
		color[x].setBlue( palo[palcache[paletteIndex|x]].b );
		color[x].setGreen( palo[palcache[paletteIndex|x]].g );
		color[x].setRed( palo[palcache[paletteIndex|x]].r );
	}

	w = viewWidth / 8;
  	h = viewHeight / 8;

	boxWidth = w;
	boxHeight = h;

	xx = 0; yy = 0;

	if ( w < h )
	{
	   h = w;
	}
	else
	{
	   w = h;
	}
	
	ii = selTile.x();
	jj = selTile.y();
	
	// Draw Tile Pixels as rectangles
	for (x=0; x < 8; x++)
	{
		yy = 0;
	
		for (y=0; y < 8; y++)
		{
			painter.fillRect( xx, yy, w, h, color[ pattern->tile[jj][ii].pixel[y][x].val & 0x03 ] );
			yy += h;
		}
		xx += w;
	}
	
	if ( drawTileGrid )
	{
		pen.setWidth( 1 );
		pen.setColor( QColor(128,128,128) );
		painter.setPen( pen );

		// Draw Tile Pixel grid lines
		xx = 0; y = 8*h;
		
		for (x=0; x<9; x++)
		{
		           painter.drawLine( xx, 0 , xx, y ); xx += w;
		}
		yy = 0; x = 8*w;
		
		for (y=0; y<9; y++)
		{
		           painter.drawLine( 0, yy , x, yy ); yy += h;
		}
	}

	x = selPix.x() * w;
	y = selPix.y() * h;

	pen.setWidth( 6 );
	pen.setColor( QColor(  0,  0,  0) );
	painter.setPen( pen );

	painter.drawRect( x, y, w, h );

	pen.setWidth( 2 );
	pen.setColor( QColor(255,  0,  0) );
	painter.setPen( pen );

	painter.drawRect( x, y, w, h );
}
//----------------------------------------------------
ppuTileEditColorPicker_t::ppuTileEditColorPicker_t(QWidget *parent)
	: QWidget(parent)
{
	int boxPixSize = 64;
	this->setFocusPolicy(Qt::StrongFocus);
	this->setMouseTracking(true);

	font.setFamily("Courier New");
	font.setStyle( QFont::StyleNormal );
	font.setStyleHint( QFont::Monospace );
	font.setPixelSize( boxPixSize / 3 );
	QFontMetrics fm(font);

	setMinimumWidth( boxPixSize * NUM_COLORS );
	setMinimumHeight( boxPixSize );

	viewWidth  = boxPixSize * NUM_COLORS;
	viewHeight = boxPixSize;

	boxWidth  = viewWidth / NUM_COLORS;
	boxHeight = viewHeight;

	selColor = 0;
	paletteIndex = 0;
	//paletteIY = 0;
	//paletteIX = 0;

	#if QT_VERSION > QT_VERSION_CHECK(5, 11, 0)
	pxCharWidth = fm.horizontalAdvance(QLatin1Char('2'));
	#else
	pxCharWidth = fm.width(QLatin1Char('2'));
	#endif
	pxCharHeight = fm.height();
	//pxCharHeight = fm.lineSpacing();
}
//----------------------------------------------------
ppuTileEditColorPicker_t::~ppuTileEditColorPicker_t(void)
{

}
//----------------------------------------------------
QPoint ppuTileEditColorPicker_t::convPixToTile( QPoint p )
{
	QPoint t(0,0);

	t.setX( p.x() / boxWidth );
	t.setY( p.y() / boxHeight );

	return t;
}
//----------------------------------------------------
void  ppuTileEditColorPicker_t::setColor( int colorIndex )
{
	//printf("Setting color to: %i \n", colorIndex );
	selColor = colorIndex;
}
//----------------------------------------------------
void  ppuTileEditColorPicker_t::setPaletteNES( int palIndex )
{
	paletteIndex = palIndex << 2;
}
//----------------------------------------------------
void ppuTileEditColorPicker_t::resizeEvent(QResizeEvent *event)
{
	viewWidth  = event->size().width();
	viewHeight = event->size().height();

	boxWidth  = viewWidth / NUM_COLORS;
	boxHeight = viewHeight;
}
//----------------------------------------------------
void ppuTileEditColorPicker_t::mouseMoveEvent(QMouseEvent *event)
{
	//QPoint tile = convPixToTile( event->pos() );

	//if ( (tile.x() < NUM_COLORS) && (tile.y() < PALETTEHEIGHT) )
	//{
	//	char stmp[64];
	//	int ix = (tile.y()<<4)|tile.x();

	//	sprintf( stmp, "Palette: $%02X", palcache[ix]);

	//	frame->setTitle( tr(stmp) );
	//}
}
//----------------------------------------------------------------------------
void ppuTileEditColorPicker_t::mousePressEvent(QMouseEvent * event)
{
	//QPoint tile = convPixToTile( event->pos() );

	//if ( event->button() == Qt::LeftButton )
	//{
	//}
	//else if ( event->button() == Qt::RightButton )
	//{
	//}
}
//----------------------------------------------------
void ppuTileEditColorPicker_t::paintEvent(QPaintEvent *event)
{
	int x,y,w,h,xx,yy;
	QPainter painter(this);
	viewWidth  = event->rect().width();
	viewHeight = event->rect().height();
	QColor color[NUM_COLORS];
	QPen pen;

	painter.setFont(font);

	pen = painter.pen();

	//printf("PPU PatternView %ix%i \n", viewWidth, viewHeight );

	w = boxWidth;
  	h = boxHeight;

	yy = 0;
	xx = 0;

	y=0;
	for (x=0; x < NUM_COLORS; x++)
	{
		color[x].setBlue( palo[palcache[paletteIndex|x]].b );
		color[x].setGreen( palo[palcache[paletteIndex|x]].g );
		color[x].setRed( palo[palcache[paletteIndex|x]].r );

		painter.fillRect( xx, yy, w, h, color[x] );
		xx += w;
	}

	y = h;
	for (int i=0; i<=NUM_COLORS; i++)
	{
		x = i*w; 
		painter.drawLine( x, 0 , x, y );
	}
	
	x = NUM_COLORS*w; 
	for (int i=0; i<=1; i++)
	{
		y = i*h;
		painter.drawLine( 0, y, x, y );
	}

	pen.setWidth( 6 );
	painter.setPen( pen );

	x = selColor * w;
	painter.drawRect( x+3, 3, w-6, h-6 );

	pen.setWidth( 2 );
	pen.setColor( QColor(255,255,255) );
	painter.setPen( pen );
	painter.drawRect( x+3, 3, w-6, h-6 );

	y = (pxCharHeight) + (h - pxCharHeight) / 2;

	for (int i=0; i<NUM_COLORS; i++)
	{
		char c[2];

		x = (i * w) + (w - pxCharWidth)/2;

		c[0] = '1' + i;
		c[1] =  0;

		if ( qGray( color[i].red(), color[i].green(), color[i].blue() ) > 128 )
	        {
			painter.setPen( QColor(  0,  0,  0) );
		}
		else
		{
			painter.setPen( QColor(255,255,255) );
	        }
		painter.drawText( x, y, tr(c) );
	}
}
//----------------------------------------------------
//----------------------------------------------------
//--- Sprite Viewer Object
//----------------------------------------------------
//----------------------------------------------------
spriteViewerDialog_t::spriteViewerDialog_t(QWidget *parent)
	: QDialog(parent, Qt::Window)
{
	QSettings    settings;
	QMenuBar    *menuBar;
	QVBoxLayout *mainLayout, *vbox, *vbox1, *vbox2, *vbox3;
	QHBoxLayout *hbox, *hbox1, *hbox2;
	QGridLayout *grid;
	QGroupBox   *frame;
	QLabel      *lbl;
	QActionGroup *group;
	QMenu *fileMenu, *viewMenu, *colorMenu, *optMenu, *subMenu;
	QAction *act;
	QFont font;
	//char stmp[64];
	int useNativeMenuBar, pxCharWidth, opt;
	ColorMenuItem *selTileColorAct, *gridColorAct, *locColorAct;

	spriteViewWindow = this;

	oamView  = new oamPatternView_t(this);
	tileView = new oamTileView_t(this);
	palView  = new oamPaletteView_t(this);
	preView  = new oamPreview_t(this);

	menuBar = new QMenuBar(this);

	// This is needed for menu bar to show up on MacOS
	g_config->getOption( "SDL.UseNativeMenuBar", &useNativeMenuBar );

	menuBar->setNativeMenuBar( useNativeMenuBar ? true : false );

	//-----------------------------------------------------------------------
	// Menu 
	//-----------------------------------------------------------------------
	// File
	fileMenu = menuBar->addMenu(tr("&File"));

	// File -> Close
	act = new QAction(tr("&Close"), this);
	act->setShortcut(QKeySequence::Close);
	act->setStatusTip(tr("Close Window"));
	connect(act, SIGNAL(triggered()), this, SLOT(closeWindow(void)) );
	
	fileMenu->addAction(act);

	// View
	viewMenu = menuBar->addMenu(tr("&View"));

	// View -> Toggle Grid
	act = new QAction(tr("Toggle &Grid"), this);
	//act->setShortcut(QKeySequence::Close);
	act->setStatusTip(tr("Toggle Grid"));
	connect(act, SIGNAL(triggered()), this, SLOT(toggleGridVis(void)) );
	
	viewMenu->addAction(act);

	colorMenu = menuBar->addMenu(tr("&Color"));

	// Color -> Selector
	selTileColorAct = new ColorMenuItem(tr("&Selector"), "SDL.OAM_TileSelColor", this);
	selTileColorAct->connectColor( &oamView->selTileColor );
	
	colorMenu->addAction(selTileColorAct);

	// Color -> Grid
	gridColorAct = new ColorMenuItem(tr("&Grid"), "SDL.OAM_TileGridColor", this);
	gridColorAct->connectColor( &oamView->gridColor );
	
	colorMenu->addAction(gridColorAct);

	// Color -> Locator
	locColorAct = new ColorMenuItem(tr("&Locator Box"), "SDL.OAM_LocatorColor", this);
	locColorAct->connectColor( &preView->boxColor );
	
	colorMenu->addAction(locColorAct);

	// View -> Show Preview
	//act = new QAction(tr("Show &Preview"), this);
	//act->setShortcut(QKeySequence::Close);
	//act->setCheckable(true);
	//act->setStatusTip(tr("Show Preview Area"));
	//connect(act, SIGNAL(triggered(bool)), this, SLOT(togglePreviewVis(bool)) );
	//
	//viewMenu->addAction(act);

	// View -> Preview Size
	//subMenu = viewMenu->addMenu(tr("Preview &Size"));
	//group   = new QActionGroup(this);
	//group->setExclusive(true);

	//act = new QAction(tr("&1x"), this);
	//act->setCheckable(true);
	//act->setChecked(true);
	//group->addAction(act);
	//subMenu->addAction(act);
	//connect(act, SIGNAL(triggered()), this, SLOT(setPreviewSize1x(void)) );

	//act = new QAction(tr("&2x"), this);
	//act->setCheckable(true);
	//act->setChecked(false);
	//group->addAction(act);
	//subMenu->addAction(act);
	//connect(act, SIGNAL(triggered()), this, SLOT(setPreviewSize2x(void)) );

	// Focus Policy
	optMenu = menuBar->addMenu(tr("&Options"));

	// Options -> Focus
	subMenu = optMenu->addMenu(tr("&Focus Policy"));
	group   = new QActionGroup(this);
	group->setExclusive(true);

	act = new QAction(tr("&Click"), this);
	act->setCheckable(true);
	act->setChecked( !oamView->getHoverFocus() );
	group->addAction(act);
	subMenu->addAction(act);
	connect(act, SIGNAL(triggered()), this, SLOT(setClickFocus(void)) );

	act = new QAction(tr("&Hover"), this);
	act->setCheckable(true);
	act->setChecked( oamView->getHoverFocus() );
	group->addAction(act);
	subMenu->addAction(act);
	connect(act, SIGNAL(triggered()), this, SLOT(setHoverFocus(void)) );
	
	//-----------------------------------------------------------------------
	// End Menu 
	//-----------------------------------------------------------------------
	
	// Monospace Font for Data Display Fields (LineEdits)
	font.setFamily("Courier New");
	font.setStyle( QFont::StyleNormal );
	font.setStyleHint( QFont::Monospace );

	QFontMetrics metrics(font);
#if QT_VERSION > QT_VERSION_CHECK(5, 11, 0)
	pxCharWidth = metrics.horizontalAdvance(QLatin1Char('2'));
#else
	pxCharWidth = metrics.width(QLatin1Char('2'));
#endif

	setWindowTitle( tr("Sprite Viewer") );

	mainLayout = new QVBoxLayout();

	mainLayout->setMenuBar( menuBar );

	setLayout( mainLayout );

	useSprRam = new QRadioButton( tr("Sprite RAM") );
	useCpuPag = new QRadioButton( tr("CPU Page #") );
	cpuPagIdx = new QSpinBox(this);

	g_config->getOption("SDL.PPU_ViewScanLine", &PPUViewScanline);

	scanLineEdit = new QSpinBox(this);
	scanLineEdit->setRange( 0, 255 );
	scanLineEdit->setValue( PPUViewScanline );

	connect( scanLineEdit, SIGNAL(valueChanged(int)), this, SLOT(scanLineChanged(int)));

	useSprRam->setChecked(true);
	useSprRam->setEnabled(false); // TODO Implement CPU paging option
	cpuPagIdx->setEnabled(false);
	useCpuPag->setEnabled(false);

	hFlipBox = new QCheckBoxRO( tr("Horizontal Flip") );
	hFlipBox->setFocusPolicy(Qt::NoFocus);

	vFlipBox = new QCheckBoxRO( tr("Vertical Flip") );
	vFlipBox->setFocusPolicy(Qt::NoFocus);

	bgPrioBox = new QCheckBoxRO( tr("Background Priority") );
	bgPrioBox->setFocusPolicy(Qt::NoFocus);

	spriteIndexBox = new QLineEdit();
	spriteIndexBox->setFont(font);
	spriteIndexBox->setReadOnly(true);
	spriteIndexBox->setMinimumWidth( 4 * pxCharWidth );

	tileAddrBox = new QLineEdit();
	tileAddrBox->setFont(font);
	tileAddrBox->setReadOnly(true);
	tileAddrBox->setMinimumWidth( 6 * pxCharWidth );

	tileIndexBox   = new QLineEdit();
	tileIndexBox->setFont(font);
	tileIndexBox->setReadOnly(true);
	tileIndexBox->setMinimumWidth( 4 * pxCharWidth );

	palAddrBox = new QLineEdit();
	palAddrBox->setFont(font);
	palAddrBox->setReadOnly(true);
	palAddrBox->setMinimumWidth( 6 * pxCharWidth );

	posBox   = new QLineEdit();
	posBox->setFont(font);
	posBox->setReadOnly(true);
	posBox->setMinimumWidth( 10 * pxCharWidth );

	showPosHex = new QCheckBox( tr("Show Position in Hex") );

	hbox1 = new QHBoxLayout();
	vbox3 = new QVBoxLayout();
	hbox  = new QHBoxLayout();
	hbox1->addLayout( vbox3 );
	vbox3->addWidget( oamView );
	vbox3->addLayout( hbox );
	hbox->addWidget( new QLabel( tr("Display on Scanline:") ), 1 );
	hbox->addWidget( scanLineEdit, 1 );
	hbox->addStretch(5);

	mainLayout->addLayout( hbox1 );

	vbox1 = new QVBoxLayout();
	hbox1->addLayout( vbox1);

	vbox2 = new QVBoxLayout();

	hbox  = new QHBoxLayout();
	vbox1->addLayout( hbox, 1 );

	hbox->addWidget( new QLabel( tr("Data Source:") ) );
	hbox->addWidget( useSprRam );
	hbox->addWidget( useCpuPag );
	hbox->addWidget( cpuPagIdx );

	frame    = new QGroupBox( tr("Sprite Info") );
	grid     = new QGridLayout();
	vbox1->addWidget( frame, 1 );
	frame->setLayout( vbox2 );

	hbox2    = new QHBoxLayout();
	frame    = new QGroupBox( tr("Tile:") );
	hbox     = new QHBoxLayout();
	frame->setLayout( hbox );
	hbox->addWidget( tileView );
	vbox2->addLayout( hbox2 );
	hbox2->addWidget( frame );
	hbox2->addLayout( grid  );
	
	vbox     = new QVBoxLayout();
	hbox->addLayout( vbox );

	vbox->addWidget( hFlipBox );

	vbox->addWidget( vFlipBox );

	vbox->addWidget( bgPrioBox );

	frame    = new QGroupBox( tr("Palette:") );
	hbox     = new QHBoxLayout();
  	hbox->addWidget( palView );
	frame->setLayout( hbox );
	vbox->addWidget( frame );

	frame    = new QGroupBox( tr("Preview:") );
	vbox     = new QVBoxLayout();
	vbox->addWidget( preView );
	frame->setLayout( vbox );
	vbox1->addWidget( frame, 10 );


	lbl      = new QLabel( tr("Sprite Index:") );
	grid->addWidget( lbl, 0, 0 );
	grid->addWidget( spriteIndexBox, 0, 1 );

	lbl      = new QLabel( tr("Tile Address:") );
	grid->addWidget( lbl, 1, 0 );
	grid->addWidget( tileAddrBox, 1, 1 );

	lbl      = new QLabel( tr("Tile Index:") );
	grid->addWidget( lbl, 2, 0 );
	grid->addWidget( tileIndexBox, 2, 1 );

	lbl      = new QLabel( tr("Palette Address:") );
	grid->addWidget( lbl, 3, 0 );
	grid->addWidget( palAddrBox, 3, 1 );

	lbl      = new QLabel( tr("Position (X,Y):") );
	grid->addWidget( lbl, 4, 0 );
	grid->addWidget( posBox, 4, 1 );

	grid->addWidget( showPosHex, 5, 0, 1, 2 );

	updateTimer  = new QTimer( this );

	connect( updateTimer, &QTimer::timeout, this, &spriteViewerDialog_t::periodicUpdate );

	updateTimer->start( 33 ); // 30hz

	resize( minimumSizeHint() );

	restoreGeometry(settings.value("spriteViewer/geometry").toByteArray());

	connect( this, SIGNAL(rejected(void)), this, SLOT(deleteLater(void)));

	g_config->getOption("SDL.OAM_ShowPosHex", &opt);
	showPosHex->setChecked( opt );
}
//----------------------------------------------------
spriteViewerDialog_t::~spriteViewerDialog_t(void)
{
	if ( this == spriteViewWindow )
	{
		spriteViewWindow = NULL;
	}
	g_config->setOption("SDL.OAM_ShowPosHex", showPosHex->isChecked() );
}
//----------------------------------------------------
void spriteViewerDialog_t::closeEvent(QCloseEvent *event)
{
	QSettings settings;
	//printf("Sprite Viewer Close Window Event\n");
	settings.setValue("spriteViewer/geometry", saveGeometry());
	done(0);
	deleteLater();
	event->accept();
}
//----------------------------------------------------
void spriteViewerDialog_t::closeWindow(void)
{
	QSettings settings;
	//printf("Close Window\n");
	settings.setValue("spriteViewer/geometry", saveGeometry());
	done(0);
	deleteLater();
}
//----------------------------------------------------
void spriteViewerDialog_t::setClickFocus(void)
{
	oamView->setHover2Focus(false);
}
//----------------------------------------------------
void spriteViewerDialog_t::setHoverFocus(void)
{
	oamView->setHover2Focus(true);
}
//----------------------------------------------------
void spriteViewerDialog_t::toggleGridVis(void)
{
	oamView->setGridVisibility( !oamView->getGridVisibility() );
}
//----------------------------------------------------
void spriteViewerDialog_t::scanLineChanged(int value)
{
	PPUViewScanline = value;
	//printf("ScanLine: %i\n", PPUViewScanline );
	g_config->setOption("SDL.PPU_ViewScanLine", PPUViewScanline);
}
//----------------------------------------------------
void spriteViewerDialog_t::periodicUpdate(void)
{
	int idx;
	char stmp[32];

	//return;

	idx = oamView->getSpriteIndex();

	sprintf( stmp, "$%02X", idx );
	spriteIndexBox->setText( tr(stmp) );

	sprintf( stmp, "$%02X", oamPattern.sprite[idx].tNum );
	tileIndexBox->setText( tr(stmp) );

	sprintf( stmp, "$%04X", oamPattern.sprite[idx].chrAddr );
	tileAddrBox->setText( tr(stmp) );

	sprintf( stmp, "$%04X", 0x3F00 + (oamPattern.sprite[idx].pal*4) );
	palAddrBox->setText( tr(stmp) );

	if ( showPosHex->isChecked() )
	{
		sprintf( stmp, "$%02X, $%02X", oamPattern.sprite[idx].x, oamPattern.sprite[idx].y );
	}
	else
	{
		sprintf( stmp, "%3i, %3i", oamPattern.sprite[idx].x, oamPattern.sprite[idx].y );
	}
	posBox->setText( tr(stmp) );

	if ( scanLineEdit->value() != PPUViewScanline )
	{
		scanLineEdit->setValue( PPUViewScanline );
	}

	hFlipBox->setChecked( oamPattern.sprite[idx].hFlip );
	vFlipBox->setChecked( oamPattern.sprite[idx].vFlip );
	bgPrioBox->setChecked( oamPattern.sprite[idx].pri  );

	tileView->setIndex(idx);
	tileView->setIndex(idx);
	palView->setIndex(idx);
	preView->setIndex(idx);

	oamView->update();
	tileView->update();
	palView->update();
	preView->update();
}
//----------------------------------------------------
//-- OAM Pattern Viewer
//----------------------------------------------------
oamPatternView_t::oamPatternView_t( QWidget *parent )
	: QWidget( parent )
{
	this->setFocusPolicy(Qt::StrongFocus);
	this->setMouseTracking(true);
	setMinimumWidth( 256 );
	setMinimumHeight( 512 );
	viewWidth = 256;
	viewHeight = 512;
	hover2Focus  = false;
	showGrid = false;

	selTileColor.setRgb(255,255,255);
	gridColor.setRgb(128,128,128);
	
	selSprite.setX(0);
	selSprite.setY(0);
	spriteIdx = 0;

	fceuLoadConfigColor("SDL.OAM_TileSelColor"  , &selTileColor  );
	fceuLoadConfigColor("SDL.OAM_TileGridColor" , &gridColor  );

	g_config->getOption("SDL.OAM_TileShowGrid", &showGrid);
	g_config->getOption("SDL.OAM_TileFocusPolicy", &hover2Focus );
}
//----------------------------------------------------
oamPatternView_t::~oamPatternView_t(void)
{

}
//----------------------------------------------------
void oamPatternView_t::setHover2Focus(bool val)
{
	hover2Focus = val;

	g_config->setOption("SDL.OAM_TileFocusPolicy", hover2Focus );
}
//----------------------------------------------------
void oamPatternView_t::setGridVisibility(bool val)
{
	showGrid = val;
	g_config->setOption("SDL.OAM_TileShowGrid", showGrid);
}
//----------------------------------------------------
int oamPatternView_t::getSpriteIndex(void){ return spriteIdx; }
//----------------------------------------------------
int oamPatternView_t::heightForWidth(int w) const
{
	return 2*w;
}
//----------------------------------------------------
QSize oamPatternView_t::minimumSizeHint(void) const
{
	return QSize(256,512);
}
//----------------------------------------------------
QSize oamPatternView_t::maximumSizeHint(void) const
{
	return QSize(512,1024);
}
//----------------------------------------------------
QSize oamPatternView_t::sizeHint(void) const
{
	return QSize(384,768);
}
//----------------------------------------------------
void oamPatternView_t::openTilePpuViewer(void)
{
	int pTable,x,y,tileAddr;

	tileAddr = oamPattern.sprite[ spriteIdx ].chrAddr;

	pTable = tileAddr >= 0x1000;
	y = (tileAddr & 0x0F00) >> 8;
	x = (tileAddr & 0x00F0) >> 4;

	openPPUViewWindow( consoleWindow );

	//printf("TileAddr: %04X   %i,%X%X\n", tileAddr, pTable, x, y );

	setPPUSelPatternTile(  pTable,  x,  y );
	setPPUSelPatternTile( !pTable, -1, -1 );
}
//----------------------------------------------------
void oamPatternView_t::resizeEvent(QResizeEvent *event)
{
	viewWidth  = event->size().width();
	viewHeight = event->size().height();
}
//----------------------------------------------------
QPoint oamPatternView_t::convPixToTile( QPoint p )
{
	QPoint t(0,0);
	int x,y,w,h,i,j;

	x = p.x(); y = p.y();

	w = oamPattern.w;
	h = oamPattern.h;

	i = w == 0 ? 0 : x / (w*8);
	j = h == 0 ? 0 : y / (h*16);

	//printf("(x,y) = (%i,%i) w=%i h=%i  $%X%X \n", x, y, w, h, jj, ii );

	t.setX(i);
	t.setY(j);

	return t;
}
//----------------------------------------------------
void oamPatternView_t::keyPressEvent(QKeyEvent *event)
{
	int x,y;

	if ( event->key() == Qt::Key_Up )
	{
		y = selSprite.y();

		y = (y - 1);

		if ( y < 0 )
		{
			y = 7;
		}
		
		selSprite.setY(y);
		spriteIdx = selSprite.y()*8 + selSprite.x();
	}
	else if ( event->key() == Qt::Key_Down )
	{
		y = selSprite.y();

		y = (y + 1);

		if ( y > 7 )
		{
			y = 0;
		}
		
		selSprite.setY(y);
		spriteIdx = selSprite.y()*8 + selSprite.x();
	}
	else if ( event->key() == Qt::Key_Left )
	{
		x = selSprite.x();

		x = (x - 1);

		if ( x < 0 )
		{
			x = 7;
		}
		
		selSprite.setX(x);
		spriteIdx = selSprite.y()*8 + selSprite.x();
	}
	else if ( event->key() == Qt::Key_Right )
	{
		x = selSprite.x();

		x = (x + 1);

		if ( x > 7 )
		{
			x = 0;
		}
		
		selSprite.setX(x);
		spriteIdx = selSprite.y()*8 + selSprite.x();
	}

}
//----------------------------------------------------
void oamPatternView_t::mouseMoveEvent(QMouseEvent *event)
{
	QPoint tile = convPixToTile( event->pos() );

	//printf("Tile: (%i,%i) = %i \n", tile.x(), tile.y(), tile.y()*8 + tile.x() );
	
	if ( hover2Focus )
	{
		if ( (tile.x() >= 0) && (tile.x() < 8) &&
			(tile.y() >= 0) && (tile.y() < 8) )
		{
			selSprite = tile;
			spriteIdx = tile.y()*8 + tile.x();
			//printf("Tile: (%i,%i) = %i \n", tile.x(), tile.y(), tile.y()*8 + tile.x() );
		}
	}


}
//----------------------------------------------------
void oamPatternView_t::mousePressEvent(QMouseEvent *event)
{
	QPoint tile = convPixToTile( event->pos() );

	if ( event->button() == Qt::LeftButton )
	{
		if ( (tile.x() >= 0) && (tile.x() < 8) &&
			(tile.y() >= 0) && (tile.y() < 8) )
		{
			selSprite = tile;
			spriteIdx = tile.y()*8 + tile.x();
			//printf("Tile: (%i,%i) = %i \n", tile.x(), tile.y(), tile.y()*8 + tile.x() );
		}
	}
}
//----------------------------------------------------
void oamPatternView_t::contextMenuEvent(QContextMenuEvent *event)
{
	QAction *act;
	QMenu menu(this);
	//QMenu *subMenu;
	//QActionGroup *group;
	//char stmp[64];

	act = new QAction(tr("Open PPU CHR &Viewer"), &menu);
	//act->setShortcut( QKeySequence(tr("E")));
	connect( act, SIGNAL(triggered(void)), this, SLOT(openTilePpuViewer(void)) );
	menu.addAction( act );

	menu.exec(event->globalPos());
}
//----------------------------------------------------
void oamPatternView_t::paintEvent(QPaintEvent *event)
{
	int i,j,x,y,w,h,xx,yy,ii,jj;
	QPainter painter(this);
	QPen pen;
	//char showSelector;

	pen = painter.pen();

	viewWidth  = event->rect().width();
	viewHeight = event->rect().height();

	w = viewWidth  / 64;
  	h = viewHeight / 128;

	if ( w < h )
	{
		h = w;
	}
	else
	{
		w = h;
	}

	oamPattern.w = w;
	oamPattern.h = h;

	for (i=0; i<64; i++)
	{
		ii = (i % 8) * (w*8);
		jj = (i / 8) * (h*16);

		for (j=0; j<2; j++)
		{
			xx = ii;

			for (x=0; x<8; x++)
			{
				yy = jj + (j*h*8);

				for (y=0; y < 8; y++)
				{
					painter.fillRect( xx, yy, w, h, oamPattern.sprite[i].tile[j].pixel[y][x].color );
					yy += h;
				}
				xx += w;
			}
		}
	}

	if ( showGrid )
	{
		int tw, th;
		pen.setWidth( 1 );
		pen.setColor( gridColor );
		painter.setPen( pen );

		tw=  8*w;
		th= 16*h;

		xx = 0;
		y = 8*th;

		for (x=0; x<=8; x++)
		{
			painter.drawLine( xx, 0 , xx, y ); xx += tw;
		}

		yy = 0;
		x = 8*tw;

		for (y=0; y<=8; y++)
		{
			painter.drawLine( 0, yy , x, yy ); yy += th;
		}
	}

	if ( (spriteIdx >= 0) && (spriteIdx < 64) )
	{
		xx = (spriteIdx % 8) * (w*8);
		yy = (spriteIdx / 8) * (h*16);

		pen.setWidth( 3 );
		pen.setColor( QColor(0, 0, 0) );
		painter.setPen( pen );

		painter.drawRect( xx, yy, w*8, h*16 );

		pen.setWidth( 1 );
		pen.setColor( selTileColor );
		painter.setPen( pen );

		painter.drawRect( xx, yy, w*8, h*16 );
	}
}
//----------------------------------------------------
//-- OAM Tile View
//----------------------------------------------------
oamTileView_t::oamTileView_t(QWidget *parent)
	: QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	viewWidth = 80;
	viewHeight = 160;
	setMinimumWidth( viewWidth );
	setMinimumHeight( viewHeight );

	spriteIdx = 0;
}
//----------------------------------------------------
oamTileView_t::~oamTileView_t(void)
{

}
//----------------------------------------------------
void oamTileView_t::setIndex( int val )
{
	spriteIdx = val;
}
//----------------------------------------------------
int  oamTileView_t::heightForWidth(int w) const
{
	return 2*w;
}
//----------------------------------------------------
QSize oamTileView_t::minimumSizeHint(void) const
{
	return QSize(8,16);
}
//----------------------------------------------------
QSize oamTileView_t::maximumSizeHint(void) const
{
	return QSize(128,256);
}
//----------------------------------------------------
QSize oamTileView_t::sizeHint(void) const
{
	return QSize(64,128);
}
//----------------------------------------------------
void oamTileView_t::resizeEvent(QResizeEvent *event)
{
	viewWidth  = event->size().width();
	viewHeight = event->size().height();
}
//----------------------------------------------------
void oamTileView_t::paintEvent(QPaintEvent *event)
{
	int j,x,y,w,h,xx,yy;
	QPainter painter(this);
	//QPen pen;
	//char showSelector;

	//pen = painter.pen();

	viewWidth  = event->rect().width();
	viewHeight = event->rect().height();

	w = viewWidth  / 8;
  	h = viewHeight / 16;

	if ( w < h )
	{
		h = w;
	}
	else
	{
		w = h;
	}

	yy = 0;

	for (j=0; j<2; j++)
	{
		for (y=0; y<8; y++)
		{
			xx = 0;
			for (x=0; x < 8; x++)
			{
				painter.fillRect( xx, yy, w, h, oamPattern.sprite[ spriteIdx ].tile[j].pixel[y][x].color );
				xx += w;
			}
			yy += h;
		}
	}
}
//----------------------------------------------------
//-- OAM Palette View
//----------------------------------------------------
oamPaletteView_t::oamPaletteView_t(QWidget *parent)
	: QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	viewHeight = 32;
	viewWidth = viewHeight*4;
	setMinimumWidth( viewWidth );
	setMinimumHeight( viewHeight );

	palIdx = 0;
}
//----------------------------------------------------
oamPaletteView_t::~oamPaletteView_t(void)
{

}
//----------------------------------------------------
void oamPaletteView_t::setIndex( int val )
{
	palIdx = oamPattern.sprite[val].pal;
}
//----------------------------------------------------
int  oamPaletteView_t::heightForWidth(int w) const
{
	return w/4;
}
//----------------------------------------------------
QSize oamPaletteView_t::minimumSizeHint(void) const
{
	return QSize(48,12);
}
//----------------------------------------------------
QSize oamPaletteView_t::maximumSizeHint(void) const
{
	return QSize(256,64);
}
//----------------------------------------------------
QSize oamPaletteView_t::sizeHint(void) const
{
	return QSize(128,32);
}
//----------------------------------------------------
void oamPaletteView_t::resizeEvent(QResizeEvent *event)
{
	viewWidth  = event->size().width();
	viewHeight = event->size().height();
}
//----------------------------------------------------
void oamPaletteView_t::paintEvent(QPaintEvent *event)
{
	int x,w,h,xx,yy,p,p2,i,j;
	QPainter painter(this);
	QColor color( 0, 0, 0);
	QColor  white(255,255,255), black(0,0,0);
	//QPen pen;
	//char showSelector;
	char c[4];

	//pen = painter.pen();

	viewWidth  = event->rect().width();
	viewHeight = event->rect().height();

	w = viewWidth  / 4;
  	h = viewHeight;

	if ( w < h )
	{
		h = w;
	}
	else
	{
		w = h;
	}

	i = w / 4;
	j = h / 4;

	p2 = palIdx * 4;
	yy = 0;
	xx = 0;
	for (x=0; x < 4; x++)
	{
		if ( palo != NULL )
		{
			p = palcache[p2 | x];
			color.setBlue( palo[p].b );
			color.setGreen( palo[p].g );
			color.setRed( palo[p].r );

			c[0] = conv2hex( (p & 0xF0) >> 4 );
			c[1] = conv2hex(  p & 0x0F);
			c[2] =  0;
		}
		painter.fillRect( xx, yy, w, h, color );

		if ( qGray( color.red(), color.green(), color.blue() ) > 128 )
		{
			painter.setPen( black );
		}
		else
		{
			painter.setPen( white );
		}
		painter.drawText( xx+i, yy+h-j, tr(c) );

		painter.setPen( black );
		painter.drawRect( xx, yy, w-1, h-1 );
		painter.setPen( white );
		painter.drawRect( xx+1, yy+1, w-3, h-3 );
		xx += w;
	}
}
//----------------------------------------------------
//-- OAM Preview
//----------------------------------------------------
oamPreview_t::oamPreview_t(QWidget *parent)
	: QWidget(parent)
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	viewHeight = 240;
	viewWidth = 256;
	setMinimumWidth( viewWidth );
	setMinimumHeight( viewHeight );
	selSprite = 0;
	cx = cy = 0;

	boxColor.setRgb( 128, 128, 128 );

	fceuLoadConfigColor("SDL.OAM_LocatorColor", &boxColor  );
}
//----------------------------------------------------
oamPreview_t::~oamPreview_t(void)
{

}
//----------------------------------------------------
void oamPreview_t::setIndex(int val)
{
	selSprite = val;
}
//----------------------------------------------------
void oamPreview_t::setMinScale(int scale)
{
	if ( scale < 1 )
	{
		scale = 1;
	}
	setMinimumWidth( scale*256 );
	setMinimumHeight( scale*240 );

	return;
}
//----------------------------------------------------
int  oamPreview_t::heightForWidth(int w) const
{
	return ((w*256)/240);
}
//----------------------------------------------------
QSize oamPreview_t::minimumSizeHint(void) const
{
	return QSize(256,240);
}
//----------------------------------------------------
QSize oamPreview_t::maximumSizeHint(void) const
{
	return QSize(512,480);
}
//----------------------------------------------------
QSize oamPreview_t::sizeHint(void) const
{
	return QSize(512,480);
}
//----------------------------------------------------
void oamPreview_t::resizeEvent(QResizeEvent *event)
{
	viewWidth  = event->size().width();
	viewHeight = event->size().height();
}
//----------------------------------------------------
void oamPreview_t::paintEvent(QPaintEvent *event)
{
	int w,h,i,j,x,y,xx,yy,nt;
	QPainter painter(this);
	QColor bgColor(0, 0, 0);
	QPen pen;
	char spriteRendered[64];
	struct oamSpriteData_t *spr;

	pen = painter.pen();

	viewWidth  = event->rect().width();
	viewHeight = event->rect().height();

	//printf("Draw: %i,%i\n", viewWidth, viewHeight );

	w = viewWidth  / 256;
  	h = viewHeight / 240;

	if ( w < h )
	{
		h = w;
	}
	else
	{
		w = h;
	}

	cx = (viewWidth  - (256*w)) / 2;
	cy = (viewHeight - (240*h)) / 2;

	if ( palo != NULL )
	{
		int p = palcache[0];

		bgColor.setRed( palo[p].r );
		bgColor.setGreen( palo[p].g );
		bgColor.setBlue( palo[p].b );
	}
	painter.fillRect( cx, cy, w*256, h*240, bgColor );

	nt = ( oamPattern.mode8x16 ) ? 2 : 1;

	for (i=63; i>=0; i--)
	{
		spr = &oamPattern.sprite[i];

		spriteRendered[i] = 0;
		//printf("Sprite: (%i,%i) -> (%02X,%02X) \n", spr->x, spr->y, spr->x, spr->y );

		// Check if sprite is off screen
		if ( spr->y >= 0xEF )
		{
			continue;
		}

		yy = (spr->y * h) + cy;

		for (j=0; j<nt; j++)
		{
			for (y=0; y<8; y++)
			{
				xx = (spr->x * w) + cx;

				for (x=0; x < 8; x++)
				{
					painter.fillRect( xx, yy, w, h, spr->tile[j].pixel[y][x].color );
					xx += w;
				}
				yy += h;
			}
		}
		spriteRendered[i] = 1;
	}

	if ( spriteRendered[ selSprite ] )
	{
		spr = &oamPattern.sprite[selSprite];

		pen.setWidth( 1 );
		pen.setColor( boxColor );
		painter.setPen( pen );

		yy = (spr->y * h) + cy;
		xx = (spr->x * w) + cx;

		painter.drawRect( xx, yy, w*8, h*nt*8 );
	}
}
//----------------------------------------------------
