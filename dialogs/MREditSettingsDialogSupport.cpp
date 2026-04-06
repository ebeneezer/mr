#include "MREditSettingsDialogInternal.hpp"

#include "MRSetupDialogs.hpp"

#include "../app/TMREditorApp.hpp"
#include "../config/MRDialogPaths.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace MREditSettingsDialogInternal {

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
	for (char &i : value)
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
	writeRecordField(record.defaultExtensions, sizeof(record.defaultExtensions),
	                 settings.defaultExtensions);
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
			errorText = "TABSIZE must be between 2 and 32.";
			return false;
		}
		tabSize = std::strtol(tabSizeText.c_str(), &end, 10);
		if (end == tabSizeText.c_str() || end == nullptr || *end != '\0' || tabSize < 2 ||
		    tabSize > 32) {
			errorText = "TABSIZE must be an integer between 2 and 32.";
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
	settings.codeFolding = (record.optionsMask & kOptionCodeFolding) != 0;
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

} // namespace MREditSettingsDialogInternal
