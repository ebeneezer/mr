#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TProgram
#define Uses_TWindow
#include <tvision/tv.h>

#include "MRWindowCommands.hpp"
#include "MRWindowCommandsInternal.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRDesktopWindow.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRWindowLayout.hpp"
#include "../../ui/MRWindowSupport.hpp"

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;

using mr::window_commands::applicationUiInt;
using mr::window_commands::kVirtualDesktopsBranch;
using mr::window_commands::normalizedVirtualDesktopCount;
using mr::window_commands::storeApplicationUiInt;

int currentVirtualDesktop() {
	return applicationUiInt(mrvmRuntimeKv(), kVirtualDesktopsBranch, "current", 1);
}

void mrRefreshVirtualDesktopSettingsSnapshot(int count, bool cyclic) {
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	storeApplicationUiInt(runtimeKv, kVirtualDesktopsBranch, "countSnapshot", normalizedVirtualDesktopCount(count));
	storeApplicationUiInt(runtimeKv, kVirtualDesktopsBranch, "cyclicSnapshot", cyclic ? 1 : 0);
}

void mrRefreshVirtualDesktopSettingsSnapshot() {
	mrRefreshVirtualDesktopSettingsSnapshot(configuredVirtualDesktops(), configuredCyclicVirtualDesktops());
}

int mrVirtualDesktopCountSnapshot() {
	return applicationUiInt(mrvmRuntimeKv(), kVirtualDesktopsBranch, "countSnapshot", 1);
}

bool mrCyclicVirtualDesktopsSnapshot() {
	return applicationUiInt(mrvmRuntimeKv(), kVirtualDesktopsBranch, "cyclicSnapshot", 0) != 0;
}

void setWindowManuallyHidden(MREditWindow *win, bool hidden) {
	if (win == nullptr) return;
	if (hidden == isWindowManuallyHidden(win)) return;
	win->setDesktopManuallyHidden(hidden);
	MRWindowLayout::handleDesktopLayoutChange();
	mrNotifyWindowTopologyChanged();
}

bool isWindowManuallyHidden(const MREditWindow *win) {
	return win != nullptr && win->desktopManuallyHidden();
}

namespace {
void postDesktopChangedMessage(int desktop) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Desktop #" + std::to_string(desktop), mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
}
} // namespace

void syncVirtualDesktopVisibility() {
	std::vector<MRDesktopWindow *> windows = allDesktopWindowsInZOrder();
	MRDesktopWindow *candidate = nullptr;
	MRDesktopWindow *current = TProgram::deskTop != nullptr ? dynamic_cast<MRDesktopWindow *>(TProgram::deskTop->current) : nullptr;
	const int activeDesktop = currentVirtualDesktop();

	for (MRDesktopWindow *window : windows) {
		TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
		const bool visible = window != nullptr && window->desktopIndex() == activeDesktop && !window->desktopManuallyHidden();

		if (nativeWindow == nullptr) continue;
		if (visible) {
			if (candidate == nullptr) candidate = window;
			if ((nativeWindow->state & sfVisible) == 0) nativeWindow->show();
		} else if ((nativeWindow->state & sfVisible) != 0)
			nativeWindow->hide();
	}

	if (candidate != nullptr && (current == nullptr || current->desktopIndex() != activeDesktop || (current->desktopNativeWindow()->state & sfVisible) == 0)) candidate->desktopNativeWindow()->select();

	if (TProgram::deskTop != nullptr) {
		TProgram::deskTop->redraw();
		TProgram::deskTop->drawView();
	}
	if (TProgram::application != nullptr) TProgram::application->redraw();
	MRWindowLayout::handleDesktopLayoutChange();
}

void setCurrentVirtualDesktop(int vd) {
	const int oldDesktop = currentVirtualDesktop();

	if (vd < 1) vd = 1;
	int maxVd = mrVirtualDesktopCountSnapshot();
	if (maxVd < 1) maxVd = 1;
	if (vd > maxVd) vd = maxVd;
	storeApplicationUiInt(mrvmRuntimeKv(), kVirtualDesktopsBranch, "current", vd);
	syncVirtualDesktopVisibility();
	if (vd != oldDesktop) postDesktopChangedMessage(vd);
}

void applyVirtualDesktopConfigurationChange(int count) {
	std::vector<MRDesktopWindow *> windows = allDesktopWindowsInZOrder();
	std::string ignoredError;

	count = normalizedVirtualDesktopCount(count);
	for (MRDesktopWindow *window : windows)
		if (window != nullptr && window->desktopIndex() > count) window->setDesktopIndex(count);

	setConfiguredVirtualDesktops(count, &ignoredError);
	mrRefreshVirtualDesktopSettingsSnapshot(count, configuredCyclicVirtualDesktops());
	setCurrentVirtualDesktop(std::min(currentVirtualDesktop(), count));
}

bool moveToNextVirtualDesktop() {
	MRDesktopWindow *window = TProgram::deskTop != nullptr ? dynamic_cast<MRDesktopWindow *>(TProgram::deskTop->current) : nullptr;
	int maxVd = mrVirtualDesktopCountSnapshot();
	if (window == nullptr || window->desktopIndex() >= maxVd) return false;
	window->setDesktopIndex(window->desktopIndex() + 1);
	syncVirtualDesktopVisibility();
	return true;
}

bool moveToPrevVirtualDesktop() {
	MRDesktopWindow *window = TProgram::deskTop != nullptr ? dynamic_cast<MRDesktopWindow *>(TProgram::deskTop->current) : nullptr;

	if (window == nullptr || window->desktopIndex() <= 1) return false;
	window->setDesktopIndex(window->desktopIndex() - 1);
	syncVirtualDesktopVisibility();
	return true;
}

bool viewportRight() {
	int maxVd = mrVirtualDesktopCountSnapshot();
	const int activeDesktop = currentVirtualDesktop();
	if (activeDesktop >= maxVd) {
		if (mrCyclicVirtualDesktopsSnapshot() && maxVd > 1) {
			setCurrentVirtualDesktop(1);
			return true;
		}
		return false;
	}
	setCurrentVirtualDesktop(activeDesktop + 1);
	return true;
}

bool viewportLeft() {
	int maxVd = mrVirtualDesktopCountSnapshot();
	const int activeDesktop = currentVirtualDesktop();
	if (activeDesktop <= 1) {
		if (mrCyclicVirtualDesktopsSnapshot() && maxVd > 1) {
			setCurrentVirtualDesktop(maxVd);
			return true;
		}
		return false;
	}
	setCurrentVirtualDesktop(activeDesktop - 1);
	return true;
}
