#ifndef TMREDITWINDOW_HPP
#define TMREDITWINDOW_HPP

#define Uses_TWindow
#define Uses_TScrollBar
#define Uses_TIndicator
#define Uses_TFileEditor
#define Uses_TRect
#define Uses_TEvent
#define Uses_TEditor
#include <tvision/tv.h>

#include <cstring>

#include "TMRFrame.hpp"

class TMREditWindow : public TWindow
{
public:
    TMREditWindow(const TRect &bounds, const char *title, int aNumber)
         : TWindowInit(&TMREditWindow::initFrame),
            TWindow(bounds, 0, aNumber),
            vScrollBar(nullptr),
            hScrollBar(nullptr),
            indicator(nullptr),
            editor(nullptr)
    {
        options |= ofTileable;

        std::strncpy(displayTitle, (title != nullptr && *title != '\0') ? title : "Untitled",
                     sizeof(displayTitle) - 1);
        displayTitle[sizeof(displayTitle) - 1] = '\0';

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

        editor = new TFileEditor(r, hScrollBar, vScrollBar, indicator, "");
        insert(editor);
    }

    virtual TPalette &getPalette() const override
    {
        return TWindow::getPalette();
    }

    virtual const char *getTitle(short) override
    {
        if (editor != nullptr && editor->fileName[0] != EOS)
            return editor->fileName;
        return displayTitle;
    }

    virtual void handleEvent(TEvent &event) override
    {
        TWindow::handleEvent(event);
        if (event.what == evBroadcast && event.message.command == cmUpdateTitle)
        {
            if (frame != nullptr)
                frame->drawView();
            clearEvent(event);
        }
    }

    bool loadFromFile(const char *fileName)
    {
        if (editor == nullptr || fileName == nullptr || *fileName == '\0')
            return false;

        strnzcpy(editor->fileName, fileName, sizeof(editor->fileName));
        fexpand(editor->fileName);
        if (!editor->loadFile())
            return false;

        updateTitleFromEditor();
        return true;
    }

    bool saveCurrentFile()
    {
        if (editor == nullptr)
            return false;

        bool ok = editor->save() == True;
        if (ok)
            updateTitleFromEditor();
        return ok;
    }

    const char *currentFileName() const
    {
        if (editor != nullptr && editor->fileName[0] != EOS)
            return editor->fileName;
        return "";
    }

    TFileEditor *getEditor() const
    {
        return editor;
    }

private:
    static TFrame *initFrame(TRect r)
    {
        return new TMRFrame(r);
    }

    void updateTitleFromEditor()
    {
        if (editor != nullptr && editor->fileName[0] != EOS)
        {
            std::strncpy(displayTitle, editor->fileName, sizeof(displayTitle) - 1);
            displayTitle[sizeof(displayTitle) - 1] = '\0';
        }
        message(owner, evBroadcast, cmUpdateTitle, 0);
    }

    TScrollBar *vScrollBar;
    TScrollBar *hScrollBar;
    TIndicator *indicator;
    TFileEditor *editor;
    char displayTitle[MAXPATH];
};

#endif
