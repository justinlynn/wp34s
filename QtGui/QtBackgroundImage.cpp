/*
 * QtBackgroundImage.cpp
 *
 *  Created on: 20 nov. 2011
 *      Author: pascal
 */

#include "QtBackgroundImage.h"
#include "QtEmulator.h"

static QPoint MOVE_MARGIN(MOVE_MARGIN_X, MOVE_MARGIN_Y);
static QPoint MOVE_OTHER_MARGIN(MOVE_MARGIN_X, -MOVE_MARGIN_Y);

QtBackgroundImage::QtBackgroundImage(const QtSkin& aSkin, QtScreen& aScreen, QtKeyboard& aKeyboard)
	: screen(aScreen), keyboard(aKeyboard), dragging(false)
{
	setSkin(aSkin);
	setPixmap(pixmap);
	setMask(pixmap.mask());
	setFocusPolicy(Qt::StrongFocus);
	setFixedSize(pixmap.size());
	connect(&aKeyboard, SIGNAL(keyPressed()), this, SLOT(updateScreen()));
}

void QtBackgroundImage::setSkin(const QtSkin& aSkin)
{
	if(!pixmap.load(QString(IMAGE_FILE_TYPE)+':'+aSkin.getPictureName()))
	{
		throw *(new QtSkinException(QString("Cannot find picture ")+aSkin.getPictureName()));
	}
	setPixmap(pixmap);
	setMask(pixmap.mask());
	setFixedSize(pixmap.size());
}

QPixmap& QtBackgroundImage::getBackgroundPixmap()
{
	return pixmap;
}

void QtBackgroundImage::keyPressEvent(QKeyEvent* aKeyEvent)
{
	keyboard.processKeyPressedEvent(*aKeyEvent);
}

void QtBackgroundImage::keyReleaseEvent(QKeyEvent* aKeyEvent)
{
	keyboard.processKeyReleasedEvent(*aKeyEvent);
}

void QtBackgroundImage::mousePressEvent(QMouseEvent* aMouseEvent)
{
	if(!keyboard.processButtonPressedEvent(*aMouseEvent) && aMouseEvent->button()==Qt::LeftButton)
	{
		dragging=true;
		lastDragPosition=aMouseEvent->globalPos();
	}
}

void QtBackgroundImage::mouseReleaseEvent(QMouseEvent* aMouseEvent)
{
	if(!dragging)
	{
		keyboard.processButtonReleasedEvent(*aMouseEvent);
	}
	dragging=false;
}

void QtBackgroundImage::mouseMoveEvent(QMouseEvent* aMouseEvent)
{
	if(!dragging)
	{
		keyboard.processMouseMovedEvent(*aMouseEvent);
	}
	else
	{
		moveWindow(aMouseEvent);
	}
}

void QtBackgroundImage::moveWindow(QMouseEvent* aMouseEvent)
{
	QWidget* parentWindow=window();
	QPoint offset=aMouseEvent->globalPos()-lastDragPosition;
	QPoint topLeft=parentWindow->pos()+parentWindow->rect().topLeft()+MOVE_MARGIN+offset;
	QPoint topRight=parentWindow->pos()+parentWindow->rect().topRight()-MOVE_OTHER_MARGIN+offset;
	QPoint bottomLeft=parentWindow->pos()+parentWindow->rect().bottomLeft()+MOVE_OTHER_MARGIN+offset;
	QPoint bottomRight=parentWindow->pos()+parentWindow->rect().bottomRight()-MOVE_MARGIN+offset;
	QDesktopWidget* desktopWidget=QApplication::desktop();
	if(desktopWidget->availableGeometry(topLeft).contains(topLeft)
			|| desktopWidget->availableGeometry(topRight).contains(topRight)
			|| desktopWidget->availableGeometry(bottomLeft).contains(bottomLeft)
			|| desktopWidget->availableGeometry(bottomRight).contains(bottomRight))
	{
		parentWindow->move(parentWindow->pos()+offset);
	}
	lastDragPosition=aMouseEvent->globalPos();
}

void QtBackgroundImage::mouseDoubleClickEvent(QMouseEvent* aMouseEvent)
{
	keyboard.processDoubleClickEvent(*aMouseEvent);
}

void QtBackgroundImage::paintEvent(QPaintEvent* aPaintEvent)
{
	QLabel::paintEvent(aPaintEvent);
	screen.paint(*this, *aPaintEvent);
	keyboard.paint(*this, *aPaintEvent);
}

void QtBackgroundImage::updateScreen()
{
	update(screen.getScreenRectangle());
}
