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
#define Uses_TListViewer
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TRadioButtons
#define Uses_TView
#define Uses_TWindow
#define Uses_TCheckBoxes
#define Uses_TSItem
#include <tvision/tv.h>

#include "MRSetup.hpp"

#include "../app/MRCommands.hpp"
#include "../config/MRDialogPaths.hpp"
#include "../keymap/MRKeymapContext.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRPalette.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRScopedHistoryUI.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "MRDirtyGating.hpp"
#include "MRKeymapManager.hpp"
#include "MRSetupCommon.hpp"
#include "MRNumericSlider.hpp"
#include "../app/commands/MRWindowCommands.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
TFrame *initSetupDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

using mr::dialogs::ensureMrmacExtension;
using mr::dialogs::execDialogRaw;
using mr::dialogs::execDialogRawWithData;
using mr::dialogs::readRecordField;

using mr::dialogs::writeRecordField;

enum : ushort {
	cmMrSetupPathsHelp = 3800,
	cmMrSetupPathsBrowseSettingsUri,
	cmMrSetupPathsBrowseMacroPath,
	cmMrSetupPathsBrowseHelpUri,
	cmMrSetupPathsBrowseTempPath,
	cmMrSetupPathsBrowseShellUri,
	cmMrSetupPathsBrowseLogUri,
	cmMrSetupBackupsAutosaveHelp = 3810,
	cmMrSetupBackupsAutosaveBrowseDirectory,
	cmMrSetupFieldChanged
};

enum {
	kPathFieldSize = 256,
	kBackupDirectoryFieldSize = 256,
	kBackupExtensionFieldSize = 32,
	kAutosaveNumberFieldSize = 16,
	kHistoryNumberFieldSize = 16
};

struct PathsDialogRecord {
	char settingsMacroPath[kPathFieldSize];
	char macroDirectoryPath[kPathFieldSize];
	char helpFilePath[kPathFieldSize];
	char tempDirectoryPath[kPathFieldSize];
	char shellExecutablePath[kPathFieldSize];
	char logFilePath[kPathFieldSize];
	ushort logHandlingChoice;
	char maxPathHistory[kHistoryNumberFieldSize];
	char maxFileHistory[kHistoryNumberFieldSize];
};

ushort logHandlingChoiceFrom(MRLogHandling handling) {
	switch (handling) {
		case MRLogHandling::Volatile:
			return 0;
		case MRLogHandling::Persist:
			return 1;
		case MRLogHandling::Journalctl:
			return 2;
	}
	return 0;
}

MRLogHandling logHandlingFromChoice(ushort choice) {
	switch (choice) {
		case 1:
			return MRLogHandling::Persist;
		case 2:
			return MRLogHandling::Journalctl;
		default:
			return MRLogHandling::Volatile;
	}
}

enum : ushort {
	kBackupMethodOff = 0,
	kBackupMethodBakFile,
	kBackupMethodDirectory
};

enum : ushort {
	kBackupFrequencyFirstSaveOnly = 0,
	kBackupFrequencyEverySave
};

struct BackupsAutosaveDialogRecord {
	ushort backupMethodChoice;
	ushort backupFrequencyChoice;
	char backupFileExtension[kBackupExtensionFieldSize];
	char backupDirectoryPath[kBackupDirectoryFieldSize];
	char inactivitySeconds[kAutosaveNumberFieldSize];
	char absoluteIntervalSeconds[kAutosaveNumberFieldSize];
};

bool pathIsRegularFile(const std::string &path) {
	struct stat st;

	if (path.empty()) return false;
	return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool pathIsDirectory(const std::string &path) {
	struct stat st;

	if (path.empty()) return false;
	return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string parentDirectoryOfPath(std::string_view path) {
	std::string normalized = normalizeConfiguredPathInput(trimAscii(path));
	const std::size_t slashPos = normalized.find_last_of('/');

	if (normalized.empty() || slashPos == std::string::npos) return std::string();
	if (slashPos == 0) return "/";
	return normalized.substr(0, slashPos);
}

std::string resolveFileDialogSeedDirectory(MRDialogHistoryScope scope, const char *buffer) {
	std::string seedPath = normalizeConfiguredPathInput(trimAscii(buffer != nullptr ? buffer : ""));

	if (seedPath.empty()) seedPath = configuredLastFileDialogPath(scope);
	if (seedPath.empty()) seedPath = configuredLastFileDialogFilePath(scope);
	if (pathIsDirectory(seedPath)) return seedPath;
	return parentDirectoryOfPath(seedPath);
}

bool deferRememberingLoadDialogPath(MRDialogHistoryScope scope) {
	switch (scope) {
		case MRDialogHistoryScope::OpenFile:
		case MRDialogHistoryScope::LoadFile:
		case MRDialogHistoryScope::BlockLoad:
		case MRDialogHistoryScope::MacroFile:
		case MRDialogHistoryScope::KeymapProfileLoad:
		case MRDialogHistoryScope::WorkspaceLoad:
		case MRDialogHistoryScope::SetupThemeLoad:
			return true;
		default:
			return false;
	}
}

std::string dialogSeedFileName(std::string_view value) {
	const std::string trimmed = trimAscii(value);
	const std::string normalized = normalizeConfiguredPathInput(trimmed);
	const std::size_t sep = normalized.find_last_of('/');

	if (!normalized.empty()) {
		if (sep == std::string::npos) return normalized;
		return normalized.substr(sep + 1);
	}
	if (trimmed.find('/') != std::string::npos || trimmed.find('\\') != std::string::npos) {
		const std::size_t rawSep = trimmed.find_last_of("/\\");
		if (rawSep == std::string::npos) return trimmed;
		return trimmed.substr(rawSep + 1);
	}
	return trimmed;
}

bool confirmOverwriteForPath(const char *primaryLabel, const char *headline, const std::string &targetPath) {
	if (!pathIsRegularFile(targetPath)) return true;
	return mr::dialogs::showUnsavedChangesDialog(primaryLabel, headline, targetPath.c_str()) == mr::dialogs::UnsavedChangesChoice::Save;
}

TAttrPair configuredColorOr(TView *view, unsigned char paletteSlot, ushort fallbackColorIndex) {
	unsigned char biosAttr = 0;

	if (configuredColorSlotOverride(paletteSlot, biosAttr)) return TAttrPair(biosAttr);
	return view != nullptr ? view->getColor(fallbackColorIndex) : TAttrPair(0x70);
}

mr::messageline::Kind toSetupMessageLineKind(MRMenuBar::MarqueeKind kind) {
	switch (kind) {
		case MRMenuBar::MarqueeKind::Success:
			return mr::messageline::Kind::Success;
		case MRMenuBar::MarqueeKind::Warning:
			return mr::messageline::Kind::Warning;
		case MRMenuBar::MarqueeKind::Error:
			return mr::messageline::Kind::Error;
		case MRMenuBar::MarqueeKind::Hero:
		case MRMenuBar::MarqueeKind::Info:
		default:
			return mr::messageline::Kind::Info;
	}
}

void setSetupDialogStatus(const std::string &text, MRMenuBar::MarqueeKind kind) {
	if (text.empty()) {
		mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
		return;
	}
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogValidation, text, toSetupMessageLineKind(kind), mr::messageline::kPriorityHigh);
}

void clearSetupDialogStatus() {
	mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
}

bool startsWithDoneButtonCaption(const char *title) {
	std::string normalized;

	if (title == nullptr) return false;
	for (const char *p = title; *p != '\0'; ++p) {
		const unsigned char ch = static_cast<unsigned char>(*p);
		if (ch == '~' || std::isspace(ch)) continue;
		if (ch == '<') break;
		normalized.push_back(static_cast<char>(std::toupper(ch)));
	}
	return normalized.rfind("DONE", 0) == 0;
}

std::string visibleButtonCaption(const char *title) {
	std::string text;

	if (title == nullptr) return text;
	for (const char *p = title; *p != '\0'; ++p)
		if (*p != '~') text.push_back(*p);
	return trimAscii(text);
}

int buttonCaptionWidth(const char *title) {
	return static_cast<int>(visibleButtonCaption(title).size());
}

void postSetupFlowError(const char *scope, const std::string &errorText) {
	if (errorText.empty()) return;
	std::string text = scope != nullptr ? std::string(scope) + ": " + errorText : errorText;
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

bool recordsEqual(const PathsDialogRecord &lhs, const PathsDialogRecord &rhs) {
	return readRecordField(lhs.settingsMacroPath) == readRecordField(rhs.settingsMacroPath) && readRecordField(lhs.macroDirectoryPath) == readRecordField(rhs.macroDirectoryPath) && readRecordField(lhs.helpFilePath) == readRecordField(rhs.helpFilePath) && readRecordField(lhs.tempDirectoryPath) == readRecordField(rhs.tempDirectoryPath) && readRecordField(lhs.shellExecutablePath) == readRecordField(rhs.shellExecutablePath) && readRecordField(lhs.logFilePath) == readRecordField(rhs.logFilePath) && lhs.logHandlingChoice == rhs.logHandlingChoice && readRecordField(lhs.maxPathHistory) == readRecordField(rhs.maxPathHistory) && readRecordField(lhs.maxFileHistory) == readRecordField(rhs.maxFileHistory);
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
	writeRecordField(record.logFilePath, sizeof(record.logFilePath), configuredLogFilePath());
	record.logHandlingChoice = logHandlingChoiceFrom(configuredLogHandling());
	writeRecordField(record.maxPathHistory, sizeof(record.maxPathHistory), std::to_string(configuredMaxPathHistory()));
	writeRecordField(record.maxFileHistory, sizeof(record.maxFileHistory), std::to_string(configuredMaxFileHistory()));
}

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
	return left.method == right.method && left.frequency == right.frequency && left.extension == right.extension && left.directory == right.directory && left.inactivity == right.inactivity && left.interval == right.interval;
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

[[nodiscard]] bool parseNonNegativeIntegerField(const std::string &text, int &valueOut) {
	if (text.empty()) return false;
	for (char ch : text)
		if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
	char *end = nullptr;
	long value = std::strtol(text.c_str(), &end, 10);
	if (end == nullptr || *end != '\0' || value < 0 || value > INT_MAX) return false;
	valueOut = static_cast<int>(value);
	return true;
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
	return lhs.backupMethod == rhs.backupMethod && lhs.backupFrequency == rhs.backupFrequency && lhs.backupExtension == rhs.backupExtension && lhs.backupDirectory == rhs.backupDirectory && lhs.autosaveInactivitySeconds == rhs.autosaveInactivitySeconds && lhs.autosaveIntervalSeconds == rhs.autosaveIntervalSeconds;
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

bool validatePathsRecord(const PathsDialogRecord &record, std::string &errorText) {
	std::string settingsPath = normalizeConfiguredPathInput(readRecordField(record.settingsMacroPath));
	std::string macroDir = normalizeConfiguredPathInput(readRecordField(record.macroDirectoryPath));
	std::string helpPath = normalizeConfiguredPathInput(readRecordField(record.helpFilePath));
	std::string tempDir = normalizeConfiguredPathInput(readRecordField(record.tempDirectoryPath));
	std::string shellPath = normalizeConfiguredPathInput(readRecordField(record.shellExecutablePath));
	std::string logFile = normalizeConfiguredPathInput(readRecordField(record.logFilePath));
	int maxPathHistory = 0;
	int maxFileHistory = 0;

	if (!validateSettingsMacroFilePath(settingsPath, &errorText)) return false;
	if (!validateMacroDirectoryPath(macroDir, &errorText)) return false;
	if (!validateHelpFilePath(helpPath, &errorText)) return false;
	if (!validateTempDirectoryPath(tempDir, &errorText)) return false;
	if (!validateShellExecutablePath(shellPath, &errorText)) return false;
	if (logHandlingFromChoice(record.logHandlingChoice) == MRLogHandling::Persist && !validateLogFilePath(logFile, &errorText)) return false;
	if (!parseNonNegativeIntegerField(trimAscii(readRecordField(record.maxPathHistory)), maxPathHistory) || maxPathHistory < 5 || maxPathHistory > 50) {
		errorText = "MAX_PATH_HISTORY must be within 5..50.";
		return false;
	}
	if (!parseNonNegativeIntegerField(trimAscii(readRecordField(record.maxFileHistory)), maxFileHistory) || maxFileHistory < 5 || maxFileHistory > 50) {
		errorText = "MAX_FILE_HISTORY must be within 5..50.";
		return false;
	}
	errorText.clear();
	return true;
}

bool saveAndReloadPathsRecord(const PathsDialogRecord &record, std::string &errorText) {
	MRSetupPaths paths = pathsFromRecord(record);
	MRSettingsWriteReport writeReport;
	MRSetupPaths dummyPaths = resolveSetupPathDefaults();
	const std::string originalSettingsPath = configuredSettingsMacroFilePath();
	const std::string originalMacroPath = defaultMacroDirectoryPath();
	const std::string originalHelpPath = configuredHelpFilePath();
	const std::string originalTempPath = configuredTempDirectoryPath();
	const std::string originalShellPath = configuredShellExecutablePath();
	const int originalMaxPathHistory = configuredMaxPathHistory();
	const int originalMaxFileHistory = configuredMaxFileHistory();
	const MRLogHandling originalLogHandling = configuredLogHandling();
	const std::string originalLogFile = configuredLogFilePath();
	const std::string maxPathHistoryText = trimAscii(readRecordField(record.maxPathHistory));
	const std::string maxFileHistoryText = trimAscii(readRecordField(record.maxFileHistory));
	const std::string logFileText = normalizeConfiguredPathInput(readRecordField(record.logFilePath));
	const MRLogHandling newLogHandling = logHandlingFromChoice(record.logHandlingChoice);

	if (!validatePathsRecord(record, errorText)) return false;
	if (!setConfiguredSettingsMacroFilePath(paths.settingsMacroUri, &errorText)) return false;
	if (!setConfiguredMacroDirectoryPath(paths.macroPath, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		return false;
	}
	if (!setConfiguredHelpFilePath(paths.helpUri, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		return false;
	}
	if (!setConfiguredTempDirectoryPath(paths.tempPath, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		return false;
	}
	if (!setConfiguredShellExecutablePath(paths.shellUri, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		(void)setConfiguredTempDirectoryPath(originalTempPath, nullptr);
		return false;
	}
	if (!setConfiguredLogHandling(newLogHandling, &errorText)) return false;
	if (!setConfiguredLogFilePath(logFileText, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		(void)setConfiguredTempDirectoryPath(originalTempPath, nullptr);
		(void)setConfiguredShellExecutablePath(originalShellPath, nullptr);
		(void)setConfiguredLogHandling(originalLogHandling, nullptr);
		return false;
	}
	if (!applyConfiguredSettingsAssignment("MAX_PATH_HISTORY", maxPathHistoryText, dummyPaths, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		(void)setConfiguredTempDirectoryPath(originalTempPath, nullptr);
		(void)setConfiguredShellExecutablePath(originalShellPath, nullptr);
		(void)setConfiguredLogHandling(originalLogHandling, nullptr);
		(void)setConfiguredLogFilePath(originalLogFile, nullptr);
		return false;
	}
	if (!applyConfiguredSettingsAssignment("MAX_FILE_HISTORY", maxFileHistoryText, dummyPaths, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		(void)setConfiguredTempDirectoryPath(originalTempPath, nullptr);
		(void)setConfiguredShellExecutablePath(originalShellPath, nullptr);
		(void)setConfiguredLogHandling(originalLogHandling, nullptr);
		(void)setConfiguredLogFilePath(originalLogFile, nullptr);
		(void)applyConfiguredSettingsAssignment("MAX_PATH_HISTORY", std::to_string(originalMaxPathHistory), dummyPaths, nullptr);
		return false;
	}
	if (!writeSettingsMacroFile(paths, &errorText, &writeReport)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		(void)setConfiguredTempDirectoryPath(originalTempPath, nullptr);
		(void)setConfiguredShellExecutablePath(originalShellPath, nullptr);
		(void)setConfiguredLogHandling(originalLogHandling, nullptr);
		(void)setConfiguredLogFilePath(originalLogFile, nullptr);
		(void)applyConfiguredSettingsAssignment("MAX_PATH_HISTORY", std::to_string(originalMaxPathHistory), dummyPaths, nullptr);
		(void)applyConfiguredSettingsAssignment("MAX_FILE_HISTORY", std::to_string(originalMaxFileHistory), dummyPaths, nullptr);
		return false;
	}
	mrLogSettingsWriteReport("installation/setup paths", writeReport);

	errorText.clear();
	return true;
}

std::string readCurrentWorkingDirectory() {
	char cwd[PATH_MAX];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr) return std::string();
	return std::string(cwd);
}

namespace {

void discardQueuedCancelEventsForTarget(TView *target) {
	TEvent event;

	if (target == nullptr) return;
	while (target->eventAvail()) {
		target->getEvent(event);
		if ((event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc)) || (event.what == evCommand && event.message.command == cmCancel)) continue;
		target->putEvent(event);
		break;
	}
}

} // namespace

void discardQueuedCancelEvent() {
	discardQueuedCancelEventsForTarget(TProgram::application != nullptr ? static_cast<TView *>(TProgram::application) : static_cast<TView *>(TProgram::deskTop));
	discardQueuedCancelEventsForTarget(static_cast<TView *>(TProgram::deskTop));
}

bool browseUriWithFileDialog(MRDialogHistoryScope scope, const char *title, std::string &selectedUri) {
	char fileName[MAXPATH];
	ushort result;

	mr::dialogs::seedFileDialogPath(scope, fileName, sizeof(fileName), "*.*");
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.*", title, "~N~ame", fdOKButton, fileName);
	if (result == cmCancel) {
		discardQueuedCancelEvent();
		return false;
	}
	selectedUri = normalizeConfiguredPathInput(fileName);
	return true;
}

bool browsePathWithDirectoryDialog(MRDialogHistoryScope scope, std::string &selectedPath) {
	std::string originalCwd = readCurrentWorkingDirectory();
	std::string seed = configuredLastFileDialogPath(scope);
	std::string picked;
	ushort result;

	if (!seed.empty()) (void)::chdir(seed.c_str());
	result = execDialogRaw(mr::dialogs::createDirectoryDialog(scope, cdNormal));
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

bool chooseThemeFileForLoad(MRDialogHistoryScope scope, std::string &selectedUri) {
	char fileName[MAXPATH];
	ushort result;

	mr::dialogs::seedFileDialogPath(scope, fileName, sizeof(fileName), "*.mrmac");
	mr::dialogs::suggestFileDialogName(fileName, sizeof(fileName), configuredColorThemeFilePath());
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.mrmac", "Load color theme", "~N~ame", fdOKButton, fileName);
	if (result == cmCancel) {
		discardQueuedCancelEvent();
		return false;
	}
	selectedUri = normalizeConfiguredPathInput(fileName);
	return true;
}

bool chooseThemeFileForSave(MRDialogHistoryScope scope, std::string &selectedUri) {
	char fileName[MAXPATH];
	ushort result;

	mr::dialogs::seedFileDialogPath(scope, fileName, sizeof(fileName), "*.mrmac");
	mr::dialogs::suggestFileDialogName(fileName, sizeof(fileName), configuredColorThemeFilePath());
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.mrmac", "Save color theme as", "~N~ame", fdOKButton, fileName);
	if (result == cmCancel) {
		discardQueuedCancelEvent();
		return false;
	}
	selectedUri = normalizeConfiguredPathInput(ensureMrmacExtension(fileName));
	if (!selectedUri.empty()) rememberLoadDialogPath(scope, selectedUri.c_str());
	return true;
}

ushort execDialog(TDialog *dialog) {
	ushort result = execDialogRaw(dialog);
	if (result == cmHelp) static_cast<void>(mrShowProjectHelp());
	return result;
}

ushort execDialogWithDataCapture(TDialog *dialog, void *data) {
	ushort result = cmCancel;
	MRDialogFoundation *foundation = nullptr;
	MRScrollableDialog *scrollable = nullptr;
	if (dialog == nullptr) return result;
	if (data != nullptr) dialog->setData(data);
	foundation = dynamic_cast<MRDialogFoundation *>(dialog);
	if (foundation != nullptr) foundation->finalizeLayout();
	else {
		scrollable = dynamic_cast<MRScrollableDialog *>(dialog);
		if (scrollable != nullptr) scrollable->initScrollIfNeeded();
	}
	result = TProgram::deskTop->execView(dialog);
	if (data != nullptr) dialog->getData(data);
	TObject::destroy(dialog);
	if (result == cmHelp) static_cast<void>(mrShowProjectHelp());
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
		data[kMrPaletteEofMarker - 1] = data[14 - 1];
		data[kMrPaletteCursorPositionMarker - 1] = data[3 - 1];
		data[kMrPaletteDialogInactiveElements - 1] = data[62 - 1];
		data[kMrPaletteMiniMapNormal - 1] = data[13 - 1];
		data[kMrPaletteMiniMapViewport - 1] = data[11 - 1];
		data[kMrPaletteMiniMapChanged - 1] = data[14 - 1];
		data[kMrPaletteMiniMapFindMarker - 1] = data[5 - 1];
		data[kMrPaletteMiniMapErrorMarker - 1] = data[42 - 1];
		data[kMrPaletteCodeFolding - 1] = data[9 - 1];
		data[kMrPaletteDesktop - 1] = 0x90;
		data[kMrPaletteVirtualDesktopMarker - 1] = 0x9F;
		return TPalette(data, static_cast<ushort>(kTotalSlots));
	}();
	TPalette palette = basePalette;
	unsigned char overrideValue = 0;

	for (int slot = 1; slot <= kMrPaletteMax; ++slot)
		if (configuredColorSlotOverride(static_cast<unsigned char>(slot), overrideValue)) palette[slot] = overrideValue;
	applyDialogScrollbarSyncToPalette(palette);
	palette[1] = palette[kMrPaletteDesktop];
	return palette;
}

bool applyWorkingColorPaletteToConfigured(const TPalette &palette, std::string &errorText) {
	static const MRColorSetupGroup groups[] = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog, MRColorSetupGroup::Help, MRColorSetupGroup::Other, MRColorSetupGroup::MiniMap};

	for (auto group : groups) {
		std::size_t count = 0;
		const MRColorSetupItem *items = colorSetupGroupItems(group, count);
		std::vector<unsigned char> values;

		if (items == nullptr || count == 0) continue;
		values.assign(count, 0);
		for (std::size_t i = 0; i < count; ++i)
			values[i] = static_cast<unsigned char>(palette[items[i].paletteIndex]);
		if (!setConfiguredColorSetupGroupValues(group, values.data(), values.size(), &errorText)) return false;
	}

	errorText.clear();
	return true;
}

class TPathsSetupDialog : public MRScrollableDialog {
  public:
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

	class TInactiveInputLine : public TInputLine {
	  public:
		TInactiveInputLine(const TRect &bounds, int maxLen) noexcept : TInputLine(bounds, maxLen) {
		}

		void draw() override {
			if ((state & sfDisabled) == 0) {
				TInputLine::draw();
				return;
			}

			TDrawBuffer buffer;
			TAttrPair color = configuredColorOr(this, kMrPaletteDialogInactiveElements, 1);

			buffer.moveChar(0, ' ', color, size.x);
			if (size.x > 1) buffer.moveStr(1, data, color, size.x - 1, firstPos);
			writeLine(0, 0, size.x, size.y, buffer);
			setCursor(0, 0);
		}

		void setState(ushort aState, Boolean enable) override {
			const ushort oldState = state;

			TInputLine::setState(aState, enable);
			if (oldState != state && (aState & (sfFocused | sfDisabled | sfSelected | sfActive))) drawView();
		}
	};

	class TInlineGlyphButton : public TView {
	  public:
		TInlineGlyphButton(const TRect &bounds, const char *glyph, ushort command) : TView(bounds), mGlyph(glyph != nullptr ? glyph : ""), mCommand(command) {
			options |= ofSelectable;
			options |= ofFirstClick;
			eventMask |= evMouseDown | evKeyDown;
		}

		void draw() override {
			TDrawBuffer b;
			ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
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

	TPathsSetupDialog(const PathsDialogRecord &initialRecord) : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(kVirtualDialogWidth, kVirtualDialogHeight), "PATHS", kVirtualDialogWidth, kVirtualDialogHeight, initSetupDialogFrame), mCurrentRecord(initialRecord) {
		buildViews();
		loadFieldsFromRecord(mCurrentRecord);
		setDialogValidationHook([this]() { return validateDialogValues(); });
		initScrollIfNeeded();
		selectContent();
	}

	ushort run(PathsDialogRecord &outRecord) {
		ushort result = TProgram::deskTop->execView(this);
		outRecord = collectRecordFromFields();
		return result;
	}

	void handleEvent(TEvent &event) override {
		MRScrollableDialog::handleEvent(event);
		updateLogFileFieldState();

		if (event.what != evCommand) return;

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

			case cmMrSetupPathsBrowseLogUri:
				browseLogUri();
				clearEvent(event);
				return;

			default:
				return;
		}
	}

  private:
	TInactiveStaticText *addLabel(const TRect &rect, const char *text) {
		TInactiveStaticText *view = new TInactiveStaticText(rect, text);
		addManaged(view, rect);
		return view;
	}

	TInactiveInputLine *addInput(const TRect &rect) {
		TInactiveInputLine *view = new TInactiveInputLine(rect, kPathFieldSize - 1);
		addManaged(view, rect);
		return view;
	}

	TRadioButtons *addRadioGroup(const TRect &rect, TSItem *items) {
		TRadioButtons *view = new TRadioButtons(rect, items);
		addManaged(view, rect);
		return view;
	}

	MRNumericSlider *addNumericSlider(const TRect &rect, int32_t minValue, int32_t maxValue, int32_t initialValue, int32_t step, int32_t pageStep) {
		MRNumericSlider *view = new MRNumericSlider(rect, minValue, maxValue, initialValue, step, pageStep, MRNumericSlider::fmtRaw, cmMRNumericSliderChanged);
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
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmMrSetupPathsHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
		int dialogWidth = kVirtualDialogWidth;
		int labelLeft = 2;
		int labelRight = 23;
		int inputLeft = 25;
		int glyphWidth = 2;
		int glyphRight = dialogWidth - 2;
		int glyphLeft = glyphRight - glyphWidth;
		int inputRight = glyphLeft;
		int buttonLeft = (dialogWidth - metrics.rowWidth) / 2;
		int buttonTop = kVirtualDialogHeight - 3;

		addLabel(TRect(labelLeft, 2, labelRight, 3), "Settings macro URI: ");
		mSettingsMacroPathField = addInput(TRect(inputLeft, 2, inputRight, 3));
		addBrowseButton(TRect(glyphLeft, 2, glyphRight, 3), cmMrSetupPathsBrowseSettingsUri);

		addLabel(TRect(labelLeft, 4, labelRight, 5), "Macro path (*.mrmac): ");
		mMacroDirectoryPathField = addInput(TRect(inputLeft, 4, inputRight, 5));
		addBrowseButton(TRect(glyphLeft, 4, glyphRight, 5), cmMrSetupPathsBrowseMacroPath);

		addLabel(TRect(labelLeft, 6, labelRight, 7), "Help file URI: ");
		mHelpFilePathField = addInput(TRect(inputLeft, 6, inputRight, 7));
		addBrowseButton(TRect(glyphLeft, 6, glyphRight, 7), cmMrSetupPathsBrowseHelpUri);

		addLabel(TRect(labelLeft, 8, labelRight, 9), "Temporary path: ");
		mTempDirectoryPathField = addInput(TRect(inputLeft, 8, inputRight, 9));
		addBrowseButton(TRect(glyphLeft, 8, glyphRight, 9), cmMrSetupPathsBrowseTempPath);

		addLabel(TRect(labelLeft, 10, labelRight, 11), "Shell executable URI: ");
		mShellExecutablePathField = addInput(TRect(inputLeft, 10, inputRight, 11));
		addBrowseButton(TRect(glyphLeft, 10, glyphRight, 11), cmMrSetupPathsBrowseShellUri);

		mLogFilePathLabel = addLabel(TRect(labelLeft, 12, labelRight, 13), "Logfile URI: ");
		mLogFilePathField = addInput(TRect(inputLeft, 12, inputRight, 13));
		mLogFilePathBrowseButton = addBrowseButton(TRect(glyphLeft, 12, glyphRight, 13), cmMrSetupPathsBrowseLogUri);

		addLabel(TRect(labelLeft, 14, labelRight, 15), "Path history: ");
		mMaxPathHistorySlider = addNumericSlider(TRect(inputLeft, 14, inputRight, 15), 5, 50, 15, 1, 5);

		addLabel(TRect(labelLeft, 16, labelRight, 17), "File history: ");
		mMaxFileHistorySlider = addNumericSlider(TRect(inputLeft, 16, inputRight, 17), 5, 50, 15, 1, 5);

		addLabel(TRect(labelLeft, 18, dialogWidth - 2, 19), "Log handling:");
		mLogHandlingField = addRadioGroup(TRect(labelLeft, 19, labelLeft + 20, 22), new TSItem("~V~olatile log", new TSItem("~L~og to file", new TSItem("Use ~J~ournalctl", nullptr))));

		mr::dialogs::addManagedUniformButtonRow(*this, buttonLeft, buttonTop, 2, buttons);
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

	static int parseHistorySliderValueOrDefault(const char *value, int fallback) {
		int parsed = fallback;
		if (!parseNonNegativeIntegerField(trimAscii(readRecordField(value)), parsed)) parsed = fallback;
		return std::clamp(parsed, 5, 50);
	}

	void loadFieldsFromRecord(const PathsDialogRecord &record) {
		setInputLineValue(mSettingsMacroPathField, record.settingsMacroPath);
		setInputLineValue(mMacroDirectoryPathField, record.macroDirectoryPath);
		setInputLineValue(mHelpFilePathField, record.helpFilePath);
		setInputLineValue(mTempDirectoryPathField, record.tempDirectoryPath);
		setInputLineValue(mShellExecutablePathField, record.shellExecutablePath);
		setInputLineValue(mLogFilePathField, record.logFilePath);
		if (mLogHandlingField != nullptr) mLogHandlingField->setData((void *)&record.logHandlingChoice);
		if (mMaxPathHistorySlider != nullptr) {
			int32_t value = parseHistorySliderValueOrDefault(record.maxPathHistory, 15);
			mMaxPathHistorySlider->setData(&value);
		}
		if (mMaxFileHistorySlider != nullptr) {
			int32_t value = parseHistorySliderValueOrDefault(record.maxFileHistory, 15);
			mMaxFileHistorySlider->setData(&value);
		}
		updateLogFileFieldState();
	}

	void saveFieldsToRecord(PathsDialogRecord &record) const {
		int32_t maxPathHistory = 15;
		int32_t maxFileHistory = 15;
		readInputLineValue(mSettingsMacroPathField, record.settingsMacroPath, sizeof(record.settingsMacroPath));
		readInputLineValue(mMacroDirectoryPathField, record.macroDirectoryPath, sizeof(record.macroDirectoryPath));
		readInputLineValue(mHelpFilePathField, record.helpFilePath, sizeof(record.helpFilePath));
		readInputLineValue(mTempDirectoryPathField, record.tempDirectoryPath, sizeof(record.tempDirectoryPath));
		readInputLineValue(mShellExecutablePathField, record.shellExecutablePath, sizeof(record.shellExecutablePath));
		readInputLineValue(mLogFilePathField, record.logFilePath, sizeof(record.logFilePath));
		if (mLogHandlingField != nullptr) mLogHandlingField->getData((void *)&record.logHandlingChoice);
		if (mMaxPathHistorySlider != nullptr) mMaxPathHistorySlider->getData(&maxPathHistory);
		if (mMaxFileHistorySlider != nullptr) mMaxFileHistorySlider->getData(&maxFileHistory);
		writeRecordField(record.maxPathHistory, sizeof(record.maxPathHistory), std::to_string(std::clamp(static_cast<int>(maxPathHistory), 5, 50)));
		writeRecordField(record.maxFileHistory, sizeof(record.maxFileHistory), std::to_string(std::clamp(static_cast<int>(maxFileHistory), 5, 50)));
	}

	PathsDialogRecord collectRecordFromFields() const {
		PathsDialogRecord record = mCurrentRecord;
		saveFieldsToRecord(record);
		return record;
	}

	std::string currentInputValue(TInputLine *inputLine) {
		char buffer[kPathFieldSize];

		std::memset(buffer, 0, sizeof(buffer));
		inputLine->getData(buffer);
		return readRecordField(buffer);
	}

	ushort currentLogHandlingChoice() const {
		ushort choice = 0;
		if (mLogHandlingField != nullptr) mLogHandlingField->getData((void *)&choice);
		return choice;
	}

	void updateLogFileFieldState() {
		const bool persistSelected = currentLogHandlingChoice() == 1;
		if (!persistSelected) {
			if (current == mLogFilePathField && mMaxPathHistorySlider != nullptr) mMaxPathHistorySlider->select();
			else if (current == mLogFilePathBrowseButton && mMaxPathHistorySlider != nullptr)
				mMaxPathHistorySlider->select();
		}
		if (mLogFilePathField != nullptr) mLogFilePathField->setState(sfDisabled, persistSelected ? False : True);
		if (mLogFilePathBrowseButton != nullptr) mLogFilePathBrowseButton->setState(sfDisabled, persistSelected ? False : True);
		if (mLogFilePathLabel != nullptr) mLogFilePathLabel->setInactive(!persistSelected);
	}

	void setInputValue(TInputLine *inputLine, const std::string &value) {
		char buffer[kPathFieldSize];

		std::memset(buffer, 0, sizeof(buffer));
		writeRecordField(buffer, sizeof(buffer), value);
		inputLine->setData(buffer);
	}

	void browseSettingsMacroUri() {
		std::string selected;
		if (browseUriWithFileDialog(MRDialogHistoryScope::SetupSettingsMacro, "Select settings macro URI", selected)) setInputValue(mSettingsMacroPathField, selected);
	}

	void browseMacroPath() {
		std::string selected;
		if (browsePathWithDirectoryDialog(MRDialogHistoryScope::SetupMacroDirectory, selected)) setInputValue(mMacroDirectoryPathField, selected);
	}

	void browseHelpUri() {
		std::string selected;
		if (browseUriWithFileDialog(MRDialogHistoryScope::SetupHelpFile, "Select help file URI", selected)) setInputValue(mHelpFilePathField, selected);
	}

	void browseTempPath() {
		std::string selected;
		if (browsePathWithDirectoryDialog(MRDialogHistoryScope::SetupTempDirectory, selected)) setInputValue(mTempDirectoryPathField, selected);
	}

	void browseShellUri() {
		std::string selected;
		if (browseUriWithFileDialog(MRDialogHistoryScope::SetupShellExecutable, "Select shell executable URI", selected)) setInputValue(mShellExecutablePathField, selected);
	}

	void browseLogUri() {
		std::string selected;
		if (browseUriWithFileDialog(MRDialogHistoryScope::SetupLogFile, "Select logfile URI", selected)) setInputValue(mLogFilePathField, selected);
	}

	DialogValidationResult validateDialogValues() {
		std::string errorText;
		DialogValidationResult result;
		PathsDialogRecord record = collectRecordFromFields();

		result.valid = validatePathsRecord(record, errorText);
		result.warningText = errorText;
		return result;
	}

	PathsDialogRecord mCurrentRecord;
	static const int kVirtualDialogWidth = 92;
	static const int kVirtualDialogHeight = 27;
	TInputLine *mSettingsMacroPathField = nullptr;
	TInputLine *mMacroDirectoryPathField = nullptr;
	TInputLine *mHelpFilePathField = nullptr;
	TInputLine *mTempDirectoryPathField = nullptr;
	TInputLine *mShellExecutablePathField = nullptr;
	TInputLine *mLogFilePathField = nullptr;
	TInactiveStaticText *mLogFilePathLabel = nullptr;
	TInlineGlyphButton *mLogFilePathBrowseButton = nullptr;
	TRadioButtons *mLogHandlingField = nullptr;
	MRNumericSlider *mMaxPathHistorySlider = nullptr;
	MRNumericSlider *mMaxFileHistorySlider = nullptr;
};

void showPathsHelpDialog() {
	std::vector<std::string> lines;
	lines.push_back("PATHS HELP");
	lines.push_back("");
	lines.push_back("Path setup overview.");
	lines.push_back("Configure settings URI, macro path, help URI, temp path and shell URI.");
	lines.push_back("Set max path/file history sizes (5..50, default 15).");
	lines.push_back("Done saves and applies current settings.");
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
		PathsDialogRecord editedRecord = workingRecord;
		TPathsSetupDialog *dialog = new TPathsSetupDialog(workingRecord);

		if (dialog == nullptr) return;
		result = dialog->run(editedRecord);
		TObject::destroy(dialog);
		const bool changed = mr::dialogs::isDialogDraftDirty(baselineRecord, editedRecord, [](const PathsDialogRecord &lhs, const PathsDialogRecord &rhs) { return recordsEqual(lhs, rhs); });

		switch (result) {
			case cmMrSetupPathsHelp:
				workingRecord = editedRecord;
				showPathsHelpDialog();
				break;

			case cmOK:
				workingRecord = editedRecord;
				if (!saveAndReloadPathsRecord(workingRecord, errorText)) {
					postSetupFlowError("Installation / Paths", errorText);
					break;
				}
				running = false;
				break;

			case cmClose:
			case cmCancel:
				if (!changed) {
					running = false;
					break;
				}
				switch (mr::dialogs::runDialogDirtyGating("Path settings have unsaved changes.")) {
					case mr::dialogs::UnsavedChangesChoice::Save:
						workingRecord = editedRecord;
						if (!saveAndReloadPathsRecord(workingRecord, errorText)) {
							postSetupFlowError("Installation / Paths", errorText);
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

void showBackupsAutosaveHelpDialog() {
	std::vector<std::string> lines;
	lines.push_back("BACKUPS & AUTOSAVE HELP");
	lines.push_back("");
	lines.push_back("This dialog models global backup and autosave policy.");
	lines.push_back("Backup method covers Off, create-backup-file and move-to-backup-path.");
	lines.push_back("Backup file extension and backup path are modeled separately.");
	lines.push_back("Autosave is modeled as keyboard inactivity and an absolute interval.");
	lines.push_back("A value of 0 turns the respective autosave trigger off.");
	execDialog(createSetupSimplePreviewDialog("BACKUPS & AUTOSAVE HELP", 88, 15, lines, false));
}

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
			ushort color = getColor((state & sfFocused) != 0 ? 2 : 1);
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

	TBackupsAutosaveSetupDialog(const BackupsAutosaveDialogRecord &initialRecord) : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(kVirtualDialogWidth, kVirtualDialogHeight), "BACKUPS & AUTOSAVE", kVirtualDialogWidth, kVirtualDialogHeight, initSetupDialogFrame), mCurrentRecord(initialRecord) {
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

		if (originalWhat == evCommand || originalWhat == evKeyDown || originalWhat == evMouseDown || originalWhat == evMouseUp || originalWhat == evBroadcast) updateBackupFieldState();
		if (originalWhat == evCommand || originalWhat == evKeyDown || originalWhat == evMouseDown || originalWhat == evMouseUp || originalWhat == evBroadcast) refreshValidationState();
		if (originalWhat == evBroadcast && (originalCommand == cmReleasedFocus || originalCommand == cmReceivedFocus || originalCommand == cmMRNumericSliderChanged) && originalInfoPtr != mBackupDirectoryBrowseButton) refreshValidationState();
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
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmMrSetupBackupsAutosaveHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
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

		mr::dialogs::addManagedUniformButtonRow(*this, buttonLeft, buttonTop, 2, buttons);
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

	std::string currentInputValue(TInputLine *inputLine, std::size_t capacity) const {
		std::vector<char> buffer(capacity, '\0');
		if (inputLine == nullptr) return std::string();
		inputLine->getData(buffer.data());
		return readRecordField(buffer.data());
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
			case cmMrSetupBackupsAutosaveHelp:
				workingRecord = editedRecord;
				showBackupsAutosaveHelpDialog();
				break;

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
} // namespace

void runColorSetupDialogFlow() {
	auto palettesEqual = [](const TPalette &lhs, const TPalette &rhs) {
		for (int slot = 1; slot <= kMrPaletteMax; ++slot)
			if (lhs[slot] != rhs[slot]) return false;
		return true;
	};
	auto persistSettingsFileOnly = [](std::string &errorText) -> bool {
		MRSetupPaths paths = resolveSetupPathDefaults();
		paths.settingsMacroUri = configuredSettingsMacroFilePath();
		paths.macroPath = defaultMacroDirectoryPath();
		paths.helpUri = configuredHelpFilePath();
		paths.tempPath = configuredTempDirectoryPath();
		paths.shellUri = configuredShellExecutablePath();
		MRSettingsWriteReport writeReport;
		if (!writeSettingsMacroFile(paths, &errorText, &writeReport)) return false;
		mrLogSettingsWriteReport("color setup", writeReport);
		return true;
	};
	auto applyAndPersistColors = [&](const TPalette &palette, std::string &errorText) -> bool {
		if (!applyWorkingColorPaletteToConfigured(palette, errorText)) return false;
		if (!persistSettingsFileOnly(errorText)) return false;
		TProgram::application->redraw();
		mrUpdateAllWindowsColorTheme();
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
		const bool changed = mr::dialogs::isDialogDraftDirty(baselinePalette, workingPalette, palettesEqual);

		switch (result) {
			case cmOK:
				if (changed) {
					if (!applyAndPersistColors(workingPalette, errorText)) {
						postSetupFlowError("Installation / Color setup", errorText);
						break;
					}
				}
				havePendingPalette = false;
				running = false;
				break;

			case cmMrColorLoadTheme: {
				std::string themeUri;

				if (!chooseThemeFileForLoad(MRDialogHistoryScope::SetupThemeLoad, themeUri)) break;
				if (!loadColorThemeFile(themeUri, &errorText)) {
					forgetLoadDialogPath(MRDialogHistoryScope::SetupThemeLoad, themeUri.c_str());
					postSetupFlowError("Color Setup / Load Theme", errorText);
					break;
				}
				if (!persistSettingsFileOnly(errorText)) {
					postSetupFlowError("Color Setup / Save settings", errorText);
					break;
				}
				TProgram::application->redraw();
				mrUpdateAllWindowsColorTheme();
				break;
			}

			case cmMrColorSaveTheme: {
				std::string themeUri;
				std::string activeThemeUri = normalizeConfiguredPathInput(configuredColorThemeFilePath());
				bool overwriteActiveTheme = false;

				if (!chooseThemeFileForSave(MRDialogHistoryScope::SetupThemeSave, themeUri)) break;
				if (!confirmOverwriteForPath("Overwrite", "Theme file exists. Overwrite?", themeUri)) break;
				overwriteActiveTheme = normalizeConfiguredPathInput(themeUri) == activeThemeUri;
				if (!applyWorkingColorPaletteToConfigured(workingPalette, errorText)) {
					postSetupFlowError("Color Setup / Save Theme", errorText);
					break;
				}
				if (!writeColorThemeFile(themeUri, &errorText)) {
					postSetupFlowError("Color Setup / Save Theme", errorText);
					break;
				}
				if (overwriteActiveTheme) {
					if (!persistSettingsFileOnly(errorText)) {
						postSetupFlowError("Color Setup / Save settings", errorText);
						break;
					}
					TProgram::application->redraw();
					mrUpdateAllWindowsColorTheme();
				}
				pendingPalette = workingPalette;
				havePendingPalette = true;
				break;
			}

			case cmClose:
			case cmCancel:
				if (!changed) {
					havePendingPalette = false;
					running = false;
					break;
				}
				switch (mr::dialogs::runDialogDirtyGating("Color settings have unsaved changes.")) {
					case mr::dialogs::UnsavedChangesChoice::Save:
						if (!applyAndPersistColors(workingPalette, errorText)) {
							postSetupFlowError("Color Setup / Save settings", errorText);
							break;
						}
						havePendingPalette = false;
						running = false;
						break;
					case mr::dialogs::UnsavedChangesChoice::Discard:
						havePendingPalette = false;
						running = false;
						break;
					case mr::dialogs::UnsavedChangesChoice::Cancel:
						pendingPalette = workingPalette;
						havePendingPalette = true;
						discardQueuedCancelEvent();
						break;
					default:
						break;
				}
				break;

			default:
				havePendingPalette = false;
				running = false;
				break;
		}
	}
}

static void runUserInterfaceSettingsDialogFlow();

bool runSetupDialogCommand(unsigned short command) {
	switch (command) {
		case cmMrSetupEditSettings:
		case cmMrSetupFilenameExtensions:
			runFileExtensionProfilesDialogFlow();
			return true;

		case cmMrSetupColorSetup:
			runColorSetupDialogFlow();
			return true;

		case cmMrSetupKeyMapping:
			runKeymapManagerDialogFlow();
			return true;

		case cmMrSetupPaths:
			runPathsSetupDialogFlow();
			return true;

		case cmMrSetupBackupsAutosave:
			runBackupsAutosaveDialogFlow();
			return true;

		case cmMrSetupUserInterfaceSettings:
			runUserInterfaceSettingsDialogFlow();
			return true;

		default:
			return false;
	}
}

bool mrSaveColorThemeFromWorkingPaletteForTesting(const TPalette &workingPalette, const std::string &themeUri, std::string *errorMessage) {
	std::string errorText;
	MRSetupPaths paths = resolveSetupPathDefaults();

	if (!applyWorkingColorPaletteToConfigured(workingPalette, errorText)) {
		if (errorMessage != nullptr) *errorMessage = errorText;
		return false;
	}
	if (!writeColorThemeFile(themeUri, &errorText)) {
		if (errorMessage != nullptr) *errorMessage = errorText;
		return false;
	}

	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	MRSettingsWriteReport writeReport;
	if (!writeSettingsMacroFile(paths, &errorText, &writeReport)) {
		if (errorMessage != nullptr) *errorMessage = errorText;
		return false;
	}
	mrLogSettingsWriteReport("save theme + sync settings", writeReport);

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

namespace {

struct UserInterfaceSettingsDialogData {
	ushort flags = 0;
	ushort virtualDesktops = 1;
	ushort cursorBehaviourChoice = 1;
	char cursorPositionMarker[12] = {0};
};

bool validateCursorPositionMarkerInput(std::string_view value, std::string &errorText) {
	std::string trimmed = trimAscii(value);
	int rowPlaceholderCount = 0;
	int colPlaceholderCount = 0;

	if (trimmed.empty()) {
		errorText = "Cursor position marker must not be empty.";
		return false;
	}
	if (trimmed.size() > 10) {
		errorText = "Cursor position marker must be at most 10 characters.";
		return false;
	}
	for (char ch : trimmed) {
		if (ch == 'R') {
			++rowPlaceholderCount;
			if (rowPlaceholderCount > 1) {
				errorText = "Cursor position marker may contain R only once.";
				return false;
			}
			continue;
		}
		if (ch == 'C') {
			++colPlaceholderCount;
			if (colPlaceholderCount > 1) {
				errorText = "Cursor position marker may contain C only once.";
				return false;
			}
		}
	}
	if (rowPlaceholderCount == 0 || colPlaceholderCount == 0) {
		errorText = "Cursor position marker must contain both R and C.";
		return false;
	}
	errorText.clear();
	return true;
}

bool userInterfaceSettingsDialogDataEqual(const UserInterfaceSettingsDialogData &lhs, const UserInterfaceSettingsDialogData &rhs) {
	return lhs.flags == rhs.flags && lhs.virtualDesktops == rhs.virtualDesktops && lhs.cursorBehaviourChoice == rhs.cursorBehaviourChoice && readRecordField(lhs.cursorPositionMarker) == readRecordField(rhs.cursorPositionMarker);
}

class TUserInterfaceSettingsDialog : public MRScrollableDialog {
  public:
	TUserInterfaceSettingsDialog(bool initialWindowManager, bool initialMenulineMessages, int initialVirtualDesktops, bool initialAutoloadWorkspace, bool initialCyclicVirtualDesktops, MRCursorBehaviour initialCursorBehaviour, const std::string &initialCursorPositionMarker) : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(68, 19), "User interface settings", 66, 17, initSetupDialogFrame) {

		int const yStart = 2;

		TCheckBoxes *cb = new TCheckBoxes(TRect(3, yStart, 34, yStart + 4), new TSItem("~W~indow Manager", new TSItem("~M~enuline messages", new TSItem("~A~uto load workspace", new TSItem("~C~ycle virtual desktops", nullptr)))));

		mOptionsField = cb;
		addManaged(mOptionsField, mOptionsField->getBounds());

		mVirtualDesktopsSlider = new MRNumericSlider(TRect(26, 7, 63, 8), 1, 9, initialVirtualDesktops, 1, 1, MRNumericSlider::fmtRaw, cmMRNumericSliderChanged);
		addManaged(mVirtualDesktopsSlider, TRect(26, 7, 63, 8));
		addManaged(new TLabel(TRect(3, 7, 25, 8), "~V~irtual desktops:", mVirtualDesktopsSlider), TRect(3, 7, 25, 8));

		addManaged(new TStaticText(TRect(3, 9, 27, 10), "Cursor behaviour:"), TRect(3, 9, 27, 10));
		mCursorBehaviourField = new TRadioButtons(TRect(3, 10, 29, 12), new TSItem("~F~ree movement", new TSItem("~B~ound to text", nullptr)));
		addManaged(mCursorBehaviourField, TRect(3, 10, 29, 12));

		mCursorPositionMarkerField = new TInputLine(TRect(28, 13, 42, 14), 11);
		addManaged(mCursorPositionMarkerField, TRect(28, 13, 42, 14));
		addManaged(new TLabel(TRect(3, 13, 27, 14), "Cursor position ~m~arker: ", mCursorPositionMarkerField), TRect(3, 13, 27, 14));

		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 2);
		int buttonRow = 15;
		int buttonLeft = (66 - metrics.rowWidth) / 2;
		mr::dialogs::addManagedUniformButtonRow(*this, buttonLeft, buttonRow, 2, buttons);

		mInitialCursorBehaviourChoice = initialCursorBehaviour == MRCursorBehaviour::FreeMovement ? 0 : 1;
		writeRecordField(mDataCursorMarker, sizeof(mDataCursorMarker), initialCursorPositionMarker);
		setDialogValidationHook([this]() { return validateDialogValues(); });

		selectContent();
	}

	void getData(void *rec) override {
		UserInterfaceSettingsDialogData *data = static_cast<UserInterfaceSettingsDialogData *>(rec);
		if (mOptionsField != nullptr) mOptionsField->getData(&data->flags);
		if (mVirtualDesktopsSlider != nullptr) {
			int32_t val = 1;
			mVirtualDesktopsSlider->getData(&val);
			data->virtualDesktops = static_cast<ushort>(val);
		}
		if (mCursorBehaviourField != nullptr) mCursorBehaviourField->getData(&data->cursorBehaviourChoice);
		if (mCursorPositionMarkerField != nullptr) mCursorPositionMarkerField->getData(data->cursorPositionMarker);
	}

	void setData(void *rec) override {
		UserInterfaceSettingsDialogData *data = static_cast<UserInterfaceSettingsDialogData *>(rec);
		if (mOptionsField != nullptr) mOptionsField->setData(&data->flags);
		if (mVirtualDesktopsSlider != nullptr) {
			int32_t val = data->virtualDesktops;
			mVirtualDesktopsSlider->setData(&val);
		}
		if (mCursorBehaviourField != nullptr) {
			if (data->cursorBehaviourChoice > 1) data->cursorBehaviourChoice = mInitialCursorBehaviourChoice;
			mCursorBehaviourField->setData(&data->cursorBehaviourChoice);
		}
		if (mCursorPositionMarkerField != nullptr) {
			if (data->cursorPositionMarker[0] == '\0') writeRecordField(data->cursorPositionMarker, sizeof(data->cursorPositionMarker), mDataCursorMarker);
			mCursorPositionMarkerField->setData(data->cursorPositionMarker);
		}
	}

	TCheckBoxes *mOptionsField = nullptr;
	MRNumericSlider *mVirtualDesktopsSlider = nullptr;
	TRadioButtons *mCursorBehaviourField = nullptr;
	TInputLine *mCursorPositionMarkerField = nullptr;
	ushort mInitialCursorBehaviourChoice = 1;
	char mDataCursorMarker[12] = {0};

  private:
	std::string currentCursorMarkerInput() const {
		char value[12] = {0};
		if (mCursorPositionMarkerField != nullptr) mCursorPositionMarkerField->getData(value);
		return readRecordField(value);
	}

	DialogValidationResult validateDialogValues() const {
		DialogValidationResult result;
		std::string errorText;
		result.valid = validateCursorPositionMarkerInput(currentCursorMarkerInput(), errorText);
		result.warningText = errorText;
		return result;
	}
};

} // namespace

static void runUserInterfaceSettingsDialogFlow() {
	bool running = true;

	while (running) {
		bool currentWm = configuredWindowManager();
		bool currentMm = configuredMenulineMessages();
		int currentVd = configuredVirtualDesktops();
		bool currentAw = configuredAutoloadWorkspace();
		bool currentCv = configuredCyclicVirtualDesktops();
		MRCursorBehaviour currentCb = configuredCursorBehaviour();
		std::string currentCp = configuredCursorPositionMarker();

		TUserInterfaceSettingsDialog *dialog = new TUserInterfaceSettingsDialog(currentWm, currentMm, currentVd, currentAw, currentCv, currentCb, currentCp);
		UserInterfaceSettingsDialogData dialogData;
		if (currentWm) dialogData.flags |= 1;
		if (currentMm) dialogData.flags |= 2;
		if (currentAw) dialogData.flags |= 4;
		if (currentCv) dialogData.flags |= 8;

		dialogData.virtualDesktops = static_cast<ushort>(currentVd);
		dialogData.cursorBehaviourChoice = currentCb == MRCursorBehaviour::FreeMovement ? 0 : 1;
		writeRecordField(dialogData.cursorPositionMarker, sizeof(dialogData.cursorPositionMarker), currentCp);

		UserInterfaceSettingsDialogData baselineData = dialogData;
		ushort result = execDialogWithDataCapture(dialog, &dialogData);
		bool newWm = (dialogData.flags & 1) != 0;
		bool newMm = (dialogData.flags & 2) != 0;
		bool newAw = (dialogData.flags & 4) != 0;
		bool newCv = (dialogData.flags & 8) != 0;
		int newVd = static_cast<int>(dialogData.virtualDesktops);
		MRCursorBehaviour newCb = dialogData.cursorBehaviourChoice == 0 ? MRCursorBehaviour::FreeMovement : MRCursorBehaviour::BoundToText;
		std::string newCp = readRecordField(dialogData.cursorPositionMarker);
		const bool changed = mr::dialogs::isDialogDraftDirty(baselineData, dialogData, userInterfaceSettingsDialogDataEqual);
		auto applyAndPersistUiSettings = [&]() -> bool {
			std::string errorText;
			if (!setConfiguredCursorBehaviour(newCb, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredCursorPositionMarker(newCp, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			setConfiguredWindowManager(newWm, &errorText);
			setConfiguredMenulineMessages(newMm, &errorText);
			setConfiguredAutoloadWorkspace(newAw, &errorText);
			setConfiguredCyclicVirtualDesktops(newCv, &errorText);
			applyVirtualDesktopConfigurationChange(newVd);
			for (MREditWindow *window : allEditWindowsInZOrder())
				if (window != nullptr && window->getEditor() != nullptr) window->getEditor()->refreshConfiguredVisualSettings();
			if (!persistConfiguredSettingsSnapshot(&errorText)) postSetupFlowError("Installation / User interface settings", errorText);
			return true;
		};

		switch (result) {
			case cmOK:
				if (changed && !applyAndPersistUiSettings()) break;
				running = false;
				break;

			case cmClose:
			case cmCancel:
				if (!changed) {
					running = false;
					break;
				}
				switch (mr::dialogs::runDialogDirtyGating("User interface settings have unsaved changes.")) {
					case mr::dialogs::UnsavedChangesChoice::Save:
						if (!applyAndPersistUiSettings()) break;
						running = false;
						break;
					case mr::dialogs::UnsavedChangesChoice::Discard:
						running = false;
						break;
					case mr::dialogs::UnsavedChangesChoice::Cancel:
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
	clearSetupDialogStatus();
}

// ---- Consolidated from MRSetupCommon.cpp ----

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

TView *deepestCurrentDialogView(TGroup *group) {
	TView *view = group != nullptr ? group->current : nullptr;

	while (view != nullptr) {
		TGroup *childGroup = dynamic_cast<TGroup *>(view);
		if (childGroup == nullptr || childGroup->current == nullptr) break;
		view = childGroup->current;
	}
	return view;
}

MRKeymapContext keymapContextForDialog(const MRScrollableDialog &dialog) {
	TView *view = deepestCurrentDialogView(dialog.managedContent());

	return dynamic_cast<TListViewer *>(view) != nullptr ? MRKeymapContext::DialogList : MRKeymapContext::Dialog;
}

MRScrollableDialog::MRScrollableDialog(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight) : MRScrollableDialog(bounds, title, virtualWidth, virtualHeight, initSetupDialogFrame) {
}

MRScrollableDialog::MRScrollableDialog(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight, TFrame *(*frameFactory)(TRect)) : TWindowInit(frameFactory), TDialog(bounds, title), mVirtualWidth(virtualWidth), mVirtualHeight(virtualHeight), mContentRect(1, 1, size.x - 1, size.y - 1) {
	mContent = createSetupDialogContentGroup(mContentRect);
	if (mContent != nullptr) {
		mContent->options |= ofSelectable;
		insert(mContent);
	}
}

MRScrollableDialog::~MRScrollableDialog() {
	if (hasDialogValidationWarning) clearSetupDialogStatus();
}

void MRScrollableDialog::detectDoneButton(TView *view) {
	TButton *button = dynamic_cast<TButton *>(view);

	if (doneButton != nullptr || button == nullptr || !startsWithDoneButtonCaption(button->title)) return;
	doneButton = button;
}

void MRScrollableDialog::addManaged(TView *view, const TRect &base) {
	ManagedItem item;
	item.view = view;
	item.base = base;
	mManagedViews.push_back(item);
	detectDoneButton(view);
	if (mContent != nullptr) {
		TRect local = base;
		local.move(-mContentRect.a.x, -mContentRect.a.y);
		view->locate(local);
		mContent->insert(view);
	} else
		insert(view);
}

void MRScrollableDialog::selectContent() {
	if (mContent != nullptr) {
		mContent->resetCurrent();
		mContent->select();
	}
}

void MRScrollableDialog::scrollToOrigin() {
	if (mHScrollBar != nullptr) mHScrollBar->setValue(0);
	if (mVScrollBar != nullptr) mVScrollBar->setValue(0);
	applyScroll();
}

void MRScrollableDialog::setDialogValidationHook(DialogValidationHook hook) {
	dialogValidationHook = std::move(hook);
}

void MRScrollableDialog::runDialogValidation() {
	DialogValidationResult result;

	if (isRunningDialogValidation) return;
	isRunningDialogValidation = true;
	if (dialogValidationHook) result = dialogValidationHook();
	if (doneButton != nullptr) {
		const bool disableDone = !result.valid;
		const bool wasDisabled = (doneButton->state & sfDisabled) != 0;
		if (disableDone != wasDisabled) {
			doneButton->setState(sfDisabled, disableDone ? True : False);
			doneButton->drawView();
		}
	}
	if (result.valid) {
		if (hasDialogValidationWarning) {
			clearSetupDialogStatus();
			hasDialogValidationWarning = false;
			lastDialogValidationWarning.clear();
		}
	} else {
		std::string warningText = result.warningText.empty() ? "Dialog contains invalid values." : result.warningText;
		if (!hasDialogValidationWarning || warningText != lastDialogValidationWarning) {
			setSetupDialogStatus(warningText, result.error ? MRMenuBar::MarqueeKind::Error : MRMenuBar::MarqueeKind::Warning);
			hasDialogValidationWarning = true;
			lastDialogValidationWarning = warningText;
		}
	}
	isRunningDialogValidation = false;
}

void MRScrollableDialog::setDoneButtonDisabled(bool disable) {
	if (doneButton == nullptr) return;
	const bool wasDisabled = (doneButton->state & sfDisabled) != 0;
	if (wasDisabled == disable) return;
	doneButton->setState(sfDisabled, disable ? True : False);
	doneButton->drawView();
}

void MRScrollableDialog::initScrollIfNeeded() {
	int virtualContentWidth = std::max(1, mVirtualWidth - 2);
	int virtualContentHeight = std::max(1, mVirtualHeight - 2);
	bool needH = false;
	bool needV = false;

	for (;;) {
		bool prevH = needH;
		bool prevV = needV;
		int viewportWidth = std::max(1, size.x - 2);
		int viewportHeight = std::max(1, size.y - 2);
		needH = virtualContentWidth > viewportWidth;
		needV = virtualContentHeight > viewportHeight;
		if (needH == prevH && needV == prevV) break;
	}

	mContentRect = TRect(1, 1, size.x - 1, size.y - 1);
	if (mContentRect.b.x <= mContentRect.a.x) mContentRect.b.x = mContentRect.a.x + 1;
	if (mContentRect.b.y <= mContentRect.a.y) mContentRect.b.y = mContentRect.a.y + 1;
	if (mContent != nullptr) mContent->locate(mContentRect);

	if (needH) {
		TRect hRect(1, size.y - 1, size.x - 1, size.y);
		if (mHScrollBar == nullptr) {
			mHScrollBar = new TScrollBar(hRect);
			insert(mHScrollBar);
		} else
			mHScrollBar->locate(hRect);
	}
	if (needV) {
		TRect vRect(size.x - 1, 1, size.x, size.y - 1);
		if (mVScrollBar == nullptr) {
			mVScrollBar = new TScrollBar(vRect);
			insert(mVScrollBar);
		} else
			mVScrollBar->locate(vRect);
	}
	if (mHScrollBar != nullptr) {
		int maxDx = std::max(0, virtualContentWidth - std::max(1, mContentRect.b.x - mContentRect.a.x));
		mHScrollBar->setParams(0, 0, maxDx, std::max(1, (mContentRect.b.x - mContentRect.a.x) / 2), 1);
	}
	if (mVScrollBar != nullptr) {
		int maxDy = std::max(0, virtualContentHeight - std::max(1, mContentRect.b.y - mContentRect.a.y));
		mVScrollBar->setParams(0, 0, maxDy, std::max(1, (mContentRect.b.y - mContentRect.a.y) / 2), 1);
	}
	applyScroll();
	runDialogValidation();
}

void MRScrollableDialog::handleEvent(TEvent &event) {
	const ushort originalWhat = event.what;

	if (mrHandleRuntimeKeymapEvent(event, keymapContextForDialog(*this), nullptr)) return;

	if (event.what == evKeyDown) {
		ushort keyCode = event.keyDown.keyCode;

		if (keyCode == kbEsc) {
			endModal(cmCancel);
			clearEvent(event);
			return;
		}
		if (mContent != nullptr) {
			if (keyCode == kbTab || keyCode == kbCtrlI) {
				mContent->selectNext(False);
				ensureCurrentVisible();
				clearEvent(event);
				return;
			}
			if (keyCode == kbShiftTab) {
				mContent->selectNext(True);
				ensureCurrentVisible();
				clearEvent(event);
				return;
			}
		}
	}

	TDialog::handleEvent(event);
	if (event.what == evBroadcast && event.message.command == cmScrollBarChanged && (event.message.infoPtr == mHScrollBar || event.message.infoPtr == mVScrollBar)) {
		applyScroll();
		clearEvent(event);
		return;
	}
	if (event.what == evKeyDown || event.what == evCommand || event.what == evMouseDown || event.what == evMouseUp) ensureCurrentVisible();
	if (originalWhat == evCommand || originalWhat == evKeyDown || originalWhat == evMouseDown || originalWhat == evMouseUp) runDialogValidation();
}

void MRScrollableDialog::ensureViewVisible(TView *view) {
	if (view == nullptr || mContent == nullptr) return;
	for (const auto &managedView : mManagedViews)
		if (managedView.view == view) {
			int dx = mHScrollBar != nullptr ? mHScrollBar->value : 0;
			int dy = mVScrollBar != nullptr ? mVScrollBar->value : 0;
			int viewportWidth = std::max(1, mContentRect.b.x - mContentRect.a.x);
			int viewportHeight = std::max(1, mContentRect.b.y - mContentRect.a.y);
			int left = managedView.base.a.x - mContentRect.a.x;
			int right = managedView.base.b.x - mContentRect.a.x;
			int top = managedView.base.a.y - mContentRect.a.y;
			int bottom = managedView.base.b.y - mContentRect.a.y;

			if (mHScrollBar != nullptr) {
				if (left < dx) mHScrollBar->setValue(std::max(0, left));
				else if (right > dx + viewportWidth)
					mHScrollBar->setValue(std::max(0, right - viewportWidth));
			}
			if (mVScrollBar != nullptr) {
				if (top < dy) mVScrollBar->setValue(std::max(0, top));
				else if (bottom > dy + viewportHeight)
					mVScrollBar->setValue(std::max(0, bottom - viewportHeight));
			}
			applyScroll();
			return;
		}
}

void MRScrollableDialog::ensureCurrentVisible() {
	TView *view = mContent != nullptr ? mContent->current : nullptr;

	while (view != nullptr) {
		TGroup *group = dynamic_cast<TGroup *>(view);
		if (group == nullptr || group->current == nullptr) break;
		view = group->current;
	}
	ensureViewVisible(view);
}

void MRScrollableDialog::applyScroll() {
	int dx = mHScrollBar != nullptr ? mHScrollBar->value : 0;
	int dy = mVScrollBar != nullptr ? mVScrollBar->value : 0;

	for (auto &managedView : mManagedViews) {
		TRect moved = managedView.base;
		moved.move(-dx, -dy);
		moved.move(-mContentRect.a.x, -mContentRect.a.y);
		managedView.view->locate(moved);
	}
	if (mContent != nullptr) mContent->drawView();
}

MRDialogFoundation::MRDialogFoundation(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight) : MRDialogFoundation(bounds, title, virtualWidth, virtualHeight, initSetupDialogFrame) {
}

MRDialogFoundation::MRDialogFoundation(const TRect &bounds, const char *title, int virtualWidth, int virtualHeight, TFrame *(*frameFactory)(TRect)) : TWindowInit(frameFactory), MRScrollableDialog(bounds, title, virtualWidth, virtualHeight, frameFactory) {
}

void MRDialogFoundation::insert(TView *view) {
	if (view == nullptr) return;
	if (view == managedContent()) {
		TDialog::insert(view);
		return;
	}
	addManaged(view, view->getBounds());
}

void MRDialogFoundation::finalizeLayout() {
	TGroup *content = managedContent();
	if (mLayoutFinalized) return;
	initScrollIfNeeded();
	if (content != nullptr && content->current == nullptr) selectContent();
	mLayoutFinalized = true;
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

namespace mr::dialogs {

TRect centeredDialogRect(int width, int height) {
	return centeredSetupDialogRect(width, height);
}

DialogButtonRowMetrics measureUniformButtonRow(std::span<const DialogButtonSpec> specs, int gap, int minButtonWidth) {
	DialogButtonRowMetrics metrics;

	if (specs.empty()) return metrics;
	for (const DialogButtonSpec &spec : specs)
		metrics.buttonWidth = std::max(metrics.buttonWidth, buttonCaptionWidth(spec.title) + 4);
	metrics.buttonWidth = std::max(metrics.buttonWidth, minButtonWidth);
	metrics.rowWidth = static_cast<int>(specs.size()) * metrics.buttonWidth + static_cast<int>(specs.size() - 1) * gap;
	return metrics;
}

void insertUniformButtonRow(MRDialogFoundation &dialog, int left, int top, int gap, std::span<const DialogButtonSpec> specs, int minButtonWidth, std::vector<TButton *> *outButtons) {
	const DialogButtonRowMetrics metrics = measureUniformButtonRow(specs, gap, minButtonWidth);
	int x = left;

	for (const DialogButtonSpec &spec : specs) {
		TRect rect(x, top, x + metrics.buttonWidth, top + 2);
		TButton *button = new TButton(rect, spec.title, spec.command, spec.flags);
		dialog.insert(button);
		if (outButtons != nullptr) outButtons->push_back(button);
		x += metrics.buttonWidth + gap;
	}
}

void addManagedUniformButtonRow(MRScrollableDialog &dialog, int left, int top, int gap, std::span<const DialogButtonSpec> specs, int minButtonWidth, std::vector<TButton *> *outButtons) {
	const DialogButtonRowMetrics metrics = measureUniformButtonRow(specs, gap, minButtonWidth);
	int x = left;

	for (const DialogButtonSpec &spec : specs) {
		TRect rect(x, top, x + metrics.buttonWidth, top + 2);
		TButton *button = new TButton(rect, spec.title, spec.command, spec.flags);
		dialog.addManaged(button, rect);
		if (outButtons != nullptr) outButtons->push_back(button);
		x += metrics.buttonWidth + gap;
	}
}

MRDialogFoundation *createScrollableDialog(const char *title, int virtualWidth, int virtualHeight) {
	return new MRDialogFoundation(centeredDialogRect(virtualWidth, virtualHeight), title, virtualWidth, virtualHeight);
}

TFileDialog *createFileDialog(MRDialogHistoryScope scope, const char *wildCard, const char *title, const char *inputName, ushort options) {
	return mr::ui::createScopedFileDialog(scope, wildCard, title, inputName, options);
}

TDialog *createDirectoryDialog(MRDialogHistoryScope scope, ushort options) {
	return mr::ui::createScopedDirectoryDialog(scope, options);
}

void seedFileDialogPath(MRDialogHistoryScope scope, char *buffer, std::size_t bufferSize, const char *pattern) {
	const char *safePattern = pattern != nullptr && *pattern != '\0' ? pattern : "*.*";

	writeRecordField(buffer, bufferSize, "");
	initRememberedLoadDialogPath(scope, buffer, bufferSize, safePattern);
}

void suggestFileDialogName(char *buffer, std::size_t bufferSize, std::string_view suggestedValue) {
	const std::string fileName = dialogSeedFileName(suggestedValue);
	const std::string seeded = readRecordField(buffer);
	std::string directory;

	if (fileName.empty()) return;
	if (pathIsDirectory(seeded)) directory = normalizeConfiguredPathInput(seeded);
	else
		directory = parentDirectoryOfPath(seeded);
	if (!directory.empty()) {
		if (directory.back() != '/') directory += '/';
		writeRecordField(buffer, bufferSize, directory + fileName);
	} else
		writeRecordField(buffer, bufferSize, fileName);
}

ushort execRememberingFileDialogWithData(MRDialogHistoryScope scope, const char *wildCard, const char *title, const char *inputName, ushort options, char *buffer) {
	std::string originalCwd = readCurrentWorkingDirectory();
	std::string seedDirectory = resolveFileDialogSeedDirectory(scope, buffer);

	if (!seedDirectory.empty()) (void)::chdir(seedDirectory.c_str());
	const ushort result = execDialogRawWithData(createFileDialog(scope, wildCard, title, inputName, options), buffer);
	if (!originalCwd.empty()) (void)::chdir(originalCwd.c_str());

	if (result != cmCancel && !deferRememberingLoadDialogPath(scope)) rememberLoadDialogPath(scope, buffer);
	return result;
}

ushort execDialog(TDialog *dialog) {
	ushort result = cmCancel;
	MRDialogFoundation *foundation = nullptr;
	MRScrollableDialog *scrollable = nullptr;

	if (dialog == nullptr || TProgram::deskTop == nullptr) return cmCancel;
	foundation = dynamic_cast<MRDialogFoundation *>(dialog);
	if (foundation != nullptr) foundation->finalizeLayout();
	else {
		scrollable = dynamic_cast<MRScrollableDialog *>(dialog);
		if (scrollable != nullptr) scrollable->initScrollIfNeeded();
	}
	result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	return result;
}

ushort execDialogWithData(TDialog *dialog, void *data) {
	ushort result = cmCancel;
	MRDialogFoundation *foundation = nullptr;
	MRScrollableDialog *scrollable = nullptr;
	if (dialog == nullptr || TProgram::deskTop == nullptr) return cmCancel;
	if (data != nullptr) dialog->setData(data);
	foundation = dynamic_cast<MRDialogFoundation *>(dialog);
	if (foundation != nullptr) foundation->finalizeLayout();
	else {
		scrollable = dynamic_cast<MRScrollableDialog *>(dialog);
		if (scrollable != nullptr) scrollable->initScrollIfNeeded();
	}
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel && data != nullptr) dialog->getData(data);
	TObject::destroy(dialog);
	return result;
}

} // namespace mr::dialogs

void insertSetupStaticLine(TDialog *dialog, int x, int y, const char *text) {
	dialog->insert(new TStaticText(TRect(x, y, x + std::strlen(text) + 1, y + 1), text));
}

TDialog *createSetupSimplePreviewDialog(const char *title, int width, int height, const std::vector<std::string> &lines, bool showOkCancelHelp) {
	MRDialogFoundation *dialog = new MRDialogFoundation(centeredSetupDialogRect(width, height), title, width, height);
	int y = 2;

	if (dialog == nullptr) return nullptr;
	for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it, ++y) {
		dialog->insert(new TStaticText(TRect(2, y, 2 + std::strlen(it->c_str()) + 1, y + 1), it->c_str()));
	}

	if (showOkCancelHelp) {
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 1);
		const int buttonLeft = (width - metrics.rowWidth) / 2;
		mr::dialogs::insertUniformButtonRow(*dialog, buttonLeft, height - 3, 1, buttons);
	} else {
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 0);
		const int buttonLeft = (width - metrics.rowWidth) / 2;
		mr::dialogs::insertUniformButtonRow(*dialog, buttonLeft, height - 3, 0, buttons);
	}

	dialog->finalizeLayout();
	return dialog;
}
