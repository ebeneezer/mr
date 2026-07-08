#define Uses_TApplication
#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TChDirDialog
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TFileDialog
#define Uses_TDrawBuffer
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TObject
#define Uses_TRadioButtons
#define Uses_TRect
#define Uses_TStaticText
#define Uses_TSItem
#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

#include "MRSetup.hpp"
#include "MRSetupSections.hpp"

#include "../../app/MRCommands.hpp"
#include "../../app/MRCommandRouter.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRBentoBox.hpp"
#include "../../ui/MRMenuBar.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/widgets/MRNumericSlider.hpp"
#include "../../ui/widgets/MRDropList.hpp"
#include "../../ui/widgets/MRSpinner.hpp"
#include "../../ui/MRPalette.hpp"
#include "../MRDirtyGating.hpp"
#include "MRSetupCommon.hpp"
#include "../../app/commands/MRWindowCommands.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

TFrame *initSetupDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

using mr::dialogs::ensureMrmacExtension;
using mr::dialogs::readRecordField;
using mr::dialogs::writeRecordField;

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

void postSetupFlowError(const char *scope, const std::string &errorText) {
	if (errorText.empty()) return;
	std::string text = scope != nullptr ? std::string(scope) + ": " + errorText : errorText;
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
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

void discardQueuedCancelEvent() {
	discardQueuedCancelEventsForTarget(TProgram::application != nullptr ? static_cast<TView *>(TProgram::application) : static_cast<TView *>(TProgram::deskTop));
	discardQueuedCancelEventsForTarget(static_cast<TView *>(TProgram::deskTop));
}

bool pathIsRegularFile(const std::string &path) {
	struct stat st;

	if (path.empty()) return false;
	return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool confirmOverwriteForPath(const char *primaryLabel, const char *headline, const std::string &targetPath) {
	if (!pathIsRegularFile(targetPath)) return true;
	return mr::dialogs::showUnsavedChangesDialog(primaryLabel, headline, targetPath.c_str()) == mr::dialogs::UnsavedChangesChoice::Save;
}

bool chooseThemeFileForLoad(MRDialogHistoryScope scope, std::string &selectedUri) {
	char fileName[MAXPATH];
	ushort result;
	std::string seed = configuredLastFileDialogFilePath(scope);

	if (seed.empty()) {
		seed = configuredLastFileDialogPath(scope);
		if (!seed.empty()) {
			if (seed.back() != '/') seed += '/';
			seed += "*.mrmac";
		}
	}
	if (seed.empty()) seed = configuredColorThemeFilePath();
	writeRecordField(fileName, sizeof(fileName), seed);
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.mrmac", "LOAD COLOR THEME", "~N~ame", fdOKButton, fileName);
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
	std::string seed = configuredLastFileDialogPath(scope);

	if (!seed.empty()) {
		if (seed.back() != '/') seed += '/';
		seed += "*.mrmac";
	}
	if (seed.empty()) seed = configuredLastFileDialogFilePath(scope);
	if (seed.empty()) seed = configuredColorThemeFilePath();
	writeRecordField(fileName, sizeof(fileName), seed);
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.mrmac", "SAVE COLOR THEME AS", "~N~ame", fdOKButton, fileName);
	if (result == cmCancel) {
		discardQueuedCancelEvent();
		return false;
	}
	selectedUri = normalizeConfiguredPathInput(ensureMrmacExtension(fileName));
	if (!selectedUri.empty()) rememberLoadDialogPath(scope, selectedUri.c_str());
	return true;
}

enum : ushort {
	cmMrSetupPathsHelp = 3800,
	cmMrSetupPathsBrowseSettingsUri,
	cmMrSetupPathsBrowseMacroPath,
	cmMrSetupPathsBrowseHelpUri,
	cmMrSetupPathsBrowseTempPath,
	cmMrSetupPathsBrowseShellUri,
	cmMrSetupPathsBrowseLogUri,
	cmMrSetupPathsChooseAudioPlayer,
	cmMrSetupPathsAcceptAudioPlayer,
	cmMrSetupPathsBrowseAudioPlayer
};

enum {
	kPathFieldSize = 256,
	kHistoryNumberFieldSize = 16
};

struct PathsDialogRecord {
	char settingsMacroPath[kPathFieldSize];
	char macroDirectoryPath[kPathFieldSize];
	char helpFilePath[kPathFieldSize];
	char tempDirectoryPath[kPathFieldSize];
	char shellExecutablePath[kPathFieldSize];
	char audioPlayerPath[kPathFieldSize];
	char logFilePath[kPathFieldSize];
	ushort logHandlingChoice;
	char maxPathHistory[kHistoryNumberFieldSize];
	char maxFileHistory[kHistoryNumberFieldSize];
	char maxWorkspaceHistory[kHistoryNumberFieldSize];
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

TAttrPair configuredColorOr(TView *view, unsigned char paletteSlot, ushort fallbackColorIndex) {
	unsigned char biosAttr = 0;

	if (configuredColorSlotOverride(paletteSlot, biosAttr)) return TAttrPair(biosAttr);
	return view != nullptr ? view->getColor(fallbackColorIndex) : TAttrPair(0x70);
}

std::string readCurrentWorkingDirectory() {
	char cwd[PATH_MAX];

	if (::getcwd(cwd, sizeof(cwd)) == nullptr) return std::string();
	return std::string(cwd);
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
	result = mr::dialogs::execDialog(mr::dialogs::createDirectoryDialog(scope, cdNormal));
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
		data[kMrPaletteMiniMapDiagnostics - 1] = 0x4E;
		data[kMrPaletteCodeFolding - 1] = data[9 - 1];
		data[kMrPaletteCodeFoldingMarker - 1] = data[9 - 1];
		data[kMrPaletteStatusLine - 1] = data[2 - 1];
		data[kMrPaletteStatusLineBold - 1] = data[3 - 1];
		data[kMrPaletteStatusLineFunctionDescription - 1] = data[4 - 1];
		data[kMrPaletteStatusLineFunctionKey - 1] = data[5 - 1];
		data[kMrPaletteDesktop - 1] = 0x90;
		data[kMrPaletteVirtualDesktopMarker - 1] = 0x9F;
		data[kMrPaletteFileCompareTextEqual - 1] = 0x1A;
		data[kMrPaletteFileCompareTextMissing - 1] = 0x1C;
		data[kMrPaletteFileCompareTextInsert - 1] = 0x1E;
		data[kMrPaletteFileCompareTextOffset - 1] = 0x1F;
		data[kMrPaletteFileCompareGutterEqual - 1] = 0x1A;
		data[kMrPaletteFileCompareGutterMissing - 1] = 0x1C;
		data[kMrPaletteFileCompareGutterInsert - 1] = 0x1E;
		data[kMrPaletteFileCompareGutterOffset - 1] = 0x1F;
		data[kMrPaletteFileCompareMiniMapEqual - 1] = 0x1A;
		data[kMrPaletteFileCompareMiniMapMissing - 1] = 0x1C;
		data[kMrPaletteFileCompareMiniMapInsert - 1] = 0x1E;
		data[kMrPaletteFileCompareMiniMapOffset - 1] = 0x1F;
		data[kMrPaletteFileCompareBentoBorder - 1] = data[8 - 1];
		data[kMrPaletteFileComparePaneBorder - 1] = data[kMrPaletteFocusedPaneBorder - 1];
		data[kMrPaletteFileCompareBentoBorderBold - 1] = data[9 - 1];
		data[kMrPaletteFileCompareFormatRuler - 1] = data[kMrPaletteFormatRuler - 1];
		data[kMrPaletteFileCompareLineNumbers - 1] = data[kMrPaletteLineNumbers - 1];
		data[kMrPaletteFileCompareFocusedPaneBorder - 1] = data[kMrPaletteFocusedPaneBorder - 1];
		data[kMrPaletteFileCompareMiniMapNormal - 1] = data[kMrPaletteMiniMapNormal - 1];
		data[kMrPaletteFileCompareMiniMapViewport - 1] = data[kMrPaletteMiniMapViewport - 1];
		data[kMrPaletteFileCompareMiniMapChanged - 1] = data[kMrPaletteMiniMapChanged - 1];
		data[kMrPaletteFileCompareMiniMapFindMarker - 1] = data[kMrPaletteMiniMapFindMarker - 1];
		data[kMrPaletteFileCompareMiniMapErrorMarker - 1] = data[kMrPaletteMiniMapErrorMarker - 1];
		data[kMrPaletteDiagnosticInformation - 1] = 0x4E;
		data[kMrPaletteOutlineFileHeader - 1] = 0x1F;
		data[kMrPaletteOutlineLevel0 - 1] = 0x1F;
		data[kMrPaletteOutlineLevel1 - 1] = 0x1E;
		data[kMrPaletteOutlineLevel2 - 1] = 0x1B;
		data[kMrPaletteOutlineLevel3 - 1] = 0x1A;
		data[kMrPaletteOutlineLevel4 - 1] = 0x1D;
		data[kMrPaletteOutlineLevel5 - 1] = 0x19;
		data[kMrPaletteOutlineLevel6 - 1] = 0x1C;
		data[kMrPaletteOutlineLevel7 - 1] = 0x13;
		data[kMrPaletteOutlineLevel8 - 1] = 0x1F;
		data[kMrPaletteOutlineLevel9 - 1] = 0x1E;
		data[kMrPaletteSpinnerHandles - 1] = data[52 - 1];
		data[kMrPaletteSpinnerDisplay - 1] = data[50 - 1];
		data[kMrPaletteFocusedSpinnerHandles - 1] = data[58 - 1];
		data[kMrPaletteFocusedSpinnerDisplay - 1] = data[51 - 1];
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
	static const MRColorSetupGroup groups[] = {MRColorSetupGroup::Window, MRColorSetupGroup::MenuDialog, MRColorSetupGroup::Help, MRColorSetupGroup::Other, MRColorSetupGroup::MiniMap, MRColorSetupGroup::FileCompareMiniMap, MRColorSetupGroup::Code, MRColorSetupGroup::FileCompare};

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

bool recordsEqual(const PathsDialogRecord &lhs, const PathsDialogRecord &rhs) {
	return readRecordField(lhs.settingsMacroPath) == readRecordField(rhs.settingsMacroPath) && readRecordField(lhs.macroDirectoryPath) == readRecordField(rhs.macroDirectoryPath) &&
	       readRecordField(lhs.helpFilePath) == readRecordField(rhs.helpFilePath) && readRecordField(lhs.tempDirectoryPath) == readRecordField(rhs.tempDirectoryPath) &&
	       readRecordField(lhs.shellExecutablePath) == readRecordField(rhs.shellExecutablePath) && readRecordField(lhs.audioPlayerPath) == readRecordField(rhs.audioPlayerPath) &&
	       readRecordField(lhs.logFilePath) == readRecordField(rhs.logFilePath) && lhs.logHandlingChoice == rhs.logHandlingChoice &&
	       readRecordField(lhs.maxPathHistory) == readRecordField(rhs.maxPathHistory) && readRecordField(lhs.maxFileHistory) == readRecordField(rhs.maxFileHistory) &&
	       readRecordField(lhs.maxWorkspaceHistory) == readRecordField(rhs.maxWorkspaceHistory);
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
	writeRecordField(record.audioPlayerPath, sizeof(record.audioPlayerPath), configuredAudioPlayerPath());
	writeRecordField(record.logFilePath, sizeof(record.logFilePath), configuredLogFilePath());
	record.logHandlingChoice = logHandlingChoiceFrom(configuredLogHandling());
	writeRecordField(record.maxPathHistory, sizeof(record.maxPathHistory), std::to_string(configuredMaxPathHistory()));
	writeRecordField(record.maxFileHistory, sizeof(record.maxFileHistory), std::to_string(configuredMaxFileHistory()));
	writeRecordField(record.maxWorkspaceHistory, sizeof(record.maxWorkspaceHistory), std::to_string(configuredMaxWorkspaceHistory()));
}

bool validatePathsRecord(const PathsDialogRecord &record, std::string &errorText) {
	std::string settingsPath = normalizeConfiguredPathInput(readRecordField(record.settingsMacroPath));
	std::string macroDir = normalizeConfiguredPathInput(readRecordField(record.macroDirectoryPath));
	std::string helpPath = normalizeConfiguredPathInput(readRecordField(record.helpFilePath));
	std::string tempDir = normalizeConfiguredPathInput(readRecordField(record.tempDirectoryPath));
	std::string shellPath = normalizeConfiguredPathInput(readRecordField(record.shellExecutablePath));
	std::string logFile = normalizeConfiguredPathInput(readRecordField(record.logFilePath));
	std::string audioPlayer = normalizeConfiguredPathInput(readRecordField(record.audioPlayerPath));
	int maxPathHistory = 0;
	int maxFileHistory = 0;
	int maxWorkspaceHistory = 0;

	if (!validateSettingsMacroFilePath(settingsPath, &errorText)) return false;
	if (!validateMacroDirectoryPath(macroDir, &errorText)) return false;
	if (!validateHelpFilePath(helpPath, &errorText)) return false;
	if (!validateTempDirectoryPath(tempDir, &errorText)) return false;
	if (!validateShellExecutablePath(shellPath, &errorText)) return false;
	if (!audioPlayer.empty() && ::access(audioPlayer.c_str(), X_OK) != 0) {
		errorText = "Audio player URI is missing or not executable.";
		return false;
	}
	if (logHandlingFromChoice(record.logHandlingChoice) == MRLogHandling::Persist && !validateLogFilePath(logFile, &errorText)) return false;
	if (!parseNonNegativeIntegerField(trimAscii(readRecordField(record.maxPathHistory)), maxPathHistory) || maxPathHistory < 5 || maxPathHistory > 50) {
		errorText = "MAX_PATH_HISTORY must be within 5..50.";
		return false;
	}
	if (!parseNonNegativeIntegerField(trimAscii(readRecordField(record.maxFileHistory)), maxFileHistory) || maxFileHistory < 5 || maxFileHistory > 50) {
		errorText = "MAX_FILE_HISTORY must be within 5..50.";
		return false;
	}
	if (!parseNonNegativeIntegerField(trimAscii(readRecordField(record.maxWorkspaceHistory)), maxWorkspaceHistory) || maxWorkspaceHistory < 5 || maxWorkspaceHistory > 50) {
		errorText = "MAX_WORKSPACE_HISTORY must be within 5..50.";
		return false;
	}
	errorText.clear();
	return true;
}

void appendUniqueAudioPlayerPath(std::vector<std::string> &paths, const std::string &path) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty()) return;
	if (::access(normalized.c_str(), X_OK) != 0) return;
	if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) paths.push_back(normalized);
}

std::string pathJoinAudioExecutable(const std::string &directory, const char *name) {
	std::string out = directory.empty() ? std::string(".") : directory;

	if (!out.empty() && out.back() != '/') out.push_back('/');
	out += name != nullptr ? name : "";
	return out;
}

std::string audioExecutableFromPath(const char *name) {
	const char *pathValue = std::getenv("PATH");
	std::string path = pathValue != nullptr ? pathValue : "";
	std::size_t start = 0;

	while (start <= path.size()) {
		std::size_t end = path.find(':', start);
		std::string directory = end == std::string::npos ? path.substr(start) : path.substr(start, end - start);
		std::string candidate = pathJoinAudioExecutable(directory, name);

		if (::access(candidate.c_str(), X_OK) == 0) return normalizeConfiguredPathInput(candidate);
		if (end == std::string::npos) break;
		start = end + 1;
	}
	return std::string();
}

std::vector<std::string> detectedAudioPlayerPaths() {
	static const char *const names[] = {
	    "paplay",
	    "pw-play",
	    "aplay",
	    "play",
	    "mpg123",
	    "mpg321",
	    "ogg123",
	    "flac123",
	    "mpv",
	    "mplayer",
	    "cvlc",
	    "ffplay",
	    "canberra-gtk-play",
	    "gst-play-1.0",
	    "sndfile-play",
	};
	std::vector<std::string> paths;

	for (const char *name : names)
		appendUniqueAudioPlayerPath(paths, audioExecutableFromPath(name));
	return paths;
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
	const std::string originalAudioPlayer = configuredAudioPlayerPath();
	const int originalMaxPathHistory = configuredMaxPathHistory();
	const int originalMaxFileHistory = configuredMaxFileHistory();
	const int originalMaxWorkspaceHistory = configuredMaxWorkspaceHistory();
	const MRLogHandling originalLogHandling = configuredLogHandling();
	const std::string originalLogFile = configuredLogFilePath();
	const std::string maxPathHistoryText = trimAscii(readRecordField(record.maxPathHistory));
	const std::string maxFileHistoryText = trimAscii(readRecordField(record.maxFileHistory));
	const std::string maxWorkspaceHistoryText = trimAscii(readRecordField(record.maxWorkspaceHistory));
	const std::string logFileText = normalizeConfiguredPathInput(readRecordField(record.logFilePath));
	const std::string audioPlayerText = normalizeConfiguredPathInput(readRecordField(record.audioPlayerPath));
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
	if (!setConfiguredAudioPlayerPath(audioPlayerText, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		(void)setConfiguredTempDirectoryPath(originalTempPath, nullptr);
		(void)setConfiguredShellExecutablePath(originalShellPath, nullptr);
		return false;
	}
	if (!setConfiguredLogHandling(newLogHandling, &errorText)) return false;
	if (!setConfiguredLogFilePath(logFileText, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		(void)setConfiguredTempDirectoryPath(originalTempPath, nullptr);
		(void)setConfiguredShellExecutablePath(originalShellPath, nullptr);
		(void)setConfiguredAudioPlayerPath(originalAudioPlayer, nullptr);
		(void)setConfiguredLogHandling(originalLogHandling, nullptr);
		return false;
	}
	if (!applyConfiguredSettingsAssignment("MAX_PATH_HISTORY", maxPathHistoryText, dummyPaths, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		(void)setConfiguredTempDirectoryPath(originalTempPath, nullptr);
		(void)setConfiguredShellExecutablePath(originalShellPath, nullptr);
		(void)setConfiguredAudioPlayerPath(originalAudioPlayer, nullptr);
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
		(void)setConfiguredAudioPlayerPath(originalAudioPlayer, nullptr);
		(void)setConfiguredLogHandling(originalLogHandling, nullptr);
		(void)setConfiguredLogFilePath(originalLogFile, nullptr);
		(void)applyConfiguredSettingsAssignment("MAX_PATH_HISTORY", std::to_string(originalMaxPathHistory), dummyPaths, nullptr);
		return false;
	}
	if (!applyConfiguredSettingsAssignment("MAX_WORKSPACE_HISTORY", maxWorkspaceHistoryText, dummyPaths, &errorText)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		(void)setConfiguredTempDirectoryPath(originalTempPath, nullptr);
		(void)setConfiguredShellExecutablePath(originalShellPath, nullptr);
		(void)setConfiguredAudioPlayerPath(originalAudioPlayer, nullptr);
		(void)setConfiguredLogHandling(originalLogHandling, nullptr);
		(void)setConfiguredLogFilePath(originalLogFile, nullptr);
		(void)applyConfiguredSettingsAssignment("MAX_PATH_HISTORY", std::to_string(originalMaxPathHistory), dummyPaths, nullptr);
		(void)applyConfiguredSettingsAssignment("MAX_FILE_HISTORY", std::to_string(originalMaxFileHistory), dummyPaths, nullptr);
		return false;
	}
	if (!writeSettingsMacroFile(paths, &errorText, &writeReport)) {
		(void)setConfiguredSettingsMacroFilePath(originalSettingsPath, nullptr);
		(void)setConfiguredMacroDirectoryPath(originalMacroPath, nullptr);
		(void)setConfiguredHelpFilePath(originalHelpPath, nullptr);
		(void)setConfiguredTempDirectoryPath(originalTempPath, nullptr);
		(void)setConfiguredShellExecutablePath(originalShellPath, nullptr);
		(void)setConfiguredAudioPlayerPath(originalAudioPlayer, nullptr);
		(void)setConfiguredLogHandling(originalLogHandling, nullptr);
		(void)setConfiguredLogFilePath(originalLogFile, nullptr);
		(void)applyConfiguredSettingsAssignment("MAX_PATH_HISTORY", std::to_string(originalMaxPathHistory), dummyPaths, nullptr);
		(void)applyConfiguredSettingsAssignment("MAX_FILE_HISTORY", std::to_string(originalMaxFileHistory), dummyPaths, nullptr);
		(void)applyConfiguredSettingsAssignment("MAX_WORKSPACE_HISTORY", std::to_string(originalMaxWorkspaceHistory), dummyPaths, nullptr);
		return false;
	}
	mrLogSettingsWriteReport("installation/setup paths", writeReport);

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

	TPathsSetupDialog(const PathsDialogRecord &initialRecord)
	    : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(kVirtualDialogWidth, kVirtualDialogHeight), "PATHS", kVirtualDialogWidth, kVirtualDialogHeight, initSetupDialogFrame), mCurrentRecord(initialRecord) {
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
		if (mAudioPlayerDropList.handleOpenListEvent(event)) return;
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

			case cmMrSetupPathsChooseAudioPlayer:
				mAudioPlayerDropList.toggle(*this, mAudioPlayerListAnchor, detectedAudioPlayerPaths(), inputLineValue(mAudioPlayerPathField), this, cmMrSetupPathsAcceptAudioPlayer, 8);
				clearEvent(event);
				return;

			case cmMrSetupPathsAcceptAudioPlayer:
				acceptAudioPlayerSelection();
				clearEvent(event);
				return;

			case cmMrSetupPathsBrowseAudioPlayer:
				browseAudioPlayer();
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
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~H~elp", cmMrSetupPathsHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 0);
		int dialogWidth = kVirtualDialogWidth;
		int labelLeft = 2;
		int labelRight = 23;
		int inputLeft = 25;
		int glyphWidth = 2;
		int glyphRight = dialogWidth - 2;
		int glyphLeft = glyphRight - glyphWidth;
		int inputRight = glyphLeft;
		int audioBrowseRight = glyphRight;
		int audioBrowseLeft = audioBrowseRight - glyphWidth;
		int audioDropRight = audioBrowseLeft;
		int audioDropLeft = audioDropRight - glyphWidth;
		int audioInputRight = audioDropLeft;
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

		addLabel(TRect(labelLeft, 12, labelRight, 13), "Audio player URI: ");
		mAudioPlayerPathField = addInput(TRect(inputLeft, 12, audioInputRight, 13));
		mAudioPlayerDropList.createButton(*this, TRect(audioDropLeft, 12, audioDropRight, 13), mAudioPlayerPathField, this, cmMrSetupPathsChooseAudioPlayer, true);
		addBrowseButton(TRect(audioBrowseLeft, 12, audioBrowseRight, 13), cmMrSetupPathsBrowseAudioPlayer);
		mAudioPlayerListAnchor = TRect(inputLeft, 12, audioBrowseRight - 3, 13);

		mLogFilePathLabel = addLabel(TRect(labelLeft, 14, labelRight, 15), "Logfile URI: ");
		mLogFilePathField = addInput(TRect(inputLeft, 14, inputRight, 15));
		mLogFilePathBrowseButton = addBrowseButton(TRect(glyphLeft, 14, glyphRight, 15), cmMrSetupPathsBrowseLogUri);

		addLabel(TRect(labelLeft, 16, labelRight, 17), "Path history: ");
		mMaxPathHistorySlider = addNumericSlider(TRect(inputLeft, 16, inputRight, 17), 5, 50, 15, 1, 5);

		addLabel(TRect(labelLeft, 18, labelRight, 19), "File history: ");
		mMaxFileHistorySlider = addNumericSlider(TRect(inputLeft, 18, inputRight, 19), 5, 50, 15, 1, 5);

		addLabel(TRect(labelLeft, 20, labelRight, 21), "Workspace history: ");
		mMaxWorkspaceHistorySlider = addNumericSlider(TRect(inputLeft, 20, inputRight, 21), 5, 50, 15, 1, 5);

		addLabel(TRect(labelLeft, 22, dialogWidth - 2, 23), "Log handling:");
		mLogHandlingField = addRadioGroup(TRect(labelLeft, 23, labelLeft + 20, 26), new TSItem("~V~olatile log", new TSItem("~L~og to file", new TSItem("Use ~J~ournalctl", nullptr))));

		mr::dialogs::addManagedUniformButtonRow(*this, buttonLeft, buttonTop, 0, buttons);
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

	static std::string inputLineValue(TInputLine *inputLine) {
		char buffer[kPathFieldSize];

		if (inputLine == nullptr) return std::string();
		std::memset(buffer, 0, sizeof(buffer));
		inputLine->getData(buffer);
		return readRecordField(buffer);
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
		setInputLineValue(mAudioPlayerPathField, record.audioPlayerPath);
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
		if (mMaxWorkspaceHistorySlider != nullptr) {
			int32_t value = parseHistorySliderValueOrDefault(record.maxWorkspaceHistory, 15);
			mMaxWorkspaceHistorySlider->setData(&value);
		}
		updateLogFileFieldState();
	}

	void saveFieldsToRecord(PathsDialogRecord &record) const {
		int32_t maxPathHistory = 15;
		int32_t maxFileHistory = 15;
		int32_t maxWorkspaceHistory = 15;
		readInputLineValue(mSettingsMacroPathField, record.settingsMacroPath, sizeof(record.settingsMacroPath));
		readInputLineValue(mMacroDirectoryPathField, record.macroDirectoryPath, sizeof(record.macroDirectoryPath));
		readInputLineValue(mHelpFilePathField, record.helpFilePath, sizeof(record.helpFilePath));
		readInputLineValue(mTempDirectoryPathField, record.tempDirectoryPath, sizeof(record.tempDirectoryPath));
		readInputLineValue(mShellExecutablePathField, record.shellExecutablePath, sizeof(record.shellExecutablePath));
		readInputLineValue(mAudioPlayerPathField, record.audioPlayerPath, sizeof(record.audioPlayerPath));
		readInputLineValue(mLogFilePathField, record.logFilePath, sizeof(record.logFilePath));
		if (mLogHandlingField != nullptr) mLogHandlingField->getData((void *)&record.logHandlingChoice);
		if (mMaxPathHistorySlider != nullptr) mMaxPathHistorySlider->getData(&maxPathHistory);
		if (mMaxFileHistorySlider != nullptr) mMaxFileHistorySlider->getData(&maxFileHistory);
		if (mMaxWorkspaceHistorySlider != nullptr) mMaxWorkspaceHistorySlider->getData(&maxWorkspaceHistory);
		writeRecordField(record.maxPathHistory, sizeof(record.maxPathHistory), std::to_string(std::clamp(static_cast<int>(maxPathHistory), 5, 50)));
		writeRecordField(record.maxFileHistory, sizeof(record.maxFileHistory), std::to_string(std::clamp(static_cast<int>(maxFileHistory), 5, 50)));
		writeRecordField(record.maxWorkspaceHistory, sizeof(record.maxWorkspaceHistory), std::to_string(std::clamp(static_cast<int>(maxWorkspaceHistory), 5, 50)));
	}

	PathsDialogRecord collectRecordFromFields() const {
		PathsDialogRecord record = mCurrentRecord;
		saveFieldsToRecord(record);
		return record;
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
		if (browseUriWithFileDialog(MRDialogHistoryScope::SetupSettingsMacro, "SELECT SETTINGS MACRO URI", selected)) setInputValue(mSettingsMacroPathField, selected);
	}

	void browseMacroPath() {
		std::string selected;
		if (browsePathWithDirectoryDialog(MRDialogHistoryScope::SetupMacroDirectory, selected)) setInputValue(mMacroDirectoryPathField, selected);
	}

	void browseHelpUri() {
		std::string selected;
		if (browseUriWithFileDialog(MRDialogHistoryScope::SetupHelpFile, "SELECT HELP FILE URI", selected)) setInputValue(mHelpFilePathField, selected);
	}

	void browseTempPath() {
		std::string selected;
		if (browsePathWithDirectoryDialog(MRDialogHistoryScope::SetupTempDirectory, selected)) setInputValue(mTempDirectoryPathField, selected);
	}

	void browseShellUri() {
		std::string selected;
		if (browseUriWithFileDialog(MRDialogHistoryScope::SetupShellExecutable, "SELECT SHELL EXECUTABLE URI", selected)) setInputValue(mShellExecutablePathField, selected);
	}

	void acceptAudioPlayerSelection() {
		std::string selectedValue;

		if (!mAudioPlayerDropList.acceptSelection(selectedValue)) return;
		setInputValue(mAudioPlayerPathField, selectedValue);
	}

	void browseAudioPlayer() {
		char fileName[MAXPATH] = {0};
		const std::string currentPath = normalizeConfiguredPathInput(inputLineValue(mAudioPlayerPathField));
		ushort result = cmCancel;

		if (!currentPath.empty()) {
			const std::size_t slashPos = currentPath.find_last_of('/');

			if (slashPos != std::string::npos) {
				std::string seed = currentPath.substr(0, slashPos + 1);
				seed += "*.*";
				writeRecordField(fileName, sizeof(fileName), seed);
			} else
				writeRecordField(fileName, sizeof(fileName), currentPath);
		} else
			writeRecordField(fileName, sizeof(fileName), "*.*");
		result = execDialogWithDataCapture(new TFileDialog("*.*", "SELECT AUDIO PLAYER URI", "~N~ame", fdOpenButton, 0), fileName);
		if (result == cmCancel) {
			discardQueuedCancelEvent();
			return;
		}
		setInputValue(mAudioPlayerPathField, normalizeConfiguredPathInput(fileName));
	}

	void browseLogUri() {
		std::string selected;
		if (browseUriWithFileDialog(MRDialogHistoryScope::SetupLogFile, "SELECT LOGFILE URI", selected)) setInputValue(mLogFilePathField, selected);
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
	static const int kVirtualDialogHeight = 31;
	TInputLine *mSettingsMacroPathField = nullptr;
	TInputLine *mMacroDirectoryPathField = nullptr;
	TInputLine *mHelpFilePathField = nullptr;
	TInputLine *mTempDirectoryPathField = nullptr;
	TInputLine *mShellExecutablePathField = nullptr;
	TInputLine *mAudioPlayerPathField = nullptr;
	TInputLine *mLogFilePathField = nullptr;
	TInactiveStaticText *mLogFilePathLabel = nullptr;
	TInlineGlyphButton *mLogFilePathBrowseButton = nullptr;
	MRDropList mAudioPlayerDropList;
	TRect mAudioPlayerListAnchor;
	TRadioButtons *mLogHandlingField = nullptr;
	MRNumericSlider *mMaxPathHistorySlider = nullptr;
	MRNumericSlider *mMaxFileHistorySlider = nullptr;
	MRNumericSlider *mMaxWorkspaceHistorySlider = nullptr;
};

void showPathsHelpDialog() {
	std::vector<std::string> lines;
	lines.push_back("PATHS HELP");
	lines.push_back("");
	lines.push_back("Path setup overview.");
	lines.push_back("Configure settings URI, macro path, help URI, temp path and shell URI.");
	lines.push_back("Set max path/file/workspace history sizes (5..50, default 15).");
	lines.push_back("Close or Escape asks for confirmation when fields were modified.");
	(void)mr::dialogs::execDialog(createSetupSimplePreviewDialog("PATHS HELP", 74, 10, lines, false));
}

enum : ushort {
	cmMrSetupBackupsAutosaveHelp = 3810,
	cmMrSetupBackupsAutosaveBrowseDirectory,
	cmMrSetupFieldChanged
};

enum {
	kBackupDirectoryFieldSize = 256,
	kBackupExtensionFieldSize = 32,
	kAutosaveNumberFieldSize = 16
};

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
	return left.method == right.method && left.frequency == right.frequency && left.extension == right.extension && left.directory == right.directory && left.inactivity == right.inactivity &&
	       left.interval == right.interval;
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
	return lhs.backupMethod == rhs.backupMethod && lhs.backupFrequency == rhs.backupFrequency && lhs.backupExtension == rhs.backupExtension && lhs.backupDirectory == rhs.backupDirectory &&
	       lhs.autosaveInactivitySeconds == rhs.autosaveInactivitySeconds && lhs.autosaveIntervalSeconds == rhs.autosaveIntervalSeconds;
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
	(void)mr::dialogs::execDialog(createSetupSimplePreviewDialog("BACKUPS & AUTOSAVE HELP", 88, 11, lines, false));
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

	TBackupsAutosaveSetupDialog(const BackupsAutosaveDialogRecord &initialRecord)
	    : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(kVirtualDialogWidth, kVirtualDialogHeight), "BACKUPS & AUTOSAVE", kVirtualDialogWidth, kVirtualDialogHeight, initSetupDialogFrame), mCurrentRecord(initialRecord) {
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
		if (originalWhat == evBroadcast && (originalCommand == cmReleasedFocus || originalCommand == cmReceivedFocus || originalCommand == cmMRNumericSliderChanged) && originalInfoPtr != mBackupDirectoryBrowseButton)
			refreshValidationState();
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
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~H~elp", cmMrSetupBackupsAutosaveHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 0);
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

		mr::dialogs::addManagedUniformButtonRow(*this, buttonLeft, buttonTop, 0, buttons);
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

struct UserInterfaceSettingsDialogData {
	ushort flags = 0;
	ushort virtualDesktops = 1;
	ushort cursorBehaviourChoice = 1;
	ushort compilerErrorMessageChoice = 1;
	ushort fileCompareStartChoice = 0;
	ushort compilerDiagnosticFlags = 0;
	ushort scrollbarVisibilityChoice = 0;
	ushort uiIndentStyleChoice = 0;
	char cursorPositionMarker[12] = {0};
	char fileCompareOriginalLeadingGutters[8] = {0};
	char fileCompareOriginalTrailingGutters[8] = {0};
	char fileCompareCompareLeadingGutters[8] = {0};
	char fileCompareCompareTrailingGutters[8] = {0};
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

bool validateFileCompareGuttersInput(std::string_view value, std::string &errorText) {
	for (char ch : trimAscii(value)) {
		switch (static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(ch)))) {
			case 'M':
			case 'D':
			case 'L':
			case 'C':
				break;
			default:
				errorText = "File compare gutters may contain only M, D, L or C.";
				return false;
		}
	}
	errorText.clear();
	return true;
}

std::vector<std::string> fileCompareGutterSpinnerValues() {
	std::vector<std::string> values;

	values.reserve(5);
	values.push_back(" ");
	values.push_back("M");
	values.push_back("D");
	values.push_back("L");
	values.push_back("C");
	return values;
}

bool userInterfaceSettingsDialogDataEqual(const UserInterfaceSettingsDialogData &lhs, const UserInterfaceSettingsDialogData &rhs) {
	return lhs.flags == rhs.flags && lhs.virtualDesktops == rhs.virtualDesktops && lhs.cursorBehaviourChoice == rhs.cursorBehaviourChoice && lhs.compilerErrorMessageChoice == rhs.compilerErrorMessageChoice &&
	       lhs.fileCompareStartChoice == rhs.fileCompareStartChoice && lhs.compilerDiagnosticFlags == rhs.compilerDiagnosticFlags && lhs.scrollbarVisibilityChoice == rhs.scrollbarVisibilityChoice && lhs.uiIndentStyleChoice == rhs.uiIndentStyleChoice &&
	       readRecordField(lhs.cursorPositionMarker) == readRecordField(rhs.cursorPositionMarker) && readRecordField(lhs.fileCompareOriginalLeadingGutters) == readRecordField(rhs.fileCompareOriginalLeadingGutters) &&
	       readRecordField(lhs.fileCompareOriginalTrailingGutters) == readRecordField(rhs.fileCompareOriginalTrailingGutters) && readRecordField(lhs.fileCompareCompareLeadingGutters) == readRecordField(rhs.fileCompareCompareLeadingGutters) &&
	       readRecordField(lhs.fileCompareCompareTrailingGutters) == readRecordField(rhs.fileCompareCompareTrailingGutters);
}

class TIndentStylePreview : public TView {
  public:
	TIndentStylePreview(const TRect &bounds) noexcept : TView(bounds) {
		eventMask = 0;
	}

	void setStyle(ushort choice) {
		if (mStyleChoice != choice) {
			mStyleChoice = choice;
			drawView();
		}
	}

	void draw() override {
		TDrawBuffer buffer;
		const TAttrPair boxColor(0x1F);
		const auto lines = previewLinesFor(mStyleChoice);
		const char topLeft = static_cast<char>(0xC9);
		const char topRight = static_cast<char>(0xBB);
		const char bottomLeft = static_cast<char>(0xC8);
		const char bottomRight = static_cast<char>(0xBC);
		const char horizontal = static_cast<char>(0xCD);
		const char vertical = static_cast<char>(0xBA);

		for (int y = 0; y < size.y; ++y) {
			buffer.moveChar(0, ' ', boxColor, size.x);
			if (y == 0 || y == size.y - 1) {
				buffer.putChar(0, y == 0 ? topLeft : bottomLeft);
				buffer.moveChar(1, horizontal, boxColor, std::max(0, size.x - 2));
				buffer.putChar(size.x - 1, y == 0 ? topRight : bottomRight);
			} else {
				buffer.putChar(0, vertical);
				buffer.putChar(size.x - 1, vertical);
				const int lineIndex = y - 1;
				if (lineIndex >= 0 && lineIndex < static_cast<int>(lines.size())) buffer.moveStr(2, lines[static_cast<std::size_t>(lineIndex)], boxColor, std::max(0, size.x - 3));
			}
			writeLine(0, y, size.x, 1, buffer);
		}
	}

  private:
	static std::array<const char *, 7> previewLinesFor(ushort choice) noexcept {
		switch (choice) {
			case 5:
				return {"if (ready)", "{   work(width);", "    notify(width);", "}", "else", "{   recover(width);", "}"};
			case 4:
				return {"if (ready)", "    {", "    work(width);", "    }", "else", "    {", "    recover(width);"};
			case 3:
				return {"if (ready)", "  {", "    work(width);", "  }", "else", "  {", "    recover(width);"};
			case 2:
				return {"if (ready)", "{", "    work(width);", "}", "else", "{", "    recover(width);"};
			case 1:
				return {"if (ready) {", "    work(width);", "    notify(width);", "} else {", "    recover(width);", "    notify(width);", "}"};
			case 0:
			default:
				return {"if (ready) {", "  work(width);", "  notify(width);", "} else {", "  recover(width);", "  notify(width);", "}"};
		}
	}

	ushort mStyleChoice = 0;
};

class TUserInterfaceSettingsDialog : public MRScrollableDialog {
  public:
	TUserInterfaceSettingsDialog(bool initialWindowManager, bool initialMenulineMessages, int initialVirtualDesktops, bool initialCyclicVirtualDesktops, MRCursorBehaviour initialCursorBehaviour,
	                            MRCompilerErrorMessagePlacement initialCompilerErrorMessagePlacement, MRScrollbarVisibility initialScrollbarVisibility, bool initialTrackCompilerWarnings, bool initialTrackCompilerNotes,
	                            MRUiIndentStyle initialUiIndentStyle, const std::string &initialCursorPositionMarker, const std::string &initialFileCompareOriginalLeadingGutters, const std::string &initialFileCompareOriginalTrailingGutters,
	                            const std::string &initialFileCompareCompareLeadingGutters, const std::string &initialFileCompareCompareTrailingGutters, MRFileCompareStartConfiguration initialFileCompareStartConfiguration,
	                            bool initialFileCompareComparePanelReadOnly)
	    : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(86, 32), "USER INTERFACE SETTINGS", 86, 32, initSetupDialogFrame) {

		int const yStart = 2;

		TCheckBoxes *cb = new TCheckBoxes(TRect(3, yStart, 36, yStart + 6),
		                                   new TSItem("~W~indow Manager",
		                                              new TSItem("~M~enuline messages",
		                                                         new TSItem("~C~ycle virtual desktops",
		                                                                    new TSItem("Track compiler ~w~arnings", new TSItem("Track compiler ~n~otes", new TSItem("R/~O~ file comparing", nullptr)))))));

		mOptionsField = cb;
		addManaged(mOptionsField, mOptionsField->getBounds());

		addManaged(new TStaticText(TRect(38, 2, 57, 3), "Cursor behaviour:"), TRect(38, 2, 57, 3));
		mCursorBehaviourField = new TRadioButtons(TRect(38, 3, 57, 6), new TSItem("~F~ree movement", new TSItem("~B~ound to text", nullptr)));
		addManaged(mCursorBehaviourField, TRect(38, 3, 57, 6));

		addManaged(new TStaticText(TRect(59, 2, 71, 3), "Scrollbars:"), TRect(59, 2, 71, 3));
		mScrollbarVisibilityField = new TRadioButtons(TRect(59, 3, 74, 6), new TSItem("~S~mart", new TSItem("~A~lways", nullptr)));
		addManaged(mScrollbarVisibilityField, TRect(59, 3, 74, 6));

		addManaged(new TStaticText(TRect(38, 7, 55, 8), "Compiler errors:"), TRect(38, 7, 55, 8));
		mCompilerErrorMessageField = new TRadioButtons(TRect(38, 8, 56, 11), new TSItem("~U~nder code", new TSItem("~R~ight margin", nullptr)));
		addManaged(mCompilerErrorMessageField, TRect(38, 8, 56, 11));

		addManaged(new TStaticText(TRect(58, 7, 78, 8), "Start configuration:"), TRect(58, 7, 78, 8));
		mFileCompareStartField = new TRadioButtons(TRect(58, 8, 83, 11), new TSItem("Original <> Compare", new TSItem("Compare <> Original", nullptr)));
		addManaged(mFileCompareStartField, TRect(58, 8, 83, 11));

		mVirtualDesktopsSlider = new MRNumericSlider(TRect(24, 12, 70, 13), 1, 9, initialVirtualDesktops, 1, 1, MRNumericSlider::fmtRaw, cmMRNumericSliderChanged);
		addManaged(mVirtualDesktopsSlider, TRect(24, 12, 70, 13));
		addManaged(new TLabel(TRect(2, 12, 23, 13), "~V~irtual desktops:", mVirtualDesktopsSlider), TRect(2, 12, 23, 13));

		mCursorPositionMarkerField = new TInputLine(TRect(28, 13, 42, 14), 11);
		addManaged(mCursorPositionMarkerField, TRect(28, 13, 42, 14));
		addManaged(new TLabel(TRect(2, 13, 27, 14), "Cursor position ~m~arker:", mCursorPositionMarkerField), TRect(2, 13, 27, 14));

		addManaged(new TStaticText(TRect(3, 16, 25, 17), "File compare gutters:"), TRect(3, 16, 25, 17));
		addManaged(new TStaticText(TRect(26, 16, 36, 17), "Original:"), TRect(26, 16, 36, 17));
		addFileCompareGutterSpinners(mFileCompareOriginalLeadingGutterSpinners, 37, 15);
		addFileCompareGutterSpinners(mFileCompareOriginalTrailingGutterSpinners, 43, 15);
		addManaged(new TStaticText(TRect(54, 16, 63, 17), "Compare:"), TRect(54, 16, 63, 17));
		addFileCompareGutterSpinners(mFileCompareCompareLeadingGutterSpinners, 64, 15);
		addFileCompareGutterSpinners(mFileCompareCompareTrailingGutterSpinners, 70, 15);

		addManaged(new TStaticText(TRect(3, 19, 20, 20), "Indent style:"), TRect(3, 19, 20, 20));
		mIndentStyleField = new TRadioButtons(TRect(3, 20, 23, 29), new TSItem("~K~&R", new TSItem("K&R~4~", new TSItem("~A~llman", new TSItem("~G~nome", new TSItem("~W~hitesmiths", new TSItem("~H~orstmann", nullptr)))))));
		addManaged(mIndentStyleField, TRect(3, 20, 23, 29));
		mIndentStylePreview = new TIndentStylePreview(TRect(25, 20, 47, 29));
		addManaged(mIndentStylePreview, TRect(25, 20, 47, 29));

		mInitialCursorBehaviourChoice = initialCursorBehaviour == MRCursorBehaviour::FreeMovement ? 0 : 1;
		mInitialCompilerErrorMessageChoice = initialCompilerErrorMessagePlacement == MRCompilerErrorMessagePlacement::UnderCode ? 0 : 1;
		mInitialFileCompareStartChoice = initialFileCompareStartConfiguration == MRFileCompareStartConfiguration::CompareOriginal ? 1 : 0;
		mInitialScrollbarVisibilityChoice = initialScrollbarVisibility == MRScrollbarVisibility::Always ? 1 : 0;
		mInitialCompilerDiagnosticFlags = (initialTrackCompilerWarnings ? 1 : 0) | (initialTrackCompilerNotes ? 2 : 0);
		writeRecordField(mDataCursorMarker, sizeof(mDataCursorMarker), initialCursorPositionMarker);
		mInitialFileCompareComparePanelReadOnly = initialFileCompareComparePanelReadOnly;
		if (mIndentStyleField != nullptr) {
			ushort styleChoice = 0;
			switch (initialUiIndentStyle) {
				case MRUiIndentStyle::KandR4:
					styleChoice = 1;
					break;
				case MRUiIndentStyle::Allman:
					styleChoice = 2;
					break;
				case MRUiIndentStyle::Gnome:
					styleChoice = 3;
					break;
				case MRUiIndentStyle::Whitesmiths:
					styleChoice = 4;
					break;
				case MRUiIndentStyle::Horstmann:
					styleChoice = 5;
					break;
				case MRUiIndentStyle::KandR:
				default:
					styleChoice = 0;
					break;
			}
			mIndentStyleField->setData(&styleChoice);
			mLastIndentStyleChoice = styleChoice;
			if (mIndentStylePreview != nullptr) mIndentStylePreview->setStyle(styleChoice);
		}
		setDialogValidationHook([this]() { return validateDialogValues(); });

		selectContent();
	}

	void handleEvent(TEvent &event) override {
		MRScrollableDialog::handleEvent(event);
		refreshIndentStylePreview();
	}

	void getData(void *rec) override {
		UserInterfaceSettingsDialogData *data = static_cast<UserInterfaceSettingsDialogData *>(rec);
		if (mOptionsField != nullptr) {
			ushort visualFlags = 0;
			mOptionsField->getData(&visualFlags);
			data->flags = visualFlags & 0x0003;
			if ((visualFlags & 0x0004) != 0) data->flags |= 0x0004;
			data->compilerDiagnosticFlags = static_cast<ushort>((visualFlags >> 3) & 0x0003);
			if ((visualFlags & 0x0020) != 0) data->flags |= 0x0008;
		}
		if (mVirtualDesktopsSlider != nullptr) {
			int32_t val = 1;
			mVirtualDesktopsSlider->getData(&val);
			data->virtualDesktops = static_cast<ushort>(val);
		}
		if (mCursorBehaviourField != nullptr) mCursorBehaviourField->getData(&data->cursorBehaviourChoice);
		if (mCompilerErrorMessageField != nullptr) mCompilerErrorMessageField->getData(&data->compilerErrorMessageChoice);
		if (mFileCompareStartField != nullptr) mFileCompareStartField->getData(&data->fileCompareStartChoice);
		if (mCompilerDiagnosticsField != nullptr) mCompilerDiagnosticsField->getData(&data->compilerDiagnosticFlags);
		if (mScrollbarVisibilityField != nullptr) mScrollbarVisibilityField->getData(&data->scrollbarVisibilityChoice);
		if (mIndentStyleField != nullptr) mIndentStyleField->getData(&data->uiIndentStyleChoice);
		if (mCursorPositionMarkerField != nullptr) mCursorPositionMarkerField->getData(data->cursorPositionMarker);
		writeRecordField(data->fileCompareOriginalLeadingGutters, sizeof(data->fileCompareOriginalLeadingGutters), fileCompareGutterSpinnersText(mFileCompareOriginalLeadingGutterSpinners));
		writeRecordField(data->fileCompareOriginalTrailingGutters, sizeof(data->fileCompareOriginalTrailingGutters), fileCompareGutterSpinnersText(mFileCompareOriginalTrailingGutterSpinners));
		writeRecordField(data->fileCompareCompareLeadingGutters, sizeof(data->fileCompareCompareLeadingGutters), fileCompareGutterSpinnersText(mFileCompareCompareLeadingGutterSpinners));
		writeRecordField(data->fileCompareCompareTrailingGutters, sizeof(data->fileCompareCompareTrailingGutters), fileCompareGutterSpinnersText(mFileCompareCompareTrailingGutterSpinners));
	}

	void setData(void *rec) override {
		UserInterfaceSettingsDialogData *data = static_cast<UserInterfaceSettingsDialogData *>(rec);
		if (mOptionsField != nullptr) {
			ushort visualFlags = static_cast<ushort>(data->flags & 0x0003);
			if ((data->flags & 0x0004) != 0) visualFlags |= 0x0004;
			visualFlags |= static_cast<ushort>((data->compilerDiagnosticFlags & 0x0003) << 3);
			if ((data->flags & 0x0008) != 0) visualFlags |= 0x0020;
			mOptionsField->setData(&visualFlags);
		}
		if (mVirtualDesktopsSlider != nullptr) {
			int32_t val = data->virtualDesktops;
			mVirtualDesktopsSlider->setData(&val);
		}
		if (mCursorBehaviourField != nullptr) {
			if (data->cursorBehaviourChoice > 1) data->cursorBehaviourChoice = mInitialCursorBehaviourChoice;
			mCursorBehaviourField->setData(&data->cursorBehaviourChoice);
		}
		if (mCompilerErrorMessageField != nullptr) {
			if (data->compilerErrorMessageChoice > 1) data->compilerErrorMessageChoice = mInitialCompilerErrorMessageChoice;
			mCompilerErrorMessageField->setData(&data->compilerErrorMessageChoice);
		}
		if (mFileCompareStartField != nullptr) {
			if (data->fileCompareStartChoice > 1) data->fileCompareStartChoice = mInitialFileCompareStartChoice;
			mFileCompareStartField->setData(&data->fileCompareStartChoice);
		}
		if (mCompilerDiagnosticsField != nullptr) {
			data->compilerDiagnosticFlags &= 3;
			if (data->compilerDiagnosticFlags > 3) data->compilerDiagnosticFlags = mInitialCompilerDiagnosticFlags;
			mCompilerDiagnosticsField->setData(&data->compilerDiagnosticFlags);
		}
		if (mScrollbarVisibilityField != nullptr) {
			if (data->scrollbarVisibilityChoice > 1) data->scrollbarVisibilityChoice = mInitialScrollbarVisibilityChoice;
			mScrollbarVisibilityField->setData(&data->scrollbarVisibilityChoice);
		}
		if (mIndentStyleField != nullptr) {
			if (data->uiIndentStyleChoice > 5) data->uiIndentStyleChoice = 0;
			mIndentStyleField->setData(&data->uiIndentStyleChoice);
		}
		if (mCursorPositionMarkerField != nullptr) {
			if (data->cursorPositionMarker[0] == '\0') writeRecordField(data->cursorPositionMarker, sizeof(data->cursorPositionMarker), mDataCursorMarker);
			mCursorPositionMarkerField->setData(data->cursorPositionMarker);
		}
		setFileCompareGutterSpinners(mFileCompareOriginalLeadingGutterSpinners, readRecordField(data->fileCompareOriginalLeadingGutters));
		setFileCompareGutterSpinners(mFileCompareOriginalTrailingGutterSpinners, readRecordField(data->fileCompareOriginalTrailingGutters));
		setFileCompareGutterSpinners(mFileCompareCompareLeadingGutterSpinners, readRecordField(data->fileCompareCompareLeadingGutters));
		setFileCompareGutterSpinners(mFileCompareCompareTrailingGutterSpinners, readRecordField(data->fileCompareCompareTrailingGutters));
	}

  private:
	static const std::size_t kFileCompareGutterSpinnerCount = 4;

	void addFileCompareGutterSpinners(std::array<MRSpinner *, kFileCompareGutterSpinnerCount> &spinners, int x, int y) {
		for (std::size_t i = 0; i < spinners.size(); ++i) {
			const int left = x + static_cast<int>(i);

			spinners[i] = new MRSpinner(TRect(left, y, left + 1, y + 3), fileCompareGutterSpinnerValues());
			addManaged(spinners[i], TRect(left, y, left + 1, y + 3));
		}
	}

	std::string fileCompareGutterSpinnersText(const std::array<MRSpinner *, kFileCompareGutterSpinnerCount> &spinners) const {
		std::string text;

		text.reserve(spinners.size());
		for (MRSpinner *spinner : spinners) {
			if (spinner == nullptr) continue;
			const std::string &value = spinner->currentValue();

			if (value != " ") text += value;
		}
		return text;
	}

	void setFileCompareGutterSpinners(std::array<MRSpinner *, kFileCompareGutterSpinnerCount> &spinners, std::string_view text) {
		std::size_t index = 0;

		for (MRSpinner *spinner : spinners)
			if (spinner != nullptr) spinner->setCurrentValue(" ");
		for (char ch : text) {
			if (index >= spinners.size()) break;
			char value = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

			switch (value) {
				case 'M':
				case 'D':
				case 'L':
				case 'C':
					if (spinners[index] != nullptr) spinners[index]->setCurrentValue(std::string(1, value));
					++index;
					break;
				default:
					break;
			}
		}
	}

	std::string currentCursorMarkerInput() const {
		char value[12] = {0};
		if (mCursorPositionMarkerField != nullptr) mCursorPositionMarkerField->getData(value);
		return readRecordField(value);
	}

	std::string currentFileCompareOriginalLeadingGuttersInput() const {
		return fileCompareGutterSpinnersText(mFileCompareOriginalLeadingGutterSpinners);
	}

	std::string currentFileCompareOriginalTrailingGuttersInput() const {
		return fileCompareGutterSpinnersText(mFileCompareOriginalTrailingGutterSpinners);
	}

	std::string currentFileCompareCompareLeadingGuttersInput() const {
		return fileCompareGutterSpinnersText(mFileCompareCompareLeadingGutterSpinners);
	}

	std::string currentFileCompareCompareTrailingGuttersInput() const {
		return fileCompareGutterSpinnersText(mFileCompareCompareTrailingGutterSpinners);
	}

	DialogValidationResult validateDialogValues() const {
		DialogValidationResult result;
		std::string errorText;
		result.valid = validateCursorPositionMarkerInput(currentCursorMarkerInput(), errorText);
		if (result.valid) result.valid = validateFileCompareGuttersInput(currentFileCompareOriginalLeadingGuttersInput(), errorText);
		if (result.valid) result.valid = validateFileCompareGuttersInput(currentFileCompareOriginalTrailingGuttersInput(), errorText);
		if (result.valid) result.valid = validateFileCompareGuttersInput(currentFileCompareCompareLeadingGuttersInput(), errorText);
		if (result.valid) result.valid = validateFileCompareGuttersInput(currentFileCompareCompareTrailingGuttersInput(), errorText);
		result.warningText = errorText;
		return result;
	}

	ushort currentIndentStyleChoice() const {
		ushort value = 0;
		if (mIndentStyleField != nullptr) const_cast<TRadioButtons *>(mIndentStyleField)->getData(&value);
		return value;
	}

	void refreshIndentStylePreview() {
		const ushort choice = currentIndentStyleChoice();
		if (choice == mLastIndentStyleChoice) return;
		mLastIndentStyleChoice = choice;
		if (mIndentStylePreview != nullptr) mIndentStylePreview->setStyle(choice);
	}

	TCheckBoxes *mOptionsField = nullptr;
	MRNumericSlider *mVirtualDesktopsSlider = nullptr;
	TRadioButtons *mCursorBehaviourField = nullptr;
	TRadioButtons *mCompilerErrorMessageField = nullptr;
	TRadioButtons *mFileCompareStartField = nullptr;
	TCheckBoxes *mCompilerDiagnosticsField = nullptr;
	TRadioButtons *mScrollbarVisibilityField = nullptr;
	TInputLine *mCursorPositionMarkerField = nullptr;
	std::array<MRSpinner *, kFileCompareGutterSpinnerCount> mFileCompareOriginalLeadingGutterSpinners{};
	std::array<MRSpinner *, kFileCompareGutterSpinnerCount> mFileCompareOriginalTrailingGutterSpinners{};
	std::array<MRSpinner *, kFileCompareGutterSpinnerCount> mFileCompareCompareLeadingGutterSpinners{};
	std::array<MRSpinner *, kFileCompareGutterSpinnerCount> mFileCompareCompareTrailingGutterSpinners{};
	TRadioButtons *mIndentStyleField = nullptr;
	TIndentStylePreview *mIndentStylePreview = nullptr;
	ushort mInitialCursorBehaviourChoice = 1;
	ushort mInitialCompilerErrorMessageChoice = 1;
	ushort mInitialFileCompareStartChoice = 0;
	ushort mInitialScrollbarVisibilityChoice = 0;
	ushort mInitialCompilerDiagnosticFlags = 0;
	bool mInitialFileCompareComparePanelReadOnly = true;
	ushort mLastIndentStyleChoice = 0;
	char mDataCursorMarker[12] = {0};
};

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
		if (TProgram::deskTop != nullptr) {
			TProgram::deskTop->redraw();
			TProgram::deskTop->drawView();
		}
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
				if (TProgram::deskTop != nullptr) {
					TProgram::deskTop->redraw();
					TProgram::deskTop->drawView();
				}
				break;
			}

			case cmMrColorSaveTheme: {
				std::string themeUri;
				std::string activeThemeDisplayName = configuredColorThemeDisplayName();

				if (!chooseThemeFileForSave(MRDialogHistoryScope::SetupThemeSave, themeUri)) break;
				if (!confirmOverwriteForPath("Overwrite", "Theme file exists. Overwrite?", themeUri)) break;
				if (!applyWorkingColorPaletteToConfigured(workingPalette, errorText)) {
					postSetupFlowError("Color Setup / Save Theme", errorText);
					break;
				}
				if (!setConfiguredColorThemeDisplayName(activeThemeDisplayName, &errorText)) {
					postSetupFlowError("Color Setup / Save Theme", errorText);
					break;
				}
				if (!writeColorThemeFile(themeUri, &errorText)) {
					postSetupFlowError("Color Setup / Save Theme", errorText);
					break;
				}
				if (!persistSettingsFileOnly(errorText)) {
					postSetupFlowError("Color Setup / Save settings", errorText);
					break;
				}
				TProgram::application->redraw();
				mrUpdateAllWindowsColorTheme();
				if (TProgram::deskTop != nullptr) {
					TProgram::deskTop->redraw();
					TProgram::deskTop->drawView();
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

void runUserInterfaceSettingsDialogFlow() {
	bool running = true;

	while (running) {
		bool currentWm = configuredWindowManager();
		bool currentMm = configuredMenulineMessages();
		int currentVd = configuredVirtualDesktops();
		bool currentCv = configuredCyclicVirtualDesktops();
		MRCursorBehaviour currentCb = configuredCursorBehaviour();
		MRCompilerErrorMessagePlacement currentCemp = configuredCompilerErrorMessagePlacement();
		MRScrollbarVisibility currentScrollbarVisibility = configuredScrollbarVisibility();
		bool currentTrackWarnings = configuredTrackCompilerWarnings();
		bool currentTrackNotes = configuredTrackCompilerNotes();
		MRUiIndentStyle currentUiIndentStyle = configuredUiIndentStyle();
		std::string currentCp = configuredCursorPositionMarker();
		std::string currentFileCompareOriginalLeadingGutters = configuredFileCompareOriginalLeadingGutters();
		std::string currentFileCompareOriginalTrailingGutters = configuredFileCompareOriginalTrailingGutters();
		std::string currentFileCompareCompareLeadingGutters = configuredFileCompareCompareLeadingGutters();
		std::string currentFileCompareCompareTrailingGutters = configuredFileCompareCompareTrailingGutters();
		MRFileCompareStartConfiguration currentFileCompareStartConfiguration = configuredFileCompareStartConfiguration();
		bool currentFileCompareComparePanelReadOnly = configuredFileCompareComparePanelReadOnly();

		TUserInterfaceSettingsDialog *dialog = new TUserInterfaceSettingsDialog(currentWm, currentMm, currentVd, currentCv, currentCb, currentCemp, currentScrollbarVisibility, currentTrackWarnings, currentTrackNotes, currentUiIndentStyle, currentCp,
		                                                                         currentFileCompareOriginalLeadingGutters, currentFileCompareOriginalTrailingGutters, currentFileCompareCompareLeadingGutters, currentFileCompareCompareTrailingGutters,
		                                                                         currentFileCompareStartConfiguration, currentFileCompareComparePanelReadOnly);
		UserInterfaceSettingsDialogData dialogData;
		if (currentWm) dialogData.flags |= 1;
		if (currentMm) dialogData.flags |= 2;
		if (currentCv) dialogData.flags |= 4;
		if (currentFileCompareComparePanelReadOnly) dialogData.flags |= 8;

		dialogData.virtualDesktops = static_cast<ushort>(currentVd);
		dialogData.cursorBehaviourChoice = currentCb == MRCursorBehaviour::FreeMovement ? 0 : 1;
		dialogData.compilerErrorMessageChoice = currentCemp == MRCompilerErrorMessagePlacement::UnderCode ? 0 : 1;
		dialogData.fileCompareStartChoice = currentFileCompareStartConfiguration == MRFileCompareStartConfiguration::CompareOriginal ? 1 : 0;
		dialogData.scrollbarVisibilityChoice = currentScrollbarVisibility == MRScrollbarVisibility::Always ? 1 : 0;
		if (currentTrackWarnings) dialogData.compilerDiagnosticFlags |= 1;
		if (currentTrackNotes) dialogData.compilerDiagnosticFlags |= 2;
		dialogData.uiIndentStyleChoice = static_cast<ushort>(currentUiIndentStyle);
		writeRecordField(dialogData.cursorPositionMarker, sizeof(dialogData.cursorPositionMarker), currentCp);
		writeRecordField(dialogData.fileCompareOriginalLeadingGutters, sizeof(dialogData.fileCompareOriginalLeadingGutters), currentFileCompareOriginalLeadingGutters);
		writeRecordField(dialogData.fileCompareOriginalTrailingGutters, sizeof(dialogData.fileCompareOriginalTrailingGutters), currentFileCompareOriginalTrailingGutters);
		writeRecordField(dialogData.fileCompareCompareLeadingGutters, sizeof(dialogData.fileCompareCompareLeadingGutters), currentFileCompareCompareLeadingGutters);
		writeRecordField(dialogData.fileCompareCompareTrailingGutters, sizeof(dialogData.fileCompareCompareTrailingGutters), currentFileCompareCompareTrailingGutters);

		UserInterfaceSettingsDialogData baselineData = dialogData;
		ushort result = execDialogWithDataCapture(dialog, &dialogData);
		bool newWm = (dialogData.flags & 1) != 0;
		bool newMm = (dialogData.flags & 2) != 0;
		bool newCv = (dialogData.flags & 4) != 0;
		bool newFileCompareComparePanelReadOnly = (dialogData.flags & 8) != 0;
		int newVd = static_cast<int>(dialogData.virtualDesktops);
		MRCursorBehaviour newCb = dialogData.cursorBehaviourChoice == 0 ? MRCursorBehaviour::FreeMovement : MRCursorBehaviour::BoundToText;
		MRCompilerErrorMessagePlacement newCemp = dialogData.compilerErrorMessageChoice == 0 ? MRCompilerErrorMessagePlacement::UnderCode : MRCompilerErrorMessagePlacement::RightMargin;
		MRFileCompareStartConfiguration newFileCompareStartConfiguration = dialogData.fileCompareStartChoice == 1 ? MRFileCompareStartConfiguration::CompareOriginal : MRFileCompareStartConfiguration::OriginalCompare;
		MRScrollbarVisibility newScrollbarVisibility = dialogData.scrollbarVisibilityChoice == 1 ? MRScrollbarVisibility::Always : MRScrollbarVisibility::Smart;
		bool newTrackWarnings = (dialogData.compilerDiagnosticFlags & 1) != 0;
		bool newTrackNotes = (dialogData.compilerDiagnosticFlags & 2) != 0;
		MRUiIndentStyle newUiIndentStyle = static_cast<MRUiIndentStyle>(dialogData.uiIndentStyleChoice);
		std::string newCp = readRecordField(dialogData.cursorPositionMarker);
		std::string newFileCompareOriginalLeadingGutters = readRecordField(dialogData.fileCompareOriginalLeadingGutters);
		std::string newFileCompareOriginalTrailingGutters = readRecordField(dialogData.fileCompareOriginalTrailingGutters);
		std::string newFileCompareCompareLeadingGutters = readRecordField(dialogData.fileCompareCompareLeadingGutters);
		std::string newFileCompareCompareTrailingGutters = readRecordField(dialogData.fileCompareCompareTrailingGutters);
		const bool changed = mr::dialogs::isDialogDraftDirty(baselineData, dialogData, userInterfaceSettingsDialogDataEqual);
		const bool compilerDiagnosticFilterChanged = currentTrackWarnings != newTrackWarnings || currentTrackNotes != newTrackNotes;
		const bool scrollbarVisibilityChanged = currentScrollbarVisibility != newScrollbarVisibility;
		auto applyAndPersistUiSettings = [&]() -> bool {
			std::string errorText;
			if (!setConfiguredCursorBehaviour(newCb, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredCompilerErrorMessagePlacement(newCemp, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredScrollbarVisibility(newScrollbarVisibility, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredTrackCompilerWarnings(newTrackWarnings, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredTrackCompilerNotes(newTrackNotes, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredUiIndentStyle(newUiIndentStyle, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredCursorPositionMarker(newCp, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareOriginalLeadingGutters(newFileCompareOriginalLeadingGutters, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareOriginalTrailingGutters(newFileCompareOriginalTrailingGutters, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareCompareLeadingGutters(newFileCompareCompareLeadingGutters, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareCompareTrailingGutters(newFileCompareCompareTrailingGutters, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareStartConfiguration(newFileCompareStartConfiguration, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			if (!setConfiguredFileCompareComparePanelReadOnly(newFileCompareComparePanelReadOnly, &errorText)) {
				setSetupDialogStatus(errorText, MRMenuBar::MarqueeKind::Warning);
				return false;
			}
			setConfiguredWindowManager(newWm, &errorText);
			setConfiguredMenulineMessages(newMm, &errorText);
			setConfiguredCyclicVirtualDesktops(newCv, &errorText);
			applyVirtualDesktopConfigurationChange(newVd);
			for (MREditWindow *window : allEditWindowsInZOrder())
				if (window != nullptr && window->getEditor() != nullptr) window->getEditor()->refreshConfiguredVisualSettings();
			for (MREditWindow *window : allEditWindowsInZOrder())
				if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window); bentoBox != nullptr && bentoBox->isFileCompareBox()) bentoBox->refreshFileCompareConfiguration();
			if (scrollbarVisibilityChanged)
				for (MREditWindow *window : allEditWindowsInZOrder())
					if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window); bentoBox != nullptr) bentoBox->changeBounds(bentoBox->getBounds());
			if (compilerDiagnosticFilterChanged)
				for (MREditWindow *window : allEditWindowsInZOrder())
					if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window); bentoBox != nullptr && bentoBox->buildOutputPane() != nullptr && bentoBox->problemsPane() != nullptr)
						static_cast<void>(bentoBox->refreshCompilerDiagnosticsFromOutput());
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

namespace {

struct LiveLogsDialogRecord {
	ushort messageLine;
	ushort systemBeep;
	ushort audioSignal;
	ushort scrollDirection;
	ushort lineNumbers;
	ushort timestamps;
	ushort syntaxHighlighting;
	char audioUri[kPathFieldSize];
};

bool liveLogsDialogRecordEqual(const LiveLogsDialogRecord &lhs, const LiveLogsDialogRecord &rhs) {
	return lhs.messageLine == rhs.messageLine && lhs.systemBeep == rhs.systemBeep && lhs.audioSignal == rhs.audioSignal && lhs.scrollDirection == rhs.scrollDirection && lhs.lineNumbers == rhs.lineNumbers && lhs.timestamps == rhs.timestamps &&
	       lhs.syntaxHighlighting == rhs.syntaxHighlighting &&
	       std::strcmp(lhs.audioUri, rhs.audioUri) == 0;
}

bool audioPlayerAvailable() {
	return !configuredAudioPlayerPath().empty();
}

class LiveLogsSetupDialog : public MRScrollableDialog {
  public:
	LiveLogsSetupDialog() : TWindowInit(initSetupDialogFrame), MRScrollableDialog(centeredSetupDialogRect(66, 15), "LIVE LOGS", 64, 13, initSetupDialogFrame), messageLineField(nullptr), audioField(nullptr), scrollDirectionField(nullptr), lineNumbersField(nullptr), audioUriField(nullptr), audioAvailable(audioPlayerAvailable()) {
		addManaged(new TStaticText(TRect(3, 2, 35, 3), "Search hits:"), TRect(3, 2, 35, 3));
		messageLineField = new TCheckBoxes(TRect(3, 3, 34, 5), new TSItem("report on message line", new TSItem("system beep", nullptr)));
		addManaged(messageLineField, TRect(3, 3, 34, 5));
		audioField = new TCheckBoxes(TRect(3, 5, 34, 6), new TSItem("audible signal via player", nullptr));
		addManaged(audioField, TRect(3, 5, 34, 6));
		if (!audioAvailable) audioField->setState(sfDisabled, True);

		addManaged(new TStaticText(TRect(36, 2, 54, 3), "Viewer:"), TRect(36, 2, 54, 3));
		lineNumbersField = new TCheckBoxes(TRect(36, 3, 58, 6), new TSItem("line numbers", new TSItem("time stamps", new TSItem("syntax highlighting", nullptr))));
		addManaged(lineNumbersField, TRect(36, 3, 58, 6));

		addManaged(new TStaticText(TRect(3, 8, 24, 9), "Scroll direction:"), TRect(3, 8, 24, 9));
		scrollDirectionField = new TRadioButtons(TRect(3, 9, 20, 11), new TSItem("down", new TSItem("up", nullptr)));
		addManaged(scrollDirectionField, TRect(3, 9, 20, 11));

		audioUriField = new TInputLine(TRect(36, 10, 62, 11), kPathFieldSize - 1);
		addManaged(new TLabel(TRect(36, 9, 54, 10), "~A~udio URI:", audioUriField), TRect(36, 9, 54, 10));
		addManaged(audioUriField, TRect(36, 10, 62, 11));
		if (!audioAvailable) audioUriField->setState(sfDisabled, True);

		selectContent();
	}

	void setData(void *rec) override {
		LiveLogsDialogRecord *record = static_cast<LiveLogsDialogRecord *>(rec);
		ushort hitFlags = 0;
		ushort viewerFlags = 0;

		if (record == nullptr) return;
		if (record->messageLine != 0) hitFlags |= 1;
		if (record->systemBeep != 0) hitFlags |= 2;
		if (audioAvailable && record->audioSignal != 0) hitFlags |= 4;
		if (record->lineNumbers != 0) viewerFlags |= 1;
		if (record->timestamps != 0) viewerFlags |= 2;
		if (record->syntaxHighlighting != 0) viewerFlags |= 4;
		if (messageLineField != nullptr) messageLineField->setData(&hitFlags);
		if (audioField != nullptr) {
			ushort audio = (hitFlags & 4) != 0 ? 1 : 0;
			audioField->setData(&audio);
		}
		if (scrollDirectionField != nullptr) scrollDirectionField->setData(&record->scrollDirection);
		if (lineNumbersField != nullptr) lineNumbersField->setData(&viewerFlags);
		if (audioUriField != nullptr) audioUriField->setData(record->audioUri);
	}

	void getData(void *rec) override {
		LiveLogsDialogRecord *record = static_cast<LiveLogsDialogRecord *>(rec);
		ushort hitFlags = 0;
		ushort viewerFlags = 0;

		if (record == nullptr) return;
		if (messageLineField != nullptr) messageLineField->getData(&hitFlags);
		if (audioField != nullptr) {
			ushort audio = 0;
			audioField->getData(&audio);
			if (audio != 0) hitFlags |= 4;
		}
		if (lineNumbersField != nullptr) lineNumbersField->getData(&viewerFlags);
		record->messageLine = (hitFlags & 1) != 0 ? 1 : 0;
		record->systemBeep = (hitFlags & 2) != 0 ? 1 : 0;
		record->audioSignal = (hitFlags & 4) != 0 ? 1 : 0;
		if (!audioAvailable) record->audioSignal = 0;
		if (scrollDirectionField != nullptr) scrollDirectionField->getData(&record->scrollDirection);
		record->lineNumbers = (viewerFlags & 1) != 0 ? 1 : 0;
		record->timestamps = (viewerFlags & 2) != 0 ? 1 : 0;
		record->syntaxHighlighting = (viewerFlags & 4) != 0 ? 1 : 0;
		if (audioUriField != nullptr) audioUriField->getData(record->audioUri);
	}

  private:
	TCheckBoxes *messageLineField;
	TCheckBoxes *audioField;
	TRadioButtons *scrollDirectionField;
	TCheckBoxes *lineNumbersField;
	TInputLine *audioUriField;
	bool audioAvailable;
};

} // namespace

void runLiveLogsSetupDialogFlow() {
	bool running = true;

	while (running) {
		MRLiveLogSettings settings = configuredLiveLogSettings();
		LiveLogsDialogRecord record{};
		std::string errorText;

		record.messageLine = settings.reportSearchHitsOnMessageLine ? 1 : 0;
		record.systemBeep = settings.reportSearchHitsWithSystemBeep ? 1 : 0;
		record.audioSignal = settings.reportSearchHitsWithAudioSignal ? 1 : 0;
		record.scrollDirection = settings.scrollDirection == MRLiveLogScrollDirection::Up ? 1 : 0;
		record.lineNumbers = settings.showLineNumbers ? 1 : 0;
		record.timestamps = settings.showTimestamps ? 1 : 0;
		record.syntaxHighlighting = settings.syntaxHighlighting ? 1 : 0;
		writeRecordField(record.audioUri, sizeof(record.audioUri), settings.audioSignalUri);

		LiveLogsDialogRecord baselineRecord = record;
		ushort result = execDialogWithDataCapture(new LiveLogsSetupDialog(), &record);
		const bool changed = mr::dialogs::isDialogDraftDirty(baselineRecord, record, liveLogsDialogRecordEqual);
		auto applyAndPersistLiveLogSettings = [&]() -> bool {
			settings.reportSearchHitsOnMessageLine = record.messageLine != 0;
			settings.reportSearchHitsWithSystemBeep = record.systemBeep != 0;
			settings.reportSearchHitsWithAudioSignal = record.audioSignal != 0;
			settings.scrollDirection = record.scrollDirection == 1 ? MRLiveLogScrollDirection::Up : MRLiveLogScrollDirection::Down;
			settings.showLineNumbers = record.lineNumbers != 0;
			settings.showTimestamps = record.timestamps != 0;
			settings.syntaxHighlighting = record.syntaxHighlighting != 0;
			settings.audioSignalUri = normalizeConfiguredPathInput(record.audioUri);
			if (!setConfiguredLiveLogSettings(settings, &errorText)) {
				postSetupFlowError("Live logs", errorText);
				return false;
			}
			for (MREditWindow *window : allEditWindowsInZOrder()) {
				if (window == nullptr || !window->isCommunicationWindow()) continue;
				window->setLogViewerOptions(settings.showLineNumbers, settings.scrollDirection);
				window->drawView();
			}
			if (!persistConfiguredSettingsSnapshot(&errorText)) {
				postSetupFlowError("Live logs", errorText);
				return false;
			}
			return true;
		};

		switch (result) {
			case cmOK:
				if (changed && !applyAndPersistLiveLogSettings()) break;
				running = false;
				break;
			case cmClose:
			case cmCancel:
				if (!changed) {
					running = false;
					break;
				}
				switch (mr::dialogs::runDialogDirtyGating("Live log settings have unsaved changes.")) {
					case mr::dialogs::UnsavedChangesChoice::Save:
						if (!applyAndPersistLiveLogSettings()) break;
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

bool mrSaveColorThemeFromWorkingPaletteForTesting(const TPalette &workingPalette, const std::string &themeUri, std::string *errorMessage) {
	std::string errorText;
	std::string activeThemeDisplayName = configuredColorThemeDisplayName();
	MRSetupPaths paths = resolveSetupPathDefaults();

	if (!applyWorkingColorPaletteToConfigured(workingPalette, errorText)) {
		if (errorMessage != nullptr) *errorMessage = errorText;
		return false;
	}
	if (!setConfiguredColorThemeDisplayName(activeThemeDisplayName, &errorText)) {
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
