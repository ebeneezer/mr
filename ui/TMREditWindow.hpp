#ifndef TMREDITWINDOW_HPP
#define TMREDITWINDOW_HPP

#define Uses_TWindow
#define Uses_TScrollBar
#define Uses_TIndicator
#define Uses_TEditor
#define Uses_TRect
#include <tvision/tv.h>

#include "TMRFrame.hpp"

class TMREditWindow : public TWindow
{
public:
	TMREditWindow(const TRect &bounds, const char *fileName, int aNumber)
		 : TWindowInit(&TMREditWindow::initFrame),
			TWindow(bounds, fileName, aNumber),
			vScrollBar(nullptr),
			hScrollBar(nullptr),
			indicator(nullptr),
			editor(nullptr)
	{
		options |= ofTileable;

		hScrollBar = new TScrollBar(TRect(18, size.y - 1, size.x - 2, size.y));
		hScrollBar->hide();
		insert(hScrollBar);

		vScrollBar = new TScrollBar(TRect(size.x - 1, 1, size.x, size.y - 1));
		vScrollBar->hide();
		insert(vScrollBar);

		indicator = new TIndicator(TRect(2, size.y - 1, 16, size.y));
		indicator->hide();
		insert(indicator);

		TRect r(getExtent());
		r.grow(-1, -1);

		editor = new TEditor(r, hScrollBar, vScrollBar, indicator, 64 * 1024);
		insert(editor);
	}

	virtual TPalette &getPalette() const override
	{
		return TWindow::getPalette();
	}

private:
	static TFrame *initFrame(TRect r)
	{
		return new TMRFrame(r);
	}

	TScrollBar *vScrollBar;
	TScrollBar *hScrollBar;
	TIndicator *indicator;
	TEditor *editor;
};

#endif