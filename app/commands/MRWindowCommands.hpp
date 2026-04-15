#ifndef MRWINDOWCOMMANDS_HPP
#define MRWINDOWCOMMANDS_HPP

#include <vector>

#define Uses_TEvent
#define Uses_TRect
#define Uses_TPoint
#include <tvision/tv.h>

class TMREditWindow;

class MRWindowManager {
  public:
    static MRWindowManager& instance();

    void dragWindow(TMREditWindow* window, TEvent& event, uchar mode);

  private:
    MRWindowManager() = default;
    ~MRWindowManager() = default;

    MRWindowManager(const MRWindowManager&) = delete;
    MRWindowManager& operator=(const MRWindowManager&) = delete;

    void snapToEdges(TMREditWindow* window, TRect limits, const TPoint& mousePos, TPoint minSize, TPoint maxSize, TRect& outBounds, bool& isSnapped);
    void animateBoundsChange(TMREditWindow* window, const TRect& targetBounds);
};

[[nodiscard]] TMREditWindow *createEditorWindow(const char *title);
[[nodiscard]] std::vector<TMREditWindow *> allEditWindowsInZOrder();
[[nodiscard]] TMREditWindow *currentEditWindow();
[[nodiscard]] TMREditWindow *findEditWindowByBufferId(int bufferId);
[[nodiscard]] bool isEmptyUntitledEditableWindow(TMREditWindow *win);
[[nodiscard]] TMREditWindow *findReusableEmptyWindow(TMREditWindow *preferred);
[[nodiscard]] bool closeCurrentEditWindow();
[[nodiscard]] bool activateRelativeEditWindow(int delta);
[[nodiscard]] bool hideCurrentEditWindow();

#endif
