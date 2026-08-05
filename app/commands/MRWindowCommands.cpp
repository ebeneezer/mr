#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TFileDialog
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TScreen
#include <tvision/tv.h>

#include "MRBentoWorkspaceCodec.hpp"
#include "MRFileCommands.hpp"
#include "MRWindowCommands.hpp"
#include "MRWindowCommandsInternal.hpp"
#include "../MRMacroDebuggerCommandRoute.hpp"
#include "../router/MRCommandRouterGit.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"
#include "../utils/MRFileIOUtils.hpp"
#include "MRPerformance.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRBentoBox/MRBentoBox.hpp"
#include "../../ui/MRBentoHexEditor/MRBentoHexEditor.hpp"
#include "../../ui/widgets/MRScopedHistoryUI.hpp"
#include "../../ui/MRWindowLayout.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../../ui/MRDesktopWindow.hpp"
#include "../../dialogs/MRWindowList.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace mr::window_commands {
void logWindowTiming(const std::string &label, long long tookUs, const std::string &detail) {
	std::ostringstream line;

	line << label << " took_us=" << tookUs;
	if (!detail.empty()) line << " " << detail;
	mrLogMessage(line.str());
}

void postWindowCommandError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}
} // namespace mr::window_commands

using mr::window_commands::applicationUiInt;
using mr::window_commands::applicationUiString;
using mr::window_commands::applicationUiUnsigned;
using mr::window_commands::kVirtualDesktopsBranch;
using mr::window_commands::kWorkspaceBranch;
using mr::window_commands::normalizedVirtualDesktopCount;
using mr::window_commands::storeApplicationUiInt;
using mr::window_commands::storeApplicationUiString;
using mr::window_commands::storeApplicationUiUnsigned;
using mr::window_commands::logWindowTiming;
using mr::window_commands::postWindowCommandError;

namespace {
void postDeferredWindowClose(MREditWindow &window) noexcept {
	TEvent event{};

	if (TProgram::application == nullptr) {
		window.close();
		return;
	}
	event.what = evCommand;
	event.message.command = cmMrDeferredWindowClose;
	event.message.infoPtr = &window;
	TProgram::application->putEvent(event);
}

void collectEditWindowsInZOrder(TView *view, void *arg) {
	std::vector<MREditWindow *> *windows = static_cast<std::vector<MREditWindow *> *>(arg);
	MREditWindow *win = dynamic_cast<MREditWindow *>(view);

	if (windows != nullptr && win != nullptr) windows->push_back(win);
}

void collectDesktopWindowsInZOrder(TView *view, void *arg) {
	std::vector<MRDesktopWindow *> *windows = static_cast<std::vector<MRDesktopWindow *> *>(arg);
	MRDesktopWindow *window = dynamic_cast<MRDesktopWindow *>(view);

	if (windows != nullptr && window != nullptr) windows->push_back(window);
}
} // namespace

std::vector<MREditWindow *> allEditWindowsInZOrder() {
	std::vector<MREditWindow *> windows;
	const auto startedAt = std::chrono::steady_clock::now();

	if (TProgram::deskTop == nullptr) return windows;

	TProgram::deskTop->forEach(collectEditWindowsInZOrder, &windows);
	{
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
		if (tookUs >= 5000) logWindowTiming("Window enumerate slow", tookUs, "count=" + std::to_string(windows.size()));
	}
	return windows;
}

std::vector<MREditWindow *> allEditWindowsAndBentoPanesInZOrder() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	std::vector<MREditWindow *> expanded;

	expanded.reserve(windows.size());
	for (MREditWindow *window : windows) {
		expanded.push_back(window);
		if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window)) bentoBox->collectVisiblePaneWindows(expanded);
	}
	return expanded;
}

std::vector<MRDesktopWindow *> allDesktopWindowsInZOrder() {
	std::vector<MRDesktopWindow *> windows;

	if (TProgram::deskTop == nullptr) return windows;
	TProgram::deskTop->forEach(collectDesktopWindowsInZOrder, &windows);
	return windows;
}

MRDesktopWindow *currentDesktopWindow() {
	return TProgram::deskTop != nullptr ? dynamic_cast<MRDesktopWindow *>(TProgram::deskTop->current) : nullptr;
}

namespace {
void collectUsedEditorWindowNumbers(std::set<short> &used) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (auto &window : windows) {
		if (window != nullptr && window->number > 0) used.insert(window->number);
	}
}

short nextEditorWindowNumberFromSet(std::set<short> &used) {
	short candidate = 1;

	while (used.find(candidate) != used.end()) {
		if (candidate == std::numeric_limits<short>::max()) return candidate;
		++candidate;
	}
	used.insert(candidate);
	return candidate;
}

short nextEditorWindowNumber() {
	std::set<short> used;

	collectUsedEditorWindowNumbers(used);
	return nextEditorWindowNumberFromSet(used);
}

void finishNewEditWindow(MREditWindow *win, bool notifyTopology = true, bool initiallyVisible = true) {
	if (win == nullptr || TProgram::deskTop == nullptr) return;
	if (!initiallyVisible) win->hide();
	TProgram::deskTop->insert(win);
	win->mVirtualDesktop = currentVirtualDesktop();
	win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
	if (win->getEditor() != nullptr) win->getEditor()->setInsertModeEnabled(configuredDefaultInsertMode());
	if (notifyTopology) mrNotifyWindowTopologyChanged();
}

MREditWindow *createEditorWindowWithNumber(const char *title, short number, bool notifyTopology, bool initiallyVisible = true) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRBentoBox(bounds, title, number, bbmDocumentViewports);
	finishNewEditWindow(win, notifyTopology, initiallyVisible);
	return win;
}

MRBentoHexEditor *createHexEditorWindowWithNumber(const char *title, short number, bool notifyTopology, bool initiallyVisible = true) {
	TRect bounds;
	MRBentoHexEditor *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRBentoHexEditor(bounds, title, number);
	finishNewEditWindow(win, notifyTopology, initiallyVisible);
	return win;
}
} // namespace

MRWindowOpenBatch::MRWindowOpenBatch() : usedNumbers(), mActive(false), mDesktopLocked(false), mDeferVisibility(false), mCreatedCount(0) {
}

void MRWindowOpenBatch::begin() {
	beginBatch(false);
}

void MRWindowOpenBatch::beginInteractive() {
	beginBatch(true);
}

void MRWindowOpenBatch::beginBatch(bool deferVisibility) {
	if (mActive) return;
	usedNumbers.clear();
	collectUsedEditorWindowNumbers(usedNumbers);
	mCreatedCount = 0;
	mDeferVisibility = deferVisibility;
	if (!mDeferVisibility && TProgram::deskTop != nullptr) {
		TProgram::deskTop->lock();
		mDesktopLocked = true;
	}
	mActive = true;
}

MREditWindow *MRWindowOpenBatch::createEditorWindow(const char *title) {
	MREditWindow *window = nullptr;

	if (!mActive) begin();
	window = createEditorWindowWithNumber(title, nextEditorWindowNumberFromSet(usedNumbers), false, !mDeferVisibility);
	if (window != nullptr) ++mCreatedCount;
	return window;
}

MRBentoHexEditor *MRWindowOpenBatch::createHexEditorWindow(const char *title) {
	MRBentoHexEditor *window = nullptr;

	if (!mActive) begin();
	window = createHexEditorWindowWithNumber(title, nextEditorWindowNumberFromSet(usedNumbers), false, !mDeferVisibility);
	if (window != nullptr) ++mCreatedCount;
	return window;
}

void MRWindowOpenBatch::finish(bool syncVisibility, bool notifyTopology) {
	const bool synchronizeDeferredVisibility = mDeferVisibility && mCreatedCount != 0 && syncVisibility;

	if (!mActive) return;
	if (mDesktopLocked && TProgram::deskTop != nullptr) TProgram::deskTop->unlock();
	mDesktopLocked = false;
	mActive = false;
	if (synchronizeDeferredVisibility && TProgram::deskTop != nullptr) TProgram::deskTop->lock();
	if (mCreatedCount != 0 && syncVisibility) syncVirtualDesktopVisibility();
	if (synchronizeDeferredVisibility && TProgram::deskTop != nullptr) TProgram::deskTop->unlock();
	if (mCreatedCount != 0 && notifyTopology) mrNotifyWindowTopologyChanged();
	mDeferVisibility = false;
}

bool MRWindowOpenBatch::active() const noexcept {
	return mActive;
}

MREditWindow *createEditorWindow(const char *title) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRBentoBox(bounds, title, nextEditorWindowNumber(), bbmDocumentViewports);
	finishNewEditWindow(win);
	return win;
}

MRBentoHexEditor *createHexEditorWindow(const char *title) {
	return createHexEditorWindowWithNumber(title, nextEditorWindowNumber(), true);
}

MREditWindow *createHelpWindow(const char *title) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRHelpWindow(bounds, title, nextEditorWindowNumber());
	finishNewEditWindow(win);
	return win;
}

MREditWindow *createLogWindow(const char *title) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRLogWindow(bounds, title, nextEditorWindowNumber());
	finishNewEditWindow(win, false);
	return win;
}

MREditWindow *createCommunicationWindow(const char *title) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRCommunicationWindow(bounds, title, nextEditorWindowNumber());
	finishNewEditWindow(win);
	return win;
}

MRBentoBox *createBentoBoxWindow(const char *title) {
	TRect bounds;
	MRBentoBox *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRBentoBox(bounds, title, nextEditorWindowNumber());
	finishNewEditWindow(win);
	return win;
}

MRBentoBox *createFileCompareBentoBoxWindow(const char *title) {
	TRect bounds;
	MRBentoBox *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRBentoBox(bounds, title, nextEditorWindowNumber(), bbmFileCompare);
	finishNewEditWindow(win);
	return win;
}

MRBentoBox *convertEditWindowToBentoBox(MREditWindow *source) {
	MRBentoBox *existingBento;
	MRBentoBox *win;
	TRect bounds;
	const char *title;
	MREditWindow::WindowRole role;
	std::string roleDetail;
	bool readOnly;
	bool changed;
	bool insertMode;
	int virtualDesktop;
	short windowNumber;

	if (source == nullptr || TProgram::deskTop == nullptr || source->getEditor() == nullptr) return nullptr;
	existingBento = dynamic_cast<MRBentoBox *>(source);
	if (existingBento != nullptr) return existingBento;
	if (!source->allowsDocumentViewportSplit()) return nullptr;
	if (source->hasTrackedExternalIoTasks()) return nullptr;

	bounds = source->getBounds();
	title = source->getTitle(0);
	role = source->windowRole();
	roleDetail = source->windowRoleDetail();
	readOnly = source->isReadOnly();
	changed = source->isFileChanged();
	insertMode = source->insertModeEnabled();
	virtualDesktop = source->mVirtualDesktop;
	windowNumber = source->number;

	win = new MRBentoBox(bounds, title != nullptr && *title != '\0' ? title : "Untitled", windowNumber, bbmDocumentViewports);
	TProgram::deskTop->insert(win);
	if (win == nullptr) return nullptr;
	win->mVirtualDesktop = virtualDesktop;
	win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
	if (win->getEditor() != nullptr) {
		win->getEditor()->shareContentStateFrom(*source->getEditor());
		win->getEditor()->setInsertModeEnabled(insertMode);
	}
	win->setWindowRole(role, roleDetail);
	win->setReadOnly(readOnly);
	if (source->currentFileName()[0] == '\0') win->setDisplayTitle(title);
	win->setFileChanged(changed);
	win->activatePrimaryPane();

	source->getEditor()->detachContentStateCopy();
	source->setFileChanged(false);
	setWindowManuallyHidden(source, false);
	static_cast<void>(mrActivateEditWindow(win));
	mrNotifyWindowTopologyChanged();
	postDeferredWindowClose(*source);
	return win;
}

MRBentoHexEditor *convertEditWindowToHexEditor(MREditWindow *source) {
	MRBentoHexEditor *existingHexEditor;
	MRBentoHexEditor *win;
	TRect bounds;
	const char *title;
	MREditWindow::WindowRole role;
	std::string roleDetail;
	bool readOnly;
	bool changed;
	bool insertMode;
	int virtualDesktop;
	short windowNumber;

	if (source == nullptr || TProgram::deskTop == nullptr || source->getEditor() == nullptr) return nullptr;
	existingHexEditor = dynamic_cast<MRBentoHexEditor *>(source);
	if (existingHexEditor != nullptr) return existingHexEditor;
	if (!source->allowsDocumentViewportSplit() || source->hasTrackedExternalIoTasks()) return nullptr;

	bounds = source->getBounds();
	title = source->getTitle(0);
	role = source->windowRole();
	roleDetail = source->windowRoleDetail();
	readOnly = source->isReadOnly();
	changed = source->isFileChanged();
	insertMode = source->insertModeEnabled();
	virtualDesktop = source->mVirtualDesktop;
	windowNumber = source->number;

	win = new MRBentoHexEditor(bounds, title != nullptr && *title != '\0' ? title : "Untitled", windowNumber);
	TProgram::deskTop->insert(win);
	win->mVirtualDesktop = virtualDesktop;
	win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
	if (win->getEditor() != nullptr) {
		win->getEditor()->shareContentStateFrom(*source->getEditor());
		win->getEditor()->setInsertModeEnabled(insertMode);
		win->synchronizePaneDocumentState();
	}
	win->setWindowRole(role, roleDetail);
	win->setReadOnly(readOnly);
	if (source->currentFileName()[0] == '\0') win->setDisplayTitle(title);
	win->setFileChanged(changed);
	win->activatePrimaryPane();

	source->getEditor()->detachContentStateCopy();
	source->setFileChanged(false);
	setWindowManuallyHidden(source, false);
	static_cast<void>(mrActivateEditWindow(win));
	mrNotifyWindowTopologyChanged();
	postDeferredWindowClose(*source);
	return win;
}

bool mrDispatchDeferredWindowClose(MREditWindow *window) {
	for (MREditWindow *candidate : allEditWindowsInZOrder()) {
		if (candidate != window) continue;
		candidate->close();
		return true;
	}
	return false;
}

MREditWindow *currentEditWindow() {
	if (TProgram::deskTop == nullptr || TProgram::deskTop->current == nullptr) return nullptr;
	return dynamic_cast<MREditWindow *>(TProgram::deskTop->current);
}

MREditWindow *currentEditorCommandWindow() {
	MREditWindow *window = currentEditWindow();

	return window != nullptr ? window->editorCommandTarget() : nullptr;
}

MREditWindow *findEditWindowByBufferId(int bufferId) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	for (auto &window : windows) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);

		if (window != nullptr && window->bufferId() == bufferId) return window;
		if (bentoBox != nullptr) {
			MREditWindow *pane = bentoBox->paneForBufferId(bufferId);
			if (pane != nullptr) return pane;
		}
	}
	return nullptr;
}

bool isEmptyUntitledEditableWindow(MREditWindow *win) {
	if (win == nullptr || win->isReadOnly() || win->currentFileName()[0] != '\0' || win->isFileChanged()) return false;
	return win->isBufferEmpty();
}

MREditWindow *findReusableEmptyWindow(MREditWindow *preferred) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	if (preferred != nullptr && isEmptyUntitledEditableWindow(preferred)) return preferred;
	for (auto &window : windows)
		if (isEmptyUntitledEditableWindow(window)) return window;
	return nullptr;
}

bool closeCurrentEditWindow() {
	MREditWindow *win = currentEditWindow();
	if (win == nullptr) return false;
	setWindowManuallyHidden(win, false);
	message(win, evCommand, cmClose, nullptr);
	return mrEnsureUsableWorkWindow(false) || currentEditWindow() == nullptr;
}

bool activateRelativeDesktopWindow(int delta) {
	std::vector<MRDesktopWindow *> windows = allDesktopWindowsInZOrder();
	MRDesktopWindow *current = currentDesktopWindow();
	std::size_t index = 0;

	if (windows.empty()) return false;
	if (current == nullptr) current = windows.front();
	for (; index < windows.size(); ++index)
		if (windows[index] == current) break;
	if (index == windows.size()) index = 0;
	int nextIndex = static_cast<int>(index) + delta;
	const int count = static_cast<int>(windows.size());

	while (nextIndex < 0)
		nextIndex += count;
	nextIndex %= count;
	MRDesktopWindow *target = windows[static_cast<std::size_t>(nextIndex)];
	TWindow *nativeWindow = target != nullptr ? target->desktopNativeWindow() : nullptr;

	if (target == nullptr || nativeWindow == nullptr) return false;
	if (target->desktopIndex() != currentVirtualDesktop()) setCurrentVirtualDesktop(target->desktopIndex());
	if (target->desktopManuallyHidden()) {
		target->setDesktopManuallyHidden(false);
		mrNotifyWindowTopologyChanged();
	}
	syncVirtualDesktopVisibility();
	nativeWindow->select();
	return true;
}

bool hideCurrentDesktopWindow() {
	MRDesktopWindow *window = currentDesktopWindow();
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;

	if (nativeWindow == nullptr) return false;
	window->setDesktopManuallyHidden(true);
	nativeWindow->hide();
	mrNotifyWindowTopologyChanged();
	return mrEnsureUsableWorkWindow();
}

bool closeCurrentDesktopWindow() {
	MRDesktopWindow *window = currentDesktopWindow();
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
	MREditWindow *editWindow = dynamic_cast<MREditWindow *>(window);

	if (editWindow != nullptr) return closeCurrentEditWindow();
	if (nativeWindow == nullptr) return false;
	message(nativeWindow, evCommand, cmClose, nativeWindow);
	return true;
}

bool zoomCurrentDesktopWindow() {
	MRDesktopWindow *window = currentDesktopWindow();
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;

	if (nativeWindow == nullptr || (nativeWindow->flags & wfZoom) == 0) return false;
	message(nativeWindow, evCommand, cmZoom, nativeWindow);
	return true;
}

void mrUpdateAllWindowsColorTheme() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	for (auto &window : windows) {
		if (window != nullptr) {
			window->applyWindowColorThemeForPath(window->currentFileName());
			if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window); bentoBox != nullptr) bentoBox->refreshBentoColorTheme();
		}
	}
}

void mrRefreshAllHexEditorProjections() {
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		MRBentoHexEditor *hexEditor = dynamic_cast<MRBentoHexEditor *>(window);

		if (hexEditor != nullptr) hexEditor->refreshHexProjection();
	}
}
