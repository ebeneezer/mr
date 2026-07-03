#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsEditSetup.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsThemesProfiles.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

static const char *const kDefaultPageBreakLiteral = "\\f";
static const char *const kDefaultWordDelimiters = ".()'\\\",#$012%^&*+-/[]?";
static const char *const kDefaultDefaultExtensions = "PAS;ASM;BAT;TXT;DO";
static const char *const kColumnBlockMoveDelete = "DELETE_SPACE";
static const char *const kColumnBlockMoveLeave = "LEAVE_SPACE";
static const char *const kDefaultModeInsert = "INSERT";
static const char *const kDefaultModeOverwrite = "OVERWRITE";
static const char *const kMiniMapPositionOff = "OFF";
static const char *const kMiniMapPositionLeading = "LEADING";
static const char *const kMiniMapPositionTrailing = "TRAILING";
static const char *const kLineNumbersPositionOff = "OFF";
static const char *const kLineNumbersPositionLeading = "LEADING";
static const char *const kLineNumbersPositionTrailing = "TRAILING";
static const char *const kCodeFoldingPositionOff = "OFF";
static const char *const kCodeFoldingPositionLeading = "LEADING";
static const char *const kDefaultGuttersOrder = "LCM";
static const char *const kIndentStyleOff = "OFF";
static const char *const kIndentStyleAutomatic = "AUTOMATIC";
static const char *const kIndentStyleSmart = "SMART";
static const char *const kFileTypeLegacyText = "LEGACY_TEXT";
static const char *const kFileTypeUnix = "UNIX";
static const char *const kFileTypeBinary = "BINARY";
static const int kDefaultTabSize = 8;
static const int kMinTabSize = 2;
static const int kMaxTabSize = 32;
static const int kDefaultMiniMapWidth = 4;
static const int kMinMiniMapWidth = 2;
static const int kMaxMiniMapWidth = 20;
static const char *const kWindowColorThemeProfileKey = "WINDOW_COLORTHEME_URI";

static const MREditSettingDescriptor kEditSettingDescriptors[] = {
    {"PAGE_BREAK", "Page break", MREditSettingSection::Text, MREditSettingKind::String, true, kOvPageBreak},
    {"WORD_DELIMITERS", "Word delimiters", MREditSettingSection::Text, MREditSettingKind::String, true, kOvWordDelimiters},
    {"DEFAULT_EXTENSIONS", "Filename extension fallback", MREditSettingSection::OpenFile, MREditSettingKind::String, true, kOvDefaultExtensions},
    {"TRUNCATE_SPACES", "Truncate whitespace", MREditSettingSection::Save, MREditSettingKind::Boolean, true, kOvTruncateSpaces},
    {"EOF_CTRL_Z", "Write Ctrl+Z EOF", MREditSettingSection::Save, MREditSettingKind::Boolean, true, kOvEofCtrlZ},
    {"EOF_CR_LF", "Write CR/LF", MREditSettingSection::Save, MREditSettingKind::Boolean, true, kOvEofCrLf},
    {"TAB_EXPAND", "Expand tabs", MREditSettingSection::Tabs, MREditSettingKind::Boolean, true, kOvTabExpand},
    {"DISPLAY_TABS", "Display tabs", MREditSettingSection::Tabs, MREditSettingKind::Boolean, true, kOvDisplayTabs},
    {"TAB_SIZE", "Tab size", MREditSettingSection::Tabs, MREditSettingKind::Integer, true, kOvTabSize},
    {"LEFT_MARGIN", "Left margin", MREditSettingSection::Formatting, MREditSettingKind::Integer, true, kOvLeftMargin},
    {"RIGHT_MARGIN", "Right margin", MREditSettingSection::Formatting, MREditSettingKind::Integer, true, kOvRightMargin},
    {"FORMAT_RULER", "Format ruler", MREditSettingSection::Formatting, MREditSettingKind::Boolean, true, kOvFormatRuler},
    {"WORD_WRAP", "Word wrap", MREditSettingSection::Formatting, MREditSettingKind::Boolean, true, kOvWordWrap},
    {"INDENT_STYLE", "Indent style", MREditSettingSection::Formatting, MREditSettingKind::Choice, true, kOvIndentStyle},
    {"CODE_LANGUAGE", "Code language", MREditSettingSection::Display, MREditSettingKind::Choice, true, kOvCodeLanguage},
    {"CODE_COLORING", "Code coloring", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvCodeColoring},
    {"CODE_FOLDING", "Code folding", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvCodeFoldingFeature},
    {"FILE_TYPE", "File type", MREditSettingSection::Formatting, MREditSettingKind::Choice, true, kOvFileType},
    {"BINARY_RECORD_LENGTH", "Binary record length", MREditSettingSection::Formatting, MREditSettingKind::Integer, true, kOvBinaryRecordLength},
    {"POST_LOAD_MACRO", "Post-load macro", MREditSettingSection::Macros, MREditSettingKind::String, true, kOvPostLoadMacro},
    {"PRE_SAVE_MACRO", "Pre-save macro", MREditSettingSection::Macros, MREditSettingKind::String, true, kOvPreSaveMacro},
    {"DEFAULT_PATH", "Default path", MREditSettingSection::Paths, MREditSettingKind::String, true, kOvDefaultPath},
    {"FORMAT_LINE", "Format line", MREditSettingSection::Formatting, MREditSettingKind::String, true, kOvFormatLine},
    {"BACKUP_FILES", "Backup files", MREditSettingSection::Save, MREditSettingKind::Boolean, true, kOvBackupFiles},
    {"BACKUP_METHOD", "Backup method", MREditSettingSection::Save, MREditSettingKind::Choice, false, kOvBackupMethod},
    {"BACKUP_FREQUENCY", "Backup frequency", MREditSettingSection::Save, MREditSettingKind::Choice, false, kOvBackupFrequency},
    {"BACKUP_EXTENSION", "Backup extension", MREditSettingSection::Save, MREditSettingKind::String, false, kOvBackupExtension},
    {"BACKUP_DIRECTORY", "Backup directory", MREditSettingSection::Save, MREditSettingKind::String, false, kOvBackupDirectory},
    {"AUTOSAVE_INACTIVITY_SECONDS", "Autosave inactivity", MREditSettingSection::Save, MREditSettingKind::Integer, false, kOvAutosaveInactivitySeconds},
    {"AUTOSAVE_INTERVAL_SECONDS", "Autosave interval", MREditSettingSection::Save, MREditSettingKind::Integer, false, kOvAutosaveIntervalSeconds},
    {"SHOW_EOF_MARKER", "Show EOF marker", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvShowEofMarker},
    {"SHOW_EOF_MARKER_EMOJI", "EOF marker emoji", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvShowEofMarkerEmoji},
    {"LINE_NUMBERS_POSITION", "Line number position", MREditSettingSection::Display, MREditSettingKind::Choice, true, kOvLineNumbersPosition},
    {"LINE_NUM_ZERO_FILL", "Zero-fill line numbers", MREditSettingSection::Display, MREditSettingKind::Boolean, true, kOvLineNumZeroFill},
    {"MINIMAP_POSITION", "Minimap position", MREditSettingSection::Display, MREditSettingKind::Choice, true, kOvMiniMapPosition},
    {"MINIMAP_WIDTH", "Minimap width", MREditSettingSection::Display, MREditSettingKind::Integer, true, kOvMiniMapWidth},
    {"MINIMAP_MARKER_GLYPH", "Minimap marker glyph", MREditSettingSection::Display, MREditSettingKind::String, true, kOvMiniMapMarkerGlyph},
    {"GUTTERS", "Gutter render order", MREditSettingSection::Display, MREditSettingKind::String, true, kOvGutters},
    {"PERSISTENT_BLOCKS", "Persistent blocks", MREditSettingSection::Blocks, MREditSettingKind::Boolean, true, kOvPersistentBlocks},
    {"CODE_FOLDING_POSITION", "Code folding position", MREditSettingSection::Display, MREditSettingKind::Choice, true, kOvCodeFoldingPosition},
    {"BLOCK_MOVE", "Block move", MREditSettingSection::Blocks, MREditSettingKind::Choice, true, kOvColumnBlockMove},
    {"DEFAULT_MODE", "Default mode", MREditSettingSection::Mode, MREditSettingKind::Choice, true, kOvDefaultMode},
    {"CURSOR_STATUS_COLOR", "Cursor status color", MREditSettingSection::Display, MREditSettingKind::String, true, kOvCursorStatusColor},
};

bool parseBooleanLiteral(const std::string &value, bool &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == "TRUE" || upper == "1" || upper == "YES" || upper == "ON") {
		outValue = true;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == "FALSE" || upper == "0" || upper == "NO" || upper == "OFF") {
		outValue = false;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "Expected boolean literal true/false.");
}

std::string canonicalBooleanLiteral(bool value) {
	return value ? "true" : "false";
}

std::string normalizeColumnBlockMove(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));
	std::string compact;

	for (char ch : key) {
		if (ch == '-' || ch == ' ') ch = '_';
		compact.push_back(ch);
	}

	if (compact == "DELETE_SPACE" || compact == "DELETE") return kColumnBlockMoveDelete;
	if (compact == "LEAVE_SPACE" || compact == "LEAVE") return kColumnBlockMoveLeave;
	return std::string();
}

std::string normalizeDefaultMode(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "INSERT") return kDefaultModeInsert;
	if (key == "OVERWRITE" || key == "OVR") return kDefaultModeOverwrite;
	return std::string();
}

std::string normalizeMiniMapPosition(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "OFF") return kMiniMapPositionOff;
	if (key == "LEADING" || key == "LEFT") return kMiniMapPositionLeading;
	if (key == "TRAILING" || key == "RIGHT") return kMiniMapPositionTrailing;
	return std::string();
}

std::string normalizeGutterPosition(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "OFF") return kLineNumbersPositionOff;
	if (key == "LEADING" || key == "LEFT") return kLineNumbersPositionLeading;
	if (key == "TRAILING" || key == "RIGHT") return kLineNumbersPositionTrailing;
	return std::string();
}

std::string normalizeLineNumbersPosition(const std::string &value) {
	return normalizeGutterPosition(value);
}

std::string normalizeCodeFoldingPosition(const std::string &value) {
	return normalizeGutterPosition(value);
}

std::string normalizeGuttersOrder(const std::string &value) {
	std::string normalized;
	std::array<bool, 3> seen = {false, false, false};
	const std::string upper = upperAscii(trimAscii(value));

	auto addChar = [&](char marker, std::size_t index) {
		if (!seen[index]) {
			normalized.push_back(marker);
			seen[index] = true;
		}
	};

	for (char ch : upper) {
		switch (ch) {
			case 'L':
				addChar('L', 0);
				break;
			case 'C':
				addChar('C', 1);
				break;
			case 'M':
				addChar('M', 2);
				break;
			default:
				break;
		}
	}
	if (normalized.empty()) normalized = kDefaultGuttersOrder;
	return normalized;
}

int utf8CodepointLength(unsigned char lead) {
	if ((lead & 0x80u) == 0u) return 1;
	if ((lead & 0xE0u) == 0xC0u) return 2;
	if ((lead & 0xF0u) == 0xE0u) return 3;
	if ((lead & 0xF8u) == 0xF0u) return 4;
	return 0;
}

bool normalizeMiniMapMarkerGlyph(const std::string &value, std::string &outGlyph, std::string *errorMessage) {
	const std::string trimmed = trimAscii(value);

	if (trimmed.empty()) {
		outGlyph = "│";
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	const unsigned char lead = static_cast<unsigned char>(trimmed[0]);
	const int cpLen = utf8CodepointLength(lead);
	if (cpLen == 0 || trimmed.size() != static_cast<std::size_t>(cpLen)) return setError(errorMessage, "MINIMAP_MARKER_GLYPH must be exactly one UTF-8 character.");
	for (int i = 1; i < cpLen; ++i) {
		const unsigned char ch = static_cast<unsigned char>(trimmed[static_cast<std::size_t>(i)]);
		if ((ch & 0xC0u) != 0x80u) return setError(errorMessage, "MINIMAP_MARKER_GLYPH must be valid UTF-8.");
	}
	if (cpLen == 1 && std::iscntrl(lead) != 0) return setError(errorMessage, "MINIMAP_MARKER_GLYPH may not be a control character.");
	outGlyph = trimmed;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string normalizeIndentStyle(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "OFF") return kIndentStyleOff;
	if (key == "AUTOMATIC" || key == "AUTO") return kIndentStyleAutomatic;
	if (key == "SMART") return kIndentStyleSmart;
	return std::string();
}

std::string normalizeFileType(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "LEGACY_TEXT" || key == "LEGACY" || key == "CRLF" || key == "TEXT") return kFileTypeLegacyText;
	if (key == "UNIX" || key == "LF") return kFileTypeUnix;
	if (key == "BINARY" || key == "BIN") return kFileTypeBinary;
	return std::string();
}

constexpr const char *kBackupMethodOff = "OFF";
constexpr const char *kBackupMethodBakFile = "BAK_FILE";
constexpr const char *kBackupMethodDirectory = "DIRECTORY";
constexpr const char *kBackupFrequencyFirstSaveOnly = "FIRST_SAVE_ONLY";
constexpr const char *kBackupFrequencyEverySave = "EVERY_SAVE";
constexpr int kMinAutosaveInactivitySeconds = 5;
constexpr int kMaxAutosaveInactivitySeconds = 100;
constexpr int kMinAutosaveIntervalSeconds = 100;
constexpr int kMaxAutosaveIntervalSeconds = 300;

std::string normalizeBackupMethod(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "OFF") return kBackupMethodOff;
	if (key == "BAK_FILE" || key == "BAKFILE" || key == "FILE") return kBackupMethodBakFile;
	if (key == "DIRECTORY" || key == "DIR" || key == "PATH") return kBackupMethodDirectory;
	return std::string();
}

std::string normalizeBackupFrequency(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "FIRST_SAVE_ONLY" || key == "FIRST_SAVE" || key == "FIRST") return kBackupFrequencyFirstSaveOnly;
	if (key == "EVERY_SAVE" || key == "EVERY") return kBackupFrequencyEverySave;
	return std::string();
}

bool normalizeAutosaveSeconds(const std::string &value, int minValue, int maxValue, int &outValue, const char *fieldName, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, std::string(fieldName) + " must be 0 or within " + std::to_string(minValue) + ".." + std::to_string(maxValue) + " seconds.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, std::string(fieldName) + " must be 0 or within " + std::to_string(minValue) + ".." + std::to_string(maxValue) + " seconds.");
	if (parsed != 0 && (parsed < minValue || parsed > maxValue)) return setError(errorMessage, std::string(fieldName) + " must be 0 or within " + std::to_string(minValue) + ".." + std::to_string(maxValue) + " seconds.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string normalizeBackupExtension(const std::string &value) {
	std::string normalized = trimAscii(value);

	while (!normalized.empty() && normalized.front() == '.')
		normalized.erase(normalized.begin());
	return normalized;
}

bool parseHexColorToken(const std::string &token, unsigned char &outValue) {
	const std::string trimmed = upperAscii(trimAscii(token));

	if (trimmed.size() != 2 || std::isxdigit(static_cast<unsigned char>(trimmed[0])) == 0 || std::isxdigit(static_cast<unsigned char>(trimmed[1])) == 0) return false;
	outValue = static_cast<unsigned char>(std::strtoul(trimmed.c_str(), nullptr, 16));
	return true;
}

bool validateBackupExtension(const std::string &value, std::string *errorMessage) {
	static const std::string invalidChars = std::string("\\") + "/*?:\"<>|";
	std::string normalized = normalizeBackupExtension(value);

	if (normalized.empty()) return setError(errorMessage, "BACKUP_EXTENSION may not be empty when BACKUP_METHOD=BAK_FILE.");
	if (normalized.size() > 255) return setError(errorMessage, "BACKUP_EXTENSION may not exceed 255 characters.");
	if (normalized.find_first_of(invalidChars) != std::string::npos) return setError(errorMessage, "BACKUP_EXTENSION contains invalid filename characters.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool validateWritableDirectoryPath(const std::string &path, const char *label, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty()) return setError(errorMessage, std::string(label) + " may not be empty.");
	if (!isWritableDirectory(normalized)) return setError(errorMessage, std::string(label) + " is missing or not writable: " + normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

constexpr int kMinBinaryRecordLength = 1;
constexpr int kMaxBinaryRecordLength = 99999;

bool parseBinaryRecordLengthLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, "BINARY_RECORD_LENGTH must be an integer between 1 and 99999.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, "BINARY_RECORD_LENGTH must be an integer between 1 and 99999.");
	if (parsed < kMinBinaryRecordLength || parsed > kMaxBinaryRecordLength) return setError(errorMessage, "BINARY_RECORD_LENGTH must be between 1 and 99999.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool normalizeCursorStatusColor(const std::string &value, std::string &outValue, std::string *errorMessage) {
	std::string normalized = upperAscii(trimAscii(value));
	unsigned char parsed = 0;
	static const char *const hex = "0123456789ABCDEF";

	if (normalized.empty()) {
		outValue.clear();
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (!parseHexColorToken(normalized, parsed)) return setError(errorMessage, "CURSOR_STATUS_COLOR must be a hex byte (00..FF) or empty.");
	outValue.clear();
	outValue.push_back(hex[(parsed >> 4) & 0x0F]);
	outValue.push_back(hex[parsed & 0x0F]);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseTabSizeLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, "TAB_SIZE must be an integer between 2 and 32.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, "TAB_SIZE must be an integer between 2 and 32.");
	if (parsed < kMinTabSize || parsed > kMaxTabSize) return setError(errorMessage, "TAB_SIZE must be between 2 and 32.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseMiniMapWidthLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, "MINIMAP_WIDTH must be between 2 and 20.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, "MINIMAP_WIDTH must be an integer between 2 and 20.");
	if (parsed < kMinMiniMapWidth || parsed > kMaxMiniMapWidth) return setError(errorMessage, "MINIMAP_WIDTH must be between 2 and 20.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

constexpr int kMinLeftMargin = 1;
constexpr int kMaxLeftMargin = 999;
constexpr int kMinRightMargin = 1;
constexpr int kMaxRightMargin = 999;

bool parseLeftMarginLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, "LEFT_MARGIN must be an integer between 1 and 999.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, "LEFT_MARGIN must be an integer between 1 and 999.");
	if (parsed < kMinLeftMargin || parsed > kMaxLeftMargin) return setError(errorMessage, "LEFT_MARGIN must be between 1 and 999.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool parseRightMarginLiteral(const std::string &value, int &outValue, std::string *errorMessage) {
	std::string text = trimAscii(value);
	char *end = nullptr;
	long parsed = 0;

	if (text.empty()) return setError(errorMessage, "RIGHT_MARGIN must be an integer between 1 and 999.");
	parsed = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || end == nullptr || *end != '\0') return setError(errorMessage, "RIGHT_MARGIN must be an integer between 1 and 999.");
	if (parsed < kMinRightMargin || parsed > kMaxRightMargin) return setError(errorMessage, "RIGHT_MARGIN must be between 1 and 999.");
	outValue = static_cast<int>(parsed);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string normalizePageBreakLiteral(const std::string &value) {
	std::string trimmed = trimAscii(value);

	if (trimmed.empty()) return kDefaultPageBreakLiteral;
	while (trimmed.size() > 2 && trimmed[0] == '\\' && trimmed[1] == '\\')
		trimmed.erase(trimmed.begin());
	if (trimmed == "\\F") return "\\f";
	if (trimmed == "\\N") return "\\n";
	if (trimmed == "\\R") return "\\r";
	if (trimmed == "\\T") return "\\t";
	if (trimmed == "\\f" || trimmed == "\\n" || trimmed == "\\r" || trimmed == "\\t") return trimmed;
	if (trimmed.size() == 1) {
		unsigned char ch = static_cast<unsigned char>(trimmed[0]);
		if (ch == '\f') return "\\f";
		if (ch == '\n') return "\\n";
		if (ch == '\r') return "\\r";
		if (ch == '\t') return "\\t";
	}
	return trimmed;
}

char decodePageBreakLiteral(const std::string &literal) {
	std::string value = normalizePageBreakLiteral(literal);

	if (value == "\\f") return '\f';
	if (value == "\\n") return '\n';
	if (value == "\\r") return '\r';
	if (value == "\\t") return '\t';
	return value.empty() ? '\f' : value[0];
}

std::vector<std::string> parseDefaultExtensions(const std::string &value) {
	std::string text = trimAscii(value);
	std::vector<std::string> out;
	std::string token;

	if (text.size() >= 2 && std::isalpha(static_cast<unsigned char>(text[0])) != 0 && text[1] == ':') text = text.substr(2);
	for (std::size_t i = 0; i <= text.size(); ++i) {
		char ch = i < text.size() ? text[i] : ';';
		if (ch == ';' || ch == ':' || ch == ',' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
			std::string ext = trimAscii(token);
			bool duplicate = false;

			token.clear();
			if (ext.empty()) continue;
			while (!ext.empty() && ext[0] == '.')
				ext.erase(ext.begin());
			ext = trimAscii(ext);
			if (ext.empty()) continue;
			for (const std::string &existing : out)
				if (existing == ext) {
					duplicate = true;
					break;
				}
			if (!duplicate) out.push_back(ext);
			continue;
		}
		token.push_back(ch);
	}
	return out;
}

std::string canonicalDefaultExtensionsLiteral(const std::string &value) {
	std::vector<std::string> list = parseDefaultExtensions(value);
	std::string out;

	for (std::size_t i = 0; i < list.size(); ++i) {
		if (i != 0) out.push_back(';');
		out += list[i];
	}
	return out;
}

bool containsAsciiSpace(const std::string &value) {
	for (char ch : value)
		if (std::isspace(static_cast<unsigned char>(ch)) != 0) return true;
	return false;
}

bool parseAndAssignBooleanLiteral(const std::string &value, bool &target, std::string *errorMessage) {
	bool parsed = false;

	if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
	target = parsed;
	return true;
}

std::string extensionSelectorForPath(std::string_view path) {
	std::string normalized = normalizeDialogPath(std::string(path).c_str());
	std::string_view base = normalized;
	std::size_t sep = base.find_last_of("/\\");

	if (sep != std::string_view::npos) base.remove_prefix(sep + 1);
	std::size_t dot = base.find_last_of('.');
	if (base.empty() || dot == std::string_view::npos || dot + 1 >= base.size()) return std::string();
	return std::string(base.substr(dot + 1));
}

unsigned long long supportedEditProfileOverrideMask() noexcept {
	static constexpr unsigned long long mask = kOvPageBreak | kOvWordDelimiters | kOvDefaultExtensions | kOvTruncateSpaces | kOvEofCtrlZ | kOvEofCrLf | kOvTabExpand | kOvDisplayTabs | kOvTabSize | kOvLeftMargin | kOvRightMargin | kOvFormatRuler | kOvWordWrap | kOvIndentStyle | kOvCodeLanguage | kOvCodeColoring | kOvCodeFoldingFeature | kOvFileType | kOvBinaryRecordLength | kOvPostLoadMacro | kOvPreSaveMacro | kOvDefaultPath | kOvFormatLine | kOvBackupFiles | kOvShowEofMarker | kOvShowEofMarkerEmoji | kOvLineNumZeroFill | kOvLineNumbersPosition | kOvMiniMapPosition | kOvMiniMapWidth | kOvMiniMapMarkerGlyph | kOvGutters | kOvPersistentBlocks | kOvCodeFoldingPosition | kOvColumnBlockMove | kOvDefaultMode | kOvCursorStatusColor;
	return mask;
}

std::string resolvedEditFormatLineValue(const std::string &value, int tabSize, int leftMargin, int rightMargin, int &resolvedLeftMargin, int &resolvedRightMargin) {
	std::string normalized;

	if (normalizeEditFormatLine(value, tabSize, leftMargin, rightMargin, normalized, &resolvedLeftMargin, &resolvedRightMargin, nullptr)) return normalized;
	resolvedLeftMargin = clampEditFormatLeftMargin(leftMargin, rightMargin);
	resolvedRightMargin = clampEditFormatRightMargin(rightMargin);
	return defaultEditFormatLineForTabSize(tabSize, resolvedLeftMargin, resolvedRightMargin);
}

int nextNumericTabFillColumn(int column, int tabSize) noexcept {
	const int normalizedTabSize = clampEditFormatTabSize(tabSize);
	const int safeColumn = std::max(1, column);
	return ((safeColumn - 1) / normalizedTabSize + 1) * normalizedTabSize + 1;
}

} // namespace

const MREditSettingDescriptor *editSettingDescriptors(std::size_t &count) {
	count = std::size(kEditSettingDescriptors);
	return kEditSettingDescriptors;
}

const MREditSettingDescriptor *findEditSettingDescriptorByKey(std::string_view key) {
	return editSettingDescriptorByKeyInternal(std::string(key));
}

const MREditSettingDescriptor *editSettingDescriptorByKeyInternal(const std::string &key) {
	std::string upper = upperAscii(trimAscii(key));

	for (const auto &descriptor : kEditSettingDescriptors)
		if (upper == descriptor.key) return &descriptor;
	return nullptr;
}

std::string normalizeEditExtensionSelector(std::string_view value) {
	return normalizeEditExtensionSelectorValue(std::string(value));
}

bool normalizeEditExtensionSelectors(std::vector<std::string> &selectors, std::string *errorMessage) {
	return normalizeEditExtensionSelectorsInPlace(selectors, errorMessage);
}

std::string normalizeEditExtensionSelectorValue(const std::string &value) {
	std::string normalized = trimAscii(value);

	while (!normalized.empty() && normalized[0] == '.')
		normalized.erase(normalized.begin());
	return trimAscii(normalized);
}

std::string canonicalEditProfileId(const std::string &value) {
	return trimAscii(value);
}

std::string profileIdLookupKey(const std::string &value) {
	return canonicalEditProfileId(value);
}

std::string canonicalEditProfileName(const std::string &value) {
	return trimAscii(value);
}

std::string canonicalWindowColorThemeUri(const std::string &value) {
	std::string trimmed = trimAscii(value);
	if (trimmed.empty()) return std::string();
	return normalizeConfiguredPathInput(trimmed);
}

bool normalizeEditExtensionSelectorsInPlace(std::vector<std::string> &selectors, std::string *errorMessage) {
	std::vector<std::string> normalized;
	std::set<std::string> seen;

	normalized.reserve(selectors.size());
	for (const std::string &selector : selectors) {
		std::string ext = normalizeEditExtensionSelectorValue(selector);

		if (ext.empty()) continue;
		if (containsAsciiSpace(ext)) return setError(errorMessage, "Extensions may not contain whitespace.");
		if (ext.find('/') != std::string::npos || ext.find('\\') != std::string::npos) return setError(errorMessage, "Extensions may not contain path separators.");
		if (seen.insert(ext).second) normalized.push_back(ext);
	}
	selectors.swap(normalized);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

int clampEditFormatTabSize(int tabSize) noexcept {
	return std::max(2, std::min(tabSize, 32));
}

int clampEditFormatRightMargin(int rightMargin) noexcept {
	return std::max(1, std::min(rightMargin, 999));
}

int clampEditFormatLeftMargin(int leftMargin, int rightMargin) noexcept {
	const int normalizedRightMargin = clampEditFormatRightMargin(rightMargin);
	if (normalizedRightMargin <= 1) return 1;
	return std::max(1, std::min(leftMargin, normalizedRightMargin - 1));
}

std::string defaultEditFormatLineForTabSize(int tabSize, int leftMargin, int rightMargin) {
	const int normalizedTabSize = clampEditFormatTabSize(tabSize);
	const int normalizedRightMargin = clampEditFormatRightMargin(rightMargin);
	const int normalizedLeftMargin = clampEditFormatLeftMargin(leftMargin, normalizedRightMargin);
	std::string out(static_cast<std::size_t>(normalizedRightMargin), '.');

	if (normalizedRightMargin <= 1) {
		out[0] = 'R';
		return out;
	}
	for (int col = normalizedTabSize; col <= normalizedRightMargin; col += normalizedTabSize)
		if (col > normalizedLeftMargin && col < normalizedRightMargin) out[static_cast<std::size_t>(col - 1)] = '|';
	out[static_cast<std::size_t>(normalizedLeftMargin - 1)] = 'L';
	out[static_cast<std::size_t>(normalizedRightMargin - 1)] = 'R';
	return out;
}

bool normalizeEditFormatLine(const std::string &value, int tabSize, int fallbackLeftMargin, int fallbackRightMargin, std::string &outValue, int *outLeftMargin, int *outRightMargin, std::string *errorMessage) {
	std::string out = value;
	const int normalizedFallbackRightMargin = clampEditFormatRightMargin(fallbackRightMargin);
	const int normalizedFallbackLeftMargin = clampEditFormatLeftMargin(fallbackLeftMargin, normalizedFallbackRightMargin);
	int lCount = 0;
	int rCount = 0;
	int lIndex = -1;
	int rIndex = -1;

	if (out.empty()) {
		outValue = defaultEditFormatLineForTabSize(tabSize, normalizedFallbackLeftMargin, normalizedFallbackRightMargin);
		if (outLeftMargin != nullptr) *outLeftMargin = normalizedFallbackLeftMargin;
		if (outRightMargin != nullptr) *outRightMargin = normalizedFallbackRightMargin;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	{
		bool legacy = true;
		for (char ch : out)
			if (ch != '!' && ch != '-') {
				legacy = false;
				break;
			}
		if (legacy) {
			outValue = defaultEditFormatLineForTabSize(tabSize, normalizedFallbackLeftMargin, normalizedFallbackRightMargin);
			if (outLeftMargin != nullptr) *outLeftMargin = normalizedFallbackLeftMargin;
			if (outRightMargin != nullptr) *outRightMargin = normalizedFallbackRightMargin;
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
	}
	for (char &ch : out)
		if (ch == ' ') ch = '.';
	for (std::size_t i = 0; i < out.size(); ++i) {
		char ch = out[i];
		if (ch != '.' && ch != '|' && ch != 'L' && ch != 'R') return setError(errorMessage, "FORMAT_LINE may only contain '.', ' ', '|', 'L' and 'R'.");
		if (ch == 'L') {
			++lCount;
			lIndex = static_cast<int>(i);
		}
		if (ch == 'R') {
			++rCount;
			rIndex = static_cast<int>(i);
		}
	}
	if (lCount > 1) return setError(errorMessage, "FORMAT_LINE must contain at most one 'L'.");
	if (rCount != 1) return setError(errorMessage, "FORMAT_LINE must contain exactly one 'R'.");
	if (lCount == 0) lIndex = 0;
	if (lIndex >= rIndex && rIndex > 0) return setError(errorMessage, "FORMAT_LINE must place 'L' before 'R'.");
	out.resize(static_cast<std::size_t>(rIndex + 1), '.');
	if (rIndex > 0) out[static_cast<std::size_t>(lIndex)] = 'L';
	out[static_cast<std::size_t>(rIndex)] = 'R';
	outValue = out;
	if (outLeftMargin != nullptr) *outLeftMargin = rIndex > 0 ? lIndex + 1 : 1;
	if (outRightMargin != nullptr) *outRightMargin = rIndex + 1;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string synchronizeEditFormatLineMargins(const std::string &value, int leftMargin, int rightMargin, int tabSize) {
	std::string normalized;
	int oldLeftMargin = 1;
	int oldRightMargin = 1;
	const int normalizedRightMargin = clampEditFormatRightMargin(rightMargin);
	const int normalizedLeftMargin = clampEditFormatLeftMargin(leftMargin, normalizedRightMargin);
	std::string out;
	const int delta = normalizedLeftMargin - oldLeftMargin;

	if (!normalizeEditFormatLine(value, tabSize, normalizedLeftMargin, normalizedRightMargin, normalized, &oldLeftMargin, &oldRightMargin, nullptr)) return defaultEditFormatLineForTabSize(tabSize, normalizedLeftMargin, normalizedRightMargin);
	out = std::string(static_cast<std::size_t>(normalizedRightMargin), '.');
	if (normalizedRightMargin <= 1) {
		out[0] = 'R';
		return out;
	}
	out[static_cast<std::size_t>(normalizedLeftMargin - 1)] = 'L';
	out[static_cast<std::size_t>(normalizedRightMargin - 1)] = 'R';
	for (int i = 0; i < static_cast<int>(normalized.size()); ++i) {
		const char ch = normalized[static_cast<std::size_t>(i)];
		const int shifted = i + delta;
		const int column = shifted + 1;

		if (ch != '|') continue;
		if (column <= normalizedLeftMargin || column >= normalizedRightMargin) continue;
		if (shifted < 0 || shifted >= normalizedRightMargin) continue;
		out[static_cast<std::size_t>(shifted)] = '|';
	}
	return out;
}

bool editFormatLineAtColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column, char symbol, std::string &outValue, int *outLeftMargin, int *outRightMargin, std::string *errorMessage) {
	std::string normalized;
	std::string edited;
	int currentLeftMargin = leftMargin;
	int currentRightMargin = rightMargin;
	const char normalizedSymbol = symbol == ' ' ? '.' : symbol;
	const int safeColumn = std::max(1, std::min(column, 999));

	if (normalizedSymbol != '.' && normalizedSymbol != '|' && normalizedSymbol != 'L' && normalizedSymbol != 'R') return setError(errorMessage, "FORMAT_LINE editor accepts only '.', ' ', '|', 'L' and 'R'.");
	if (!normalizeEditFormatLine(value, tabSize, leftMargin, rightMargin, normalized, &currentLeftMargin, &currentRightMargin, errorMessage)) return false;
	edited = normalized;
	if (static_cast<int>(edited.size()) < safeColumn) edited.append(static_cast<std::size_t>(safeColumn - static_cast<int>(edited.size())), '.');
	if (normalizedSymbol == 'L') {
		if (safeColumn >= currentRightMargin) return setError(errorMessage, "FORMAT_LINE must place 'L' before 'R'.");
		for (char &ch : edited)
			if (ch == 'L') ch = '.';
		edited[static_cast<std::size_t>(safeColumn - 1)] = 'L';
	} else if (normalizedSymbol == 'R') {
		if (safeColumn <= currentLeftMargin) return setError(errorMessage, "FORMAT_LINE must place 'R' after 'L'.");
		for (char &ch : edited)
			if (ch == 'R') ch = '.';
		edited.resize(static_cast<std::size_t>(safeColumn), '.');
		edited[static_cast<std::size_t>(safeColumn - 1)] = 'R';
	} else if (normalizedSymbol == '|') {
		if (safeColumn <= currentLeftMargin || safeColumn >= currentRightMargin) {
			outValue = normalized;
			if (outLeftMargin != nullptr) *outLeftMargin = currentLeftMargin;
			if (outRightMargin != nullptr) *outRightMargin = currentRightMargin;
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
		edited[static_cast<std::size_t>(safeColumn - 1)] = '|';
	} else {
		if (safeColumn != currentLeftMargin && safeColumn != currentRightMargin && safeColumn <= static_cast<int>(edited.size())) edited[static_cast<std::size_t>(safeColumn - 1)] = '.';
	}
	return normalizeEditFormatLine(edited, tabSize, currentLeftMargin, currentRightMargin, outValue, outLeftMargin, outRightMargin, errorMessage);
}

bool translateEditFormatLine(const std::string &value, int tabSize, int leftMargin, int rightMargin, int deltaColumns, std::string &outValue, int *outLeftMargin, int *outRightMargin, std::string *errorMessage) {
	std::string normalized;
	int currentLeftMargin = leftMargin;
	int currentRightMargin = rightMargin;
	int clampedDelta = deltaColumns;
	std::string translated;

	if (!normalizeEditFormatLine(value, tabSize, leftMargin, rightMargin, normalized, &currentLeftMargin, &currentRightMargin, errorMessage)) return false;
	clampedDelta = std::max(1 - currentLeftMargin, std::min(deltaColumns, 999 - currentRightMargin));
	if (currentRightMargin <= 1) {
		outValue = "R";
		if (outLeftMargin != nullptr) *outLeftMargin = 1;
		if (outRightMargin != nullptr) *outRightMargin = 1;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	translated.assign(static_cast<std::size_t>(currentRightMargin + clampedDelta), '.');
	translated[static_cast<std::size_t>(currentLeftMargin + clampedDelta - 1)] = 'L';
	translated[static_cast<std::size_t>(currentRightMargin + clampedDelta - 1)] = 'R';
	for (int i = 0; i < static_cast<int>(normalized.size()); ++i) {
		const int shiftedColumn = i + clampedDelta + 1;
		if (normalized[static_cast<std::size_t>(i)] != '|') continue;
		if (shiftedColumn <= currentLeftMargin + clampedDelta || shiftedColumn >= currentRightMargin + clampedDelta) continue;
		translated[static_cast<std::size_t>(shiftedColumn - 1)] = '|';
	}
	return normalizeEditFormatLine(translated, tabSize, currentLeftMargin + clampedDelta, currentRightMargin + clampedDelta, outValue, outLeftMargin, outRightMargin, errorMessage);
}

int nextResolvedEditFormatTabStopColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column) {
	int resolvedLeftMargin = 1;
	int resolvedRightMargin = 1;
	const std::string normalized = resolvedEditFormatLineValue(value, tabSize, leftMargin, rightMargin, resolvedLeftMargin, resolvedRightMargin);
	const int safeColumn = std::max(1, column);

	for (int i = safeColumn; i < static_cast<int>(normalized.size()); ++i)
		if (normalized[static_cast<std::size_t>(i)] == '|') return i + 1;
	return safeColumn;
}

int prevResolvedEditFormatTabStopColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column) {
	int resolvedLeftMargin = 1;
	int resolvedRightMargin = 1;
	const std::string normalized = resolvedEditFormatLineValue(value, tabSize, leftMargin, rightMargin, resolvedLeftMargin, resolvedRightMargin);
	const int safeColumn = std::max(1, column);
	int i = std::min(std::max(0, safeColumn - 2), static_cast<int>(normalized.size()) - 1);

	for (; i >= 0; --i)
		if (normalized[static_cast<std::size_t>(i)] == '|') return i + 1;
	if (safeColumn > resolvedLeftMargin) return resolvedLeftMargin;
	return safeColumn;
}

int resolvedEditFormatTabDisplayColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column) {
	const int safeColumn = std::max(1, column);
	const int resolvedTabStop = nextResolvedEditFormatTabStopColumn(value, tabSize, leftMargin, rightMargin, safeColumn);

	if (resolvedTabStop > safeColumn) return resolvedTabStop;
	return nextNumericTabFillColumn(safeColumn, tabSize);
}

int resolvedEditFormatIndentColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int preferredColumn) {
	int resolvedLeftMargin = 1;
	int resolvedRightMargin = 1;
	const std::string normalized = resolvedEditFormatLineValue(value, tabSize, leftMargin, rightMargin, resolvedLeftMargin, resolvedRightMargin);
	const int safePreferredColumn = std::max(1, preferredColumn);
	int resolvedColumn = resolvedLeftMargin;

	if (safePreferredColumn <= resolvedLeftMargin) return resolvedLeftMargin;
	for (int i = 0; i < static_cast<int>(normalized.size()); ++i) {
		if (normalized[static_cast<std::size_t>(i)] != '|') continue;
		if (i + 1 > safePreferredColumn) break;
		resolvedColumn = i + 1;
	}
	return resolvedColumn;
}

std::string buildEditIndentFill(const MREditSetupSettings &settings, int startColumn, int targetColumn, bool preferTabs) {
	std::string out;
	int currentColumn = std::max(1, startColumn);
	const int safeTargetColumn = std::max(currentColumn, targetColumn);

	while (currentColumn < safeTargetColumn) {
		const int nextTabColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
		if (preferTabs && nextTabColumn <= safeTargetColumn) {
			out.push_back('\t');
			currentColumn = nextTabColumn;
		} else {
			out.push_back(' ');
			++currentColumn;
		}
	}
	return out;
}

bool applyEditSetupValueInternal(MREditSetupSettings &current, const std::string &keyName, const std::string &value, std::string *errorMessage) {
	std::string upperKeyName = upperAscii(trimAscii(keyName));
	std::string normalized;

	if (upperKeyName == "PAGE_BREAK") current.pageBreak = normalizePageBreakLiteral(value);
	else if (upperKeyName == "WORD_DELIMITERS") {
		if (trimAscii(value).empty()) current.wordDelimiters = resolveEditSetupDefaults().wordDelimiters;
		else
			current.wordDelimiters = value;
	} else if (upperKeyName == "DEFAULT_EXTENSIONS")
		current.defaultExtensions = canonicalDefaultExtensionsLiteral(value);
	else if (upperKeyName == "TRUNCATE_SPACES") {
		if (!parseAndAssignBooleanLiteral(value, current.truncateSpaces, errorMessage)) return false;
	} else if (upperKeyName == "EOF_CTRL_Z") {
		if (!parseAndAssignBooleanLiteral(value, current.eofCtrlZ, errorMessage)) return false;
	} else if (upperKeyName == "EOF_CR_LF") {
		if (!parseAndAssignBooleanLiteral(value, current.eofCrLf, errorMessage)) return false;
	} else if (upperKeyName == "TAB_EXPAND") {
		if (!parseAndAssignBooleanLiteral(value, current.tabExpand, errorMessage)) return false;
	} else if (upperKeyName == "DISPLAY_TABS") {
		if (!parseAndAssignBooleanLiteral(value, current.displayTabs, errorMessage)) return false;
	} else if (upperKeyName == "TAB_SIZE") {
		int tabSize = 0;
		if (!parseTabSizeLiteral(value, tabSize, errorMessage)) return false;
		current.tabSize = tabSize;
		current.formatLine = defaultEditFormatLineForTabSize(current.tabSize, current.leftMargin, current.rightMargin);
	} else if (upperKeyName == "LEFT_MARGIN") {
		int leftMargin = 0;
		if (!parseLeftMarginLiteral(value, leftMargin, errorMessage)) return false;
		current.leftMargin = leftMargin;
		current.formatLine = synchronizeEditFormatLineMargins(current.formatLine, current.leftMargin, current.rightMargin, current.tabSize);
	} else if (upperKeyName == "RIGHT_MARGIN") {
		int rightMargin = 0;
		if (!parseRightMarginLiteral(value, rightMargin, errorMessage)) return false;
		current.rightMargin = rightMargin;
		current.formatLine = synchronizeEditFormatLineMargins(current.formatLine, current.leftMargin, current.rightMargin, current.tabSize);
	} else if (upperKeyName == "FORMAT_RULER") {
		if (!parseAndAssignBooleanLiteral(value, current.formatRuler, errorMessage)) return false;
	} else if (upperKeyName == "WORD_WRAP") {
		if (!parseAndAssignBooleanLiteral(value, current.wordWrap, errorMessage)) return false;
	} else if (upperKeyName == "INDENT_STYLE") {
		normalized = normalizeIndentStyle(value);
		if (normalized.empty()) return setError(errorMessage, "INDENT_STYLE must be OFF, AUTOMATIC or SMART.");
		current.indentStyle = normalized;
	} else if (upperKeyName == "CODE_LANGUAGE") {
		normalized = upperAscii(trimAscii(value));
		if (normalized.empty()) normalized = "NONE";
		if (normalized != "NONE" && normalized != "AUTO" && normalized != "C" && normalized != "CPP" && normalized != "PYTHON" && normalized != "JAVASCRIPT" && normalized != "TYPESCRIPT" && normalized != "TSX" &&
			normalized != "BASH" && normalized != "ZSH" && normalized != "FISH" && normalized != "JSON" && normalized != "YAML" && normalized != "XML" && normalized != "PERL" && normalized != "SWIFT" &&
			normalized != "RUST" && normalized != "GO" && normalized != "PASCAL" && normalized != "SYSTEMD" && normalized != "MAKE" && normalized != "MRMAC" && normalized != "MARKDOWN" && normalized != "LATEX" &&
			normalized != "KOTLIN" && normalized != "CSHARP")
			return setError(errorMessage, "CODE_LANGUAGE must be NONE, AUTO, C, CPP, PYTHON, JAVASCRIPT, TYPESCRIPT, TSX, BASH, ZSH, FISH, JSON, YAML, XML, PERL, SWIFT, RUST, GO, PASCAL, SYSTEMD, MAKE, MRMAC, MARKDOWN, LATEX, KOTLIN or CSHARP.");
		current.codeLanguage = normalized;
	} else if (upperKeyName == "CODE_COLORING") {
		if (!parseAndAssignBooleanLiteral(value, current.codeColoring, errorMessage)) return false;
	} else if (upperKeyName == "CODE_FOLDING") {
		if (!parseAndAssignBooleanLiteral(value, current.codeFoldingFeature, errorMessage)) return false;
	} else if (upperKeyName == "FILE_TYPE") {
		normalized = normalizeFileType(value);
		if (normalized.empty()) return setError(errorMessage, "FILE_TYPE must be LEGACY_TEXT, UNIX or BINARY.");
		current.fileType = normalized;
	} else if (upperKeyName == "BINARY_RECORD_LENGTH") {
		int binaryRecordLength = 0;
		if (!parseBinaryRecordLengthLiteral(value, binaryRecordLength, errorMessage)) return false;
		current.binaryRecordLength = binaryRecordLength;
	} else if (upperKeyName == "POST_LOAD_MACRO")
		current.postLoadMacro = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "PRE_SAVE_MACRO")
		current.preSaveMacro = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "DEFAULT_PATH")
		current.defaultPath = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "FORMAT_LINE") {
		int leftMargin = current.leftMargin;
		int rightMargin = current.rightMargin;
		if (!normalizeEditFormatLine(value, current.tabSize, current.leftMargin, current.rightMargin, normalized, &leftMargin, &rightMargin, errorMessage)) return false;
		current.formatLine = normalized;
		current.leftMargin = leftMargin;
		current.rightMargin = rightMargin;
	} else if (upperKeyName == "BACKUP_FILES") {
		if (!parseAndAssignBooleanLiteral(value, current.backupFiles, errorMessage)) return false;
		if (!current.backupFiles) current.backupMethod = kBackupMethodOff;
		else if (normalizeBackupMethod(current.backupMethod).empty() || current.backupMethod == kBackupMethodOff)
			current.backupMethod = kBackupMethodBakFile;
	} else if (upperKeyName == "BACKUP_METHOD") {
		normalized = normalizeBackupMethod(value);
		if (normalized.empty()) return setError(errorMessage, "BACKUP_METHOD must be OFF, BAK_FILE or DIRECTORY.");
		current.backupMethod = normalized;
		current.backupFiles = normalized != kBackupMethodOff;
	} else if (upperKeyName == "BACKUP_FREQUENCY") {
		normalized = normalizeBackupFrequency(value);
		if (normalized.empty()) return setError(errorMessage, "BACKUP_FREQUENCY must be FIRST_SAVE_ONLY or EVERY_SAVE.");
		current.backupFrequency = normalized;
	} else if (upperKeyName == "BACKUP_EXTENSION")
		current.backupExtension = normalizeBackupExtension(value);
	else if (upperKeyName == "BACKUP_DIRECTORY")
		current.backupDirectory = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
	else if (upperKeyName == "AUTOSAVE_INACTIVITY_SECONDS") {
		int parsedSeconds = 0;
		if (!normalizeAutosaveSeconds(value, kMinAutosaveInactivitySeconds, kMaxAutosaveInactivitySeconds, parsedSeconds, "AUTOSAVE_INACTIVITY_SECONDS", errorMessage)) return false;
		current.autosaveInactivitySeconds = parsedSeconds;
	} else if (upperKeyName == "AUTOSAVE_INTERVAL_SECONDS") {
		int parsedSeconds = 0;
		if (!normalizeAutosaveSeconds(value, kMinAutosaveIntervalSeconds, kMaxAutosaveIntervalSeconds, parsedSeconds, "AUTOSAVE_INTERVAL_SECONDS", errorMessage)) return false;
		current.autosaveIntervalSeconds = parsedSeconds;
	} else if (upperKeyName == "SHOW_EOF_MARKER") {
		if (!parseAndAssignBooleanLiteral(value, current.showEofMarker, errorMessage)) return false;
	} else if (upperKeyName == "SHOW_EOF_MARKER_EMOJI") {
		if (!parseAndAssignBooleanLiteral(value, current.showEofMarkerEmoji, errorMessage)) return false;
	} else if (upperKeyName == "LINE_NUMBERS_POSITION") {
		normalized = normalizeLineNumbersPosition(value);
		if (normalized.empty()) return setError(errorMessage, "LINE_NUMBERS_POSITION must be OFF, LEADING or TRAILING.");
		current.lineNumbersPosition = normalized;
		current.showLineNumbers = normalized != kLineNumbersPositionOff;
	} else if (upperKeyName == "LINE_NUM_ZERO_FILL") {
		if (!parseAndAssignBooleanLiteral(value, current.lineNumZeroFill, errorMessage)) return false;
	} else if (upperKeyName == "MINIMAP_POSITION") {
		normalized = normalizeMiniMapPosition(value);
		if (normalized.empty()) return setError(errorMessage, "MINIMAP_POSITION must be OFF, LEADING or TRAILING.");
		current.miniMapPosition = normalized;
	} else if (upperKeyName == "MINIMAP_WIDTH") {
		int miniMapWidth = 0;
		if (!parseMiniMapWidthLiteral(value, miniMapWidth, errorMessage)) return false;
		current.miniMapWidth = miniMapWidth;
	} else if (upperKeyName == "MINIMAP_MARKER_GLYPH") {
		if (!normalizeMiniMapMarkerGlyph(value, normalized, errorMessage)) return false;
		current.miniMapMarkerGlyph = normalized;
	} else if (upperKeyName == "GUTTERS")
		current.gutters = normalizeGuttersOrder(value);
	else if (upperKeyName == "PERSISTENT_BLOCKS") {
		if (!parseAndAssignBooleanLiteral(value, current.persistentBlocks, errorMessage)) return false;
	} else if (upperKeyName == "CODE_FOLDING_POSITION") {
		normalized = normalizeCodeFoldingPosition(value);
		if (normalized.empty()) return setError(errorMessage, "CODE_FOLDING_POSITION must be OFF, LEADING or TRAILING.");
		current.codeFoldingPosition = normalized;
		current.codeFolding = normalized != kCodeFoldingPositionOff;
	} else if (upperKeyName == "BLOCK_MOVE") {
		normalized = normalizeColumnBlockMove(value);
		if (normalized.empty()) return setError(errorMessage, "BLOCK_MOVE must be DELETE_SPACE or LEAVE_SPACE.");
		current.columnBlockMove = normalized;
	} else if (upperKeyName == "DEFAULT_MODE") {
		normalized = normalizeDefaultMode(value);
		if (normalized.empty()) return setError(errorMessage, "DEFAULT_MODE must be INSERT or OVERWRITE.");
		current.defaultMode = normalized;
	} else if (upperKeyName == "CURSOR_STATUS_COLOR") {
		if (!normalizeCursorStatusColor(value, normalized, errorMessage)) return false;
		current.cursorStatusColor = normalized;
	} else
		return setError(errorMessage, "Unknown edit setting key.");

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string editSetupValueLiteral(const MREditSetupSettings &settings, const char *key) {
	std::string upperKey = upperAscii(trimAscii(key != nullptr ? key : ""));

	if (upperKey == "PAGE_BREAK") return settings.pageBreak;
	if (upperKey == "WORD_DELIMITERS") return settings.wordDelimiters;
	if (upperKey == "DEFAULT_EXTENSIONS") return settings.defaultExtensions;
	if (upperKey == "TRUNCATE_SPACES") return formatEditSetupBoolean(settings.truncateSpaces);
	if (upperKey == "EOF_CTRL_Z") return formatEditSetupBoolean(settings.eofCtrlZ);
	if (upperKey == "EOF_CR_LF") return formatEditSetupBoolean(settings.eofCrLf);
	if (upperKey == "TAB_EXPAND") return formatEditSetupBoolean(settings.tabExpand);
	if (upperKey == "DISPLAY_TABS") return formatEditSetupBoolean(settings.displayTabs);
	if (upperKey == "TAB_SIZE") return std::to_string(settings.tabSize);
	if (upperKey == "LEFT_MARGIN") return std::to_string(settings.leftMargin);
	if (upperKey == "RIGHT_MARGIN") return std::to_string(settings.rightMargin);
	if (upperKey == "FORMAT_RULER") return formatEditSetupBoolean(settings.formatRuler);
	if (upperKey == "WORD_WRAP") return formatEditSetupBoolean(settings.wordWrap);
	if (upperKey == "INDENT_STYLE") return settings.indentStyle;
	if (upperKey == "CODE_LANGUAGE") return settings.codeLanguage;
	if (upperKey == "CODE_COLORING") return formatEditSetupBoolean(settings.codeColoring);
	if (upperKey == "CODE_FOLDING") return formatEditSetupBoolean(settings.codeFoldingFeature);
	if (upperKey == "FILE_TYPE") return settings.fileType;
	if (upperKey == "BINARY_RECORD_LENGTH") return std::to_string(settings.binaryRecordLength);
	if (upperKey == "POST_LOAD_MACRO") return settings.postLoadMacro;
	if (upperKey == "PRE_SAVE_MACRO") return settings.preSaveMacro;
	if (upperKey == "DEFAULT_PATH") return settings.defaultPath;
	if (upperKey == "FORMAT_LINE") return settings.formatLine;
	if (upperKey == "BACKUP_FILES") return formatEditSetupBoolean(settings.backupFiles);
	if (upperKey == "BACKUP_METHOD") return settings.backupMethod;
	if (upperKey == "BACKUP_FREQUENCY") return settings.backupFrequency;
	if (upperKey == "BACKUP_EXTENSION") return settings.backupExtension;
	if (upperKey == "BACKUP_DIRECTORY") return settings.backupDirectory;
	if (upperKey == "AUTOSAVE_INACTIVITY_SECONDS") return std::to_string(settings.autosaveInactivitySeconds);
	if (upperKey == "AUTOSAVE_INTERVAL_SECONDS") return std::to_string(settings.autosaveIntervalSeconds);
	if (upperKey == "SHOW_EOF_MARKER") return formatEditSetupBoolean(settings.showEofMarker);
	if (upperKey == "SHOW_EOF_MARKER_EMOJI") return formatEditSetupBoolean(settings.showEofMarkerEmoji);
	if (upperKey == "LINE_NUMBERS_POSITION") return settings.lineNumbersPosition;
	if (upperKey == "LINE_NUM_ZERO_FILL") return formatEditSetupBoolean(settings.lineNumZeroFill);
	if (upperKey == "MINIMAP_POSITION") return settings.miniMapPosition;
	if (upperKey == "MINIMAP_WIDTH") return std::to_string(settings.miniMapWidth);
	if (upperKey == "MINIMAP_MARKER_GLYPH") return settings.miniMapMarkerGlyph;
	if (upperKey == "GUTTERS") return settings.gutters;
	if (upperKey == "PERSISTENT_BLOCKS") return formatEditSetupBoolean(settings.persistentBlocks);
	if (upperKey == "CODE_FOLDING_POSITION") return settings.codeFoldingPosition;
	if (upperKey == "BLOCK_MOVE") return settings.columnBlockMove;
	if (upperKey == "DEFAULT_MODE") return settings.defaultMode;
	if (upperKey == "CURSOR_STATUS_COLOR") return settings.cursorStatusColor;
	return std::string();
}

bool normalizeEditProfileOverridesInPlace(MREditExtensionProfile &profile, std::string *errorMessage) {
	std::size_t descriptorCount = 0;
	const MREditSettingDescriptor *descriptors = editSettingDescriptors(descriptorCount);
	MREditSetupSettings normalizedValues = resolveEditSetupDefaults();
	unsigned long long mask = profile.overrides.mask;

	if ((mask & ~supportedEditProfileOverrideMask()) != 0) return setError(errorMessage, "Extension profile override mask contains unsupported bits.");
	for (std::size_t i = 0; i < descriptorCount; ++i) {
		const MREditSettingDescriptor &descriptor = descriptors[i];

		if (!descriptor.profileSupported) continue;
		if ((mask & descriptor.overrideBit) == 0) continue;
		if (!applyEditSetupValueInternal(normalizedValues, descriptor.key, editSetupValueLiteral(profile.overrides.values, descriptor.key), errorMessage)) return false;
	}

	profile.overrides.mask = mask;
	profile.overrides.values = normalizedValues;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool validateNormalizedEditProfiles(const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage) {
	std::set<std::string> profileIds;
	std::map<std::string, std::string> selectorOwners;

	for (const MREditExtensionProfile &profile : profiles) {
		std::string id = canonicalEditProfileId(profile.id);
		std::string name = canonicalEditProfileName(profile.name);
		std::string lookup = profileIdLookupKey(id);

		if (id.empty()) return setError(errorMessage, "Extension profile id may not be empty.");
		if (name.empty()) return setError(errorMessage, "Extension profile name may not be empty.");
		if (!profileIds.insert(lookup).second) return setError(errorMessage, "Duplicate extension profile id: " + id + " (" + name + ")");
		if (!profile.windowColorThemeUri.empty() && !validateColorThemeFilePath(profile.windowColorThemeUri, errorMessage)) return false;
		for (const std::string &ext : profile.extensions) {
			std::string ownerLabel = id + " (" + name + ")";
			auto it = selectorOwners.find(ext);
			if (it != selectorOwners.end()) return setError(errorMessage, "Duplicate profile extension '" + ext + "': " + it->second + " and " + ownerLabel);
			selectorOwners.insert(std::make_pair(ext, ownerLabel));
		}
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

MREditSetupSettings resolveEditSetupDefaults() {
	MREditSetupSettings defaults;

	defaults.pageBreak = kDefaultPageBreakLiteral;
	defaults.wordDelimiters = kDefaultWordDelimiters;
	defaults.defaultExtensions = kDefaultDefaultExtensions;
	defaults.truncateSpaces = true;
	defaults.eofCtrlZ = false;
	defaults.eofCrLf = false;
	defaults.tabExpand = true;
	defaults.displayTabs = false;
	defaults.tabSize = kDefaultTabSize;
	defaults.leftMargin = 1;
	defaults.rightMargin = 78;
	defaults.formatRuler = false;
	defaults.wordWrap = true;
	defaults.indentStyle = kIndentStyleOff;
	defaults.codeLanguage = "NONE";
	defaults.codeColoring = false;
	defaults.codeFoldingFeature = false;
	defaults.fileType = kFileTypeUnix;
	defaults.binaryRecordLength = 100;
	defaults.postLoadMacro.clear();
	defaults.preSaveMacro.clear();
	defaults.defaultPath.clear();
	defaults.formatLine = defaultEditFormatLineForTabSize(defaults.tabSize, defaults.leftMargin, defaults.rightMargin);
	defaults.backupMethod = kBackupMethodBakFile;
	defaults.backupFrequency = kBackupFrequencyFirstSaveOnly;
	defaults.backupExtension = "bak";
	defaults.backupDirectory.clear();
	defaults.autosaveInactivitySeconds = 15;
	defaults.autosaveIntervalSeconds = 180;
	defaults.backupFiles = true;
	defaults.showEofMarker = false;
	defaults.showEofMarkerEmoji = true;
	defaults.showLineNumbers = false;
	defaults.lineNumbersPosition = kLineNumbersPositionOff;
	defaults.lineNumZeroFill = false;
	defaults.persistentBlocks = true;
	defaults.codeFolding = false;
	defaults.codeFoldingPosition = kCodeFoldingPositionOff;
	defaults.columnBlockMove = kColumnBlockMoveDelete;
	defaults.defaultMode = kDefaultModeInsert;
	defaults.cursorStatusColor.clear();
	defaults.miniMapPosition = kMiniMapPositionOff;
	defaults.miniMapWidth = kDefaultMiniMapWidth;
	defaults.miniMapMarkerGlyph = "│";
	defaults.gutters = kDefaultGuttersOrder;
	return defaults;
}

MREditSetupSettings mergeEditSetupSettings(const MREditSetupSettings &defaults, const MREditSetupOverrides &overrides) {
	MREditSetupSettings merged = defaults;

	if ((overrides.mask & kOvPageBreak) != 0) merged.pageBreak = overrides.values.pageBreak;
	if ((overrides.mask & kOvWordDelimiters) != 0) merged.wordDelimiters = overrides.values.wordDelimiters;
	if ((overrides.mask & kOvDefaultExtensions) != 0) merged.defaultExtensions = overrides.values.defaultExtensions;
	if ((overrides.mask & kOvTruncateSpaces) != 0) merged.truncateSpaces = overrides.values.truncateSpaces;
	if ((overrides.mask & kOvEofCtrlZ) != 0) merged.eofCtrlZ = overrides.values.eofCtrlZ;
	if ((overrides.mask & kOvEofCrLf) != 0) merged.eofCrLf = overrides.values.eofCrLf;
	if ((overrides.mask & kOvTabExpand) != 0) merged.tabExpand = overrides.values.tabExpand;
	if ((overrides.mask & kOvDisplayTabs) != 0) merged.displayTabs = overrides.values.displayTabs;
	if ((overrides.mask & kOvTabSize) != 0) merged.tabSize = overrides.values.tabSize;
	if ((overrides.mask & kOvLeftMargin) != 0) merged.leftMargin = overrides.values.leftMargin;
	if ((overrides.mask & kOvRightMargin) != 0) merged.rightMargin = overrides.values.rightMargin;
	if ((overrides.mask & kOvFormatRuler) != 0) merged.formatRuler = overrides.values.formatRuler;
	if ((overrides.mask & kOvWordWrap) != 0) merged.wordWrap = overrides.values.wordWrap;
	if ((overrides.mask & kOvIndentStyle) != 0) merged.indentStyle = overrides.values.indentStyle;
	if ((overrides.mask & kOvCodeLanguage) != 0) merged.codeLanguage = overrides.values.codeLanguage;
	if ((overrides.mask & kOvCodeColoring) != 0) merged.codeColoring = overrides.values.codeColoring;
	if ((overrides.mask & kOvCodeFoldingFeature) != 0) merged.codeFoldingFeature = overrides.values.codeFoldingFeature;
	if ((overrides.mask & kOvFileType) != 0) merged.fileType = overrides.values.fileType;
	if ((overrides.mask & kOvBinaryRecordLength) != 0) merged.binaryRecordLength = overrides.values.binaryRecordLength;
	if ((overrides.mask & kOvPostLoadMacro) != 0) merged.postLoadMacro = overrides.values.postLoadMacro;
	if ((overrides.mask & kOvPreSaveMacro) != 0) merged.preSaveMacro = overrides.values.preSaveMacro;
	if ((overrides.mask & kOvDefaultPath) != 0) merged.defaultPath = overrides.values.defaultPath;
	if ((overrides.mask & kOvFormatLine) != 0) merged.formatLine = overrides.values.formatLine;
	if ((overrides.mask & kOvBackupFiles) != 0) merged.backupFiles = overrides.values.backupFiles;
	if ((overrides.mask & kOvBackupMethod) != 0) merged.backupMethod = overrides.values.backupMethod;
	if ((overrides.mask & kOvBackupFrequency) != 0) merged.backupFrequency = overrides.values.backupFrequency;
	if ((overrides.mask & kOvBackupExtension) != 0) merged.backupExtension = overrides.values.backupExtension;
	if ((overrides.mask & kOvBackupDirectory) != 0) merged.backupDirectory = overrides.values.backupDirectory;
	if ((overrides.mask & kOvAutosaveInactivitySeconds) != 0) merged.autosaveInactivitySeconds = overrides.values.autosaveInactivitySeconds;
	if ((overrides.mask & kOvAutosaveIntervalSeconds) != 0) merged.autosaveIntervalSeconds = overrides.values.autosaveIntervalSeconds;
	if ((overrides.mask & kOvShowEofMarker) != 0) merged.showEofMarker = overrides.values.showEofMarker;
	if ((overrides.mask & kOvShowEofMarkerEmoji) != 0) merged.showEofMarkerEmoji = overrides.values.showEofMarkerEmoji;
	if ((overrides.mask & kOvLineNumbersPosition) != 0) merged.lineNumbersPosition = overrides.values.lineNumbersPosition;
	if ((overrides.mask & kOvLineNumZeroFill) != 0) merged.lineNumZeroFill = overrides.values.lineNumZeroFill;
	if ((overrides.mask & kOvMiniMapPosition) != 0) merged.miniMapPosition = overrides.values.miniMapPosition;
	if ((overrides.mask & kOvMiniMapWidth) != 0) merged.miniMapWidth = overrides.values.miniMapWidth;
	if ((overrides.mask & kOvMiniMapMarkerGlyph) != 0) merged.miniMapMarkerGlyph = overrides.values.miniMapMarkerGlyph;
	if ((overrides.mask & kOvGutters) != 0) merged.gutters = overrides.values.gutters;
	if ((overrides.mask & kOvPersistentBlocks) != 0) merged.persistentBlocks = overrides.values.persistentBlocks;
	if ((overrides.mask & kOvCodeFoldingPosition) != 0) merged.codeFoldingPosition = overrides.values.codeFoldingPosition;
	if ((overrides.mask & kOvColumnBlockMove) != 0) merged.columnBlockMove = overrides.values.columnBlockMove;
	if ((overrides.mask & kOvDefaultMode) != 0) merged.defaultMode = overrides.values.defaultMode;
	if ((overrides.mask & kOvCursorStatusColor) != 0) merged.cursorStatusColor = overrides.values.cursorStatusColor;
	{
		std::string lineNumbersPosition = normalizeLineNumbersPosition(merged.lineNumbersPosition);
		if (lineNumbersPosition.empty()) lineNumbersPosition = merged.showLineNumbers ? kLineNumbersPositionLeading : kLineNumbersPositionOff;
		merged.lineNumbersPosition = lineNumbersPosition;
		merged.showLineNumbers = lineNumbersPosition != kLineNumbersPositionOff;
	}
	{
		std::string codeFoldingPosition = normalizeCodeFoldingPosition(merged.codeFoldingPosition);
		if (codeFoldingPosition.empty()) codeFoldingPosition = merged.codeFolding ? kCodeFoldingPositionLeading : kCodeFoldingPositionOff;
		merged.codeFoldingPosition = codeFoldingPosition;
		merged.codeFolding = codeFoldingPosition != kCodeFoldingPositionOff;
	}
	return merged;
}

const std::vector<MREditExtensionProfile> &configuredEditExtensionProfiles() {
	recordSettingsRuntimeRead();
	return configuredEditProfiles();
}

bool setConfiguredEditExtensionProfiles(const std::vector<MREditExtensionProfile> &profiles, std::string *errorMessage) {
	std::vector<MREditExtensionProfile> normalized = profiles;
	const std::vector<MREditExtensionProfile> previous = configuredEditProfiles();

	for (MREditExtensionProfile &profile : normalized) {
		profile.id = canonicalEditProfileId(profile.id);
		profile.name = canonicalEditProfileName(profile.name);
		profile.windowColorThemeUri = canonicalWindowColorThemeUri(profile.windowColorThemeUri);
		profile.compilerProfileId = canonicalCompilerProfileId(profile.compilerProfileId);
		if (!normalizeEditExtensionSelectorsInPlace(profile.extensions, errorMessage)) return false;
		if (!normalizeEditProfileOverridesInPlace(profile, errorMessage)) return false;
	}
	if (!validateNormalizedEditProfiles(normalized, errorMessage)) return false;
	configuredEditProfiles() = normalized;
	if (previous != normalized) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applyConfiguredEditExtensionProfileDirective(const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, std::string *errorMessage) {
	std::string op = upperAscii(trimAscii(operation));
	std::string id = canonicalEditProfileId(profileId);
	std::vector<MREditExtensionProfile> profiles = configuredEditProfiles();
	MREditExtensionProfile *profile = nullptr;

	if (op.empty()) return setError(errorMessage, "MRFEPROFILE operation may not be empty.");
	if (id.empty()) return setError(errorMessage, "MRFEPROFILE profile id may not be empty.");
	for (std::size_t i = 0; i < profiles.size(); ++i)
		if (profileIdLookupKey(profiles[i].id) == profileIdLookupKey(id)) {
			profile = &profiles[i];
			break;
		}

	if (op == "DEFINE") {
		std::string name = canonicalEditProfileName(arg3);

		if (name.empty() && trimAscii(arg4).empty()) name = id;
		if (name.empty()) return setError(errorMessage, "MRFEPROFILE DEFINE requires a non-empty display name.");
		if (profile != nullptr) return setError(errorMessage, "Duplicate extension profile id: " + id);
		MREditExtensionProfile created;
		created.id = id;
		created.name = name;
		created.overrides.values = resolveEditSetupDefaults();
		profiles.push_back(created);
		return setConfiguredEditExtensionProfiles(profiles, errorMessage);
	}

	if (profile == nullptr) return setError(errorMessage, "Unknown extension profile id: " + id);
	if (op == "EXT") {
		profile->extensions.push_back(arg3);
		return setConfiguredEditExtensionProfiles(profiles, errorMessage);
	}
	if (op == "SET") {
		if (upperAscii(trimAscii(arg3)) == kWindowColorThemeProfileKey) {
			std::string normalizedTheme = canonicalWindowColorThemeUri(arg4);
			if (!normalizedTheme.empty() && !validateColorThemeFilePath(normalizedTheme, errorMessage)) return false;
			profile->windowColorThemeUri = normalizedTheme;
			return setConfiguredEditExtensionProfiles(profiles, errorMessage);
		}
		if (upperAscii(trimAscii(arg3)) == "COMPILER_PROFILE") {
			profile->compilerProfileId = canonicalCompilerProfileId(arg4);
			return setConfiguredEditExtensionProfiles(profiles, errorMessage);
		}

		const MREditSettingDescriptor *descriptor = editSettingDescriptorByKeyInternal(arg3);
		if (descriptor == nullptr) return setError(errorMessage, "Unknown edit setting key for extension profile.");
		if (!descriptor->profileSupported) return setError(errorMessage, std::string("Setting is global-only and cannot be overridden: ") + descriptor->key);
		if (!applyEditSetupValueInternal(profile->overrides.values, descriptor->key, arg4, errorMessage)) return false;
		profile->overrides.mask |= descriptor->overrideBit;
		return setConfiguredEditExtensionProfiles(profiles, errorMessage);
	}

	return setError(errorMessage, "MRFEPROFILE supports operations DEFINE, EXT and SET.");
}

bool effectiveEditSetupSettingsForPath(const std::string &path, MREditSetupSettings &out, std::string *matchedProfileName) {
	MREditSetupSettings defaults = configuredEditSetupSettings();
	std::string ext = extensionSelectorForPath(path);

	out = defaults;
	if (matchedProfileName != nullptr) matchedProfileName->clear();
	if (ext.empty()) return true;
	for (const MREditExtensionProfile &profile : configuredEditProfiles())
		for (const std::string &selector : profile.extensions)
			if (selector == ext) {
				out = mergeEditSetupSettings(defaults, profile.overrides);
				if (matchedProfileName != nullptr) *matchedProfileName = profile.name;
				return true;
			}
	return true;
}

bool effectiveEditWindowColorThemePathForPath(const std::string &path, std::string &themeUri, std::string *matchedProfileName) {
	std::string ext = extensionSelectorForPath(path);

	themeUri = configuredColorThemeFilePath();
	if (matchedProfileName != nullptr) matchedProfileName->clear();
	if (ext.empty()) return true;
	for (const MREditExtensionProfile &profile : configuredEditProfiles())
		for (const std::string &selector : profile.extensions)
			if (selector == ext) {
				if (!profile.windowColorThemeUri.empty()) themeUri = profile.windowColorThemeUri;
				if (matchedProfileName != nullptr) *matchedProfileName = profile.name;
				return true;
			}
	return true;
}

bool effectiveCompilerProfileForPath(const std::string &path, MRCompilerProfile &out, std::string *matchedProfileName, std::string *errorMessage) {
	std::string ext = extensionSelectorForPath(path);
	std::string compilerProfileId;
	bool profileMatched = false;

	out = MRCompilerProfile();
	if (matchedProfileName != nullptr) matchedProfileName->clear();
	if (errorMessage != nullptr) errorMessage->clear();
	if (ext.empty()) return setError(errorMessage, "No filename extension for compiler profile lookup.");
	for (const MREditExtensionProfile &profile : configuredEditProfiles()) {
		if (profileMatched) break;
		for (const std::string &selector : profile.extensions) {
			if (profileMatched) break;
			if (selector == ext) {
				compilerProfileId = canonicalCompilerProfileId(profile.compilerProfileId);
				if (matchedProfileName != nullptr) *matchedProfileName = profile.name;
				profileMatched = true;
			}
		}
	}
	if (!profileMatched) return setError(errorMessage, "No filename extension profile matched this source file.");
	if (compilerProfileId.empty()) return setError(errorMessage, "No compiler profile linked for this filename extension.");
	for (const MRCompilerProfile &profile : configuredCompilerProfiles())
		if (profile.id == compilerProfileId) {
			out = profile;
			return true;
		}
	for (const MRCompilerProfile &profile : detectedCompilerProfiles())
		if (profile.id == compilerProfileId) {
			out = profile;
			return true;
		}
	return setError(errorMessage, "Linked compiler profile is not configured: " + compilerProfileId);
}

MREditSetupSettings configuredEditSetupSettings() {
	static bool initialized = false;
	MREditSetupSettings &configured = configuredEditSettings();

	recordSettingsRuntimeRead();
	if (!initialized) {
		configured = resolveEditSetupDefaults();
		initialized = true;
	}
	return configured;
}

bool setConfiguredEditSetupSettings(const MREditSetupSettings &settings, std::string *errorMessage) {
	MREditSetupSettings defaults = resolveEditSetupDefaults();
	const MREditSetupSettings previous = configuredEditSettings();
	MREditSetupSettings normalized = settings;
	std::string pageBreak = normalizePageBreakLiteral(settings.pageBreak);
	std::string wordDelimiters = settings.wordDelimiters.empty() ? defaults.wordDelimiters : settings.wordDelimiters;
	std::string defaultExts = canonicalDefaultExtensionsLiteral(settings.defaultExtensions);
	std::string columnStyle = normalizeColumnBlockMove(settings.columnBlockMove);
	std::string defaultMode = normalizeDefaultMode(settings.defaultMode);
	std::string indentStyle = normalizeIndentStyle(settings.indentStyle);
	std::string fileType = normalizeFileType(settings.fileType);
	std::string codeLanguage = upperAscii(trimAscii(settings.codeLanguage));
	std::string lineNumbersPosition = normalizeLineNumbersPosition(settings.lineNumbersPosition);
	std::string miniMapPosition = normalizeMiniMapPosition(settings.miniMapPosition);
	std::string codeFoldingPosition = normalizeCodeFoldingPosition(settings.codeFoldingPosition);
	std::string gutters = normalizeGuttersOrder(settings.gutters);
	std::string formatLine;
	std::string cursorStatusColor;
	std::string miniMapMarkerGlyph;
	int normalizedFormatLeftMargin = settings.leftMargin;
	int normalizedFormatRightMargin = settings.rightMargin;
	std::string postLoadMacro = trimAscii(settings.postLoadMacro).empty() ? std::string() : normalizeConfiguredPathInput(settings.postLoadMacro);
	std::string preSaveMacro = trimAscii(settings.preSaveMacro).empty() ? std::string() : normalizeConfiguredPathInput(settings.preSaveMacro);
	std::string defaultPath = trimAscii(settings.defaultPath).empty() ? std::string() : normalizeConfiguredPathInput(settings.defaultPath);

	if (wordDelimiters.empty()) return setError(errorMessage, "WORD_DELIMITERS may not be empty.");
	if (columnStyle.empty()) return setError(errorMessage, "BLOCK_MOVE must be DELETE_SPACE or LEAVE_SPACE.");
	if (defaultMode.empty()) return setError(errorMessage, "DEFAULT_MODE must be INSERT or OVERWRITE.");
	if (indentStyle.empty()) return setError(errorMessage, "INDENT_STYLE must be OFF, AUTOMATIC or SMART.");
	if (codeLanguage.empty()) codeLanguage = "NONE";
	if (codeLanguage != "NONE" && codeLanguage != "AUTO" && codeLanguage != "C" && codeLanguage != "CPP" && codeLanguage != "PYTHON" && codeLanguage != "JAVASCRIPT" && codeLanguage != "TYPESCRIPT" && codeLanguage != "TSX" &&
		codeLanguage != "BASH" && codeLanguage != "ZSH" && codeLanguage != "FISH" && codeLanguage != "JSON" && codeLanguage != "YAML" && codeLanguage != "XML" && codeLanguage != "PERL" && codeLanguage != "SWIFT" &&
		codeLanguage != "RUST" && codeLanguage != "GO" && codeLanguage != "PASCAL" && codeLanguage != "SYSTEMD" && codeLanguage != "MAKE" && codeLanguage != "MRMAC" && codeLanguage != "MARKDOWN" &&
		codeLanguage != "LATEX" && codeLanguage != "KOTLIN" && codeLanguage != "CSHARP")
		return setError(errorMessage, "CODE_LANGUAGE must be NONE, AUTO, C, CPP, PYTHON, JAVASCRIPT, TYPESCRIPT, TSX, BASH, ZSH, FISH, JSON, YAML, XML, PERL, SWIFT, RUST, GO, PASCAL, SYSTEMD, MAKE, MRMAC, MARKDOWN, LATEX, KOTLIN or CSHARP.");
	if (fileType.empty()) return setError(errorMessage, "FILE_TYPE must be LEGACY_TEXT, UNIX or BINARY.");
	if (lineNumbersPosition.empty()) lineNumbersPosition = settings.showLineNumbers ? kLineNumbersPositionLeading : kLineNumbersPositionOff;
	if (miniMapPosition.empty()) return setError(errorMessage, "MINIMAP_POSITION must be OFF, LEADING or TRAILING.");
	if (codeFoldingPosition.empty()) codeFoldingPosition = settings.codeFolding ? kCodeFoldingPositionLeading : kCodeFoldingPositionOff;
	if (!normalizeCursorStatusColor(settings.cursorStatusColor, cursorStatusColor, errorMessage)) return false;
	if (!normalizeMiniMapMarkerGlyph(settings.miniMapMarkerGlyph, miniMapMarkerGlyph, errorMessage)) return false;
	if (settings.binaryRecordLength < kMinBinaryRecordLength || settings.binaryRecordLength > kMaxBinaryRecordLength) return setError(errorMessage, "BINARY_RECORD_LENGTH must be between 1 and 99999.");
	if (settings.tabSize < kMinTabSize || settings.tabSize > kMaxTabSize) return setError(errorMessage, "TAB_SIZE must be between 2 and 32.");
	if (settings.leftMargin < kMinLeftMargin || settings.leftMargin > kMaxLeftMargin) return setError(errorMessage, "LEFT_MARGIN must be between 1 and 999.");
	if (settings.rightMargin < kMinRightMargin || settings.rightMargin > kMaxRightMargin) return setError(errorMessage, "RIGHT_MARGIN must be between 1 and 999.");
	if (!normalizeEditFormatLine(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, formatLine, &normalizedFormatLeftMargin, &normalizedFormatRightMargin, errorMessage)) return false;
	if (settings.rightMargin > 1 && settings.leftMargin >= settings.rightMargin) return setError(errorMessage, "LEFT_MARGIN must be less than RIGHT_MARGIN.");
	if (settings.miniMapWidth < kMinMiniMapWidth || settings.miniMapWidth > kMaxMiniMapWidth) return setError(errorMessage, "MINIMAP_WIDTH must be between 2 and 20.");

	normalized.truncateSpaces = settings.truncateSpaces;
	normalized.eofCtrlZ = settings.eofCtrlZ;
	normalized.eofCrLf = settings.eofCrLf;
	normalized.tabExpand = settings.tabExpand;
	normalized.displayTabs = settings.displayTabs;
	normalized.tabSize = settings.tabSize;
	normalized.leftMargin = settings.leftMargin;
	normalized.rightMargin = settings.rightMargin;
	normalized.formatRuler = settings.formatRuler;
	normalized.wordWrap = settings.wordWrap;
	normalized.indentStyle = indentStyle;
	normalized.codeLanguage = codeLanguage;
	normalized.codeColoring = settings.codeColoring;
	normalized.codeFoldingFeature = settings.codeFoldingFeature;
	normalized.fileType = fileType;
	normalized.binaryRecordLength = settings.binaryRecordLength;
	normalized.postLoadMacro = postLoadMacro;
	normalized.preSaveMacro = preSaveMacro;
	normalized.defaultPath = defaultPath;
	normalized.formatLine = synchronizeEditFormatLineMargins(formatLine, normalized.leftMargin, normalized.rightMargin, normalized.tabSize);
	normalized.backupMethod = normalizeBackupMethod(settings.backupMethod);
	if (normalized.backupMethod.empty()) return setError(errorMessage, "BACKUP_METHOD must be OFF, BAK_FILE or DIRECTORY.");
	normalized.backupFrequency = normalizeBackupFrequency(settings.backupFrequency);
	if (normalized.backupFrequency.empty()) return setError(errorMessage, "BACKUP_FREQUENCY must be FIRST_SAVE_ONLY or EVERY_SAVE.");
	normalized.backupExtension = normalizeBackupExtension(settings.backupExtension);
	normalized.backupDirectory = trimAscii(settings.backupDirectory).empty() ? std::string() : normalizeConfiguredPathInput(settings.backupDirectory);
	if (normalized.backupMethod == kBackupMethodBakFile && !validateBackupExtension(normalized.backupExtension, errorMessage)) return false;
	if (normalized.backupMethod == kBackupMethodDirectory && !validateWritableDirectoryPath(normalized.backupDirectory, "BACKUP_DIRECTORY", errorMessage)) return false;
	if (normalized.backupMethod != kBackupMethodBakFile) normalized.backupExtension = normalizeBackupExtension(settings.backupExtension).empty() ? defaults.backupExtension : normalizeBackupExtension(settings.backupExtension);
	if (normalized.backupMethod != kBackupMethodDirectory && trimAscii(normalized.backupDirectory).empty()) normalized.backupDirectory.clear();
	if (settings.autosaveInactivitySeconds != 0 && (settings.autosaveInactivitySeconds < kMinAutosaveInactivitySeconds || settings.autosaveInactivitySeconds > kMaxAutosaveInactivitySeconds)) return setError(errorMessage, "AUTOSAVE_INACTIVITY_SECONDS must be 0 or within 5..100 seconds.");
	if (settings.autosaveIntervalSeconds != 0 && (settings.autosaveIntervalSeconds < kMinAutosaveIntervalSeconds || settings.autosaveIntervalSeconds > kMaxAutosaveIntervalSeconds)) return setError(errorMessage, "AUTOSAVE_INTERVAL_SECONDS must be 0 or within 100..300 seconds.");
	normalized.autosaveInactivitySeconds = settings.autosaveInactivitySeconds;
	normalized.autosaveIntervalSeconds = settings.autosaveIntervalSeconds;
	normalized.backupFiles = normalized.backupMethod != kBackupMethodOff;
	normalized.showEofMarker = settings.showEofMarker;
	normalized.showEofMarkerEmoji = settings.showEofMarkerEmoji;
	normalized.showLineNumbers = lineNumbersPosition != kLineNumbersPositionOff;
	normalized.lineNumbersPosition = lineNumbersPosition;
	normalized.lineNumZeroFill = settings.lineNumZeroFill;
	normalized.miniMapPosition = miniMapPosition;
	normalized.miniMapWidth = settings.miniMapWidth;
	normalized.miniMapMarkerGlyph = miniMapMarkerGlyph;
	normalized.gutters = gutters;
	normalized.persistentBlocks = settings.persistentBlocks;
	normalized.codeFolding = codeFoldingPosition != kCodeFoldingPositionOff;
	normalized.codeFoldingPosition = codeFoldingPosition;
	normalized.pageBreak = pageBreak;
	normalized.wordDelimiters = wordDelimiters;
	normalized.defaultExtensions = defaultExts;
	normalized.columnBlockMove = columnStyle;
	normalized.defaultMode = defaultMode;
	normalized.cursorStatusColor = cursorStatusColor;
	configuredEditSettings() = normalized;
	if (previous != normalized) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applyConfiguredEditSetupValue(const std::string &key, const std::string &value, std::string *errorMessage) {
	MREditSetupSettings current = configuredEditSetupSettings();

	if (!applyEditSetupValueInternal(current, key, value, errorMessage)) return false;
	return setConfiguredEditSetupSettings(current, errorMessage);
}

std::string formatEditSetupBoolean(bool value) {
	return canonicalBooleanLiteral(value);
}

std::vector<std::string> configuredDefaultExtensionList() {
	return parseDefaultExtensions(configuredEditSetupSettings().defaultExtensions);
}

bool configuredDefaultInsertMode() {
	return upperAscii(configuredEditSetupSettings().defaultMode) != kDefaultModeOverwrite;
}

bool configuredTabExpandSetting() {
	return configuredEditSetupSettings().tabExpand;
}

bool configuredDisplayTabsSetting() {
	return configuredEditSetupSettings().displayTabs;
}

int configuredTabSizeSetting() {
	return configuredEditSetupSettings().tabSize;
}

bool configuredBackupFilesSetting() {
	return configuredEditSetupSettings().backupFiles;
}

bool configuredPersistentBlocksSetting() {
	return configuredEditSetupSettings().persistentBlocks;
}

char configuredPageBreakCharacter() {
	return decodePageBreakLiteral(configuredEditSetupSettings().pageBreak);
}
