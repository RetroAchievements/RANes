/* ---------------------------------------------------------------------------------
Implementation file of SELECTION class
Copyright (c) 2011-2013 AnS

(The MIT License)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------------
Selection - Manager of selections
[Single instance]

* contains definition of the type "Set of selected frames"
* stores array of Sets of selected frames (History of selections)
* saves and loads the data from a project file. On error: clears the array and starts new history by making empty selection
* constantly tracks changes in selected rows of Piano Roll List, and makes a decision to create new point of Selection rollback
* implements all Selection restoring operations: undo, redo
* on demand: changes current selection: remove selection, jump to a frame with Selection cursor, select region, select all, select between Markers, reselect clipboard
* regularly ensures that Selection doesn't go beyond curent Piano Roll limits, detects if Selection moved to another Marker and updates Note in the lower text field
* implements the working of lower buttons << and >> (jumping on Markers)
* also here's the code of lower text field (for editing Marker Notes)
* stores resource: save id, lower text field prefix
------------------------------------------------------------------------------------ */

#include "fceu.h"
#include "Qt/TasEditor/inputlog.h"
#include "Qt/TasEditor/playback.h"
#include "Qt/TasEditor/taseditor_project.h"
#include "Qt/TasEditor/TasEditorWindow.h"

extern int joysticksPerFrame[INPUT_TYPES_TOTAL];

// resources
static char selection_save_id[SELECTION_ID_LEN] = "SELECTION";
static char selection_skipsave_id[SELECTION_ID_LEN] = "SELECTIOX";
static char lowerMarkerText[] = "Marker ";

SELECTION::SELECTION()
{
	trackSelectionChanges = true;
	lastSelectionBeginning = -1;

	previousMarkerButtonState = previousMarkerButtonOldState = false;
	nextMarkerButtonState = nextMarkerButtonOldState = false;
	buttonHoldTimer = 0;

	historyCursorPos = -1;
	historyStartPos = 0;
	historySize = 1;
	historyTotalItems = 0;
}

void SELECTION::init()
{
	reset();
}
void SELECTION::free()
{
	// clear history
	rowsSelectionHistory.resize(0);
	historyTotalItems = 0;
	tempRowsSelection.clear();
}
void SELECTION::reset()
{
	free();
	// init vars
	displayedMarkerNumber = 0;
	lastSelectionBeginning = -1;
	historySize = taseditorConfig->maxUndoLevels + 1;
	rowsSelectionHistory.resize(historySize);
	historyStartPos = 0;
	historyCursorPos = -1;
	// create initial selection
	addNewSelectionToHistory();
	trackSelectionChanges = true;
	reset_vars();
}
void SELECTION::reset_vars()
{
	previousMarkerButtonOldState = previousMarkerButtonState = false;
	nextMarkerButtonOldState = nextMarkerButtonState = false;
	mustFindCurrentMarker = true;
}
void SELECTION::update()
{
	updateSelectionSize();

	// update << and >> buttons
	previousMarkerButtonOldState = previousMarkerButtonState;
	//previousMarkerButtonState = ((Button_GetState(hwndPreviousMarkerButton) & BST_PUSHED) != 0);
	if (previousMarkerButtonState)
	{
		if (!previousMarkerButtonOldState)
		{
			buttonHoldTimer = getTasEditorTime();
			jumpToPreviousMarker();
		} else if (buttonHoldTimer + BUTTON_HOLD_REPEAT_DELAY < getTasEditorTime())
		{
			jumpToPreviousMarker();
		}
	}
	nextMarkerButtonOldState = nextMarkerButtonState;
	//nextMarkerButtonState = (Button_GetState(hwndNextMarkerButton) & BST_PUSHED) != 0;
	if (nextMarkerButtonState)
	{
		if (!nextMarkerButtonOldState)
		{
			buttonHoldTimer = getTasEditorTime();
			jumpToNextMarker();
		}
		else if (buttonHoldTimer + BUTTON_HOLD_REPEAT_DELAY < getTasEditorTime())
		{
			jumpToNextMarker();
		}
	}

	// track changes of Selection beginning (Selection cursor)
	if (lastSelectionBeginning != getCurrentRowsSelectionBeginning())
	{
		lastSelectionBeginning = getCurrentRowsSelectionBeginning();
		mustFindCurrentMarker = true;
	}

	// update "Selection's Marker text" if needed
	if (mustFindCurrentMarker)
	{
		markersManager->updateEditedMarkerNote();
		displayedMarkerNumber = markersManager->getMarkerAboveFrame(lastSelectionBeginning);
		redrawMarkerData();
		mustFindCurrentMarker = false;
	}

}

void SELECTION::updateSelectionSize()
{
	// keep Selection within Piano Roll limits
	if (getCurrentRowsSelection().size())
	{
		int delete_index;
		int movie_size = currMovieData.getNumRecords();
		while (true)
		{
			delete_index = *getCurrentRowsSelection().rbegin();
			if (delete_index < movie_size) break;
			getCurrentRowsSelection().erase(delete_index);
			if (!getCurrentRowsSelection().size()) break;
		}
	}
}

void SELECTION::updateHistoryLogSize()
{
	int new_history_size = taseditorConfig->maxUndoLevels + 1;
	std::vector<RowsSelection> new_selections_history(new_history_size);
	int pos = historyCursorPos, source_pos = historyCursorPos;
	if (pos >= new_history_size)
	{
		pos = new_history_size - 1;
	}
	int new_history_cursor_pos = pos;
	// copy old "undo" snapshots
	while (pos >= 0)
	{
		new_selections_history[pos] = rowsSelectionHistory[(historyStartPos + source_pos) % historySize];
		pos--;
		source_pos--;
	}
	// copy old "redo" snapshots
	int num_redo_snapshots = historyTotalItems - (historyCursorPos + 1);
	int space_available = new_history_size - (new_history_cursor_pos + 1);
	int i = (num_redo_snapshots <= space_available) ? num_redo_snapshots : space_available;
	int new_history_total_items = new_history_cursor_pos + i + 1;
	for (; i > 0; i--)
	{
		new_selections_history[new_history_cursor_pos + i] = rowsSelectionHistory[(historyStartPos + historyCursorPos + i) % historySize];
	}
	// finish
	rowsSelectionHistory = new_selections_history;
	historySize = new_history_size;
	historyStartPos = 0;
	historyCursorPos = new_history_cursor_pos;
	historyTotalItems = new_history_total_items;
}

void SELECTION::redrawMarkerData()
{
	// redraw Marker num
	char new_text[MAX_NOTE_LEN] = {0};
	if (displayedMarkerNumber <= 9999)		// if there's too many digits in the number then don't show the word "Marker" before the number
	{
		strcpy(new_text, lowerMarkerText);
	}
	char num[16];
	sprintf( num, "%i", displayedMarkerNumber);
	strcat(new_text, num);
	strcat(new_text, " ");
	tasWin->lowerMarkerLabel->setText( QObject::tr(new_text) );

	// change Marker Note
	strcpy(new_text, markersManager->getNoteCopy(displayedMarkerNumber).c_str());
	tasWin->lowerMarkerNote->setText( QObject::tr(new_text) );

	//printf("Marker %i: '%s'\n", displayedMarkerNumber, new_text );
}

void SELECTION::jumpToPreviousMarker(int speed)
{
	// if nothing is selected, consider Playback cursor as current selection
	int index = getCurrentRowsSelectionBeginning();
	if (index < 0) index = currFrameCounter;
	// jump trough "speed" amount of previous Markers
	while (speed > 0)
	{
		for (index--; index >= 0; index--)
		{
			if (markersManager->getMarkerAtFrame(index)) break;
		}
		speed--;
	}
	if (index >= 0)
	{
		jumpToFrame(index);							// jump to the Marker
	}
	else
	{
		jumpToFrame(0);								// jump to the beginning of Piano Roll
	}
}
void SELECTION::jumpToNextMarker(int speed)
{
	// if nothing is selected, consider Playback cursor as current selection
	int index = getCurrentRowsSelectionBeginning();
	if (index < 0) index = currFrameCounter;
	int last_frame = currMovieData.getNumRecords() - 1;		// the end of Piano Roll
	// jump trough "speed" amount of previous Markers
	while (speed > 0)
	{
		for (++index; index <= last_frame; ++index)
		{
			if (markersManager->getMarkerAtFrame(index)) break;
		}
		speed--;
	}
	if (index <= last_frame)
	{
		jumpToFrame(index);			// jump to Marker
	}
	else
	{
		jumpToFrame(last_frame);	// jump to the end of Piano Roll
	}
}
void SELECTION::jumpToFrame(int frame)
{
	clearAllRowsSelection();
	setRowSelection(frame);
	tasWin->pianoRoll->followSelection();
}
// ----------------------------------------------------------
void SELECTION::save(EMUFILE *os, bool really_save)
{
	if (really_save)
	{
		int last_tick = -1;

		setTasProjectProgressBarText("Saving Selection...");
		setTasProjectProgressBar( 0, historyTotalItems );

		// write "SELECTION" string
		os->fwrite(selection_save_id, SELECTION_ID_LEN);
		// write vars
		write32le(historyCursorPos, os);
		write32le(historyTotalItems, os);
		// write selections starting from history_start_pos
		for (int i = 0; i < historyTotalItems; ++i)
		{
			saveSelection(rowsSelectionHistory[(historyStartPos + i) % historySize], os);

			if (i / SAVING_HISTORY_PROGRESSBAR_UPDATE_RATE > last_tick)
			{
				setTasProjectProgressBar( i, historyTotalItems );
				playback->setProgressbar(i, historyTotalItems);
				last_tick = i / PROGRESSBAR_UPDATE_RATE;
			}
		}
		// write clipboard_selection
		saveSelection(splicer->getClipboardSelection(), os);

		setTasProjectProgressBar( historyTotalItems, historyTotalItems );
	}
	else
	{
		// write "SELECTIOX" string
		os->fwrite(selection_skipsave_id, SELECTION_ID_LEN);
	}
}
// returns true if couldn't load
bool SELECTION::load(EMUFILE *is, unsigned int offset)
{
	int i, total;
	if (offset)
	{
		if (is->fseek(offset, SEEK_SET)) goto error;
	} else
	{
		reset();
		return false;
	}
	// read "SELECTION" string
	char save_id[SELECTION_ID_LEN];
	if ((int)is->fread(save_id, SELECTION_ID_LEN) < SELECTION_ID_LEN) goto error;
	if (!strcmp(selection_skipsave_id, save_id))
	{
		// string says to skip loading Selection
		FCEU_printf("No Selection in the file\n");
		reset();
		return false;
	}
	if (strcmp(selection_save_id, save_id)) goto error;		// string is not valid
	// read vars
	if (!read32le(&historyCursorPos, is)) goto error;
	if (!read32le(&historyTotalItems, is)) goto error;
	if (historyCursorPos > historyTotalItems) goto error;
	historyStartPos = 0;
	// read selections
	total = historyTotalItems;
	if (historyTotalItems > historySize)
	{
		// user can't afford that much undo levels, skip some selections
		int num_selections_to_skip = historyTotalItems - historySize;
		// first try to skip selections over history_cursor_pos (future selections), because "redo" is less important than "undo"
		int num_redo_selections = historyTotalItems-1 - historyCursorPos;
		if (num_selections_to_skip >= num_redo_selections)
		{
			// skip all redo selections
			historyTotalItems = historyCursorPos+1;
			num_selections_to_skip -= num_redo_selections;
			// and still need to skip some undo selections
			for (i = 0; i < num_selections_to_skip; ++i)
				if (skipLoadSelection(is)) goto error;
			total -= num_selections_to_skip;
			historyCursorPos -= num_selections_to_skip;
		}
		historyTotalItems -= num_selections_to_skip;
	}
	// load selections
	for (i = 0; i < historyTotalItems; ++i)
	{
		if (loadSelection(rowsSelectionHistory[i], is)) goto error;
	}
	// skip redo selections if needed
	for (; i < total; ++i)
		if (skipLoadSelection(is)) goto error;
	
	// read clipboard_selection
	if (loadSelection(splicer->getClipboardSelection(), is)) goto error;
	// all ok
	enforceRowsSelectionToList();
	reset_vars();
	return false;
error:
	FCEU_printf("Error loading Selection\n");
	reset();
	return true;
}

void SELECTION::saveSelection(RowsSelection& selection, EMUFILE *os)
{
	write32le(selection.size(), os);
	if (selection.size())
	{
		for(RowsSelection::iterator it(selection.begin()); it != selection.end(); it++)
		{
			write32le(*it, os);
		}
	}
}
bool SELECTION::loadSelection(RowsSelection& selection, EMUFILE *is)
{
	int temp_int, temp_size;
	if (!read32le(&temp_size, is)) return true;
	selection.clear();
	for(; temp_size > 0; temp_size--)
	{
		if (!read32le(&temp_int, is)) return true;
		selection.insert(temp_int);
	}
	return false;
}
bool SELECTION::skipLoadSelection(EMUFILE *is)
{
	int temp_size;
	if (!read32le(&temp_size, is)) return true;
	if (is->fseek(temp_size * sizeof(int), SEEK_CUR)) return true;
	return false;
}
// ----------------------------------------------------------
// used to track selection
void SELECTION::noteThatItemRangeChanged(int startItem, int endItem, int newValue )
{
	bool ON =   newValue;
	//bool OFF = !newValue;

	if (ON)
	{
		for(int i = startItem; i <= endItem; ++i)
		{
			getCurrentRowsSelection().insert(i);
		}
	}
	else
	{
		for(int i = startItem; i <= endItem; ++i)
		{
			getCurrentRowsSelection().erase(i);
		}
	}

	splicer->mustRedrawInfoAboutSelection = true;
}
void SELECTION::noteThatItemChanged(int item, int newValue )
{
	bool ON =   newValue;
	bool OFF = !newValue;

	//if the item is -1, apply the change to all items
	if (item == -1)
	{
		if (OFF)
		{
			// clear all (actually add new empty Selection to history)
			if (getCurrentRowsSelection().size() && trackSelectionChanges)
			{
				addNewSelectionToHistory();
			}
		}
		else if (ON)
		{
			// select all
			for(int i = currMovieData.getNumRecords() - 1; i >= 0; i--)
			{
				getCurrentRowsSelection().insert(i);
			}
		}
	}
	else
	{
		if (ON)
		{
			getCurrentRowsSelection().insert(item);
		}
		else if (OFF) 
		{
			getCurrentRowsSelection().erase(item);
		}
	}

	splicer->mustRedrawInfoAboutSelection = true;
}
// ----------------------------------------------------------
void SELECTION::addNewSelectionToHistory()
{
	// create new empty selection
	RowsSelection selectionFrames;
	// increase current position
	// history uses ring buffer (vector with fixed size) to avoid resizing
	if (historyCursorPos+1 >= historySize)
	{
		// reached the end of available history_size - move history_start_pos (thus deleting oldest selection)
		historyCursorPos = historySize-1;
		historyStartPos = (historyStartPos + 1) % historySize;
	} else
	{
		// didn't reach the end of history yet
		historyCursorPos++;
		if (historyCursorPos >= historyTotalItems)
			historyTotalItems = historyCursorPos+1;
	}
	// add
	rowsSelectionHistory[(historyStartPos + historyCursorPos) % historySize] = selectionFrames;
}
void SELECTION::addCurrentSelectionToHistory()
{
	// create the copy of current selection
	RowsSelection selectionFrames = rowsSelectionHistory[(historyStartPos + historyCursorPos) % historySize];
	// increase current position
	// history uses ring buffer (vector with fixed size) to avoid resizing
	if (historyCursorPos+1 >= historySize)
	{
		// reached the end of available history_size - move history_start_pos (thus deleting oldest selection)
		historyCursorPos = historySize-1;
		historyStartPos = (historyStartPos + 1) % historySize;
	} else
	{
		// didn't reach the end of history yet
		historyCursorPos++;
		if (historyCursorPos >= historyTotalItems)
			historyTotalItems = historyCursorPos+1;
	}
	// add
	rowsSelectionHistory[(historyStartPos + historyCursorPos) % historySize] = selectionFrames;
}

void SELECTION::jumpInTime(int new_pos)
{
	if (new_pos < 0) new_pos = 0; else if (new_pos >= historyTotalItems) new_pos = historyTotalItems-1;
	if (new_pos == historyCursorPos) return;

	// make jump
	historyCursorPos = new_pos;
	// update Piano Roll items
	enforceRowsSelectionToList();
	// also keep Selection within Piano Roll
	updateSelectionSize();
}
void SELECTION::undo()
{
	jumpInTime(historyCursorPos - 1);
}
void SELECTION::redo()
{
	jumpInTime(historyCursorPos + 1);
}
// ----------------------------------------------------------
bool SELECTION::isRowSelected(int index)
{
	std::map <int, int>::iterator it;
	/*
	if (CurrentSelection().find(frame) == CurrentSelection().end())
		return false;
	return true;
	*/
	//return false; // ListView_GetItemState(pianoRoll.hwndList, index, LVIS_SELECTED) != 0;
	it = selList.find(index);

	if ( it != selList.end() )
	{
		return it->second ? true : false;
	}
	return false;
}

void SELECTION::clearAllRowsSelection()
{
	if ( selList.size() > 0 )
	{
		noteThatItemChanged(-1, 0);
	}
	//ListView_SetItemState(pianoRoll.hwndList, -1, 0, LVIS_SELECTED);
	selList.clear();
}
void SELECTION::clearSingleRowSelection(int index)
{
	if ( selList.size() == 0 )
	{
		return;
	}
	std::map <int, int>::iterator it;
	//ListView_SetItemState(pianoRoll.hwndList, index, 0, LVIS_SELECTED);
	
	it = selList.find(index);

	if ( it != selList.end() )
	{
		if ( it->second )
		{
			noteThatItemChanged(index, 0);
		}
		selList.erase(it);
	}
}
void SELECTION::clearRegionOfRowsSelection(int start, int end)
{
	if ( selList.size() == 0 )
	{
		return;
	}
	std::map <int, int>::iterator start_it, end_it;
	//for (int i = start; i < end; ++i)
	//	ListView_SetItemState(pianoRoll.hwndList, i, 0, LVIS_SELECTED);
	
	start_it = selList.find(start);
	  end_it = selList.find(end);

	if ( (start_it != selList.end()) )
	{
		if ( (end_it != selList.end()) )
		{
			selList.erase( start_it, end_it );
		}
		else
		{
			while (start_it != selList.end())
			{
				start_it = selList.erase( start_it );
			}
		}
		noteThatItemRangeChanged(start, end, 0);
	}
	else
	{
		selList.clear();
		noteThatItemChanged( -1, 0);
	}
}

void SELECTION::selectAllRows(void)
{
	noteThatItemChanged(-1, 1);

	for (size_t i = 0; i < currMovieData.records.size(); ++i)
	{
		selList[i] = 1;
	}
	//ListView_SetItemState(pianoRoll.hwndList, -1, LVIS_SELECTED, LVIS_SELECTED);
}
void SELECTION::setRowSelection(int index)
{
	std::map <int, int>::iterator it;

	it = selList.find(index);

	if ( it != selList.end() )
	{
		if ( !it->second )
		{
			noteThatItemChanged(index, 1);
			it->second = 1;
		}
	}
	else
	{
		noteThatItemChanged(index, 1);
		selList[index] = 1;
	}
	//ListView_SetItemState(pianoRoll.hwndList, index, LVIS_SELECTED, LVIS_SELECTED);
}
void SELECTION::setRegionOfRowsSelection(int start, int end)
{
	//for (int i = start; i < end; ++i)
	//	ListView_SetItemState(pianoRoll.hwndList, i, LVIS_SELECTED, LVIS_SELECTED);
	noteThatItemRangeChanged(start, end, 1);

	for (int i = start; i < end; ++i)
	{
		selList[i] = 1;
	}
}

void SELECTION::setRegionOfRowsSelectionUsingPattern(int start, int end)
{
	int pattern_offset = 0, current_pattern = taseditorConfig->currentPattern;
	for (int i = start; i <= end; ++i)
	{
		// skip lag frames
		if (taseditorConfig->autofirePatternSkipsLag && greenzone->lagLog.getLagInfoAtFrame(i) == LAGGED_YES)
		{
			continue;
		}
		if (tasWin->patterns[current_pattern][pattern_offset])
		{
			selList[i] = 1;
			//ListView_SetItemState(pianoRoll.hwndList, i, LVIS_SELECTED, LVIS_SELECTED);
		}
		else
		{
			selList[i] = 0;
			//ListView_SetItemState(pianoRoll.hwndList, i, 0, LVIS_SELECTED);
		}
		pattern_offset++;
		if (pattern_offset >= (int)tasWin->patterns[current_pattern].size())
		{
			pattern_offset -= tasWin->patterns[current_pattern].size();
		}
	}
}
void SELECTION::selectAllRowsBetweenMarkers()
{
	int center, upper_border, lower_border;
	int upper_marker, lower_marker;
	int movie_size = currMovieData.getNumRecords();

	// if nothing is selected then Playback cursor serves as Selection cursor
	if (getCurrentRowsSelection().size())
	{
		upper_border = center = *getCurrentRowsSelection().begin();
		lower_border = *getCurrentRowsSelection().rbegin();
	}
	else
	{
		lower_border = upper_border = center = currFrameCounter;
	}

	// find Markers
	// searching up starting from center-0
	for (upper_marker = center; upper_marker >= 0; upper_marker--)
	{
		if (markersManager->getMarkerAtFrame(upper_marker)) break;
	}
	// searching down starting from center+1
	for (lower_marker = center+1; lower_marker < movie_size; ++lower_marker)
	{
		if (markersManager->getMarkerAtFrame(lower_marker)) break;
	}

	clearAllRowsSelection();

	// special case
	if (upper_marker == -1 && lower_marker == movie_size)
	{
		selectAllRows();
		return;
	}

	// selecting circle: 1-2-3-4-1-2-3-4...
	if (upper_border > upper_marker+1 || lower_border < lower_marker-1 || lower_border > lower_marker)
	{
		// 1 - default: select all between Markers, not including lower Marker
		if (upper_marker < 0) upper_marker = 0;
		for (int i = upper_marker; i < lower_marker; ++i)
		{
			//ListView_SetItemState(pianoRoll.hwndList, i, LVIS_SELECTED, LVIS_SELECTED);
			selList[i] = 1;
		}
	}
	else if (upper_border == upper_marker && lower_border == lower_marker-1)
	{
		// 2 - selected all between Markers and upper Marker selected too: select all between Markers, not including Markers
		for (int i = upper_marker+1; i < lower_marker; ++i)
		{
			//ListView_SetItemState(pianoRoll.hwndList, i, LVIS_SELECTED, LVIS_SELECTED);
			selList[i] = 1;
		}
	}
	else if (upper_border == upper_marker+1 && lower_border == lower_marker-1)
	{
		// 3 - selected all between Markers, nut including Markers: select all between Markers, not including upper Marker
		if (lower_marker >= movie_size) lower_marker = movie_size - 1;
		for (int i = upper_marker+1; i <= lower_marker; ++i)
		{
			//ListView_SetItemState(pianoRoll.hwndList, i, LVIS_SELECTED, LVIS_SELECTED);
			selList[i] = 1;
		}
	}
	else if (upper_border == upper_marker+1 && lower_border == lower_marker)
	{
		// 4 - selected all between Markers and lower Marker selected too: select all bertween Markers, including Markers
		if (upper_marker < 0) upper_marker = 0;
		if (lower_marker >= movie_size) lower_marker = movie_size - 1;
		for (int i = upper_marker; i <= lower_marker; ++i)
		{
			//ListView_SetItemState(pianoRoll.hwndList, i, LVIS_SELECTED, LVIS_SELECTED);
			selList[i] = 1;
		}
	}
	else
	{
		// return to 1
		if (upper_marker < 0) upper_marker = 0;
		for (int i = upper_marker; i < lower_marker; ++i)
		{
			//ListView_SetItemState(pianoRoll.hwndList, i, LVIS_SELECTED, LVIS_SELECTED);
			selList[i] = 1;
		}
	}
}
void SELECTION::reselectClipboard()
{
	RowsSelection clipboard_selection = splicer->getClipboardSelection();
	if (clipboard_selection.size() == 0) return;

	clearAllRowsSelection();
	getCurrentRowsSelection() = clipboard_selection;
	enforceRowsSelectionToList();
	// also keep Selection within Piano Roll
	updateSelectionSize();
}

void SELECTION::transposeVertically(int shift)
{
	if (!shift) return;
	RowsSelection* current_selection = getCopyOfCurrentRowsSelection();
	if (current_selection->size())
	{
		clearAllRowsSelection();
		int pos;
		if (shift > 0)
		{
			int movie_size = currMovieData.getNumRecords();
			RowsSelection::reverse_iterator current_selection_rend(current_selection->rend());
			for(RowsSelection::reverse_iterator it(current_selection->rbegin()); it != current_selection_rend; it++)
			{
				pos = (*it) + shift;
				if (pos < movie_size)
				{
					//ListView_SetItemState(pianoRoll.hwndList, pos, LVIS_SELECTED, LVIS_SELECTED);
					selList[pos] = 1;
					noteThatItemChanged(pos, 1);
				}
			}
		}
		else
		{
			RowsSelection::iterator current_selection_end(current_selection->end());
			for(RowsSelection::iterator it(current_selection->begin()); it != current_selection_end; it++)
			{
				pos = (*it) + shift;
				if (pos >= 0)
				{
					//ListView_SetItemState(pianoRoll.hwndList, pos, LVIS_SELECTED, LVIS_SELECTED);
					selList[pos] = 1;
					noteThatItemChanged(pos, 1);
				}
			}
		}
	}
}

void SELECTION::enforceRowsSelectionToList()
{
	trackSelectionChanges = false;
	clearAllRowsSelection();
	for(RowsSelection::reverse_iterator it(getCurrentRowsSelection().rbegin()); it != getCurrentRowsSelection().rend(); it++)
	{
		//ListView_SetItemState(pianoRoll.hwndList, *it, LVIS_SELECTED, LVIS_SELECTED);
		selList[*it] = 1;
	}
	trackSelectionChanges = true;
}

// getters
int SELECTION::getCurrentRowsSelectionSize()
{
	return rowsSelectionHistory[(historyStartPos + historyCursorPos) % historySize].size();
}
int SELECTION::getCurrentRowsSelectionBeginning()
{
	if (rowsSelectionHistory[(historyStartPos + historyCursorPos) % historySize].size())
		return *rowsSelectionHistory[(historyStartPos + historyCursorPos) % historySize].begin();
	else
		return -1;
}
int SELECTION::getCurrentRowsSelectionEnd()
{
	if (rowsSelectionHistory[(historyStartPos + historyCursorPos) % historySize].size())
		return *rowsSelectionHistory[(historyStartPos + historyCursorPos) % historySize].rbegin();
	else
		return -1;
}
RowsSelection* SELECTION::getCopyOfCurrentRowsSelection()
{
	// copy current Selection to temp_selection
	tempRowsSelection = rowsSelectionHistory[(historyStartPos + historyCursorPos) % historySize];
	return &tempRowsSelection;
}

// this getter is private
RowsSelection& SELECTION::getCurrentRowsSelection()
{
	return rowsSelectionHistory[(historyStartPos + historyCursorPos) % historySize];
}
// -------------------------------------------------------------------------
LowerMarkerNoteEdit::LowerMarkerNoteEdit( QWidget *parent )
	: QLineEdit(parent)
{
	setEchoMode( QLineEdit::Normal );
}
// -------------------------------------------------------------------------
LowerMarkerNoteEdit::~LowerMarkerNoteEdit(void)
{
}
// -------------------------------------------------------------------------
void LowerMarkerNoteEdit::focusInEvent(QFocusEvent *event)
{
	markersManager->markerNoteEditMode = MARKER_NOTE_EDIT_LOWER;
	QLineEdit::focusInEvent(event);
}
// -------------------------------------------------------------------------
void LowerMarkerNoteEdit::focusOutEvent(QFocusEvent *event)
{
	if (markersManager->markerNoteEditMode == MARKER_NOTE_EDIT_LOWER)
	{
		markersManager->updateEditedMarkerNote();
		markersManager->markerNoteEditMode = MARKER_NOTE_EDIT_NONE;
	}
	QLineEdit::focusOutEvent(event);
}
// -------------------------------------------------------------------------
void LowerMarkerNoteEdit::keyPressEvent(QKeyEvent *event)
{
	//printf("Key Press: 0x%x \n", event->key() );

	if ( event->key() == Qt::Key_Escape)
	{
		setText( QString::fromStdString(markersManager->getNoteCopy(selection->displayedMarkerNumber)) );
		event->accept();
	}
	else if ( (event->key() == Qt::Key_Enter) || (event->key() == Qt::Key_Return) )
	{
		if (markersManager->markerNoteEditMode == MARKER_NOTE_EDIT_LOWER)
		{
			markersManager->updateEditedMarkerNote();
			markersManager->markerNoteEditMode = MARKER_NOTE_EDIT_NONE;
		}
		tasWin->pianoRoll->setFocus();
		event->accept();
	}
	else
	{
		QLineEdit::keyPressEvent(event);
	}
}
// -------------------------------------------------------------------------
void LowerMarkerNoteEdit::mousePressEvent(QMouseEvent * event)
{

	if ( event->button() == Qt::MiddleButton )
	{
		playback->handleMiddleButtonClick();
	}
	else if ( (event->button() == Qt::LeftButton) || (event->button() == Qt::RightButton) )
	{
		// scroll to the Marker
		if (taseditorConfig->followMarkerNoteContext)
		{
			tasWin->pianoRoll->followMarker(selection->displayedMarkerNumber);
		}
	}
}
// -------------------------------------------------------------------------
