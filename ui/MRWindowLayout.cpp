#include "MRWindowLayout.hpp"

#include "MREditWindow.hpp"
#include "MRDesktopWindow.hpp"
#include "MRWindowSupport.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../dialogs/MRWindowList.hpp"

#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TEvent
#define Uses_TProgram
#define Uses_TScreen
#define Uses_TText
#include <tvision/tv.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
#include <string>
#include <vector>

extern TPoint shadowSize;

namespace {

constexpr int kMinimizedGap = 3;
constexpr int kMinimizedHeight = 1;
constexpr int kMinimizedTitleMaxWidth = 20;
constexpr int kMinimizedMinWidth = 4;
constexpr int kMinimizedRightPadding = 0;
constexpr char kMinimizedMenuGlyph[] = "≡";
constexpr char kMinimizedRestoreGlyph[] = "▴";
constexpr char kMinimizedReinsertGlyph[] = "▾";

constexpr char kMinimizedEllipsis[] = "…";
constexpr char kMinimizedFallbackTitle[] = "?No-File";

bool g_layoutDirty = true;
bool g_lastDesktopExtentValid = false;
TRect g_lastDesktopExtent;
std::string g_minimizedTitleBuffer;

void logWindowLayoutTiming(const std::string &label, long long tookUs, const std::string &detail) {
	std::ostringstream line;

	line << label << " took_us=" << tookUs;
	if (!detail.empty()) line << " " << detail;
	mrLogMessage(line.str());
}

TRect fullDesktopBounds() noexcept {
	if (TProgram::deskTop == nullptr) return TRect(0, 0, 1, 1);
	return TProgram::deskTop->getExtent();
}

bool sameDesktopAndVisible(const MRDesktopWindow *window, int virtualDesktop) {
	return window != nullptr && window->desktopIndex() == virtualDesktop && !window->desktopManuallyHidden();
}

std::size_t nextCodeUnitCount(const std::string &text, std::size_t index) noexcept {
	const std::size_t count = TText::next(text, index);
	return count == 0 ? 1 : count;
}

std::string baseNameForMinimizedTitle(const MRDesktopWindow *window) {
	std::string title;
	const char *windowTitle = window != nullptr ? window->desktopMinimizedTitle() : nullptr;

	if (windowTitle != nullptr)
		title = windowTitle;
	else
		title = kMinimizedFallbackTitle;

	const std::size_t sep = title.find_last_of("\\/");
	if (sep == std::string::npos || sep + 1 >= title.size()) return title.empty() ? std::string(kMinimizedFallbackTitle) : title;
	return title.substr(sep + 1);
}

std::string truncateDisplayWidthMiddle(const std::string &text, int maxWidth) {
	const int fullWidth = strwidth(text.c_str());
	const int ellipsisWidth = strwidth(kMinimizedEllipsis);
	std::vector<std::string> glyphs;
	std::vector<int> widths;
	std::string prefix;
	std::string suffix;
	int prefixWidth = 0;
	int suffixWidth = 0;
	std::size_t index = 0;

	if (fullWidth <= maxWidth) return text;
	if (maxWidth <= ellipsisWidth) return std::string(kMinimizedEllipsis);

	while (index < text.size()) {
		const std::size_t count = nextCodeUnitCount(text, index);
		glyphs.push_back(text.substr(index, count));
		widths.push_back(strwidth(glyphs.back().c_str()));
		index += count;
	}

	for (std::size_t left = 0, right = glyphs.empty() ? 0 : glyphs.size() - 1; !glyphs.empty() && left <= right;) {
		if (prefixWidth + suffixWidth + widths[left] + ellipsisWidth <= maxWidth) {
			prefix += glyphs[left];
			prefixWidth += widths[left];
			++left;
		} else
			break;

		if (left > right) break;
		if (prefixWidth + suffixWidth + widths[right] + ellipsisWidth <= maxWidth) {
			suffix.insert(0, glyphs[right]);
			suffixWidth += widths[right];
			if (right == 0) break;
			--right;
		} else
			break;
	}

	return prefix + kMinimizedEllipsis + suffix;
}

std::string minimizedDisplayTitleString(const MRDesktopWindow *window) {
	return truncateDisplayWidthMiddle(baseNameForMinimizedTitle(window), kMinimizedTitleMaxWidth);
}

int minimizedMenuWidth() noexcept {
	return strwidth(kMinimizedMenuGlyph);
}

int minimizedRestoreWidth() noexcept {
	return strwidth(kMinimizedRestoreGlyph);
}

int minimizedReinsertWidth() noexcept {
	return strwidth(kMinimizedReinsertGlyph);
}

int minimizedTitleWidth(const MRDesktopWindow *window) {
	return strwidth(minimizedDisplayTitleString(window).c_str());
}

int minimizedWindowWidthValue(const MRDesktopWindow *window) {
	return std::max(kMinimizedMinWidth, minimizedMenuWidth() + 1 + minimizedTitleWidth(window) + 1 + minimizedRestoreWidth() + minimizedReinsertWidth() + kMinimizedRightPadding);
}

TRect normalizedMinimizedBounds(const MRDesktopWindow *window, const TRect &bounds, const TRect &desktop) {
	const int width = minimizedWindowWidthValue(window);
	const int maxX = std::max(desktop.a.x, desktop.b.x - width);
	const int maxY = std::max(desktop.a.y, desktop.b.y - kMinimizedHeight);
	const int x = std::min(std::max(bounds.a.x, desktop.a.x), maxX);
	const int y = std::min(std::max(bounds.a.y, desktop.a.y), maxY);

	return TRect(x, y, x + width, y + kMinimizedHeight);
}

bool rectsOverlap(const TRect &lhs, const TRect &rhs) noexcept {
	return lhs.a.x < rhs.b.x && rhs.a.x < lhs.b.x && lhs.a.y < rhs.b.y && rhs.a.y < lhs.b.y;
}

bool minimizedBoundsFitDesktop(const MRDesktopWindow *window, const TRect &bounds) {
	const TRect desktop = fullDesktopBounds();
	const TRect normalized = normalizedMinimizedBounds(window, bounds, desktop);

	return bounds.a.x == normalized.a.x && bounds.a.y == normalized.a.y && bounds.b.x == normalized.b.x && bounds.b.y == normalized.b.y;
}

int minimizedRowsForDesktop(int virtualDesktop) {
	const TRect desktop = fullDesktopBounds();
	const int dockY = desktop.b.y - kMinimizedHeight;
	int rows = 0;

	for (MRDesktopWindow *window : allDesktopWindowsInZOrder()) {
		TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
		if (nativeWindow == nullptr || !window->desktopMinimized() || !sameDesktopAndVisible(window, virtualDesktop)) continue;
		if (nativeWindow->getBounds().a.y != dockY) continue;
		rows = std::max(rows, desktop.b.y - nativeWindow->getBounds().a.y);
	}
	return std::min(rows, std::max(1, desktop.b.y - desktop.a.y));
}

TRect usableDesktopBoundsForDesktop(int virtualDesktop) {
	TRect usable = fullDesktopBounds();
	const int rows = minimizedRowsForDesktop(virtualDesktop);

	usable.b.y -= rows;
	if (usable.b.y <= usable.a.y) usable.b.y = usable.a.y + 1;
	return usable;
}

TRect clampToBounds(const TRect &bounds, const TRect &limits) {
	int width = bounds.b.x - bounds.a.x;
	int height = bounds.b.y - bounds.a.y;
	int x = bounds.a.x;
	int y = bounds.a.y;

	width = std::max(1, std::min(width, limits.b.x - limits.a.x));
	height = std::max(1, std::min(height, limits.b.y - limits.a.y));
	x = std::min(std::max(x, limits.a.x), limits.b.x - width);
	y = std::min(std::max(y, limits.a.y), limits.b.y - height);
	return TRect(x, y, x + width, y + height);
}

void placeVisibleWindow(MRDesktopWindow *window, const TRect &bounds) {
	const auto startedAt = std::chrono::steady_clock::now();
	long long locateUs = 0;
	long long frameUs = 0;
	long long syncUs = 0;
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;

	if (nativeWindow == nullptr) return;
	TRect target = bounds;
	if (nativeWindow->getBounds() != target) {
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		nativeWindow->locate(target);
		locateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	} else {
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		nativeWindow->changeBounds(target);
		locateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	if (nativeWindow->frame != nullptr) {
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		nativeWindow->frame->drawView();
		frameUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	if (!window->desktopMinimized()) {
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		window->synchronizeDesktopContents();
		syncUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	{
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
		if (tookUs >= 10000) {
			std::ostringstream detail;

			detail << "locate_us=" << locateUs << " frame_us=" << frameUs << " sync_us=" << syncUs << " min=" << (window->desktopMinimized() ? 1 : 0);
			logWindowLayoutTiming("Window layout place visible slow", tookUs, detail.str());
		}
	}
}

void setHiddenWindowBounds(MRDesktopWindow *window, const TRect &bounds) {
	if (window == nullptr) return;
	window->applyDesktopBounds(bounds);
}

void markLayoutDirty() noexcept {
	g_layoutDirty = true;
}

void reflowMinimizedWindowsForDesktop(int virtualDesktop) {
	const auto startedAt = std::chrono::steady_clock::now();
	const TRect desktop = fullDesktopBounds();
	const int dockY = desktop.b.y - kMinimizedHeight;
	std::vector<MRDesktopWindow *> windows;
	int nextX = desktop.a.x;
	int nextY = dockY;
	long long collectUs = 0;
	long long sortUs = 0;
	long long placeUs = 0;

	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		for (MRDesktopWindow *window : allDesktopWindowsInZOrder()) {
			TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
			if (nativeWindow != nullptr && window->desktopMinimized() && sameDesktopAndVisible(window, virtualDesktop) && nativeWindow->getBounds().a.y == dockY) windows.push_back(window);
		}
		collectUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}

	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		std::sort(windows.begin(), windows.end(), [](const MRDesktopWindow *lhs, const MRDesktopWindow *rhs) {
			const TRect lhsBounds = lhs->desktopNativeWindow()->getBounds();
			const TRect rhsBounds = rhs->desktopNativeWindow()->getBounds();
			if (lhsBounds.a.y != rhsBounds.a.y) return lhsBounds.a.y > rhsBounds.a.y;
			return lhsBounds.a.x < rhsBounds.a.x;
		});
		sortUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}

	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		for (MRDesktopWindow *window : windows) {
			const int width = minimizedWindowWidthValue(window);
			TRect target(nextX, nextY, nextX + width, nextY + kMinimizedHeight);

			if (target.b.x > desktop.b.x) {
				nextX = desktop.a.x;
				nextY = std::max(desktop.a.y, nextY - 1);
				target = TRect(nextX, nextY, nextX + width, nextY + kMinimizedHeight);
			}

			placeVisibleWindow(window, normalizedMinimizedBounds(window, target, desktop));
			nextX = window->desktopNativeWindow()->getBounds().b.x + kMinimizedGap;
		}
		placeUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	{
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
		if (tookUs >= 10000 || windows.size() >= 20) {
			std::ostringstream detail;

			detail << "desktop=" << virtualDesktop << " windows=" << windows.size() << " collect_us=" << collectUs << " sort_us=" << sortUs << " place_us=" << placeUs;
			logWindowLayoutTiming("Window layout reflow minimized timing", tookUs, detail.str());
		}
	}
}

TRect nextMinimizedBounds(MRDesktopWindow *window) {
	const TRect desktop = fullDesktopBounds();
	const int dockY = desktop.b.y - kMinimizedHeight;
	std::vector<MRDesktopWindow *> windows;
	int x = desktop.a.x;
	int y = dockY;
	const int width = minimizedWindowWidthValue(window);

	for (MRDesktopWindow *candidate : allDesktopWindowsInZOrder()) {
		TWindow *nativeWindow = candidate != nullptr ? candidate->desktopNativeWindow() : nullptr;
		if (nativeWindow != nullptr && candidate != window && candidate->desktopMinimized() && sameDesktopAndVisible(candidate, window->desktopIndex()) && nativeWindow->getBounds().a.y == dockY) windows.push_back(candidate);
	}

	std::sort(windows.begin(), windows.end(), [](const MRDesktopWindow *lhs, const MRDesktopWindow *rhs) {
		const TRect lhsBounds = lhs->desktopNativeWindow()->getBounds();
		const TRect rhsBounds = rhs->desktopNativeWindow()->getBounds();
		if (lhsBounds.a.y != rhsBounds.a.y) return lhsBounds.a.y > rhsBounds.a.y;
		return lhsBounds.a.x < rhsBounds.a.x;
	});

	for (MRDesktopWindow *candidate : windows) {
		const TRect candidateBounds = candidate->desktopNativeWindow()->getBounds();
		if (candidateBounds.a.y != y) {
			y = candidateBounds.a.y;
			x = candidateBounds.b.x + kMinimizedGap;
		} else
			x = std::max(x, candidateBounds.b.x + kMinimizedGap);
	}

	if (x + width > desktop.b.x) {
		x = desktop.a.x;
		y = std::max(desktop.a.y, y - 1);
	}

	return normalizedMinimizedBounds(window, TRect(x, y, x + width, y + kMinimizedHeight), desktop);
}

bool minimizedBoundsConflict(MRDesktopWindow *window, const TRect &bounds) {
	if (!minimizedBoundsFitDesktop(window, bounds)) return true;
	for (MRDesktopWindow *candidate : allDesktopWindowsInZOrder()) {
		TWindow *nativeWindow = candidate != nullptr ? candidate->desktopNativeWindow() : nullptr;
		if (nativeWindow == nullptr || candidate == window || !candidate->desktopMinimized() || candidate->desktopIndex() != window->desktopIndex() || candidate->desktopManuallyHidden()) continue;
		if (rectsOverlap(bounds, nativeWindow->getBounds())) return true;
	}
	return false;
}

void clampWindowsToUsableDesktop() {
	const auto startedAt = std::chrono::steady_clock::now();
	std::size_t considered = 0;
	std::size_t placed = 0;

	for (MRDesktopWindow *window : allDesktopWindowsInZOrder()) {
		TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;

		if (nativeWindow == nullptr || window->desktopMinimized() || window->desktopManuallyHidden()) continue;
		++considered;
		window->applyDesktopBounds(clampToBounds(nativeWindow->getBounds(), usableDesktopBoundsForDesktop(window->desktopIndex())));
		++placed;
	}
	{
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
		if (tookUs >= 10000 || placed >= 50) {
			std::ostringstream detail;

			detail << "considered=" << considered << " placed=" << placed;
			logWindowLayoutTiming("Window layout clamp timing", tookUs, detail.str());
		}
	}
}

void refreshDesktop() {
	const auto startedAt = std::chrono::steady_clock::now();
	long long desktopUs = 0;
	long long appUs = 0;

	if (TProgram::deskTop != nullptr) {
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		TProgram::deskTop->redraw();
		TProgram::deskTop->drawView();
		desktopUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	if (TProgram::application != nullptr) {
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		TProgram::application->redraw();
		appUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	{
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
		if (tookUs >= 10000) {
			std::ostringstream detail;

			detail << "desktop_us=" << desktopUs << " app_us=" << appUs;
			logWindowLayoutTiming("Window layout refresh desktop slow", tookUs, detail.str());
		}
	}
}

void updateLayoutAfterStateChange() {
	markLayoutDirty();
	MRWindowLayout::handleDesktopLayoutChange();
	mrNotifyWindowTopologyChanged();
}

} // namespace

void MRWindowLayout::handleDragView(MRDesktopWindow *window, TEvent &event, uchar mode, TRect &limits, TPoint minSize, TPoint maxSize) {
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
	MRDesktopMinimizedState minimizedState;

	if (nativeWindow == nullptr) return;
	window->readDesktopMinimizedState(minimizedState);

	if (minimizedState.minimized && (mode & dmDragMove) != 0 && event.what == evMouseDown) {
		TRect trayLimits = fullDesktopBounds();
		TRect originalBounds = nativeWindow->getBounds();
		TPoint offset = nativeWindow->origin - event.mouse.where;
		const int width = MRWindowLayout::minimizedWindowWidth(window);

		nativeWindow->setState(sfDragging, True);
		do {
			TPoint currentMouse = event.mouse.where;
			TPoint newOrigin = currentMouse + offset;
			TRect targetBounds = originalBounds;

			newOrigin.x = std::min(std::max(newOrigin.x, trayLimits.a.x - width + 1), trayLimits.b.x - 1);
			newOrigin.y = std::min(std::max(newOrigin.y, trayLimits.a.y), trayLimits.b.y - kMinimizedHeight);
			targetBounds.a = newOrigin;
			targetBounds.b.x = newOrigin.x + width;
			targetBounds.b.y = newOrigin.y + kMinimizedHeight;
			if (targetBounds != nativeWindow->getBounds()) nativeWindow->locate(targetBounds);
		} while (nativeWindow->mouseEvent(event, evMouseMove));
		minimizedState.lastMinimizedBounds = nativeWindow->getBounds();
		window->storeDesktopMinimizedState(minimizedState);
		nativeWindow->setState(sfDragging, False);
		updateLayoutAfterStateChange();
		return;
	}

	if (!configuredWindowManager() || (mode & dmDragMove) == 0 || event.what != evMouseDown) {
		nativeWindow->TWindow::dragView(event, mode, limits, minSize, maxSize);
		return;
	}

	TRect dragLimits = minimizedState.minimized ? fullDesktopBounds() : usableDesktopBoundsForDesktop(window->desktopIndex());
	TRect originalBounds = nativeWindow->getBounds();
	TPoint offset = nativeWindow->origin - event.mouse.where;

	nativeWindow->setState(sfDragging, True);

	do {
		TPoint currentMouse = event.mouse.where;
		TPoint mouseLocal = nativeWindow->owner->makeLocal(currentMouse);
		TRect deskExtent = dragLimits;
		bool snapped = false;
		TRect targetBounds = originalBounds;

		if (!minimizedState.minimized) {
			if (mouseLocal.x <= deskExtent.a.x) {
				targetBounds.a.x = deskExtent.a.x;
				targetBounds.a.y = deskExtent.a.y;
				targetBounds.b.x = deskExtent.a.x + (deskExtent.b.x - deskExtent.a.x) / 2;
				targetBounds.b.y = deskExtent.b.y;
				snapped = true;
			} else if (mouseLocal.x >= deskExtent.b.x - 1) {
				targetBounds.a.x = deskExtent.a.x + (deskExtent.b.x - deskExtent.a.x) / 2;
				targetBounds.a.y = deskExtent.a.y;
				targetBounds.b.x = deskExtent.b.x;
				targetBounds.b.y = deskExtent.b.y;
				snapped = true;
			} else if (mouseLocal.y <= deskExtent.a.y) {
				targetBounds.a.x = deskExtent.a.x;
				targetBounds.a.y = deskExtent.a.y;
				targetBounds.b.x = deskExtent.b.x;
				targetBounds.b.y = deskExtent.a.y + (deskExtent.b.y - deskExtent.a.y) / 2;
				snapped = true;
			} else if (mouseLocal.y >= deskExtent.b.y - 1) {
				targetBounds.a.x = deskExtent.a.x;
				targetBounds.a.y = deskExtent.a.y + (deskExtent.b.y - deskExtent.a.y) / 2;
				targetBounds.b.x = deskExtent.b.x;
				targetBounds.b.y = deskExtent.b.y;
				snapped = true;
			}
		}

		if (!snapped) {
			TPoint newOrigin = currentMouse + offset;
			TPoint originalSize;
			originalSize.x = originalBounds.b.x - originalBounds.a.x;
			originalSize.y = originalBounds.b.y - originalBounds.a.y;

			newOrigin.x = std::min(std::max(newOrigin.x, dragLimits.a.x - originalSize.x + 1), dragLimits.b.x - 1);
			newOrigin.y = std::min(std::max(newOrigin.y, dragLimits.a.y - originalSize.y + 1), dragLimits.b.y - 1);

			if ((mode & dmLimitLoX) != 0) newOrigin.x = std::max(newOrigin.x, dragLimits.a.x);
			if ((mode & dmLimitLoY) != 0) newOrigin.y = std::max(newOrigin.y, dragLimits.a.y);
			if ((mode & dmLimitHiX) != 0) newOrigin.x = std::min(newOrigin.x, dragLimits.b.x - originalSize.x);
			if ((mode & dmLimitHiY) != 0) newOrigin.y = std::min(newOrigin.y, dragLimits.b.y - originalSize.y);

			targetBounds.a = newOrigin;
			targetBounds.b.x = newOrigin.x + originalSize.x;
			targetBounds.b.y = newOrigin.y + originalSize.y;
		}

		if (targetBounds != nativeWindow->getBounds()) nativeWindow->locate(targetBounds);
	} while (nativeWindow->mouseEvent(event, evMouseMove));

	nativeWindow->setState(sfDragging, False);
	if (minimizedState.minimized) {
		minimizedState.lastMinimizedBounds = nativeWindow->getBounds();
		window->storeDesktopMinimizedState(minimizedState);
		updateLayoutAfterStateChange();
	}
}

bool MRWindowLayout::isWindowMinimized(const MRDesktopWindow *window) noexcept {
	return window != nullptr && window->desktopMinimized();
}

int MRWindowLayout::minimizedDesktopRows() noexcept {
	return minimizedRowsForDesktop(currentVirtualDesktop());
}

TRect MRWindowLayout::usableDesktopBounds() noexcept {
	return usableDesktopBoundsForDesktop(currentVirtualDesktop());
}

TRect MRWindowLayout::minimizedBoundsForWorkspace(const MREditWindow *window) noexcept {
	MRDesktopMinimizedState minimizedState;

	if (window == nullptr) return TRect(0, 0, 1, 1);
	window->readDesktopMinimizedState(minimizedState);
	if (minimizedState.minimized) return window->getBounds();
	return normalizedMinimizedBounds(window, nextMinimizedBounds(const_cast<MREditWindow *>(window)), fullDesktopBounds());
}

TRect MRWindowLayout::restoreBoundsForWorkspace(const MREditWindow *window) noexcept {
	MRDesktopMinimizedState minimizedState;

	if (window == nullptr) return TRect(0, 0, 1, 1);
	window->readDesktopMinimizedState(minimizedState);
	if (minimizedState.minimized) return minimizedState.restoreBounds;
	return window->getBounds();
}

const MRWindowLayout::MinimizedGlyphs &MRWindowLayout::minimizedGlyphs() noexcept {
	static const MinimizedGlyphs glyphs = {kMinimizedMenuGlyph, kMinimizedRestoreGlyph, kMinimizedReinsertGlyph};
	return glyphs;
}

MRWindowLayout::MinimizedLayout MRWindowLayout::minimizedLayout(const MRDesktopWindow *window, int width) noexcept {
	const int menuWidth = minimizedMenuWidth();
	const int restoreWidth = minimizedRestoreWidth();
	const int reinsertWidth = minimizedReinsertWidth();
	const int titleStart = std::min(width, menuWidth + 1);
	const int reinsertEnd = std::max(0, width - kMinimizedRightPadding);
	const int reinsertStart = std::max(titleStart, reinsertEnd - reinsertWidth);
	const int restoreEnd = reinsertStart;
	const int restoreStart = std::max(titleStart, restoreEnd - restoreWidth);
	const int titleWidth = std::min(minimizedDisplayTitleWidth(window), std::max(0, restoreStart - titleStart - 1));

	return {0, menuWidth, titleStart, titleStart + titleWidth, restoreStart, restoreEnd, reinsertStart, reinsertEnd};
}

const char *MRWindowLayout::minimizedDisplayTitle(const MRDesktopWindow *window) noexcept {
	g_minimizedTitleBuffer = minimizedDisplayTitleString(window);
	return g_minimizedTitleBuffer.c_str();
}

int MRWindowLayout::minimizedDisplayTitleWidth(const MRDesktopWindow *window) noexcept {
	return minimizedTitleWidth(window);
}

int MRWindowLayout::minimizedWindowWidth(const MRDesktopWindow *window) noexcept {
	return minimizedWindowWidthValue(window);
}

bool MRWindowLayout::isMinimizedRestoreGlyphHit(const MRDesktopWindow *window, TPoint local) noexcept {
	TWindow *nativeWindow = window != nullptr ? const_cast<MRDesktopWindow *>(window)->desktopNativeWindow() : nullptr;
	if (nativeWindow == nullptr || !window->desktopMinimized() || local.y != 0) return false;
	const MinimizedLayout layout = minimizedLayout(window, nativeWindow->size.x);
	return local.x >= layout.restoreStart && local.x < layout.restoreEnd;
}

bool MRWindowLayout::isMinimizedReinsertGlyphHit(const MRDesktopWindow *window, TPoint local) noexcept {
	TWindow *nativeWindow = window != nullptr ? const_cast<MRDesktopWindow *>(window)->desktopNativeWindow() : nullptr;
	if (nativeWindow == nullptr || !window->desktopMinimized() || local.y != 0) return false;
	const MinimizedLayout layout = minimizedLayout(window, nativeWindow->size.x);
	return local.x >= layout.reinsertStart && local.x < layout.reinsertEnd;
}

void MRWindowLayout::minimizeWindow(MRDesktopWindow *window) {
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
	MRDesktopMinimizedState minimizedState;
	TRect target;

	if (nativeWindow == nullptr) return;
	window->readDesktopMinimizedState(minimizedState);
	if (minimizedState.minimized) return;
	minimizedState.restoreBounds = clampToBounds(nativeWindow->getBounds(), usableDesktopBoundsForDesktop(window->desktopIndex()));
	if (minimizedState.lastMinimizedBounds.a.x < minimizedState.lastMinimizedBounds.b.x && minimizedState.lastMinimizedBounds.a.y < minimizedState.lastMinimizedBounds.b.y) {
		target = normalizedMinimizedBounds(window, minimizedState.lastMinimizedBounds, fullDesktopBounds());
		if (minimizedBoundsConflict(window, target)) target = nextMinimizedBounds(window);
	} else
		target = nextMinimizedBounds(window);
	const bool wasVisible = (nativeWindow->state & sfVisible) != 0;
	const bool oldShadowWasSet = (nativeWindow->state & sfShadow) != 0;
	if (wasVisible) {
		nativeWindow->hide();
		refreshDesktop();
		if (oldShadowWasSet) TScreen::flushScreen();
	}
	minimizedState.bufferedBeforeMinimize = (nativeWindow->options & ofBuffered) != 0;
	if (minimizedState.bufferedBeforeMinimize) nativeWindow->freeBuffer();
	nativeWindow->options &= ~ofBuffered;
	minimizedState.minimized = true;
	window->storeDesktopMinimizedState(minimizedState);
	if ((nativeWindow->state & sfShadow) != 0) nativeWindow->setState(sfShadow, False);
	setHiddenWindowBounds(window, target);
	window->layoutDesktopContents();
	if (wasVisible) nativeWindow->show();
	minimizedState.lastMinimizedBounds = nativeWindow->getBounds();
	window->storeDesktopMinimizedState(minimizedState);
	refreshDesktop();
	updateLayoutAfterStateChange();
}

void MRWindowLayout::reinsertMinimizedWindow(MRDesktopWindow *window) {
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
	MRDesktopMinimizedState minimizedState;

	if (nativeWindow == nullptr) return;
	window->readDesktopMinimizedState(minimizedState);
	if (!minimizedState.minimized) return;
	placeVisibleWindow(window, nextMinimizedBounds(window));
	minimizedState.lastMinimizedBounds = nativeWindow->getBounds();
	window->storeDesktopMinimizedState(minimizedState);
	updateLayoutAfterStateChange();
}

void MRWindowLayout::restoreWindow(MRDesktopWindow *window) {
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
	MRDesktopMinimizedState minimizedState;

	if (nativeWindow == nullptr) return;
	window->readDesktopMinimizedState(minimizedState);
	if (!minimizedState.minimized) return;
	const bool wasVisible = (nativeWindow->state & sfVisible) != 0;
	const TRect target = clampToBounds(minimizedState.restoreBounds, usableDesktopBoundsForDesktop(window->desktopIndex()));
	minimizedState.lastMinimizedBounds = nativeWindow->getBounds();
	if (wasVisible) {
		nativeWindow->hide();
		refreshDesktop();
	}
	nativeWindow->options = minimizedState.bufferedBeforeMinimize ? ushort(nativeWindow->options | ofBuffered) : ushort(nativeWindow->options & ~ofBuffered);
	nativeWindow->freeBuffer();
	minimizedState.bufferedBeforeMinimize = false;
	minimizedState.minimized = false;
	window->storeDesktopMinimizedState(minimizedState);
	nativeWindow->setState(sfShadow, True);
	setHiddenWindowBounds(window, target);
	window->layoutDesktopContents();
	if (wasVisible) {
		nativeWindow->show();
		nativeWindow->select();
	}
	window->synchronizeDesktopContents();
	updateLayoutAfterStateChange();
}

void MRWindowLayout::toggleMinimizedWindow(MRDesktopWindow *window) {
	if (window == nullptr) return;
	if (window->desktopMinimized()) restoreWindow(window);
	else
		minimizeWindow(window);
}

void MRWindowLayout::applyWorkspaceState(MREditWindow *window, const TRect &bounds, const TRect &restoreBounds, bool minimized, bool notifyTopology, bool projectNow) {
	if (window == nullptr) return;
	TRect target;

	MRDesktopMinimizedState minimizedState;

	window->readDesktopMinimizedState(minimizedState);
	minimizedState.restoreBounds = clampToBounds(restoreBounds, usableDesktopBoundsForDesktop(window->desktopIndex()));
	minimizedState.minimized = minimized;
	window->storeDesktopMinimizedState(minimizedState);
	window->setState(sfShadow, minimized ? False : True);
	if (minimized) {
		target = minimizedBoundsConflict(window, bounds) ? nextMinimizedBounds(window) : normalizedMinimizedBounds(window, bounds, fullDesktopBounds());
		if (projectNow) placeVisibleWindow(window, target);
		else {
			setHiddenWindowBounds(window, target);
			window->layoutEditorChrome();
		}
		window->readDesktopMinimizedState(minimizedState);
		minimizedState.lastMinimizedBounds = window->getBounds();
		window->storeDesktopMinimizedState(minimizedState);
	} else {
		target = clampToBounds(bounds, usableDesktopBoundsForDesktop(window->desktopIndex()));
		if (projectNow) placeVisibleWindow(window, target);
		else {
			setHiddenWindowBounds(window, target);
			window->layoutEditorChrome();
		}
	}
	markLayoutDirty();
	if (notifyTopology) mrNotifyWindowTopologyChanged();
}

void MRWindowLayout::applyBatchWindowBounds(MRDesktopWindow *window, const TRect &bounds) {
	if (window == nullptr) return;
	window->applyDesktopBounds(bounds);
}

void MRWindowLayout::refreshDesktopProjection() {
	refreshDesktop();
}

void MRWindowLayout::handleDesktopLayoutChange() {
	const auto startedAt = std::chrono::steady_clock::now();
	long long reflowUs = 0;
	long long clampUs = 0;
	long long refreshUs = 0;
	std::size_t desktopCount = 0;

	if (TProgram::deskTop == nullptr) return;

	const TRect currentDesktopExtent = fullDesktopBounds();
	const bool extentChanged = !g_lastDesktopExtentValid || currentDesktopExtent != g_lastDesktopExtent;

	if (!extentChanged && !g_layoutDirty) return;

	if (extentChanged) {
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		std::set<int> desktops;
		for (MRDesktopWindow *window : allDesktopWindowsInZOrder())
			if (window != nullptr && window->desktopMinimized() && !window->desktopManuallyHidden()) desktops.insert(window->desktopIndex());
		desktopCount = desktops.size();
		for (int virtualDesktop : desktops)
			reflowMinimizedWindowsForDesktop(virtualDesktop);
		reflowUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}

	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
	clampWindowsToUsableDesktop();
		clampUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	g_lastDesktopExtent = currentDesktopExtent;
	g_lastDesktopExtentValid = true;
	g_layoutDirty = false;
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
	refreshDesktop();
		refreshUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	{
		std::ostringstream detail;
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();

		detail << "extent_changed=" << (extentChanged ? 1 : 0) << " desktops=" << desktopCount << " reflow_us=" << reflowUs << " clamp_us=" << clampUs << " refresh_us=" << refreshUs;
		if (tookUs >= 10000 || clampUs >= 10000 || refreshUs >= 10000) logWindowLayoutTiming("Window layout desktop change timing", tookUs, detail.str());
	}
}
