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
	std::size_t startedDocumentId = 0;
	std::size_t startedDocumentVersion = 0;
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

struct MultiFileReplaceCheckpoint {
	MREditWindow *window = nullptr;
	std::size_t documentId = 0;
	std::size_t expectedVersion = 0;
	std::size_t undoDepth = 0;
	std::size_t replacements = 0;
	bool temporaryWindow = false;
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

enum class MultiReplaceAllOutcome : unsigned char {
	Completed = 0,
	Aborted = 1,
	Reverted = 2,
	Error = 3
};

void postSearchWarning(std::string_view text);
void postSearchError(std::string_view text);
void postDialogWarning(std::string_view text);
void postNoHitsWarning();
void postMultiSearchStartedWarning();
void postSearchCancelledError();

std::string baseNameFromPath(const std::string &path);
bool loadSessionFileText(const MultiFileSearchFileResult &file, std::string &outText, std::string &errorText);
MultiFileSearchFileResult *currentSessionFile(MultiFileSearchSession &session);
SearchMatchEntry *currentSessionMatch(MultiFileSearchSession &session);
std::size_t sessionTotalMatchCount(const MultiFileSearchSession &session);
std::size_t sessionCurrentMatchOrdinal(const MultiFileSearchSession &session);
bool activateSessionCurrentMatch(MultiFileSearchSession &session);
bool previewSessionCurrentMatch(MultiFileSearchSession &session);
bool loadAllSessionFiles(MultiFileSearchSession &session, std::string &errorText);
bool validateMultiFileSessionSources(MultiFileSearchSession &session, std::string &errorText);
bool moveSessionMatch(MultiFileSearchSession &session, int direction, bool wrap);
void closeTemporaryWindowsForSession(MultiFileSearchSession &session);
bool replaceCurrentSessionMatch(MultiFileSearchSession &session, bool advanceAfterReplace, std::string &errorText);
bool replaceAllSessionMatchesInFile(MultiFileSearchSession &session, std::size_t fileIndex, MultiFileReplaceCheckpoint &checkpoint, std::size_t &replacementCount, std::string &errorText);
bool validateSessionFileReplaceCheckpoint(const MultiFileSearchSession &session, std::size_t fileIndex, const MultiFileReplaceCheckpoint &checkpoint, std::string &errorText);
bool revertSessionFileReplacements(MultiFileSearchSession &session, std::size_t fileIndex, MultiFileReplaceCheckpoint &checkpoint, std::string &errorText);
bool showMultiFileSessionCollectionError(const std::string &errorText);

bool promptMultiFileSearchValues(const std::string &patternSeed, std::string &pattern, MRMultiSearchDialogOptions &options, MultiFileSearchSession &outSession);
bool promptMultiFileSarValues(const std::string &patternSeed, const std::string &replacementSeed, std::string &pattern, std::string &replacement, MRMultiSarDialogOptions &options, MultiFileSearchSession &outSession);
MultiDialogAction runMultiFileResultsDialog(MultiFileSearchSession &session);
MultiReplaceAllOutcome runMultiFileReplaceAllDialog(MultiFileSearchSession &session, std::size_t &completedCount, std::string &errorText);

#endif
