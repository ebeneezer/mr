#define Uses_TButton
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
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TSItem
#define Uses_TView
#include <tvision/tv.h>

#include "MREditSettingsDialogInternal.hpp"
#include "MRSetupDialogCommon.hpp"

#include "../app/TMREditorApp.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/TMRMenuBar.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace MREditSettingsDialogInternal;

enum : ushort {
	cmMrSetupFilenameProfilesHelp = 3820,
	cmMrSetupFilenameProfilesAdd,
	cmMrSetupFilenameProfilesCopy,
	cmMrSetupFilenameProfilesDelete,
	cmMrSetupFilenameProfilesBrowseColorTheme,
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

std::chrono::milliseconds marqueeDurationForText(const std::string &text);

struct EditProfileDraft {
	bool isDefault = false;
	std::string id;
	std::string name;
	std::string extensionsLiteral;
	std::string colorThemeUri;
	EditSettingsDialogRecord settingsRecord;
};

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
		mr::messageline::postTimed(mr::messageline::Owner::DialogInteraction, warningText_,
		                          mr::messageline::Kind::Warning,
		                          marqueeDurationForText(warningText_),
		                          mr::messageline::kPriorityHigh);
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

std::vector<std::string> splitExtensionLiteral(const std::string &literal) {
	std::vector<std::string> values;
	std::string current;

	for (char ch : literal) {
		if (ch == ';' || ch == ',') {
			std::string token = trimAscii(current);
			if (!token.empty())
				values.push_back(token);
			current.clear();
		} else
			current.push_back(ch);
	}
	current = trimAscii(current);
	if (!current.empty())
		values.push_back(current);
	return values;
}

bool validateProfileIdLiteral(const EditProfileDraft &draft, std::string &errorText) {
	std::string id = trimAscii(draft.id);

	if (draft.isDefault) {
		errorText.clear();
		return true;
	}
	if (id.empty()) {
		errorText = "Profile ID may not be empty.";
		return false;
	}
	for (char ch : id)
		if (!(std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-' || ch == '.')) {
			errorText = "Profile ID allows only letters, digits, '_', '-' and '.'.";
			return false;
		}
	errorText.clear();
	return true;
}

bool validateProfileNameLiteral(const EditProfileDraft &draft, std::string &errorText) {
	if (draft.isDefault) {
		errorText.clear();
		return true;
	}
	if (trimAscii(draft.name).empty()) {
		errorText = "Description may not be empty.";
		return false;
	}
	errorText.clear();
	return true;
}

bool validateProfileExtensionsLiteral(const EditProfileDraft &draft, std::string &errorText) {
	std::vector<std::string> selectors;

	if (draft.isDefault) {
		errorText.clear();
		return true;
	}
	selectors = splitExtensionLiteral(draft.extensionsLiteral);
	if (selectors.size() > 1) {
		errorText = "Profile extension accepts exactly one selector.";
		return false;
	}
	if (!normalizeEditExtensionSelectors(selectors, &errorText)) {
		if (errorText.rfind("Extensions", 0) == 0)
			errorText.replace(0, std::strlen("Extensions"), "Extension");
		return false;
	}
	errorText.clear();
	return true;
}

bool validateProfileColorThemeLiteral(const EditProfileDraft &draft, std::string &errorText) {
	std::string themeUri = trimAscii(draft.colorThemeUri);

	if (themeUri.empty()) {
		errorText.clear();
		return true;
	}
	return validateColorThemeFilePath(themeUri, &errorText);
}

bool validateDraftRecordFields(const EditProfileDraft &draft, std::string &errorText) {
	MREditSetupSettings ignored;
	return recordToSettings(draft.settingsRecord, ignored, errorText);
}

bool validateDraftLocally(const EditProfileDraft &draft, std::string &errorText) {
	if (!validateProfileIdLiteral(draft, errorText))
		return false;
	if (!validateProfileNameLiteral(draft, errorText))
		return false;
	if (!validateProfileExtensionsLiteral(draft, errorText))
		return false;
	if (!validateProfileColorThemeLiteral(draft, errorText))
		return false;
	if (!validateDraftRecordFields(draft, errorText))
		return false;
	errorText.clear();
	return true;
}

unsigned int computeOverrideMask(const MREditSetupSettings &defaults, const MREditSetupSettings &effective) {
	unsigned int mask = kOvNone;

	if (effective.pageBreak != defaults.pageBreak)
		mask |= kOvPageBreak;
	if (effective.wordDelimiters != defaults.wordDelimiters)
		mask |= kOvWordDelimiters;
	if (effective.defaultExtensions != defaults.defaultExtensions)
		mask |= kOvDefaultExtensions;
	if (effective.truncateSpaces != defaults.truncateSpaces)
		mask |= kOvTruncateSpaces;
	if (effective.eofCtrlZ != defaults.eofCtrlZ)
		mask |= kOvEofCtrlZ;
	if (effective.eofCrLf != defaults.eofCrLf)
		mask |= kOvEofCrLf;
	if (effective.tabExpand != defaults.tabExpand)
		mask |= kOvTabExpand;
	if (effective.tabSize != defaults.tabSize)
		mask |= kOvTabSize;
	if (effective.backupFiles != defaults.backupFiles)
		mask |= kOvBackupFiles;
	if (effective.showEofMarker != defaults.showEofMarker)
		mask |= kOvShowEofMarker;
	if (effective.showEofMarkerEmoji != defaults.showEofMarkerEmoji)
		mask |= kOvShowEofMarkerEmoji;
	if (effective.showLineNumbers != defaults.showLineNumbers)
		mask |= kOvShowLineNumbers;
	if (effective.lineNumZeroFill != defaults.lineNumZeroFill)
		mask |= kOvLineNumZeroFill;
	if (effective.persistentBlocks != defaults.persistentBlocks)
		mask |= kOvPersistentBlocks;
	if (effective.codeFolding != defaults.codeFolding)
		mask |= kOvCodeFolding;
	if (upperAscii(effective.columnBlockMove) != upperAscii(defaults.columnBlockMove))
		mask |= kOvColumnBlockMove;
	if (upperAscii(effective.defaultMode) != upperAscii(defaults.defaultMode))
		mask |= kOvDefaultMode;
	return mask;
}

void settingsToRecordLocal(const MREditSetupSettings &settings, EditSettingsDialogRecord &record) {
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
	if (settings.codeFolding)
		record.optionsMask |= kOptionCodeFolding;
	if (settings.showLineNumbers)
		record.optionsMask |= kOptionShowLineNumbers;
	if (settings.lineNumZeroFill)
		record.optionsMask |= kOptionLineNumZeroFill;
	record.tabExpandChoice = settings.tabExpand ? kTabExpandTabs : kTabExpandSpaces;
	record.columnBlockMoveChoice = (columnMove == "LEAVE_SPACE") ? kColumnMoveLeaveSpace : kColumnMoveDeleteSpace;
	record.defaultModeChoice = (defaultMode == "OVERWRITE") ? kDefaultModeOverwrite : kDefaultModeInsert;
}

std::string joinExtensionsLiteral(const std::vector<std::string> &extensions) {
	std::string out;

	for (std::size_t i = 0; i < extensions.size(); ++i) {
		if (i != 0)
			out += ';';
		out += extensions[i];
	}
	return out;
}

bool draftsEqual(const EditProfileDraft &lhs, const EditProfileDraft &rhs) {
	return lhs.isDefault == rhs.isDefault && trimAscii(lhs.id) == trimAscii(rhs.id) &&
	       trimAscii(lhs.name) == trimAscii(rhs.name) &&
	       trimAscii(lhs.extensionsLiteral) == trimAscii(rhs.extensionsLiteral) &&
	       trimAscii(lhs.colorThemeUri) == trimAscii(rhs.colorThemeUri) &&
	       recordsEqual(lhs.settingsRecord, rhs.settingsRecord);
}

bool draftListsEqual(const std::vector<EditProfileDraft> &lhs, const std::vector<EditProfileDraft> &rhs) {
	if (lhs.size() != rhs.size())
		return false;
	for (std::size_t i = 0; i < lhs.size(); ++i)
		if (!draftsEqual(lhs[i], rhs[i]))
			return false;
	return true;
}

EditProfileDraft draftFromProfile(const MREditExtensionProfile &profile) {
	EditProfileDraft draft;
	MREditSetupSettings effective = mergeEditSetupSettings(configuredEditSetupSettings(), profile.overrides);

	draft.isDefault = false;
	draft.id = profile.id;
	draft.name = profile.name;
	draft.extensionsLiteral = joinExtensionsLiteral(profile.extensions);
	draft.colorThemeUri = profile.windowColorThemeUri;
	settingsToRecordLocal(effective, draft.settingsRecord);
	return draft;
}

EditProfileDraft makeDefaultDraft() {
	EditProfileDraft draft;

	draft.isDefault = true;
	draft.id = kDefaultProfileId;
	draft.name = configuredDefaultProfileDescription();
	draft.extensionsLiteral.clear();
	draft.colorThemeUri = configuredColorThemeFilePath();
	settingsToRecordLocal(configuredEditSetupSettings(), draft.settingsRecord);
	return draft;
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
	config.clusterTopY = 13;
	config.tabSizeY = topY + 3;
	config.tabSizeFieldWidth = 4;
	config.includeDefaultExtensions = true;
	config.compactTextRows = true;
	config.tabExpandBesideDefaultMode = true;
	return config;
}

std::string padRightAscii(const std::string &value, std::size_t width) {
	if (value.size() >= width)
		return value;
	return value + std::string(width - value.size(), ' ');
}

std::string buildProfileListLabel(const EditProfileDraft &draft, std::size_t idWidth) {
	std::string id = draft.isDefault ? std::string("DEFAULT") : trimAscii(draft.id);
	std::string name = trimAscii(draft.name);
	std::string label = padRightAscii(id.empty() ? std::string("<empty>") : id, idWidth);

	if (!name.empty())
		label += " " + name;
	return label;
}

std::string nextUniqueProfileId(const std::vector<EditProfileDraft> &existingDrafts, const std::string &seed) {
	std::string trimmedSeed = trimAscii(seed).empty() ? "profile" : trimAscii(seed);
	std::string candidate = trimmedSeed;
	int ordinal = 2;
	bool unique = false;

	while (!unique) {
		unique = true;
		for (const EditProfileDraft &existing : existingDrafts)
			if (!existing.isDefault && upperAscii(trimAscii(existing.id)) == upperAscii(candidate)) {
				unique = false;
				break;
			}
		if (!unique)
			candidate = trimmedSeed + "_" + std::to_string(ordinal++);
	}
	return candidate;
}

EditProfileDraft makeNewDraft(const std::vector<EditProfileDraft> &existingDrafts) {
	EditProfileDraft draft;

	draft.isDefault = false;
	draft.id = nextUniqueProfileId(existingDrafts, "profile");
	draft.name = "New profile";
	draft.extensionsLiteral.clear();
	draft.colorThemeUri = configuredColorThemeFilePath();
	settingsToRecordLocal(configuredEditSetupSettings(), draft.settingsRecord);
	return draft;
}

EditProfileDraft makeCopiedDraft(const EditProfileDraft &source, const std::vector<EditProfileDraft> &existingDrafts) {
	EditProfileDraft draft = source;
	std::string baseId = trimAscii(source.id).empty() ? "profile" : trimAscii(source.id) + "_copy";
	std::string baseName = trimAscii(source.name).empty() ? "Copied profile" : trimAscii(source.name) + " Copy";

	draft.isDefault = false;
	draft.id = nextUniqueProfileId(existingDrafts, baseId);
	draft.name = baseName;
	draft.extensionsLiteral.clear();
	return draft;
}

mr::messageline::Kind toMessageLineKind(TMRMenuBar::MarqueeKind kind) {
	switch (kind) {
		case TMRMenuBar::MarqueeKind::Success:
			return mr::messageline::Kind::Success;
		case TMRMenuBar::MarqueeKind::Warning:
			return mr::messageline::Kind::Warning;
		case TMRMenuBar::MarqueeKind::Error:
			return mr::messageline::Kind::Error;
		case TMRMenuBar::MarqueeKind::Info:
		default:
			return mr::messageline::Kind::Info;
	}
}

std::chrono::milliseconds marqueeDurationForText(const std::string &text) {
	return std::chrono::milliseconds(static_cast<long long>(text.size()) * 100);
}

void setDialogStatus(const std::string &text, TMRMenuBar::MarqueeKind kind) {
	if (text.empty()) {
		mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
		return;
	}
	mr::messageline::postTimed(mr::messageline::Owner::DialogValidation, text, toMessageLineKind(kind),
	                          marqueeDurationForText(text), mr::messageline::kPriorityHigh);
}

void clearDialogStatus() {
	mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
}

bool draftsToConfiguredState(const std::vector<EditProfileDraft> &drafts, MREditSetupSettings &defaultsOut,
                             std::vector<MREditExtensionProfile> &profilesOut, std::string &defaultThemePathOut,
                             std::string &errorText) {
	bool haveDefault = false;
	std::string defaultDescription;

	profilesOut.clear();
	defaultThemePathOut.clear();
	for (const EditProfileDraft &draft : drafts) {
		if (!validateDraftLocally(draft, errorText)) {
			std::string label = draft.isDefault ? std::string("DEFAULT") : trimAscii(draft.id);
			errorText = label + ": " + errorText;
			return false;
		}
		if (draft.isDefault) {
			if (!recordToSettings(draft.settingsRecord, defaultsOut, errorText)) {
				errorText = "DEFAULT: " + errorText;
				return false;
			}
			defaultDescription = trimAscii(draft.name);
			defaultThemePathOut = trimAscii(draft.colorThemeUri);
			if (defaultThemePathOut.empty())
				defaultThemePathOut = defaultColorThemeFilePath();
			if (!validateColorThemeFilePath(defaultThemePathOut, &errorText)) {
				errorText = "DEFAULT: " + errorText;
				return false;
			}
			haveDefault = true;
			break;
		}
	}

	if (!haveDefault) {
		errorText = "DEFAULT profile is missing.";
		return false;
	}

	for (const EditProfileDraft &draft : drafts) {
		MREditSetupSettings effective;
		MREditExtensionProfile profile;

		if (draft.isDefault)
			continue;
		if (!recordToSettings(draft.settingsRecord, effective, errorText)) {
			errorText = trimAscii(draft.id) + ": " + errorText;
			return false;
		}
		profile.id = trimAscii(draft.id);
		profile.name = trimAscii(draft.name);
		profile.extensions = splitExtensionLiteral(draft.extensionsLiteral);
		profile.windowColorThemeUri = trimAscii(draft.colorThemeUri);
		profile.overrides.values = effective;
		profile.overrides.mask = computeOverrideMask(defaultsOut, effective);
		profilesOut.push_back(profile);
	}

	if (!setConfiguredDefaultProfileDescription(defaultDescription, &errorText))
		return false;
	if (!setConfiguredColorThemeFilePath(defaultThemePathOut, &errorText))
		return false;
	if (!setConfiguredEditSetupSettings(defaultsOut, &errorText))
		return false;
	if (!setConfiguredEditExtensionProfiles(profilesOut, &errorText))
		return false;
	return true;
}

std::string joinCommaSeparated(const std::vector<std::string> &values);

std::string profileOwnerLabel(const EditProfileDraft &draft) {
	std::string id = trimAscii(draft.id);
	std::string name = trimAscii(draft.name);

	if (draft.isDefault)
		return "DEFAULT";
	if (id.empty())
		id = "<empty>";
	if (name.empty())
		return id;
	return id + " (" + name + ")";
}

bool validateDraftsForUi(const std::vector<EditProfileDraft> &drafts, int currentIndex, std::string &errorText) {
	std::map<std::string, std::vector<std::size_t>> ids;
	std::map<std::string, std::vector<std::size_t>> exts;
	std::vector<std::size_t> order;

	order.reserve(drafts.size());
	if (currentIndex >= 0 && currentIndex < static_cast<int>(drafts.size()))
		order.push_back(static_cast<std::size_t>(currentIndex));
	for (std::size_t i = 0; i < drafts.size(); ++i)
		if (static_cast<int>(i) != currentIndex)
			order.push_back(i);

	for (std::size_t i : order) {
		std::string localError;
		if (!validateDraftLocally(drafts[i], localError)) {
			if (static_cast<int>(i) == currentIndex)
				errorText = localError;
			else
				errorText = profileOwnerLabel(drafts[i]) + ": " + localError;
			return false;
		}
	}

	for (std::size_t i = 0; i < drafts.size(); ++i) {
		if (drafts[i].isDefault)
			continue;
		ids[upperAscii(trimAscii(drafts[i].id))].push_back(i);
		{
			std::vector<std::string> selectors = splitExtensionLiteral(drafts[i].extensionsLiteral);
			std::string extError;
			if (!normalizeEditExtensionSelectors(selectors, &extError)) {
				if (extError.rfind("Extensions", 0) == 0)
					extError.replace(0, std::strlen("Extensions"), "Extension");
				if (static_cast<int>(i) == currentIndex)
					errorText = extError;
				else
					errorText = profileOwnerLabel(drafts[i]) + ": " + extError;
				return false;
			}
			if (!selectors.empty())
				exts[selectors.front()].push_back(i);
		}
	}

	for (const auto &entry : ids)
		if (entry.second.size() > 1) {
			std::vector<std::string> owners;
			for (std::size_t idx : entry.second)
				if (static_cast<int>(idx) != currentIndex)
					owners.push_back(profileOwnerLabel(drafts[idx]));
			if (currentIndex >= 0 && std::find(entry.second.begin(), entry.second.end(), static_cast<std::size_t>(currentIndex)) != entry.second.end()) {
				errorText = "Duplicate profile ID '" + trimAscii(drafts[currentIndex].id) + "': " + joinCommaSeparated(owners);
				return false;
			}
			errorText = "Duplicate profile ID '" + trimAscii(drafts[entry.second.front()].id) + "': " + joinCommaSeparated(owners);
			return false;
		}

	for (const auto &entry : exts)
		if (entry.second.size() > 1) {
			std::vector<std::string> owners;
			for (std::size_t idx : entry.second)
				if (static_cast<int>(idx) != currentIndex)
					owners.push_back(profileOwnerLabel(drafts[idx]));
			if (currentIndex >= 0 && std::find(entry.second.begin(), entry.second.end(), static_cast<std::size_t>(currentIndex)) != entry.second.end()) {
				errorText = "Duplicate profile extension '" + entry.first + "': " + joinCommaSeparated(owners);
				return false;
			}
			errorText = "Duplicate profile extension '" + entry.first + "': " + joinCommaSeparated(owners);
			return false;
		}

	errorText.clear();
	return true;
}

bool validateDraftsForSave(const std::vector<EditProfileDraft> &drafts, std::string &errorText) {
	if (!validateDraftsForUi(drafts, -1, errorText))
		return false;
	errorText.clear();
	return true;
}

bool saveAndReloadEditProfiles(const std::vector<EditProfileDraft> &drafts, std::string &errorText) {
	TMREditorApp *app = dynamic_cast<TMREditorApp *>(TProgram::application);
	MRSetupPaths paths;
	MREditSetupSettings defaultsCandidate;
	std::vector<MREditExtensionProfile> profilesCandidate;
	std::string defaultThemePathCandidate;

	if (app == nullptr) {
		errorText = "Application error: TMREditorApp is unavailable.";
		return false;
	}
	if (!draftsToConfiguredState(drafts, defaultsCandidate, profilesCandidate, defaultThemePathCandidate, errorText))
		return false;

	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();

	if (!writeSettingsMacroFile(paths, &errorText))
		return false;
	if (!app->reloadSettingsMacroFromPath(paths.settingsMacroUri, &errorText))
		return false;

	errorText.clear();
	return true;
}

std::vector<std::string> dirtyDraftIds(const std::vector<EditProfileDraft> &initialDrafts,
	                                  const std::vector<EditProfileDraft> &drafts) {
	std::vector<std::string> out;
	std::size_t count = std::max(initialDrafts.size(), drafts.size());
	for (std::size_t i = 0; i < count; ++i) {
		const EditProfileDraft *initial = i < initialDrafts.size() ? &initialDrafts[i] : nullptr;
		const EditProfileDraft *current = i < drafts.size() ? &drafts[i] : nullptr;
		if (initial != nullptr && current != nullptr && draftsEqual(*initial, *current))
			continue;
		std::string id;
		if (current != nullptr)
			id = trimAscii(current->id);
		if (id.empty() && initial != nullptr)
			id = trimAscii(initial->id);
		if (id.empty())
			id = "<empty>";
		out.push_back(id);
	}
	return out;
}

std::string joinCommaSeparated(const std::vector<std::string> &values) {
	std::string out;
	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i != 0)
			out += ", ";
		out += values[i];
	}
	return out;
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
	      panel_(makeProfilesPanelConfig(kDialogWidth - 1, 32, 45, kDialogWidth - 6, 6)) {
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
	static const int kDialogWidth = 104;
	static const int kVisibleHeight = 24;
	static const int kVirtualHeight = 34;

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
		const int listWidth = 27;
		const int listBottom = 8;
		const int rightLeft = 32;
		const int rightRight = kDialogWidth - 2;
		const int fieldLeft = 45;
		const int glyphWidth = 3;
		const int colorGlyphLeft = rightRight - glyphWidth;
		const int fieldRight = colorGlyphLeft - 1;
		const int buttonRow = 9;
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
		profileColorThemeBrowseButton_ = addGlyphButton(TRect(colorGlyphLeft, 5, rightRight, 6),
		                                             cmMrSetupFilenameProfilesBrowseColorTheme);

		panel_.buildViews(*this);

		doneButton_ = addButton(TRect(doneLeft, bottomTop, doneLeft + 10, bottomTop + 2), "~D~one", cmOK,
		                        bfDefault);
		addButton(TRect(cancelLeft, bottomTop, cancelLeft + 12, bottomTop + 2), "C~a~ncel", cmCancel,
		          bfNormal);
		addButton(TRect(helpLeft, bottomTop, helpLeft + 8, bottomTop + 2), "~H~elp", cmMrSetupFilenameProfilesHelp,
		          bfNormal);
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
		const bool isDefault = draft.isDefault;

		writeInputLineString(profileIdField_, draft.id, kProfileIdFieldSize);
		writeInputLineString(profileNameField_, draft.name, kProfileNameFieldSize);
		writeInputLineString(profileExtensionsField_, draft.extensionsLiteral, kProfileExtensionsFieldSize);
		writeInputLineString(profileColorThemeField_, draft.colorThemeUri, kProfileColorThemeFieldSize);
		panel_.loadFieldsFromRecord(draft.settingsRecord);
		if (profileIdField_ != nullptr) {
			profileIdField_->setState(sfVisible, True);
			profileIdField_->setState(sfDisabled, False);
			setInputReadOnly(profileIdField_, isDefault, "read-only with DEFAULT profile");
		}
		if (profileNameField_ != nullptr) {
			profileNameField_->setState(sfVisible, True);
			profileNameField_->setState(sfDisabled, False);
			setInputReadOnly(profileNameField_, false, "");
		}
		if (profileExtensionsField_ != nullptr) {
			profileExtensionsField_->setState(sfVisible, True);
			profileExtensionsField_->setState(sfDisabled, False);
			setInputReadOnly(profileExtensionsField_, isDefault, "read-only with DEFAULT profile");
		}
		if (profileColorThemeField_ != nullptr) {
			profileColorThemeField_->setState(sfVisible, True);
			profileColorThemeField_->setState(sfDisabled, False);
			setInputReadOnly(profileColorThemeField_, false, "");
		}
		if (profileColorThemeBrowseButton_ != nullptr) {
			profileColorThemeBrowseButton_->setState(sfVisible, True);
			profileColorThemeBrowseButton_->setState(sfDisabled, False);
		}
		if (profileIdLabel_ != nullptr)
			profileIdLabel_->setInactive(isDefault);
		if (profileNameLabel_ != nullptr)
			profileNameLabel_->setInactive(False);
		if (profileExtensionsLabel_ != nullptr)
			profileExtensionsLabel_->setInactive(isDefault);
		if (profileColorThemeLabel_ != nullptr)
			profileColorThemeLabel_->setInactive(False);
		if (deleteButton_ != nullptr)
			deleteButton_->setState(sfDisabled, isDefault ? True : False);
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

	void refreshValidationState() {
		std::string errorText;

		saveWidgetsToCurrentDraft();
		refreshProfileList();
		if (!validateDraftsForUi(drafts_, currentIndex_, errorText)) {
			isValid_ = false;
			if (doneButton_ != nullptr)
				doneButton_->setState(sfDisabled, True);
			if (errorText != lastValidationText_) {
				setDialogStatus(errorText, TMRMenuBar::MarqueeKind::Warning);
				lastValidationText_ = errorText;
			}
		} else {
			isValid_ = true;
			if (doneButton_ != nullptr)
				doneButton_->setState(sfDisabled, False);
			lastValidationText_.clear();
			clearDialogStatus();
		}
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
	lines.push_back("Done writes settings.mrmac and reloads the configuration.");
	TDialog *dialog = createSetupSimplePreviewDialog("FILENAME EXTENSIONS HELP", 78, 14, lines, false);
	if (dialog != nullptr) {
		TProgram::deskTop->execView(dialog);
		TObject::destroy(dialog);
	}
}

} // namespace

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
					mr::messageline::postTimed(mr::messageline::Owner::DialogValidation, errorText,
					                          mr::messageline::Kind::Warning,
					                          marqueeDurationForText(errorText),
					                          mr::messageline::kPriorityHigh);
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
							mr::messageline::postTimed(mr::messageline::Owner::DialogValidation, errorText,
							                          mr::messageline::Kind::Warning,
							                          marqueeDurationForText(errorText),
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
