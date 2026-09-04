#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TEvent
#define Uses_TFileDialog
#define Uses_MsgBox
#define Uses_TObject
#define Uses_TScreen
#include <tvision/tv.h>

#include "MRBentoWorkspaceCodec.hpp"
#include "MRWindowCommands.hpp"
#include "MRWindowCommandsInternal.hpp"
#include "../MRMacroDebuggerCommandRoute.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../mrmac/mrmac.h"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRFrame.hpp"
#include "../../ui/MRBentoBox/MRBentoBox.hpp"
#include "../../ui/MRBentoHexEditor/MRBentoHexEditor.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"
#include "../../ui/widgets/MRScopedHistoryUI.hpp"

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;

using mr::window_commands::applicationUiInt;
using mr::window_commands::applicationUiString;
using mr::window_commands::applicationUiUnsigned;
using mr::window_commands::kWorkspaceBranch;
using mr::window_commands::logWindowTiming;
using mr::window_commands::postWindowCommandError;
using mr::window_commands::steadyClockMilliseconds;
using mr::window_commands::storeApplicationUiInt;
using mr::window_commands::storeApplicationUiString;
using mr::window_commands::storeApplicationUiUnsigned;

static constexpr long long kWorkspaceRestoreAnnouncementThresholdMs = 3000;
static constexpr long long kWorkspaceRestoreEstimatedEntryMs = 20;
static constexpr long long kWorkspaceRestoreEstimatedBentoExtraMs = 20;
static constexpr long long kWorkspaceRestoreEstimatedFileCompareExtraMs = 20;
static constexpr long long kWorkspaceRestoreEstimatedDebuggerExtraMs = 60;
static constexpr long long kWorkspaceRestoreEstimatedFileCompareBytesPerMs = 512 * 1024;

namespace {
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

bool parseWorkspaceIntPair(const std::string &text, int &first, int &second) {
	const std::size_t comma = text.find(',');

	if (comma == std::string::npos || text.find(',', comma + 1) != std::string::npos) return false;
	return parseWorkspaceInt(text.substr(0, comma), first) && parseWorkspaceInt(text.substr(comma + 1), second);
}

enum class WorkspaceOptionKind : unsigned char {
	Minimized,
	RestoreSize,
	RestorePosition,
	MainFile,
	Bento,
	Debugger,
	FileCompareOriginal,
	FileCompareCompare,
};

struct WorkspaceOptionDescriptor {
	const char *key;
	WorkspaceOptionKind kind;
};

static const WorkspaceOptionDescriptor kWorkspaceOptionDescriptors[] = {
	{"min", WorkspaceOptionKind::Minimized},
	{"restore", WorkspaceOptionKind::RestoreSize},
	{"rpos", WorkspaceOptionKind::RestorePosition},
	{"main", WorkspaceOptionKind::MainFile},
	{"bento", WorkspaceOptionKind::Bento},
	{"debug", WorkspaceOptionKind::Debugger},
	{"fco", WorkspaceOptionKind::FileCompareOriginal},
	{"fcc", WorkspaceOptionKind::FileCompareCompare},
};

const WorkspaceOptionDescriptor *workspaceOptionDescriptor(const std::string &key) noexcept {
	for (const WorkspaceOptionDescriptor &descriptor : kWorkspaceOptionDescriptors)
		if (key == descriptor.key) return &descriptor;
	return nullptr;
}

int workspaceVirtualDesktopOrRandom(int savedDesktop) {
	int maxDesktop = mrVirtualDesktopCountSnapshot();

	if (maxDesktop < 1) maxDesktop = 1;
	if (savedDesktop >= 1 && savedDesktop <= maxDesktop) return savedDesktop;

	std::mt19937 generator(std::random_device{}());
	std::uniform_int_distribution<int> distribution(1, maxDesktop);
	return distribution(generator);
}

bool parseWorkspaceEntry(const std::string &line, WorkspaceEntry &entry, bool logBootstrapNormalization = false) {
	static const std::regex linePattern(R"(MRSETUP\s*\(\s*'WORKSPACE'\s*,\s*'((?:''|[^'])*)'\s*\)\s*;?)", std::regex_constants::ECMAScript | std::regex_constants::icase);
	static const std::regex payloadPattern(R"(^URL=(.*) size=(-?\d+),(-?\d+) pos=(-?\d+),(-?\d+) cursor=(-?\d+),(-?\d+) vd=(-?\d+)(?: (.*))?$)", std::regex_constants::ECMAScript);
	std::smatch match;
	std::smatch payloadMatch;
	std::string payload;
	std::vector<std::string> bootstrapLogMessages;
	std::string fileCompareOriginalToken;
	std::string fileCompareCompareToken;
	bool restoreSizeSeen = false;
	bool restorePositionSeen = false;

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
	entry.restoreWidth = entry.width;
	entry.restoreHeight = entry.height;
	entry.restoreX = entry.x;
	entry.restoreY = entry.y;
	if (payloadMatch[9].matched)
		for (const std::string &optionText : splitWorkspaceToken(payloadMatch[9].str(), ' ')) {
			const std::size_t equals = optionText.find('=');
			const std::string key = equals == std::string::npos ? optionText : optionText.substr(0, equals);
			const std::string value = equals == std::string::npos ? std::string() : optionText.substr(equals + 1);
			const WorkspaceOptionDescriptor *descriptor = workspaceOptionDescriptor(key);
			bool accepted = descriptor != nullptr && equals != std::string::npos;

			if (accepted)
				switch (descriptor->kind) {
					case WorkspaceOptionKind::Minimized:
						accepted = value == "0" || value == "1";
						if (accepted) entry.minimized = value == "1";
						break;
					case WorkspaceOptionKind::RestoreSize:
						{
							int width = entry.width;
							int height = entry.height;

							accepted = parseWorkspaceIntPair(value, width, height);
							if (accepted) {
								entry.restoreWidth = width;
								entry.restoreHeight = height;
							}
						}
						restoreSizeSeen = accepted;
						break;
					case WorkspaceOptionKind::RestorePosition:
						{
							int x = entry.x;
							int y = entry.y;

							accepted = parseWorkspaceIntPair(value, x, y);
							if (accepted) {
								entry.restoreX = x;
								entry.restoreY = y;
							}
						}
						restorePositionSeen = accepted;
						break;
					case WorkspaceOptionKind::MainFile:
						accepted = value == "0" || value == "1";
						if (accepted) entry.mainFile = value == "1";
						break;
					case WorkspaceOptionKind::Bento:
						entry.hasBentoSnapshot = mr::workspace::parseBentoSnapshot(value, entry.bentoSnapshot, &bootstrapLogMessages);
						accepted = entry.hasBentoSnapshot;
						break;
					case WorkspaceOptionKind::Debugger:
						entry.hasMacroDebuggerConfiguration = mr::workspace::parseMacroDebuggerConfiguration(value, entry.macroDebuggerConfiguration);
						accepted = entry.hasMacroDebuggerConfiguration;
						break;
					case WorkspaceOptionKind::FileCompareOriginal:
						fileCompareOriginalToken = value;
						break;
					case WorkspaceOptionKind::FileCompareCompare:
						fileCompareCompareToken = value;
						break;
				}
			if (!accepted) bootstrapLogMessages.push_back("Workspace bootstrap dropped unknown or unsupported option key=" + key + ".");
		}
	if (restoreSizeSeen != restorePositionSeen) {
		entry.restoreWidth = entry.width;
		entry.restoreHeight = entry.height;
		entry.restoreX = entry.x;
		entry.restoreY = entry.y;
		bootstrapLogMessages.push_back("Workspace bootstrap dropped incomplete restore geometry.");
	}
	if (entry.hasMacroDebuggerConfiguration && !entry.hasBentoSnapshot) {
		entry.hasMacroDebuggerConfiguration = false;
		entry.macroDebuggerConfiguration = MRMacroDebuggerWorkspaceConfiguration();
		bootstrapLogMessages.push_back("Workspace bootstrap dropped debugger configuration without Bento layout.");
	}
	if (!fileCompareOriginalToken.empty() || !fileCompareCompareToken.empty()) {
		entry.hasFileCompareSources = !fileCompareOriginalToken.empty() && !fileCompareCompareToken.empty() &&
		                              workspaceHexDecode(fileCompareOriginalToken, entry.fileCompareOriginalUrl) &&
		                              workspaceHexDecode(fileCompareCompareToken, entry.fileCompareCompareUrl) &&
		                              !entry.fileCompareOriginalUrl.empty() && !entry.fileCompareCompareUrl.empty();
		if (!entry.hasFileCompareSources) {
			entry.fileCompareOriginalUrl.clear();
			entry.fileCompareCompareUrl.clear();
			bootstrapLogMessages.push_back("Workspace bootstrap dropped incomplete or invalid file-compare source configuration.");
		}
	}
	if (entry.url.empty()) return false;
	if (logBootstrapNormalization)
		for (const std::string &message : bootstrapLogMessages)
			mrLogMessage(message);
	return true;
}

void applyWorkspaceEntryGeometry(MREditWindow *window, const WorkspaceEntry &entry) {
	if (window == nullptr || entry.width <= 0 || entry.height <= 0 || entry.x < 0 || entry.y < 0) return;
	const TRect bounds(entry.x, entry.y, entry.x + entry.width, entry.y + entry.height);
	const TRect restoreBounds(entry.restoreX, entry.restoreY, entry.restoreX + std::max(entry.restoreWidth, 1), entry.restoreY + std::max(entry.restoreHeight, 1));
	std::ostringstream detail;

	MRWindowLayout::applyWorkspaceState(window, bounds, restoreBounds, entry.minimized, false, false);
	detail << "Workspace geometry restored window=" << window->number << " saved=" << bounds.a.x << "," << bounds.a.y << "," << bounds.b.x << "," << bounds.b.y << " restore=" << restoreBounds.a.x << "," << restoreBounds.a.y << "," << restoreBounds.b.x << "," << restoreBounds.b.y << " applied=" << window->getBounds().a.x << "," << window->getBounds().a.y << "," << window->getBounds().b.x << "," << window->getBounds().b.y << " min=" << (entry.minimized ? 1 : 0) << ".";
	mrLogMessage(detail.str());
}

void drawWorkspaceRestoreProgressNow(int processedEntries, int totalEntries) {
	mr::messageline::setStaticProgress(static_cast<std::size_t>(std::max(0, processedEntries)), static_cast<std::size_t>(std::max(0, totalEntries)));
	TScreen::flushScreen();
}

bool workspaceRestoreCancelRequested() {
	static constexpr int kMaximumQueuedKeys = 16;

	for (int i = 0; i < kMaximumQueuedKeys; ++i) {
		TEvent event{};

		event.getKeyEvent();
		if (event.what == evNothing) return false;
		if (event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc)) return true;
	}
	return false;
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
	if (window->getEditor() != nullptr) source.snapshot = window->getEditor()->readSnapshot();
	return source;
}

std::string fileCompareWorkspaceTitle(const MRBentoCompareSetup &setup) {
	std::string title = "Compare: " + setup.original.title + " / " + setup.compare.title;

	if (title.size() > 72) title = title.substr(0, 69) + "...";
	return title;
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
	if (bentoBox == nullptr || !bentoBox->initializeFileCompare(std::move(setup)) || !bentoBox->restoreWorkspaceSnapshot(entry.bentoSnapshot)) {
		closeWorkspaceWindow(bentoBox);
		closeWorkspaceWindow(compareWindow);
		closeWorkspaceWindow(originalWindow);
		return nullptr;
	}
	bentoBox->refreshFileCompareConfiguration();
	if (!bentoBox->startFileCompareProjection()) {
		closeWorkspaceWindow(bentoBox);
		closeWorkspaceWindow(compareWindow);
		closeWorkspaceWindow(originalWindow);
		return nullptr;
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


} // namespace

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
			bentoPayload = mr::workspace::encodeBentoSnapshot(bentoBox->workspaceSnapshot());
			MRMacroDebuggerWorkspaceConfiguration macroDebuggerConfiguration;

			if (bentoBox->macroDebuggerWorkspaceConfiguration(macroDebuggerConfiguration)) macroDebuggerPayload = " debug=" + mr::workspace::encodeMacroDebuggerConfiguration(macroDebuggerConfiguration);
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
		restoreBounds = win->restoreWorkspaceBounds();
		bounds = win->isMinimized() ? win->minimizedWorkspaceBounds() : restoreBounds;
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

std::vector<std::string> mrSettingsFileAutosavedWorkspaceFiles() {
	std::string content;
	std::string errorText;
	std::string path = configuredSettingsMacroFilePath();
	std::vector<std::string> files;
	std::size_t parsedEntries = 0;

	if (path.find(".mrmac") == std::string::npos) path += ".mrmac";
	if (!readTextFile(path, content, errorText)) {
		mrLogMessage("Workspace autosave probe failed path=" + path + " error=" + errorText + ".");
		return files;
	}
	{
		std::istringstream input(content);
		std::string line;

		while (std::getline(input, line)) {
			WorkspaceEntry entry;

			if (!parseWorkspaceEntry(line, entry)) continue;
			++parsedEntries;
			if (entry.hasBentoSnapshot && entry.bentoSnapshot.mode == bbmFileCompare && entry.hasFileCompareSources) {
				files.push_back(entry.fileCompareOriginalUrl);
				files.push_back(entry.fileCompareCompareUrl);
			} else
				files.push_back(entry.url);
		}
	}
	mrLogMessage("Workspace autosave probe path=" + path + " entries=" + std::to_string(parsedEntries) + " files=" + std::to_string(files.size()) + ".");
	return files;
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

bool mrSaveWorkspace(const std::string &filename) {
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
	return writeTextFile(dest, source);
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
	long long preflightUs = 0;
	long long parseLoopUs = 0;
	long long visibilityUs = 0;
	long long estimatedRestoreMs = 0;
	long long estimatedFileCompareBytes = 0;
	int totalWorkspaceEntries = 0;
	std::chrono::steady_clock::time_point lastWorkspaceProgressAt = std::chrono::steady_clock::time_point::min();
	const ushort restoreCursorLines = TScreen::cursorLines;
	bool cursorSuppressedForRestore = false;
	bool announceWorkspaceRestore = false;
	bool workspaceRestoreCancelled = false;

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
			const std::string text = "Unable to read workspace: " + workspaceDisplayName(dest);

			mrLogMessage("Workspace load failed read path=" + dest + " error=" + errorText + ".");
			mr::messageline::postAutoTimed(mr::messageline::Owner::WorkspaceRestore, text, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
			return;
		}
		readUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	mrLogMessage("Workspace load begin path=" + dest + " bytes=" + std::to_string(currentContent.size()) + ".");
	std::vector<WorkspaceEntry> workspaceEntries;
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		std::istringstream input(currentContent);
		std::string line;

		while (std::getline(input, line)) {
			WorkspaceEntry entry;

			if (!parseWorkspaceEntry(line, entry, true)) continue;
			estimatedRestoreMs += kWorkspaceRestoreEstimatedEntryMs;
			if (entry.hasBentoSnapshot) estimatedRestoreMs += kWorkspaceRestoreEstimatedBentoExtraMs;
			if (entry.hasMacroDebuggerConfiguration) estimatedRestoreMs += kWorkspaceRestoreEstimatedDebuggerExtraMs;
			if (entry.hasBentoSnapshot && entry.bentoSnapshot.mode == bbmFileCompare) {
				const std::string *sourcePaths[] = {&entry.fileCompareOriginalUrl, &entry.fileCompareCompareUrl};

				estimatedRestoreMs += kWorkspaceRestoreEstimatedFileCompareExtraMs;
				for (const std::string *sourcePath : sourcePaths) {
					struct stat sourceInfo{};

					if (::stat(sourcePath->c_str(), &sourceInfo) != 0 || !S_ISREG(sourceInfo.st_mode) || sourceInfo.st_size <= 0) continue;
					estimatedFileCompareBytes += static_cast<long long>(sourceInfo.st_size);
					estimatedRestoreMs += static_cast<long long>(sourceInfo.st_size) / kWorkspaceRestoreEstimatedFileCompareBytesPerMs;
					if (static_cast<long long>(sourceInfo.st_size) % kWorkspaceRestoreEstimatedFileCompareBytesPerMs != 0) ++estimatedRestoreMs;
				}
			}
			workspaceEntries.push_back(std::move(entry));
		}
		preflightUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	totalWorkspaceEntries = static_cast<int>(workspaceEntries.size());
	announceWorkspaceRestore = estimatedRestoreMs >= kWorkspaceRestoreAnnouncementThresholdMs;
	storeApplicationUiInt(mrvmRuntimeKv(), kWorkspaceBranch, "restoreInProgress", announceWorkspaceRestore ? 1 : 0);
	mrLogMessage("Workspace restore estimate path=" + dest + " entries=" + std::to_string(totalWorkspaceEntries) + " file_compare_bytes=" + std::to_string(estimatedFileCompareBytes) + " estimated_ms=" + std::to_string(estimatedRestoreMs) + " announced=" + (announceWorkspaceRestore ? "1" : "0") + ".");
	if (announceWorkspaceRestore) {
		cursorSuppressedForRestore = true;
		TScreen::cursorLines = 0;
		TScreen::setCursorType(0);
		hideAllEditorFrameHoverPopups();
		mr::messageline::setStaticMode(true);
		drawWorkspaceRestoreProgressNow(0, totalWorkspaceEntries);
		lastWorkspaceProgressAt = std::chrono::steady_clock::now();
	}

	const auto restoreStartedAt = std::chrono::steady_clock::now();
	bool discardedWorkspaceEntries = false;
	bool loadedMainFile = false;
	int parsedWorkspaceEntries = 0;
	int loadedWorkspaceEntries = 0;
	MRWindowOpenBatch openBatch;
	storeApplicationUiString(mrvmRuntimeKv(), kWorkspaceBranch, "mainFilePath", std::string());
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		openBatch.begin();
		for (const WorkspaceEntry &entry : workspaceEntries) {
			const auto entryStartedAt = std::chrono::steady_clock::now();
			MREditWindow *win = nullptr;
			MRFileEditor *editor = nullptr;
			std::string err;
			int resolvedVirtualDesktop = 1;
			long long createUs = 0;
			long long fileLoadUs = 0;
			long long bentoUs = 0;
			long long geometryUs = 0;
			long long cursorUs = 0;

			if (announceWorkspaceRestore) {
				const auto now = std::chrono::steady_clock::now();
				const bool shouldReportProgress = now - lastWorkspaceProgressAt >= std::chrono::milliseconds(1500);

				if (shouldReportProgress) {
					drawWorkspaceRestoreProgressNow(parsedWorkspaceEntries, totalWorkspaceEntries);
					lastWorkspaceProgressAt = now;
				}
				if (workspaceRestoreCancelRequested()) {
					workspaceRestoreCancelled = true;
					break;
				}
			}
			++parsedWorkspaceEntries;
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
				MRMacroDebuggerWorkspaceConfiguration configuration = entry.macroDebuggerConfiguration;

				if (bentoBox == nullptr || !bentoBox->ensureMacroDebuggerPanes(debuggerOutput, variables, watches)) {
					mrLogMessage("Workspace load failed debugger Bento restore url=" + entry.url + ".");
					message(win, evCommand, cmClose, nullptr);
					discardedWorkspaceEntries = true;
					continue;
				}
				const std::string debuggerMacroName = !configuration.macroName.empty() ? configuration.macroName : configuration.macroKey;
				if (mrMacroDebuggerForSourceIdentity(entry.url, debuggerMacroName, bentoBox) != nullptr) {
					mrLogMessage("Workspace bootstrap dropped duplicate debugger owner url=" + entry.url + ".");
				} else {
					if (configuration.sourcePath.empty()) configuration.sourcePath = entry.url;
					bentoBox->restoreMacroDebuggerWorkspaceConfiguration(configuration);
				}
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
	storeApplicationUiInt(mrvmRuntimeKv(), kWorkspaceBranch, "restoreInProgress", 0);
	if (announceWorkspaceRestore) hideAllEditorFrameHoverPopups();
	{
		const auto phaseStartedAt = std::chrono::steady_clock::now();
		syncVirtualDesktopVisibility();
		requestMRGitStatusProbe(currentEditWindow());
		if (loadedWorkspaceEntries != 0) mrNotifyWindowTopologyChanged();
		visibilityUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phaseStartedAt).count();
	}
	mrLogMessage("Workspace load summary path=" + dest + " parsed=" + std::to_string(parsedWorkspaceEntries) + " loaded=" + std::to_string(loadedWorkspaceEntries) + " discarded=" + (discardedWorkspaceEntries ? "1" : "0") + " cancelled=" + (workspaceRestoreCancelled ? "1" : "0") + ".");
	{
		std::ostringstream detail;
		const long long tookUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - loadStartedAt).count();
		const long long actualRestoreMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - restoreStartedAt).count();

		detail << "read_us=" << readUs << " preflight_us=" << preflightUs << " parse_loop_us=" << parseLoopUs << " visibility_us=" << visibilityUs << " estimated_ms=" << estimatedRestoreMs << " actual_restore_ms=" << actualRestoreMs << " announced=" << (announceWorkspaceRestore ? 1 : 0) << " cancelled=" << (workspaceRestoreCancelled ? 1 : 0) << " parsed=" << parsedWorkspaceEntries << " loaded=" << loadedWorkspaceEntries << " bytes=" << currentContent.size() << " path=\"" << dest << "\"";
		logWindowTiming("Workspace load total timing", tookUs, detail.str());
	}
	if ((parsedWorkspaceEntries != 0 || workspaceRestoreCancelled) && loadedWorkspaceEntries == 0) {
		setRuntimePreserveAutosavedWorkspace(true);
		mrLogMessage("Workspace load restored no entries; autosaved workspace was left preserved.");
	}
	if (discardedWorkspaceEntries) mrLogMessage("Workspace load skipped one or more invalid entries; source was left unchanged.");
	if (cursorSuppressedForRestore) {
		TScreen::cursorLines = restoreCursorLines;
		TScreen::setCursorType(restoreCursorLines);
		if (TProgram::deskTop != nullptr) TProgram::deskTop->resetCursor();
	}
	if (announceWorkspaceRestore) {
		const int skippedWorkspaceEntries = std::max(0, parsedWorkspaceEntries - loadedWorkspaceEntries);
		const long long tookMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - restoreStartedAt).count();
		std::string text;
		const bool entriesSkipped = skippedWorkspaceEntries != 0 || workspaceRestoreCancelled;

		drawWorkspaceRestoreProgressNow(parsedWorkspaceEntries, totalWorkspaceEntries);
		mr::messageline::setStaticMode(false);
		TScreen::flushScreen();
		if (workspaceRestoreCancelled) {
			text = "Workspace restore cancelled: " + std::to_string(loadedWorkspaceEntries) + "/" + std::to_string(totalWorkspaceEntries) + " entries loaded, " + std::to_string(parsedWorkspaceEntries) + " processed in " + std::to_string(tookMs) + " ms.";
		} else {
			text = "Workspace restored: " + std::to_string(loadedWorkspaceEntries) + "/" + std::to_string(parsedWorkspaceEntries) + " entries in " + std::to_string(tookMs) + " ms.";
			if (skippedWorkspaceEntries != 0) text += " " + std::to_string(skippedWorkspaceEntries) + " skipped.";
		}
		mr::messageline::postAutoTimed(mr::messageline::Owner::WorkspaceRestore, text, entriesSkipped ? mr::messageline::Kind::Warning : mr::messageline::Kind::Info, entriesSkipped ? mr::messageline::kPriorityHigh : mr::messageline::kPriorityMedium);
	}
}
