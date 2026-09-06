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

bool applyColorSetupSettingsToConfigured(const MRColorSetupSettings &settings, std::string &errorText) {
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Window, settings.windowColors.data(), settings.windowColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MenuDialog, settings.menuDialogColors.data(), settings.menuDialogColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Help, settings.helpColors.data(), settings.helpColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Other, settings.otherColors.data(), settings.otherColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::MiniMap, settings.miniMapColors.data(), settings.miniMapColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::FileCompareMiniMap, settings.fileCompareMiniMapColors.data(), settings.fileCompareMiniMapColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Code, settings.codeColors.data(), settings.codeColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::FileCompare, settings.fileCompareColors.data(), settings.fileCompareColors.size(), &errorText)) return false;
	if (!setConfiguredColorSetupGroupValues(MRColorSetupGroup::Debugger, settings.debuggerColors.data(), settings.debuggerColors.size(), &errorText)) return false;
	errorText.clear();
	return true;
}


} // namespace

void runColorSetupDialogFlow() {
	auto colorSettingsEqual = [](const MRColorSetupSettings &lhs, const MRColorSetupSettings &rhs) { return lhs == rhs; };
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
	auto applyAndPersistColors = [&](const MRColorSetupSettings &settings, std::string &errorText) -> bool {
		if (!applyColorSetupSettingsToConfigured(settings, errorText)) return false;
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
	MRColorSetupSettings pendingSettings = configuredColorSetupSettings();
	bool havePendingSettings = false;

	while (running) {
		MRColorSetupSettings baselineSettings = configuredColorSetupSettings();
		MRColorSetupSettings workingSettings = havePendingSettings ? pendingSettings : baselineSettings;
		ushort result = execDialogWithDataCapture(createColorSetupDialog(), &workingSettings);
		const bool changed = mr::dialogs::isDialogDraftDirty(baselineSettings, workingSettings, colorSettingsEqual);

		switch (result) {
			case cmOK:
				if (changed) {
					if (!applyAndPersistColors(workingSettings, errorText)) {
						postSetupFlowError("Installation / Color setup", errorText);
						break;
					}
				}
				havePendingSettings = false;
				running = false;
				break;

			case cmMrColorLoadTheme: {
				std::string themeUri;
				std::string loadWarning;

				if (!chooseThemeFileForLoad(MRDialogHistoryScope::SetupThemeLoad, themeUri)) break;
				if (!loadColorThemeFile(themeUri, &loadWarning)) {
					forgetLoadDialogPath(MRDialogHistoryScope::SetupThemeLoad, themeUri.c_str());
					postSetupFlowError("Color Setup / Load Theme", loadWarning);
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
				if (!loadWarning.empty()) setSetupDialogStatus(loadWarning, MRMenuBar::MarqueeKind::Warning);
				havePendingSettings = false;
				break;
			}

			case cmMrColorSaveTheme: {
				std::string themeUri;
				std::string activeThemeDisplayName = configuredColorThemeDisplayName();

				if (!chooseThemeFileForSave(MRDialogHistoryScope::SetupThemeSave, themeUri)) break;
				if (!confirmOverwriteForPath("Overwrite", "Theme file exists. Overwrite?", themeUri)) break;
				if (!applyColorSetupSettingsToConfigured(workingSettings, errorText)) {
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
				pendingSettings = workingSettings;
				havePendingSettings = true;
				break;
			}

			case cmClose:
			case cmCancel:
				if (!changed) {
					havePendingSettings = false;
					running = false;
					break;
				}
				switch (mr::dialogs::runDialogDirtyGating("Color settings have unsaved changes.")) {
					case mr::dialogs::UnsavedChangesChoice::Save:
						if (!applyAndPersistColors(workingSettings, errorText)) {
							postSetupFlowError("Color Setup / Save settings", errorText);
							break;
						}
						havePendingSettings = false;
						running = false;
						break;
					case mr::dialogs::UnsavedChangesChoice::Discard:
						havePendingSettings = false;
						running = false;
						break;
					case mr::dialogs::UnsavedChangesChoice::Cancel:
						pendingSettings = workingSettings;
						havePendingSettings = true;
						discardQueuedCancelEvent();
						break;
					default:
						break;
				}
				break;

			default:
				havePendingSettings = false;
				running = false;
				break;
		}
	}
}
