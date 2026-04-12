#define Uses_TApplication
#define Uses_TChDirDialog
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TFileDialog
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TButton
#define Uses_TColorGroup
#define Uses_TColorGroupList
#define Uses_TColorItem
#define Uses_TColorItemList
#define Uses_TColorDisplay
#define Uses_TColorSelector
#define Uses_TMonoSelector
#define Uses_TLabel
#define Uses_TDrawBuffer
#define Uses_TGroup
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TRadioButtons
#define Uses_TSItem
#define Uses_TView
#include <tvision/tv.h>

#include "MRSetupDialogs.hpp"

#include "../app/MRCommands.hpp"
#include "../app/TMREditorApp.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../ui/MRPalette.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/TMRMenuBar.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "MRUnsavedChangesDialog.hpp"
#include "MRSetupDialogCommon.hpp"
#include "MRNumericSlider.hpp"

#include <chrono>
#include <cctype>
#include <cstring>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
using mr::dialogs::execDialogRaw;
using mr::dialogs::execDialogRawWithData;
using mr::dialogs::ensureMrmacExtension;
using mr::dialogs::readRecordField;
using mr::dialogs::trimAscii;
using mr::dialogs::writeRecordField;
enum : ushort {
	cmMrSetupPathsHelp = 3800,
	cmMrSetupPathsBrowseSettingsUri,
	cmMrSetupPathsBrowseMacroPath,
	cmMrSetupPathsBrowseHelpUri,
	cmMrSetupPathsBrowseTempPath,
	cmMrSetupPathsBrowseShellUri,
	cmMrSetupBackupsAutosaveHelp = 3810,
	cmMrSetupBackupsAutosaveBrowseDirectory,
	cmMrSetupFieldChanged
};

enum {
	kPathFieldSize = 256,
	kBackupDirectoryFieldSize = 256,
	kBackupExtensionFieldSize = 32,
	kAutosaveNumberFieldSize = 16
};

struct PathsDialogRecord {
	char settingsMacroPath[kPathFieldSize];
	char macroDirectoryPath[kPathFieldSize];
	char helpFilePath[kPathFieldSize];
	char tempDirectoryPath[kPathFieldSize];
	char shellExecutablePath[kPathFieldSize];
};

enum : ushort {
	kBackupMethodChoiceOff = 0,
	kBackupMethodChoiceBakFile,
	kBackupMethodChoiceDirectory
};

enum : ushort {
	kBackupFrequencyChoiceFirstSaveOnly = 0,
	kBackupFrequencyChoiceEverySave
};

struct BackupsAutosaveDialogRecord {
	ushort backupMethodChoice;
	ushort backupFrequencyChoice;
	char backupExtension[kBackupExtensionFieldSize];
	char backupDirectory[kBackupDirectoryFieldSize];
	char autosaveInactivitySeconds[kAutosaveNumberFieldSize];
	char autosaveIntervalSeconds[kAutosaveNumberFieldSize];
};

bool validateBackupsAutosaveRecord(const BackupsAutosaveDialogRecord &record, std::string &errorText);

bool pathIsRegularFile(const std::string &path) {
	struct stat st;

	if (path.empty())
		return false;
	return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool confirmOverwriteForPath(const char *primaryLabel, const char *headline, const std::string &targetPath) {
	if (!pathIsRegularFile(targetPath))
		return true;
	return mr::dialogs::showUnsavedChangesDialog(primaryLabel, headline, targetPath.c_str()) ==
	       mr::dialogs::UnsavedChangesChoice::Save;
}


mr::messageline::Kind toSetupMessageLineKind(TMRMenuBar::MarqueeKind kind) {
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

void setSetupDialogStatus(const std::string &text, TMRMenuBar::MarqueeKind kind) {
	if (text.empty()) {
		mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
		return;
	}
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogValidation, text,
	                              toSetupMessageLineKind(kind), mr::messageline::kPriorityHigh);
}

void clearSetupDialogStatus() {
	mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
}

bool recordsEqual(const PathsDialogRecord &lhs, const PathsDialogRecord &rhs) {
	return readRecordField(lhs.settingsMacroPath) == readRecordField(rhs.settingsMacroPath) &&
	       readRecordField(lhs.macroDirectoryPath) == readRecordField(rhs.macroDirectoryPath) &&
	       readRecordField(lhs.helpFilePath) == readRecordField(rhs.helpFilePath) &&
	       readRecordField(lhs.tempDirectoryPath) == readRecordField(rhs.tempDirectoryPath) &&
	       readRecordField(lhs.shellExecutablePath) == readRecordField(rhs.shellExecutablePath);
}

MRSetupPaths pathsFromRecord(const PathsDialogRecord &record) {
	MRSetupPaths paths;
	paths.settingsMacroUri = normalizeConfiguredPathInput(readRecordField(record.settingsMacroPath));
	paths.macroPath = normalizeConfiguredPathInput(readRecordField(record.macroDirectoryPath));
	paths.helpUri = normalizeConfiguredPathInput(readRecordField(record.helpFilePath));
	paths.tempPath = normalizeConfiguredPathInput(readRecordField(record.tempDirectoryPath));
	paths.shellUri = normalizeConfiguredPathInput(readRecordField(record.shellExecutablePath));
	return paths;
}

void initPathsDialogRecord(PathsDialogRecord &record) {
	std::memset(&record, 0, sizeof(record));
	writeRecordField(record.settingsMacroPath, sizeof(record.settingsMacroPath), configuredSettingsMacroFilePath());
	writeRecordField(record.macroDirectoryPath, sizeof(record.macroDirectoryPath), defaultMacroDirectoryPath());
	writeRecordField(record.helpFilePath, sizeof(record.helpFilePath), configuredHelpFilePath());
	writeRecordField(record.tempDirectoryPath, sizeof(record.tempDirectoryPath), configuredTempDirectoryPath());
	writeRecordField(record.shellExecutablePath, sizeof(record.shellExecutablePath), configuredShellExecutablePath());
}

bool recordsEqual(const BackupsAutosaveDialogRecord &lhs, const BackupsAutosaveDialogRecord &rhs) {
	return lhs.backupMethodChoice == rhs.backupMethodChoice &&
	       lhs.backupFrequencyChoice == rhs.backupFrequencyChoice &&
	       readRecordField(lhs.backupExtension) == readRecordField(rhs.backupExtension) &&
	       readRecordField(lhs.backupDirectory) == readRecordField(rhs.backupDirectory) &&
	       readRecordField(lhs.autosaveInactivitySeconds) == readRecordField(rhs.autosaveInactivitySeconds) &&
	       readRecordField(lhs.autosaveIntervalSeconds) == readRecordField(rhs.autosaveIntervalSeconds);
}

ushort backupMethodChoiceFromSettings(const MRFileExtensionEditorSettings &settings) {
	const std::string method = trimAscii(settings.backupMethod);

	if (method == "DIRECTORY")
		return kBackupMethodChoiceDirectory;
	if (method == "OFF")
		return kBackupMethodChoiceOff;
	return kBackupMethodChoiceBakFile;
}

ushort backupFrequencyChoiceFromSettings(const MRFileExtensionEditorSettings &settings) {
	return trimAscii(settings.backupFrequency) == "EVERY_SAVE" ? kBackupFrequencyChoiceEverySave
	                                                    : kBackupFrequencyChoiceFirstSaveOnly;
}

std::string backupMethodLiteralFromChoice(ushort choice) {
	switch (choice) {
		case kBackupMethodChoiceDirectory:
			return "DIRECTORY";
		case kBackupMethodChoiceOff:
			return "OFF";
		case kBackupMethodChoiceBakFile:
		default:
			return "BAK_FILE";
	}
}

std::string backupFrequencyLiteralFromChoice(ushort choice) {
	return choice == kBackupFrequencyChoiceEverySave ? "EVERY_SAVE" : "FIRST_SAVE_ONLY";
}

void initBackupsAutosaveDialogRecord(BackupsAutosaveDialogRecord &record) {
	const MRFileExtensionEditorSettings settings = configuredFileExtensionEditorSettings();

	std::memset(&record, 0, sizeof(record));
	record.backupMethodChoice = backupMethodChoiceFromSettings(settings);
	record.backupFrequencyChoice = backupFrequencyChoiceFromSettings(settings);
	writeRecordField(record.backupExtension, sizeof(record.backupExtension), settings.backupExtension);
	writeRecordField(record.backupDirectory, sizeof(record.backupDirectory), settings.backupDirectory);
	writeRecordField(record.autosaveInactivitySeconds, sizeof(record.autosaveInactivitySeconds),
	                 std::to_string(settings.autosaveInactivitySeconds));
	writeRecordField(record.autosaveIntervalSeconds, sizeof(record.autosaveIntervalSeconds),
	                 std::to_string(settings.autosaveIntervalSeconds));
}

[[nodiscard]] bool parseNonNegativeIntegerField(const std::string &text, int &valueOut) {
	if (text.empty())
		return false;
	for (char ch : text)
		if (!std::isdigit(static_cast<unsigned char>(ch)))
			return false;
	char *end = nullptr;
	long value = std::strtol(text.c_str(), &end, 10);
	if (end == nullptr || *end != '\0' || value < 0 || value > INT_MAX)
		return false;
	valueOut = static_cast<int>(value);
	return true;
}

bool recordToBackupsAutosaveSettings(const BackupsAutosaveDialogRecord &record, MRFileExtensionEditorSettings &settings,
                                     std::string &errorText) {
	int numericValue = 0;
	std::string normalizedBackupExtension;

	if (!validateBackupsAutosaveRecord(record, errorText))
		return false;
	settings = configuredFileExtensionEditorSettings();
	settings.backupMethod = backupMethodLiteralFromChoice(record.backupMethodChoice);
	settings.backupFrequency = backupFrequencyLiteralFromChoice(record.backupFrequencyChoice);
	if (!normalizeBackupExtension(readRecordField(record.backupExtension), normalizedBackupExtension, &errorText))
		return false;
	settings.backupExtension = normalizedBackupExtension;
	settings.backupDirectory = normalizeConfiguredPathInput(readRecordField(record.backupDirectory));
	if (!parseNonNegativeIntegerField(trimAscii(readRecordField(record.autosaveInactivitySeconds)), numericValue)) {
		errorText = "Keyboard inactivity must be a non-negative integer.";
		return false;
	}
	settings.autosaveInactivitySeconds = numericValue;
	if (!parseNonNegativeIntegerField(trimAscii(readRecordField(record.autosaveIntervalSeconds)), numericValue)) {
		errorText = "Absolute interval must be a non-negative integer.";
		return false;
	}
	settings.autosaveIntervalSeconds = numericValue;
	errorText.clear();
	return true;
}

MRSetupPaths configuredSettingsWritePaths() {
	MRSetupPaths paths = resolveSetupPathDefaults();
	std::string value;

	value = configuredSettingsMacroFilePath();
	if (!value.empty())
		paths.settingsMacroUri = value;
	value = configuredMacroDirectoryPath();
	if (!value.empty())
		paths.macroPath = value;
	value = configuredHelpFilePath();
	if (!value.empty())
		paths.helpUri = value;
	value = configuredTempDirectoryPath();
	if (!value.empty())
		paths.tempPath = value;
	value = configuredShellExecutablePath();
	if (!value.empty())
		paths.shellUri = value;
	return paths;
}

bool sameBackupsAutosaveSettings(const MRFileExtensionEditorSettings &lhs, const MRFileExtensionEditorSettings &rhs) {
	return trimAscii(lhs.backupMethod) == trimAscii(rhs.backupMethod) &&
	       trimAscii(lhs.backupFrequency) == trimAscii(rhs.backupFrequency) &&
	       trimAscii(lhs.backupExtension) == trimAscii(rhs.backupExtension) &&
	       normalizeConfiguredPathInput(lhs.backupDirectory) == normalizeConfiguredPathInput(rhs.backupDirectory) &&
	       lhs.autosaveInactivitySeconds == rhs.autosaveInactivitySeconds &&
	       lhs.autosaveIntervalSeconds == rhs.autosaveIntervalSeconds;
}

bool persistBackupsAutosaveSettingsIfNeeded(const BackupsAutosaveDialogRecord &record, std::string &errorText) {
	MRFileExtensionEditorSettings currentSettings = configuredFileExtensionEditorSettings();
	MRFileExtensionEditorSettings updatedSettings;
	MRSettingsWriteReport writeReport;
	MRSetupPaths paths;

	if (!recordToBackupsAutosaveSettings(record, updatedSettings, errorText))
		return false;
	if (sameBackupsAutosaveSettings(updatedSettings, currentSettings)) {
		errorText.clear();
		return true;
	}
	if (!setConfiguredFileExtensionEditorSettings(updatedSettings, &errorText))
		return false;
	paths = configuredSettingsWritePaths();
	if (!writeSettingsMacroFile(paths, &errorText, &writeReport))
		return false;
	mrLogSettingsWriteReport("backups/autosave", writeReport);
	errorText.clear();
	return true;
}

bool validateBackupsAutosaveRecord(const BackupsAutosaveDialogRecord &record, std::string &errorText) {
	int numericValue = 0;
	std::string normalizedBackupExtension;
	std::string backupDirectory = normalizeConfiguredPathInput(readRecordField(record.backupDirectory));
	std::string inactivity = trimAscii(readRecordField(record.autosaveInactivitySeconds));
	std::string absoluteInterval = trimAscii(readRecordField(record.autosaveIntervalSeconds));

	if (!normalizeBackupExtension(readRecordField(record.backupExtension), normalizedBackupExtension, &errorText))
		return false;
	if (record.backupMethodChoice == kBackupMethodChoiceBakFile && normalizedBackupExtension.empty()) {
		errorText = "Backup file extension is required for 'create backup file'.";
		return false;
	}
	if (record.backupMethodChoice == kBackupMethodChoiceDirectory) {
		if (!validateBackupDirectoryPath(backupDirectory, &errorText))
			return false;
	}
	if (!parseNonNegativeIntegerField(inactivity, numericValue)) {
		errorText = "Keyboard inactivity must be a non-negative integer.";
		return false;
	}
	if (numericValue != 0 && (numericValue < 5 || numericValue > 100)) {
		errorText = "Keyboard inactivity must be 0 or within 5..100 seconds.";
		return false;
	}
	if (!parseNonNegativeIntegerField(absoluteInterval, numericValue)) {
		errorText = "Absolute interval must be a non-negative integer.";
		return false;
	}
	if (numericValue != 0 && (numericValue < 100 || numericValue > 300)) {
		errorText = "Absolute interval must be 0 or within 100..300 seconds.";
		return false;
	}
	errorText.clear();
	return true;
}

bool validatePathsRecord(const PathsDialogRecord &record, std::string &errorText) {
	std::string settingsPath = normalizeConfiguredPathInput(readRecordField(record.settingsMacroPath));
	std::string macroDir = normalizeConfiguredPathInput(readRecordField(record.macroDirectoryPath));
	std::string helpPath = normalizeConfiguredPathInput(readRecordField(record.helpFilePath));
	std::string tempDir = normalizeConfiguredPathInput(readRecordField(record.tempDirectoryPath));
	std::string shellPath = normalizeConfiguredPathInput(readRecordField(record.shellExecutablePath));

	if (!validateSettingsMacroFilePath(settingsPath, &errorText))
		return false;
	if (!validateMacroDirectoryPath(macroDir, &errorText))
		return false;
	if (!validateHelpFilePath(helpPath, &errorText))
		return false;
	if (!validateTempDirectoryPath(tempDir, &errorText))
		return false;
	if (!validateShellExecutablePath(shellPath, &errorText))
		return false;
	errorText.clear();
	return true;
}

bool sameSetupPaths(const MRSetupPaths &lhs, const MRSetupPaths &rhs) {
	return normalizeConfiguredPathInput(lhs.settingsMacroUri) == normalizeConfiguredPathInput(rhs.settingsMacroUri) &&
	       normalizeConfiguredPathInput(lhs.macroPath) == normalizeConfiguredPathInput(rhs.macroPath) &&
	       normalizeConfiguredPathInput(lhs.helpUri) == normalizeConfiguredPathInput(rhs.helpUri) &&
	       normalizeConfiguredPathInput(lhs.tempPath) == normalizeConfiguredPathInput(rhs.tempPath) &&
	       normalizeConfiguredPathInput(lhs.shellUri) == normalizeConfiguredPathInput(rhs.shellUri);
}

bool applySetupPathsToConfigured(const MRSetupPaths &paths, std::string &errorText) {
	if (!setConfiguredSettingsMacroFilePath(paths.settingsMacroUri, &errorText))
		return false;
	if (!setConfiguredMacroDirectoryPath(paths.macroPath, &errorText))
		return false;
	if (!setConfiguredHelpFilePath(paths.helpUri, &errorText))
		return false;
	if (!setConfiguredTempDirectoryPath(paths.tempPath, &errorText))
		return false;
	if (!setConfiguredShellExecutablePath(paths.shellUri, &errorText))
		return false;
	if (!setConfiguredLastFileDialogPath(paths.macroPath, &errorText))
		return false;
	errorText.clear();
	return true;
}

bool persistPathsSettingsIfNeeded(const PathsDialogRecord &record, std::string &errorText) {
	MRSetupPaths updatedPaths;
	MRSetupPaths currentPaths = configuredSettingsWritePaths();
	MRSettingsWriteReport writeReport;

	if (!validatePathsRecord(record, errorText))
		return false;
	updatedPaths = pathsFromRecord(record);
	if (sameSetupPaths(updatedPaths, currentPaths)) {
		errorText.clear();
		return true;
	}
	if (!applySetupPathsToConfigured(updatedPaths, errorText))
		return false;
	if (!writeSettingsMacroFile(updatedPaths, &errorText, &writeReport))
		return false;
	mrLogSettingsWriteReport("installation/setup paths", writeReport);
	return true;
}

std::string readCurrentWorkingDirectory() {
	char cwd[PATH_MAX];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr)
		return std::string();
	return std::string(cwd);
}

namespace {

void discardQueuedCancelEventsForTarget(TView *target) {
	TEvent event;

	if (target == nullptr)
		return;
	while (target->eventAvail()) {
		target->getEvent(event);
		if ((event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc)) ||
		    (event.what == evCommand && event.message.command == cmCancel))
			continue;
		target->putEvent(event);
		break;
	}
}

} // namespace

void discardQueuedCancelEvent() {
	discardQueuedCancelEventsForTarget(TProgram::application != nullptr ? static_cast<TView *>(TProgram::application)
	                                                           : static_cast<TView *>(TProgram::deskTop));
	discardQueuedCancelEventsForTarget(static_cast<TView *>(TProgram::deskTop));
}

bool browseUriWithFileDialog(const char *title, const std::string &currentValue, std::string &selectedUri) {
	char fileName[MAXPATH];
	std::string seed = trimAscii(currentValue);
	ushort result;

	initRememberedLoadDialogPath(fileName, sizeof(fileName), "*.*");
	if (!seed.empty())
		writeRecordField(fileName, sizeof(fileName), seed);
	result = execDialogRawWithData(new TFileDialog("*.*", title, "~N~ame", fdOKButton, 230), fileName);
	if (result == cmCancel) {
		discardQueuedCancelEvent();
		return false;
	}
	rememberLoadDialogPath(fileName);
	selectedUri = normalizeConfiguredPathInput(fileName);
	return true;
}

bool browsePathWithDirectoryDialog(const std::string &currentValue, std::string &selectedPath) {
	std::string originalCwd = readCurrentWorkingDirectory();
	std::string seed = normalizeConfiguredPathInput(trimAscii(currentValue));
	std::string picked;
	ushort result;

	if (!seed.empty())
		(void)::chdir(seed.c_str());
	result = execDialogRaw(new TChDirDialog(cdNormal, 231));
	picked = readCurrentWorkingDirectory();
	if (!originalCwd.empty())
		(void)::chdir(originalCwd.c_str());
	if (result == cmCancel) {
		discardQueuedCancelEvent();
		return false;
	}
	selectedPath = normalizeConfiguredPathInput(picked);
	return !selectedPath.empty();
}

bool chooseThemeFileForLoad(std::string &selectedUri) {
	char fileName[MAXPATH];
	ushort result;

	initRememberedLoadDialogPath(fileName, sizeof(fileName), "*.mrmac");
	result = execDialogRawWithData(new TFileDialog("*.mrmac", "Load color theme", "~N~ame", fdOKButton, 232),
	                               fileName);
	if (result == cmCancel) {
		discardQueuedCancelEvent();
		return false;
	}
	rememberLoadDialogPath(fileName);
	selectedUri = normalizeConfiguredPathInput(fileName);
	return true;
}

bool chooseThemeFileForSave(std::string &selectedUri) {
	char fileName[MAXPATH];
	ushort result;

	initRememberedLoadDialogPath(fileName, sizeof(fileName), "*.mrmac");
	result = execDialogRawWithData(new TFileDialog("*.mrmac", "Save color theme as", "~N~ame", fdOKButton, 233),
	                               fileName);
	if (result == cmCancel) {
		discardQueuedCancelEvent();
		return false;
	}
	rememberLoadDialogPath(fileName);
	selectedUri = normalizeConfiguredPathInput(ensureMrmacExtension(fileName));
	return true;
}

ushort execDialog(TDialog *dialog) {
	ushort result = execDialogRaw(dialog);
	if (result == cmHelp)
		static_cast<void>(mrShowProjectHelp());
	return result;
}

ushort execDialogWithDataCapture(TDialog *dialog, void *data) {
	ushort result = cmCancel;
	if (dialog == nullptr)
		return result;
	if (data != nullptr)
		dialog->setData(data);
	result = TProgram::deskTop->execView(dialog);
	if (data != nullptr)
		dialog->getData(data);
	TObject::destroy(dialog);
	if (result == cmHelp)
		static_cast<void>(mrShowProjectHelp());
	return result;
}

void applyDialogScrollbarSyncToPalette(TPalette &palette) {
	auto syncDialogScrollbarsToFrame = [&](int base) {
		palette[base + 3] = palette[base + 0];
		palette[base + 4] = palette[base + 0];
		palette[base + 23] = palette[base + 0];
		palette[base + 24] = palette[base + 0];
	};
	syncDialogScrollbarsToFrame(32);
	syncDialogScrollbarsToFrame(64);
	syncDialogScrollbarsToFrame(96);
}

TPalette buildColorSetupWorkingPalette() {
	static const TPalette basePalette = []() -> TPalette {
		static const int kBaseSlots = 135;
		static const int kTotalSlots = kMrPaletteMax;
		static const char cp[] = cpAppColor;
		TColorAttr data[kTotalSlots];
		int i = 0;

		for (i = 0; i < kBaseSlots; ++i)
			data[i] = static_cast<unsigned char>(cp[i]);
		data[kMrPaletteCurrentLine - 1] = data[10 - 1];
		data[kMrPaletteCurrentLineInBlock - 1] = data[12 - 1];
		data[kMrPaletteChangedText - 1] = data[14 - 1];
		data[kMrPaletteMessageError - 1] = data[42 - 1];
		data[kMrPaletteMessage - 1] = data[43 - 1];
		data[kMrPaletteMessageWarning - 1] = data[44 - 1];
		data[kMrPaletteMessageHero - 1] = data[43 - 1];
		data[kMrPaletteLineNumbers - 1] = data[9 - 1];
		return TPalette(data, static_cast<ushort>(kTotalSlots));
	}();
	TPalette palette = basePalette;
	unsigned char overrideValue = 0;

	for (int slot = 1; slot <= kMrPaletteMax; ++slot)
		if (configuredColorSlotOverride(static_cast<unsigned char>(slot), overrideValue))
			palette[slot] = overrideValue;
	applyDialogScrollbarSyncToPalette(palette);
	palette[1] = currentPalette.desktop;
	return palette;
}

bool applyWorkingColorPaletteToConfigured(const TPalette &palette, std::string &errorText) {
	static const MRColorSetupGroup groups[] = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog,
	                                           MRColorSetupGroup::Help, MRColorSetupGroup::Other};

	for (auto group : groups) {
		std::size_t count = 0;
		const MRColorSetupItem *items = colorSetupGroupItems(group, count);
		std::vector<unsigned char> values;

		if (items == nullptr || count == 0)
			continue;
		values.assign(count, 0);
		for (std::size_t i = 0; i < count; ++i)
			values[i] = static_cast<unsigned char>(palette[items[i].paletteIndex]);
		if (!setConfiguredColorSetupGroupValues(group, values.data(), values.size(), &errorText))
			return false;
	}

	errorText.clear();
	return true;
}

class TPathsSetupDialog : public MRScrollableDialog {
  public:
	class TInlineGlyphButton : public TView {
	  public:
		TInlineGlyphButton(const TRect &bounds, const char *glyph, ushort command)
		    : TView(bounds), glyphText(glyph != nullptr ? glyph : ""), commandCode(command) {
			options |= ofSelectable;
			options |= ofFirstClick;
			eventMask |= evMouseDown | evKeyDown;
		}

		void draw() override {
			TDrawBuffer b;
			ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
			int glyphWidth = strwidth(glyphText.c_str());
			int x = std::max(0, (size.x - glyphWidth) / 2);

			b.moveChar(0, ' ', color, size.x);
			b.moveStr(static_cast<ushort>(x), glyphText.c_str(), color, size.x - x);
			writeLine(0, 0, size.x, size.y, b);
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
			message(target != nullptr ? target : owner, evCommand, commandCode, this);
		}

		std::string glyphText;
		ushort commandCode;
	};

	TPathsSetupDialog(const PathsDialogRecord &baselineState, const PathsDialogRecord &initialState)
	    : TWindowInit(&TDialog::initFrame),
	      MRScrollableDialog(centeredSetupDialogRect(kVirtualDialogWidth, kVirtualDialogHeight),
	                         "PATHS", kVirtualDialogWidth, kVirtualDialogHeight),
	      baselineRecord(baselineState), workingRecord(initialState) {
		buildViews();
		loadFieldsFromRecord(workingRecord);
		initScrollIfNeeded();
		selectContent();
	}

	ushort run(PathsDialogRecord &outRecord, bool &changed) {
		ushort result = TProgram::deskTop->execView(this);
		saveFieldsToRecord(workingRecord);
		outRecord = workingRecord;
		changed = !recordsEqual(baselineRecord, workingRecord);
		return result;
	}

	void handleEvent(TEvent &event) override {
		MRScrollableDialog::handleEvent(event);

		if (event.what != evCommand)
			return;

		switch (event.message.command) {
			case cmMrSetupPathsHelp:
				endModal(event.message.command);
				clearEvent(event);
				return;

			case cmMrSetupPathsBrowseSettingsUri:
				browseSettingsMacroUri();
				clearEvent(event);
				return;

			case cmMrSetupPathsBrowseMacroPath:
				browseMacroPath();
				clearEvent(event);
				return;

			case cmMrSetupPathsBrowseHelpUri:
				browseHelpUri();
				clearEvent(event);
				return;

			case cmMrSetupPathsBrowseTempPath:
				browseTempPath();
				clearEvent(event);
				return;

			case cmMrSetupPathsBrowseShellUri:
				browseShellUri();
				clearEvent(event);
				return;

			default:
				return;
		}
	}

  private:
	TStaticText *addLabel(const TRect &rect, const char *text) {
		TStaticText *view = new TStaticText(rect, text);
		addManaged(view, rect);
		return view;
	}

	TInputLine *addInput(const TRect &rect) {
		TInputLine *view = new TInputLine(rect, kPathFieldSize - 1);
		addManaged(view, rect);
		return view;
	}

	TInlineGlyphButton *addBrowseButton(const TRect &rect, ushort command) {
		TInlineGlyphButton *view = new TInlineGlyphButton(rect, "🔎", command);
		view->options &= ~ofSelectable;
		addManaged(view, rect);
		return view;
	}

	TButton *addButton(const TRect &rect, const char *title, ushort command, ushort flags) {
		TButton *view = new TButton(rect, title, command, flags);
		addManaged(view, rect);
		return view;
	}

	void buildViews() {
		int dialogWidth = kVirtualDialogWidth;
		int inputLeft = 2;
		int glyphWidth = 2;
		int glyphRight = dialogWidth - 2;
		int glyphLeft = glyphRight - glyphWidth;
		int inputRight = glyphLeft;
		int doneLeft = dialogWidth / 2 - 17;
		int cancelLeft = doneLeft + 12;
		int helpLeft = cancelLeft + 14;
		int buttonTop = kVirtualDialogHeight - 3;

		addLabel(TRect(2, 2, dialogWidth - 2, 3), "Settings macro URI:");
		settingsMacroPathField = addInput(TRect(inputLeft, 3, inputRight, 4));
		addBrowseButton(TRect(glyphLeft, 3, glyphRight, 4), cmMrSetupPathsBrowseSettingsUri);

		addLabel(TRect(2, 5, dialogWidth - 2, 6), "Macro path (*.mrmac):");
		macroDirectoryPathField = addInput(TRect(inputLeft, 6, inputRight, 7));
		addBrowseButton(TRect(glyphLeft, 6, glyphRight, 7), cmMrSetupPathsBrowseMacroPath);

		addLabel(TRect(2, 8, dialogWidth - 2, 9), "Help file URI:");
		helpFilePathField = addInput(TRect(inputLeft, 9, inputRight, 10));
		addBrowseButton(TRect(glyphLeft, 9, glyphRight, 10), cmMrSetupPathsBrowseHelpUri);

		addLabel(TRect(2, 11, dialogWidth - 2, 12), "Temporary path:");
		tempDirectoryPathField = addInput(TRect(inputLeft, 12, inputRight, 13));
		addBrowseButton(TRect(glyphLeft, 12, glyphRight, 13), cmMrSetupPathsBrowseTempPath);

		addLabel(TRect(2, 14, dialogWidth - 2, 15), "Shell executable URI:");
		shellExecutablePathField = addInput(TRect(inputLeft, 15, inputRight, 16));
		addBrowseButton(TRect(glyphLeft, 15, glyphRight, 16), cmMrSetupPathsBrowseShellUri);

		addButton(TRect(doneLeft, buttonTop, doneLeft + 10, buttonTop + 2), "Done", cmOK, bfDefault);
		addButton(TRect(cancelLeft, buttonTop, cancelLeft + 12, buttonTop + 2), "Cancel", cmCancel,
		          bfNormal);
		addButton(TRect(helpLeft, buttonTop, helpLeft + 8, buttonTop + 2), "Help",
		          cmMrSetupPathsHelp, bfNormal);
	}

	static void setInputLineValue(TInputLine *inputLine, const char *value) {
		char buffer[kPathFieldSize];

		std::memset(buffer, 0, sizeof(buffer));
		writeRecordField(buffer, sizeof(buffer), readRecordField(value));
		inputLine->setData(buffer);
	}

	static void readInputLineValue(TInputLine *inputLine, char *dest, std::size_t destSize) {
		char buffer[kPathFieldSize];

		std::memset(buffer, 0, sizeof(buffer));
		inputLine->getData(buffer);
		writeRecordField(dest, destSize, readRecordField(buffer));
	}

	void loadFieldsFromRecord(const PathsDialogRecord &record) {
		setInputLineValue(settingsMacroPathField, record.settingsMacroPath);
		setInputLineValue(macroDirectoryPathField, record.macroDirectoryPath);
		setInputLineValue(helpFilePathField, record.helpFilePath);
		setInputLineValue(tempDirectoryPathField, record.tempDirectoryPath);
		setInputLineValue(shellExecutablePathField, record.shellExecutablePath);
	}

	void saveFieldsToRecord(PathsDialogRecord &record) {
		readInputLineValue(settingsMacroPathField, record.settingsMacroPath,
		                   sizeof(record.settingsMacroPath));
		readInputLineValue(macroDirectoryPathField, record.macroDirectoryPath,
		                   sizeof(record.macroDirectoryPath));
		readInputLineValue(helpFilePathField, record.helpFilePath, sizeof(record.helpFilePath));
		readInputLineValue(tempDirectoryPathField, record.tempDirectoryPath,
		                   sizeof(record.tempDirectoryPath));
		readInputLineValue(shellExecutablePathField, record.shellExecutablePath,
		                   sizeof(record.shellExecutablePath));
	}

	std::string currentInputValue(TInputLine *inputLine) {
		char buffer[kPathFieldSize];

		std::memset(buffer, 0, sizeof(buffer));
		inputLine->getData(buffer);
		return readRecordField(buffer);
	}

	void setInputValue(TInputLine *inputLine, const std::string &value) {
		char buffer[kPathFieldSize];

		std::memset(buffer, 0, sizeof(buffer));
		writeRecordField(buffer, sizeof(buffer), value);
		inputLine->setData(buffer);
	}

	void browseSettingsMacroUri() {
		std::string selected;
		if (browseUriWithFileDialog("Select settings macro URI", currentInputValue(settingsMacroPathField),
		                            selected))
			setInputValue(settingsMacroPathField, selected);
	}

	void browseMacroPath() {
		std::string selected;
		if (browsePathWithDirectoryDialog(currentInputValue(macroDirectoryPathField), selected))
			setInputValue(macroDirectoryPathField, selected);
	}

	void browseHelpUri() {
		std::string selected;
		if (browseUriWithFileDialog("Select help file URI", currentInputValue(helpFilePathField), selected))
			setInputValue(helpFilePathField, selected);
	}

	void browseTempPath() {
		std::string selected;
		if (browsePathWithDirectoryDialog(currentInputValue(tempDirectoryPathField), selected))
			setInputValue(tempDirectoryPathField, selected);
	}

	void browseShellUri() {
		std::string selected;
		if (browseUriWithFileDialog("Select shell executable URI", currentInputValue(shellExecutablePathField),
		                            selected))
			setInputValue(shellExecutablePathField, selected);
	}

	PathsDialogRecord baselineRecord;
	PathsDialogRecord workingRecord;
	static const int kVirtualDialogWidth = 84;
	static const int kVirtualDialogHeight = 23;
	TInputLine *settingsMacroPathField = nullptr;
	TInputLine *macroDirectoryPathField = nullptr;
	TInputLine *helpFilePathField = nullptr;
	TInputLine *tempDirectoryPathField = nullptr;
	TInputLine *shellExecutablePathField = nullptr;
};

void showPathsHelpDummyDialog() {
	std::vector<std::string> lines;
	lines.push_back("PATHS HELP");
	lines.push_back("");
	lines.push_back("Dummy help screen.");
	lines.push_back("Configure settings URI, macro path, help URI, temp path and shell URI.");
	lines.push_back("OK serializes settings.mrmac from the current settings state.");
	lines.push_back("Cancel asks for confirmation when fields were modified.");
	execDialog(createSetupSimplePreviewDialog("PATHS HELP", 74, 16, lines, false));
}

void runPathsSetupDialogFlow() {
	bool running = true;
	std::string errorText;
	PathsDialogRecord baselineRecord;
	PathsDialogRecord workingRecord;

	initPathsDialogRecord(baselineRecord);
	workingRecord = baselineRecord;
	while (running) {
		ushort result;
		bool changed = false;
		PathsDialogRecord editedRecord = workingRecord;
		TPathsSetupDialog *dialog = new TPathsSetupDialog(baselineRecord, workingRecord);

		if (dialog == nullptr)
			return;
		result = dialog->run(editedRecord, changed);
		TObject::destroy(dialog);

		switch (result) {
			case cmMrSetupPathsHelp:
				workingRecord = editedRecord;
				showPathsHelpDummyDialog();
				break;

			case cmOK:
				workingRecord = editedRecord;
				if (!changed) {
					running = false;
					break;
				}
				if (!persistPathsSettingsIfNeeded(workingRecord, errorText)) {
					messageBox(mfError | mfOKButton, "Installation / Paths\n\n%s", errorText.c_str());
					break;
				}
				running = false;
				break;

			case cmCancel:
				if (!changed) {
					running = false;
					break;
				}
				switch (mr::dialogs::showUnsavedChangesDialog(
				    "Save", "Path settings have unsaved changes.")) {
					case mr::dialogs::UnsavedChangesChoice::Save:
						workingRecord = editedRecord;
						if (!persistPathsSettingsIfNeeded(workingRecord, errorText)) {
							messageBox(mfError | mfOKButton, "Installation / Paths\n\n%s", errorText.c_str());
							break;
						}
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
class TNotifyingInputLine : public TInputLine {
  public:
	TNotifyingInputLine(const TRect &bounds, int maxLen, ushort changeCommand) noexcept
	    : TInputLine(bounds, maxLen), bufferCapacity(maxLen + 1), changeCommand(changeCommand) {
	}

	void handleEvent(TEvent &event) override {
		std::string beforeText = currentText();
		TView *target = owner;

		TInputLine::handleEvent(event);
		while (target != nullptr && dynamic_cast<TDialog *>(target) == nullptr)
			target = target->owner;

		if (currentText() != beforeText)
			message(target != nullptr ? target : owner, evBroadcast, changeCommand, this);
	}

  private:
	std::string currentText() const {
		std::vector<char> buffer(bufferCapacity, '\0');
		const_cast<TNotifyingInputLine *>(this)->getData(buffer.data());
		return std::string(buffer.data());
	}

	std::size_t bufferCapacity = 0;
	ushort changeCommand = 0;
};

void showBackupsAutosaveHelpDialog() {
	std::vector<std::string> lines;
	lines.push_back("BACKUPS & AUTOSAVE HELP");
	lines.push_back("");
	lines.push_back("This dialog edits global backup and autosave settings.");
	lines.push_back("Backup method covers Off, backup file and backup directory.");
	lines.push_back("OK serializes settings.mrmac from the current settings state.");
	lines.push_back("Autosave is modeled as keyboard inactivity and a fixed interval.");
	lines.push_back("A value of 0 turns the respective autosave trigger off.");
	execDialog(createSetupSimplePreviewDialog("BACKUPS & AUTOSAVE HELP", 88, 15, lines, false));
}

class TBackupsAutosaveSetupDialog : public MRScrollableDialog {
  public:
	class TInlineGlyphButton : public TView {
	  public:
		TInlineGlyphButton(const TRect &bounds, const char *glyph, ushort command)
		    : TView(bounds), glyphText(glyph != nullptr ? glyph : ""), commandCode(command) {
			options |= ofSelectable;
			options |= ofFirstClick;
			eventMask |= evMouseDown | evKeyDown;
		}

		void draw() override {
			TDrawBuffer b;
			ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
			int glyphWidth = strwidth(glyphText.c_str());
			int x = std::max(0, (size.x - glyphWidth) / 2);

			b.moveChar(0, ' ', color, size.x);
			b.moveStr(static_cast<ushort>(x), glyphText.c_str(), color, size.x - x);
			writeLine(0, 0, size.x, size.y, b);
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
			message(target != nullptr ? target : owner, evCommand, commandCode, this);
		}

		std::string glyphText;
		ushort commandCode;
	};

	TBackupsAutosaveSetupDialog(const BackupsAutosaveDialogRecord &baselineState,
	                           const BackupsAutosaveDialogRecord &initialState)
	    : TWindowInit(&TDialog::initFrame),
	      MRScrollableDialog(centeredSetupDialogRect(kVirtualDialogWidth, kVirtualDialogHeight),
	                         "BACKUPS & AUTOSAVE", kVirtualDialogWidth, kVirtualDialogHeight),
	      baselineRecord(baselineState), workingRecord(initialState) {
		buildViews();
		loadFieldsFromRecord(workingRecord);
		updateBackupFieldState();
		initScrollIfNeeded();
		selectContent();
		refreshValidationState();
	}

	~TBackupsAutosaveSetupDialog() override {
		clearSetupDialogStatus();
	}

	ushort run(BackupsAutosaveDialogRecord &outRecord, bool &changed) {
		ushort result = TProgram::deskTop->execView(this);
		saveFieldsToRecord(workingRecord);
		outRecord = workingRecord;
		changed = !recordsEqual(baselineRecord, workingRecord);
		return result;
	}

	void handleEvent(TEvent &event) override {
		ushort originalWhat = event.what;
		ushort originalCommand = (event.what == evCommand || event.what == evBroadcast) ? event.message.command : 0;
		void *originalInfoPtr = event.what == evBroadcast ? event.message.infoPtr : nullptr;
		ushort originalKey = event.what == evKeyDown ? event.keyDown.keyCode : 0;
		ushort backupMethod = currentBackupMethod();
		bool forwardTab = event.what == evKeyDown &&
		                 (event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI);
		bool backwardTab = event.what == evKeyDown && event.keyDown.keyCode == kbShiftTab;

		if (forwardTab) {
			if (current == backupFrequencyField) {
				if (backupMethod == kBackupMethodChoiceBakFile && backupExtensionField != nullptr)
					backupExtensionField->select();
				else if (inactivitySecondsSlider != nullptr)
					inactivitySecondsSlider->select();
				clearEvent(event);
				refreshValidationState();
				return;
			}
			if (current == backupExtensionField || current == backupDirectoryField ||
			    current == backupDirectoryBrowseButton) {
				if (inactivitySecondsSlider != nullptr)
					inactivitySecondsSlider->select();
				clearEvent(event);
				refreshValidationState();
				return;
			}
		}

		if (backwardTab) {
			if (current == inactivitySecondsSlider) {
				if (backupMethod == kBackupMethodChoiceBakFile && backupExtensionField != nullptr &&
				    (backupExtensionField->state & sfDisabled) == 0)
					backupExtensionField->select();
				else if (backupFrequencyField != nullptr)
					backupFrequencyField->select();
				clearEvent(event);
				refreshValidationState();
				return;
			}
			if (current == backupExtensionField || current == backupDirectoryField ||
			    current == backupDirectoryBrowseButton) {
				if (backupFrequencyField != nullptr)
					backupFrequencyField->select();
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
			if (!validState) {
				clearEvent(event);
				return;
			}
		}

		if (event.what == evCommand) {
			switch (event.message.command) {
				case cmMrSetupBackupsAutosaveHelp:
					endModal(event.message.command);
					clearEvent(event);
					return;

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

		if (originalWhat == evCommand || originalWhat == evKeyDown || originalWhat == evMouseDown ||
		    originalWhat == evMouseUp || originalWhat == evBroadcast)
			updateBackupFieldState();
		if (originalWhat == evCommand || originalWhat == evKeyDown || originalWhat == evMouseDown ||
		    originalWhat == evMouseUp || originalWhat == evBroadcast)
			refreshValidationState();
		if (originalWhat == evBroadcast &&
		    (originalCommand == cmReleasedFocus || originalCommand == cmReceivedFocus ||
		     originalCommand == cmMRNumericSliderChanged) &&
		    originalInfoPtr != backupDirectoryBrowseButton)
			refreshValidationState();
		if (originalWhat == evKeyDown &&
		    (originalKey == kbTab || originalKey == kbCtrlI || originalKey == kbShiftTab))
			refreshValidationState();
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

	MRNumericSlider *addNumericSlider(const TRect &rect, int32_t minValue, int32_t maxValue, int32_t initialValue,
	                                 int32_t step, int32_t pageStep) {
		MRNumericSlider *view = new MRNumericSlider(rect, minValue, maxValue, initialValue, step, pageStep,
		                                            MRNumericSlider::fmtRaw, cmMRNumericSliderChanged);
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

	TButton *addButton(const TRect &rect, const char *title, ushort command, ushort flags) {
		TButton *view = new TButton(rect, title, command, flags);
		addManaged(view, rect);
		return view;
	}

	void buildViews() {
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
		const int doneLeft = dialogWidth / 2 - 17;
		const int cancelLeft = doneLeft + 12;
		const int helpLeft = cancelLeft + 14;
		const int buttonTop = kVirtualDialogHeight - 3;

		addLabel(TRect(leftGroupLeft, 2, leftGroupRight, 3), "Backup method:");
		backupMethodField = addRadioGroup(TRect(leftGroupLeft, 3, leftGroupRight, 6),
		                                  new TSItem("~O~ff",
		                                             new TSItem("create ~B~ackup file",
		                                                        new TSItem("move to backup ~D~irectory", nullptr))));

		addLabel(TRect(rightGroupLeft, 2, rightGroupRight, 3), "Backup frequency:");
		backupFrequencyField = addRadioGroup(TRect(rightGroupLeft, 3, rightGroupRight, 5),
		                                     new TSItem("~F~irst save only",
		                                                new TSItem("E~v~ery save", nullptr)));

		addLabel(TRect(2, 7, textFieldLeft - 1, 8), "Backup extension:");
		backupExtensionField = addInput(TRect(textFieldLeft, 7, backupExtFieldRight, 8),
		                                kBackupExtensionFieldSize - 1);

		addLabel(TRect(2, 8, textFieldLeft - 1, 9), "Backup directory:");
		backupDirectoryField = addInput(TRect(textFieldLeft, 8, backupPathFieldRight, 9),
		                                kBackupDirectoryFieldSize - 1);
		backupDirectoryBrowseButton =
		    addBrowseButton(TRect(glyphLeft, 8, glyphRight, 9), cmMrSetupBackupsAutosaveBrowseDirectory);

		addLabel(TRect(2, 10, dialogWidth - 2, 11), "Autosave in seconds, 0 = OFF:");
		addLabel(TRect(2, 11, textFieldLeft - 1, 12), "Keyboard inactivity:");
		inactivitySecondsSlider = addNumericSlider(TRect(textFieldLeft, 11, autosaveFieldRight, 12), 0, 100,
		                                           15, 5, 10);
		addLabel(TRect(2, 12, textFieldLeft - 1, 13), "Autosave interval:");
		absoluteIntervalSlider = addNumericSlider(TRect(textFieldLeft, 12, autosaveFieldRight, 13), 0, 300,
		                                         180, 10, 50);

		doneButton = addButton(TRect(doneLeft, buttonTop, doneLeft + 10, buttonTop + 2), "O~K~", cmOK, bfDefault);
		addButton(TRect(cancelLeft, buttonTop, cancelLeft + 12, buttonTop + 2), "~C~ancel", cmCancel,
		          bfNormal);
		addButton(TRect(helpLeft, buttonTop, helpLeft + 8, buttonTop + 2), "~H~elp",
		          cmMrSetupBackupsAutosaveHelp, bfNormal);
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

	void setValidationState(bool valid, const std::string &errorText) {
		validState = valid;
		if (doneButton != nullptr) {
			const bool wasDisabled = (doneButton->state & sfDisabled) != 0;
			const bool shouldDisable = !valid;
			if (wasDisabled != shouldDisable) {
				doneButton->setState(sfDisabled, shouldDisable ? True : False);
				doneButton->drawView();
			}
		}
		if (valid) {
			lastValidationText.clear();
			clearSetupDialogStatus();
		} else if (errorText != lastValidationText) {
			setSetupDialogStatus(errorText, TMRMenuBar::MarqueeKind::Warning);
			lastValidationText = errorText;
		}
	}

	void refreshValidationState() {
		std::string errorText;

		saveFieldsToRecord(workingRecord);
		setValidationState(validateBackupsAutosaveRecord(workingRecord, errorText), errorText);
	}

	static int parseSliderValueOrDefault(const char *value, int fallback, int minimumEnabled, int maximumEnabled) {
		int parsed = fallback;
		if (!parseNonNegativeIntegerField(trimAscii(readRecordField(value)), parsed))
			parsed = fallback;
		if (parsed == 0)
			return 0;
		if (parsed < minimumEnabled)
			return minimumEnabled;
		if (parsed > maximumEnabled)
			return maximumEnabled;
		return parsed;
	}

	static void writeSliderValue(MRNumericSlider *slider, char *dest, std::size_t destSize, int fallback,
	                            int minimumEnabled, int maximumEnabled) {
		int32_t value = fallback;
		if (slider != nullptr)
			slider->getData(&value);
		int normalized = static_cast<int>(value);
		if (normalized != 0)
			normalized = std::clamp(normalized, minimumEnabled, maximumEnabled);
		writeRecordField(dest, destSize, std::to_string(normalized));
	}

	std::string currentInputValue(TInputLine *inputLine, std::size_t capacity) const {
		std::vector<char> buffer(capacity, '\0');
		if (inputLine == nullptr)
			return std::string();
		inputLine->getData(buffer.data());
		return readRecordField(buffer.data());
	}

	void setInputValue(TInputLine *inputLine, std::size_t capacity, const std::string &value) {
		std::vector<char> buffer(capacity, '\0');
		writeRecordField(buffer.data(), buffer.size(), value);
		inputLine->setData(buffer.data());
	}

	void loadFieldsFromRecord(const BackupsAutosaveDialogRecord &record) {
		if (backupMethodField != nullptr)
			backupMethodField->setData((void *)&record.backupMethodChoice);
		if (backupFrequencyField != nullptr)
			backupFrequencyField->setData((void *)&record.backupFrequencyChoice);
		setInputLineValue(backupExtensionField, record.backupExtension, sizeof(record.backupExtension));
		setInputLineValue(backupDirectoryField, record.backupDirectory, sizeof(record.backupDirectory));
		if (inactivitySecondsSlider != nullptr) {
			int32_t value = parseSliderValueOrDefault(record.autosaveInactivitySeconds, 15, 5, 100);
			inactivitySecondsSlider->setData(&value);
		}
		if (absoluteIntervalSlider != nullptr) {
			int32_t value = parseSliderValueOrDefault(record.autosaveIntervalSeconds, 180, 100, 300);
			absoluteIntervalSlider->setData(&value);
		}
	}

	void saveFieldsToRecord(BackupsAutosaveDialogRecord &record) {
		if (backupMethodField != nullptr)
			backupMethodField->getData((void *)&record.backupMethodChoice);
		if (backupFrequencyField != nullptr)
			backupFrequencyField->getData((void *)&record.backupFrequencyChoice);
		readInputLineValue(backupExtensionField, record.backupExtension, sizeof(record.backupExtension));
		readInputLineValue(backupDirectoryField, record.backupDirectory, sizeof(record.backupDirectory));
		writeSliderValue(inactivitySecondsSlider, record.autosaveInactivitySeconds, sizeof(record.autosaveInactivitySeconds), 15,
		                5, 100);
		writeSliderValue(absoluteIntervalSlider, record.autosaveIntervalSeconds,
		                sizeof(record.autosaveIntervalSeconds), 180, 100, 300);
	}

	ushort currentBackupMethod() const {
		ushort method = kBackupMethodChoiceOff;
		if (backupMethodField != nullptr)
			backupMethodField->getData((void *)&method);
		return method;
	}

	void updateBackupFieldState() {
		ushort method = currentBackupMethod();
		const bool extensionEnabled = method == kBackupMethodChoiceBakFile;
		const bool pathEnabled = method == kBackupMethodChoiceDirectory;
		const bool frequencyEnabled = method != kBackupMethodChoiceOff;
		if (backupFrequencyField != nullptr)
			backupFrequencyField->setState(sfDisabled, frequencyEnabled ? False : True);
		if (backupExtensionField != nullptr)
			backupExtensionField->setState(sfDisabled, extensionEnabled ? False : True);
		if (backupDirectoryField != nullptr)
			backupDirectoryField->setState(sfDisabled, pathEnabled ? False : True);
		if (backupDirectoryBrowseButton != nullptr)
			backupDirectoryBrowseButton->setState(sfDisabled, pathEnabled ? False : True);
	}

	bool mouseHitsBackupBrowseButton(TEvent &event) {
		return event.what == evMouseDown && backupDirectoryBrowseButton != nullptr &&
		       (backupDirectoryBrowseButton->state & sfDisabled) == 0 &&
		       backupDirectoryBrowseButton->containsMouse(event);
	}

	void browseBackupDirectory() {
		std::string selected;
		if (browsePathWithDirectoryDialog(currentInputValue(backupDirectoryField, kBackupDirectoryFieldSize), selected))
			setInputValue(backupDirectoryField, kBackupDirectoryFieldSize, selected);
		if (backupDirectoryField != nullptr)
			backupDirectoryField->select();
	}

	BackupsAutosaveDialogRecord baselineRecord;
	BackupsAutosaveDialogRecord workingRecord;
	static const int kVirtualDialogWidth = 96;
	static const int kVirtualDialogHeight = 17;
	bool validState = true;
	std::string lastValidationText;
	TRadioButtons *backupMethodField = nullptr;
	TRadioButtons *backupFrequencyField = nullptr;
	TInputLine *backupExtensionField = nullptr;
	TInputLine *backupDirectoryField = nullptr;
	TView *backupDirectoryBrowseButton = nullptr;
	MRNumericSlider *inactivitySecondsSlider = nullptr;
	MRNumericSlider *absoluteIntervalSlider = nullptr;
	TButton *doneButton = nullptr;
};

void runBackupsAutosaveDialogFlow() {
	bool running = true;
	std::string errorText;
	BackupsAutosaveDialogRecord baselineRecord;
	BackupsAutosaveDialogRecord workingRecord;

	initBackupsAutosaveDialogRecord(baselineRecord);
	workingRecord = baselineRecord;

	while (running) {
		ushort result;
		bool changed = false;
		BackupsAutosaveDialogRecord editedRecord = workingRecord;
		TBackupsAutosaveSetupDialog *dialog = new TBackupsAutosaveSetupDialog(baselineRecord, workingRecord);

		if (dialog == nullptr)
			return;
		result = dialog->run(editedRecord, changed);
		TObject::destroy(dialog);

		switch (result) {
			case cmMrSetupBackupsAutosaveHelp:
				workingRecord = editedRecord;
				showBackupsAutosaveHelpDialog();
				break;

			case cmOK:
				workingRecord = editedRecord;
				if (!changed) {
					running = false;
					break;
				}
				if (!persistBackupsAutosaveSettingsIfNeeded(workingRecord, errorText)) {
					messageBox(mfError | mfOKButton, "Installation / Backups / Autosave\n\n%s", errorText.c_str());
					break;
				}
				running = false;
				break;

			case cmCancel:
				if (!changed) {
					running = false;
					break;
				}
				switch (mr::dialogs::showUnsavedChangesDialog(
				    "Save", "Backup and autosave settings have unsaved changes.")) {
					case mr::dialogs::UnsavedChangesChoice::Save:
						workingRecord = editedRecord;
						if (!persistBackupsAutosaveSettingsIfNeeded(workingRecord, errorText)) {
							messageBox(mfError | mfOKButton,
							           "Installation / Backups / Autosave\n\n%s", errorText.c_str());
							break;
						}
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
} // namespace

void runColorSetupDialogFlow() {
	auto persistSettingsFileOnly = [](std::string &errorText) -> bool {
		MRSetupPaths paths = resolveSetupPathDefaults();
		paths.settingsMacroUri = configuredSettingsMacroFilePath();
		paths.macroPath = defaultMacroDirectoryPath();
		paths.helpUri = configuredHelpFilePath();
		paths.tempPath = configuredTempDirectoryPath();
		paths.shellUri = configuredShellExecutablePath();
		MRSettingsWriteReport writeReport;
		if (!writeSettingsMacroFile(paths, &errorText, &writeReport))
			return false;
		mrLogSettingsWriteReport("color setup", writeReport);
		return true;
	};

	bool running = true;
	std::string errorText;
	TPalette pendingPalette = buildColorSetupWorkingPalette();
	bool havePendingPalette = false;

	while (running) {
		TPalette baselinePalette = buildColorSetupWorkingPalette();
		TPalette workingPalette = havePendingPalette ? pendingPalette : baselinePalette;
		ushort result = execDialogWithDataCapture(createColorSetupDialog(), &workingPalette);

		switch (result) {
			case cmOK:
				if (!applyWorkingColorPaletteToConfigured(workingPalette, errorText)) {
					messageBox(mfError | mfOKButton, "Installation / Color setup\n\n%s", errorText.c_str());
					break;
				}
				if (!persistSettingsFileOnly(errorText)) {
					messageBox(mfError | mfOKButton, "Color Setup / Save settings\n\n%s", errorText.c_str());
					break;
				}
				TProgram::application->redraw();
				havePendingPalette = false;
				running = false;
				break;

			case cmMrColorLoadTheme: {
				std::string themeUri;

				if (!chooseThemeFileForLoad(themeUri))
					break;
				if (!loadColorThemeFile(themeUri, &errorText)) {
					messageBox(mfError | mfOKButton, "Color Setup / Load Theme\n\n%s", errorText.c_str());
					break;
				}
				if (!persistSettingsFileOnly(errorText)) {
					messageBox(mfError | mfOKButton, "Color Setup / Save settings\n\n%s", errorText.c_str());
					break;
				}
				TProgram::application->redraw();
				break;
			}

			case cmMrColorSaveTheme: {
				std::string themeUri;
				std::string activeThemeUri = normalizeConfiguredPathInput(configuredColorThemeFilePath());
				bool overwriteActiveTheme = false;

				if (!chooseThemeFileForSave(themeUri))
					break;
				if (!confirmOverwriteForPath("Overwrite", "Theme file exists. Overwrite?", themeUri))
					break;
				overwriteActiveTheme =
				    normalizeConfiguredPathInput(themeUri) == activeThemeUri;
				if (!applyWorkingColorPaletteToConfigured(workingPalette, errorText)) {
					messageBox(mfError | mfOKButton, "Color Setup / Save Theme\n\n%s", errorText.c_str());
					break;
				}
				if (!writeColorThemeFile(themeUri, &errorText)) {
					messageBox(mfError | mfOKButton, "Color Setup / Save Theme\n\n%s", errorText.c_str());
					break;
				}
				if (overwriteActiveTheme) {
					if (!persistSettingsFileOnly(errorText)) {
						messageBox(mfError | mfOKButton, "Color Setup / Save settings\n\n%s", errorText.c_str());
						break;
					}
					TProgram::application->redraw();
				}
				pendingPalette = workingPalette;
				havePendingPalette = true;
				break;
			}

			case cmCancel: {
				// Color setup uses explicit commit actions only (OK / Load Theme / Save Theme).
				// Cancel always discards in-dialog edits without a dirty prompt.
				havePendingPalette = false;
				running = false;
				break;
			}

			default:
				havePendingPalette = false;
				running = false;
				break;
		}
	}
}

bool mrSaveColorThemeFromWorkingPaletteForTesting(const TPalette &workingPalette,
                                                  const std::string &themeUri,
                                                  std::string *errorMessage) {
	std::string errorText;
	MRSetupPaths paths = resolveSetupPathDefaults();

	if (!applyWorkingColorPaletteToConfigured(workingPalette, errorText)) {
		if (errorMessage != nullptr)
			*errorMessage = errorText;
		return false;
	}
	if (!writeColorThemeFile(themeUri, &errorText)) {
		if (errorMessage != nullptr)
			*errorMessage = errorText;
		return false;
	}

	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	MRSettingsWriteReport writeReport;
	if (!writeSettingsMacroFile(paths, &errorText, &writeReport)) {
		if (errorMessage != nullptr)
			*errorMessage = errorText;
		return false;
	}
	mrLogSettingsWriteReport("save theme + sync settings", writeReport);

	if (errorMessage != nullptr)
		errorMessage->clear();
	return true;
}

void runInstallationAndSetupDialogFlow() {
	bool running = true;

	while (running) {
		ushort result = execDialog(createInstallationAndSetupDialog());
		switch (result) {
			case cmMrSetupEditSettings:
			case cmMrSetupFilenameExtensions:
				runFileExtensionProfilesDialogFlow();
				break;

			case cmMrSetupColorSetup:
				runColorSetupDialogFlow();
				break;

			case cmMrSetupSwappingEmsXms:
				runPathsSetupDialogFlow();
				break;

			case cmMrSetupBackupsTempAutosave:
				runBackupsAutosaveDialogFlow();
				break;

			case cmMrSetupSearchAndReplaceDefaults:
				messageBox(mfInformation | mfOKButton,
				           "Installation / Search and Replace defaults\n\nDummy implementation for now.");
				break;

			case cmCancel:
			default:
				running = false;
				break;
		}
	}
}


// ---- Consolidated from MRSetupDialogCommon.cpp ----

namespace {

class TSetupDialogContentGroup : public TGroup {
  public:
	explicit TSetupDialogContentGroup(const TRect &bounds) : TGroup(bounds) {
	}

	void draw() override {
		TDrawBuffer buffer;
		TColorAttr color = owner != nullptr ? owner->mapColor(1) : mapColor(1);
		TView *child = first();

		buffer.moveChar(0, ' ', color, size.x);
		for (short y = 0; y < size.y; ++y)
			writeLine(0, y, size.x, 1, buffer);
		drawSubViews(child, nullptr);
	}
};

} // namespace

TGroup *createSetupDialogContentGroup(const TRect &bounds) {
	return new TSetupDialogContentGroup(bounds);
}

MRScrollableDialog::MRScrollableDialog(const TRect &bounds, const char *title, int virtualWidth,
                                       int virtualHeight)
    : TWindowInit(&TDialog::initFrame), TDialog(bounds, title), virtualWidth(virtualWidth),
      virtualHeight(virtualHeight), contentRect(1, 1, size.x - 1, size.y - 1) {
	contentGroup = createSetupDialogContentGroup(contentRect);
	if (contentGroup != nullptr) {
		contentGroup->options |= ofSelectable;
		insert(contentGroup);
	}
}

void MRScrollableDialog::addManaged(TView *view, const TRect &base) {
	ManagedItem item;
	item.view = view;
	item.base = base;
	managedViews.push_back(item);
	if (contentGroup != nullptr) {
		TRect local = base;
		local.move(-contentRect.a.x, -contentRect.a.y);
		view->locate(local);
		contentGroup->insert(view);
	} else
		insert(view);
}

void MRScrollableDialog::selectContent() {
	if (contentGroup != nullptr) {
		contentGroup->resetCurrent();
		contentGroup->select();
	}
}

void MRScrollableDialog::scrollToOrigin() {
	if (horizontalScrollBar != nullptr)
		horizontalScrollBar->setValue(0);
	if (verticalScrollBar != nullptr)
		verticalScrollBar->setValue(0);
	applyScroll();
}

void MRScrollableDialog::initScrollIfNeeded() {
	int virtualContentWidth = std::max(1, virtualWidth - 2);
	int virtualContentHeight = std::max(1, virtualHeight - 2);
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

	contentRect = TRect(1, 1, size.x - 1, size.y - 1);
	if (contentRect.b.x <= contentRect.a.x)
		contentRect.b.x = contentRect.a.x + 1;
	if (contentRect.b.y <= contentRect.a.y)
		contentRect.b.y = contentRect.a.y + 1;
	if (contentGroup != nullptr)
		contentGroup->locate(contentRect);

	if (needH) {
		TRect hRect(1, size.y - 1, size.x - 1, size.y);
		if (horizontalScrollBar == nullptr) {
			horizontalScrollBar = new TScrollBar(hRect);
			insert(horizontalScrollBar);
		} else
			horizontalScrollBar->locate(hRect);
	}
	if (needV) {
		TRect vRect(size.x - 1, 1, size.x, size.y - 1);
		if (verticalScrollBar == nullptr) {
			verticalScrollBar = new TScrollBar(vRect);
			insert(verticalScrollBar);
		} else
			verticalScrollBar->locate(vRect);
	}
	if (horizontalScrollBar != nullptr) {
		int maxDx = std::max(0, virtualContentWidth - std::max(1, contentRect.b.x - contentRect.a.x));
		horizontalScrollBar->setParams(0, 0, maxDx,
		                       std::max(1, (contentRect.b.x - contentRect.a.x) / 2), 1);
	}
	if (verticalScrollBar != nullptr) {
		int maxDy = std::max(0, virtualContentHeight - std::max(1, contentRect.b.y - contentRect.a.y));
		verticalScrollBar->setParams(0, 0, maxDy,
		                       std::max(1, (contentRect.b.y - contentRect.a.y) / 2), 1);
	}
	applyScroll();
}

void MRScrollableDialog::handleEvent(TEvent &event) {
	if (event.what == evKeyDown) {
		ushort keyCode = event.keyDown.keyCode;

		if (keyCode == kbEsc) {
			endModal(cmCancel);
			clearEvent(event);
			return;
		}
		if (contentGroup != nullptr) {
			if (keyCode == kbTab || keyCode == kbCtrlI) {
				contentGroup->selectNext(False);
				ensureCurrentVisible();
				clearEvent(event);
				return;
			}
			if (keyCode == kbShiftTab) {
				contentGroup->selectNext(True);
				ensureCurrentVisible();
				clearEvent(event);
				return;
			}
		}
	}

	TDialog::handleEvent(event);
	if (event.what == evBroadcast && event.message.command == cmScrollBarChanged &&
	    (event.message.infoPtr == horizontalScrollBar || event.message.infoPtr == verticalScrollBar)) {
		applyScroll();
		clearEvent(event);
		return;
	}
	if (event.what == evKeyDown || event.what == evCommand || event.what == evMouseDown || event.what == evMouseUp)
		ensureCurrentVisible();
}

void MRScrollableDialog::ensureViewVisible(TView *view) {
	if (view == nullptr || contentGroup == nullptr)
		return;
	for (const auto &managedView : managedViews)
		if (managedView.view == view) {
			int dx = horizontalScrollBar != nullptr ? horizontalScrollBar->value : 0;
			int dy = verticalScrollBar != nullptr ? verticalScrollBar->value : 0;
			int viewportWidth = std::max(1, contentRect.b.x - contentRect.a.x);
			int viewportHeight = std::max(1, contentRect.b.y - contentRect.a.y);
			int left = managedView.base.a.x - contentRect.a.x;
			int right = managedView.base.b.x - contentRect.a.x;
			int top = managedView.base.a.y - contentRect.a.y;
			int bottom = managedView.base.b.y - contentRect.a.y;

			if (horizontalScrollBar != nullptr) {
				if (left < dx)
					horizontalScrollBar->setValue(std::max(0, left));
				else if (right > dx + viewportWidth)
					horizontalScrollBar->setValue(std::max(0, right - viewportWidth));
			}
			if (verticalScrollBar != nullptr) {
				if (top < dy)
					verticalScrollBar->setValue(std::max(0, top));
				else if (bottom > dy + viewportHeight)
					verticalScrollBar->setValue(std::max(0, bottom - viewportHeight));
			}
			applyScroll();
			return;
		}
}

void MRScrollableDialog::ensureCurrentVisible() {
	TView *view = contentGroup != nullptr ? contentGroup->current : nullptr;

	while (view != nullptr) {
		TGroup *group = dynamic_cast<TGroup *>(view);
		if (group == nullptr || group->current == nullptr)
			break;
		view = group->current;
	}
	ensureViewVisible(view);
}

void MRScrollableDialog::applyScroll() {
	int dx = horizontalScrollBar != nullptr ? horizontalScrollBar->value : 0;
	int dy = verticalScrollBar != nullptr ? verticalScrollBar->value : 0;

	for (auto &managedView : managedViews) {
		TRect moved = managedView.base;
		moved.move(-dx, -dy);
		moved.move(-contentRect.a.x, -contentRect.a.y);
		managedView.view->locate(moved);
	}
	if (contentGroup != nullptr)
		contentGroup->drawView();
}

TRect centeredSetupDialogRect(int width, int height) {
	TRect r = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, 80, 25);
	int availableWidth = std::max(1, r.b.x - r.a.x);
	int availableHeight = std::max(1, r.b.y - r.a.y);
	int safeWidth = std::max(10, std::min(width, availableWidth));
	int safeHeight = std::max(6, std::min(height, availableHeight));
	int left = r.a.x + std::max(0, (availableWidth - safeWidth) / 2);
	int top = r.a.y + std::max(0, (availableHeight - safeHeight) / 2);

	return TRect(left, top, left + safeWidth, top + safeHeight);
}

void insertSetupStaticLine(TDialog *dialog, int x, int y, const char *text) {
	dialog->insert(new TStaticText(TRect(x, y, x + std::strlen(text) + 1, y + 1), text));
}

TDialog *createSetupSimplePreviewDialog(const char *title, int width, int height,
                                        const std::vector<std::string> &lines,
                                        bool showOkCancelHelp) {
	MRScrollableDialog *dialog =
	    new MRScrollableDialog(centeredSetupDialogRect(width, height), title, width, height);
	int y = 2;

	if (dialog == nullptr)
		return nullptr;
	for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it, ++y) {
		TRect lineRect(2, y, 2 + std::strlen(it->c_str()) + 1, y + 1);
		dialog->addManaged(new TStaticText(lineRect, it->c_str()), lineRect);
	}

	if (showOkCancelHelp) {
		TRect okRect(width - 34, height - 3, width - 24, height - 1);
		TRect cancelRect(width - 23, height - 3, width - 10, height - 1);
		TRect helpRect(width - 9, height - 3, width - 2, height - 1);
		dialog->addManaged(new TButton(okRect, "OK", cmOK, bfDefault), okRect);
		dialog->addManaged(new TButton(cancelRect, "Cancel", cmCancel, bfNormal), cancelRect);
		dialog->addManaged(new TButton(helpRect, "Help", cmHelp, bfNormal), helpRect);
	} else {
		TRect doneRect(width / 2 - 4, height - 3, width / 2 + 4, height - 1);
		dialog->addManaged(new TButton(doneRect, "Done", cmOK, bfDefault), doneRect);
	}

	dialog->initScrollIfNeeded();
	dialog->selectContent();
	return dialog;
}
