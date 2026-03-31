#define Uses_TKeys
#define Uses_MsgBox
#define Uses_TDialog
#define Uses_TStaticText
#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#include <tvision/tv.h>

#include "TMREditorApp.hpp"

#include "../coprocessor/MRCoprocessor.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/mrvm.hpp"
#include "../services/MRCoprocessorDispatch.hpp"
#include "../services/MRDialogPaths.hpp"
#include "../services/MRPerformance.hpp"
#include "../services/MRWindowCommands.hpp"
#include "../ui/TMRDeskTop.hpp"
#include "../ui/TMREditWindow.hpp"
#include "../ui/TMRMenuBar.hpp"
#include "../ui/TMRStatusLine.hpp"
#include "../ui/MRPalette.hpp"
#include "../ui/MRWindowSupport.hpp"
#include "MRAppState.hpp"
#include "MRCommandRouter.hpp"
#include "MRCommands.hpp"
#include "MRMenuFactory.hpp"

#include <ctime>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace {
static constexpr std::chrono::milliseconds kRecordingBlinkInterval(450);

class TMacroBindCaptureDialog : public TDialog {
  public:
	TMacroBindCaptureDialog()
	    : TWindowInit(&TDialog::initFrame), TDialog(TRect(0, 0, 52, 8), "Bind Recorded Macro Key"),
	      hasCaptured_(false), keyCode_(kbNoKey), controlState_(0) {
		options |= ofCentered;
		insert(new TStaticText(TRect(2, 2, 50, 6),
		                       "Press key to bind the recorded macro.\nEsc = no binding."));
	}

	virtual void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown) {
			TKey pressed(event.keyDown);
			if (pressed == TKey(kbEsc)) {
				endModal(cmCancel);
				clearEvent(event);
				return;
			}
			if (pressed == TKey(kbAltF10)) {
				clearEvent(event);
				return;
			}
			hasCaptured_ = true;
			keyCode_ = event.keyDown.keyCode;
			controlState_ = event.keyDown.controlKeyState;
			endModal(cmOK);
			clearEvent(event);
			return;
		}
		TDialog::handleEvent(event);
	}

	bool hasCaptured() const noexcept {
		return hasCaptured_;
	}

	ushort keyCode() const noexcept {
		return keyCode_;
	}

	ushort controlState() const noexcept {
		return controlState_;
	}

  private:
	bool hasCaptured_;
	ushort keyCode_;
	ushort controlState_;
};

std::string trimAscii(const std::string &value) {
	std::size_t start = 0;
	std::size_t end = value.size();

	while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		--end;
	return value.substr(start, end - start);
}

std::string expandUserPath(const std::string &input) {
	std::string path = trimAscii(input);
	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0')
			return std::string(home) + path.substr(1);
	}
	return path;
}

std::string ensureMrmacExtension(const std::string &path) {
	std::size_t dotPos = path.rfind('.');
	if (dotPos != std::string::npos) {
		std::string ext = path.substr(dotPos);
		for (std::size_t i = 0; i < ext.size(); ++i)
			ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
		if (ext == ".mrmac")
			return path;
	}
	return path + ".mrmac";
}

std::string makeRecordedMacroName(unsigned long counter) {
	char timePart[32];
	std::time_t now = std::time(nullptr);
	std::tm tmNow;

#if defined(__unix__)
	localtime_r(&now, &tmNow);
#else
	tmNow = *std::localtime(&now);
#endif
	std::strftime(timePart, sizeof(timePart), "Recorded_%Y%m%d_%H%M%S", &tmNow);
	return std::string(timePart) + "_" + std::to_string(counter);
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
	if (std::isprint(ch) == 0)
		return;
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
	static const ComboSpec combos[] = {{"", 0},
	                                   {"Shft", kbShift},
	                                   {"Ctrl", kbCtrlShift},
	                                   {"Alt", kbAltShift},
	                                   {"CtrlShft", static_cast<ushort>(kbCtrlShift | kbShift)},
	                                   {"AltShft", static_cast<ushort>(kbAltShift | kbShift)},
	                                   {"CtrlAlt", static_cast<ushort>(kbCtrlShift | kbAltShift)},
	                                   {"CtrlAltShft",
	                                    static_cast<ushort>(kbCtrlShift | kbAltShift | kbShift)}};
	static const NamedKeySpec named[] = {{"Enter", kbEnter},
	                                     {"Tab", kbTab},
	                                     {"Esc", kbEsc},
	                                     {"Backspace", kbBack},
	                                     {"Up", kbUp},
	                                     {"Down", kbDown},
	                                     {"Left", kbLeft},
	                                     {"Right", kbRight},
	                                     {"PgUp", kbPgUp},
	                                     {"PgDn", kbPgDn},
	                                     {"Home", kbHome},
	                                     {"End", kbEnd},
	                                     {"Ins", kbIns},
	                                     {"Del", kbDel},
	                                     {"Grey-", kbGrayMinus},
	                                     {"Grey+", kbGrayPlus},
	                                     {"Grey*", static_cast<ushort>('*')},
	                                     {"Space", static_cast<ushort>(' ')},
	                                     {"Minus", static_cast<ushort>('-')},
	                                     {"Equal", static_cast<ushort>('=')},
	                                     {"F1", kbF1},
	                                     {"F2", kbF2},
	                                     {"F3", kbF3},
	                                     {"F4", kbF4},
	                                     {"F5", kbF5},
	                                     {"F6", kbF6},
	                                     {"F7", kbF7},
	                                     {"F8", kbF8},
	                                     {"F9", kbF9},
	                                     {"F10", kbF10},
	                                     {"F11", kbF11},
	                                     {"F12", kbF12}};
	TKey pressed(keyCode, controlKeyState);

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

	if (keyCode != kbNoKey && keyCode < 256 && std::isprint(static_cast<unsigned char>(keyCode)) != 0) {
		outToken = "<";
		outToken.push_back(static_cast<char>(keyCode));
		outToken += ">";
		return true;
	}
	return false;
}

bool writeTextFile(const std::string &path, const std::string &content) {
	std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << content;
	return out.good();
}

bool validateMacroSource(const std::string &source, std::string &errorText) {
	size_t bytecodeSize = 0;
	unsigned char *bytecode = compile_macro_code(source.c_str(), &bytecodeSize);

	if (bytecode == nullptr) {
		const char *err = get_last_compile_error();
		errorText = (err != nullptr && *err != '\0') ? err : "Compilation failed.";
		return false;
	}
	std::free(bytecode);
	errorText.clear();
	return true;
}

std::string buildTopRightCursorStatus() {
	TMREditWindow *win = currentEditWindow();
	if (win == nullptr || win->getEditor() == nullptr)
		return std::string();
	if (isEmptyUntitledEditableWindow(win))
		return std::string();

	char buf[64];
	std::snprintf(buf, sizeof(buf), "%lu|%lu", win->cursorLineNumber(), win->cursorColumnNumber());
	return std::string(buf);
}

TMRMenuBar::HeroKind mapHeroKind(mr::performance::HeroKind kind) {
	switch (kind) {
		case mr::performance::HeroKind::Success:
			return TMRMenuBar::HeroKind::Success;
		case mr::performance::HeroKind::Warning:
			return TMRMenuBar::HeroKind::Warning;
		case mr::performance::HeroKind::Error:
			return TMRMenuBar::HeroKind::Error;
		case mr::performance::HeroKind::Info:
		default:
			return TMRMenuBar::HeroKind::Info;
	}
}
} // namespace

TMenuBar *TMREditorApp::initMRMenuBar(TRect r) {
	return createMRMenuBar(r);
}

TStatusLine *TMREditorApp::initMRStatusLine(TRect r) {
	r.a.y = r.b.y - 1;
	return new TMRStatusLine(r, *new TStatusDef(0, 0xFFFF) +
	                                *new TStatusItem("~F1~ Help", kbF1, cmMrHelpContents) +
	                                *new TStatusItem("~F10~ Menu", kbF10, cmMenu) +
	                                *new TStatusItem("~Alt-F10~ Rec", kbAltF10,
	                                                 cmMrMacroToggleRecording) +
	                                *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit));
}

TDeskTop *TMREditorApp::initMRDeskTop(TRect r) {
	r.a.y++;
	r.b.y--;
	return new TMRDeskTop(r);
}

TMREditorApp::TMREditorApp()
    : TProgInit(&TMREditorApp::initMRStatusLine, &TMREditorApp::initMRMenuBar,
                &TMREditorApp::initMRDeskTop),
      exitPrepared_(false), keystrokeRecording_(false), recordingMarkerVisible_(false),
      recordedKeySequence_(), recordedMacroCounter_(0), recordedSessionMacroFiles_(),
      recordingBlinkToggleAt_(std::chrono::steady_clock::now() + kRecordingBlinkInterval),
      indexedMacroWarmupActive_(false), indexedMacroWarmupLoadedFiles_(0) {
	mr::coprocessor::globalCoprocessor().setResultHandler(handleCoprocessorResult);
	bootstrapIndexedMacroBindings();
	createEditorWindow("?No-File?");
	mrEnsureLogWindow(false);
	syncRecordingUiState();
	mrLogMessage("Editor session started.");
	updateAppCommandState();
}

TMREditorApp::~TMREditorApp() {
	prepareForQuit();
	mr::coprocessor::globalCoprocessor().shutdown(true);
}

void TMREditorApp::prepareForQuit() {
	if (exitPrepared_)
		return;

	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	std::size_t pendingTaskCount = 0;

	exitPrepared_ = true;
	for (std::size_t i = 0; i < windows.size(); ++i)
		if (windows[i] != nullptr)
			pendingTaskCount += windows[i]->prepareCoprocessorTasksForShutdown();

	if (pendingTaskCount != 0) {
		std::string line = "Exit requested; cancelling ";
		line += std::to_string(pendingTaskCount);
		line += " running or pending coprocessor task";
		if (pendingTaskCount != 1)
			line += "s";
		line += ".";
		mrLogMessage(line.c_str());
		mr::coprocessor::globalCoprocessor().pump(64);
	}
}

bool TMREditorApp::isRecorderToggleKey(const TEvent &event) const {
	return event.what == evKeyDown && TKey(event.keyDown) == TKey(kbAltF10);
}

bool TMREditorApp::isRecorderToggleCommand(const TEvent &event) const {
	return event.what == evCommand && event.message.command == cmMrMacroToggleRecording;
}

void TMREditorApp::redrawRecordingMarkerFrames() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	for (std::size_t i = 0; i < windows.size(); ++i) {
		if (windows[i] != nullptr && windows[i]->frame != nullptr)
			windows[i]->frame->drawView();
	}
}

void TMREditorApp::syncRecordingUiState() {
	mrSetKeystrokeRecordingActive(keystrokeRecording_);
	mrSetKeystrokeRecordingMarkerVisible(keystrokeRecording_ && recordingMarkerVisible_);
	if (auto *mrStatusLine = dynamic_cast<TMRStatusLine *>(statusLine))
		mrStatusLine->setRecordingState(keystrokeRecording_, recordingMarkerVisible_);
	redrawRecordingMarkerFrames();
}

void TMREditorApp::updateRecordingBlink() {
	std::chrono::steady_clock::time_point now;
	if (!keystrokeRecording_)
		return;

	now = std::chrono::steady_clock::now();
	if (now < recordingBlinkToggleAt_)
		return;

	recordingMarkerVisible_ = !recordingMarkerVisible_;
	recordingBlinkToggleAt_ = now + kRecordingBlinkInterval;
	mrSetKeystrokeRecordingMarkerVisible(recordingMarkerVisible_);
	if (auto *mrStatusLine = dynamic_cast<TMRStatusLine *>(statusLine))
		mrStatusLine->setRecordingState(keystrokeRecording_, recordingMarkerVisible_);
	redrawRecordingMarkerFrames();
}

void TMREditorApp::startKeystrokeRecording() {
	keystrokeRecording_ = true;
	recordingMarkerVisible_ = true;
	recordingBlinkToggleAt_ = std::chrono::steady_clock::now() + kRecordingBlinkInterval;
	recordedKeySequence_.clear();
	syncRecordingUiState();
	mrLogMessage("Keystroke recording started (Alt-F10 to stop).");
}

void TMREditorApp::appendRecordedKeyEvent(const TEvent &event) {
	std::string keyToken;
	ushort state;

	if (event.what != evKeyDown)
		return;
	state = event.keyDown.controlKeyState;

	if ((state & kbPaste) != 0 && event.keyDown.textLength > 0) {
		for (uchar i = 0; i < event.keyDown.textLength; ++i)
			appendEscapedKeyInChar(recordedKeySequence_, static_cast<unsigned char>(event.keyDown.text[i]));
		return;
	}
	if (event.keyDown.textLength > 0) {
		for (uchar i = 0; i < event.keyDown.textLength; ++i)
			appendEscapedKeyInChar(recordedKeySequence_, static_cast<unsigned char>(event.keyDown.text[i]));
		return;
	}
	if (keyInTokenFromEvent(event.keyDown.keyCode, state, keyToken))
		recordedKeySequence_ += keyToken;
}

bool TMREditorApp::captureBindingKeySpec(std::string &keySpec) {
	TMacroBindCaptureDialog *dialog = nullptr;
	ushort modalResult;
	bool captured = false;
	ushort keyCode = kbNoKey;
	ushort controlState = 0;

	keySpec.clear();
	dialog = new TMacroBindCaptureDialog();
	if (dialog == nullptr)
		return false;
	modalResult = deskTop != nullptr ? deskTop->execView(dialog) : cmCancel;
	captured = dialog->hasCaptured();
	keyCode = dialog->keyCode();
	controlState = dialog->controlState();
	TObject::destroy(dialog);

	if (modalResult == cmCancel || !captured)
		return true;
	if (!keyInTokenFromEvent(keyCode, controlState, keySpec)) {
		messageBox(mfError | mfOKButton,
		           "Unsupported binding key.\nUse a function key or a Ctrl/Alt/Shift combination.");
		return false;
	}
	return true;
}

void TMREditorApp::finalizeKeystrokeRecording() {
	enum { SavePathBufferSize = 512 };
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

	if (recordedKeySequence_.empty()) {
		messageBox(mfInformation | mfOKButton,
		           "Keystroke recording is empty.\n\nNothing to bind or save.");
		return;
	}

	if (!captureBindingKeySpec(keySpec))
		return;

	std::memset(savePathBuffer, 0, sizeof(savePathBuffer));
	if (inputBox("KEYSTROKE RECORDER", "~S~ave .mrmac (leer=nur Session-Bindung)",
	             savePathBuffer, static_cast<uchar>(sizeof(savePathBuffer) - 1)) != cmCancel)
		savePath = trimAscii(savePathBuffer);
	if (!savePath.empty())
		savePath = ensureMrmacExtension(expandUserPath(savePath));

	macroName = makeRecordedMacroName(++recordedMacroCounter_);
	source << "$MACRO " << macroName;
	if (!keySpec.empty())
		source << " TO " << keySpec << " FROM EDIT";
	else
		source << " FROM EDIT";
	source << ";\n";
	source << "KEY_IN('" << recordedKeySequence_ << "');\n";
	source << "END_MACRO;\n";
	macroSource = source.str();

	if (!validateMacroSource(macroSource, validationError)) {
		messageBox(mfError | mfOKButton, "Recorded macro is invalid:\n\n%s", validationError.c_str());
		return;
	}

	if (!savePath.empty()) {
		if (!writeTextFile(savePath, macroSource)) {
			messageBox(mfError | mfOKButton, "Could not save recorded macro:\n%s", savePath.c_str());
			return;
		}
		std::string line = "Saved recorded macro to ";
		line += savePath;
		mrLogMessage(line.c_str());
	}

	if (!keySpec.empty()) {
		if (!savePath.empty())
			sessionPath = savePath;
		else {
			sessionPath = "/tmp/mr_recorded_" + std::to_string(static_cast<long>(::getpid())) + "_" +
			              std::to_string(recordedMacroCounter_) + ".mrmac";
			if (!writeTextFile(sessionPath, macroSource)) {
				messageBox(mfError | mfOKButton, "Could not create session macro file.");
				return;
			}
			recordedSessionMacroFiles_.push_back(sessionPath);
		}

		if (!mrvmLoadMacroFile(sessionPath, &loadError)) {
			messageBox(mfError | mfOKButton, "Could not bind recorded macro:\n\n%s", loadError.c_str());
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
	if (!keySpec.empty())
		summary += "\nBound key: " + keySpec;
	if (!savePath.empty())
		summary += "\nSaved: " + savePath;
	messageBox(mfInformation | mfOKButton, "%s", summary.c_str());
}

void TMREditorApp::stopKeystrokeRecording() {
	keystrokeRecording_ = false;
	recordingMarkerVisible_ = false;
	syncRecordingUiState();
	mrLogMessage("Keystroke recording stopped.");
	finalizeKeystrokeRecording();
	recordedKeySequence_.clear();
}

void TMREditorApp::bootstrapIndexedMacroBindings() {
	std::size_t fileCount = 0;
	std::size_t bindingCount = 0;
	std::string directory = defaultMacroDirectoryPath();

	mrvmBootstrapBoundMacroIndex(directory, &fileCount, &bindingCount);
	indexedMacroWarmupLoadedFiles_ = 0;
	indexedMacroWarmupActive_ = (fileCount != 0);

	if (fileCount == 0) {
		mrLogMessage("Macro bootstrap: no bound .mrmac files found.");
		return;
	}

	{
		std::string line = "Macro bootstrap indexed ";
		line += std::to_string(bindingCount);
		line += " binding";
		if (bindingCount != 1)
			line += "s";
		line += " across ";
		line += std::to_string(fileCount);
		line += " file";
		if (fileCount != 1)
			line += "s";
		line += " in ";
		line += directory;
		line += ".";
		mrLogMessage(line.c_str());
	}
}

void TMREditorApp::warmIndexedMacroBindings() {
	std::string loadedPath;
	std::string failedPath;
	std::string errorText;

	if (!indexedMacroWarmupActive_)
		return;

	if (mrvmWarmLoadNextIndexedMacroFile(&loadedPath, &failedPath, &errorText)) {
		++indexedMacroWarmupLoadedFiles_;
		return;
	}

	if (!failedPath.empty()) {
		std::string line = "Macro warmup failed for ";
		line += failedPath;
		line += ": ";
		line += errorText.empty() ? std::string("unknown error") : errorText;
		mrLogMessage(line.c_str());
	}

	if (!mrvmHasPendingIndexedMacroWarmup()) {
		std::string line = "Macro warmup completed: ";
		line += std::to_string(indexedMacroWarmupLoadedFiles_);
		line += " file";
		if (indexedMacroWarmupLoadedFiles_ != 1)
			line += "s";
		line += " loaded.";
		mrLogMessage(line.c_str());
		indexedMacroWarmupActive_ = false;
	}
}

void TMREditorApp::handleEvent(TEvent &event) {
	if (isRecorderToggleCommand(event)) {
		if (keystrokeRecording_)
			stopKeystrokeRecording();
		else
			startKeystrokeRecording();
		clearEvent(event);
		return;
	}
	if (isRecorderToggleKey(event)) {
		if (keystrokeRecording_)
			stopKeystrokeRecording();
		else
			startKeystrokeRecording();
		clearEvent(event);
		return;
	}
	if (keystrokeRecording_ && event.what == evKeyDown)
		appendRecordedKeyEvent(event);

	if (event.what == evCommand && event.message.command == cmQuit)
		prepareForQuit();
	TApplication::handleEvent(event);

	if (event.what != evCommand)
		return;
	if (handleMRCommand(event.message.command))
		clearEvent(event);
}

void TMREditorApp::idle() {
	TApplication::idle();
	updateRecordingBlink();
	warmIndexedMacroBindings();
	mr::coprocessor::globalCoprocessor().pump(8);
	if (auto *mrMenuBar = dynamic_cast<TMRMenuBar *>(menuBar)) {
		mr::performance::HeroNotice hero;
		mrMenuBar->setRightStatus(buildTopRightCursorStatus());
		if (mr::performance::currentHeroNotice(hero))
			mrMenuBar->setHeroStatus(hero.text, mapHeroKind(hero.kind));
		else
			mrMenuBar->setHeroStatus(std::string());
	}
	updateAppCommandState();
}

TPalette &TMREditorApp::getPalette() const {
	static TPalette palette(cpAppColor, sizeof(cpAppColor) - 1);
	// Force readable gray-dialog contrast: black on gray for frame/text/labels.
	palette[32] = static_cast<char>(0x70); // Dialog frame passive
	palette[33] = static_cast<char>(0x70); // Dialog frame active
	palette[34] = static_cast<char>(0x70); // Dialog frame icon/title accents
	palette[37] = static_cast<char>(0x70); // StaticText
	palette[38] = static_cast<char>(0x70); // Label normal
	palette[39] = static_cast<char>(0x70); // Label selected
	palette[40] = static_cast<char>(0x70); // Label shortcut
	palette[1] = currentPalette.desktop;
	return palette;
}
