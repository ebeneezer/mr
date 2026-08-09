#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TFileDialog
#define Uses_TGroup
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TListViewer
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TView
#include <tvision/tv.h>

#include "MRSetupCommon.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../keymap/MRKeymapContext.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRMenuBar.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/widgets/MRScopedHistoryUI.hpp"
#include "../../ui/MRWindowSupport.hpp"

#include <array>
#include <limits>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {

TFrame *initSetupDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

bool pathIsDirectory(const std::string &path) {
	struct stat st;

	if (path.empty()) return false;
	return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string parentDirectoryOfPath(std::string_view path) {
	std::string normalized = normalizeConfiguredPathInput(trimAscii(path));
	const std::size_t slashPos = normalized.find_last_of('/');

	if (normalized.empty() || slashPos == std::string::npos) return std::string();
	if (slashPos == 0) return "/";
	return normalized.substr(0, slashPos);
}

std::string resolveFileDialogSeedDirectory(MRDialogHistoryScope scope, const char *buffer) {
	std::string seedPath = normalizeConfiguredPathInput(trimAscii(buffer != nullptr ? buffer : ""));

	if (seedPath.empty()) seedPath = configuredLastFileDialogPath(scope);
	if (seedPath.empty()) seedPath = configuredLastFileDialogFilePath(scope);
	if (pathIsDirectory(seedPath)) return seedPath;
	return parentDirectoryOfPath(seedPath);
}

bool deferRememberingLoadDialogPath(MRDialogHistoryScope scope) {
	switch (scope) {
		case MRDialogHistoryScope::OpenFile:
		case MRDialogHistoryScope::LoadFile:
		case MRDialogHistoryScope::BlockSave:
		case MRDialogHistoryScope::BlockLoad:
		case MRDialogHistoryScope::MacroFile:
		case MRDialogHistoryScope::KeymapProfileLoad:
		case MRDialogHistoryScope::WorkspaceLoad:
		case MRDialogHistoryScope::SetupThemeLoad:
		case MRDialogHistoryScope::PdfExport:
			return true;
		default:
			return false;
	}
}

std::string dialogSeedFileName(std::string_view value) {
	const std::string trimmed = trimAscii(value);
	const std::string normalized = normalizeConfiguredPathInput(trimmed);
	const std::size_t sep = normalized.find_last_of('/');

	if (!normalized.empty()) {
		if (sep == std::string::npos) return normalized;
		return normalized.substr(sep + 1);
	}
	if (trimmed.find('/') != std::string::npos || trimmed.find('\\') != std::string::npos) {
		const std::size_t rawSep = trimmed.find_last_of("/\\");
		if (rawSep == std::string::npos) return trimmed;
		return trimmed.substr(rawSep + 1);
	}
	return trimmed;
}

mr::messageline::Kind toSetupMessageLineKind(MRMenuBar::MarqueeKind kind) {
	switch (kind) {
		case MRMenuBar::MarqueeKind::Success:
			return mr::messageline::Kind::Success;
		case MRMenuBar::MarqueeKind::Warning:
			return mr::messageline::Kind::Warning;
		case MRMenuBar::MarqueeKind::Error:
			return mr::messageline::Kind::Error;
		case MRMenuBar::MarqueeKind::Hero:
		case MRMenuBar::MarqueeKind::Info:
		default:
			return mr::messageline::Kind::Info;
	}
}

void setSetupDialogStatus(const std::string &text, MRMenuBar::MarqueeKind kind) {
	if (text.empty()) {
		mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
		return;
	}
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogValidation, text, toSetupMessageLineKind(kind), mr::messageline::kPriorityHigh);
}

void clearSetupDialogStatus() {
	mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
}

bool startsWithDoneButtonCaption(const char *title) {
	std::string normalized;

	if (title == nullptr) return false;
	for (const char *p = title; *p != '\0'; ++p) {
		const unsigned char ch = static_cast<unsigned char>(*p);
		if (ch == '~' || std::isspace(ch)) continue;
		if (ch == '<') break;
		normalized.push_back(static_cast<char>(std::toupper(ch)));
	}
	return normalized.rfind("DONE", 0) == 0;
}

std::string visibleButtonCaption(const char *title) {
	std::string text;

	if (title == nullptr) return text;
	for (const char *p = title; *p != '\0'; ++p)
		if (*p != '~') text.push_back(*p);
	return trimAscii(text);
}

int buttonCaptionWidth(const char *title) {
	return static_cast<int>(visibleButtonCaption(title).size());
}

std::string readCurrentWorkingDirectory() {
	char cwd[PATH_MAX];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr) return std::string();
	return std::string(cwd);
}

class TSetupDialogContentGroup : public TGroup {
  public:
	explicit TSetupDialogContentGroup(const TRect &bounds) : TGroup(bounds) {
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr color = owner != nullptr ? owner->mapColor(1) : mapColor(1);
		TView *child = first();

		buffer.moveChar(0, ' ', color, size.x);
		for (short y = 0; y < size.y; ++y)
			writeLine(0, y, size.x, 1, buffer);
		drawSubViews(child, nullptr);
	}
};

TView *deepestCurrentDialogView(TGroup *group) {
	TView *view = group != nullptr ? group->current : nullptr;

	while (view != nullptr) {
		TGroup *childGroup = dynamic_cast<TGroup *>(view);
		if (childGroup == nullptr || childGroup->current == nullptr) break;
		view = childGroup->current;
	}
	return view;
}

MRKeymapContext keymapContextForDialog(const MRScrollableDialog &dialog) {
	TView *view = deepestCurrentDialogView(dialog.managedContent());

	return dynamic_cast<TListViewer *>(view) != nullptr ? MRKeymapContext::DialogList : MRKeymapContext::Dialog;
}

} // namespace

TGroup *createSetupDialogContentGroup(const TRect &bounds) {
	return new TSetupDialogContentGroup(bounds);
}

MRDialogViewport::MRDialogViewport(TDialog &dialog, int virtualWidth, int virtualHeight, MRDialogViewportOwnership ownership)
    : mDialog(dialog), mVirtualWidth(virtualWidth), mVirtualHeight(virtualHeight), mContentRect(1, 1, dialog.size.x - 1, dialog.size.y - 1) {
	if (ownership == MRDialogViewportOwnership::ContentGroup) {
		mContent = createSetupDialogContentGroup(mContentRect);
		if (mContent != nullptr) {
			mContent->options |= ofSelectable;
			mDialog.insert(mContent);
		}
	}
}

void MRDialogViewport::addManaged(TView *view, const TRect &base) {
	ManagedItem item;

	if (view == nullptr) return;
	item.view = view;
	item.base = base;
	mManagedViews.push_back(item);
	if (mContent != nullptr) {
		TRect local = base;
		local.move(-mContentRect.a.x, -mContentRect.a.y);
		view->locate(local);
		mContent->insert(view);
	} else {
		TRect direct = base;
		view->locate(direct);
		if (view->owner == nullptr) mDialog.insert(view);
	}
}

void MRDialogViewport::removeManaged(TView *view) {
	if (view == nullptr) return;
	for (auto it = mManagedViews.begin(); it != mManagedViews.end(); ++it)
		if (it->view == view) {
			if (mContent != nullptr)
				mContent->remove(view);
			else
				mDialog.remove(view);
			mManagedViews.erase(it);
			return;
		}
}

void MRDialogViewport::selectContent() {
	if (mContent != nullptr) {
		mContent->resetCurrent();
		mContent->select();
	} else
		mDialog.resetCurrent();
}

void MRDialogViewport::scrollToOrigin() {
	if (mHScrollBar != nullptr) mHScrollBar->setValue(0);
	if (mVScrollBar != nullptr) mVScrollBar->setValue(0);
	applyScroll();
}

void MRDialogViewport::initScrollIfNeeded() {
	int virtualContentWidth = std::max(1, mVirtualWidth - 2);
	int virtualContentHeight = std::max(1, mVirtualHeight - 2);
	bool needH = false;
	bool needV = false;

	for (;;) {
		bool prevH = needH;
		bool prevV = needV;
		int viewportWidth = std::max(1, mDialog.size.x - 2);
		int viewportHeight = std::max(1, mDialog.size.y - 2);
		needH = virtualContentWidth > viewportWidth;
		needV = virtualContentHeight > viewportHeight;
		if (needH == prevH && needV == prevV) break;
	}

	mContentRect = TRect(1, 1, mDialog.size.x - 1, mDialog.size.y - 1);
	if (mContentRect.b.x <= mContentRect.a.x) mContentRect.b.x = mContentRect.a.x + 1;
	if (mContentRect.b.y <= mContentRect.a.y) mContentRect.b.y = mContentRect.a.y + 1;
	if (mContent != nullptr) mContent->locate(mContentRect);

	if (needH) {
		TRect hRect(1, mDialog.size.y - 1, mDialog.size.x - 1, mDialog.size.y);
		if (mHScrollBar == nullptr) {
			mHScrollBar = new TScrollBar(hRect);
			mDialog.insert(mHScrollBar);
		} else
			mHScrollBar->locate(hRect);
	}
	if (needV) {
		TRect vRect(mDialog.size.x - 1, 1, mDialog.size.x, mDialog.size.y - 1);
		if (mVScrollBar == nullptr) {
			mVScrollBar = new TScrollBar(vRect);
			mDialog.insert(mVScrollBar);
		} else
			mVScrollBar->locate(vRect);
	}
	if (mHScrollBar != nullptr) {
		int maxDx = std::max(0, virtualContentWidth - std::max(1, mContentRect.b.x - mContentRect.a.x));
		mHScrollBar->setParams(0, 0, maxDx, std::max(1, (mContentRect.b.x - mContentRect.a.x) / 2), 1);
	}
	if (mVScrollBar != nullptr) {
		int maxDy = std::max(0, virtualContentHeight - std::max(1, mContentRect.b.y - mContentRect.a.y));
		mVScrollBar->setParams(0, 0, maxDy, std::max(1, (mContentRect.b.y - mContentRect.a.y) / 2), 1);
	}
	if (mContent == nullptr) {
		if (mHScrollBar != nullptr) mHScrollBar->makeFirst();
		if (mVScrollBar != nullptr) mVScrollBar->makeFirst();
	}
	applyScroll();
}

void MRDialogViewport::drawChrome() {
	mDialog.clip = mDialog.getExtent();
	if (mDialog.frame != nullptr) {
		TView *next = mDialog.frame->nextView();
		const bool wasLast = mDialog.last == mDialog.frame;

		mDialog.removeView(mDialog.frame);
		mDialog.insertView(mDialog.frame, mDialog.first());
		mDialog.frame->draw();
		mDialog.removeView(mDialog.frame);
		mDialog.insertView(mDialog.frame, wasLast ? nullptr : next);
	}
	mDialog.clip = mContentRect;
	for (TView *view = mDialog.first(); view != nullptr; view = view->nextView())
		if (view != mDialog.frame && view != mHScrollBar && view != mVScrollBar) view->drawView();
	mDialog.clip = mDialog.getExtent();
	if (mHScrollBar != nullptr) mHScrollBar->drawView();
	if (mVScrollBar != nullptr) mVScrollBar->drawView();
	if (mDialog.buffer != nullptr) mDialog.writeBuf(0, 0, mDialog.size.x, mDialog.size.y, mDialog.buffer);
	mDialog.clip = mContentRect;
}

bool MRDialogViewport::handleNavigationEvent(TEvent &event) {
	TGroup *focusOwner = mContent != nullptr ? mContent : &mDialog;

	if (event.what != evKeyDown) return false;
	if (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) {
		focusOwner->selectNext(False);
		ensureCurrentVisible();
		event.what = evNothing;
		return true;
	}
	if (event.keyDown.keyCode == kbShiftTab) {
		focusOwner->selectNext(True);
		ensureCurrentVisible();
		event.what = evNothing;
		return true;
	}
	return false;
}

bool MRDialogViewport::handleScrollEvent(TEvent &event) {
	if (event.what != evBroadcast || event.message.command != cmScrollBarChanged || (event.message.infoPtr != mHScrollBar && event.message.infoPtr != mVScrollBar)) return false;
	applyScroll();
	event.what = evNothing;
	return true;
}

void MRDialogViewport::ensureCurrentVisible() {
	TView *view = mContent != nullptr ? mContent->current : mDialog.current;

	while (view != nullptr) {
		TGroup *group = dynamic_cast<TGroup *>(view);
		if (group == nullptr || group->current == nullptr) break;
		view = group->current;
	}
	ensureViewVisible(view);
}

void MRDialogViewport::ensureViewVisible(TView *view) {
	if (view == nullptr) return;
	for (const auto &managedView : mManagedViews)
		if (managedView.view == view) {
			int dx = mHScrollBar != nullptr ? mHScrollBar->value : 0;
			int dy = mVScrollBar != nullptr ? mVScrollBar->value : 0;
			int viewportWidth = std::max(1, mContentRect.b.x - mContentRect.a.x);
			int viewportHeight = std::max(1, mContentRect.b.y - mContentRect.a.y);
			int left = managedView.base.a.x - mContentRect.a.x;
			int right = managedView.base.b.x - mContentRect.a.x;
			int top = managedView.base.a.y - mContentRect.a.y;
			int bottom = managedView.base.b.y - mContentRect.a.y;

			if (mHScrollBar != nullptr) {
				if (right - left > viewportWidth) {
					if (left < dx || left >= dx + viewportWidth) mHScrollBar->setValue(std::max(0, left));
				} else if (left < dx)
					mHScrollBar->setValue(std::max(0, left));
				else if (right > dx + viewportWidth)
					mHScrollBar->setValue(std::max(0, right - viewportWidth));
			}
			if (mVScrollBar != nullptr) {
				if (bottom - top > viewportHeight) {
					if (top < dy || top >= dy + viewportHeight) mVScrollBar->setValue(std::max(0, top));
				} else if (top < dy)
					mVScrollBar->setValue(std::max(0, top));
				else if (bottom > dy + viewportHeight)
					mVScrollBar->setValue(std::max(0, bottom - viewportHeight));
			}
			applyScroll();
			return;
		}
}

void MRDialogViewport::applyScroll() {
	int dx = mHScrollBar != nullptr ? mHScrollBar->value : 0;
	int dy = mVScrollBar != nullptr ? mVScrollBar->value : 0;

	for (auto &managedView : mManagedViews) {
		TRect moved = managedView.base;
		moved.move(-dx, -dy);
		if (mContent != nullptr) moved.move(-mContentRect.a.x, -mContentRect.a.y);
		managedView.view->locate(moved);
	}
	if (mContent != nullptr)
		mContent->drawView();
	else {
		mDialog.drawView();
		drawChrome();
	}
}

MRScrollableDialog::MRScrollableDialog(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight) : MRScrollableDialog(bounds, title, virtualWidth, virtualHeight, initSetupDialogFrame) {
}

MRScrollableDialog::MRScrollableDialog(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight, TFrame *(*frameFactory)(TRect))
	: TWindowInit(frameFactory), TDialog(bounds, title), mViewport(*this, virtualWidth, virtualHeight, MRDialogViewportOwnership::ContentGroup) {
}

MRScrollableDialog::~MRScrollableDialog() {
	if (hasDialogValidationWarning) clearSetupDialogStatus();
}

void MRScrollableDialog::detectDoneButton(TView *view) {
	TButton *button = dynamic_cast<TButton *>(view);

	if (doneButton != nullptr || button == nullptr || !startsWithDoneButtonCaption(button->title)) return;
	doneButton = button;
}

void MRScrollableDialog::addManaged(TView *view, const TRect &base) {
	detectDoneButton(view);
	mViewport.addManaged(view, base);
}

void MRScrollableDialog::removeManaged(TView *view) {
	mViewport.removeManaged(view);
}

void MRScrollableDialog::selectContent() {
	mViewport.selectContent();
}

void MRScrollableDialog::scrollToOrigin() {
	mViewport.scrollToOrigin();
}

void MRScrollableDialog::setDialogValidationHook(DialogValidationHook hook) {
	dialogValidationHook = std::move(hook);
}

void MRScrollableDialog::runDialogValidation() {
	DialogValidationResult result;

	if (isRunningDialogValidation) return;
	isRunningDialogValidation = true;
	if (dialogValidationHook) result = dialogValidationHook();
	if (doneButton != nullptr) {
		const bool disableDone = !result.valid;
		const bool wasDisabled = (doneButton->state & sfDisabled) != 0;
		if (disableDone != wasDisabled) {
			doneButton->setState(sfDisabled, disableDone ? True : False);
			doneButton->drawView();
		}
	}
	if (result.valid) {
		if (hasDialogValidationWarning) {
			clearSetupDialogStatus();
			hasDialogValidationWarning = false;
			lastDialogValidationWarning.clear();
		}
	} else {
		std::string warningText = result.warningText.empty() ? "Dialog contains invalid values." : result.warningText;
		if (!hasDialogValidationWarning || warningText != lastDialogValidationWarning) {
			setSetupDialogStatus(warningText, result.error ? MRMenuBar::MarqueeKind::Error : MRMenuBar::MarqueeKind::Warning);
			hasDialogValidationWarning = true;
			lastDialogValidationWarning = warningText;
		}
	}
	isRunningDialogValidation = false;
}

void MRScrollableDialog::setDoneButtonDisabled(bool disable) {
	if (doneButton == nullptr) return;
	const bool wasDisabled = (doneButton->state & sfDisabled) != 0;
	if (wasDisabled == disable) return;
	doneButton->setState(sfDisabled, disable ? True : False);
	doneButton->drawView();
}

void MRScrollableDialog::initScrollIfNeeded() {
	mViewport.initScrollIfNeeded();
	runDialogValidation();
}

void MRScrollableDialog::handleEvent(TEvent &event) {
	const ushort originalWhat = event.what;

	if (mrHandleRuntimeKeymapEvent(event, keymapContextForDialog(*this), nullptr)) return;

	if (event.what == evKeyDown) {
		ushort keyCode = event.keyDown.keyCode;

		if (keyCode == kbEsc) {
			endModal(cmCancel);
			clearEvent(event);
			return;
		}
		if (mViewport.handleNavigationEvent(event)) return;
	}

	TDialog::handleEvent(event);
	if (mViewport.handleScrollEvent(event)) return;
	if (event.what == evKeyDown || event.what == evCommand || event.what == evMouseDown || event.what == evMouseUp) mViewport.ensureCurrentVisible();
	if (originalWhat == evCommand || originalWhat == evKeyDown || originalWhat == evMouseDown || originalWhat == evMouseUp) runDialogValidation();
}

MRDialogFoundation::MRDialogFoundation(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight) : MRDialogFoundation(bounds, title, virtualWidth, virtualHeight, initSetupDialogFrame) {
}

MRDialogFoundation::MRDialogFoundation(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight, TFrame *(*frameFactory)(TRect))
    : TWindowInit(frameFactory), MRScrollableDialog(bounds, title, virtualWidth, virtualHeight, frameFactory) {
}

void MRDialogFoundation::setState(ushort aState, Boolean enable) {
	MRScrollableDialog::setState(aState, enable);
	if ((aState & (sfFocused | sfSelected | sfActive)) != 0 && frame != nullptr) frame->drawView();
}

void MRDialogFoundation::insert(TView *view) {
	if (view == nullptr) return;
	if (view == managedContent()) {
		TDialog::insert(view);
		return;
	}
	addManaged(view, view->getBounds());
}

void MRDialogFoundation::finalizeLayout() {
	TGroup *content = managedContent();
	if (mLayoutFinalized) return;
	initScrollIfNeeded();
	if (content != nullptr && content->current == nullptr) selectContent();
	mLayoutFinalized = true;
}

TRect centeredSetupDialogRect(int width, int height) {
	TRect r = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	int availableWidth = std::max(1, r.b.x - r.a.x);
	int availableHeight = std::max(1, r.b.y - r.a.y);
	int safeWidth = std::max(10, std::min(width, availableWidth));
	int safeHeight = std::max(6, std::min(height, availableHeight));
	int left = r.a.x + std::max(0, (availableWidth - safeWidth) / 2);
	int top = r.a.y + std::max(0, (availableHeight - safeHeight) / 2);

	return TRect(left, top, left + safeWidth, top + safeHeight);
}

namespace mr::dialogs {

TRect centeredDialogRect(int width, int height) {
	return centeredSetupDialogRect(width, height);
}

DialogButtonRowMetrics measureUniformButtonRow(std::span<const DialogButtonSpec> specs, int gap, int minButtonWidth) {
	DialogButtonRowMetrics metrics;

	if (specs.empty()) return metrics;
	for (const DialogButtonSpec &spec : specs)
		metrics.buttonWidth = std::max(metrics.buttonWidth, buttonCaptionWidth(spec.title) + 4);
	metrics.buttonWidth = std::max(metrics.buttonWidth, minButtonWidth);
	metrics.rowWidth = static_cast<int>(specs.size()) * metrics.buttonWidth + static_cast<int>(specs.size() - 1) * gap;
	return metrics;
}

void insertUniformButtonRow(MRDialogFoundation &dialog, int left, int top, int gap, std::span<const DialogButtonSpec> specs, int minButtonWidth, std::vector<TButton *> *outButtons) {
	const DialogButtonRowMetrics metrics = measureUniformButtonRow(specs, gap, minButtonWidth);
	int x = left;

	for (const DialogButtonSpec &spec : specs) {
		TRect rect(x, top, x + metrics.buttonWidth, top + 2);
		TButton *button = new TButton(rect, spec.title, spec.command, spec.flags);
		dialog.insert(button);
		if (outButtons != nullptr) outButtons->push_back(button);
		x += metrics.buttonWidth + gap;
	}
}

void addManagedUniformButtonRow(MRScrollableDialog &dialog, int left, int top, int gap, std::span<const DialogButtonSpec> specs, int minButtonWidth, std::vector<TButton *> *outButtons) {
	const DialogButtonRowMetrics metrics = measureUniformButtonRow(specs, gap, minButtonWidth);
	int x = left;

	for (const DialogButtonSpec &spec : specs) {
		TRect rect(x, top, x + metrics.buttonWidth, top + 2);
		TButton *button = new TButton(rect, spec.title, spec.command, spec.flags);
		dialog.addManaged(button, rect);
		if (outButtons != nullptr) outButtons->push_back(button);
		x += metrics.buttonWidth + gap;
	}
}

MRDialogFoundation *createScrollableDialog(const char *title, int virtualWidth, int virtualHeight) {
	return new MRDialogFoundation(centeredDialogRect(virtualWidth, virtualHeight), title, virtualWidth, virtualHeight);
}

ushort execTextInputDialog(const char *title, const char *label, char *buffer, std::size_t limit) {
	constexpr int kVirtualWidth = 60;
	constexpr int kVirtualHeight = 10;
	const std::size_t safeLimit = std::min(limit, static_cast<std::size_t>(std::numeric_limits<short>::max()));
	MRDialogFoundation *dialog = createScrollableDialog(title != nullptr ? title : "INPUT", kVirtualWidth, kVirtualHeight);
	TInputLine *input = new TInputLine(TRect(3, 4, kVirtualWidth - 3, 5), static_cast<int>(safeLimit));
	const std::array buttons{DialogButtonSpec{"~O~K", cmOK, bfDefault}, DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
	const DialogButtonRowMetrics metrics = measureUniformButtonRow(buttons, 2);

	dialog->insert(new TLabel(TRect(3, 2, kVirtualWidth - 3, 3), label != nullptr ? label : "Value:", input));
	dialog->insert(input);
	insertUniformButtonRow(*dialog, (kVirtualWidth - metrics.rowWidth) / 2, 6, 2, buttons);
	return execDialogWithData(dialog, buffer);
}

TFileDialog *createFileDialog(MRDialogHistoryScope scope, const char *wildCard, const char *title, const char *inputName, ushort options) {
	return mr::ui::createScopedFileDialog(scope, wildCard, title, inputName, options);
}

TDialog *createDirectoryDialog(MRDialogHistoryScope scope, ushort options) {
	return mr::ui::createScopedDirectoryDialog(scope, options);
}

void seedFileDialogPath(MRDialogHistoryScope scope, char *buffer, std::size_t bufferSize, const char *pattern) {
	const char *safePattern = pattern != nullptr && *pattern != '\0' ? pattern : "*.*";

	writeRecordField(buffer, bufferSize, "");
	initRememberedLoadDialogPath(scope, buffer, bufferSize, safePattern);
}

void suggestFileDialogName(char *buffer, std::size_t bufferSize, std::string_view suggestedValue) {
	const std::string fileName = dialogSeedFileName(suggestedValue);
	const std::string seeded = readRecordField(buffer);
	std::string directory;

	if (fileName.empty()) return;
	if (pathIsDirectory(seeded)) directory = normalizeConfiguredPathInput(seeded);
	else
		directory = parentDirectoryOfPath(seeded);
	if (!directory.empty()) {
		if (directory.back() != '/') directory += '/';
		writeRecordField(buffer, bufferSize, directory + fileName);
	} else
		writeRecordField(buffer, bufferSize, fileName);
}

ushort execRememberingFileDialogWithData(MRDialogHistoryScope scope, const char *wildCard, const char *title, const char *inputName, ushort options, char *buffer) {
	std::string originalCwd = readCurrentWorkingDirectory();
	std::string seedDirectory = resolveFileDialogSeedDirectory(scope, buffer);

	if (!seedDirectory.empty()) (void)::chdir(seedDirectory.c_str());
	const ushort result = execDialogWithData(createFileDialog(scope, wildCard, title, inputName, options), buffer);
	if (!originalCwd.empty()) (void)::chdir(originalCwd.c_str());

	if (result != cmCancel && !deferRememberingLoadDialogPath(scope)) rememberLoadDialogPath(scope, buffer);
	return result;
}

ushort execDialog(TDialog *dialog) {
	ushort result = cmCancel;
	MRDialogFoundation *foundation = nullptr;
	MRScrollableDialog *scrollable = nullptr;

	if (dialog == nullptr || TProgram::deskTop == nullptr) return cmCancel;
	foundation = dynamic_cast<MRDialogFoundation *>(dialog);
	if (foundation != nullptr) foundation->finalizeLayout();
	else {
		scrollable = dynamic_cast<MRScrollableDialog *>(dialog);
		if (scrollable != nullptr) scrollable->initScrollIfNeeded();
	}
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	return result;
}

ushort execDialogWithData(TDialog *dialog, void *data) {
	ushort result = cmCancel;
	MRDialogFoundation *foundation = nullptr;
	MRScrollableDialog *scrollable = nullptr;
	if (dialog == nullptr || TProgram::deskTop == nullptr) return cmCancel;
	if (data != nullptr) dialog->setData(data);
	foundation = dynamic_cast<MRDialogFoundation *>(dialog);
	if (foundation != nullptr) foundation->finalizeLayout();
	else {
		scrollable = dynamic_cast<MRScrollableDialog *>(dialog);
		if (scrollable != nullptr) scrollable->initScrollIfNeeded();
	}
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel && data != nullptr) dialog->getData(data);
	TObject::destroy(dialog);
	return result;
}

} // namespace mr::dialogs
