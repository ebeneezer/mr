#include "MRWorkspaceServiceContext.hpp"

#include "../commands/MRWindowCommands.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../ui/MREditWindow.hpp"

#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace mr::services {
namespace {
void appendCompileContextEntry(std::vector<MRWorkspaceCompileContextEntry> &entries, const std::string &value, const std::string &source);

std::string directoryForPath(const std::string &path) {
	std::filesystem::path fsPath(path);
	std::filesystem::path parent = fsPath.parent_path();

	if (parent.empty()) return ".";
	return parent.lexically_normal().generic_string();
}

std::string trimAsciiLocal(const std::string &text) {
	std::size_t first = 0;
	std::size_t last = text.size();

	while (first < last && static_cast<unsigned char>(text[first]) <= ' ')
		++first;
	while (last > first && static_cast<unsigned char>(text[last - 1]) <= ' ')
		--last;
	return text.substr(first, last - first);
}

bool pathIsSameOrUnder(const std::string &parent, const std::string &path) {
	if (parent.empty()) return false;
	if (parent == path) return true;
	if (parent == "/") return !path.empty() && path.front() == '/';
	if (path.size() <= parent.size()) return false;
	if (path.compare(0, parent.size(), parent) != 0) return false;
	return path[parent.size()] == '/';
}

std::string joinWorkspaceRootPath(const std::string &rootPath, const std::string &path) {
	std::filesystem::path fsPath(path);

	if (path.empty()) return std::string();
	if (fsPath.is_absolute()) return fsPath.lexically_normal().generic_string();
	return (std::filesystem::path(rootPath) / fsPath).lexically_normal().generic_string();
}

std::string makefileAssignmentOperator(const std::string &line, std::size_t &operatorPos) {
	static const char *operators[] = { "+=", "?=", ":=", "=" };

	for (const char *op : operators) {
		operatorPos = line.find(op);
		if (operatorPos != std::string::npos) return op;
	}
	operatorPos = std::string::npos;
	return std::string();
}

void readMakefileVariables(const std::string &makefilePath, std::map<std::string, std::string> &variables) {
	std::ifstream input(makefilePath);
	std::string physical;
	std::string logical;

	while (std::getline(input, physical)) {
		if (!physical.empty() && physical.back() == '\r') physical.pop_back();
		if (!physical.empty() && physical.back() == '\\') {
			physical.pop_back();
			logical += physical;
			logical += ' ';
			continue;
		}
		logical += physical;
		const std::size_t comment = logical.find('#');
		std::size_t operatorPos = std::string::npos;
		std::string op;
		std::string left;
		std::string right;

		if (comment != std::string::npos) logical.resize(comment);
		op = makefileAssignmentOperator(logical, operatorPos);
		if (!op.empty()) {
			left = trimAsciiLocal(logical.substr(0, operatorPos));
			right = trimAsciiLocal(logical.substr(operatorPos + op.size()));
			if (!left.empty()) {
				if (op == "+=") {
					if (!variables[left].empty() && !right.empty()) variables[left] += ' ';
					variables[left] += right;
				} else if (op != "?=" || variables.find(left) == variables.end())
					variables[left] = right;
			}
		}
		logical.clear();
	}
}

std::string resolveMakefileVariables(const std::string &value, const std::map<std::string, std::string> &variables) {
	std::string resolved = value;

	for (int pass = 0; pass < 8; ++pass) {
		std::string next;
		bool changed = false;

		for (std::size_t index = 0; index < resolved.size();) {
			if (resolved[index] == '$' && index + 2 < resolved.size() && resolved[index + 1] == '(') {
				const std::size_t end = resolved.find(')', index + 2);
				std::string name;
				std::map<std::string, std::string>::const_iterator found;

				if (end == std::string::npos) {
					next += resolved.substr(index);
					index = resolved.size();
					continue;
				}
				name = resolved.substr(index + 2, end - index - 2);
				found = variables.find(name);
				if (found != variables.end()) next += found->second;
				changed = true;
				index = end + 1;
				continue;
			}
			next.push_back(resolved[index]);
			++index;
		}
		resolved = next;
		if (!changed) break;
	}
	return resolved;
}

bool splitMakefileWords(const std::string &text, std::vector<std::string> &words) {
	enum QuoteMode {
		qmNone = 0,
		qmSingle,
		qmDouble
	};
	QuoteMode quoteMode = qmNone;
	std::string current;
	bool hasCurrent = false;

	for (std::size_t index = 0; index < text.size(); ++index) {
		const char ch = text[index];

		if (quoteMode == qmSingle) {
			if (ch == '\'') quoteMode = qmNone;
			else {
				current.push_back(ch);
				hasCurrent = true;
			}
			continue;
		}
		if (quoteMode == qmDouble) {
			if (ch == '"') {
				quoteMode = qmNone;
				continue;
			}
			current.push_back(ch);
			hasCurrent = true;
			continue;
		}
		if (ch == '\'') {
			quoteMode = qmSingle;
			hasCurrent = true;
			continue;
		}
		if (ch == '"') {
			quoteMode = qmDouble;
			hasCurrent = true;
			continue;
		}
		if (static_cast<unsigned char>(ch) <= ' ') {
			if (hasCurrent) {
				words.push_back(current);
				current.clear();
				hasCurrent = false;
			}
			continue;
		}
		current.push_back(ch);
		hasCurrent = true;
	}
	if (quoteMode != qmNone) return false;
	if (hasCurrent) words.push_back(current);
	return true;
}

void appendMakefileIncludeFlags(std::vector<MRWorkspaceCompileContextEntry> &entries, const std::string &rootPath, const std::string &flagText) {
	std::vector<std::string> words;

	if (!splitMakefileWords(flagText, words)) return;
	for (std::size_t index = 0; index < words.size(); ++index) {
		std::string path;

		if (words[index] == "-I" && index + 1 < words.size()) {
			++index;
			path = words[index];
		} else if (words[index].rfind("-I", 0) == 0 && words[index].size() > 2)
			path = words[index].substr(2);
		if (path.empty() || path[0] == '$') continue;
		appendCompileContextEntry(entries, joinWorkspaceRootPath(rootPath, path), "Makefile");
	}
}

void appendMakefileIncludePaths(MRWorkspaceCompileContext &context, const MRServiceRootContext &root) {
	static const char *flagVariables[] = { "INCLUDES", "CPPFLAGS", "CFLAGS", "CXXFLAGS" };
	std::map<std::string, std::string> variables;
	const std::string rootPath = normalizeWorkspaceServicePath(root.rootPath);
	const std::string makefilePath = joinWorkspaceRootPath(rootPath, "Makefile");

	if (!root.hasRoot || rootPath.empty() || !std::filesystem::is_regular_file(makefilePath)) return;
	readMakefileVariables(makefilePath, variables);
	for (const char *name : flagVariables) {
		const std::map<std::string, std::string>::const_iterator found = variables.find(name);

		if (found == variables.end()) continue;
		appendMakefileIncludeFlags(context.includePaths, rootPath, resolveMakefileVariables(found->second, variables));
	}
}

std::string parentDirectoryOfDirectory(const std::string &path) {
	std::filesystem::path fsPath(path);
	std::filesystem::path parent = fsPath.parent_path();

	if (parent.empty()) return ".";
	return parent.lexically_normal().generic_string();
}

std::string commonDirectoryForDocuments(const std::vector<MRWorkspaceDocumentSnapshot> &documents) {
	std::string common;
	bool initialized = false;

	for (const MRWorkspaceDocumentSnapshot &document : documents) {
		const std::string directory = directoryForPath(document.path);

		if (!initialized) {
			common = directory;
			initialized = true;
			continue;
		}
		while (!common.empty() && !pathIsSameOrUnder(common, directory)) {
			const std::string parent = parentDirectoryOfDirectory(common);
			if (parent == common) {
				common.clear();
				break;
			}
			common = parent;
		}
	}
	return common;
}

bool snapshotContainsMainFile(const MRWorkspaceServiceSnapshot &snapshot) noexcept {
	for (const MRWorkspaceDocumentSnapshot &document : snapshot.documents)
		if (document.mainFile) return true;
	return false;
}

void appendCompileContextEntry(std::vector<MRWorkspaceCompileContextEntry> &entries, const std::string &value, const std::string &source) {
	MRWorkspaceCompileContextEntry entry;
	std::string normalized = normalizeConfiguredPathInput(value);

	if (normalized.empty()) return;
	for (const MRWorkspaceCompileContextEntry &candidate : entries)
		if (candidate.value == normalized) return;
	entry.value = normalized;
	entry.source = source;
	entries.push_back(entry);
}

void appendCompileContextTextEntry(std::vector<MRWorkspaceCompileContextEntry> &entries, const std::string &value, const std::string &source) {
	MRWorkspaceCompileContextEntry entry;
	std::string trimmed = value;

	while (!trimmed.empty() && static_cast<unsigned char>(trimmed.front()) <= ' ')
		trimmed.erase(trimmed.begin());
	while (!trimmed.empty() && static_cast<unsigned char>(trimmed.back()) <= ' ')
		trimmed.pop_back();
	if (trimmed.empty()) return;
	for (const MRWorkspaceCompileContextEntry &candidate : entries)
		if (candidate.value == trimmed && candidate.source == source) return;
	entry.value = trimmed;
	entry.source = source;
	entries.push_back(entry);
}

void appendEnvironmentPathList(std::vector<MRWorkspaceCompileContextEntry> &entries, const char *name) {
	const char *raw = std::getenv(name);
	std::string current;

	if (raw == nullptr || raw[0] == '\0') return;
	for (const char ch : std::string(raw)) {
		if (ch == ':') {
			appendCompileContextEntry(entries, current, name);
			current.clear();
		} else
			current.push_back(ch);
	}
	appendCompileContextEntry(entries, current, name);
}

void appendEnvironmentFlagText(std::vector<MRWorkspaceCompileContextEntry> &entries, const char *name) {
	const char *raw = std::getenv(name);

	if (raw == nullptr || raw[0] == '\0') return;
	appendCompileContextTextEntry(entries, raw, name);
}

std::string compileContextAnchorPath(const MRWorkspaceServiceSnapshot &snapshot, std::string &source) {
	if (snapshot.mainFile.hasMainFile && !snapshot.mainFile.path.empty()) {
		source = "workspace main file";
		return snapshot.mainFile.path;
	}
	for (const MRWorkspaceDocumentSnapshot &document : snapshot.documents)
		if (!document.path.empty()) {
			source = "first workspace document";
			return document.path;
		}
	source.clear();
	return std::string();
}

MRWorkspaceCompileContext deriveCompileContext(const MRWorkspaceServiceSnapshot &snapshot) {
	static const char *includeEnvironmentNames[] = { "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH" };
	static const char *flagEnvironmentNames[] = { "CPPFLAGS", "CFLAGS", "CXXFLAGS", "CC", "CXX" };
	MRWorkspaceCompileContext context;
	MRCompilerProfile profile;
	std::string matchedProfileName;
	std::string errorMessage;

	context.anchorPath = compileContextAnchorPath(snapshot, context.anchorSource);
	if (context.anchorPath.empty()) {
		context.errorMessage = "No workspace document for compiler profile lookup.";
		return context;
	}
	if (!effectiveCompilerProfileForPath(context.anchorPath, profile, &matchedProfileName, &errorMessage)) {
		context.errorMessage = errorMessage;
		return context;
	}
	context.available = true;
	context.compilerProfileId = profile.id;
	context.compilerProfileName = profile.name;
	context.compilerProfileMatch = matchedProfileName;
	context.toolchain = profile.toolchain;
	context.executablePath = profile.executablePath;
	context.targetTriple = profile.targetTriple;
	for (const std::string &path : profile.includePaths)
		appendCompileContextEntry(context.includePaths, path, "MR compiler profile");
	appendCompileContextTextEntry(context.buildFlags, profile.buildFlags, "MR compiler profile");
	for (const char *name : includeEnvironmentNames)
		appendEnvironmentPathList(context.includePaths, name);
	for (const char *name : flagEnvironmentNames)
		appendEnvironmentFlagText(context.buildFlags, name);
	appendMakefileIncludePaths(context, snapshot.root);
	return context;
}

MRServiceRootContext deriveRootContext(const MRWorkspaceServiceSnapshot &snapshot) {
	MRServiceRootContext root;

	if (snapshot.documents.empty()) {
		root.reason = "no loaded workspace files";
		return root;
	}
	if (snapshotContainsMainFile(snapshot)) {
		const std::string common = commonDirectoryForDocuments(snapshot.documents);
		if (!common.empty() && common != "/") {
			root.hasRoot = true;
			root.rootPath = common;
			root.reason = "main file anchored common directory";
		} else {
			root.reason = "main file common directory is too broad";
		}
		return root;
	}
	if (snapshot.documents.size() == 1) {
		const std::string directory = directoryForPath(snapshot.documents.front().path);
		if (!directory.empty() && directory != "/") {
			root.hasRoot = true;
			root.rootPath = directory;
			root.reason = "single document directory";
		} else {
			root.reason = "single document directory is too broad";
		}
		return root;
	}
	const std::string common = commonDirectoryForDocuments(snapshot.documents);
	if (!common.empty() && common != "/") {
		bool sameDirectory = true;
		for (const MRWorkspaceDocumentSnapshot &document : snapshot.documents)
			if (directoryForPath(document.path) != common) sameDirectory = false;
		if (sameDirectory) {
			root.hasRoot = true;
			root.rootPath = common;
			root.reason = "shared document directory";
			return root;
		}
	}
	root.reason = "no main file";
	return root;
}
} // namespace

std::string normalizeWorkspaceServicePath(const std::string &path) {
	if (path.empty()) return std::string();
	return std::filesystem::path(path).lexically_normal().generic_string();
}

void MRWorkspaceServiceContext::clearMainFile() noexcept {
	mainFileKind = mfkNone;
	mainFileBufferId = 0;
	mainFilePath.clear();
}

void MRWorkspaceServiceContext::setMainFileByBufferId(int bufferId) noexcept {
	if (bufferId <= 0) {
		clearMainFile();
		return;
	}
	mainFileKind = mfkBufferId;
	mainFileBufferId = bufferId;
	mainFilePath.clear();
}

void MRWorkspaceServiceContext::setMainFileByPath(const std::string &path) {
	const std::string normalized = normalizeWorkspaceServicePath(path);

	if (normalized.empty()) {
		clearMainFile();
		return;
	}
	mainFileKind = mfkPath;
	mainFileBufferId = 0;
	mainFilePath = normalized;
}

MRWorkspaceMainFileState MRWorkspaceServiceContext::configuredMainFile() const {
	MRWorkspaceMainFileState state;

	if (mainFileKind == mfkBufferId) {
		state.hasMainFile = true;
		state.bufferId = mainFileBufferId;
	} else if (mainFileKind == mfkPath) {
		state.hasMainFile = true;
		state.path = mainFilePath;
	}
	return state;
}

MRWorkspaceServiceSnapshot MRWorkspaceServiceContext::buildSnapshot(const std::vector<MRWorkspaceDocumentSnapshot> &documents) const {
	MRWorkspaceServiceSnapshot snapshot;

	snapshot.documents.reserve(documents.size());
	for (const MRWorkspaceDocumentSnapshot &document : documents) {
		if (document.path.empty()) continue;
		MRWorkspaceDocumentSnapshot next = document;
		const bool documentIsMainFile = next.mainFile;
		next.path = normalizeWorkspaceServicePath(next.path);
		next.mainFile = false;
		if (mainFileKind == mfkBufferId && next.bufferId == mainFileBufferId) next.mainFile = true;
		if (mainFileKind == mfkPath && next.path == mainFilePath) next.mainFile = true;
		if (mainFileKind == mfkNone && documentIsMainFile) next.mainFile = true;
		if (next.mainFile) {
			snapshot.mainFile.hasMainFile = true;
			snapshot.mainFile.bufferId = next.bufferId;
			snapshot.mainFile.path = next.path;
		}
		snapshot.documents.push_back(next);
	}
	snapshot.root = deriveRootContext(snapshot);
	snapshot.compileContext = deriveCompileContext(snapshot);
	return snapshot;
}

std::vector<MRWorkspaceDocumentSnapshot> collectCurrentWorkspaceDocuments() {
	std::vector<MRWorkspaceDocumentSnapshot> documents;
	std::set<int> seenBufferIds;
	const std::vector<MREditWindow *> windows = allEditWindowsAndBentoPanesInZOrder();

	for (MREditWindow *window : windows) {
		if (window == nullptr || window->currentFileName()[0] == '\0') continue;
		if (seenBufferIds.find(window->bufferId()) != seenBufferIds.end()) continue;
		seenBufferIds.insert(window->bufferId());

		MRWorkspaceDocumentSnapshot document;
		document.bufferId = window->bufferId();
		document.documentId = window->documentId();
		document.documentVersion = window->documentVersion();
		document.path = normalizeWorkspaceServicePath(window->currentFileName());
		document.languageName = window->syntaxLanguageName();
		document.mainFile = mrIsWorkspaceMainFile(window);
		documents.push_back(document);
	}
	return documents;
}

MRWorkspaceServiceSnapshot buildCurrentWorkspaceServiceSnapshot(const MRWorkspaceServiceContext &context) {
	return context.buildSnapshot(collectCurrentWorkspaceDocuments());
}

} // namespace mr::services
