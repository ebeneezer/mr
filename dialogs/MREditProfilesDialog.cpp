#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TChDirDialog
#define Uses_TCollection
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TFileDialog
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TListBox
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRadioButtons
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TSItem
#define Uses_TView
#include <tvision/tv.h>

#include "MREditProfilesDraftSupport.hpp"
#include "MREditProfilesPanelInternal.hpp"
#include "MREditProfilesRecordSupport.hpp"
#include "MRSetupDialogCommon.hpp"

#include "../app/TMREditorApp.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/TMRMenuBar.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
using namespace MREditProfilesDialogInternal;

enum : ushort {
	cmMrSetupFilenameProfilesHelp = 3820,
	cmMrSetupFilenameProfilesAdd,
	cmMrSetupFilenameProfilesCopy,
	cmMrSetupFilenameProfilesDelete,
	cmMrSetupFilenameProfilesBrowseColorTheme,
	cmMrSetupFilenameProfilesBrowsePostLoadMacro,
	cmMrSetupFilenameProfilesBrowsePreSaveMacro,
	cmMrSetupFilenameProfilesBrowseDefaultPath,
	cmMrSetupFilenameProfilesSelectionChanged,
	cmMrSetupFilenameProfilesFieldChanged,
	cmMrSetupFilenameProfilesFieldFocusChanged
};

enum {
	kProfileIdFieldSize = 64,
	kProfileNameFieldSize = 128,
	kProfileExtensionsFieldSize = 256,
	kProfileColorThemeFieldSize = 256
};

const char *kDefaultProfileId = "DEFAULT";

char *dupCString(const std::string &value) {
	char *copy = new char[value.size() + 1];
	std::memcpy(copy, value.c_str(), value.size() + 1);
	return copy;
}

class TPlainStringCollection : public TCollection {
  public:
	TPlainStringCollection(short aLimit, short aDelta) noexcept : TCollection(aLimit, aDelta) {
	}

  protected:
	void freeItem(void *item) override {
		delete[] static_cast<char *>(item);
	}

  private:
	void *readItem(ipstream &) override {
		return nullptr;
	}

	void writeItem(void *, opstream &) override {
	}
};

class TProfileListBox : public TListBox {
  public:
	TProfileListBox(const TRect &bounds, TScrollBar *aScrollBar) noexcept : TListBox(bounds, 1, aScrollBar) {
	}

	void focusItemNum(short item) override {
		short oldFocused = focused;

		TListBox::focusItemNum(item);
		if (focused != oldFocused)
			dispatchSelectionChanged();
	}

  private:
	void dispatchSelectionChanged() {
		TView *target = owner;

		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;
		message(target != nullptr ? target : owner, evBroadcast, cmMrSetupFilenameProfilesSelectionChanged,
		        this);
	}
};

TAttrPair configuredColorOr(TView *view, unsigned char paletteSlot, ushort fallbackColorIndex) {
	unsigned char biosAttr = 0;

	if (configuredColorSlotOverride(paletteSlot, biosAttr))
		return TAttrPair(biosAttr);
	return view != nullptr ? view->getColor(fallbackColorIndex) : TAttrPair(0x70);
}

class TInactiveStaticText : public TStaticText {
  public:
	TInactiveStaticText(const TRect &bounds, const char *text) noexcept : TStaticText(bounds, text) {
	}

	void setInactive(bool inactive) {
		if (inactive_ != inactive) {
			inactive_ = inactive;
			drawView();
		}
	}

	void draw() override {
		TDrawBuffer buffer;
		char text[256];
		TAttrPair color = inactive_ ? configuredColorOr(this, kMrPaletteDialogInactiveElements, 1) : getColor(1);

		buffer.moveChar(0, ' ', color, size.x);
		getText(text);
		buffer.moveStr(0, text, color, size.x);
		writeLine(0, 0, size.x, size.y, buffer);
	}

  private:
	bool inactive_ = false;
};

class TInlineGlyphButton : public TView {
  public:
	TInlineGlyphButton(const TRect &bounds, const char *glyph, ushort command)
	    : TView(bounds), glyph_(glyph != nullptr ? glyph : ""), command_(command) {
		options |= ofSelectable;
		options |= ofFirstClick;
		eventMask |= evMouseDown | evKeyDown;
	}

	void draw() override {
		TDrawBuffer buffer;
		ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
		int x = 0;

		buffer.moveChar(0, ' ', color, size.x);
		if (size.x > 1)
			x = (size.x - 1) / 2;
		buffer.moveStr(static_cast<ushort>(x), glyph_.c_str(), color, size.x - x);
		writeLine(0, 0, size.x, size.y, buffer);
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evMouseDown) {
			dispatchCommand();
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			TKey key(event.keyDown);

			if (key == TKey(kbEnter) || key == TKey(' ')) {
				dispatchCommand();
				clearEvent(event);
				return;
			}
		}
		TView::handleEvent(event);
	}

  private:
	void dispatchCommand() {
		TView *target = owner;

		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;
		message(target != nullptr ? target : owner, evCommand, command_, this);
	}

	std::string glyph_;
	ushort command_;
};

class TNotifyingInputLine : public TInputLine {
  public:
	TNotifyingInputLine(const TRect &bounds, int maxLen) noexcept : TInputLine(bounds, maxLen), capacity_(maxLen + 1) {
	}

	void handleEvent(TEvent &event) override {
		std::string beforeText = currentText();
		bool beforeFocused = (state & sfFocused) != 0;
		ushort originalWhat = event.what;
		ushort originalCommand = event.what == evBroadcast ? event.message.command : 0;
		TView *target = owner;

		TInputLine::handleEvent(event);
		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;

		if (currentText() != beforeText)
			message(target != nullptr ? target : owner, evBroadcast,
			        cmMrSetupFilenameProfilesFieldChanged, this);

		if (((state & sfFocused) != 0) != beforeFocused ||
		    (originalWhat == evBroadcast &&
		     (originalCommand == cmReleasedFocus || originalCommand == cmReceivedFocus)))
			message(target != nullptr ? target : owner, evBroadcast,
			        cmMrSetupFilenameProfilesFieldFocusChanged, this);
	}

  private:
	std::string currentText() const {
		std::vector<char> buffer(capacity_, '\0');
		const_cast<TNotifyingInputLine *>(this)->getData(buffer.data());
		return std::string(buffer.data());
	}

	std::size_t capacity_ = 0;
};

class TReadOnlyAwareInputLine : public TNotifyingInputLine {
  public:
	TReadOnlyAwareInputLine(const TRect &bounds, int maxLen) noexcept : TNotifyingInputLine(bounds, maxLen) {
	}

	void setReadOnly(bool readOnly) noexcept {
		readOnly_ = readOnly;
	}

	void setWarningText(const std::string &warningText) {
		warningText_ = warningText;
	}

	void handleEvent(TEvent &event) override {
		if (!readOnly_) {
			TNotifyingInputLine::handleEvent(event);
			return;
		}

		if (event.what == evMouseDown) {
			select();
			postWarning();
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown && isMutatingKey(event)) {
			postWarning();
			clearEvent(event);
			return;
		}
		TNotifyingInputLine::handleEvent(event);
	}

  private:
	static bool isNavigationKey(ushort keyCode) noexcept {
		return keyCode == kbTab || keyCode == kbShiftTab || keyCode == kbCtrlI || keyCode == kbLeft ||
		       keyCode == kbRight || keyCode == kbUp || keyCode == kbDown || keyCode == kbHome ||
		       keyCode == kbEnd || keyCode == kbEsc || keyCode == kbEnter;
	}

	bool isMutatingKey(const TEvent &event) const noexcept {
		ushort keyCode = event.keyDown.keyCode;
		unsigned char ch = static_cast<unsigned char>(event.keyDown.charScan.charCode);
		if (isNavigationKey(keyCode))
			return false;
		if (keyCode == kbBack || keyCode == kbDel)
			return true;
		return std::isprint(ch) != 0;
	}

	void postWarning() const {
		if (warningText_.empty())
			return;
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, warningText_,
		                              mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
	}

	bool readOnly_ = false;
	std::string warningText_;
};

std::string readInputLineString(TInputLine *inputLine, std::size_t capacity) {
	std::vector<char> buffer(capacity, '\0');

	if (inputLine == nullptr)
		return std::string();
	inputLine->getData(buffer.data());
	return readRecordField(buffer.data());
}

void writeInputLineString(TInputLine *inputLine, const std::string &value, std::size_t capacity) {
	std::vector<char> buffer(capacity, '\0');

	if (inputLine == nullptr)
		return;
	writeRecordField(buffer.data(), buffer.size(), value);
	inputLine->setData(buffer.data());
}


std::string currentWorkingDirectoryLocal() {
	char cwd[PATH_MAX];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr)
		return std::string();
	return std::string(cwd);
}

bool browseMrmacFileUri(const char *title, const std::string &currentValue, std::string &selectedUri) {
	char fileName[MAXPATH];
	std::string seed = trimAscii(currentValue);
	ushort result;

	if (!seed.empty())
		seed = normalizeConfiguredPathInput(seed);
	if (!seed.empty())
		writeRecordField(fileName, sizeof(fileName), seed);
	else
		initRememberedLoadDialogPath(fileName, sizeof(fileName), "*.mrmac");
	result = mr::dialogs::execDialogRawWithData(new TFileDialog("*.mrmac", title, "~N~ame", fdOKButton, 234), fileName);
	if (result == cmCancel)
		return false;
	rememberLoadDialogPath(fileName);
	selectedUri = normalizeConfiguredPathInput(fileName);
	return !selectedUri.empty();
}

bool browseDirectoryPath(const std::string &currentValue, std::string &selectedPath) {
	std::string originalCwd = currentWorkingDirectoryLocal();
	std::string seed = normalizeConfiguredPathInput(trimAscii(currentValue));
	std::string picked;
	ushort result;

	if (!seed.empty())
		(void)::chdir(seed.c_str());
	result = mr::dialogs::execDialogRaw(new TChDirDialog(cdNormal, 235));
	picked = currentWorkingDirectoryLocal();
	if (!originalCwd.empty())
		(void)::chdir(originalCwd.c_str());
	if (result == cmCancel)
		return false;
	selectedPath = normalizeConfiguredPathInput(picked);
	return !selectedPath.empty();
}

bool browseColorThemeUri(const std::string &currentValue, std::string &selectedUri) {
	char fileName[MAXPATH];
	std::string seed = trimAscii(currentValue);
	TFileDialog *dialog = nullptr;
	ushort result = cmCancel;

	if (!seed.empty()) {
		seed = normalizeConfiguredPathInput(seed);
		writeRecordField(fileName, sizeof(fileName), seed);
	} else {
		std::string macroPath = configuredMacroDirectoryPath();
		if (macroPath.empty())
			initRememberedLoadDialogPath(fileName, sizeof(fileName), "*.mrmac");
		else {
			macroPath = normalizeConfiguredPathInput(macroPath);
			if (!macroPath.empty() && macroPath.back() != '/')
				macroPath += '/';
			macroPath += "*.mrmac";
			writeRecordField(fileName, sizeof(fileName), macroPath);
		}
	}
	dialog = new TFileDialog("*.mrmac", "Color theme file", "~N~ame", fdOKButton, 232);
	if (dialog == nullptr)
		return false;
	dialog->setData(fileName);
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel)
		dialog->getData(fileName);
	TObject::destroy(dialog);
	if (result == cmCancel)
		return false;
	rememberLoadDialogPath(fileName);
	selectedUri = normalizeConfiguredPathInput(fileName);
	return true;
}

EditSettingsPanelConfig makeProfilesPanelConfig(int dialogWidth, int labelLeft, int inputLeft, int inputRight,
                                                int topY) {
	EditSettingsPanelConfig config;
	config.topY = topY;
	config.dialogWidth = dialogWidth;
	config.labelLeft = labelLeft;
	config.inputLeft = inputLeft;
	config.inputRight = inputRight;
	config.clusterLeft = 2;
	config.clusterTopY = -1;
	config.tabSizeY = topY + 3;
	config.includeDefaultExtensions = true;
	config.compactTextRows = true;
	config.tabExpandBesideDefaultMode = true;
	return config;
}


mr::messageline::Kind toMessageLineKind(TMRMenuBar::MarqueeKind kind) {
	switch (kind) {
		case TMRMenuBar::MarqueeKind::Success:
			return mr::messageline::Kind::Success;
		case TMRMenuBar::MarqueeKind::Warning:
			return mr::messageline::Kind::Warning;
		case TMRMenuBar::MarqueeKind::Error:
			return mr::messageline::Kind::Error;
		case TMRMenuBar::MarqueeKind::Hero:
		case TMRMenuBar::MarqueeKind::Info:
		default:
			return mr::messageline::Kind::Info;
	}
}

void setDialogStatus(const std::string &text, TMRMenuBar::MarqueeKind kind) {
	if (text.empty()) {
		mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
		return;
	}
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogValidation, text, toMessageLineKind(kind),
	                              mr::messageline::kPriorityHigh);
}

void clearDialogStatus() {
	mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
}

void postValidationWarning(const std::string &text) {
	if (text.empty())
		return;
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogValidation, text,
	                              mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
}

ushort runDirtyProfilesDialog(const std::vector<std::string> &dirtyIds) {
	class TDirtyProfilesDialog : public TDialog {
	  public:
		TDirtyProfilesDialog(const std::string &ids)
		    : TWindowInit(&TDialog::initFrame),
		      TDialog(centeredSetupDialogRect(74, 11), "UNSAVED PROFILES") {
			insert(new TStaticText(TRect(2, 2, 70, 3), "Discard changed filename-extension profiles?"));
			insert(new TStaticText(TRect(2, 4, 70, 5), "Dirty profile IDs:"));
			insert(new TStaticText(TRect(2, 5, 70, 7), ids.c_str()));
			insert(new TButton(TRect(14, 8, 26, 10), "~S~ave All", cmYes, bfDefault));
			insert(new TButton(TRect(29, 8, 41, 10), "~D~iscard", cmNo, bfNormal));
			insert(new TButton(TRect(44, 8, 56, 10), "~C~ancel", cmCancel, bfNormal));
		}
	};
	TDirtyProfilesDialog *dialog = new TDirtyProfilesDialog(joinCommaSeparated(dirtyIds));
	if (dialog == nullptr)
		return cmCancel;
	ushort result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	return result;
}

class TEditProfilesDialog : public MRScrollableDialog {
  public:
	TEditProfilesDialog(const std::vector<EditProfileDraft> &initialDrafts,
	                    const std::vector<EditProfileDraft> &workingDrafts)
	    : TWindowInit(&TDialog::initFrame),
	      MRScrollableDialog(centeredSetupDialogRect(kDialogWidth, kVisibleHeight), "FILENAME EXTENSIONS",
	                         kDialogWidth, kVirtualHeight),
	      initialDrafts_(initialDrafts), drafts_(workingDrafts),
	      panel_(makeProfilesPanelConfig(kDialogWidth - 1, 30, 49, kDialogWidth - 2, 6)) {
		buildViews();
		if (!drafts_.empty())
			currentIndex_ = 0;
		refreshProfileList();
		loadCurrentDraftToWidgets();
		refreshValidationState();
		initScrollIfNeeded();
		if (profileList_ != nullptr)
			profileList_->select();
		scrollToOrigin();
	}

	~TEditProfilesDialog() override {
		clearDialogStatus();
		mr::messageline::clearOwner(mr::messageline::Owner::DialogInteraction);
	}

	ushort run(std::vector<EditProfileDraft> &outDrafts, bool &changed) {
		ushort result = TProgram::deskTop->execView(this);
		saveWidgetsToCurrentDraft();
		outDrafts = drafts_;
		changed = !draftListsEqual(initialDrafts_, drafts_);
		clearDialogStatus();
		return result;
	}

	void handleEvent(TEvent &event) override {
		ushort originalWhat = event.what;
		ushort originalCommand = (event.what == evCommand || event.what == evBroadcast) ? event.message.command : 0;
		void *originalInfoPtr = event.what == evBroadcast ? event.message.infoPtr : nullptr;
		ushort originalKey = event.what == evKeyDown ? event.keyDown.keyCode : 0;

		MRScrollableDialog::handleEvent(event);
		if (originalWhat == evBroadcast && event.what == evBroadcast &&
		    event.message.command == cmMrSetupFilenameProfilesSelectionChanged &&
		    event.message.infoPtr == profileList_) {
			changeSelection(selectedListIndex(), false);
			clearEvent(event);
			return;
		}
		if (originalWhat == evCommand && originalCommand == cmOK) {
			saveWidgetsToCurrentDraft();
			refreshValidationState();
			if (!isValid_) {
				clearEvent(event);
				return;
			}
		}
		if (originalWhat == evCommand) {
			switch (originalCommand) {
				case cmMrSetupFilenameProfilesBrowseColorTheme:
					browseCurrentColorTheme();
					clearEvent(event);
					return;
				case cmMrEditSettingsPanelBrowsePostLoadMacro:
					browseCurrentPostLoadMacro();
					clearEvent(event);
					return;
				case cmMrEditSettingsPanelBrowsePreSaveMacro:
					browseCurrentPreSaveMacro();
					clearEvent(event);
					return;
				case cmMrEditSettingsPanelBrowseDefaultPath:
					browseCurrentDefaultPath();
					clearEvent(event);
					return;
				case cmMrSetupFilenameProfilesHelp:
					endModal(event.message.command);
					clearEvent(event);
					return;
				case cmMrSetupFilenameProfilesAdd:
					addProfile();
					clearEvent(event);
					return;
				case cmMrSetupFilenameProfilesCopy:
					copyCurrentProfile();
					clearEvent(event);
					return;
				case cmMrSetupFilenameProfilesDelete:
					deleteCurrentProfile();
					clearEvent(event);
					return;
				default:
					break;
			}
		}
		if (originalWhat == evCommand || originalWhat == evKeyDown || originalWhat == evMouseDown ||
		    originalWhat == evMouseUp || originalWhat == evMouseMove)
			refreshValidationState();
		if (originalWhat == evBroadcast &&
		    (originalCommand == cmReleasedFocus || originalCommand == cmReceivedFocus ||
		     originalCommand == cmMrSetupFilenameProfilesFieldChanged ||
		     originalCommand == cmMrSetupFilenameProfilesFieldFocusChanged ||
		     originalCommand == cmMrEditSettingsPanelChanged ||
		     originalCommand == cmMrEditSettingsPanelFocusChanged) &&
		    originalInfoPtr != profileList_)
			refreshValidationState();
		if (originalWhat == evKeyDown &&
		    (originalKey == kbTab || originalKey == kbCtrlI || originalKey == kbShiftTab))
			refreshValidationState();
	}

  private:
	static const int kDialogWidth = 107;
	static const int kVisibleHeight = 24;
	static const int kVirtualHeight = 40;

	TInactiveStaticText *addLabel(const TRect &rect, const char *text) {
		TInactiveStaticText *view = new TInactiveStaticText(rect, text);
		addManaged(view, rect);
		return view;
	}

	TReadOnlyAwareInputLine *addInput(const TRect &rect, int maxLen) {
		TReadOnlyAwareInputLine *view = new TReadOnlyAwareInputLine(rect, maxLen);
		addManaged(view, rect);
		return view;
	}

	void setInputReadOnly(TInputLine *input, bool readOnly, const std::string &warningText) {
		TReadOnlyAwareInputLine *view = dynamic_cast<TReadOnlyAwareInputLine *>(input);
		if (view == nullptr)
			return;
		view->setReadOnly(readOnly);
		view->setWarningText(warningText);
	}

	TButton *addButton(const TRect &rect, const char *title, ushort command, ushort flags) {
		TButton *view = new TButton(rect, title, command, flags);
		addManaged(view, rect);
		return view;
	}

	TInlineGlyphButton *addGlyphButton(const TRect &rect, ushort command) {
		TInlineGlyphButton *view = new TInlineGlyphButton(rect, "🔎", command);
		addManaged(view, rect);
		return view;
	}

	TScrollBar *addScrollBar(const TRect &rect) {
		TScrollBar *view = new TScrollBar(rect);
		addManaged(view, rect);
		return view;
	}

	TProfileListBox *addProfileListBox(const TRect &rect, TScrollBar *scrollBar) {
		TProfileListBox *view = new TProfileListBox(rect, scrollBar);
		addManaged(view, rect);
		return view;
	}

	void buildViews() {
		const int listLeft = 2;
		const int listWidth = 25;
		const int listBottom = 13;
		const int rightLeft = 31;
		const int fieldLeft = 49;
		const int glyphWidth = 2;
		const int colorGlyphRight = kDialogWidth - 2;
		const int colorGlyphLeft = colorGlyphRight - glyphWidth;
		const int fieldRight = colorGlyphLeft;
		const int buttonRow = 14;
		const int bottomTop = kVirtualHeight - 3;
		const int doneLeft = kDialogWidth / 2 - 17;
		const int cancelLeft = doneLeft + 12;
		const int helpLeft = cancelLeft + 14;

		addLabel(TRect(listLeft, 2, listLeft + 12, 3), "Profiles:");
		profileListScrollBar_ = addScrollBar(TRect(listLeft + listWidth, 3, listLeft + listWidth + 1, listBottom));
		profileList_ = addProfileListBox(TRect(listLeft, 3, listLeft + listWidth, listBottom), profileListScrollBar_);

		addButton(TRect(listLeft, buttonRow, listLeft + 8, buttonRow + 2), "Ne~w~", cmMrSetupFilenameProfilesAdd,
		          bfNormal);
		addButton(TRect(listLeft + 8, buttonRow, listLeft + 16, buttonRow + 2), "Cop~y~",
		          cmMrSetupFilenameProfilesCopy, bfNormal);
		deleteButton_ = addButton(TRect(listLeft + 16, buttonRow, listLeft + 26, buttonRow + 2), "De~l~ete ",
		                         cmMrSetupFilenameProfilesDelete, bfNormal);

		profileIdLabel_ = addLabel(TRect(rightLeft, 2, fieldLeft - 1, 3), "Profile ID:");
		profileIdField_ = addInput(TRect(fieldLeft, 2, fieldRight, 3), kProfileIdFieldSize - 1);

		profileNameLabel_ = addLabel(TRect(rightLeft, 3, fieldLeft - 1, 4), "Description:");
		profileNameField_ = addInput(TRect(fieldLeft, 3, fieldRight, 4), kProfileNameFieldSize - 1);

		profileExtensionsLabel_ = addLabel(TRect(rightLeft, 4, fieldLeft - 1, 5), "Extension:");
		profileExtensionsField_ = addInput(TRect(fieldLeft, 4, fieldRight, 5), kProfileExtensionsFieldSize - 1);

		profileColorThemeLabel_ = addLabel(TRect(rightLeft, 5, fieldLeft - 1, 6), "Colortheme:");
		profileColorThemeField_ = addInput(TRect(fieldLeft, 5, fieldRight, 6), kProfileColorThemeFieldSize - 1);
		profileColorThemeBrowseButton_ = addGlyphButton(TRect(colorGlyphLeft, 5, colorGlyphRight, 6),
		                                             cmMrSetupFilenameProfilesBrowseColorTheme);

		panel_.buildViews(*this);

		doneButton_ = addButton(TRect(doneLeft, bottomTop, doneLeft + 10, bottomTop + 2), "~D~one", cmOK,
		                        bfDefault);
		addButton(TRect(cancelLeft, bottomTop, cancelLeft + 12, bottomTop + 2), "C~a~ncel", cmCancel,
		          bfNormal);
		addButton(TRect(helpLeft, bottomTop, helpLeft + 8, bottomTop + 2), "~H~elp", cmMrSetupFilenameProfilesHelp,
		          bfNormal);
	}

	void setLabelInactive(TInactiveStaticText *label, bool inactive) {
		if (label != nullptr)
			label->setInactive(inactive ? True : False);
	}

	void applyFieldState(TInputLine *input, bool readOnly, const std::string &warningText) {
		if (input == nullptr)
			return;
		input->setState(sfVisible, True);
		input->setState(sfDisabled, False);
		setInputReadOnly(input, readOnly, warningText);
	}

	void loadCurrentDraftFieldValues(const EditProfileDraft &draft) {
		writeInputLineString(profileIdField_, draft.id, kProfileIdFieldSize);
		writeInputLineString(profileNameField_, draft.name, kProfileNameFieldSize);
		writeInputLineString(profileExtensionsField_, draft.extensionsLiteral, kProfileExtensionsFieldSize);
		writeInputLineString(profileColorThemeField_, draft.colorThemeUri, kProfileColorThemeFieldSize);
		panel_.loadFieldsFromRecord(draft.settingsRecord);
	}

	void applyCurrentDraftWidgetState(bool isDefault) {
		applyFieldState(profileIdField_, isDefault, "read-only with DEFAULT profile");
		applyFieldState(profileNameField_, false, "");
		applyFieldState(profileExtensionsField_, isDefault, "read-only with DEFAULT profile");
		applyFieldState(profileColorThemeField_, false, "");
		if (profileColorThemeBrowseButton_ != nullptr) {
			profileColorThemeBrowseButton_->setState(sfVisible, True);
			profileColorThemeBrowseButton_->setState(sfDisabled, False);
		}
		setLabelInactive(profileIdLabel_, isDefault);
		setLabelInactive(profileNameLabel_, false);
		setLabelInactive(profileExtensionsLabel_, isDefault);
		setLabelInactive(profileColorThemeLabel_, false);
		if (deleteButton_ != nullptr)
			deleteButton_->setState(sfDisabled, isDefault ? True : False);
	}

	void setValidationState(bool valid, const std::string &errorText) {
		isValid_ = valid;
		if (doneButton_ != nullptr)
			doneButton_->setState(sfDisabled, valid ? False : True);
		if (valid) {
			lastValidationText_.clear();
			clearDialogStatus();
		} else if (errorText != lastValidationText_) {
			setDialogStatus(errorText, TMRMenuBar::MarqueeKind::Warning);
			lastValidationText_ = errorText;
		}
	}

	void saveWidgetsToCurrentDraft() {
		if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(drafts_.size()))
			return;
		EditProfileDraft &draft = drafts_[currentIndex_];

		panel_.saveFieldsToRecord(draft.settingsRecord);
		if (draft.isDefault) {
			draft.id = kDefaultProfileId;
			draft.name = readInputLineString(profileNameField_, kProfileNameFieldSize);
			draft.extensionsLiteral.clear();
			draft.colorThemeUri = readInputLineString(profileColorThemeField_, kProfileColorThemeFieldSize);
			return;
		}
		draft.id = readInputLineString(profileIdField_, kProfileIdFieldSize);
		draft.name = readInputLineString(profileNameField_, kProfileNameFieldSize);
		draft.extensionsLiteral = readInputLineString(profileExtensionsField_, kProfileExtensionsFieldSize);
		draft.colorThemeUri = readInputLineString(profileColorThemeField_, kProfileColorThemeFieldSize);
	}

	void loadCurrentDraftToWidgets() {
		if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(drafts_.size()))
			return;
		const EditProfileDraft &draft = drafts_[currentIndex_];

		loadCurrentDraftFieldValues(draft);
		applyCurrentDraftWidgetState(draft.isDefault);
		panel_.syncDynamicStates();
	}

	void refreshProfileList() {
		TPlainStringCollection *items = new TPlainStringCollection(std::max<short>(1, drafts_.size()), 5);
		TListBoxRec data;
		int selection = currentIndex_;
		std::size_t idWidth = std::strlen(kDefaultProfileId);

		if (items == nullptr || profileList_ == nullptr)
			return;
		for (const EditProfileDraft &draft : drafts_)
			idWidth = std::max(idWidth, trimAscii(draft.isDefault ? std::string(kDefaultProfileId) : draft.id).size());
		idWidth = std::min<std::size_t>(18, std::max<std::size_t>(7, idWidth));
		for (const EditProfileDraft &draft : drafts_)
			items->insert(dupCString(buildProfileListLabel(draft, idWidth)));
		if (selection < 0 && !drafts_.empty())
			selection = 0;
		if (selection >= static_cast<int>(drafts_.size()))
			selection = drafts_.empty() ? 0 : static_cast<int>(drafts_.size()) - 1;
		data.items = items;
		data.selection = static_cast<ushort>(std::max(0, selection));
		profileList_->setData(&data);
	}

	int selectedListIndex() const {
		TListBoxRec data;

		if (profileList_ == nullptr || drafts_.empty())
			return -1;
		profileList_->getData((void *)&data);
		if (data.selection >= drafts_.size())
			return static_cast<int>(drafts_.size()) - 1;
		return static_cast<int>(data.selection);
	}

	void setCurrentIndex(int index) {
		currentIndex_ = index;
		refreshProfileList();
	}

	void changeSelection(int index, bool moveFocusToFields) {
		if (index == currentIndex_ || index < 0 || index >= static_cast<int>(drafts_.size()))
			return;
		saveWidgetsToCurrentDraft();
		currentIndex_ = index;
		loadCurrentDraftToWidgets();
		scrollToOrigin();
		if (moveFocusToFields)
			selectTopField();
		else if (profileList_ != nullptr)
			profileList_->select();
		refreshValidationState();
	}

	void selectTopField() {
		scrollToOrigin();
		if (currentIndex_ >= 0 && currentIndex_ < static_cast<int>(drafts_.size()) && drafts_[currentIndex_].isDefault) {
			if (profileNameField_ != nullptr)
				profileNameField_->select();
		} else if (profileIdField_ != nullptr)
			profileIdField_->select();
		else
			selectContent();
	}

	void addProfile() {
		saveWidgetsToCurrentDraft();
		drafts_.push_back(makeNewDraft(drafts_));
		currentIndex_ = static_cast<int>(drafts_.size()) - 1;
		setCurrentIndex(currentIndex_);
		loadCurrentDraftToWidgets();
		selectTopField();
		refreshValidationState();
	}

	void copyCurrentProfile() {
		if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(drafts_.size()))
			return;
		saveWidgetsToCurrentDraft();
		drafts_.push_back(makeCopiedDraft(drafts_[currentIndex_], drafts_));
		currentIndex_ = static_cast<int>(drafts_.size()) - 1;
		setCurrentIndex(currentIndex_);
		loadCurrentDraftToWidgets();
		selectTopField();
		refreshValidationState();
	}

	void deleteCurrentProfile() {
		if (currentIndex_ <= 0 || currentIndex_ >= static_cast<int>(drafts_.size()))
			return;
		{
			const EditProfileDraft &draft = drafts_[currentIndex_];
			std::string caption = trimAscii(draft.name).empty() ? trimAscii(draft.id)
			                                              : trimAscii(draft.id) + " / " + trimAscii(draft.name);
			if (messageBox(mfConfirmation | mfYesButton | mfNoButton, "Delete profile:\n%s",
			               caption.c_str()) != cmYes)
				return;
		}
		drafts_.erase(drafts_.begin() + currentIndex_);
		if (currentIndex_ >= static_cast<int>(drafts_.size()))
			currentIndex_ = static_cast<int>(drafts_.size()) - 1;
		setCurrentIndex(currentIndex_);
		loadCurrentDraftToWidgets();
		selectTopField();
		refreshValidationState();
	}

	void browseCurrentColorTheme() {
		if (currentIndex_ < 0 || currentIndex_ >= static_cast<int>(drafts_.size()))
			return;
		std::string selectedUri;
		std::string currentValue = readInputLineString(profileColorThemeField_, kProfileColorThemeFieldSize);
		if (!browseColorThemeUri(currentValue, selectedUri))
			return;
		writeInputLineString(profileColorThemeField_, selectedUri, kProfileColorThemeFieldSize);
		saveWidgetsToCurrentDraft();
		refreshValidationState();
	}

	void browseCurrentPostLoadMacro() {
		std::string selectedUri;
		if (!browseMrmacFileUri("Select post-load macro", panel_.postLoadMacroValue(), selectedUri))
			return;
		panel_.setPostLoadMacroValue(selectedUri);
		saveWidgetsToCurrentDraft();
		refreshValidationState();
	}

	void browseCurrentPreSaveMacro() {
		std::string selectedUri;
		if (!browseMrmacFileUri("Select pre-save macro", panel_.preSaveMacroValue(), selectedUri))
			return;
		panel_.setPreSaveMacroValue(selectedUri);
		saveWidgetsToCurrentDraft();
		refreshValidationState();
	}

	void browseCurrentDefaultPath() {
		std::string selectedPath;
		if (!browseDirectoryPath(panel_.defaultPathValue(), selectedPath))
			return;
		panel_.setDefaultPathValue(selectedPath);
		saveWidgetsToCurrentDraft();
		refreshValidationState();
	}

	void refreshValidationState() {
		std::string errorText;

		saveWidgetsToCurrentDraft();
		panel_.syncDynamicStates();
		refreshProfileList();
		setValidationState(validateDraftsForUi(drafts_, currentIndex_, errorText), errorText);
	}

	std::vector<EditProfileDraft> initialDrafts_;
	std::vector<EditProfileDraft> drafts_;
	int currentIndex_ = -1;
	TProfileListBox *profileList_ = nullptr;
	TScrollBar *profileListScrollBar_ = nullptr;
	TInactiveStaticText *profileIdLabel_ = nullptr;
	TInactiveStaticText *profileNameLabel_ = nullptr;
	TInactiveStaticText *profileExtensionsLabel_ = nullptr;
	TInactiveStaticText *profileColorThemeLabel_ = nullptr;
	TInputLine *profileIdField_ = nullptr;
	TInputLine *profileNameField_ = nullptr;
	TInputLine *profileExtensionsField_ = nullptr;
	TInputLine *profileColorThemeField_ = nullptr;
	TInlineGlyphButton *profileColorThemeBrowseButton_ = nullptr;
	TButton *deleteButton_ = nullptr;
	TButton *doneButton_ = nullptr;
	bool isValid_ = true;
	std::string lastValidationText_;
	EditSettingsPanel panel_;
};

void showEditProfilesHelpDialog() {
	std::vector<std::string> lines;
	lines.push_back("FILENAME EXTENSIONS HELP");
	lines.push_back("");
	lines.push_back("DEFAULT contains the global edit settings and cannot be deleted.");
	lines.push_back("Each additional profile has its own ID, description and exact extension list.");
	lines.push_back("Extension matching is exact and case-sensitive.");
	lines.push_back("Right margin, word wrap, macros and default path are profile-specific.");
	lines.push_back("Done writes settings.mrmac and reloads the configuration.");
	TDialog *dialog = createSetupSimplePreviewDialog("FILENAME EXTENSIONS HELP", 78, 14, lines, false);
	if (dialog != nullptr) {
		TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
	}
}

} // namespace MREditProfilesDialogInternal

void runEditExtensionProfilesDialogFlow() {
	std::vector<EditProfileDraft> baselineDrafts;
	std::vector<EditProfileDraft> workingDrafts;
	bool running = true;

	workingDrafts.push_back(makeDefaultDraft());
	for (const MREditExtensionProfile &profile : configuredEditExtensionProfiles())
		workingDrafts.push_back(draftFromProfile(profile));
	baselineDrafts = workingDrafts;

	while (running) {
		ushort result = cmCancel;
		bool changed = false;
		std::vector<EditProfileDraft> editedDrafts = workingDrafts;
		std::string errorText;
		TEditProfilesDialog *dialog = new TEditProfilesDialog(baselineDrafts, workingDrafts);

		if (dialog == nullptr)
			return;
		result = dialog->run(editedDrafts, changed);
		TObject::destroy(dialog);

		switch (result) {
			case cmMrSetupFilenameProfilesHelp:
				showEditProfilesHelpDialog();
				break;

			case cmOK:
				workingDrafts = editedDrafts;
				if (!saveAndReloadEditProfiles(workingDrafts, errorText)) {
					postValidationWarning(errorText);
					break;
				}
				baselineDrafts = workingDrafts;
				running = false;
				break;

			case cmCancel:
				if (changed) {
					workingDrafts = editedDrafts;
					std::vector<std::string> dirtyIds = dirtyDraftIds(baselineDrafts, editedDrafts);
					ushort discardResult = runDirtyProfilesDialog(dirtyIds);
					if (discardResult == cmYes) {
						workingDrafts = editedDrafts;
						if (!saveAndReloadEditProfiles(workingDrafts, errorText)) {
							mr::messageline::postAutoTimed(mr::messageline::Owner::DialogValidation, errorText,
							                              mr::messageline::Kind::Warning,
							                              mr::messageline::kPriorityHigh);
							break;
						}
						running = false;
						break;
					}
					if (discardResult == cmCancel) {
						workingDrafts = editedDrafts;
						break;
					}
				}
				running = false;
				break;

			default:
				running = false;
				break;
		}
	}
}
