#define Uses_TApplication
#define Uses_TEvent
#define Uses_THelpFile
#define Uses_THelpViewer
#define Uses_TKeys
#define Uses_TObject
#define Uses_TProgram
#define Uses_TRect
#define Uses_TScrollBar
#define Uses_TWindow
#define Uses_fpstream
#include <tvision/tv.h>
#include <tvision/help.h>

#include "MRHelpSystem.hpp"

#include "../app/MREditorApp.hpp"
#include "../app/MRHelpTopics.generated.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"
#include "MRFrame.hpp"
#include "MRMessageLineController.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {
static constexpr std::uint32_t kHelpFileMagic = 0x46484246U;

[[nodiscard]] std::string helpFileName(std::string_view path) {
	const std::size_t separator = path.find_last_of("\\/");

	if (separator == std::string_view::npos) return std::string(path);
	return std::string(path.substr(separator + 1));
}

[[nodiscard]] std::string currentWorkingDirectory() {
	std::array<char, 1024> path{};

	if (::getcwd(path.data(), path.size()) == nullptr) return std::string();
	return std::string(path.data());
}

[[nodiscard]] std::string executableDirectory() {
	std::array<char, 4096> path{};
	const ssize_t length = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
	std::size_t separator;

	if (length <= 0) return std::string();
	path[static_cast<std::size_t>(length)] = '\0';
	separator = std::string_view(path.data()).find_last_of('/');
	if (separator == std::string_view::npos) return std::string();
	return std::string(path.data(), separator);
}

[[nodiscard]] std::string resolveHelpFilePath() {
	const std::string configured = configuredHelpFilePath();
	const std::string fromWorkingDirectory = currentWorkingDirectory();
	const std::string fromExecutable = executableDirectory();
	const std::string configuredName = helpFileName(configured);
	std::string candidate;

	if (!configured.empty() && ::access(configured.c_str(), R_OK) == 0) return configured;
	if (!fromWorkingDirectory.empty()) {
		candidate = fromWorkingDirectory + "/" + configuredName;
		if (::access(candidate.c_str(), R_OK) == 0) return candidate;
	}
	if (!fromExecutable.empty()) {
		candidate = fromExecutable + "/" + configuredName;
		if (::access(candidate.c_str(), R_OK) == 0) return candidate;
	}
	return configured;
}

[[nodiscard]] bool hasValidHelpHeader(const std::string &path) {
	std::ifstream input(path, std::ios::in | std::ios::binary);
	std::uint32_t magic = 0;

	if (!input.read(reinterpret_cast<char *>(&magic), sizeof(magic))) return false;
	return magic == kHelpFileMagic;
}

void postHelpError(const std::string &text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}
} // namespace

class MRHelpViewer final : public THelpViewer {
  public:
	MRHelpViewer(const TRect &bounds, TScrollBar *horizontalScrollBar, TScrollBar *verticalScrollBar, THelpFile *helpFile, int context, MRHelpSystem &helpSystem) noexcept
	    : THelpViewer(bounds, horizontalScrollBar, verticalScrollBar, helpFile, static_cast<ushort>(context)), helpSystem(helpSystem), context(context) {
	}

	~MRHelpViewer() override {
		helpSystem.releaseViewer(this);
	}

	ushort getHelpCtx() override {
		return static_cast<ushort>(context);
	}

	void navigateTo(int targetContext, bool recordHistory) {
		if (targetContext == context) return;
		if (recordHistory) helpSystem.recordTransition(context, targetContext);
		context = targetContext;
		switchToTopic(targetContext);
	}

	void handleEvent(TEvent &event) override {
		int targetContext = context;
		bool transitionPending = false;

		if (event.what == evKeyDown) {
			const TKey key(event.keyDown);

			if (key == TKey(kbBack) || key == TKey(kbAltF1)) {
				if (helpSystem.previousContext(targetContext)) {
					navigateTo(targetContext, false);
				}
				clearEvent(event);
				return;
			}
			if (key == TKey(kbEnter) && selected > 0 && selected <= topic->getNumCrossRefs()) {
				TPoint location;
				uchar length;

				topic->getCrossRef(selected - 1, location, length, targetContext);
				transitionPending = true;
			}
		} else if (event.what == evMouseDown) {
			const TPoint localMouse = makeLocal(event.mouse.where) + delta;
			TPoint location;
			uchar length;

			for (int index = 0; index < topic->getNumCrossRefs(); ++index) {
				topic->getCrossRef(index, location, length, targetContext);
				if (location.y == localMouse.y + 1 && localMouse.x >= location.x && localMouse.x < location.x + length) {
					transitionPending = true;
					break;
				}
			}
		}

		THelpViewer::handleEvent(event);
		if (transitionPending && targetContext != context) {
			helpSystem.recordTransition(context, targetContext);
			context = targetContext;
		}
	}

  private:
	MRHelpSystem &helpSystem;
	int context;
};

namespace {
class MRHelpWindow final : public TWindow {
  public:
	MRHelpWindow(const TRect &bounds, THelpFile *helpFile, int context, MRHelpSystem &helpSystem) noexcept
	    : TWindowInit(&MRHelpWindow::initFrame), TWindow(bounds, "MR HELP", wnNoNumber), viewer(nullptr) {
		TRect viewerBounds = getExtent();

		viewerBounds.grow(-2, -1);
		viewer = new MRHelpViewer(viewerBounds, standardScrollBar(sbHorizontal | sbHandleKeyboard), standardScrollBar(sbVertical | sbHandleKeyboard), helpFile, context, helpSystem);
		insert(viewer);
	}

	TPalette &getPalette() const override {
		static TPalette palette(cHelpWindow, sizeof(cHelpWindow) - 1);
		return palette;
	}

	MRHelpViewer *helpViewer() const noexcept {
		return viewer;
	}

  private:
	static TFrame *initFrame(TRect bounds) {
		return new MRFrame(bounds);
	}

	MRHelpViewer *viewer;
};
} // namespace

MRHelpSystem::MRHelpSystem() noexcept : history(), currentContext(hcContents), currentContextValid(false), helpOpen(false), activeViewer(nullptr) {
}

bool MRHelpSystem::showTopic(ushort context) {
	const int requestedContext = context == hcNoContext ? hcContents : context;
	const int priorContext = currentContext;
	const bool priorContextValid = currentContextValid;

	if (helpOpen) {
		if (activeViewer == nullptr) return false;
		activeViewer->navigateTo(requestedContext, true);
		return true;
	}
	if (priorContextValid && priorContext != requestedContext) history.push_back(priorContext);
	currentContext = requestedContext;
	currentContextValid = true;
	if (showTopicWithoutHistory(requestedContext)) return true;
	currentContext = priorContext;
	currentContextValid = priorContextValid;
	if (priorContextValid && priorContext != requestedContext) history.pop_back();
	return false;
}

bool MRHelpSystem::showPreviousTopic() {
	int context;
	const int priorContext = currentContext;
	const bool priorContextValid = currentContextValid;

	if (helpOpen) {
		if (activeViewer == nullptr) return false;
		if (!previousContext(context)) return showTopic(hcContents);
		activeViewer->navigateTo(context, false);
		return true;
	}
	if (!previousContext(context)) return showTopic(hcContents);
	if (showTopicWithoutHistory(context)) return true;
	history.push_back(context);
	currentContext = priorContext;
	currentContextValid = priorContextValid;
	return false;
}

bool MRHelpSystem::showTopicWithoutHistory(int context) {
	const std::string path = resolveHelpFilePath();
	fpstream *stream;
	THelpFile *helpFile;
	MRHelpWindow *window;
	TRect desktopBounds;
	int desktopWidth;
	int helpWidth;
	int helpLeft;

	if (TProgram::application == nullptr || TProgram::deskTop == nullptr || path.empty() || !hasValidHelpHeader(path)) {
		postHelpError("Unable to load TVision help file: " + path);
		return false;
	}
	stream = new fpstream(path.c_str(), ios::in | ios::binary);
	if (!stream->good()) {
		delete stream;
		postHelpError("Unable to open help file: " + path);
		return false;
	}
	helpFile = new THelpFile(*stream);
	desktopBounds = TProgram::deskTop->getBounds();
	desktopWidth = desktopBounds.b.x - desktopBounds.a.x;
	helpWidth = desktopWidth * 3 / 4;
	helpLeft = desktopBounds.a.x + (desktopWidth - helpWidth) / 2;
	window = new MRHelpWindow(TRect(helpLeft, desktopBounds.a.y + 2, helpLeft + helpWidth, desktopBounds.b.y - 2), helpFile, context, *this);
	window = static_cast<MRHelpWindow *>(TProgram::application->validView(window));
	if (window == nullptr) return false;
	activeViewer = window->helpViewer();

	helpOpen = true;
	TProgram::application->execView(window);
	helpOpen = false;
	TObject::destroy(window);
	return true;
}

void MRHelpSystem::recordTransition(int fromContext, int toContext) {
	if (fromContext == toContext) return;
	history.push_back(fromContext);
	currentContext = toContext;
	currentContextValid = true;
}

bool MRHelpSystem::previousContext(int &context) {
	if (history.empty()) return false;
	context = history.back();
	history.pop_back();
	currentContext = context;
	currentContextValid = true;
	return true;
}

void MRHelpSystem::releaseViewer(MRHelpViewer *viewer) noexcept {
	if (activeViewer == viewer) activeViewer = nullptr;
}

bool mrShowProjectHelp(ushort context) {
	MREditorApp *application = dynamic_cast<MREditorApp *>(TProgram::application);

	return application != nullptr && application->showHelpTopic(context);
}

bool mrShowPreviousProjectHelp() {
	MREditorApp *application = dynamic_cast<MREditorApp *>(TProgram::application);

	return application != nullptr && application->showPreviousHelpTopic();
}
