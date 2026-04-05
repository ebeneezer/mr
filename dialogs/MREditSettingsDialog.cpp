#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDialog
#define Uses_TDeskTop
#define Uses_TGroup
#define Uses_TInputLine
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRadioButtons
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TSItem
#define Uses_TKeys
#include <tvision/tv.h>

#include "MREditSettingsDialogInternal.hpp"
#include "MRSetupDialogCommon.hpp"
#include "MRSetupDialogs.hpp"

#include <algorithm>
<<<<<<< HEAD
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
=======
>>>>>>> 7a37a03 (git is shit)
#include <vector>

namespace {
using namespace MREditSettingsDialogInternal;

enum : ushort {
	cmMrSetupEditSettingsHelp = 3810
};

<<<<<<< HEAD
enum {
	kPageBreakFieldSize = 64,
	kWordDelimsFieldSize = 256,
	kDefaultExtsFieldSize = 256,
	kTabSizeFieldSize = 8
};

enum : ushort {
	kOptionTruncateSpaces = 0x0001,
	kOptionEofCtrlZ = 0x0002,
	kOptionEofCrLf = 0x0004,
	kOptionShowLineNumbers = 0x0008,
	kOptionLineNumZeroFill = 0x0010,
	kOptionPersistentBlocks = 0x0020,
	kOptionBackupFiles = 0x0040,
	kOptionShowEofMarker = 0x0080,
	kOptionShowEofMarkerEmoji = 0x0100
};

enum : ushort {
	kLeftOptionTruncateSpaces = 0x0001,
	kLeftOptionEofCtrlZ = 0x0002,
	kLeftOptionEofCrLf = 0x0004,
	kLeftOptionBackupFiles = 0x0008,
	kLeftOptionShowEofMarker = 0x0010
};

enum : ushort {
	kRightOptionShowLineNumbers = 0x0001,
	kRightOptionLineNumZeroFill = 0x0002,
	kRightOptionPersistentBlocks = 0x0004,
	kRightOptionShowEofMarkerEmoji = 0x0008
};

enum : ushort {
	kTabExpandTabs = 0,
	kTabExpandSpaces = 1
};

enum : ushort {
	kColumnMoveDeleteSpace = 0,
	kColumnMoveLeaveSpace = 1
};

enum : ushort {
	kDefaultModeInsert = 0,
	kDefaultModeOverwrite = 1
};

struct EditSettingsDialogRecord {
	char pageBreak[kPageBreakFieldSize];
	char wordDelimiters[kWordDelimsFieldSize];
	char defaultExtensions[kDefaultExtsFieldSize];
	char tabSize[kTabSizeFieldSize];
	ushort optionsMask;
	ushort tabExpandChoice;
	ushort columnBlockMoveChoice;
	ushort defaultModeChoice;
};

std::string trimAscii(const std::string &value) {
	std::size_t start = 0;
	std::size_t end = value.size();

	while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return value.substr(start, end - start);
}

std::string upperAscii(std::string value) {
	for (char & i : value)
		i = static_cast<char>(std::toupper(static_cast<unsigned char>(i)));
	return value;
}

std::string readRecordField(const char *value) {
	return trimAscii(value != nullptr ? value : "");
}

void writeRecordField(char *dest, std::size_t destSize, const std::string &value) {
	if (dest == nullptr || destSize == 0)
		return;
	std::memset(dest, 0, destSize);
	std::strncpy(dest, value.c_str(), destSize - 1);
	dest[destSize - 1] = '\0';
}

bool recordsEqual(const EditSettingsDialogRecord &lhs, const EditSettingsDialogRecord &rhs) {
	return readRecordField(lhs.pageBreak) == readRecordField(rhs.pageBreak) &&
	       readRecordField(lhs.wordDelimiters) == readRecordField(rhs.wordDelimiters) &&
	       readRecordField(lhs.defaultExtensions) == readRecordField(rhs.defaultExtensions) &&
	       readRecordField(lhs.tabSize) == readRecordField(rhs.tabSize) &&
	       lhs.optionsMask == rhs.optionsMask && lhs.tabExpandChoice == rhs.tabExpandChoice &&
	       lhs.columnBlockMoveChoice == rhs.columnBlockMoveChoice &&
	       lhs.defaultModeChoice == rhs.defaultModeChoice;
}

void initEditSettingsDialogRecord(EditSettingsDialogRecord &record) {
	MREditSetupSettings settings = configuredEditSetupSettings();
	std::string columnMove = upperAscii(settings.columnBlockMove);
	std::string defaultMode = upperAscii(settings.defaultMode);

	std::memset(&record, 0, sizeof(record));
	writeRecordField(record.pageBreak, sizeof(record.pageBreak), settings.pageBreak);
	writeRecordField(record.wordDelimiters, sizeof(record.wordDelimiters), settings.wordDelimiters);
	writeRecordField(record.defaultExtensions, sizeof(record.defaultExtensions), settings.defaultExtensions);
	writeRecordField(record.tabSize, sizeof(record.tabSize), std::to_string(settings.tabSize));
	record.optionsMask = 0;
	if (settings.truncateSpaces)
		record.optionsMask |= kOptionTruncateSpaces;
	if (settings.eofCtrlZ)
		record.optionsMask |= kOptionEofCtrlZ;
	if (settings.eofCrLf)
		record.optionsMask |= kOptionEofCrLf;
	if (settings.backupFiles)
		record.optionsMask |= kOptionBackupFiles;
	if (settings.showEofMarker)
		record.optionsMask |= kOptionShowEofMarker;
	if (settings.showEofMarkerEmoji)
		record.optionsMask |= kOptionShowEofMarkerEmoji;
	if (settings.persistentBlocks)
		record.optionsMask |= kOptionPersistentBlocks;
	if (settings.showLineNumbers)
		record.optionsMask |= kOptionShowLineNumbers;
	if (settings.lineNumZeroFill)
		record.optionsMask |= kOptionLineNumZeroFill;
	record.tabExpandChoice = settings.tabExpand ? kTabExpandTabs : kTabExpandSpaces;
	record.columnBlockMoveChoice =
	    (columnMove == "LEAVE_SPACE") ? kColumnMoveLeaveSpace : kColumnMoveDeleteSpace;
	record.defaultModeChoice =
	    (defaultMode == "OVERWRITE") ? kDefaultModeOverwrite : kDefaultModeInsert;
}

bool recordToSettings(const EditSettingsDialogRecord &record, MREditSetupSettings &settings,
                      std::string &errorText) {
	settings = configuredEditSetupSettings();
	settings.pageBreak = readRecordField(record.pageBreak);
	settings.wordDelimiters = readRecordField(record.wordDelimiters);
	settings.defaultExtensions = readRecordField(record.defaultExtensions);
	{
		std::string tabSizeText = readRecordField(record.tabSize);
		char *end = nullptr;
		long tabSize = 0;
		if (tabSizeText.empty()) {
			errorText = "TABSIZE must be between 1 and 32.";
			return false;
		}
		tabSize = std::strtol(tabSizeText.c_str(), &end, 10);
		if (end == tabSizeText.c_str() || end == nullptr || *end != '\0' || tabSize < 1 || tabSize > 32) {
			errorText = "TABSIZE must be an integer between 1 and 32.";
			return false;
		}
		settings.tabSize = static_cast<int>(tabSize);
	}
	settings.truncateSpaces = (record.optionsMask & kOptionTruncateSpaces) != 0;
	settings.eofCtrlZ = (record.optionsMask & kOptionEofCtrlZ) != 0;
	settings.eofCrLf = (record.optionsMask & kOptionEofCrLf) != 0;
	settings.backupFiles = (record.optionsMask & kOptionBackupFiles) != 0;
	settings.showEofMarker = (record.optionsMask & kOptionShowEofMarker) != 0;
	settings.showEofMarkerEmoji = (record.optionsMask & kOptionShowEofMarkerEmoji) != 0;
	settings.persistentBlocks = (record.optionsMask & kOptionPersistentBlocks) != 0;
	settings.showLineNumbers = (record.optionsMask & kOptionShowLineNumbers) != 0;
	settings.lineNumZeroFill = (record.optionsMask & kOptionLineNumZeroFill) != 0;
	settings.tabExpand = record.tabExpandChoice == kTabExpandTabs;
	settings.columnBlockMove =
	    (record.columnBlockMoveChoice == kColumnMoveLeaveSpace) ? "LEAVE_SPACE" : "DELETE_SPACE";
	settings.defaultMode =
	    (record.defaultModeChoice == kDefaultModeOverwrite) ? "OVERWRITE" : "INSERT";
	errorText.clear();
	return true;
}

bool saveAndReloadEditSettings(const EditSettingsDialogRecord &record, std::string &errorText) {
	MREditSetupSettings settings;
	MRSetupPaths paths;
	TMREditorApp *app = dynamic_cast<TMREditorApp *>(TProgram::application);

	if (!recordToSettings(record, settings, errorText))
		return false;
	if (!setConfiguredEditSetupSettings(settings, &errorText))
		return false;

	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();

	if (!writeSettingsMacroFile(paths, &errorText))
		return false;
	if (app == nullptr) {
		errorText = "Application error: TMREditorApp is unavailable.";
		return false;
	}
	if (!app->reloadSettingsMacroFromPath(paths.settingsMacroUri, &errorText))
		return false;
	errorText.clear();
	return true;
}

=======
>>>>>>> 7a37a03 (git is shit)
class TEditSettingsDialog : public TDialog {
  public:
	struct ManagedItem {
		TView *view;
		TRect base;
	};

	explicit TEditSettingsDialog(const EditSettingsDialogRecord &initialRecord)
	    : TWindowInit(&TDialog::initFrame),
	      TDialog(centeredSetupDialogRect(kVirtualDialogWidth, kVirtualDialogHeight),
	              "EDIT SETTINGS"),
	      initialRecord_(initialRecord), currentRecord_(initialRecord) {
		contentRect_ = TRect(1, 1, size.x - 1, size.y - 1);
		content = createSetupDialogContentGroup(contentRect_);
		if (content != nullptr) {
			content->options |= ofSelectable;
			insert(content);
		}

		int dialogWidth = kVirtualDialogWidth;
		int inputLeft = 32;
		int inputRight = dialogWidth - 2;
		int optionsHeadingX = 2;
		int optionsLeft = 2;
		int optionsRight = optionsLeft + 28;
		int lineNumbersLeft = optionsRight + 3;
		int lineNumbersRight = lineNumbersLeft + 15;
		int eofMarkerLeft = lineNumbersRight + 3;
		int eofMarkerRight = eofMarkerLeft + 15;
		int defaultModeLeft = eofMarkerRight + 3;
		int defaultModeRight = defaultModeLeft + 15;
		int tabExpandLeft = lineNumbersLeft;
		int tabExpandRight = tabExpandLeft + 15;
		int columnBlockMoveLeft = optionsLeft;
		int columnBlockMoveRight = optionsLeft + 18;

		int doneLeft = dialogWidth / 2 - 17;
		int cancelLeft = doneLeft + 12;
		int helpLeft = cancelLeft + 14;
		int buttonTop = size.y - 3;

		addManaged(new TStaticText(TRect(2, 2, inputLeft - 2, 3), "Page break string:"),
		           TRect(2, 2, inputLeft - 2, 3));
		pageBreakField_ =
		    new TInputLine(TRect(inputLeft, 2, inputRight, 3), kPageBreakFieldSize - 1);
		addManaged(pageBreakField_, TRect(inputLeft, 2, inputRight, 3));

		addManaged(new TStaticText(TRect(2, 4, inputLeft - 2, 5), "Word delimiters:"),
		           TRect(2, 4, inputLeft - 2, 5));
		wordDelimitersField_ =
		    new TInputLine(TRect(inputLeft, 4, inputRight, 5), kWordDelimsFieldSize - 1);
		addManaged(wordDelimitersField_, TRect(inputLeft, 4, inputRight, 5));

		addManaged(new TStaticText(TRect(2, 6, inputLeft - 2, 7), "Default file extension(s):"),
		           TRect(2, 6, inputLeft - 2, 7));
		defaultExtensionsField_ =
		    new TInputLine(TRect(inputLeft, 6, inputRight, 7), kDefaultExtsFieldSize - 1);
		addManaged(defaultExtensionsField_, TRect(inputLeft, 6, inputRight, 7));
		addManaged(new TStaticText(TRect(2, 22, 12, 23), "Tab size:"), TRect(2, 22, 12, 23));
		tabSizeField_ = new TInputLine(TRect(13, 22, 20, 23), kTabSizeFieldSize - 1);
		addManaged(tabSizeField_, TRect(13, 22, 20, 23));

		
		addManaged(new TStaticText(TRect(optionsHeadingX, 8, dialogWidth - 2, 9), "Options:"),
		           TRect(optionsHeadingX, 8, dialogWidth - 2, 9));

		optionsLeftField = new TCheckBoxes(
		    TRect(optionsLeft, 9, optionsRight, 14),
		    new TSItem(
		        "~T~runcate spaces",
		        new TSItem("Control-~Z~ at EOF",
		                   new TSItem("~C~R/LF at EOF",
		                              new TSItem("create ~B~ackup (.bak) on save",
		                                         new TSItem("~P~ersistent blocks", nullptr))))));
		addManaged(optionsLeftField, TRect(optionsLeft, 9, optionsRight, 14));

		addManaged(new TStaticText(TRect(lineNumbersLeft, 8, lineNumbersRight, 9), "Line numbers:"),
		           TRect(lineNumbersLeft, 8, lineNumbersRight, 9));
		lineNumbersField = new TRadioButtons(
		    TRect(lineNumbersLeft, 9, lineNumbersRight, 12),
		    new TSItem("~O~ff", new TSItem("O~n~", new TSItem("Leading ~0~", nullptr))));
		addManaged(lineNumbersField, TRect(lineNumbersLeft, 9, lineNumbersRight, 12));

		addManaged(new TStaticText(TRect(eofMarkerLeft, 8, eofMarkerRight, 9), "EOF marker:"),
		           TRect(eofMarkerLeft, 8, eofMarkerRight, 9));
		eofMarkerField = new TRadioButtons(
		    TRect(eofMarkerLeft, 9, eofMarkerRight, 12),
		    new TSItem("~O~ff", new TSItem("~P~lain", new TSItem("~E~moji", nullptr))));
		addManaged(eofMarkerField, TRect(eofMarkerLeft, 9, eofMarkerRight, 12));

		addManaged(new TStaticText(TRect(defaultModeLeft, 8, defaultModeRight, 9), "Default mode:"),
		           TRect(defaultModeLeft, 8, defaultModeRight, 9));
		defaultModeField_ =
		    new TRadioButtons(TRect(defaultModeLeft, 9, defaultModeRight, 12),
		                      new TSItem("~I~nsert", new TSItem("~O~verwrite", nullptr)));
		addManaged(defaultModeField_, TRect(defaultModeLeft, 9, defaultModeRight, 12));

		addManaged(new TStaticText(TRect(tabExpandLeft, 13, tabExpandRight, 14), "Tab expand:"),
		           TRect(tabExpandLeft, 13, tabExpandRight, 14));
		tabExpandField_ = new TRadioButtons(
		    TRect(tabExpandLeft, 14, tabExpandRight, 17),
		    new TSItem("~T~abs", new TSItem("~S~paces", nullptr)));
		addManaged(tabExpandField_, TRect(tabExpandLeft, 14, tabExpandRight, 17));

		addManaged(new TStaticText(TRect(columnBlockMoveLeft, 18, columnBlockMoveRight, 19),
		                           "Column block move:"),
		           TRect(columnBlockMoveLeft, 18, columnBlockMoveRight, 19));
		columnBlockMoveField_ =
		    new TRadioButtons(TRect(columnBlockMoveLeft, 19, columnBlockMoveRight, 22),
		                      new TSItem("~D~elete space", new TSItem("~L~eave space", nullptr)));
		addManaged(columnBlockMoveField_, TRect(columnBlockMoveLeft, 19, columnBlockMoveRight, 22));

		
		addManaged(new TStaticText(TRect(2, 23, 12, 24), "Tab size:"), TRect(2, 23, 12, 24));
		tabSizeField_ = new TInputLine(TRect(13, 23, 20, 24), kTabSizeFieldSize - 1);
		addManaged(tabSizeField_, TRect(13, 23, 20, 24));

		addManaged(new TButton(TRect(doneLeft, buttonTop, doneLeft + 10, buttonTop + 2), "~D~one",
		                       cmOK, bfDefault),
		           TRect(doneLeft, buttonTop, doneLeft + 10, buttonTop + 2));
		addManaged(new TButton(TRect(cancelLeft, buttonTop, cancelLeft + 12, buttonTop + 2),
		                       "~C~ancel", cmCancel, bfNormal),
		           TRect(cancelLeft, buttonTop, cancelLeft + 12, buttonTop + 2));
		addManaged(new TButton(TRect(helpLeft, buttonTop, helpLeft + 8, buttonTop + 2), "~H~elp",
		                       cmMrSetupEditSettingsHelp, bfNormal),
		           TRect(helpLeft, buttonTop, helpLeft + 8, buttonTop + 2));

		loadFieldsFromRecord(currentRecord_);
		initScrollIfNeeded();

		if (content != nullptr) {
			content->resetCurrent();
			content->select();
		}

	}

	ushort run(EditSettingsDialogRecord &outRecord, bool &changed) {
		ushort result = TProgram::deskTop->execView(this);
		saveFieldsToRecord(currentRecord_);
		outRecord = currentRecord_;
		changed = !recordsEqual(initialRecord_, currentRecord_);
		return result;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown && content != nullptr) {
			ushort keyCode = event.keyDown.keyCode;

			if (keyCode == kbTab || keyCode == kbCtrlI) {
				content->selectNext(False);
				clearEvent(event);
				return;
			}
			if (keyCode == kbShiftTab) {
				content->selectNext(True);
				clearEvent(event);
				return;
			}
		}

		TDialog::handleEvent(event);

		if (event.what == evCommand && event.message.command == cmMrSetupEditSettingsHelp) {
			endModal(event.message.command);
			clearEvent(event);
		}
		if (event.what == evBroadcast && event.message.command == cmScrollBarChanged &&
		    (event.message.infoPtr == hScrollBar_ || event.message.infoPtr == vScrollBar_)) {
			applyScroll();
			clearEvent(event);
		}
	}

  private:
	static const int kVirtualDialogWidth = 88;
	static const int kVirtualDialogHeight = 26;

	void addManaged(TView *view, const TRect &base) {
		ManagedItem item;
		item.view = view;
		item.base = base;
		managedViews_.push_back(item);
		if (content != nullptr) {
			TRect local = base;
			local.move(-contentRect_.a.x, -contentRect_.a.y);
			view->locate(local);
			content->insert(view);
		} else
			insert(view);
	}

	void applyScroll() {
		int dx = hScrollBar_ != nullptr ? hScrollBar_->value : 0;
		int dy = vScrollBar_ != nullptr ? vScrollBar_->value : 0;

		for (auto &managedView : managedViews_) {
			TRect moved = managedView.base;
			moved.move(-dx, -dy);
			moved.move(-contentRect_.a.x, -contentRect_.a.y);
			managedView.view->locate(moved);
		}
		if (content != nullptr)
			content->drawView();
	}

	void initScrollIfNeeded() {
		int virtualContentWidth = std::max(1, kVirtualDialogWidth - 2);
		int virtualContentHeight = std::max(1, kVirtualDialogHeight - 2);
		bool needH = false;
		bool needV = false;

		for (;;) {
			bool prevH = needH;
			bool prevV = needV;
			int viewportWidth = std::max(1, size.x - 2);
			int viewportHeight = std::max(1, size.y - 2);
			needH = virtualContentWidth > viewportWidth;
			needV = virtualContentHeight > viewportHeight;
			if (needH == prevH && needV == prevV)
				break;
		}

		contentRect_ = TRect(1, 1, size.x - 1, size.y - 1);
		if (contentRect_.b.x <= contentRect_.a.x)
			contentRect_.b.x = contentRect_.a.x + 1;
		if (contentRect_.b.y <= contentRect_.a.y)
			contentRect_.b.y = contentRect_.a.y + 1;
		if (content != nullptr)
			content->locate(contentRect_);

		if (needH) {
			TRect hRect(1, size.y - 1, size.x - 1, size.y);
			if (hScrollBar_ == nullptr) {
				hScrollBar_ = new TScrollBar(hRect);
				insert(hScrollBar_);
			} else
				hScrollBar_->locate(hRect);
		}
		if (needV) {
			TRect vRect(size.x - 1, 1, size.x, size.y - 1);
			if (vScrollBar_ == nullptr) {
				vScrollBar_ = new TScrollBar(vRect);
				insert(vScrollBar_);
			} else
				vScrollBar_->locate(vRect);
		}

		if (hScrollBar_ != nullptr) {
			int maxDx =
			    std::max(0, virtualContentWidth - std::max(1, contentRect_.b.x - contentRect_.a.x));
			hScrollBar_->setParams(0, 0, maxDx,
			                       std::max(1, (contentRect_.b.x - contentRect_.a.x) / 2), 1);
		}
		if (vScrollBar_ != nullptr) {
			int maxDy = std::max(0, virtualContentHeight -
			                            std::max(1, contentRect_.b.y - contentRect_.a.y));
			vScrollBar_->setParams(0, 0, maxDy,
			                       std::max(1, (contentRect_.b.y - contentRect_.a.y) / 2), 1);
		}
		applyScroll();
	}

	static void setInputLineValue(TInputLine *inputLine, const char *value, std::size_t capacity) {
		std::vector<char> buffer(capacity, '\0');
		writeRecordField(buffer.data(), buffer.size(), readRecordField(value));
		inputLine->setData(buffer.data());
	}

	static void readInputLineValue(TInputLine *inputLine, char *dest, std::size_t destSize) {
		std::vector<char> buffer(destSize, '\0');
		inputLine->getData(buffer.data());
		writeRecordField(dest, destSize, readRecordField(buffer.data()));
	}

	ushort currentOptionsMask() noexcept {
		ushort leftMask = 0;
		ushort lineNumbersChoice = kLineNumbersOff;
		ushort eofMarkerChoice = kEofMarkerOff;
		ushort options = 0;

		if (optionsLeftField != nullptr)
			optionsLeftField->getData((void *)&leftMask);
		if (lineNumbersField != nullptr)
			lineNumbersField->getData((void *)&lineNumbersChoice);
		if (eofMarkerField != nullptr)
			eofMarkerField->getData((void *)&eofMarkerChoice);

		if ((leftMask & kLeftOptionTruncateSpaces) != 0)
			options |= kOptionTruncateSpaces;
		if ((leftMask & kLeftOptionEofCtrlZ) != 0)
			options |= kOptionEofCtrlZ;
		if ((leftMask & kLeftOptionEofCrLf) != 0)
			options |= kOptionEofCrLf;
		if ((leftMask & kLeftOptionBackupFiles) != 0)
			options |= kOptionBackupFiles;
		if ((leftMask & kLeftOptionPersistentBlocks) != 0)
			options |= kOptionPersistentBlocks;

		switch (lineNumbersChoice) {
			case kLineNumbersOn:
				options |= kOptionShowLineNumbers;
				break;
			case kLineNumbersLeadingZero:
				options |= kOptionShowLineNumbers;
				options |= kOptionLineNumZeroFill;
				break;
			default:
				break;
		}

		switch (eofMarkerChoice) {
			case kEofMarkerPlain:
				options |= kOptionShowEofMarker;
				break;
			case kEofMarkerEmoji:
				options |= kOptionShowEofMarker;
				options |= kOptionShowEofMarkerEmoji;
				break;
			default:
				break;
		}

		return options;
	}

	void setOptionsMask(ushort options) {
		ushort leftMask = 0;
		ushort lineNumbersChoice = kLineNumbersOff;
		ushort eofMarkerChoice = kEofMarkerOff;

		if ((options & kOptionTruncateSpaces) != 0)
			leftMask |= kLeftOptionTruncateSpaces;
		if ((options & kOptionEofCtrlZ) != 0)
			leftMask |= kLeftOptionEofCtrlZ;
		if ((options & kOptionEofCrLf) != 0)
			leftMask |= kLeftOptionEofCrLf;
		if ((options & kOptionBackupFiles) != 0)
			leftMask |= kLeftOptionBackupFiles;
		if ((options & kOptionPersistentBlocks) != 0)
			leftMask |= kLeftOptionPersistentBlocks;

		if ((options & kOptionShowLineNumbers) != 0) {
			lineNumbersChoice = ((options & kOptionLineNumZeroFill) != 0) ? kLineNumbersLeadingZero
			                                                              : kLineNumbersOn;
		}
		if ((options & kOptionShowEofMarker) != 0) {
			eofMarkerChoice =
			    ((options & kOptionShowEofMarkerEmoji) != 0) ? kEofMarkerEmoji : kEofMarkerPlain;
		}

		if (optionsLeftField != nullptr)
			optionsLeftField->setData((void *)&leftMask);
		if (lineNumbersField != nullptr)
			lineNumbersField->setData((void *)&lineNumbersChoice);
		if (eofMarkerField != nullptr)
			eofMarkerField->setData((void *)&eofMarkerChoice);
	}

	void loadFieldsFromRecord(const EditSettingsDialogRecord &record) {
		setInputLineValue(pageBreakField_, record.pageBreak, sizeof(record.pageBreak));
<<<<<<< HEAD
		setInputLineValue(wordDelimitersField_, record.wordDelimiters, sizeof(record.wordDelimiters));
		setInputLineValue(defaultExtensionsField_, record.defaultExtensions, sizeof(record.defaultExtensions));
=======
		setInputLineValue(wordDelimitersField_, record.wordDelimiters,
		                  sizeof(record.wordDelimiters));
		setInputLineValue(defaultExtensionsField_, record.defaultExtensions,
		                  sizeof(record.defaultExtensions));
>>>>>>> 7a37a03 (git is shit)
		setInputLineValue(tabSizeField_, record.tabSize, sizeof(record.tabSize));
		setOptionsMask(record.optionsMask);
		tabExpandField_->setData((void *)&record.tabExpandChoice);
		columnBlockMoveField_->setData((void *)&record.columnBlockMoveChoice);
		defaultModeField_->setData((void *)&record.defaultModeChoice);
	}

	void saveFieldsToRecord(EditSettingsDialogRecord &record) {
		readInputLineValue(pageBreakField_, record.pageBreak, sizeof(record.pageBreak));
<<<<<<< HEAD
		readInputLineValue(wordDelimitersField_, record.wordDelimiters, sizeof(record.wordDelimiters));
		readInputLineValue(defaultExtensionsField_, record.defaultExtensions, sizeof(record.defaultExtensions));
		readInputLineValue(tabSizeField_, record.tabSize, sizeof(record.tabSize));
		syncOptionDependencies(currentOptionsMask());
=======
		readInputLineValue(wordDelimitersField_, record.wordDelimiters,
		                   sizeof(record.wordDelimiters));
		readInputLineValue(defaultExtensionsField_, record.defaultExtensions,
		                   sizeof(record.defaultExtensions));
		readInputLineValue(tabSizeField_, record.tabSize, sizeof(record.tabSize));
>>>>>>> 7a37a03 (git is shit)
		record.optionsMask = currentOptionsMask();
		tabExpandField_->getData((void *)&record.tabExpandChoice);
		columnBlockMoveField_->getData((void *)&record.columnBlockMoveChoice);
		defaultModeField_->getData((void *)&record.defaultModeChoice);
	}

	EditSettingsDialogRecord initialRecord_;
	EditSettingsDialogRecord currentRecord_;
	TRect contentRect_;
	TGroup *content = nullptr;
	std::vector<ManagedItem> managedViews_;
	TScrollBar *hScrollBar_ = nullptr;
	TScrollBar *vScrollBar_ = nullptr;
	TInputLine *pageBreakField_ = nullptr;
	TInputLine *wordDelimitersField_ = nullptr;
	TInputLine *defaultExtensionsField_ = nullptr;
	TInputLine *tabSizeField_ = nullptr;
<<<<<<< HEAD
	TCheckBoxes *optionsLeftField_ = nullptr;
	TCheckBoxes *optionsRightField_ = nullptr;
=======
	TCheckBoxes *optionsLeftField = nullptr;
	TRadioButtons *lineNumbersField = nullptr;
	TRadioButtons *eofMarkerField = nullptr;
>>>>>>> 7a37a03 (git is shit)
	TRadioButtons *tabExpandField_ = nullptr;
	TRadioButtons *columnBlockMoveField_ = nullptr;
	TRadioButtons *defaultModeField_ = nullptr;
};

void showEditSettingsHelpDummyDialog() {
	std::vector<std::string> lines;
	lines.push_back("EDIT SETTINGS HELP");
	lines.push_back("");
	lines.push_back("Use checkboxes and radio groups to configure edit defaults.");
	lines.push_back("Backup (.bak) creation on save is configured in Options.");
	lines.push_back("EOF marker style is configured with a radio group.");
	lines.push_back("Line number style is configured with a radio group.");
	lines.push_back("Done writes settings.mrmac and reloads silently.");
	lines.push_back("Cancel asks for confirmation when fields were modified.");
	TDialog *dialog = createSetupSimplePreviewDialog("EDIT SETTINGS HELP", 74, 16, lines, false);
	if (dialog != nullptr) {
		TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
	}
}
} // namespace

void runEditSettingsDialogFlow() {
	using namespace MREditSettingsDialogInternal;

	bool running = true;
	EditSettingsDialogRecord workingRecord;

	initEditSettingsDialogRecord(workingRecord);
	while (running) {
		ushort result;
		bool changed = false;
		EditSettingsDialogRecord editedRecord = workingRecord;
		std::string errorText;
		TEditSettingsDialog *dialog = new TEditSettingsDialog(workingRecord);

		if (dialog == nullptr)
			return;
		result = dialog->run(editedRecord, changed);
		TObject::destroy(dialog);
		workingRecord = editedRecord;

		switch (result) {
			case cmMrSetupEditSettingsHelp:
				showEditSettingsHelpDummyDialog();
				break;
			case cmOK:
				if (!saveAndReloadEditSettings(workingRecord, errorText)) {
					messageBox(mfError | mfOKButton, "Installation / Edit settings\n\n%s",
					           errorText.c_str());
					break;
				}
				running = false;
				break;
			case cmCancel:
				if (changed) {
					if (messageBox(mfConfirmation | mfYesButton | mfNoButton,
					               "Discard changed edit settings?") != cmYes)
						break;
				}
				running = false;
				break;
			default:
				running = false;
				break;
		}
	}
}
