#include "MRCommandRouterSearchMultiFileCollect.hpp"
#include "MRCommandRouterSearchCore.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fnmatch.h>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../../ui/MREditWindow.hpp"
#include "../../ui/MRWindowSupport.hpp"
#include "../utils/MRFileIOUtils.hpp"
#include "../utils/MRStringUtils.hpp"

namespace {

constexpr std::size_t kSnapshotCopyChunkSize = 64 * 1024;
constexpr std::chrono::milliseconds kProgressInterval(50);

struct MultiFileSearchCandidate {
	std::string normalizedPath;
	bool inMemory = false;
	std::size_t documentId = 0;
	std::size_t version = 0;
	mr::editor::ReadSnapshot snapshot;
};

class SearchProgressReporter {
  public:
	explicit SearchProgressReporter(const mr::coprocessor::TaskInfo &taskInfo) : taskInfo(taskInfo), lastPostAt(std::chrono::steady_clock::now() - kProgressInterval) {
	}

	void post(std::string phase, std::size_t completed, std::size_t total, std::size_t hits, bool force = false) {
		const auto now = std::chrono::steady_clock::now();
		if (!force && now - lastPostAt < kProgressInterval) return;

		mr::coprocessor::Result result;
		result.task = taskInfo;
		result.status = mr::coprocessor::TaskStatus::Completed;
		result.payload = std::make_shared<mr::coprocessor::TaskProgressPayload>(completed, total, hits, std::move(phase));
		mr::coprocessor::globalCoprocessor().post(std::move(result));
		lastPostAt = now;
	}

  private:
	const mr::coprocessor::TaskInfo &taskInfo;
	std::chrono::steady_clock::time_point lastPostAt;
};

std::vector<std::string> splitFilespecTokens(std::string_view literal) {
	std::vector<std::string> tokens;
	std::istringstream in{std::string(literal)};
	std::string token;

	while (in >> token) {
		token = trimAscii(token);
		if (token.empty()) continue;
		for (char &ch : token)
			if (ch == '\\') ch = '/';
		tokens.push_back(token);
	}
	if (tokens.empty()) tokens.push_back("*.*");
	return tokens;
}

bool filespecMatchesPath(const std::filesystem::path &candidatePath, const std::filesystem::path &startingPath, const std::vector<std::string> &tokens) {
	std::string baseName = candidatePath.filename().string();
	std::error_code relEc;
	std::string relativeText;
	std::string fullText;

	if (!startingPath.empty()) {
		const std::filesystem::path relativePath = std::filesystem::relative(candidatePath, startingPath, relEc);
		if (!relEc) relativeText = relativePath.lexically_normal().string();
	}
	for (char &ch : relativeText)
		if (ch == '\\') ch = '/';
	for (const std::string &token : tokens) {
		const bool hasPathSeparator = token.find('/') != std::string::npos;
		const char *subject = nullptr;

		if (hasPathSeparator) {
			if (!relativeText.empty() && relativeText != "." && relativeText.rfind("../", 0) != 0 && relativeText != "..") subject = relativeText.c_str();
			else {
				if (fullText.empty()) fullText = normalizedSearchPath(candidatePath);
				subject = fullText.c_str();
			}
		} else
			subject = baseName.c_str();
		if (subject != nullptr && fnmatch(token.c_str(), subject, 0) == 0) return true;
	}
	return false;
}

void appendCandidateUnique(const std::filesystem::path &path, const MultiFileSearchMemorySource *memorySource, std::vector<MultiFileSearchCandidate> &outCandidates, std::map<std::string, std::size_t> &seen) {
	const std::string normalized = memorySource != nullptr ? memorySource->normalizedPath : normalizedSearchPath(path);
	auto it = seen.find(normalized);

	if (normalized.empty()) return;
	if (it != seen.end()) {
		if (memorySource != nullptr) {
			MultiFileSearchCandidate &candidate = outCandidates[it->second];
			candidate.inMemory = true;
			candidate.documentId = memorySource->documentId;
			candidate.version = memorySource->version;
			candidate.snapshot = memorySource->snapshot;
		}
		return;
	}

	MultiFileSearchCandidate candidate;
	candidate.normalizedPath = normalized;
	if (memorySource != nullptr) {
		candidate.inMemory = true;
		candidate.documentId = memorySource->documentId;
		candidate.version = memorySource->version;
		candidate.snapshot = memorySource->snapshot;
	}
	seen.emplace(normalized, outCandidates.size());
	outCandidates.push_back(std::move(candidate));
}

bool collectMultiFileSearchCandidates(const MRMultiSearchDialogOptions &options, const std::vector<MultiFileSearchMemorySource> &memorySources, const mr::coprocessor::TaskInfo &info, SearchProgressReporter &progress, std::vector<MultiFileSearchCandidate> &candidates) {
	std::map<std::string, std::size_t> seen;
	const std::vector<std::string> filespecTokens = splitFilespecTokens(options.filespec);
	std::filesystem::path startingPath;
	std::error_code ec;

	candidates.clear();
	progress.post("Collecting", 0, 0, 0, true);

	if (options.restrictToWorkspace) {
		for (const MultiFileSearchMemorySource &source : memorySources) {
			if (info.cancelRequested()) return false;
			if (!filespecMatchesPath(source.normalizedPath, std::filesystem::path(), filespecTokens)) continue;
			appendCandidateUnique(source.normalizedPath, &source, candidates, seen);
			progress.post("Collecting", candidates.size(), 0, 0);
		}
	} else {
		startingPath = normalizeConfiguredPathInput(options.startingPath);
		if (startingPath.empty()) {
			startingPath = std::filesystem::current_path(ec);
			if (ec) startingPath = ".";
		}
		startingPath = startingPath.lexically_normal();
		if (std::filesystem::is_regular_file(startingPath, ec) && !ec) {
			if (filespecMatchesPath(startingPath, startingPath.parent_path(), filespecTokens)) appendCandidateUnique(startingPath, nullptr, candidates, seen);
		} else {
			ec.clear();
			if (std::filesystem::is_directory(startingPath, ec) && !ec) {
				if (options.searchSubdirectories) {
					std::filesystem::recursive_directory_iterator it(startingPath, std::filesystem::directory_options::skip_permission_denied, ec);
					const std::filesystem::recursive_directory_iterator end;
					for (; !ec && it != end; it.increment(ec)) {
						if (info.cancelRequested()) return false;
						if (!it->is_regular_file(ec) || ec) {
							ec.clear();
							continue;
						}
						if (filespecMatchesPath(it->path(), startingPath, filespecTokens)) appendCandidateUnique(it->path(), nullptr, candidates, seen);
						progress.post("Collecting", candidates.size(), 0, 0);
					}
				} else {
					std::filesystem::directory_iterator it(startingPath, std::filesystem::directory_options::skip_permission_denied, ec);
					const std::filesystem::directory_iterator end;
					for (; !ec && it != end; it.increment(ec)) {
						if (info.cancelRequested()) return false;
						if (!it->is_regular_file(ec) || ec) {
							ec.clear();
							continue;
						}
						if (filespecMatchesPath(it->path(), startingPath, filespecTokens)) appendCandidateUnique(it->path(), nullptr, candidates, seen);
						progress.post("Collecting", candidates.size(), 0, 0);
					}
				}
			}
		}

		if (options.searchFilesInMemory) {
			for (const MultiFileSearchMemorySource &source : memorySources) {
				if (info.cancelRequested()) return false;
				if (!filespecMatchesPath(source.normalizedPath, startingPath, filespecTokens)) continue;
				appendCandidateUnique(source.normalizedPath, &source, candidates, seen);
			}
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const MultiFileSearchCandidate &lhs, const MultiFileSearchCandidate &rhs) { return lhs.normalizedPath < rhs.normalizedPath; });
	progress.post("Collecting", candidates.size(), candidates.size(), 0, true);
	return !info.cancelRequested();
}

bool materializeSnapshotCancellable(const mr::editor::ReadSnapshot &snapshot, const std::atomic_bool &cancelFlag, std::string &text) {
	text.clear();
	text.reserve(snapshot.length());
	for (std::size_t pieceIndex = 0; pieceIndex < snapshot.pieceCount(); ++pieceIndex) {
		const mr::editor::PieceChunkView piece = snapshot.pieceChunk(pieceIndex);
		std::size_t offset = 0;

		while (offset < piece.length) {
			if (cancelFlag.load(std::memory_order_acquire)) {
				text.clear();
				return false;
			}
			const std::size_t length = std::min(kSnapshotCopyChunkSize, piece.length - offset);
			text.append(piece.data + offset, length);
			offset += length;
		}
	}
	return !cancelFlag.load(std::memory_order_acquire);
}

MultiFileCollectOutcome collectMultiFileSession(MultiFileSearchSession &session, const MRMultiSearchDialogOptions &options, const std::vector<MultiFileSearchMemorySource> &memorySources, const std::string &pattern, const std::string &replacement, bool replaceMode, bool keepFilesOpen, const mr::coprocessor::TaskInfo &info, std::string &errorText) {
	std::atomic_bool fallbackCancel(false);
	const std::atomic_bool &cancelFlag = info.cancelFlag != nullptr ? *info.cancelFlag : fallbackCancel;
	SearchProgressReporter progress(info);
	std::vector<MultiFileSearchCandidate> candidates;
	pcre2_code *code = nullptr;
	std::string regexError;
	const MRSearchTextType textType = options.wholeWords ? MRSearchTextType::Word : (options.regularExpressions ? MRSearchTextType::Pcre : MRSearchTextType::Literal);
	const std::string patternExpression = buildSearchPatternExpression(pattern, textType);
	std::size_t filesSearched = 0;
	std::size_t totalHits = 0;

	session = MultiFileSearchSession();
	session.pattern = pattern;
	session.replacement = replacement;
	session.caseSensitive = options.caseSensitive;
	session.wholeWords = options.wholeWords;
	session.regularExpressions = options.regularExpressions;
	session.replaceMode = replaceMode;
	session.keepFilesOpen = keepFilesOpen;

	if (!compileSearchRegex(patternExpression, !options.caseSensitive, &code, regexError, true)) {
		errorText = "Invalid search pattern: " + regexError;
		return MultiFileCollectOutcome::Error;
	}
	if (code == nullptr) {
		errorText = "Unable to compile search pattern.";
		return MultiFileCollectOutcome::Error;
	}
	if (!collectMultiFileSearchCandidates(options, memorySources, info, progress, candidates)) {
		pcre2_code_free(code);
		errorText.clear();
		return MultiFileCollectOutcome::Cancelled;
	}

	progress.post("Searching", 0, candidates.size(), 0, true);
	for (const MultiFileSearchCandidate &candidate : candidates) {
		MultiFileSearchFileResult file;
		std::string text;
		std::string readError;
		std::vector<SearchMatchEntry> matches;
		bool readCancelled = false;

		if (info.cancelRequested()) {
			pcre2_code_free(code);
			errorText.clear();
			return MultiFileCollectOutcome::Cancelled;
		}
		if (candidate.inMemory) {
			if (!materializeSnapshotCancellable(candidate.snapshot, cancelFlag, text)) {
				pcre2_code_free(code);
				errorText.clear();
				return MultiFileCollectOutcome::Cancelled;
			}
		} else if (!readTextFileCancellable(candidate.normalizedPath, text, readError, cancelFlag, readCancelled)) {
			pcre2_code_free(code);
			if (readCancelled) {
				errorText.clear();
				return MultiFileCollectOutcome::Cancelled;
			}
			errorText = readError.empty() ? "Unable to read file: " + candidate.normalizedPath : readError;
			return MultiFileCollectOutcome::Error;
		}
		++filesSearched;
		const RegexCollectOutcome matchOutcome = collectRegexMatchesCancellable(text, code, matches, cancelFlag);
		if (matchOutcome == RegexCollectOutcome::Cancelled) {
			pcre2_code_free(code);
			errorText.clear();
			return MultiFileCollectOutcome::Cancelled;
		}
		if (matchOutcome == RegexCollectOutcome::Error) {
			pcre2_code_free(code);
			errorText = "Unable to allocate regex match state.";
			return MultiFileCollectOutcome::Error;
		}
		totalHits += matches.size();
		if (!matches.empty()) {
			file.normalizedPath = candidate.normalizedPath;
			file.fileName = baseNameFromPath(candidate.normalizedPath);
			file.matches.swap(matches);
			file.startedInMemory = candidate.inMemory;
			file.startedDocumentId = candidate.documentId;
			file.startedDocumentVersion = candidate.version;
			session.files.push_back(std::move(file));
		}
		progress.post("Searching", filesSearched, candidates.size(), totalHits);
	}
	pcre2_code_free(code);
	progress.post("Searching", filesSearched, candidates.size(), totalHits, true);
	if (session.files.empty()) {
		errorText.clear();
		return MultiFileCollectOutcome::NoHits;
	}
	session.valid = true;
	session.selectedFileIndex = 0;
	errorText.clear();
	return MultiFileCollectOutcome::Success;
}

} // namespace

std::string normalizedSearchPath(const std::filesystem::path &path) {
	std::error_code ec;
	std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);

	if (ec || normalized.empty()) {
		ec.clear();
		normalized = std::filesystem::absolute(path, ec);
	}
	if (ec || normalized.empty()) normalized = path.lexically_normal();
	std::string result = normalized.lexically_normal().string();
	for (char &ch : result)
		if (ch == '\\') ch = '/';
	return result;
}

void captureMultiFileSearchMemorySources(std::vector<MultiFileSearchMemorySource> &outSources) {
	const std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
	std::map<std::string, std::size_t> seen;

	outSources.clear();
	outSources.reserve(windows.size());
	for (MREditWindow *window : windows) {
		MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
		if (editor == nullptr || !editor->hasPersistentFileName()) continue;
		const std::string normalizedPath = normalizedSearchPath(editor->persistentFileName());
		if (normalizedPath.empty() || seen.find(normalizedPath) != seen.end()) continue;

		MultiFileSearchMemorySource source;
		source.normalizedPath = normalizedPath;
		source.documentId = editor->documentId();
		source.version = editor->documentVersion();
		source.snapshot = editor->readSnapshot();
		seen.emplace(normalizedPath, outSources.size());
		outSources.push_back(std::move(source));
	}
}

mr::coprocessor::Result runMultiFileSearchTask(const mr::coprocessor::TaskInfo &info, const MRMultiSearchDialogOptions &options, const std::vector<MultiFileSearchMemorySource> &memorySources, const std::string &pattern, const std::string &replacement, bool replaceMode, bool keepFilesOpen) {
	mr::coprocessor::Result result;
	std::shared_ptr<MultiFileSearchSession> session = std::make_shared<MultiFileSearchSession>();
	std::string errorText;
	const MultiFileCollectOutcome outcome = collectMultiFileSession(*session, options, memorySources, pattern, replacement, replaceMode, keepFilesOpen, info, errorText);

	result.task = info;
	if (outcome == MultiFileCollectOutcome::Cancelled) result.status = mr::coprocessor::TaskStatus::Cancelled;
	else if (outcome == MultiFileCollectOutcome::Error) {
		result.status = mr::coprocessor::TaskStatus::Failed;
		result.error = errorText;
	} else
		result.status = mr::coprocessor::TaskStatus::Completed;
	result.payload = std::make_shared<MultiFileSearchFinishedPayload>(outcome, std::move(session), std::move(errorText));
	return result;
}
