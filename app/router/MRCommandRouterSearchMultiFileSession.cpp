#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TObject
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#include <tvision/tv.h>

#include "MRCommandRouterSearchMultiFileCollect.hpp"
#include "MRCommandRouterSearchMultiFileSession.hpp"
#include "MRCommandRouterSearchCore.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../MREditorApp.hpp"
#include "../commands/MRFileCommands.hpp"
#include "../commands/MRWindowCommands.hpp"
#include "../utils/MRFileIOUtils.hpp"

namespace {

bool collectMatchesForMultiFileText(const std::string &text, const MultiFileSearchSession &session, std::vector<SearchMatchEntry> &outMatches, std::string &errorText) {
	pcre2_code *code = nullptr;
	const MRSearchTextType textType = session.regularExpressions ? MRSearchTextType::Pcre : MRSearchTextType::Literal;

	outMatches.clear();
	if (!compileSearchRegex(buildSearchPatternExpression(session.pattern, textType), !session.caseSensitive, &code, errorText)) return false;
	static_cast<void>(collectRegexMatches(text, code, outMatches));
	pcre2_code_free(code);
	errorText.clear();
	return true;
}

MREditWindow *findOpenWindowForNormalizedPath(const std::string &normalizedPath) {
	std::vector<MREditWindow *> windows = allEditWindowsInZOrder();

	for (MREditWindow *window : windows) {
		MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
		if (editor == nullptr || !editor->hasPersistentFileName()) continue;
		if (normalizedSearchPath(editor->persistentFileName()) == normalizedPath) return window;
	}
	return nullptr;
}

bool ensureWindowLoadedForSessionFile(MultiFileSearchFileResult &file, bool activate, std::string &errorText) {
	MREditWindow *window = file.window;

	if (window == nullptr || window->getEditor() == nullptr || !window->getEditor()->hasPersistentFileName() || normalizedSearchPath(window->getEditor()->persistentFileName()) != file.normalizedPath) {
		window = findOpenWindowForNormalizedPath(file.normalizedPath);
		file.temporaryWindow = false;
	}
	if (window == nullptr) {
		window = createEditorWindow(file.normalizedPath.c_str());
		if (window == nullptr) {
			errorText = "Unable to create editor window.";
			return false;
		}
		if (!loadResolvedFileIntoWindow(window, file.normalizedPath, "Multi-file search load")) {
			message(window, evCommand, cmClose, nullptr);
			errorText = "Unable to load file: " + file.normalizedPath;
			return false;
		}
		file.temporaryWindow = true;
	}
	if (activate) static_cast<void>(mrActivateEditWindow(window));
	file.window = window;
	errorText.clear();
	return true;
}

void closeTemporarySessionWindow(MultiFileSearchFileResult &file, bool keepFilesOpen) {
	if (keepFilesOpen || !file.temporaryWindow || file.window == nullptr) return;
	message(file.window, evCommand, cmClose, nullptr);
	file.window = nullptr;
	file.temporaryWindow = false;
}

bool removeSessionFileAt(MultiFileSearchSession &session, std::size_t index) {
	if (index >= session.files.size()) return false;
	session.files.erase(session.files.begin() + static_cast<long long>(index));
	if (session.files.empty()) {
		session.selectedFileIndex = 0;
		return true;
	}
	if (session.selectedFileIndex >= session.files.size()) session.selectedFileIndex = session.files.size() - 1;
	return true;
}

bool refreshMatchesForSessionFile(MultiFileSearchSession &session, std::size_t fileIndex, std::string &errorText) {
	std::vector<SearchMatchEntry> matches;
	std::string text;

	if (fileIndex >= session.files.size()) return false;
	if (!loadSessionFileText(session.files[fileIndex], text, errorText)) return false;
	if (!collectMatchesForMultiFileText(text, session, matches, errorText)) return false;
	session.files[fileIndex].matches.swap(matches);
	if (session.files[fileIndex].selectedMatchIndex >= session.files[fileIndex].matches.size()) session.files[fileIndex].selectedMatchIndex = session.files[fileIndex].matches.empty() ? 0 : session.files[fileIndex].matches.size() - 1;
	errorText.clear();
	return true;
}

} // namespace

std::string baseNameFromPath(const std::string &path) {
	const std::size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool loadSessionFileText(const MultiFileSearchFileResult &file, std::string &outText, std::string &errorText) {
	MREditWindow *window = file.window;
	MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;

	if (editor != nullptr && editor->hasPersistentFileName() && normalizedSearchPath(editor->persistentFileName()) == file.normalizedPath) {
		outText = editor->snapshotText();
		errorText.clear();
		return true;
	}
	if (!readTextFile(file.normalizedPath, outText, errorText)) {
		if (errorText.empty()) errorText = "Unable to read file: " + file.normalizedPath;
		return false;
	}
	errorText.clear();
	return true;
}

MultiFileSearchFileResult *currentSessionFile(MultiFileSearchSession &session) {
	if (session.files.empty()) return nullptr;
	if (session.selectedFileIndex >= session.files.size()) session.selectedFileIndex = session.files.size() - 1;
	return &session.files[session.selectedFileIndex];
}

MREditWindow *preferredSessionRestoreWindow(MultiFileSearchSession &session, MREditWindow *fallback) {
	MultiFileSearchFileResult *selectedFile = currentSessionFile(session);

	if (selectedFile != nullptr && selectedFile->window != nullptr && selectedFile->window->getEditor() != nullptr) return selectedFile->window;
	return fallback;
}

SearchMatchEntry *currentSessionMatch(MultiFileSearchSession &session) {
	MultiFileSearchFileResult *file = currentSessionFile(session);
	if (file == nullptr || file->matches.empty()) return nullptr;
	if (file->selectedMatchIndex >= file->matches.size()) file->selectedMatchIndex = file->matches.size() - 1;
	return &file->matches[file->selectedMatchIndex];
}

std::size_t sessionTotalMatchCount(const MultiFileSearchSession &session) {
	std::size_t total = 0;
	for (const MultiFileSearchFileResult &file : session.files)
		total += file.matches.size();
	return total;
}

std::size_t sessionCurrentMatchOrdinal(const MultiFileSearchSession &session) {
	if (session.files.empty() || session.selectedFileIndex >= session.files.size()) return 0;

	std::size_t ordinal = 0;
	for (std::size_t i = 0; i < session.files.size(); ++i) {
		const MultiFileSearchFileResult &file = session.files[i];
		if (file.matches.empty()) continue;
		if (i == session.selectedFileIndex) {
			const std::size_t index = std::min(file.selectedMatchIndex + 1, file.matches.size());
			return ordinal + index;
		}
		ordinal += file.matches.size();
	}
	return 0;
}

bool moveSessionMatch(MultiFileSearchSession &session, int direction, bool wrap) {
	if (session.files.empty()) return false;
	if (direction == 0) return true;
	MultiFileSearchFileResult *file = currentSessionFile(session);
	if (file == nullptr || file->matches.empty()) return false;
	if (direction > 0) {
		if (file->selectedMatchIndex + 1 < file->matches.size()) {
			++file->selectedMatchIndex;
			return true;
		}
		for (std::size_t i = session.selectedFileIndex + 1; i < session.files.size(); ++i)
			if (!session.files[i].matches.empty()) {
				session.selectedFileIndex = i;
				session.files[i].selectedMatchIndex = 0;
				return true;
			}
		if (wrap)
			for (std::size_t i = 0; i <= session.selectedFileIndex && i < session.files.size(); ++i)
				if (!session.files[i].matches.empty()) {
					session.selectedFileIndex = i;
					session.files[i].selectedMatchIndex = 0;
					return true;
				}
		return false;
	}

	if (file->selectedMatchIndex > 0) {
		--file->selectedMatchIndex;
		return true;
	}
	for (std::size_t i = session.selectedFileIndex; i > 0; --i)
		if (!session.files[i - 1].matches.empty()) {
			session.selectedFileIndex = i - 1;
			session.files[i - 1].selectedMatchIndex = session.files[i - 1].matches.size() - 1;
			return true;
		}
	if (wrap)
		for (std::size_t i = session.files.size(); i > session.selectedFileIndex + 1; --i)
			if (!session.files[i - 1].matches.empty()) {
				session.selectedFileIndex = i - 1;
				session.files[i - 1].selectedMatchIndex = session.files[i - 1].matches.size() - 1;
				return true;
			}
	return false;
}

bool activateSessionCurrentMatch(MultiFileSearchSession &session) {
	MultiFileSearchFileResult *file = currentSessionFile(session);
	SearchMatchEntry *match = currentSessionMatch(session);
	std::string errorText;
	std::size_t start = 0;
	std::size_t end = 0;

	if (file == nullptr || match == nullptr) return false;
	if (!ensureWindowLoadedForSessionFile(*file, true, errorText)) {
		if (!errorText.empty()) postSearchError(errorText);
		return false;
	}
	if (file->window == nullptr || file->window->getEditor() == nullptr) return false;
	start = match->start;
	end = std::max(match->end, match->start);
	if (end == start) {
		if (end < file->window->getEditor()->bufferLength()) ++end;
		else if (start > 0)
			--start;
	}
	file->window->getEditor()->setCursorOffset(start);
	file->window->getEditor()->setSelectionOffsets(start, end);
	file->window->getEditor()->revealCursor(True);
	syncVmLastSearch(file->window, true, start, end, start);
	{
		MRSearchDialogOptions searchOptions;
		searchOptions.textType = session.regularExpressions ? MRSearchTextType::Pcre : MRSearchTextType::Literal;
		searchOptions.direction = MRSearchDirection::Forward;
		searchOptions.mode = MRSearchMode::StopFirst;
		searchOptions.caseSensitive = session.caseSensitive;
		searchOptions.globalSearch = true;
		searchOptions.restrictToMarkedBlock = false;
		searchOptions.searchAllWindows = false;
		updateMiniMapFindMarkers(file->window, session.pattern, searchOptions);
	}
	return true;
}

bool loadAllSessionFiles(MultiFileSearchSession &session, std::string &errorText) {
	for (MultiFileSearchFileResult &file : session.files) {
		if (!ensureWindowLoadedForSessionFile(file, false, errorText)) return false;
		file.temporaryWindow = false;
	}
	errorText.clear();
	return true;
}

bool replaceCurrentSessionMatch(MultiFileSearchSession &session, bool advanceAfterReplace, std::string &errorText) {
	MultiFileSearchFileResult *file = currentSessionFile(session);
	SearchMatchEntry *match = currentSessionMatch(session);
	MRFileEditor *editor = nullptr;
	std::size_t currentFileIndex = session.selectedFileIndex;
	std::size_t cursorStart = 0;
	std::size_t cursorEnd = 0;

	if (file == nullptr || match == nullptr) return false;
	if (!ensureWindowLoadedForSessionFile(*file, true, errorText)) return false;
	editor = file->window != nullptr ? file->window->getEditor() : nullptr;
	if (editor == nullptr) {
		errorText = "No editor window.";
		return false;
	}
	if (!editor->replaceRangeAndSelect(static_cast<uint>(match->start), static_cast<uint>(match->end), session.replacement.data(), static_cast<uint>(session.replacement.size()))) {
		errorText = "Replace failed.";
		return false;
	}
	cursorStart = match->start;
	cursorEnd = match->start + session.replacement.size();
	editor->setCursorOffset(cursorEnd);
	editor->setSelectionOffsets(cursorEnd, cursorEnd);
	editor->revealCursor(True);
	syncVmLastSearch(file->window, true, cursorStart, cursorEnd, editor->cursorOffset());
	if (!refreshMatchesForSessionFile(session, currentFileIndex, errorText)) return false;
	if (currentFileIndex >= session.files.size()) {
		errorText.clear();
		return true;
	}
	if (session.files[currentFileIndex].matches.empty()) {
		closeTemporarySessionWindow(session.files[currentFileIndex], session.keepFilesOpen);
		static_cast<void>(removeSessionFileAt(session, currentFileIndex));
		if (session.files.empty()) {
			errorText.clear();
			return true;
		}
		if (advanceAfterReplace && session.selectedFileIndex < session.files.size()) {
			if (session.files[session.selectedFileIndex].matches.empty()) moveSessionMatch(session, 1, false);
		}
		errorText.clear();
		return true;
	}
	if (advanceAfterReplace) {
		if (!moveSessionMatch(session, 1, false) && session.selectedFileIndex >= session.files.size()) session.selectedFileIndex = session.files.size() - 1;
	}
	errorText.clear();
	return true;
}

void closeTemporaryWindowsForSession(MultiFileSearchSession &session) {
	for (MultiFileSearchFileResult &file : session.files)
		closeTemporarySessionWindow(file, session.keepFilesOpen);
}

bool showMultiFileSessionCollectionError(const std::string &errorText) {
	if (!errorText.empty()) postSearchError(errorText);
	return true;
}
