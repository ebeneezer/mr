#define Uses_TColorGroup
#define Uses_TColorGroupList
#define Uses_TColorItem
#define Uses_TColorItemList
#define Uses_TColorDisplay
#define Uses_TColorSelector
#define Uses_TMonoSelector
#include <tvision/tv.h>

#include "MRSetup.hpp"
#include "MRSetupSections.hpp"

#include "../../app/MRCommands.hpp"
#include "../../ui/MRPalette.hpp"
#include "../MRKeymapManager.hpp"
#include "MRSetupCommon.hpp"

#include <chrono>
#include <string>

namespace {
} // namespace

bool runSetupDialogCommand(unsigned short command) {
	switch (command) {
		case cmMrSetupEditSettings:
		case cmMrSetupFilenameExtensions:
			runFileExtensionProfilesDialogFlow();
			return true;

		case cmMrSetupCompilerProfiles:
			runCompilerProfilesDialogFlow();
			return true;

		case cmMrSetupColorSetup:
			runColorSetupDialogFlow();
			return true;

		case cmMrSetupKeyMapping:
			runKeymapManagerDialogFlow();
			return true;

		case cmMrSetupPaths:
			runPathsSetupDialogFlow();
			return true;

		case cmMrSetupBackupsAutosave:
			runBackupsAutosaveDialogFlow();
			return true;

		case cmMrSetupMouseKeyRepeat:
		case cmMrSetupSearchAndReplaceDefaults:
		case cmMrSetupUserInterfaceSettings:
			runUserInterfaceSettingsDialogFlow();
			return true;

		case cmMrSetupLiveLogs:
			runLiveLogsSetupDialogFlow();
			return true;

		default:
			return false;
	}
}
