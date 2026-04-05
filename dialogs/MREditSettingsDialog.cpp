#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDeskTop
#define Uses_TInputLine
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
	static const int kDialogHeight = 26;

	int inputLeft = 32;
	int inputRight = kDialogWidth - 2;
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
	int doneLeft = kDialogWidth / 2 - 17;
	int cancelLeft = doneLeft + 12;
	int helpLeft = cancelLeft + 14;
	int buttonTop = kDialogHeight - 3;
};

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
		loadFieldsFromRecord(currentRecord_);
		initScrollIfNeeded();
		selectContent();
	}

	ushort run(EditSettingsDialogRecord &outRecord, bool &changed) {
		ushort result = TProgram::deskTop->execView(this);
		saveFieldsToRecord(currentRecord_);
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
	TStaticText *addLabel(const TRect &rect, const char *text) {
		TStaticText *view = new TStaticText(rect, text);
		addManaged(view, rect);
		return view;
	}

	TInputLine *addInput(const TRect &rect, int maxLen) {
		TInputLine *view = new TInputLine(rect, maxLen);
		addManaged(view, rect);
		return view;
	}

	TCheckBoxes *addCheckGroup(const TRect &rect, TSItem *items) {
		TCheckBoxes *view = new TCheckBoxes(rect, items);
		addManaged(view, rect);
		return view;
	}

	TRadioButtons *addRadioGroup(const TRect &rect, TSItem *items) {
		TRadioButtons *view = new TRadioButtons(rect, items);
		addManaged(view, rect);
		return view;
	}

	TButton *addButton(const TRect &rect, const char *title, ushort command, ushort flags) {
		TButton *view = new TButton(rect, title, command, flags);
		addManaged(view, rect);
		return view;
	}

	void buildViews() {
		const EditSettingsLayout g;

		addLabel(TRect(2, 2, g.inputLeft - 2, 3), "Page break string:");
		pageBreakField_ = addInput(TRect(g.inputLeft, 2, g.inputRight, 3), kPageBreakFieldSize - 1);

		addLabel(TRect(2, 4, g.inputLeft - 2, 5), "Word delimiters:");
		wordDelimitersField_ =
		    addInput(TRect(g.inputLeft, 4, g.inputRight, 5), kWordDelimsFieldSize - 1);

		addLabel(TRect(2, 6, g.inputLeft - 2, 7), "Default file extension(s):");
		defaultExtensionsField_ =
		    addInput(TRect(g.inputLeft, 6, g.inputRight, 7), kDefaultExtsFieldSize - 1);

		addLabel(TRect(g.optionsHeadingX, 8, EditSettingsLayout::kDialogWidth - 2, 9), "Options:");

		optionsLeftField_ = addCheckGroup(
		    TRect(g.optionsLeft, 9, g.optionsRight, 14),
		    new TSItem("~T~runcate spaces",
		               new TSItem("Control-~Z~ at EOF",
		                          new TSItem("~C~R/LF at EOF",
		                                     new TSItem("create ~B~ackup (.bak) on save",
		                                                new TSItem("~P~ersistent blocks", nullptr))))));

		addLabel(TRect(g.lineNumbersLeft, 8, g.lineNumbersRight, 9), "Line numbers:");
		lineNumbersField_ = addRadioGroup(
		    TRect(g.lineNumbersLeft, 9, g.lineNumbersRight, 12),
		    new TSItem("~O~ff", new TSItem("O~n~", new TSItem("Leading ~0~", nullptr))));

		addLabel(TRect(g.eofMarkerLeft, 8, g.eofMarkerRight, 9), "EOF marker:");
		eofMarkerField_ = addRadioGroup(
		    TRect(g.eofMarkerLeft, 9, g.eofMarkerRight, 12),
		    new TSItem("~O~ff", new TSItem("~P~lain", new TSItem("~E~moji", nullptr))));

		addLabel(TRect(g.defaultModeLeft, 8, g.defaultModeRight, 9), "Default mode:");
		defaultModeField_ = addRadioGroup(
		    TRect(g.defaultModeLeft, 9, g.defaultModeRight, 12),
		    new TSItem("~I~nsert", new TSItem("~O~verwrite", nullptr)));

		addLabel(TRect(g.tabExpandLeft, 13, g.tabExpandRight, 14), "Tab expand:");
		tabExpandField_ = addRadioGroup(
		    TRect(g.tabExpandLeft, 14, g.tabExpandRight, 17),
		    new TSItem("~T~abs", new TSItem("~S~paces", nullptr)));

		addLabel(TRect(g.columnBlockMoveLeft, 18, g.columnBlockMoveRight, 19), "Column block move:");
		columnBlockMoveField_ = addRadioGroup(
		    TRect(g.columnBlockMoveLeft, 19, g.columnBlockMoveRight, 22),
		    new TSItem("~D~elete space", new TSItem("~L~eave space", nullptr)));

		addLabel(TRect(2, 23, 12, 24), "Tab size:");
		tabSizeField_ = addInput(TRect(13, 23, 20, 24), kTabSizeFieldSize - 1);

		addButton(TRect(g.doneLeft, g.buttonTop, g.doneLeft + 10, g.buttonTop + 2), "~D~one", cmOK,
		          bfDefault);
		addButton(TRect(g.cancelLeft, g.buttonTop, g.cancelLeft + 12, g.buttonTop + 2),
		          "~C~ancel", cmCancel, bfNormal);
		addButton(TRect(g.helpLeft, g.buttonTop, g.helpLeft + 8, g.buttonTop + 2), "~H~elp",
		          cmMrSetupEditSettingsHelp, bfNormal);
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

	void loadFieldsFromRecord(const EditSettingsDialogRecord &record) {
		setInputLineValue(pageBreakField_, record.pageBreak, sizeof(record.pageBreak));
		setInputLineValue(wordDelimitersField_, record.wordDelimiters, sizeof(record.wordDelimiters));
		setInputLineValue(defaultExtensionsField_, record.defaultExtensions,
		                  sizeof(record.defaultExtensions));
		setInputLineValue(tabSizeField_, record.tabSize, sizeof(record.tabSize));
		setOptionsMask(record.optionsMask);
		tabExpandField_->setData((void *)&record.tabExpandChoice);
		columnBlockMoveField_->setData((void *)&record.columnBlockMoveChoice);
		defaultModeField_->setData((void *)&record.defaultModeChoice);
	}

	void saveFieldsToRecord(EditSettingsDialogRecord &record) {
		readInputLineValue(pageBreakField_, record.pageBreak, sizeof(record.pageBreak));
		readInputLineValue(wordDelimitersField_, record.wordDelimiters,
		                   sizeof(record.wordDelimiters));
		readInputLineValue(defaultExtensionsField_, record.defaultExtensions,
		                   sizeof(record.defaultExtensions));
		readInputLineValue(tabSizeField_, record.tabSize, sizeof(record.tabSize));
		record.optionsMask = currentOptionsMask();
		tabExpandField_->getData((void *)&record.tabExpandChoice);
		columnBlockMoveField_->getData((void *)&record.columnBlockMoveChoice);
		defaultModeField_->getData((void *)&record.defaultModeChoice);
	}

	EditSettingsDialogRecord initialRecord_;
	EditSettingsDialogRecord currentRecord_;
	TInputLine *pageBreakField_ = nullptr;
	TInputLine *wordDelimitersField_ = nullptr;
	TInputLine *defaultExtensionsField_ = nullptr;
	TInputLine *tabSizeField_ = nullptr;
	TCheckBoxes *optionsLeftField_ = nullptr;
	TRadioButtons *lineNumbersField_ = nullptr;
	TRadioButtons *eofMarkerField_ = nullptr;
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
