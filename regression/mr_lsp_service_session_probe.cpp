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

bool pollUntilCounts(mr::services::MRLspServiceSession &session, std::size_t diagnostics, std::size_t locations, std::size_t hovers, std::size_t completions, std::string &failureReason, std::size_t codeActions = 0) {
	std::string errorMessage;

	for (int i = 0; i < 50; ++i) {
		if (!session.poll(errorMessage)) {
			failureReason = "session poll failed: " + errorMessage;
			return false;
		}
		if (session.results().diagnosticResults().size() == diagnostics && session.results().locationResults().size() == locations && session.results().hoverResults().size() == hovers &&
		    session.results().completionResults().size() == completions && session.results().codeActionResults().size() == codeActions)
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
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceServiceSnapshot workspace = makeWorkspace(1);
	std::string errorMessage;

	profile.profileName = "probe";
	profile.executablePath = "./regression/mr_lsp_session_peer";
	profile.workingDirectory = ".";
	return expect(session.startRuntime(workspace, profile, errorMessage), "session runtime start: " + errorMessage, failureReason);
}

bool testInitializeSpec(std::string &failureReason) {
	mr::lsp::LspSessionSpec sessionSpec;
	mr::lsp::LspInitializeSpec spec;
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceServiceSnapshot workspace = makeWorkspace(1);
	mr::services::MRWorkspaceServiceSnapshot noRootWorkspace;
	std::string errorMessage;
	const std::string expectedRootParams = "{\"processId\":null,\"rootPath\":\"/tmp/mr/project\",\"rootUri\":\"file:///tmp/mr/project\",\"workspaceFolders\":[{\"uri\":\"file:///tmp/mr/project\",\"name\":\"project\"}],\"capabilities\":{}}";
	const std::string expectedNullParams = "{\"processId\":null,\"rootPath\":null,\"rootUri\":null,\"workspaceFolders\":null,\"capabilities\":{}}";

	sessionSpec.process.executablePath = "./regression/mr_lsp_session_peer";
	if (!expect(mr::services::buildLspInitializeSpecFromWorkspace(workspace, sessionSpec, spec, errorMessage), "root initialize spec: " + errorMessage, failureReason)) return false;
	if (!expect(spec.session.process.executablePath == sessionSpec.process.executablePath, "initialize executable", failureReason)) return false;
	if (!expect(spec.initializeParamsJson == expectedRootParams, "root initialize params", failureReason)) return false;
	if (!expect(mr::services::buildLspInitializeSpecFromWorkspace(noRootWorkspace, sessionSpec, spec, errorMessage), "null initialize spec: " + errorMessage, failureReason)) return false;
	if (!expect(spec.initializeParamsJson == expectedNullParams, "null initialize params", failureReason)) return false;

	profile.profileName = "probe";
	profile.executablePath = "./regression/mr_lsp_session_peer";
	profile.arguments.push_back("--probe");
	if (!expect(mr::services::buildLspInitializeSpecFromServerProfile(workspace, profile, spec, errorMessage), "profile initialize spec: " + errorMessage, failureReason)) return false;
	if (!expect(spec.session.process.executablePath == profile.executablePath, "profile executable", failureReason)) return false;
	if (!expect(spec.session.process.arguments.size() == 1 && spec.session.process.arguments[0] == "--probe", "profile arguments", failureReason)) return false;
	if (!expect(spec.session.process.workingDirectory == workspace.root.rootPath, "profile workspace cwd", failureReason)) return false;
	profile.workingDirectory = "/tmp";
	if (!expect(mr::services::buildLspInitializeSpecFromServerProfile(workspace, profile, spec, errorMessage), "profile cwd initialize spec: " + errorMessage, failureReason)) return false;
	if (!expect(spec.session.process.workingDirectory == "/tmp", "profile explicit cwd", failureReason)) return false;
	profile.executablePath.clear();
	return expect(!mr::services::buildLspInitializeSpecFromServerProfile(workspace, profile, spec, errorMessage), "empty profile executable accepted", failureReason);
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
	if (!expect(results.codeActionResults().size() == 1, "codeAction result count", failureReason)) return false;
	if (!expect(results.codeActionResults()[0].items.size() == 2, "codeAction item count", failureReason)) return false;
	if (!expect(results.codeActionResults()[0].items[0].title == "Insert semicolon", "codeAction first title", failureReason)) return false;
	if (!expect(results.codeActionResults()[0].items[0].hasEdit, "codeAction first edit", failureReason)) return false;
	return true;
}

bool testSessionGuards(std::string &failureReason) {
	mr::services::MRLspServiceSession session;
	std::string errorMessage;

	if (!expect(!session.requestDefinition(mr::lsp::LspTextPosition{0, 0}, errorMessage), "definition without workspace accepted", failureReason)) return false;
	if (!expect(!session.openDocument(makeWorkspace(1), makeSourceSnapshot(2, "int main() {}\n"), errorMessage), "version mismatch accepted", failureReason)) return false;
	return true;
}

bool testServiceCommandSpec(std::string &failureReason) {
	mr::services::MRLspServiceCommandSpec spec;

	if (!expect(mr::services::lspServiceCommandSpec(mr::services::MRLspServiceCommandId::GoToDefinition, spec), "definition command spec missing", failureReason)) return false;
	if (!expect(spec.requestKind == mr::services::MRLspServiceRequestKind::Definition, "definition command request kind", failureReason)) return false;
	if (!expect(std::string(spec.actionId) == "MR_LSP_GOTO_DEFINITION", "definition command action id", failureReason)) return false;
	if (!expect(!spec.includeDeclaration, "definition command include declaration", failureReason)) return false;
	if (!expect(mr::services::lspServiceCommandSpec(mr::services::MRLspServiceCommandId::FindReferences, spec), "references command spec missing", failureReason)) return false;
	if (!expect(spec.requestKind == mr::services::MRLspServiceRequestKind::References, "references command request kind", failureReason)) return false;
	if (!expect(std::string(spec.actionId) == "MR_LSP_FIND_REFERENCES", "references command action id", failureReason)) return false;
	if (!expect(spec.includeDeclaration, "references command include declaration", failureReason)) return false;
	if (!expect(mr::services::lspServiceCommandSpec(mr::services::MRLspServiceCommandId::ShowHover, spec), "hover command spec missing", failureReason)) return false;
	if (!expect(spec.requestKind == mr::services::MRLspServiceRequestKind::Hover, "hover command request kind", failureReason)) return false;
	if (!expect(mr::services::lspServiceCommandSpec(mr::services::MRLspServiceCommandId::Complete, spec), "completion command spec missing", failureReason)) return false;
	if (!expect(spec.requestKind == mr::services::MRLspServiceRequestKind::Completion, "completion command request kind", failureReason)) return false;
	return expect(!mr::services::lspServiceCommandSpec(static_cast<mr::services::MRLspServiceCommandId>(99), spec), "invalid command spec accepted", failureReason);
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

bool testRuntimeFacadePath(std::string &failureReason) {
	const std::string path = "/tmp/mr/project/src/main.cpp";
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, path.c_str());
	mr::services::MRLspServiceSession session;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	std::string errorMessage;

	if (!startSession(session, failureReason)) return false;
	if (!replaceText(editor, "int main() { return 5; }\n", failureReason)) return false;
	document = documentForEditor(editor, path);
	workspace = workspaceForDocument(document);
	if (!expect(
			session.syncEditorDocumentAndRequest(
				workspace,
				document,
				editor,
				mr::services::MRLspServiceRequestKind::Definition,
				mr::lsp::LspTextPosition{3, 5},
				true,
				errorMessage),
			"runtime definition request: " + errorMessage,
			failureReason))
		return false;
	if (!pollUntilCounts(session, 1, 1, 0, 0, failureReason)) return false;
	if (!expect(
			session.syncEditorDocumentAndRequest(
				workspace,
				document,
				editor,
				mr::services::MRLspServiceRequestKind::References,
				mr::lsp::LspTextPosition{3, 5},
				true,
				errorMessage),
			"runtime references request: " + errorMessage,
			failureReason))
		return false;
	if (!pollUntilCounts(session, 1, 2, 0, 0, failureReason)) return false;
	if (!expect(
			session.syncEditorDocumentAndRequest(
				workspace,
				document,
				editor,
				mr::services::MRLspServiceRequestKind::Hover,
				mr::lsp::LspTextPosition{3, 5},
				false,
				errorMessage),
			"runtime hover request: " + errorMessage,
			failureReason))
		return false;
	if (!pollUntilCounts(session, 1, 2, 1, 0, failureReason)) return false;
	if (!expect(
			session.syncEditorDocumentAndRequest(
				workspace,
				document,
				editor,
				mr::services::MRLspServiceRequestKind::Completion,
				mr::lsp::LspTextPosition{3, 5},
				false,
				errorMessage),
			"runtime completion request: " + errorMessage,
			failureReason))
		return false;
	if (!pollUntilCounts(session, 1, 2, 1, 1, failureReason)) return false;
	if (!expect(session.closeDocument(errorMessage), "close runtime document: " + errorMessage, failureReason)) return false;
	return expect(session.shutdown(errorMessage), "shutdown runtime session: " + errorMessage, failureReason);
}

bool testEditorDocumentServiceRequestPath(std::string &failureReason) {
	const std::string path = "/tmp/mr/project/src/main.cpp";
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, path.c_str());
	mr::services::MRLspServiceSession session;
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	std::string errorMessage;

	profile.profileName = "probe";
	profile.executablePath = "./regression/mr_lsp_session_peer";
	profile.workingDirectory = ".";
	if (!expect(!session.runtimeActive(), "new runtime is active", failureReason)) return false;
	if (!replaceText(editor, "int main() { return 6; }\n", failureReason)) return false;
	document = documentForEditor(editor, path);
	workspace = workspaceForDocument(document);
	if (!expect(
			session.requestEditorDocumentServiceCommand(
				workspace,
				profile,
				document,
				editor,
				mr::services::MRLspServiceCommandId::GoToDefinition,
				mr::lsp::LspTextPosition{3, 5},
				errorMessage),
			"editor service definition command: " + errorMessage,
			failureReason))
		return false;
	if (!expect(session.runtimeActive(), "started runtime is not active", failureReason)) return false;
	if (!pollUntilCounts(session, 1, 1, 0, 0, failureReason)) return false;
	if (!expect(
			session.requestEditorDocumentServiceCommand(
				workspace,
				profile,
				document,
				editor,
				mr::services::MRLspServiceCommandId::ShowHover,
				mr::lsp::LspTextPosition{3, 5},
				errorMessage),
			"editor service hover command: " + errorMessage,
			failureReason))
		return false;
	if (!pollUntilCounts(session, 1, 1, 1, 0, failureReason)) return false;
	profile.arguments.push_back("--restart");
	if (!expect(
			session.requestEditorDocumentServiceCommand(
				workspace,
				profile,
				document,
				editor,
				mr::services::MRLspServiceCommandId::Complete,
				mr::lsp::LspTextPosition{3, 5},
				errorMessage),
			"editor service completion command after profile change: " + errorMessage,
			failureReason))
		return false;
	if (!expect(session.runtimeActive(), "restarted runtime is not active", failureReason)) return false;
	if (!pollUntilCounts(session, 1, 0, 0, 1, failureReason)) return false;
	if (!expect(session.closeDocument(errorMessage), "close editor service document: " + errorMessage, failureReason)) return false;
	return expect(session.shutdown(errorMessage), "shutdown editor service session: " + errorMessage, failureReason);
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
	if (!expect(!session.results().diagnosticResults().empty(), "diagnostic result missing before codeAction", failureReason)) return false;
	if (!expect(!session.results().diagnosticResults()[0].diagnostics.empty(), "diagnostic entry missing before codeAction", failureReason)) return false;
	if (!expect(session.requestCodeActionsForDiagnostic(session.results().diagnosticResults()[0], session.results().diagnosticResults()[0].diagnostics[0], errorMessage), "request codeAction: " + errorMessage, failureReason)) return false;
	if (!pollUntilCounts(session, 1, 2, 1, 1, failureReason, 1)) return false;
	if (!verifyResults(session.results(), failureReason)) return false;
	if (!expect(session.closeDocument(errorMessage), "close document: " + errorMessage, failureReason)) return false;
	return expect(session.shutdown(errorMessage), "shutdown: " + errorMessage, failureReason);
}

bool testProtocolShaperServicePath(std::string &failureReason) {
	const std::string path = "/tmp/mr/project/src/main.cpp";
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, path.c_str());
	mr::services::MRLspServiceSession session;
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	std::string errorMessage;

	profile.profileName = "protocol-shaper";
	profile.executablePath = "./regression/mr_lsp_protocol_shaper";
	profile.workingDirectory = ".";
	if (!replaceText(editor, "int main() { return 8; }\n", failureReason)) return false;
	document = documentForEditor(editor, path);
	workspace = workspaceForDocument(document);
	if (!expect(
			session.requestEditorDocumentServiceCommand(
				workspace,
				profile,
				document,
				editor,
				mr::services::MRLspServiceCommandId::GoToDefinition,
				mr::lsp::LspTextPosition{0, 4},
				errorMessage),
			"protocol shaper definition command: " + errorMessage,
			failureReason))
		return false;
	if (!pollUntilCounts(session, 1, 1, 0, 0, failureReason)) return false;
	if (!expect(session.results().diagnosticResults()[0].diagnostics[0].message == "protocol shaper opened document", "protocol shaper diagnostic", failureReason)) return false;
	if (!expect(session.results().locationResults()[0].locations[0].path == path, "protocol shaper definition path", failureReason)) return false;
	if (!expect(
			session.requestEditorDocumentServiceCommand(
				workspace,
				profile,
				document,
				editor,
				mr::services::MRLspServiceCommandId::ShowHover,
				mr::lsp::LspTextPosition{0, 4},
				errorMessage),
			"protocol shaper hover command: " + errorMessage,
			failureReason))
		return false;
	if (!pollUntilCounts(session, 1, 1, 1, 0, failureReason)) return false;
	if (!expect(session.results().hoverResults()[0].hover.value == "mr protocol shaper hover", "protocol shaper hover value", failureReason)) return false;
	if (!expect(
			session.requestEditorDocumentServiceCommand(
				workspace,
				profile,
				document,
				editor,
				mr::services::MRLspServiceCommandId::Complete,
				mr::lsp::LspTextPosition{0, 4},
				errorMessage),
			"protocol shaper completion command: " + errorMessage,
			failureReason))
		return false;
	if (!pollUntilCounts(session, 1, 1, 1, 1, failureReason)) return false;
	if (!expect(session.results().completionResults()[0].items[0].label == "shaperCompletionOne", "protocol shaper completion label", failureReason)) return false;
	if (!expect(session.requestCodeActionsForDiagnostic(session.results().diagnosticResults()[0], session.results().diagnosticResults()[0].diagnostics[0], errorMessage), "protocol shaper codeAction request: " + errorMessage, failureReason)) return false;
	if (!pollUntilCounts(session, 1, 1, 1, 1, failureReason, 1)) return false;
	if (!expect(session.results().codeActionResults()[0].items[0].title == "protocol shaper quick fix", "protocol shaper codeAction title", failureReason)) return false;
	if (!expect(session.closeDocument(errorMessage), "close protocol shaper document: " + errorMessage, failureReason)) return false;
	return expect(session.shutdown(errorMessage), "shutdown protocol shaper session: " + errorMessage, failureReason);
}

bool testProtocolShaperMalformedStart(std::string &failureReason) {
	const std::string path = "/tmp/mr/project/src/main.cpp";
	MRFileEditor editor(TRect(0, 0, 80, 16), nullptr, nullptr, nullptr, path.c_str());
	mr::services::MRLspServiceSession session;
	mr::services::MRLspServerProfile profile;
	mr::services::MRWorkspaceDocumentSnapshot document;
	mr::services::MRWorkspaceServiceSnapshot workspace;
	std::string errorMessage;

	profile.profileName = "protocol-shaper-malformed";
	profile.executablePath = "./regression/mr_lsp_protocol_shaper";
	profile.arguments.push_back("--scenario");
	profile.arguments.push_back("malformed");
	profile.workingDirectory = ".";
	if (!replaceText(editor, "int main() { return 9; }\n", failureReason)) return false;
	document = documentForEditor(editor, path);
	workspace = workspaceForDocument(document);
	if (!expect(
			!session.requestEditorDocumentServiceCommand(
				workspace,
				profile,
				document,
				editor,
				mr::services::MRLspServiceCommandId::GoToDefinition,
				mr::lsp::LspTextPosition{0, 4},
				errorMessage),
			"malformed protocol shaper accepted",
			failureReason))
		return false;
	if (!expect(errorMessage.find("Content-Length") != std::string::npos, "malformed protocol shaper error text", failureReason)) return false;
	return expect(!session.runtimeActive(), "malformed protocol shaper runtime remains active", failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testInitializeSpec(failureReason)) return false;
	if (!testSessionGuards(failureReason)) return false;
	if (!testServiceCommandSpec(failureReason)) return false;
	if (!testEditorSessionPath(failureReason)) return false;
	if (!testSyncEditorSessionPath(failureReason)) return false;
	if (!testRuntimeFacadePath(failureReason)) return false;
	if (!testEditorDocumentServiceRequestPath(failureReason)) return false;
	if (!testSessionHappyPath(failureReason)) return false;
	if (!testProtocolShaperServicePath(failureReason)) return false;
	if (!testProtocolShaperMalformedStart(failureReason)) return false;
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
