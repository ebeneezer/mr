#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TFileDialog
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TScreen
#include <tvision/tv.h>

#include "MRFileCommands.hpp"
#include "MRWindowCommands.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../coprocessor/MRCoprocessor.hpp"
#include "../utils/MRFileIOUtils.hpp"
#include "MRPerformance.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRMenuBar.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRBentoBox/MRBentoBox.hpp"
#include "../../ui/MRBentoHexEditor/MRBentoHexEditor.hpp"
#include "../../ui/widgets/MRScopedHistoryUI.hpp"
#include "../../ui/MRWindowLayout.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../../ui/MRDesktopWindow.hpp"
#include "../../dialogs/MRWindowList.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"

namespace {
void logWindowTiming(const std::string &label, long long tookUs, const std::string &detail) {
	std::ostringstream line;

	line << label << " took_us=" << tookUs;
	if (!detail.empty()) line << " " << detail;
	mrLogMessage(line.str());
}

void postWindowCommandError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void postDeferredWindowClose(MREditWindow &window) noexcept {
	TEvent event{};

	if (TProgram::application == nullptr) {
		window.close();
		return;
	}
	event.what = evCommand;
	event.message.command = cmMrDeferredWindowClose;
	event.message.infoPtr = &window;
	TProgram::application->putEvent(event);
}

void collectEditWindowsInZOrder(TView *view, void *arg) {
	std::vector<MREditWindow *> *windows = static_cast<std::vector<MREditWindow *> *>(arg);
	MREditWindow *win = dynamic_cast<MREditWindow *>(view);

	if (windows != nullptr && win != nullptr) windows->push_back(win);
}

void collectDesktopWindowsInZOrder(TView *view, void *arg) {
	std::vector<MRDesktopWindow *> *windows = static_cast<std::vector<MRDesktopWindow *> *>(arg);
	MRDesktopWindow *window = dynamic_cast<MRDesktopWindow *>(view);

	if (windows != nullptr && window != nullptr) windows->push_back(window);
}
} // namespace

std::vector<MREditWindow *> allEditWindowsInZOrder() {
	std::vector<MREditWindow *> windows;
	const auto startedAt = std::chrono::steady_clock::now();

	if (TProgram::deskTop == nullptr) return windows;

	TProgram::deskTop->forEach(collectEditWindowsInZOrder, &windows);
	{
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();
		if (tookUs >= 5000) logWindowTiming("Window enumerate slow", tookUs, "count=" + std::to_string(windows.size()));
	}
	return windows;
}

std::vector<MREditWindow *> allEditWindowsAndBentoPanesInZOrder() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	std::vector<MREditWindow *> expanded;

	expanded.reserve(windows.size());
	for (MREditWindow *window : windows) {
		expanded.push_back(window);
		if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window)) bentoBox->collectVisiblePaneWindows(expanded);
	}
	return expanded;
}

std::vector<MRDesktopWindow *> allDesktopWindowsInZOrder() {
	std::vector<MRDesktopWindow *> windows;

	if (TProgram::deskTop == nullptr) return windows;
	TProgram::deskTop->forEach(collectDesktopWindowsInZOrder, &windows);
	return windows;
}

MRDesktopWindow *currentDesktopWindow() {
	return TProgram::deskTop != nullptr ? dynamic_cast<MRDesktopWindow *>(TProgram::deskTop->current) : nullptr;
}

namespace {
void collectUsedEditorWindowNumbers(std::set<short> &used) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (auto &window : windows) {
		if (window != nullptr && window->number > 0) used.insert(window->number);
	}
}

short nextEditorWindowNumberFromSet(std::set<short> &used) {
	short candidate = 1;

	while (used.find(candidate) != used.end()) {
		if (candidate == std::numeric_limits<short>::max()) return candidate;
		++candidate;
	}
	used.insert(candidate);
	return candidate;
}

short nextEditorWindowNumber() {
	std::set<short> used;

	collectUsedEditorWindowNumbers(used);
	return nextEditorWindowNumberFromSet(used);
}

void finishNewEditWindow(MREditWindow *win, bool notifyTopology = true) {
	if (win == nullptr || TProgram::deskTop == nullptr) return;
	TProgram::deskTop->insert(win);
	win->mVirtualDesktop = currentVirtualDesktop();
	win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
	if (win->getEditor() != nullptr) win->getEditor()->setInsertModeEnabled(configuredDefaultInsertMode());
	if (notifyTopology) mrNotifyWindowTopologyChanged();
}

MREditWindow *createEditorWindowWithNumber(const char *title, short number, bool notifyTopology) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRBentoBox(bounds, title, number, bbmDocumentViewports);
	finishNewEditWindow(win, notifyTopology);
	return win;
}

MRBentoHexEditor *createHexEditorWindowWithNumber(const char *title, short number, bool notifyTopology) {
	TRect bounds;
	MRBentoHexEditor *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRBentoHexEditor(bounds, title, number);
	finishNewEditWindow(win, notifyTopology);
	return win;
}
} // namespace

MRWindowOpenBatch::MRWindowOpenBatch() : usedNumbers(), mActive(false), mDesktopLocked(false), mCreatedCount(0) {
}

void MRWindowOpenBatch::begin() {
	if (mActive) return;
	usedNumbers.clear();
	collectUsedEditorWindowNumbers(usedNumbers);
	mCreatedCount = 0;
	if (TProgram::deskTop != nullptr) {
		TProgram::deskTop->lock();
		mDesktopLocked = true;
	}
	mActive = true;
}

MREditWindow *MRWindowOpenBatch::createEditorWindow(const char *title) {
	MREditWindow *window = nullptr;

	if (!mActive) begin();
	window = createEditorWindowWithNumber(title, nextEditorWindowNumberFromSet(usedNumbers), false);
	if (window != nullptr) ++mCreatedCount;
	return window;
}

MRBentoHexEditor *MRWindowOpenBatch::createHexEditorWindow(const char *title) {
	MRBentoHexEditor *window = nullptr;

	if (!mActive) begin();
	window = createHexEditorWindowWithNumber(title, nextEditorWindowNumberFromSet(usedNumbers), false);
	if (window != nullptr) ++mCreatedCount;
	return window;
}

void MRWindowOpenBatch::finish(bool syncVisibility, bool notifyTopology) {
	if (!mActive) return;
	if (mDesktopLocked && TProgram::deskTop != nullptr) TProgram::deskTop->unlock();
	mDesktopLocked = false;
	mActive = false;
	if (mCreatedCount != 0 && syncVisibility) syncVirtualDesktopVisibility();
	if (mCreatedCount != 0 && notifyTopology) mrNotifyWindowTopologyChanged();
}

bool MRWindowOpenBatch::active() const noexcept {
	return mActive;
}

static int g_currentVirtualDesktop = 1;
static int g_virtualDesktopCountSnapshot = 1;
static bool g_cyclicVirtualDesktopsSnapshot = false;
static std::string g_workspaceMainFilePath;
static bool g_workspaceAutosaveDirty = false;
static bool g_workspaceRestoreInProgress = false;
static std::chrono::steady_clock::time_point g_workspaceAutosaveDue;
static constexpr std::chrono::milliseconds kWorkspaceAutosaveDelay(1000);
static constexpr int kWorkspaceRestoreProgressEntryThreshold = 50;

namespace {
int normalizedVirtualDesktopCount(int count) {
	if (count < 1) return 1;
	if (count > 9) return 9;
	return count;
}

struct WorkspaceEntry {
	std::string url;
	int width = -1;
	int height = -1;
	int x = -1;
	int y = -1;
	int restoreWidth = -1;
	int restoreHeight = -1;
	int restoreX = -1;
	int restoreY = -1;
	int column = 1;
	int line = 1;
	int vd = 1;
	bool minimized = false;
	bool hasBentoSnapshot = false;
	MRBentoWorkspaceSnapshot bentoSnapshot;
	bool hasMacroDebuggerConfiguration = false;
	MRMacroDebuggerWorkspaceConfiguration macroDebuggerConfiguration;
	bool hasFileCompareSources = false;
	std::string fileCompareOriginalUrl;
	std::string fileCompareCompareUrl;
	bool mainFile = false;
};

std::string workspaceHexEncode(std::string_view value) {
	static constexpr char hex[] = "0123456789ABCDEF";
	std::string encoded;

	encoded.reserve(value.size() * 2);
	for (unsigned char ch : value) {
		encoded.push_back(hex[(ch >> 4) & 0x0F]);
		encoded.push_back(hex[ch & 0x0F]);
	}
	return encoded;
}

int workspaceHexValue(char ch) noexcept {
	if (ch >= '0' && ch <= '9') return ch - '0';
	if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
	if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
	return -1;
}

bool workspaceHexDecode(const std::string &encoded, std::string &decoded) {
	if ((encoded.size() % 2) != 0) return false;
	decoded.clear();
	decoded.reserve(encoded.size() / 2);
	for (std::size_t i = 0; i < encoded.size(); i += 2) {
		const int high = workspaceHexValue(encoded[i]);
		const int low = workspaceHexValue(encoded[i + 1]);
		if (high < 0 || low < 0) return false;
		decoded.push_back(static_cast<char>((high << 4) | low));
	}
	return true;
}

std::string escapeMrmacSingleQuotedLiteral(std::string_view value) {
	std::string escaped;

	escaped.reserve(value.size());
	for (char ch : value) {
		if (ch == '\'') escaped += "''";
		else
			escaped.push_back(ch);
	}
	return escaped;
}

std::string unescapeMrmacSingleQuotedLiteral(std::string_view value) {
	std::string unescaped;

	unescaped.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		char ch = value[i];
		if (ch == '\'' && i + 1 < value.size() && value[i + 1] == '\'') {
			unescaped.push_back('\'');
			++i;
		} else
			unescaped.push_back(ch);
	}
	return unescaped;
}

std::string workspaceDisplayName(const std::string &path) {
	std::size_t pos = path.find_last_of("\\/");
	return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::vector<std::string> splitWorkspaceToken(const std::string &text, char delimiter) {
	std::vector<std::string> parts;
	std::string part;
	std::istringstream input(text);

	while (std::getline(input, part, delimiter)) parts.push_back(part);
	return parts;
}

bool parseWorkspaceInt(const std::string &text, int &value) {
	char *end = nullptr;
	const long parsed = std::strtol(text.c_str(), &end, 10);

	if (end == text.c_str() || *end != '\0') return false;
	if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) return false;
	value = static_cast<int>(parsed);
	return true;
}

bool parseWorkspacePrefixedInt(const std::string &text, const char *prefix, int &value) {
	const std::string expected(prefix);

	if (text.rfind(expected, 0) != 0) return false;
	return parseWorkspaceInt(text.substr(expected.size()), value);
}

std::string encodeBentoWorkspaceSnapshot(const MRBentoWorkspaceSnapshot &snapshot) {
	std::ostringstream out;

	out << "v1";
	out << ",m:" << static_cast<int>(snapshot.mode);
	out << ",r:" << snapshot.rootNode;
	out << ",a:" << snapshot.activeLeafId;
	out << ",x:" << snapshot.maximizedLeafId;
	out << ",n:";
	for (std::size_t i = 0; i < snapshot.nodes.size(); ++i) {
		const MRBentoWorkspaceNode &node = snapshot.nodes[i];
		if (i != 0) out << ".";
		out << node.kind << ":" << node.orientation << ":" << node.dividerPosition << ":" << node.firstChild << ":" << node.secondChild << ":" << node.leafId;
	}
	out << ",l:";
	for (std::size_t i = 0; i < snapshot.leaves.size(); ++i) {
		const MRBentoWorkspaceLeaf &leaf = snapshot.leaves[i];
		if (i != 0) out << ".";
		out << leaf.id << ":" << static_cast<int>(leaf.role) << ":" << (leaf.visible ? 1 : 0);
	}
	return out.str();
}

bool parseBentoWorkspaceSnapshot(const std::string &token, MRBentoWorkspaceSnapshot &snapshot) {
	std::vector<std::string> fields = splitWorkspaceToken(token, ',');
	int mode = 0;

	if (fields.size() != 7 || fields[0] != "v1") return false;
	if (!parseWorkspacePrefixedInt(fields[1], "m:", mode)) return false;
	if (!parseWorkspacePrefixedInt(fields[2], "r:", snapshot.rootNode)) return false;
	if (!parseWorkspacePrefixedInt(fields[3], "a:", snapshot.activeLeafId)) return false;
	if (!parseWorkspacePrefixedInt(fields[4], "x:", snapshot.maximizedLeafId)) return false;
	if (mode < bbmToolWorkspace || mode > bbmFileCompare) return false;
	snapshot.mode = static_cast<MRBentoBoxMode>(mode);

	if (fields[5].rfind("n:", 0) != 0 || fields[6].rfind("l:", 0) != 0) return false;
	snapshot.nodes.clear();
	snapshot.leaves.clear();

	for (const std::string &nodeText : splitWorkspaceToken(fields[5].substr(2), '.')) {
		std::vector<std::string> values = splitWorkspaceToken(nodeText, ':');
		MRBentoWorkspaceNode node;

		if (values.size() != 6) return false;
		if (!parseWorkspaceInt(values[0], node.kind)) return false;
		if (!parseWorkspaceInt(values[1], node.orientation)) return false;
		if (!parseWorkspaceInt(values[2], node.dividerPosition)) return false;
		if (!parseWorkspaceInt(values[3], node.firstChild)) return false;
		if (!parseWorkspaceInt(values[4], node.secondChild)) return false;
		if (!parseWorkspaceInt(values[5], node.leafId)) return false;
		snapshot.nodes.push_back(node);
	}
	for (const std::string &leafText : splitWorkspaceToken(fields[6].substr(2), '.')) {
		std::vector<std::string> values = splitWorkspaceToken(leafText, ':');
		MRBentoWorkspaceLeaf leaf;
		int role = 0;
		int visible = 0;

		if (values.size() != 3) return false;
		if (!parseWorkspaceInt(values[0], leaf.id)) return false;
		if (!parseWorkspaceInt(values[1], role)) return false;
		if (!parseWorkspaceInt(values[2], visible)) return false;
		if (role < bprSource || role > bprExtensionLast || (visible != 0 && visible != 1)) return false;
		leaf.role = static_cast<MRBentoPaneRole>(role);
		leaf.visible = visible != 0;
		snapshot.leaves.push_back(leaf);
	}
	return !snapshot.nodes.empty() && !snapshot.leaves.empty();
}

std::string encodeMacroDebuggerWorkspaceConfiguration(const MRMacroDebuggerWorkspaceConfiguration &configuration) {
	std::ostringstream out;

	out << "v1";
	out << ",k:" << workspaceHexEncode(configuration.macroKey);
	out << ",n:" << workspaceHexEncode(configuration.macroName);
	out << ",b:";
	for (std::size_t index = 0; index < configuration.breakpoints.size(); ++index) {
		const MRMacroDebuggerWorkspaceBreakpoint &breakpoint = configuration.breakpoints[index];

		if (index != 0) out << ".";
		out << workspaceHexEncode(breakpoint.macroKey) << ":" << breakpoint.line << ":" << (breakpoint.enabled ? 1 : 0);
	}
	out << ",w:";
	for (std::size_t index = 0; index < configuration.watches.size(); ++index) {
		const MRMacroDebuggerWorkspaceWatch &watch = configuration.watches[index];

		if (index != 0) out << ".";
		out << workspaceHexEncode(watch.expression) << ":" << (watch.enabled ? 1 : 0);
	}
	return out.str();
}

bool parseMacroDebuggerWorkspaceConfiguration(const std::string &token, MRMacroDebuggerWorkspaceConfiguration &configuration) {
	std::vector<std::string> fields = splitWorkspaceToken(token, ',');

	configuration = MRMacroDebuggerWorkspaceConfiguration();
	if (fields.size() != 5 || fields[0] != "v1" || fields[1].rfind("k:", 0) != 0 || fields[2].rfind("n:", 0) != 0 || fields[3].rfind("b:", 0) != 0 || fields[4].rfind("w:", 0) != 0) return false;
	if (!workspaceHexDecode(fields[1].substr(2), configuration.macroKey) || !workspaceHexDecode(fields[2].substr(2), configuration.macroName) || configuration.macroKey.empty()) return false;
	if (!fields[3].substr(2).empty())
		for (const std::string &entry : splitWorkspaceToken(fields[3].substr(2), '.')) {
			std::vector<std::string> values = splitWorkspaceToken(entry, ':');
			MRMacroDebuggerWorkspaceBreakpoint breakpoint;
			int enabled = 0;

			if (values.size() != 3 || !workspaceHexDecode(values[0], breakpoint.macroKey) || !parseWorkspaceInt(values[1], breakpoint.line) || !parseWorkspaceInt(values[2], enabled) || breakpoint.macroKey.empty() || breakpoint.line <= 0 || (enabled != 0 && enabled != 1)) return false;
			breakpoint.enabled = enabled != 0;
			configuration.breakpoints.push_back(breakpoint);
		}
	if (!fields[4].substr(2).empty())
		for (const std::string &entry : splitWorkspaceToken(fields[4].substr(2), '.')) {
			std::vector<std::string> values = splitWorkspaceToken(entry, ':');
			MRMacroDebuggerWorkspaceWatch watch;
			int enabled = 0;

			if (values.size() != 2 || !workspaceHexDecode(values[0], watch.expression) || !parseWorkspaceInt(values[1], enabled) || watch.expression.empty() || (enabled != 0 && enabled != 1)) return false;
			watch.enabled = enabled != 0;
			configuration.watches.push_back(watch);
		}
	return true;
}

int workspaceVirtualDesktopOrRandom(int savedDesktop) {
	int maxDesktop = mrVirtualDesktopCountSnapshot();

	if (maxDesktop < 1) maxDesktop = 1;
	if (savedDesktop >= 1 && savedDesktop <= maxDesktop) return savedDesktop;

	static std::mt19937 generator(std::random_device{}());
	std::uniform_int_distribution<int> distribution(1, maxDesktop);
	return distribution(generator);
}

bool parseWorkspaceEntry(const std::string &line, WorkspaceEntry &entry) {
	static const std::regex linePattern(R"(MRSETUP\s*\(\s*'WORKSPACE'\s*,\s*'((?:''|[^'])*)'\s*\)\s*;?)", std::regex_constants::ECMAScript | std::regex_constants::icase);
	static const std::regex payloadPattern(R"(^URL=(.*) size=(-?\d+),(-?\d+) pos=(-?\d+),(-?\d+) cursor=(-?\d+),(-?\d+) vd=(-?\d+)(?: min=(0|1) restore=(-?\d+),(-?\d+) rpos=(-?\d+),(-?\d+))?(?: main=(0|1))?(?: bento=([^ ]+))?(?: debug=([^ ]+))?(?: fco=([0-9A-Fa-f]+) fcc=([0-9A-Fa-f]+))?$)", std::regex_constants::ECMAScript);
	std::smatch match;
	std::smatch payloadMatch;
	std::string payload;

	if (!std::regex_search(line, match, linePattern)) return false;
	payload = unescapeMrmacSingleQuotedLiteral(match[1].str());
	if (!std::regex_match(payload, payloadMatch, payloadPattern)) return false;

	entry.url = payloadMatch[1].str();
	entry.width = std::stoi(payloadMatch[2].str());
	entry.height = std::stoi(payloadMatch[3].str());
	entry.x = std::stoi(payloadMatch[4].str());
	entry.y = std::stoi(payloadMatch[5].str());
	entry.column = std::stoi(payloadMatch[6].str());
	entry.line = std::stoi(payloadMatch[7].str());
	entry.vd = std::stoi(payloadMatch[8].str());
	entry.minimized = payloadMatch[9].matched && payloadMatch[9].str() == "1";
	if (payloadMatch[10].matched && payloadMatch[11].matched && payloadMatch[12].matched && payloadMatch[13].matched) {
		entry.restoreWidth = std::stoi(payloadMatch[10].str());
		entry.restoreHeight = std::stoi(payloadMatch[11].str());
		entry.restoreX = std::stoi(payloadMatch[12].str());
		entry.restoreY = std::stoi(payloadMatch[13].str());
	} else {
		entry.restoreWidth = entry.width;
		entry.restoreHeight = entry.height;
		entry.restoreX = entry.x;
		entry.restoreY = entry.y;
	}
	entry.mainFile = payloadMatch[14].matched && payloadMatch[14].str() == "1";
	if (payloadMatch[15].matched) {
		entry.hasBentoSnapshot = parseBentoWorkspaceSnapshot(payloadMatch[15].str(), entry.bentoSnapshot);
		if (!entry.hasBentoSnapshot) return false;
	}
	if (payloadMatch[16].matched) {
		entry.hasMacroDebuggerConfiguration = parseMacroDebuggerWorkspaceConfiguration(payloadMatch[16].str(), entry.macroDebuggerConfiguration);
		if (!entry.hasMacroDebuggerConfiguration || !entry.hasBentoSnapshot) return false;
	}
	if (payloadMatch[17].matched || payloadMatch[18].matched) {
		if (!payloadMatch[17].matched || !payloadMatch[18].matched) return false;
		if (!workspaceHexDecode(payloadMatch[17].str(), entry.fileCompareOriginalUrl)) return false;
		if (!workspaceHexDecode(payloadMatch[18].str(), entry.fileCompareCompareUrl)) return false;
		entry.hasFileCompareSources = !entry.fileCompareOriginalUrl.empty() && !entry.fileCompareCompareUrl.empty();
		if (!entry.hasFileCompareSources) return false;
	}
	return !entry.url.empty();
}

void applyWorkspaceEntryGeometry(MREditWindow *window, const WorkspaceEntry &entry) {
	if (window == nullptr || entry.width <= 0 || entry.height <= 0 || entry.x < 0 || entry.y < 0) return;
	const TRect bounds(entry.x, entry.y, entry.x + entry.width, entry.y + entry.height);
	const TRect restoreBounds(entry.restoreX, entry.restoreY, entry.restoreX + std::max(entry.restoreWidth, 1), entry.restoreY + std::max(entry.restoreHeight, 1));
	std::ostringstream detail;

	MRWindowLayout::applyWorkspaceState(window, bounds, restoreBounds, entry.minimized, false, false);
	detail << "Workspace geometry restored window=" << window->number
	       << " saved=" << bounds.a.x << "," << bounds.a.y << "," << bounds.b.x << "," << bounds.b.y
	       << " restore=" << restoreBounds.a.x << "," << restoreBounds.a.y << "," << restoreBounds.b.x << "," << restoreBounds.b.y
	       << " applied=" << window->getBounds().a.x << "," << window->getBounds().a.y << "," << window->getBounds().b.x << "," << window->getBounds().b.y
	       << " min=" << (entry.minimized ? 1 : 0) << ".";
	mrLogMessage(detail.str());
}

int countWorkspaceEntriesInSource(const std::string &source) {
	std::istringstream input(source);
	std::string line;
	int count = 0;

	while (std::getline(input, line)) {
		WorkspaceEntry entry;

		if (parseWorkspaceEntry(line, entry)) ++count;
	}
	return count;
}

void drawWorkspaceMessageLineNow(const std::string &text, MRMenuBar::MarqueeKind kind) {
	MRMenuBar *menuBar = dynamic_cast<MRMenuBar *>(TProgram::menuBar);

	if (menuBar == nullptr) return;
	menuBar->setAutoMarqueeStatus(text, kind);
	menuBar->drawView();
	TScreen::flushScreen();
}

void postWorkspaceRestoreProgress(int processedEntries, int totalEntries, bool refreshNow) {
	std::string text = "Workspace restore: " + std::to_string(processedEntries) + "/" + std::to_string(totalEntries);

	mr::messageline::postSticky(mr::messageline::Owner::HeroEvent, text, mr::messageline::Kind::Info, mr::messageline::kPriorityHigh);
	if (refreshNow) drawWorkspaceMessageLineNow(text, MRMenuBar::MarqueeKind::Hero);
}

void hideAllEditorFrameHoverPopups() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (MREditWindow *window : windows)
		if (window != nullptr) {
			MRFrame *frame = dynamic_cast<MRFrame *>(window->frame);
			if (frame != nullptr) frame->updateTaskHover(TPoint(), true);
		}
}

void restoreEditorCursor(MRFileEditor *editor, int line, int column) {
	if (editor == nullptr) return;
	line = std::max(1, line);
	column = std::max(1, column);
	editor->restoreCursorViewState(static_cast<std::size_t>(line - 1), column - 1);
}

std::string workspacePathBaseName(const std::string &path) {
	const std::size_t pos = path.find_last_of("\\/");
	return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string fileCompareSourceTitle(MREditWindow *window) {
	if (window == nullptr) return "?No-File";
	std::string fileName = window->currentFileName();
	if (!fileName.empty()) return workspacePathBaseName(fileName);
	const char *title = window->getTitle(0);
	return title != nullptr && *title != '\0' ? std::string(title) : std::string("?No-File");
}

MRBentoCompareSource captureWorkspaceFileCompareSource(MREditWindow *window) {
	MRBentoCompareSource source;

	if (window == nullptr) return source;
	source.window = window;
	source.bufferId = window->bufferId();
	source.documentId = window->documentId();
	source.version = window->documentVersion();
	source.wasVisible = (window->state & sfVisible) != 0;
	source.wasManuallyHidden = isWindowManuallyHidden(window);
	source.title = fileCompareSourceTitle(window);
	if (window->getEditor() != nullptr) source.text = window->getEditor()->snapshotText();
	return source;
}

std::string fileCompareWorkspaceTitle(const MRBentoCompareSetup &setup) {
	std::string title = "Compare: " + setup.original.title + " / " + setup.compare.title;

	if (title.size() > 72) title = title.substr(0, 69) + "...";
	return title;
}

std::uint64_t submitWorkspaceFileCompareTask(MRBentoBox *bentoBox, const MRBentoCompareSetup &setup) {
	std::vector<std::string> originalLines;
	std::vector<std::string> compareLines;

	if (bentoBox == nullptr) return 0;
	mr::diff::mrSplitTextLinesForDiff(setup.original.text, originalLines);
	mr::diff::mrSplitTextLinesForDiff(setup.compare.text, compareLines);

	return mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FileCompare, setup.original.documentId, setup.original.version, "file compare", [originalLines, compareLines, originalDocumentId = setup.original.documentId, originalVersion = setup.original.version, compareDocumentId = setup.compare.documentId, compareVersion = setup.compare.version](const mr::coprocessor::TaskInfo &task, std::stop_token stopToken) {
		mr::coprocessor::Result result;
		std::vector<mr::diff::MRDiffHunk> hunks;
		std::string errorText;

		result.task = task;
		if (!mr::diff::mrComputeMyersDiff(originalLines, compareLines, hunks, &errorText, stopToken)) {
			result.status = stopToken.stop_requested() ? mr::coprocessor::TaskStatus::Cancelled : mr::coprocessor::TaskStatus::Failed;
			result.error = errorText;
			return result;
		}
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::FileComparePayload>(originalDocumentId, originalVersion, compareDocumentId, compareVersion, originalLines.size(), compareLines.size(), std::move(hunks));
		return result;
	});
}

void closeWorkspaceWindow(MREditWindow *win) {
	if (win != nullptr) message(win, evCommand, cmClose, nullptr);
}

bool loadWorkspaceSourceWindow(const std::string &path, int virtualDesktop, MREditWindow *&window) {
	std::string errorText;
	MRFileEditor *editor = nullptr;

	window = createEditorWindow(path.c_str());
	editor = window != nullptr ? window->getEditor() : nullptr;
	if (window == nullptr || editor == nullptr || !editor->loadMappedFile(path.c_str(), errorText)) {
		closeWorkspaceWindow(window);
		window = nullptr;
		return false;
	}
	window->mVirtualDesktop = virtualDesktop;
	return true;
}

MRBentoBox *restoreFileCompareWorkspaceEntry(const WorkspaceEntry &entry, int virtualDesktop) {
	MREditWindow *originalWindow = nullptr;
	MREditWindow *compareWindow = nullptr;
	MRBentoBox *bentoBox = nullptr;
	MRBentoCompareSetup setup;
	std::uint64_t taskId = 0;
	std::string title;

	if (!entry.hasBentoSnapshot || entry.bentoSnapshot.mode != bbmFileCompare || !entry.hasFileCompareSources) return nullptr;
	if (!loadWorkspaceSourceWindow(entry.fileCompareOriginalUrl, virtualDesktop, originalWindow)) return nullptr;
	if (!loadWorkspaceSourceWindow(entry.fileCompareCompareUrl, virtualDesktop, compareWindow)) {
		closeWorkspaceWindow(originalWindow);
		return nullptr;
	}

	setup.original = captureWorkspaceFileCompareSource(originalWindow);
	setup.compare = captureWorkspaceFileCompareSource(compareWindow);
	title = fileCompareWorkspaceTitle(setup);
	bentoBox = createFileCompareBentoBoxWindow(title.c_str());
	if (bentoBox != nullptr) {
		bentoBox->mVirtualDesktop = virtualDesktop;
		applyWorkspaceEntryGeometry(bentoBox, entry);
	}
	if (bentoBox == nullptr || !bentoBox->initializeFileCompare(setup) || !bentoBox->restoreWorkspaceSnapshot(entry.bentoSnapshot)) {
		closeWorkspaceWindow(bentoBox);
		closeWorkspaceWindow(compareWindow);
		closeWorkspaceWindow(originalWindow);
		return nullptr;
	}
	bentoBox->refreshFileCompareConfiguration();

	taskId = submitWorkspaceFileCompareTask(bentoBox, setup);
	if (taskId != 0) {
		bentoBox->setFileCompareTask(taskId);
		bentoBox->trackCoprocessorTask(taskId, mr::coprocessor::TaskKind::FileCompare, "file compare");
	}
	static_cast<void>(mrActivateEditWindow(bentoBox));
	return bentoBox;
}

bool isFileCompareSourceWindowForAnyBento(MREditWindow *window, const std::vector<MRBentoBox *> &fileCompareBoxes) {
	if (window == nullptr) return false;
	for (MRBentoBox *bentoBox : fileCompareBoxes)
		if (bentoBox != nullptr && bentoBox->containsFileCompareSourceWindow(window)) return true;
	return false;
}

std::string normalizedWorkspacePathForWindow(const MREditWindow *win) {
	const MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	const char *path = editor != nullptr ? editor->persistentFileName() : nullptr;
	std::string normalizedPath;

	if (path == nullptr || *path == '\0') return std::string();
	normalizedPath = normalizeConfiguredPathInput(path);
	return normalizedPath.empty() ? std::string(path) : normalizedPath;
}

} // namespace

bool mrSetWorkspaceMainFile(MREditWindow *win) {
	const std::string oldPath = g_workspaceMainFilePath;
	const std::string newPath = normalizedWorkspacePathForWindow(win);

	if (newPath.empty()) {
		postWindowCommandError("Workspace main file requires a saved file.");
		return false;
	}
	g_workspaceMainFilePath = newPath;
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		const std::string windowPath = normalizedWorkspacePathForWindow(window);
		if (window != nullptr && window->frame != nullptr && (windowPath == oldPath || windowPath == g_workspaceMainFilePath)) window->frame->drawView();
	}
	mrNotifyWindowTopologyChanged();
	return true;
}

void mrClearWorkspaceMainFile() {
	const std::string oldPath = g_workspaceMainFilePath;

	if (oldPath.empty()) return;
	g_workspaceMainFilePath.clear();
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		const std::string windowPath = normalizedWorkspacePathForWindow(window);
		if (window != nullptr && window->frame != nullptr && windowPath == oldPath) window->frame->drawView();
	}
	mrNotifyWindowTopologyChanged();
}

bool mrIsWorkspaceMainFile(const MREditWindow *win) {
	const std::string path = normalizedWorkspacePathForWindow(win);

	return !path.empty() && !g_workspaceMainFilePath.empty() && path == g_workspaceMainFilePath;
}

std::string mrWorkspaceMainFilePath() {
	return g_workspaceMainFilePath;
}

void mrMarkWorkspaceAutosaveDirty(const char *source, const MREditWindow *window) {
	const bool wasDirty = g_workspaceAutosaveDirty;
	const bool preserveBefore = runtimePreserveAutosavedWorkspace();
	const char *reason = source != nullptr && *source != '\0' ? source : "unspecified";

	g_workspaceAutosaveDirty = true;
	g_workspaceAutosaveDue = std::chrono::steady_clock::now() + kWorkspaceAutosaveDelay;
	if (configuredAutosaveWorkspace()) setRuntimePreserveAutosavedWorkspace(false);
	if (!wasDirty) {
		std::ostringstream detail;

		detail << "Workspace autosave dirty false->true source=" << reason << " autosave=" << (configuredAutosaveWorkspace() ? 1 : 0) << " preserve=" << (preserveBefore ? 1 : 0) << "->" << (runtimePreserveAutosavedWorkspace() ? 1 : 0);
		if (window != nullptr) {
			const TRect bounds = window->getBounds();

			detail << " window=" << window->number << " bounds=" << bounds.a.x << "," << bounds.a.y << "," << bounds.b.x << "," << bounds.b.y;
		}
		mrLogMessage(detail.str());
	}
}

namespace {
void flushWorkspaceAutosave(bool force) {
	const auto startedAt = std::chrono::steady_clock::now();
	std::string errorText;
	MRSettingsWriteReport report;
	long long persistUs = 0;

	if (!g_workspaceAutosaveDirty) return;
	if (!configuredAutosaveWorkspace()) return;
	if (runtimePreserveAutosavedWorkspace()) return;
	if (!force && std::chrono::steady_clock::now() < g_workspaceAutosaveDue) return;
	mrLogMessage(std::string("Workspace autosave flush begin force=") + (force ? "1" : "0") + ".");
	mrLogMessage(std::string("Workspace autosave dirty true->false source=flush force=") + (force ? "1" : "0") + ".");
	g_workspaceAutosaveDirty = false;
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		if (!persistConfiguredSettingsSnapshotWithWorkspace(&errorText, &report)) {
			g_workspaceAutosaveDirty = true;
			g_workspaceAutosaveDue = std::chrono::steady_clock::now() + kWorkspaceAutosaveDelay;
			mrLogMessage("Workspace autosave dirty false->true source=flush-failed.");
			if (!errorText.empty()) mrLogMessage("Workspace autosave failed: " + errorText);
			return;
		}
		persistUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	mrLogSettingsWriteReport("workspace autosave", report);
	logWindowTiming("Workspace autosave flush timing", std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count(), "persist_us=" + std::to_string(persistUs));
	mrLogMessage("Workspace autosave flush end.");
}
} // namespace

void mrFlushWorkspaceAutosaveIfDue() {
	flushWorkspaceAutosave(false);
}

void mrFlushWorkspaceAutosaveNow() {
	flushWorkspaceAutosave(true);
}

bool mrWorkspaceRestoreInProgress() {
	return g_workspaceRestoreInProgress;
}

std::string buildSettingsMacroSourceWithWorkspace(const MRSetupPaths &paths) {
	std::string source = buildSettingsMacroSource(paths);
	const std::size_t endMacro = source.rfind("END_MACRO;");
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	std::vector<MRBentoBox *> fileCompareBoxes;
	int candidateWindows = 0;
	int writtenEntries = 0;

	if (endMacro == std::string::npos) return source;
	for (MREditWindow *win : windows) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(win);
		if (bentoBox != nullptr && bentoBox->isFileCompareBox()) fileCompareBoxes.push_back(bentoBox);
	}
	for (MREditWindow *win : windows) {
		MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
		std::string url;
		std::string fileCompareOriginalUrl;
		std::string fileCompareCompareUrl;
		TRect bounds;
		TRect restoreBounds;
		int cursorColumn = 1;
		int cursorLine = 1;
		int vd = 1;
		bool minimized = false;
		std::string bentoPayload;
		std::string macroDebuggerPayload;
		std::string fileComparePayload;

		if (win == nullptr || editor == nullptr) {
			mrLogMessage("Workspace serialize skipped window without editor.");
			continue;
		}
		++candidateWindows;
		if (isFileCompareSourceWindowForAnyBento(win, fileCompareBoxes)) {
			mrLogMessage("Workspace serialize skipped file-compare source window: " + workspaceDisplayName(editor->persistentFileName()));
			continue;
		}
		if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(win)) {
			bentoPayload = encodeBentoWorkspaceSnapshot(bentoBox->workspaceSnapshot());
			MRMacroDebuggerWorkspaceConfiguration macroDebuggerConfiguration;

			if (bentoBox->macroDebuggerWorkspaceConfiguration(macroDebuggerConfiguration)) macroDebuggerPayload = " debug=" + encodeMacroDebuggerWorkspaceConfiguration(macroDebuggerConfiguration);
			if (bentoBox->isFileCompareBox()) {
				if (!bentoBox->fileCompareWorkspaceSourcePaths(fileCompareOriginalUrl, fileCompareCompareUrl)) {
					mrLogMessage("Workspace serialize skipped file-compare Bento without source paths.");
					continue;
				}
				url = fileCompareOriginalUrl;
				fileComparePayload = " fco=" + workspaceHexEncode(fileCompareOriginalUrl) + " fcc=" + workspaceHexEncode(fileCompareCompareUrl);
			}
		}
		if (url.empty()) url = editor->persistentFileName();
		if (url.empty()) {
			mrLogMessage("Workspace serialize skipped window without persistent filename.");
			continue;
		}
		bounds = win->isMinimized() ? win->minimizedWorkspaceBounds() : win->getBounds();
		restoreBounds = win->restoreWorkspaceBounds();
		cursorColumn = editor->currentColumnNumber();
		cursorLine = editor->currentLineNumber();
		vd = win->mVirtualDesktop;
		minimized = win->isMinimized();
		source.insert(endMacro, "MRSETUP('WORKSPACE', 'URL=" + escapeMrmacSingleQuotedLiteral(url) + " size=" + std::to_string(bounds.b.x - bounds.a.x) + "," + std::to_string(bounds.b.y - bounds.a.y) + " pos=" + std::to_string(bounds.a.x) + "," + std::to_string(bounds.a.y) + " cursor=" + std::to_string(cursorColumn) + "," + std::to_string(cursorLine) + " vd=" + std::to_string(vd) + " min=" + std::to_string(minimized ? 1 : 0) + " restore=" + std::to_string(restoreBounds.b.x - restoreBounds.a.x) + "," + std::to_string(restoreBounds.b.y - restoreBounds.a.y) + " rpos=" + std::to_string(restoreBounds.a.x) + "," + std::to_string(restoreBounds.a.y) + (mrIsWorkspaceMainFile(win) ? std::string(" main=1") : std::string()) + (bentoPayload.empty() ? std::string() : " bento=" + bentoPayload) + macroDebuggerPayload + fileComparePayload + "');\n");
		++writtenEntries;
	}
	mrLogMessage("Workspace serialize summary candidates=" + std::to_string(candidateWindows) + " entries=" + std::to_string(writtenEntries) + " settings=" + paths.settingsMacroUri + ".");
	return source;
}

bool mrSettingsFileHasAutosavedWorkspace() {
	std::string content;
	std::string errorText;
	std::string path = configuredSettingsMacroFilePath();

	if (path.find(".mrmac") == std::string::npos) path += ".mrmac";
	if (!readTextFile(path, content, errorText)) {
		mrLogMessage("Workspace autosave probe failed path=" + path + " error=" + errorText + ".");
		return false;
	}
	{
		std::istringstream input(content);
		std::string line;
		int parsedEntries = 0;

		while (std::getline(input, line)) {
			WorkspaceEntry entry;

			if (parseWorkspaceEntry(line, entry)) {
				++parsedEntries;
				mrLogMessage("Workspace autosave probe found entry url=" + entry.url + ".");
				return true;
			}
		}
		mrLogMessage("Workspace autosave probe found no entries path=" + path + " parsed=" + std::to_string(parsedEntries) + ".");
	}
	return false;
}

bool mrClearAutosavedWorkspace() {
	std::string content;
	std::string errorText;
	std::string path = configuredSettingsMacroFilePath();
	std::ostringstream output;
	bool removed = false;

	if (path.find(".mrmac") == std::string::npos) path += ".mrmac";
	if (!readTextFile(path, content, errorText)) return false;
	{
		std::istringstream input(content);
		std::string line;

		while (std::getline(input, line)) {
			WorkspaceEntry entry;

			if (parseWorkspaceEntry(line, entry)) {
				removed = true;
				continue;
			}
			output << line << '\n';
		}
	}
	if (!removed) return true;
	if (!writeTextFile(path, output.str())) {
		postWindowCommandError("Unable to clear autosaved workspace.");
		return false;
	}
	return true;
}

int currentVirtualDesktop() {
	return g_currentVirtualDesktop;
}

void mrRefreshVirtualDesktopSettingsSnapshot(int count, bool cyclic) {
	g_virtualDesktopCountSnapshot = normalizedVirtualDesktopCount(count);
	g_cyclicVirtualDesktopsSnapshot = cyclic;
}

void mrRefreshVirtualDesktopSettingsSnapshot() {
	mrRefreshVirtualDesktopSettingsSnapshot(configuredVirtualDesktops(), configuredCyclicVirtualDesktops());
}

int mrVirtualDesktopCountSnapshot() {
	return g_virtualDesktopCountSnapshot;
}

bool mrCyclicVirtualDesktopsSnapshot() {
	return g_cyclicVirtualDesktopsSnapshot;
}

void setWindowManuallyHidden(MREditWindow *win, bool hidden) {
	if (win == nullptr) return;
	if (hidden == isWindowManuallyHidden(win)) return;
	win->setDesktopManuallyHidden(hidden);
	MRWindowLayout::handleDesktopLayoutChange();
	mrNotifyWindowTopologyChanged();
}

bool isWindowManuallyHidden(const MREditWindow *win) {
	return win != nullptr && win->desktopManuallyHidden();
}

namespace {
void postDesktopChangedMessage(int desktop) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "Desktop #" + std::to_string(desktop), mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
}
} // namespace

void syncVirtualDesktopVisibility() {
	std::vector<MRDesktopWindow *> windows = allDesktopWindowsInZOrder();
	MRDesktopWindow *candidate = nullptr;
	MRDesktopWindow *current = TProgram::deskTop != nullptr ? dynamic_cast<MRDesktopWindow *>(TProgram::deskTop->current) : nullptr;

	for (MRDesktopWindow *window : windows) {
		TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
		const bool visible = window != nullptr && window->desktopIndex() == g_currentVirtualDesktop && !window->desktopManuallyHidden();

		if (nativeWindow == nullptr) continue;
		if (visible) {
			if (candidate == nullptr) candidate = window;
			if ((nativeWindow->state & sfVisible) == 0) nativeWindow->show();
		} else if ((nativeWindow->state & sfVisible) != 0)
			nativeWindow->hide();
	}

	if (candidate != nullptr && (current == nullptr || current->desktopIndex() != g_currentVirtualDesktop || (current->desktopNativeWindow()->state & sfVisible) == 0)) candidate->desktopNativeWindow()->select();

	if (TProgram::deskTop != nullptr) {
		TProgram::deskTop->redraw();
		TProgram::deskTop->drawView();
	}
	if (TProgram::application != nullptr) TProgram::application->redraw();
	MRWindowLayout::handleDesktopLayoutChange();
}

void setCurrentVirtualDesktop(int vd) {
	const int oldDesktop = g_currentVirtualDesktop;

	if (vd < 1) vd = 1;
	int maxVd = mrVirtualDesktopCountSnapshot();
	if (maxVd < 1) maxVd = 1;
	if (vd > maxVd) vd = maxVd;
	g_currentVirtualDesktop = vd;
	syncVirtualDesktopVisibility();
	if (g_currentVirtualDesktop != oldDesktop) postDesktopChangedMessage(vd);
}

void applyVirtualDesktopConfigurationChange(int count) {
	std::vector<MRDesktopWindow *> windows = allDesktopWindowsInZOrder();
	std::string ignoredError;

	count = normalizedVirtualDesktopCount(count);
	for (MRDesktopWindow *window : windows)
		if (window != nullptr && window->desktopIndex() > count) window->setDesktopIndex(count);

	setConfiguredVirtualDesktops(count, &ignoredError);
	mrRefreshVirtualDesktopSettingsSnapshot(count, configuredCyclicVirtualDesktops());
	setCurrentVirtualDesktop(std::min(currentVirtualDesktop(), count));
}

bool moveToNextVirtualDesktop() {
	MRDesktopWindow *window = TProgram::deskTop != nullptr ? dynamic_cast<MRDesktopWindow *>(TProgram::deskTop->current) : nullptr;
	int maxVd = mrVirtualDesktopCountSnapshot();
	if (window == nullptr || window->desktopIndex() >= maxVd) return false;
	window->setDesktopIndex(window->desktopIndex() + 1);
	syncVirtualDesktopVisibility();
	return true;
}

bool moveToPrevVirtualDesktop() {
	MRDesktopWindow *window = TProgram::deskTop != nullptr ? dynamic_cast<MRDesktopWindow *>(TProgram::deskTop->current) : nullptr;

	if (window == nullptr || window->desktopIndex() <= 1) return false;
	window->setDesktopIndex(window->desktopIndex() - 1);
	syncVirtualDesktopVisibility();
	return true;
}

bool viewportRight() {
	int maxVd = mrVirtualDesktopCountSnapshot();
	if (g_currentVirtualDesktop >= maxVd) {
		if (mrCyclicVirtualDesktopsSnapshot() && maxVd > 1) {
			setCurrentVirtualDesktop(1);
			return true;
		}
		return false;
	}
	setCurrentVirtualDesktop(g_currentVirtualDesktop + 1);
	return true;
}

bool viewportLeft() {
	int maxVd = mrVirtualDesktopCountSnapshot();
	if (g_currentVirtualDesktop <= 1) {
		if (mrCyclicVirtualDesktopsSnapshot() && maxVd > 1) {
			setCurrentVirtualDesktop(maxVd);
			return true;
		}
		return false;
	}
	setCurrentVirtualDesktop(g_currentVirtualDesktop - 1);
	return true;
}

void mrSaveWorkspace(const std::string &filename) {
	std::string settingsPath = filename;
	MRSetupPaths paths;
	std::string dest;
	std::string source;
	if (settingsPath.empty()) {
		settingsPath = configuredSettingsMacroFilePath();
	}
	dest = settingsPath;
	if (dest.find(".mrmac") == std::string::npos) {
		dest += ".mrmac";
	}
	paths.settingsMacroUri = dest;
	paths.macroPath = defaultMacroDirectoryPath();
	paths.helpUri = configuredHelpFilePath();
	paths.tempPath = configuredTempDirectoryPath();
	paths.shellUri = configuredShellExecutablePath();
	source = buildSettingsMacroSourceWithWorkspace(paths);
	writeTextFile(dest, source);
}

bool mrLoadWorkspaceWithDialog() {
	char fileName[MAXPATH];
	std::string selectedPath;
	bool readable = false;

	mr::dialogs::seedFileDialogPath(MRDialogHistoryScope::WorkspaceLoad, fileName, sizeof(fileName), "*.mrmac");
	if (mr::dialogs::execRememberingFileDialogWithData(MRDialogHistoryScope::WorkspaceLoad, "*.mrmac", "LOAD WORKSPACE FROM", "~N~ame", fdOpenButton, fileName) == cmCancel) return false;

	selectedPath = normalizeConfiguredPathInput(fileName);
	if (selectedPath.empty()) return false;

	readable = ::access(selectedPath.c_str(), F_OK) == 0 && ::access(selectedPath.c_str(), R_OK) == 0;
	mrLoadWorkspace(selectedPath);
	if (readable) rememberLoadDialogPath(MRDialogHistoryScope::WorkspaceLoad, selectedPath.c_str());
	else
		forgetLoadDialogPath(MRDialogHistoryScope::WorkspaceLoad, selectedPath.c_str());
	return true;
}

void mrLoadWorkspace(const std::string &filename) {
	const auto loadStartedAt = std::chrono::steady_clock::now();
	std::string settingsPath = filename;
	long long readUs = 0;
	long long parseLoopUs = 0;
	long long visibilityUs = 0;
	int totalWorkspaceEntries = 0;
	std::chrono::steady_clock::time_point lastWorkspaceProgressAt = std::chrono::steady_clock::time_point::min();
	const ushort restoreCursorLines = TScreen::cursorLines;
	bool cursorSuppressedForRestore = false;

	if (settingsPath.empty()) {
		settingsPath = configuredSettingsMacroFilePath();
	}
	std::string dest = settingsPath;
	if (dest.find(".mrmac") == std::string::npos) {
		dest += ".mrmac";
	}

	std::string currentContent;
	std::string errorText;
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		if (!readTextFile(dest, currentContent, errorText)) {
			mrLogMessage("Workspace load failed read path=" + dest + " error=" + errorText + ".");
			mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, "Unable to read workspace: " + workspaceDisplayName(dest), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return;
		}
		readUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	mrLogMessage("Workspace load begin path=" + dest + " bytes=" + std::to_string(currentContent.size()) + ".");
	totalWorkspaceEntries = countWorkspaceEntriesInSource(currentContent);
	g_workspaceRestoreInProgress = totalWorkspaceEntries > kWorkspaceRestoreProgressEntryThreshold;
	if (g_workspaceRestoreInProgress) {
		cursorSuppressedForRestore = true;
		TScreen::cursorLines = 0;
		TScreen::setCursorType(0);
		mr::messageline::clearOwner(mr::messageline::Owner::DialogInteraction);
		mr::messageline::clearOwner(mr::messageline::Owner::DialogValidation);
		mr::messageline::clearOwner(mr::messageline::Owner::MacroMessage);
		mr::messageline::clearOwner(mr::messageline::Owner::MacroMarquee);
		mr::messageline::clearOwner(mr::messageline::Owner::MacroBrain);
		mr::messageline::clearOwner(mr::messageline::Owner::HeroEventFollowup);
		hideAllEditorFrameHoverPopups();
		postWorkspaceRestoreProgress(0, totalWorkspaceEntries, true);
		lastWorkspaceProgressAt = std::chrono::steady_clock::now();
	}

	std::istringstream iss(currentContent);
	std::string line;
	bool discardedWorkspaceEntries = false;
	bool loadedMainFile = false;
	int parsedWorkspaceEntries = 0;
	int loadedWorkspaceEntries = 0;
	MRWindowOpenBatch openBatch;
	g_workspaceMainFilePath.clear();
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		openBatch.begin();
		while (std::getline(iss, line)) {
			const auto entryStartedAt = std::chrono::steady_clock::now();
			WorkspaceEntry entry;
			MREditWindow *win = nullptr;
			MRFileEditor *editor = nullptr;
			std::string err;
			int resolvedVirtualDesktop = 1;
			long long createUs = 0;
			long long fileLoadUs = 0;
			long long bentoUs = 0;
			long long geometryUs = 0;
			long long cursorUs = 0;

				if (!parseWorkspaceEntry(line, entry)) continue;
				++parsedWorkspaceEntries;
				if (g_workspaceRestoreInProgress) {
					const auto now = std::chrono::steady_clock::now();
					const bool shouldReportProgress = parsedWorkspaceEntries == 1 || parsedWorkspaceEntries == totalWorkspaceEntries || now - lastWorkspaceProgressAt >= std::chrono::milliseconds(1500);

				if (shouldReportProgress) {
					postWorkspaceRestoreProgress(parsedWorkspaceEntries, totalWorkspaceEntries, true);
					lastWorkspaceProgressAt = now;
				}
			}
			resolvedVirtualDesktop = workspaceVirtualDesktopOrRandom(entry.vd);

			if (entry.hasBentoSnapshot && entry.bentoSnapshot.mode == bbmFileCompare) {
				const auto subStartedAt = std::chrono::steady_clock::now();
				win = restoreFileCompareWorkspaceEntry(entry, resolvedVirtualDesktop);
				bentoUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - subStartedAt).count();
				editor = win != nullptr ? win->getEditor() : nullptr;
				if (win == nullptr || editor == nullptr) {
					mrLogMessage("Workspace load failed file-compare restore url=" + entry.url + ".");
					discardedWorkspaceEntries = true;
					continue;
				}
			} else {
				{
					const auto subStartedAt = std::chrono::steady_clock::now();
					win = entry.hasBentoSnapshot && MRBentoHexEditor::matchesWorkspaceSnapshot(entry.bentoSnapshot) ? static_cast<MREditWindow *>(openBatch.createHexEditorWindow(entry.url.c_str())) : openBatch.createEditorWindow(entry.url.c_str());
					createUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - subStartedAt).count();
				}
				editor = win != nullptr ? win->getEditor() : nullptr;
				{
					const auto subStartedAt = std::chrono::steady_clock::now();
					if (win == nullptr || editor == nullptr || !editor->loadMappedFile(entry.url.c_str(), err)) {
						fileLoadUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - subStartedAt).count();
						mrLogMessage("Workspace load failed file url=" + entry.url + " window=" + (win != nullptr ? "1" : "0") + " editor=" + (editor != nullptr ? "1" : "0") + " error=" + err + ".");
						if (win != nullptr) message(win, evCommand, cmClose, nullptr);
						discardedWorkspaceEntries = true;
						continue;
					}
					fileLoadUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - subStartedAt).count();
				}
			}
			if (!entry.hasBentoSnapshot || entry.bentoSnapshot.mode != bbmFileCompare) {
				win->mVirtualDesktop = resolvedVirtualDesktop;
				{
					const auto subStartedAt = std::chrono::steady_clock::now();
					applyWorkspaceEntryGeometry(win, entry);
					geometryUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - subStartedAt).count();
				}
			}
			if (entry.hasBentoSnapshot && entry.bentoSnapshot.mode != bbmFileCompare) {
				const auto subStartedAt = std::chrono::steady_clock::now();
				MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(win);
				if (bentoBox == nullptr || !bentoBox->restoreWorkspaceSnapshot(entry.bentoSnapshot)) {
					bentoUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - subStartedAt).count();
					mrLogMessage("Workspace load failed Bento snapshot restore url=" + entry.url + " bento_window=" + (bentoBox != nullptr ? "1" : "0") + ".");
					message(win, evCommand, cmClose, nullptr);
					discardedWorkspaceEntries = true;
					continue;
				}
				bentoUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - subStartedAt).count();
			}
			if (entry.hasMacroDebuggerConfiguration) {
				MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(win);
				MREditWindow *debuggerOutput = nullptr;
				MREditWindow *variables = nullptr;
				MREditWindow *watches = nullptr;

				if (bentoBox == nullptr || !bentoBox->ensureMacroDebuggerPanes(debuggerOutput, variables, watches)) {
					mrLogMessage("Workspace load failed debugger Bento restore url=" + entry.url + ".");
					message(win, evCommand, cmClose, nullptr);
					discardedWorkspaceEntries = true;
					continue;
				}
				bentoBox->restoreMacroDebuggerWorkspaceConfiguration(entry.macroDebuggerConfiguration);
			}
			{
			const auto subStartedAt = std::chrono::steady_clock::now();
			restoreEditorCursor(editor, entry.line, entry.column);
			if (MRBentoHexEditor *hexEditor = dynamic_cast<MRBentoHexEditor *>(win); hexEditor != nullptr) hexEditor->synchronizeByteCursorFromDocument();
			cursorUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - subStartedAt).count();
			}
			if (entry.mainFile && !loadedMainFile) {
				static_cast<void>(mrSetWorkspaceMainFile(win));
				loadedMainFile = true;
			}
			++loadedWorkspaceEntries;
			{
				const long long entryUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - entryStartedAt).count();
				std::ostringstream detail;

				detail << "entry=" << loadedWorkspaceEntries << " parsed=" << parsedWorkspaceEntries << " create_us=" << createUs << " file_us=" << fileLoadUs << " bento_us=" << bentoUs << " geometry_us=" << geometryUs << " cursor_us=" << cursorUs << " min=" << (entry.minimized ? 1 : 0) << " url=\"" << entry.url << "\"";
				if (entryUs >= 250000) logWindowTiming("Workspace load entry slow", entryUs, detail.str());
			}
		}
		openBatch.finish(false, false);
		parseLoopUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
		}
		g_workspaceRestoreInProgress = false;
		if (totalWorkspaceEntries > kWorkspaceRestoreProgressEntryThreshold) hideAllEditorFrameHoverPopups();
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		syncVirtualDesktopVisibility();
		if (loadedWorkspaceEntries != 0) mrNotifyWindowTopologyChanged();
		visibilityUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	mrLogMessage("Workspace load summary path=" + dest + " parsed=" + std::to_string(parsedWorkspaceEntries) + " loaded=" + std::to_string(loadedWorkspaceEntries) + " discarded=" + (discardedWorkspaceEntries ? "1" : "0") + ".");
	{
		std::ostringstream detail;
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - loadStartedAt).count();

		detail << "read_us=" << readUs << " parse_loop_us=" << parseLoopUs << " visibility_us=" << visibilityUs << " parsed=" << parsedWorkspaceEntries << " loaded=" << loadedWorkspaceEntries << " bytes=" << currentContent.size() << " path=\"" << dest << "\"";
		logWindowTiming("Workspace load total timing", tookUs, detail.str());
		}
		if (parsedWorkspaceEntries != 0 && loadedWorkspaceEntries == 0) {
			setRuntimePreserveAutosavedWorkspace(true);
			mrLogMessage("Workspace load restored no entries; autosaved workspace was left preserved.");
		}
		if (discardedWorkspaceEntries) mrLogMessage("Workspace load skipped one or more invalid entries; source was left unchanged.");
		if (totalWorkspaceEntries > kWorkspaceRestoreProgressEntryThreshold) {
			mr::messageline::clearOwner(mr::messageline::Owner::HeroEvent);
			drawWorkspaceMessageLineNow(std::string(), MRMenuBar::MarqueeKind::Hero);
		}
	if (cursorSuppressedForRestore) {
		TScreen::cursorLines = restoreCursorLines;
		TScreen::setCursorType(restoreCursorLines);
		if (TProgram::deskTop != nullptr) TProgram::deskTop->resetCursor();
	}
}

MREditWindow *createEditorWindow(const char *title) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRBentoBox(bounds, title, nextEditorWindowNumber(), bbmDocumentViewports);
	finishNewEditWindow(win);
	return win;
}

MRBentoHexEditor *createHexEditorWindow(const char *title) {
	return createHexEditorWindowWithNumber(title, nextEditorWindowNumber(), true);
}

MREditWindow *createHelpWindow(const char *title) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRHelpWindow(bounds, title, nextEditorWindowNumber());
	finishNewEditWindow(win);
	return win;
}

MREditWindow *createLogWindow(const char *title) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRLogWindow(bounds, title, nextEditorWindowNumber());
	finishNewEditWindow(win, false);
	return win;
}

MREditWindow *createCommunicationWindow(const char *title) {
	TRect bounds;
	MREditWindow *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRCommunicationWindow(bounds, title, nextEditorWindowNumber());
	finishNewEditWindow(win);
	return win;
}

MRBentoBox *createBentoBoxWindow(const char *title) {
	TRect bounds;
	MRBentoBox *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRBentoBox(bounds, title, nextEditorWindowNumber());
	finishNewEditWindow(win);
	return win;
}

MRBentoBox *createFileCompareBentoBoxWindow(const char *title) {
	TRect bounds;
	MRBentoBox *win;

	if (TProgram::deskTop == nullptr) return nullptr;
	bounds = MRWindowLayout::usableDesktopBounds();
	bounds.grow(-2, -1);
	win = new MRBentoBox(bounds, title, nextEditorWindowNumber(), bbmFileCompare);
	finishNewEditWindow(win);
	return win;
}

MRBentoBox *convertEditWindowToBentoBox(MREditWindow *source) {
	MRBentoBox *existingBento;
	MRBentoBox *win;
	TRect bounds;
	const char *title;
	MREditWindow::WindowRole role;
	std::string roleDetail;
	bool readOnly;
	bool changed;
	bool insertMode;
	int virtualDesktop;
	short windowNumber;

	if (source == nullptr || TProgram::deskTop == nullptr || source->getEditor() == nullptr) return nullptr;
	existingBento = dynamic_cast<MRBentoBox *>(source);
	if (existingBento != nullptr) return existingBento;
	if (!source->allowsDocumentViewportSplit()) return nullptr;
	if (source->hasTrackedExternalIoTasks()) return nullptr;

	bounds = source->getBounds();
	title = source->getTitle(0);
	role = source->windowRole();
	roleDetail = source->windowRoleDetail();
	readOnly = source->isReadOnly();
	changed = source->isFileChanged();
	insertMode = source->insertModeEnabled();
	virtualDesktop = source->mVirtualDesktop;
	windowNumber = source->number;

	win = new MRBentoBox(bounds, title != nullptr && *title != '\0' ? title : "Untitled", windowNumber, bbmDocumentViewports);
	TProgram::deskTop->insert(win);
	if (win == nullptr) return nullptr;
	win->mVirtualDesktop = virtualDesktop;
	win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
	if (win->getEditor() != nullptr) {
		win->getEditor()->shareContentStateFrom(*source->getEditor());
		win->getEditor()->setInsertModeEnabled(insertMode);
	}
	win->setWindowRole(role, roleDetail);
	win->setReadOnly(readOnly);
	if (source->currentFileName()[0] == '\0') win->setDisplayTitle(title);
	win->setFileChanged(changed);
	win->activatePrimaryPane();

	source->getEditor()->detachContentStateCopy();
	source->setFileChanged(false);
	setWindowManuallyHidden(source, false);
	static_cast<void>(mrActivateEditWindow(win));
	mrNotifyWindowTopologyChanged();
	postDeferredWindowClose(*source);
	return win;
}

MRBentoHexEditor *convertEditWindowToHexEditor(MREditWindow *source) {
	MRBentoHexEditor *existingHexEditor;
	MRBentoHexEditor *win;
	TRect bounds;
	const char *title;
	MREditWindow::WindowRole role;
	std::string roleDetail;
	bool readOnly;
	bool changed;
	bool insertMode;
	int virtualDesktop;
	short windowNumber;

	if (source == nullptr || TProgram::deskTop == nullptr || source->getEditor() == nullptr) return nullptr;
	existingHexEditor = dynamic_cast<MRBentoHexEditor *>(source);
	if (existingHexEditor != nullptr) return existingHexEditor;
	if (!source->allowsDocumentViewportSplit() || source->hasTrackedExternalIoTasks()) return nullptr;

	bounds = source->getBounds();
	title = source->getTitle(0);
	role = source->windowRole();
	roleDetail = source->windowRoleDetail();
	readOnly = source->isReadOnly();
	changed = source->isFileChanged();
	insertMode = source->insertModeEnabled();
	virtualDesktop = source->mVirtualDesktop;
	windowNumber = source->number;

	win = new MRBentoHexEditor(bounds, title != nullptr && *title != '\0' ? title : "Untitled", windowNumber);
	TProgram::deskTop->insert(win);
	win->mVirtualDesktop = virtualDesktop;
	win->flags |= (wfMove | wfGrow | wfZoom | wfClose);
	if (win->getEditor() != nullptr) {
		win->getEditor()->shareContentStateFrom(*source->getEditor());
		win->getEditor()->setInsertModeEnabled(insertMode);
		win->synchronizePaneDocumentState();
	}
	win->setWindowRole(role, roleDetail);
	win->setReadOnly(readOnly);
	if (source->currentFileName()[0] == '\0') win->setDisplayTitle(title);
	win->setFileChanged(changed);
	win->activatePrimaryPane();

	source->getEditor()->detachContentStateCopy();
	source->setFileChanged(false);
	setWindowManuallyHidden(source, false);
	static_cast<void>(mrActivateEditWindow(win));
	mrNotifyWindowTopologyChanged();
	postDeferredWindowClose(*source);
	return win;
}

bool mrDispatchDeferredWindowClose(MREditWindow *window) {
	for (MREditWindow *candidate : allEditWindowsInZOrder()) {
		if (candidate != window) continue;
		candidate->close();
		return true;
	}
	return false;
}

MREditWindow *currentEditWindow() {
	if (TProgram::deskTop == nullptr || TProgram::deskTop->current == nullptr) return nullptr;
	return dynamic_cast<MREditWindow *>(TProgram::deskTop->current);
}

MREditWindow *currentEditorCommandWindow() {
	MREditWindow *window = currentEditWindow();

	return window != nullptr ? window->editorCommandTarget() : nullptr;
}

MREditWindow *findEditWindowByBufferId(int bufferId) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	for (auto &window : windows) {
		MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window);

		if (window != nullptr && window->bufferId() == bufferId) return window;
		if (bentoBox != nullptr) {
			MREditWindow *pane = bentoBox->paneForBufferId(bufferId);
			if (pane != nullptr) return pane;
		}
	}
	return nullptr;
}

bool isEmptyUntitledEditableWindow(MREditWindow *win) {
	if (win == nullptr || win->isReadOnly() || win->currentFileName()[0] != '\0' || win->isFileChanged()) return false;
	return win->isBufferEmpty();
}

MREditWindow *findReusableEmptyWindow(MREditWindow *preferred) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	if (preferred != nullptr && isEmptyUntitledEditableWindow(preferred)) return preferred;
	for (auto &window : windows)
		if (isEmptyUntitledEditableWindow(window)) return window;
	return nullptr;
}

bool closeCurrentEditWindow() {
	MREditWindow *win = currentEditWindow();
	if (win == nullptr) return false;
	setWindowManuallyHidden(win, false);
	message(win, evCommand, cmClose, nullptr);
	return mrEnsureUsableWorkWindow(false) || currentEditWindow() == nullptr;
}

bool activateRelativeEditWindow(int delta) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	MREditWindow *current = currentEditWindow();
	std::size_t index;

	if (windows.empty()) return false;
	if (current == nullptr) return mrActivateEditWindow(windows.front());

	for (index = 0; index < windows.size(); ++index) {
		if (windows[index] == current) {
			int nextIndex = static_cast<int>(index) + delta;
			int count = static_cast<int>(windows.size());

			while (nextIndex < 0)
				nextIndex += count;
			nextIndex %= count;
			return mrActivateEditWindow(windows[static_cast<std::size_t>(nextIndex)]);
		}
	}
	return mrActivateEditWindow(windows.front());
}

bool activateRelativeDesktopWindow(int delta) {
	std::vector<MRDesktopWindow *> windows = allDesktopWindowsInZOrder();
	MRDesktopWindow *current = currentDesktopWindow();
	std::size_t index = 0;

	if (windows.empty()) return false;
	if (current == nullptr) current = windows.front();
	for (; index < windows.size(); ++index)
		if (windows[index] == current) break;
	if (index == windows.size()) index = 0;
	int nextIndex = static_cast<int>(index) + delta;
	const int count = static_cast<int>(windows.size());

	while (nextIndex < 0)
		nextIndex += count;
	nextIndex %= count;
	MRDesktopWindow *target = windows[static_cast<std::size_t>(nextIndex)];
	TWindow *nativeWindow = target != nullptr ? target->desktopNativeWindow() : nullptr;

	if (target == nullptr || nativeWindow == nullptr) return false;
	if (target->desktopIndex() != currentVirtualDesktop()) setCurrentVirtualDesktop(target->desktopIndex());
	if (target->desktopManuallyHidden()) {
		target->setDesktopManuallyHidden(false);
		mrNotifyWindowTopologyChanged();
	}
	syncVirtualDesktopVisibility();
	nativeWindow->select();
	return true;
}

bool hideCurrentEditWindow() {
	MREditWindow *win = currentEditWindow();
	if (win == nullptr) return false;
	return hideCurrentDesktopWindow();
}

bool hideCurrentDesktopWindow() {
	MRDesktopWindow *window = currentDesktopWindow();
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;

	if (nativeWindow == nullptr) return false;
	window->setDesktopManuallyHidden(true);
	nativeWindow->hide();
	mrNotifyWindowTopologyChanged();
	return mrEnsureUsableWorkWindow();
}

bool closeCurrentDesktopWindow() {
	MRDesktopWindow *window = currentDesktopWindow();
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
	MREditWindow *editWindow = dynamic_cast<MREditWindow *>(window);

	if (editWindow != nullptr) return closeCurrentEditWindow();
	if (nativeWindow == nullptr) return false;
	message(nativeWindow, evCommand, cmClose, nativeWindow);
	return true;
}

bool zoomCurrentDesktopWindow() {
	MRDesktopWindow *window = currentDesktopWindow();
	TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;

	if (nativeWindow == nullptr || (nativeWindow->flags & wfZoom) == 0) return false;
	message(nativeWindow, evCommand, cmZoom, nativeWindow);
	return true;
}

void mrUpdateAllWindowsColorTheme() {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	for (auto &window : windows) {
		if (window != nullptr) {
			window->applyWindowColorThemeForPath(window->currentFileName());
			if (MRBentoBox *bentoBox = dynamic_cast<MRBentoBox *>(window); bentoBox != nullptr) bentoBox->refreshBentoColorTheme();
		}
	}
}

void mrRefreshAllHexEditorProjections() {
	for (MREditWindow *window : allEditWindowsInZOrder()) {
		MRBentoHexEditor *hexEditor = dynamic_cast<MRBentoHexEditor *>(window);

		if (hexEditor != nullptr) hexEditor->refreshHexProjection();
	}
}

// ---- Consolidated from MRFileCommands.cpp ----

namespace {
[[nodiscard]] std::string normalizeTvPath(std::string_view path) {
	std::string result(path);

	for (char &ch : result)
		if (ch == '\\') ch = '/';
#ifdef __unix__
	if (result.size() >= 2 && ((result[0] >= 'A' && result[0] <= 'Z') || (result[0] >= 'a' && result[0] <= 'z')) && result[1] == ':') result.erase(0, 2);
#endif
	return result;
}

[[nodiscard]] std::string trimPathInput(std::string_view path) {
	std::size_t start = 0;
	std::size_t end = path.size();

	while (start < end && std::isspace(static_cast<unsigned char>(path[start])) != 0)
		++start;
	while (end > start && (std::isspace(static_cast<unsigned char>(path[end - 1])) != 0 || static_cast<unsigned char>(path[end - 1]) < 32))
		--end;

	std::string result(path.substr(start, end - start));
	if (result.size() >= 2 && ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\''))) result = result.substr(1, result.size() - 2);
	return result;
}

[[nodiscard]] std::string expandUserPath(std::string_view path) {
	std::string result;

	if (path.empty()) return std::string();
	result = normalizeTvPath(trimPathInput(path));
	if (result.size() >= 2 && result[0] == '~' && result[1] == '/') {
		const char *home = std::getenv("HOME");
		if (home != nullptr && *home != '\0') return std::string(home) + result.substr(1);
	}
	return result;
}

[[nodiscard]] bool hasWildcardPattern(std::string_view path) {
	return path.find('*') != std::string_view::npos || path.find('?') != std::string_view::npos;
}

[[nodiscard]] std::size_t lastPathSeparator(std::string_view path) {
	const std::size_t slash = path.find_last_of('/');
	const std::size_t backslash = path.find_last_of('\\');

	if (slash == std::string_view::npos) return backslash;
	if (backslash == std::string_view::npos) return slash;
	return std::max(slash, backslash);
}

[[nodiscard]] std::string baseNameForDisplay(const std::string &path) {
	const std::size_t sep = lastPathSeparator(path);

	if (sep == std::string::npos || sep + 1 >= path.size()) return path;
	return path.substr(sep + 1);
}

[[nodiscard]] long long roundedMilliseconds(double valueMs) {
	if (valueMs <= 0.0) return 0;
	return static_cast<long long>(valueMs + 0.5);
}

void postLoadHeroEvents(const std::string &resolvedPath, std::size_t bytes, double loadMs, std::size_t lineCount, bool lineCountExact, double lineCountMs) {
	const std::string fileName = baseNameForDisplay(resolvedPath);
	const std::string loadText = "loaded " + fileName + " in " + (roundedMilliseconds(loadMs) >= 1 ? std::to_string(roundedMilliseconds(loadMs)) : "<1") + " ms";
	std::string lineText;
	const std::chrono::milliseconds loadDuration = mr::messageline::autoDurationForText(loadText);

	if (lineCountExact)
		lineText = "indexed " + std::to_string(bytes) + " bytes, " + std::to_string(lineCount) + " lines, " + std::to_string(roundedMilliseconds(lineCountMs)) + " ms";
	else
		lineText = "mapped " + std::to_string(bytes) + " bytes, est. " + std::to_string(lineCount) + " lines, index warming";
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, loadText, mr::messageline::Kind::Success, mr::messageline::kPriorityHigh);
	mr::messageline::postAutoTimedAfter(mr::messageline::Owner::HeroEventFollowup, lineText, mr::messageline::Kind::Info, loadDuration, mr::messageline::kPriorityLow);
}

[[nodiscard]] bool hasExtensionInBaseName(std::string_view path) {
	const std::size_t sep = lastPathSeparator(path);
	const std::size_t dot = path.find_last_of('.');

	return dot != std::string_view::npos && (sep == std::string_view::npos || dot > sep);
}

[[nodiscard]] bool resolveWithConfiguredExtensions(const std::string &basePath, std::string &resolvedPath) {
	const std::vector<std::string> extensions = configuredDefaultExtensionList();
	std::set<std::string> tried;

	for (const std::string &ext : extensions) {
		std::array<std::string, 3> candidates = {ext, ext, ext};

		if (ext.empty()) continue;
		for (std::size_t p = 0; p < ext.size(); ++p) {
			candidates[1][p] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[p])));
			candidates[2][p] = static_cast<char>(std::toupper(static_cast<unsigned char>(ext[p])));
		}

		for (const std::string &candidateExt : candidates) {
			std::string candidate = basePath + "." + candidateExt;
			if (!tried.insert(candidate).second) continue;
			if (::access(candidate.c_str(), F_OK) == 0 && ::access(candidate.c_str(), R_OK) == 0) {
				resolvedPath = candidate;
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] bool shouldAutoOpenHexEditor(const std::string &resolvedPath) {
	return configuredAutoDetectBinaryFiles() && fileContainsNulInBoundarySamples(resolvedPath);
}

MREditWindow *createFileLoadWindow(MRWindowOpenBatch &openBatch, bool useBatch, bool useHexEditor) {
	if (useBatch) return useHexEditor ? static_cast<MREditWindow *>(openBatch.createHexEditorWindow("?No-File?")) : openBatch.createEditorWindow("?No-File?");
	return useHexEditor ? static_cast<MREditWindow *>(createHexEditorWindow("?No-File?")) : createEditorWindow("?No-File?");
}

MREditWindow *applyAutoDetectedHexEditor(MREditWindow *window, bool useHexEditor) {
	MRBentoHexEditor *hexEditor = nullptr;

	if (!useHexEditor || window == nullptr) return window;
	hexEditor = dynamic_cast<MRBentoHexEditor *>(window);
	if (hexEditor != nullptr) return hexEditor;
	hexEditor = convertEditWindowToHexEditor(window);
	if (hexEditor != nullptr) return hexEditor;
	postWindowCommandError("Could not activate Hex editor for detected binary file.");
	return window;
}
} // namespace

bool promptForPath(MRDialogHistoryScope scope, const char *title, char *fileName, std::size_t fileNameSize) {
	ushort result = cmCancel;

	if (fileName == nullptr || fileNameSize == 0) return false;
	fileName[0] = '\0';
	mr::dialogs::seedFileDialogPath(scope, fileName, fileNameSize, "*.*");
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.*", title, "~N~ame", fdOpenButton, fileName);
	if (result == cmCancel) return false;
	return true;
}

bool promptForPath(const char *title, char *fileName, std::size_t fileNameSize) {
	const MRDialogHistoryScope scope = std::string_view(title != nullptr ? title : "") == "LOAD FILE" ? MRDialogHistoryScope::LoadFile : MRDialogHistoryScope::OpenFile;
	return promptForPath(scope, title, fileName, fileNameSize);
}

bool promptForSaveAsPath(const char *title, const char *initialPath, std::string &outResolvedPath) {
	char fileName[MAXPATH] = {0};
	ushort result = cmCancel;
	MRDialogHistoryScope scope = MRDialogHistoryScope::EditorSaveAs;

	outResolvedPath.clear();
	if (std::string_view(title != nullptr ? title : "") == "SAVE LOG AS") scope = MRDialogHistoryScope::SaveLogAs;
	mr::dialogs::seedFileDialogPath(scope, fileName, sizeof(fileName), "*.*");
	mr::dialogs::suggestFileDialogName(fileName, sizeof(fileName), initialPath != nullptr ? std::string_view(initialPath) : std::string_view());
	result = mr::dialogs::execRememberingFileDialogWithData(scope, "*.*", title, "~N~ame", fdOKButton, fileName);
	if (result == cmCancel) return false;
	outResolvedPath = expandUserPath(fileName);
	if (outResolvedPath.empty()) {
		postWindowCommandError("No file name specified.");
		return false;
	}
	if (hasWildcardPattern(outResolvedPath)) {
		postWindowCommandError("Wildcards are not allowed in save file names.");
		return false;
	}
	rememberLoadDialogPath(scope, outResolvedPath.c_str());
	return true;
}

bool saveWindowSnapshotToPath(MREditWindow *win, const std::string &resolvedPath) {
	std::ofstream outFile;
	std::string text;
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;

	if (win == nullptr || editor == nullptr || resolvedPath.empty()) return false;
	text = editor->snapshotText();
	outFile.open(resolvedPath.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outFile.is_open()) return false;
	outFile.write(text.data(), static_cast<std::streamsize>(text.size()));
	outFile.close();
	return outFile.good();
}

bool resolveReadableExistingPath(MRDialogHistoryScope scope, const char *path, std::string &resolvedPath, bool reportErrors) {
	bool disableExtensionSearch = false;
	std::string rawInput = expandUserPath(path != nullptr ? std::string_view(path) : std::string_view());

	static_cast<void>(scope);
	resolvedPath = rawInput;
	if (!resolvedPath.empty() && resolvedPath.back() == '.' && !hasWildcardPattern(resolvedPath)) {
		disableExtensionSearch = true;
		resolvedPath.pop_back();
	}
	if (resolvedPath.empty()) {
		if (reportErrors) postWindowCommandError("No file name specified.");
		return false;
	}
	if (::access(resolvedPath.c_str(), F_OK) != 0 && !disableExtensionSearch && !hasWildcardPattern(resolvedPath) && !hasExtensionInBaseName(resolvedPath)) static_cast<void>(resolveWithConfiguredExtensions(resolvedPath, resolvedPath));
	if (access(resolvedPath.c_str(), F_OK) != 0) {
		if (reportErrors) postWindowCommandError("File does not exist: " + resolvedPath);
		return false;
	}
	if (access(resolvedPath.c_str(), R_OK) != 0) {
		if (reportErrors) postWindowCommandError("File is not readable: " + resolvedPath);
		return false;
	}
	return true;
}

bool loadResolvedFileIntoWindow(MREditWindow *win, const std::string &resolvedPath, const char *operationLabel) {
	const auto fallbackLoadStartedAt = std::chrono::steady_clock::now();
	if (win == nullptr) return false;
	if (!win->loadFromFile(resolvedPath.c_str())) {
		postWindowCommandError("Unable to load file: " + resolvedPath);
		return false;
	}
	if (MRBentoHexEditor *hexEditor = dynamic_cast<MRBentoHexEditor *>(win); hexEditor != nullptr) {
		hexEditor->synchronizePaneDocumentState();
	}
	const MRFileEditor::LoadTiming timing = win->lastLoadTiming();
	std::size_t bytes = win->bufferLength();
	std::size_t lines = 0;
	bool linesExact = false;
	double loadMs = 0.0;
	double lineCountMs = 0.0;

	if (timing.valid) {
		bytes = timing.bytes;
		lines = timing.lines;
		linesExact = timing.linesExact;
		loadMs = timing.mappedLoadMs;
		lineCountMs = timing.lineCountMs;
	} else {
		loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fallbackLoadStartedAt).count();
		if (win->exactLineCountKnown()) {
			const auto lineCountStartedAt = std::chrono::steady_clock::now();
			lines = win->bufferLineCount();
			lineCountMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lineCountStartedAt).count();
			linesExact = true;
		} else
			lines = win->estimatedLineCount();
	}

	mr::performance::recordUiEvent(operationLabel != nullptr ? operationLabel : "Load file", static_cast<std::size_t>(win->bufferId()), win->documentId(), bytes, loadMs, resolvedPath);
	mr::performance::recordUiEvent("Line count", static_cast<std::size_t>(win->bufferId()), win->documentId(), bytes, lineCountMs, resolvedPath);
	postLoadHeroEvents(resolvedPath, bytes, loadMs, lines, linesExact, lineCountMs);
	return true;
}

bool openResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow) {
	MREditWindow *current = currentEditWindow();
	MREditWindow *previousActive = restoreWindow != nullptr ? restoreWindow : current;
	MREditWindow *lastLoadedWindow = nullptr;
	MRWindowOpenBatch openBatch;
	const bool useBatch = resolvedPaths.size() > 1;

	for (const std::string &resolvedPath : resolvedPaths) {
		const bool useHexEditor = shouldAutoOpenHexEditor(resolvedPath);
		MREditWindow *target = findReusableEmptyWindow(current);
		bool createdTarget = false;

		if (target == nullptr) {
			target = createFileLoadWindow(openBatch, useBatch, useHexEditor);
			createdTarget = true;
		}
		if (target == nullptr) continue;
		if (!loadResolvedFileIntoWindow(target, resolvedPath, "Open file")) {
			forgetLoadDialogPath(MRDialogHistoryScope::OpenFile, resolvedPath.c_str());
			if (createdTarget) message(target, evCommand, cmClose, nullptr);
			if (target != nullptr && isEmptyUntitledEditableWindow(target) && current != target && current != nullptr) static_cast<void>(mrActivateEditWindow(current));
			continue;
		}
		MREditWindow *loadedTarget = applyAutoDetectedHexEditor(target, useHexEditor);

		if (previousActive == target) previousActive = loadedTarget;
		if (current == target) current = loadedTarget;
		target = loadedTarget;
		rememberLoadDialogPath(MRDialogHistoryScope::OpenFile, resolvedPath.c_str());
		lastLoadedWindow = target;
		current = target;
	}
	if (openBatch.active()) openBatch.finish(true, lastLoadedWindow != nullptr);
	if (lastLoadedWindow != nullptr) {
		if (activation == MRLoadedWindowActivation::ActivateLast) static_cast<void>(mrActivateEditWindow(lastLoadedWindow));
		else if (previousActive != nullptr && previousActive != lastLoadedWindow)
			static_cast<void>(mrActivateEditWindow(previousActive));
	}
	return lastLoadedWindow != nullptr;
}

bool loadResolvedFilesIntoWindows(const std::vector<std::string> &resolvedPaths, MRLoadedWindowActivation activation, MREditWindow *restoreWindow) {
	MREditWindow *target = currentEditWindow();
	MREditWindow *previousActive = restoreWindow != nullptr ? restoreWindow : target;
	MREditWindow *lastLoadedWindow = nullptr;
	bool createdTarget = false;
	bool first = true;
	MRWindowOpenBatch openBatch;
	const bool useBatch = resolvedPaths.size() > 1;

	if (resolvedPaths.empty()) return false;
	if (target != nullptr && !target->confirmAbandonForReload())
		return false;
	for (const std::string &resolvedPath : resolvedPaths) {
		const bool useHexEditor = shouldAutoOpenHexEditor(resolvedPath);
		MREditWindow *loadTarget = first ? target : nullptr;
		bool createdLoadTarget = false;

		if (first && loadTarget == nullptr) {
			loadTarget = createFileLoadWindow(openBatch, useBatch, useHexEditor);
			createdTarget = true;
			createdLoadTarget = true;
			target = loadTarget;
		} else if (!first) {
			loadTarget = findReusableEmptyWindow(nullptr);
			if (loadTarget == nullptr) {
				loadTarget = createFileLoadWindow(openBatch, useBatch, useHexEditor);
				createdLoadTarget = true;
			}
		}
		if (loadTarget == nullptr) {
			first = false;
			continue;
		}
		if (!loadResolvedFileIntoWindow(loadTarget, resolvedPath, "Load file")) {
			forgetLoadDialogPath(MRDialogHistoryScope::LoadFile, resolvedPath.c_str());
			if (createdLoadTarget || (first && createdTarget)) message(loadTarget, evCommand, cmClose, nullptr);
			if (first && createdTarget) target = nullptr;
			first = false;
			continue;
		}
		MREditWindow *loadedTarget = applyAutoDetectedHexEditor(loadTarget, useHexEditor);

		if (previousActive == loadTarget) previousActive = loadedTarget;
		if (first) target = loadedTarget;
		loadTarget = loadedTarget;
		rememberLoadDialogPath(MRDialogHistoryScope::LoadFile, resolvedPath.c_str());
		lastLoadedWindow = loadTarget;
		first = false;
	}
	if (openBatch.active()) openBatch.finish(true, lastLoadedWindow != nullptr);
	if (lastLoadedWindow != nullptr) {
		if (activation == MRLoadedWindowActivation::ActivateLast) static_cast<void>(mrActivateEditWindow(lastLoadedWindow));
		else if (previousActive != nullptr && previousActive != lastLoadedWindow)
			static_cast<void>(mrActivateEditWindow(previousActive));
	}
	return lastLoadedWindow != nullptr;
}

bool saveEditWindowAs(MREditWindow *win) {
	std::string resolvedPath;
	bool isLogWindow = false;
	const char *initialPath = nullptr;

	if (win == nullptr) return false;
	if (win->isReadOnly()) {
		isLogWindow = win->windowRole() == MREditWindow::wrLog;
		if (!isLogWindow) {
			messageBox(mfInformation | mfOKButton, "Window is read-only.");
			mrLogMessage("Save As rejected for read-only window.");
			return false;
		}
		initialPath = nullptr;
		if (!win->windowRoleDetail().empty()) initialPath = win->windowRoleDetail().c_str();
		if (!promptForSaveAsPath("SAVE LOG AS", initialPath, resolvedPath)) return false;
		auto startedAt = std::chrono::steady_clock::now();
		if (!saveWindowSnapshotToPath(win, resolvedPath)) {
			postWindowCommandError("Unable to save log file: " + resolvedPath);
			mrLogMessage("Save As failed.");
			return false;
		}
		mr::performance::recordUiEvent("Save log as", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(), resolvedPath);
		win->setWindowRole(MREditWindow::wrLog, resolvedPath);
		mrLogMessage("Log window saved as a new file.");
		return true;
	}
	auto startedAt = std::chrono::steady_clock::now();
	MREditWindow *previousActive = currentEditWindow();
	if (previousActive != win) static_cast<void>(mrActivateEditWindow(win));
	if (!win->saveCurrentFileAs()) {
		if (previousActive != nullptr && previousActive != win) static_cast<void>(mrActivateEditWindow(previousActive));
		mrLogMessage("Save As failed.");
		return false;
	}
	if (previousActive != nullptr && previousActive != win) static_cast<void>(mrActivateEditWindow(previousActive));
	mr::performance::recordUiEvent("Save file as", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(), win->currentFileName());
	mrLogMessage("Window saved as a new file.");
	return true;
}

bool saveAllDirtyEditWindows() {
	std::vector<MREditWindow *> dirtyWindows;
	std::size_t savedCount = 0;

	for (MREditWindow *win : allEditWindowsInZOrder()) {
		if (win == nullptr || !win->isFileChanged() || win->isReadOnly()) continue;
		dirtyWindows.push_back(win);
	}
	if (dirtyWindows.empty()) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, "No dirty windows to save.", mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
		return true;
	}
	for (MREditWindow *win : dirtyWindows) {
		if (win == nullptr || !win->isFileChanged() || win->isReadOnly()) continue;
		if (win->canSaveInPlace()) {
			auto startedAt = std::chrono::steady_clock::now();
			if (!win->saveCurrentFile()) {
				postWindowCommandError("Save all stopped: save failed.");
				mrLogMessage("Save all failed.");
				return false;
			}
			mr::performance::recordUiEvent("Save file", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(), win->currentFileName());
			++savedCount;
			continue;
		}
		if (!saveEditWindowAs(win)) {
			postWindowCommandError("Save all cancelled.");
			mrLogMessage("Save all cancelled.");
			return false;
		}
		++savedCount;
	}
	{
		std::ostringstream line;
		line << "Saved " << savedCount << " dirty window";
		if (savedCount != 1) line << "s";
		line << ".";
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, line.str(), mr::messageline::Kind::Info, mr::messageline::kPriorityMedium);
		mrLogMessage(line.str());
	}
	return true;
}

bool revertEditWindow(MREditWindow *win) {
	std::string path;

	if (win == nullptr) return false;
	path = win->currentFileName();
	if (path.empty()) {
		postWindowCommandError("No saved file to revert to.");
		return false;
	}
	if (win->isFileChanged() && messageBox(mfConfirmation | mfYesButton | mfNoButton, "Revert window and discard changes?\n%s", path.c_str()) != cmYes) return false;
	auto startedAt = std::chrono::steady_clock::now();
	if (!win->loadFromFile(path.c_str())) {
		postWindowCommandError("Unable to revert file: " + path);
		mrLogMessage("Revert failed.");
		return false;
	}
	mr::performance::recordUiEvent("Revert file", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(), path);
	mrLogMessage("Window reverted.");
	return true;
}

bool saveCurrentEditWindow() {
	MREditWindow *win = currentEditWindow();

	if (win == nullptr) return false;
	if (win->isReadOnly()) {
		messageBox(mfInformation | mfOKButton, "Window is read-only.");
		mrLogMessage("Save rejected for read-only window.");
		return false;
	}
	if (!win->isFileChanged()) return true;
	if (win->canSaveInPlace()) {
		auto startedAt = std::chrono::steady_clock::now();
		if (!win->saveCurrentFile()) {
			mrLogMessage("Save failed.");
			return false;
		}
		mr::performance::recordUiEvent("Save file", static_cast<std::size_t>(win->bufferId()), win->documentId(), win->bufferLength(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count(), win->currentFileName());
		mrLogMessage("Window saved.");
		return true;
	}
	return saveEditWindowAs(win);
}

bool saveCurrentEditWindowAs() {
	MREditWindow *win = currentEditWindow();
	return saveEditWindowAs(win);
}

bool handleWindowCascade() {
	const auto startedAt = std::chrono::steady_clock::now();
	const auto enumerateStartedAt = startedAt;
	std::vector<MRDesktopWindow *> allWindows = allDesktopWindowsInZOrder();
	std::vector<MRDesktopWindow *> visibleWindows;
	TRect desktopBounds;
	long long enumerateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - enumerateStartedAt).count();
	long long filterUs = 0;
	long long locateUs = 0;

	if (TProgram::deskTop == nullptr) return false;

	desktopBounds = MRWindowLayout::usableDesktopBounds();

	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		for (auto it = allWindows.rbegin(); it != allWindows.rend(); ++it) {
			MRDesktopWindow *window = *it;
			TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
			if (nativeWindow != nullptr && (nativeWindow->options & ofTileable) != 0 && (nativeWindow->state & sfVisible) != 0 && !window->desktopMinimized()) {
				visibleWindows.push_back(window);
			}
		}
		filterUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}

	if (visibleWindows.empty()) return true;

	int cascadeIndex = 0;
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		TProgram::deskTop->lock();
		for (MRDesktopWindow *window : visibleWindows) {
			const auto windowStartedAt = std::chrono::steady_clock::now();
			TRect bounds;
			bounds.a.x = desktopBounds.a.x + cascadeIndex;
			bounds.a.y = desktopBounds.a.y + cascadeIndex;
			bounds.b.x = desktopBounds.b.x;
			bounds.b.y = desktopBounds.b.y;
			MRWindowLayout::applyBatchWindowBounds(window, bounds);
			{
				const long long windowUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - windowStartedAt).count();
				if (windowUs >= 10000) logWindowTiming("Window cascade bounds slow", windowUs, "index=" + std::to_string(cascadeIndex));
			}
			cascadeIndex++;
		}
		TProgram::deskTop->unlock();
		MRWindowLayout::refreshDesktopProjection();
		mrNotifyWindowTopologyChanged();
		locateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	{
		std::ostringstream detail;
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();

		detail << "all=" << allWindows.size() << " visible=" << visibleWindows.size() << " enumerate_us=" << enumerateUs << " filter_us=" << filterUs << " locate_us=" << locateUs;
		logWindowTiming("Window cascade timing", tookUs, detail.str());
	}
	return true;
}

bool handleWindowTile() {
	const auto startedAt = std::chrono::steady_clock::now();
	const auto enumerateStartedAt = startedAt;
	std::vector<MRDesktopWindow *> allWindows = allDesktopWindowsInZOrder();
	std::vector<MRDesktopWindow *> visibleWindows;
	TRect desktopBounds;
	long long enumerateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - enumerateStartedAt).count();
	long long filterUs = 0;
	long long locateUs = 0;

	if (TProgram::deskTop == nullptr) return false;

	desktopBounds = MRWindowLayout::usableDesktopBounds();

	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		for (auto it = allWindows.rbegin(); it != allWindows.rend(); ++it) {
			MRDesktopWindow *window = *it;
			TWindow *nativeWindow = window != nullptr ? window->desktopNativeWindow() : nullptr;
			if (nativeWindow != nullptr && (nativeWindow->options & ofTileable) != 0 && (nativeWindow->state & sfVisible) != 0 && !window->desktopMinimized()) {
				visibleWindows.push_back(window);
			}
		}
		filterUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}

	int count = visibleWindows.size();

	if (count > 9) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEvent, "max 9 windows can be tiled", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return true;
	}

	if (count == 0) return true;

	std::vector<TRect> rects(count);
	int width = desktopBounds.b.x - desktopBounds.a.x;
	int height = desktopBounds.b.y - desktopBounds.a.y;
	int halfWidth = width / 2;
	int halfHeight = height / 2;

	switch (count) {
		case 1:
			rects[0] = desktopBounds;
			break;
		case 2:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + halfWidth, desktopBounds.b.y);
			rects[1] = TRect(desktopBounds.a.x + halfWidth, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 3:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + halfWidth, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + halfWidth, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 4:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + halfWidth, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + halfWidth, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + halfWidth, desktopBounds.b.y);
			rects[3] = TRect(desktopBounds.a.x + halfWidth, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 5:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + width / 3, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + width / 3, desktopBounds.a.y, desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[3] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + halfWidth, desktopBounds.b.y);
			rects[4] = TRect(desktopBounds.a.x + halfWidth, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 6:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + width / 3, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + width / 3, desktopBounds.a.y, desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[3] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + width / 3, desktopBounds.b.y);
			rects[4] = TRect(desktopBounds.a.x + width / 3, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 2 * width / 3, desktopBounds.b.y);
			rects[5] = TRect(desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 7:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + width / 4, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + width / 4, desktopBounds.a.y, desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y, desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y + halfHeight);
			rects[3] = TRect(desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[4] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + width / 3, desktopBounds.b.y);
			rects[5] = TRect(desktopBounds.a.x + width / 3, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 2 * width / 3, desktopBounds.b.y);
			rects[6] = TRect(desktopBounds.a.x + 2 * width / 3, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 8:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + width / 4, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + width / 4, desktopBounds.a.y, desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y, desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y + halfHeight);
			rects[3] = TRect(desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[4] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + width / 4, desktopBounds.b.y);
			rects[5] = TRect(desktopBounds.a.x + width / 4, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 2 * width / 4, desktopBounds.b.y);
			rects[6] = TRect(desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 3 * width / 4, desktopBounds.b.y);
			rects[7] = TRect(desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
		case 9:
			rects[0] = TRect(desktopBounds.a.x, desktopBounds.a.y, desktopBounds.a.x + width / 5, desktopBounds.a.y + halfHeight);
			rects[1] = TRect(desktopBounds.a.x + width / 5, desktopBounds.a.y, desktopBounds.a.x + 2 * width / 5, desktopBounds.a.y + halfHeight);
			rects[2] = TRect(desktopBounds.a.x + 2 * width / 5, desktopBounds.a.y, desktopBounds.a.x + 3 * width / 5, desktopBounds.a.y + halfHeight);
			rects[3] = TRect(desktopBounds.a.x + 3 * width / 5, desktopBounds.a.y, desktopBounds.a.x + 4 * width / 5, desktopBounds.a.y + halfHeight);
			rects[4] = TRect(desktopBounds.a.x + 4 * width / 5, desktopBounds.a.y, desktopBounds.b.x, desktopBounds.a.y + halfHeight);
			rects[5] = TRect(desktopBounds.a.x, desktopBounds.a.y + halfHeight, desktopBounds.a.x + width / 4, desktopBounds.b.y);
			rects[6] = TRect(desktopBounds.a.x + width / 4, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 2 * width / 4, desktopBounds.b.y);
			rects[7] = TRect(desktopBounds.a.x + 2 * width / 4, desktopBounds.a.y + halfHeight, desktopBounds.a.x + 3 * width / 4, desktopBounds.b.y);
			rects[8] = TRect(desktopBounds.a.x + 3 * width / 4, desktopBounds.a.y + halfHeight, desktopBounds.b.x, desktopBounds.b.y);
			break;
	}

	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		TProgram::deskTop->lock();
		for (int i = 0; i < count; i++) {
			const auto windowStartedAt = std::chrono::steady_clock::now();
			MRWindowLayout::applyBatchWindowBounds(visibleWindows[i], rects[i]);
			{
				const long long windowUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - windowStartedAt).count();
				if (windowUs >= 10000) logWindowTiming("Window tile bounds slow", windowUs, "index=" + std::to_string(i));
			}
		}
		TProgram::deskTop->unlock();
		MRWindowLayout::refreshDesktopProjection();
		mrNotifyWindowTopologyChanged();
		locateUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	{
		std::ostringstream detail;
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count();

		detail << "all=" << allWindows.size() << " visible=" << visibleWindows.size() << " enumerate_us=" << enumerateUs << " filter_us=" << filterUs << " locate_us=" << locateUs;
		logWindowTiming("Window tile timing", tookUs, detail.str());
	}
	return true;
}
