#include "MRLspServerProfile.hpp"

#include <cstdlib>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

std::string trimAsciiLocal(const std::string &text) {
	std::size_t first = 0;
	std::size_t last = text.size();

	while (first < last && static_cast<unsigned char>(text[first]) <= ' ')
		++first;
	while (last > first && static_cast<unsigned char>(text[last - 1]) <= ' ')
		--last;
	return text.substr(first, last - first);
}

struct LspServerStartEntry {
	MRSyntaxLanguage language;
	const char *profileName;
	const char *executableName;
	const char *argument1;
	const char *argument2;
};

const LspServerStartEntry lspServerStartTable[] = {
	{ MRSyntaxLanguage::C, "builtin-c-clangd", "clangd", nullptr, nullptr },
	{ MRSyntaxLanguage::Cpp, "builtin-cpp-clangd", "clangd", nullptr, nullptr },
	{ MRSyntaxLanguage::JavaScript, "builtin-javascript-typescript-language-server", "typescript-language-server", "--stdio", nullptr },
	{ MRSyntaxLanguage::Python, "builtin-python-pylsp", "pylsp", nullptr, nullptr },
	{ MRSyntaxLanguage::Json, "builtin-json-vscode-json-language-server", "vscode-json-language-server", "--stdio", nullptr },
	{ MRSyntaxLanguage::Yaml, "builtin-yaml-language-server", "yaml-language-server", "--stdio", nullptr },
	{ MRSyntaxLanguage::Xml, "builtin-xml-lemminx", "lemminx", nullptr, nullptr },
	{ MRSyntaxLanguage::Bash, "builtin-bash-language-server", "bash-language-server", "start", nullptr },
	{ MRSyntaxLanguage::Zsh, "builtin-zsh-bash-language-server", "bash-language-server", "start", nullptr },
	{ MRSyntaxLanguage::Fish, "builtin-fish-bash-language-server", "bash-language-server", "start", nullptr },
	{ MRSyntaxLanguage::Swift, "builtin-swift-sourcekit-lsp", "sourcekit-lsp", nullptr, nullptr },
	{ MRSyntaxLanguage::Rust, "builtin-rust-rust-analyzer", "rust-analyzer", nullptr, nullptr },
	{ MRSyntaxLanguage::Go, "builtin-go-gopls", "gopls", nullptr, nullptr },
	{ MRSyntaxLanguage::Kotlin, "builtin-kotlin-language-server", "kotlin-language-server", nullptr, nullptr },
	{ MRSyntaxLanguage::CSharp, "builtin-csharp-csharp-ls", "csharp-ls", nullptr, nullptr },
	{ MRSyntaxLanguage::Markdown, "builtin-markdown-marksman", "marksman", nullptr, nullptr },
};

bool isExecutableFile(const std::string &path) {
	return !path.empty() && ::access(path.c_str(), X_OK) == 0;
}

bool executableNameHasPath(const std::string &name) {
	return name.find('/') != std::string::npos;
}

bool findExecutableOnPath(const std::string &executableName, std::string &resolvedPath) {
	const char *pathEnvironment = std::getenv("PATH");
	std::string pathText = pathEnvironment != nullptr ? pathEnvironment : "";
	std::size_t start = 0;

	resolvedPath.clear();
	if (executableName.empty()) return false;
	if (executableNameHasPath(executableName)) {
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

void appendLspServerStartArguments(mr::services::MRLspServerProfile &profile, const LspServerStartEntry &entry) {
	if (entry.argument1 != nullptr && entry.argument1[0] != '\0') profile.arguments.push_back(entry.argument1);
	if (entry.argument2 != nullptr && entry.argument2[0] != '\0') profile.arguments.push_back(entry.argument2);
}

std::string candidateExecutableListForLanguage(MRSyntaxLanguage language) {
	const std::size_t entryCount = sizeof(lspServerStartTable) / sizeof(lspServerStartTable[0]);
	std::string text;

	for (std::size_t index = 0; index < entryCount; ++index) {
		if (lspServerStartTable[index].language != language) continue;
		if (!text.empty()) text += ", ";
		text += lspServerStartTable[index].executableName;
	}
	return text;
}

} // namespace

namespace mr::services {

bool buildLspServerProfileFromEnvironment(MRLspServerProfile &profile) {
	const char *server = std::getenv("MR_LSP_SERVER");
	const char *arguments = std::getenv("MR_LSP_SERVER_ARGS");
	std::istringstream argumentStream(arguments != nullptr ? arguments : "");
	std::string argument;
	std::string serverText = server != nullptr ? server : "";

	profile = MRLspServerProfile();
	serverText = trimAsciiLocal(serverText);
	if (serverText.empty()) return false;
	profile.profileName = "environment";
	profile.executablePath = serverText;
	profile.workingDirectory = ".";
	while (argumentStream >> argument)
		profile.arguments.push_back(argument);
	return true;
}

bool buildLspServerProfileForLanguage(MRSyntaxLanguage language, const std::string &languageName, MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage) {
	const std::size_t entryCount = sizeof(lspServerStartTable) / sizeof(lspServerStartTable[0]);
	std::string executablePath;
	std::string candidates;

	profile = MRLspServerProfile();
	configurationSource.clear();
	errorMessage.clear();

	if (buildLspServerProfileFromEnvironment(profile)) {
		configurationSource = "MR_LSP_SERVER";
		return true;
	}

	for (std::size_t index = 0; index < entryCount; ++index) {
		const LspServerStartEntry &entry = lspServerStartTable[index];

		if (entry.language != language) continue;
		if (!findExecutableOnPath(entry.executableName, executablePath)) continue;
		profile.profileName = entry.profileName;
		profile.executablePath = executablePath;
		appendLspServerStartArguments(profile, entry);
		profile.workingDirectory.clear();
		configurationSource = std::string("built-in language mapping: ") + languageName;
		return true;
	}

	candidates = candidateExecutableListForLanguage(language);
	if (candidates.empty()) {
		errorMessage = "No built-in LSP server for language " + languageName + ". Set MR_LSP_SERVER.";
	} else {
		errorMessage = "Built-in LSP server for language " + languageName + " is not executable or not in PATH: " + candidates;
	}
	return false;
}

std::string lspServerProfileArgumentText(const MRLspServerProfile &profile) {
	std::ostringstream argumentText;

	for (std::size_t i = 0; i < profile.arguments.size(); ++i) {
		if (i != 0) argumentText << " ";
		argumentText << profile.arguments[i];
	}
	return argumentText.str();
}

} // namespace mr::services
