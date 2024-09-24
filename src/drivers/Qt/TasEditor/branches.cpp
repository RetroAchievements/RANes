/* ---------------------------------------------------------------------------------
Implementation file of Branches class
Copyright (c) 2011-2013 AnS

(The MIT License)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------------
Branches - Manager of Branches
[Single instance]

* stores info about Branches (relations of Bookmarks) and the id of current Branch
* also stores the time of the last modification (see fireball) and the time of project beginning (see cloudlet)
* also caches data used in calculations (cached_first_difference, cached_timelines)
* saves and loads the data from a project file. On error: sends warning to caller
* implements the working of Branches Tree: creating, recalculating relations, animating, redrawing, mouseover, clicks
* on demand: reacts on Bookmarks/current Movie changes and recalculates the Branches Tree
* regularly updates animations in Branches Tree and calculates Playback cursor position on the Tree
* stores resources: coordinates for building Branches Tree, animation timings
------------------------------------------------------------------------------------ */

#include <math.h>
#include <zlib.h>

#include <QToolTip>
#include <QFontMetrics>

#include "utils/xstring.h"
#include "Qt/fceuWrapper.h"
#include "Qt/TasEditor/taseditor_project.h"
#include "Qt/TasEditor/TasEditorWindow.h"


//extern COLORREF bookmark_flash_colors[TOTAL_BOOKMARK_COMMANDS][FLASH_PHASE_MAX+1];

// resources
// corners cursor animation
static int corners_cursor_shift[BRANCHES_ANIMATION_FRAMES] = {0, 0, 1, 1, 2, 2, 2, 2, 1, 1, 0, 0 };

BRANCHES::BRANCHES(QWidget *parent)
	: QWidget(parent)
{
	std::string fontString;

	mustRedrawBranchesBitmap = false;
	mustRecalculateBranchesTree = false;
	branchRightclicked = 0;
	currentBranch = 0;
	changesSinceCurrentBranch = false;
	memset( cloudTimestamp, 0, sizeof(cloudTimestamp) );
	memset( currentPosTimestamp, 0, sizeof(currentPosTimestamp) );
	transitionPhase = 0;
	currentAnimationFrame = 0;
	nextAnimationTime = 0;
	playbackCursorX = playbackCursorY = 0;
	cornersCursorX = cornersCursorY = 0;
	fireballSize = 0;
	lastItemUnderMouse = -1;
	cloudX = cloudPreviousX = cloudCurrentX = BRANCHES_CLOUD_X;
	cloudY = cloudPreviousY = cloudCurrentY = BRANCHES_CLOUD_Y;

	imageItem  = 0;
	imageTimer = new QTimer(this);
	imageTimer->setSingleShot(true);
	imageTimer->setInterval(100);
	connect( imageTimer, SIGNAL(timeout(void)), this, SLOT(showImage(void)) );

	//this->parent = qobject_cast <TasEditorWindow*>( parent );
	this->setFocusPolicy(Qt::StrongFocus);
	this->setMouseTracking(true);
	this->setMinimumWidth(BRANCHES_BITMAP_WIDTH);
	this->setMinimumHeight(BRANCHES_BITMAP_HEIGHT);

	viewWidth  = BRANCHES_BITMAP_WIDTH;
	viewHeight = BRANCHES_BITMAP_HEIGHT;

	viewRect = QRect( 0, 0, viewWidth, viewHeight );

	g_config->getOption("SDL.TasBranchesFont", &fontString);

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
	font.setBold(true);

	calcFontData();
}

BRANCHES::~BRANCHES(void)
{
}

//----------------------------------------------------------------------------
void BRANCHES::setFont( QFont &newFont )
{
	font = newFont;
	font.setBold(true);
	QWidget::setFont( font );
	calcFontData();
	reset();
	recalculateParents();
	recalculateBranchesTree();
}

void BRANCHES::calcFontData(void)
{
	int w,h,ch='0',bs;
	QWidget::setFont(font);
	QFontMetrics metrics(font);
#if QT_VERSION > QT_VERSION_CHECK(5, 11, 0)
	pxCharWidth = metrics.horizontalAdvance(QLatin1Char(ch));
#else
	pxCharWidth = metrics.width(QLatin1Char(ch));
#endif
	//printf("        Width: %i\n", metrics.width(ch) );
	//printf("      Advance: %i\n", metrics.horizontalAdvance(ch) );
	//printf(" Left Bearing: %i\n", metrics.leftBearing(ch) );
	//printf("Right Bearing: %i\n", metrics.rightBearing(ch) );
	//printf("       Ascent: %i\n", metrics.ascent() );
	//printf("      Descent: %i\n", metrics.descent() );
	//printf("       Height: %i\n", metrics.height() );
	//printf("   Cap Height: %i\n", metrics.capHeight() );
	//printf(" Line Spacing: %i\n", metrics.lineSpacing() );
	//printf(" Line Leading: %i\n", metrics.leading() );

	//for (int i=0; i<TOTAL_BOOKMARKS; i++)
	//{
	//	char txt[8];
	//	QRect boundingRect;

	//	txt[0] = i + '0';
	//	txt[1] = 0;

	//	boundingRect = metrics.boundingRect( txt[0] );

	//	printf("Char:'%c'  x:%i   y:%i   w:%i   h:%i\n", txt[0], boundingRect.x(), boundingRect.y(), boundingRect.width(), boundingRect.height() );

	//	boundingRect = metrics.boundingRect( tr(txt) );

	//	printf("Char:'%c'  x:%i   y:%i   w:%i   h:%i\n", txt[0], boundingRect.x(), boundingRect.y(), boundingRect.width(), boundingRect.height() );

	//	boundingRect = metrics.tightBoundingRect( tr(txt) );

	//	printf("Char:'%c'  x:%i   y:%i   w:%i   h:%i\n", txt[0], boundingRect.x(), boundingRect.y(), boundingRect.width(), boundingRect.height() );
	//}
	
	bs = (metrics.leftBearing(QChar(ch)) + metrics.rightBearing(QChar(ch))) / 2;

	pxCharHeight   = metrics.capHeight();
	//pxCharHeight   = metrics.ascent();
	pxLineSpacing  = metrics.height();

	pxBoxWidth  = (2*pxCharWidth)+1;
	pxBoxHeight = pxLineSpacing;
	pxSelWidth  = (pxBoxWidth  * 7) / 8;
	pxSelHeight = (pxBoxHeight * 7) / 8;

	pxTextOffsetX  = -(pxBoxWidth/2 );
	pxTextOffsetY  =  (pxBoxHeight/2);
	pxTextOffsetX  = -(pxBoxWidth/2 ) + (pxBoxWidth  - pxCharWidth)/2 + bs;
	pxTextOffsetY  =  (pxBoxHeight/2) - (pxBoxHeight - pxCharHeight)/2 + (pxBoxHeight - pxCharHeight + 1) % 2;

	pxMinGridWidth = (pxBoxWidth + 2);
	pxMaxGridWidth = (pxBoxWidth * 2);
	pxGridWidth    =  pxMinGridWidth;

	pxMinGridHalfHeight = (pxBoxHeight + 2)/2;
	pxMaxGridHalfHeight = (pxBoxHeight * 4)/2;
	pxGridHalfHeight    =  pxMinGridHalfHeight;

	w = pxMinGridWidth * 13;
	h = pxMinGridHalfHeight * 13 * 2;

	if (w < BRANCHES_BITMAP_WIDTH ) w = BRANCHES_BITMAP_WIDTH;
	if (h < BRANCHES_BITMAP_HEIGHT) h = BRANCHES_BITMAP_HEIGHT;

	this->setMinimumWidth(w);
	this->setMinimumHeight(h);
}

void BRANCHES::init()
{
	free();

	// subclass BranchesBitmap

	// init arrays
	branchX.resize(TOTAL_BOOKMARKS+1);
	branchY.resize(TOTAL_BOOKMARKS+1);
	branchPreviousX.resize(TOTAL_BOOKMARKS+1);
	branchPreviousY.resize(TOTAL_BOOKMARKS+1);
	branchCurrentX.resize(TOTAL_BOOKMARKS+1);
	branchCurrentY.resize(TOTAL_BOOKMARKS+1);
	cloudPreviousX = BRANCHES_CLOUD_X;
	cloudPreviousY = BRANCHES_CLOUD_Y;

	// set positions of slots to default coordinates
	for (int i = TOTAL_BOOKMARKS; i >= 0; i--)
	{
		branchX[i] = branchPreviousX[i] = branchCurrentX[i] = EMPTY_BRANCHES_X_BASE;
		branchY[i] = branchPreviousY[i] = branchCurrentY[i] = EMPTY_BRANCHES_Y_BASE + pxBoxHeight * ((i + TOTAL_BOOKMARKS - 1) % TOTAL_BOOKMARKS);
	}
	reset();
	cornersCursorX = cornersCursorY = 0;
	nextAnimationTime = 0;

	update();
}
void BRANCHES::free()
{
	parents.resize(0);
	cachedFirstDifferences.resize(0);
	cachedTimelines.resize(0);
	branchX.resize(0);
	branchY.resize(0);
	branchPreviousX.resize(0);
	branchPreviousY.resize(0);
	branchCurrentX.resize(0);
	branchCurrentY.resize(0);
}
void BRANCHES::reset()
{
	parents.resize(TOTAL_BOOKMARKS);
	for (int i = TOTAL_BOOKMARKS-1; i >= 0; i--)
		parents[i] = ITEM_UNDER_MOUSE_CLOUD;

	cachedTimelines.resize(TOTAL_BOOKMARKS);
	cachedFirstDifferences.resize(TOTAL_BOOKMARKS);
	for (int i = TOTAL_BOOKMARKS-1; i >= 0; i--)
	{
		cachedTimelines[i] = ITEM_UNDER_MOUSE_CLOUD;
		cachedFirstDifferences[i].resize(TOTAL_BOOKMARKS);
		for (int t = TOTAL_BOOKMARKS-1; t >= 0; t--)
			cachedFirstDifferences[i][t] = FIRST_DIFFERENCE_UNKNOWN;
	}

	resetVars();
	// set positions of slots to default coordinates
	for (int i = TOTAL_BOOKMARKS; i >= 0; i--)
	{
		branchPreviousX[i] = branchCurrentX[i];
		branchPreviousY[i] = branchCurrentY[i];
		branchX[i] = EMPTY_BRANCHES_X_BASE;
		branchY[i] = EMPTY_BRANCHES_Y_BASE + pxBoxHeight * ((i + TOTAL_BOOKMARKS - 1) % TOTAL_BOOKMARKS);
	}
	cloudPreviousX = cloudCurrentX;
	cloudPreviousY = cloudCurrentY;
	cloudX = cloudCurrentX = BRANCHES_CLOUD_X;
	cloudY = cloudCurrentY = BRANCHES_CLOUD_Y;
	transitionPhase = BRANCHES_TRANSITION_MAX;

	currentBranch = ITEM_UNDER_MOUSE_CLOUD;
	changesSinceCurrentBranch = false;
	fireballSize = 0;

	// set cloud_time and current_pos_time
	setCurrentPosTimestamp();
	strcpy(cloudTimestamp, currentPosTimestamp);
}
void BRANCHES::resetVars()
{
	transitionPhase = currentAnimationFrame = 0;
	playbackCursorX = playbackCursorY = 0;
	branchRightclicked = lastItemUnderMouse = -1;
	mustRecalculateBranchesTree = mustRedrawBranchesBitmap = true;
	nextAnimationTime = getTasEditorTime() + BRANCHES_ANIMATION_TICK;
}

void BRANCHES::update()
{
	unsigned int currentTime;

	if (mustRecalculateBranchesTree)
	{
		recalculateBranchesTree();
	}

	currentTime = getTasEditorTime();

	// once per 40 milliseconds update branches_bitmap
	if (currentTime > nextAnimationTime)
	{
		// animate branches_bitmap
		nextAnimationTime = currentTime + BRANCHES_ANIMATION_TICK;
		currentAnimationFrame = (currentAnimationFrame + 1) % BRANCHES_ANIMATION_FRAMES;
		if (bookmarks->editMode == EDIT_MODE_BRANCHES)
		{
			// update floating "empty" branches
			int floating_phase_target;
			for (int i = 0; i < TOTAL_BOOKMARKS; ++i)
			{
				if (!bookmarks->bookmarksArray[i].notEmpty)
				{
					if (i == bookmarks->itemUnderMouse)
					{
						floating_phase_target = MAX_FLOATING_PHASE;
					}
					else
					{
						floating_phase_target = 0;
					}
					if (bookmarks->bookmarksArray[i].floatingPhase > floating_phase_target)
					{
						bookmarks->bookmarksArray[i].floatingPhase--;
						mustRedrawBranchesBitmap = true;
					}
					else if (bookmarks->bookmarksArray[i].floatingPhase < floating_phase_target)
					{
						bookmarks->bookmarksArray[i].floatingPhase++;
						mustRedrawBranchesBitmap = true;
					}
				}
			}
			// grow or shrink fireball size
			if (changesSinceCurrentBranch)
			{
				fireballSize++;
				if (fireballSize > BRANCHES_FIREBALL_MAX_SIZE) fireballSize = BRANCHES_FIREBALL_MAX_SIZE;
			} else
			{
				fireballSize--;
				if (fireballSize < 0) fireballSize = 0;
			}
			// also update transition from old to new tree
			if (transitionPhase)
			{
				transitionPhase--;
				// recalculate current positions of branch items
				for (int i = 0; i <= TOTAL_BOOKMARKS; ++i)
				{
					branchCurrentX[i] = (branchX[i] * (BRANCHES_TRANSITION_MAX - transitionPhase) + branchPreviousX[i] * transitionPhase) / BRANCHES_TRANSITION_MAX;
					branchCurrentY[i] = (branchY[i] * (BRANCHES_TRANSITION_MAX - transitionPhase) + branchPreviousY[i] * transitionPhase) / BRANCHES_TRANSITION_MAX;
				}
				cloudCurrentX = (cloudX * (BRANCHES_TRANSITION_MAX - transitionPhase) + cloudPreviousX * transitionPhase) / BRANCHES_TRANSITION_MAX;
				cloudCurrentY = (cloudY * (BRANCHES_TRANSITION_MAX - transitionPhase) + cloudPreviousY * transitionPhase) / BRANCHES_TRANSITION_MAX;
				mustRedrawBranchesBitmap = true;
				bookmarks->mustCheckItemUnderMouse = true;
			}
			else if (!mustRedrawBranchesBitmap)
			{
				// just update sprites
				//InvalidateRect(bookmarks->hwndBranchesBitmap, 0, FALSE);
				QWidget::update();
			}
			// calculate Playback cursor position
			int branch, tempBranchX, tempBranchY, parent, parentX, parentY, upperFrame, lowerFrame;
			double distance;
			if (currentBranch != ITEM_UNDER_MOUSE_CLOUD)
			{
				if (changesSinceCurrentBranch)
				{
					parent = ITEM_UNDER_MOUSE_FIREBALL;
				}
				else
				{
					parent = findFullTimelineForBranch(currentBranch);
					if (parent != currentBranch && bookmarks->bookmarksArray[parent].snapshot.keyFrame == bookmarks->bookmarksArray[currentBranch].snapshot.keyFrame)
					{
						parent = currentBranch;
					}
				}
				do
				{
					branch = parent;
					if (branch == ITEM_UNDER_MOUSE_FIREBALL)
						parent = currentBranch;
					else
						parent = parents[branch];
					if (parent == ITEM_UNDER_MOUSE_CLOUD)
						lowerFrame = -1;
					else
						lowerFrame = bookmarks->bookmarksArray[parent].snapshot.keyFrame;
				} while (parent != ITEM_UNDER_MOUSE_CLOUD && currFrameCounter < lowerFrame);

				if (branch == ITEM_UNDER_MOUSE_FIREBALL)
					upperFrame = currMovieData.getNumRecords() - 1;
				else
					upperFrame = bookmarks->bookmarksArray[branch].snapshot.keyFrame;
				tempBranchX = branchCurrentX[branch];
				tempBranchY = branchCurrentY[branch];
				if (parent == ITEM_UNDER_MOUSE_CLOUD)
				{
					parentX = cloudCurrentX;
					parentY = cloudCurrentY;
				}
				else
				{
	 				parentX = branchCurrentX[parent];
					parentY = branchCurrentY[parent];
				}
				if (upperFrame != lowerFrame)
				{
					distance = (double)(currFrameCounter - lowerFrame) / (double)(upperFrame - lowerFrame);
					if (distance > 1.0) distance = 1.0;
				}
				else
				{
					distance = 1.0;
				}
				playbackCursorX = parentX + distance * (tempBranchX - parentX);
				playbackCursorY = parentY + distance * (tempBranchY - parentY);
			}
			else
			{
				if (changesSinceCurrentBranch)
				{
					// special case: there's only cloud + fireball
					upperFrame = currMovieData.getNumRecords() - 1;
					lowerFrame = 0;
					parentX = cloudCurrentX;
					parentY = cloudCurrentY;
					tempBranchX = branchCurrentX[ITEM_UNDER_MOUSE_FIREBALL];
					tempBranchY = branchCurrentY[ITEM_UNDER_MOUSE_FIREBALL];
					if (upperFrame != lowerFrame)
						distance = (double)(currFrameCounter - lowerFrame) / (double)(upperFrame - lowerFrame);
					else
						distance = 0;
					if (distance > 1.0) distance = 1.0;
					playbackCursorX = parentX + distance * (tempBranchX - parentX);
					playbackCursorY = parentY + distance * (tempBranchY - parentY);
				} else
				{
					// special case: there's only cloud
					playbackCursorX = cloudCurrentX;
					playbackCursorY = cloudCurrentY;
				}
			}
			// move corners cursor to Playback cursor position
			double dx = playbackCursorX - cornersCursorX;
			double dy = playbackCursorY - cornersCursorY;
			distance = sqrt(dx*dx + dy*dy);
			if (distance < CURSOR_MIN_DISTANCE || distance > CURSOR_MAX_DISTANCE)
			{
				// teleport
				cornersCursorX = playbackCursorX;
				cornersCursorY = playbackCursorY;
			}
			else
			{
				// advance
				double speed = sqrt(distance);
				if (speed < CURSOR_MIN_SPEED)
					speed = CURSOR_MIN_SPEED;
				cornersCursorX += dx * speed / distance;
				cornersCursorY += dy * speed / distance;
			}

			if (lastItemUnderMouse != bookmarks->itemUnderMouse)
			{
				mustRedrawBranchesBitmap = true;
				lastItemUnderMouse = bookmarks->itemUnderMouse;
			}
			//printf("Draw Clock: %lu \n", nextAnimationTime );
			mustRedrawBranchesBitmap = true;
			//if (mustRedrawBranchesBitmap)
			//{
			//	redrawBranchesBitmap();
			//}
		}
	}
	if ( mustRedrawBranchesBitmap )
	{
		QWidget::update();
		mustRedrawBranchesBitmap = false;
	}
}

void BRANCHES::save(EMUFILE *os)
{
	setTasProjectProgressBarText("Saving Branches...");
	setTasProjectProgressBar( 0, TOTAL_BOOKMARKS );

	// write cloud time
	os->fwrite(cloudTimestamp, TIMESTAMP_LENGTH);
	// write current branch and flag of changes since it
	write32le(currentBranch, os);
	if (changesSinceCurrentBranch)
	{
		write8le((uint8)1, os);
	}
	else
	{
		write8le((uint8)0, os);
	}
	// write current_position time
	os->fwrite(currentPosTimestamp, TIMESTAMP_LENGTH);
	// write all 10 parents
	for (int i = 0; i < TOTAL_BOOKMARKS; ++i)
	{
		write32le(parents[i], os);
	}
	// write cached_timelines
	os->fwrite(&cachedTimelines[0], TOTAL_BOOKMARKS);
	// write cached_first_difference
	for (int i = 0; i < TOTAL_BOOKMARKS; ++i)
	{
		for (int t = 0; t < TOTAL_BOOKMARKS; ++t)
		{
			write32le(cachedFirstDifferences[i][t], os);
		}
	}
	setTasProjectProgressBar( TOTAL_BOOKMARKS, TOTAL_BOOKMARKS );
}
// returns true if couldn't load
bool BRANCHES::load(EMUFILE *is)
{
	// read cloud time
	if ((int)is->fread(cloudTimestamp, TIMESTAMP_LENGTH) < TIMESTAMP_LENGTH) goto error;
	// read current branch and flag of changes since it
	uint8 tmp;
	if (!read32le(&currentBranch, is)) goto error;
	if (!read8le(&tmp, is)) goto error;
	changesSinceCurrentBranch = (tmp != 0);
	// read current_position time
	if ((int)is->fread(currentPosTimestamp, TIMESTAMP_LENGTH) < TIMESTAMP_LENGTH) goto error;
	// read all 10 parents
	for (int i = 0; i < TOTAL_BOOKMARKS; ++i)
	{
		if (!read32le(&parents[i], is)) goto error;
	}
	// read cached_timelines
	if ((int)is->fread(&cachedTimelines[0], TOTAL_BOOKMARKS) < TOTAL_BOOKMARKS) goto error;
	// read cached_first_difference
	for (int i = 0; i < TOTAL_BOOKMARKS; ++i)
	{
		for (int t = 0; t < TOTAL_BOOKMARKS; ++t)
		{
			if (!read32le(&cachedFirstDifferences[i][t], is)) goto error;
		}
	}
	// all ok
	resetVars();
	return false;
error:
	FCEU_printf("Error loading branches\n");
	return true;
}
// ----------------------------------------------------------
void BRANCHES::redrawBranchesBitmap()
{
	QWidget::update();
}

void BRANCHES::mouseDoubleClickEvent(QMouseEvent * event)
{
	int item = findItemUnderMouse( event->pos().x(), event->pos().y() );

	bookmarks->itemUnderMouse = item;

	if (event->button() & Qt::LeftButton)
	{
		// double click on Branches Tree = deploy the Branch
		int branchUnderMouse = bookmarks->itemUnderMouse;
		if (branchUnderMouse == ITEM_UNDER_MOUSE_CLOUD)
		{
			playback->jump(0);
		}
		else if (branchUnderMouse >= 0 && branchUnderMouse < TOTAL_BOOKMARKS && bookmarks->bookmarksArray[branchUnderMouse].notEmpty)
		{
			bookmarks->command(COMMAND_DEPLOY, branchUnderMouse);
		}
		else if (branchUnderMouse == ITEM_UNDER_MOUSE_FIREBALL)
		{
			playback->jump(currMovieData.getNumRecords() - 1);
		}
	}
}

void BRANCHES::mousePressEvent(QMouseEvent * event)
{
	FCEU_CRITICAL_SECTION( emuLock );
	int item = findItemUnderMouse( event->pos().x(), event->pos().y() );

	bookmarks->itemUnderMouse = item;

	if (event->button() & Qt::LeftButton)
	{
		// single click on Branches Tree = send Playback to the Bookmark
		int branchUnderMouse = item;
		if (branchUnderMouse == ITEM_UNDER_MOUSE_CLOUD)
		{
			playback->jump(0);
		}
		else if ( (branchUnderMouse >= 0) && (branchUnderMouse < TOTAL_BOOKMARKS) && bookmarks->bookmarksArray[branchUnderMouse].notEmpty)
		{
			bookmarks->command(COMMAND_JUMP, branchUnderMouse);
		}
		else if (branchUnderMouse == ITEM_UNDER_MOUSE_FIREBALL)
		{
			playback->jump(currMovieData.getNumRecords() - 1);
		}

		// double click on Branches Tree = deploy the Branch
//		int branchUnderMouse = bookmarks.itemUnderMouse;
//		if (branchUnderMouse == ITEM_UNDER_MOUSE_CLOUD)
//		{
//			playback->jump(0);
//		} else if (branchUnderMouse >= 0 && branchUnderMouse < TOTAL_BOOKMARKS && bookmarks.bookmarksArray[branchUnderMouse].notEmpty)
//		{
//			bookmarks->command(COMMAND_DEPLOY, branchUnderMouse);
//		} else if (branchUnderMouse == ITEM_UNDER_MOUSE_FIREBALL)
//		{
//			playback->jump(currMovieData.getNumRecords() - 1);
//		}

	}
	else if (event->button() & Qt::RightButton)
	{
		branchRightclicked = item;
		//if (branches.branchRightclicked >= 0 && branches.branchRightclicked < TOTAL_BOOKMARKS)

	}
	else if (event->button() & Qt::MiddleButton)
	{
		playback->handleMiddleButtonClick();
	}
}

void BRANCHES::mouseReleaseEvent(QMouseEvent * event)
{
	FCEU_CRITICAL_SECTION( emuLock );
	int item = findItemUnderMouse( event->pos().x(), event->pos().y() );

	bookmarks->itemUnderMouse = item;

	if (event->button() & Qt::LeftButton)
	{

	}
	else if (event->button() & Qt::RightButton)
	{
		if ( (branchRightclicked >= 0) && (branchRightclicked < TOTAL_BOOKMARKS) && (branchRightclicked == item) )
		{
			bookmarks->command(COMMAND_SET, branchRightclicked);
		}
		//ReleaseCapture();
		branchRightclicked = ITEM_UNDER_MOUSE_NONE;
	}
}

void BRANCHES::showImage(void)
{
	FCEU_CRITICAL_SECTION( emuLock );
	
	bool item_valid = (imageItem >= 0) && (imageItem < TOTAL_BOOKMARKS);

	if ( item_valid && (imageItem != bookmarkPreviewPopup::currentIndex()) )
	{
		bookmarkPreviewPopup *popup = bookmarkPreviewPopup::currentInstance();

		if ( popup == NULL )
		{
			popup = new bookmarkPreviewPopup(imageItem, this);

			connect( this, SIGNAL(imageIndexChanged(int)), popup, SLOT(imageIndexChanged(int)) );

			popup->show();
		}
		else
		{
			popup->reloadImage(imageItem);
		}
	}
}

void BRANCHES::mouseMoveEvent(QMouseEvent * event)
{
	FCEU_CRITICAL_SECTION( emuLock );
	int item, item_valid;

	item = findItemUnderMouse( event->pos().x(), event->pos().y() );

	item_valid = (item >= 0) && (item < TOTAL_BOOKMARKS);

	bookmarks->itemUnderMouse = item;

	if ( item_valid && bookmarks->bookmarksArray[item].notEmpty)
	{
		if ( item != imageItem )
		{
			emit imageIndexChanged(item);
		}
		imageItem = item;
		imageTimer->start();
		QToolTip::hideText();
	}
	else
	{
		item = -1;
		if ( item != imageItem )
		{
			emit imageIndexChanged(item);
		}
		imageItem = item;
		imageTimer->stop();
	}

	//if (event->button() & Qt::LeftButton)
	//{

	//}
	//else if (event->button() & Qt::RightButton)
	//{

	//}

}

void BRANCHES::focusOutEvent(QFocusEvent *event)
{
	int item = -1;
	if ( item != imageItem )
	{
		emit imageIndexChanged(item);
	}
	imageItem = item;

	bookmarks->itemUnderMouse = ITEM_UNDER_MOUSE_NONE;
}

void BRANCHES::leaveEvent(QEvent *event)
{
	int item = -1;
	if ( item != imageItem )
	{
		emit imageIndexChanged(item);
	}
	imageItem = item;

	bookmarks->itemUnderMouse = ITEM_UNDER_MOUSE_NONE;
}

bool BRANCHES::event(QEvent *event)
{
	if (event->type() == QEvent::ToolTip)
	{
		FCEU_CRITICAL_SECTION( emuLock );
		int item, item_valid;
		QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);

		item = findItemUnderMouse( helpEvent->pos().x(), helpEvent->pos().y() );

		item_valid = (item >= 0) && (item < TOTAL_BOOKMARKS);

		if ( item_valid && bookmarks->bookmarksArray[item].notEmpty)
		{
			//static_cast<bookmarkPreviewPopup*>(fceuCustomToolTipShow( helpEvent->globalPos(), new bookmarkPreviewPopup(item, this) ));
			//QToolTip::showText(helpEvent->globalPos(), tr(stmp), this );
			QToolTip::hideText();
			event->ignore();
		}
		else if ( taseditorConfig && taseditorConfig->tooltipsEnabled )
		{
			QToolTip::showText(helpEvent->globalPos(), tr("Right click = set Bookmark, single Left click = jump to Bookmark, double Left click = load Branch") );
		}
		return true;
	}
	return QWidget::event(event);
}

void BRANCHES::resizeEvent(QResizeEvent *event)
{
	viewWidth  = event->size().width();
	viewHeight = event->size().height();

	//printf("BranchView: %i x %i\n", viewWidth, viewHeight );

	recalculateBranchesTree();
}

void BRANCHES::paintEvent(QPaintEvent *event)
{
	int x,y;
	char txt[4];
	int visHeight;
	QPixmap spriteSheet(":/icons/branch_spritesheet.png");

	QPainter painter(this);

	viewRect = event->rect();

	//viewWidth  = event->rect().width();
	visHeight = event->rect().height();

//	// draw background
	QLinearGradient linearGrad(QPointF(0, 0), QPointF(viewWidth, viewHeight));
    	linearGrad.setColorAt(0, QColor(0xBF,0xE2,0xEF));
    	linearGrad.setColorAt(1, QColor(0xE5,0xFB,0xFF));

	//painter.fillRect( 0, 0, viewWidth, viewHeight, linearGrad );
	painter.fillRect( viewRect, linearGrad );

//	// lines
	int branch, tempBranchX, tempBranchY, tempBranchX2, tempBranchY2, parentX, parentY, childID;
	//SelectObject(hBitmapDC, normalPen);
	painter.setPen( QColor( 0, 0, 0 ) );

	//x = 0;
	//while (x<viewWidth)
	//{
	//	painter.drawLine( x, 0, x, viewHeight );
	//	x += pxGridWidth;
	//}

	for (int t = children.size() - 1; t >= 0; t--)
	{
		if (t > 0)
		{
			parentX = branchCurrentX[t-1];
			parentY = branchCurrentY[t-1];
		}
		else
		{
			parentX = cloudCurrentX;
			parentY = cloudCurrentY;
		}
		for (int i = children[t].size() - 1; i >= 0; i--)
		{
			childID = children[t][i];
			if ( (childID >= 0) && (childID < TOTAL_BOOKMARKS) )
			{
				//MoveToEx(hBitmapDC, parentX, parentY, 0);
				//LineTo(hBitmapDC, branchCurrentX[childID], branchCurrentY[childID]);
				painter.drawLine( parentX, parentY, branchCurrentX[childID], branchCurrentY[childID] );
			}
		}
	}
	// lines for current timeline
	if (currentBranch != ITEM_UNDER_MOUSE_CLOUD)
	{
		painter.setPen( QColor( 0xE0, 0x20, 0x00 ) );

		//SelectObject(hBitmapDC, timelinePen);
		if (changesSinceCurrentBranch)
		{
			branch = currentBranch;
		}
		else
		{
			branch = findFullTimelineForBranch(currentBranch);
		}
		while (branch >= 0)
		{
			tempBranchX = branchCurrentX[branch];
			tempBranchY = branchCurrentY[branch];
			//MoveToEx(hBitmapDC, tempBranchX, tempBranchY, 0);
			branch = parents[branch];
			if (branch == ITEM_UNDER_MOUSE_CLOUD)
			{
				tempBranchX2 = cloudCurrentX;
				tempBranchY2 = cloudCurrentY;
			} else
			{
	 			tempBranchX2 = branchCurrentX[branch];
				tempBranchY2 = branchCurrentY[branch];
			}
			//LineTo(hBitmapDC, tempBranchX, tempBranchY);
			painter.drawLine( tempBranchX, tempBranchY, tempBranchX2, tempBranchY2 );
		}
	}
	if (isSafeToShowBranchesData())
	{
		// lines for item under mouse
		if (bookmarks->itemUnderMouse == ITEM_UNDER_MOUSE_FIREBALL || (bookmarks->itemUnderMouse >= 0 && bookmarks->itemUnderMouse < TOTAL_BOOKMARKS && bookmarks->bookmarksArray[bookmarks->itemUnderMouse].notEmpty))
		{
			painter.setPen( QColor( 0x80, 0x90, 0xFF ) );

			//SelectObject(hBitmapDC, selectPen);
			if (bookmarks->itemUnderMouse == ITEM_UNDER_MOUSE_FIREBALL)
			{
				branch = currentBranch;
			}
			else
			{
				branch = findFullTimelineForBranch(bookmarks->itemUnderMouse);
			}
			while (branch >= 0)
			{
				tempBranchX = branchCurrentX[branch];
				tempBranchY = branchCurrentY[branch];
				//MoveToEx(hBitmapDC, tempBranchX, tempBranchY, 0);
				branch = parents[branch];
				if (branch == ITEM_UNDER_MOUSE_CLOUD)
				{
					tempBranchX2 = cloudCurrentX;
					tempBranchY2 = cloudCurrentY;
				} else
				{
	 				tempBranchX2 = branchCurrentX[branch];
					tempBranchY2 = branchCurrentY[branch];
				}
				//LineTo(hBitmapDC, tempBranchX, tempBranchY);
				painter.drawLine( tempBranchX, tempBranchY, tempBranchX2, tempBranchY2 );
			}
		}
	}
	if (changesSinceCurrentBranch)
	{
		if (isSafeToShowBranchesData() && bookmarks->itemUnderMouse == ITEM_UNDER_MOUSE_FIREBALL)
		{
			//SelectObject(hBitmapDC, selectPen);
		}
		else
		{
			//SelectObject(hBitmapDC, timelinePen);
		}
		if (currentBranch == ITEM_UNDER_MOUSE_CLOUD)
		{
			parentX = cloudCurrentX;
			parentY = cloudCurrentY;
		}
		else
		{
			parentX = branchCurrentX[currentBranch];
			parentY = branchCurrentY[currentBranch];
		}
		//MoveToEx(hBitmapDC, parentX, parentY, 0);
		tempBranchX = branchCurrentX[ITEM_UNDER_MOUSE_FIREBALL];
		tempBranchY = branchCurrentY[ITEM_UNDER_MOUSE_FIREBALL];
		//LineTo(hBitmapDC, tempBranchX, tempBranchY);
		painter.drawLine( parentX, parentY, tempBranchX, tempBranchY );
	}
//	// cloud
//	TransparentBlt(hBitmapDC, cloudCurrentX - BRANCHES_CLOUD_HALFWIDTH, BRANCHES_CLOUD_Y - BRANCHES_CLOUD_HALFHEIGHT, BRANCHES_CLOUD_WIDTH, BRANCHES_CLOUD_HEIGHT, hSpritesheetDC, BRANCHES_CLOUD_SPRITESHEET_X, BRANCHES_CLOUD_SPRITESHEET_Y, BRANCHES_CLOUD_WIDTH, BRANCHES_CLOUD_HEIGHT, 0x00FF00);
	//painter.drawPixmap( cloudCurrentX - BRANCHES_CLOUD_HALFWIDTH, BRANCHES_CLOUD_Y - BRANCHES_CLOUD_HALFHEIGHT, BRANCHES_CLOUD_WIDTH, BRANCHES_CLOUD_HEIGHT, cloud );
	painter.drawPixmap( cloudCurrentX - BRANCHES_CLOUD_HALFWIDTH, cloudCurrentY - BRANCHES_CLOUD_HALFHEIGHT, BRANCHES_CLOUD_WIDTH, BRANCHES_CLOUD_HEIGHT, 
			spriteSheet, BRANCHES_CLOUD_SPRITESHEET_X, BRANCHES_CLOUD_SPRITESHEET_Y, BRANCHES_CLOUD_WIDTH, BRANCHES_CLOUD_HEIGHT );

//	// branches rectangles
	for (int i = 0; i < TOTAL_BOOKMARKS; ++i)
	{
		x = branchCurrentX[i] - pxBoxWidth/2;
		y = branchCurrentY[i] - pxBoxHeight/2;

		//tempRect.right = tempRect.left + DIGIT_RECT_WIDTH;
		//tempRect.bottom = tempRect.top + DIGIT_RECT_HEIGHT;
		if (!bookmarks->bookmarksArray[i].notEmpty && bookmarks->bookmarksArray[i].floatingPhase > 0)
		{
			x += bookmarks->bookmarksArray[i].floatingPhase;
			//tempRect.left += bookmarks->bookmarksArray[i].floatingPhase;
			//tempRect.right += bookmarks->bookmarksArray[i].floatingPhase;
		}
		if (bookmarks->bookmarksArray[i].flashPhase)
		{
			// draw colored rect
			//HBRUSH color_brush = CreateSolidBrush(bookmark_flash_colors[bookmarks.bookmarksArray[i].flashType][bookmarks.bookmarksArray[i].flashPhase]);
			//FrameRect(hBitmapDC, &tempRect, color_brush);
			//DeleteObject(color_brush);
		}
		else
		{
			painter.setPen( QColor( 0x0, 0x0, 0x0 ) );
			// draw black rect
			//FrameRect(hBitmapDC, &tempRect, normalBrush);
		}
		box[i] = QRect( x, y, pxBoxWidth, pxBoxHeight );

		if (i == bookmarks->itemUnderMouse)
		{
			painter.fillRect( x, y, pxBoxWidth, pxBoxHeight, QColor(255,235,154) );
		}
		else
		{
			painter.fillRect( x, y, pxBoxWidth, pxBoxHeight, QColor(255,255,255) );
		}
		painter.drawRect( x, y, pxBoxWidth, pxBoxHeight );
	}
	// digits
	for (int i = 0; i < TOTAL_BOOKMARKS; ++i)
	{
		//int xa, ya;
		//QRect boundingRect;

		x = branchCurrentX[i] + pxTextOffsetX;
		y = branchCurrentY[i] + pxTextOffsetY;

		txt[0] = i + '0';
		txt[1] = 0;

		//boundingRect = painter.boundingRect( box[i], Qt::AlignLeft | Qt::AlignTop, tr(txt) );

		//xa = (box[i].width()  - boundingRect.width()) / 2;
		//ya = (box[i].height() - boundingRect.height()) / 2;

		if (i == currentBranch)
		{
			painter.setPen( QColor( 58, 179, 255 ) );
			painter.drawText( x, y, tr(txt) );
			//painter.drawRect( txtBox[i].translated(x,y) );
			//painter.drawText( txtBox[i].translated(x,y), Qt::AlignLeft | Qt::AlignTop, tr(txt) );
			//painter.drawText( box[i], Qt::AlignCenter, tr(txt) );
			//painter.drawText( box[i].adjusted( xa, ya, xa, ya ), Qt::AlignLeft | Qt::AlignTop, tr(txt) );
		}
		else
		{
			painter.setPen( QColor( 0, 194, 64 ) );

			if (!bookmarks->bookmarksArray[i].notEmpty && bookmarks->bookmarksArray[i].floatingPhase > 0)
			{
				x += bookmarks->bookmarksArray[i].floatingPhase;
			}
			painter.drawText( x, y, tr(txt) );
			//painter.drawRect( txtBox[i].translated(x,y) );
			//painter.drawText( txtBox[i].translated(x,y), Qt::AlignLeft | Qt::AlignTop, tr(txt) );
			//painter.drawText( box[i], Qt::AlignCenter, tr(txt) );
			//painter.drawText( box[i].adjusted( xa, ya, xa, ya ), Qt::AlignLeft | Qt::AlignTop, tr(txt) );
		}
	}
	if (isSafeToShowBranchesData())
	{
		// keyframe of item under cursor (except cloud - it doesn't have particular frame)
		if (bookmarks->itemUnderMouse == ITEM_UNDER_MOUSE_FIREBALL || (bookmarks->itemUnderMouse >= 0 && bookmarks->itemUnderMouse < TOTAL_BOOKMARKS && bookmarks->bookmarksArray[bookmarks->itemUnderMouse].notEmpty))
		{
			int x,y;
			char framenum_string[16] = {0};
			if (bookmarks->itemUnderMouse < TOTAL_BOOKMARKS)
			{
				sprintf( framenum_string, "%07i", bookmarks->bookmarksArray[bookmarks->itemUnderMouse].snapshot.keyFrame );
			}
			else
			{
				sprintf( framenum_string, "%07i", currFrameCounter );
			}
			x = viewRect.x() + (2 * pxBoxWidth);
			y = pxLineSpacing;

			painter.setPen( QColor( BRANCHES_TEXT_SHADOW_COLOR ) );
			painter.drawText( x+1, y+1, tr(framenum_string) );

			painter.setPen( QColor( BRANCHES_TEXT_COLOR ) );
			painter.drawText( x, y, tr(framenum_string) );
		}
		// time of item under cursor
		if (bookmarks->itemUnderMouse > ITEM_UNDER_MOUSE_NONE)
		{
			int x,y;
			x = viewRect.x() + (2 * pxBoxWidth);
			y = visHeight - (pxLineSpacing / 2);

			if (bookmarks->itemUnderMouse == ITEM_UNDER_MOUSE_CLOUD)
			{
				// draw shadow of text
				painter.setPen( QColor( BRANCHES_TEXT_SHADOW_COLOR ) );
				painter.drawText( x+1, y+1, tr(cloudTimestamp) );

				painter.setPen( QColor( BRANCHES_TEXT_COLOR ) );
				painter.drawText( x, y, tr(cloudTimestamp) );
			}
			else if (bookmarks->itemUnderMouse == ITEM_UNDER_MOUSE_FIREBALL)
			{
				// fireball - show current_pos_time
				painter.setPen( QColor( BRANCHES_TEXT_SHADOW_COLOR ) );
				painter.drawText( x+1, y+1, tr(currentPosTimestamp) );

				painter.setPen( QColor( BRANCHES_TEXT_COLOR ) );
				painter.drawText( x, y, tr(currentPosTimestamp) );
			}
			else if (bookmarks->bookmarksArray[bookmarks->itemUnderMouse].notEmpty)
			{
				painter.setPen( QColor( BRANCHES_TEXT_SHADOW_COLOR ) );
				painter.drawText( x+1, y+1, tr(bookmarks->bookmarksArray[bookmarks->itemUnderMouse].snapshot.description) );

				painter.setPen( QColor( BRANCHES_TEXT_COLOR ) );
				painter.drawText( x, y, tr(bookmarks->bookmarksArray[bookmarks->itemUnderMouse].snapshot.description) );
			}
		}
	}

	// blinking red frame on selected slot
	if (taseditorConfig->oldControlSchemeForBranching && ((currentAnimationFrame + 1) % 6))
	{
		QPen pen = painter.pen();
		int selected_slot = bookmarks->getSelectedSlot();
		x = branchCurrentX[selected_slot] - pxBoxWidth/2;
		y = branchCurrentY[selected_slot] - pxBoxHeight/2;
		pen.setColor( QColor( 0xCA, 0x56, 0x56 ) );
		pen.setWidth(3);
		painter.setPen( pen );
		painter.drawRect( x, y, pxBoxWidth, pxBoxHeight );
		pen.setWidth(1);
		painter.setPen( pen );
	}

	// fireball
	if (fireballSize)
	{
		tempBranchX = branchCurrentX[ITEM_UNDER_MOUSE_FIREBALL] - BRANCHES_FIREBALL_HALFWIDTH;
		tempBranchY = branchCurrentY[ITEM_UNDER_MOUSE_FIREBALL] - BRANCHES_FIREBALL_HALFHEIGHT;
		if (fireballSize >= BRANCHES_FIREBALL_MAX_SIZE)
		{
			painter.drawPixmap( tempBranchX, tempBranchY, BRANCHES_FIREBALL_WIDTH, BRANCHES_FIREBALL_HEIGHT,
				spriteSheet, currentAnimationFrame * BRANCHES_FIREBALL_WIDTH + BRANCHES_FIREBALL_SPRITESHEET_X, BRANCHES_FIREBALL_SPRITESHEET_Y, BRANCHES_FIREBALL_WIDTH, BRANCHES_FIREBALL_HEIGHT );
			//TransparentBlt(hBufferDC, tempBranchX, tempBranchY, BRANCHES_FIREBALL_WIDTH, BRANCHES_FIREBALL_HEIGHT, hSpritesheetDC, currentAnimationFrame * BRANCHES_FIREBALL_WIDTH + BRANCHES_FIREBALL_SPRITESHEET_X, BRANCHES_FIREBALL_SPRITESHEET_Y, BRANCHES_FIREBALL_WIDTH, BRANCHES_FIREBALL_HEIGHT, 0x00FF00);
		}
		else
		{
			painter.drawPixmap( tempBranchX, tempBranchY, BRANCHES_FIREBALL_WIDTH, BRANCHES_FIREBALL_HEIGHT,
				spriteSheet, BRANCHES_FIREBALL_SPRITESHEET_END_X - fireballSize * BRANCHES_FIREBALL_WIDTH, BRANCHES_FIREBALL_SPRITESHEET_Y, BRANCHES_FIREBALL_WIDTH, BRANCHES_FIREBALL_HEIGHT );
			//TransparentBlt(hBufferDC, tempBranchX, tempBranchY, BRANCHES_FIREBALL_WIDTH, BRANCHES_FIREBALL_HEIGHT, hSpritesheetDC, BRANCHES_FIREBALL_SPRITESHEET_END_X - fireballSize * BRANCHES_FIREBALL_WIDTH, BRANCHES_FIREBALL_SPRITESHEET_Y, BRANCHES_FIREBALL_WIDTH, BRANCHES_FIREBALL_HEIGHT, 0x00FF00);
		}
	}

	// corners cursor
	painter.setPen( QColor( 0x0, 0x0, 0x0 ) );
	int current_corners_cursor_shift = corners_cursor_shift[currentAnimationFrame];
	int corner_x, corner_y;
	// upper left
	corner_x = cornersCursorX - current_corners_cursor_shift - pxSelWidth;
	corner_y = cornersCursorY - current_corners_cursor_shift - pxSelHeight;
	painter.drawLine( corner_x, corner_y, corner_x                      , corner_y+BRANCHES_CORNER_HEIGHT );
	painter.drawLine( corner_x, corner_y, corner_x+BRANCHES_CORNER_WIDTH, corner_y   );
	//TransparentBlt(hBufferDC, corner_x, corner_y, BRANCHES_CORNER_WIDTH, BRANCHES_CORNER_HEIGHT, hSpritesheetDC, BRANCHES_CORNER1_SPRITESHEET_X, BRANCHES_CORNER1_SPRITESHEET_Y, BRANCHES_CORNER_WIDTH, BRANCHES_CORNER_HEIGHT, 0x00FF00);
	// upper right
	corner_x = cornersCursorX + current_corners_cursor_shift + pxSelWidth;
	corner_y = cornersCursorY - current_corners_cursor_shift - pxSelHeight;
	painter.drawLine( corner_x, corner_y, corner_x                      , corner_y+BRANCHES_CORNER_HEIGHT );
	painter.drawLine( corner_x, corner_y, corner_x-BRANCHES_CORNER_WIDTH, corner_y   );
	//TransparentBlt(hBufferDC, corner_x, corner_y, BRANCHES_CORNER_WIDTH, BRANCHES_CORNER_HEIGHT, hSpritesheetDC, BRANCHES_CORNER2_SPRITESHEET_X, BRANCHES_CORNER2_SPRITESHEET_Y, BRANCHES_CORNER_WIDTH, BRANCHES_CORNER_HEIGHT, 0x00FF00);
	// lower left
	corner_x = cornersCursorX - current_corners_cursor_shift - pxSelWidth;
	corner_y = cornersCursorY + current_corners_cursor_shift + pxSelHeight;
	painter.drawLine( corner_x, corner_y, corner_x                      , corner_y-BRANCHES_CORNER_HEIGHT );
	painter.drawLine( corner_x, corner_y, corner_x+BRANCHES_CORNER_WIDTH, corner_y   );
	//TransparentBlt(hBufferDC, corner_x, corner_y, BRANCHES_CORNER_WIDTH, BRANCHES_CORNER_HEIGHT, hSpritesheetDC, BRANCHES_CORNER3_SPRITESHEET_X, BRANCHES_CORNER3_SPRITESHEET_Y, BRANCHES_CORNER_WIDTH, BRANCHES_CORNER_HEIGHT, 0x00FF00);
	// lower right
	corner_x = cornersCursorX + current_corners_cursor_shift + pxSelWidth;
	corner_y = cornersCursorY + current_corners_cursor_shift + pxSelHeight;
	painter.drawLine( corner_x, corner_y, corner_x                      , corner_y-BRANCHES_CORNER_HEIGHT );
	painter.drawLine( corner_x, corner_y, corner_x-BRANCHES_CORNER_WIDTH, corner_y   );

}

// ----------------------------------------------------------------------------------------
// getters
int BRANCHES::getParentOf(int child)
{
	return parents[child];
}
int BRANCHES::getCurrentBranch()
{
	return currentBranch;
}
bool BRANCHES::areThereChangesSinceCurrentBranch()
{
	return changesSinceCurrentBranch;
}
// this getter contains formula to decide whether it's safe to show Branches Data now
bool BRANCHES::isSafeToShowBranchesData()
{
	if (bookmarks->editMode == EDIT_MODE_BRANCHES && transitionPhase)
	{
		return false;		// can't show data when Branches Tree is transforming
	}
	return true;
}

void BRANCHES::handleBookmarkSet(int slot)
{
	// new Branch is written into the slot
	invalidateRelationsOfBranchSlot(slot);
	recalculateParents();
	currentBranch = slot;
	changesSinceCurrentBranch = false;
	mustRecalculateBranchesTree = true;
}
void BRANCHES::handleBookmarkDeploy(int slot)
{
	currentBranch = slot;
	changesSinceCurrentBranch = false;
	mustRecalculateBranchesTree = true;
}
void BRANCHES::handleHistoryJump(int newCurrentBranch, bool newChangesSinceCurrentBranch)
{
	recalculateParents();
	currentBranch = newCurrentBranch;
	changesSinceCurrentBranch = newChangesSinceCurrentBranch;
	if (newChangesSinceCurrentBranch)
	{
		setCurrentPosTimestamp();
	}
	mustRecalculateBranchesTree = true;
}

void BRANCHES::invalidateRelationsOfBranchSlot(int slot)
{
	for (int i = TOTAL_BOOKMARKS-1; i >= 0; i--)
	{
		cachedTimelines[i] = ITEM_UNDER_MOUSE_CLOUD;
		cachedFirstDifferences[i][slot] = FIRST_DIFFERENCE_UNKNOWN;
		cachedFirstDifferences[slot][i] = FIRST_DIFFERENCE_UNKNOWN;
		parents[i] = ITEM_UNDER_MOUSE_CLOUD;
	}
}
// returns the frame of first difference between InputLogs of snapshots of two Branches
int BRANCHES::getFirstDifferenceBetween(int firstBranch, int secondBranch)
{
	if (firstBranch == secondBranch)
		return bookmarks->bookmarksArray[firstBranch].snapshot.inputlog.size;

	if (cachedFirstDifferences[firstBranch][secondBranch] == FIRST_DIFFERENCE_UNKNOWN)
	{
		if (bookmarks->bookmarksArray[firstBranch].notEmpty && bookmarks->bookmarksArray[secondBranch].notEmpty)
		{	
			int frame = bookmarks->bookmarksArray[firstBranch].snapshot.inputlog.findFirstChange(bookmarks->bookmarksArray[secondBranch].snapshot.inputlog);
			if (frame < 0)
			{
				frame = bookmarks->bookmarksArray[firstBranch].snapshot.inputlog.size;
			}
			cachedFirstDifferences[firstBranch][secondBranch] = frame;
			cachedFirstDifferences[secondBranch][firstBranch] = frame;
			return frame;
		} else return 0;
	} else
		return cachedFirstDifferences[firstBranch][secondBranch];
}

int BRANCHES::findFullTimelineForBranch(int branchNumber)
{
	if (cachedTimelines[branchNumber] == ITEM_UNDER_MOUSE_CLOUD)
	{
		cachedTimelines[branchNumber] = branchNumber;		// by default
		std::vector<int> candidates;
		int tempKeyFrame, tempParent, maxKeyFrame, maxFirstDifference;
		// 1 - find max_first_difference among Branches that are in the same timeline
		maxFirstDifference = -1;
		int firstDiff;
		for (int i = TOTAL_BOOKMARKS-1; i >= 0; i--)
		{
			if (i != branchNumber && bookmarks->bookmarksArray[i].notEmpty)
			{
				firstDiff = getFirstDifferenceBetween(branchNumber, i);
				if (firstDiff >= bookmarks->bookmarksArray[i].snapshot.keyFrame)
				{
					if (maxFirstDifference < firstDiff)
					{
						maxFirstDifference = firstDiff;
					}
				}
			}
		}
		// 2 - find max_keyframe among those Branches whose first_diff >= max_first_difference
		maxKeyFrame = -1;
		for (int i = TOTAL_BOOKMARKS-1; i >= 0; i--)
		{
			if (bookmarks->bookmarksArray[i].notEmpty)
			{
				if (i != branchNumber && getFirstDifferenceBetween(branchNumber, i) >= maxFirstDifference && getFirstDifferenceBetween(branchNumber, i) >= bookmarks->bookmarksArray[i].snapshot.keyFrame)
				{
					// ensure that this candidate belongs to children/grandchildren of current branch
					tempParent = parents[i];
					while (tempParent != ITEM_UNDER_MOUSE_CLOUD && tempParent != branchNumber)
					{
						tempParent = parents[tempParent];
					}
					if (tempParent == branchNumber)
					{
						candidates.push_back(i);
						tempKeyFrame = bookmarks->bookmarksArray[i].snapshot.keyFrame;
						if (maxKeyFrame < tempKeyFrame)
						{
							maxKeyFrame = tempKeyFrame;
						}
					}
				}
			}
		}
		// 3 - remove those candidates who have keyframe < max_keyframe
		for (int i = candidates.size()-1; i >= 0; i--)
		{
			if (bookmarks->bookmarksArray[candidates[i]].snapshot.keyFrame < maxKeyFrame)
			{
				candidates.erase(candidates.begin() + i);
			}
		}
		// 4 - get first of candidates (if there are many then it will be the Branch with highest id number)
		if (candidates.size())
			cachedTimelines[branchNumber] = candidates[0];
	}
	return cachedTimelines[branchNumber];
}

void BRANCHES::setChangesMadeSinceBranch()
{
	bool oldStateOfChangesSinceCurrentBranch = changesSinceCurrentBranch;
	changesSinceCurrentBranch = true;
	setCurrentPosTimestamp();
	// recalculate branch tree if previous_changes = false
	if (!oldStateOfChangesSinceCurrentBranch)
	{
		mustRecalculateBranchesTree = true;
	}
	else if (bookmarks->itemUnderMouse == ITEM_UNDER_MOUSE_FIREBALL)
	{
		mustRedrawBranchesBitmap = true;	// to redraw fireball's time
	}
}

int BRANCHES::findItemUnderMouse(int mouseX, int mouseY)
{
	int item = ITEM_UNDER_MOUSE_NONE;
	QPoint mouse( mouseX, mouseY );

	for (int i = 0; i < TOTAL_BOOKMARKS; ++i)
	{
		if ( box[i].contains( mouse ) )
		{
			item = i;
		}
		//if (item == ITEM_UNDER_MOUSE_NONE && mouseX >= branchCurrentX[i] - DIGIT_RECT_HALFWIDTH_COLLISION && mouseX < branchCurrentX[i] - DIGIT_RECT_HALFWIDTH_COLLISION + DIGIT_RECT_WIDTH_COLLISION && mouseY >= branchCurrentY[i] - DIGIT_RECT_HALFHEIGHT_COLLISION && mouseY < branchCurrentY[i] - DIGIT_RECT_HALFHEIGHT_COLLISION + DIGIT_RECT_HEIGHT_COLLISION)
		//{
		//	item = i;
		//}
	}
	if (item == ITEM_UNDER_MOUSE_NONE && mouseX >= cloudCurrentX - BRANCHES_CLOUD_HALFWIDTH && mouseX < cloudCurrentX - BRANCHES_CLOUD_HALFWIDTH + BRANCHES_CLOUD_WIDTH && mouseY >= cloudCurrentY - BRANCHES_CLOUD_HALFHEIGHT && mouseY < cloudCurrentY - BRANCHES_CLOUD_HALFHEIGHT + BRANCHES_CLOUD_HEIGHT)
	{
		item = ITEM_UNDER_MOUSE_CLOUD;
	}
	if (item == ITEM_UNDER_MOUSE_NONE && changesSinceCurrentBranch && mouseX >= branchCurrentX[ITEM_UNDER_MOUSE_FIREBALL] - DIGIT_RECT_HALFWIDTH_COLLISION && mouseX < branchCurrentX[ITEM_UNDER_MOUSE_FIREBALL] - DIGIT_RECT_HALFWIDTH_COLLISION + DIGIT_RECT_WIDTH_COLLISION && mouseY >= branchCurrentY[ITEM_UNDER_MOUSE_FIREBALL] - DIGIT_RECT_HALFHEIGHT_COLLISION && mouseY < branchCurrentY[ITEM_UNDER_MOUSE_FIREBALL] - DIGIT_RECT_HALFHEIGHT_COLLISION + DIGIT_RECT_HEIGHT_COLLISION)
	{
		item = ITEM_UNDER_MOUSE_FIREBALL;
	}
	return item;
}

void BRANCHES::setCurrentPosTimestamp()
{
	time_t raw_time;
	time(&raw_time);
	struct tm * timeinfo = localtime(&raw_time);
	strftime(currentPosTimestamp, TIMESTAMP_LENGTH, "%H:%M:%S", timeinfo);
}

void BRANCHES::recalculateParents()
{
	// find best parent for every Branch
	std::vector<int> candidates;
	int tempKeyFrame, tempParent, maxKeyFrame, maxFirstDifference;
	for (int i1 = TOTAL_BOOKMARKS-1; i1 >= 0; i1--)
	{
		int i = (i1 + 1) % TOTAL_BOOKMARKS;
		if (bookmarks->bookmarksArray[i].notEmpty)
		{
			int keyframe = bookmarks->bookmarksArray[i].snapshot.keyFrame;
			// 1 - find all candidates and max_keyframe among them
			candidates.resize(0);
			maxKeyFrame = -1;
			for (int t1 = TOTAL_BOOKMARKS-1; t1 >= 0; t1--)
			{
				int t = (t1 + 1) % TOTAL_BOOKMARKS;
				tempKeyFrame = bookmarks->bookmarksArray[t].snapshot.keyFrame;
				if (t != i && bookmarks->bookmarksArray[t].notEmpty && tempKeyFrame <= keyframe && getFirstDifferenceBetween(t, i) >= tempKeyFrame)
				{
					// ensure that this candidate doesn't belong to children/grandchildren of this Branch
					tempParent = parents[t];
					while (tempParent != ITEM_UNDER_MOUSE_CLOUD && tempParent != i)
						tempParent = parents[tempParent];
					if (tempParent == ITEM_UNDER_MOUSE_CLOUD)
					{
						// all ok, this is good candidate for being the parent of the Branch
						candidates.push_back(t);
						if (maxKeyFrame < tempKeyFrame)
							maxKeyFrame = tempKeyFrame;
					}
				}
			}
			if (candidates.size())
			{
				// 2 - remove those candidates who have keyframe < max_keyframe
				// and for those who have keyframe == max_keyframe, find max_first_difference
				maxFirstDifference = -1;
				for (int t = candidates.size()-1; t >= 0; t--)
				{
					if (bookmarks->bookmarksArray[candidates[t]].snapshot.keyFrame < maxKeyFrame)
					{
						candidates.erase(candidates.begin() + t);
					}
					else if (maxFirstDifference < getFirstDifferenceBetween(candidates[t], i))
					{
						maxFirstDifference = getFirstDifferenceBetween(candidates[t], i);
					}
				}
				// 3 - remove those candidates who have FirstDifference < max_first_difference
				for (int t = candidates.size()-1; t >= 0; t--)
				{
					if (getFirstDifferenceBetween(candidates[t], i) < maxFirstDifference)
						candidates.erase(candidates.begin() + t);
				}
				// 4 - get first of candidates (if there are many then it will be the Branch with highest id number)
				if (candidates.size())
				{
					parents[i] = candidates[0];
				}
			}
		}
	}
}
void BRANCHES::recalculateBranchesTree()
{
	// save previous values
	for (int i = TOTAL_BOOKMARKS; i >= 0; i--)
	{
		branchPreviousX[i] = (branchX[i] * (BRANCHES_TRANSITION_MAX - transitionPhase) + branchPreviousX[i] * transitionPhase) / BRANCHES_TRANSITION_MAX;
		branchPreviousY[i] = (branchY[i] * (BRANCHES_TRANSITION_MAX - transitionPhase) + branchPreviousY[i] * transitionPhase) / BRANCHES_TRANSITION_MAX;
	}
	cloudPreviousX = (cloudX * (BRANCHES_TRANSITION_MAX - transitionPhase) + cloudPreviousX * transitionPhase) / BRANCHES_TRANSITION_MAX;
	cloudPreviousY = (cloudY * (BRANCHES_TRANSITION_MAX - transitionPhase) + cloudPreviousY * transitionPhase) / BRANCHES_TRANSITION_MAX;
	transitionPhase = BRANCHES_TRANSITION_MAX;

	// 0. Prepare arrays
	gridX.resize(0);
	gridY.resize(0);
	children.resize(0);
	gridHeight.resize(0);
	gridX.resize(TOTAL_BOOKMARKS+1);
	gridY.resize(TOTAL_BOOKMARKS+1);
	children.resize(TOTAL_BOOKMARKS+2);		// 0th item is for cloud's children
	gridHeight.resize(TOTAL_BOOKMARKS+1);
	for (int i = TOTAL_BOOKMARKS; i >= 0; i--)
		gridHeight[i] = 1;

	// 1. Define GridX of branches (distribute to levels) and GridHeight of branches
	int current_grid_x = 0;
	std::vector<std::vector<int>> BranchesLevels;

	std::vector<uint8> UndistributedBranches;
	UndistributedBranches.resize(TOTAL_BOOKMARKS);	// 1, 2, 3, 4, 5, 6, 7, 8, 9, 0
	for (int i = UndistributedBranches.size()-1; i >= 0; i--)
		UndistributedBranches[i] = (i + 1) % TOTAL_BOOKMARKS;
	// remove all empty branches
	for (int i = UndistributedBranches.size()-1; i >= 0; i--)
	{
		if (!bookmarks->bookmarksArray[UndistributedBranches[i]].notEmpty)
		{
			UndistributedBranches.erase(UndistributedBranches.begin() + i);
		}
	}
	// highest level: cloud (id = -1)
	BranchesLevels.resize(current_grid_x+1);
	BranchesLevels[current_grid_x].resize(1);
	BranchesLevels[current_grid_x][0] = ITEM_UNDER_MOUSE_CLOUD;
	// go lower until all branches are arranged to levels
	int current_parent;
	while(UndistributedBranches.size())
	{
		current_grid_x++;
		BranchesLevels.resize(current_grid_x+1);
		BranchesLevels[current_grid_x].resize(0);
		for (int t = BranchesLevels[current_grid_x-1].size()-1; t >= 0; t--)
		{
			current_parent = BranchesLevels[current_grid_x-1][t];
			for (int i = UndistributedBranches.size()-1; i >= 0; i--)
			{
				if (parents[UndistributedBranches[i]] == current_parent)
				{
					// assign this branch to current level
					gridX[UndistributedBranches[i]] = current_grid_x;
					BranchesLevels[current_grid_x].push_back(UndistributedBranches[i]);
					// also add it to parent's Children array
					children[current_parent+1].push_back(UndistributedBranches[i]);
					UndistributedBranches.erase(UndistributedBranches.begin() + i);
				}
			}
			if (current_parent >= 0)
			{
				gridHeight[current_parent] = children[current_parent+1].size();
				if (children[current_parent+1].size() > 1)
					recursiveAddHeight(parents[current_parent], gridHeight[current_parent] - 1);
				else
					gridHeight[current_parent] = 1;		// its own height
			}
		}
	}
	if (changesSinceCurrentBranch)
	{
		// also define "current_pos" GridX
		if (currentBranch >= 0)
		{
			if (children[currentBranch+1].size() < MAX_NUM_CHILDREN_ON_CANVAS_HEIGHT)
			{
				// "current_pos" becomes a child of current branch
				gridX[ITEM_UNDER_MOUSE_FIREBALL] = gridX[currentBranch] + 1;
				if ((int)BranchesLevels.size() <= gridX[ITEM_UNDER_MOUSE_FIREBALL])
					BranchesLevels.resize(gridX[ITEM_UNDER_MOUSE_FIREBALL] + 1);
				BranchesLevels[gridX[ITEM_UNDER_MOUSE_FIREBALL]].push_back(ITEM_UNDER_MOUSE_FIREBALL);
				children[currentBranch + 1].push_back(ITEM_UNDER_MOUSE_FIREBALL);
				if (children[currentBranch+1].size() > 1)
					recursiveAddHeight(currentBranch, 1);
			} else
			{
				// special case 0: if there's too many children on one level (more than canvas can show)
				// then "current_pos" becomes special branch above current branch
				gridX[ITEM_UNDER_MOUSE_FIREBALL] = gridX[currentBranch];
				gridY[ITEM_UNDER_MOUSE_FIREBALL] = gridY[currentBranch] - 7;
			}
		} else
		{
			// special case 1: fireball is the one and only child of cloud
			gridX[ITEM_UNDER_MOUSE_FIREBALL] = 1;
			gridY[ITEM_UNDER_MOUSE_FIREBALL] = 0;
			if ((int)BranchesLevels.size() <= gridX[ITEM_UNDER_MOUSE_FIREBALL])
				BranchesLevels.resize(gridX[ITEM_UNDER_MOUSE_FIREBALL] + 1);
			BranchesLevels[gridX[ITEM_UNDER_MOUSE_FIREBALL]].push_back(ITEM_UNDER_MOUSE_FIREBALL);
		}
	}
	// define grid_width
	int grid_width, cloud_prefix = 0;
	if (BranchesLevels.size()-1 > 0)
	{
		//grid_width = BRANCHES_CANVAS_WIDTH / (BranchesLevels.size()-1);
		grid_width = width() / (BranchesLevels.size()+1);
		//if (grid_width < BRANCHES_GRID_MIN_WIDTH)
		//{
		//	grid_width = BRANCHES_GRID_MIN_WIDTH;
		//}
		//else if (grid_width > BRANCHES_GRID_MAX_WIDTH)
		//{
		//	grid_width = BRANCHES_GRID_MAX_WIDTH;
		//}
		if (grid_width < pxMinGridWidth)
		{
			grid_width = pxMinGridWidth;
		}
		else if (grid_width > pxMaxGridWidth)
		{
			grid_width = pxMaxGridWidth;
		}
	}
	else
	{
		grid_width = pxMaxGridWidth;
	}
	pxGridWidth = grid_width;

	if (grid_width < MIN_CLOUD_LINE_LENGTH)
	{
		cloud_prefix = MIN_CLOUD_LINE_LENGTH - grid_width;
	}

	// 2. Define GridY of branches
	recursiveSetYPos(ITEM_UNDER_MOUSE_CLOUD, 0);
	// define grid_halfheight
	int grid_halfheight;
	int totalHeight = 0;
	for (int i = children[0].size()-1; i >= 0; i--)
	{
		totalHeight += gridHeight[children[0][i]];
	}
	if (totalHeight)
	{
		//grid_halfheight = BRANCHES_CANVAS_HEIGHT / (2 * totalHeight);
		grid_halfheight = height() / (2 * totalHeight);
		//if (grid_halfheight < BRANCHES_GRID_MIN_HALFHEIGHT)
		//{
		//	grid_halfheight = BRANCHES_GRID_MIN_HALFHEIGHT;
		//}
		//else if (grid_halfheight > BRANCHES_GRID_MAX_HALFHEIGHT)
		//{
		//	grid_halfheight = BRANCHES_GRID_MAX_HALFHEIGHT;
		//}

		if (grid_halfheight < pxMinGridHalfHeight)
		{
			grid_halfheight = pxMinGridHalfHeight;
		}
		else if (grid_halfheight > pxMaxGridHalfHeight)
		{
			grid_halfheight = pxMaxGridHalfHeight;
		}
	}
	else
	{
		//grid_halfheight = BRANCHES_GRID_MAX_HALFHEIGHT;
		grid_halfheight = pxMaxGridHalfHeight;
	}
	pxGridHalfHeight = grid_halfheight;

	// special case 2: if chain of branches is too long, the last item (fireball) goes up
	//if (changesSinceCurrentBranch)
	//{
	//	if (gridX[ITEM_UNDER_MOUSE_FIREBALL] > MAX_CHAIN_LEN)
	//	{
	//		gridX[ITEM_UNDER_MOUSE_FIREBALL] = MAX_CHAIN_LEN;
	//		gridY[ITEM_UNDER_MOUSE_FIREBALL] -= 2;
	//	}
	//}
	// special case 3: if some branch crosses upper or lower border of canvas
	int parent;
	for (int t = TOTAL_BOOKMARKS; t >= 0; t--)
	{
		if (gridY[t] > MAX_GRID_Y_POS)
		{
			if (t < TOTAL_BOOKMARKS)
				parent = parents[t];
			else
				parent = currentBranch;
			int pos = MAX_GRID_Y_POS;
			for (int i = 0; i < (int)children[parent+1].size(); ++i)
			{
				gridY[children[parent+1][i]] = pos;
				if (children[parent+1][i] == currentBranch)
					gridY[ITEM_UNDER_MOUSE_FIREBALL] = pos;
				pos -= 2;
			}
		} else if (gridY[t] < -MAX_GRID_Y_POS)
		{
			if (t < TOTAL_BOOKMARKS)
				parent = parents[t];
			else
				parent = currentBranch;
			int pos = -MAX_GRID_Y_POS;
			for (int i = children[parent+1].size()-1; i >= 0; i--)
			{
				gridY[children[parent+1][i]] = pos;
				if (children[parent+1][i] == currentBranch)
					gridY[ITEM_UNDER_MOUSE_FIREBALL] = pos;
				pos += 2;
			}
		}
	}
	// special case 4: if cloud has all 10 children, then one child will be out of canvas
	//if (children[0].size() == TOTAL_BOOKMARKS)
	//{
	//	// find this child and move it to be visible
	//	for (int t = TOTAL_BOOKMARKS - 1; t >= 0; t--)
	//	{
	//		if (gridY[t] > MAX_GRID_Y_POS)
	//		{
	//			gridY[t] = MAX_GRID_Y_POS;
	//			gridX[t] -= 2;
	//			// also move fireball to position near this branch
	//			if (changesSinceCurrentBranch && currentBranch == t)
	//			{
	//				gridY[ITEM_UNDER_MOUSE_FIREBALL] = gridY[t];
	//				gridX[ITEM_UNDER_MOUSE_FIREBALL] = gridX[t] + 1;
	//			}
	//			break;
	//		} else if (gridY[t] < -MAX_GRID_Y_POS)
	//		{
	//			gridY[t] = -MAX_GRID_Y_POS;
	//			gridX[t] -= 2;
	//			// also move fireball to position near this branch
	//			if (changesSinceCurrentBranch && currentBranch == t)
	//			{
	//				gridY[ITEM_UNDER_MOUSE_FIREBALL] = gridY[t];
	//				gridX[ITEM_UNDER_MOUSE_FIREBALL] = gridX[t] + 1;
	//			}
	//			break;
	//		}
	//	}
	//}

	int cloudYMin = EMPTY_BRANCHES_Y_BASE + (pxBoxHeight*4);
	int cloudYMax = (height()/2) + grid_halfheight;

	cloudY = cloudYMin + ( (cloudYMax - cloudYMin) * totalHeight ) / 10;

	// 3. Set pixel positions of branches
	int max_x = 0;
	for (int i = TOTAL_BOOKMARKS-1; i >= 0; i--)
	{
		if (bookmarks->bookmarksArray[i].notEmpty)
		{
			branchX[i] = cloud_prefix + gridX[i] * grid_width;
			branchY[i] = cloudY + gridY[i] * grid_halfheight;
		}
		else
		{
			branchX[i] = EMPTY_BRANCHES_X_BASE;
			branchY[i] = EMPTY_BRANCHES_Y_BASE + pxBoxHeight * ((i + TOTAL_BOOKMARKS - 1) % TOTAL_BOOKMARKS);
		}
		if (max_x < branchX[i]) max_x = branchX[i];
	}
	if (changesSinceCurrentBranch)
	{
		// also set pixel position of "current_pos"
		branchX[ITEM_UNDER_MOUSE_FIREBALL] = cloud_prefix + gridX[ITEM_UNDER_MOUSE_FIREBALL] * grid_width;
		branchY[ITEM_UNDER_MOUSE_FIREBALL] = cloudY + gridY[ITEM_UNDER_MOUSE_FIREBALL] * grid_halfheight;
	}
	else if (currentBranch >= 0)
	{
		branchX[ITEM_UNDER_MOUSE_FIREBALL] = cloud_prefix + gridX[currentBranch] * grid_width;
		branchY[ITEM_UNDER_MOUSE_FIREBALL] = cloudY + gridY[currentBranch] * grid_halfheight;
	}
	else
	{
		branchX[ITEM_UNDER_MOUSE_FIREBALL] = 0;
		branchY[ITEM_UNDER_MOUSE_FIREBALL] = cloudY;
	}
	if (max_x < branchX[ITEM_UNDER_MOUSE_FIREBALL])
	{
		max_x = branchX[ITEM_UNDER_MOUSE_FIREBALL];
	}

	// align whole tree horizontally
	//cloudX = (BRANCHES_BITMAP_WIDTH + BASE_HORIZONTAL_SHIFT - max_x) / 2;
	cloudX = (width() + BASE_HORIZONTAL_SHIFT - max_x) / 2;
	//if (cloudX < MIN_CLOUD_X)
	//{
	//	cloudX = MIN_CLOUD_X;
	//}
	if (cloudX < pxBoxWidth)
	{
		cloudX = pxBoxWidth;
	}
	for (int i = TOTAL_BOOKMARKS-1; i >= 0; i--)
	{
		if (bookmarks->bookmarksArray[i].notEmpty)
		{
			branchX[i] += cloudX;
		}
	}
	branchX[ITEM_UNDER_MOUSE_FIREBALL] += cloudX;

	// finished recalculating
	mustRecalculateBranchesTree = false;
	mustRedrawBranchesBitmap = true;
}
void BRANCHES::recursiveAddHeight(int branchNumber, int amount)
{
	if (branchNumber >= 0)
	{
		gridHeight[branchNumber] += amount;
		if (parents[branchNumber] >= 0)
			recursiveAddHeight(parents[branchNumber], amount);
	}
}
void BRANCHES::recursiveSetYPos(int parent, int parentY)
{
	if (children[parent+1].size())
	{
		// find total height of children
		int totalHeight = 0;
		for (int i = children[parent+1].size()-1; i >= 0; i--)
			totalHeight += gridHeight[children[parent+1][i]];
		// set Y of children and subchildren
		for (int i = children[parent+1].size()-1; i >= 0; i--)
		{
			int child_id = children[parent+1][i];
			gridY[child_id] = parentY + gridHeight[child_id] - totalHeight;
			recursiveSetYPos(child_id, gridY[child_id]);
			parentY += 2 * gridHeight[child_id];
		}
	}
}

// ----------------------------------------------------------------------------------------
//LRESULT APIENTRY BranchesBitmapWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
//{
//	extern BRANCHES branches;
//	switch(msg)
//	{
//		case WM_SETCURSOR:
//		{
//			taseditorWindow.mustUpdateMouseCursor = true;
//			return true;
//		}
//		case WM_MOUSEMOVE:
//		{
//			if (!bookmarks.mouseOverBranchesBitmap)
//			{
//				bookmarks.mouseOverBranchesBitmap = true;
//				bookmarks.tme.hwndTrack = hWnd;
//				TrackMouseEvent(&bookmarks.tme);
//			}
//			bookmarks.handleMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
//			break;
//		}
//		case WM_MOUSELEAVE:
//		{
//			bookmarks.mouseOverBranchesBitmap = false;
//			bookmarks.handleMouseMove(-1, -1);
//			break;
//		}
//		case WM_PAINT:
//		{
//			PAINTSTRUCT ps;
//			branches.paintBranchesBitmap(BeginPaint(hWnd, &ps));
//			EndPaint(hWnd, &ps);
//			return 0;
//		}
//		case WM_LBUTTONDOWN:
//		{
//			// single click on Branches Tree = send Playback to the Bookmark
//			int branchUnderMouse = bookmarks.itemUnderMouse;
//			if (branchUnderMouse == ITEM_UNDER_MOUSE_CLOUD)
//			{
//				playback->jump(0);
//			} else if (branchUnderMouse >= 0 && branchUnderMouse < TOTAL_BOOKMARKS && bookmarks.bookmarksArray[branchUnderMouse].notEmpty)
//			{
//				bookmarks->command(COMMAND_JUMP, branchUnderMouse);
//			} else if (branchUnderMouse == ITEM_UNDER_MOUSE_FIREBALL)
//			{
//				playback->jump(currMovieData.getNumRecords() - 1);
//			}
//			//if (GetFocus() != hWnd)
//			//{
//			//	SetFocus(hWnd);
//			//}
//			return 0;
//		}
//		case WM_LBUTTONDBLCLK:
//		{
//			// double click on Branches Tree = deploy the Branch
//			int branchUnderMouse = bookmarks.itemUnderMouse;
//			if (branchUnderMouse == ITEM_UNDER_MOUSE_CLOUD)
//			{
//				playback->jump(0);
//			} else if (branchUnderMouse >= 0 && branchUnderMouse < TOTAL_BOOKMARKS && bookmarks.bookmarksArray[branchUnderMouse].notEmpty)
//			{
//				bookmarks->command(COMMAND_DEPLOY, branchUnderMouse);
//			} else if (branchUnderMouse == ITEM_UNDER_MOUSE_FIREBALL)
//			{
//				playback->jump(currMovieData.getNumRecords() - 1);
//			}
//			if (GetFocus() != hWnd)
//			{
//				SetFocus(hWnd);
//			}
//			return 0;
//		}
//		case WM_RBUTTONDOWN:
//		case WM_RBUTTONDBLCLK:
//		{
//			if (GetFocus() != hWnd)
//				SetFocus(hWnd);
//			branches.branchRightclicked = branches.findItemUnderMouse(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
//			if (branches.branchRightclicked >= 0 && branches.branchRightclicked < TOTAL_BOOKMARKS)
//				SetCapture(hWnd);
//			return 0;
//		}
//		case WM_RBUTTONUP:
//		{
//			if (branches.branchRightclicked >= 0 && branches.branchRightclicked < TOTAL_BOOKMARKS
//				&& branches.branchRightclicked == branches.findItemUnderMouse(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
//				bookmarks.command(COMMAND_SET, branches.branchRightclicked);
//			ReleaseCapture();
//			branches.branchRightclicked = ITEM_UNDER_MOUSE_NONE;
//			return 0;
//		}
//		case WM_MBUTTONDOWN:
//		case WM_MBUTTONDBLCLK:
//		{
//			if (GetFocus() != hWnd)
//				SetFocus(hWnd);
//			playback.handleMiddleButtonClick();
//			return 0;
//		}
//		case WM_MOUSEWHEEL:
//		{
//			branches.branchRightclicked = ITEM_UNDER_MOUSE_NONE;	// ensure that accidental rightclick on BookmarksList won't set Bookmarks when user does rightbutton + wheel
//			return SendMessage(pianoRoll.hwndList, msg, wParam, lParam);
//		}
//	}
//	return CallWindowProc(hwndBranchesBitmap_oldWndProc, hWnd, msg, wParam, lParam);
//}


