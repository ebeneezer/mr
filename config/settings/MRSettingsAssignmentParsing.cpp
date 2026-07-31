#include "../../app/MRVersion.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsAssignmentInternal.hpp"
#include "MRSettingsAssignments.hpp"
#include "MRSettingsCompilerProfiles.hpp"
#include "MRSettingsEditSetup.hpp"
#include "MRSettingsHistory.hpp"
#include "MRSettingsRuntime.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsSnapshotIO.hpp"
#include "MRSettingsThemesProfiles.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mr::settings_assignment {

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
static const char *const kFileCompareStartOriginalCompare = "ORIGINAL_COMPARE";
static const char *const kFileCompareStartCompareOriginal = "COMPARE_ORIGINAL";

struct MRSettingsKeyDescriptor {
	const char *key;
	MRSettingsKeyClass keyClass;
	bool serialized;
};

static const MRSettingsKeyDescriptor kFixedSettingsKeyDescriptors[] = {
    {mrSettingsVersionSetupKey(), MRSettingsKeyClass::Version, true},
    {"SETTINGSPATH", MRSettingsKeyClass::Path, true},
    {"MACROPATH", MRSettingsKeyClass::Path, true},
    {"HELPPATH", MRSettingsKeyClass::Path, true},
    {"TEMPDIR", MRSettingsKeyClass::Path, true},
    {"SHELLPATH", MRSettingsKeyClass::Path, true},
    {"WINDOW_MANAGER", MRSettingsKeyClass::Global, true},
    {"MESSAGES", MRSettingsKeyClass::Global, true},
    {"AUTODETECT_BINARY_FILES", MRSettingsKeyClass::Global, true},
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
    {"MULTI_SEARCH_WHOLE_WORDS", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_FILES_IN_MEMORY", MRSettingsKeyClass::Global, true},
    {"MULTI_SEARCH_RESTRICT_WORKSPACE", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_FILESPEC", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_TEXT", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_REPLACEMENT", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_STARTING_PATH", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_SUBDIRECTORIES", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_CASE_SENSITIVE", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_REGULAR_EXPRESSIONS", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_WHOLE_WORDS", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_FILES_IN_MEMORY", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_KEEP_FILES_OPEN", MRSettingsKeyClass::Global, true},
    {"MULTI_SAR_RESTRICT_WORKSPACE", MRSettingsKeyClass::Global, true},
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
    {"ACQUIRE_COMMAND", MRSettingsKeyClass::Global, true},
    {"ACQUIRE_COMMAND_HISTORY", MRSettingsKeyClass::Global, false},
    {"LIVE_LOG_REPORT_MESSAGE_LINE", MRSettingsKeyClass::Global, true},
    {"LIVE_LOG_REPORT_BEEP", MRSettingsKeyClass::Global, true},
    {"LIVE_LOG_REPORT_AUDIO", MRSettingsKeyClass::Global, true},
    {"LIVE_LOG_SCROLL_DIRECTION", MRSettingsKeyClass::Global, true},
    {"LIVE_LOG_LINE_NUMBERS", MRSettingsKeyClass::Global, true},
    {"LIVE_LOG_TIMESTAMPS", MRSettingsKeyClass::Global, true},
    {"LIVE_LOG_SYNTAX_HIGHLIGHTING", MRSettingsKeyClass::Global, true},
    {"LIVE_LOG_AUDIO_URI", MRSettingsKeyClass::Global, true},
    {"LIVE_LOG_JOURNAL_TAG_HISTORY", MRSettingsKeyClass::Global, false},
    {"AUDIO_PLAYER", MRSettingsKeyClass::Global, true},
    {"VIRTUAL_DESKTOPS", MRSettingsKeyClass::Global, true},
    {"CYCLIC_VIRTUAL_DESKTOPS", MRSettingsKeyClass::Global, true},
    {"CURSOR_BEHAVIOUR", MRSettingsKeyClass::Global, true},
    {"COMPILER_ERROR_MESSAGE_PLACEMENT", MRSettingsKeyClass::Global, true},
    {"SCROLLBAR_VISIBILITY", MRSettingsKeyClass::Global, true},
    {"TRACK_COMPILER_WARNINGS", MRSettingsKeyClass::Global, true},
	    {"TRACK_COMPILER_NOTES", MRSettingsKeyClass::Global, true},
	    {"UI_INDENT_STYLE", MRSettingsKeyClass::Global, true},
	    {"CURSOR_POSITION_MARKER", MRSettingsKeyClass::Global, true},
	    {kWindowColorThemeProfileKey, MRSettingsKeyClass::Global, true},
    {"FILE_COMPARE_ORIGINAL_LEADING_GUTTERS", MRSettingsKeyClass::Global, true},
    {"FILE_COMPARE_ORIGINAL_TRAILING_GUTTERS", MRSettingsKeyClass::Global, true},
    {"FILE_COMPARE_COMPARE_LEADING_GUTTERS", MRSettingsKeyClass::Global, true},
    {"FILE_COMPARE_COMPARE_TRAILING_GUTTERS", MRSettingsKeyClass::Global, true},
    {"FILE_COMPARE_START_CONFIGURATION", MRSettingsKeyClass::Global, true},
    {"FILE_COMPARE_COMPARE_PANEL_READ_ONLY", MRSettingsKeyClass::Global, true},
    {"AUTOSAVE_WORKSPACE", MRSettingsKeyClass::Global, true},
    {"AUTOLOAD_WORKSPACE", MRSettingsKeyClass::Global, true},
    {"LOG_HANDLING", MRSettingsKeyClass::Global, true},
    {"LOGFILE", MRSettingsKeyClass::Global, true},
    {"AUTOEXEC_MACRO", MRSettingsKeyClass::Global, false},
    {"LASTFILEDIALOGPATH", MRSettingsKeyClass::Global, false},
    {"WORKSPACE", MRSettingsKeyClass::Global, false},
    {"MAX_PATH_HISTORY", MRSettingsKeyClass::Global, true},
    {"MAX_FILE_HISTORY", MRSettingsKeyClass::Global, true},
    {"MAX_WORKSPACE_HISTORY", MRSettingsKeyClass::Global, true},
    {"PATH_HISTORY", MRSettingsKeyClass::Global, false},
    {"FILE_HISTORY", MRSettingsKeyClass::Global, false},
    {kDialogLastPathKey, MRSettingsKeyClass::Global, false},
    {kDialogPathHistoryKey, MRSettingsKeyClass::Global, false},
    {kDialogFileHistoryKey, MRSettingsKeyClass::Global, false},
    {"KEYMAP_PROFILE", MRSettingsKeyClass::Global, false},
    {"KEYMAP_BIND", MRSettingsKeyClass::Global, false},
    {"ACTIVE_KEYMAP_PROFILE", MRSettingsKeyClass::Global, false},
    {"DEFAULT_PROFILE_DESCRIPTION", MRSettingsKeyClass::Global, true},
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

bool parseLiveLogScrollDirectionLiteral(const std::string &value, MRLiveLogScrollDirection &outValue, std::string *errorMessage) {
	const std::string upper = upperAscii(trimAscii(value));

	if (upper == "DOWN") {
		outValue = MRLiveLogScrollDirection::Down;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == "UP") {
		outValue = MRLiveLogScrollDirection::Up;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "LIVE_LOG_SCROLL_DIRECTION must be DOWN or UP.");
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

bool parseCompilerErrorMessagePlacementLiteral(const std::string &value, MRCompilerErrorMessagePlacement &outValue, std::string *errorMessage) {
	const std::string upper = upperAscii(trimAscii(value));

	if (upper == "UNDER_CODE" || upper == "UNDER") {
		outValue = MRCompilerErrorMessagePlacement::UnderCode;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == "RIGHT_MARGIN" || upper == "RIGHT") {
		outValue = MRCompilerErrorMessagePlacement::RightMargin;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "COMPILER_ERROR_MESSAGE_PLACEMENT must be UNDER_CODE or RIGHT_MARGIN.");
}

bool parseScrollbarVisibilityLiteral(const std::string &value, MRScrollbarVisibility &outValue, std::string *errorMessage) {
	const std::string upper = upperAscii(trimAscii(value));

	if (upper == "SMART") {
		outValue = MRScrollbarVisibility::Smart;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == "ALWAYS") {
		outValue = MRScrollbarVisibility::Always;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "SCROLLBAR_VISIBILITY must be SMART or ALWAYS.");
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

bool parseFileCompareStartConfigurationLiteral(const std::string &value, MRFileCompareStartConfiguration &outValue, std::string *errorMessage) {
	const std::string upper = upperAscii(trimAscii(value));

	if (upper == kFileCompareStartOriginalCompare || upper == "ORIGINAL_COMPARE" || upper == "ORIGINAL<>COMPARE") {
		outValue = MRFileCompareStartConfiguration::OriginalCompare;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (upper == kFileCompareStartCompareOriginal || upper == "COMPARE_ORIGINAL" || upper == "COMPARE<>ORIGINAL") {
		outValue = MRFileCompareStartConfiguration::CompareOriginal;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "FILE_COMPARE_START_CONFIGURATION must be ORIGINAL_COMPARE or COMPARE_ORIGINAL.");
}

bool normalizeFileCompareGutters(const std::string &value, std::string &out, std::string *errorMessage) {
	out.clear();
	for (char ch : trimAscii(value)) {
		switch (static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(ch)))) {
			case 'M':
			case 'D':
			case 'L':
			case 'C':
				out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
				break;
			default:
				return setError(errorMessage, "FILE_COMPARE_*_GUTTERS may contain only M, D, L or C.");
		}
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
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

bool keymapDiagnosticsContainError(const std::vector<MRKeymapDiagnostic> &diagnostics) noexcept {
	for (const MRKeymapDiagnostic &diagnostic : diagnostics)
		if (diagnostic.severity == MRKeymapDiagnosticSeverity::Error) return true;
	return false;
}

std::string firstKeymapDiagnosticMessage(const std::vector<MRKeymapDiagnostic> &diagnostics) {
	for (const MRKeymapDiagnostic &diagnostic : diagnostics)
		if (!diagnostic.message.empty()) return diagnostic.message;
	return "Invalid keymap payload.";
}

bool applyKeymapProfileRecord(std::vector<MRKeymapProfile> &profiles, const std::string &value, std::string *errorMessage) {
	MRKeymapProfile profile;
	const std::vector<MRKeymapDiagnostic> diagnostics = parseKeymapProfilePayload(value, profile);

	if (keymapDiagnosticsContainError(diagnostics)) return setError(errorMessage, firstKeymapDiagnosticMessage(diagnostics));
	for (MRKeymapProfile &existing : profiles)
		if (existing.name == profile.name) {
			existing = profile;
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
	profiles.push_back(profile);
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applyKeymapBindingRecord(std::vector<MRKeymapProfile> &profiles, const std::string &value, std::string *errorMessage) {
	MRKeymapBindingRecord binding;
	const std::vector<MRKeymapDiagnostic> diagnostics = parseKeymapBindingPayload(value, binding);

	if (keymapDiagnosticsContainError(diagnostics)) return setError(errorMessage, firstKeymapDiagnosticMessage(diagnostics));
	for (MRKeymapProfile &profile : profiles)
		if (profile.name == binding.profileName) {
			profile.bindings.push_back(binding);
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
	return setError(errorMessage, "Binding references unknown keymap profile: " + binding.profileName);
}

bool parseActiveKeymapProfileRecord(const std::string &value, std::string &activeProfile, std::string *errorMessage) {
	MRKeymapProfile profile;
	const std::vector<MRKeymapDiagnostic> diagnostics = parseKeymapProfilePayload(value, profile);

	if (keymapDiagnosticsContainError(diagnostics)) return setError(errorMessage, firstKeymapDiagnosticMessage(diagnostics));
	activeProfile = profile.name;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

} // namespace mr::settings_assignment

using namespace mr::settings_assignment;

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

std::vector<std::string> canonicalSerializedSettingsKeys() {
	std::size_t editDescriptorCount = 0;
	const MREditSettingDescriptor *editDescriptors = editSettingDescriptors(editDescriptorCount);
	std::vector<std::string> keys;

	keys.reserve(canonicalSerializedSettingsKeyCount());
	for (const auto &descriptor : kFixedSettingsKeyDescriptors)
		if (descriptor.serialized) keys.push_back(descriptor.key);
	for (std::size_t i = 0; i < editDescriptorCount; ++i)
		keys.push_back(editDescriptors[i].key);
	return keys;
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
