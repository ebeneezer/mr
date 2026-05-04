#ifndef MRWINDOWMANAGER_HPP
#define MRWINDOWMANAGER_HPP

#define Uses_TEvent
#define Uses_TRect
#define Uses_TPoint
#include <tvision/tv.h>

class MREditWindow;

class MRWindowManager {
  public:
	struct MinimizedGlyphs {
		const char *menu;
		const char *restore;
		const char *reinsert;
	};

	struct MinimizedLayout {
		int menuStart;
		int menuEnd;
		int titleStart;
		int titleEnd;
		int restoreStart;
		int restoreEnd;
		int reinsertStart;
		int reinsertEnd;
	};

	static void handleDragView(MREditWindow *window, TEvent &event, uchar mode, TRect &limits, TPoint minSize, TPoint maxSize);
	[[nodiscard]] static bool isWindowMinimized(const MREditWindow *window) noexcept;
	[[nodiscard]] static int minimizedDesktopRows() noexcept;
	[[nodiscard]] static TRect usableDesktopBounds() noexcept;
	[[nodiscard]] static TRect minimizedBoundsForWorkspace(const MREditWindow *window) noexcept;
	[[nodiscard]] static TRect restoreBoundsForWorkspace(const MREditWindow *window) noexcept;
	[[nodiscard]] static const MinimizedGlyphs &minimizedGlyphs() noexcept;
	[[nodiscard]] static MinimizedLayout minimizedLayout(const MREditWindow *window, int width) noexcept;
	[[nodiscard]] static const char *minimizedDisplayTitle(const MREditWindow *window) noexcept;
	[[nodiscard]] static int minimizedDisplayTitleWidth(const MREditWindow *window) noexcept;
	[[nodiscard]] static int minimizedWindowWidth(const MREditWindow *window) noexcept;
	[[nodiscard]] static bool isMinimizedRestoreGlyphHit(const MREditWindow *window, TPoint local) noexcept;
	[[nodiscard]] static bool isMinimizedReinsertGlyphHit(const MREditWindow *window, TPoint local) noexcept;
	static void minimizeWindow(MREditWindow *window);
	static void reinsertMinimizedWindow(MREditWindow *window);
	static void restoreWindow(MREditWindow *window);
	static void toggleMinimizedWindow(MREditWindow *window);
	static void applyWorkspaceState(MREditWindow *window, const TRect &bounds, const TRect &restoreBounds, bool minimized);
	static void handleDesktopLayoutChange();
};

#endif
