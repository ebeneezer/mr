#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDialog
#define Uses_TDeskTop
#define Uses_TGroup
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRadioButtons
#define Uses_TRect
#define Uses_TScrollBar
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
	int dialogWidth = 88;
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
};

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
		content_ = createSetupDialogContentGroup(contentRect_);
		if (content_ != nullptr) {
			content_->options |= ofSelectable;
			insert(content_);
		}

		buildDialog();
		loadFieldsFromRecord(currentRecord_);
		initScrollIfNeeded();
		initializeFocus();
	}

	ushort run(EditSettingsDialogRecord &outRecord, bool &changed) {
		ushort result = TProgram::deskTop->execView(this);
		saveFieldsToRecord(currentRecord_);
		outRecord = currentRecord_;
		changed = !recordsEqual(initialRecord_, currentRecord_);
		return result;
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown && content_ != nullptr) {
			ushort keyCode = event.keyDown.keyCode;

			if (keyCode == kbTab || keyCode == kbCtrlI) {
				content_->selectNext(False);
				clearEvent(event);
				return;
			}
			if (keyCode == kbShiftTab) {
				content_->selectNext(True);
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
	static const int kButtonTop = kVirtualDialogHeight - 3;

	void buildDialog() {
		EditSettingsLayout l;

		addStaticText(TRect(2, 2, l.inputLeft - 2, 3), "Page break string:");
		pageBreakField_ = addInputLine(TRect(l.inputLeft, 2, l.inputRight, 3), kPageBreakFieldSize - 1);

		addStaticText(TRect(2, 4, l.inputLeft - 2, 5), "Word delimiters:");
		wordDelimitersField_ =
		    addInputLine(TRect(l.inputLeft, 4, l.inputRight, 5), kWordDelimsFieldSize - 1);

		addStaticText(TRect(2, 6, l.inputLeft - 2, 7), "Default file extension(s):");
		defaultExtensionsField_ =
		    addInputLine(TRect(l.inputLeft, 6, l.inputRight, 7), kDefaultExtsFieldSize - 1);

		addStaticText(TRect(l.optionsHeadingX, 8, l.dialogWidth - 2, 9), "Options:");

		addStaticText(TRect(l.lineNumbersLeft, 8, l.lineNumbersRight, 9), "Line numbers:");
		addStaticText(TRect(l.eofMarkerLeft, 8, l.eofMarkerRight, 9), "EOF marker:");
		addStaticText(TRect(l.defaultModeLeft, 8, l.defaultModeRight, 9), "Default mode:");
		addStaticText(TRect(l.tabExpandLeft, 13, l.tabExpandRight, 14), "Tab expand:");
		addStaticText(TRect(l.columnBlockMoveLeft, 18, l.columnBlockMoveRight, 19),
		              "Column block move:");
		addStaticText(TRect(2, 23, 12, 24), "Tab size:");

		optionsLeftField_ = addCheckBoxes(
		    TRect(l.optionsLeft, 9, l.optionsRight, 14),
		    new TSItem("~T~runcate spaces",
		               new TSItem("Control-~Z~ at EOF",
		                          new TSItem("~C~R/LF at EOF",
		                                     new TSItem("create ~B~ackup (.bak) on save",
		                                                new TSItem("~P~ersistent blocks", nullptr))))));

		lineNumbersField_ = addRadioButtons(
		    TRect(l.lineNumbersLeft, 9, l.lineNumbersRight, 12),
		    new TSItem("~O~ff", new TSItem("O~n~", new TSItem("Leading ~0~", nullptr))));

		eofMarkerField_ = addRadioButtons(
		    TRect(l.eofMarkerLeft, 9, l.eofMarkerRight, 12),
		    new TSItem("~O~ff", new TSItem("~P~lain", new TSItem("~E~moji", nullptr))));

		defaultModeField_ = addRadioButtons(
		    TRect(l.defaultModeLeft, 9, l.defaultModeRight, 12),
		    new TSItem("~I~nsert", new TSItem("~O~verwrite", nullptr)));

		tabExpandField_ = addRadioButtons(
		    TRect(l.tabExpandLeft, 14, l.tabExpandRight, 17),
		    new TSItem("~T~abs", new TSItem("~S~paces", nullptr)));

		columnBlockMoveField_ = addRadioButtons(
		    TRect(l.columnBlockMoveLeft, 19, l.columnBlockMoveRight, 22),
		    new TSItem("~D~elete space", new TSItem("~L~eave space", nullptr)));

		tabSizeField_ = addInputLine(TRect(13, 23, 20, 24), kTabSizeFieldSize - 1);

		addButton(TRect(l.doneLeft, kButtonTop, l.doneLeft + 10, kButtonTop + 2), "~D~one", cmOK,
		          bfDefault);
		addButton(TRect(l.cancelLeft, kButtonTop, l.cancelLeft + 12, kButtonTop + 2), "~C~ancel",
		          cmCancel, bfNormal);
		addButton(TRect(l.helpLeft, kButtonTop, l.helpLeft + 8, kButtonTop + 2), "~H~elp",
		          cmMrSetupEditSettingsHelp, bfNormal);
	}

	void initializeFocus() {
		if (content_ != nullptr) {
			content_->resetCurrent();
			content_->select();
		}
	}

	void addManaged(TView *view, const TRect &base) {
		ManagedItem item;
		item.view = view;
		item.base = base;
		managedViews_.push_back(item);
		if (content_ != nullptr) {
			TRect local = base;
			local.move(-contentRect_.a.x, -contentRect_.a.y);
			view->locate(local);
			content_->insert(view);
		} else
			insert(view);
	}

	void addStaticText(const TRect &bounds, const char *text) {
		addManaged(new TStaticText(bounds, text), bounds);
	}

	TInputLine *addInputLine(const TRect &bounds, int maxLen) {
		TInputLine *view = new TInputLine(bounds, maxLen);
		addManaged(view, bounds);
		return view;
	}

	TCheckBoxes *addCheckBoxes(const TRect &bounds, TSItem *items) {
		TCheckBoxes *view = new TCheckBoxes(bounds, items);
		addManaged(view, bounds);
		return view;
	}

	TRadioButtons *addRadioButtons(const TRect &bounds, TSItem *items) {
		TRadioButtons *view = new TRadioButtons(bounds, items);
		addManaged(view, bounds);
		return view;
	}

	TButton *addButton(const TRect &bounds, const char *title, ushort command, ushort flags) {
		TButton *view = new TButton(bounds, title, command, flags);
		addManaged(view, bounds);
		return view;
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
		if (content_ != nullptr)
			content_->drawView();
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
		if (content_ != nullptr)
			content_->locate(contentRect_);

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
			int maxDy =
			    std::max(0, virtualContentHeight - std::max(1, contentRect_.b.y - contentRect_.a.y));
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

	ushort currentOptionsMask() const noexcept {
		ushort leftMask = 0;
		ushort lineNumbersChoice = kLineNumbersOff;
		ushort eofMarkerChoice = kEofMarkerOff;

		if (optionsLeftField_ != nullptr)
			optionsLeftField_->getData((void *)&leftMask);
		if (lineNumbersField_ != nullptr)
			lineNumbersField_->getData((void *)&lineNumbersChoice);
		if (eofMarkerField_ != nullptr)
			eofMarkerField_->getData((void *)&eofMarkerChoice);

		return buildOptionsMask(leftMask, lineNumbersChoice, eofMarkerChoice);
	}

	void setOptionsMask(ushort options) {
		ushort leftMask = 0;
		ushort lineNumbersChoice = kLineNumbersOff;
		ushort eofMarkerChoice = kEofMarkerOff;

		splitOptionsMask(options, leftMask, lineNumbersChoice, eofMarkerChoice);

		if (optionsLeftField_ != nullptr)
			optionsLeftField_->setData((void *)&leftMask);
		if (lineNumbersField_ != nullptr)
			lineNumbersField_->setData((void *)&lineNumbersChoice);
		if (eofMarkerField_ != nullptr)
			eofMarkerField_->setData((void *)&eofMarkerChoice);
	}

	void loadFieldsFromRecord(const EditSettingsDialogRecord &record) {
		setInputLineValue(pageBreakField_, record.pageBreak, sizeof(record.pageBreak));
		setInputLineValue(wordDelimitersField_, record.wordDelimiters,
		                  sizeof(record.wordDelimiters));
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
	TRect contentRect_;
	TGroup *content_ = nullptr;
	std::vector<ManagedItem> managedViews_;
	TScrollBar *hScrollBar_ = nullptr;
	TScrollBar *vScrollBar_ = nullptr;
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
