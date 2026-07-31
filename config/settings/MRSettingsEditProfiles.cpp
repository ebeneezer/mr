#include "MRSettingsEditSetup.hpp"
#include "MRSettingsEditConstants.hpp"
#include "MRSettingsRuntime.hpp"
#include "MRSettingsRuntimeState.hpp"
#include "MRSettingsThemesProfiles.hpp"
#include "../../app/utils/MRStringUtils.hpp"

#include <algorithm>
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

std::string normalizeLineNumbersPosition(const std::string &value) {
	const std::string normalized = upperAscii(trimAscii(value));
	if (normalized == "OFF" || normalized == "LEADING" || normalized == "TRAILING") return normalized;
	return std::string();
}

std::string normalizeCodeFoldingPosition(const std::string &value) {
	return normalizeLineNumbersPosition(value);
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

bool isLatexExtensionSelector(const std::string &value) {
	const std::string upper = upperAscii(value);

	return upper == "TEX" || upper == "LTX" || upper == "STY" || upper == "CLS";
}

bool editExtensionSelectorMatches(const std::string &selector, const std::string &ext) {
	std::string selectorUpper;
	std::string extUpper;

	if (selector == ext) return true;
	selectorUpper = upperAscii(selector);
	extUpper = upperAscii(ext);
	return selectorUpper == extUpper && isLatexExtensionSelector(selectorUpper) && isLatexExtensionSelector(extUpper);
}

unsigned long long supportedEditProfileOverrideMask() noexcept {
	static constexpr unsigned long long mask = kOvPageBreak | kOvWordDelimiters | kOvDefaultExtensions | kOvTruncateSpaces | kOvEofCtrlZ | kOvEofCrLf | kOvTabExpand | kOvDisplayTabs | kOvTabSize | kOvLeftMargin | kOvRightMargin | kOvFormatRuler | kOvWordWrap | kOvIndentStyle | kOvCodeLanguage | kOvCodeColoring | kOvFileType | kOvBinaryRecordLength | kOvPostLoadMacro | kOvPreSaveMacro | kOvDefaultPath | kOvFormatLine | kOvBackupFiles | kOvShowEofMarker | kOvShowEofMarkerEmoji | kOvLineNumZeroFill | kOvLineNumbersPosition | kOvMiniMapPosition | kOvMiniMapWidth | kOvMiniMapMarkerGlyph | kOvGutters | kOvPersistentBlocks | kOvCodeFoldingPosition | kOvColumnBlockMove | kOvDefaultMode | kOvCursorStatusColor;
	return mask;
}


} // namespace

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

std::vector<MREditExtensionProfile> configuredEditExtensionProfiles() {
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
	storeConfiguredEditProfiles(normalized);
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
			if (editExtensionSelectorMatches(selector, ext)) {
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
			if (editExtensionSelectorMatches(selector, ext)) {
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
			if (editExtensionSelectorMatches(selector, ext)) {
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
