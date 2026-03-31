#define Uses_TKeys
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
#include "../services/MRCoprocessorDispatch.hpp"
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

#include <cstdio>

namespace {
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
      exitPrepared_(false) {
	mr::coprocessor::globalCoprocessor().setResultHandler(handleCoprocessorResult);
	createEditorWindow("?No-File?");
	mrEnsureLogWindow(false);
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

void TMREditorApp::handleEvent(TEvent &event) {
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
	palette[1] = currentPalette.desktop;
	return palette;
}
