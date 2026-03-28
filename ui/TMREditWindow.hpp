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
#include <string>

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
            editor(nullptr),
            bufferId_(allocateBufferId()),
            firstSaveDone_(false),
            temporaryFileUsed_(false),
            temporaryFileName_()
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

        temporaryFileUsed_ = false;
        temporaryFileName_.clear();
        updateTitleFromEditor();
        return true;
    }

    bool saveCurrentFile()
    {
        if (editor == nullptr)
            return false;

        bool ok = editor->save() == True;
        if (ok)
        {
            firstSaveDone_ = true;
            temporaryFileUsed_ = false;
            temporaryFileName_.clear();
            updateTitleFromEditor();
        }
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

    bool hasBeenSavedInSession() const
    {
        return firstSaveDone_;
    }

    bool eofInMemory() const
    {
        return editor != nullptr;
    }

    int bufferId() const
    {
        return bufferId_;
    }

    bool isTemporaryFile() const
    {
        return temporaryFileUsed_;
    }

    const char *temporaryFileName() const
    {
        return temporaryFileName_.c_str();
    }

    bool isFileChanged() const
    {
        return editor != nullptr && editor->modified == True;
    }

    void setFileChanged(bool changed)
    {
        if (editor != nullptr)
        {
            editor->modified = changed ? True : False;
            editor->update(ufUpdate);
        }
    }

    void setCurrentFileName(const char *fileName)
    {
        if (editor == nullptr)
            return;

        if (fileName == nullptr || *fileName == '\0')
            editor->fileName[0] = EOS;
        else
        {
            strnzcpy(editor->fileName, fileName, sizeof(editor->fileName));
            fexpand(editor->fileName);
        }
        updateTitleFromEditor();
    }

private:
    static int allocateBufferId()
    {
        static int nextId = 1;
        return nextId++;
    }

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
    int bufferId_;
    bool firstSaveDone_;
    bool temporaryFileUsed_;
    std::string temporaryFileName_;
    char displayTitle[MAXPATH];
};

#endif
