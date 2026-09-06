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
#include "../../app/MRHelpTopics.generated.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRBentoBox/MRBentoBox.hpp"
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

using mr::dialogs::discardQueuedCancelEvent;
using mr::dialogs::execDialogWithDataCapture;
using mr::dialogs::initSetupDialogFrame;
using mr::dialogs::postSetupFlowError;
using mr::dialogs::readRecordField;
using mr::dialogs::writeRecordField;

enum : ushort {
	cmMrSetupPathsBrowseSettingsUri = 3801,
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
	TColorAttr configured;

	if (configuredColorSlotOverride(paletteSlot, configured)) return TAttrPair(configured);
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
			TAttrPair color = getColor((state & sfFocused) != 0 ? 2 : 1);
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
		helpCtx = hcDialogPaths;
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
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
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
		TFileDialog *dialog = mr::dialogs::createFileDialog(MRDialogHistoryScope::General, "*.*", "SELECT AUDIO PLAYER URI", "~N~ame", fdOpenButton);
		dialog->helpCtx = hcDialogCompilerFile;
		result = execDialogWithDataCapture(dialog, fileName);
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

enum : ushort {
	cmMrSetupBackupsAutosaveBrowseDirectory = 3811,
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


} // namespace

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
