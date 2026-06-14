#include <iostream>
#include <string>
#include <vector>

#include "../app/services/MRWorkspaceServiceContext.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

mr::services::MRWorkspaceDocumentSnapshot document(int bufferId, std::size_t documentId, std::size_t version, const std::string &path) {
	mr::services::MRWorkspaceDocumentSnapshot snapshot;

	snapshot.bufferId = bufferId;
	snapshot.documentId = documentId;
	snapshot.documentVersion = version;
	snapshot.path = path;
	snapshot.languageName = "cpp";
	return snapshot;
}

bool testMainFileByBufferId(std::string &failureReason) {
	mr::services::MRWorkspaceServiceContext context;
	std::vector<mr::services::MRWorkspaceDocumentSnapshot> documents;

	documents.push_back(document(10, 100, 1, "/tmp/mr/project/src/main.cpp"));
	documents.push_back(document(11, 101, 2, "/tmp/mr/project/include/main.hpp"));
	context.setMainFileByBufferId(10);

	const mr::services::MRWorkspaceServiceSnapshot snapshot = context.buildSnapshot(documents);
	if (!expect(snapshot.documents.size() == 2, "document count", failureReason)) return false;
	if (!expect(snapshot.mainFile.hasMainFile, "main file missing", failureReason)) return false;
	if (!expect(snapshot.mainFile.bufferId == 10, "main file buffer", failureReason)) return false;
	if (!expect(snapshot.documents[0].mainFile, "main document marker", failureReason)) return false;
	if (!expect(!snapshot.documents[1].mainFile, "secondary document marker", failureReason)) return false;
	if (!expect(snapshot.root.hasRoot, "root missing", failureReason)) return false;
	if (!expect(snapshot.root.rootPath == "/tmp/mr/project", "root path", failureReason)) return false;
	if (!expect(snapshot.root.reason == "main file anchored common directory", "root reason", failureReason)) return false;
	return true;
}

bool testMainFileByPathNormalization(std::string &failureReason) {
	mr::services::MRWorkspaceServiceContext context;
	std::vector<mr::services::MRWorkspaceDocumentSnapshot> documents;

	documents.push_back(document(20, 200, 3, "/tmp/mr/project/src/../src/main.cpp"));
	context.setMainFileByPath("/tmp/mr/project/src/main.cpp");

	const mr::services::MRWorkspaceServiceSnapshot snapshot = context.buildSnapshot(documents);
	if (!expect(snapshot.mainFile.hasMainFile, "path main file missing", failureReason)) return false;
	if (!expect(snapshot.mainFile.path == "/tmp/mr/project/src/main.cpp", "normalized main path", failureReason)) return false;
	if (!expect(snapshot.root.hasRoot, "single main root missing", failureReason)) return false;
	if (!expect(snapshot.root.rootPath == "/tmp/mr/project/src", "single main root", failureReason)) return false;
	return true;
}

bool testNoMainFileMultiDirectory(std::string &failureReason) {
	mr::services::MRWorkspaceServiceContext context;
	std::vector<mr::services::MRWorkspaceDocumentSnapshot> documents;

	documents.push_back(document(30, 300, 1, "/tmp/mr/project/src/a.cpp"));
	documents.push_back(document(31, 301, 1, "/tmp/mr/project/include/a.hpp"));

	const mr::services::MRWorkspaceServiceSnapshot snapshot = context.buildSnapshot(documents);
	if (!expect(!snapshot.mainFile.hasMainFile, "unexpected main file", failureReason)) return false;
	if (!expect(!snapshot.root.hasRoot, "unexpected root without main", failureReason)) return false;
	if (!expect(snapshot.root.reason == "no main file", "no main reason", failureReason)) return false;
	return true;
}

bool testSingleDocumentRoot(std::string &failureReason) {
	mr::services::MRWorkspaceServiceContext context;
	std::vector<mr::services::MRWorkspaceDocumentSnapshot> documents;

	documents.push_back(document(40, 400, 9, "/tmp/mr/project/single.cpp"));

	const mr::services::MRWorkspaceServiceSnapshot snapshot = context.buildSnapshot(documents);
	if (!expect(snapshot.root.hasRoot, "single root missing", failureReason)) return false;
	if (!expect(snapshot.root.rootPath == "/tmp/mr/project", "single root path", failureReason)) return false;
	if (!expect(snapshot.root.reason == "single document directory", "single root reason", failureReason)) return false;
	return true;
}

bool testInvalidMainFileSelection(std::string &failureReason) {
	mr::services::MRWorkspaceServiceContext context;
	std::vector<mr::services::MRWorkspaceDocumentSnapshot> documents;

	documents.push_back(document(50, 500, 1, "/tmp/mr/project/main.cpp"));
	context.setMainFileByBufferId(-1);
	if (!expect(!context.configuredMainFile().hasMainFile, "negative main selection kept", failureReason)) return false;
	context.setMainFileByPath("");
	if (!expect(!context.configuredMainFile().hasMainFile, "empty path main selection kept", failureReason)) return false;
	context.setMainFileByBufferId(51);

	const mr::services::MRWorkspaceServiceSnapshot snapshot = context.buildSnapshot(documents);
	if (!expect(!snapshot.mainFile.hasMainFile, "unloaded main file accepted", failureReason)) return false;
	if (!expect(snapshot.root.hasRoot, "single document fallback root missing", failureReason)) return false;
	return true;
}

bool runProbe(std::string &failureReason) {
	if (!testMainFileByBufferId(failureReason)) return false;
	if (!testMainFileByPathNormalization(failureReason)) return false;
	if (!testNoMainFileMultiDirectory(failureReason)) return false;
	if (!testSingleDocumentRoot(failureReason)) return false;
	if (!testInvalidMainFileSelection(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_workspace_service_context_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_workspace_service_context_probe passed\n";
	return 0;
}
