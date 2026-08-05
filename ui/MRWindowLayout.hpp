#ifndef MRWINDOWLAYOUT_HPP
#define MRWINDOWLAYOUT_HPP

#define Uses_TEvent
#define Uses_TRect
#define Uses_TPoint
#include <tvision/tv.h>

#include <string>

class MREditWindow;
class MRDesktopWindow;

class MRWindowLayout {
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

	static void handleDragView(MRDesktopWindow *window, TEvent &event, uchar mode, TRect &limits, TPoint minSize, TPoint maxSize);
	[[nodiscard]] static bool isWindowMinimized(const MRDesktopWindow *window) noexcept;
	[[nodiscard]] static TRect usableDesktopBounds() noexcept;
	[[nodiscard]] static TRect minimizedBoundsForWorkspace(const MREditWindow *window) noexcept;
	[[nodiscard]] static TRect restoreBoundsForWorkspace(const MREditWindow *window) noexcept;
	[[nodiscard]] static const MinimizedGlyphs &minimizedGlyphs() noexcept;
	[[nodiscard]] static MinimizedLayout minimizedLayout(const MRDesktopWindow *window, int width) noexcept;
	[[nodiscard]] static std::string minimizedDisplayTitle(const MRDesktopWindow *window);
	[[nodiscard]] static int minimizedDisplayTitleWidth(const MRDesktopWindow *window) noexcept;
	[[nodiscard]] static int minimizedWindowWidth(const MRDesktopWindow *window) noexcept;
	[[nodiscard]] static bool isMinimizedRestoreGlyphHit(const MRDesktopWindow *window, TPoint local) noexcept;
	[[nodiscard]] static bool isMinimizedReinsertGlyphHit(const MRDesktopWindow *window, TPoint local) noexcept;
	static void minimizeWindow(MRDesktopWindow *window);
	static void reinsertMinimizedWindow(MRDesktopWindow *window);
	static void restoreWindow(MRDesktopWindow *window);
	static void toggleMinimizedWindow(MRDesktopWindow *window);
	static void applyWorkspaceState(MREditWindow *window, const TRect &bounds, const TRect &restoreBounds, bool minimized, bool notifyTopology = true, bool projectNow = true);
	static void applyBatchWindowBounds(MRDesktopWindow *window, const TRect &bounds);
	static void refreshDesktopProjection();
	static void handleDesktopLayoutChange();
};

#endif
