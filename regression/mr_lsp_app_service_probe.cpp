#include <cstddef>
#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

#include "../app/services/MRLspAppService.hpp"
#include "../ui/MRFileEditor/MRFileEditor.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

bool replaceText(MRFileEditor &editor, const std::string &text, std::string &failureReason) {
	if (editor.replaceBufferData(text.data(), static_cast<uint>(text.size()))) return true;
	failureReason = "replace buffer data failed";
	return false;
}

mr::services::MRWorkspaceDocumentSnapshot documentForEditor(const MRFileEditor &editor, const std::string &path, int bufferId) {
	mr::services::MRWorkspaceDocumentSnapshot document;

	document.bufferId = bufferId;
	document.documentId = editor.documentId();
	document.documentVersion = editor.documentVersion();
	document.path = path;
	document.languageName = editor.syntaxLanguageName();
	return document;
}

mr::services::MRLspServerProfile makeProbeProfile() {
	mr::services::MRLspServerProfile profile;

	profile.profileName = "probe";
	profile.executablePath = "./regression/mr_lsp_session_peer";
	profile.workingDirectory = ".";
	return profile;
}

bool pollUntilCounts(mr::services::MRLspAppService &service, std::size_t diagnostics, std::size_t locations, std::size_t hovers, std::size_t completions, std::string &failureReason) {
	std::string errorMessage;

	for (int i = 0; i < 50; ++i) {
		if (!service.poll(errorMessage)) {
			failureReason = "app service poll failed: " + errorMessage;
			return false;
		}
		if (service.results().diagnosticResults().size() == diagnostics && service.results().locationResults().size() == locations && service.results().hoverResults().size() == hovers &&
		    service.results().completionResults().size() == completions)
			return true;
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected app service result counts not observed";
	return false;
}

bool pollUntilDiagnosticMessage(mr::services::MRLspAppService &service, const std::string &message, std::string &failureReason) {
	std::string errorMessage;

	for (int i = 0; i < 50; ++i) {
		if (!service.poll(errorMessage)) {
			failureReason = "app service diagnostic poll failed: " + errorMessage;
			return false;
		}
		for (const mr::services::MRServiceDiagnosticResult &result : service.results().diagnosticResults()) {
			if (result.header.state != mr::services::MRServiceResultState::Current) continue;
			if (result.diagnostics.empty()) continue;
			if (result.diagnostics[0].message == message) return true;
		}
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected app service diagnostic message not observed";
	return false;
}

bool testWorkspaceBoundary(std::string &failureReason) {
	mr::services::MRLspAppService service;
	mr::services::MRWorkspaceDocumentSnapshot mainDocument;
	mr::services::MRWorkspaceDocumentSnapshot sideDocument;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	std::vector<mr::services::MRWorkspaceDocumentSnapshot> documents;

	mainDocument.bufferId = 10;
	mainDocument.documentId = 100;
	mainDocument.documentVersion = 1;
	mainDocument.path = "/tmp/mr/project/src/main.cpp";
	mainDocument.languageName = "cpp";
	sideDocument.bufferId = 11;
	sideDocument.documentId = 101;
	sideDocument.documentVersion = 1;
	sideDocument.path = "/tmp/mr/project/src/lib.cpp";
	sideDocument.languageName = "cpp";
	documents.push_back(mainDocument);
	documents.push_back(sideDocument);

	if (!expect(!service.configuredMainFile().hasMainFile, "new app service has main file", failureReason)) return false;
	service.setMainFileByBufferId(mainDocument.bufferId);
	workspace = service.buildWorkspaceSnapshot(documents);
	if (!expect(workspace.mainFile.hasMainFile, "buffer main file missing", failureReason)) return false;
	if (!expect(workspace.mainFile.bufferId == mainDocument.bufferId, "buffer main file id", failureReason)) return false;
	if (!expect(workspace.mainFile.path == mainDocument.path, "buffer main file path", failureReason)) return false;
	if (!expect(workspace.documents.size() == 2, "workspace document count", failureReason)) return false;
	if (!expect(workspace.documents[0].mainFile && !workspace.documents[1].mainFile, "workspace main file marker", failureReason)) return false;
	if (!expect(workspace.root.hasRoot, "workspace root missing", failureReason)) return false;

	service.setMainFileByPath(sideDocument.path);
	workspace = service.buildWorkspaceSnapshot(documents);
	if (!expect(workspace.mainFile.hasMainFile, "path main file missing", failureReason)) return false;
	if (!expect(workspace.mainFile.bufferId == sideDocument.bufferId, "path main file id", failureReason)) return false;
	if (!expect(workspace.mainFile.path == sideDocument.path, "path main file path", failureReason)) return false;
	if (!expect(!workspace.documents[0].mainFile && workspace.documents[1].mainFile, "path main file marker", failureReason)) return false;

	service.clearMainFile();
	return expect(!service.configuredMainFile().hasMainFile, "cleared main file remains configured", failureReason);
}

bool testRequestGuard(std::string &failureReason) {
	const std::string path = "/tmp/mr/project/src/main.cpp";
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, path.c_str());
	mr::services::MRLspAppService service;
	mr::services::MRLspServerProfile profile = makeProbeProfile();
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	std::string errorMessage;

	if (!replaceText(editor, "int main() { return 0; }\n", failureReason)) return false;
	document = documentForEditor(editor, path, 10);
	return expect(
		!service.requestEditorCommand(
			profile,
			workspace,
			document,
			editor,
			mr::services::MRLspServiceCommandId::GoToDefinition,
			mr::lsp::LspTextPosition{3, 5},
			errorMessage),
		"empty app service workspace accepted",
		failureReason);
}

bool testEditorCommandPath(std::string &failureReason) {
	const std::string path = "/tmp/mr/project/src/main.cpp";
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, path.c_str());
	mr::services::MRLspAppService service;
	mr::services::MRLspServerProfile profile = makeProbeProfile();
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	std::vector<mr::services::MRWorkspaceDocumentSnapshot> documents;
	std::string errorMessage;

	if (!expect(!service.runtimeActive(), "new app service runtime is active", failureReason)) return false;
	if (!replaceText(editor, "int main() { return 7; }\n", failureReason)) return false;
	document = documentForEditor(editor, path, 10);
	documents.push_back(document);
	service.setMainFileByBufferId(document.bufferId);
	workspace = service.buildWorkspaceSnapshot(documents);

	if (!expect(
			service.requestEditorCommand(
				profile,
				workspace,
				document,
				editor,
				mr::services::MRLspServiceCommandId::GoToDefinition,
				mr::lsp::LspTextPosition{3, 5},
				errorMessage),
			"app service definition command: " + errorMessage,
			failureReason))
		return false;
	if (!expect(service.runtimeActive(), "started app service runtime is not active", failureReason)) return false;
	if (!pollUntilCounts(service, 1, 1, 0, 0, failureReason)) return false;

	if (!expect(
			service.requestEditorCommand(
				profile,
				workspace,
				document,
				editor,
				mr::services::MRLspServiceCommandId::ShowHover,
				mr::lsp::LspTextPosition{3, 5},
				errorMessage),
			"app service hover command: " + errorMessage,
			failureReason))
		return false;
	if (!pollUntilCounts(service, 1, 1, 1, 0, failureReason)) return false;
	if (!expect(service.shutdown(errorMessage), "app service shutdown: " + errorMessage, failureReason)) return false;
	return expect(!service.runtimeActive(), "shutdown app service runtime is active", failureReason);
}

bool testSyncOnlyDiagnosticsPath(std::string &failureReason) {
	const std::string path = "/tmp/mr/project/src/main.cpp";
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, path.c_str());
	mr::services::MRLspAppService service;
	mr::services::MRLspServerProfile profile = makeProbeProfile();
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	std::vector<mr::services::MRWorkspaceDocumentSnapshot> documents;
	std::string errorMessage;

	if (!replaceText(editor, "int main() { return 7; }\n", failureReason)) return false;
	document = documentForEditor(editor, path, 10);
	documents.push_back(document);
	service.setMainFileByBufferId(document.bufferId);
	workspace = service.buildWorkspaceSnapshot(documents);
	if (!expect(service.syncEditorDocument(profile, workspace, document, editor, errorMessage), "app service sync open: " + errorMessage, failureReason)) return false;
	if (!pollUntilDiagnosticMessage(service, "opened diagnostic", failureReason)) return false;

	if (!replaceText(editor, "int main() { return ; }\n", failureReason)) return false;
	document = documentForEditor(editor, path, 10);
	documents.clear();
	documents.push_back(document);
	workspace = service.buildWorkspaceSnapshot(documents);
	if (!expect(service.syncEditorDocument(profile, workspace, document, editor, errorMessage), "app service sync change: " + errorMessage, failureReason)) return false;
	if (!pollUntilDiagnosticMessage(service, "changed diagnostic", failureReason)) return false;
	if (!expect(service.shutdown(errorMessage), "app service sync shutdown: " + errorMessage, failureReason)) return false;
	return expect(!service.runtimeActive(), "sync shutdown app service runtime is active", failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testWorkspaceBoundary(failureReason)) return false;
	if (!testRequestGuard(failureReason)) return false;
	if (!testEditorCommandPath(failureReason)) return false;
	if (!testSyncOnlyDiagnosticsPath(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_app_service_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_app_service_probe passed\n";
	return 0;
}
