#include "../../app/utils/MRFileIOUtils.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../../app/commands/MRWindowCommands.hpp"
#include "MRSettingsAssignments.hpp"
#include "MRSettingsEditSetup.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntime.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsSnapshotIO.hpp"
#include "MRSettingsStorage.hpp"
#include "MRSettingsThemesProfiles.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <map>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

namespace {

bool setError(std::string *errorMessage, const std::string &message);
bool isPayloadKeyStart(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalpha(uch) != 0;
}

bool isPayloadKeyChar(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '_';
}

bool isPayloadSpace(char ch) noexcept {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

struct ScopedHistoryPayloadMember {
	std::string key;
	std::string value;
};

bool parseScopedHistoryPayloadMembers(std::string_view payload, std::vector<ScopedHistoryPayloadMember> &members, std::string *errorMessage) {
	std::size_t pos = 0;

	while (pos < payload.size()) {
		while (pos < payload.size() && isPayloadSpace(payload[pos]))
			++pos;
		if (pos >= payload.size()) break;
		if (!isPayloadKeyStart(payload[pos])) return setError(errorMessage, "Dialog history payload syntax error.");

		const std::size_t keyStart = pos;
		++pos;
		while (pos < payload.size() && isPayloadKeyChar(payload[pos]))
			++pos;
		std::string key = upperAscii(std::string(payload.substr(keyStart, pos - keyStart)));

		if (pos >= payload.size() || payload[pos] != '=') return setError(errorMessage, "Dialog history payload is missing '='.");
		++pos;
		if (pos >= payload.size() || payload[pos] != '"') return setError(errorMessage, "Dialog history payload expects quoted values.");
		++pos;

		std::string value;
		while (pos < payload.size()) {
			const char ch = payload[pos++];
			if (ch == '"') break;
			if (ch == '\\') {
				if (pos >= payload.size()) return setError(errorMessage, "Dialog history payload has a dangling escape.");
				switch (const char escaped = payload[pos++]) {
					case '"':
					case '\\':
						value.push_back(escaped);
						break;
					case 'n':
						value.push_back('\n');
						break;
					case 'r':
						value.push_back('\r');
						break;
					case 't':
						value.push_back('\t');
						break;
					default:
						return setError(errorMessage, "Dialog history payload has an unsupported escape.");
				}
				continue;
			}
			value.push_back(ch);
		}
		if (pos > payload.size() || payload[pos - 1] != '"') return setError(errorMessage, "Dialog history payload has an unterminated value.");

		members.push_back({std::move(key), std::move(value)});
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

const ScopedHistoryPayloadMember *findScopedHistoryPayloadMember(const std::vector<ScopedHistoryPayloadMember> &members, std::string_view key) noexcept {
	for (const ScopedHistoryPayloadMember &member : members)
		if (member.key == key) return &member;
	return nullptr;
}

bool parseScopedHistoryPayload(std::string_view payload, const char *valueMemberName, MRDialogHistoryScope &scopeOut, std::string &valueOut, std::string *errorMessage) {
	std::vector<ScopedHistoryPayloadMember> members;
	const ScopedHistoryPayloadMember *scopeMember = nullptr;
	const ScopedHistoryPayloadMember *valueMember = nullptr;
	const MRDialogHistoryScopeSpec *scopeSpec = nullptr;

	if (!parseScopedHistoryPayloadMembers(payload, members, errorMessage)) return false;
	scopeMember = findScopedHistoryPayloadMember(members, "SCOPE");
	valueMember = findScopedHistoryPayloadMember(members, upperAscii(std::string(valueMemberName)));
	if (scopeMember == nullptr || valueMember == nullptr) return setError(errorMessage, "Dialog history payload requires scope and value members.");
	scopeSpec = findDialogHistoryScopeSpecByName(scopeMember->value);
	if (scopeSpec == nullptr) return setError(errorMessage, "Dialog history payload references an unknown scope.");
	scopeOut = scopeSpec->scope;
	valueOut = valueMember->value;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

void trimSnapshotHistoryToLimit(std::vector<std::string> &entries, int limit) {
	if (limit < 0) limit = 0;
	if (entries.size() > static_cast<std::size_t>(limit)) entries.resize(static_cast<std::size_t>(limit));
}

void addHistoryEntry(std::vector<std::string> &entries, const std::string &value, int limit) {
	if (value.empty()) return;
	entries.erase(std::remove(entries.begin(), entries.end(), value), entries.end());
	entries.insert(entries.begin(), value);
	trimSnapshotHistoryToLimit(entries, limit);
}

void addSerializedHistoryEntry(std::vector<std::string> &entries, const std::string &value, int limit, bool normalizeAsPath) {
	const std::string prepared = normalizeAsPath ? normalizeConfiguredPathInput(value) : trimAscii(value);

	if (prepared.empty()) return;
	for (const std::string &entry : entries)
		if (entry == prepared) return;
	entries.push_back(prepared);
	trimSnapshotHistoryToLimit(entries, limit);
}

static const char *const kThemeSettingsKey = "COLORTHEMEURI";
static const char *const kKeymapSettingsKey = "KEYMAPURI";
static const char *const kWindowColorThemeProfileKey = "WINDOW_COLORTHEME_URI";
static const char *const kSettingsVersionKey = "SETTINGS_VERSION";
static const char *const kCurrentSettingsVersion = "2";
static const char *const kSearchTextTypeLiteral = "LITERAL";
static const char *const kSearchTextTypePcre = "PCRE";
static const char *const kSearchTextTypeWord = "WORD";
static const char *const kSearchDirectionForward = "FORWARD";
static const char *const kSearchDirectionBackward = "BACKWARD";
static const char *const kSearchModeStopFirst = "STOP_FIRST_OCCURRENCE";
static const char *const kSearchModePromptNext = "PROMPT_FOR_NEXT_MATCH";
static const char *const kSearchModeListAll = "LIST_ALL_OCCURRENCES";
static const char *const kSarModeReplaceFirst = "REPLACE_FIRST_OCCURRENCE";
static const char *const kSarModePromptEach = "PROMPT_FOR_EACH_REPLACE";
static const char *const kSarModeReplaceAll = "REPLACE_ALL_OCCURRENCES";
static const char *const kSarLeaveCursorEnd = "END_OF_REPLACE_STRING";
static const char *const kSarLeaveCursorStart = "START_OF_REPLACE_STRING";
static const char *const kLogHandlingVolatile = "VOLATILE";
static const char *const kLogHandlingPersist = "PERSIST";
static const char *const kLogHandlingJournalctl = "JOURNALCTL";
static const char *const kCursorBehaviourBoundToText = "BOUND_TO_TEXT";
static const char *const kCursorBehaviourFreeMovement = "FREE_MOVEMENT";
static const char *const kDialogLastPathKey = "DIALOG_LAST_PATH";
static const char *const kDialogPathHistoryKey = "DIALOG_PATH_HISTORY";
static const char *const kDialogFileHistoryKey = "DIALOG_FILE_HISTORY";

struct MRSettingsKeyDescriptor {
	const char *key;
	MRSettingsKeyClass keyClass;
	bool serialized;
};

static const MRSettingsKeyDescriptor kFixedSettingsKeyDescriptors[] = {
    {kSettingsVersionKey, MRSettingsKeyClass::Version, true},
    {"SETTINGSPATH", MRSettingsKeyClass::Path, true},
    {"MACROPATH", MRSettingsKeyClass::Path, true},
    {"HELPPATH", MRSettingsKeyClass::Path, true},
    {"TEMPDIR", MRSettingsKeyClass::Path, true},
    {"SHELLPATH", MRSettingsKeyClass::Path, true},
    {"WINDOW_MANAGER", MRSettingsKeyClass::Global, true},
    {"MESSAGES", MRSettingsKeyClass::Global, true},
    {"SEARCH_TEXT_TYPE", MRSettingsKeyClass::Global, true},
    {"SEARCH_DIRECTION", MRSettingsKeyClass::Global, true},
    {"SEARCH_MODE", MRSettingsKeyClass::Global, true},
    {"SEARCH_CASE_SENSITIVE", MRSettingsKeyClass::Global, true},
    {"SEARCH_GLOBAL_SEARCH", MRSettingsKeyClass::Global, true},
    {"SEARCH_RESTRICT_MARKED_BLOCK", MRSettingsKeyClass::Global, true},
    {"SEARCH_ALL_WINDOWS", MRSettingsKeyClass::Global, true},
    {"SEARCH_LIST_ALL_OCCURRENCES", MRSettingsKeyClass::Global, false},
    {"SAR_TEXT_TYPE", MRSettingsKeyClass::Global, true},
    {"SAR_DIRECTION", MRSettingsKeyClass::Global, true},
    {"SAR_MODE", MRSettingsKeyClass::Global, true},
    {"SAR_LEAVE_CURSOR_AT", MRSettingsKeyClass::Global, true},
    {"SAR_CASE_SENSITIVE", MRSettingsKeyClass::Global, true},
    {"SAR_GLOBAL_SEARCH", MRSettingsKeyClass::Global, true},
    {"SAR_RESTRICT_MARKED_BLOCK", MRSettingsKeyClass::Global, true},
    {"SAR_ALL_WINDOWS", MRSettingsKeyClass::Global, true},
    {"SAR_REPLACE_MODE", MRSettingsKeyClass::Global, false},
    {"SAR_PROMPT_EACH_REPLACE", MRSettingsKeyClass::Global, false},
    {"MULTI_SEARCH_FILESPEC", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_TEXT", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_STARTING_PATH", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_SUBDIRECTORIES", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_CASE_SENSITIVE", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_REGULAR_EXPRESSIONS", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_FILES_IN_MEMORY", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_FILESPEC", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_TEXT", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_REPLACEMENT", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_STARTING_PATH", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_SUBDIRECTORIES", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_CASE_SENSITIVE", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_REGULAR_EXPRESSIONS", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_FILES_IN_MEMORY", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_KEEP_FILES_OPEN", MRSettingsKeyClass::Global, true},
    {"MULTI_FILESPEC_HISTORY", MRSettingsKeyClass::Global, false},
    {"MULTI_PATH_HISTORY", MRSettingsKeyClass::Global, false},
    {"PDF_EXPORT_PATH", MRSettingsKeyClass::Global, true},
    {"PDF_EXPORT_PAGE_SEPARATOR", MRSettingsKeyClass::Global, true},
    {"PDF_EXPORT_FONT_FAMILY", MRSettingsKeyClass::Global, true},
    {"PDF_EXPORT_FONT_SIZE", MRSettingsKeyClass::Global, true},
    {"PDF_EXPORT_HEADER_LINE", MRSettingsKeyClass::Global, true},
    {"PDF_EXPORT_FOOTER_LINE", MRSettingsKeyClass::Global, true},
    {"PDF_EXPORT_USE_PRINT_MARGIN", MRSettingsKeyClass::Global, false},
    {"PDF_EXPORT_PRINT_MARGIN_COLUMNS", MRSettingsKeyClass::Global, false},
    {"PDF_EXPORT_TEXT_WIDTH", MRSettingsKeyClass::Global, true},
    {"PDF_EXPORT_LEFT_MARGIN_POINTS", MRSettingsKeyClass::Global, true},
    {"PDF_EXPORT_RIGHT_MARGIN_POINTS", MRSettingsKeyClass::Global, true},
    {"PDF_EXPORT_TOP_MARGIN_POINTS", MRSettingsKeyClass::Global, true},
    {"PDF_EXPORT_BOTTOM_MARGIN_POINTS", MRSettingsKeyClass::Global, true},
    {"VIRTUAL_DESKTOPS", MRSettingsKeyClass::Global, true},
    {"CYCLIC_VIRTUAL_DESKTOPS", MRSettingsKeyClass::Global, true},
    {"CURSOR_BEHAVIOUR", MRSettingsKeyClass::Global, true},
    {"UI_INDENT_STYLE", MRSettingsKeyClass::Global, true},
    {"CURSOR_POSITION_MARKER", MRSettingsKeyClass::Global, true},
    {"AUTOLOAD_WORKSPACE", MRSettingsKeyClass::Global, true},
    {"LOG_HANDLING", MRSettingsKeyClass::Global, true},
    {"LOGFILE", MRSettingsKeyClass::Global, true},
    {"AUTOEXEC_MACRO", MRSettingsKeyClass::Global, false},
    {"LASTFILEDIALOGPATH", MRSettingsKeyClass::Global, false},
    {"WORKSPACE", MRSettingsKeyClass::Global, false},
    {"MAX_PATH_HISTORY", MRSettingsKeyClass::Global, true},
    {"MAX_FILE_HISTORY", MRSettingsKeyClass::Global, true},
    {"PATH_HISTORY", MRSettingsKeyClass::Global, false},
    {"FILE_HISTORY", MRSettingsKeyClass::Global, false},
    {kDialogLastPathKey, MRSettingsKeyClass::Global, false},
    {kDialogPathHistoryKey, MRSettingsKeyClass::Global, false},
    {kDialogFileHistoryKey, MRSettingsKeyClass::Global, false},
    {"DEFAULT_PROFILE_DESCRIPTION", MRSettingsKeyClass::Global, true},
    {kKeymapSettingsKey, MRSettingsKeyClass::Global, true},
    {"ACTIVE_KEYMAP_PROFILE", MRSettingsKeyClass::Global, true},
    {"KEYMAP_PROFILE", MRSettingsKeyClass::Global, false},
    {"KEYMAP_BIND", MRSettingsKeyClass::Global, false},
    {kThemeSettingsKey, MRSettingsKeyClass::Global, true},
    {"WINDOWCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"MENUDIALOGCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"HELPCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"OTHERCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"MINIMAPCOLORS", MRSettingsKeyClass::ColorInline, false},
    {"CODECOLORS", MRSettingsKeyClass::ColorInline, false},
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

bool parseLogHandlingLiteral(const std::string &value, MRLogHandling &outValue, std::string *errorMessage) {
	const std::string upper = upperAscii(trimAscii(value));

	if (upper == kLogHandlingVolatile) {
		outValue = MRLogHandling::Volatile;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kLogHandlingPersist) {
		outValue = MRLogHandling::Persist;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kLogHandlingJournalctl) {
		outValue = MRLogHandling::Journalctl;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "Expected log handling VOLATILE, PERSIST or JOURNALCTL.");
}

bool parseCursorBehaviourLiteral(const std::string &value, MRCursorBehaviour &outValue, std::string *errorMessage) {
	const std::string upper = upperAscii(trimAscii(value));

	if (upper == kCursorBehaviourBoundToText || upper == "BOUND") {
		outValue = MRCursorBehaviour::BoundToText;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kCursorBehaviourFreeMovement || upper == "FREE") {
		outValue = MRCursorBehaviour::FreeMovement;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "CURSOR_BEHAVIOUR must be BOUND_TO_TEXT or FREE_MOVEMENT.");
}

bool parseUiIndentStyleLiteral(const std::string &value, MRUiIndentStyle &outValue, std::string *errorMessage) {
	const std::string upper = upperAscii(trimAscii(value));

	if (upper == "K_AND_R" || upper == "K&R" || upper == "KANDR") {
		outValue = MRUiIndentStyle::KandR;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == "K_AND_R4" || upper == "K&R4" || upper == "KANDR4") {
		outValue = MRUiIndentStyle::KandR4;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == "ALLMAN") {
		outValue = MRUiIndentStyle::Allman;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == "GNOME") {
		outValue = MRUiIndentStyle::Gnome;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == "WHITESMITHS") {
		outValue = MRUiIndentStyle::Whitesmiths;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == "HORSTMANN" || upper == "HORTMANN") {
		outValue = MRUiIndentStyle::Horstmann;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "UI_INDENT_STYLE must be K_AND_R, K_AND_R4, ALLMAN, GNOME, WHITESMITHS or HORSTMANN.");
}

bool parseSearchTextTypeLiteral(const std::string &value, MRSearchTextType &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == kSearchTextTypeLiteral) {
		outValue = MRSearchTextType::Literal;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSearchTextTypeWord) {
		outValue = MRSearchTextType::Word;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSearchTextTypePcre || upper == "REGEX" || upper == "REGULAR_EXPRESSION") {
		outValue = MRSearchTextType::Pcre;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SEARCH_TEXT_TYPE/SAR_TEXT_TYPE must be LITERAL, PCRE or WORD.");
}

bool parseSearchDirectionLiteral(const std::string &value, MRSearchDirection &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == kSearchDirectionForward) {
		outValue = MRSearchDirection::Forward;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSearchDirectionBackward) {
		outValue = MRSearchDirection::Backward;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SEARCH_DIRECTION/SAR_DIRECTION must be FORWARD or BACKWARD.");
}

bool parseSearchModeLiteral(const std::string &value, MRSearchMode &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == kSearchModeStopFirst || upper == "STOP_FIRST") {
		outValue = MRSearchMode::StopFirst;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSearchModePromptNext || upper == "PROMPT_NEXT") {
		outValue = MRSearchMode::PromptNext;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSearchModeListAll || upper == "LIST_ALL") {
		outValue = MRSearchMode::ListAll;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SEARCH_MODE must be STOP_FIRST_OCCURRENCE, PROMPT_FOR_NEXT_MATCH or LIST_ALL_OCCURRENCES.");
}

bool parseSarModeLiteral(const std::string &value, MRSarMode &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == kSarModeReplaceFirst || upper == "FIRST") {
		outValue = MRSarMode::ReplaceFirst;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSarModePromptEach || upper == "PROMPT_EACH") {
		outValue = MRSarMode::PromptEach;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSarModeReplaceAll || upper == "ALL") {
		outValue = MRSarMode::ReplaceAll;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SAR_MODE must be REPLACE_FIRST_OCCURRENCE, PROMPT_FOR_EACH_REPLACE or REPLACE_ALL_OCCURRENCES.");
}

bool parseSarLeaveCursorLiteral(const std::string &value, MRSarLeaveCursor &outValue, std::string *errorMessage) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == kSarLeaveCursorEnd || upper == "END") {
		outValue = MRSarLeaveCursor::EndOfReplaceString;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kSarLeaveCursorStart || upper == "START") {
		outValue = MRSarLeaveCursor::StartOfReplaceString;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SAR_LEAVE_CURSOR_AT must be END_OF_REPLACE_STRING or START_OF_REPLACE_STRING.");
}

bool normalizeCursorPositionMarker(const std::string &value, std::string &out, std::string *errorMessage) {
	std::string trimmed = trimAscii(value);
	int rCount = 0;
	int cCount = 0;

	if (trimmed.empty()) return setError(errorMessage, "must not be empty.");
	if (trimmed.size() > 10) return setError(errorMessage, "must be at most 10 characters.");
	out.clear();
	out.reserve(trimmed.size());
	for (char ch : trimmed) {
		if (ch == 'R') {
			++rCount;
			if (rCount > 1) return setError(errorMessage, "R placeholder may appear only once.");
			out.push_back(ch);
			continue;
		}
		if (ch == 'C') {
			++cCount;
			if (cCount > 1) return setError(errorMessage, "C placeholder may appear only once.");
			out.push_back(ch);
			continue;
		}
		out.push_back(ch);
	}
	if (rCount == 0 || cCount == 0) return setError(errorMessage, "must contain R and C placeholder.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

} // namespace

MRSettingsKeyClass classifySettingsKey(std::string_view key) {
	std::string upper = upperAscii(trimAscii(key));

	if (upper.empty()) return MRSettingsKeyClass::Unknown;
	for (const auto &descriptor : kFixedSettingsKeyDescriptors)
		if (upper == descriptor.key) return descriptor.keyClass;
	return editSettingDescriptorByKeyInternal(upper) != nullptr ? MRSettingsKeyClass::Edit : MRSettingsKeyClass::Unknown;
}

bool isCanonicalSerializedSettingsKey(std::string_view key) {
	std::string upper = upperAscii(trimAscii(key));

	if (upper.empty()) return false;
	for (const auto &descriptor : kFixedSettingsKeyDescriptors)
		if (upper == descriptor.key) return descriptor.serialized;
	return editSettingDescriptorByKeyInternal(upper) != nullptr;
}

std::size_t canonicalSerializedSettingsKeyCount() {
	std::size_t editDescriptorCount = 0;
	std::size_t fixedSerializedCount = 0;
	std::size_t serializedEditCount = 0;
	editSettingDescriptors(editDescriptorCount);

	for (const auto &descriptor : kFixedSettingsKeyDescriptors)
		if (descriptor.serialized) ++fixedSerializedCount;
	for (std::size_t i = 0; i < editDescriptorCount; ++i)
		++serializedEditCount;
	return fixedSerializedCount + serializedEditCount;
}

bool resetConfiguredSettingsModel(const std::string &settingsPath, MRSetupPaths &paths, std::string *errorMessage) {
	paths = resolveSetupPathDefaults();
	paths.settingsMacroUri = normalizeConfiguredPathInput(settingsPath);
	if (paths.settingsMacroUri.empty()) return setError(errorMessage, "Settings path is empty.");
	if (!setConfiguredSettingsMacroFilePath(paths.settingsMacroUri, errorMessage)) return false;
	if (!setConfiguredMacroDirectoryPath(paths.macroPath, errorMessage)) return false;
	if (!setConfiguredHelpFilePath(paths.helpUri, errorMessage)) return false;
	if (!setConfiguredTempDirectoryPath(paths.tempPath, errorMessage)) return false;
	if (!setConfiguredShellExecutablePath(paths.shellUri, errorMessage)) return false;
	if (!setConfiguredLogFilePath(defaultLogFilePathForSettings(paths.settingsMacroUri), errorMessage)) return false;
	if (!setConfiguredLastFileDialogPath(paths.macroPath, errorMessage)) return false;
	if (!setConfiguredDefaultProfileDescription("Global defaults", errorMessage)) return false;
	if (!setConfiguredSearchDialogOptions(MRSearchDialogOptions(), errorMessage)) return false;
	if (!setConfiguredSarDialogOptions(MRSarDialogOptions(), errorMessage)) return false;
	if (!setConfiguredMultiSearchDialogOptions(MRMultiSearchDialogOptions(), errorMessage)) return false;
	if (!setConfiguredMultiSarDialogOptions(MRMultiSarDialogOptions(), errorMessage)) return false;
	if (!setConfiguredPdfExportSettings(MRPdfExportSettings(), errorMessage)) return false;
	if (!setConfiguredCursorBehaviour(MRCursorBehaviour::BoundToText, errorMessage)) return false;
	if (!setConfiguredUiIndentStyle(MRUiIndentStyle::KandR, errorMessage)) return false;
	if (!setConfiguredCursorPositionMarker("R:C", errorMessage)) return false;
	if (!setConfiguredLogHandling(MRLogHandling::Volatile, errorMessage)) return false;
	configuredAutoexecMacroStorage().clear();
	if (!setConfiguredEditSetupSettings(resolveEditSetupDefaults(), errorMessage)) return false;
	configuredColorSettings() = resolveColorSetupDefaults();
	configuredColorSettingsInitialized() = true;
	if (!setConfiguredEditExtensionProfiles(std::vector<MREditExtensionProfile>(), errorMessage)) return false;
	if (!setConfiguredKeymapProfiles(std::vector<MRKeymapProfile>(), errorMessage)) return false;
	if (!setConfiguredKeymapFilePath("", errorMessage)) return false;
	if (!setConfiguredActiveKeymapProfile("DEFAULT", errorMessage)) return false;
	if (!setConfiguredColorThemeFilePath(defaultColorThemeFilePath(), errorMessage)) return false;
	configuredPathHistoryLimit() = kHistoryLimitDefault;
	configuredFileHistoryLimit() = kHistoryLimitDefault;
	for (MRScopedDialogHistoryState &state : configuredDialogHistoryStorage()) {
		state.lastPath.clear();
		state.pathHistory.clear();
		state.fileHistory.clear();
	}
	configuredMultiFilespecHistoryStorage().clear();
	configuredMultiPathHistoryStorage().clear();
	configuredHistoryEpochCounter() = std::max(static_cast<long long>(0), static_cast<long long>(std::time(nullptr)));
	clearConfiguredSettingsDirty();
	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = configuredMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applyConfiguredSettingsAssignment(const std::string &key, const std::string &value, MRSetupPaths &paths, std::string *errorMessage) {
	switch (classifySettingsKey(key)) {
		case MRSettingsKeyClass::Unknown:
			return setError(errorMessage, "Unsupported MRSETUP key.");
		case MRSettingsKeyClass::Version:
			if (trimAscii(value) != kCurrentSettingsVersion) return setError(errorMessage, "Unsupported settings version.");
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		case MRSettingsKeyClass::Path: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "SETTINGSPATH") {
				paths.settingsMacroUri = configuredSettingsMacroFilePath();
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MACROPATH") {
				if (!validateMacroDirectoryPath(value, errorMessage)) return false;
				if (!setConfiguredMacroDirectoryPath(value, errorMessage)) return false;
				paths.macroPath = normalizeConfiguredPathInput(value);
				return true;
			}
			if (upper == "HELPPATH") {
				if (!validateHelpFilePath(value, errorMessage)) return false;
				if (!setConfiguredHelpFilePath(value, errorMessage)) return false;
				paths.helpUri = normalizeConfiguredPathInput(value);
				return true;
			}
			if (upper == "TEMPDIR") {
				if (!validateTempDirectoryPath(value, errorMessage)) return false;
				if (!setConfiguredTempDirectoryPath(value, errorMessage)) return false;
				paths.tempPath = normalizeConfiguredPathInput(value);
				return true;
			}
			if (upper == "SHELLPATH") {
				if (!validateShellExecutablePath(value, errorMessage)) return false;
				if (!setConfiguredShellExecutablePath(value, errorMessage)) return false;
				paths.shellUri = normalizeConfiguredPathInput(value);
				return true;
			}
			break;
		}
		case MRSettingsKeyClass::Global: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "WINDOW_MANAGER") {
				bool parsed = true;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredWindowManager(parsed, errorMessage);
			}
			if (upper == "MESSAGES") {
				bool parsed = true;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredMenulineMessages(parsed, errorMessage);
			}
			if (upper == "SEARCH_TEXT_TYPE") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseSearchTextTypeLiteral(value, options.textType, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_DIRECTION") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseSearchDirectionLiteral(value, options.direction, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_MODE") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseSearchModeLiteral(value, options.mode, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_CASE_SENSITIVE") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_GLOBAL_SEARCH") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.globalSearch, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_RESTRICT_MARKED_BLOCK") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.restrictToMarkedBlock, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_ALL_WINDOWS") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.searchAllWindows, errorMessage)) return false;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SEARCH_LIST_ALL_OCCURRENCES") {
				MRSearchDialogOptions options = configuredSearchDialogOptions();
				bool listAll = false;
				if (!parseBooleanLiteral(value, listAll, errorMessage)) return false;
				options.mode = listAll ? MRSearchMode::ListAll : MRSearchMode::StopFirst;
				return setConfiguredSearchDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_TEXT_TYPE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSearchTextTypeLiteral(value, options.textType, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_DIRECTION") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSearchDirectionLiteral(value, options.direction, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_MODE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSarModeLiteral(value, options.mode, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_LEAVE_CURSOR_AT") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseSarLeaveCursorLiteral(value, options.leaveCursorAt, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_CASE_SENSITIVE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_GLOBAL_SEARCH") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.globalSearch, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_RESTRICT_MARKED_BLOCK") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.restrictToMarkedBlock, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_ALL_WINDOWS") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				if (!parseBooleanLiteral(value, options.searchAllWindows, errorMessage)) return false;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_REPLACE_MODE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				MRSarMode mode = MRSarMode::ReplaceFirst;
				if (!parseSarModeLiteral(value, mode, errorMessage)) return false;
				options.mode = mode == MRSarMode::ReplaceAll ? MRSarMode::ReplaceAll : MRSarMode::ReplaceFirst;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "SAR_PROMPT_EACH_REPLACE") {
				MRSarDialogOptions options = configuredSarDialogOptions();
				bool promptEach = false;
				if (!parseBooleanLiteral(value, promptEach, errorMessage)) return false;
				if (promptEach) options.mode = MRSarMode::PromptEach;
				else if (options.mode == MRSarMode::PromptEach)
					options.mode = MRSarMode::ReplaceFirst;
				return setConfiguredSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_FILESPEC") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				options.filespec = trimAscii(value);
				if (options.filespec.empty()) options.filespec = "*.*";
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_TEXT") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				options.searchText = value;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_STARTING_PATH") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				options.startingPath = normalizeConfiguredPathInput(value);
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_SUBDIRECTORIES") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.searchSubdirectories, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_CASE_SENSITIVE") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_REGULAR_EXPRESSIONS") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.regularExpressions, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SEARCH_FILES_IN_MEMORY") {
				MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();
				if (!parseBooleanLiteral(value, options.searchFilesInMemory, errorMessage)) return false;
				return setConfiguredMultiSearchDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_FILESPEC") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.filespec = trimAscii(value);
				if (options.filespec.empty()) options.filespec = "*.*";
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_TEXT") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.searchText = value;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_REPLACEMENT") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.replacementText = value;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_STARTING_PATH") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				options.startingPath = normalizeConfiguredPathInput(value);
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_SUBDIRECTORIES") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.searchSubdirectories, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_CASE_SENSITIVE") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.caseSensitive, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_REGULAR_EXPRESSIONS") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.regularExpressions, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_FILES_IN_MEMORY") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.searchFilesInMemory, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "MULTI_SAR_KEEP_FILES_OPEN") {
				MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();
				if (!parseBooleanLiteral(value, options.keepFilesOpen, errorMessage)) return false;
				return setConfiguredMultiSarDialogOptions(options, errorMessage);
			}
			if (upper == "PDF_EXPORT_PATH") {
				MRPdfExportSettings settings = configuredPdfExportSettings();
				settings.outputPath = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_PAGE_SEPARATOR") {
				MRPdfExportSettings settings = configuredPdfExportSettings();
				settings.pageSeparatorLiteral = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_FONT_FAMILY") {
				MRPdfExportSettings settings = configuredPdfExportSettings();
				settings.fontFamily = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_FONT_SIZE") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 1 || parsed > 40) return setError(errorMessage, "PDF_EXPORT_FONT_SIZE must be within 1..40.");
				settings.fontSizePoints = static_cast<int>(parsed);
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_HEADER_LINE") {
				MRPdfExportSettings settings = configuredPdfExportSettings();
				settings.headerLine = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_FOOTER_LINE") {
				MRPdfExportSettings settings = configuredPdfExportSettings();
				settings.footerLine = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_USE_PRINT_MARGIN") {
				bool enabled = true;
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (!parseBooleanLiteral(value, enabled, errorMessage)) return false;
				if (!enabled) settings.textWidth = "0";
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_PRINT_MARGIN_COLUMNS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TEXT_WIDTH must be within 0..9999.");
				if (trimAscii(settings.textWidth) == "0") {
					if (errorMessage != nullptr) errorMessage->clear();
					return true;
				}
				settings.textWidth = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_TEXT_WIDTH") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TEXT_WIDTH must be within 0..9999.");
				settings.textWidth = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_LEFT_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_LEFT_MARGIN_POINTS must be within 0..9999.");
				settings.leftMarginPoints = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_RIGHT_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_RIGHT_MARGIN_POINTS must be within 0..9999.");
				settings.rightMarginPoints = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_TOP_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TOP_MARGIN_POINTS must be within 0..9999.");
				settings.topMarginPoints = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "PDF_EXPORT_BOTTOM_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);
				MRPdfExportSettings settings = configuredPdfExportSettings();

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_BOTTOM_MARGIN_POINTS must be within 0..9999.");
				settings.bottomMarginPoints = value;
				return setConfiguredPdfExportSettings(settings, errorMessage);
			}
			if (upper == "VIRTUAL_DESKTOPS") {
				int parsed = 1;
				try {
					parsed = std::stoi(value);
				} catch (...) {
					parsed = 1;
				}
				if (parsed < 1) parsed = 1;
				if (parsed > 9) parsed = 9;
				applyVirtualDesktopConfigurationChange(parsed);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "CYCLIC_VIRTUAL_DESKTOPS") {
				bool parsed = false;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredCyclicVirtualDesktops(parsed, errorMessage);
			}
			if (upper == "CURSOR_BEHAVIOUR") {
				MRCursorBehaviour behaviour = MRCursorBehaviour::BoundToText;
				if (!parseCursorBehaviourLiteral(value, behaviour, errorMessage)) return false;
				return setConfiguredCursorBehaviour(behaviour, errorMessage);
			}
			if (upper == "UI_INDENT_STYLE") {
				MRUiIndentStyle style = MRUiIndentStyle::KandR;
				if (!parseUiIndentStyleLiteral(value, style, errorMessage)) return false;
				return setConfiguredUiIndentStyle(style, errorMessage);
			}
			if (upper == "CURSOR_POSITION_MARKER") return setConfiguredCursorPositionMarker(value, errorMessage);
			if (upper == "AUTOLOAD_WORKSPACE") {
				bool parsed = false;
				if (!parseBooleanLiteral(value, parsed, errorMessage)) return false;
				return setConfiguredAutoloadWorkspace(parsed, errorMessage);
			}
			if (upper == "LOG_HANDLING") {
				MRLogHandling handling = MRLogHandling::Volatile;
				if (!parseLogHandlingLiteral(value, handling, errorMessage)) return false;
				return setConfiguredLogHandling(handling, errorMessage);
			}
			if (upper == "LOGFILE") return setConfiguredLogFilePath(value, errorMessage);
			if (upper == "AUTOEXEC_MACRO") return addConfiguredAutoexecMacroEntry(value, errorMessage);
			if (upper == "LASTFILEDIALOGPATH") return setConfiguredLastFileDialogPath(value, errorMessage);
			if (upper == "WORKSPACE") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MAX_PATH_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_PATH_HISTORY")) return false;
				return setConfiguredPathHistoryLimitValue(parsed, errorMessage);
			}
			if (upper == "MAX_FILE_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_FILE_HISTORY")) return false;
				return setConfiguredFileHistoryLimitValue(parsed, errorMessage);
			}
			if (upper == "PATH_HISTORY") {
				addSerializedHistoryEntry(dialogHistoryState(MRDialogHistoryScope::General).pathHistory, value, configuredPathHistoryLimit(), true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "FILE_HISTORY") {
				addSerializedHistoryEntry(dialogHistoryState(MRDialogHistoryScope::General).fileHistory, value, configuredFileHistoryLimit(), true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogLastPathKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedPath;

				if (!parseScopedHistoryPayload(value, "path", scope, parsedPath, errorMessage)) return false;
				return setScopedDialogLastPath(scope, parsedPath, errorMessage);
			}
			if (upper == kDialogPathHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				addSerializedHistoryEntry(dialogHistoryState(scope).pathHistory, parsedValue, configuredPathHistoryLimit(), true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogFileHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				addSerializedHistoryEntry(dialogHistoryState(scope).fileHistory, parsedValue, configuredFileHistoryLimit(), true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_FILESPEC_HISTORY") {
				addSerializedHistoryEntry(configuredMultiFilespecHistoryStorage(), value, configuredFileHistoryLimit(), false);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_PATH_HISTORY") {
				addSerializedHistoryEntry(configuredMultiPathHistoryStorage(), value, configuredPathHistoryLimit(), true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "DEFAULT_PROFILE_DESCRIPTION") return setConfiguredDefaultProfileDescription(value, errorMessage);
			if (upper == kKeymapSettingsKey) return setConfiguredKeymapFilePath(value, errorMessage);
			if (upper == "ACTIVE_KEYMAP_PROFILE" || upper == "KEYMAP_PROFILE" || upper == "KEYMAP_BIND") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kThemeSettingsKey) return setConfiguredColorThemeFilePath(value, errorMessage);
			break;
		}
		case MRSettingsKeyClass::Edit:
			return applyConfiguredEditSetupValue(key, value, errorMessage);
		case MRSettingsKeyClass::ColorInline:
			return applyConfiguredColorSetupValue(key, value, errorMessage);
	}
	return setError(errorMessage, "Unsupported MRSETUP key.");
}

bool applySettingsSnapshotAssignment(MRSettingsSnapshot &snapshot, const std::string &key, const std::string &value, std::string *errorMessage) {
	switch (classifySettingsKey(key)) {
		case MRSettingsKeyClass::Unknown:
			return setError(errorMessage, "Unsupported MRSETUP key.");
		case MRSettingsKeyClass::Version:
			if (trimAscii(value) != kCurrentSettingsVersion) return setError(errorMessage, "Unsupported settings version.");
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		case MRSettingsKeyClass::Path: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "SETTINGSPATH") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MACROPATH") {
				const std::string normalized = normalizeConfiguredPathInput(value);
				MRSettingsSnapshot::DialogHistoryState &generalHistory = snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)];

				if (!validateMacroDirectoryPath(value, errorMessage)) return false;
				snapshot.paths.macroPath = makeAbsolutePath(normalized);
				if (generalHistory.pathHistory.empty() && isReadableDirectory(snapshot.paths.macroPath)) addHistoryEntry(generalHistory.pathHistory, snapshot.paths.macroPath, snapshot.maxPathHistory);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "HELPPATH") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateHelpFilePath(value, errorMessage)) return false;
				snapshot.paths.helpUri = makeAbsolutePath(normalized);
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupHelpFile, snapshot.paths.helpUri, errorMessage);
			}
			if (upper == "TEMPDIR") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateTempDirectoryPath(value, errorMessage)) return false;
				snapshot.paths.tempPath = makeAbsolutePath(normalized);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "SHELLPATH") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateShellExecutablePath(value, errorMessage)) return false;
				snapshot.paths.shellUri = makeAbsolutePath(normalized);
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupShellExecutable, snapshot.paths.shellUri, errorMessage);
			}
			break;
		}
		case MRSettingsKeyClass::Global: {
			std::string upper = upperAscii(trimAscii(key));
			if (upper == "WINDOW_MANAGER") {
				if (!parseBooleanLiteral(value, snapshot.windowManagerEnabled, errorMessage)) return false;
				return true;
			}
			if (upper == "MESSAGES") {
				if (!parseBooleanLiteral(value, snapshot.menulineMessagesEnabled, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_TEXT_TYPE") {
				if (!parseSearchTextTypeLiteral(value, snapshot.searchDialogOptions.textType, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_DIRECTION") {
				if (!parseSearchDirectionLiteral(value, snapshot.searchDialogOptions.direction, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_MODE") {
				if (!parseSearchModeLiteral(value, snapshot.searchDialogOptions.mode, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_GLOBAL_SEARCH") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.globalSearch, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_RESTRICT_MARKED_BLOCK") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.restrictToMarkedBlock, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_ALL_WINDOWS") {
				if (!parseBooleanLiteral(value, snapshot.searchDialogOptions.searchAllWindows, errorMessage)) return false;
				return true;
			}
			if (upper == "SEARCH_LIST_ALL_OCCURRENCES") {
				bool listAll = false;
				if (!parseBooleanLiteral(value, listAll, errorMessage)) return false;
				snapshot.searchDialogOptions.mode = listAll ? MRSearchMode::ListAll : MRSearchMode::StopFirst;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "SAR_TEXT_TYPE") {
				if (!parseSearchTextTypeLiteral(value, snapshot.sarDialogOptions.textType, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_DIRECTION") {
				if (!parseSearchDirectionLiteral(value, snapshot.sarDialogOptions.direction, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_MODE") {
				if (!parseSarModeLiteral(value, snapshot.sarDialogOptions.mode, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_LEAVE_CURSOR_AT") {
				if (!parseSarLeaveCursorLiteral(value, snapshot.sarDialogOptions.leaveCursorAt, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_GLOBAL_SEARCH") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.globalSearch, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_RESTRICT_MARKED_BLOCK") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.restrictToMarkedBlock, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_ALL_WINDOWS") {
				if (!parseBooleanLiteral(value, snapshot.sarDialogOptions.searchAllWindows, errorMessage)) return false;
				return true;
			}
			if (upper == "SAR_REPLACE_MODE") {
				MRSarMode mode = MRSarMode::ReplaceFirst;
				if (!parseSarModeLiteral(value, mode, errorMessage)) return false;
				snapshot.sarDialogOptions.mode = mode == MRSarMode::ReplaceAll ? MRSarMode::ReplaceAll : MRSarMode::ReplaceFirst;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "SAR_PROMPT_EACH_REPLACE") {
				bool promptEach = false;
				if (!parseBooleanLiteral(value, promptEach, errorMessage)) return false;
				if (promptEach) snapshot.sarDialogOptions.mode = MRSarMode::PromptEach;
				else if (snapshot.sarDialogOptions.mode == MRSarMode::PromptEach)
					snapshot.sarDialogOptions.mode = MRSarMode::ReplaceFirst;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_FILESPEC") {
				snapshot.multiSearchDialogOptions.filespec = trimAscii(value);
				if (snapshot.multiSearchDialogOptions.filespec.empty()) snapshot.multiSearchDialogOptions.filespec = "*.*";
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_TEXT") {
				snapshot.multiSearchDialogOptions.searchText = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_STARTING_PATH") {
				snapshot.multiSearchDialogOptions.startingPath = normalizeConfiguredPathInput(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SEARCH_SUBDIRECTORIES") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.searchSubdirectories, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SEARCH_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SEARCH_REGULAR_EXPRESSIONS") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.regularExpressions, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SEARCH_FILES_IN_MEMORY") {
				if (!parseBooleanLiteral(value, snapshot.multiSearchDialogOptions.searchFilesInMemory, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_FILESPEC") {
				snapshot.multiSarDialogOptions.filespec = trimAscii(value);
				if (snapshot.multiSarDialogOptions.filespec.empty()) snapshot.multiSarDialogOptions.filespec = "*.*";
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_TEXT") {
				snapshot.multiSarDialogOptions.searchText = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_REPLACEMENT") {
				snapshot.multiSarDialogOptions.replacementText = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_STARTING_PATH") {
				snapshot.multiSarDialogOptions.startingPath = normalizeConfiguredPathInput(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_SAR_SUBDIRECTORIES") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.searchSubdirectories, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_CASE_SENSITIVE") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.caseSensitive, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_REGULAR_EXPRESSIONS") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.regularExpressions, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_FILES_IN_MEMORY") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.searchFilesInMemory, errorMessage)) return false;
				return true;
			}
			if (upper == "MULTI_SAR_KEEP_FILES_OPEN") {
				if (!parseBooleanLiteral(value, snapshot.multiSarDialogOptions.keepFilesOpen, errorMessage)) return false;
				return true;
			}
			if (upper == "PDF_EXPORT_PATH") {
				snapshot.pdfExportSettings.outputPath = value;
				if (!trimAscii(value).empty()) return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::PdfExport, value, errorMessage);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_PAGE_SEPARATOR") {
				snapshot.pdfExportSettings.pageSeparatorLiteral = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_FONT_FAMILY") {
				snapshot.pdfExportSettings.fontFamily = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_FONT_SIZE") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 1 || parsed > 40) return setError(errorMessage, "PDF_EXPORT_FONT_SIZE must be within 1..40.");
				snapshot.pdfExportSettings.fontSizePoints = static_cast<int>(parsed);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_HEADER_LINE") {
				snapshot.pdfExportSettings.headerLine = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_FOOTER_LINE") {
				snapshot.pdfExportSettings.footerLine = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_USE_PRINT_MARGIN") {
				bool enabled = true;
				if (!parseBooleanLiteral(value, enabled, errorMessage)) return false;
				if (!enabled) snapshot.pdfExportSettings.textWidth = "0";
				return true;
			}
			if (upper == "PDF_EXPORT_PRINT_MARGIN_COLUMNS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TEXT_WIDTH must be within 0..9999.");
				if (trimAscii(snapshot.pdfExportSettings.textWidth) != "0") snapshot.pdfExportSettings.textWidth = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_TEXT_WIDTH") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TEXT_WIDTH must be within 0..9999.");
				snapshot.pdfExportSettings.textWidth = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_LEFT_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_LEFT_MARGIN_POINTS must be within 0..9999.");
				snapshot.pdfExportSettings.leftMarginPoints = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_RIGHT_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_RIGHT_MARGIN_POINTS must be within 0..9999.");
				snapshot.pdfExportSettings.rightMarginPoints = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_TOP_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_TOP_MARGIN_POINTS must be within 0..9999.");
				snapshot.pdfExportSettings.topMarginPoints = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "PDF_EXPORT_BOTTOM_MARGIN_POINTS") {
				const std::string trimmed = trimAscii(value);
				char *end = nullptr;
				const long parsed = std::strtol(trimmed.c_str(), &end, 10);

				if (trimmed.empty() || end == trimmed.c_str() || end == nullptr || *end != '\0' || parsed < 0 || parsed > 9999) return setError(errorMessage, "PDF_EXPORT_BOTTOM_MARGIN_POINTS must be within 0..9999.");
				snapshot.pdfExportSettings.bottomMarginPoints = value;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "VIRTUAL_DESKTOPS") {
				int parsed = 1;
				try {
					parsed = std::stoi(value);
				} catch (...) {
					parsed = 1;
				}
				if (parsed < 1) parsed = 1;
				if (parsed > 9) parsed = 9;
				snapshot.virtualDesktops = parsed;
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "CYCLIC_VIRTUAL_DESKTOPS") {
				if (!parseBooleanLiteral(value, snapshot.cyclicVirtualDesktops, errorMessage)) return false;
				return true;
			}
			if (upper == "CURSOR_BEHAVIOUR") {
				if (!parseCursorBehaviourLiteral(value, snapshot.cursorBehaviour, errorMessage)) return false;
				return true;
			}
			if (upper == "UI_INDENT_STYLE") {
				if (!parseUiIndentStyleLiteral(value, snapshot.uiIndentStyle, errorMessage)) return false;
				return true;
			}
			if (upper == "CURSOR_POSITION_MARKER") return normalizeCursorPositionMarker(value, snapshot.cursorPositionMarker, errorMessage);
			if (upper == "AUTOLOAD_WORKSPACE") {
				if (!parseBooleanLiteral(value, snapshot.autoloadWorkspace, errorMessage)) return false;
				return true;
			}
			if (upper == "LOG_HANDLING") {
				if (!parseLogHandlingLiteral(value, snapshot.logHandling, errorMessage)) return false;
				return true;
			}
			if (upper == "LOGFILE") {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateLogFilePath(value, errorMessage)) return false;
				snapshot.logFilePath = makeAbsolutePath(normalized);
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupLogFile, snapshot.logFilePath, errorMessage);
			}
			if (upper == "AUTOEXEC_MACRO") {
				const std::string normalized = normalizeAutoexecMacroEntry(value);
				if (!validateAutoexecMacroEntry(normalized, errorMessage)) return false;
				if (std::find(snapshot.autoexecMacros.begin(), snapshot.autoexecMacros.end(), normalized) == snapshot.autoexecMacros.end()) snapshot.autoexecMacros.push_back(normalized);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "LASTFILEDIALOGPATH") return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::General, value, errorMessage);
			if (upper == "WORKSPACE") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MAX_PATH_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_PATH_HISTORY")) return false;
				return setSnapshotPathHistoryLimit(snapshot, parsed, errorMessage);
			}
			if (upper == "MAX_FILE_HISTORY") {
				int parsed = 0;
				if (!parseHistoryLimitLiteral(value, parsed, errorMessage, "MAX_FILE_HISTORY")) return false;
				return setSnapshotFileHistoryLimit(snapshot, parsed, errorMessage);
			}
			if (upper == "PATH_HISTORY") {
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)].pathHistory, value, snapshot.maxPathHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "FILE_HISTORY") {
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(MRDialogHistoryScope::General)].fileHistory, value, snapshot.maxFileHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogLastPathKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedPath;

				if (!parseScopedHistoryPayload(value, "path", scope, parsedPath, errorMessage)) return false;
				return setSnapshotScopedDialogLastPath(snapshot, scope, parsedPath, errorMessage);
			}
			if (upper == kDialogPathHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(scope)].pathHistory, parsedValue, snapshot.maxPathHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kDialogFileHistoryKey) {
				MRDialogHistoryScope scope = MRDialogHistoryScope::General;
				std::string parsedValue;

				if (!parseScopedHistoryPayload(value, "value", scope, parsedValue, errorMessage)) return false;
				addSerializedHistoryEntry(snapshot.dialogHistory[dialogHistoryScopeIndex(scope)].fileHistory, parsedValue, snapshot.maxFileHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_FILESPEC_HISTORY") {
				addSerializedHistoryEntry(snapshot.multiFilespecHistory, value, snapshot.maxFileHistory, false);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "MULTI_PATH_HISTORY") {
				addSerializedHistoryEntry(snapshot.multiPathHistory, value, snapshot.maxPathHistory, true);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == "DEFAULT_PROFILE_DESCRIPTION") {
				snapshot.defaultProfileDescription = trimAscii(value);
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kKeymapSettingsKey) {
				const std::string normalized = normalizeConfiguredPathInput(value);
				struct stat st;

				if (normalized.empty()) {
					snapshot.keymapFilePath.clear();
					if (errorMessage != nullptr) errorMessage->clear();
					return true;
				}
				if (::stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return setError(errorMessage, "Keymap URI must include a filename.");
				snapshot.keymapFilePath = makeAbsolutePath(normalized);
				if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::KeymapProfileLoad, snapshot.keymapFilePath, errorMessage)) return false;
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::KeymapProfileSave, snapshot.keymapFilePath, errorMessage);
			}
			if (upper == "ACTIVE_KEYMAP_PROFILE" || upper == "KEYMAP_PROFILE" || upper == "KEYMAP_BIND") {
				if (errorMessage != nullptr) errorMessage->clear();
				return true;
			}
			if (upper == kThemeSettingsKey) {
				const std::string normalized = normalizeConfiguredPathInput(value);

				if (!validateColorThemeFilePath(value, errorMessage)) return false;
				snapshot.colorThemeFilePath = makeAbsolutePath(normalized);
				if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeLoad, snapshot.colorThemeFilePath, errorMessage)) return false;
				return setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeSave, snapshot.colorThemeFilePath, errorMessage);
			}
			break;
		}
		case MRSettingsKeyClass::Edit:
			return applyEditSetupValueInternal(snapshot.editSettings, key, value, errorMessage);
		case MRSettingsKeyClass::ColorInline:
			return applyColorSetupValueInternal(snapshot.colorSettings, key, value, errorMessage);
	}
	return setError(errorMessage, "Unsupported MRSETUP key.");
}

bool applySettingsSnapshotEditExtensionProfileDirective(MRSettingsSnapshot &snapshot, const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, std::string *errorMessage) {
	std::string op = upperAscii(trimAscii(operation));
	std::string id = canonicalEditProfileId(profileId);
	std::vector<MREditExtensionProfile> profiles = snapshot.editProfiles;
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
		return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
	}
	if (profile == nullptr) return setError(errorMessage, "Unknown extension profile id: " + id);
	if (op == "EXT") {
		profile->extensions.push_back(arg3);
		return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
	}
	if (op == "SET") {
		if (upperAscii(trimAscii(arg3)) == kWindowColorThemeProfileKey) {
			std::string normalizedTheme = canonicalWindowColorThemeUri(arg4);
			if (!normalizedTheme.empty() && !validateColorThemeFilePath(normalizedTheme, errorMessage)) return false;
			profile->windowColorThemeUri = normalizedTheme;
			return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
		}
		const MREditSettingDescriptor *descriptor = editSettingDescriptorByKeyInternal(arg3);

		if (descriptor == nullptr) return setError(errorMessage, "Unknown edit setting key for extension profile.");
		if (!descriptor->profileSupported) return setError(errorMessage, std::string("Setting is global-only and cannot be overridden: ") + descriptor->key);
		if (!applyEditSetupValueInternal(profile->overrides.values, descriptor->key, arg4, errorMessage)) return false;
		profile->overrides.mask |= descriptor->overrideBit;
		return setSnapshotEditProfiles(snapshot, profiles, errorMessage);
	}
	return setError(errorMessage, "MRFEPROFILE supports operations DEFINE, EXT and SET.");
}

bool loadColorThemeFileIntoSettingsSnapshot(MRSettingsSnapshot &snapshot, std::string *errorMessage) {
	std::string normalized = normalizeConfiguredPathInput(snapshot.colorThemeFilePath);
	std::string source;
	std::map<std::string, std::string> assignments;
	static const char *const order[] = {"WINDOWCOLORS", "MENUDIALOGCOLORS", "HELPCOLORS", "OTHERCOLORS", "MINIMAPCOLORS", "CODECOLORS"};

	if (!validateColorThemeFilePath(normalized, errorMessage)) return false;
	if (!ensureColorThemeFileExists(normalized, errorMessage)) return false;
	if (!readTextFile(normalized, source)) return setError(errorMessage, "Unable to read color theme file: " + normalized);
	if (!parseThemeSetupAssignments(source, assignments, errorMessage)) return false;
	for (const char *key : order) {
		std::string applyError;
		if (!applyColorSetupValueInternal(snapshot.colorSettings, key, assignments[key], &applyError)) return setError(errorMessage, "Theme apply failed for " + std::string(key) + ": " + applyError);
	}
	snapshot.colorThemeFilePath = makeAbsolutePath(normalized);
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeLoad, snapshot.colorThemeFilePath, errorMessage)) return false;
	if (!setSnapshotScopedDialogLastPath(snapshot, MRDialogHistoryScope::SetupThemeSave, snapshot.colorThemeFilePath, errorMessage)) return false;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}
