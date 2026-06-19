#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TObject
#define Uses_TEvent
#define Uses_TRect
#define Uses_TView
#include <tvision/tv.h>

#include "MRCommandRouterSearchMultiFileCollect.hpp"
#include "MRCommandRouterSearchCore.hpp"

#include <algorithm>
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
#include "../commands/MRWindowCommands.hpp"
#include "../utils/MRFileIOUtils.hpp"
#include "../utils/MRStringUtils.hpp"

namespace {

struct MultiFileSearchCandidate {
	std::string normalizedPath;
	MREditWindow *window = nullptr;
	bool inMemory = false;
};

bool shouldCancelLongRunningSearch() {
	auto pollEscFromTarget = [](TView *target) {
		TEvent event;
		if (target == nullptr) return false;
		while (target->eventAvail()) {
			target->getEvent(event);
			if (event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc)) return true;
			target->putEvent(event);
			break;
		}
		return false;
	};

	if (pollEscFromTarget(TProgram::application != nullptr ? static_cast<TView *>(TProgram::application) : static_cast<TView *>(TProgram::deskTop))) return true;
	return pollEscFromTarget(static_cast<TView *>(TProgram::deskTop));
}

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
	std::filesystem::path relativePath = std::filesystem::relative(candidatePath, startingPath, relEc);
	std::string relativeText = relEc ? std::string() : relativePath.lexically_normal().string();
	std::string fullText = normalizedSearchPath(candidatePath);

	for (char &ch : relativeText)
		if (ch == '\\') ch = '/';
	for (const std::string &token : tokens) {
		const bool hasPathSeparator = token.find('/') != std::string::npos;
		const char *subject = nullptr;

		if (hasPathSeparator) {
			if (!relativeText.empty() && relativeText != "." && relativeText.rfind("../", 0) != 0 && relativeText != "..") subject = relativeText.c_str();
			else
				subject = fullText.c_str();
		} else
			subject = baseName.c_str();
		if (subject != nullptr && fnmatch(token.c_str(), subject, 0) == 0) return true;
	}
	return false;
}

void appendCandidateUnique(const std::filesystem::path &path, bool inMemory, MREditWindow *window, std::vector<MultiFileSearchCandidate> &outCandidates, std::map<std::string, std::size_t> &seen) {
	const std::string normalized = normalizedSearchPath(path);
	auto it = seen.find(normalized);

	if (normalized.empty()) return;
	if (it != seen.end()) {
		MultiFileSearchCandidate &entry = outCandidates[it->second];
		if (inMemory) {
			entry.inMemory = true;
			if (entry.window == nullptr) entry.window = window;
		}
		return;
	}
	seen.emplace(normalized, outCandidates.size());
	outCandidates.push_back(MultiFileSearchCandidate{normalized, window, inMemory});
}

std::vector<MultiFileSearchCandidate> collectMultiFileSearchCandidates(const MRMultiSearchDialogOptions &options) {
	std::vector<MultiFileSearchCandidate> candidates;
	std::map<std::string, std::size_t> seen;
	std::filesystem::path startingPath = normalizeConfiguredPathInput(options.startingPath);
	std::vector<std::string> filespecTokens = splitFilespecTokens(options.filespec);
	std::error_code ec;

	if (startingPath.empty()) {
		startingPath = std::filesystem::current_path(ec);
		if (ec) startingPath = ".";
	}
	startingPath = startingPath.lexically_normal();
	if (options.restrictToWorkspace) {
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		for (MREditWindow *window : windows) {
			MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			std::filesystem::path windowPath;

			if (editor == nullptr || !editor->hasPersistentFileName()) continue;
			windowPath = editor->persistentFileName();
			if (windowPath.empty()) continue;
			if (!filespecMatchesPath(windowPath, startingPath, filespecTokens)) continue;
			appendCandidateUnique(windowPath, true, window, candidates, seen);
		}
	} else if (std::filesystem::is_regular_file(startingPath, ec) && !ec) {
		if (filespecMatchesPath(startingPath, startingPath.parent_path(), filespecTokens)) appendCandidateUnique(startingPath, false, nullptr, candidates, seen);
	} else if (std::filesystem::is_directory(startingPath, ec) && !ec) {
		if (options.searchSubdirectories) {
			std::filesystem::recursive_directory_iterator it(startingPath, std::filesystem::directory_options::skip_permission_denied, ec);
			const std::filesystem::recursive_directory_iterator end;
			for (; !ec && it != end; it.increment(ec)) {
				if (!it->is_regular_file(ec) || ec) {
					ec.clear();
					continue;
				}
				if (!filespecMatchesPath(it->path(), startingPath, filespecTokens)) continue;
				appendCandidateUnique(it->path(), false, nullptr, candidates, seen);
			}
		} else {
			std::filesystem::directory_iterator it(startingPath, std::filesystem::directory_options::skip_permission_denied, ec);
			const std::filesystem::directory_iterator end;
			for (; !ec && it != end; it.increment(ec)) {
				if (!it->is_regular_file(ec) || ec) {
					ec.clear();
					continue;
				}
				if (!filespecMatchesPath(it->path(), startingPath, filespecTokens)) continue;
				appendCandidateUnique(it->path(), false, nullptr, candidates, seen);
			}
		}
	}

	if (!options.restrictToWorkspace && options.searchFilesInMemory) {
		std::vector<MREditWindow *> windows = allEditWindowsInZOrder();
		for (MREditWindow *window : windows) {
			MRFileEditor *editor = window != nullptr ? window->getEditor() : nullptr;
			std::filesystem::path windowPath;
			if (editor == nullptr || !editor->hasPersistentFileName()) continue;
			windowPath = editor->persistentFileName();
			if (windowPath.empty()) continue;
			if (!filespecMatchesPath(windowPath, startingPath, filespecTokens)) continue;
			appendCandidateUnique(windowPath, true, window, candidates, seen);
		}
	}

	std::sort(candidates.begin(), candidates.end(), [](const MultiFileSearchCandidate &lhs, const MultiFileSearchCandidate &rhs) { return lhs.normalizedPath < rhs.normalizedPath; });
	return candidates;
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

MultiFileCollectOutcome collectMultiFileSession(MultiFileSearchSession &session, const MRMultiSearchDialogOptions &options, const std::string &pattern, const std::string &replacement, bool replaceMode, bool keepFilesOpen, std::string &errorText) {
	std::vector<MultiFileSearchCandidate> candidates = collectMultiFileSearchCandidates(options);
	pcre2_code *code = nullptr;
	std::string regexError;
	const MRSearchTextType textType = options.regularExpressions ? MRSearchTextType::Pcre : MRSearchTextType::Literal;
	const std::string patternExpression = buildSearchPatternExpression(pattern, textType);
	std::size_t filesSearched = 0;
	std::size_t totalHits = 0;
	bool cancelled = false;
	auto lastProgressAt = std::chrono::steady_clock::now();

	session = MultiFileSearchSession();
	session.pattern = pattern;
	session.replacement = replacement;
	session.caseSensitive = options.caseSensitive;
	session.regularExpressions = options.regularExpressions;
	session.replaceMode = replaceMode;
	session.keepFilesOpen = keepFilesOpen;

	if (!compileSearchRegex(patternExpression, !options.caseSensitive, &code, regexError)) {
		errorText = "Invalid search pattern: " + regexError;
		return MultiFileCollectOutcome::Error;
	}
	if (code == nullptr) {
		errorText = "Unable to compile search pattern.";
		return MultiFileCollectOutcome::Error;
	}
	for (const MultiFileSearchCandidate &candidate : candidates) {
		MultiFileSearchFileResult file;
		std::string text;
		std::string readError;
		std::vector<SearchMatchEntry> matches;

		if (shouldCancelLongRunningSearch()) {
			cancelled = true;
			break;
		}
		if (candidate.window != nullptr && candidate.window->getEditor() != nullptr) text = candidate.window->getEditor()->snapshotText();
		else if (!readTextFile(candidate.normalizedPath, text, readError)) {
			if (readError.empty()) readError = "Unable to read file: " + candidate.normalizedPath;
			errorText = readError;
			pcre2_code_free(code);
			return MultiFileCollectOutcome::Error;
		}
		++filesSearched;
		static_cast<void>(collectRegexMatches(text, code, matches));
		totalHits += matches.size();
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - lastProgressAt >= std::chrono::seconds(5)) {
				postMultiSearchProgress(filesSearched, totalHits);
				lastProgressAt = now;
			}
		}
		if (matches.empty()) continue;
		file.normalizedPath = candidate.normalizedPath;
		file.fileName = baseNameFromPath(candidate.normalizedPath);
		file.matches.swap(matches);
		file.selectedMatchIndex = 0;
		file.startedInMemory = candidate.inMemory;
		file.window = candidate.window;
		session.files.push_back(file);
	}
	if (code != nullptr) pcre2_code_free(code);
	postMultiSearchProgress(filesSearched, totalHits);
	if (cancelled) postSearchCancelledError();
	if (session.files.empty()) {
		errorText.clear();
		return cancelled ? MultiFileCollectOutcome::Cancelled : MultiFileCollectOutcome::NoHits;
	}
	session.valid = true;
	session.selectedFileIndex = 0;
	errorText.clear();
	return cancelled ? MultiFileCollectOutcome::Cancelled : MultiFileCollectOutcome::Success;
}
