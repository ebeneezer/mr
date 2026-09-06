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
using mr::dialogs::writeRecordField;

enum { kPathFieldSize = 256 };

void clearSetupDialogStatus() {
	mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
}

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
		const std::array buttons{mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
		const mr::dialogs::DialogButtonRowMetrics metrics = mr::dialogs::measureUniformButtonRow(buttons, 0);

		helpCtx = hcDialogLiveLogs;
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
		mr::dialogs::addManagedUniformButtonRow(*this, (64 - metrics.rowWidth) / 2, 11, 0, buttons);

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
