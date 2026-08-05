#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsEditSetup.hpp"
#include "MRSettingsEditConstants.hpp"
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

static constexpr std::array<std::string_view, 27> kCodeLanguages = {
    "AUTO",
    "BASH",
    "BASIC",
    "C",
    "CPP",
    "CSHARP",
    "FISH",
    "GO",
    "JAVASCRIPT",
    "JSON",
    "KOTLIN",
    "LATEX",
    "MAKE",
    "MARKDOWN",
    "MRMAC",
    "NONE",
    "PASCAL",
    "PERL",
    "PYTHON",
    "RUST",
    "SWIFT",
    "SYSTEMD",
    "TSX",
    "TYPESCRIPT",
    "XML",
    "YAML",
    "ZSH",
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

bool normalizeCodeLanguage(const std::string &value, std::string &outValue) {
	outValue = upperAscii(trimAscii(value));
	if (outValue.empty()) outValue = "NONE";
	return std::binary_search(kCodeLanguages.begin(), kCodeLanguages.end(), std::string_view(outValue));
}

std::string normalizeFileType(const std::string &value) {
	std::string key = upperAscii(trimAscii(value));

	if (key == "LEGACY_TEXT" || key == "LEGACY" || key == "CRLF" || key == "TEXT") return kFileTypeLegacyText;
	if (key == "UNIX" || key == "LF") return kFileTypeUnix;
	if (key == "BINARY" || key == "BIN") return kFileTypeBinary;
	return std::string();
}

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

bool applyEditSetupValueInternal(MREditSetupSettings &current, const std::string &keyName, const std::string &value, std::string *errorMessage) {
	const MREditSettingDescriptor *descriptor = editSettingDescriptorByKeyInternal(keyName);
	std::string normalized;

	if (descriptor == nullptr) return setError(errorMessage, "Unknown edit setting key.");
	switch (descriptor->overrideBit) {
		case kOvPageBreak:
			current.pageBreak = normalizePageBreakLiteral(value);
			break;
		case kOvWordDelimiters:
			if (trimAscii(value).empty()) current.wordDelimiters = resolveEditSetupDefaults().wordDelimiters;
			else
				current.wordDelimiters = value;
			break;
		case kOvDefaultExtensions:
			current.defaultExtensions = canonicalDefaultExtensionsLiteral(value);
			break;
		case kOvTruncateSpaces:
			if (!parseAndAssignBooleanLiteral(value, current.truncateSpaces, errorMessage)) return false;
			break;
		case kOvEofCtrlZ:
			if (!parseAndAssignBooleanLiteral(value, current.eofCtrlZ, errorMessage)) return false;
			break;
		case kOvEofCrLf:
			if (!parseAndAssignBooleanLiteral(value, current.eofCrLf, errorMessage)) return false;
			break;
		case kOvTabExpand:
			if (!parseAndAssignBooleanLiteral(value, current.tabExpand, errorMessage)) return false;
			break;
		case kOvDisplayTabs:
			if (!parseAndAssignBooleanLiteral(value, current.displayTabs, errorMessage)) return false;
			break;
		case kOvTabSize: {
			int tabSize = 0;
			if (!parseTabSizeLiteral(value, tabSize, errorMessage)) return false;
			current.tabSize = tabSize;
			current.formatLine = defaultEditFormatLineForTabSize(current.tabSize, current.leftMargin, current.rightMargin);
			break;
		}
		case kOvLeftMargin: {
			int leftMargin = 0;
			if (!parseLeftMarginLiteral(value, leftMargin, errorMessage)) return false;
			current.leftMargin = leftMargin;
			current.formatLine = synchronizeEditFormatLineMargins(current.formatLine, current.leftMargin, current.rightMargin, current.tabSize);
			break;
		}
		case kOvRightMargin: {
			int rightMargin = 0;
			if (!parseRightMarginLiteral(value, rightMargin, errorMessage)) return false;
			current.rightMargin = rightMargin;
			current.formatLine = synchronizeEditFormatLineMargins(current.formatLine, current.leftMargin, current.rightMargin, current.tabSize);
			break;
		}
		case kOvFormatRuler:
			if (!parseAndAssignBooleanLiteral(value, current.formatRuler, errorMessage)) return false;
			break;
		case kOvWordWrap:
			if (!parseAndAssignBooleanLiteral(value, current.wordWrap, errorMessage)) return false;
			break;
		case kOvIndentStyle:
			normalized = normalizeIndentStyle(value);
			if (normalized.empty()) return setError(errorMessage, "INDENT_STYLE must be OFF, AUTOMATIC or SMART.");
			current.indentStyle = normalized;
			break;
		case kOvCodeLanguage:
			if (!normalizeCodeLanguage(value, normalized)) return setError(errorMessage, "CODE_LANGUAGE must be NONE, AUTO, C, CPP, PYTHON, JAVASCRIPT, TYPESCRIPT, TSX, BASH, ZSH, FISH, JSON, YAML, XML, PERL, SWIFT, RUST, GO, PASCAL, BASIC, SYSTEMD, MAKE, MRMAC, MARKDOWN, LATEX, KOTLIN or CSHARP.");
			current.codeLanguage = normalized;
			break;
		case kOvCodeColoring:
			if (!parseAndAssignBooleanLiteral(value, current.codeColoring, errorMessage)) return false;
			break;
		case kOvFileType:
			normalized = normalizeFileType(value);
			if (normalized.empty()) return setError(errorMessage, "FILE_TYPE must be LEGACY_TEXT, UNIX or BINARY.");
			current.fileType = normalized;
			break;
		case kOvBinaryRecordLength: {
			int binaryRecordLength = 0;
			if (!parseBinaryRecordLengthLiteral(value, binaryRecordLength, errorMessage)) return false;
			current.binaryRecordLength = binaryRecordLength;
			break;
		}
		case kOvPostLoadMacro:
			current.postLoadMacro = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
			break;
		case kOvPreSaveMacro:
			current.preSaveMacro = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
			break;
		case kOvDefaultPath:
			current.defaultPath = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
			break;
		case kOvFormatLine: {
			int leftMargin = current.leftMargin;
			int rightMargin = current.rightMargin;
			if (!normalizeEditFormatLine(value, current.tabSize, current.leftMargin, current.rightMargin, normalized, &leftMargin, &rightMargin, errorMessage)) return false;
			current.formatLine = normalized;
			current.leftMargin = leftMargin;
			current.rightMargin = rightMargin;
			break;
		}
		case kOvBackupFiles:
			if (!parseAndAssignBooleanLiteral(value, current.backupFiles, errorMessage)) return false;
			if (!current.backupFiles) current.backupMethod = kBackupMethodOff;
			else if (normalizeBackupMethod(current.backupMethod).empty() || current.backupMethod == kBackupMethodOff)
				current.backupMethod = kBackupMethodBakFile;
			break;
		case kOvBackupMethod:
			normalized = normalizeBackupMethod(value);
			if (normalized.empty()) return setError(errorMessage, "BACKUP_METHOD must be OFF, BAK_FILE or DIRECTORY.");
			current.backupMethod = normalized;
			current.backupFiles = normalized != kBackupMethodOff;
			break;
		case kOvBackupFrequency:
			normalized = normalizeBackupFrequency(value);
			if (normalized.empty()) return setError(errorMessage, "BACKUP_FREQUENCY must be FIRST_SAVE_ONLY or EVERY_SAVE.");
			current.backupFrequency = normalized;
			break;
		case kOvBackupExtension:
			current.backupExtension = normalizeBackupExtension(value);
			break;
		case kOvBackupDirectory:
			current.backupDirectory = trimAscii(value).empty() ? std::string() : normalizeConfiguredPathInput(value);
			break;
		case kOvAutosaveInactivitySeconds: {
			int parsedSeconds = 0;
			if (!normalizeAutosaveSeconds(value, kMinAutosaveInactivitySeconds, kMaxAutosaveInactivitySeconds, parsedSeconds, "AUTOSAVE_INACTIVITY_SECONDS", errorMessage)) return false;
			current.autosaveInactivitySeconds = parsedSeconds;
			break;
		}
		case kOvAutosaveIntervalSeconds: {
			int parsedSeconds = 0;
			if (!normalizeAutosaveSeconds(value, kMinAutosaveIntervalSeconds, kMaxAutosaveIntervalSeconds, parsedSeconds, "AUTOSAVE_INTERVAL_SECONDS", errorMessage)) return false;
			current.autosaveIntervalSeconds = parsedSeconds;
			break;
		}
		case kOvShowEofMarker:
			if (!parseAndAssignBooleanLiteral(value, current.showEofMarker, errorMessage)) return false;
			break;
		case kOvShowEofMarkerEmoji:
			if (!parseAndAssignBooleanLiteral(value, current.showEofMarkerEmoji, errorMessage)) return false;
			break;
		case kOvLineNumbersPosition:
			normalized = normalizeLineNumbersPosition(value);
			if (normalized.empty()) return setError(errorMessage, "LINE_NUMBERS_POSITION must be OFF, LEADING or TRAILING.");
			current.lineNumbersPosition = normalized;
			current.showLineNumbers = normalized != kLineNumbersPositionOff;
			break;
		case kOvLineNumZeroFill:
			if (!parseAndAssignBooleanLiteral(value, current.lineNumZeroFill, errorMessage)) return false;
			break;
		case kOvMiniMapPosition:
			normalized = normalizeMiniMapPosition(value);
			if (normalized.empty()) return setError(errorMessage, "MINIMAP_POSITION must be OFF, LEADING or TRAILING.");
			current.miniMapPosition = normalized;
			break;
		case kOvMiniMapWidth: {
			int miniMapWidth = 0;
			if (!parseMiniMapWidthLiteral(value, miniMapWidth, errorMessage)) return false;
			current.miniMapWidth = miniMapWidth;
			break;
		}
		case kOvMiniMapMarkerGlyph:
			if (!normalizeMiniMapMarkerGlyph(value, normalized, errorMessage)) return false;
			current.miniMapMarkerGlyph = normalized;
			break;
		case kOvGutters:
			current.gutters = normalizeGuttersOrder(value);
			break;
		case kOvPersistentBlocks:
			if (!parseAndAssignBooleanLiteral(value, current.persistentBlocks, errorMessage)) return false;
			break;
		case kOvCodeFoldingPosition:
			normalized = normalizeCodeFoldingPosition(value);
			if (normalized.empty()) return setError(errorMessage, "CODE_FOLDING_POSITION must be OFF, LEADING or TRAILING.");
			current.codeFoldingPosition = normalized;
			current.codeFolding = normalized != kCodeFoldingPositionOff;
			break;
		case kOvColumnBlockMove:
			normalized = normalizeColumnBlockMove(value);
			if (normalized.empty()) return setError(errorMessage, "BLOCK_MOVE must be DELETE_SPACE or LEAVE_SPACE.");
			current.columnBlockMove = normalized;
			break;
		case kOvDefaultMode:
			normalized = normalizeDefaultMode(value);
			if (normalized.empty()) return setError(errorMessage, "DEFAULT_MODE must be INSERT or OVERWRITE.");
			current.defaultMode = normalized;
			break;
		case kOvCursorStatusColor:
			if (!normalizeCursorStatusColor(value, normalized, errorMessage)) return false;
			current.cursorStatusColor = normalized;
			break;
		default:
			return setError(errorMessage, "Unknown edit setting key.");
	}

	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string editSetupValueLiteral(const MREditSetupSettings &settings, const char *key) {
	const MREditSettingDescriptor *descriptor = key != nullptr ? editSettingDescriptorByKeyInternal(key) : nullptr;

	if (descriptor == nullptr) return std::string();
	switch (descriptor->overrideBit) {
		case kOvPageBreak: return settings.pageBreak;
		case kOvWordDelimiters: return settings.wordDelimiters;
		case kOvDefaultExtensions: return settings.defaultExtensions;
		case kOvTruncateSpaces: return formatEditSetupBoolean(settings.truncateSpaces);
		case kOvEofCtrlZ: return formatEditSetupBoolean(settings.eofCtrlZ);
		case kOvEofCrLf: return formatEditSetupBoolean(settings.eofCrLf);
		case kOvTabExpand: return formatEditSetupBoolean(settings.tabExpand);
		case kOvDisplayTabs: return formatEditSetupBoolean(settings.displayTabs);
		case kOvTabSize: return std::to_string(settings.tabSize);
		case kOvLeftMargin: return std::to_string(settings.leftMargin);
		case kOvRightMargin: return std::to_string(settings.rightMargin);
		case kOvFormatRuler: return formatEditSetupBoolean(settings.formatRuler);
		case kOvWordWrap: return formatEditSetupBoolean(settings.wordWrap);
		case kOvIndentStyle: return settings.indentStyle;
		case kOvCodeLanguage: return settings.codeLanguage;
		case kOvCodeColoring: return formatEditSetupBoolean(settings.codeColoring);
		case kOvFileType: return settings.fileType;
		case kOvBinaryRecordLength: return std::to_string(settings.binaryRecordLength);
		case kOvPostLoadMacro: return settings.postLoadMacro;
		case kOvPreSaveMacro: return settings.preSaveMacro;
		case kOvDefaultPath: return settings.defaultPath;
		case kOvFormatLine: return settings.formatLine;
		case kOvBackupFiles: return formatEditSetupBoolean(settings.backupFiles);
		case kOvBackupMethod: return settings.backupMethod;
		case kOvBackupFrequency: return settings.backupFrequency;
		case kOvBackupExtension: return settings.backupExtension;
		case kOvBackupDirectory: return settings.backupDirectory;
		case kOvAutosaveInactivitySeconds: return std::to_string(settings.autosaveInactivitySeconds);
		case kOvAutosaveIntervalSeconds: return std::to_string(settings.autosaveIntervalSeconds);
		case kOvShowEofMarker: return formatEditSetupBoolean(settings.showEofMarker);
		case kOvShowEofMarkerEmoji: return formatEditSetupBoolean(settings.showEofMarkerEmoji);
		case kOvLineNumbersPosition: return settings.lineNumbersPosition;
		case kOvLineNumZeroFill: return formatEditSetupBoolean(settings.lineNumZeroFill);
		case kOvMiniMapPosition: return settings.miniMapPosition;
		case kOvMiniMapWidth: return std::to_string(settings.miniMapWidth);
		case kOvMiniMapMarkerGlyph: return settings.miniMapMarkerGlyph;
		case kOvGutters: return settings.gutters;
		case kOvPersistentBlocks: return formatEditSetupBoolean(settings.persistentBlocks);
		case kOvCodeFoldingPosition: return settings.codeFoldingPosition;
		case kOvColumnBlockMove: return settings.columnBlockMove;
		case kOvDefaultMode: return settings.defaultMode;
		case kOvCursorStatusColor: return settings.cursorStatusColor;
		default: return std::string();
	}
}

MREditSetupSettings configuredEditSetupSettings() {
	recordSettingsRuntimeRead();
	return configuredEditSettings();
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
	std::string codeLanguage;
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
	if (!normalizeCodeLanguage(settings.codeLanguage, codeLanguage)) return setError(errorMessage, "CODE_LANGUAGE must be NONE, AUTO, C, CPP, PYTHON, JAVASCRIPT, TYPESCRIPT, TSX, BASH, ZSH, FISH, JSON, YAML, XML, PERL, SWIFT, RUST, GO, PASCAL, BASIC, SYSTEMD, MAKE, MRMAC, MARKDOWN, LATEX, KOTLIN or CSHARP.");
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
	storeConfiguredEditSettings(normalized);
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
