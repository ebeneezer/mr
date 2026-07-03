#ifndef MRCOMMANDROUTERSEARCHMULTIFILESESSION_HPP
#define MRCOMMANDROUTERSEARCHMULTIFILESESSION_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "MRCommandRouterSearchCore.hpp"
#include "../../dialogs/setup/MRSetupCommon.hpp"

class MREditWindow;

struct MultiFileSearchFileResult {
	std::string normalizedPath;
	std::string fileName;
	std::vector<SearchMatchEntry> matches;
	std::size_t selectedMatchIndex = 0;
	bool startedInMemory = false;
	bool temporaryWindow = false;
	MREditWindow *window = nullptr;
};

struct MultiFileSearchSession {
	bool valid = false;
	bool replaceMode = false;
	bool caseSensitive = false;
	bool wholeWords = false;
	bool regularExpressions = true;
	bool keepFilesOpen = false;
	std::string pattern;
	std::string replacement;
	std::vector<MultiFileSearchFileResult> files;
	std::size_t selectedFileIndex = 0;
};

enum class MultiFileCollectOutcome : unsigned char {
	Success = 0,
	NoHits = 1,
	Cancelled = 2,
	Error = 3
};

enum class MultiDialogAction : unsigned char {
	Cancel = 0,
	Done = 1,
	Replace = 2,
	ReplaceAll = 3,
	Skip = 4,
	Load = 5,
	LoadAll = 6
};

void postSearchWarning(std::string_view text);
void postSearchError(std::string_view text);
void postDialogWarning(std::string_view text);
void persistSearchDialogSettingsSnapshot();
void postNoHitsWarning();
void postMultiSearchStartedWarning();
void postSearchCancelledError();
void postMultiSearchProgress(std::size_t filesSearched, std::size_t totalHits);

std::string baseNameFromPath(const std::string &path);
bool loadSessionFileText(const MultiFileSearchFileResult &file, std::string &outText, std::string &errorText);
MultiFileSearchFileResult *currentSessionFile(MultiFileSearchSession &session);
SearchMatchEntry *currentSessionMatch(MultiFileSearchSession &session);
std::size_t sessionTotalMatchCount(const MultiFileSearchSession &session);
std::size_t sessionCurrentMatchOrdinal(const MultiFileSearchSession &session);
bool activateSessionCurrentMatch(MultiFileSearchSession &session);
bool previewSessionCurrentMatch(MultiFileSearchSession &session);
bool loadAllSessionFiles(MultiFileSearchSession &session, std::string &errorText);
bool moveSessionMatch(MultiFileSearchSession &session, int direction, bool wrap);
void closeTemporaryWindowsForSession(MultiFileSearchSession &session);
bool replaceCurrentSessionMatch(MultiFileSearchSession &session, bool advanceAfterReplace, std::string &errorText);
bool showMultiFileSessionCollectionError(const std::string &errorText);

bool promptMultiFileSearchValues(const std::string &patternSeed, std::string &pattern, MRMultiSearchDialogOptions &options, MultiFileSearchSession &outSession);
bool promptMultiFileSarValues(const std::string &patternSeed, const std::string &replacementSeed, std::string &pattern, std::string &replacement, MRMultiSarDialogOptions &options, MultiFileSearchSession &outSession);
MultiDialogAction runMultiFileResultsDialog(MultiFileSearchSession &session);

#endif
