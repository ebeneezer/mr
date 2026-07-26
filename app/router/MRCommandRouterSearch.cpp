#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TObject
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TButton
#define Uses_TFileDialog
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TListViewer
#define Uses_TDrawBuffer
#define Uses_MsgBox
#define Uses_TCheckBoxes
#define Uses_TRadioButtons
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TClipboard
#define Uses_THardwareInfo
#define Uses_TSItem
#include <tvision/tv.h>

#include "MRCommandRouterSearch.hpp"
#include "MRCommandRouterSearchCore.hpp"
#include "MRCommandRouterSearchDialogs.hpp"
#include "MRCommandRouterSearchMultiFile.hpp"
#include "../MRCommandRouter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fnmatch.h>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../dialogs/MRFileInformation.hpp"
#include "../../dialogs/MRAbout.hpp"
#include "../../dialogs/setup/MRSetup.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../utils/MRFileIOUtils.hpp"
#include "../utils/MRStringUtils.hpp"
#include "../../keymap/MRKeymapActionCatalog.hpp"
#include "../../keymap/MRKeymapSequence.hpp"
#include "../../mrmac/MRMacroRunner.hpp"
#include "../../mrmac/MRVM.hpp"
#include "../commands/MRExternalCommand.hpp"
#include "../commands/MRFileCommands.hpp"
#include "../commands/MRWindowCommands.hpp"
#include "../../dialogs/MRMacroFile.hpp"
#include "../../dialogs/MRWindowList.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRMenuBar.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../MREditorApp.hpp"
#include "../MRCommands.hpp"

namespace {
struct SearchUiState {
	bool hasPrevious = false;
	bool hasAcceptedPattern = false;
	std::string pattern;
	std::string acceptedPattern;
	std::string replacement;
	std::size_t lastStart = 0;
	std::size_t lastEnd = 0;
	MRSearchDialogOptions acceptedOptions;
	MRSearchDialogOptions lastOptions;
};

SearchUiState g_searchUiState;

enum class SearchResultsContextKind : unsigned char {
	None = 0,
	SingleFile = 1,
	MultiFile = 2
};

struct SearchResultsContext {
	SearchResultsContextKind kind = SearchResultsContextKind::None;
	MREditWindow *window = nullptr;
};

SearchResultsContext g_searchResultsContext;

struct PendingTransientSelectionClear {
	bool active = false;
	std::string normalizedPath;
	std::size_t start = 0;
	std::size_t end = 0;
};

PendingTransientSelectionClear g_pendingTransientSelectionClear;

std::string normalizedSearchPath(const std::filesystem::path &path) {
	std::error_code ec;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);

	if (ec || normalized.empty()) {
		ec.clear();
		normalized = std::filesystem::absolute(path, ec);
	}
	if (ec || normalized.empty()) normalized = path.lexically_normal();
	std::string result = normalized.lexically_normal().string();
	for (char &ch : result)
		if (ch == '\\') ch = '/';
	return result;
}

const char *wrappedSearchMessage(MRSearchDirection direction) {
	return direction == MRSearchDirection::Backward ? "search wrapped to EOF" : "search wrapped to TOF";
}

constexpr const char *kSearchTextRequiredMessage = "Search text must not be empty.";
constexpr const char *kNoMarkedBlockSelectedMessage = "No marked block selected.";
constexpr const char *kNoPreviousSearchMessage = "No previous search.";

void postSearchWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text), mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

void postSearchError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void postDialogWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

void activateMatch(MREditWindow *win, const SearchMatchEntry &match, const std::string &pattern, const MRSearchDialogOptions &options);

class FoundListView : public TListViewer {
  public:
	FoundListView(const TRect &bounds, TScrollBar *aScrollBar, MREditWindow *aWindow, const std::vector<SearchMatchEntry> &aEntries, const std::string &aPattern, const MRSearchDialogOptions &aOptions) noexcept : TListViewer(bounds, 1, nullptr, aScrollBar), window(aWindow), entries(aEntries), pattern(aPattern), options(aOptions) {
		setRange(static_cast<short>(entries.size()));
	}

	void getText(char *dest, short item, short maxLen) override {
		std::size_t copyLen = 0;

		if (dest == nullptr || maxLen <= 0) return;
		if (item < 0 || static_cast<std::size_t>(item) >= entries.size()) {
			dest[0] = EOS;
			return;
		}
		copyLen = static_cast<std::size_t>(maxLen - 1);
		std::strncpy(dest, entries[static_cast<std::size_t>(item)].preview.c_str(), copyLen);
		dest[copyLen] = EOS;
	}

	void draw() override {
		TDrawBuffer buffer;
		const bool active = (state & (sfSelected | sfActive)) == (sfSelected | sfActive);
		const TColorAttr normalColor = getColor(active ? 1 : 2);
		const TColorAttr selectedColor = getColor(4);
		const TColorAttr focusedColor = active ? static_cast<TColorAttr>(getColor(3)) : selectedColor;

		for (short row = 0; row < size.y; ++row) {
			short item = topItem + row;
			bool isFocusedRow = active && item == focused && range > 0;
			TColorAttr rowColor = normalColor;
			TColorAttr matchColor = selectedColor;

			if (isFocusedRow) {
				rowColor = focusedColor;
				matchColor = selectedColor;
				setCursor(1, row);
			} else if (item < range && isSelected(item)) {
				rowColor = selectedColor;
				matchColor = focusedColor;
			}

			buffer.moveChar(0, ' ', rowColor, size.x);
			if (item < range) {
				const SearchMatchEntry &entry = entries[static_cast<std::size_t>(item)];
				ushort x = 1;
				ushort limit = static_cast<ushort>(std::max(0, size.x - 1));
				auto drawSegment = [&](const std::string &segment, TColorAttr color) {
					for (char ch : segment) {
						if (x >= limit) break;
						buffer.putChar(x, static_cast<uchar>(ch));
						buffer.putAttribute(x, color);
						++x;
					}
				};
				std::size_t splitA = std::min(entry.previewMatchOffset, entry.preview.size());
				std::size_t splitB = std::min(splitA + entry.previewMatchLength, entry.preview.size());
				drawSegment(entry.preview.substr(0, splitA), rowColor);
				drawSegment(entry.preview.substr(splitA, splitB - splitA), matchColor);
				drawSegment(entry.preview.substr(splitB), rowColor);
			}
			writeLine(0, row, size.x, 1, buffer);
		}
	}

	void handleEvent(TEvent &event) override {
		const bool isDoubleClickActivation = event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0 && (event.mouse.eventFlags & meDoubleClick) != 0;
		const short oldFocused = focused;

		TListViewer::handleEvent(event);
		if (focused != oldFocused) previewFocusedItem();
		if (isDoubleClickActivation && focused >= 0 && focused < range) {
			message(owner, evCommand, cmOK, nullptr);
			clearEvent(event);
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter && focused >= 0 && focused < range) {
			message(owner, evCommand, cmOK, nullptr);
			clearEvent(event);
		}
	}

	void previewFocusedItem() {
		if (focused < 0 || focused >= range || static_cast<std::size_t>(focused) >= entries.size() || window == nullptr) return;
		activateMatch(window, entries[static_cast<std::size_t>(focused)], pattern, options);
	}

  private:
	MREditWindow *window;
	const std::vector<SearchMatchEntry> &entries;
	const std::string &pattern;
	const MRSearchDialogOptions &options;
};

bool showFoundListDialog(MREditWindow *win, const std::string &pattern, const MRSearchDialogOptions &options, const std::vector<SearchMatchEntry> &matches, std::size_t &selectedIndex) {
	MRDialogFoundation *dialog = nullptr;
	TScrollBar *scrollBar = nullptr;
	FoundListView *listView = nullptr;
	ushort result = cmCancel;
	const int visibleRows = std::max<int>(6, std::min<int>(static_cast<int>(matches.size()), 13));
	const short width = 92;
	const short height = static_cast<short>(visibleRows + 8);
	const short buttonY = static_cast<short>(height - 3);

	if (TProgram::deskTop == nullptr || matches.empty()) return false;
	dialog = mr::dialogs::createScrollableDialog("FOUND LIST", width, height);
	scrollBar = new TScrollBar(TRect(width - 3, 2, width - 2, height - 4));
	dialog->insert(scrollBar);
	listView = new FoundListView(TRect(2, 2, width - 3, height - 4), scrollBar, win, matches, pattern, options);
	if (selectedIndex < matches.size()) listView->focusItemNum(static_cast<short>(selectedIndex));
	listView->previewFocusedItem();
	dialog->insert(listView);
	{
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);
		mr::dialogs::insertUniformButtonRow(*dialog, (width - metrics.rowWidth) / 2, buttonY, 1, buttons);
	}
	dialog->setDialogValidationHook([listView]() {
		MRScrollableDialog::DialogValidationResult result;
		result.valid = listView != nullptr && listView->focused >= 0;
		if (!result.valid) result.warningText = "Select a match.";
		return result;
	});
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	if (result == cmOK && listView->focused >= 0 && static_cast<std::size_t>(listView->focused) < matches.size()) {
		selectedIndex = static_cast<std::size_t>(listView->focused);
		TObject::destroy(dialog);
		return true;
	}
	TObject::destroy(dialog);
	return false;
}

bool promptSearchPattern(std::string &pattern, MRSearchDialogOptions &options) {
	enum {
		kInputBufferSize = 256
	};
	char patternInput[kInputBufferSize];
	std::string selectionSeed;
	ushort typeChoice = 0;
	ushort directionChoice = options.direction == MRSearchDirection::Backward ? 1 : 0;
	ushort modeChoice = 0;
	ushort optionMask = 0;
	ushort result = cmCancel;
	MRDialogFoundation *dialog = nullptr;
	TInputLine *patternField = nullptr;
	TRadioButtons *typeField = nullptr;
	TRadioButtons *directionField = nullptr;
	TRadioButtons *modeField = nullptr;
	TCheckBoxes *optionsField = nullptr;

	std::memset(patternInput, 0, sizeof(patternInput));
	if (TProgram::deskTop == nullptr) return false;
	selectionSeed = searchSeedFromCurrentSelection();
	if (!selectionSeed.empty()) strnzcpy(patternInput, selectionSeed.c_str(), sizeof(patternInput));
	else if (!g_searchUiState.acceptedPattern.empty())
		strnzcpy(patternInput, g_searchUiState.acceptedPattern.c_str(), sizeof(patternInput));
	else if (!g_searchUiState.pattern.empty())
		strnzcpy(patternInput, g_searchUiState.pattern.c_str(), sizeof(patternInput));

	switch (options.textType) {
		case MRSearchTextType::Literal:
			typeChoice = 0;
			break;
		case MRSearchTextType::Pcre:
			typeChoice = 1;
			break;
		case MRSearchTextType::Word:
			typeChoice = 2;
			break;
	}
	switch (options.mode) {
		case MRSearchMode::StopFirst:
			modeChoice = 0;
			break;
		case MRSearchMode::PromptNext:
			modeChoice = 1;
			break;
		case MRSearchMode::ListAll:
			modeChoice = 2;
			break;
	}
	if (options.caseSensitive) optionMask |= 0x0001;
	if (options.globalSearch) optionMask |= 0x0002;
	if (options.restrictToMarkedBlock) optionMask |= 0x0004;
	if (options.searchAllWindows) optionMask |= 0x0008;

	dialog = mr::dialogs::createScrollableDialog("SEARCH", 96, 22);
	patternField = new TInputLine(TRect(15, 2, 93, 3), kInputBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 2, 15, 3), "Search ~f~or:", patternField));
	dialog->insert(patternField);
	dialog->insert(new TStaticText(TRect(3, 4, 10, 5), "Type:"));
	typeField = new TRadioButtons(TRect(3, 5, 40, 8), new TSItem(" ~L~iteral", new TSItem(" ~R~egular expressions (PCRE)", new TSItem(" ~W~ord/Phrase search", nullptr))));
	dialog->insert(typeField);
	dialog->insert(new TStaticText(TRect(42, 4, 53, 5), "Direction:"));
	directionField = new TRadioButtons(TRect(42, 5, 59, 8), new TSItem(" ~F~orward", new TSItem(" ~B~ackward", nullptr)));
	dialog->insert(directionField);
	dialog->insert(new TStaticText(TRect(61, 4, 67, 5), "Mode:"));
	modeField = new TRadioButtons(TRect(61, 5, 93, 8), new TSItem(" ~S~top on first occurrence", new TSItem(" ~P~rompt for next match", new TSItem(" L~i~st all occurrences", nullptr))));
	dialog->insert(modeField);
	dialog->insert(new TStaticText(TRect(3, 10, 12, 11), "Options:"));
	optionsField = new TCheckBoxes(TRect(3, 11, 40, 16), new TSItem("~C~ase sensitive", new TSItem("~G~lobal search", new TSItem("~R~estrict to marked block", new TSItem("Search all ~w~indows", nullptr)))));
	dialog->insert(optionsField);
	{
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~G~o", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 3);
		mr::dialogs::insertUniformButtonRow(*dialog, (76 - metrics.rowWidth) / 2, 18, 3, buttons);
	}
	patternField->setData(patternInput);
	typeField->setData(&typeChoice);
	directionField->setData(&directionChoice);
	modeField->setData(&modeChoice);
	optionsField->setData(&optionMask);
	dialog->setDialogValidationHook([patternField]() {
		MRScrollableDialog::DialogValidationResult result;
		char text[kInputBufferSize] = {0};

		if (patternField != nullptr) patternField->getData(text);
		result.valid = !trimAscii(text).empty();
		if (!result.valid) result.warningText = kSearchTextRequiredMessage;
		return result;
	});
	dialog->finalizeLayout();
	if (patternField != nullptr) patternField->select();
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel) {
		patternField->getData(patternInput);
		typeField->getData(&typeChoice);
		directionField->getData(&directionChoice);
		modeField->getData(&modeChoice);
		optionsField->getData(&optionMask);
		pattern = trimAscii(patternInput);
		options.textType = typeChoice == 1 ? MRSearchTextType::Pcre : (typeChoice == 2 ? MRSearchTextType::Word : MRSearchTextType::Literal);
		options.direction = directionChoice == 1 ? MRSearchDirection::Backward : MRSearchDirection::Forward;
		options.mode = modeChoice == 1 ? MRSearchMode::PromptNext : (modeChoice == 2 ? MRSearchMode::ListAll : MRSearchMode::StopFirst);
		options.caseSensitive = (optionMask & 0x0001) != 0;
		options.globalSearch = (optionMask & 0x0002) != 0;
		options.restrictToMarkedBlock = (optionMask & 0x0004) != 0;
		options.searchAllWindows = (optionMask & 0x0008) != 0;
		g_searchUiState.hasAcceptedPattern = true;
		g_searchUiState.acceptedPattern = pattern;
		g_searchUiState.acceptedOptions = options;
		static_cast<void>(setConfiguredSearchDialogOptions(options));
	}
	TObject::destroy(dialog);
	return result != cmCancel;
}

bool promptReplaceValues(std::string &pattern, std::string &replacement, MRSarDialogOptions &options) {
	enum {
		kPatternBufferSize = 256,
		kReplacementBufferSize = 256
	};
	char patternInput[kPatternBufferSize];
	char replacementInput[kReplacementBufferSize];
	std::string selectionSeed;
	ushort typeChoice = 0;
	ushort directionChoice = options.direction == MRSearchDirection::Backward ? 1 : 0;
	ushort modeChoice = 0;
	ushort leaveCursorChoice = options.leaveCursorAt == MRSarLeaveCursor::StartOfReplaceString ? 1 : 0;
	ushort optionMask = 0;
	ushort result = cmCancel;
	MRDialogFoundation *dialog = nullptr;
	TInputLine *patternField = nullptr;
	TInputLine *replacementField = nullptr;
	TRadioButtons *typeField = nullptr;
	TRadioButtons *directionField = nullptr;
	TRadioButtons *modeField = nullptr;
	TRadioButtons *leaveCursorField = nullptr;
	TCheckBoxes *optionsField = nullptr;

	std::memset(patternInput, 0, sizeof(patternInput));
	std::memset(replacementInput, 0, sizeof(replacementInput));
	if (TProgram::deskTop == nullptr) return false;
	selectionSeed = searchSeedFromCurrentSelection();
	if (!selectionSeed.empty()) strnzcpy(patternInput, selectionSeed.c_str(), sizeof(patternInput));
	else if (!g_searchUiState.pattern.empty())
		strnzcpy(patternInput, g_searchUiState.pattern.c_str(), sizeof(patternInput));
	if (!g_searchUiState.replacement.empty()) strnzcpy(replacementInput, g_searchUiState.replacement.c_str(), sizeof(replacementInput));

	switch (options.textType) {
		case MRSearchTextType::Literal:
			typeChoice = 0;
			break;
		case MRSearchTextType::Pcre:
			typeChoice = 1;
			break;
		case MRSearchTextType::Word:
			typeChoice = 2;
			break;
	}
	switch (options.mode) {
		case MRSarMode::ReplaceFirst:
			modeChoice = 0;
			break;
		case MRSarMode::PromptEach:
			modeChoice = 1;
			break;
		case MRSarMode::ReplaceAll:
			modeChoice = 2;
			break;
	}
	if (options.caseSensitive) optionMask |= 0x0001;
	if (options.globalSearch) optionMask |= 0x0002;
	if (options.restrictToMarkedBlock) optionMask |= 0x0004;
	if (options.searchAllWindows) optionMask |= 0x0008;

	dialog = mr::dialogs::createScrollableDialog("SEARCH AND REPLACE", 92, 24);
	patternField = new TInputLine(TRect(17, 2, 89, 3), kPatternBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 2, 16, 3), "Search ~f~or:", patternField));
	dialog->insert(patternField);
	replacementField = new TInputLine(TRect(17, 4, 89, 5), kReplacementBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 4, 16, 5), "Replace ~w~ith:", replacementField));
	dialog->insert(replacementField);
	dialog->insert(new TStaticText(TRect(3, 6, 10, 7), "Type:"));
	typeField = new TRadioButtons(TRect(3, 7, 37, 10), new TSItem(" ~L~iteral", new TSItem(" ~R~egular expressions (PCRE)", new TSItem(" ~W~ord/Phrase search", nullptr))));
	dialog->insert(typeField);
	dialog->insert(new TStaticText(TRect(39, 6, 50, 7), "Direction:"));
	directionField = new TRadioButtons(TRect(39, 7, 56, 10), new TSItem(" ~F~orward", new TSItem(" ~B~ackward", nullptr)));
	dialog->insert(directionField);
	dialog->insert(new TStaticText(TRect(58, 6, 64, 7), "Mode:"));
	modeField = new TRadioButtons(TRect(58, 7, 89, 10), new TSItem(" Replace ~f~irst occurrence", new TSItem(" ~P~rompt for each replace", new TSItem(" Replace ~a~ll occurrences", nullptr))));
	dialog->insert(modeField);
	dialog->insert(new TStaticText(TRect(3, 12, 20, 13), "Leave cursor at:"));
	leaveCursorField = new TRadioButtons(TRect(3, 13, 37, 15), new TSItem(" ~E~nd of replace string", new TSItem(" ~S~tart of replace string", nullptr)));
	dialog->insert(leaveCursorField);
	dialog->insert(new TStaticText(TRect(39, 12, 48, 13), "Options:"));
	optionsField = new TCheckBoxes(TRect(39, 13, 72, 18), new TSItem("~C~ase sensitive", new TSItem("~G~lobal search", new TSItem("~R~estrict to marked block", new TSItem("Search all ~w~indows", nullptr)))));
	dialog->insert(optionsField);
	{
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~G~o", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 3);
		mr::dialogs::insertUniformButtonRow(*dialog, (92 - metrics.rowWidth) / 2, 20, 3, buttons);
	}
	patternField->setData(patternInput);
	replacementField->setData(replacementInput);
	typeField->setData(&typeChoice);
	directionField->setData(&directionChoice);
	modeField->setData(&modeChoice);
	leaveCursorField->setData(&leaveCursorChoice);
	optionsField->setData(&optionMask);
	dialog->setDialogValidationHook([patternField]() {
		MRScrollableDialog::DialogValidationResult result;
		char text[kPatternBufferSize] = {0};

		if (patternField != nullptr) patternField->getData(text);
		result.valid = !trimAscii(text).empty();
		if (!result.valid) result.warningText = kSearchTextRequiredMessage;
		return result;
	});
	dialog->finalizeLayout();
	if (patternField != nullptr) patternField->select();
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel) {
		patternField->getData(patternInput);
		replacementField->getData(replacementInput);
		typeField->getData(&typeChoice);
		directionField->getData(&directionChoice);
		modeField->getData(&modeChoice);
		leaveCursorField->getData(&leaveCursorChoice);
		optionsField->getData(&optionMask);
		pattern = trimAscii(patternInput);
		replacement = replacementInput;
		options.textType = typeChoice == 1 ? MRSearchTextType::Pcre : (typeChoice == 2 ? MRSearchTextType::Word : MRSearchTextType::Literal);
		options.direction = directionChoice == 1 ? MRSearchDirection::Backward : MRSearchDirection::Forward;
		options.mode = modeChoice == 1 ? MRSarMode::PromptEach : (modeChoice == 2 ? MRSarMode::ReplaceAll : MRSarMode::ReplaceFirst);
		options.leaveCursorAt = leaveCursorChoice == 1 ? MRSarLeaveCursor::StartOfReplaceString : MRSarLeaveCursor::EndOfReplaceString;
		options.caseSensitive = (optionMask & 0x0001) != 0;
		options.globalSearch = (optionMask & 0x0002) != 0;
		options.restrictToMarkedBlock = (optionMask & 0x0004) != 0;
		options.searchAllWindows = (optionMask & 0x0008) != 0;
		static_cast<void>(setConfiguredSarDialogOptions(options));
	}
	TObject::destroy(dialog);
	return result != cmCancel;
}
MREditWindow *resolveSearchResultsWindow(MREditWindow *window) {
	if (window == nullptr) return nullptr;
	for (MREditWindow *candidate : allEditWindowsInZOrder())
		if (candidate == window) return candidate;
	return nullptr;
}

void rememberSingleSearchResultContext(MREditWindow *window, const std::string &pattern, const MRSearchDialogOptions &options, std::size_t start, std::size_t end) {
	g_searchUiState.hasPrevious = true;
	g_searchUiState.pattern = pattern;
	g_searchUiState.lastStart = start;
	g_searchUiState.lastEnd = end;
	g_searchUiState.lastOptions = options;
	g_searchResultsContext.kind = SearchResultsContextKind::SingleFile;
	g_searchResultsContext.window = window;
}

void rememberMultiFileSearchResultContext() {
	if (!hasPreviousMultiFileSearchResults()) {
		g_searchResultsContext.kind = SearchResultsContextKind::None;
		g_searchResultsContext.window = nullptr;
		return;
	}
	g_searchResultsContext.kind = SearchResultsContextKind::MultiFile;
	g_searchResultsContext.window = nullptr;
}

void activateMatch(MREditWindow *win, const SearchMatchEntry &match, const std::string &pattern, const MRSearchDialogOptions &options) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::size_t end = std::max(match.end, match.start);

	if (editor == nullptr) return;
	if (end == match.start) {
		if (end < editor->bufferLength()) ++end;
		else if (match.start > 0)
			--end;
	}
	editor->setCursorOffset(match.start);
	editor->setSelectionOffsets(match.start, end);
	editor->revealCursor(True);
	syncVmLastSearch(win, true, match.start, end, match.start);
	rememberSingleSearchResultContext(win, pattern, options, match.start, end);
}

bool performSearch(MREditWindow *win, const std::string &pattern, std::size_t startOffset, bool updateState, const MRSearchDialogOptions &options, bool showNotFoundMessage, bool *outWrapped = nullptr) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	pcre2_code *code = nullptr;
	std::string regexError;
	std::string patternExpression;
	std::string text;
	std::size_t selectionStart = 0;
	std::size_t selectionEnd = 0;
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;
	std::size_t matchStart = 0;
	std::size_t matchEnd = 0;
	bool wrapped = false;

	if (outWrapped != nullptr) *outWrapped = false;
	if (editor == nullptr) return false;
	if (pattern.empty()) {
		postSearchError(kSearchTextRequiredMessage);
		return false;
	}
	patternExpression = buildSearchPatternExpression(pattern, options.textType);
	if (!compileSearchRegex(patternExpression, !options.caseSensitive, &code, regexError)) {
		postSearchError("Invalid search pattern: " + regexError);
		return false;
	}
	text = editor->snapshotText();
	rangeStart = 0;
	rangeEnd = text.size();
	if (options.restrictToMarkedBlock) {
		selectionStart = editor->selectionStartOffset();
		selectionEnd = editor->selectionEndOffset();
		rangeStart = std::min(selectionStart, selectionEnd);
		rangeEnd = std::max(selectionStart, selectionEnd);
		if (rangeStart >= rangeEnd) {
			pcre2_code_free(code);
			postDialogWarning(kNoMarkedBlockSelectedMessage);
			return false;
		}
	}
	if (!findRegexWithWrap(text, code, startOffset, options.direction, rangeStart, rangeEnd, options.globalSearch, matchStart, matchEnd, wrapped)) {
		pcre2_code_free(code);
		if (showNotFoundMessage) postSearchWarning("No match found.");
		syncVmLastSearch(win, false, 0, 0, 0);
		return false;
	}
	if (matchEnd == matchStart) {
		if (matchEnd < text.size()) ++matchEnd;
		else if (matchStart > 0)
			--matchStart;
	}
	editor->setCursorOffset(matchStart);
	editor->setSelectionOffsets(matchStart, matchEnd);
	editor->revealCursor(True);
	syncVmLastSearch(win, true, matchStart, matchEnd, matchStart);
	if (updateState) rememberSingleSearchResultContext(win, pattern, options, matchStart, matchEnd);
	pcre2_code_free(code);
	if (outWrapped != nullptr) *outWrapped = wrapped;
	return true;
}

} // namespace

bool handleSearchFindText() {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	MRSearchDialogOptions options = configuredSearchDialogOptions();
	std::string pattern;

	if (editor == nullptr) return true;
	if (!promptSearchPattern(pattern, options)) {
		editor->clearFindMarkerRanges();
		clearSearchSelection(win);
		return true;
	}
	updateMiniMapFindMarkers(win, pattern, options);
	if (options.mode == MRSearchMode::ListAll) {
		pcre2_code *code = nullptr;
		std::string regexError;
		std::vector<SearchMatchEntry> matches;
		std::size_t selectedIndex = 0;
		std::string text = editor->snapshotText();
		const std::string patternExpression = buildSearchPatternExpression(pattern, options.textType);
		std::size_t rangeStart = 0;
		std::size_t rangeEnd = text.size();

		if (!compileSearchRegex(patternExpression, !options.caseSensitive, &code, regexError)) {
			postSearchError("Invalid search pattern: " + regexError);
			return true;
		}
		static_cast<void>(collectRegexMatches(text, code, matches));
		pcre2_code_free(code);
		if (options.restrictToMarkedBlock) {
			rangeStart = std::min(editor->selectionStartOffset(), editor->selectionEndOffset());
			rangeEnd = std::max(editor->selectionStartOffset(), editor->selectionEndOffset());
			if (rangeStart >= rangeEnd) {
				postDialogWarning(kNoMarkedBlockSelectedMessage);
				clearSearchSelection(win);
				return true;
			}
			matches.erase(std::remove_if(matches.begin(), matches.end(), [&](const SearchMatchEntry &entry) { return entry.start < rangeStart || entry.end > rangeEnd; }), matches.end());
		}
		if (matches.empty()) {
			postSearchWarning("No match found.");
			clearSearchSelection(win);
			return true;
		}
		if (matches.size() == 1) {
			activateMatch(win, matches[0], pattern, options);
			return true;
		}
		if (showFoundListDialog(win, pattern, options, matches, selectedIndex)) activateMatch(win, matches[selectedIndex], pattern, options);
		else
			clearSearchSelection(win);
		return true;
	}

	if (options.searchAllWindows && options.mode == MRSearchMode::StopFirst) {
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		auto it = std::find(windows.begin(), windows.end(), win);
		bool found = false;
		if (it != windows.end()) std::rotate(windows.begin(), it, windows.end());
		for (std::size_t i = 0; i < windows.size(); ++i) {
			MREditWindow *candidate = windows[i];
			MRFileEditor *candidateEditor = candidate != nullptr ? candidate->getEditor() : nullptr;
			std::size_t startOffset = 0;
			bool wrapped = false;

			if (candidateEditor == nullptr) continue;
			if (options.direction == MRSearchDirection::Backward) startOffset = (i == 0) ? candidateEditor->cursorOffset() : candidateEditor->bufferLength();
			else
				startOffset = (i == 0) ? candidateEditor->cursorOffset() : 0;
			if (!performSearch(candidate, pattern, startOffset, true, options, false, &wrapped)) continue;
			if (candidate != win) static_cast<void>(mrActivateEditWindow(candidate));
			if (wrapped) mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, wrappedSearchMessage(options.direction), mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
			found = true;
			break;
		}
		if (!found) {
			postSearchWarning("No match found.");
			clearSearchSelection(win);
		}
		return true;
	}

	{
		std::size_t startOffset = editor->cursorOffset();
		bool wrapped = false;

		if (!performSearch(win, pattern, startOffset, true, options, true, &wrapped)) return true;
		if (wrapped) mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, wrappedSearchMessage(options.direction), mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
		if (options.mode == MRSearchMode::PromptNext) {
			while (true) {
				std::size_t currentStart = editor->selectionStartOffset();
				std::size_t currentEnd = editor->selectionEndOffset();
				std::size_t nextStartOffset = 0;
				SearchPreviewParts preview = previewForMatch(editor->snapshotText(), currentStart, currentEnd);
				PromptSearchDecision decision = promptSearchDecisionDialog(preview);

				if (decision == PromptSearchDecision::Cancel) {
					clearSearchSelection(win);
					break;
				}
				if (decision == PromptSearchDecision::Stop) break;
				if (options.direction == MRSearchDirection::Backward) nextStartOffset = currentStart;
				else {
					nextStartOffset = currentEnd;
					if (currentEnd <= currentStart) nextStartOffset = std::min(editor->bufferLength(), currentStart + 1);
				}
				if (!performSearch(win, pattern, nextStartOffset, true, options, false, &wrapped)) break;
				if (wrapped) mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, wrappedSearchMessage(options.direction), mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
			}
		}
	}
	return true;
}

bool handleSearchRepeatPrevious() {
	MREditWindow *win = resolveSearchResultsWindow(g_searchResultsContext.window);
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::size_t startOffset = 0;
	bool wrapped = false;

	if (!g_searchUiState.hasPrevious || g_searchUiState.pattern.empty()) {
		postDialogWarning(kNoPreviousSearchMessage);
		return true;
	}
	if (editor == nullptr) {
		postDialogWarning(kNoPreviousSearchMessage);
		return true;
	}
	if (g_searchUiState.lastOptions.direction == MRSearchDirection::Backward) startOffset = g_searchUiState.lastStart;
	else {
		startOffset = g_searchUiState.lastEnd;
		if (g_searchUiState.lastEnd <= g_searchUiState.lastStart) startOffset = std::min(editor->bufferLength(), g_searchUiState.lastStart + 1);
	}
	if (!performSearch(win, g_searchUiState.pattern, startOffset, true, g_searchUiState.lastOptions, true, &wrapped)) return true;
	if (win != currentEditWindow()) static_cast<void>(mrActivateEditWindow(win));
	updateMiniMapFindMarkers(win, g_searchUiState.pattern, g_searchUiState.lastOptions);
	if (wrapped) mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, wrappedSearchMessage(g_searchUiState.lastOptions.direction), mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleSearchReplace() {
	MREditWindow *win = currentEditWindow();
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	MRSarDialogOptions options = configuredSarDialogOptions();
	std::string pattern;
	std::string replacement;
	std::size_t start = 0;
	std::size_t end = 0;
	std::size_t cursorTargetStart = 0;
	std::size_t cursorTargetEnd = 0;
	std::size_t replacedCount = 0;
	bool cancelledByUser = false;

	if (editor == nullptr || win == nullptr) return true;
	if (!promptReplaceValues(pattern, replacement, options)) {
		editor->clearFindMarkerRanges();
		clearSearchSelection(win);
		return true;
	}
	if (pattern.empty()) {
		postSearchError(kSearchTextRequiredMessage);
		return true;
	}
	{
		MRSearchDialogOptions searchOptions;
		searchOptions.textType = options.textType;
		searchOptions.direction = options.direction;
		searchOptions.mode = MRSearchMode::StopFirst;
		searchOptions.caseSensitive = options.caseSensitive;
		searchOptions.globalSearch = options.globalSearch;
		searchOptions.restrictToMarkedBlock = options.restrictToMarkedBlock;
		searchOptions.searchAllWindows = options.searchAllWindows;
		updateMiniMapFindMarkers(win, pattern, searchOptions);

		if (options.mode == MRSarMode::ReplaceFirst) {
			if (!performSearch(win, pattern, editor->cursorOffset(), false, searchOptions, true, nullptr)) return true;
			start = editor->selectionStartOffset();
			end = editor->selectionEndOffset();
			if (end < start) std::swap(start, end);
			if (!editor->replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end), replacement.data(), static_cast<uint>(replacement.size()))) {
				postSearchError("Replace failed.");
				return true;
			}
			cursorTargetStart = start;
			cursorTargetEnd = start + replacement.size();
			replacedCount = 1;
		} else {
			pcre2_code *code = nullptr;
			std::string regexError;
			std::string text = editor->snapshotText();
			std::vector<SearchMatchEntry> matches;
			long long delta = 0;
			const std::size_t initialCursor = editor->cursorOffset();
			std::size_t rangeStart = 0;
			std::size_t rangeEnd = text.size();
			bool forceReplaceAll = options.mode == MRSarMode::ReplaceAll;
			const bool forwardOrder = options.direction != MRSearchDirection::Backward;

			if (!compileSearchRegex(buildSearchPatternExpression(pattern, options.textType), !options.caseSensitive, &code, regexError)) {
				postSearchError("Invalid search pattern: " + regexError);
				return true;
			}
			static_cast<void>(collectRegexMatches(text, code, matches));
			pcre2_code_free(code);
			if (options.restrictToMarkedBlock) {
				rangeStart = std::min(editor->selectionStartOffset(), editor->selectionEndOffset());
				rangeEnd = std::max(editor->selectionStartOffset(), editor->selectionEndOffset());
				if (rangeStart >= rangeEnd) {
					postDialogWarning(kNoMarkedBlockSelectedMessage);
					clearSearchSelection(win);
					return true;
				}
			}
			matches.erase(std::remove_if(matches.begin(), matches.end(),
			                             [&](const SearchMatchEntry &entry) {
				                             if (entry.start < rangeStart || entry.end > rangeEnd) return true;
				                             if (options.globalSearch) return false;
				                             if (options.direction == MRSearchDirection::Backward) return entry.start >= initialCursor;
				                             return entry.start < initialCursor;
			                             }),
			              matches.end());
			if (matches.empty()) {
				postSearchWarning("No match found.");
				clearSearchSelection(win);
				return true;
			}
			if (options.direction == MRSearchDirection::Backward) std::reverse(matches.begin(), matches.end());
			for (std::size_t matchIndex = 0; matchIndex < matches.size(); ++matchIndex) {
				const SearchMatchEntry &match = matches[matchIndex];
				std::size_t currentStart = match.start;
				std::size_t currentEnd = match.end;

				if (forwardOrder) {
					currentStart = static_cast<std::size_t>(static_cast<long long>(match.start) + delta);
					currentEnd = static_cast<std::size_t>(static_cast<long long>(match.end) + delta);
				}

				if (!forceReplaceAll) {
					std::size_t promptStart = currentStart;
					std::size_t promptEnd = currentEnd;
					SearchPreviewParts promptPreview;
					if (promptEnd <= promptStart) {
						if (promptEnd < editor->bufferLength()) ++promptEnd;
						else if (promptStart > 0)
							--promptStart;
					}
					editor->setCursorOffset(promptStart);
					editor->setSelectionOffsets(promptStart, promptEnd);
					editor->revealCursor(True);
					syncVmLastSearch(win, true, promptStart, promptEnd, promptStart);
					promptPreview = previewForMatch(editor->snapshotText(), promptStart, promptEnd);
					PromptReplaceDecision decision = promptReplaceDecisionDialog("SEARCH AND REPLACE", promptPreview, replacement);
					if (decision == PromptReplaceDecision::Cancel) {
						cancelledByUser = true;
						break;
					}
					if (decision == PromptReplaceDecision::Skip) continue;
					if (decision == PromptReplaceDecision::ReplaceAll) forceReplaceAll = true;
				}

				if (forceReplaceAll) {
					std::vector<MRTextBufferModel::Range> replacementRanges;
					long long batchDeltaBeforeLast = 0;

					replacementRanges.reserve(matches.size() - matchIndex);
					if (forwardOrder) {
						for (std::size_t i = matchIndex; i < matches.size(); ++i) {
							const SearchMatchEntry &pendingMatch = matches[i];
							const std::size_t pendingStart = static_cast<std::size_t>(static_cast<long long>(pendingMatch.start) + delta);
							const std::size_t pendingEnd = static_cast<std::size_t>(static_cast<long long>(pendingMatch.end) + delta);

							replacementRanges.push_back(MRTextBufferModel::Range(pendingStart, pendingEnd));
							if (i + 1 < matches.size()) batchDeltaBeforeLast += static_cast<long long>(replacement.size()) - static_cast<long long>(pendingMatch.end - pendingMatch.start);
						}
						cursorTargetStart = static_cast<std::size_t>(static_cast<long long>(replacementRanges.back().start) + batchDeltaBeforeLast);
					} else {
						for (std::size_t i = matches.size(); i > matchIndex; --i)
							replacementRanges.push_back(MRTextBufferModel::Range(matches[i - 1].start, matches[i - 1].end));
						cursorTargetStart = replacementRanges.front().start;
					}
					if (!editor->replaceRangesAndCollapse(replacementRanges, replacement.data(), replacement.size())) {
						postSearchError("Replace failed.");
						return true;
					}
					cursorTargetEnd = cursorTargetStart + replacement.size();
					replacedCount += replacementRanges.size();
					break;
				}

				if (!editor->replaceRangeAndSelect(static_cast<uint>(currentStart), static_cast<uint>(currentEnd), replacement.data(), static_cast<uint>(replacement.size()))) {
					postSearchError("Replace failed.");
					return true;
				}
				cursorTargetStart = currentStart;
				cursorTargetEnd = currentStart + replacement.size();
				++replacedCount;
				if (forwardOrder) delta += static_cast<long long>(replacement.size()) - static_cast<long long>(match.end - match.start);
			}
			if (replacedCount == 0) {
				if (cancelledByUser) {
					clearSearchSelection(win);
				}
				postSearchWarning("No replacements.");
				return true;
			}
		}
		if (options.leaveCursorAt == MRSarLeaveCursor::StartOfReplaceString) {
			editor->setCursorOffset(cursorTargetStart);
			editor->setSelectionOffsets(cursorTargetStart, cursorTargetStart);
		} else {
			editor->setCursorOffset(cursorTargetEnd);
			editor->setSelectionOffsets(cursorTargetEnd, cursorTargetEnd);
		}
		editor->revealCursor(True);

		g_searchUiState.replacement = replacement;
		rememberSingleSearchResultContext(win, pattern, searchOptions, cursorTargetStart, cursorTargetEnd);
		syncVmLastSearch(win, true, cursorTargetStart, cursorTargetEnd, editor->cursorOffset());
		if (cancelledByUser) {
			clearSearchSelection(win);
		} else
			updateMiniMapFindMarkers(win, pattern, searchOptions);
		postSearchWarning(std::to_string(replacedCount) + " replacements");
	}
	return true;
}

bool handleSearchMultiFileSearch() {
	const bool hadPrevious = hasPreviousMultiFileSearchResults();
	const bool handled = handleMultiFileSearchDialog(g_searchUiState.pattern);

	if (hadPrevious || hasPreviousMultiFileSearchResults()) rememberMultiFileSearchResultContext();
	return handled;
}

bool handleSearchListFilesFromLastSearch() {
	const bool hadPrevious = hasPreviousMultiFileSearchResults();
	const bool handled = handleLastMultiFileSearchListDialog();

	if (hadPrevious || hasPreviousMultiFileSearchResults()) rememberMultiFileSearchResultContext();
	return handled;
}

bool handleSearchMultiFileSearchReplace() {
	const bool hadPrevious = hasPreviousMultiFileSearchResults();
	const bool handled = handleMultiFileSearchReplaceDialog(g_searchUiState.pattern, g_searchUiState.replacement);

	if (hadPrevious || hasPreviousMultiFileSearchResults()) rememberMultiFileSearchResultContext();
	return handled;
}


bool handleSearchResultsNext() {
	switch (g_searchResultsContext.kind) {
		case SearchResultsContextKind::SingleFile:
			return handleSearchRepeatPrevious();
		case SearchResultsContextKind::MultiFile:
			return handleNextMultiFileSearchResult();
		case SearchResultsContextKind::None:
			postDialogWarning(kNoPreviousSearchMessage);
			return true;
	}
	return true;
}

void clearTransientSelectionIfPending(const TEvent &event) {
	if (!g_pendingTransientSelectionClear.active || event.what != evKeyDown) return;
	g_pendingTransientSelectionClear.active = false;
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
		if (editor == nullptr || !editor->hasPersistentFileName()) continue;
		if (normalizedSearchPath(editor->persistentFileName()) != g_pendingTransientSelectionClear.normalizedPath) continue;
		{
			std::size_t selStart = editor->selectionStartOffset();
			std::size_t selEnd = editor->selectionEndOffset();
			if (selEnd < selStart) std::swap(selStart, selEnd);
			if (selStart != g_pendingTransientSelectionClear.start || selEnd != g_pendingTransientSelectionClear.end) break;
		}
		{
			const std::size_t cursor = editor->cursorOffset();
			editor->setSelectionOffsets(cursor, cursor);
		}
		break;
	}
}

void currentSearchPatternSnapshot(std::string &pattern, MRSearchDialogOptions &options) {
	if (g_searchUiState.hasAcceptedPattern) {
		pattern = g_searchUiState.acceptedPattern;
		options = g_searchUiState.acceptedOptions;
		return;
	}
	pattern = g_searchUiState.pattern;
	options = g_searchUiState.hasPrevious ? g_searchUiState.lastOptions : configuredSearchDialogOptions();
}
