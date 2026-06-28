#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "../lsp/MRLspCompletion.hpp"
#include "../lsp/MRLspDocumentSymbols.hpp"
#include "../lsp/MRLspHover.hpp"
#include "../lsp/MRLspSignatureHelp.hpp"
#include "../lsp/MRLspUri.hpp"

namespace {

struct LiveServerSpec {
	const char *name;
	const char *executable;
	const char *argument1;
	const char *argument2;
	const char *languageId;
	const char *fileName;
	const char *documentText;
	const char *completionNeedle;
	const char *completionTrigger;
	const char *expectedCompletion;
	bool requireExpectedCompletion;
	bool requireDocumentation;
	bool requireSnippet;
};

struct LiveServerRun {
	LiveServerSpec spec;
	std::string executablePath;
	std::filesystem::path workDirectory;
	std::filesystem::path filePath;
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspDocumentService *documentService = nullptr;
};

struct CompletionCheck {
	const char *label;
	const char *needle;
	const char *trigger;
	const char *expectedCompletion;
	bool requireExpectedCompletion;
	bool requireDocumentation;
	bool requireSnippet;
};

const char *kDigestifDocument =
	"\\documentclass{article}\n"
	"\\usepackage{tikz}\n"
	"\\begin{document}\n"
	"\\usepackage{ti\n"
	"\\beg\n"
	"\\begin{tab\n"
	"\\begin{tabular\n"
	"\\begin{tikzpicture}\n"
	"\\draw[li\n"
	"\\end{tikzpicture}\n"
	"\\section{Alpha}\\label{sec:alpha}\n"
	"See \\ref{sec:alpha}.\n"
	"\\end{document}\n";

const LiveServerSpec kServerSpecs[] = {
	{ "digestif", "digestif", nullptr, nullptr, "latex", "main.tex", kDigestifDocument, "\\usepackage{ti", "{", "tikz", true, true, false },
	{ "texlab", "texlab", "run", nullptr, "latex", "main.tex", kDigestifDocument, "\\usepackage{ti", "{", "tikz", false, false, false },
	{ "clangd", "clangd", nullptr, nullptr, "cpp", "main.cpp", "#include <vector>\nint main() {\n    std::ve\n}\n", "std::ve", ".", "vector", false, false, false },
	{ "typescript-language-server", "typescript-language-server", "--stdio", nullptr, "typescript", "main.ts", "const alphaValue = 1;\nalpha\n", "alpha", "", "alphaValue", false, false, false },
	{ "pyright-langserver", "pyright-langserver", "--stdio", nullptr, "python", "main.py", "import os\nos.pa\n", "os.pa", ".", "path", false, false, false },
	{ "basedpyright-langserver", "basedpyright-langserver", "--stdio", nullptr, "python", "main.py", "import os\nos.pa\n", "os.pa", ".", "path", false, false, false },
	{ "pylsp", "pylsp", nullptr, nullptr, "python", "main.py", "import os\nos.pa\n", "os.pa", ".", "path", false, false, false },
	{ "vscode-json-language-server", "vscode-json-language-server", "--stdio", nullptr, "json", "main.json", "{\n  \"alpha\": true\n}\n", "\"alpha", "\"", "alpha", false, false, false },
	{ "yaml-language-server", "yaml-language-server", "--stdio", nullptr, "yaml", "main.yaml", "alpha: true\nbr\n", "br", "", "true", false, false, false },
	{ "bash-language-server", "bash-language-server", "start", nullptr, "shellscript", "main.sh", "#!/usr/bin/env bash\necho he\n", "echo he", "", "help", false, false, false },
	{ "fish-lsp", "fish-lsp", nullptr, nullptr, "fish", "main.fish", "echo he\n", "echo he", "", "help", false, false, false },
	{ "sourcekit-lsp", "sourcekit-lsp", nullptr, nullptr, "swift", "main.swift", "import Foundation\npri\n", "pri", "", "print", false, false, false },
	{ "rust-analyzer", "rust-analyzer", nullptr, nullptr, "rust", "main.rs", "fn main() {\n    prin\n}\n", "prin", "", "println", false, false, false },
	{ "gopls", "gopls", nullptr, nullptr, "go", "main.go", "package main\nfunc main() {\n    prin\n}\n", "prin", "", "println", false, false, false },
	{ "perlnavigator", "perlnavigator", "--stdio", nullptr, "perl", "main.pl", "use strict;\npri\n", "pri", "", "print", false, false, false },
	{ "kotlin-lsp", "kotlin-lsp", nullptr, nullptr, "kotlin", "Main.kt", "fun main() {\n    pri\n}\n", "pri", "", "println", false, false, false },
	{ "kotlin-language-server", "kotlin-language-server", nullptr, nullptr, "kotlin", "Main.kt", "fun main() {\n    pri\n}\n", "pri", "", "println", false, false, false },
	{ "csharp-ls", "csharp-ls", nullptr, nullptr, "csharp", "Program.cs", "class Program { static void Main() { Con } }\n", "Con", "", "Console", false, false, false },
	{ "pasls", "pasls", nullptr, nullptr, "pascal", "main.pas", "program main;\nbegin\n  writ\nend.\n", "writ", "", "writeln", false, false, false },
	{ "marksman", "marksman", nullptr, nullptr, "markdown", "main.md", "# Alpha\n\n[Al\n", "[Al", "[", "Alpha", false, false, false },
};

const CompletionCheck kDigestifChecks[] = {
	{ "package completion", "\\usepackage{ti", "{", "tikz", true, true, false },
	{ "environment completion", "\\begin{tab", "{", "tabular", true, false, false },
	{ "environment completion inside literal", "\\begin{tabu", "{", "tabular", true, false, false },
	{ "begin snippet completion", "\\beg", "", "begin", false, false, false },
	{ "tikz option completion", "\\draw[li", "[", "line", true, false, false },
};

std::string jsonString(const std::string &value) {
	std::string out = "\"";

	for (std::size_t index = 0; index < value.size(); ++index) {
		const char ch = value[index];
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
	return out;
}

bool isExecutableFile(const std::string &path) {
	return !path.empty() && ::access(path.c_str(), X_OK) == 0;
}

bool findExecutableOnPath(const std::string &executableName, std::string &resolvedPath) {
	const char *pathEnvironment = std::getenv("PATH");
	const std::string pathText = pathEnvironment != nullptr ? pathEnvironment : "";
	std::size_t start = 0;

	resolvedPath.clear();
	if (executableName.find('/') != std::string::npos) {
		if (!isExecutableFile(executableName)) return false;
		resolvedPath = executableName;
		return true;
	}
	while (start <= pathText.size()) {
		std::size_t end = pathText.find(':', start);
		std::string directory;
		std::string candidate;

		if (end == std::string::npos) end = pathText.size();
		directory = pathText.substr(start, end - start);
		if (directory.empty()) directory = ".";
		candidate = directory;
		if (!candidate.empty() && candidate[candidate.size() - 1] != '/') candidate += "/";
		candidate += executableName;
		if (isExecutableFile(candidate)) {
			resolvedPath = candidate;
			return true;
		}
		if (end == pathText.size()) break;
		start = end + 1;
	}
	return false;
}

std::filesystem::path makeWorkDirectory(const std::string &name) {
	std::ostringstream text;

	text << "/tmp/mr-lsp-live-" << ::getpid() << '-' << name;
	return std::filesystem::path(text.str());
}

bool writeDocumentFile(const std::filesystem::path &path, const std::string &text, std::string &errorMessage) {
	std::ofstream out(path);

	if (!out) {
		errorMessage = "cannot create " + path.string();
		return false;
	}
	out << text;
	if (!out) {
		errorMessage = "cannot write " + path.string();
		return false;
	}
	errorMessage.clear();
	return true;
}

bool findNeedlePosition(const std::string &text, const std::string &needle, mr::lsp::LspTextPosition &position, std::string &errorMessage) {
	const std::size_t found = text.find(needle);
	int line = 0;
	int character = 0;

	if (found == std::string::npos) {
		errorMessage = "needle not found: " + needle;
		return false;
	}
	for (std::size_t index = 0; index < found + needle.size(); ++index) {
		if (text[index] == '\n') {
			++line;
			character = 0;
		} else {
			++character;
		}
	}
	position.line = line;
	position.character = character;
	errorMessage.clear();
	return true;
}

bool pollLifecycleUntilState(mr::lsp::LspLifecycle &lifecycle, mr::lsp::LspLifecycleState state, int timeoutMs, std::string &errorMessage) {
	std::vector<mr::lsp::LspInboundMessage> messages;
	const int stepMs = 20;
	const int rounds = timeoutMs / stepMs;

	for (int round = 0; round < rounds; ++round) {
		if (!lifecycle.poll(messages, errorMessage)) return false;
		if (lifecycle.state() == state) {
			errorMessage.clear();
			return true;
		}
		::poll(nullptr, 0, stepMs);
	}
	errorMessage = std::string("lifecycle did not reach ") + mr::lsp::lspLifecycleStateName(state) + ", state=" + mr::lsp::lspLifecycleStateName(lifecycle.state());
	return false;
}

std::string initializeParams(const std::filesystem::path &workDirectory) {
	std::string rootUri;
	std::string uriError;

	if (!mr::lsp::pathToFileUri(workDirectory.string(), rootUri, uriError)) rootUri.clear();
	return std::string("{\"processId\":null,\"rootUri\":") + jsonString(rootUri) +
	       ",\"rootPath\":" + jsonString(workDirectory.string()) +
	       ",\"capabilities\":{\"textDocument\":{\"completion\":{\"completionItem\":{\"snippetSupport\":true,\"documentationFormat\":[\"markdown\",\"plaintext\"],\"resolveSupport\":{\"properties\":[\"documentation\",\"detail\",\"additionalTextEdits\"]}},\"contextSupport\":true},\"hover\":{\"contentFormat\":[\"markdown\",\"plaintext\"]},\"signatureHelp\":{\"signatureInformation\":{\"documentationFormat\":[\"markdown\",\"plaintext\"]}},\"definition\":{},\"references\":{},\"documentSymbol\":{}},\"workspace\":{\"symbol\":{}}}}";
}

bool startServer(LiveServerRun &run, std::string &errorMessage) {
	mr::lsp::LspInitializeSpec initializeSpec;

	if (!findExecutableOnPath(run.spec.executable, run.executablePath)) {
		errorMessage = "SKIP " + std::string(run.spec.name) + ": executable not found: " + run.spec.executable;
		return false;
	}
	run.workDirectory = makeWorkDirectory(run.spec.name);
	run.filePath = run.workDirectory / run.spec.fileName;
	std::filesystem::remove_all(run.workDirectory);
	std::filesystem::create_directories(run.workDirectory);
	if (!writeDocumentFile(run.filePath, run.spec.documentText, errorMessage)) return false;

	initializeSpec.session.process.executablePath = run.executablePath;
	if (run.spec.argument1 != nullptr) initializeSpec.session.process.arguments.push_back(run.spec.argument1);
	if (run.spec.argument2 != nullptr) initializeSpec.session.process.arguments.push_back(run.spec.argument2);
	initializeSpec.session.process.workingDirectory = run.workDirectory.string();
	initializeSpec.initializeParamsJson = initializeParams(run.workDirectory);
	if (!run.lifecycle.start(initializeSpec, errorMessage)) return false;
	if (!pollLifecycleUntilState(run.lifecycle, mr::lsp::LspLifecycleState::Initialized, 5000, errorMessage)) return false;
	if (!run.lifecycle.sendInitialized(errorMessage)) return false;
	errorMessage.clear();
	return true;
}

bool stopServer(LiveServerRun &run, std::string &errorMessage) {
	int exitStatus = -1;

	if (run.documentService != nullptr && run.documentService->isOpen()) static_cast<void>(run.documentService->close(errorMessage));
	if (run.lifecycle.state() == mr::lsp::LspLifecycleState::Initialized) {
		if (!run.lifecycle.shutdown(errorMessage)) return false;
		if (!pollLifecycleUntilState(run.lifecycle, mr::lsp::LspLifecycleState::Shutdown, 2000, errorMessage)) return false;
		if (!run.lifecycle.exit(errorMessage)) return false;
		static_cast<void>(run.lifecycle.wait(1000, exitStatus));
	} else {
		run.lifecycle.requestStop();
		static_cast<void>(run.lifecycle.wait(500, exitStatus));
	}
	std::filesystem::remove_all(run.workDirectory);
	errorMessage.clear();
	return true;
}

bool openDocument(LiveServerRun &run, mr::lsp::LspDocumentService &service, std::string &errorMessage) {
	mr::lsp::LspDocumentSourceSnapshot snapshot;

	snapshot.absolutePath = run.filePath.string();
	snapshot.languageId = run.spec.languageId;
	snapshot.version = 1;
	snapshot.text = run.spec.documentText;
	run.documentService = &service;
	if (!service.open(snapshot, errorMessage)) return false;
	::poll(nullptr, 0, 250);
	errorMessage.clear();
	return true;
}

bool pollCompletion(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::lsp::LspCompletionAdapter &adapter, mr::lsp::LspCompletionRequest &request, mr::lsp::LspCompletionResult &result, std::string &errorMessage) {
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int round = 0; round < 150; ++round) {
		if (!lifecycle.poll(messages, errorMessage)) return false;
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consume(message, service, request, result, accepted, errorMessage)) return false;
			if (accepted) return true;
		}
		::poll(nullptr, 0, 20);
	}
	errorMessage = "completion response timeout";
	return false;
}

bool pollCompletionResolve(mr::lsp::LspLifecycle &lifecycle, mr::lsp::LspCompletionAdapter &adapter, mr::lsp::LspCompletionResolveRequest &request, mr::lsp::LspCompletionResolveResult &result, std::string &errorMessage) {
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int round = 0; round < 100; ++round) {
		if (!lifecycle.poll(messages, errorMessage)) return false;
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consumeResolve(message, request, result, accepted, errorMessage)) return false;
			if (accepted) return true;
		}
		::poll(nullptr, 0, 20);
	}
	errorMessage = "completionItem/resolve response timeout";
	return false;
}

void resolveCompletionItems(LiveServerRun &run, mr::lsp::LspCompletionAdapter &adapter, mr::lsp::LspCompletionResult &result) {
	for (std::size_t index = 0; index < result.items.size(); ++index) {
		mr::lsp::LspCompletionResolveRequest request;
		mr::lsp::LspCompletionResolveResult resolved;
		std::string errorMessage;

		if (!adapter.requestResolve(run.lifecycle, result.items[index], request, errorMessage)) continue;
		if (!pollCompletionResolve(run.lifecycle, adapter, request, resolved, errorMessage)) continue;
		if (!resolved.item.label.empty()) result.items[index] = resolved.item;
	}
}

bool requestCompletion(LiveServerRun &run, mr::lsp::LspDocumentService &service, const CompletionCheck &check, mr::lsp::LspCompletionResult &result, std::string &errorMessage) {
	mr::lsp::LspCompletionAdapter adapter;
	mr::lsp::LspCompletionRequest request;
	mr::lsp::LspTextPosition position;

	if (!findNeedlePosition(run.spec.documentText, check.needle, position, errorMessage)) return false;
	if (!adapter.requestCompletion(run.lifecycle, service, position, check.trigger, request, errorMessage)) return false;
	if (!pollCompletion(run.lifecycle, service, adapter, request, result, errorMessage)) return false;
	resolveCompletionItems(run, adapter, result);
	return true;
}

bool completionHasExpectedItem(const mr::lsp::LspCompletionResult &result, const std::string &expected, bool requireDocumentation, bool requireSnippet) {
	for (std::size_t index = 0; index < result.items.size(); ++index) {
		const mr::lsp::LspCompletionItem &item = result.items[index];
		const bool labelMatch = item.label.find(expected) != std::string::npos;
		const bool detailMatch = item.detail.find(expected) != std::string::npos;
		const bool textMatch = item.insertText.find(expected) != std::string::npos || item.textEditNewText.find(expected) != std::string::npos;
		const bool hasDocumentation = !requireDocumentation || !item.documentation.empty();
		const bool hasSnippet = !requireSnippet || (item.hasInsertTextFormat && item.insertTextFormat == 2) || item.insertText.find("${") != std::string::npos || item.textEditNewText.find("${") != std::string::npos;

		if ((labelMatch || detailMatch || textMatch) && hasDocumentation && hasSnippet) return true;
	}
	return false;
}

std::string firstCompletionSummary(const mr::lsp::LspCompletionResult &result) {
	std::ostringstream out;
	const std::size_t limit = result.items.size() < 5 ? result.items.size() : 5;

	for (std::size_t index = 0; index < limit; ++index) {
		if (index > 0) out << ", ";
		out << result.items[index].label;
		if (!result.items[index].detail.empty()) out << " [" << result.items[index].detail << "]";
		if (!result.items[index].documentation.empty()) out << " {doc}";
		if ((result.items[index].hasInsertTextFormat && result.items[index].insertTextFormat == 2) || result.items[index].insertText.find("${") != std::string::npos || result.items[index].textEditNewText.find("${") != std::string::npos)
			out << " {snippet}";
	}
	return out.str();
}

bool pollDocumentSymbols(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::lsp::LspDocumentSymbolsAdapter &adapter, mr::lsp::LspDocumentSymbolsRequest &request, mr::lsp::LspDocumentSymbolsResult &result, std::string &errorMessage) {
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int round = 0; round < 100; ++round) {
		if (!lifecycle.poll(messages, errorMessage)) return false;
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consume(message, service, request, result, accepted, errorMessage)) return false;
			if (accepted) return true;
		}
		::poll(nullptr, 0, 20);
	}
	errorMessage = "documentSymbol response timeout";
	return false;
}

std::size_t documentSymbolCount(LiveServerRun &run, mr::lsp::LspDocumentService &service, std::string &errorMessage) {
	mr::lsp::LspDocumentSymbolsAdapter adapter;
	mr::lsp::LspDocumentSymbolsRequest request;
	mr::lsp::LspDocumentSymbolsResult result;

	if (!adapter.requestDocumentSymbols(run.lifecycle, service, request, errorMessage)) return 0;
	if (!pollDocumentSymbols(run.lifecycle, service, adapter, request, result, errorMessage)) return 0;
	errorMessage.clear();
	return result.symbols.size();
}

bool pollHover(mr::lsp::LspLifecycle &lifecycle, const mr::lsp::LspDocumentService &service, mr::lsp::LspHoverAdapter &adapter, mr::lsp::LspHoverRequest &request, mr::lsp::LspHoverResult &result, std::string &errorMessage) {
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int round = 0; round < 100; ++round) {
		if (!lifecycle.poll(messages, errorMessage)) return false;
		for (const mr::lsp::LspInboundMessage &message : messages) {
			bool accepted = false;
			if (!adapter.consume(message, service, request, result, accepted, errorMessage)) return false;
			if (accepted) return true;
		}
		::poll(nullptr, 0, 20);
	}
	errorMessage = "hover response timeout";
	return false;
}

std::size_t hoverTextSize(LiveServerRun &run, mr::lsp::LspDocumentService &service, std::string &errorMessage) {
	mr::lsp::LspHoverAdapter adapter;
	mr::lsp::LspHoverRequest request;
	mr::lsp::LspHoverResult result;
	mr::lsp::LspTextPosition position;

	if (!findNeedlePosition(run.spec.documentText, run.spec.completionNeedle, position, errorMessage)) return 0;
	if (!adapter.requestHover(run.lifecycle, service, position, request, errorMessage)) return 0;
	if (!pollHover(run.lifecycle, service, adapter, request, result, errorMessage)) return 0;
	errorMessage.clear();
	return result.value.size();
}

bool runCompletionCheck(LiveServerRun &run, mr::lsp::LspDocumentService &service, const CompletionCheck &check, bool hardFail, std::string &failureReason) {
	mr::lsp::LspCompletionResult result;
	std::string errorMessage;

	if (!requestCompletion(run, service, check, result, errorMessage)) {
		if (hardFail) {
			failureReason = std::string(run.spec.name) + " " + check.label + ": " + errorMessage;
			return false;
		}
		std::cout << "WARN " << run.spec.name << " " << check.label << ": " << errorMessage << "\n";
		return true;
	}
	std::cout << "INFO " << run.spec.name << " " << check.label << ": items=" << result.items.size() << " [" << firstCompletionSummary(result) << "]\n";
	if (check.requireExpectedCompletion && !completionHasExpectedItem(result, check.expectedCompletion, check.requireDocumentation, check.requireSnippet)) {
		if (hardFail) {
			failureReason = std::string(run.spec.name) + " " + check.label + ": expected " + check.expectedCompletion + " not found";
			return false;
		}
		std::cout << "WARN " << run.spec.name << " expected " << check.expectedCompletion << " not found\n";
	}
	return true;
}

bool runServerProbe(const LiveServerSpec &spec, int &skipped, std::string &failureReason) {
	LiveServerRun run{ spec };
	mr::lsp::LspDocumentService service(run.lifecycle);
	std::string errorMessage;
	const bool hardFail = std::string(spec.name) == "digestif";

	if (!startServer(run, errorMessage)) {
		if (errorMessage.starts_with("SKIP ")) {
			++skipped;
			std::cout << errorMessage << "\n";
			return true;
		}
		if (hardFail) {
			failureReason = std::string(spec.name) + " start: " + errorMessage;
			return false;
		}
		std::cout << "WARN " << spec.name << " start failed: " << errorMessage << "\n";
		return true;
	}
	if (!openDocument(run, service, errorMessage)) {
		static_cast<void>(stopServer(run, errorMessage));
		failureReason = std::string(spec.name) + " open: " + errorMessage;
		return !hardFail;
	}
	if (std::string(spec.name) == "digestif") {
		const std::size_t checkCount = sizeof(kDigestifChecks) / sizeof(kDigestifChecks[0]);

		for (std::size_t index = 0; index < checkCount; ++index) {
			if (!runCompletionCheck(run, service, kDigestifChecks[index], true, failureReason)) {
				static_cast<void>(stopServer(run, errorMessage));
				return false;
			}
		}
	} else {
		CompletionCheck check{ "smoke completion", spec.completionNeedle, spec.completionTrigger, spec.expectedCompletion, spec.requireExpectedCompletion, spec.requireDocumentation, spec.requireSnippet };
		if (!runCompletionCheck(run, service, check, false, failureReason)) {
			static_cast<void>(stopServer(run, errorMessage));
			return false;
		}
	}
	{
		const std::size_t symbols = documentSymbolCount(run, service, errorMessage);
		if (!errorMessage.empty())
			std::cout << "WARN " << spec.name << " documentSymbol: " << errorMessage << "\n";
		else
			std::cout << "INFO " << spec.name << " documentSymbol: items=" << symbols << "\n";
	}
	{
		const std::size_t hoverSize = hoverTextSize(run, service, errorMessage);
		if (!errorMessage.empty())
			std::cout << "WARN " << spec.name << " hover: " << errorMessage << "\n";
		else
			std::cout << "INFO " << spec.name << " hover: bytes=" << hoverSize << "\n";
	}
	if (!stopServer(run, errorMessage)) {
		failureReason = std::string(spec.name) + " stop: " + errorMessage;
		return false;
	}
	return true;
}

bool runProbe(std::string &failureReason) {
	const std::size_t serverCount = sizeof(kServerSpecs) / sizeof(kServerSpecs[0]);
	int skipped = 0;

	for (std::size_t index = 0; index < serverCount; ++index) {
		if (!runServerProbe(kServerSpecs[index], skipped, failureReason)) return false;
	}
	std::cout << "mr_lsp_live_matrix_probe completed, skipped=" << skipped << "\n";
	return true;
}

} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_live_matrix_probe failed: " << failureReason << "\n";
		return 1;
	}
	return 0;
}
