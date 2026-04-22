#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TObject
#define Uses_TEvent
#define Uses_TRect
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
#define Uses_TSItem
#include <tvision/tv.h>

#include "MRCommandRouter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../dialogs/MRFileInformationDialog.hpp"
#include "../dialogs/MRAboutDialog.hpp"
#include "../dialogs/MRSetupDialogs.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../app/utils/MRStringUtils.hpp"
#include "../mrmac/mrvm.hpp"
#include "../app/commands/MRExternalCommand.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../dialogs/MRMacroFileDialog.hpp"
#include "../dialogs/MRWindowListDialog.hpp"
#include "../ui/TMREditWindow.hpp"
#include "../ui/TMRMenuBar.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "TMREditorApp.hpp"
#include "MRCommands.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace {
bool startExternalCommandInWindow(TMREditWindow *win, const std::string &commandLine, bool replaceBuffer,
                                  bool activate, bool closeOnFailure);

struct SearchUiState {
	bool hasPrevious = false;
	std::string pattern;
	std::string replacement;
	std::size_t lastStart = 0;
	std::size_t lastEnd = 0;
	MRSearchDialogOptions lastOptions;
};

SearchUiState g_searchUiState;

struct SearchMatchEntry {
	std::size_t start = 0;
	std::size_t end = 0;
	std::size_t line = 1;
	std::size_t column = 1;
	std::string preview;
	std::size_t previewMatchOffset = 0;
	std::size_t previewMatchLength = 0;
};

enum class PromptReplaceDecision : unsigned char {
	Replace = 0,
	Skip = 1,
	ReplaceAll = 2,
	Cancel = 3
};

enum class PromptSearchDecision : unsigned char {
	Next = 0,
	Stop = 1,
	Cancel = 2
};

const char *placeholderCommandTitle(ushort command) {
	switch (command) {
		case cmMrFileOpen:
			return "File / Open";
		case cmMrFileLoad:
			return "File / Load";
		case cmMrFileSave:
			return "File / Save";
		case cmMrFileSaveAs:
			return "File / Save File As";
		case cmMrFileInformation:
			return "File / Information";
		case cmMrFileMerge:
			return "File / Merge";
		case cmMrFilePrint:
			return "File / Print";
		case cmMrFileShellToDos:
			return "File / Shell to DOS";

		case cmMrEditUndo:
			return "Edit / Undo";
		case cmMrEditRedo:
			return "Edit / Redo";
		case cmMrEditCutToBuffer:
			return "Edit / Cut to buffer";
		case cmMrEditCopyToBuffer:
			return "Edit / Copy to buffer";
		case cmMrEditAppendToBuffer:
			return "Edit / Append to buffer";
		case cmMrEditCutAndAppendToBuffer:
			return "Edit / Cut and append to buffer";
		case cmMrEditPasteFromBuffer:
			return "Edit / Paste from buffer";
		case cmMrEditRepeatCommand:
			return "Edit / Repeat command";

		case cmMrWindowClose:
			return "Window / Close";
		case cmMrWindowSplit:
			return "Window / Split";
		case cmMrWindowList:
			return "Window / List";
		case cmMrWindowNext:
			return "Window / Next";
		case cmMrWindowPrevious:
			return "Window / Previous";
		case cmMrWindowAdjacent:
			return "Window / Adjacent";
		case cmMrWindowHide:
			return "Window / Hide";
		case cmMrWindowModifySize:
			return "Window / Modify size";
		case cmMrWindowZoom:
			return "Window / Zoom";
		case cmMrWindowMinimize:
			return "Window / Minimize";
		case cmMrWindowLink:
			return "Window / Link";
		case cmMrWindowUnlink:
			return "Window / Unlink";

		case cmMrBlockCopy:
			return "Block / Copy block";
		case cmMrBlockMove:
			return "Block / Move block";
		case cmMrBlockDelete:
			return "Block / Delete block";
		case cmMrBlockSaveToDisk:
			return "Block / Save block to disk";
		case cmMrBlockIndent:
			return "Block / Indent block";
		case cmMrBlockUndent:
			return "Block / Undent block";
		case cmMrBlockWindowCopy:
			return "Block / Window copy";
		case cmMrBlockWindowMove:
			return "Block / Window move";
		case cmMrBlockMarkLines:
			return "Block / Mark lines of text";
		case cmMrBlockMarkColumns:
			return "Block / Mark columns of text";
		case cmMrBlockMarkStream:
			return "Block / Mark stream of text";
		case cmMrBlockEndMarking:
			return "Block / End marking";
		case cmMrBlockTurnMarkingOff:
			return "Block / Turn marking off";
		case cmMrBlockPersistent:
			return "Block / Persistent blocks";

		case cmMrSearchFindText:
			return "Search / Search for text";
		case cmMrSearchReplace:
			return "Search / Search and Replace";
		case cmMrSearchRepeatPrevious:
			return "Search / Repeat previous search";
		case cmMrSearchPushMarker:
			return "Search / Push position onto marker stack";
		case cmMrSearchGetMarker:
			return "Search / Get position from marker stack";
		case cmMrSearchSetRandomAccessMark:
			return "Search / Set random access mark";
		case cmMrSearchRetrieveRandomAccessMark:
			return "Search / Retrieve random access mark";
		case cmMrSearchGotoLineNumber:
			return "Search / Goto line number";

		case cmMrTextLayout:
			return "Text / Layout";
		case cmMrTextUpperCaseMenu:
			return "Text / Upper case";
		case cmMrTextLowerCaseMenu:
			return "Text / Lower case";
		case cmMrTextCenterLine:
			return "Text / Center line";
		case cmMrTextTimeDateStamp:
			return "Text / Time/Date stamp";
		case cmMrTextReformatParagraph:
			return "Text / Re-format paragraph";
		case cmMrTextUpperCasePlaceholder:
			return "Text / Upper case";
		case cmMrTextLowerCasePlaceholder:
			return "Text / Lower case";

		case cmMrOtherMacroManager:
			return "Other / Macro manager";
		case cmMrOtherExecuteProgram:
			return "Other / Execute program";
		case cmMrOtherStopProgram:
			return "Other / Stop current program";
		case cmMrOtherRestartProgram:
			return "Other / Restart current program";
		case cmMrOtherClearOutput:
			return "Other / Clear current output";
		case cmMrOtherFindNextCompilerError:
			return "Other / Find next compiler error";
		case cmMrOtherMatchBraceOrParen:
			return "Other / Match brace or paren";
		case cmMrOtherAsciiTable:
			return "Other / Ascii table";

		case cmMrHelpContents:
			return "Help / Table of contents";
		case cmMrHelpKeys:
			return "Help / Keys";
		case cmMrHelpDetailedIndex:
			return "Help / Detailed index";
		case cmMrHelpPreviousTopic:
			return "Help / Previous topic";
		case cmMrHelpAbout:
			return "Help / About";

		case cmMrDevCancelMacroTasks:
			return "Dev / Cancel background macros";
		case cmMrDevHeroEventProbe:
			return "Dev / Test hero event";

		case cmMrSetupKeyMapping:
			return "Installation / Key mapping";
		case cmMrSetupMouseKeyRepeat:
			return "Installation / Mouse / Key repeat setup";
		case cmMrSetupFilenameExtensions:
			return "Installation / Filename extensions";
		case cmMrSetupPaths:
			return "Installation / Paths";
		case cmMrSetupBackupsAutosave:
			return "Installation / Backups / Autosave";
		case cmMrSetupUserInterfaceSettings:
			return "Installation / User interface settings";
		case cmMrSetupSearchAndReplaceDefaults:
			return "Installation / Search and Replace defaults";

		default:
			return nullptr;
	}
}

void showPlaceholderCommandBox(const char *title) {
	if (title == nullptr)
		title = "Command";
	messageBox(mfInformation | mfOKButton, "%s\n\nPlaceholder implementation for now.", title);
}

TRect centeredDialogRect(short width, short height) {
	TRect desk(0, 0, width, height);

	if (TProgram::deskTop != nullptr)
		desk = TProgram::deskTop->getExtent();
	const short deskWidth = desk.b.x - desk.a.x;
	const short deskHeight = desk.b.y - desk.a.y;
	const short left = desk.a.x + std::max<short>(0, (deskWidth - width) / 2);
	const short top = desk.a.y + std::max<short>(0, (deskHeight - height) / 2);
	return TRect(left, top, left + width, top + height);
}

std::string escapeRegexLiteral(std::string_view value) {
	static constexpr const char *kMetaChars = R"(\.^$|()[]{}*+?-)";
	std::string escaped;

	escaped.reserve(value.size() * 2);
	for (char ch : value) {
		if (std::strchr(kMetaChars, ch) != nullptr)
			escaped.push_back('\\');
		escaped.push_back(ch);
	}
	return escaped;
}

std::string buildSearchPatternExpression(const std::string &pattern, MRSearchTextType type) {
	if (type == MRSearchTextType::Pcre)
		return pattern;

	const std::string literal = escapeRegexLiteral(pattern);
	if (type == MRSearchTextType::Word)
		return std::string("\\b") + literal + "\\b";
	return literal;
}

const char *wrappedSearchMessage(MRSearchDirection direction) {
	return direction == MRSearchDirection::Backward ? "search wrapped to EOF"
	                                                : "search wrapped to TOF";
}

void postSearchWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text),
	                               mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

struct SearchPreviewParts {
	std::string text;
	std::size_t matchOffset = 0;
	std::size_t matchLength = 0;
	std::string previousLine;
	std::string matchLine;
	std::string nextLine;
};

SearchPreviewParts previewForMatch(const std::string &text, std::size_t start, std::size_t end) {
	auto sanitizeLine = [](std::string value) {
		for (char &ch : value)
			if (ch == '\t' || ch == '\r' || ch == '\n')
				ch = ' ';
		return value;
	};
	const std::size_t safeStart = std::min(start, text.size());
	const std::size_t safeEnd = std::min(std::max(end, safeStart), text.size());
	const std::size_t lineStart = text.rfind('\n', safeStart == 0 ? 0 : safeStart - 1);
	const std::size_t left = (lineStart == std::string::npos) ? 0 : lineStart + 1;
	const std::size_t lineEnd = text.find('\n', safeStart);
	const std::size_t right = (lineEnd == std::string::npos) ? text.size() : lineEnd;
	const std::size_t windowLeft = safeStart > 24 ? safeStart - 24 : left;
	const std::size_t windowRight = std::min(right, safeEnd + 24);
	std::string leftText = text.substr(windowLeft, safeStart - windowLeft);
	std::string matchText = text.substr(safeStart, safeEnd - safeStart);
	std::string rightText = text.substr(safeEnd, windowRight - safeEnd);
	SearchPreviewParts parts;

	parts.matchOffset = leftText.size();
	parts.matchLength = matchText.size();
	parts.text = leftText + matchText + rightText;
	parts.text = sanitizeLine(parts.text);
	parts.matchLine = sanitizeLine(text.substr(left, right - left));
	if (left > 0) {
		const std::size_t prevEnd = left - 1;
		const std::size_t prevStartBreak = text.rfind('\n', prevEnd == 0 ? 0 : prevEnd - 1);
		const std::size_t prevStart = prevStartBreak == std::string::npos ? 0 : prevStartBreak + 1;
		parts.previousLine = sanitizeLine(text.substr(prevStart, prevEnd - prevStart));
	}
	if (right < text.size()) {
		const std::size_t nextStart = right + 1;
		const std::size_t nextEndBreak = text.find('\n', nextStart);
		const std::size_t nextEnd = nextEndBreak == std::string::npos ? text.size() : nextEndBreak;
		parts.nextLine = sanitizeLine(text.substr(nextStart, nextEnd - nextStart));
	}
	return parts;
}

std::size_t centeredPreviewLeft(const std::string &line, std::size_t matchOffset, std::size_t matchLength,
                                std::size_t width) {
	if (line.empty() || width == 0 || line.size() <= width)
		return 0;
	const std::size_t safeOffset = std::min(matchOffset, line.size());
	const std::size_t safeLength = std::min(matchLength, line.size() - safeOffset);
	const std::size_t center = safeOffset + safeLength / 2;
	const std::size_t maxLeft = line.size() - width;
	std::size_t left = center > width / 2 ? center - width / 2 : 0;

	if (safeLength > 0) {
		const std::size_t minLeft = (safeOffset + safeLength > width) ? safeOffset + safeLength - width : 0;
		const std::size_t maxLeftForVisibleStart = safeOffset;
		left = std::max(left, minLeft);
		left = std::min(left, maxLeftForVisibleStart);
	}
	return std::min(left, maxLeft);
}

void lineColumnForOffset(const std::string &text, std::size_t offset, std::size_t &line, std::size_t &column) {
	const std::size_t safe = std::min(offset, text.size());
	std::size_t currentLine = 1;
	std::size_t lastLineStart = 0;

	for (std::size_t i = 0; i < safe; ++i)
		if (text[i] == '\n') {
			++currentLine;
			lastLineStart = i + 1;
		}
	line = currentLine;
	column = safe - lastLineStart + 1;
}

bool collectRegexMatches(const std::string &text, pcre2_code *code, std::vector<SearchMatchEntry> &outMatches) {
	pcre2_match_data *matchData = nullptr;
	std::size_t seek = 0;

	outMatches.clear();
	matchData = pcre2_match_data_create_from_pattern(code, nullptr);
	if (matchData == nullptr)
		return false;
	while (seek <= text.size()) {
		int rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(text.data()),
		                     static_cast<PCRE2_SIZE>(text.size()), static_cast<PCRE2_SIZE>(seek), 0, matchData,
		                     nullptr);
		PCRE2_SIZE *ovector = nullptr;
		std::size_t start = 0;
		std::size_t end = 0;
		SearchMatchEntry entry;

		if (rc < 0)
			break;
		ovector = pcre2_get_ovector_pointer(matchData);
		start = static_cast<std::size_t>(ovector[0]);
		end = static_cast<std::size_t>(ovector[1]);
		if (end < start || end > text.size())
			break;
		entry.start = start;
		entry.end = end;
		lineColumnForOffset(text, start, entry.line, entry.column);
		{
			SearchPreviewParts preview = previewForMatch(text, start, end);
			entry.preview = preview.text;
			entry.previewMatchOffset = preview.matchOffset;
			entry.previewMatchLength = preview.matchLength;
		}
		outMatches.push_back(entry);
		if (end > seek)
			seek = end;
		else
			++seek;
	}
	pcre2_match_data_free(matchData);
	return true;
}

void activateMatch(TMREditWindow *win, const SearchMatchEntry &match, const std::string &pattern,
                   const MRSearchDialogOptions &options);

class FoundListView : public TListViewer {
  public:
	FoundListView(const TRect &bounds, TScrollBar *aScrollBar, TMREditWindow *aWindow,
	              const std::vector<SearchMatchEntry> &aEntries, const std::string &aPattern,
	              const MRSearchDialogOptions &aOptions) noexcept
	    : TListViewer(bounds, 1, nullptr, aScrollBar), window_(aWindow), entries_(aEntries),
	      pattern_(aPattern), options_(aOptions) {
		setRange(static_cast<short>(entries_.size()));
	}

	void getText(char *dest, short item, short maxLen) override {
		std::size_t copyLen = 0;

		if (dest == nullptr || maxLen <= 0)
			return;
		if (item < 0 || static_cast<std::size_t>(item) >= entries_.size()) {
			dest[0] = EOS;
			return;
		}
		copyLen = static_cast<std::size_t>(maxLen - 1);
		std::strncpy(dest, entries_[static_cast<std::size_t>(item)].preview.c_str(), copyLen);
		dest[copyLen] = EOS;
	}

	void draw() override {
		TDrawBuffer buffer;
		const bool active = (state & (sfSelected | sfActive)) == (sfSelected | sfActive);
		const TColorAttr normalColor = getColor(active ? 1 : 2);
		const TColorAttr selectedColor = getColor(4);
		const TColorAttr focusedColor =
		    active ? static_cast<TColorAttr>(getColor(3)) : selectedColor;

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
				const SearchMatchEntry &entry = entries_[static_cast<std::size_t>(item)];
				ushort x = 1;
				ushort limit = static_cast<ushort>(std::max(0, size.x - 1));
				auto drawSegment = [&](const std::string &segment, TColorAttr color) {
					for (char ch : segment) {
						if (x >= limit)
							break;
						buffer.putChar(x, static_cast<uchar>(ch));
						buffer.putAttribute(x, color);
						++x;
					}
				};
				std::size_t splitA = std::min(entry.previewMatchOffset, entry.preview.size());
				std::size_t splitB =
				    std::min(splitA + entry.previewMatchLength, entry.preview.size());
				drawSegment(entry.preview.substr(0, splitA), rowColor);
				drawSegment(entry.preview.substr(splitA, splitB - splitA), matchColor);
				drawSegment(entry.preview.substr(splitB), rowColor);
			}
			writeLine(0, row, size.x, 1, buffer);
		}
	}

	void handleEvent(TEvent &event) override {
		const bool isDoubleClickActivation =
		    event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0 &&
		    (event.mouse.eventFlags & meDoubleClick) != 0;
		const short oldFocused = focused;

		TListViewer::handleEvent(event);
		if (focused != oldFocused)
			previewFocusedItem();
		if (isDoubleClickActivation && focused >= 0 && focused < range) {
			message(owner, evCommand, cmOK, nullptr);
			clearEvent(event);
		}
		if (event.what == evKeyDown && ctrlToArrow(event.keyDown.keyCode) == kbEnter &&
		    focused >= 0 && focused < range) {
			message(owner, evCommand, cmOK, nullptr);
			clearEvent(event);
		}
	}

	void previewFocusedItem() {
		if (focused < 0 || focused >= range || static_cast<std::size_t>(focused) >= entries_.size() ||
		    window_ == nullptr)
			return;
		activateMatch(window_, entries_[static_cast<std::size_t>(focused)], pattern_, options_);
	}

  private:
	TMREditWindow *window_;
	const std::vector<SearchMatchEntry> &entries_;
	const std::string &pattern_;
	const MRSearchDialogOptions &options_;
};

bool showFoundListDialog(TMREditWindow *win, const std::string &pattern,
                         const MRSearchDialogOptions &options,
                         const std::vector<SearchMatchEntry> &matches, std::size_t &selectedIndex) {
	TDialog *dialog = nullptr;
	TScrollBar *scrollBar = nullptr;
	FoundListView *listView = nullptr;
	ushort result = cmCancel;
	const int visibleRows = std::max<int>(6, std::min<int>(static_cast<int>(matches.size()), 13));
	const short width = 92;
	const short height = static_cast<short>(visibleRows + 8);
	const short buttonY = static_cast<short>(height - 3);

	if (TProgram::deskTop == nullptr || matches.empty())
		return false;
	dialog = new TDialog(centeredDialogRect(width, height), "FOUND LIST");
	scrollBar = new TScrollBar(TRect(width - 3, 2, width - 2, height - 4));
	dialog->insert(scrollBar);
	listView = new FoundListView(TRect(2, 2, width - 3, height - 4), scrollBar, win, matches, pattern, options);
	if (selectedIndex < matches.size())
		listView->focusItemNum(static_cast<short>(selectedIndex));
	listView->previewFocusedItem();
	dialog->insert(listView);
	dialog->insert(new TButton(TRect(width / 2 - 12, buttonY, width / 2 - 2, buttonY + 2), "~D~one", cmOK, bfDefault));
	dialog->insert(new TButton(TRect(width / 2 + 1, buttonY, width / 2 + 13, buttonY + 2), "~C~ancel", cmCancel,
	                           bfNormal));
	result = TProgram::deskTop->execView(dialog);
	if (result == cmOK && listView->focused >= 0 &&
	    static_cast<std::size_t>(listView->focused) < matches.size()) {
		selectedIndex = static_cast<std::size_t>(listView->focused);
		TObject::destroy(dialog);
		return true;
	}
	TObject::destroy(dialog);
	return false;
}

PromptReplaceDecision promptReplaceDecisionDialog(const std::string &title, const SearchPreviewParts &preview,
                                                  const std::string &replacement) {
	class PromptPreviewView : public TView {
	  public:
		PromptPreviewView(const TRect &bounds, const SearchPreviewParts &preview,
		                  const std::string &replacement)
		    : TView(bounds), before_(preview.matchLine), beforeMatchOffset_(preview.matchOffset),
		      beforeMatchLength_(preview.matchLength), after_(), afterMatchOffset_(preview.matchOffset),
		      afterMatchLength_(replacement.size()) {
			const std::size_t safeOffset = std::min(beforeMatchOffset_, before_.size());
			const std::size_t safeLength = std::min(beforeMatchLength_, before_.size() - safeOffset);

			after_ = before_.substr(0, safeOffset);
			after_ += replacement;
			if (safeOffset + safeLength <= before_.size())
				after_ += before_.substr(safeOffset + safeLength);
			for (char &ch : after_)
				if (ch == '\t' || ch == '\r' || ch == '\n')
					ch = ' ';
			beforeMatchOffset_ = safeOffset;
			beforeMatchLength_ = safeLength;
		}

		void draw() override {
			TDrawBuffer b;
			const TColorAttr normal = getColor(1);
			const TColorAttr accent = getColor(3);
			const std::size_t width = static_cast<std::size_t>(std::max<int>(0, size.x));
			const std::size_t beforeLeft = centeredPreviewLeft(before_, beforeMatchOffset_, beforeMatchLength_, width);
			const std::size_t afterLeft = centeredPreviewLeft(after_, afterMatchOffset_, afterMatchLength_, width);
			auto drawLine = [&](short y, const std::string &line, std::size_t lineLeft, std::size_t markOffset,
			                    std::size_t markLength) {
				b.moveChar(0, ' ', normal, size.x);
				for (ushort x = 0; x < static_cast<ushort>(size.x); ++x) {
					const std::size_t source = lineLeft + static_cast<std::size_t>(x);
					char ch = source < line.size() ? line[source] : ' ';
					const bool inMark = source >= markOffset && source < (markOffset + markLength);
					b.putChar(x, static_cast<uchar>(ch));
					b.putAttribute(x, inMark ? accent : normal);
				}
				writeLine(0, y, size.x, 1, b);
			};

			for (short y = 0; y < size.y; ++y) {
				b.moveChar(0, ' ', normal, size.x);
				writeLine(0, y, size.x, 1, b);
			}
			const short centerY = size.y / 2;
			drawLine(static_cast<short>(std::max(0, centerY - 1)), before_, beforeLeft, beforeMatchOffset_,
			         beforeMatchLength_);
			b.moveChar(0, ' ', normal, size.x);
			b.moveStr(static_cast<ushort>(std::max(0, (size.x - 2) / 2)), "->", accent, size.x);
			writeLine(0, centerY, size.x, 1, b);
			drawLine(static_cast<short>(std::min<int>(size.y - 1, centerY + 1)), after_, afterLeft,
			         afterMatchOffset_, afterMatchLength_);
		}

	  private:
		std::string before_;
		std::size_t beforeMatchOffset_;
		std::size_t beforeMatchLength_;
		std::string after_;
		std::size_t afterMatchOffset_;
		std::size_t afterMatchLength_;
	};

	TDialog *dialog = nullptr;
	ushort result = cmCancel;

	if (TProgram::deskTop == nullptr)
		return PromptReplaceDecision::Cancel;
	dialog = new TDialog(centeredDialogRect(88, 14), title.c_str());
	dialog->insert(new PromptPreviewView(TRect(2, 2, 86, 8), preview, replacement));
	dialog->insert(new TButton(TRect(10, 10, 24, 12), "~R~eplace", cmOK, bfDefault));
	dialog->insert(new TButton(TRect(26, 10, 38, 12), "~S~kip", cmNo, bfNormal));
	dialog->insert(new TButton(TRect(40, 10, 58, 12), "Replace ~A~ll", cmYes, bfNormal));
	dialog->insert(new TButton(TRect(60, 10, 74, 12), "~C~ancel", cmCancel, bfNormal));
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	if (result == cmOK)
		return PromptReplaceDecision::Replace;
	if (result == cmNo)
		return PromptReplaceDecision::Skip;
	if (result == cmYes)
		return PromptReplaceDecision::ReplaceAll;
	return PromptReplaceDecision::Cancel;
}

PromptSearchDecision promptSearchDecisionDialog(const SearchPreviewParts &preview) {
	class SearchPromptPreviewView : public TView {
	  public:
		SearchPromptPreviewView(const TRect &bounds, const SearchPreviewParts &preview)
		    : TView(bounds), preview_(preview) {
		}

		void draw() override {
			TDrawBuffer b;
			const TColorAttr normal = getColor(1);
			const TColorAttr accent = getColor(3);
			const std::size_t width = static_cast<std::size_t>(std::max<int>(0, size.x));
			const std::size_t lineLeft =
			    centeredPreviewLeft(preview_.matchLine, preview_.matchOffset, preview_.matchLength, width);
			auto drawLine = [&](short y, const std::string &line, bool highlightMatch) {
				b.moveChar(0, ' ', normal, size.x);
				for (ushort x = 0; x < static_cast<ushort>(size.x); ++x) {
					const std::size_t source = lineLeft + static_cast<std::size_t>(x);
					char ch = source < line.size() ? line[source] : ' ';
					const bool inMatch =
					    highlightMatch && source >= preview_.matchOffset &&
					    source < (preview_.matchOffset + preview_.matchLength);
					b.putChar(x, static_cast<uchar>(ch));
					b.putAttribute(x, inMatch ? accent : normal);
				}
				writeLine(0, y, size.x, 1, b);
			};

			for (short y = 0; y < size.y; ++y) {
				b.moveChar(0, ' ', normal, size.x);
				writeLine(0, y, size.x, 1, b);
			}
			const short centerY = size.y / 2;
			drawLine(static_cast<short>(std::max(0, centerY - 1)), preview_.previousLine, false);
			drawLine(centerY, preview_.matchLine, true);
			drawLine(static_cast<short>(std::min<int>(size.y - 1, centerY + 1)), preview_.nextLine, false);
		}

	  private:
		SearchPreviewParts preview_;
	};

	TDialog *dialog = nullptr;
	ushort result = cmCancel;

	if (TProgram::deskTop == nullptr)
		return PromptSearchDecision::Cancel;
	dialog = new TDialog(centeredDialogRect(88, 13), "SEARCH");
	dialog->insert(new SearchPromptPreviewView(TRect(2, 2, 86, 9), preview));
	dialog->insert(new TButton(TRect(20, 10, 32, 12), "~N~ext", cmOK, bfDefault));
	dialog->insert(new TButton(TRect(34, 10, 46, 12), "~S~top", cmNo, bfNormal));
	dialog->insert(new TButton(TRect(48, 10, 62, 12), "~C~ancel", cmCancel, bfNormal));
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	if (result == cmOK)
		return PromptSearchDecision::Next;
	if (result == cmNo)
		return PromptSearchDecision::Stop;
	return PromptSearchDecision::Cancel;
}

bool promptSearchPattern(std::string &pattern, MRSearchDialogOptions &options) {
	enum { kInputBufferSize = 256 };
	char patternInput[kInputBufferSize];
	ushort typeChoice = 0;
	ushort directionChoice = options.direction == MRSearchDirection::Backward ? 1 : 0;
	ushort modeChoice = 0;
	ushort optionMask = 0;
	ushort result = cmCancel;
	TDialog *dialog = nullptr;
	TInputLine *patternField = nullptr;
	TRadioButtons *typeField = nullptr;
	TRadioButtons *directionField = nullptr;
	TRadioButtons *modeField = nullptr;
	TCheckBoxes *optionsField = nullptr;

	std::memset(patternInput, 0, sizeof(patternInput));
	if (TProgram::deskTop == nullptr)
		return false;
	if (!g_searchUiState.pattern.empty())
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
	if (options.caseSensitive)
		optionMask |= 0x0001;
	if (options.globalSearch)
		optionMask |= 0x0002;
	if (options.restrictToMarkedBlock)
		optionMask |= 0x0004;
	if (options.searchAllWindows)
		optionMask |= 0x0008;

	dialog = new TDialog(centeredDialogRect(94, 22), "SEARCH");
	patternField = new TInputLine(TRect(17, 2, 92, 3), kInputBufferSize - 1);
	dialog->insert(new TLabel(TRect(4, 2, 17, 3), "Search ~f~or:", patternField));
	dialog->insert(patternField);
	dialog->insert(new TStaticText(TRect(4, 4, 11, 5), "Type:"));
	typeField = new TRadioButtons(
	    TRect(4, 5, 41, 8),
	    new TSItem(" ~L~iteral",
	               new TSItem(" ~R~egular expressions (PCRE)",
	                          new TSItem(" ~W~ord/Phrase search", nullptr))));
	dialog->insert(typeField);
	dialog->insert(new TStaticText(TRect(43, 4, 54, 5), "Direction:"));
	directionField = new TRadioButtons(TRect(43, 5, 60, 8),
	                                   new TSItem(" ~F~orward", new TSItem(" ~B~ackward", nullptr)));
	dialog->insert(directionField);
	dialog->insert(new TStaticText(TRect(62, 4, 68, 5), "Mode:"));
	modeField = new TRadioButtons(TRect(62, 5, 92, 8),
	                              new TSItem(" ~S~top on first occurrence",
	                                         new TSItem(" ~P~rompt for next match",
	                                                    new TSItem(" L~i~st all occurrences", nullptr))));
	dialog->insert(modeField);
	dialog->insert(new TStaticText(TRect(4, 10, 13, 11), "Options:"));
	optionsField = new TCheckBoxes(
	    TRect(4, 11, 41, 16),
	    new TSItem("~C~ase sensitive",
	               new TSItem("~G~lobal search",
	                          new TSItem("~R~estrict to marked block",
	                                     new TSItem("Search all ~w~indows", nullptr)))));
	dialog->insert(optionsField);
	dialog->insert(new TButton(TRect(36, 18, 48, 20), "~O~K", cmOK, bfDefault));
	dialog->insert(new TButton(TRect(51, 18, 64, 20), "~C~ancel", cmCancel, bfNormal));
	patternField->setData(patternInput);
	typeField->setData(&typeChoice);
	directionField->setData(&directionChoice);
	modeField->setData(&modeChoice);
	optionsField->setData(&optionMask);
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel) {
		patternField->getData(patternInput);
		typeField->getData(&typeChoice);
		directionField->getData(&directionChoice);
		modeField->getData(&modeChoice);
		optionsField->getData(&optionMask);
		pattern = trimAscii(patternInput);
		options.textType = typeChoice == 1 ? MRSearchTextType::Pcre
		                                   : (typeChoice == 2 ? MRSearchTextType::Word : MRSearchTextType::Literal);
		options.direction = directionChoice == 1 ? MRSearchDirection::Backward : MRSearchDirection::Forward;
		options.mode = modeChoice == 1 ? MRSearchMode::PromptNext
		                               : (modeChoice == 2 ? MRSearchMode::ListAll : MRSearchMode::StopFirst);
		options.caseSensitive = (optionMask & 0x0001) != 0;
		options.globalSearch = (optionMask & 0x0002) != 0;
		options.restrictToMarkedBlock = (optionMask & 0x0004) != 0;
		options.searchAllWindows = (optionMask & 0x0008) != 0;
		static_cast<void>(setConfiguredSearchDialogOptions(options));
	}
	TObject::destroy(dialog);
	return result != cmCancel;
}

bool promptReplaceValues(std::string &pattern, std::string &replacement, MRSarDialogOptions &options) {
	enum { kPatternBufferSize = 256, kReplacementBufferSize = 256 };
	char patternInput[kPatternBufferSize];
	char replacementInput[kReplacementBufferSize];
	ushort typeChoice = 0;
	ushort directionChoice = options.direction == MRSearchDirection::Backward ? 1 : 0;
	ushort modeChoice = 0;
	ushort leaveCursorChoice =
	    options.leaveCursorAt == MRSarLeaveCursor::StartOfReplaceString ? 1 : 0;
	ushort optionMask = 0;
	ushort result = cmCancel;
	TDialog *dialog = nullptr;
	TInputLine *patternField = nullptr;
	TInputLine *replacementField = nullptr;
	TRadioButtons *typeField = nullptr;
	TRadioButtons *directionField = nullptr;
	TRadioButtons *modeField = nullptr;
	TRadioButtons *leaveCursorField = nullptr;
	TCheckBoxes *optionsField = nullptr;

	std::memset(patternInput, 0, sizeof(patternInput));
	std::memset(replacementInput, 0, sizeof(replacementInput));
	if (TProgram::deskTop == nullptr)
		return false;
	if (!g_searchUiState.pattern.empty())
		strnzcpy(patternInput, g_searchUiState.pattern.c_str(), sizeof(patternInput));
	if (!g_searchUiState.replacement.empty())
		strnzcpy(replacementInput, g_searchUiState.replacement.c_str(), sizeof(replacementInput));

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
	if (options.caseSensitive)
		optionMask |= 0x0001;
	if (options.globalSearch)
		optionMask |= 0x0002;
	if (options.restrictToMarkedBlock)
		optionMask |= 0x0004;
	if (options.searchAllWindows)
		optionMask |= 0x0008;

	dialog = new TDialog(centeredDialogRect(92, 24), "SEARCH AND REPLACE");
	patternField = new TInputLine(TRect(17, 2, 89, 3), kPatternBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 2, 16, 3), "Search ~f~or:", patternField));
	dialog->insert(patternField);
	replacementField = new TInputLine(TRect(17, 4, 89, 5), kReplacementBufferSize - 1);
	dialog->insert(new TLabel(TRect(2, 4, 16, 5), "Replace ~w~ith:", replacementField));
	dialog->insert(replacementField);
	dialog->insert(new TStaticText(TRect(3, 6, 10, 7), "Type:"));
	typeField = new TRadioButtons(
	    TRect(3, 7, 37, 10),
	    new TSItem(" ~L~iteral",
	               new TSItem(" ~R~egular expressions (PCRE)",
	                          new TSItem(" ~W~ord/Phrase search", nullptr))));
	dialog->insert(typeField);
	dialog->insert(new TStaticText(TRect(39, 6, 50, 7), "Direction:"));
	directionField = new TRadioButtons(TRect(39, 7, 56, 10),
	                                   new TSItem(" ~F~orward", new TSItem(" ~B~ackward", nullptr)));
	dialog->insert(directionField);
	dialog->insert(new TStaticText(TRect(58, 6, 64, 7), "Mode:"));
	modeField = new TRadioButtons(
	    TRect(58, 7, 89, 10),
	    new TSItem(" Replace ~f~irst occurrence",
	               new TSItem(" ~P~rompt for each replace",
	                          new TSItem(" Replace ~a~ll occurrences", nullptr))));
	dialog->insert(modeField);
	dialog->insert(new TStaticText(TRect(3, 12, 20, 13), "Leave cursor at:"));
	leaveCursorField =
	    new TRadioButtons(TRect(3, 13, 37, 16),
	                      new TSItem(" ~E~nd of replace string",
	                                 new TSItem(" ~S~tart of replace string", nullptr)));
	dialog->insert(leaveCursorField);
	dialog->insert(new TStaticText(TRect(39, 12, 48, 13), "Options:"));
	optionsField = new TCheckBoxes(
	    TRect(39, 13, 72, 18),
	    new TSItem("~C~ase sensitive",
	               new TSItem("~G~lobal search",
	                          new TSItem("~R~estrict to marked block",
	                                     new TSItem("Search all ~w~indows", nullptr)))));
	dialog->insert(optionsField);
	dialog->insert(new TButton(TRect(34, 19, 46, 21), "~O~K", cmOK, bfDefault));
	dialog->insert(new TButton(TRect(49, 19, 64, 21), "~C~ancel", cmCancel, bfNormal));
	patternField->setData(patternInput);
	replacementField->setData(replacementInput);
	typeField->setData(&typeChoice);
	directionField->setData(&directionChoice);
	modeField->setData(&modeChoice);
	leaveCursorField->setData(&leaveCursorChoice);
	optionsField->setData(&optionMask);
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
		options.textType = typeChoice == 1 ? MRSearchTextType::Pcre
		                                   : (typeChoice == 2 ? MRSearchTextType::Word : MRSearchTextType::Literal);
		options.direction = directionChoice == 1 ? MRSearchDirection::Backward : MRSearchDirection::Forward;
		options.mode = modeChoice == 1 ? MRSarMode::PromptEach
		                               : (modeChoice == 2 ? MRSarMode::ReplaceAll : MRSarMode::ReplaceFirst);
		options.leaveCursorAt = leaveCursorChoice == 1 ? MRSarLeaveCursor::StartOfReplaceString
		                                               : MRSarLeaveCursor::EndOfReplaceString;
		options.caseSensitive = (optionMask & 0x0001) != 0;
		options.globalSearch = (optionMask & 0x0002) != 0;
		options.restrictToMarkedBlock = (optionMask & 0x0004) != 0;
		options.searchAllWindows = (optionMask & 0x0008) != 0;
		static_cast<void>(setConfiguredSarDialogOptions(options));
	}
	TObject::destroy(dialog);
	return result != cmCancel;
}

bool promptGotoLineNumber(long &lineNumber) {
	enum { kInputBufferSize = 32 };
	char input[kInputBufferSize];
	char *endPtr = nullptr;
	long parsed = 0;
	std::string tail;
	uchar limit = static_cast<uchar>(kInputBufferSize - 1);

	std::memset(input, 0, sizeof(input));
	if (inputBox("GOTO LINE NUMBER", "~L~ine", input, limit) == cmCancel)
		return false;
	parsed = std::strtol(input, &endPtr, 10);
	tail = trimAscii(endPtr != nullptr ? endPtr : "");
	if (endPtr == input || !tail.empty() || parsed < 1) {
		messageBox(mfError | mfOKButton, "Please enter a valid line number.");
		return false;
	}
	lineNumber = parsed;
	return true;
}

bool compileSearchRegex(const std::string &patternExpression, bool ignoreCase, pcre2_code **outCode,
                        std::string &errorText) {
	int errorCode = 0;
	PCRE2_SIZE errorOffset = 0;
	uint32_t options = PCRE2_UTF | PCRE2_UCP;
	char errorBuffer[256];
	int messageLength = 0;

	*outCode = nullptr;
	if (ignoreCase)
		options |= PCRE2_CASELESS;
	*outCode = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(patternExpression.c_str()),
	                         static_cast<PCRE2_SIZE>(patternExpression.size()), options, &errorCode,
	                         &errorOffset, nullptr);
	if (*outCode != nullptr) {
		errorText.clear();
		return true;
	}
	std::memset(errorBuffer, 0, sizeof(errorBuffer));
	messageLength = static_cast<int>(
	    pcre2_get_error_message(errorCode, reinterpret_cast<PCRE2_UCHAR *>(errorBuffer), sizeof(errorBuffer)));
	if (messageLength < 0)
		errorText = "Regex compile error.";
	else
		errorText = std::string(errorBuffer, static_cast<std::size_t>(messageLength));
	errorText += " (offset ";
	errorText += std::to_string(static_cast<unsigned long long>(errorOffset));
	errorText += ")";
	return false;
}

bool findRegexForward(const std::string &text, pcre2_code *code, std::size_t startOffset,
                      std::size_t &matchStart, std::size_t &matchEnd) {
	pcre2_match_data *matchData = nullptr;
	PCRE2_SIZE *ovector = nullptr;
	int rc = 0;
	const std::size_t safeStart = std::min(startOffset, text.size());

	matchData = pcre2_match_data_create_from_pattern(code, nullptr);
	if (matchData == nullptr)
		return false;
	rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(text.data()), static_cast<PCRE2_SIZE>(text.size()),
	                 static_cast<PCRE2_SIZE>(safeStart), 0, matchData, nullptr);
	if (rc < 0) {
		pcre2_match_data_free(matchData);
		return false;
	}
	ovector = pcre2_get_ovector_pointer(matchData);
	matchStart = static_cast<std::size_t>(ovector[0]);
	matchEnd = static_cast<std::size_t>(ovector[1]);
	pcre2_match_data_free(matchData);
	return matchEnd >= matchStart;
}

bool findRegexForwardInRange(const std::string &text, pcre2_code *code, std::size_t startOffset,
                             std::size_t rangeStart, std::size_t rangeEnd,
                             std::size_t &matchStart, std::size_t &matchEnd) {
	const std::size_t safeStart = std::min(std::max(startOffset, rangeStart), text.size());
	const std::size_t safeEnd = std::min(rangeEnd, text.size());
	std::size_t seek = safeStart;

	if (safeStart >= safeEnd)
		return false;
	while (seek < safeEnd) {
		std::size_t nextStart = 0;
		std::size_t nextEnd = 0;
		if (!findRegexForward(text, code, seek, nextStart, nextEnd))
			return false;
		if (nextStart >= safeEnd)
			return false;
		if (nextStart >= rangeStart && nextEnd <= safeEnd) {
			matchStart = nextStart;
			matchEnd = nextEnd;
			return true;
		}
		if (nextEnd > seek)
			seek = nextEnd;
		else
			++seek;
	}
	return false;
}

bool findLastRegexBeforeLimit(const std::string &text, pcre2_code *code, std::size_t limitOffset,
                              std::size_t rangeStart, std::size_t rangeEnd,
                              std::size_t &matchStart, std::size_t &matchEnd) {
	const std::size_t boundedStart = std::min(rangeStart, text.size());
	const std::size_t boundedEnd = std::min(rangeEnd, text.size());
	const std::size_t limit = std::min(std::max(limitOffset, boundedStart), boundedEnd);
	std::size_t candidateStart = 0;
	std::size_t candidateEnd = 0;
	std::size_t seek = boundedStart;
	bool found = false;

	if (boundedStart >= boundedEnd)
		return false;
	while (seek < boundedEnd) {
		std::size_t nextStart = 0;
		std::size_t nextEnd = 0;

		if (!findRegexForwardInRange(text, code, seek, boundedStart, boundedEnd, nextStart, nextEnd))
			break;
		if (nextStart >= limit)
			break;
		candidateStart = nextStart;
		candidateEnd = nextEnd;
		found = true;
		if (nextEnd > seek)
			seek = nextEnd;
		else
			++seek;
	}
	if (!found)
		return false;
	matchStart = candidateStart;
	matchEnd = candidateEnd;
	return true;
}

bool findRegexWithWrap(const std::string &text, pcre2_code *code, std::size_t startOffset,
                       MRSearchDirection direction, std::size_t rangeStart, std::size_t rangeEnd,
                       bool allowWrap, std::size_t &matchStart, std::size_t &matchEnd, bool &wrapped) {
	const std::size_t safeRangeStart = std::min(rangeStart, text.size());
	const std::size_t safeRangeEnd = std::min(rangeEnd, text.size());
	const std::size_t safeStart = std::min(std::max(startOffset, safeRangeStart), safeRangeEnd);

	wrapped = false;
	if (direction == MRSearchDirection::Forward) {
		if (findRegexForwardInRange(text, code, safeStart, safeRangeStart, safeRangeEnd, matchStart, matchEnd))
			return true;
		if (!allowWrap)
			return false;
		if (safeStart <= safeRangeStart)
			return false;
		if (findRegexForwardInRange(text, code, safeRangeStart, safeRangeStart, safeRangeEnd, matchStart, matchEnd)) {
			wrapped = true;
			return true;
		}
		return false;
	}
	if (findLastRegexBeforeLimit(text, code, safeStart, safeRangeStart, safeRangeEnd, matchStart, matchEnd))
		return true;
	if (!allowWrap)
		return false;
	if (safeStart >= safeRangeEnd)
		return false;
	if (findLastRegexBeforeLimit(text, code, safeRangeEnd, safeRangeStart, safeRangeEnd, matchStart, matchEnd)) {
		wrapped = true;
		return true;
	}
	return false;
}

void syncVmLastSearch(TMREditWindow *win, bool valid, std::size_t start, std::size_t end,
                      std::size_t cursor) {
	std::string fileName;
	if (win == nullptr)
		return;
	fileName = win->currentFileName();
	mrvmUiReplaceWindowLastSearch(win, fileName, valid, start, end, cursor);
}

void clearSearchSelection(TMREditWindow *win) {
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	const std::size_t cursor = editor != nullptr ? editor->cursorOffset() : 0;

	if (editor == nullptr)
		return;
	editor->setSelectionOffsets(cursor, cursor);
	syncVmLastSearch(win, false, 0, 0, cursor);
}

void updateMiniMapFindMarkers(TMREditWindow *win, const std::string &pattern,
                              const MRSearchDialogOptions &options) {
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::vector<std::pair<std::size_t, std::size_t>> ranges;
	pcre2_code *code = nullptr;
	std::string regexError;
	std::string text;
	std::vector<SearchMatchEntry> matches;
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;

	if (editor == nullptr)
		return;
	if (pattern.empty()) {
		editor->clearFindMarkerRanges();
		return;
	}
	if (!compileSearchRegex(buildSearchPatternExpression(pattern, options.textType), !options.caseSensitive,
	                        &code, regexError)) {
		editor->clearFindMarkerRanges();
		return;
	}
	text = editor->snapshotText();
	rangeStart = 0;
	rangeEnd = text.size();
	if (options.restrictToMarkedBlock) {
		rangeStart = std::min(editor->selectionStartOffset(), editor->selectionEndOffset());
		rangeEnd = std::max(editor->selectionStartOffset(), editor->selectionEndOffset());
		if (rangeStart >= rangeEnd) {
			pcre2_code_free(code);
			editor->clearFindMarkerRanges();
			return;
		}
	}
	static_cast<void>(collectRegexMatches(text, code, matches));
	pcre2_code_free(code);
	for (const SearchMatchEntry &match : matches) {
		if (match.start < rangeStart || match.end > rangeEnd)
			continue;
		std::size_t start = std::min(match.start, text.size());
		std::size_t end = std::min(std::max(match.end, start), text.size());
		if (end == start) {
			if (end < text.size())
				++end;
			else if (start > 0)
				--start;
		}
		if (end > start)
			ranges.push_back(std::make_pair(start, end));
	}
	editor->setFindMarkerRanges(ranges);
}

void activateMatch(TMREditWindow *win, const SearchMatchEntry &match, const std::string &pattern,
                   const MRSearchDialogOptions &options) {
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::size_t end = std::max(match.end, match.start);

	if (editor == nullptr)
		return;
	if (end == match.start) {
		if (end < editor->bufferLength())
			++end;
		else if (match.start > 0)
			--end;
	}
	editor->setCursorOffset(match.start);
	editor->setSelectionOffsets(match.start, end);
	editor->revealCursor(True);
	syncVmLastSearch(win, true, match.start, end, match.start);
	g_searchUiState.hasPrevious = true;
	g_searchUiState.pattern = pattern;
	g_searchUiState.lastStart = match.start;
	g_searchUiState.lastEnd = end;
	g_searchUiState.lastOptions = options;
}

bool performSearch(TMREditWindow *win, const std::string &pattern, std::size_t startOffset, bool updateState,
                   const MRSearchDialogOptions &options, bool showNotFoundMessage,
                   bool *outWrapped = nullptr) {
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
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

	if (outWrapped != nullptr)
		*outWrapped = false;
	if (editor == nullptr)
		return false;
	if (pattern.empty()) {
		messageBox(mfError | mfOKButton, "Search text must not be empty.");
		return false;
	}
	patternExpression = buildSearchPatternExpression(pattern, options.textType);
	if (!compileSearchRegex(patternExpression, !options.caseSensitive, &code, regexError)) {
		messageBox(mfError | mfOKButton, "Invalid search pattern:\n%s", regexError.c_str());
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
			messageBox(mfInformation | mfOKButton, "No marked block selected.");
			return false;
		}
	}
	if (!findRegexWithWrap(text, code, startOffset, options.direction, rangeStart, rangeEnd,
	                       options.globalSearch, matchStart, matchEnd, wrapped)) {
		pcre2_code_free(code);
		if (showNotFoundMessage)
			postSearchWarning("No match found.");
		syncVmLastSearch(win, false, 0, 0, 0);
		return false;
	}
	if (matchEnd == matchStart) {
		if (matchEnd < text.size())
			++matchEnd;
		else if (matchStart > 0)
			--matchStart;
	}
	editor->setCursorOffset(matchStart);
	editor->setSelectionOffsets(matchStart, matchEnd);
	editor->revealCursor(True);
	syncVmLastSearch(win, true, matchStart, matchEnd, matchStart);
	if (updateState) {
		g_searchUiState.hasPrevious = true;
		g_searchUiState.pattern = pattern;
		g_searchUiState.lastStart = matchStart;
		g_searchUiState.lastEnd = matchEnd;
		g_searchUiState.lastOptions = options;
	}
	pcre2_code_free(code);
	if (outWrapped != nullptr)
		*outWrapped = wrapped;
	return true;
}

bool handleSearchFindText() {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	MRSearchDialogOptions options = configuredSearchDialogOptions();
	std::string pattern;

	if (editor == nullptr)
		return true;
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
			messageBox(mfError | mfOKButton, "Invalid search pattern:\n%s", regexError.c_str());
			return true;
		}
		static_cast<void>(collectRegexMatches(text, code, matches));
		pcre2_code_free(code);
		if (options.restrictToMarkedBlock) {
			rangeStart = std::min(editor->selectionStartOffset(), editor->selectionEndOffset());
			rangeEnd = std::max(editor->selectionStartOffset(), editor->selectionEndOffset());
			if (rangeStart >= rangeEnd) {
				messageBox(mfInformation | mfOKButton, "No marked block selected.");
				clearSearchSelection(win);
				return true;
			}
			matches.erase(
			    std::remove_if(matches.begin(), matches.end(),
			                   [&](const SearchMatchEntry &entry) {
				                   return entry.start < rangeStart || entry.end > rangeEnd;
			                   }),
			    matches.end());
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
		if (showFoundListDialog(win, pattern, options, matches, selectedIndex))
			activateMatch(win, matches[selectedIndex], pattern, options);
		return true;
	}

	if (options.searchAllWindows && options.mode == MRSearchMode::StopFirst) {
		std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
		auto it = std::find(windows.begin(), windows.end(), win);
		bool found = false;
		if (it != windows.end())
			std::rotate(windows.begin(), it, windows.end());
		for (std::size_t i = 0; i < windows.size(); ++i) {
			TMREditWindow *candidate = windows[i];
			TMRFileEditor *candidateEditor = candidate != nullptr ? candidate->getEditor() : nullptr;
			std::size_t startOffset = 0;
			bool wrapped = false;

			if (candidateEditor == nullptr)
				continue;
			if (options.direction == MRSearchDirection::Backward)
				startOffset = (i == 0) ? candidateEditor->cursorOffset() : candidateEditor->bufferLength();
			else
				startOffset = (i == 0) ? candidateEditor->cursorOffset() : 0;
			if (!performSearch(candidate, pattern, startOffset, true, options, false, &wrapped))
				continue;
			if (candidate != win)
				static_cast<void>(mrActivateEditWindow(candidate));
			if (wrapped)
				mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup,
				                               wrappedSearchMessage(options.direction),
				                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
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

		if (!performSearch(win, pattern, startOffset, true, options, true, &wrapped))
			return true;
		if (wrapped)
			mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup,
			                               wrappedSearchMessage(options.direction),
			                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
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
				if (decision == PromptSearchDecision::Stop)
					break;
				if (options.direction == MRSearchDirection::Backward)
					nextStartOffset = currentStart;
				else {
					nextStartOffset = currentEnd;
					if (currentEnd <= currentStart)
						nextStartOffset = std::min(editor->bufferLength(), currentStart + 1);
				}
				if (!performSearch(win, pattern, nextStartOffset, true, options, false, &wrapped))
					break;
				if (wrapped)
					mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup,
					                               wrappedSearchMessage(options.direction),
					                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
			}
		}
	}
	return true;
}

bool handleSearchRepeatPrevious() {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::size_t startOffset = 0;
	bool wrapped = false;

	if (editor == nullptr)
		return true;
	if (!g_searchUiState.hasPrevious || g_searchUiState.pattern.empty()) {
		messageBox(mfInformation | mfOKButton, "No previous search.");
		return true;
	}
	if (g_searchUiState.lastOptions.direction == MRSearchDirection::Backward)
		startOffset = g_searchUiState.lastStart;
	else {
		startOffset = g_searchUiState.lastEnd;
		if (g_searchUiState.lastEnd <= g_searchUiState.lastStart)
			startOffset = std::min(editor->bufferLength(), g_searchUiState.lastStart + 1);
	}
	if (!performSearch(win, g_searchUiState.pattern, startOffset, true, g_searchUiState.lastOptions,
	                   true, &wrapped))
		return true;
	updateMiniMapFindMarkers(win, g_searchUiState.pattern, g_searchUiState.lastOptions);
	if (wrapped)
		mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup,
		                               wrappedSearchMessage(g_searchUiState.lastOptions.direction),
		                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleSearchReplace() {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	MRSarDialogOptions options = configuredSarDialogOptions();
	std::string pattern;
	std::string replacement;
	std::size_t start = 0;
	std::size_t end = 0;
	std::size_t cursorTargetStart = 0;
	std::size_t cursorTargetEnd = 0;
	std::size_t replacedCount = 0;
	bool cancelledByUser = false;

	if (editor == nullptr || win == nullptr)
		return true;
	if (!promptReplaceValues(pattern, replacement, options)) {
		editor->clearFindMarkerRanges();
		clearSearchSelection(win);
		return true;
	}
	if (pattern.empty()) {
		messageBox(mfError | mfOKButton, "Search text must not be empty.");
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
			if (!performSearch(win, pattern, editor->cursorOffset(), false, searchOptions, true, nullptr))
				return true;
			start = editor->selectionStartOffset();
			end = editor->selectionEndOffset();
			if (end < start)
				std::swap(start, end);
			if (!editor->replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end),
			                                   replacement.data(), static_cast<uint>(replacement.size()))) {
				messageBox(mfError | mfOKButton, "Replace failed.");
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

			if (!compileSearchRegex(buildSearchPatternExpression(pattern, options.textType), !options.caseSensitive, &code,
			                        regexError)) {
				messageBox(mfError | mfOKButton, "Invalid search pattern:\n%s", regexError.c_str());
				return true;
			}
			static_cast<void>(collectRegexMatches(text, code, matches));
			pcre2_code_free(code);
			if (options.restrictToMarkedBlock) {
				rangeStart = std::min(editor->selectionStartOffset(), editor->selectionEndOffset());
				rangeEnd = std::max(editor->selectionStartOffset(), editor->selectionEndOffset());
				if (rangeStart >= rangeEnd) {
					messageBox(mfInformation | mfOKButton, "No marked block selected.");
					clearSearchSelection(win);
					return true;
				}
			}
			matches.erase(std::remove_if(matches.begin(), matches.end(),
			                             [&](const SearchMatchEntry &entry) {
				                             if (entry.start < rangeStart || entry.end > rangeEnd)
					                             return true;
				                             if (options.globalSearch)
					                             return false;
				                             if (options.direction == MRSearchDirection::Backward)
					                             return entry.start >= initialCursor;
				                             return entry.start < initialCursor;
			                             }),
			              matches.end());
			if (matches.empty()) {
				postSearchWarning("No match found.");
				clearSearchSelection(win);
				return true;
			}
			if (options.direction == MRSearchDirection::Backward)
				std::reverse(matches.begin(), matches.end());
			for (const SearchMatchEntry &match : matches) {
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
						if (promptEnd < editor->bufferLength())
							++promptEnd;
						else if (promptStart > 0)
							--promptStart;
					}
					editor->setCursorOffset(promptStart);
					editor->setSelectionOffsets(promptStart, promptEnd);
					editor->revealCursor(True);
					syncVmLastSearch(win, true, promptStart, promptEnd, promptStart);
					promptPreview = previewForMatch(editor->snapshotText(), promptStart, promptEnd);
					PromptReplaceDecision decision =
					    promptReplaceDecisionDialog("SEARCH AND REPLACE", promptPreview, replacement);
					if (decision == PromptReplaceDecision::Cancel) {
						cancelledByUser = true;
						break;
					}
					if (decision == PromptReplaceDecision::Skip)
						continue;
					if (decision == PromptReplaceDecision::ReplaceAll)
						forceReplaceAll = true;
				}

				if (!editor->replaceRangeAndSelect(static_cast<uint>(currentStart), static_cast<uint>(currentEnd),
				                                   replacement.data(), static_cast<uint>(replacement.size()))) {
					messageBox(mfError | mfOKButton, "Replace failed.");
					return true;
				}
				cursorTargetStart = currentStart;
				cursorTargetEnd = currentStart + replacement.size();
				++replacedCount;
				if (forwardOrder)
					delta += static_cast<long long>(replacement.size()) -
					         static_cast<long long>(match.end - match.start);
			}
			if (replacedCount == 0) {
				if (cancelledByUser)
					clearSearchSelection(win);
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

		g_searchUiState.hasPrevious = true;
		g_searchUiState.pattern = pattern;
		g_searchUiState.replacement = replacement;
		g_searchUiState.lastStart = cursorTargetStart;
		g_searchUiState.lastEnd = cursorTargetEnd;
		g_searchUiState.lastOptions = searchOptions;
		syncVmLastSearch(win, true, cursorTargetStart, cursorTargetEnd, editor->cursorOffset());
		updateMiniMapFindMarkers(win, pattern, searchOptions);
		postSearchWarning(std::to_string(replacedCount) + " replacements");
	}
	return true;
}

bool handleSearchGotoLineNumber() {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	long lineNumber = 0;
	std::size_t offset = 0;
	std::size_t line = 1;
	std::size_t length = 0;

	if (editor == nullptr)
		return true;
	if (!promptGotoLineNumber(lineNumber))
		return true;

	length = editor->bufferLength();
	offset = 0;
	line = 1;
	while (line < static_cast<std::size_t>(lineNumber) && offset < length) {
		std::size_t next = editor->nextLineOffset(offset);
		if (next <= offset)
			break;
		offset = next;
		++line;
	}
	editor->setCursorOffset(offset);
	editor->setSelectionOffsets(offset, offset);
	editor->revealCursor(True);
	return true;
}

bool handleFileOpen() {
	enum { FileNameBufferSize = MAXPATH };
	char fileName[FileNameBufferSize];
	TMREditWindow *target;
	TMREditWindow *current = currentEditWindow();
	std::string resolvedPath;
	std::string logLine;
	bool createdTarget = false;

	if (!promptForPath("Open File", fileName, sizeof(fileName)))
		return true;
	if (!resolveReadableExistingPath(fileName, resolvedPath))
		return true;

	target = findReusableEmptyWindow(current);
	if (target == nullptr) {
		target = createEditorWindow("?No-File?");
		createdTarget = true;
	}
	if (!loadResolvedFileIntoWindow(target, resolvedPath, "Open file")) {
		if (createdTarget && target != nullptr)
			message(target, evCommand, cmClose, nullptr);
		if (target != nullptr && isEmptyUntitledEditableWindow(target) && current != target && current != nullptr)
			static_cast<void>(mrActivateEditWindow(current));
		return true;
	}
	static_cast<void>(mrActivateEditWindow(target));
	logLine = "Opened file: ";
	logLine += target->currentFileName();
	if (target->isReadOnly())
		logLine += " [read-only]";
	mrLogMessage(logLine.c_str());
	return true;
}

bool handleFileLoad() {
	enum { FileNameBufferSize = MAXPATH };
	char fileName[FileNameBufferSize];
	TMREditWindow *target = currentEditWindow();
	std::string resolvedPath;
	std::string logLine;
	bool createdTarget = false;

	if (!promptForPath("Load File", fileName, sizeof(fileName)))
		return true;
	if (!resolveReadableExistingPath(fileName, resolvedPath))
		return true;
	if (target == nullptr) {
		target = createEditorWindow("?No-File?");
		createdTarget = true;
	} else if (!target->confirmAbandonForReload())
		return true;
	if (!loadResolvedFileIntoWindow(target, resolvedPath, "Load file")) {
		if (createdTarget && target != nullptr)
			message(target, evCommand, cmClose, nullptr);
		return true;
	}
	static_cast<void>(mrActivateEditWindow(target));
	logLine = "Loaded file into active window: ";
	logLine += target->currentFileName();
	if (target->isReadOnly())
		logLine += " [read-only]";
	mrLogMessage(logLine.c_str());
	return true;
}

bool handleExecuteProgram() {
	std::string commandLine;
	TMREditWindow *win;

	if (!promptForCommandLine(commandLine))
		return true;
	if (commandLine.empty()) {
		messageBox(mfInformation | mfOKButton, "No command specified.");
		return true;
	}

	win = createEditorWindow(shortenCommandTitle(commandLine).c_str());
	if (win == nullptr) {
		messageBox(mfError | mfOKButton, "Unable to create communication window.");
		return true;
	}
	startExternalCommandInWindow(win, commandLine, true, true, true);
	return true;
}

bool startExternalCommandInWindow(TMREditWindow *win, const std::string &commandLine, bool replaceBuffer,
                                  bool activate, bool closeOnFailure) {
	std::string title;
	std::string initialText;
	std::ostringstream logLine;
	std::uint64_t taskId;

	if (win == nullptr)
		return false;
	title = shortenCommandTitle(commandLine);
	initialText = "$ " + commandLine + "\n\n";
	if (replaceBuffer) {
		if (!win->replaceTextBuffer(initialText.c_str(), title.c_str())) {
			if (closeOnFailure)
				message(win, evCommand, cmClose, nullptr);
			messageBox(mfError | mfOKButton, "Unable to prepare communication window.");
			return false;
		}
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	win->setWindowRole(TMREditWindow::wrCommunicationCommand, commandLine);
	if (activate)
		static_cast<void>(mrActivateEditWindow(win));

	taskId = mr::coprocessor::globalCoprocessor().submit(
	    mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::ExternalIo,
	    static_cast<std::size_t>(win->bufferId()), 0, std::string("external-io: ") + commandLine,
	    [commandLine, channelId = static_cast<std::size_t>(win->bufferId())](
	        const mr::coprocessor::TaskInfo &info, std::stop_token stopToken) {
		    return runExternalCommandTask(info, stopToken, channelId, commandLine);
	    });
	if (taskId == 0) {
		if (closeOnFailure)
			message(win, evCommand, cmClose, nullptr);
		messageBox(mfError | mfOKButton, "Unable to start external command worker.");
		return false;
	}
	win->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::ExternalIo, commandLine);

	logLine << "Started external command in communication window #" << win->bufferId() << ": "
	        << commandLine << " [task #" << taskId << "]";
	mrLogMessage(logLine.str().c_str());
	return true;
}

bool handleCancelBackgroundMacros() {
	TMREditWindow *win = currentEditWindow();
	std::ostringstream line;
	std::size_t taskCount;

	if (win == nullptr)
		return true;
	taskCount = win->trackedMacroTaskCount();
	if (taskCount == 0) {
		messageBox(mfInformation | mfOKButton, "No background macro tasks are running in this window.");
		return true;
	}
	if (!win->cancelTrackedMacroTasks())
		return true;
	line << "Requested cancel of " << taskCount << " background macro task";
	if (taskCount != 1)
		line << "s";
	line << " in window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

bool handleHeroEventProbe() {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, "Hero event probe",
	                              mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool dispatchEditorCommand(ushort editorCommand, bool requiresWritable) {
	TMREditWindow *win = currentEditWindow();
	TMRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (win == nullptr || editor == nullptr)
		return true;
	if (requiresWritable && win->isReadOnly()) {
		messageBox(mfInformation | mfOKButton, "Window is read-only.");
		return true;
	}
	message(editor, evCommand, editorCommand, nullptr);
	return true;
}

bool dispatchEditorClipboardCommand(ushort editorCommand, bool requiresWritable) {
	return dispatchEditorCommand(editorCommand, requiresWritable);
}

ushort execDialogWithDataLocal(TDialog *dialog, void *data) {
	ushort result = cmCancel;

	if (dialog == nullptr || TProgram::deskTop == nullptr)
		return cmCancel;
	if (data != nullptr)
		dialog->setData(data);
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel && data != nullptr)
		dialog->getData(data);
	TObject::destroy(dialog);
	return result;
}

void syncPersistentBlocksMenuState() {
	if (auto *mrMenuBar = dynamic_cast<TMRMenuBar *>(TProgram::menuBar))
		mrMenuBar->setPersistentBlocksMenuState(configuredPersistentBlocksSetting());
}

bool handleBlockAction(bool ok, const char *failureText) {
	if (!ok && failureText != nullptr && *failureText != '\0')
		messageBox(mfInformation | mfOKButton, "%s", failureText);
	return true;
}

bool hasMarkedTextForBlockOperation(TMREditWindow *win) {
	if (win == nullptr || !win->hasBlock())
		return false;
	if (win->blockStatus() == TMREditWindow::bmStream &&
	    win->blockAnchorPtr() == win->blockEffectiveEndPtr())
		return false;
	return true;
}

bool promptBlockSavePath(std::string &outPath) {
	char buffer[MAXPATH] = {0};
	ushort result = cmCancel;
	static constexpr ushort kBlockSaveDialogHistoryId = 45;
	TMREditWindow *win = currentEditWindow();

	outPath.clear();
	if (win != nullptr && win->currentFileName() != nullptr && *win->currentFileName() != '\0')
		strnzcpy(buffer, win->currentFileName(), sizeof(buffer));
	else
		initRememberedLoadDialogPath(buffer, sizeof(buffer), "*.*");
	result = execDialogWithDataLocal(
	    new TFileDialog("*.*", "Save block as", "~N~ame", fdOKButton, kBlockSaveDialogHistoryId), buffer);
	if (result == cmCancel)
		return false;
	fexpand(buffer);
	rememberLoadDialogPath(buffer);
	outPath = buffer;
	if (outPath.find('*') != std::string::npos || outPath.find('?') != std::string::npos)
		return false;
	return !outPath.empty();
}

bool chooseInterWindowBlockTarget(int &sourceWindowIndex) {
	TMREditWindow *targetWin = currentEditWindow();
	TMREditWindow *sourceWin = nullptr;

	sourceWindowIndex = 0;
	if (targetWin == nullptr)
		return false;
	sourceWin = mrShowWindowListDialog(mrwlActivateWindow, targetWin);
	if (sourceWin == nullptr)
		return false;
	if (sourceWin == targetWin) {
		messageBox(mfInformation | mfOKButton, "Choose another window for inter-window block operation.");
		return false;
	}
	if (!hasMarkedTextForBlockOperation(sourceWin)) {
		messageBox(mfInformation | mfOKButton, "No block marked in the selected source window.");
		return false;
	}
	sourceWindowIndex = mrvmUiCurrentWindowIndex(sourceWin);
	static_cast<void>(mrActivateEditWindow(targetWin));
	return true;
}

bool togglePersistentBlocksSetting() {
	MREditSetupSettings settings = configuredEditSetupSettings();
	MRSetupPaths paths;
	MRSettingsWriteReport writeReport;
	TMREditorApp *app = dynamic_cast<TMREditorApp *>(TProgram::application);
	std::string errorText;
	bool enabled;

	settings.persistentBlocks = !settings.persistentBlocks;
	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		messageBox(mfError | mfOKButton, "Persistent blocks update failed:\n%s", errorText.c_str());
		return true;
	}

	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	if (!writeSettingsMacroFile(paths, &errorText, &writeReport)) {
		messageBox(mfError | mfOKButton, "Settings save failed:\n%s", errorText.c_str());
		return true;
	}
	mrLogSettingsWriteReport("persistent blocks toggle", writeReport);
	if (app != nullptr && !app->reloadSettingsMacroFromPath(paths.settingsMacroUri, &errorText)) {
		messageBox(mfError | mfOKButton, "Settings reload failed:\n%s", errorText.c_str());
		return true;
	}

	enabled = configuredPersistentBlocksSetting();
	mrLogMessage(enabled ? "Persistent blocks enabled." : "Persistent blocks disabled.");
	syncPersistentBlocksMenuState();
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction,
	                               enabled ? "Persistent blocks: ON" : "Persistent blocks: OFF",
	                               mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleStopCurrentProgram() {
	TMREditWindow *win = currentEditWindow();
	std::ostringstream line;
	std::size_t taskCount;

	if (win == nullptr || !win->isCommunicationWindow())
		return true;
	taskCount = win->trackedTaskCount(mr::coprocessor::TaskKind::ExternalIo);
	if (taskCount == 0) {
		messageBox(mfInformation | mfOKButton, "No external program task is running in this window.");
		return true;
	}
	if (!win->cancelTrackedExternalIoTasks())
		return true;
	line << "Requested stop of " << taskCount << " external program task";
	if (taskCount != 1)
		line << "s";
	line << " in communication window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

bool handleRestartCurrentProgram() {
	TMREditWindow *win = currentEditWindow();

	if (win == nullptr || win->windowRole() != TMREditWindow::wrCommunicationCommand)
		return true;
	if (win->hasTrackedExternalIoTasks()) {
		messageBox(mfInformation | mfOKButton, "Stop the current program before restarting it.");
		return true;
	}
	if (win->windowRoleDetail().empty()) {
		messageBox(mfInformation | mfOKButton, "No restartable command is associated with this window.");
		return true;
	}
	startExternalCommandInWindow(win, win->windowRoleDetail(), true, true, false);
	return true;
}

bool handleClearCurrentOutput() {
	TMREditWindow *win = currentEditWindow();
	std::ostringstream line;

	if (win == nullptr)
		return true;
	if (win->windowRole() == TMREditWindow::wrLog) {
		if (!mrClearLogWindow()) {
			messageBox(mfError | mfOKButton, "Unable to clear log window.");
			return true;
		}
		mrLogMessage("Log window cleared.");
		return true;
	}
	if (!win->isCommunicationWindow())
		return true;
	if (win->hasTrackedExternalIoTasks()) {
		messageBox(mfInformation | mfOKButton, "Stop the current program before clearing its output.");
		return true;
	}
	if (!win->replaceTextBuffer("", win->getTitle(0))) {
		messageBox(mfError | mfOKButton, "Unable to clear communication window.");
		return true;
	}
	win->setReadOnly(true);
	win->setFileChanged(false);
	line << "Cleared communication window #" << win->bufferId() << ".";
	mrLogMessage(line.str().c_str());
	return true;
}

} // namespace

bool handleMRCommand(ushort command) {
	switch (command) {
		case cmMrFileOpen:
			return handleFileOpen();

		case cmMrFileLoad:
			return handleFileLoad();

		case cmMrFileSave:
			static_cast<void>(saveCurrentEditWindow());
			return true;

		case cmMrFileSaveAs:
			static_cast<void>(saveCurrentEditWindowAs());
			return true;

		case cmMrFileInformation:
			showFileInformationDialog(currentEditWindow());
			return true;

		case cmMrEditUndo:
			return dispatchEditorCommand(cmMrEditUndo, true);

		case cmMrEditRedo:
			return dispatchEditorCommand(cmMrEditRedo, true);

		case cmMrEditCutToBuffer:
			return dispatchEditorClipboardCommand(cmCut, true);

		case cmMrEditCopyToBuffer:
			return dispatchEditorClipboardCommand(cmCopy, false);

		case cmMrEditPasteFromBuffer:
			return dispatchEditorClipboardCommand(cmPaste, true);

		case cmMrSearchPushMarker:
			return handleBlockAction(mrvmUiPushMarker(), "Unable to push position onto marker stack.");

		case cmMrSearchGetMarker:
			return handleBlockAction(mrvmUiGetMarker(), "No marker position on stack.");

		case cmMrSearchFindText:
			return handleSearchFindText();

		case cmMrSearchReplace:
			return handleSearchReplace();

		case cmMrSearchRepeatPrevious:
			return handleSearchRepeatPrevious();

		case cmMrSearchGotoLineNumber:
			return handleSearchGotoLineNumber();

		case cmMrBlockCopy:
			return handleBlockAction(mrvmUiCopyBlock(), "No block marked.");

		case cmMrBlockMove:
			return handleBlockAction(mrvmUiMoveBlock(), "Unable to move block.");

		case cmMrBlockDelete:
			return handleBlockAction(mrvmUiDeleteBlock(), "Unable to delete block.");

		case cmMrBlockSaveToDisk: {
			std::string savePath;
			if (!promptBlockSavePath(savePath))
				return true;
			return handleBlockAction(mrvmUiSaveBlockToFile(savePath), "Unable to save block.");
		}

		case cmMrBlockIndent:
			return handleBlockAction(mrvmUiIndentBlock(), "Unable to indent block.");

		case cmMrBlockUndent:
			return handleBlockAction(mrvmUiUndentBlock(), "Unable to undent block.");

		case cmMrBlockWindowCopy: {
			bool ok = false;
			int sourceWindowIndex = 0;
			if (!chooseInterWindowBlockTarget(sourceWindowIndex))
				return true;
			if (sourceWindowIndex > 0)
				ok = mrvmUiWindowCopyBlock(sourceWindowIndex);
			return handleBlockAction(ok, "Inter-window block copy failed.");
		}

		case cmMrBlockWindowMove: {
			bool ok = false;
			int sourceWindowIndex = 0;
			if (!chooseInterWindowBlockTarget(sourceWindowIndex))
				return true;
			if (sourceWindowIndex > 0)
				ok = mrvmUiWindowMoveBlock(sourceWindowIndex);
			return handleBlockAction(ok, "Inter-window block move failed.");
		}

		case cmMrBlockMarkLines:
			return handleBlockAction(mrvmUiBlockBeginLine(), "Unable to start line block marking.");

		case cmMrBlockMarkColumns:
			return handleBlockAction(mrvmUiBlockBeginColumn(), "Unable to start column block marking.");

		case cmMrBlockMarkStream:
			return handleBlockAction(mrvmUiBlockBeginStream(), "Unable to start stream block marking.");

		case cmMrBlockEndMarking:
			return handleBlockAction(mrvmUiBlockEndMarking(), "No active block marking.");

		case cmMrBlockTurnMarkingOff:
			return handleBlockAction(mrvmUiBlockTurnMarkingOff(), "No block marked.");

		case cmMrBlockPersistent:
			return togglePersistentBlocksSetting();

		case cmMrWindowOpen:
			static_cast<void>(createEditorWindow("?No-File?"));
			mrLogMessage("New empty window opened.");
			return true;

		case cmMrWindowClose:
			static_cast<void>(closeCurrentEditWindow());
			return true;

		case cmMrWindowList: {
			TMREditWindow *selected = mrShowWindowListDialog(mrwlActivateWindow, currentEditWindow());
			if (selected != nullptr)
				static_cast<void>(mrActivateEditWindow(selected));
			return true;
		}

		case cmMrWindowNext:
			static_cast<void>(activateRelativeEditWindow(1));
			return true;

		case cmMrWindowPrevious:
			static_cast<void>(activateRelativeEditWindow(-1));
			return true;

		case cmMrWindowHide:
			static_cast<void>(hideCurrentEditWindow());
			return true;

		case cmMrWindowZoom:
			mrvmUiZoomCurrentWindow();
			return true;

		case cmMrWindowCascade:
			return handleWindowCascade();

		case cmMrWindowTile:
			return handleWindowTile();

		case cmMrWindowNextDesktop:
			return viewportRight();

		case cmMrWindowPrevDesktop:
			return viewportLeft();

		case cmMrWindowMoveToNextDesktop:
			return moveToNextVirtualDesktop();

		case cmMrWindowMoveToPrevDesktop:
			return moveToPrevVirtualDesktop();

		case cmMrTextUpperCaseMenu:
			return dispatchEditorCommand(cmMrTextUpperCaseMenu, true);

		case cmMrTextLowerCaseMenu:
			return dispatchEditorCommand(cmMrTextLowerCaseMenu, true);

		case cmMrWindowLink:
			mrvmUiLinkCurrentWindow();
			return true;

		case cmMrWindowUnlink:
			mrvmUiUnlinkCurrentWindow();
			return true;

		case cmMrHelpContents:
		case cmMrHelpKeys:
		case cmMrHelpDetailedIndex:
		case cmMrHelpPreviousTopic:
		case cmHelp:
			static_cast<void>(mrShowProjectHelp());
			return true;

		case cmMrOtherInstallationAndSetup:
			runInstallationAndSetupDialogFlow();
			return true;

		case cmMrHelpAbout:
			showAboutDialog();
			return true;

		case cmMrOtherExecuteProgram:
			return handleExecuteProgram();

		case cmMrOtherStopProgram:
			return handleStopCurrentProgram();

		case cmMrOtherRestartProgram:
			return handleRestartCurrentProgram();

		case cmMrOtherClearOutput:
			return handleClearCurrentOutput();

		case cmMrOtherMacroManager:
			return runMacroManagerDialog();

		case cmMrDevCancelMacroTasks:
			return handleCancelBackgroundMacros();

		case cmMrDevHeroEventProbe:
			return handleHeroEventProbe();

				default: {
					const char *title = placeholderCommandTitle(command);
				if (title != nullptr) {
					showPlaceholderCommandBox(title);
					return true;
				}
			return false;
		}
	}
}
