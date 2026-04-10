#include "MREditProfilesDraftSupport.hpp"

#include "MREditProfilesRecordSupport.hpp"

#include "../app/TMREditorApp.hpp"
#include "../config/MRDialogPaths.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>

namespace MREditProfilesDialogInternal {
namespace {

const char *kDefaultProfileId = "DEFAULT";

enum : unsigned int {
	kOvNone = 0x0000,
	kOvPageBreak = ::kOvPageBreak,
	kOvWordDelimiters = ::kOvWordDelimiters,
	kOvDefaultExtensions = ::kOvDefaultExtensions,
	kOvTruncateSpaces = ::kOvTruncateSpaces,
	kOvEofCtrlZ = ::kOvEofCtrlZ,
	kOvEofCrLf = ::kOvEofCrLf,
	kOvTabExpand = ::kOvTabExpand,
	kOvTabSize = ::kOvTabSize,
	kOvRightMargin = ::kOvRightMargin,
	kOvWordWrap = ::kOvWordWrap,
	kOvIndentStyle = ::kOvIndentStyle,
	kOvFileType = ::kOvFileType,
	kOvBinaryRecordLength = ::kOvBinaryRecordLength,
	kOvPostLoadMacro = ::kOvPostLoadMacro,
	kOvPreSaveMacro = ::kOvPreSaveMacro,
	kOvDefaultPath = ::kOvDefaultPath,
	kOvFormatLine = ::kOvFormatLine,
	kOvBackupFiles = ::kOvBackupFiles,
	kOvShowEofMarker = ::kOvShowEofMarker,
	kOvShowEofMarkerEmoji = ::kOvShowEofMarkerEmoji,
	kOvShowLineNumbers = ::kOvShowLineNumbers,
	kOvLineNumZeroFill = ::kOvLineNumZeroFill,
	kOvPersistentBlocks = ::kOvPersistentBlocks,
	kOvCodeFolding = ::kOvCodeFolding,
	kOvColumnBlockMove = ::kOvColumnBlockMove,
	kOvDefaultMode = ::kOvDefaultMode
};

[[nodiscard]] bool validateProfileIdLiteral(const EditProfileDraft &draft, std::string &errorText) {
	std::string id = trimAscii(draft.id);

	if (draft.isDefault) {
		errorText.clear();
		return true;
	}
	if (id.empty()) {
		errorText = "Profile ID may not be empty.";
		return false;
	}
	for (char ch : id)
		if (!(std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-' || ch == '.')) {
			errorText = "Profile ID allows only letters, digits, '_', '-' and '.'.";
			return false;
		}
	errorText.clear();
	return true;
}

[[nodiscard]] bool validateProfileNameLiteral(const EditProfileDraft &draft, std::string &errorText) {
	if (draft.isDefault) {
		errorText.clear();
		return true;
	}
	if (trimAscii(draft.name).empty()) {
		errorText = "Description may not be empty.";
		return false;
	}
	errorText.clear();
	return true;
}

[[nodiscard]] bool validateProfileExtensionsLiteral(const EditProfileDraft &draft, std::string &errorText) {
	std::vector<std::string> selectors;

	if (draft.isDefault) {
		errorText.clear();
		return true;
	}
	selectors = splitExtensionLiteral(draft.extensionsLiteral);
	if (selectors.size() > 1) {
		errorText = "Profile extension accepts exactly one selector.";
		return false;
	}
	if (!normalizeEditExtensionSelectors(selectors, &errorText)) {
		if (errorText.rfind("Extensions", 0) == 0)
			errorText.replace(0, std::strlen("Extensions"), "Extension");
		return false;
	}
	errorText.clear();
	return true;
}

[[nodiscard]] bool validateProfileColorThemeLiteral(const EditProfileDraft &draft, std::string &errorText) {
	std::string themeUri = trimAscii(draft.colorThemeUri);

	if (themeUri.empty()) {
		errorText.clear();
		return true;
	}
	return validateColorThemeFilePath(themeUri, &errorText);
}

[[nodiscard]] bool validateDraftRecordFields(const EditProfileDraft &draft, std::string &errorText) {
	MREditSetupSettings ignored;
	return recordToSettings(draft.settingsRecord, ignored, errorText);
}

[[nodiscard]] bool validateDraftLocally(const EditProfileDraft &draft, std::string &errorText) {
	if (!validateProfileIdLiteral(draft, errorText))
		return false;
	if (!validateProfileNameLiteral(draft, errorText))
		return false;
	if (!validateProfileExtensionsLiteral(draft, errorText))
		return false;
	if (!validateProfileColorThemeLiteral(draft, errorText))
		return false;
	if (!validateDraftRecordFields(draft, errorText))
		return false;
	errorText.clear();
	return true;
}

[[nodiscard]] unsigned int computeOverrideMask(const MREditSetupSettings &defaults,
                                               const MREditSetupSettings &effective) {
	unsigned int mask = kOvNone;

	if (effective.pageBreak != defaults.pageBreak)
		mask |= kOvPageBreak;
	if (effective.wordDelimiters != defaults.wordDelimiters)
		mask |= kOvWordDelimiters;
	if (effective.defaultExtensions != defaults.defaultExtensions)
		mask |= kOvDefaultExtensions;
	if (effective.truncateSpaces != defaults.truncateSpaces)
		mask |= kOvTruncateSpaces;
	if (effective.eofCtrlZ != defaults.eofCtrlZ)
		mask |= kOvEofCtrlZ;
	if (effective.eofCrLf != defaults.eofCrLf)
		mask |= kOvEofCrLf;
	if (effective.tabExpand != defaults.tabExpand)
		mask |= kOvTabExpand;
	if (effective.tabSize != defaults.tabSize)
		mask |= kOvTabSize;
	if (effective.rightMargin != defaults.rightMargin)
		mask |= kOvRightMargin;
	if (effective.wordWrap != defaults.wordWrap)
		mask |= kOvWordWrap;
	if (upperAscii(effective.indentStyle) != upperAscii(defaults.indentStyle))
		mask |= kOvIndentStyle;
	if (upperAscii(effective.fileType) != upperAscii(defaults.fileType))
		mask |= kOvFileType;
	if (effective.binaryRecordLength != defaults.binaryRecordLength)
		mask |= kOvBinaryRecordLength;
	if (trimAscii(effective.postLoadMacro) != trimAscii(defaults.postLoadMacro))
		mask |= kOvPostLoadMacro;
	if (trimAscii(effective.preSaveMacro) != trimAscii(defaults.preSaveMacro))
		mask |= kOvPreSaveMacro;
	if (trimAscii(effective.defaultPath) != trimAscii(defaults.defaultPath))
		mask |= kOvDefaultPath;
	if (effective.formatLine != defaults.formatLine)
		mask |= kOvFormatLine;
	if (effective.backupFiles != defaults.backupFiles)
		mask |= kOvBackupFiles;
	if (effective.showEofMarker != defaults.showEofMarker)
		mask |= kOvShowEofMarker;
	if (effective.showEofMarkerEmoji != defaults.showEofMarkerEmoji)
		mask |= kOvShowEofMarkerEmoji;
	if (effective.showLineNumbers != defaults.showLineNumbers)
		mask |= kOvShowLineNumbers;
	if (effective.lineNumZeroFill != defaults.lineNumZeroFill)
		mask |= kOvLineNumZeroFill;
	if (effective.persistentBlocks != defaults.persistentBlocks)
		mask |= kOvPersistentBlocks;
	if (effective.codeFolding != defaults.codeFolding)
		mask |= kOvCodeFolding;
	if (upperAscii(effective.columnBlockMove) != upperAscii(defaults.columnBlockMove))
		mask |= kOvColumnBlockMove;
	if (upperAscii(effective.defaultMode) != upperAscii(defaults.defaultMode))
		mask |= kOvDefaultMode;
	return mask;
}


[[nodiscard]] std::string joinExtensionsLiteral(const std::vector<std::string> &extensions) {
	std::string out;
	for (std::size_t i = 0; i < extensions.size(); ++i) {
		if (i != 0)
			out += "; ";
		out += extensions[i];
	}
	return out;
}

[[nodiscard]] std::string nextUniqueProfileId(const std::vector<EditProfileDraft> &existingDrafts,
                                              const std::string &seed) {
	std::string base = trimAscii(seed).empty() ? std::string("profile") : trimAscii(seed);
	for (int suffix = 0;; ++suffix) {
		std::string candidate = suffix == 0 ? base : base + std::to_string(suffix + 1);
		std::string folded = upperAscii(candidate);
		bool used = false;
		for (const EditProfileDraft &existing : existingDrafts)
			if (upperAscii(trimAscii(existing.id)) == folded) {
				used = true;
				break;
			}
		if (!used)
			return candidate;
	}
}

[[nodiscard]] bool draftsToConfiguredState(const std::vector<EditProfileDraft> &drafts,
                                           MREditSetupSettings &defaultsOut,
                                           std::vector<MREditExtensionProfile> &profilesOut,
                                           std::string &defaultThemePathOut, std::string &errorText) {
	bool haveDefault = false;
	std::string defaultDescription;

	profilesOut.clear();
	defaultThemePathOut.clear();
	for (const EditProfileDraft &draft : drafts) {
		if (!validateDraftLocally(draft, errorText)) {
			std::string label = draft.isDefault ? std::string("DEFAULT") : trimAscii(draft.id);
			errorText = label + ": " + errorText;
			return false;
		}
		if (draft.isDefault) {
			if (!recordToSettings(draft.settingsRecord, defaultsOut, errorText)) {
				errorText = "DEFAULT: " + errorText;
				return false;
			}
			defaultDescription = trimAscii(draft.name);
			defaultThemePathOut = trimAscii(draft.colorThemeUri);
			if (defaultThemePathOut.empty())
				defaultThemePathOut = defaultColorThemeFilePath();
			if (!validateColorThemeFilePath(defaultThemePathOut, &errorText)) {
				errorText = "DEFAULT: " + errorText;
				return false;
			}
			haveDefault = true;
			break;
		}
	}

	if (!haveDefault) {
		errorText = "DEFAULT profile is missing.";
		return false;
	}

	for (const EditProfileDraft &draft : drafts) {
		MREditSetupSettings effective;
		MREditExtensionProfile profile;

		if (draft.isDefault)
			continue;
		if (!recordToSettings(draft.settingsRecord, effective, errorText)) {
			errorText = trimAscii(draft.id) + ": " + errorText;
			return false;
		}
		profile.id = trimAscii(draft.id);
		profile.name = trimAscii(draft.name);
		profile.extensions = splitExtensionLiteral(draft.extensionsLiteral);
		profile.windowColorThemeUri = trimAscii(draft.colorThemeUri);
		profile.overrides.values = effective;
		profile.overrides.mask = computeOverrideMask(defaultsOut, effective);
		profilesOut.push_back(profile);
	}

	if (!setConfiguredDefaultProfileDescription(defaultDescription, &errorText))
		return false;
	if (!setConfiguredColorThemeFilePath(defaultThemePathOut, &errorText))
		return false;
	if (!setConfiguredEditSetupSettings(defaultsOut, &errorText))
		return false;
	if (!setConfiguredEditExtensionProfiles(profilesOut, &errorText))
		return false;
	return true;
}

std::string joinCommaSeparatedLocal(const std::vector<std::string> &values) {
	std::string out;
	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i != 0)
			out += ", ";
		out += values[i];
	}
	return out;
}

[[nodiscard]] std::string profileOwnerLabel(const EditProfileDraft &draft) {
	std::string id = trimAscii(draft.id);
	std::string name = trimAscii(draft.name);

	if (draft.isDefault)
		return "DEFAULT";
	if (id.empty())
		id = "<empty>";
	if (name.empty())
		return id;
	return id + " (" + name + ")";
}

} // namespace

std::string joinCommaSeparated(const std::vector<std::string> &values) {
	return joinCommaSeparatedLocal(values);
}

std::vector<std::string> splitExtensionLiteral(const std::string &literal) {
	std::vector<std::string> values;
	std::string current;

	for (char ch : literal) {
		if (ch == ';' || ch == ',') {
			std::string token = trimAscii(current);
			if (!token.empty())
				values.push_back(token);
			current.clear();
		} else
			current.push_back(ch);
	}
	current = trimAscii(current);
	if (!current.empty())
		values.push_back(current);
	return values;
}

void settingsToRecordLocal(const MREditSetupSettings &settings, EditSettingsDialogRecord &record) {
	std::string columnMove = upperAscii(settings.columnBlockMove);
	std::string defaultMode = upperAscii(settings.defaultMode);

	std::memset(&record, 0, sizeof(record));
	writeRecordField(record.pageBreak, sizeof(record.pageBreak), settings.pageBreak);
	writeRecordField(record.wordDelimiters, sizeof(record.wordDelimiters), settings.wordDelimiters);
	writeRecordField(record.defaultExtensions, sizeof(record.defaultExtensions), settings.defaultExtensions);
	writeRecordField(record.tabSize, sizeof(record.tabSize), std::to_string(settings.tabSize));
	writeRecordField(record.rightMargin, sizeof(record.rightMargin), std::to_string(settings.rightMargin));
	writeRecordField(record.postLoadMacro, sizeof(record.postLoadMacro), settings.postLoadMacro);
	writeRecordField(record.preSaveMacro, sizeof(record.preSaveMacro), settings.preSaveMacro);
	writeRecordField(record.defaultPath, sizeof(record.defaultPath), settings.defaultPath);
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
	record.tabExpandChoice = settings.tabExpand ? kTabExpandSpaces : kTabExpandTabs;
	record.columnBlockMoveChoice = (columnMove == "LEAVE_SPACE") ? kColumnMoveLeaveSpace : kColumnMoveDeleteSpace;
	record.defaultModeChoice = (defaultMode == "OVERWRITE") ? kDefaultModeOverwrite : kDefaultModeInsert;
}

bool draftsEqual(const EditProfileDraft &lhs, const EditProfileDraft &rhs) {
	return lhs.isDefault == rhs.isDefault && trimAscii(lhs.id) == trimAscii(rhs.id) &&
	       trimAscii(lhs.name) == trimAscii(rhs.name) &&
	       trimAscii(lhs.extensionsLiteral) == trimAscii(rhs.extensionsLiteral) &&
	       trimAscii(lhs.colorThemeUri) == trimAscii(rhs.colorThemeUri) &&
	       recordsEqual(lhs.settingsRecord, rhs.settingsRecord);
}

bool draftListsEqual(const std::vector<EditProfileDraft> &lhs, const std::vector<EditProfileDraft> &rhs) {
	if (lhs.size() != rhs.size())
		return false;
	for (std::size_t i = 0; i < lhs.size(); ++i)
		if (!draftsEqual(lhs[i], rhs[i]))
			return false;
	return true;
}

EditProfileDraft draftFromProfile(const MREditExtensionProfile &profile) {
	EditProfileDraft draft;
	MREditSetupSettings effective = configuredEditSetupSettings();

	draft.isDefault = false;
	draft.id = profile.id;
	draft.name = profile.name;
	draft.extensionsLiteral = joinExtensionsLiteral(profile.extensions);
	draft.colorThemeUri = profile.windowColorThemeUri;
	effective = mergeEditSetupSettings(effective, profile.overrides);
	settingsToRecordLocal(effective, draft.settingsRecord);
	return draft;
}

EditProfileDraft makeDefaultDraft() {
	EditProfileDraft draft;
	MREditSetupSettings defaults = configuredEditSetupSettings();

	draft.isDefault = true;
	draft.id = kDefaultProfileId;
	draft.name = configuredDefaultProfileDescription();
	draft.extensionsLiteral.clear();
	draft.colorThemeUri = configuredColorThemeFilePath();
	settingsToRecordLocal(defaults, draft.settingsRecord);
	return draft;
}

std::string buildProfileListLabel(const EditProfileDraft &draft, std::size_t idWidth) {
	std::string id = draft.isDefault ? std::string(kDefaultProfileId) : trimAscii(draft.id);
	std::string name = trimAscii(draft.name);

	if (id.empty())
		id = "<empty>";
	if (name.empty())
		return id;
	if (id.size() < idWidth)
		id.append(idWidth - id.size(), ' ');
	return id + "  " + name;
}

EditProfileDraft makeNewDraft(const std::vector<EditProfileDraft> &existingDrafts) {
	EditProfileDraft draft;

	draft.isDefault = false;
	draft.id = nextUniqueProfileId(existingDrafts, "profile");
	draft.name = "New profile";
	draft.extensionsLiteral.clear();
	draft.colorThemeUri.clear();
	settingsToRecordLocal(configuredEditSetupSettings(), draft.settingsRecord);
	return draft;
}

EditProfileDraft makeCopiedDraft(const EditProfileDraft &source,
                                 const std::vector<EditProfileDraft> &existingDrafts) {
	EditProfileDraft draft = source;
	std::string sourceId = trimAscii(source.id);
	std::string baseId = sourceId.empty() ? std::string("profile") : sourceId + "_copy";

	draft.isDefault = false;
	draft.id = nextUniqueProfileId(existingDrafts, baseId);
	return draft;
}

bool validateDraftsForUi(const std::vector<EditProfileDraft> &drafts, int currentIndex,
                         std::string &errorText) {
	std::map<std::string, std::vector<std::size_t>> ids;
	std::map<std::string, std::vector<std::size_t>> exts;
	std::vector<std::size_t> order;

	order.reserve(drafts.size());
	if (currentIndex >= 0 && currentIndex < static_cast<int>(drafts.size()))
		order.push_back(static_cast<std::size_t>(currentIndex));
	for (std::size_t i = 0; i < drafts.size(); ++i)
		if (static_cast<int>(i) != currentIndex)
			order.push_back(i);

	for (std::size_t i : order) {
		std::string localError;
		if (!validateDraftLocally(drafts[i], localError)) {
			if (static_cast<int>(i) == currentIndex)
				errorText = localError;
			else
				errorText = profileOwnerLabel(drafts[i]) + ": " + localError;
			return false;
		}
	}

	for (std::size_t i = 0; i < drafts.size(); ++i) {
		if (drafts[i].isDefault)
			continue;
		ids[upperAscii(trimAscii(drafts[i].id))].push_back(i);
		{
			std::vector<std::string> selectors = splitExtensionLiteral(drafts[i].extensionsLiteral);
			std::string extError;
			if (!normalizeEditExtensionSelectors(selectors, &extError)) {
				if (extError.rfind("Extensions", 0) == 0)
					extError.replace(0, std::strlen("Extensions"), "Extension");
				if (static_cast<int>(i) == currentIndex)
					errorText = extError;
				else
					errorText = profileOwnerLabel(drafts[i]) + ": " + extError;
				return false;
			}
			if (!selectors.empty())
				exts[selectors.front()].push_back(i);
		}
	}

	for (const auto &entry : ids)
		if (entry.second.size() > 1) {
			std::vector<std::string> owners;
			for (std::size_t idx : entry.second)
				if (static_cast<int>(idx) != currentIndex)
					owners.push_back(profileOwnerLabel(drafts[idx]));
			if (currentIndex >= 0 &&
			    std::find(entry.second.begin(), entry.second.end(),
			              static_cast<std::size_t>(currentIndex)) != entry.second.end()) {
				errorText = "Duplicate profile ID '" + trimAscii(drafts[currentIndex].id) + "': " +
				            joinCommaSeparatedLocal(owners);
				return false;
			}
			errorText = "Duplicate profile ID '" + trimAscii(drafts[entry.second.front()].id) + "': " +
			            joinCommaSeparatedLocal(owners);
			return false;
		}

	for (const auto &entry : exts)
		if (entry.second.size() > 1) {
			std::vector<std::string> owners;
			for (std::size_t idx : entry.second)
				if (static_cast<int>(idx) != currentIndex)
					owners.push_back(profileOwnerLabel(drafts[idx]));
			if (currentIndex >= 0 &&
			    std::find(entry.second.begin(), entry.second.end(),
			              static_cast<std::size_t>(currentIndex)) != entry.second.end()) {
				errorText = "Duplicate profile extension '" + entry.first + "': " + joinCommaSeparatedLocal(owners);
				return false;
			}
			errorText = "Duplicate profile extension '" + entry.first + "': " + joinCommaSeparatedLocal(owners);
			return false;
		}

	errorText.clear();
	return true;
}

bool saveAndReloadEditProfiles(const std::vector<EditProfileDraft> &drafts, std::string &errorText) {
	TMREditorApp *app = dynamic_cast<TMREditorApp *>(TProgram::application);
	MRSetupPaths paths;
	MREditSetupSettings defaultsCandidate;
	std::vector<MREditExtensionProfile> profilesCandidate;
	std::string defaultThemePathCandidate;

	if (app == nullptr) {
		errorText = "Application error: TMREditorApp is unavailable.";
		return false;
	}
	if (!draftsToConfiguredState(drafts, defaultsCandidate, profilesCandidate, defaultThemePathCandidate,
	                             errorText))
		return false;

	paths.settingsMacroUri = configuredSettingsMacroFilePath();
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();

	if (!writeSettingsMacroFile(paths, &errorText))
		return false;
	if (!app->reloadSettingsMacroFromPath(paths.settingsMacroUri, &errorText))
		return false;

	errorText.clear();
	return true;
}

std::vector<std::string> dirtyDraftIds(const std::vector<EditProfileDraft> &initialDrafts,
                                       const std::vector<EditProfileDraft> &drafts) {
	std::vector<std::string> out;
	std::size_t count = std::max(initialDrafts.size(), drafts.size());
	for (std::size_t i = 0; i < count; ++i) {
		const EditProfileDraft *initial = i < initialDrafts.size() ? &initialDrafts[i] : nullptr;
		const EditProfileDraft *current = i < drafts.size() ? &drafts[i] : nullptr;
		if (initial != nullptr && current != nullptr && draftsEqual(*initial, *current))
			continue;
		std::string id;
		if (current != nullptr)
			id = trimAscii(current->id);
		if (id.empty() && initial != nullptr)
			id = trimAscii(initial->id);
		if (id.empty())
			id = "<empty>";
		out.push_back(id);
	}
	return out;
}

} // namespace MREditProfilesDialogInternal
