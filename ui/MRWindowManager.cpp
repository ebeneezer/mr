#include "MRWindowManager.hpp"

#include "MREditWindow.hpp"
#include "MRWindowSupport.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../dialogs/MRWindowList.hpp"

#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TEvent
#define Uses_TProgram
#define Uses_TScreen
#define Uses_TText
#include <tvision/tv.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

extern TPoint shadowSize;

namespace {

constexpr int kMinimizedGap = 3;
constexpr int kMinimizedHeight = 1;
constexpr int kMinimizedTitleMaxWidth = 20;
constexpr int kMinimizedMinWidth = 4;
constexpr int kMinimizedRightPadding = 0;

constexpr char kMinimizedEllipsis[] = "…";
constexpr char kMinimizedFallbackTitle[] = "?No-File";

bool g_layoutDirty = true;
bool g_lastDesktopExtentValid = false;
TRect g_lastDesktopExtent;
std::string g_minimizedTitleBuffer;

TRect fullDesktopBounds() noexcept {
	if (TProgram::deskTop == nullptr) return TRect(0, 0, 1, 1);
	return TProgram::deskTop->getExtent();
}

bool sameDesktopAndVisible(const MREditWindow *window, int virtualDesktop) {
	return window != nullptr && window->mVirtualDesktop == virtualDesktop && !isWindowManuallyHidden(window);
}

std::size_t nextCodeUnitCount(const std::string &text, std::size_t index) noexcept {
	const std::size_t count = TText::next(text, index);
	return count == 0 ? 1 : count;
}

std::string baseNameForMinimizedTitle(const MREditWindow *window) {
	std::string title;
	const char *windowTitle = window != nullptr ? const_cast<MREditWindow *>(window)->getTitle(0) : nullptr;

	if (window != nullptr && window->currentFileName()[0] != '\0') title = window->currentFileName();
	else if (windowTitle != nullptr)
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

std::string minimizedDisplayTitleString(const MREditWindow *window) {
	return truncateDisplayWidthMiddle(baseNameForMinimizedTitle(window), kMinimizedTitleMaxWidth);
}

int minimizedTitleWidth(const MREditWindow *window) {
	return strwidth(minimizedDisplayTitleString(window).c_str());
}

int minimizedWindowWidthValue(const MREditWindow *window) {
	return std::max(kMinimizedMinWidth, strwidth("☰") + 1 + minimizedTitleWidth(window) + 1 + strwidth("↗") + strwidth("↓") + kMinimizedRightPadding);
}

TRect normalizedMinimizedBounds(const MREditWindow *window, const TRect &bounds, const TRect &desktop) {
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

bool minimizedBoundsFitDesktop(const MREditWindow *window, const TRect &bounds) {
	const TRect desktop = fullDesktopBounds();
	const TRect normalized = normalizedMinimizedBounds(window, bounds, desktop);

	return bounds.a.x == normalized.a.x && bounds.a.y == normalized.a.y && bounds.b.x == normalized.b.x && bounds.b.y == normalized.b.y;
}

int minimizedRowsForDesktop(int virtualDesktop) {
	const TRect desktop = fullDesktopBounds();
	int rows = 0;

	for (MREditWindow *window : allEditWindowsInZOrder()) {
		if (window == nullptr || !window->isMinimized() || !sameDesktopAndVisible(window, virtualDesktop)) continue;
		rows = std::max(rows, desktop.b.y - window->getBounds().a.y);
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

void placeVisibleWindow(MREditWindow *window, const TRect &bounds) {
	if (window == nullptr) return;
	TRect target = bounds;
	if (window->getBounds() != target)
		window->locate(target);
	else
		window->changeBounds(target);
	if (window->frame != nullptr) window->frame->drawView();
	if (window->getEditor() != nullptr && !window->isMinimized()) window->getEditor()->syncFromEditorState();
}

void setHiddenWindowBounds(MREditWindow *window, const TRect &bounds) {
	if (window == nullptr) return;
	window->setBounds(bounds);
	if (window->frame != nullptr) {
		const TRect extent = window->getExtent();
		window->frame->setBounds(extent);
	}
}

void markLayoutDirty() noexcept {
	g_layoutDirty = true;
}

void reflowMinimizedWindowsForDesktop(int virtualDesktop) {
	const TRect desktop = fullDesktopBounds();
	std::vector<MREditWindow *> windows;
	int nextX = desktop.a.x;
	int nextY = desktop.b.y - kMinimizedHeight;

	for (MREditWindow *window : allEditWindowsInZOrder())
		if (window != nullptr && window->isMinimized() && sameDesktopAndVisible(window, virtualDesktop)) windows.push_back(window);

	std::sort(windows.begin(), windows.end(), [](const MREditWindow *lhs, const MREditWindow *rhs) {
		if (lhs->getBounds().a.y != rhs->getBounds().a.y) return lhs->getBounds().a.y > rhs->getBounds().a.y;
		return lhs->getBounds().a.x < rhs->getBounds().a.x;
	});

	for (MREditWindow *window : windows) {
		const int width = minimizedWindowWidthValue(window);
		TRect target(nextX, nextY, nextX + width, nextY + kMinimizedHeight);

		if (target.b.x > desktop.b.x) {
			nextX = desktop.a.x;
			nextY = std::max(desktop.a.y, nextY - 1);
			target = TRect(nextX, nextY, nextX + width, nextY + kMinimizedHeight);
		}

		placeVisibleWindow(window, normalizedMinimizedBounds(window, target, desktop));
		nextX = window->getBounds().b.x + kMinimizedGap;
	}
}

TRect nextMinimizedBounds(MREditWindow *window) {
	const TRect desktop = fullDesktopBounds();
	std::vector<MREditWindow *> windows;
	int x = desktop.a.x;
	int y = desktop.b.y - kMinimizedHeight;
	const int width = minimizedWindowWidthValue(window);

	for (MREditWindow *candidate : allEditWindowsInZOrder())
		if (candidate != nullptr && candidate != window && candidate->isMinimized() && sameDesktopAndVisible(candidate, window->mVirtualDesktop)) windows.push_back(candidate);

	std::sort(windows.begin(), windows.end(), [](const MREditWindow *lhs, const MREditWindow *rhs) {
		if (lhs->getBounds().a.y != rhs->getBounds().a.y) return lhs->getBounds().a.y > rhs->getBounds().a.y;
		return lhs->getBounds().a.x < rhs->getBounds().a.x;
	});

	for (MREditWindow *candidate : windows) {
		if (candidate->getBounds().a.y != y) {
			y = candidate->getBounds().a.y;
			x = candidate->getBounds().b.x + kMinimizedGap;
		} else
			x = std::max(x, candidate->getBounds().b.x + kMinimizedGap);
	}

	if (x + width > desktop.b.x) {
		x = desktop.a.x;
		y = std::max(desktop.a.y, y - 1);
	}

	return normalizedMinimizedBounds(window, TRect(x, y, x + width, y + kMinimizedHeight), desktop);
}

bool minimizedBoundsConflict(MREditWindow *window, const TRect &bounds) {
	if (!minimizedBoundsFitDesktop(window, bounds)) return true;
	for (MREditWindow *candidate : allEditWindowsInZOrder()) {
		if (candidate == nullptr || candidate == window || !candidate->isMinimized() || candidate->mVirtualDesktop != window->mVirtualDesktop || isWindowManuallyHidden(candidate)) continue;
		if (rectsOverlap(bounds, candidate->getBounds())) return true;
	}
	return false;
}

void clampWindowsToUsableDesktop() {
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		if (window == nullptr || window->isMinimized() || isWindowManuallyHidden(window)) continue;
		placeVisibleWindow(window, clampToBounds(window->getBounds(), usableDesktopBoundsForDesktop(window->mVirtualDesktop)));
	}
}

void refreshDesktop() {
	if (TProgram::deskTop != nullptr) {
		TProgram::deskTop->redraw();
		TProgram::deskTop->drawView();
	}
	if (TProgram::application != nullptr) TProgram::application->redraw();
}

void updateLayoutAfterStateChange() {
	markLayoutDirty();
	MRWindowManager::handleDesktopLayoutChange();
	mrNotifyWindowTopologyChanged();
}

} // namespace

void MRWindowManager::handleDragView(MREditWindow *window, TEvent &event, uchar mode, TRect &limits, TPoint minSize, TPoint maxSize) {
	if (window == nullptr) return;

	if (window->mMinimized && (mode & dmDragMove) != 0 && event.what == evMouseDown) {
		TRect trayLimits = fullDesktopBounds();
		TRect originalBounds = window->getBounds();
		TPoint offset = window->origin - event.mouse.where;
		const int width = MRWindowManager::minimizedWindowWidth(window);

		window->setState(sfDragging, True);
		do {
			TPoint currentMouse = event.mouse.where;
			TPoint newOrigin = currentMouse + offset;
			TRect targetBounds = originalBounds;

			newOrigin.x = std::min(std::max(newOrigin.x, trayLimits.a.x - width + 1), trayLimits.b.x - 1);
			newOrigin.y = std::min(std::max(newOrigin.y, trayLimits.a.y), trayLimits.b.y - kMinimizedHeight);
			targetBounds.a = newOrigin;
			targetBounds.b.x = newOrigin.x + width;
			targetBounds.b.y = newOrigin.y + kMinimizedHeight;
			if (targetBounds != window->getBounds()) window->locate(targetBounds);
		} while (window->mouseEvent(event, evMouseMove));
		window->mLastMinimizedBounds = window->getBounds();
		window->setState(sfDragging, False);
		updateLayoutAfterStateChange();
		return;
	}

	if (!configuredWindowManager() || (mode & dmDragMove) == 0 || event.what != evMouseDown) {
		window->TWindow::dragView(event, mode, limits, minSize, maxSize);
		return;
	}

	TRect dragLimits = window->mMinimized ? fullDesktopBounds() : usableDesktopBoundsForDesktop(window->mVirtualDesktop);
	TRect originalBounds = window->getBounds();
	TPoint offset = window->origin - event.mouse.where;

	window->setState(sfDragging, True);

	do {
		TPoint currentMouse = event.mouse.where;
		TPoint mouseLocal = window->owner->makeLocal(currentMouse);
		TRect deskExtent = dragLimits;
		bool snapped = false;
		TRect targetBounds = originalBounds;

		if (!window->mMinimized) {
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

		if (targetBounds != window->getBounds()) window->locate(targetBounds);
	} while (window->mouseEvent(event, evMouseMove));

	window->setState(sfDragging, False);
	if (window->mMinimized) {
		window->mLastMinimizedBounds = window->getBounds();
		updateLayoutAfterStateChange();
	}
}

bool MRWindowManager::isWindowMinimized(const MREditWindow *window) noexcept {
	return window != nullptr && window->mMinimized;
}

int MRWindowManager::minimizedDesktopRows() noexcept {
	return minimizedRowsForDesktop(currentVirtualDesktop());
}

TRect MRWindowManager::usableDesktopBounds() noexcept {
	return usableDesktopBoundsForDesktop(currentVirtualDesktop());
}

TRect MRWindowManager::minimizedBoundsForWorkspace(const MREditWindow *window) noexcept {
	if (window == nullptr) return TRect(0, 0, 1, 1);
	if (window->mMinimized) return window->getBounds();
	return normalizedMinimizedBounds(window, nextMinimizedBounds(const_cast<MREditWindow *>(window)), fullDesktopBounds());
}

TRect MRWindowManager::restoreBoundsForWorkspace(const MREditWindow *window) noexcept {
	if (window == nullptr) return TRect(0, 0, 1, 1);
	if (window->mMinimized) return window->mRestoreBounds;
	return window->getBounds();
}

const char *MRWindowManager::minimizedDisplayTitle(const MREditWindow *window) noexcept {
	g_minimizedTitleBuffer = minimizedDisplayTitleString(window);
	return g_minimizedTitleBuffer.c_str();
}

int MRWindowManager::minimizedDisplayTitleWidth(const MREditWindow *window) noexcept {
	return minimizedTitleWidth(window);
}

int MRWindowManager::minimizedWindowWidth(const MREditWindow *window) noexcept {
	return minimizedWindowWidthValue(window);
}

bool MRWindowManager::isMinimizedRestoreGlyphHit(const MREditWindow *window, TPoint local) noexcept {
	if (window == nullptr || !window->mMinimized || local.y != 0) return false;
	const int restoreWidth = strwidth("↗");
	const int reinsertWidth = strwidth("↓");
	const int restoreStart = window->size.x - kMinimizedRightPadding - reinsertWidth - restoreWidth - 1;
	return local.x >= restoreStart && local.x < restoreStart + restoreWidth;
}

bool MRWindowManager::isMinimizedReinsertGlyphHit(const MREditWindow *window, TPoint local) noexcept {
	if (window == nullptr || !window->mMinimized || local.y != 0) return false;
	const int reinsertWidth = strwidth("↓");
	const int reinsertStart = window->size.x - kMinimizedRightPadding - reinsertWidth - 1;
	return local.x >= reinsertStart && local.x < reinsertStart + reinsertWidth;
}

void MRWindowManager::minimizeWindow(MREditWindow *window) {
	if (window == nullptr || window->mMinimized) return;
	TRect target;
	const bool wasVisible = (window->state & sfVisible) != 0;
	window->mRestoreBounds = clampToBounds(window->getBounds(), usableDesktopBoundsForDesktop(window->mVirtualDesktop));
	if (window->mLastMinimizedBounds.a.x < window->mLastMinimizedBounds.b.x && window->mLastMinimizedBounds.a.y < window->mLastMinimizedBounds.b.y) {
		target = normalizedMinimizedBounds(window, window->mLastMinimizedBounds, fullDesktopBounds());
		if (minimizedBoundsConflict(window, target)) target = nextMinimizedBounds(window);
	} else
		target = nextMinimizedBounds(window);
	if (wasVisible) {
		window->hide();
		refreshDesktop();
		TScreen::flushScreen();
	}
	window->mMinimized = true;
	if ((window->state & sfShadow) != 0) window->setState(sfShadow, False);
	setHiddenWindowBounds(window, target);
	window->layoutEditorChrome();
	if (wasVisible) window->show();
	window->mLastMinimizedBounds = window->getBounds();
	refreshDesktop();
	updateLayoutAfterStateChange();
}

void MRWindowManager::reinsertMinimizedWindow(MREditWindow *window) {
	if (window == nullptr || !window->mMinimized) return;
	placeVisibleWindow(window, nextMinimizedBounds(window));
	window->mLastMinimizedBounds = window->getBounds();
	updateLayoutAfterStateChange();
}

void MRWindowManager::restoreWindow(MREditWindow *window) {
	if (window == nullptr || !window->mMinimized) return;
	const bool wasVisible = (window->state & sfVisible) != 0;
	const TRect target = clampToBounds(window->mRestoreBounds, usableDesktopBoundsForDesktop(window->mVirtualDesktop));
	window->mLastMinimizedBounds = window->getBounds();
	if (wasVisible) {
		window->hide();
		refreshDesktop();
	}
	window->setState(sfShadow, True);
	window->mMinimized = false;
	setHiddenWindowBounds(window, target);
	window->layoutEditorChrome();
	if (wasVisible) {
		window->show();
		window->select();
	}
	if (window->getEditor() != nullptr) window->getEditor()->syncFromEditorState();
	updateLayoutAfterStateChange();
}

void MRWindowManager::toggleMinimizedWindow(MREditWindow *window) {
	if (window == nullptr) return;
	if (window->mMinimized) restoreWindow(window);
	else
		minimizeWindow(window);
}

void MRWindowManager::applyWorkspaceState(MREditWindow *window, const TRect &bounds, const TRect &restoreBounds, bool minimized) {
	if (window == nullptr) return;
	window->mRestoreBounds = clampToBounds(restoreBounds, usableDesktopBoundsForDesktop(window->mVirtualDesktop));
	window->mMinimized = minimized;
	window->setState(sfShadow, minimized ? False : True);
	if (minimized) {
		const TRect target = minimizedBoundsConflict(window, bounds) ? nextMinimizedBounds(window) : normalizedMinimizedBounds(window, bounds, fullDesktopBounds());
		placeVisibleWindow(window, target);
		window->mLastMinimizedBounds = window->getBounds();
	} else
		placeVisibleWindow(window, clampToBounds(bounds, usableDesktopBoundsForDesktop(window->mVirtualDesktop)));
	markLayoutDirty();
	mrNotifyWindowTopologyChanged();
}

void MRWindowManager::handleDesktopLayoutChange() {
	if (TProgram::deskTop == nullptr) return;

	const TRect currentDesktopExtent = fullDesktopBounds();
	const bool extentChanged = !g_lastDesktopExtentValid || currentDesktopExtent != g_lastDesktopExtent;

	if (!extentChanged && !g_layoutDirty) return;

	if (extentChanged) {
		std::set<int> desktops;
		for (MREditWindow *window : allEditWindowsInZOrder())
			if (window != nullptr && window->mMinimized && !isWindowManuallyHidden(window)) desktops.insert(window->mVirtualDesktop);
		for (int virtualDesktop : desktops)
			reflowMinimizedWindowsForDesktop(virtualDesktop);
	}

	clampWindowsToUsableDesktop();
	g_lastDesktopExtent = currentDesktopExtent;
	g_lastDesktopExtentValid = true;
	g_layoutDirty = false;
	refreshDesktop();
}
