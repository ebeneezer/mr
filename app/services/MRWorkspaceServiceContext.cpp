#include "MRWorkspaceServiceContext.hpp"

#include "../commands/MRWindowCommands.hpp"
#include "../../ui/MREditWindow.hpp"

#include <filesystem>
#include <set>

namespace mr::services {
namespace {
std::string directoryForPath(const std::string &path) {
	std::filesystem::path fsPath(path);
	std::filesystem::path parent = fsPath.parent_path();

	if (parent.empty()) return ".";
	return parent.lexically_normal().generic_string();
}

bool pathIsSameOrUnder(const std::string &parent, const std::string &path) {
	if (parent.empty()) return false;
	if (parent == path) return true;
	if (parent == "/") return !path.empty() && path.front() == '/';
	if (path.size() <= parent.size()) return false;
	if (path.compare(0, parent.size(), parent) != 0) return false;
	return path[parent.size()] == '/';
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
		next.path = normalizeWorkspaceServicePath(next.path);
		next.mainFile = false;
		if (mainFileKind == mfkBufferId && next.bufferId == mainFileBufferId) next.mainFile = true;
		if (mainFileKind == mfkPath && next.path == mainFilePath) next.mainFile = true;
		if (next.mainFile) {
			snapshot.mainFile.hasMainFile = true;
			snapshot.mainFile.bufferId = next.bufferId;
			snapshot.mainFile.path = next.path;
		}
		snapshot.documents.push_back(next);
	}
	snapshot.root = deriveRootContext(snapshot);
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
		documents.push_back(document);
	}
	return documents;
}

MRWorkspaceServiceSnapshot buildCurrentWorkspaceServiceSnapshot(const MRWorkspaceServiceContext &context) {
	return context.buildSnapshot(collectCurrentWorkspaceDocuments());
}

} // namespace mr::services
