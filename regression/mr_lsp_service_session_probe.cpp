#include <cstddef>
#include <cstdint>
#include <iostream>
#include <poll.h>
#include <string>

#include "../app/services/MRLspServiceSession.hpp"
#include "../ui/MRFileEditor/MRFileEditor.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

mr::lsp::LspDocumentSourceSnapshot makeSourceSnapshot(std::int64_t version, const std::string &text) {
	mr::lsp::LspDocumentSourceSnapshot snapshot;

	snapshot.absolutePath = "/tmp/mr/project/src/main.cpp";
	snapshot.languageId = "cpp";
	snapshot.version = version;
	snapshot.text = text;
	return snapshot;
}

mr::services::MRWorkspaceServiceSnapshot makeWorkspace(std::size_t version) {
	mr::services::MRWorkspaceServiceSnapshot workspace;
	mr::services::MRWorkspaceDocumentSnapshot document;

	document.bufferId = 10;
	document.documentId = 100;
	document.documentVersion = version;
	document.path = "/tmp/mr/project/src/main.cpp";
	document.languageName = "cpp";
	document.mainFile = true;
	workspace.documents.push_back(document);
	workspace.mainFile.hasMainFile = true;
	workspace.mainFile.bufferId = document.bufferId;
	workspace.mainFile.path = document.path;
	workspace.root.hasRoot = true;
	workspace.root.rootPath = "/tmp/mr/project";
	workspace.root.reason = "probe root";
	return workspace;
}

mr::services::MRWorkspaceDocumentSnapshot documentForEditor(const MRFileEditor &editor, const std::string &path) {
	mr::services::MRWorkspaceDocumentSnapshot document;

	document.bufferId = 10;
	document.documentId = editor.documentId();
	document.documentVersion = editor.documentVersion();
	document.path = path;
	document.languageName = editor.syntaxLanguageName();
	document.mainFile = true;
	return document;
}

mr::services::MRWorkspaceServiceSnapshot workspaceForDocument(const mr::services::MRWorkspaceDocumentSnapshot &document) {
	mr::services::MRWorkspaceServiceSnapshot workspace;

	workspace.documents.push_back(document);
	workspace.mainFile.hasMainFile = true;
	workspace.mainFile.bufferId = document.bufferId;
	workspace.mainFile.path = document.path;
	workspace.root.hasRoot = true;
	workspace.root.rootPath = "/tmp/mr/project";
	workspace.root.reason = "probe root";
	return workspace;
}

bool replaceText(MRFileEditor &editor, const std::string &text, std::string &failureReason) {
	if (editor.replaceBufferData(text.data(), static_cast<uint>(text.size()))) return true;
	failureReason = "replace buffer data failed";
	return false;
}

bool pollUntilCounts(mr::services::MRLspServiceSession &session, std::size_t diagnostics, std::size_t locations, std::size_t hovers, std::size_t completions, std::string &failureReason) {
	std::string errorMessage;

	for (int i = 0; i < 50; ++i) {
		if (!session.poll(errorMessage)) {
			failureReason = "session poll failed: " + errorMessage;
			return false;
		}
		if (session.results().diagnosticResults().size() == diagnostics && session.results().locationResults().size() == locations && session.results().hoverResults().size() == hovers &&
		    session.results().completionResults().size() == completions)
			return true;
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected service result counts not observed";
	return false;
}

bool pollUntilCurrentDiagnostics(mr::services::MRLspServiceSession &session, const std::string &path, std::size_t documentVersion, const std::string &message, std::string &failureReason) {
	std::string errorMessage;

	for (int i = 0; i < 50; ++i) {
		if (!session.poll(errorMessage)) {
			failureReason = "session poll failed: " + errorMessage;
			return false;
		}
		for (const mr::services::MRServiceDiagnosticResult &result : session.results().diagnosticResults()) {
			if (result.header.state == mr::services::MRServiceResultState::Current &&
			    result.header.identity.path == path &&
			    result.header.identity.documentVersion == documentVersion &&
			    !result.diagnostics.empty() &&
			    result.diagnostics[0].message == message)
				return true;
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected current diagnostics not observed";
	return false;
}

bool startSession(mr::services::MRLspServiceSession &session, std::string &failureReason) {
	mr::lsp::LspInitializeSpec spec;
	std::string errorMessage;

	spec.session.process.executablePath = "./regression/mr_lsp_session_peer";
	spec.initializeParamsJson = "{\"processId\":null,\"rootUri\":null,\"capabilities\":{}}";
	if (!expect(session.start(spec, errorMessage), "session start: " + errorMessage, failureReason)) return false;
	return expect(session.sendInitialized(errorMessage), "session initialized: " + errorMessage, failureReason);
}

bool verifyResults(const mr::services::MRServiceResultStore &results, std::string &failureReason) {
	if (!expect(results.diagnosticResults().size() == 1, "diagnostics result count", failureReason)) return false;
	if (!expect(results.diagnosticResults()[0].header.state == mr::services::MRServiceResultState::Current, "diagnostics state", failureReason)) return false;
	if (!expect(results.diagnosticResults()[0].header.identity.documentVersion == 2, "diagnostics version", failureReason)) return false;
	if (!expect(results.diagnosticResults()[0].diagnostics[0].message == "changed diagnostic", "diagnostics message", failureReason)) return false;
	if (!expect(results.locationResults().size() == 2, "location result count", failureReason)) return false;
	if (!expect(results.locationResults()[0].header.kind == mr::services::MRServiceResultKind::Definition, "definition kind", failureReason)) return false;
	if (!expect(results.locationResults()[0].locations[0].path == "/tmp/mr/project/src/main.cpp", "definition path", failureReason)) return false;
	if (!expect(results.locationResults()[1].header.kind == mr::services::MRServiceResultKind::References, "references kind", failureReason)) return false;
	if (!expect(results.locationResults()[1].locations.size() == 2, "references count", failureReason)) return false;
	if (!expect(results.hoverResults().size() == 1, "hover result count", failureReason)) return false;
	if (!expect(results.hoverResults()[0].hover.markupKind == "markdown", "hover kind", failureReason)) return false;
	if (!expect(results.completionResults().size() == 1, "completion result count", failureReason)) return false;
	if (!expect(results.completionResults()[0].items.size() == 2, "completion item count", failureReason)) return false;
	if (!expect(results.completionResults()[0].items[0].label == "main", "completion first label", failureReason)) return false;
	return true;
}

bool testSessionGuards(std::string &failureReason) {
	mr::services::MRLspServiceSession session;
	std::string errorMessage;

	if (!expect(!session.requestDefinition(mr::lsp::LspTextPosition{0, 0}, errorMessage), "definition without workspace accepted", failureReason)) return false;
	if (!expect(!session.openDocument(makeWorkspace(1), makeSourceSnapshot(2, "int main() {}\n"), errorMessage), "version mismatch accepted", failureReason)) return false;
	return true;
}

bool testEditorSessionPath(std::string &failureReason) {
	const std::string path = "/tmp/mr/project/src/main.cpp";
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, path.c_str());
	mr::services::MRLspServiceSession session;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	std::string errorMessage;

	if (!startSession(session, failureReason)) return false;
	if (!replaceText(editor, "int main() { return 0; }\n", failureReason)) return false;
	document = documentForEditor(editor, path);
	workspace = workspaceForDocument(document);
	if (!expect(session.openEditorDocument(workspace, document, editor, errorMessage), "open editor document: " + errorMessage, failureReason)) return false;
	if (!pollUntilCurrentDiagnostics(session, path, document.documentVersion, "opened diagnostic", failureReason)) return false;
	if (!replaceText(editor, "int main() { return 2; }\n", failureReason)) return false;
	document = documentForEditor(editor, path);
	workspace = workspaceForDocument(document);
	if (!expect(session.changeEditorDocument(workspace, document, editor, errorMessage), "change editor document: " + errorMessage, failureReason)) return false;
	if (!pollUntilCurrentDiagnostics(session, path, document.documentVersion, "changed diagnostic", failureReason)) return false;
	if (!expect(session.closeDocument(errorMessage), "close editor document: " + errorMessage, failureReason)) return false;
	return expect(session.shutdown(errorMessage), "shutdown editor session: " + errorMessage, failureReason);
}

bool testSyncEditorSessionPath(std::string &failureReason) {
	const std::string mainPath = "/tmp/mr/project/src/main.cpp";
	const std::string otherPath = "/tmp/mr/project/src/other.cpp";
	MRFileEditor mainEditor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, mainPath.c_str());
	MRFileEditor otherEditor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, otherPath.c_str());
	mr::services::MRLspServiceSession session;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	std::string errorMessage;

	if (!startSession(session, failureReason)) return false;
	if (!replaceText(mainEditor, "int main() { return 0; }\n", failureReason)) return false;
	document = documentForEditor(mainEditor, mainPath);
	workspace = workspaceForDocument(document);
	if (!expect(session.syncEditorDocument(workspace, document, mainEditor, errorMessage), "sync open editor document: " + errorMessage, failureReason)) return false;
	if (!pollUntilCurrentDiagnostics(session, mainPath, document.documentVersion, "opened diagnostic", failureReason)) return false;
	if (!expect(session.syncEditorDocument(workspace, document, mainEditor, errorMessage), "sync unchanged editor document: " + errorMessage, failureReason)) return false;
	if (!replaceText(mainEditor, "int main() { return 3; }\n", failureReason)) return false;
	document = documentForEditor(mainEditor, mainPath);
	workspace = workspaceForDocument(document);
	if (!expect(session.syncEditorDocument(workspace, document, mainEditor, errorMessage), "sync changed editor document: " + errorMessage, failureReason)) return false;
	if (!pollUntilCurrentDiagnostics(session, mainPath, document.documentVersion, "changed diagnostic", failureReason)) return false;
	if (!replaceText(otherEditor, "int other() { return 4; }\n", failureReason)) return false;
	document = documentForEditor(otherEditor, otherPath);
	workspace = workspaceForDocument(document);
	if (!expect(session.syncEditorDocument(workspace, document, otherEditor, errorMessage), "sync other editor document: " + errorMessage, failureReason)) return false;
	if (!pollUntilCurrentDiagnostics(session, otherPath, document.documentVersion, "opened diagnostic", failureReason)) return false;
	if (!expect(session.closeDocument(errorMessage), "close synced document: " + errorMessage, failureReason)) return false;
	return expect(session.shutdown(errorMessage), "shutdown synced session: " + errorMessage, failureReason);
}

bool testSessionHappyPath(std::string &failureReason) {
	mr::services::MRLspServiceSession session;
	std::string errorMessage;

	if (!startSession(session, failureReason)) return false;
	if (!expect(session.openDocument(makeWorkspace(1), makeSourceSnapshot(1, "int main() { return 0; }\n"), errorMessage), "open document: " + errorMessage, failureReason)) return false;
	if (!pollUntilCounts(session, 1, 0, 0, 0, failureReason)) return false;
	if (!expect(session.changeDocument(makeWorkspace(2), makeSourceSnapshot(2, "int main() { return 1; }\n"), errorMessage), "change document: " + errorMessage, failureReason)) return false;
	if (!pollUntilCounts(session, 1, 0, 0, 0, failureReason)) return false;
	if (!expect(session.requestDefinition(mr::lsp::LspTextPosition{3, 5}, errorMessage), "request definition: " + errorMessage, failureReason)) return false;
	if (!pollUntilCounts(session, 1, 1, 0, 0, failureReason)) return false;
	if (!expect(session.requestReferences(mr::lsp::LspTextPosition{3, 5}, true, errorMessage), "request references: " + errorMessage, failureReason)) return false;
	if (!pollUntilCounts(session, 1, 2, 0, 0, failureReason)) return false;
	if (!expect(session.requestHover(mr::lsp::LspTextPosition{3, 5}, errorMessage), "request hover: " + errorMessage, failureReason)) return false;
	if (!pollUntilCounts(session, 1, 2, 1, 0, failureReason)) return false;
	if (!expect(session.requestCompletion(mr::lsp::LspTextPosition{3, 5}, errorMessage), "request completion: " + errorMessage, failureReason)) return false;
	if (!pollUntilCounts(session, 1, 2, 1, 1, failureReason)) return false;
	if (!verifyResults(session.results(), failureReason)) return false;
	if (!expect(session.closeDocument(errorMessage), "close document: " + errorMessage, failureReason)) return false;
	return expect(session.shutdown(errorMessage), "shutdown: " + errorMessage, failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testSessionGuards(failureReason)) return false;
	if (!testEditorSessionPath(failureReason)) return false;
	if (!testSyncEditorSessionPath(failureReason)) return false;
	if (!testSessionHappyPath(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_service_session_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_service_session_probe passed\n";
	return 0;
}
