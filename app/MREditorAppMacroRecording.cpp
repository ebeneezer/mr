#include "utils/MRFileIOUtils.hpp"
#include "utils/MRStringUtils.hpp"
#define Uses_TKeys
#define Uses_MsgBox
#define Uses_TDialog
#define Uses_TStaticText
#define Uses_TFileDialog
#define Uses_TButton
#define Uses_TObject
#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TDrawBuffer
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#define Uses_TScreen
#include <tvision/tv.h>

#include "MREditorApp.hpp"

#include "../mrmac/mrmac.h"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/MRMacroRunner.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "../config/settings/MRSettingsRuntimeState.hpp"
#include "../dialogs/MRDirtyGating.hpp"
#include "../dialogs/setup/MRSetupCommon.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../app/commands/MRFileCommands.hpp"
#include "../ui/MRDeskTop.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRHelpSystem.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRStatusLine.hpp"
#include "../ui/MRSidekickEditor.hpp"
#include "../ui/MRFrame.hpp"
#include "../ui/MRWindowLayout.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "MRCommandRouter.hpp"
#include "MRCommands.hpp"
#include "MRFunctionKeyBindings.hpp"
#include "MRMenuFactory.hpp"
#include "MRMacroDebuggerCommandRoute.hpp"
#include "MRHelpTopics.generated.hpp"

#include <ctime>
#include <chrono>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace {
TFrame *initMrDialogFrame(TRect bounds) {
	return new MRFrame(bounds);
}
class TMacroBindCaptureDialog : public MRDialogFoundation {
  public:
	TMacroBindCaptureDialog() : TWindowInit(initMrDialogFrame), MRDialogFoundation(centeredSetupDialogRect(52, 10), "BIND RECORDED MACRO KEY", 52, 10, initMrDialogFrame), captureAccepted(false), capturedKeyCode(kbNoKey), capturedControlState(0) {
		helpCtx = hcDialogMacroBindCapture;
		insert(new TStaticText(TRect(2, 2, 50, 6), "Press key to bind the recorded macro.\nEsc = no binding."));
		insert(new TButton(TRect(39, 7, 50, 9), "~H~elp", cmHelp, bfNormal));
	}

	virtual void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			TKey pressed(event.keyDown);
			if (pressed == TKey(kbF1)) {
				static_cast<void>(mrShowProjectHelp(hcDialogMacroBindCapture));
				clearEvent(event);
				return;
			}
			if (pressed == TKey(kbShiftF1)) {
				static_cast<void>(mrShowProjectHelp(hcDetailedIndex));
				clearEvent(event);
				return;
			}
			if (pressed == TKey(kbAltF1)) {
				static_cast<void>(mrShowPreviousProjectHelp());
				clearEvent(event);
				return;
			}
			if (pressed == TKey(kbEsc)) {
				endModal(cmCancel);
				clearEvent(event);
				return;
			}
			if (pressed == TKey(kbAltF10)) {
				clearEvent(event);
				return;
			}
			captureAccepted = true;
			capturedKeyCode = event.keyDown.keyCode;
			capturedControlState = event.keyDown.controlKeyState;
			endModal(cmOK);
			clearEvent(event);
			return;
		}
		MRDialogFoundation::handleEvent(event);
	}

	bool hasCaptured() const noexcept {
		return captureAccepted;
	}

	ushort keyCode() const noexcept {
		return capturedKeyCode;
	}

	ushort controlState() const noexcept {
		return capturedControlState;
	}

  private:
	bool captureAccepted;
	ushort capturedKeyCode;
	ushort capturedControlState;
};

std::string makeRecordedMacroName(unsigned long counter) {
	std::array<char, 32> timePart{};
	std::time_t now = std::time(nullptr);
	std::tm tmNow;

#if defined(__unix__)
	localtime_r(&now, &tmNow);
#else
	tmNow = *std::localtime(&now);
#endif
	std::strftime(timePart.data(), timePart.size(), "Recorded_%Y%m%d_%H%M%S", &tmNow);
	return std::string(timePart.data()) + "_" + std::to_string(counter);
}

void appendEscapedKeyInChar(std::string &out, unsigned char ch) {
	if (ch == '\'') {
		out += "''";
		return;
	}
	if (ch == '\r' || ch == '\n') {
		out += "<Enter>";
		return;
	}
	if (ch == '\t') {
		out += "<Tab>";
		return;
	}
	if (ch == '\b') {
		out += "<Backspace>";
		return;
	}
	if (ch == 27) {
		out += "<Esc>";
		return;
	}
	if (ch == 127) {
		out += "<Del>";
		return;
	}
	if (std::isprint(ch) == 0) return;
	out.push_back(static_cast<char>(ch));
}

bool keyInTokenFromEvent(ushort keyCode, ushort controlKeyState, std::string &outToken) {
	struct ComboSpec {
		const char *prefix;
		ushort mods;
	};
	struct NamedKeySpec {
		const char *token;
		ushort code;
	};
	static const ComboSpec combos[] = {{"", 0}, {"Shft", kbShift}, {"Ctrl", kbCtrlShift}, {"Alt", kbAltShift}, {"CtrlShft", static_cast<ushort>(kbCtrlShift | kbShift)}, {"AltShft", static_cast<ushort>(kbAltShift | kbShift)}, {"CtrlAlt", static_cast<ushort>(kbCtrlShift | kbAltShift)}, {"CtrlAltShft", static_cast<ushort>(kbCtrlShift | kbAltShift | kbShift)}, {"Super", kbSuperShift}, {"SuperShft", static_cast<ushort>(kbSuperShift | kbShift)}, {"SuperCtrl", static_cast<ushort>(kbSuperShift | kbCtrlShift)}, {"SuperAlt", static_cast<ushort>(kbSuperShift | kbAltShift)}, {"SuperCtrlShft", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbShift)}, {"SuperAltShft", static_cast<ushort>(kbSuperShift | kbAltShift | kbShift)}, {"SuperCtrlAlt", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbAltShift)}, {"SuperCtrlAltShft", static_cast<ushort>(kbSuperShift | kbCtrlShift | kbAltShift | kbShift)}};
	static const NamedKeySpec named[] = {{"Enter", kbEnter}, {"Tab", kbTab}, {"Esc", kbEsc}, {"Backspace", kbBack}, {"Up", kbUp}, {"Down", kbDown}, {"Left", kbLeft}, {"Right", kbRight}, {"PgUp", kbPgUp}, {"PgDn", kbPgDn}, {"Home", kbHome}, {"End", kbEnd}, {"Ins", kbIns}, {"Del", kbDel}, {"Grey-", kbGrayMinus}, {"Grey+", kbGrayPlus}, {"Grey*", static_cast<ushort>('*')}, {"Space", static_cast<ushort>(' ')}, {"Minus", static_cast<ushort>('-')}, {"Equal", static_cast<ushort>('=')}, {"F1", kbF1}, {"F2", kbF2}, {"F3", kbF3}, {"F4", kbF4}, {"F5", kbF5}, {"F6", kbF6}, {"F7", kbF7}, {"F8", kbF8}, {"F9", kbF9}, {"F10", kbF10}, {"F11", kbF11}, {"F12", kbF12}};
	TKey pressed(keyCode, controlKeyState);

	if (keyCode == kbNoKey && (controlKeyState & kbCtrlShift) != 0 && (controlKeyState & (kbAltShift | kbSuperShift | kbPaste)) == 0) {
		outToken = "<CtrlSpace>";
		mrLogMessage("KEYDBG record raw Ctrl-Space normalized to <CtrlSpace>");
		return true;
	}

	for (const ComboSpec &combo : combos)
		for (const NamedKeySpec &entry : named)
			if (pressed == TKey(entry.code, combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken += entry.token;
				outToken += ">";
				return true;
			}

	for (const ComboSpec &combo : combos) {
		for (char c = 'A'; c <= 'Z'; ++c)
			if (pressed == TKey(static_cast<ushort>(c), combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken.push_back(c);
				outToken += ">";
				return true;
			}
		for (char c = '0'; c <= '9'; ++c)
			if (pressed == TKey(static_cast<ushort>(c), combo.mods)) {
				outToken = "<";
				outToken += combo.prefix;
				outToken.push_back(c);
				outToken += ">";
				return true;
			}
	}

	if (keyCode >= 1 && keyCode <= 26 && (controlKeyState & (kbAltShift | kbPaste)) == 0) {
		outToken = "<Ctrl";
		outToken.push_back(static_cast<char>('A' + keyCode - 1));
		outToken += ">";
		return true;
	}

	if (keyCode != kbNoKey && keyCode < 256 && std::isprint(static_cast<unsigned char>(keyCode)) != 0) {
		outToken = "<";
		outToken.push_back(static_cast<char>(keyCode));
		outToken += ">";
		return true;
	}
	return false;
}

bool validateMacroSource(std::string_view source, std::string &errorText) {
	size_t bytecodeSize = 0;
	std::string sourceText(source);
	unsigned char *bytecode = compile_macro_code(sourceText.c_str(), &bytecodeSize);

	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		errorText = (err != nullptr && *err != '\0') ? err : "Compilation failed.";
		return false;
	}
	std::free(bytecode);
	errorText.clear();
	return true;
}
} // namespace
bool MREditorApp::isRecorderToggleKey(const TEvent &event) const {
	return event.what == evKeyDown && TKey(event.keyDown) == TKey(kbAltF10);
}

bool MREditorApp::isRecorderToggleCommand(const TEvent &event) const {
	return event.what == evCommand && event.message.command == cmMrMacroToggleRecording;
}

void MREditorApp::redrawActiveMarkerFrame() {
	MREditWindow *window = currentEditWindow();
	if (window == nullptr || window->frame == nullptr || (window->state & sfVisible) == 0) return;
	window->frame->drawView();
}

void MREditorApp::syncRecordingUiState() {
	mrSetKeystrokeRecordingActive(keystrokeRecording);
	mrSetKeystrokeRecordingMarkerVisible(keystrokeRecording && recordingMarkerVisible);
	if (auto *mrStatusLine = dynamic_cast<MRStatusLine *>(statusLine)) mrStatusLine->setRecordingState(keystrokeRecording, recordingMarkerVisible);
	redrawActiveMarkerFrame();
}

void MREditorApp::updateRecordingBlink() {
	std::chrono::steady_clock::time_point now;
	if (!keystrokeRecording) return;

	now = std::chrono::steady_clock::now();
	if (now < recordingBlinkToggleAt) return;

	recordingMarkerVisible = !recordingMarkerVisible;
	recordingBlinkToggleAt = now + recordingBlinkInterval;
	mrSetKeystrokeRecordingMarkerVisible(recordingMarkerVisible);
	if (auto *mrStatusLine = dynamic_cast<MRStatusLine *>(statusLine)) mrStatusLine->setRecordingState(keystrokeRecording, recordingMarkerVisible);
	redrawActiveMarkerFrame();
}

void MREditorApp::updateMacroBrainBlink() {
	std::chrono::steady_clock::time_point now;
	if (!mrIsMacroBrainMarkerActive()) return;

	now = std::chrono::steady_clock::now();
	if (now < macroBrainBlinkToggleAt) return;

	macroBrainMarkerVisible = !macroBrainMarkerVisible;
	macroBrainBlinkToggleAt = now + recordingBlinkInterval;
	mrSetMacroBrainMarkerVisible(macroBrainMarkerVisible);
	redrawActiveMarkerFrame();
}

void MREditorApp::startKeystrokeRecording() {
	keystrokeRecording = true;
	recordingMarkerVisible = true;
	recordingBlinkToggleAt = std::chrono::steady_clock::now() + recordingBlinkInterval;
	recordedKeySequence.clear();
	syncRecordingUiState();
	mr::messageline::postSticky(mr::messageline::Owner::MacroMessage, "recordings started, ALT-F10 ends", mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
	mrLogMessage("Keystroke recording started (Alt-F10 to stop).");
}

void MREditorApp::appendRecordedKeyEvent(const TEvent &event) {
	std::string keyToken;
	ushort state;

	if (event.what != evKeyDown) return;
	state = event.keyDown.controlKeyState;

	if ((state & kbPaste) != 0 && event.keyDown.textLength > 0) {
		for (uchar i = 0; i < event.keyDown.textLength; ++i)
			appendEscapedKeyInChar(recordedKeySequence, static_cast<unsigned char>(event.keyDown.text[i]));
		return;
	}
	if (event.keyDown.textLength > 0) {
		for (uchar i = 0; i < event.keyDown.textLength; ++i)
			appendEscapedKeyInChar(recordedKeySequence, static_cast<unsigned char>(event.keyDown.text[i]));
		return;
	}
	if (keyInTokenFromEvent(event.keyDown.keyCode, state, keyToken)) recordedKeySequence += keyToken;
}

bool MREditorApp::captureBindingKeySpec(std::string &keySpec) {
	TMacroBindCaptureDialog *dialog = nullptr;
	ushort modalResult;
	bool captured = false;
	ushort keyCode = kbNoKey;
	ushort controlState = 0;

	keySpec.clear();
	dialog = new TMacroBindCaptureDialog();
	if (dialog == nullptr) return false;
	dialog->finalizeLayout();
	modalResult = deskTop != nullptr ? deskTop->execView(dialog) : cmCancel;
	captured = dialog->hasCaptured();
	keyCode = dialog->keyCode();
	controlState = dialog->controlState();
	TObject::destroy(dialog);

	if (modalResult == cmCancel || !captured) return true;
	if (!keyInTokenFromEvent(keyCode, controlState, keySpec)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Unsupported binding key. Use a function key or a Ctrl/Alt/Shift/Super combination.", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	return true;
}

void MREditorApp::finalizeKeystrokeRecording() {
	enum {
		SavePathBufferSize = 512
	};
	char savePathBuffer[SavePathBufferSize];
	std::string keySpec;
	std::string savePath;
	std::string macroName;
	std::ostringstream source;
	std::string macroSource;
	std::string validationError;
	std::string sessionPath;
	std::string loadError;
	std::string summary;

	if (recordedKeySequence.empty()) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Keystroke recording is empty. Nothing to bind or save.", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return;
	}

	if (!captureBindingKeySpec(keySpec)) return;

	std::memset(savePathBuffer, 0, sizeof(savePathBuffer));
	if (mr::dialogs::execTextInputDialog("KEYSTROKE RECORDER", "~S~ave .mrmac (empty=session binding only)", savePathBuffer, sizeof(savePathBuffer) - 1) != cmCancel) savePath = trimAscii(savePathBuffer);
	if (!savePath.empty()) savePath = mr::dialogs::ensureMrmacExtension(expandUserPath(savePath));

	macroName = makeRecordedMacroName(++recordedMacroCounter);
	source << "$MACRO " << macroName;
	if (!keySpec.empty()) source << " TO " << keySpec << " FROM EDIT";
	else
		source << " FROM EDIT";
	source << ";\n";
	source << "KEY_IN('" << recordedKeySequence << "');\n";
	source << "END_MACRO;\n";
	macroSource = source.str();

	if (!validateMacroSource(macroSource, validationError)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Recorded macro is invalid: " + validationError, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return;
	}

	if (!savePath.empty()) {
		std::error_code fileStatusError;

		if (std::filesystem::is_regular_file(savePath, fileStatusError) &&
		    mr::dialogs::showUnsavedChangesDialog("Overwrite", "Macro file exists. Overwrite?", savePath.c_str()) != mr::dialogs::UnsavedChangesChoice::Save)
			return;
		if (!writeTextFile(savePath, normalizeTextForSave(macroSource, effectiveTextSaveOptionsForPath(savePath)))) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Could not save recorded macro: " + savePath, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return;
		}
		std::string line = "Saved recorded macro to ";
		line += savePath;
		mrLogMessage(line.c_str());
	}

	if (!keySpec.empty()) {
		if (!savePath.empty()) sessionPath = savePath;
		else {
			sessionPath = configuredTempDirectoryPath() + "/mr_recorded_" + std::to_string(static_cast<long>(::getpid())) + "_" + std::to_string(recordedMacroCounter) + ".mrmac";
			if (!writeTextFile(sessionPath, normalizeTextForSave(macroSource, effectiveTextSaveOptionsForPath(sessionPath)))) {
				mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Could not create session macro file.", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
				return;
			}
			recordedSessionMacroFiles.push_back(sessionPath);
		}

		if (!mrvmLoadMacroFile(sessionPath, &loadError)) {
			mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Could not bind recorded macro: " + loadError, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return;
		}
		{
			std::string line = "Recorded macro bound to ";
			line += keySpec;
			line += " via ";
			line += sessionPath;
			mrLogMessage(line.c_str());
		}
	}

	summary = "Recording finalized.";
	if (!keySpec.empty()) summary += " Bound key: " + keySpec + ".";
	if (!savePath.empty()) summary += " Saved: " + savePath + ".";
	mr::messageline::postAutoTimed(mr::messageline::Owner::MacroMessage, summary, mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
}

void MREditorApp::stopKeystrokeRecording() {
	keystrokeRecording = false;
	recordingMarkerVisible = false;
	syncRecordingUiState();
	mr::messageline::clearOwner(mr::messageline::Owner::MacroMessage);
	mrLogMessage("Keystroke recording stopped.");
	finalizeKeystrokeRecording();
	recordedKeySequence.clear();
}
