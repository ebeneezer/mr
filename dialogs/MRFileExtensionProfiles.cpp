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

#include "MRFileExtensionEditorSettingsInternal.hpp"
#include "MRDirtyGating.hpp"
#include "MRFileExtensionProfilesSupport.hpp"
#include "MRSetupCommon.hpp"
#include "../app/utils/MRStringUtils.hpp"

#include "../app/MREditorApp.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../app/commands/MRWindowCommands.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
using namespace MRFileExtensionProfilesInternal;

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
		if (focused != oldFocused) dispatchSelectionChanged();
	}

  private:
	void dispatchSelectionChanged() {
		TView *target = owner;

		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;
		message(target != nullptr ? target : owner, evBroadcast, cmMrSetupFilenameProfilesSelectionChanged, this);
	}
};

TAttrPair configuredColorOr(TView *view, unsigned char paletteSlot, ushort fallbackColorIndex) {
	unsigned char biosAttr = 0;

	if (configuredColorSlotOverride(paletteSlot, biosAttr)) return TAttrPair(biosAttr);
	return view != nullptr ? view->getColor(fallbackColorIndex) : TAttrPair(0x70);
}

class TInactiveStaticText : public TStaticText {
  public:
	TInactiveStaticText(const TRect &bounds, const char *text) noexcept : TStaticText(bounds, text) {
	}

	void setInactive(bool inactive) {
		if (mInactive != inactive) {
			mInactive = inactive;
			drawView();
		}
	}

	void draw() override {
		TDrawBuffer buffer;
		char text[256];
		TAttrPair color = mInactive ? configuredColorOr(this, kMrPaletteDialogInactiveElements, 1) : getColor(1);

		buffer.moveChar(0, ' ', color, size.x);
		getText(text);
		buffer.moveStr(0, text, color, size.x);
		writeLine(0, 0, size.x, size.y, buffer);
	}

  private:
	bool mInactive = false;
};

class TInlineGlyphButton : public TView {
  public:
	TInlineGlyphButton(const TRect &bounds, const char *glyph, ushort command) : TView(bounds), mGlyph(glyph != nullptr ? glyph : ""), mCommand(command) {
		options |= ofSelectable;
		options |= ofFirstClick;
		eventMask |= evMouseDown | evKeyDown;
	}

	void draw() override {
		TDrawBuffer buffer;
		ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
		int x = 0;

		buffer.moveChar(0, ' ', color, size.x);
		if (size.x > 1) x = (size.x - 1) / 2;
		buffer.moveStr(static_cast<ushort>(x), mGlyph.c_str(), color, size.x - x);
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
		message(target != nullptr ? target : owner, evCommand, mCommand, this);
	}

	std::string mGlyph;
	ushort mCommand;
};

class TNotifyingInputLine : public TInputLine {
  public:
	TNotifyingInputLine(const TRect &bounds, int maxLen) noexcept : TInputLine(bounds, maxLen), mCapacity(maxLen + 1) {
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

		if (currentText() != beforeText) message(target != nullptr ? target : owner, evBroadcast, cmMrSetupFilenameProfilesFieldChanged, this);

		if (((state & sfFocused) != 0) != beforeFocused || (originalWhat == evBroadcast && (originalCommand == cmReleasedFocus || originalCommand == cmReceivedFocus))) message(target != nullptr ? target : owner, evBroadcast, cmMrSetupFilenameProfilesFieldFocusChanged, this);
	}

  private:
	std::string currentText() const {
		std::vector<char> buffer(mCapacity, '\0');
		const_cast<TNotifyingInputLine *>(this)->getData(buffer.data());
		return std::string(buffer.data());
	}

	std::size_t mCapacity = 0;
};

class TReadOnlyAwareInputLine : public TNotifyingInputLine {
  public:
	TReadOnlyAwareInputLine(const TRect &bounds, int maxLen) noexcept : TNotifyingInputLine(bounds, maxLen) {
	}

	void setReadOnly(bool readOnly) noexcept {
		mReadOnly = readOnly;
	}

	void setWarningText(const std::string &warningText) {
		mWarningText = warningText;
	}

	void handleEvent(TEvent &event) override {
		if (!mReadOnly) {
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
		return keyCode == kbTab || keyCode == kbShiftTab || keyCode == kbCtrlI || keyCode == kbLeft || keyCode == kbRight || keyCode == kbUp || keyCode == kbDown || keyCode == kbHome || keyCode == kbEnd || keyCode == kbEsc || keyCode == kbEnter;
	}

	bool isMutatingKey(const TEvent &event) const noexcept {
		ushort keyCode = event.keyDown.keyCode;
		unsigned char ch = static_cast<unsigned char>(event.keyDown.charScan.charCode);
		if (isNavigationKey(keyCode)) return false;
		if (keyCode == kbBack || keyCode == kbDel) return true;
		return std::isprint(ch) != 0;
	}

	void postWarning() const {
		if (mWarningText.empty()) return;
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, mWarningText, mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
	}

	bool mReadOnly = false;
	std::string mWarningText;
};

std::string readInputLineString(TInputLine *inputLine, std::size_t capacity) {
	std::vector<char> buffer(capacity, '\0');

	if (inputLine == nullptr) return std::string();
	inputLine->getData(buffer.data());
	return readRecordField(buffer.data());
}

void writeInputLineString(TInputLine *inputLine, const std::string &value, std::size_t capacity) {
	std::vector<char> buffer(capacity, '\0');

	if (inputLine == nullptr) return;
	writeRecordField(buffer.data(), buffer.size(), value);
	inputLine->setData(buffer.data());
}

std::string readCurrentWorkingDirectory() {
	char cwd[PATH_MAX];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr) return std::string();
	return std::string(cwd);
}

bool browseMrmacFileUri(MRDialogHistoryScope scope, const char *title, std::string &selectedUri) {
	char fileName[MAXPATH];
	ushort result;

	mr::dialogs::seedFileDialogPath(scope, fileName, sizeof(fileName), "*.mrmac");
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.mrmac", title, "~N~ame", fdOKButton, fileName);
	if (result == cmCancel) return false;
	selectedUri = normalizeConfiguredPathInput(fileName);
	return !selectedUri.empty();
}

bool browseDirectoryPath(MRDialogHistoryScope scope, std::string &selectedPath) {
	std::string originalCwd = readCurrentWorkingDirectory();
	std::string seed = configuredLastFileDialogPath(scope);
	std::string picked;
	ushort result;

	if (!seed.empty()) (void)::chdir(seed.c_str());
	result = mr::dialogs::execDialog(mr::dialogs::createDirectoryDialog(scope, cdNormal));
	picked = readCurrentWorkingDirectory();
	if (!originalCwd.empty()) (void)::chdir(originalCwd.c_str());
	if (result == cmCancel) return false;
	selectedPath = normalizeConfiguredPathInput(picked);
	if (!selectedPath.empty()) rememberLoadDialogPath(scope, selectedPath.c_str());
	return !selectedPath.empty();
}

bool browseColorThemeUri(MRDialogHistoryScope scope, std::string &selectedUri) {
	char fileName[MAXPATH];
	ushort result = cmCancel;

	mr::dialogs::seedFileDialogPath(scope, fileName, sizeof(fileName), "*.mrmac");
	if (configuredLastFileDialogPath(scope).empty() && configuredLastFileDialogFilePath(scope).empty()) {
		std::string macroPath = normalizeConfiguredPathInput(configuredMacroDirectoryPath());
		if (!macroPath.empty()) {
			if (macroPath.back() != '/') macroPath += '/';
			macroPath += "*.mrmac";
			writeRecordField(fileName, sizeof(fileName), macroPath);
		}
	}
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.mrmac", "Color theme file", "~N~ame", fdOKButton, fileName);
	if (result == cmCancel) return false;
	selectedUri = normalizeConfiguredPathInput(fileName);
	return true;
}

FileExtensionEditorSettingsPanelConfig makeEditorSettingsPanelConfig(int dialogWidth, int labelLeft, int inputLeft, int inputRight, int topY) {
	FileExtensionEditorSettingsPanelConfig panelConfig;
	panelConfig.topY = topY;
	panelConfig.dialogWidth = dialogWidth;
	panelConfig.labelLeft = labelLeft;
	panelConfig.inputLeft = inputLeft;
	panelConfig.inputRight = inputRight;
	panelConfig.clusterLeft = 2;
	panelConfig.clusterTopY = -1;
	panelConfig.tabSizeY = topY + 3;
	panelConfig.includeDefaultExtensions = true;
	panelConfig.compactTextRows = true;
	panelConfig.tabExpandBesideDefaultMode = true;
	return panelConfig;
}

void clearDialogStatus() {
	mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
}

void postDialogError(const std::string &text) {
	if (text.empty()) return;
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

class TEditProfilesDialog : public MRScrollableDialog {
  public:
	TEditProfilesDialog(const std::vector<EditProfileDraft> &workingDrafts) : TWindowInit(&TDialog::initFrame), MRScrollableDialog(centeredSetupDialogRect(kDialogWidth, kVisibleHeight), "FILENAME EXTENSIONS", kDialogWidth, kVirtualHeight), draftList(workingDrafts), editorSettingsPanel(makeEditorSettingsPanelConfig(kDialogWidth - 1, 37, 56, kDialogWidth - 2, 6)) {
		buildViews();
		setDialogValidationHook([this]() { return validateDialogValues(); });
		if (!draftList.empty()) mCurrentIndex = 0;
		refreshProfileList();
		loadCurrentDraftToWidgets();
		refreshValidationState();
		initScrollIfNeeded();
		if (mProfileList != nullptr) mProfileList->select();
		scrollToOrigin();
	}

	~TEditProfilesDialog() override {
		clearDialogStatus();
		mr::messageline::clearOwner(mr::messageline::Owner::DialogInteraction);
	}

	ushort run(std::vector<EditProfileDraft> &outDrafts) {
		ushort result = TProgram::deskTop->execView(this);
		saveWidgetsToCurrentDraft();
		outDrafts = draftList;
		clearDialogStatus();
		return result;
	}

	void handleEvent(TEvent &event) override {
		ushort originalWhat = event.what;
		ushort originalCommand = (event.what == evCommand || event.what == evBroadcast) ? event.message.command : 0;
		void *originalInfoPtr = event.what == evBroadcast ? event.message.infoPtr : nullptr;
		ushort originalKey = event.what == evKeyDown ? event.keyDown.keyCode : 0;

		if (event.what == evKeyDown && event.keyDown.keyCode == kbEsc && editorSettingsPanel.codeLanguageListVisible()) {
			editorSettingsPanel.hideCodeLanguageList();
			refreshValidationState();
			clearEvent(event);
			return;
		}
		if (event.what == evMouseDown && editorSettingsPanel.codeLanguageListVisible() && !editorSettingsPanel.codeLanguageListContainsPoint(event.mouse.where)) {
			editorSettingsPanel.hideCodeLanguageList();
			refreshValidationState();
			clearEvent(event);
			return;
		}
		MRScrollableDialog::handleEvent(event);
		if (originalWhat == evBroadcast && event.what == evBroadcast && event.message.command == cmMrSetupFilenameProfilesSelectionChanged && event.message.infoPtr == mProfileList) {
			changeSelection(selectedListIndex(), false);
			clearEvent(event);
			return;
		}
		if (originalWhat == evCommand && originalCommand == cmOK) {
			saveWidgetsToCurrentDraft();
			refreshValidationState();
			if (!mIsValid) {
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
				case cmMrFileExtensionEditorSettingsPanelBrowsePostLoadMacro:
					browseCurrentPostLoadMacro();
					clearEvent(event);
					return;
				case cmMrFileExtensionEditorSettingsPanelBrowsePreSaveMacro:
					browseCurrentPreSaveMacro();
					clearEvent(event);
					return;
				case cmMrFileExtensionEditorSettingsPanelBrowseDefaultPath:
					browseCurrentDefaultPath();
					clearEvent(event);
					return;
				case cmMrFileExtensionEditorSettingsPanelChooseCodeLanguage:
					editorSettingsPanel.toggleCodeLanguageList(*this);
					clearEvent(event);
					return;
				case cmMrFileExtensionEditorSettingsPanelAcceptCodeLanguage:
					if (editorSettingsPanel.acceptCodeLanguageListSelection()) saveWidgetsToCurrentDraft();
					refreshValidationState();
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
		if (originalWhat == evCommand || originalWhat == evKeyDown || originalWhat == evMouseDown || originalWhat == evMouseUp || originalWhat == evMouseMove) refreshValidationState();
		if (originalWhat == evBroadcast && (originalCommand == cmReleasedFocus || originalCommand == cmReceivedFocus || originalCommand == cmMrSetupFilenameProfilesFieldChanged || originalCommand == cmMrSetupFilenameProfilesFieldFocusChanged || originalCommand == cmMrFileExtensionEditorSettingsPanelChanged || originalCommand == cmMrFileExtensionEditorSettingsPanelFocusChanged) && originalInfoPtr != mProfileList) refreshValidationState();
		if (originalWhat == evKeyDown && (originalKey == kbTab || originalKey == kbCtrlI || originalKey == kbShiftTab)) refreshValidationState();
	}

  private:
	static const int kDialogWidth = 114;
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
		if (view == nullptr) return;
		view->setReadOnly(readOnly);
		view->setWarningText(warningText);
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
		const std::array listButtons{mr::dialogs::DialogButtonSpec{"Ne~w~", cmMrSetupFilenameProfilesAdd, bfNormal}, mr::dialogs::DialogButtonSpec{"Cop~y~", cmMrSetupFilenameProfilesCopy, bfNormal}, mr::dialogs::DialogButtonSpec{"De~l~ete", cmMrSetupFilenameProfilesDelete, bfNormal}};
		const std::array bottomButtons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmMrSetupFilenameProfilesHelp, bfNormal}};
		std::vector<TButton *> listButtonViews;
		const mr::dialogs::DialogButtonRowMetrics bottomMetrics = mr::dialogs::measureUniformButtonRow(bottomButtons, 2);
		const int listLeft = 2;
		const int listWidth = 29;
		const int listBottom = 13;
		const int rightLeft = 38;
		const int fieldLeft = 56;
		const int glyphWidth = 2;
		const int colorGlyphRight = kDialogWidth - 2;
		const int colorGlyphLeft = colorGlyphRight - glyphWidth;
		const int fieldRight = colorGlyphLeft;
		const int buttonRow = 14;
		const int bottomTop = kVirtualHeight - 3;
		const int bottomButtonLeft = (kDialogWidth - bottomMetrics.rowWidth) / 2;

		addLabel(TRect(listLeft, 2, listLeft + 12, 3), "Profiles:");
		mProfileListScrollBar = addScrollBar(TRect(listLeft + listWidth, 3, listLeft + listWidth + 1, listBottom));
		mProfileList = addProfileListBox(TRect(listLeft, 3, listLeft + listWidth, listBottom), mProfileListScrollBar);

		mr::dialogs::addManagedUniformButtonRow(*this, listLeft, buttonRow, 0, listButtons, 0, &listButtonViews);
		if (listButtonViews.size() >= 3) mDeleteButton = listButtonViews[2];

		mProfileIdLabel = addLabel(TRect(rightLeft, 2, fieldLeft - 1, 3), "Profile ID:");
		mProfileIdField = addInput(TRect(fieldLeft, 2, fieldRight, 3), kProfileIdFieldSize - 1);

		mProfileNameLabel = addLabel(TRect(rightLeft, 3, fieldLeft - 1, 4), "Description:");
		mProfileNameField = addInput(TRect(fieldLeft, 3, fieldRight, 4), kProfileNameFieldSize - 1);

		mProfileExtensionsLabel = addLabel(TRect(rightLeft, 4, fieldLeft - 1, 5), "Extension:");
		mProfileExtensionsField = addInput(TRect(fieldLeft, 4, fieldRight, 5), kProfileExtensionsFieldSize - 1);

		mProfileColorThemeLabel = addLabel(TRect(rightLeft, 5, fieldLeft - 1, 6), "Colortheme:");
		mProfileColorThemeField = addInput(TRect(fieldLeft, 5, fieldRight, 6), kProfileColorThemeFieldSize - 1);
		mProfileColorThemeBrowseButton = addGlyphButton(TRect(colorGlyphLeft, 5, colorGlyphRight, 6), cmMrSetupFilenameProfilesBrowseColorTheme);

		editorSettingsPanel.buildViews(*this);

		mr::dialogs::addManagedUniformButtonRow(*this, bottomButtonLeft, bottomTop, 2, bottomButtons);
	}

	void setLabelInactive(TInactiveStaticText *label, bool inactive) {
		if (label != nullptr) label->setInactive(inactive ? True : False);
	}

	void applyFieldState(TInputLine *input, bool readOnly, const std::string &warningText) {
		if (input == nullptr) return;
		input->setState(sfVisible, True);
		input->setState(sfDisabled, False);
		setInputReadOnly(input, readOnly, warningText);
	}

	void loadCurrentDraftFieldValues(const EditProfileDraft &draft) {
		writeInputLineString(mProfileIdField, draft.id, kProfileIdFieldSize);
		writeInputLineString(mProfileNameField, draft.name, kProfileNameFieldSize);
		writeInputLineString(mProfileExtensionsField, draft.extensionsLiteral, kProfileExtensionsFieldSize);
		writeInputLineString(mProfileColorThemeField, draft.colorThemeUri, kProfileColorThemeFieldSize);
		editorSettingsPanel.loadFieldsFromRecord(draft.settingsRecord);
	}

	void applyCurrentDraftWidgetState(bool isDefault) {
		applyFieldState(mProfileIdField, isDefault, "read-only with DEFAULT profile");
		applyFieldState(mProfileNameField, false, "");
		applyFieldState(mProfileExtensionsField, isDefault, "read-only with DEFAULT profile");
		applyFieldState(mProfileColorThemeField, false, "");
		if (mProfileColorThemeBrowseButton != nullptr) {
			mProfileColorThemeBrowseButton->setState(sfVisible, True);
			mProfileColorThemeBrowseButton->setState(sfDisabled, False);
		}
		setLabelInactive(mProfileIdLabel, isDefault);
		setLabelInactive(mProfileNameLabel, false);
		setLabelInactive(mProfileExtensionsLabel, isDefault);
		setLabelInactive(mProfileColorThemeLabel, false);
		if (mDeleteButton != nullptr) mDeleteButton->setState(sfDisabled, isDefault ? True : False);
	}

	void setValidationState(bool valid, const std::string &errorText) {
		(void)errorText;
		mIsValid = valid;
	}

	void saveWidgetsToCurrentDraft() {
		if (mCurrentIndex < 0 || mCurrentIndex >= static_cast<int>(draftList.size())) return;
		EditProfileDraft &draft = draftList[mCurrentIndex];

		editorSettingsPanel.saveFieldsToRecord(draft.settingsRecord);
		if (draft.isDefault) {
			draft.id = kDefaultProfileId;
			draft.name = readInputLineString(mProfileNameField, kProfileNameFieldSize);
			draft.extensionsLiteral.clear();
			draft.colorThemeUri = readInputLineString(mProfileColorThemeField, kProfileColorThemeFieldSize);
			return;
		}
		draft.id = readInputLineString(mProfileIdField, kProfileIdFieldSize);
		draft.name = readInputLineString(mProfileNameField, kProfileNameFieldSize);
		draft.extensionsLiteral = readInputLineString(mProfileExtensionsField, kProfileExtensionsFieldSize);
		draft.colorThemeUri = readInputLineString(mProfileColorThemeField, kProfileColorThemeFieldSize);
	}

	EditProfileDraft collectCurrentDraftFromWidgets() const {
		EditProfileDraft draft;
		if (mCurrentIndex < 0 || mCurrentIndex >= static_cast<int>(draftList.size())) return draft;
		draft = draftList[mCurrentIndex];
		editorSettingsPanel.saveFieldsToRecord(draft.settingsRecord);
		if (draft.isDefault) {
			draft.id = kDefaultProfileId;
			draft.name = readInputLineString(mProfileNameField, kProfileNameFieldSize);
			draft.extensionsLiteral.clear();
			draft.colorThemeUri = readInputLineString(mProfileColorThemeField, kProfileColorThemeFieldSize);
		} else {
			draft.id = readInputLineString(mProfileIdField, kProfileIdFieldSize);
			draft.name = readInputLineString(mProfileNameField, kProfileNameFieldSize);
			draft.extensionsLiteral = readInputLineString(mProfileExtensionsField, kProfileExtensionsFieldSize);
			draft.colorThemeUri = readInputLineString(mProfileColorThemeField, kProfileColorThemeFieldSize);
		}
		return draft;
	}

	void loadCurrentDraftToWidgets() {
		if (mCurrentIndex < 0 || mCurrentIndex >= static_cast<int>(draftList.size())) return;
		const EditProfileDraft &draft = draftList[mCurrentIndex];

		loadCurrentDraftFieldValues(draft);
		applyCurrentDraftWidgetState(draft.isDefault);
		editorSettingsPanel.syncDynamicStates();
	}

	void refreshProfileList() {
		TPlainStringCollection *items = new TPlainStringCollection(std::max<short>(1, draftList.size()), 5);
		TListBoxRec data;
		int selection = mCurrentIndex;
		std::size_t idWidth = std::strlen(kDefaultProfileId);

		if (items == nullptr || mProfileList == nullptr) return;
		for (const EditProfileDraft &draft : draftList)
			idWidth = std::max(idWidth, trimAscii(draft.isDefault ? std::string(kDefaultProfileId) : draft.id).size());
		idWidth = std::min<std::size_t>(18, std::max<std::size_t>(7, idWidth));
		for (const EditProfileDraft &draft : draftList)
			items->insert(dupCString(buildProfileListLabel(draft, idWidth)));
		if (selection < 0 && !draftList.empty()) selection = 0;
		if (selection >= static_cast<int>(draftList.size())) selection = draftList.empty() ? 0 : static_cast<int>(draftList.size()) - 1;
		data.items = items;
		data.selection = static_cast<ushort>(std::max(0, selection));
		mProfileList->setData(&data);
	}

	int selectedListIndex() const {
		TListBoxRec data;

		if (mProfileList == nullptr || draftList.empty()) return -1;
		mProfileList->getData((void *)&data);
		if (data.selection >= draftList.size()) return static_cast<int>(draftList.size()) - 1;
		return static_cast<int>(data.selection);
	}

	void setCurrentIndex(int index) {
		mCurrentIndex = index;
		refreshProfileList();
	}

	void changeSelection(int index, bool moveFocusToFields) {
		if (index == mCurrentIndex || index < 0 || index >= static_cast<int>(draftList.size())) return;
		saveWidgetsToCurrentDraft();
		mCurrentIndex = index;
		loadCurrentDraftToWidgets();
		scrollToOrigin();
		if (moveFocusToFields) selectTopField();
		else if (mProfileList != nullptr)
			mProfileList->select();
		refreshValidationState();
	}

	void selectTopField() {
		scrollToOrigin();
		if (mCurrentIndex >= 0 && mCurrentIndex < static_cast<int>(draftList.size()) && draftList[mCurrentIndex].isDefault) {
			if (mProfileNameField != nullptr) mProfileNameField->select();
		} else if (mProfileIdField != nullptr)
			mProfileIdField->select();
		else
			selectContent();
	}

	void addProfile() {
		saveWidgetsToCurrentDraft();
		draftList.push_back(makeNewDraft(draftList));
		mCurrentIndex = static_cast<int>(draftList.size()) - 1;
		setCurrentIndex(mCurrentIndex);
		loadCurrentDraftToWidgets();
		selectTopField();
		refreshValidationState();
	}

	void copyCurrentProfile() {
		if (mCurrentIndex < 0 || mCurrentIndex >= static_cast<int>(draftList.size())) return;
		saveWidgetsToCurrentDraft();
		draftList.push_back(makeCopiedDraft(draftList[mCurrentIndex], draftList));
		mCurrentIndex = static_cast<int>(draftList.size()) - 1;
		setCurrentIndex(mCurrentIndex);
		loadCurrentDraftToWidgets();
		selectTopField();
		refreshValidationState();
	}

	void deleteCurrentProfile() {
		if (mCurrentIndex <= 0 || mCurrentIndex >= static_cast<int>(draftList.size())) return;
		{
			const EditProfileDraft &draft = draftList[mCurrentIndex];
			std::string caption = trimAscii(draft.name).empty() ? trimAscii(draft.id) : trimAscii(draft.id) + " / " + trimAscii(draft.name);
			if (messageBox(mfConfirmation | mfYesButton | mfNoButton, "Delete profile:\n%s", caption.c_str()) != cmYes) return;
		}
		draftList.erase(draftList.begin() + mCurrentIndex);
		if (mCurrentIndex >= static_cast<int>(draftList.size())) mCurrentIndex = static_cast<int>(draftList.size()) - 1;
		setCurrentIndex(mCurrentIndex);
		loadCurrentDraftToWidgets();
		selectTopField();
		refreshValidationState();
	}

	void browseCurrentColorTheme() {
		if (mCurrentIndex < 0 || mCurrentIndex >= static_cast<int>(draftList.size())) return;
		std::string selectedUri;
		if (!browseColorThemeUri(MRDialogHistoryScope::ExtensionThemeFile, selectedUri)) return;
		writeInputLineString(mProfileColorThemeField, selectedUri, kProfileColorThemeFieldSize);
		saveWidgetsToCurrentDraft();
		refreshValidationState();
	}

	void browseCurrentPostLoadMacro() {
		std::string selectedUri;
		if (!browseMrmacFileUri(MRDialogHistoryScope::ExtensionPostLoadMacro, "Select post-load macro", selectedUri)) return;
		editorSettingsPanel.setPostLoadMacroValue(selectedUri);
		saveWidgetsToCurrentDraft();
		refreshValidationState();
	}

	void browseCurrentPreSaveMacro() {
		std::string selectedUri;
		if (!browseMrmacFileUri(MRDialogHistoryScope::ExtensionPreSaveMacro, "Select pre-save macro", selectedUri)) return;
		editorSettingsPanel.setPreSaveMacroValue(selectedUri);
		saveWidgetsToCurrentDraft();
		refreshValidationState();
	}

	void browseCurrentDefaultPath() {
		std::string selectedPath;
		if (!browseDirectoryPath(MRDialogHistoryScope::ExtensionDefaultPath, selectedPath)) return;
		editorSettingsPanel.setDefaultPathValue(selectedPath);
		saveWidgetsToCurrentDraft();
		refreshValidationState();
	}

	void refreshValidationState() {
		runDialogValidation();
	}

	DialogValidationResult validateDialogValues() {
		std::string errorText;
		DialogValidationResult result;

		editorSettingsPanel.syncDynamicStates();
		if (mCurrentIndex >= 0 && mCurrentIndex < static_cast<int>(draftList.size())) {
			EditProfileDraft currentDraft = collectCurrentDraftFromWidgets();
			result.valid = validateDraftsForUi(draftList, mCurrentIndex, &currentDraft, errorText);
		} else
			result.valid = validateDraftsForUi(draftList, mCurrentIndex, errorText);
		result.warningText = errorText;
		setValidationState(result.valid, result.warningText);
		return result;
	}

	std::vector<EditProfileDraft> draftList;
	int mCurrentIndex = -1;
	TProfileListBox *mProfileList = nullptr;
	TScrollBar *mProfileListScrollBar = nullptr;
	TInactiveStaticText *mProfileIdLabel = nullptr;
	TInactiveStaticText *mProfileNameLabel = nullptr;
	TInactiveStaticText *mProfileExtensionsLabel = nullptr;
	TInactiveStaticText *mProfileColorThemeLabel = nullptr;
	TInputLine *mProfileIdField = nullptr;
	TInputLine *mProfileNameField = nullptr;
	TInputLine *mProfileExtensionsField = nullptr;
	TInputLine *mProfileColorThemeField = nullptr;
	TInlineGlyphButton *mProfileColorThemeBrowseButton = nullptr;
	TButton *mDeleteButton = nullptr;
	bool mIsValid = true;
	FileExtensionEditorSettingsPanel editorSettingsPanel;
};

void showEditProfilesHelpDialog() {
	std::vector<std::string> lines;
	lines.push_back("FILENAME EXTENSIONS HELP");
	lines.push_back("");
	lines.push_back("DEFAULT contains the global edit settings and cannot be deleted.");
	lines.push_back("Each additional profile has its own ID, description and exact extension list.");
	lines.push_back("Extension matching is exact and case-sensitive.");
	lines.push_back("Margins, format ruler, word wrap, macros and default path are profile-specific.");
	lines.push_back("Done writes settings.mrmac and applies the configured values via the VM.");
	TDialog *dialog = createSetupSimplePreviewDialog("FILENAME EXTENSIONS HELP", 78, 14, lines, false);
	if (dialog != nullptr) {
		TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
	}
}

} // namespace

void runFileExtensionProfilesDialogFlow() {
	std::vector<EditProfileDraft> baselineDrafts;
	std::vector<EditProfileDraft> workingDrafts;
	bool running = true;
	std::string normalizeError;

	workingDrafts.push_back(makeDefaultDraft());
	for (const MRFileExtensionProfile &profile : configuredFileExtensionProfiles())
		workingDrafts.push_back(draftFromProfile(profile));
	if (!normalizeDraftListSyntax(workingDrafts, normalizeError)) {
		postDialogError("FE entry normalization failed: " + normalizeError);
		return;
	}
	baselineDrafts = workingDrafts;

	while (running) {
		ushort result = cmCancel;
		std::vector<EditProfileDraft> editedDrafts = workingDrafts;
		std::string errorText;
		TEditProfilesDialog *dialog = new TEditProfilesDialog(workingDrafts);

		if (dialog == nullptr) return;
		result = dialog->run(editedDrafts);
		TObject::destroy(dialog);
		const bool changed = mr::dialogs::isDialogDraftDirty(baselineDrafts, editedDrafts, draftListsEqual);
		switch (result) {
			case cmMrSetupFilenameProfilesHelp:
				showEditProfilesHelpDialog();
				break;

			case cmOK:
				workingDrafts = editedDrafts;
				if (!saveAndReloadEditProfiles(workingDrafts, errorText)) {
					postDialogError(errorText);
					break;
				}
				mrUpdateAllWindowsColorTheme();
				baselineDrafts = workingDrafts;
				running = false;
				break;

			case cmClose:
			case cmCancel:
				if (changed) {
					workingDrafts = editedDrafts;
					std::vector<std::string> dirtyIds = dirtyDraftIds(baselineDrafts, editedDrafts);
					mr::dialogs::UnsavedChangesChoice discardResult = mr::dialogs::runDialogDirtyListGating("UNSAVED PROFILES", "Discard changed filename-extension profiles?", "Dirty profile IDs:", dirtyIds, "~S~ave All");
					if (discardResult == mr::dialogs::UnsavedChangesChoice::Save) {
						workingDrafts = editedDrafts;
						if (!saveAndReloadEditProfiles(workingDrafts, errorText)) {
							postDialogError(errorText);
							break;
						}
						mrUpdateAllWindowsColorTheme();
						running = false;
						break;
					}
					if (discardResult == mr::dialogs::UnsavedChangesChoice::Cancel) {
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
