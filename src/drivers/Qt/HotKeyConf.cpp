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
// HotKeyConf.cpp
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include <SDL.h>
#include <QHeaderView>
#include <QCloseEvent>
#include <QMessageBox>

#include "Qt/main.h"
#include "Qt/dface.h"
#include "Qt/input.h"
#include "Qt/config.h"
#include "Qt/keyscan.h"
#include "Qt/fceuWrapper.h"
#include "Qt/HotKeyConf.h"

//----------------------------------------------------------------------------
HotKeyConfDialog_t::HotKeyConfDialog_t(QWidget *parent)
	: QDialog(parent)
{
	QVBoxLayout *mainLayout;
	QHBoxLayout *hbox;
	QPushButton *closeButton, *resetDefaults;
	QTreeWidgetItem *item;
	std::string prefix = "SDL.Hotkeys.";
	const char *hkName, *hkKeySeq, *hkTitle, *hkGroup;

	setWindowTitle("Hotkey Configuration");

	resize(512, 512);

	mainLayout = new QVBoxLayout();

	tree = new HotKeyConfTree_t(this);

	tree->setColumnCount(2);
	tree->setSelectionMode( QAbstractItemView::SingleSelection );
	tree->setAlternatingRowColors(true);

	item = new QTreeWidgetItem();
	item->setText(0, QString::fromStdString("Command"));
	item->setText(1, QString::fromStdString("Key"));
	item->setTextAlignment(0, Qt::AlignLeft);
	item->setTextAlignment(1, Qt::AlignCenter);

	tree->setHeaderItem(item);

	tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

	for (int i = 0; i < HK_MAX; i++)
	{
		getHotKeyConfig( i, &hkName, &hkKeySeq, &hkTitle, &hkGroup );

		tree->addGroup( hkGroup );

		tree->addItem( hkGroup, i );
	}
	tree->finalize();

	//connect( tree, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(hotKeyDoubleClicked(QTreeWidgetItem*,int) ) );
	connect( tree, SIGNAL(itemActivated(QTreeWidgetItem*,int)), this, SLOT(hotKeyActivated(QTreeWidgetItem*,int) ) );

	mainLayout->addWidget(tree);

	resetDefaults = new QPushButton( tr("Restore Defaults") );
	resetDefaults->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
	connect(resetDefaults, SIGNAL(clicked(void)), this, SLOT(resetDefaultsCB(void)));

	closeButton = new QPushButton( tr("Close") );
	closeButton->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
	connect(closeButton, SIGNAL(clicked(void)), this, SLOT(closeWindow(void)));

	hbox = new QHBoxLayout();
	hbox->addWidget( resetDefaults, 1 );
	hbox->addStretch(5);
	hbox->addWidget( closeButton, 1 );
	mainLayout->addLayout( hbox );

	setLayout(mainLayout);
}
//----------------------------------------------------------------------------
HotKeyConfDialog_t::~HotKeyConfDialog_t(void)
{
	//printf("Destroy Hot Key Config Window\n");
}
//----------------------------------------------------------------------------
void HotKeyConfDialog_t::closeEvent(QCloseEvent *event)
{
	//printf("Hot Key Close Window Event\n");
	done(0);
	deleteLater();
	event->accept();
}
//----------------------------------------------------------------------------
void HotKeyConfDialog_t::closeWindow(void)
{
	//printf("Close Window\n");
	done(0);
	deleteLater();
}
//----------------------------------------------------------------------------
void HotKeyConfTree_t::finalize(void)
{
	QTreeWidgetItem *item;
	std::map <std::string, QTreeWidgetItem*>::iterator it;

	for (it=groupMap.begin(); it!=groupMap.end(); it++)
	{
		item = it->second;

		addTopLevelItem(item);

		item->setExpanded(true);
	}
}
//----------------------------------------------------------------------------
QTreeWidgetItem *HotKeyConfTree_t::addItem( const char *group, int hkIdx )
{
	QTreeWidgetItem *itemGroup;
	HotKeyConfTreeItem_t *item;
	const char *hkName, *hkKeySeq, *hkTitle, *hkGroup;

	itemGroup = findGroup(group);

	if ( itemGroup == NULL )
	{
		return NULL;
	}
	getHotKeyConfig( hkIdx, &hkName, &hkKeySeq, &hkTitle, &hkGroup );

	item = new HotKeyConfTreeItem_t(hkIdx);

	item->setText(0, tr(hkTitle));
	item->setText(1, Hotkeys[hkIdx].getKeySeq().toString());

	item->setTextAlignment(0, Qt::AlignLeft);
	item->setTextAlignment(1, Qt::AlignCenter);

	itemGroup->addChild(item);
	return NULL;
}
//----------------------------------------------------------------------------
QTreeWidgetItem *HotKeyConfTree_t::addGroup( const char *group)
{
	std::string s = group;

	return addGroup(s);
}
//----------------------------------------------------------------------------
QTreeWidgetItem *HotKeyConfTree_t::addGroup(std::string group)
{
	QTreeWidgetItem *item;

	item = findGroup(group);

	if ( item == NULL )
	{
		item = new QTreeWidgetItem( this, 0);

		item->setText(0, QString::fromStdString(group));

		groupMap[group] = item;
	}
	return item;
}
//----------------------------------------------------------------------------
QTreeWidgetItem *HotKeyConfTree_t::findGroup( const char *group)
{
	std::string s = group;

	return findGroup(s);
}
//----------------------------------------------------------------------------
QTreeWidgetItem *HotKeyConfTree_t::findGroup( std::string group)
{
	std::map <std::string, QTreeWidgetItem*>::iterator it;

	it = groupMap.find(group);

	if ( it != groupMap.end() )
	{
		return it->second;
	}
	return NULL;
}
//----------------------------------------------------------------------------
HotKeyConfTreeItem_t::HotKeyConfTreeItem_t(int idx, QTreeWidgetItem *parent)
	: QTreeWidgetItem(parent,1)
{
	hkIdx = idx;
}
//----------------------------------------------------------------------------
HotKeyConfTreeItem_t::~HotKeyConfTreeItem_t(void)
{

}
//----------------------------------------------------------------------------
void HotKeyConfDialog_t::resetDefaultsCB(void)
{
	QTreeWidgetItem *groupItem;
	HotKeyConfTreeItem_t *item;
	std::string confName;
	std::string prefix = "SDL.Hotkeys.";
	const char *name, *keySeq;

	for (int i=0; i<tree->topLevelItemCount(); i++)
	{
		groupItem = tree->topLevelItem(i);

		for (int j=0; j<groupItem->childCount(); j++)
		{
			item = static_cast<HotKeyConfTreeItem_t*>(groupItem->child(j));

			getHotKeyConfig( item->hkIdx, &name, &keySeq );

			confName = prefix + name;

			g_config->setOption( confName, keySeq );

			item->setText(1, tr(keySeq));
		}
	}
	setHotKeys();

}
//----------------------------------------------------------------------------
void HotKeyConfDialog_t::keyPressEvent(QKeyEvent *event)
{
	//printf("Hotkey Window Key Press: 0x%x \n", event->key() );
	event->accept();
}
//----------------------------------------------------------------------------
void HotKeyConfDialog_t::keyReleaseEvent(QKeyEvent *event)
{
	//printf("Hotkey Window Key Release: 0x%x \n", event->key() );
	event->accept();
}
//----------------------------------------------------------------------------
void HotKeyConfDialog_t::hotKeyActivated(QTreeWidgetItem *item, int column)
{
	HotKeyConfTreeItem_t *hkItem = static_cast<HotKeyConfTreeItem_t*>(item);

	if ( hkItem->type() == 0 )
	{
		return;
	}
	HotKeyConfSetDialog_t *win = new HotKeyConfSetDialog_t( 1, hkItem, this );

	win->exec();
}
//----------------------------------------------------------------------------
void HotKeyConfDialog_t::hotKeyDoubleClicked(QTreeWidgetItem *item, int column)
{
	HotKeyConfTreeItem_t *hkItem = static_cast<HotKeyConfTreeItem_t*>(item);

	if ( hkItem->type() == 0 )
	{
		return;
	}
	HotKeyConfSetDialog_t *win = new HotKeyConfSetDialog_t( 0, hkItem, this );

	win->exec();
}
//----------------------------------------------------------------------------
HotKeyConfTree_t::HotKeyConfTree_t(QWidget *parent)
	: QTreeWidget(parent)
{

}
//----------------------------------------------------------------------------
HotKeyConfTree_t::~HotKeyConfTree_t(void)
{

}
//----------------------------------------------------------------------------
void HotKeyConfTree_t::keyPressEvent(QKeyEvent *event)
{
	QTreeWidget::keyPressEvent(event);
}
//----------------------------------------------------------------------------
void HotKeyConfTree_t::keyReleaseEvent(QKeyEvent *event)
{
	QTreeWidget::keyReleaseEvent(event);
}
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
HotKeyConfSetDialog_t::HotKeyConfSetDialog_t( int discardNum, HotKeyConfTreeItem_t *itemIn, QWidget *parent )
	: QDialog(parent)
{
	QVBoxLayout *mainLayout;
	QHBoxLayout *hbox;
	QPushButton *clearButton, *okButton;

	item = itemIn;
	discardCount = discardNum;

	setWindowTitle("Set Hot Key");

	mainLayout = new QVBoxLayout();
	hbox       = new QHBoxLayout();

	keySeqText = new QLineEdit();
	keySeqText->setReadOnly(true);
	keySeqText->setText( tr("Press a Key") );

	mainLayout->addWidget(keySeqText);

	mainLayout->addLayout( hbox );

	clearButton = new QPushButton( tr("Clear") );
	   okButton = new QPushButton( tr("Ok") );

	clearButton->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
	   okButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));

	hbox->addWidget( clearButton );
	hbox->addWidget( okButton    );

	setLayout( mainLayout );

	connect( clearButton, SIGNAL(clicked(void)), this, SLOT(cleanButtonCB(void)) );
	connect(    okButton, SIGNAL(clicked(void)), this, SLOT(   okButtonCB(void)) );
}
//----------------------------------------------------------------------------
HotKeyConfSetDialog_t::~HotKeyConfSetDialog_t(void)
{

}
//----------------------------------------------------------------------------
void HotKeyConfSetDialog_t::closeEvent(QCloseEvent *event)
{
	//printf("Hot Key Close Window Event\n");
	done(0);
	deleteLater();
	event->accept();
}
//----------------------------------------------------------------------------
void HotKeyConfSetDialog_t::closeWindow(void)
{
	//printf("Close Window\n");
	done(0);
	deleteLater();
}
//----------------------------------------------------------------------------
void HotKeyConfSetDialog_t::keyPressEvent(QKeyEvent *event)
{
	//printf("Hotkey Window Key Press: 0x%x \n", event->key() );
	if ( discardCount == 0 )
	{
		assignHotkey(event);
	}
	else if ( discardCount > 0 )
	{
		discardCount--;
	}
	event->accept();
}
//----------------------------------------------------------------------------
void HotKeyConfSetDialog_t::keyReleaseEvent(QKeyEvent *event)
{
	if ( discardCount == 0 )
	{
		assignHotkey(event);
	}
	else if ( discardCount > 0 )
	{
		discardCount--;
	}
	//printf("Hotkey Window Key Release: 0x%x \n", event->key() );
	//assignHotkey(event);
	event->accept();
}
//----------------------------------------------------------------------------
void HotKeyConfSetDialog_t::checkForConflicts(int hkIdx)
{
	for (int i=0; i<HK_MAX; i++)
	{
		if ( hkIdx == i )
		{
			continue;
		}
		if ( Hotkeys[hkIdx].getKeySeq().matches( Hotkeys[i].getKeySeq() ) )
		{
			std::string msg;
			const char *hkName1, *hkKeySeq1, *hkTitle1, *hkGroup1;
			const char *hkName2, *hkKeySeq2, *hkTitle2, *hkGroup2;

			getHotKeyConfig(     i, &hkName1, &hkKeySeq1, &hkTitle1, &hkGroup1 );
			getHotKeyConfig( hkIdx, &hkName2, &hkKeySeq2, &hkTitle2, &hkGroup2 );

			msg.append(hkGroup2);
			msg.append(" :: ");
			msg.append(hkTitle2);
			msg.append("\n\n");
			msg.append("Conflicts with:\n\n");
			msg.append(hkGroup1);
			msg.append(" :: ");
			msg.append(hkTitle1);

			QMessageBox::warning( this, 
						tr("Hotkey Conflict Warning"), tr(msg.c_str()) );
			//printf("Warning Key Conflict: %i and %i \n", hkIdx, i );
		}
	}
}
//----------------------------------------------------------------------------
void HotKeyConfSetDialog_t::assignHotkey(QKeyEvent *event)
{
	bool keyIsModifier;
	//QKeySequence ks( event->modifiers() + event->key() );
	QKeySequence ks( convKeyEvent2Sequence(event) );
	SDL_Keycode k = convQtKey2SDLKeyCode((Qt::Key)event->key());
	//SDL_Keymod m = convQtKey2SDLModifier(event->modifiers());

	keyIsModifier = (k == SDLK_LCTRL) || (k == SDLK_RCTRL) ||
			(k == SDLK_LSHIFT) || (k == SDLK_RSHIFT) ||
			(k == SDLK_LALT) || (k == SDLK_RALT) ||
			(k == SDLK_LGUI) || (k == SDLK_RGUI) ||
			(k == SDLK_CAPSLOCK);

	//printf("Assign: '%s' %i  0x%08x\n", ks.toString().toStdString().c_str(), event->key(), event->key() );

	if ((k != SDLK_UNKNOWN) && !keyIsModifier)
	{
		std::string keyText;
		std::string prefix = "SDL.Hotkeys.";
		std::string confName;

		confName = prefix + Hotkeys[item->hkIdx].getConfigName();

		keyText = ks.toString().toStdString();

		g_config->setOption( confName, keyText);

		setHotKeys();

		checkForConflicts(item->hkIdx);

		if ( item )
		{
			item->setText(1, QString::fromStdString(keyText));
		}

		done(0);
		deleteLater();
	}
}
//----------------------------------------------------------------------------
void HotKeyConfSetDialog_t::cleanButtonCB(void)
{
	std::string prefix = "SDL.Hotkeys.";
	std::string confName;

	confName = prefix + Hotkeys[item->hkIdx].getConfigName();

	g_config->setOption( confName, "");

	setHotKeys();

	if ( item )
	{
		item->setText(1, tr(""));
	}

	done(0);
	deleteLater();
}
//----------------------------------------------------------------------------
void HotKeyConfSetDialog_t::okButtonCB(void)
{
	done(0);
	deleteLater();
}
//----------------------------------------------------------------------------
