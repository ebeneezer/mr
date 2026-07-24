#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TRect
#define Uses_TButton
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_MsgBox
#include <tvision/tv.h>

#include "MRCommandRouterText.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <string_view>

#include "../../app/commands/MRWindowCommands.hpp"
#include "../../app/utils/MRStringUtils.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRFileEditor/MRFileEditor.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../MREditorApp.hpp"

namespace {
TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}

void postTextCommandError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

class NumericInputDialog final : public MRDialogFoundation {
  public:
	NumericInputDialog(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, const MRRouterIntegerInputLayout &layout) : TWindowInit(initMrDialogFrame), MRDialogFoundation(mr::dialogs::centeredDialogRect(layout.width, layout.height), title, layout.width, layout.height, initMrDialogFrame), mHelpText(helpText != nullptr ? helpText : ""), mMinValue(minValue), mMaxValue(maxValue) {
		char buffer[32] = {0};

		std::snprintf(buffer, sizeof(buffer), "%d", initialValue);
		mInputField = new TInputLine(TRect(layout.inputLeft, 2, layout.inputRight, 3), layout.inputRight - layout.inputLeft);
		if (label != nullptr && label[0] != '\0') insert(new TLabel(TRect(2, 2, layout.inputLeft, 3), label, mInputField));
		insert(mInputField);
		if (layout.showHelp) {
			const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}, mr::dialogs::DialogButtonSpec{"~H~elp", cmHelp, bfNormal}};
			mr::dialogs::insertUniformButtonRow(*this, layout.buttonLeft, layout.buttonY, layout.buttonGap, buttons);
		} else {
			const std::array buttons{mr::dialogs::DialogButtonSpec{"~D~one", cmOK, bfDefault}, mr::dialogs::DialogButtonSpec{"~C~ancel", cmCancel, bfNormal}};
			mr::dialogs::insertUniformButtonRow(*this, layout.buttonLeft, layout.buttonY, layout.buttonGap, buttons);
		}
		mInputField->setData(buffer);
		setDialogValidationHook([this]() {
			MRScrollableDialog::DialogValidationResult result;
			int ignored = 0;

			result.valid = tryReadValue(ignored);
			if (!result.valid) result.warningText = "Please enter an integer in range.";
			return result;
		});
		finalizeLayout();
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evCommand && event.message.command == cmHelp) {
			messageBox(mfInformation | mfOKButton, "%s", mHelpText.c_str());
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

	[[nodiscard]] bool tryReadValue(int &outValue) const {
		char buffer[32] = {0};
		char *endPtr = nullptr;
		long parsed = 0;

		if (mInputField == nullptr) return false;
		const_cast<TInputLine *>(mInputField)->getData(buffer);
		parsed = std::strtol(buffer, &endPtr, 10);
		if (endPtr == buffer || !trimAscii(endPtr != nullptr ? endPtr : "").empty()) return false;
		if (parsed < mMinValue || parsed > mMaxValue) return false;
		outValue = static_cast<int>(parsed);
		return true;
	}

  private:
	std::string mHelpText;
	TInputLine *mInputField = nullptr;
	int mMinValue = 0;
	int mMaxValue = 0;
};

bool persistVisibleEditSetupSettingsWithFeedback(const MREditSetupSettings &settings, const std::string &errorPrefix) {
	MRSettingsWriteReport writeReport;
	std::string errorText;

	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		postTextCommandError(errorPrefix + errorText);
		return false;
	}
	if (!persistConfiguredSettingsSnapshot(&errorText, &writeReport)) {
		postTextCommandError("Settings save failed: " + errorText);
		return false;
	}
	mrLogSettingsWriteReport("visible edit setup update", writeReport);
	for (MREditWindow *window : allEditWindowsInZOrder())
		if (window != nullptr && window->getEditor() != nullptr) window->getEditor()->refreshConfiguredVisualSettings();
	mrRefreshEditorApplicationUiSettingsSnapshot();
	return true;
}
} // namespace

MRRouterIntegerInputLayout defaultRouterIntegerInputLayout() {
	return MRRouterIntegerInputLayout{};
}

bool promptRouterIntegerValue(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, int &outValue) {
	return promptRouterIntegerValue(title, label, helpText, initialValue, minValue, maxValue, outValue, defaultRouterIntegerInputLayout());
}

bool promptRouterIntegerValue(const char *title, const char *label, const char *helpText, int initialValue, int minValue, int maxValue, int &outValue, const MRRouterIntegerInputLayout &layout) {
	NumericInputDialog *dialog = new NumericInputDialog(title, label, helpText, initialValue, minValue, maxValue, layout);
	bool accepted = false;
	ushort result = cmCancel;

	if (dialog == nullptr) return false;
	if (TProgram::deskTop == nullptr) {
		TObject::destroy(dialog);
		return false;
	}
	dialog->finalizeLayout();
	result = TProgram::deskTop->execView(dialog);
	if (result != cmCancel) accepted = dialog->tryReadValue(outValue);
	TObject::destroy(dialog);
	return accepted;
}

bool handleSetRightMargin() {
	MREditSetupSettings settings = configuredEditSetupSettings();
	MRRouterIntegerInputLayout layout = defaultRouterIntegerInputLayout();
	int minimumMargin = std::min(999, std::max(1, settings.leftMargin + 1));
	int margin = settings.rightMargin > 0 ? settings.rightMargin : 78;

	layout.width = 30;
	layout.height = 8;
	layout.inputLeft = 4;
	layout.inputRight = 26;
	layout.buttonY = 4;
	layout.buttonLeft = 4;
	layout.buttonGap = 2;
	layout.showHelp = false;
	if (margin < minimumMargin) margin = minimumMargin;
	if (!promptRouterIntegerValue("SET RIGHT MARGIN", "", "Set the global RIGHT_MARGIN used for editor formatting.", margin, minimumMargin, 999, margin, layout)) return true;
	settings.rightMargin = margin;
	settings.formatLine = synchronizeEditFormatLineMargins(settings.formatLine, settings.leftMargin, settings.rightMargin, settings.tabSize);
	if (!persistVisibleEditSetupSettingsWithFeedback(settings, "Right margin update failed: ")) return true;
	mrLogMessage(("Right margin set to " + std::to_string(margin) + ".").c_str());
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Right margin updated.", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleSetLeftMargin(MREditWindow *window) {
	MREditSetupSettings settings = configuredEditSetupSettings();
	int maximumMargin = std::max(1, settings.rightMargin - 1);
	int margin = settings.leftMargin > 0 ? settings.leftMargin : 1;

	static_cast<void>(window);
	if (!promptRouterIntegerValue("SET LEFT MARGIN", "~M~argin:", "Set the global LEFT_MARGIN used for editor formatting.", margin, 1, maximumMargin, margin)) return true;
	settings.leftMargin = margin;
	settings.formatLine = synchronizeEditFormatLineMargins(settings.formatLine, settings.leftMargin, settings.rightMargin, settings.tabSize);
	if (!persistVisibleEditSetupSettingsWithFeedback(settings, "Left margin update failed: ")) return true;
	mrLogMessage(("Left margin set to " + std::to_string(margin) + ".").c_str());
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Left margin updated.", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleToggleWordWrap() {
	MREditSetupSettings settings = configuredEditSetupSettings();

	settings.wordWrap = !settings.wordWrap;
	if (!persistVisibleEditSetupSettingsWithFeedback(settings, "Word wrap update failed: ")) return true;
	return true;
}

bool handleTogglePersistentBlocks() {
	MREditSetupSettings settings = configuredEditSetupSettings();

	settings.persistentBlocks = !settings.persistentBlocks;
	if (!persistVisibleEditSetupSettingsWithFeedback(settings, "Persistent blocks update failed: ")) return true;
	mrLogMessage(settings.persistentBlocks ? "Persistent blocks enabled." : "Persistent blocks disabled.");
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, settings.persistentBlocks ? "Persistent blocks: ON" : "Persistent blocks: OFF", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleToggleFormatRuler() {
	MREditSetupSettings settings = configuredEditSetupSettings();

	settings.formatRuler = !settings.formatRuler;
	if (!persistVisibleEditSetupSettingsWithFeedback(settings, "Format ruler update failed: ")) return true;
	mrLogMessage(settings.formatRuler ? "Format ruler enabled." : "Format ruler disabled.");
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, settings.formatRuler ? "Format ruler: ON" : "Format ruler: OFF", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	return true;
}

bool handleReformatParagraph(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const MREditSetupSettings settings = configuredEditSetupSettings();

	if (editor == nullptr || window == nullptr || window->isReadOnly()) return false;
	return editor->formatParagraph(settings.leftMargin, settings.rightMargin);
}

bool handleReformatDocument(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const MREditSetupSettings settings = configuredEditSetupSettings();

	if (editor == nullptr || window == nullptr || window->isReadOnly()) return false;
	return editor->formatDocument(settings.leftMargin, settings.rightMargin);
}

bool handleJustifyParagraph(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const MREditSetupSettings settings = configuredEditSetupSettings();

	if (editor == nullptr || window == nullptr || window->isReadOnly()) return false;
	return editor->justifyParagraph(settings.leftMargin, settings.rightMargin);
}

bool handleCenterLine(MREditWindow *window) {
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
	const MREditSetupSettings settings = configuredEditSetupSettings();

	if (editor == nullptr || window == nullptr || window->isReadOnly()) return false;
	return editor->centerCurrentLine(settings.leftMargin, settings.rightMargin);
}
