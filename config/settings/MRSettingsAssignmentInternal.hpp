#ifndef MRSETTINGSASSIGNMENTINTERNAL_HPP
#define MRSETTINGSASSIGNMENTINTERNAL_HPP

#include "MRSettingsRuntime.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mr::settings_assignment {

constexpr const char *kWindowColorThemeProfileKey = "WINDOW_COLORTHEME_URI";
constexpr const char *kDialogLastPathKey = "DIALOG_LAST_PATH";
constexpr const char *kDialogPathHistoryKey = "DIALOG_PATH_HISTORY";
constexpr const char *kDialogFileHistoryKey = "DIALOG_FILE_HISTORY";

bool setError(std::string *errorMessage, const std::string &message);
void addHistoryEntry(std::vector<std::string> &entries, const std::string &value, int limit);
void addSerializedHistoryEntry(std::vector<std::string> &entries, const std::string &value, int limit, bool normalizeAsPath);
bool parseBooleanLiteral(const std::string &value, bool &outValue, std::string *errorMessage);
bool parseLogHandlingLiteral(const std::string &value, MRLogHandling &outValue, std::string *errorMessage);
bool parseLiveLogScrollDirectionLiteral(const std::string &value, MRLiveLogScrollDirection &outValue, std::string *errorMessage);
bool parseCursorBehaviourLiteral(const std::string &value, MRCursorBehaviour &outValue, std::string *errorMessage);
bool parseCompilerErrorMessagePlacementLiteral(const std::string &value, MRCompilerErrorMessagePlacement &outValue, std::string *errorMessage);
bool parseScrollbarVisibilityLiteral(const std::string &value, MRScrollbarVisibility &outValue, std::string *errorMessage);
bool parseUiIndentStyleLiteral(const std::string &value, MRUiIndentStyle &outValue, std::string *errorMessage);
bool parseFileCompareStartConfigurationLiteral(const std::string &value, MRFileCompareStartConfiguration &outValue, std::string *errorMessage);
bool normalizeFileCompareGutters(const std::string &value, std::string &out, std::string *errorMessage);
bool parseSearchTextTypeLiteral(const std::string &value, MRSearchTextType &outValue, std::string *errorMessage);
bool parseSearchDirectionLiteral(const std::string &value, MRSearchDirection &outValue, std::string *errorMessage);
bool parseSearchModeLiteral(const std::string &value, MRSearchMode &outValue, std::string *errorMessage);
bool parseSarModeLiteral(const std::string &value, MRSarMode &outValue, std::string *errorMessage);
bool parseSarLeaveCursorLiteral(const std::string &value, MRSarLeaveCursor &outValue, std::string *errorMessage);
bool normalizeCursorPositionMarker(const std::string &value, std::string &out, std::string *errorMessage);
bool applyKeymapProfileRecord(std::vector<MRKeymapProfile> &profiles, const std::string &value, std::string *errorMessage);
bool applyKeymapBindingRecord(std::vector<MRKeymapProfile> &profiles, const std::string &value, std::string *errorMessage);
bool parseActiveKeymapProfileRecord(const std::string &value, std::string &activeProfile, std::string *errorMessage);
bool parseScopedHistoryPayload(std::string_view payload, const char *valueMemberName, MRDialogHistoryScope &scopeOut, std::string &valueOut, std::string *errorMessage);

} // namespace mr::settings_assignment

#endif
