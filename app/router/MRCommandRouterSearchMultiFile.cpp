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
#include <string>
#include <string_view>

#include "../../config/settings/MRSettingsStorage.hpp"
#include "../../ui/MRMessageLineController.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../MREditorApp.hpp"
#include "../commands/MRWindowCommands.hpp"

static MultiFileSearchSession g_lastMultiFileSearchSession;
static constexpr const char *kNoPreviousMultiFileSearchListMessage = "No previous multi-file search list.";

void postSearchWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text), mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

void postSearchError(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::HeroEventFollowup, std::string(text), mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

void postDialogWarning(std::string_view text) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, std::string(text), mr::messageline::Kind::Warning, mr::messageline::kPriorityMedium);
}

void persistSearchDialogSettingsSnapshot() {
	std::string errorText;

	if (!persistConfiguredSettingsSnapshot(&errorText) && !errorText.empty()) postSearchError("Settings save failed: " + errorText);
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
	return g_lastMultiFileSearchSession.valid && !g_lastMultiFileSearchSession.files.empty();
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
		g_lastMultiFileSearchSession = session;
		switch (runMultiFileResultsDialog(g_lastMultiFileSearchSession)) {
			case MultiDialogAction::Done:
				if (previousWindow != nullptr) static_cast<void>(mrActivateEditWindow(previousWindow));
				return true;
			case MultiDialogAction::Load:
				static_cast<void>(activateSessionCurrentMatch(g_lastMultiFileSearchSession));
				return true;
			case MultiDialogAction::LoadAll:
				if (!loadAllSessionFiles(g_lastMultiFileSearchSession, errorText)) {
					if (!errorText.empty()) postSearchError(errorText);
					closeTemporaryWindowsForSession(g_lastMultiFileSearchSession);
					return true;
				}
				static_cast<void>(activateSessionCurrentMatch(g_lastMultiFileSearchSession));
				return true;
			case MultiDialogAction::Cancel:
				continue;
			default:
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
	std::string errorText;
	MREditWindow *previousWindow = currentEditWindow();

	if (!hasPreviousMultiFileSearchResults()) {
		postDialogWarning(kNoPreviousMultiFileSearchListMessage);
		return true;
	}
	action = runMultiFileResultsDialog(g_lastMultiFileSearchSession);
	if (action == MultiDialogAction::Load) return activateSessionCurrentMatch(g_lastMultiFileSearchSession);
	if (action == MultiDialogAction::LoadAll) {
		if (!loadAllSessionFiles(g_lastMultiFileSearchSession, errorText)) {
			if (!errorText.empty()) postSearchError(errorText);
			closeTemporaryWindowsForSession(g_lastMultiFileSearchSession);
			return true;
		}
		return activateSessionCurrentMatch(g_lastMultiFileSearchSession);
	}
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
		g_lastMultiFileSearchSession = session;
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
	if (!hasPreviousMultiFileSearchResults()) {
		postDialogWarning(kNoPreviousMultiFileSearchListMessage);
		return true;
	}
	if (!moveSessionMatch(g_lastMultiFileSearchSession, 1, true)) return true;
	return activateSessionCurrentMatch(g_lastMultiFileSearchSession);
}
