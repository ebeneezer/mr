#define Uses_TChDirDialog
#define Uses_TButton
#define Uses_TDialog
#define Uses_TFileDialog
#define Uses_TFileInputLine
#define Uses_TFileInfoPane
#define Uses_TFileList
#define Uses_THistory
#define Uses_TInputLine
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TObject
#define Uses_TRect
#define Uses_TView
#include <tvision/tv.h>
#include <tvision/compat/borland/dos.h>
#include <tvision/compat/borland/io.h>

#include "MRDropList.hpp"
#include "MRScopedHistoryUI.hpp"

#include "../MRFrame.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../app/MRHelpTopics.generated.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
enum : ushort {
	cmMrScopedHistoryChoose = 3868,
	cmMrScopedHistoryAccept,
	cmMrFileDialogToggleHidden = 3872
};

TFrame *initScopedHistoryDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

ushort fileDialogHelpContext(MRDialogHistoryScope scope) noexcept {
	switch (scope) {
		case MRDialogHistoryScope::General:
			return hcDialogCompilerFile;
		case MRDialogHistoryScope::WorkspaceLoad:
		case MRDialogHistoryScope::WorkspaceSave:
			return hcDialogWorkspaceFile;
		case MRDialogHistoryScope::SetupThemeLoad:
		case MRDialogHistoryScope::SetupThemeSave:
		case MRDialogHistoryScope::ExtensionThemeFile:
			return hcDialogColorThemeFile;
		default:
			return hcDialogFileChooser;
	}
}

const char *const kMonthNames[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string humanReadableFileSize(long size) {
	if (size < 0) size = 0;
	if (size < 1024) return std::to_string(size) + "b";
	if (size < 1024 * 1024) return std::to_string(size / 1024) + "k";
	if (size < 1024 * 1024 * 1024) return std::to_string(size / (1024 * 1024)) + "M";
	return std::to_string(size / (1024 * 1024 * 1024)) + "G";
}

std::string middleEllipsized(std::string_view text, std::size_t maxWidth) {
	if (text.size() <= maxWidth) return std::string(text);
	if (maxWidth <= 3) return std::string(text.substr(0, maxWidth));
	const std::size_t leftCount = (maxWidth - 3) / 2;
	const std::size_t rightCount = maxWidth - 3 - leftCount;
	return std::string(text.substr(0, leftCount)) + "..." + std::string(text.substr(text.size() - rightCount));
}

std::string fileDateTimeText(const TSearchRec &file) {
	const struct ftime *time = reinterpret_cast<const struct ftime *>(&file.time);
	const int monthIndex = std::clamp<int>(time->ft_month - 1, 0, 11);
	int hour = time->ft_hour;
	const bool pm = hour >= 12;
	hour %= 12;
	if (hour == 0) hour = 12;

	char buffer[64] = {0};
	std::snprintf(buffer, sizeof(buffer), "%s %02d,%04d %02d:%02d%s", kMonthNames[monthIndex], time->ft_day, time->ft_year + 1980, hour, time->ft_min, pm ? "p" : "a");
	return std::string(buffer);
}

void adoptNativeDialogControls(TDialog &dialog, MRDialogViewport &viewport) {
	struct NativeControl {
		TView *view;
		TRect bounds;
	};

	const int virtualWidth = dialog.size.x;
	const int virtualHeight = dialog.size.y;
	TView *selectedView = dialog.current;
	std::vector<NativeControl> controls;

	for (TView *child = dialog.first(); child != nullptr; child = child->nextView())
		if (child != dialog.frame) controls.push_back(NativeControl{child, child->getBounds()});

	TRect bounds = centeredSetupDialogRect(virtualWidth, virtualHeight);
	dialog.locate(bounds);
	for (const NativeControl &control : controls)
		viewport.addManaged(control.view, control.bounds);
	viewport.initScrollIfNeeded();
	viewport.selectContent();
	if (selectedView != nullptr) selectedView->select();
	viewport.ensureCurrentVisible();
}

class TScopedFileInfoPane final : public TView {
 public:
	TScopedFileInfoPane(const TRect &bounds) noexcept : TView(bounds) {
		eventMask |= evBroadcast;
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr color = getColor(0x01);
		TFileDialog *dialog = dynamic_cast<TFileDialog *>(owner);

		buffer.moveChar(0, ' ', color, static_cast<ushort>(size.x));
		if (dialog != nullptr) {
			char path[MAXPATH] = {0};
			std::size_t copied = strnzcpy(path, dialog->directory != nullptr ? dialog->directory : "", MAXPATH);
			strnzcpy(path + copied, dialog->wildCard, MAXPATH - copied);
			fexpand(path);
			buffer.moveStr(1, path, color, static_cast<ushort>(std::max(0, size.x - 1)));
		}
		writeLine(0, 0, static_cast<ushort>(size.x), 1, buffer);

		buffer.moveChar(0, ' ', color, static_cast<ushort>(size.x));
		if (fileBlock.name[0] != '\0') {
			const std::string sizeText = humanReadableFileSize(fileBlock.size);
			const std::string dateTimeText = fileDateTimeText(fileBlock);
			const std::string rightText = sizeText + "  " + dateTimeText;
			const int rightStart = std::max(1, size.x - 1 - static_cast<int>(rightText.size()));
			const int nameWidth = std::max(0, rightStart - 2);
			const std::string nameText = middleEllipsized(fileBlock.name, static_cast<std::size_t>(nameWidth));

			if (!nameText.empty()) buffer.moveStr(1, nameText.c_str(), color, static_cast<ushort>(std::max(0, rightStart - 1)));
			buffer.moveStr(static_cast<short>(rightStart), rightText.c_str(), color, static_cast<ushort>(std::max(0, size.x - rightStart)));
		}
		writeLine(0, 1, static_cast<ushort>(size.x), 1, buffer);

		buffer.moveChar(0, ' ', color, static_cast<ushort>(size.x));
		writeLine(0, 2, static_cast<ushort>(size.x), static_cast<ushort>(std::max(0, size.y - 2)), buffer);
	}

	void handleEvent(TEvent &event) override {
		TView::handleEvent(event);
		if (event.what == evBroadcast && event.message.command == cmFileFocused) {
			fileBlock = *static_cast<TSearchRec *>(event.message.infoPtr);
			drawView();
		}
	}

 private:
	TSearchRec fileBlock = {};
};

class TFileDialogEnterInterceptor final : public TView {
 public:
	TFileDialogEnterInterceptor(TInputLine *aFileName) noexcept : TView(TRect(0, 0, 0, 0)), fileName(aFileName) {
		options |= ofPreProcess;
		eventMask |= evKeyDown;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown && event.keyDown.keyCode == kbEnter && fileName != nullptr && (fileName->state & sfFocused) != 0) {
			TEvent commandEvent;
			std::memset(&commandEvent, 0, sizeof(commandEvent));
			commandEvent.what = evCommand;
			commandEvent.message.command = cmFileOpen;
			putEvent(commandEvent);
			clearEvent(event);
			return;
		}
		TView::handleEvent(event);
	}

 private:
	TInputLine *fileName = nullptr;
};

class TFileDialogToggleButton final : public TButton {
 public:
	TFileDialogToggleButton(const TRect &bounds, ushort command, bool active) noexcept : TButton(bounds, "Hidden", command, bfNormal), active(active) {}

	void draw() override {
		drawState(active ? True : False);
	}

	void setActive(bool value) {
		if (active == value) return;
		active = value;
		drawView();
	}

 private:
	bool active = false;
};

class TWheelFileDialog final : public TFileDialog {
 public:
	TWheelFileDialog(MRDialogHistoryScope aScope, const char *wildCard, const char *title, const char *inputName, ushort options) noexcept
	    : TWindowInit(initScopedHistoryDialogFrame), TFileDialog(wildCard, title, inputName, options | fdHelpButton, 0), viewport(*this, size.x, size.y, MRDialogViewportOwnership::Dialog), scope(aScope), dialogOptions(options) {
		helpCtx = fileDialogHelpContext(scope);
		insert(new TFileDialogEnterInterceptor(fileName));
		replaceHistoryView(static_cast<TInputLine *>(fileName));
		replaceInfoPane();
		removeFileMenuCancelButton();
		installHiddenButton();
		appendConfiguredHiddenEntries();
		adoptNativeDialogControls(*this, viewport);
	}

	void draw() override {
		TFileDialog::draw();
		viewport.drawChrome();
	}

	void handleEvent(TEvent &event) override {
		const ushort originalWhat = event.what;
		std::vector<std::string> entries;
		configuredScopedDialogFileHistoryEntries(scope, entries);
		if (historyLink != nullptr) {
			TRect bounds = historyLink->getBounds();
			short visibleRows = scopedHistoryVisibleRows(bounds);

			bounds.b.x++;
			if (historyDropList.handleLinkedInputEvent(event, *this, bounds, entries, historyLink, this, cmMrScopedHistoryAccept, visibleRows)) return;
		}
		if (event.what == evCommand && event.message.command == cmMrScopedHistoryChoose) {
			toggleHistoryList();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrScopedHistoryAccept) {
			if (acceptHistorySelection() && (dialogOptions & fdOpenButton) != 0) {
				TEvent commandEvent;
				std::memset(&commandEvent, 0, sizeof(commandEvent));
				commandEvent.what = evCommand;
				commandEvent.message.command = cmOK;
				putEvent(commandEvent);
			}
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrFileDialogToggleHidden) {
			const bool active = !configuredFileDialogShowHiddenFiles();

			static_cast<void>(setConfiguredFileDialogShowHiddenFiles(active));
			if (hiddenButton != nullptr) hiddenButton->setActive(active);
			if (fileList != nullptr && directory != nullptr) {
				fileList->readDirectory(directory, wildCard);
				appendConfiguredHiddenEntries();
			}
			clearEvent(event);
			return;
		}
		if (event.what == evBroadcast && event.message.command == cmFileDoubleClicked && (dialogOptions & fdOpenButton) != 0) {
			TSearchRec *selectedEntry = static_cast<TSearchRec *>(event.message.infoPtr);

			if (selectedEntry != nullptr) {
				std::string selectedPath = selectedEntry->name;

				if ((selectedEntry->attr & FA_DIREC) != 0) {
					selectedPath += "\\";
					selectedPath += wildCard;
				}
				strnzcpy(fileName->data, selectedPath.c_str(), MAXPATH);
				fileName->selectAll(False);
				fileName->drawView();
			}
		}
		if (event.what == evMouseWheel && fileList != nullptr && fileList->containsMouse(event) && fileList->range > 0) {
			const int delta = event.mouse.wheel == mwUp || event.mouse.wheel == mwLeft ? -1 : 1;
			const short next = static_cast<short>(std::clamp<int>(fileList->focused + delta, 0, fileList->range - 1));

			fileList->focusItemNum(next);
			clearEvent(event);
			return;
		}
		if (viewport.handleNavigationEvent(event)) return;
		TFileDialog::handleEvent(event);
		if (viewport.handleScrollEvent(event)) return;
		if (originalWhat == evKeyDown || originalWhat == evCommand || originalWhat == evMouseDown || originalWhat == evMouseUp) viewport.ensureCurrentVisible();
	}

	void sizeLimits(TPoint &min, TPoint &max) override {
		TDialog::sizeLimits(min, max);
	}

	Boolean valid(ushort command) override {
		const std::string previousDirectory = directory != nullptr ? directory : "";
		TFileCollection *previousEntries = fileList != nullptr ? fileList->list() : nullptr;

		if ((dialogOptions & fdOpenButton) != 0 && command == cmFileOpen && fileName != nullptr) {
			std::string rawInput = fileName->data != nullptr ? fileName->data : "";

			while (!rawInput.empty() && (rawInput.front() == ' ' || rawInput.front() == '\t' || rawInput.front() == '\r' || rawInput.front() == '\n'))
				rawInput.erase(rawInput.begin());
			while (!rawInput.empty() && (rawInput.back() == ' ' || rawInput.back() == '\t' || rawInput.back() == '\r' || rawInput.back() == '\n'))
				rawInput.pop_back();

			if (!rawInput.empty() && rawInput[0] == '~') {
				const char *home = std::getenv("HOME");
				std::string expandedInput = rawInput;

				if (home != nullptr && *home != '\0') {
					if (rawInput == "~") expandedInput = home;
					else if (rawInput.size() >= 2 && rawInput[1] == '/') expandedInput = std::string(home) + rawInput.substr(1);
				}
				if (expandedInput != rawInput) {
					const std::string normalizedInput = normalizeConfiguredPathInput(expandedInput);

					if (!normalizedInput.empty()) strnzcpy(fileName->data, normalizedInput.c_str(), MAXPATH);
				}
			}
		}

		const Boolean result = TFileDialog::valid(command);
		if (fileList != nullptr && fileList->list() != previousEntries) appendConfiguredHiddenEntries();
		if ((dialogOptions & fdOpenButton) != 0 && command == cmFileOpen && result == False) {
			const std::string currentDirectory = directory != nullptr ? directory : "";

			if (!currentDirectory.empty() && previousDirectory != currentDirectory) rememberLoadDialogPath(scope, currentDirectory.c_str());
		}
		return result;
	}

 private:
	void installHiddenButton() {
		TButton *primary = nullptr;
		TRect primaryBounds;
		std::vector<TButton *> following;

		for (TView *child = first(); child != nullptr; child = child->nextView()) {
			TButton *button = dynamic_cast<TButton *>(child);
			if (button == nullptr) continue;
			const TRect bounds = button->getBounds();
			if (primary == nullptr || bounds.a.y < primaryBounds.a.y) {
				primary = button;
				primaryBounds = bounds;
			}
		}
		if (primary == nullptr) return;

		for (TView *child = first(); child != nullptr; child = child->nextView()) {
			TButton *button = dynamic_cast<TButton *>(child);
			if (button != nullptr && button != primary && button->getBounds().a.y > primaryBounds.a.y) following.push_back(button);
		}
		std::sort(following.begin(), following.end(), [](const TButton *left, const TButton *right) {
			return left->getBounds().a.y < right->getBounds().a.y;
		});

		TRect hiddenBounds = primaryBounds;
		hiddenBounds.a.y += 3;
		hiddenBounds.b.y += 3;
		short nextTop = static_cast<short>(hiddenBounds.a.y + 3);
		for (TButton *button : following) {
			TRect bounds = button->getBounds();
			if (bounds.a.y < nextTop) {
				const short delta = static_cast<short>(nextTop - bounds.a.y);
				bounds.a.y += delta;
				bounds.b.y += delta;
				button->locate(bounds);
			}
			nextTop = static_cast<short>(bounds.a.y + 3);
		}

		hiddenButton = new TFileDialogToggleButton(hiddenBounds, cmMrFileDialogToggleHidden, configuredFileDialogShowHiddenFiles());
		hiddenButton->growMode = primary->growMode;
		insert(hiddenButton);
	}

	void appendConfiguredHiddenEntries() {
		TFileCollection *entries;
		ffblk search = {};
		char path[MAXPATH] = {0};
		bool inserted = false;

		if (!configuredFileDialogShowHiddenFiles() || fileList == nullptr || directory == nullptr) return;
		entries = fileList->list();
		if (entries == nullptr) return;

		std::size_t copied = strnzcpy(path, directory, MAXPATH);
		strnzcpy(path + copied, wildCard, MAXPATH - copied);
		int result = findfirst(path, &search, FA_RDONLY | FA_ARCH | FA_HIDDEN);
		while (result == 0) {
			if ((search.ff_attrib & FA_HIDDEN) != 0 && (search.ff_attrib & FA_DIREC) == 0) {
				TSearchRec *entry = new TSearchRec;

				entry->attr = static_cast<uchar>(search.ff_attrib);
				entry->time = static_cast<std::int32_t>((static_cast<std::uint32_t>(search.ff_fdate) << 16) | search.ff_ftime);
				entry->size = search.ff_fsize;
				strnzcpy(entry->name, search.ff_name, sizeof(entry->name));
				entries->insert(entry);
				inserted = true;
			}
			result = findnext(&search);
		}

		copied = strnzcpy(path, directory, MAXPATH);
		strnzcpy(path + copied, "*.*", MAXPATH - copied);
		search = {};
		result = findfirst(path, &search, FA_DIREC | FA_HIDDEN);
		while (result == 0) {
			const bool hiddenDirectory = (search.ff_attrib & (FA_DIREC | FA_HIDDEN)) == (FA_DIREC | FA_HIDDEN);
			if (hiddenDirectory && std::strcmp(search.ff_name, ".") != 0 && std::strcmp(search.ff_name, "..") != 0) {
				TSearchRec *entry = new TSearchRec;

				entry->attr = static_cast<uchar>(search.ff_attrib);
				entry->time = static_cast<std::int32_t>((static_cast<std::uint32_t>(search.ff_fdate) << 16) | search.ff_ftime);
				entry->size = search.ff_fsize;
				strnzcpy(entry->name, search.ff_name, sizeof(entry->name));
				entries->insert(entry);
				inserted = true;
			}
			result = findnext(&search);
		}

		if (inserted) {
			fileList->setRange(entries->getCount());
			fileList->focusItem(0);
			fileList->drawView();
		}
	}

	void toggleHistoryList() {
		std::vector<std::string> entries;
		TRect bounds;

		if (historyLink == nullptr) return;
		configuredScopedDialogFileHistoryEntries(scope, entries);
		if (entries.empty()) return;
		bounds = historyLink->getBounds();
		bounds.b.x++;
		historyDropList.toggle(*this, bounds, entries, std::string(historyLink->data), this, cmMrScopedHistoryAccept, scopedHistoryVisibleRows(bounds));
	}

	short scopedHistoryVisibleRows(const TRect &bounds) const {
		short visibleRows = 7;
		if (visibleRows > size.y - bounds.a.y - 1) visibleRows = static_cast<short>(size.y - bounds.a.y - 1);
		if (visibleRows < 1) visibleRows = 1;
		return visibleRows;
	}

	void hideHistoryList() {
		historyDropList.hide();
		if (historyLink != nullptr) historyLink->selectAll(True);
	}

	bool acceptHistorySelection() {
		std::string value;

		if (!historyDropList.acceptSelection(value) || historyLink == nullptr) return false;
		strnzcpy(historyLink->data, value.c_str(), historyLink->maxLen + 1);
		historyLink->selectAll(True);
		historyLink->drawView();
		return true;
	}

	void replaceHistoryView(TInputLine *link) {
		if (link == nullptr) return;
		for (TView *child = first(); child != nullptr;) {
			TView *next = child->nextView();
			if (dynamic_cast<THistory *>(child) != nullptr) {
				const TRect childBounds = child->getBounds();
				const ushort childGrowMode = child->growMode;
				remove(child);
				TObject::destroy(child);
				TView *history = historyDropList.createButton(*this, childBounds, link, this, cmMrScopedHistoryChoose, true);
				history->growMode = childGrowMode;
				historyLink = link;
				return;
			}
			child = next;
		}
	}

	void replaceInfoPane() {
		for (TView *child = first(); child != nullptr;) {
			TView *next = child->nextView();
			if (dynamic_cast<TFileInfoPane *>(child) != nullptr) {
				const TRect childBounds = child->getBounds();
				const ushort childGrowMode = child->growMode;
				remove(child);
				TObject::destroy(child);
				TView *pane = new TScopedFileInfoPane(childBounds);
				insert(pane);
				pane->growMode = childGrowMode;
				return;
			}
			child = next;
		}
	}

	void removeFileMenuCancelButton() {
		switch (scope) {
			case MRDialogHistoryScope::EditorSaveAs:
			case MRDialogHistoryScope::LiveLogOpen:
			case MRDialogHistoryScope::LoadFile:
			case MRDialogHistoryScope::SaveLogAs:
			case MRDialogHistoryScope::WorkspaceLoad:
			case MRDialogHistoryScope::WorkspaceSave:
			case MRDialogHistoryScope::PdfExport:
				break;
			default:
				return;
		}
		for (TView *child = first(); child != nullptr;) {
			TView *next = child->nextView();
			TButton *button = dynamic_cast<TButton *>(child);
			if (button != nullptr && button->title != nullptr && std::strcmp(button->title, "Cancel") == 0) {
				remove(child);
				TObject::destroy(child);
				return;
			}
			child = next;
		}
	}

	MRDialogViewport viewport;
	MRDialogHistoryScope scope;
	ushort dialogOptions = 0;
	TFileDialogToggleButton *hiddenButton = nullptr;
	TInputLine *historyLink = nullptr;
	MRDropList historyDropList;
};

class TWheelChDirDialog final : public TChDirDialog {
 public:
	TWheelChDirDialog(MRDialogHistoryScope aScope, ushort options) noexcept
	    : TWindowInit(initScopedHistoryDialogFrame), TChDirDialog(options | cdHelpButton, 0), viewport(*this, size.x, size.y, MRDialogViewportOwnership::Dialog), scope(aScope) {
		helpCtx = hcDialogDirectoryChooser;
		replaceHistoryView(findInputLine(TRect(3, 3, 42, 4)));
		adoptNativeDialogControls(*this, viewport);
	}

	void draw() override {
		TChDirDialog::draw();
		viewport.drawChrome();
	}

	const char *getTitle(short) override {
		return "CHANGE DIRECTORY";
	}

	void handleEvent(TEvent &event) override {
		const ushort originalWhat = event.what;
		std::vector<std::string> entries;
		configuredScopedDialogPathHistoryEntries(scope, entries);
		if (historyLink != nullptr) {
			TRect bounds = historyLink->getBounds();
			short visibleRows = scopedHistoryVisibleRows(bounds);

			bounds.b.x++;
			if (historyDropList.handleLinkedInputEvent(event, *this, bounds, entries, historyLink, this, cmMrScopedHistoryAccept, visibleRows)) return;
		}
		if (event.what == evCommand && event.message.command == cmMrScopedHistoryChoose) {
			toggleHistoryList();
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrScopedHistoryAccept) {
			acceptHistorySelection();
			clearEvent(event);
			return;
		}
		if (viewport.handleNavigationEvent(event)) return;
		TChDirDialog::handleEvent(event);
		if (viewport.handleScrollEvent(event)) return;
		if (originalWhat == evKeyDown || originalWhat == evCommand || originalWhat == evMouseDown || originalWhat == evMouseUp) viewport.ensureCurrentVisible();
	}

	void sizeLimits(TPoint &min, TPoint &max) override {
		TDialog::sizeLimits(min, max);
	}

  private:
	TInputLine *findInputLine(const TRect &bounds) {
		for (TView *child = first(); child != nullptr; child = child->nextView())
			if (child->getBounds() == bounds) return dynamic_cast<TInputLine *>(child);
		return nullptr;
	}

	void toggleHistoryList() {
		std::vector<std::string> entries;
		TRect bounds;

		if (historyLink == nullptr) return;
		configuredScopedDialogPathHistoryEntries(scope, entries);
		if (entries.empty()) return;
		bounds = historyLink->getBounds();
		bounds.b.x++;
		historyDropList.toggle(*this, bounds, entries, std::string(historyLink->data), this, cmMrScopedHistoryAccept, scopedHistoryVisibleRows(bounds));
	}

	short scopedHistoryVisibleRows(const TRect &bounds) const {
		short visibleRows = 7;
		if (visibleRows > size.y - bounds.a.y - 1) visibleRows = static_cast<short>(size.y - bounds.a.y - 1);
		if (visibleRows < 1) visibleRows = 1;
		return visibleRows;
	}

	void hideHistoryList() {
		historyDropList.hide();
		if (historyLink != nullptr) historyLink->selectAll(True);
	}

	void acceptHistorySelection() {
		std::string value;

		if (!historyDropList.acceptSelection(value) || historyLink == nullptr) return;
		strnzcpy(historyLink->data, value.c_str(), historyLink->maxLen + 1);
		historyLink->selectAll(True);
		historyLink->drawView();
	}

	void replaceHistoryView(TInputLine *link) {
		if (link == nullptr) return;
		for (TView *child = first(); child != nullptr;) {
			TView *next = child->nextView();
			if (dynamic_cast<THistory *>(child) != nullptr) {
				const TRect childBounds = child->getBounds();
				const ushort childGrowMode = child->growMode;
				remove(child);
				TObject::destroy(child);
				TView *history = historyDropList.createButton(*this, childBounds, link, this, cmMrScopedHistoryChoose, true);
				history->growMode = childGrowMode;
				historyLink = link;
				return;
			}
			child = next;
		}
	}

	MRDialogViewport viewport;
	MRDialogHistoryScope scope;
	TInputLine *historyLink = nullptr;
	MRDropList historyDropList;
};

} // namespace

namespace mr::ui {

TFileDialog *createScopedFileDialog(MRDialogHistoryScope scope, const char *wildCard, const char *title, const char *inputName, unsigned short options) {
	return new TWheelFileDialog(scope, wildCard, title, inputName, options);
}

TDialog *createScopedDirectoryDialog(MRDialogHistoryScope scope, unsigned short options) {
	return new TWheelChDirDialog(scope, options);
}
} // namespace mr::ui
