#include "MRLspServiceSession.hpp"

#include "MRLspEditorSource.hpp"
#include "../../lsp/MRLspUri.hpp"
#include "../../ui/MRWindowSupport.hpp"

#include <poll.h>
#include <sstream>
#include <vector>

namespace {

void appendJsonString(std::string &out, const std::string &text) {
	out.push_back('"');
	for (char ch : text) {
		if (ch == '"' || ch == '\\') {
			out.push_back('\\');
			out.push_back(ch);
		} else if (ch == '\n') {
			out += "\\n";
		} else if (ch == '\r') {
			out += "\\r";
		} else if (ch == '\t') {
			out += "\\t";
		} else {
			out.push_back(ch);
		}
	}
	out.push_back('"');
}

std::string workspaceFolderNameForRootPath(const std::string &rootPath) {
	const std::size_t slash = rootPath.find_last_of('/');

	if (rootPath.empty()) return "mr-workspace";
	if (slash == std::string::npos) return rootPath;
	if (slash + 1 < rootPath.size()) return rootPath.substr(slash + 1);
	return rootPath;
}

std::string executableBaseName(const std::string &path) {
	const std::size_t slash = path.find_last_of('/');

	if (slash == std::string::npos) return path;
	if (slash + 1 < path.size()) return path.substr(slash + 1);
	return std::string();
}

bool profileUsesClangd(const mr::services::MRLspServerProfile &profile) {
	const std::string baseName = executableBaseName(profile.executablePath);

	return baseName == "clangd" || baseName.starts_with("clangd-");
}

bool argumentListContains(const std::vector<std::string> &arguments, const std::string &argument) {
	for (const std::string &candidate : arguments)
		if (candidate == argument) return true;
	return false;
}

void appendArgumentOnce(std::vector<std::string> &arguments, const std::string &argument) {
	if (!argumentListContains(arguments, argument)) arguments.push_back(argument);
}

bool splitShellLikeWords(const std::string &text, std::vector<std::string> &words, std::string &errorMessage) {
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
			if (ch == '\\' && index + 1 < text.size()) {
				++index;
				current.push_back(text[index]);
				hasCurrent = true;
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
		if (ch == '\\' && index + 1 < text.size()) {
			++index;
			current.push_back(text[index]);
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
	if (quoteMode != qmNone) {
		errorMessage = "LSP compile context contains an unterminated quoted build flag.";
		return false;
	}
	if (hasCurrent) words.push_back(current);
	errorMessage.clear();
	return true;
}

bool compileContextFallbackFlagIsLanguageStandard(const std::string &flag) {
	return flag.starts_with("-std=") || flag.starts_with("--std=");
}

void appendCompileContextFallbackFlags(const mr::services::MRWorkspaceCompileContext &context, std::vector<std::string> &fallbackFlags, std::string &errorMessage) {
	for (const mr::services::MRWorkspaceCompileContextEntry &entry : context.includePaths)
		fallbackFlags.push_back("-I" + entry.value);
	if (!context.targetTriple.empty()) fallbackFlags.push_back("--target=" + context.targetTriple);
	for (const mr::services::MRWorkspaceCompileContextEntry &entry : context.buildFlags) {
		std::vector<std::string> words;

		if (entry.source == "CC" || entry.source == "CXX") continue;
		if (!splitShellLikeWords(entry.value, words, errorMessage)) return;
		for (const std::string &word : words)
			if (!compileContextFallbackFlagIsLanguageStandard(word)) fallbackFlags.push_back(word);
	}
	errorMessage.clear();
}

std::string compileContextFingerprint(const mr::services::MRWorkspaceCompileContext &context) {
	std::ostringstream line;

	line << (context.available ? "available" : "unavailable") << '\n'
	     << context.anchorPath << '\n'
	     << context.anchorSource << '\n'
	     << context.compilerProfileId << '\n'
	     << context.compilerProfileName << '\n'
	     << context.compilerProfileMatch << '\n'
	     << context.toolchain << '\n'
	     << context.executablePath << '\n'
	     << context.targetTriple << '\n'
	     << context.errorMessage << '\n';
	for (const mr::services::MRWorkspaceCompileContextEntry &entry : context.includePaths)
		line << "I:" << entry.source << ':' << entry.value << '\n';
	for (const mr::services::MRWorkspaceCompileContextEntry &entry : context.buildFlags)
		line << "F:" << entry.source << ':' << entry.value << '\n';
	return line.str();
}

void appendCompileContextEntries(std::ostringstream &line, const char *label, const std::vector<mr::services::MRWorkspaceCompileContextEntry> &entries) {
	line << " " << label << "=";
	if (entries.empty()) {
		line << "none";
		return;
	}
	for (std::size_t index = 0; index < entries.size(); ++index) {
		if (index != 0) line << "; ";
		line << entries[index].value << " [" << entries[index].source << "]";
	}
}

void logLspCompileContext(const mr::services::MRWorkspaceServiceSnapshot &workspace, const mr::services::MRLspServerProfile &profile, const std::vector<std::string> &fallbackFlags) {
	const mr::services::MRWorkspaceCompileContext &context = workspace.compileContext;
	std::ostringstream line;

	if (!profileUsesClangd(profile)) return;
	line << "LSP clangd compile context:";
	if (!context.available) {
		line << " unavailable";
		if (!context.errorMessage.empty()) line << " reason=" << context.errorMessage;
		mrLogMessage(line.str());
		return;
	}
	line << " anchor=" << context.anchorPath << " [" << context.anchorSource << "]"
	     << " profile=" << context.compilerProfileName;
	if (!context.compilerProfileMatch.empty()) line << " match=" << context.compilerProfileMatch;
	if (!context.toolchain.empty()) line << " toolchain=" << context.toolchain;
	if (!context.executablePath.empty()) line << " compiler=" << context.executablePath;
	if (!context.targetTriple.empty()) line << " target=" << context.targetTriple;
	line << " fallbackFlags=" << fallbackFlags.size();
	appendCompileContextEntries(line, "includes", context.includePaths);
	appendCompileContextEntries(line, "flags", context.buildFlags);
	mrLogMessage(line.str());
}

void appendJsonStringArray(std::string &out, const std::vector<std::string> &values) {
	out.push_back('[');
	for (std::size_t index = 0; index < values.size(); ++index) {
		if (index != 0) out.push_back(',');
		appendJsonString(out, values[index]);
	}
	out.push_back(']');
}

void skipJsonWhitespaceLocal(const std::string &text, std::size_t &pos) noexcept {
	while (pos < text.size() && static_cast<unsigned char>(text[pos]) <= ' ')
		++pos;
}

bool initializeCapabilityIsAdvertised(const std::string &payload, const std::string &key) {
	const std::string quotedKey = "\"" + key + "\"";
	std::size_t pos = payload.find(quotedKey);

	if (pos == std::string::npos) return false;
	pos += quotedKey.size();
	skipJsonWhitespaceLocal(payload, pos);
	if (pos >= payload.size() || payload[pos] != ':') return false;
	++pos;
	skipJsonWhitespaceLocal(payload, pos);
	if (pos >= payload.size()) return false;
	if (payload.compare(pos, 4, "null") == 0) return false;
	if (payload.compare(pos, 5, "false") == 0) return false;
	return true;
}

bool findJsonObjectEndLocal(const std::string &text, std::size_t openPos, std::size_t &closePos) {
	int depth = 0;
	bool inString = false;
	bool escaped = false;

	if (openPos >= text.size() || text[openPos] != '{') return false;
	for (std::size_t pos = openPos; pos < text.size(); ++pos) {
		const char ch = text[pos];
		if (inString) {
			if (escaped) escaped = false;
			else if (ch == '\\') escaped = true;
			else if (ch == '"') inString = false;
			continue;
		}
		if (ch == '"') {
			inString = true;
			continue;
		}
		if (ch == '{') {
			++depth;
			continue;
		}
		if (ch == '}') {
			--depth;
			if (depth == 0) {
				closePos = pos;
				return true;
			}
		}
	}
	return false;
}

bool parseJsonStringArrayLocal(const std::string &text, std::size_t arrayStart, std::vector<std::string> &values) {
	std::size_t pos = arrayStart + 1;

	if (arrayStart >= text.size() || text[arrayStart] != '[') return false;
	values.clear();
	for (;;) {
		std::string value;

		skipJsonWhitespaceLocal(text, pos);
		if (pos >= text.size()) return false;
		if (text[pos] == ']') return true;
		if (text[pos] != '"') return false;
		++pos;
		while (pos < text.size()) {
			const char ch = text[pos++];
			if (ch == '"') break;
			if (ch == '\\') {
				if (pos >= text.size()) return false;
				value.push_back(text[pos++]);
			} else {
				value.push_back(ch);
			}
		}
		values.push_back(value);
		skipJsonWhitespaceLocal(text, pos);
		if (pos >= text.size()) return false;
		if (text[pos] == ',') {
			++pos;
			continue;
		}
		if (text[pos] == ']') return true;
		return false;
	}
}

void extractCompletionTriggerCharacters(const std::string &payload, std::vector<std::string> &triggerCharacters) {
	const std::string providerKey = "\"completionProvider\"";
	const std::string triggerKey = "\"triggerCharacters\"";
	const std::size_t providerPos = payload.find(providerKey);
	std::size_t providerValue = 0;
	std::size_t providerEnd = 0;
	std::size_t triggerPos = 0;
	std::size_t triggerValue = 0;

	triggerCharacters.clear();
	if (providerPos == std::string::npos) return;
	providerValue = providerPos + providerKey.size();
	skipJsonWhitespaceLocal(payload, providerValue);
	if (providerValue >= payload.size() || payload[providerValue] != ':') return;
	++providerValue;
	skipJsonWhitespaceLocal(payload, providerValue);
	if (providerValue >= payload.size() || payload[providerValue] != '{') return;
	if (!findJsonObjectEndLocal(payload, providerValue, providerEnd)) return;
	triggerPos = payload.find(triggerKey, providerValue);
	if (triggerPos == std::string::npos || triggerPos > providerEnd) return;
	triggerValue = triggerPos + triggerKey.size();
	skipJsonWhitespaceLocal(payload, triggerValue);
	if (triggerValue >= payload.size() || payload[triggerValue] != ':') return;
	++triggerValue;
	skipJsonWhitespaceLocal(payload, triggerValue);
	if (triggerValue > providerEnd || triggerValue >= payload.size() || payload[triggerValue] != '[') return;
	static_cast<void>(parseJsonStringArrayLocal(payload, triggerValue, triggerCharacters));
}

bool completionResolveProviderAdvertised(const std::string &payload) {
	const std::string providerKey = "\"completionProvider\"";
	const std::string resolveKey = "\"resolveProvider\"";
	const std::size_t providerPos = payload.find(providerKey);
	std::size_t providerValue = 0;
	std::size_t providerEnd = 0;
	std::size_t resolvePos = 0;
	std::size_t resolveValue = 0;

	if (providerPos == std::string::npos) return false;
	providerValue = providerPos + providerKey.size();
	skipJsonWhitespaceLocal(payload, providerValue);
	if (providerValue >= payload.size() || payload[providerValue] != ':') return false;
	++providerValue;
	skipJsonWhitespaceLocal(payload, providerValue);
	if (providerValue >= payload.size() || payload[providerValue] != '{') return false;
	if (!findJsonObjectEndLocal(payload, providerValue, providerEnd)) return false;
	resolvePos = payload.find(resolveKey, providerValue);
	if (resolvePos == std::string::npos || resolvePos > providerEnd) return false;
	resolveValue = resolvePos + resolveKey.size();
	skipJsonWhitespaceLocal(payload, resolveValue);
	if (resolveValue >= payload.size() || payload[resolveValue] != ':') return false;
	++resolveValue;
	skipJsonWhitespaceLocal(payload, resolveValue);
	return resolveValue <= providerEnd && payload.compare(resolveValue, 4, "true") == 0;
}

const char *lspClientCapabilitiesJson() noexcept {
	return "\"capabilities\":{"
	       "\"workspace\":{\"workspaceFolders\":true,\"symbol\":{\"dynamicRegistration\":false}},"
	       "\"textDocument\":{"
	       "\"synchronization\":{\"didSave\":true},"
	       "\"publishDiagnostics\":{\"relatedInformation\":true},"
	       "\"hover\":{\"contentFormat\":[\"markdown\",\"plaintext\"]},"
	       "\"completion\":{\"contextSupport\":true,\"completionItem\":{"
	       "\"snippetSupport\":true,"
	       "\"commitCharactersSupport\":true,"
	       "\"documentationFormat\":[\"markdown\",\"plaintext\"],"
	       "\"deprecatedSupport\":true,"
	       "\"preselectSupport\":true,"
	       "\"insertReplaceSupport\":true,"
	       "\"resolveSupport\":{\"properties\":[\"documentation\",\"detail\",\"additionalTextEdits\"]}"
	       "},\"completionItemKind\":{\"valueSet\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25]}},"
	       "\"signatureHelp\":{\"signatureInformation\":{\"documentationFormat\":[\"markdown\",\"plaintext\"]}},"
	       "\"definition\":{\"linkSupport\":true},"
	       "\"references\":{\"dynamicRegistration\":false},"
	       "\"documentHighlight\":{\"dynamicRegistration\":false},"
	       "\"documentSymbol\":{\"hierarchicalDocumentSymbolSupport\":true,\"symbolKind\":{\"valueSet\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26]}},"
	       "\"codeAction\":{\"dynamicRegistration\":false},"
	       "\"rename\":{\"prepareSupport\":true}"
	       "}}";
}

const mr::services::MRLspServiceCommandSpec lspServiceCommandTable[] = {
	{ mr::services::MRLspServiceCommandId::GoToDefinition, mr::services::MRLspServiceRequestKind::Definition, false, "MR_LSP_GOTO_DEFINITION", "LSP Go To Definition" },
	{ mr::services::MRLspServiceCommandId::FindReferences, mr::services::MRLspServiceRequestKind::References, true, "MR_LSP_FIND_REFERENCES", "LSP Find References" },
	{ mr::services::MRLspServiceCommandId::ShowHover, mr::services::MRLspServiceRequestKind::Hover, false, "MR_LSP_SHOW_HOVER", "LSP Show Hover" },
	{ mr::services::MRLspServiceCommandId::Complete, mr::services::MRLspServiceRequestKind::Completion, false, "MR_LSP_COMPLETE", "LSP Complete" },
	{ mr::services::MRLspServiceCommandId::DocumentHighlight, mr::services::MRLspServiceRequestKind::DocumentHighlight, false, "MR_LSP_DOCUMENT_HIGHLIGHT", "LSP Document Highlight" },
	{ mr::services::MRLspServiceCommandId::DocumentSymbols, mr::services::MRLspServiceRequestKind::DocumentSymbols, false, "MR_LSP_DOCUMENT_SYMBOLS", "LSP Document Symbols" },
	{ mr::services::MRLspServiceCommandId::WorkspaceSymbols, mr::services::MRLspServiceRequestKind::WorkspaceSymbols, false, "MR_LSP_WORKSPACE_SYMBOLS", "LSP Workspace Symbols" },
	{ mr::services::MRLspServiceCommandId::SignatureHelp, mr::services::MRLspServiceRequestKind::SignatureHelp, false, "MR_LSP_SIGNATURE_HELP", "LSP Signature Help" },
	{ mr::services::MRLspServiceCommandId::Rename, mr::services::MRLspServiceRequestKind::Rename, false, "MR_LSP_RENAME", "LSP Rename" },
};

mr::lsp::LspCodeActionRange codeActionRangeFromServiceRange(const mr::services::MRServiceTextRange &range) {
	mr::lsp::LspCodeActionRange codeActionRange;

	codeActionRange.start.line = range.start.line;
	codeActionRange.start.character = range.start.character;
	codeActionRange.end.line = range.end.line;
	codeActionRange.end.character = range.end.character;
	return codeActionRange;
}

} // namespace

namespace mr::services {

MRLspServiceSession::MRLspServiceSession() noexcept
	: documentService(lifecycle) {
}

bool buildLspInitializeSpecFromWorkspace(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspSessionSpec &sessionSpec, const std::vector<std::string> &fallbackFlags, mr::lsp::LspInitializeSpec &spec, std::string &errorMessage) {
	std::string rootUri;
	std::string params;
	const std::string rootPath = normalizeWorkspaceServicePath(workspace.root.rootPath);

	spec = mr::lsp::LspInitializeSpec();
	spec.session = sessionSpec;
	params = "{\"processId\":null,";
	if (workspace.root.hasRoot) {
		if (!mr::lsp::pathToFileUri(rootPath, rootUri, errorMessage)) return false;
		params += "\"rootPath\":";
		appendJsonString(params, rootPath);
		params += ",\"rootUri\":";
		appendJsonString(params, rootUri);
		params += ",\"workspaceFolders\":[{\"uri\":";
		appendJsonString(params, rootUri);
		params += ",\"name\":";
		appendJsonString(params, workspaceFolderNameForRootPath(rootPath));
		params += "}],";
	} else {
		params += "\"rootPath\":null,\"rootUri\":null,\"workspaceFolders\":null,";
	}
	if (!fallbackFlags.empty()) {
		params += "\"initializationOptions\":{\"fallbackFlags\":";
		appendJsonStringArray(params, fallbackFlags);
		params += "},";
	}
	params += lspClientCapabilitiesJson();
	params += "}";
	spec.initializeParamsJson = params;
	errorMessage.clear();
	return true;
}

bool buildLspInitializeSpecFromWorkspace(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspSessionSpec &sessionSpec, mr::lsp::LspInitializeSpec &spec, std::string &errorMessage) {
	const std::vector<std::string> fallbackFlags;

	return buildLspInitializeSpecFromWorkspace(workspace, sessionSpec, fallbackFlags, spec, errorMessage);
}

bool buildLspInitializeSpecFromServerProfile(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, mr::lsp::LspInitializeSpec &spec, std::string &errorMessage) {
	mr::lsp::LspSessionSpec sessionSpec;
	std::vector<std::string> fallbackFlags;

	if (profile.executablePath.empty()) {
		errorMessage = "LSP server profile executable path is empty.";
		return false;
	}
	sessionSpec.process.executablePath = profile.executablePath;
	sessionSpec.process.arguments = profile.arguments;
	if (profileUsesClangd(profile)) {
		appendArgumentOnce(sessionSpec.process.arguments, "--compile_args_from=lsp");
		appendArgumentOnce(sessionSpec.process.arguments, "--strong-workspace-mode");
		if (workspace.compileContext.available) appendCompileContextFallbackFlags(workspace.compileContext, fallbackFlags, errorMessage);
		if (!errorMessage.empty()) return false;
	}
	if (!profile.workingDirectory.empty()) {
		sessionSpec.process.workingDirectory = normalizeWorkspaceServicePath(profile.workingDirectory);
	} else if (workspace.root.hasRoot) {
		sessionSpec.process.workingDirectory = normalizeWorkspaceServicePath(workspace.root.rootPath);
	}
	return buildLspInitializeSpecFromWorkspace(workspace, sessionSpec, fallbackFlags, spec, errorMessage);
}

bool lspServiceCommandSpec(MRLspServiceCommandId command, MRLspServiceCommandSpec &spec) noexcept {
	const std::size_t commandCount = sizeof(lspServiceCommandTable) / sizeof(lspServiceCommandTable[0]);

	for (std::size_t index = 0; index < commandCount; ++index) {
		if (lspServiceCommandTable[index].command != command) continue;
		spec = lspServiceCommandTable[index];
		return true;
	}
	spec = MRLspServiceCommandSpec();
	return false;
}

bool MRLspServiceSession::start(const mr::lsp::LspInitializeSpec &spec, std::string &errorMessage) {
	clearRequests();
	clearRuntimeBinding();
	resultStore.clear();
	hasActiveWorkspace = false;
	activeEditorDocumentId = 0;
	activeEditorDocumentVersion = 0;
	activeEditorDocumentPath.clear();
	return lifecycle.start(spec, errorMessage);
}

bool MRLspServiceSession::start(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, std::string &errorMessage) {
	mr::lsp::LspInitializeSpec spec;

	if (!buildLspInitializeSpecFromServerProfile(workspace, profile, spec, errorMessage)) return false;
	return start(spec, errorMessage);
}

bool MRLspServiceSession::startRuntime(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, std::string &errorMessage) {
	std::vector<std::string> fallbackFlags;

	if (profileUsesClangd(profile) && workspace.compileContext.available) {
		appendCompileContextFallbackFlags(workspace.compileContext, fallbackFlags, errorMessage);
		if (!errorMessage.empty()) return false;
	}
	if (!start(workspace, profile, errorMessage)) return false;
	if (!sendInitialized(errorMessage)) {
		close();
		return false;
	}
	updateCapabilitiesFromInitializeResponse(lifecycle.initializeResponsePayload());
	logLspCompileContext(workspace, profile, fallbackFlags);
	activeServerProfile = profile;
	activeRuntimeCompileContextFingerprint = compileContextFingerprint(workspace.compileContext);
	activeRuntimeHasRoot = workspace.root.hasRoot;
	if (workspace.root.hasRoot) {
		activeRuntimeRootPath = normalizeWorkspaceServicePath(workspace.root.rootPath);
	} else {
		activeRuntimeRootPath.clear();
	}
	hasActiveRuntime = true;
	errorMessage.clear();
	return true;
}

bool MRLspServiceSession::ensureRuntime(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile, std::string &errorMessage) {
	if (runtimeMatches(workspace, profile)) {
		errorMessage.clear();
		return true;
	}
	if (lifecycle.state() != mr::lsp::LspLifecycleState::Stopped && lifecycle.state() != mr::lsp::LspLifecycleState::Shutdown)
		close();
	return startRuntime(workspace, profile, errorMessage);
}

bool MRLspServiceSession::sendInitialized(std::string &errorMessage) {
	if (!pollUntilState(mr::lsp::LspLifecycleState::Initialized, errorMessage)) return false;
	return lifecycle.sendInitialized(errorMessage);
}

bool MRLspServiceSession::openDocument(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDocumentSourceSnapshot &source, std::string &errorMessage) {
	if (!acceptWorkspaceForSource(workspace, source, errorMessage)) return false;
	if (!documentService.open(source, errorMessage)) return false;
	activeWorkspace = workspace;
	hasActiveWorkspace = true;
	clearRequests();
	return true;
}

bool MRLspServiceSession::changeDocument(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDocumentSourceSnapshot &source, std::string &errorMessage) {
	if (!acceptWorkspaceForSource(workspace, source, errorMessage)) return false;
	if (!documentService.change(source, errorMessage)) return false;
	activeWorkspace = workspace;
	hasActiveWorkspace = true;
	resultStore.markStaleAgainstWorkspace(activeWorkspace);
	return true;
}

bool MRLspServiceSession::openEditorDocument(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, std::string &errorMessage) {
	mr::lsp::LspDocumentSourceSnapshot source;

	if (!buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage)) return false;
	if (!openDocument(workspace, source, errorMessage)) return false;
	activeEditorDocumentId = document.documentId;
	activeEditorDocumentVersion = document.documentVersion;
	activeEditorDocumentPath = normalizeWorkspaceServicePath(document.path);
	return true;
}

bool MRLspServiceSession::changeEditorDocument(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, std::string &errorMessage) {
	mr::lsp::LspDocumentSourceSnapshot source;

	if (!buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage)) return false;
	if (!changeDocument(workspace, source, errorMessage)) return false;
	activeEditorDocumentId = document.documentId;
	activeEditorDocumentVersion = document.documentVersion;
	activeEditorDocumentPath = normalizeWorkspaceServicePath(document.path);
	return true;
}

bool MRLspServiceSession::syncEditorDocument(const MRWorkspaceServiceSnapshot &workspace, const MRWorkspaceDocumentSnapshot &document, const MRFileEditor &editor, std::string &errorMessage) {
	mr::lsp::LspDocumentSourceSnapshot source;
	const std::string documentPath = normalizeWorkspaceServicePath(document.path);

	if (!buildLspDocumentSourceSnapshotFromEditor(document, editor, source, errorMessage)) return false;
	if (!documentService.hasOpenDocument(source, errorMessage)) {
		if (!openDocument(workspace, source, errorMessage)) return false;
		activeEditorDocumentId = document.documentId;
		activeEditorDocumentVersion = document.documentVersion;
		activeEditorDocumentPath = documentPath;
		return true;
	}
	if (documentService.matchesSentVersion(source, errorMessage)) {
		if (!documentService.activate(source, errorMessage)) return false;
		activeEditorDocumentId = document.documentId;
		activeEditorDocumentVersion = document.documentVersion;
		activeEditorDocumentPath = documentPath;
		return true;
	}
	if (documentService.isStaleForSentVersion(source, errorMessage)) {
		if (!documentService.close(source, errorMessage)) return false;
		if (!openDocument(workspace, source, errorMessage)) return false;
		activeEditorDocumentId = document.documentId;
		activeEditorDocumentVersion = document.documentVersion;
		activeEditorDocumentPath = documentPath;
		return true;
	}
	if (!changeDocument(workspace, source, errorMessage)) return false;
	activeEditorDocumentId = document.documentId;
	activeEditorDocumentVersion = document.documentVersion;
	activeEditorDocumentPath = documentPath;
	return true;
}

bool MRLspServiceSession::syncEditorDocumentAndRequest(
	const MRWorkspaceServiceSnapshot &workspace,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	MRLspServiceRequestKind requestKind,
	mr::lsp::LspTextPosition position,
	bool includeDeclaration,
	const std::string &completionTriggerCandidate,
	std::string &errorMessage) {
	if (!syncEditorDocument(workspace, document, editor, errorMessage)) return false;
	if (!requestKindSupported(requestKind, errorMessage)) return false;
	switch (requestKind) {
		case MRLspServiceRequestKind::Definition:
			return requestDefinition(position, errorMessage);
		case MRLspServiceRequestKind::References:
			return requestReferences(position, includeDeclaration, errorMessage);
		case MRLspServiceRequestKind::Hover:
			return requestHover(position, errorMessage);
		case MRLspServiceRequestKind::Completion:
			return requestCompletion(position, completionTriggerCandidate, errorMessage);
		case MRLspServiceRequestKind::DocumentHighlight:
			return requestDocumentHighlight(position, errorMessage);
		case MRLspServiceRequestKind::DocumentSymbols:
			return requestDocumentSymbols(errorMessage);
		case MRLspServiceRequestKind::WorkspaceSymbols:
			return requestWorkspaceSymbols(std::string(), errorMessage);
		case MRLspServiceRequestKind::SignatureHelp:
			return requestSignatureHelp(position, errorMessage);
		case MRLspServiceRequestKind::Rename:
			errorMessage = "LSP rename requires a new name.";
			return false;
	}
	errorMessage = "LSP service request kind is unknown.";
	return false;
}

bool MRLspServiceSession::requestEditorDocumentService(
	const MRWorkspaceServiceSnapshot &workspace,
	const MRLspServerProfile &profile,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	MRLspServiceRequestKind requestKind,
	mr::lsp::LspTextPosition position,
	bool includeDeclaration,
	const std::string &completionTriggerCandidate,
	std::string &errorMessage) {
	if (!ensureRuntime(workspace, profile, errorMessage)) return false;
	return syncEditorDocumentAndRequest(workspace, document, editor, requestKind, position, includeDeclaration, completionTriggerCandidate, errorMessage);
}

bool MRLspServiceSession::requestEditorDocumentServiceCommand(
	const MRWorkspaceServiceSnapshot &workspace,
	const MRLspServerProfile &profile,
	const MRWorkspaceDocumentSnapshot &document,
	const MRFileEditor &editor,
	MRLspServiceCommandId command,
	mr::lsp::LspTextPosition position,
	const std::string &completionTriggerCandidate,
	std::string &errorMessage) {
	MRLspServiceCommandSpec spec;

	if (!lspServiceCommandSpec(command, spec)) {
		errorMessage = "LSP service command is unknown.";
		return false;
	}
	return requestEditorDocumentService(workspace, profile, document, editor, spec.requestKind, position, spec.includeDeclaration, completionTriggerCandidate, errorMessage);
}

bool MRLspServiceSession::poll(std::string &errorMessage) {
	std::vector<mr::lsp::LspInboundMessage> messages;

	if (!lifecycle.poll(messages, errorMessage)) return false;
	for (const mr::lsp::LspInboundMessage &message : messages)
		if (!consumeInboundMessage(message, errorMessage)) return false;
	errorMessage.clear();
	return true;
}

bool MRLspServiceSession::requestDefinition(mr::lsp::LspTextPosition position, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (!requestKindSupported(MRLspServiceRequestKind::Definition, errorMessage)) return false;
	if (!definitionAdapter.requestDefinition(lifecycle, documentService, position, definitionRequest, errorMessage)) return false;
	definitionRequestVersion = activeEditorDocumentVersion;
	return true;
}

bool MRLspServiceSession::requestReferences(mr::lsp::LspTextPosition position, bool includeDeclaration, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (!requestKindSupported(MRLspServiceRequestKind::References, errorMessage)) return false;
	if (!referencesAdapter.requestReferences(lifecycle, documentService, position, includeDeclaration, referencesRequest, errorMessage)) return false;
	referencesRequestVersion = activeEditorDocumentVersion;
	return true;
}

bool MRLspServiceSession::requestHover(mr::lsp::LspTextPosition position, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (!requestKindSupported(MRLspServiceRequestKind::Hover, errorMessage)) return false;
	if (!hoverAdapter.requestHover(lifecycle, documentService, position, hoverRequest, errorMessage)) return false;
	hoverRequestVersion = activeEditorDocumentVersion;
	return true;
}

bool MRLspServiceSession::requestCompletion(mr::lsp::LspTextPosition position, const std::string &triggerCandidate, std::string &errorMessage) {
	std::string triggerCharacter;

	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (!requestKindSupported(MRLspServiceRequestKind::Completion, errorMessage)) return false;
	static_cast<void>(completionTriggerCharacterAccepted(triggerCandidate, triggerCharacter));
	if (!completionAdapter.requestCompletion(lifecycle, documentService, position, triggerCharacter, completionRequest, errorMessage)) return false;
	completionRequestVersion = activeEditorDocumentVersion;
	return true;
}

bool MRLspServiceSession::requestDocumentHighlight(mr::lsp::LspTextPosition position, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (!requestKindSupported(MRLspServiceRequestKind::DocumentHighlight, errorMessage)) return false;
	if (!documentHighlightAdapter.requestDocumentHighlight(lifecycle, documentService, position, documentHighlightRequest, errorMessage)) return false;
	documentHighlightRequestVersion = activeEditorDocumentVersion;
	return true;
}

bool MRLspServiceSession::requestDocumentSymbols(std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (!requestKindSupported(MRLspServiceRequestKind::DocumentSymbols, errorMessage)) return false;
	if (!documentSymbolsAdapter.requestDocumentSymbols(lifecycle, documentService, documentSymbolsRequest, errorMessage)) return false;
	documentSymbolsRequestVersion = activeEditorDocumentVersion;
	return true;
}

bool MRLspServiceSession::requestWorkspaceSymbols(const std::string &query, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (!requestKindSupported(MRLspServiceRequestKind::WorkspaceSymbols, errorMessage)) return false;
	return documentSymbolsAdapter.requestWorkspaceSymbols(lifecycle, query, workspaceSymbolsRequest, errorMessage);
}

bool MRLspServiceSession::requestSignatureHelp(mr::lsp::LspTextPosition position, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (!requestKindSupported(MRLspServiceRequestKind::SignatureHelp, errorMessage)) return false;
	if (!signatureHelpAdapter.requestSignatureHelp(lifecycle, documentService, position, signatureHelpRequest, errorMessage)) return false;
	signatureHelpRequestVersion = activeEditorDocumentVersion;
	return true;
}

bool MRLspServiceSession::requestRename(mr::lsp::LspTextPosition position, const std::string &newName, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (!requestKindSupported(MRLspServiceRequestKind::Rename, errorMessage)) return false;
	if (!renameAdapter.requestRename(lifecycle, documentService, position, newName, renameRequest, errorMessage)) return false;
	renameRequestVersion = activeEditorDocumentVersion;
	return true;
}

bool MRLspServiceSession::requestCodeActionsForDiagnostic(const MRServiceDiagnosticResult &diagnosticResult, const MRServiceDiagnosticEntry &diagnostic, std::string &errorMessage) {
	if (!hasActiveWorkspace) {
		errorMessage = "LSP service session has no active workspace.";
		return false;
	}
	if (diagnosticResult.header.state != MRServiceResultState::Current) {
		errorMessage = "LSP codeAction diagnostic result is not current.";
		return false;
	}
	if (!serviceDocumentIdentityMatches(activeWorkspace, diagnosticResult.header.identity)) {
		errorMessage = "LSP codeAction diagnostic result no longer matches workspace.";
		return false;
	}
	if (diagnosticResult.header.identity.uri != documentService.documentUri()) {
		errorMessage = "LSP codeAction diagnostic document does not match open document.";
		return false;
	}
	if (!supportsCodeAction) {
		errorMessage = "LSP server does not advertise code action support.";
		return false;
	}
	if (!codeActionAdapter.requestCodeActions(lifecycle, documentService, codeActionRangeFromServiceRange(diagnostic.reportedRange), diagnostic.rawLspDiagnosticJson, codeActionRequest, errorMessage)) return false;
	codeActionRequestRange = diagnostic.navigationRange;
	codeActionRequestVersion = diagnosticResult.header.identity.documentVersion;
	return true;
}

bool MRLspServiceSession::resolveCompletionItem(const MRServiceCompletionItem &item, MRServiceCompletionItem &resolvedItem, std::string &errorMessage) {
	mr::lsp::LspCompletionItem lspItem;
	mr::lsp::LspCompletionResolveRequest request;
	mr::lsp::LspCompletionResolveResult result;
	std::vector<mr::lsp::LspInboundMessage> messages;

	resolvedItem = item;
	if (!supportsCompletionResolve || item.rawLspCompletionItemJson.empty()) {
		errorMessage.clear();
		return true;
	}
	lspItem.rawJson = item.rawLspCompletionItemJson;
	lspItem.label = item.label;
	if (!completionAdapter.requestResolve(lifecycle, lspItem, request, errorMessage)) return false;
	for (int round = 0; round < 25; ++round) {
		if (!lifecycle.poll(messages, errorMessage)) return false;
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;

			if (!completionAdapter.consumeResolve(message, request, result, accepted, errorMessage)) return false;
			if (accepted) {
				if (!result.item.label.empty()) resolvedItem = buildServiceCompletionItemFromLsp(result.item);
				errorMessage.clear();
				return true;
			}
			if (!consumeInboundMessage(message, errorMessage)) return false;
		}
		::poll(nullptr, 0, 20);
	}
	errorMessage.clear();
	return true;
}

bool MRLspServiceSession::closeDocument(std::string &errorMessage) {
	if (!documentService.isOpen()) {
		activeEditorDocumentId = 0;
		activeEditorDocumentVersion = 0;
		activeEditorDocumentPath.clear();
		errorMessage.clear();
		return true;
	}
	if (!documentService.close(errorMessage)) return false;
	clearRequests();
	hasActiveWorkspace = false;
	activeWorkspace = MRWorkspaceServiceSnapshot();
	activeEditorDocumentId = 0;
	activeEditorDocumentVersion = 0;
	activeEditorDocumentPath.clear();
	return true;
}

bool MRLspServiceSession::shutdown(std::string &errorMessage) {
	int exitStatus = -1;

	if (!lifecycle.shutdown(errorMessage)) return false;
	if (!pollUntilState(mr::lsp::LspLifecycleState::Shutdown, errorMessage)) return false;
	if (!lifecycle.exit(errorMessage)) return false;
	if (!lifecycle.wait(1000, exitStatus)) {
		errorMessage = "LSP service session wait failed.";
		return false;
	}
	if (exitStatus != 0) {
		errorMessage = "LSP service session exited with status " + std::to_string(exitStatus) + ".";
		return false;
	}
	errorMessage.clear();
	return true;
}

void MRLspServiceSession::close() {
	clearRequests();
	clearRuntimeBinding();
	documentService.clear();
	lifecycle.close();
	hasActiveWorkspace = false;
	activeWorkspace = MRWorkspaceServiceSnapshot();
	activeEditorDocumentId = 0;
	activeEditorDocumentVersion = 0;
	activeEditorDocumentPath.clear();
}

const MRServiceResultStore &MRLspServiceSession::results() const noexcept {
	return resultStore;
}

bool MRLspServiceSession::runtimeActive() const noexcept {
	return hasActiveRuntime && lifecycle.state() == mr::lsp::LspLifecycleState::Initialized;
}

bool MRLspServiceSession::runtimeCapabilitiesKnown() const noexcept {
	return runtimeActive();
}

bool MRLspServiceSession::supportsRequestKind(MRLspServiceRequestKind requestKind) const noexcept {
	switch (requestKind) {
		case MRLspServiceRequestKind::Definition:
			return supportsDefinition;
		case MRLspServiceRequestKind::References:
			return supportsReferences;
		case MRLspServiceRequestKind::Hover:
			return supportsHover;
		case MRLspServiceRequestKind::Completion:
			return supportsCompletion;
		case MRLspServiceRequestKind::DocumentHighlight:
			return supportsDocumentHighlight;
		case MRLspServiceRequestKind::DocumentSymbols:
			return supportsDocumentSymbols;
		case MRLspServiceRequestKind::WorkspaceSymbols:
			return supportsWorkspaceSymbols;
		case MRLspServiceRequestKind::SignatureHelp:
			return supportsSignatureHelp;
		case MRLspServiceRequestKind::Rename:
			return supportsRename;
	}
	return false;
}

bool MRLspServiceSession::supportsCodeActions() const noexcept {
	return supportsCodeAction;
}

std::string MRLspServiceSession::activeHoverRequestId() const {
	return hoverRequest.pending ? hoverRequest.idText : std::string();
}

std::string MRLspServiceSession::activeSignatureHelpRequestId() const {
	return signatureHelpRequest.pending ? signatureHelpRequest.idText : std::string();
}

bool MRLspServiceSession::pollUntilState(mr::lsp::LspLifecycleState expectedState, std::string &errorMessage) {
	for (int i = 0; i < 50; ++i) {
		if (!poll(errorMessage)) return false;
		if (lifecycle.state() == expectedState) return true;
		::poll(nullptr, 0, 20);
	}
	errorMessage = "LSP service session did not reach expected lifecycle state.";
	return false;
}

bool MRLspServiceSession::acceptWorkspaceForSource(const MRWorkspaceServiceSnapshot &workspace, const mr::lsp::LspDocumentSourceSnapshot &source, std::string &errorMessage) {
	const std::string sourcePath = normalizeWorkspaceServicePath(source.absolutePath);

	if (sourcePath.empty()) {
		errorMessage = "LSP service source path is empty.";
		return false;
	}
	for (const MRWorkspaceDocumentSnapshot &document : workspace.documents) {
		if (document.path != sourcePath) continue;
		if (document.documentVersion != static_cast<std::size_t>(source.version)) {
			errorMessage = "LSP service source version does not match workspace document version.";
			return false;
		}
		errorMessage.clear();
		return true;
	}
	errorMessage = "LSP service source document is not part of the workspace.";
	return false;
}

bool MRLspServiceSession::runtimeMatches(const MRWorkspaceServiceSnapshot &workspace, const MRLspServerProfile &profile) const {
	std::string rootPath;

	if (!hasActiveRuntime) return false;
	if (lifecycle.state() != mr::lsp::LspLifecycleState::Initialized) return false;
	if (activeServerProfile.profileName != profile.profileName) return false;
	if (activeServerProfile.executablePath != profile.executablePath) return false;
	if (activeServerProfile.arguments != profile.arguments) return false;
	if (activeServerProfile.workingDirectory != profile.workingDirectory) return false;
	if (activeServerProfile.lspMiddlewarePath != profile.lspMiddlewarePath) return false;
	if (profileUsesClangd(profile) && activeRuntimeCompileContextFingerprint != compileContextFingerprint(workspace.compileContext)) return false;
	if (activeRuntimeHasRoot != workspace.root.hasRoot) return false;
	if (!workspace.root.hasRoot) return true;
	rootPath = normalizeWorkspaceServicePath(workspace.root.rootPath);
	return activeRuntimeRootPath == rootPath;
}

bool MRLspServiceSession::requestKindSupported(MRLspServiceRequestKind requestKind, std::string &errorMessage) const {
	const char *name = "requested";

	switch (requestKind) {
		case MRLspServiceRequestKind::Definition:
			name = "definition";
			break;
		case MRLspServiceRequestKind::References:
			name = "references";
			break;
		case MRLspServiceRequestKind::Hover:
			name = "hover";
			break;
		case MRLspServiceRequestKind::Completion:
			name = "completion";
			break;
		case MRLspServiceRequestKind::DocumentHighlight:
			name = "document highlight";
			break;
		case MRLspServiceRequestKind::DocumentSymbols:
			name = "document symbols";
			break;
		case MRLspServiceRequestKind::WorkspaceSymbols:
			name = "workspace symbols";
			break;
		case MRLspServiceRequestKind::SignatureHelp:
			name = "signature help";
			break;
		case MRLspServiceRequestKind::Rename:
			name = "rename";
			break;
	}
	if (supportsRequestKind(requestKind)) {
		errorMessage.clear();
		return true;
	}
	errorMessage = std::string("LSP server does not advertise ") + name + " support.";
	return false;
}

bool MRLspServiceSession::completionTriggerCharacterAccepted(const std::string &candidate, std::string &triggerCharacter) const {
	triggerCharacter.clear();
	if (candidate.empty()) return false;
	for (const std::string &serverTriggerCharacter : completionTriggerCharacters) {
		if (serverTriggerCharacter != candidate) continue;
		triggerCharacter = candidate;
		return true;
	}
	return false;
}

void MRLspServiceSession::updateCapabilitiesFromInitializeResponse(const std::string &payload) noexcept {
	supportsDefinition = initializeCapabilityIsAdvertised(payload, "definitionProvider");
	supportsReferences = initializeCapabilityIsAdvertised(payload, "referencesProvider");
	supportsHover = initializeCapabilityIsAdvertised(payload, "hoverProvider");
	supportsCompletion = initializeCapabilityIsAdvertised(payload, "completionProvider");
	supportsDocumentHighlight = initializeCapabilityIsAdvertised(payload, "documentHighlightProvider");
	supportsDocumentSymbols = initializeCapabilityIsAdvertised(payload, "documentSymbolProvider");
	supportsWorkspaceSymbols = initializeCapabilityIsAdvertised(payload, "workspaceSymbolProvider");
	supportsSignatureHelp = initializeCapabilityIsAdvertised(payload, "signatureHelpProvider");
	supportsRename = initializeCapabilityIsAdvertised(payload, "renameProvider");
	supportsCodeAction = initializeCapabilityIsAdvertised(payload, "codeActionProvider");
	supportsCompletionResolve = completionResolveProviderAdvertised(payload);
	extractCompletionTriggerCharacters(payload, completionTriggerCharacters);
}

bool MRLspServiceSession::consumeInboundMessage(const mr::lsp::LspInboundMessage &message, std::string &errorMessage) {
	mr::lsp::LspDiagnosticBatch batch;
	mr::lsp::LspDefinitionResult definition;
	mr::lsp::LspReferencesResult references;
	mr::lsp::LspHoverResult hover;
	mr::lsp::LspCompletionResult completion;
	mr::lsp::LspCodeActionResult codeActions;
	mr::lsp::LspDocumentHighlightResult documentHighlight;
	mr::lsp::LspDocumentSymbolsResult documentSymbols;
	mr::lsp::LspWorkspaceSymbolsResult workspaceSymbols;
	mr::lsp::LspSignatureHelpResult signatureHelp;
	mr::lsp::LspRenameResult rename;
	bool accepted = false;

	if (hasActiveWorkspace) {
		if (!diagnosticsAdapter.consume(message, documentService, batch, errorMessage)) return false;
		if (batch.accepted) resultStore.putDiagnostics(buildServiceDiagnosticsFromLsp(activeWorkspace, batch));
	}

	if (!definitionAdapter.consume(message, documentService, definitionRequest, definition, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putLocations(buildServiceDefinitionFromLsp(activeWorkspace, definitionRequest.uri, definitionRequestVersion, definitionRequest.idText, definition));
		return true;
	}

	if (!referencesAdapter.consume(message, documentService, referencesRequest, references, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putLocations(buildServiceReferencesFromLsp(activeWorkspace, referencesRequest.uri, referencesRequestVersion, referencesRequest.idText, references));
		return true;
	}

	if (!hoverAdapter.consume(message, documentService, hoverRequest, hover, accepted, errorMessage)) return false;
	if (!errorMessage.empty()) {
		MRServiceHoverResult result;
		std::string hoverPath;
		std::string uriError;

		result.header.source = MRServiceResultSource::Lsp;
		result.header.kind = MRServiceResultKind::Hover;
		result.header.state = MRServiceResultState::Error;
		result.header.requestId = hoverRequest.idText;
		result.header.errorMessage = errorMessage;
		if (mr::lsp::fileUriToPath(hoverRequest.uri, hoverPath, uriError)) hoverPath = normalizeWorkspaceServicePath(hoverPath);
		for (const MRWorkspaceDocumentSnapshot &document : activeWorkspace.documents) {
			if (document.path != hoverPath) continue;

			result.header.identity.valid = true;
			result.header.identity.bufferId = document.bufferId;
			result.header.identity.documentId = document.documentId;
			result.header.identity.documentVersion = hoverRequestVersion;
			result.header.identity.path = document.path;
			result.header.identity.uri = hoverRequest.uri;
			break;
		}
		resultStore.putHover(result);
		errorMessage.clear();
		return true;
	}
	if (accepted) {
		resultStore.putHover(buildServiceHoverFromLsp(activeWorkspace, hoverRequestVersion, hoverRequest.idText, hover));
		return true;
	}

	if (!completionAdapter.consume(message, documentService, completionRequest, completion, accepted, errorMessage)) return false;
	if (accepted) {
		MRServiceCompletionResult result = buildServiceCompletionFromLsp(activeWorkspace, completionRequest.uri, completionRequestVersion, completionRequest.idText, completion);

		result.hasRequestPosition = true;
		result.requestPosition = MRServiceTextPosition{completionRequest.position.line, completionRequest.position.character};
		result.hasTriggerCharacter = completionRequest.hasTriggerCharacter;
		result.triggerCharacter = completionRequest.triggerCharacter;
		result.lspMiddlewarePath = activeServerProfile.lspMiddlewarePath;
		resultStore.putCompletion(result);
	}
	if (!documentHighlightAdapter.consume(message, documentService, documentHighlightRequest, documentHighlight, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putDocumentHighlights(buildServiceDocumentHighlightsFromLsp(activeWorkspace, documentHighlightRequest.uri, documentHighlightRequestVersion, documentHighlightRequest.idText, documentHighlight));
		return true;
	}
	if (!codeActionAdapter.consume(message, documentService, codeActionRequest, codeActions, accepted, errorMessage)) return false;
	if (accepted) {
		MRServiceCodeActionResult result = buildServiceCodeActionsFromLsp(activeWorkspace, codeActionRequest.uri, codeActionRequestVersion, codeActionRequest.idText, codeActions);

		result.hasContextRange = true;
		result.contextRange = codeActionRequestRange;
		resultStore.putCodeActions(result);
	}
	if (!documentSymbolsAdapter.consume(message, documentService, documentSymbolsRequest, documentSymbols, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putDocumentSymbols(buildServiceDocumentSymbolsFromLsp(activeWorkspace, documentSymbolsRequest.uri, documentSymbolsRequestVersion, documentSymbolsRequest.idText, documentSymbols));
		return true;
	}
	if (!documentSymbolsAdapter.consumeWorkspaceSymbols(message, workspaceSymbolsRequest, workspaceSymbols, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putDocumentSymbols(buildServiceWorkspaceSymbolsFromLsp(workspaceSymbolsRequest.idText, workspaceSymbols));
		return true;
	}
	if (!signatureHelpAdapter.consume(message, documentService, signatureHelpRequest, signatureHelp, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putSignatureHelp(buildServiceSignatureHelpFromLsp(activeWorkspace, signatureHelpRequest.uri, signatureHelpRequestVersion, signatureHelpRequest.idText, signatureHelp));
		return true;
	}
	if (!renameAdapter.consume(message, documentService, renameRequest, rename, accepted, errorMessage)) return false;
	if (accepted) {
		resultStore.putRename(buildServiceRenameFromLsp(activeWorkspace, renameRequest.uri, renameRequestVersion, renameRequest.idText, rename));
	}
	return true;
}

void MRLspServiceSession::clearRequests() noexcept {
	definitionRequest = mr::lsp::LspDefinitionRequest();
	referencesRequest = mr::lsp::LspReferencesRequest();
	hoverRequest = mr::lsp::LspHoverRequest();
	completionRequest = mr::lsp::LspCompletionRequest();
	documentHighlightRequest = mr::lsp::LspDocumentHighlightRequest();
	documentSymbolsRequest = mr::lsp::LspDocumentSymbolsRequest();
	workspaceSymbolsRequest = mr::lsp::LspWorkspaceSymbolsRequest();
	signatureHelpRequest = mr::lsp::LspSignatureHelpRequest();
	codeActionRequest = mr::lsp::LspCodeActionRequest();
	renameRequest = mr::lsp::LspRenameRequest();
	definitionRequestVersion = 0;
	referencesRequestVersion = 0;
	hoverRequestVersion = 0;
	completionRequestVersion = 0;
	documentHighlightRequestVersion = 0;
	documentSymbolsRequestVersion = 0;
	signatureHelpRequestVersion = 0;
	codeActionRequestRange = MRServiceTextRange();
	codeActionRequestVersion = 0;
	renameRequestVersion = 0;
}

void MRLspServiceSession::clearRuntimeBinding() noexcept {
	activeServerProfile = MRLspServerProfile();
	activeRuntimeRootPath.clear();
	activeRuntimeCompileContextFingerprint.clear();
	activeRuntimeHasRoot = false;
	hasActiveRuntime = false;
	supportsDefinition = false;
	supportsReferences = false;
	supportsHover = false;
	supportsCompletion = false;
	supportsDocumentHighlight = false;
	supportsDocumentSymbols = false;
	supportsWorkspaceSymbols = false;
	supportsSignatureHelp = false;
	supportsRename = false;
	supportsCodeAction = false;
	supportsCompletionResolve = false;
	completionTriggerCharacters.clear();
}

} // namespace mr::services
