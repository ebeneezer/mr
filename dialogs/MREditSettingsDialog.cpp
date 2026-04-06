#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDeskTop
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRadioButtons
#define Uses_TRect
#define Uses_TStaticText
#define Uses_TSItem
#include <tvision/tv.h>

#include "MREditSettingsDialogInternal.hpp"
#include "MRSetupDialogCommon.hpp"
#include "MRSetupDialogs.hpp"

#include <algorithm>
#include <vector>

namespace {
using namespace MREditSettingsDialogInternal;

enum : ushort {
	cmMrSetupEditSettingsHelp = 3810
};

struct EditSettingsLayout {
	static const int kDialogWidth = 88;
	static const int kDialogHeight = 28;

	explicit EditSettingsLayout(const EditSettingsPanelConfig &config)
	    : labelLeft(config.labelLeft), inputLeft(config.inputLeft),
	      inputRight(config.inputRight > config.inputLeft ? config.inputRight : config.dialogWidth - 2),
	      optionsHeadingX(config.clusterLeft >= 0 ? config.clusterLeft : labelLeft),
	      optionsLeft(optionsHeadingX), optionsRight(optionsLeft + 28), lineNumbersLeft(optionsRight + 3),
	      lineNumbersRight(lineNumbersLeft + 15), eofMarkerLeft(lineNumbersRight + 3),
	      eofMarkerRight(eofMarkerLeft + 15), defaultModeLeft(eofMarkerRight + 3),
	      defaultModeRight(defaultModeLeft + 15), tabExpandLeft(lineNumbersLeft),
	      tabExpandRight(tabExpandLeft + 15), columnBlockMoveLeft(optionsLeft),
	      columnBlockMoveRight(optionsLeft + 18), buttonTop(kDialogHeight - 3), topY(config.topY),
	      pageBreakY(topY), wordDelimitersY(pageBreakY + (config.compactTextRows ? 1 : 2)),
	      defaultExtensionsY(config.includeDefaultExtensions ? wordDelimitersY + (config.compactTextRows ? 1 : 2) : -1),
	      optionsHeadingY(config.clusterTopY >= 0 ? config.clusterTopY :
	                     (config.includeDefaultExtensions ? defaultExtensionsY + 2 : wordDelimitersY + 2)),
	      optionsBodyY(optionsHeadingY + 1), tabExpandHeadingY(optionsBodyY + 5),
	      tabExpandBodyY(tabExpandHeadingY + 1), columnBlockMoveHeadingY(tabExpandBodyY + 4),
	      columnBlockMoveBodyY(columnBlockMoveHeadingY + 1),
	      tabSizeY(config.tabSizeY >= 0 ? config.tabSizeY : columnBlockMoveBodyY + 4),
	      contentBottomY(std::max(tabSizeY + 1, columnBlockMoveBodyY + 3)),
	      doneLeft(config.dialogWidth / 2 - 17), cancelLeft(doneLeft + 12),
	      helpLeft(cancelLeft + 14) {
	}

	int labelLeft;
	int inputLeft;
	int inputRight;
	int optionsHeadingX;
	int optionsLeft;
	int optionsRight;
	int lineNumbersLeft;
	int lineNumbersRight;
	int eofMarkerLeft;
	int eofMarkerRight;
	int defaultModeLeft;
	int defaultModeRight;
	int tabExpandLeft;
	int tabExpandRight;
	int columnBlockMoveLeft;
	int columnBlockMoveRight;
	int buttonTop;
	int topY;
	int pageBreakY;
	int wordDelimitersY;
	int defaultExtensionsY;
	int optionsHeadingY;
	int optionsBodyY;
	int tabExpandHeadingY;
	int tabExpandBodyY;
	int columnBlockMoveHeadingY;
	int columnBlockMoveBodyY;
	int tabSizeY;
	int contentBottomY;
	int doneLeft;
	int cancelLeft;
	int helpLeft;
};

TStaticText *addPanelLabel(MRScrollableDialog &dialog, const TRect &rect, const char *text) {
	TStaticText *view = new TStaticText(rect, text);
	dialog.addManaged(view, rect);
	return view;
}

TInputLine *addPanelInput(MRScrollableDialog &dialog, const TRect &rect, int maxLen) {
	TInputLine *view = new TInputLine(rect, maxLen);
	dialog.addManaged(view, rect);
	return view;
}

class TNumericInputLine : public TInputLine {
  public:
	TNumericInputLine(const TRect &bounds, int maxLen) noexcept : TInputLine(bounds, maxLen) {
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			const ushort keyCode = event.keyDown.keyCode;
			const unsigned char ch = static_cast<unsigned char>(event.keyDown.charScan.charCode);
			if ((ch >= '0' && ch <= '9') || keyCode == kbBack || keyCode == kbDel || keyCode == kbLeft ||
			    keyCode == kbRight || keyCode == kbHome || keyCode == kbEnd || keyCode == kbTab ||
			    keyCode == kbShiftTab || keyCode == kbCtrlI || keyCode == kbEnter) {
				TInputLine::handleEvent(event);
				return;
			}
			clearEvent(event);
			return;
		}
		TInputLine::handleEvent(event);
	}
};

TInputLine *addNumericPanelInput(MRScrollableDialog &dialog, const TRect &rect, int maxLen) {
	TInputLine *view = new TNumericInputLine(rect, maxLen);
	dialog.addManaged(view, rect);
	return view;
}

TCheckBoxes *addPanelCheckGroup(MRScrollableDialog &dialog, const TRect &rect, TSItem *items) {
	TCheckBoxes *view = new TCheckBoxes(rect, items);
	dialog.addManaged(view, rect);
	return view;
}

TRadioButtons *addPanelRadioGroup(MRScrollableDialog &dialog, const TRect &rect, TSItem *items) {
	TRadioButtons *view = new TRadioButtons(rect, items);
	dialog.addManaged(view, rect);
	return view;
}

class TEditSettingsDialog : public MRScrollableDialog {
  public:
	explicit TEditSettingsDialog(const EditSettingsDialogRecord &initialRecord)
	    : TWindowInit(&TDialog::initFrame),
	      MRScrollableDialog(centeredSetupDialogRect(EditSettingsLayout::kDialogWidth,
	                                                 EditSettingsLayout::kDialogHeight),
	                         "EDIT SETTINGS", EditSettingsLayout::kDialogWidth,
	                         EditSettingsLayout::kDialogHeight),
	      initialRecord_(initialRecord), currentRecord_(initialRecord) {
		buildViews();
		panel_.loadFieldsFromRecord(currentRecord_);
		initScrollIfNeeded();
		panel_.primaryView() != nullptr ? panel_.primaryView()->select() : selectContent();
	}

	ushort run(EditSettingsDialogRecord &outRecord, bool &changed) {
		ushort result = TProgram::deskTop->execView(this);
		panel_.saveFieldsToRecord(currentRecord_);
		outRecord = currentRecord_;
		changed = !recordsEqual(initialRecord_, currentRecord_);
		return result;
	}

	void handleEvent(TEvent &event) override {
		MRScrollableDialog::handleEvent(event);

		if (event.what == evCommand && event.message.command == cmMrSetupEditSettingsHelp) {
			endModal(event.message.command);
			clearEvent(event);
		}
	}

  private:
	TButton *addButton(const TRect &rect, const char *title, ushort command, ushort flags) {
		TButton *view = new TButton(rect, title, command, flags);
		addManaged(view, rect);
		return view;
	}

	void buildViews() {
		const EditSettingsLayout g((EditSettingsPanelConfig()));

		panel_.buildViews(*this);
		addButton(TRect(g.doneLeft, g.buttonTop, g.doneLeft + 10, g.buttonTop + 2), "~D~one", cmOK,
		          bfDefault);
		addButton(TRect(g.cancelLeft, g.buttonTop, g.cancelLeft + 12, g.buttonTop + 2),
		          "~C~ancel", cmCancel, bfNormal);
		addButton(TRect(g.helpLeft, g.buttonTop, g.helpLeft + 8, g.buttonTop + 2), "~H~elp",
		          cmMrSetupEditSettingsHelp, bfNormal);
	}

	EditSettingsDialogRecord initialRecord_;
	EditSettingsDialogRecord currentRecord_;
	EditSettingsPanel panel_;
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

namespace MREditSettingsDialogInternal {

EditSettingsPanel::EditSettingsPanel(const EditSettingsPanelConfig &config) : config_(config) {
}

void EditSettingsPanel::buildViews(MRScrollableDialog &dialog) {
	const EditSettingsLayout g(config_);

	addPanelLabel(dialog, TRect(g.labelLeft, g.pageBreakY, g.inputLeft - 2, g.pageBreakY + 1),
	              "Page break:");
	pageBreakField_ = addPanelInput(dialog, TRect(g.inputLeft, g.pageBreakY, g.inputRight, g.pageBreakY + 1),
	                               kPageBreakFieldSize - 1);

	addPanelLabel(dialog,
	              TRect(g.labelLeft, g.wordDelimitersY, g.inputLeft - 2, g.wordDelimitersY + 1),
	              "Word delim:");
	wordDelimitersField_ = addPanelInput(dialog,
	                                    TRect(g.inputLeft, g.wordDelimitersY, g.inputRight,
	                                          g.wordDelimitersY + 1),
	                                    kWordDelimsFieldSize - 1);

	if (config_.includeDefaultExtensions) {
		addPanelLabel(dialog,
		              TRect(g.labelLeft, g.defaultExtensionsY, g.inputLeft - 2, g.defaultExtensionsY + 1),
		              "Auto ext.:");
		defaultExtensionsField_ = addPanelInput(
		    dialog, TRect(g.inputLeft, g.defaultExtensionsY, g.inputRight, g.defaultExtensionsY + 1),
		    kDefaultExtsFieldSize - 1);
	}

	addPanelLabel(dialog, TRect(g.labelLeft, g.tabSizeY, g.inputLeft - 2, g.tabSizeY + 1), "Tab size:");
	tabSizeField_ = addNumericPanelInput(dialog,
	                             TRect(g.inputLeft, g.tabSizeY, g.inputLeft + config_.tabSizeFieldWidth,
	                                   g.tabSizeY + 1),
	                             kTabSizeFieldSize - 1);

	addPanelLabel(dialog,
	              TRect(g.optionsHeadingX, g.optionsHeadingY, config_.dialogWidth - 2, g.optionsHeadingY + 1),
	              "Options:");

	optionsLeftField_ = addPanelCheckGroup(
	    dialog, TRect(g.optionsLeft, g.optionsBodyY, g.optionsRight, g.optionsBodyY + 6),
	    new TSItem("~T~runcate spaces",
	               new TSItem("Control-~Z~ at EOF",
	                          new TSItem("~C~R/LF at EOF",
	                                     new TSItem("create ~B~ackup (.bak) on save",
	                                                new TSItem("~P~ersistent blocks",
	                                                         new TSItem("code fo~L~ding", nullptr)))))));

	addPanelLabel(dialog,
	              TRect(g.lineNumbersLeft, g.optionsHeadingY, g.lineNumbersRight, g.optionsHeadingY + 1),
	              "Line numbers:");
	lineNumbersField_ = addPanelRadioGroup(
	    dialog, TRect(g.lineNumbersLeft, g.optionsBodyY, g.lineNumbersRight, g.optionsBodyY + 3),
	    new TSItem("~O~ff", new TSItem("O~n~", new TSItem("Leading ~0~", nullptr))));

	addPanelLabel(dialog,
	              TRect(g.eofMarkerLeft, g.optionsHeadingY, g.eofMarkerRight, g.optionsHeadingY + 1),
	              "EOF marker:");
	eofMarkerField_ = addPanelRadioGroup(
	    dialog, TRect(g.eofMarkerLeft, g.optionsBodyY, g.eofMarkerRight, g.optionsBodyY + 3),
	    new TSItem("~O~ff", new TSItem("~P~lain", new TSItem("~E~moji", nullptr))));

	addPanelLabel(dialog,
	              TRect(g.defaultModeLeft, g.optionsHeadingY, g.defaultModeRight, g.optionsHeadingY + 1),
	              "Default mode:");
	defaultModeField_ = addPanelRadioGroup(
	    dialog, TRect(g.defaultModeLeft, g.optionsBodyY, g.defaultModeRight, g.optionsBodyY + 3),
	    new TSItem("~I~nsert", new TSItem("~O~verwrite", nullptr)));

	addPanelLabel(dialog,
	              TRect(g.tabExpandLeft, g.tabExpandHeadingY, g.tabExpandRight, g.tabExpandHeadingY + 1),
	              "Tab expand:");
	tabExpandField_ = addPanelRadioGroup(
	    dialog, TRect(g.tabExpandLeft, g.tabExpandBodyY, g.tabExpandRight, g.tabExpandBodyY + 3),
	    new TSItem("~T~abs", new TSItem("~S~paces", nullptr)));

	addPanelLabel(dialog,
	              TRect(g.columnBlockMoveLeft, g.columnBlockMoveHeadingY, g.columnBlockMoveLeft + 18,
	                    g.columnBlockMoveHeadingY + 1),
	              "Column block move:");
	columnBlockMoveField_ = addPanelRadioGroup(
	    dialog,
	    TRect(g.columnBlockMoveLeft, g.columnBlockMoveBodyY, g.columnBlockMoveRight,
	          g.columnBlockMoveBodyY + 3),
	    new TSItem("~D~elete space", new TSItem("~L~eave space", nullptr)));

}

TView *EditSettingsPanel::primaryView() const noexcept {
	return pageBreakField_;
}

void EditSettingsPanel::setInputLineValue(TInputLine *inputLine, const char *value, std::size_t capacity) {
	std::vector<char> buffer(capacity, '\0');
	writeRecordField(buffer.data(), buffer.size(), readRecordField(value));
	inputLine->setData(buffer.data());
}

void EditSettingsPanel::readInputLineValue(TInputLine *inputLine, char *dest, std::size_t destSize) {
	std::vector<char> buffer(destSize, '\0');
	inputLine->getData(buffer.data());
	writeRecordField(dest, destSize, readRecordField(buffer.data()));
}

ushort EditSettingsPanel::currentOptionsMask() const noexcept {
	ushort leftMask = 0;
	ushort lineNumbersChoice = kLineNumbersOff;
	ushort eofMarkerChoice = kEofMarkerOff;
	ushort options = 0;

	if (optionsLeftField_ != nullptr)
		optionsLeftField_->getData((void *)&leftMask);
	if (lineNumbersField_ != nullptr)
		lineNumbersField_->getData((void *)&lineNumbersChoice);
	if (eofMarkerField_ != nullptr)
		eofMarkerField_->getData((void *)&eofMarkerChoice);

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
	if ((leftMask & kLeftOptionCodeFolding) != 0)
		options |= kOptionCodeFolding;

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

void EditSettingsPanel::setOptionsMask(ushort options) {
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
	if ((options & kOptionCodeFolding) != 0)
		leftMask |= kLeftOptionCodeFolding;

	if ((options & kOptionShowLineNumbers) != 0)
		lineNumbersChoice = ((options & kOptionLineNumZeroFill) != 0) ? kLineNumbersLeadingZero
		                                                             : kLineNumbersOn;
	if ((options & kOptionShowEofMarker) != 0)
		eofMarkerChoice = ((options & kOptionShowEofMarkerEmoji) != 0) ? kEofMarkerEmoji
		                                                              : kEofMarkerPlain;

	if (optionsLeftField_ != nullptr)
		optionsLeftField_->setData((void *)&leftMask);
	if (lineNumbersField_ != nullptr)
		lineNumbersField_->setData((void *)&lineNumbersChoice);
	if (eofMarkerField_ != nullptr)
		eofMarkerField_->setData((void *)&eofMarkerChoice);
}

void EditSettingsPanel::loadFieldsFromRecord(const EditSettingsDialogRecord &record) {
	setInputLineValue(pageBreakField_, record.pageBreak, sizeof(record.pageBreak));
	setInputLineValue(wordDelimitersField_, record.wordDelimiters, sizeof(record.wordDelimiters));
	if (defaultExtensionsField_ != nullptr)
		setInputLineValue(defaultExtensionsField_, record.defaultExtensions,
		                  sizeof(record.defaultExtensions));
	setInputLineValue(tabSizeField_, record.tabSize, sizeof(record.tabSize));
	setOptionsMask(record.optionsMask);
	if (tabExpandField_ != nullptr)
		tabExpandField_->setData((void *)&record.tabExpandChoice);
	if (columnBlockMoveField_ != nullptr)
		columnBlockMoveField_->setData((void *)&record.columnBlockMoveChoice);
	if (defaultModeField_ != nullptr)
		defaultModeField_->setData((void *)&record.defaultModeChoice);
}

void EditSettingsPanel::saveFieldsToRecord(EditSettingsDialogRecord &record) const {
	readInputLineValue(pageBreakField_, record.pageBreak, sizeof(record.pageBreak));
	readInputLineValue(wordDelimitersField_, record.wordDelimiters, sizeof(record.wordDelimiters));
	if (defaultExtensionsField_ != nullptr)
		readInputLineValue(defaultExtensionsField_, record.defaultExtensions,
		                   sizeof(record.defaultExtensions));
	readInputLineValue(tabSizeField_, record.tabSize, sizeof(record.tabSize));
	record.optionsMask = currentOptionsMask();
	if (tabExpandField_ != nullptr)
		tabExpandField_->getData((void *)&record.tabExpandChoice);
	if (columnBlockMoveField_ != nullptr)
		columnBlockMoveField_->getData((void *)&record.columnBlockMoveChoice);
	if (defaultModeField_ != nullptr)
		defaultModeField_->getData((void *)&record.defaultModeChoice);
}

} // namespace MREditSettingsDialogInternal

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
