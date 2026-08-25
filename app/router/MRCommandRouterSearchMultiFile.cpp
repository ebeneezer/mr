#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TObject
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#define Uses_TButton
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TListViewer
#define Uses_TDrawBuffer
#define Uses_TCheckBoxes
#define Uses_TScrollBar
#define Uses_TStaticText
#define Uses_TSItem
#include <tvision/tv.h>

#include "MRCommandRouterSearchMultiFile.hpp"
#include "MRCommandRouterSearchMultiFileSession.hpp"

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>

#include "../../mrmac/mrmac.h"
#include "../../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../../mrmac/vm/MRVMValue.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../MREditorApp.hpp"
#include "../commands/MRWindowCommands.hpp"

static constexpr const char *kNoPreviousMultiFileSearchListMessage = "No previous multi-file search list.";

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;

namespace {
VirtualMachine::Value multiFileSearchRoot(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value applicationUi = runtimeKv.ensureRoot("APPLICATIONUI");
	VirtualMachine::Value search = runtimeKv.ensureChild(applicationUi, "search");
	return runtimeKv.ensureChild(search, "multiFile");
}

int readInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int fallback = 0) {
	MRVMHashStore &store = runtimeKv.globalStore();
	if (!mrvmHashContainsValue(store, store, parent, key)) return fallback;
	VirtualMachine::Value stored = mrvmHashReadValue(store, store, parent, key);
	return stored.type == TYPE_INT ? stored.i : fallback;
}

std::size_t readSize(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key) {
	MRVMHashStore &store = runtimeKv.globalStore();
	if (!mrvmHashContainsValue(store, store, parent, key)) return 0;
	VirtualMachine::Value stored = mrvmHashReadValue(store, store, parent, key);
	if (stored.type != TYPE_STR) return 0;
	try {
		std::size_t consumed = 0;
		const unsigned long long parsed = std::stoull(stored.s, &consumed, 10);
		return consumed == stored.s.size() ? static_cast<std::size_t>(parsed) : 0;
	} catch (...) {
		return 0;
	}
}

std::string readString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key) {
	MRVMHashStore &store = runtimeKv.globalStore();
	if (!mrvmHashContainsValue(store, store, parent, key)) return std::string();
	VirtualMachine::Value stored = mrvmHashReadValue(store, store, parent, key);
	return stored.type == TYPE_STR ? stored.s : std::string();
}

void writeInt(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, int value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeInt(value));
}

void writeSize(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, std::size_t value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(std::to_string(value)));
}

void writeString(MRVMRuntimeKv &runtimeKv, const VirtualMachine::Value &parent, const char *key, const std::string &value) {
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, parent, key, mrvmMakeString(value));
}

MREditWindow *windowForBufferId(int bufferId) {
	for (MREditWindow *window : allEditWindowsInZOrder())
		if (window != nullptr && window->bufferId() == bufferId) return window;
	return nullptr;
}

MultiFileSearchSession lastMultiFileSearchSession() {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value root = multiFileSearchRoot(runtimeKv);
	MultiFileSearchSession session;

	session.valid = readInt(runtimeKv, root, "valid") != 0;
	session.replaceMode = readInt(runtimeKv, root, "replaceMode") != 0;
	session.caseSensitive = readInt(runtimeKv, root, "caseSensitive") != 0;
	session.wholeWords = readInt(runtimeKv, root, "wholeWords") != 0;
	session.regularExpressions = readInt(runtimeKv, root, "regularExpressions", 1) != 0;
	session.keepFilesOpen = readInt(runtimeKv, root, "keepFilesOpen") != 0;
	session.pattern = readString(runtimeKv, root, "pattern");
	session.replacement = readString(runtimeKv, root, "replacement");
	session.selectedFileIndex = readSize(runtimeKv, root, "selectedFileIndex");

	VirtualMachine::Value files;
	if (!runtimeKv.findChild(root, "files", files)) return session;
	const int fileCount = readInt(runtimeKv, files, "count");
	for (int fileIndex = 0; fileIndex < fileCount; ++fileIndex) {
		VirtualMachine::Value storedFile;
		if (!runtimeKv.findChild(files, std::to_string(fileIndex), storedFile)) continue;
		MultiFileSearchFileResult file;
		file.normalizedPath = readString(runtimeKv, storedFile, "normalizedPath");
		file.fileName = readString(runtimeKv, storedFile, "fileName");
		file.selectedMatchIndex = readSize(runtimeKv, storedFile, "selectedMatchIndex");
		file.startedInMemory = readInt(runtimeKv, storedFile, "startedInMemory") != 0;
		file.startedDocumentId = readSize(runtimeKv, storedFile, "startedDocumentId");
		file.startedDocumentVersion = readSize(runtimeKv, storedFile, "startedDocumentVersion");
		file.temporaryWindow = readInt(runtimeKv, storedFile, "temporaryWindow") != 0;
		file.window = windowForBufferId(readInt(runtimeKv, storedFile, "windowBufferId"));

		VirtualMachine::Value matches;
		if (runtimeKv.findChild(storedFile, "matches", matches)) {
			const int matchCount = readInt(runtimeKv, matches, "count");
			for (int matchIndex = 0; matchIndex < matchCount; ++matchIndex) {
				VirtualMachine::Value storedMatch;
				if (!runtimeKv.findChild(matches, std::to_string(matchIndex), storedMatch)) continue;
				SearchMatchEntry match;
				match.start = readSize(runtimeKv, storedMatch, "start");
				match.end = readSize(runtimeKv, storedMatch, "end");
				match.line = readSize(runtimeKv, storedMatch, "line");
				match.column = readSize(runtimeKv, storedMatch, "column");
				match.preview = readString(runtimeKv, storedMatch, "preview");
				match.previewMatchOffset = readSize(runtimeKv, storedMatch, "previewMatchOffset");
				match.previewMatchLength = readSize(runtimeKv, storedMatch, "previewMatchLength");
				file.matches.push_back(match);
			}
		}
		session.files.push_back(file);
	}
	return session;
}

void storeLastMultiFileSearchSession(const MultiFileSearchSession &session) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	VirtualMachine::Value root = multiFileSearchRoot(runtimeKv);
	VirtualMachine::Value files = runtimeKv.replaceChild(root, "files");

	writeInt(runtimeKv, root, "valid", session.valid ? 1 : 0);
	writeInt(runtimeKv, root, "replaceMode", session.replaceMode ? 1 : 0);
	writeInt(runtimeKv, root, "caseSensitive", session.caseSensitive ? 1 : 0);
	writeInt(runtimeKv, root, "wholeWords", session.wholeWords ? 1 : 0);
	writeInt(runtimeKv, root, "regularExpressions", session.regularExpressions ? 1 : 0);
	writeInt(runtimeKv, root, "keepFilesOpen", session.keepFilesOpen ? 1 : 0);
	writeString(runtimeKv, root, "pattern", session.pattern);
	writeString(runtimeKv, root, "replacement", session.replacement);
	writeSize(runtimeKv, root, "selectedFileIndex", session.selectedFileIndex);
	writeInt(runtimeKv, files, "count", static_cast<int>(session.files.size()));

	for (std::size_t fileIndex = 0; fileIndex < session.files.size(); ++fileIndex) {
		const MultiFileSearchFileResult &file = session.files[fileIndex];
		VirtualMachine::Value storedFile = runtimeKv.ensureChild(files, std::to_string(fileIndex));
		VirtualMachine::Value matches = runtimeKv.ensureChild(storedFile, "matches");
		writeString(runtimeKv, storedFile, "normalizedPath", file.normalizedPath);
		writeString(runtimeKv, storedFile, "fileName", file.fileName);
		writeSize(runtimeKv, storedFile, "selectedMatchIndex", file.selectedMatchIndex);
		writeInt(runtimeKv, storedFile, "startedInMemory", file.startedInMemory ? 1 : 0);
		writeSize(runtimeKv, storedFile, "startedDocumentId", file.startedDocumentId);
		writeSize(runtimeKv, storedFile, "startedDocumentVersion", file.startedDocumentVersion);
		writeInt(runtimeKv, storedFile, "temporaryWindow", file.temporaryWindow ? 1 : 0);
		writeInt(runtimeKv, storedFile, "windowBufferId", file.window != nullptr ? file.window->bufferId() : 0);
		writeInt(runtimeKv, matches, "count", static_cast<int>(file.matches.size()));
		for (std::size_t matchIndex = 0; matchIndex < file.matches.size(); ++matchIndex) {
			const SearchMatchEntry &match = file.matches[matchIndex];
			VirtualMachine::Value storedMatch = runtimeKv.ensureChild(matches, std::to_string(matchIndex));
			writeSize(runtimeKv, storedMatch, "start", match.start);
			writeSize(runtimeKv, storedMatch, "end", match.end);
			writeSize(runtimeKv, storedMatch, "line", match.line);
			writeSize(runtimeKv, storedMatch, "column", match.column);
			writeString(runtimeKv, storedMatch, "preview", match.preview);
			writeSize(runtimeKv, storedMatch, "previewMatchOffset", match.previewMatchOffset);
			writeSize(runtimeKv, storedMatch, "previewMatchLength", match.previewMatchLength);
		}
	}
}
} // namespace

void postSearchWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text), mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

void postSearchError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void postDialogWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

void postNoHitsWarning() {
	mr::messageline::postTimed(mr::messageline::Owner::HeroEventFollowup, "no hits found", mr::messageline::Kind::Warning, std::chrono::seconds(5), mr::messageline::kPriorityMedium);
}

void postMultiSearchStartedWarning() {
	mr::messageline::postTimed(mr::messageline::Owner::HeroEventFollowup, "searching ...", mr::messageline::Kind::Warning, std::chrono::seconds(5), mr::messageline::kPriorityMedium);
	if (TProgram::application != nullptr) TProgram::application->drawView();
}

void postSearchCancelledError() {
	mr::messageline::postTimed(mr::messageline::Owner::HeroEventFollowup, "search cancelled", mr::messageline::Kind::Error, std::chrono::seconds(5), mr::messageline::kPriorityHigh);
}

bool hasPreviousMultiFileSearchResults() {
	const MultiFileSearchSession session = lastMultiFileSearchSession();
	return session.valid && !session.files.empty();
}

MRMultiSearchDialogOptions workspaceMultiSearchOptions(const std::string &patternSeed, const std::string &startingPath) {
	MRMultiSearchDialogOptions options = configuredMultiSearchDialogOptions();

	options.searchSubdirectories = false;
	options.restrictToWorkspace = true;
	options.searchFilesInMemory = false;
	options.wholeWords = true;
	options.regularExpressions = false;
	options.caseSensitive = true;
	options.filespec = "*";
	options.startingPath = startingPath;
	options.searchText = patternSeed;
	return options;
}

MRMultiSarDialogOptions workspaceMultiSarOptions(const std::string &patternSeed, const std::string &replacementSeed, const std::string &startingPath) {
	MRMultiSarDialogOptions options = configuredMultiSarDialogOptions();

	options.searchSubdirectories = false;
	options.restrictToWorkspace = true;
	options.searchFilesInMemory = false;
	options.wholeWords = true;
	options.regularExpressions = false;
	options.caseSensitive = true;
	options.filespec = "*";
	options.startingPath = startingPath;
	options.searchText = patternSeed;
	options.replacementText = replacementSeed.empty() ? patternSeed : replacementSeed;
	return options;
}

bool handleMultiFileSearchDialogWithOptions(const std::string &patternSeed, MRMultiSearchDialogOptions options) {
	std::string pattern;
	MREditWindow *previousWindow = currentEditWindow();
	for (;;) {
		MultiFileSearchSession session;
		std::string errorText;
		if (!promptMultiFileSearchValues(patternSeed, pattern, options, session)) {
			if (previousWindow != nullptr) static_cast<void>(mrActivateEditWindow(previousWindow));
			return true;
		}
		storeLastMultiFileSearchSession(session);
		switch (runMultiFileResultsDialog(session)) {
			case MultiDialogAction::Done:
				storeLastMultiFileSearchSession(session);
				if (previousWindow != nullptr) static_cast<void>(mrActivateEditWindow(previousWindow));
				return true;
			case MultiDialogAction::Load:
				static_cast<void>(activateSessionCurrentMatch(session));
				storeLastMultiFileSearchSession(session);
				return true;
			case MultiDialogAction::LoadAll:
				if (!loadAllSessionFiles(session, errorText)) {
					if (!errorText.empty()) postSearchError(errorText);
					closeTemporaryWindowsForSession(session);
					storeLastMultiFileSearchSession(session);
					return true;
				}
				static_cast<void>(activateSessionCurrentMatch(session));
				storeLastMultiFileSearchSession(session);
				return true;
			case MultiDialogAction::Cancel:
				storeLastMultiFileSearchSession(session);
				continue;
			default:
				storeLastMultiFileSearchSession(session);
				return true;
		}
	}
	return true;
}

bool handleMultiFileSearchDialog(const std::string &patternSeed) {
	return handleMultiFileSearchDialogWithOptions(patternSeed, configuredMultiSearchDialogOptions());
}

bool handleWorkspaceMultiFileSearchDialog(const std::string &patternSeed, const std::string &startingPath) {
	MRMultiSearchDialogOptions options = workspaceMultiSearchOptions(patternSeed, startingPath);
	return handleMultiFileSearchDialogWithOptions(patternSeed, options);
}

bool handleLastMultiFileSearchListDialog() {
	MultiDialogAction action = MultiDialogAction::Cancel;
	MultiFileSearchSession session = lastMultiFileSearchSession();
	std::string errorText;
	MREditWindow *previousWindow = currentEditWindow();

	if (!hasPreviousMultiFileSearchResults()) {
		postDialogWarning(kNoPreviousMultiFileSearchListMessage);
		return true;
	}
	action = runMultiFileResultsDialog(session);
	if (action == MultiDialogAction::Load) {
		const bool activated = activateSessionCurrentMatch(session);
		storeLastMultiFileSearchSession(session);
		return activated;
	}
	if (action == MultiDialogAction::LoadAll) {
		if (!loadAllSessionFiles(session, errorText)) {
			if (!errorText.empty()) postSearchError(errorText);
			closeTemporaryWindowsForSession(session);
			storeLastMultiFileSearchSession(session);
			return true;
		}
		const bool activated = activateSessionCurrentMatch(session);
		storeLastMultiFileSearchSession(session);
		return activated;
	}
	storeLastMultiFileSearchSession(session);
	if (previousWindow != nullptr) static_cast<void>(mrActivateEditWindow(previousWindow));
	return true;
}

bool handleMultiFileSearchReplaceDialogWithOptions(const std::string &patternSeed, const std::string &replacementSeed, MRMultiSarDialogOptions sarOptions) {
	std::string pattern;
	std::string replacement;
	for (;;) {
		MultiFileSearchSession session;
		std::string errorText;
		std::size_t replacedCount = 0;
		std::size_t revertedCount = 0;
		bool returnToSearchDialog = false;
		bool replaceAllAborted = false;

		if (!promptMultiFileSarValues(patternSeed, replacementSeed, pattern, replacement, sarOptions, session)) return true;

		while (!session.files.empty()) {
			const MultiDialogAction action = runMultiFileResultsDialog(session);
			if (action == MultiDialogAction::Done) break;
			if (action == MultiDialogAction::Cancel) {
				returnToSearchDialog = true;
				break;
			}
			if (action == MultiDialogAction::Skip) {
				if (!moveSessionMatch(session, 1, false)) break;
				continue;
			}
			if (action == MultiDialogAction::ReplaceAll) {
				std::size_t replaceAllCount = 0;
				const MultiReplaceAllOutcome outcome = runMultiFileReplaceAllDialog(session, replaceAllCount, errorText);

				switch (outcome) {
					case MultiReplaceAllOutcome::Reverted:
						revertedCount = replaceAllCount;
						break;
					case MultiReplaceAllOutcome::Aborted:
						replaceAllAborted = true;
						replacedCount += replaceAllCount;
						break;
					case MultiReplaceAllOutcome::Completed:
						replacedCount += replaceAllCount;
						break;
					case MultiReplaceAllOutcome::Error:
						if (replaceAllCount != 0) errorText += " " + std::to_string(replaceAllCount) + " replacements retained.";
						if (!errorText.empty()) postSearchError(errorText);
						closeTemporaryWindowsForSession(session);
						return true;
				}
				break;
			}
			if (action == MultiDialogAction::Replace) {
				if (!replaceCurrentSessionMatch(session, true, errorText)) {
					if (!errorText.empty()) postSearchError(errorText);
					closeTemporaryWindowsForSession(session);
					return true;
				}
				++replacedCount;
			}
		}
		closeTemporaryWindowsForSession(session);
		if (returnToSearchDialog) continue;
		storeLastMultiFileSearchSession(session);
		if (revertedCount != 0) {
			std::string message = std::to_string(revertedCount) + " Replace All replacements reverted";
			if (replacedCount != 0) message += "; " + std::to_string(replacedCount) + " earlier replacements retained";
			postSearchWarning(message);
		}
		else if (replaceAllAborted && replacedCount != 0)
			postSearchWarning(std::to_string(replacedCount) + " replacements retained");
		else if (replacedCount == 0)
			postSearchWarning("No replacements.");
		else
			postSearchWarning(std::to_string(replacedCount) + " replacements");
		return true;
	}
	return true;
}

bool handleMultiFileSearchReplaceDialog(const std::string &patternSeed, const std::string &replacementSeed) {
	return handleMultiFileSearchReplaceDialogWithOptions(patternSeed, replacementSeed, configuredMultiSarDialogOptions());
}

bool handleWorkspaceMultiFileSearchReplaceDialog(const std::string &patternSeed, const std::string &replacementSeed, const std::string &startingPath) {
	return handleMultiFileSearchReplaceDialogWithOptions(patternSeed, replacementSeed, workspaceMultiSarOptions(patternSeed, replacementSeed, startingPath));
}

bool handleNextMultiFileSearchResult() {
	MultiFileSearchSession session = lastMultiFileSearchSession();

	if (!hasPreviousMultiFileSearchResults()) {
		postDialogWarning(kNoPreviousMultiFileSearchListMessage);
		return true;
	}
	if (!moveSessionMatch(session, 1, true)) return true;
	const bool activated = activateSessionCurrentMatch(session);
	storeLastMultiFileSearchSession(session);
	return activated;
}
