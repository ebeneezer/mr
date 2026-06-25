#include "../app/services/MRLspServerProfile.hpp"
#include "../config/settings/MRSettingsRuntime.hpp"

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

struct EnvironmentSnapshot {
	std::string path;
	std::string server;
	std::string args;
	bool hasPath = false;
	bool hasServer = false;
	bool hasArgs = false;
};

void captureEnvironment(EnvironmentSnapshot &snapshot) {
	const char *value = std::getenv("PATH");
	snapshot.hasPath = value != nullptr;
	snapshot.path = value != nullptr ? value : "";
	value = std::getenv("MR_LSP_SERVER");
	snapshot.hasServer = value != nullptr;
	snapshot.server = value != nullptr ? value : "";
	value = std::getenv("MR_LSP_SERVER_ARGS");
	snapshot.hasArgs = value != nullptr;
	snapshot.args = value != nullptr ? value : "";
}

void restoreEnvironment(const EnvironmentSnapshot &snapshot) {
	if (snapshot.hasPath) ::setenv("PATH", snapshot.path.c_str(), 1);
	else
		::unsetenv("PATH");
	if (snapshot.hasServer) ::setenv("MR_LSP_SERVER", snapshot.server.c_str(), 1);
	else
		::unsetenv("MR_LSP_SERVER");
	if (snapshot.hasArgs) ::setenv("MR_LSP_SERVER_ARGS", snapshot.args.c_str(), 1);
	else
		::unsetenv("MR_LSP_SERVER_ARGS");
}

bool createExecutable(const std::string &path, std::string &failureReason) {
	const char payload[] = "#!/bin/sh\nexit 0\n";
	const int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0755);

	if (fd < 0) {
		failureReason = "unable to create fake executable: " + path;
		return false;
	}
	if (::write(fd, payload, static_cast<unsigned int>(std::strlen(payload))) < 0) {
		::close(fd);
		failureReason = "unable to write fake executable: " + path;
		return false;
	}
	::close(fd);
	if (::chmod(path.c_str(), 0755) != 0) {
		failureReason = "unable to chmod fake executable: " + path;
		return false;
	}
	return true;
}

bool createFakeServerDirectory(std::string &directory, std::string &failureReason) {
	char pattern[] = "/tmp/mr-lsp-server-profile-XXXXXX";
	char *created = ::mkdtemp(pattern);

	if (created == nullptr) {
		failureReason = "unable to create fake server directory";
		return false;
	}
	directory = created;
	if (!createExecutable(directory + "/clangd", failureReason)) return false;
	if (!createExecutable(directory + "/pylsp", failureReason)) return false;
	if (!createExecutable(directory + "/typescript-language-server", failureReason)) return false;
	if (!createExecutable(directory + "/texlab", failureReason)) return false;
	if (!createExecutable(directory + "/custom-lsp", failureReason)) return false;
	return true;
}

bool testEnvironmentOverride(std::string &failureReason) {
	mr::services::MRLspServerProfile profile;

	::setenv("MR_LSP_SERVER", "/tmp/mr-custom-lsp", 1);
	::setenv("MR_LSP_SERVER_ARGS", "--stdio --probe", 1);
	if (!expect(mr::services::buildLspServerProfileFromEnvironment(profile), "environment profile missing", failureReason)) return false;
	if (!expect(profile.profileName == "environment", "environment profile name", failureReason)) return false;
	if (!expect(profile.executablePath == "/tmp/mr-custom-lsp", "environment executable", failureReason)) return false;
	if (!expect(profile.workingDirectory == ".", "environment working directory", failureReason)) return false;
	if (!expect(profile.arguments.size() == 2, "environment argument count", failureReason)) return false;
	if (!expect(profile.arguments[0] == "--stdio" && profile.arguments[1] == "--probe", "environment arguments", failureReason)) return false;
	return true;
}

bool testBuiltInMapping(std::string &failureReason) {
	mr::services::MRLspServerProfile profile;
	std::string source;
	std::string errorMessage;

	::unsetenv("MR_LSP_SERVER");
	::unsetenv("MR_LSP_SERVER_ARGS");
	if (!expect(mr::services::lspServerExecutableCandidatesForLanguage(MRSyntaxLanguage::C) == "clangd", "C candidate list", failureReason)) return false;
	if (!expect(mr::services::buildLspServerProfileForLanguage(MRSyntaxLanguage::C, "C", profile, source, errorMessage), "C profile missing: " + errorMessage, failureReason)) return false;
	if (!expect(profile.profileName == "builtin-c-clangd", "C profile name", failureReason)) return false;
	if (!expect(profile.executablePath.find("/clangd") != std::string::npos, "C executable", failureReason)) return false;
	if (!expect(profile.arguments.empty(), "C arguments", failureReason)) return false;
	if (!expect(source == "built-in language mapping: C", "C source", failureReason)) return false;

	if (!expect(mr::services::buildLspServerProfileForLanguage(MRSyntaxLanguage::Python, "Python", profile, source, errorMessage), "Python profile missing: " + errorMessage, failureReason)) return false;
	if (!expect(profile.profileName == "builtin-python-pylsp", "Python profile name", failureReason)) return false;
	if (!expect(profile.executablePath.find("/pylsp") != std::string::npos, "Python executable", failureReason)) return false;

	if (!expect(mr::services::lspServerExecutableCandidatesForLanguage(MRSyntaxLanguage::JavaScript) == "typescript-language-server", "JavaScript candidate list", failureReason)) return false;
	if (!expect(mr::services::buildLspServerProfileForLanguage(MRSyntaxLanguage::JavaScript, "JavaScript", profile, source, errorMessage), "JavaScript profile missing: " + errorMessage, failureReason)) return false;
	if (!expect(profile.profileName == "builtin-javascript-typescript-language-server", "JavaScript profile name", failureReason)) return false;
	if (!expect(profile.executablePath.find("/typescript-language-server") != std::string::npos, "JavaScript executable", failureReason)) return false;
	if (!expect(profile.arguments.size() == 1 && profile.arguments[0] == "--stdio", "JavaScript arguments", failureReason)) return false;
	if (!expect(mr::services::lspServerProfileArgumentText(profile) == "--stdio", "argument text", failureReason)) return false;
	if (!expect(mr::services::lspServerExecutableCandidatesForLanguage(MRSyntaxLanguage::Latex) == "digestif, texlab", "LaTeX candidate list", failureReason)) return false;
	if (!expect(mr::services::buildLspServerProfileForLanguage(MRSyntaxLanguage::Latex, "LaTeX", profile, source, errorMessage), "LaTeX profile missing: " + errorMessage, failureReason)) return false;
	if (!expect(profile.profileName == "builtin-latex-texlab", "LaTeX profile name", failureReason)) return false;
	if (!expect(profile.executablePath.find("/texlab") != std::string::npos, "LaTeX executable", failureReason)) return false;
	if (!expect(profile.arguments.size() == 1 && profile.arguments[0] == "run", "LaTeX arguments", failureReason)) return false;
	if (!expect(profile.lspMiddlewarePath.empty(), "LaTeX middleware must not be auto-filled while MRExpand is reset", failureReason)) return false;
	return true;
}

bool testCompilerProfileOverride(const std::string &fakeDirectory, std::string &failureReason) {
	MRCompilerProfile compilerProfile;
	mr::services::MRLspServerProfile profile;
	std::string source;
	std::string errorMessage;

	compilerProfile.id = "LSP_OVERRIDE";
	compilerProfile.name = "LSP override";
	compilerProfile.toolchain = "CUSTOM";
	compilerProfile.lspExecutablePath = fakeDirectory + "/custom-lsp";
	compilerProfile.lspArguments = "--stdio --custom";
	compilerProfile.lspWorkingDirectory = "/tmp";
	const std::string environmentServer = fakeDirectory + "/clangd";

	::setenv("MR_LSP_SERVER", environmentServer.c_str(), 1);
	::setenv("MR_LSP_SERVER_ARGS", "--env", 1);
	if (!expect(mr::services::buildLspServerProfileForLanguageWithCompilerProfile(MRSyntaxLanguage::Cpp, "C++", compilerProfile, profile, source, errorMessage), "compiler profile LSP missing: " + errorMessage, failureReason)) return false;
	if (!expect(profile.profileName == "compiler-profile-LSP_OVERRIDE", "compiler profile source name", failureReason)) return false;
	if (!expect(profile.executablePath.find("/custom-lsp") != std::string::npos, "compiler profile executable", failureReason)) return false;
	if (!expect(profile.arguments.size() == 2 && profile.arguments[0] == "--stdio" && profile.arguments[1] == "--custom", "compiler profile arguments", failureReason)) return false;
	if (!expect(profile.workingDirectory == "/tmp", "compiler profile working directory", failureReason)) return false;
	if (!expect(source == "compiler profile: LSP_OVERRIDE", "compiler profile source", failureReason)) return false;
	compilerProfile.lspExecutablePath = fakeDirectory + "/texlab";
	compilerProfile.lspArguments = "run";
	compilerProfile.lspWorkingDirectory.clear();
	compilerProfile.lspMiddlewarePath.clear();
	if (!expect(mr::services::buildLspServerProfileForLanguageWithCompilerProfile(MRSyntaxLanguage::Latex, "LaTeX", compilerProfile, profile, source, errorMessage), "compiler profile LaTeX LSP missing: " + errorMessage, failureReason)) return false;
	if (!expect(profile.lspMiddlewarePath.empty(), "compiler profile LaTeX default middleware must stay empty while MRExpand is reset", failureReason)) return false;
	return true;
}

bool testFailurePaths(std::string &failureReason) {
	mr::services::MRLspServerProfile profile;
	std::string source;
	std::string errorMessage;

	::unsetenv("MR_LSP_SERVER");
	::setenv("PATH", "/tmp/mr-lsp-server-profile-empty", 1);
	if (!expect(mr::services::lspServerExecutableCandidatesForLanguage(MRSyntaxLanguage::PlainText).empty(), "PlainText candidate list", failureReason)) return false;
	if (!expect(!mr::services::buildLspServerProfileForLanguage(MRSyntaxLanguage::PlainText, "Plain Text", profile, source, errorMessage), "PlainText profile accepted", failureReason)) return false;
	if (!expect(errorMessage.find("No built-in LSP server") != std::string::npos, "PlainText error text", failureReason)) return false;
	if (!expect(!mr::services::buildLspServerProfileForLanguage(MRSyntaxLanguage::C, "C", profile, source, errorMessage), "missing clangd accepted", failureReason)) return false;
	if (!expect(errorMessage.find("clangd") != std::string::npos, "missing clangd error text", failureReason)) return false;
	return true;
}

bool runProbe(std::string &failureReason) {
	EnvironmentSnapshot snapshot;
	std::string fakeDirectory;
	bool ok = false;

	captureEnvironment(snapshot);
	if (!createFakeServerDirectory(fakeDirectory, failureReason)) {
		restoreEnvironment(snapshot);
		return false;
	}
	::setenv("PATH", fakeDirectory.c_str(), 1);
	ok = testEnvironmentOverride(failureReason) && testBuiltInMapping(failureReason) && testCompilerProfileOverride(fakeDirectory, failureReason) && testFailurePaths(failureReason);
	restoreEnvironment(snapshot);
	return ok;
}

} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_server_profile_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_server_profile_probe passed\n";
	return 0;
}
