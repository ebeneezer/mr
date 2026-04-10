#include "MREditProfilesRecordSupport.hpp"

#include "MRSetupDialogCommon.hpp"
#include "MRSetupDialogs.hpp"

#include "../app/TMREditorApp.hpp"
#include "../config/MRDialogPaths.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace MREditProfilesDialogInternal {

std::string trimAscii(const std::string &value) {
	return mr::dialogs::trimAscii(value);
}

std::string upperAscii(std::string value) {
	for (char &i : value)
		i = static_cast<char>(std::toupper(static_cast<unsigned char>(i)));
	return value;
}

std::string readRecordField(const char *value) {
	return mr::dialogs::readRecordField(value);
}

void writeRecordField(char *dest, std::size_t destSize, const std::string &value) {
	mr::dialogs::writeRecordField(dest, destSize, value);
}

bool recordsEqual(const EditSettingsDialogRecord &lhs, const EditSettingsDialogRecord &rhs) {
	return readRecordField(lhs.pageBreak) == readRecordField(rhs.pageBreak) &&
	       readRecordField(lhs.wordDelimiters) == readRecordField(rhs.wordDelimiters) &&
	       readRecordField(lhs.defaultExtensions) == readRecordField(rhs.defaultExtensions) &&
	       readRecordField(lhs.tabSize) == readRecordField(rhs.tabSize) &&
	       readRecordField(lhs.rightMargin) == readRecordField(rhs.rightMargin) &&
	       readRecordField(lhs.binaryRecordLength) == readRecordField(rhs.binaryRecordLength) &&
	       readRecordField(lhs.postLoadMacro) == readRecordField(rhs.postLoadMacro) &&
	       readRecordField(lhs.preSaveMacro) == readRecordField(rhs.preSaveMacro) &&
	       readRecordField(lhs.defaultPath) == readRecordField(rhs.defaultPath) &&
	       readRecordField(lhs.formatLine) == readRecordField(rhs.formatLine) &&
	       readRecordField(lhs.cursorStatusColor) == readRecordField(rhs.cursorStatusColor) &&
	       lhs.optionsMask == rhs.optionsMask && lhs.tabExpandChoice == rhs.tabExpandChoice &&
	       lhs.indentStyleChoice == rhs.indentStyleChoice && lhs.fileTypeChoice == rhs.fileTypeChoice &&
	       lhs.columnBlockMoveChoice == rhs.columnBlockMoveChoice &&
	       lhs.defaultModeChoice == rhs.defaultModeChoice;
}

void initEditSettingsDialogRecord(EditSettingsDialogRecord &record) {
	MREditSetupSettings settings = configuredEditSetupSettings();
	std::string columnMove = upperAscii(settings.columnBlockMove);
	std::string defaultMode = upperAscii(settings.defaultMode);
	std::string indentStyle = upperAscii(settings.indentStyle);
	std::string fileType = upperAscii(settings.fileType);

	std::memset(&record, 0, sizeof(record));
	writeRecordField(record.pageBreak, sizeof(record.pageBreak), settings.pageBreak);
	writeRecordField(record.wordDelimiters, sizeof(record.wordDelimiters), settings.wordDelimiters);
	writeRecordField(record.defaultExtensions, sizeof(record.defaultExtensions),
	                 settings.defaultExtensions);
	writeRecordField(record.tabSize, sizeof(record.tabSize), std::to_string(settings.tabSize));
	writeRecordField(record.rightMargin, sizeof(record.rightMargin), std::to_string(settings.rightMargin));
	writeRecordField(record.binaryRecordLength, sizeof(record.binaryRecordLength),
	                 std::to_string(settings.binaryRecordLength));
	writeRecordField(record.postLoadMacro, sizeof(record.postLoadMacro), settings.postLoadMacro);
	writeRecordField(record.preSaveMacro, sizeof(record.preSaveMacro), settings.preSaveMacro);
	writeRecordField(record.defaultPath, sizeof(record.defaultPath), settings.defaultPath);
	writeRecordField(record.formatLine, sizeof(record.formatLine), settings.formatLine);
	writeRecordField(record.cursorStatusColor, sizeof(record.cursorStatusColor), settings.cursorStatusColor);

	record.optionsMask = 0;
	if (settings.truncateSpaces)
		record.optionsMask |= kOptionTruncateSpaces;
	if (settings.eofCtrlZ)
		record.optionsMask |= kOptionEofCtrlZ;
	if (settings.eofCrLf)
		record.optionsMask |= kOptionEofCrLf;
	if (settings.wordWrap)
		record.optionsMask |= kOptionWordWrap;
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
	record.indentStyleChoice = (indentStyle == "AUTOMATIC") ? kIndentStyleAutomatic
	                       : (indentStyle == "SMART") ? kIndentStyleSmart
	                                                  : kIndentStyleOff;
	record.fileTypeChoice = (fileType == "LEGACY_TEXT") ? kFileTypeLegacyText
	                    : (fileType == "BINARY") ? kFileTypeBinary
	                                             : kFileTypeUnix;
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

	{
		std::string rightMarginText = readRecordField(record.rightMargin);
		char *end = nullptr;
		long rightMargin = 0;
		if (rightMarginText.empty()) {
			errorText = "RIGHT_MARGIN must be between 1 and 999.";
			return false;
		}
		rightMargin = std::strtol(rightMarginText.c_str(), &end, 10);
		if (end == rightMarginText.c_str() || end == nullptr || *end != '\0' || rightMargin < 1 ||
		    rightMargin > 999) {
			errorText = "RIGHT_MARGIN must be an integer between 1 and 999.";
			return false;
		}
		settings.rightMargin = static_cast<int>(rightMargin);
	}

	{
		std::string binaryRecordLengthText = readRecordField(record.binaryRecordLength);
		char *end = nullptr;
		long binaryRecordLength = 0;
		if (binaryRecordLengthText.empty()) {
			errorText = "BINARY_RECORD_LENGTH must be between 1 and 99999.";
			return false;
		}
		binaryRecordLength = std::strtol(binaryRecordLengthText.c_str(), &end, 10);
		if (end == binaryRecordLengthText.c_str() || end == nullptr || *end != '\0' || binaryRecordLength < 1 ||
		    binaryRecordLength > 99999) {
			errorText = "BINARY_RECORD_LENGTH must be an integer between 1 and 99999.";
			return false;
		}
		settings.binaryRecordLength = static_cast<int>(binaryRecordLength);
	}

	settings.postLoadMacro = readRecordField(record.postLoadMacro);
	settings.preSaveMacro = readRecordField(record.preSaveMacro);
	settings.defaultPath = readRecordField(record.defaultPath);
	settings.formatLine = readRecordField(record.formatLine);
	settings.cursorStatusColor = upperAscii(trimAscii(readRecordField(record.cursorStatusColor)));
	if (!settings.cursorStatusColor.empty()) {
		unsigned int parsed = 0;
		static const char *const hex = "0123456789ABCDEF";
		if (settings.cursorStatusColor.size() > 2) {
			errorText = "CURSOR_STATUS_COLOR must be hex 00..FF or empty.";
			return false;
		}
		for (char ch : settings.cursorStatusColor)
			if (std::isxdigit(static_cast<unsigned char>(ch)) == 0) {
				errorText = "CURSOR_STATUS_COLOR must be hex 00..FF or empty.";
				return false;
			}
		parsed = static_cast<unsigned int>(std::strtoul(settings.cursorStatusColor.c_str(), nullptr, 16));
		if (parsed > 0xFF) {
			errorText = "CURSOR_STATUS_COLOR must be hex 00..FF or empty.";
			return false;
		}
		settings.cursorStatusColor.clear();
		settings.cursorStatusColor.push_back(hex[(parsed >> 4) & 0x0F]);
		settings.cursorStatusColor.push_back(hex[parsed & 0x0F]);
	}
	if (trimAscii(settings.formatLine).empty())
		settings.formatLine = std::string(8, ' ');
	if (!settings.postLoadMacro.empty() && !mr::dialogs::hasMrmacExtension(settings.postLoadMacro)) {
		errorText = "POST_LOAD_MACRO must end with .mrmac.";
		return false;
	}
	if (!settings.preSaveMacro.empty() && !mr::dialogs::hasMrmacExtension(settings.preSaveMacro)) {
		errorText = "PRE_SAVE_MACRO must end with .mrmac.";
		return false;
	}

	settings.truncateSpaces = (record.optionsMask & kOptionTruncateSpaces) != 0;
	settings.eofCtrlZ = (record.optionsMask & kOptionEofCtrlZ) != 0;
	settings.eofCrLf = (record.optionsMask & kOptionEofCrLf) != 0;
	settings.wordWrap = (record.optionsMask & kOptionWordWrap) != 0;
	settings.backupFiles = (record.optionsMask & kOptionBackupFiles) != 0;
	settings.showEofMarker = (record.optionsMask & kOptionShowEofMarker) != 0;
	settings.showEofMarkerEmoji = (record.optionsMask & kOptionShowEofMarkerEmoji) != 0;
	settings.persistentBlocks = (record.optionsMask & kOptionPersistentBlocks) != 0;
	settings.codeFolding = (record.optionsMask & kOptionCodeFolding) != 0;
	settings.showLineNumbers = (record.optionsMask & kOptionShowLineNumbers) != 0;
	settings.lineNumZeroFill = (record.optionsMask & kOptionLineNumZeroFill) != 0;
	settings.tabExpand = record.tabExpandChoice == kTabExpandTabs;
	settings.indentStyle = (record.indentStyleChoice == kIndentStyleAutomatic) ? "AUTOMATIC"
	                    : (record.indentStyleChoice == kIndentStyleSmart) ? "SMART"
	                                                                   : "OFF";
	settings.fileType = (record.fileTypeChoice == kFileTypeLegacyText) ? "LEGACY_TEXT"
	                  : (record.fileTypeChoice == kFileTypeBinary) ? "BINARY"
	                                                             : "UNIX";
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

} // namespace MREditProfilesDialogInternal
