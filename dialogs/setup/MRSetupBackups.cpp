#define Uses_TApplication
#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TChDirDialog
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TFileDialog
#define Uses_TDrawBuffer
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TObject
#define Uses_TRadioButtons
#define Uses_TRect
#define Uses_TStaticText
#define Uses_TSItem
#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

#include "MRSetup.hpp"
#include "MRSetupSections.hpp"

#include "../../app/MRCommands.hpp"
#include "../../app/MRCommandRouter.hpp"
#include "../../app/MRHelpTopics.generated.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRBentoBox/MRBentoBox.hpp"
#include "../../ui/MRMenuBar.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/widgets/MRNumericSlider.hpp"
#include "../../ui/widgets/MRDropList.hpp"
#include "../../ui/widgets/MRSpinner.hpp"
#include "../../ui/MRPalette.hpp"
#include "../MRDirtyGating.hpp"
#include "MRSetupCommon.hpp"
#include "../../app/commands/MRWindowCommands.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using mr::dialogs::discardQueuedCancelEvent;
using mr::dialogs::initSetupDialogFrame;
using mr::dialogs::postSetupFlowError;
using mr::dialogs::readRecordField;
using mr::dialogs::writeRecordField;

enum : ushort {
	cmMrSetupBackupsAutosaveBrowseDirectory = 3811,
	cmMrSetupFieldChanged
};

enum {
	kBackupDirectoryFieldSize = 256,
	kBackupExtensionFieldSize = 32,
	kAutosaveNumberFieldSize = 16
};

enum : ushort {
	kBackupMethodOff = 0,
	kBackupMethodBakFile,
	kBackupMethodDirectory
};

enum : ushort {
	kBackupFrequencyFirstSaveOnly = 0,
	kBackupFrequencyEverySave
};

[[nodiscard]] bool parseNonNegativeIntegerField(const std::string &text, int &valueOut) {
	char *end = nullptr;
	const long parsed = std::strtol(text.c_str(), &end, 10);

	if (text.empty() || end == text.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > INT_MAX) return false;
	valueOut = static_cast<int>(parsed);
	return true;
}

void clearSetupDialogStatus() {
	mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
}

std::string readCurrentWorkingDirectory() {
	char cwd[PATH_MAX];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr) return std::string();
	return std::string(cwd);
}

bool browsePathWithDirectoryDialog(MRDialogHistoryScope scope, std::string &selectedPath) {
	const std::string originalCwd = readCurrentWorkingDirectory();
	const std::string seed = configuredLastFileDialogPath(scope);
	std::string picked;
	ushort result;

	if (!seed.empty()) (void)::chdir(seed.c_str());
	result = mr::dialogs::execDialog(mr::dialogs::createDirectoryDialog(scope, cdNormal));
	picked = readCurrentWorkingDirectory();
	if (!originalCwd.empty()) (void)::chdir(originalCwd.c_str());
	if (result == cmCancel) {
		discardQueuedCancelEvent();
		return false;
	}
	selectedPath = normalizeConfiguredPathInput(picked);
	if (!selectedPath.empty()) rememberLoadDialogPath(scope, selectedPath.c_str());
	return !selectedPath.empty();
}

struct BackupsAutosaveDialogRecord {
	ushort backupMethodChoice;
	ushort backupFrequencyChoice;
	char backupFileExtension[kBackupExtensionFieldSize];
	char backupDirectoryPath[kBackupDirectoryFieldSize];
	char inactivitySeconds[kAutosaveNumberFieldSize];
	char absoluteIntervalSeconds[kAutosaveNumberFieldSize];
};

bool recordsEqual(const BackupsAutosaveDialogRecord &lhs, const BackupsAutosaveDialogRecord &rhs) {
	auto normalizeForDirty = [](const BackupsAutosaveDialogRecord &record) {
		struct Snapshot {
			ushort method = kBackupMethodOff;
			ushort frequency = kBackupFrequencyFirstSaveOnly;
			std::string extension;
			std::string directory;
			std::string inactivity;
			std::string interval;
		};
		Snapshot snapshot;

		snapshot.method = record.backupMethodChoice;
		snapshot.frequency = record.backupFrequencyChoice;
		snapshot.extension = trimAscii(readRecordField(record.backupFileExtension));
		snapshot.directory = normalizeConfiguredPathInput(readRecordField(record.backupDirectoryPath));
		snapshot.inactivity = trimAscii(readRecordField(record.inactivitySeconds));
		snapshot.interval = trimAscii(readRecordField(record.absoluteIntervalSeconds));
		if (snapshot.method != kBackupMethodBakFile) snapshot.extension.clear();
		if (snapshot.method != kBackupMethodDirectory) snapshot.directory.clear();
		if (snapshot.method == kBackupMethodOff) {
			snapshot.frequency = kBackupFrequencyFirstSaveOnly;
			snapshot.inactivity = "0";
			snapshot.interval = "0";
		}
		return snapshot;
	};

	const auto left = normalizeForDirty(lhs);
	const auto right = normalizeForDirty(rhs);
	return left.method == right.method && left.frequency == right.frequency && left.extension == right.extension && left.directory == right.directory && left.inactivity == right.inactivity &&
	       left.interval == right.interval;
}

void initBackupsAutosaveDialogRecord(BackupsAutosaveDialogRecord &record) {
	MREditSetupSettings settings = configuredEditSetupSettings();
	std::memset(&record, 0, sizeof(record));
	record.backupMethodChoice = kBackupMethodBakFile;
	if (settings.backupMethod == "OFF") record.backupMethodChoice = kBackupMethodOff;
	else if (settings.backupMethod == "DIRECTORY")
		record.backupMethodChoice = kBackupMethodDirectory;
	record.backupFrequencyChoice = settings.backupFrequency == "EVERY_SAVE" ? kBackupFrequencyEverySave : kBackupFrequencyFirstSaveOnly;
	writeRecordField(record.backupFileExtension, sizeof(record.backupFileExtension), settings.backupExtension);
	writeRecordField(record.backupDirectoryPath, sizeof(record.backupDirectoryPath), settings.backupDirectory);
	writeRecordField(record.inactivitySeconds, sizeof(record.inactivitySeconds), std::to_string(settings.autosaveInactivitySeconds));
	writeRecordField(record.absoluteIntervalSeconds, sizeof(record.absoluteIntervalSeconds), std::to_string(settings.autosaveIntervalSeconds));
}

MREditSetupSettings editSetupSettingsFromBackupsAutosaveRecord(const BackupsAutosaveDialogRecord &record) {
	MREditSetupSettings settings = configuredEditSetupSettings();
	settings.backupMethod = record.backupMethodChoice == kBackupMethodOff ? "OFF" : record.backupMethodChoice == kBackupMethodDirectory ? "DIRECTORY" : "BAK_FILE";
	settings.backupFrequency = record.backupFrequencyChoice == kBackupFrequencyEverySave ? "EVERY_SAVE" : "FIRST_SAVE_ONLY";
	settings.backupExtension = trimAscii(readRecordField(record.backupFileExtension));
	settings.backupDirectory = normalizeConfiguredPathInput(readRecordField(record.backupDirectoryPath));
	settings.autosaveInactivitySeconds = std::atoi(trimAscii(readRecordField(record.inactivitySeconds)).c_str());
	settings.autosaveIntervalSeconds = std::atoi(trimAscii(readRecordField(record.absoluteIntervalSeconds)).c_str());
	settings.backupFiles = settings.backupMethod != "OFF";
	return settings;
}

bool validateBackupsAutosaveRecord(const BackupsAutosaveDialogRecord &record, std::string &errorText) {
	MREditSetupSettings settings = editSetupSettingsFromBackupsAutosaveRecord(record);
	MREditSetupSettings originalSettings = configuredEditSetupSettings();

	if (!setConfiguredEditSetupSettings(settings, &errorText)) return false;
	(void)setConfiguredEditSetupSettings(originalSettings, nullptr);
	errorText.clear();
	return true;
}

bool sameBackupsAutosaveSettings(const MREditSetupSettings &lhs, const MREditSetupSettings &rhs) {
	return lhs.backupMethod == rhs.backupMethod && lhs.backupFrequency == rhs.backupFrequency && lhs.backupExtension == rhs.backupExtension && lhs.backupDirectory == rhs.backupDirectory &&
	       lhs.autosaveInactivitySeconds == rhs.autosaveInactivitySeconds && lhs.autosaveIntervalSeconds == rhs.autosaveIntervalSeconds;
}

bool persistBackupsAutosaveRecord(const BackupsAutosaveDialogRecord &record, std::string &errorText) {
	MREditSetupSettings configuredSettings = configuredEditSetupSettings();
	MREditSetupSettings updatedSettings = editSetupSettingsFromBackupsAutosaveRecord(record);
	MRSetupPaths paths = resolveSetupPathDefaults();
	MRSettingsWriteReport writeReport;

	if (!setConfiguredEditSetupSettings(updatedSettings, &errorText)) return false;
	updatedSettings = configuredEditSetupSettings();
	if (sameBackupsAutosaveSettings(configuredSettings, updatedSettings)) {
		errorText.clear();
		return true;
	}
	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	if (!writeSettingsMacroFile(paths, &errorText, &writeReport)) {
		(void)setConfiguredEditSetupSettings(configuredSettings, nullptr);
		return false;
	}
	mrLogSettingsWriteReport("installation/setup backups-autosave", writeReport);
	errorText.clear();
	return true;
}

class TNotifyingInputLine : public TInputLine {
  public:
	TNotifyingInputLine(const TRect &bounds, int maxLen, ushort changeCommand) noexcept : TInputLine(bounds, maxLen), mCapacity(maxLen + 1), mChangeCommand(changeCommand) {
	}

	void handleEvent(TEvent &event) override {
		std::string beforeText = currentText();
		TView *target = owner;

		TInputLine::handleEvent(event);
		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;

		if (currentText() != beforeText) message(target != nullptr ? target : owner, evBroadcast, mChangeCommand, this);
	}

  private:
	std::string currentText() const {
		std::vector<char> buffer(mCapacity, '\0');
		const_cast<TNotifyingInputLine *>(this)->getData(buffer.data());
		return std::string(buffer.data());
	}

	std::size_t mCapacity = 0;
	ushort mChangeCommand = 0;
};

class TBackupsAutosaveSetupDialog : public MRScrollableDialog {
  public:
	class TInlineGlyphButton : public TView {
	  public:
		TInlineGlyphButton(const TRect &bounds, const char *glyph, ushort command) : TView(bounds), mGlyph(glyph != nullptr ? glyph : ""), mCommand(command) {
			options |= ofSelectable;
			options |= ofFirstClick;
			eventMask |= evMouseDown | evKeyDown;
		}

		void draw() override {
			TDrawBuffer b;
			TAttrPair color = getColor((state & sfFocused) != 0 ? 2 : 1);
			int glyphWidth = strwidth(mGlyph.c_str());
			int x = std::max(0, (size.x - glyphWidth) / 2);

			b.moveChar(0, ' ', color, size.x);
			b.moveStr(static_cast<ushort>(x), mGlyph.c_str(), color, size.x - x);
			writeLine(0, 0, size.x, size.y, b);
		}

		void handleEvent(TEvent &event) override {
			if ((state & sfDisabled) != 0) {
				TView::handleEvent(event);
				return;
			}
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

	TBackupsAutosaveSetupDialog(const BackupsAutosaveDialogRecord &initialRecord)
	    : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(kVirtualDialogWidth, kVirtualDialogHeight), "BACKUPS & AUTOSAVE", kVirtualDialogWidth, kVirtualDialogHeight, initSetupDialogFrame), mCurrentRecord(initialRecord) {
		helpCtx = hcDialogBackups;
		buildViews();
		setDialogValidationHook([this]() { return validateDialogValues(); });
		loadFieldsFromRecord(mCurrentRecord);
		updateBackupFieldState();
		initScrollIfNeeded();
		selectContent();
		refreshValidationState();
	}

	~TBackupsAutosaveSetupDialog() override {
		clearSetupDialogStatus();
	}

	ushort run(BackupsAutosaveDialogRecord &outRecord) {
		ushort result = TProgram::deskTop->execView(this);
		outRecord = collectRecordFromFields();
		return result;
	}

	void handleEvent(TEvent &event) override {
		ushort originalWhat = event.what;
		ushort originalCommand = (event.what == evCommand || event.what == evBroadcast) ? event.message.command : 0;
		void *originalInfoPtr = event.what == evBroadcast ? event.message.infoPtr : nullptr;
		ushort originalKey = event.what == evKeyDown ? event.keyDown.keyCode : 0;
		ushort backupMethod = currentBackupMethod();
		bool forwardTab = event.what == evKeyDown && (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI);
		bool backwardTab = event.what == evKeyDown && event.keyDown.keyCode == kbShiftTab;

		if (forwardTab) {
			if (current == mBackupFrequencyField) {
				if (backupMethod == kBackupMethodBakFile && mBackupExtensionField != nullptr) mBackupExtensionField->select();
				else if (mInactivitySecondsSlider != nullptr)
					mInactivitySecondsSlider->select();
				clearEvent(event);
				refreshValidationState();
				return;
			}
			if (current == mBackupExtensionField || current == mBackupDirectoryField || current == mBackupDirectoryBrowseButton) {
				if (mInactivitySecondsSlider != nullptr) mInactivitySecondsSlider->select();
				clearEvent(event);
				refreshValidationState();
				return;
			}
		}

		if (backwardTab) {
			if (current == mInactivitySecondsSlider) {
				if (backupMethod == kBackupMethodBakFile && mBackupExtensionField != nullptr && (mBackupExtensionField->state & sfDisabled) == 0) mBackupExtensionField->select();
				else if (mBackupFrequencyField != nullptr)
					mBackupFrequencyField->select();
				clearEvent(event);
				refreshValidationState();
				return;
			}
			if (current == mBackupExtensionField || current == mBackupDirectoryField || current == mBackupDirectoryBrowseButton) {
				if (mBackupFrequencyField != nullptr) mBackupFrequencyField->select();
				clearEvent(event);
				refreshValidationState();
				return;
			}
		}

		if (event.what == evMouseDown && mouseHitsBackupBrowseButton(event)) {
			browseBackupDirectory();
			updateBackupFieldState();
			refreshValidationState();
			clearEvent(event);
			return;
		}

		MRScrollableDialog::handleEvent(event);

		if (originalWhat == evCommand && originalCommand == cmOK) {
			refreshValidationState();
			if (!mIsValid) {
				clearEvent(event);
				return;
			}
		}

		if (event.what == evCommand) {
			switch (event.message.command) {
				case cmMrSetupBackupsAutosaveBrowseDirectory:
					browseBackupDirectory();
					updateBackupFieldState();
					refreshValidationState();
					clearEvent(event);
					return;

				default:
					break;
			}
		}

		if (originalWhat == evCommand || originalWhat == evKeyDown || originalWhat == evMouseDown || originalWhat == evMouseUp || originalWhat == evBroadcast) updateBackupFieldState();
		if (originalWhat == evCommand || originalWhat == evKeyDown || originalWhat == evMouseDown || originalWhat == evMouseUp || originalWhat == evBroadcast) refreshValidationState();
		if (originalWhat == evBroadcast && (originalCommand == cmReleasedFocus || originalCommand == cmReceivedFocus || originalCommand == cmMRNumericSliderChanged) && originalInfoPtr != mBackupDirectoryBrowseButton)
			refreshValidationState();
		if (originalWhat == evKeyDown && (originalKey == kbTab || originalKey == kbCtrlI || originalKey == kbShiftTab)) refreshValidationState();
	}

  private:
	TStaticText *addLabel(const TRect &rect, const char *text) {
		TStaticText *view = new TStaticText(rect, text);
		addManaged(view, rect);
		return view;
	}

	TInputLine *addInput(const TRect &rect, int maxLen) {
		TInputLine *view = new TNotifyingInputLine(rect, maxLen, cmMrSetupFieldChanged);
		addManaged(view, rect);
		return view;
	}

	MRNumericSlider *addNumericSlider(const TRect &rect, int32_t minValue, int32_t maxValue, int32_t initialValue, int32_t step, int32_t pageStep) {
		MRNumericSlider *view = new MRNumericSlider(rect, minValue, maxValue, initialValue, step, pageStep, MRNumericSlider::fmtRaw, cmMRNumericSliderChanged);
		addManaged(view, rect);
		return view;
	}

	TRadioButtons *addRadioGroup(const TRect &rect, TSItem *items) {
		TRadioButtons *view = new TRadioButtons(rect, items);
		addManaged(view, rect);
		return view;
	}

	TInlineGlyphButton *addBrowseButton(const TRect &rect, ushort command) {
		TInlineGlyphButton *view = new TInlineGlyphButton(rect, "🔎", command);
		view->options &= ~ofSelectable;
		addManaged(view, rect);
		return view;
	}

	void buildViews() {
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 0);
		const int dialogWidth = kVirtualDialogWidth;
		const int leftGroupLeft = 2;
		const int leftGroupRight = 28;
		const int rightGroupLeft = 31;
		const int rightGroupRight = 53;
		const int textFieldLeft = 23;
		const int backupExtFieldRight = textFieldLeft + 12;
		const int glyphWidth = 2;
		const int glyphRight = dialogWidth - 2;
		const int glyphLeft = glyphRight - glyphWidth;
		const int backupPathFieldRight = glyphLeft;
		const int autosaveFieldRight = dialogWidth - 2;
		const int buttonLeft = (dialogWidth - metrics.rowWidth) / 2;
		const int buttonTop = kVirtualDialogHeight - 3;

		addLabel(TRect(leftGroupLeft, 2, leftGroupRight, 3), "Backup method:");
		mBackupMethodField = addRadioGroup(TRect(leftGroupLeft, 3, leftGroupRight, 6), new TSItem("~O~ff", new TSItem("create ~B~ackup file", new TSItem("move to backup ~P~ath", nullptr))));

		addLabel(TRect(rightGroupLeft, 2, rightGroupRight, 3), "Backup frequency:");
		mBackupFrequencyField = addRadioGroup(TRect(rightGroupLeft, 3, rightGroupRight, 5), new TSItem("~F~irst save only", new TSItem("E~v~ery save", nullptr)));

		addLabel(TRect(2, 7, textFieldLeft - 1, 8), "Backup file ext.:");
		mBackupExtensionField = addInput(TRect(textFieldLeft, 7, backupExtFieldRight, 8), kBackupExtensionFieldSize - 1);

		addLabel(TRect(2, 8, textFieldLeft - 1, 9), "Backup path:");
		mBackupDirectoryField = addInput(TRect(textFieldLeft, 8, backupPathFieldRight, 9), kBackupDirectoryFieldSize - 1);
		mBackupDirectoryBrowseButton = addBrowseButton(TRect(glyphLeft, 8, glyphRight, 9), cmMrSetupBackupsAutosaveBrowseDirectory);

		addLabel(TRect(2, 10, dialogWidth - 2, 11), "Autosave in seconds, 0 = OFF:");
		addLabel(TRect(2, 11, textFieldLeft - 1, 12), "Keyboard inactivity:");
		mInactivitySecondsSlider = addNumericSlider(TRect(textFieldLeft, 11, autosaveFieldRight, 12), 0, 100, 15, 5, 10);
		addLabel(TRect(2, 12, textFieldLeft - 1, 13), "Intervall auto save:");
		mAbsoluteIntervalSlider = addNumericSlider(TRect(textFieldLeft, 12, autosaveFieldRight, 13), 0, 300, 180, 10, 50);

		mr::dialogs::addManagedUniformButtonRow(*this, buttonLeft, buttonTop, 0, buttons);
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

	void refreshValidationState() {
		runDialogValidation();
	}

	DialogValidationResult validateDialogValues() {
		std::string errorText;
		DialogValidationResult result;
		BackupsAutosaveDialogRecord record = collectRecordFromFields();

		result.valid = validateBackupsAutosaveRecord(record, errorText);
		result.warningText = errorText;
		mIsValid = result.valid;
		return result;
	}

	static int parseSliderValueOrDefault(const char *value, int fallback, int minimumEnabled, int maximumEnabled) {
		int parsed = fallback;
		if (!parseNonNegativeIntegerField(trimAscii(readRecordField(value)), parsed)) parsed = fallback;
		if (parsed == 0) return 0;
		if (parsed < minimumEnabled) return minimumEnabled;
		if (parsed > maximumEnabled) return maximumEnabled;
		return parsed;
	}

	static void writeSliderValue(MRNumericSlider *slider, char *dest, std::size_t destSize, int fallback, int minimumEnabled, int maximumEnabled) {
		int32_t value = fallback;
		if (slider != nullptr) slider->getData(&value);
		int normalized = static_cast<int>(value);
		if (normalized != 0) normalized = std::clamp(normalized, minimumEnabled, maximumEnabled);
		writeRecordField(dest, destSize, std::to_string(normalized));
	}

	void setInputValue(TInputLine *inputLine, std::size_t capacity, const std::string &value) {
		std::vector<char> buffer(capacity, '\0');
		writeRecordField(buffer.data(), buffer.size(), value);
		inputLine->setData(buffer.data());
	}

	void loadFieldsFromRecord(const BackupsAutosaveDialogRecord &record) {
		if (mBackupMethodField != nullptr) mBackupMethodField->setData((void *)&record.backupMethodChoice);
		if (mBackupFrequencyField != nullptr) mBackupFrequencyField->setData((void *)&record.backupFrequencyChoice);
		setInputLineValue(mBackupExtensionField, record.backupFileExtension, sizeof(record.backupFileExtension));
		setInputLineValue(mBackupDirectoryField, record.backupDirectoryPath, sizeof(record.backupDirectoryPath));
		if (mInactivitySecondsSlider != nullptr) {
			int32_t value = parseSliderValueOrDefault(record.inactivitySeconds, 15, 5, 100);
			mInactivitySecondsSlider->setData(&value);
		}
		if (mAbsoluteIntervalSlider != nullptr) {
			int32_t value = parseSliderValueOrDefault(record.absoluteIntervalSeconds, 180, 100, 300);
			mAbsoluteIntervalSlider->setData(&value);
		}
	}

	void saveFieldsToRecord(BackupsAutosaveDialogRecord &record) const {
		if (mBackupMethodField != nullptr) mBackupMethodField->getData((void *)&record.backupMethodChoice);
		if (mBackupFrequencyField != nullptr) mBackupFrequencyField->getData((void *)&record.backupFrequencyChoice);
		readInputLineValue(mBackupExtensionField, record.backupFileExtension, sizeof(record.backupFileExtension));
		readInputLineValue(mBackupDirectoryField, record.backupDirectoryPath, sizeof(record.backupDirectoryPath));
		writeSliderValue(mInactivitySecondsSlider, record.inactivitySeconds, sizeof(record.inactivitySeconds), 15, 5, 100);
		writeSliderValue(mAbsoluteIntervalSlider, record.absoluteIntervalSeconds, sizeof(record.absoluteIntervalSeconds), 180, 100, 300);
	}

	BackupsAutosaveDialogRecord collectRecordFromFields() const {
		BackupsAutosaveDialogRecord record = mCurrentRecord;
		saveFieldsToRecord(record);
		return record;
	}

	ushort currentBackupMethod() const {
		ushort method = kBackupMethodOff;
		if (mBackupMethodField != nullptr) mBackupMethodField->getData((void *)&method);
		return method;
	}

	void updateBackupFieldState() {
		ushort method = currentBackupMethod();
		const bool backupOff = method == kBackupMethodOff;
		const bool extensionEnabled = method == kBackupMethodBakFile;
		const bool pathEnabled = method == kBackupMethodDirectory;
		if (mBackupFrequencyField != nullptr) mBackupFrequencyField->setState(sfDisabled, backupOff ? True : False);
		if (mBackupExtensionField != nullptr) mBackupExtensionField->setState(sfDisabled, extensionEnabled ? False : True);
		if (mBackupDirectoryField != nullptr) mBackupDirectoryField->setState(sfDisabled, pathEnabled ? False : True);
		if (mBackupDirectoryBrowseButton != nullptr) mBackupDirectoryBrowseButton->setState(sfDisabled, pathEnabled ? False : True);
		if (mInactivitySecondsSlider != nullptr) mInactivitySecondsSlider->setState(sfDisabled, backupOff ? True : False);
		if (mAbsoluteIntervalSlider != nullptr) mAbsoluteIntervalSlider->setState(sfDisabled, backupOff ? True : False);
	}

	bool mouseHitsBackupBrowseButton(TEvent &event) {
		return event.what == evMouseDown && mBackupDirectoryBrowseButton != nullptr && (mBackupDirectoryBrowseButton->state & sfDisabled) == 0 && mBackupDirectoryBrowseButton->containsMouse(event);
	}

	void browseBackupDirectory() {
		std::string selected;
		if (browsePathWithDirectoryDialog(MRDialogHistoryScope::SetupBackupDirectory, selected)) setInputValue(mBackupDirectoryField, kBackupDirectoryFieldSize, selected);
		if (mBackupDirectoryField != nullptr) mBackupDirectoryField->select();
	}

	BackupsAutosaveDialogRecord mCurrentRecord;
	static const int kVirtualDialogWidth = 96;
	static const int kVirtualDialogHeight = 17;
	bool mIsValid = true;
	TRadioButtons *mBackupMethodField = nullptr;
	TRadioButtons *mBackupFrequencyField = nullptr;
	TInputLine *mBackupExtensionField = nullptr;
	TInputLine *mBackupDirectoryField = nullptr;
	TView *mBackupDirectoryBrowseButton = nullptr;
	MRNumericSlider *mInactivitySecondsSlider = nullptr;
	MRNumericSlider *mAbsoluteIntervalSlider = nullptr;
};


} // namespace

void runBackupsAutosaveDialogFlow() {
	bool running = true;
	std::string errorText;
	BackupsAutosaveDialogRecord baselineRecord;
	BackupsAutosaveDialogRecord workingRecord;
	initBackupsAutosaveDialogRecord(baselineRecord);
	workingRecord = baselineRecord;

	while (running) {
		ushort result;
		BackupsAutosaveDialogRecord editedRecord = workingRecord;
		TBackupsAutosaveSetupDialog *dialog = new TBackupsAutosaveSetupDialog(workingRecord);

		if (dialog == nullptr) return;
		result = dialog->run(editedRecord);
		TObject::destroy(dialog);
		const bool changed = mr::dialogs::isDialogDraftDirty(baselineRecord, editedRecord, [](const BackupsAutosaveDialogRecord &lhs, const BackupsAutosaveDialogRecord &rhs) { return recordsEqual(lhs, rhs); });

		switch (result) {
			case cmOK:
				workingRecord = editedRecord;
				if (!persistBackupsAutosaveRecord(workingRecord, errorText)) {
					postSetupFlowError("Installation / Backups / Autosave", errorText);
					break;
				}
				baselineRecord = workingRecord;
				running = false;
				break;

			case cmClose:
			case cmCancel:
				if (!changed) {
					running = false;
					break;
				}
				switch (mr::dialogs::runDialogDirtyGating("Backup and autosave settings have unsaved changes.")) {
					case mr::dialogs::UnsavedChangesChoice::Save:
						workingRecord = editedRecord;
						if (!persistBackupsAutosaveRecord(workingRecord, errorText)) {
							postSetupFlowError("Installation / Backups / Autosave", errorText);
							break;
						}
						baselineRecord = workingRecord;
						running = false;
						break;
					case mr::dialogs::UnsavedChangesChoice::Discard:
						running = false;
						break;
					case mr::dialogs::UnsavedChangesChoice::Cancel:
						workingRecord = editedRecord;
						discardQueuedCancelEvent();
						break;
					default:
						break;
				}
				break;

			default:
				running = false;
				break;
		}
	}
}
