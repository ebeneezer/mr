#include "MRLspServerProfile.hpp"

#include "../../config/settings/MRSettingsRuntime.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
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
	const char *middlewarePath;
};

const LspServerStartEntry lspServerStartTable[] = {
	{ MRSyntaxLanguage::C, "builtin-c-clangd", "clangd", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Cpp, "builtin-cpp-clangd", "clangd", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::JavaScript, "builtin-javascript-typescript-language-server", "typescript-language-server", "--stdio", nullptr, nullptr },
	{ MRSyntaxLanguage::Python, "builtin-python-pyright", "pyright-langserver", "--stdio", nullptr, nullptr },
	{ MRSyntaxLanguage::Python, "builtin-python-basedpyright", "basedpyright-langserver", "--stdio", nullptr, nullptr },
	{ MRSyntaxLanguage::Python, "builtin-python-pylsp", "pylsp", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Json, "builtin-json-vscode-json-language-server", "vscode-json-language-server", "--stdio", nullptr, nullptr },
	{ MRSyntaxLanguage::Yaml, "builtin-yaml-language-server", "yaml-language-server", "--stdio", nullptr, nullptr },
	{ MRSyntaxLanguage::Xml, "builtin-xml-lemminx", "lemminx", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Bash, "builtin-bash-language-server", "bash-language-server", "start", nullptr, nullptr },
	{ MRSyntaxLanguage::Zsh, "builtin-zsh-bash-language-server", "bash-language-server", "start", nullptr, nullptr },
	{ MRSyntaxLanguage::Fish, "builtin-fish-fish-lsp", "fish-lsp", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Swift, "builtin-swift-sourcekit-lsp", "sourcekit-lsp", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Rust, "builtin-rust-rust-analyzer", "rust-analyzer", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Go, "builtin-go-gopls", "gopls", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Perl, "builtin-perl-perlnavigator", "perlnavigator", "--stdio", nullptr, nullptr },
	{ MRSyntaxLanguage::Kotlin, "builtin-kotlin-lsp", "kotlin-lsp", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Kotlin, "builtin-kotlin-language-server", "kotlin-language-server", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::CSharp, "builtin-csharp-csharp-ls", "csharp-ls", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Pascal, "builtin-pascal-pasls", "pasls", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Systemd, "builtin-systemd-systemd-lsp", "systemd-lsp", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Make, "builtin-make-make-ls", "make-ls", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Make, "builtin-make-autotools-language-server", "autotools-language-server", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Markdown, "builtin-markdown-marksman", "marksman", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Latex, "builtin-latex-digestif", "digestif", nullptr, nullptr, nullptr },
	{ MRSyntaxLanguage::Latex, "builtin-latex-texlab", "texlab", "run", nullptr, nullptr },
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

std::string executableDirectory() {
	std::array<char, 4096> path{};
	const ssize_t len = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
	std::size_t pos;

	if (len <= 0) return std::string();
	path[static_cast<std::size_t>(len)] = '\0';
	pos = std::string_view(path.data()).find_last_of('/');
	if (pos == std::string_view::npos) return std::string();
	return std::string(path.data(), pos);
}

void appendLspServerStartArguments(mr::services::MRLspServerProfile &profile, const LspServerStartEntry &entry) {
	if (entry.argument1 != nullptr && entry.argument1[0] != '\0') profile.arguments.push_back(entry.argument1);
	if (entry.argument2 != nullptr && entry.argument2[0] != '\0') profile.arguments.push_back(entry.argument2);
}

std::string usableMiddlewarePath(const char *path) {
	std::filesystem::path candidate;
	std::error_code errorCode;
	std::string baseDirectory;

	if (path == nullptr || path[0] == '\0') return std::string();
	candidate = std::filesystem::path(path);
	if (std::filesystem::exists(candidate)) return std::filesystem::absolute(candidate, errorCode).string();
	if (candidate.is_relative()) {
		baseDirectory = executableDirectory();
		if (!baseDirectory.empty()) {
			candidate = std::filesystem::path(baseDirectory) / path;
			if (std::filesystem::exists(candidate)) return std::filesystem::absolute(candidate, errorCode).string();
		}
	}
	return std::string();
}

std::string candidateExecutableListForLanguage(MRSyntaxLanguage language);
std::string defaultMiddlewarePathForLanguage(MRSyntaxLanguage language);

std::vector<std::string> lspArgumentsForEntry(const LspServerStartEntry &entry) {
	std::vector<std::string> arguments;

	if (entry.argument1 != nullptr && entry.argument1[0] != '\0') arguments.push_back(entry.argument1);
	if (entry.argument2 != nullptr && entry.argument2[0] != '\0') arguments.push_back(entry.argument2);
	return arguments;
}

std::vector<std::string> splitArgumentText(const std::string &arguments) {
	std::istringstream stream(arguments);
	std::vector<std::string> out;
	std::string argument;

	while (stream >> argument)
		out.push_back(argument);
	return out;
}

bool buildBuiltInLspServerProfileForLanguage(MRSyntaxLanguage language, const std::string &languageName, mr::services::MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage) {
	const std::size_t entryCount = sizeof(lspServerStartTable) / sizeof(lspServerStartTable[0]);
	std::string executablePath;
	std::string candidates;

	for (std::size_t index = 0; index < entryCount; ++index) {
		const LspServerStartEntry &entry = lspServerStartTable[index];

		if (entry.language != language) continue;
		if (!findExecutableOnPath(entry.executableName, executablePath)) continue;
		profile.profileName = entry.profileName;
		profile.executablePath = executablePath;
		appendLspServerStartArguments(profile, entry);
		profile.workingDirectory.clear();
		profile.lspMiddlewarePath = usableMiddlewarePath(entry.middlewarePath);
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

std::string defaultMiddlewarePathForLanguage(MRSyntaxLanguage language) {
	const std::size_t entryCount = sizeof(lspServerStartTable) / sizeof(lspServerStartTable[0]);

	for (std::size_t index = 0; index < entryCount; ++index) {
		if (lspServerStartTable[index].language != language) continue;
		std::string path = usableMiddlewarePath(lspServerStartTable[index].middlewarePath);
		if (!path.empty()) return path;
	}
	return std::string();
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

bool buildLspServerProfileFromCompilerProfile(const MRCompilerProfile &compilerProfile, MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage) {
	std::string executablePath;

	profile = MRLspServerProfile();
	configurationSource.clear();
	errorMessage.clear();

	if (trimAsciiLocal(compilerProfile.lspExecutablePath).empty()) return false;
	if (!findExecutableOnPath(compilerProfile.lspExecutablePath, executablePath)) {
		errorMessage = "Configured LSP server is not executable or not in PATH: " + compilerProfile.lspExecutablePath;
		return false;
	}
	profile.profileName = compilerProfile.id.empty() ? "compiler-profile" : "compiler-profile-" + compilerProfile.id;
	profile.executablePath = executablePath;
	profile.arguments = splitArgumentText(compilerProfile.lspArguments);
	profile.workingDirectory = compilerProfile.lspWorkingDirectory;
	profile.lspMiddlewarePath = usableMiddlewarePath(compilerProfile.lspMiddlewarePath.c_str());
	if (profile.lspMiddlewarePath.empty()) profile.lspMiddlewarePath = compilerProfile.lspMiddlewarePath;
	configurationSource = compilerProfile.id.empty() ? "compiler profile" : "compiler profile: " + compilerProfile.id;
	return true;
}

bool buildLspServerProfileForLanguage(MRSyntaxLanguage language, const std::string &languageName, MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage) {
	profile = MRLspServerProfile();
	configurationSource.clear();
	errorMessage.clear();

	if (buildLspServerProfileFromEnvironment(profile)) {
		configurationSource = "MR_LSP_SERVER";
		return true;
	}
	return buildBuiltInLspServerProfileForLanguage(language, languageName, profile, configurationSource, errorMessage);
}

bool buildLspServerProfileForLanguageWithCompilerProfile(MRSyntaxLanguage language, const std::string &languageName, const MRCompilerProfile &compilerProfile, MRLspServerProfile &profile, std::string &configurationSource, std::string &errorMessage) {
	if (buildLspServerProfileFromCompilerProfile(compilerProfile, profile, configurationSource, errorMessage)) {
		if (profile.lspMiddlewarePath.empty()) profile.lspMiddlewarePath = defaultMiddlewarePathForLanguage(language);
		return true;
	}
	if (!errorMessage.empty()) return false;
	if (!buildLspServerProfileForLanguage(language, languageName, profile, configurationSource, errorMessage)) return false;
	if (!compilerProfile.lspMiddlewarePath.empty()) {
		const std::string resolvedMiddlewarePath = usableMiddlewarePath(compilerProfile.lspMiddlewarePath.c_str());

		profile.lspMiddlewarePath = resolvedMiddlewarePath.empty() ? compilerProfile.lspMiddlewarePath : resolvedMiddlewarePath;
	}
	return true;
}

bool resolveLspServerCandidate(const MRLspServerCandidate &candidate, MRLspServerProfile &profile) {
	std::string executablePath;

	profile = MRLspServerProfile();
	if (!findExecutableOnPath(candidate.executableName, executablePath)) return false;
	profile.profileName = candidate.profileName;
	profile.executablePath = executablePath;
	profile.arguments = candidate.arguments;
	profile.workingDirectory.clear();
	profile.lspMiddlewarePath = candidate.middlewarePath;
	return true;
}

std::vector<MRLspServerCandidate> lspServerCandidatesForLanguage(MRSyntaxLanguage language) {
	const std::size_t entryCount = sizeof(lspServerStartTable) / sizeof(lspServerStartTable[0]);
	std::vector<MRLspServerCandidate> candidates;

	for (std::size_t index = 0; index < entryCount; ++index) {
		const LspServerStartEntry &entry = lspServerStartTable[index];
		MRLspServerCandidate candidate;

		if (entry.language != language) continue;
		candidate.language = entry.language;
		candidate.profileName = entry.profileName;
		candidate.executableName = entry.executableName;
		candidate.arguments = lspArgumentsForEntry(entry);
		candidate.middlewarePath = usableMiddlewarePath(entry.middlewarePath);
		candidates.push_back(candidate);
	}
	return candidates;
}

std::vector<MRLspServerCandidate> availableLspServerCandidatesForLanguage(MRSyntaxLanguage language) {
	std::vector<MRLspServerCandidate> candidates = lspServerCandidatesForLanguage(language);
	std::vector<MRLspServerCandidate> available;

	for (const MRLspServerCandidate &candidate : candidates) {
		std::string executablePath;

		if (findExecutableOnPath(candidate.executableName, executablePath)) available.push_back(candidate);
	}
	return available;
}

std::string lspServerExecutableCandidatesForLanguage(MRSyntaxLanguage language) {
	return candidateExecutableListForLanguage(language);
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
