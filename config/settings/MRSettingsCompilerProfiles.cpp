#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsCompilerProfiles.hpp"
#include "MRSettingsRuntimeState.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

struct CompilerProbe {
	std::string toolchain;
	std::string executablePath;
	std::string versionText;
	std::string targetTriple;
	std::vector<std::string> includePaths;
	std::vector<std::string> libraryPaths;
	std::vector<std::string> runtimePaths;
};

std::vector<std::string> commandLines(const std::string &command) {
	std::array<char, 2048> buffer{};
	std::vector<std::string> lines;
	std::string line;
	std::string shellPath = configuredShellExecutablePath();
	int pipeFds[2] = {-1, -1};
	pid_t childPid = -1;
	int waitStatus = 0;

	if (command.empty() || shellPath.empty()) return lines;
	if (::pipe(pipeFds) != 0) return lines;
	childPid = ::fork();
	if (childPid < 0) {
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		return lines;
	}
	if (childPid == 0) {
		::dup2(pipeFds[1], STDOUT_FILENO);
		::close(pipeFds[0]);
		::close(pipeFds[1]);
		::execl(shellPath.c_str(), shellPath.c_str(), "-lc", command.c_str(), static_cast<char *>(nullptr));
		::_exit(127);
	}
	::close(pipeFds[1]);
	for (;;) {
		const ssize_t count = ::read(pipeFds[0], buffer.data(), buffer.size());

		if (count > 0) {
			for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index) {
				if (buffer[index] == '\n') {
					while (!line.empty() && line.back() == '\r')
						line.pop_back();
					lines.push_back(line);
					line.clear();
				} else
					line.push_back(buffer[index]);
			}
			continue;
		}
		if (count == 0) break;
		if (errno == EINTR) continue;
		break;
	}
	::close(pipeFds[0]);
	while (::waitpid(childPid, &waitStatus, 0) < 0 && errno == EINTR)
		;
	if (!line.empty()) {
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
			line.pop_back();
		lines.push_back(line);
	}
	return lines;
}

std::string commandFirstLine(const std::string &command) {
	std::vector<std::string> lines = commandLines(command);

	return lines.empty() ? std::string() : lines.front();
}

std::string commandText(const std::string &command) {
	std::vector<std::string> lines = commandLines(command);
	std::string out;

	for (const std::string &line : lines) {
		if (!out.empty()) out += '\n';
		out += line;
	}
	return out;
}

std::string shellQuote(const std::string &value) {
	std::string out = "'";

	for (char ch : value) {
		if (ch == '\'') out += "'\\''";
		else
			out.push_back(ch);
	}
	out.push_back('\'');
	return out;
}

void appendUniquePath(std::vector<std::string> &paths, const std::string &path) {
	std::string normalized = normalizeConfiguredPathInput(path);

	if (normalized.empty()) return;
	if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) paths.push_back(normalized);
}

bool containsPathSeparator(const std::string &value) {
	return value.find('/') != std::string::npos;
}

bool executableFileExists(const std::string &path) {
	std::string normalized = normalizeConfiguredPathInput(path);

	return !normalized.empty() && ::access(normalized.c_str(), X_OK) == 0;
}

std::string pathJoinExecutable(const std::string &directory, const std::string &name) {
	std::string out = directory.empty() ? std::string(".") : directory;

	if (!out.empty() && out.back() != '/') out += '/';
	out += name;
	return out;
}

std::string processPathSearchKey() {
	const char *pathValue = std::getenv("PATH");

	return pathValue != nullptr ? pathValue : "";
}

std::string executableFromPath(const std::string &name) {
	std::string trimmed = trimAscii(name);
	std::string pathText = processPathSearchKey();
	std::string current;

	if (trimmed.empty()) return std::string();
	if (containsPathSeparator(trimmed)) return executableFileExists(trimmed) ? normalizeConfiguredPathInput(trimmed) : std::string();
	for (char ch : pathText) {
		if (ch == ':') {
			const std::string candidate = pathJoinExecutable(current, trimmed);
			if (executableFileExists(candidate)) return normalizeConfiguredPathInput(candidate);
			current.clear();
		} else
			current.push_back(ch);
	}
	{
		const std::string candidate = pathJoinExecutable(current, trimmed);
		if (executableFileExists(candidate)) return normalizeConfiguredPathInput(candidate);
	}
	return std::string();
}

std::string resolveCompilerExecutable(const std::string &value) {
	std::string trimmed = trimAscii(value);

	if (trimmed.empty()) return std::string();
	if (containsPathSeparator(trimmed)) return executableFileExists(trimmed) ? normalizeConfiguredPathInput(trimmed) : std::string();
	return executableFromPath(trimmed);
}

std::string baseNameOfPath(const std::string &path) {
	std::size_t slash = path.find_last_of('/');

	if (slash == std::string::npos) return path;
	return path.substr(slash + 1);
}

std::vector<std::string> splitColonList(std::string_view value) {
	std::vector<std::string> out;
	std::string current;

	for (char ch : value) {
		if (ch == ':') {
			current = trimAscii(current);
			if (!current.empty()) out.push_back(normalizeConfiguredPathInput(current));
			current.clear();
		} else
			current.push_back(ch);
	}
	current = trimAscii(current);
	if (!current.empty()) out.push_back(normalizeConfiguredPathInput(current));
	return out;
}

std::string jsonStringValueAfterKey(const std::string &source, const std::string &key) {
	std::string needle = "\"" + key + "\"";
	std::size_t pos = source.find(needle);
	std::size_t quote = std::string::npos;
	std::size_t end = std::string::npos;

	if (pos == std::string::npos) return std::string();
	pos = source.find(':', pos + needle.size());
	if (pos == std::string::npos) return std::string();
	quote = source.find('"', pos + 1);
	if (quote == std::string::npos) return std::string();
	end = source.find('"', quote + 1);
	if (end == std::string::npos) return std::string();
	return source.substr(quote + 1, end - quote - 1);
}

std::vector<std::string> jsonStringArrayAfterKey(const std::string &source, const std::string &key) {
	std::vector<std::string> out;
	std::string needle = "\"" + key + "\"";
	std::size_t pos = source.find(needle);
	std::size_t start = std::string::npos;
	std::size_t end = std::string::npos;

	if (pos == std::string::npos) return out;
	start = source.find('[', pos + needle.size());
	if (start == std::string::npos) return out;
	end = source.find(']', start + 1);
	if (end == std::string::npos) return out;
	pos = start + 1;
	while (pos < end) {
		std::size_t quote = source.find('"', pos);
		std::size_t close = std::string::npos;

		if (quote == std::string::npos || quote >= end) break;
		close = source.find('"', quote + 1);
		if (close == std::string::npos || close > end) break;
		appendUniquePath(out, source.substr(quote + 1, close - quote - 1));
		pos = close + 1;
	}
	return out;
}

std::string swiftTargetInfo(const std::string &compilerPath) {
	return commandText(shellQuote(compilerPath) + " -print-target-info 2>/dev/null");
}

std::vector<std::string> includeSearchPaths(const std::string &compilerPath) {
	std::vector<std::string> out;
	std::vector<std::string> lines = commandLines("printf '' | " + shellQuote(compilerPath) + " -x c++ -std=c++20 -v -E - 2>&1");
	bool active = false;

	for (const std::string &line : lines) {
		if (line.find("#include <...> search starts here:") != std::string::npos) {
			active = true;
			continue;
		}
		if (active && line.find("End of search list.") != std::string::npos) break;
		if (active) {
			std::string path = trimAscii(line);
			if (!path.empty()) out.push_back(normalizeConfiguredPathInput(path));
		}
	}
	return out;
}

std::vector<std::string> librarySearchPaths(const std::string &compilerPath) {
	std::vector<std::string> lines = commandLines(shellQuote(compilerPath) + " -print-search-dirs 2>/dev/null");

	for (const std::string &line : lines)
		if (line.rfind("libraries: =", 0) == 0) return splitColonList(line.substr(std::strlen("libraries: =")));
	return std::vector<std::string>();
}

std::vector<std::string> runtimePaths(const std::string &compilerPath, const std::string &toolchain) {
	std::vector<std::string> out;

	if (toolchain == "GCC") {
		std::string stdcpp = normalizeConfiguredPathInput(commandFirstLine(shellQuote(compilerPath) + " -print-file-name=libstdc++.so 2>/dev/null"));
		std::string gcc = normalizeConfiguredPathInput(commandFirstLine(shellQuote(compilerPath) + " -print-file-name=libgcc_s.so 2>/dev/null"));
		if (!stdcpp.empty()) out.push_back(stdcpp);
		if (!gcc.empty() && gcc != stdcpp) out.push_back(gcc);
	} else if (toolchain == "CLANG") {
		std::string resource = normalizeConfiguredPathInput(commandFirstLine(shellQuote(compilerPath) + " -print-resource-dir 2>/dev/null"));
		std::string builtins = normalizeConfiguredPathInput(commandFirstLine(shellQuote(compilerPath) + " -print-file-name=libclang_rt.builtins-x86_64.a 2>/dev/null"));
		appendUniquePath(out, resource);
		appendUniquePath(out, builtins);
	} else if (toolchain == "SWIFT") {
		const std::string targetInfo = swiftTargetInfo(compilerPath);
		for (const std::string &path : jsonStringArrayAfterKey(targetInfo, "runtimeLibraryPaths"))
			appendUniquePath(out, path);
		appendUniquePath(out, jsonStringValueAfterKey(targetInfo, "runtimeResourcePath"));
	}
	return out;
}

std::vector<std::string> swiftImportPathsFromTargetInfo(const std::string &targetInfo) {
	return jsonStringArrayAfterKey(targetInfo, "runtimeLibraryImportPaths");
}

std::vector<std::string> swiftLibraryPathsFromTargetInfo(const std::string &targetInfo) {
	return jsonStringArrayAfterKey(targetInfo, "runtimeLibraryPaths");
}

std::vector<std::string> swiftRuntimePathsFromTargetInfo(const std::string &targetInfo) {
	std::vector<std::string> out;

	for (const std::string &path : jsonStringArrayAfterKey(targetInfo, "runtimeLibraryPaths"))
		appendUniquePath(out, path);
	appendUniquePath(out, jsonStringValueAfterKey(targetInfo, "runtimeResourcePath"));
	return out;
}

std::string targetTriple(const std::string &compilerPath) {
	return trimAscii(commandFirstLine(shellQuote(compilerPath) + " -dumpmachine 2>/dev/null"));
}

std::string versionText(const std::string &compilerPath) {
	const std::string executableName = upperAscii(baseNameOfPath(compilerPath));
	if (executableName == "QB64PE") return "QB64-PE";

	return trimAscii(commandFirstLine(shellQuote(compilerPath) + " --version 2>/dev/null"));
}

std::string detectCompilerToolchain(const std::string &compilerPath, const std::string &version) {
	std::string name = upperAscii(baseNameOfPath(compilerPath));
	std::string versionUpper = upperAscii(version);

	if (name.find("SWIFTC") != std::string::npos) return "SWIFT";
	if (name == "FBC" || versionUpper.find("FREEBASIC") != std::string::npos) return "FREEBASIC";
	if (name == "QB64PE" || versionUpper.find("QB64") != std::string::npos) return "QB64PE";
	if (name == "GBC3" || versionUpper.find("GAMBAS COMPILER") != std::string::npos) return "GAMBAS";
	if (name.find("LATEXMK") != std::string::npos || versionUpper.find("LATEXMK") != std::string::npos) return "LATEXMK";
	if (name == "PDFLATEX" || name == "XELATEX" || name == "LUALATEX" || name == "LATEX" || name == "PLATEX" || name == "UPLATEX" || name == "DVILUALATEX") return "LATEX";
	if (name.find("CLANG") != std::string::npos || versionUpper.find("CLANG") != std::string::npos) return "CLANG";
	if (name.find("G++") != std::string::npos || name.find("GCC") != std::string::npos || versionUpper.find("GCC") != std::string::npos) return "GCC";
	return std::string();
}

std::string compilerProfileFlavor(const MRCompilerProfile &profile) {
	std::string source = upperAscii(profile.id + " " + profile.name);

	if (source.find("DEBUG") != std::string::npos) return "DEBUG";
	if (source.find("SPEED") != std::string::npos) return "SPEED";
	if (source.find("SIZE") != std::string::npos) return "SIZE";
	return "NORMAL";
}

std::string defaultBuildFlagsForProfile(const std::string &toolchain, const MRCompilerProfile &profile) {
	const std::string flavor = compilerProfileFlavor(profile);

	if (toolchain == "SWIFT") {
		if (flavor == "DEBUG") return "-g -Onone";
		if (flavor == "SPEED") return "-O -whole-module-optimization";
		if (flavor == "SIZE") return "-Osize";
		return "-O";
	}
	if (toolchain == "GCC" || toolchain == "CLANG") {
		if (flavor == "DEBUG") return "-std=c++20 -g -O0 -Wall -Wextra";
		if (flavor == "SPEED") return "-std=c++20 -O3 -march=native -Wall";
		if (flavor == "SIZE") return "-std=c++20 -Os -Wall";
		return "-std=c++20 -O2 -Wall";
	}
	if (toolchain == "FREEBASIC") {
		if (flavor == "DEBUG") return "-g -exx -O 0";
		if (flavor == "SPEED") return "-O 3";
		if (flavor == "SIZE") return "-O s -strip";
		return "-O 2";
	}
	if (toolchain == "QB64PE") {
		if (flavor == "DEBUG") return "-x -w";
		return "-x -q";
	}
	if (toolchain == "GAMBAS") {
		if (flavor == "DEBUG") return "-a -g -w";
		if (flavor == "SPEED" || flavor == "SIZE") return "-a -x -w";
		return "-a -w";
	}
	if (toolchain == "LATEXMK") return "-pdf -interaction=nonstopmode -file-line-error -synctex=1 -cd";
	if (toolchain == "LATEX") return "-interaction=nonstopmode -file-line-error -synctex=1";
	return std::string();
}

CompilerProbe probeCppCompiler(const std::string &toolchain, const std::string &compilerPath) {
	CompilerProbe probe;

	probe.toolchain = toolchain;
	probe.executablePath = compilerPath;
	probe.versionText = versionText(compilerPath);
	probe.targetTriple = targetTriple(compilerPath);
	probe.includePaths = includeSearchPaths(compilerPath);
	probe.libraryPaths = librarySearchPaths(compilerPath);
	probe.runtimePaths = runtimePaths(compilerPath, toolchain);
	return probe;
}

CompilerProbe probeSwiftCompiler(const std::string &compilerPath) {
	CompilerProbe probe;
	std::string targetInfo = swiftTargetInfo(compilerPath);

	probe.toolchain = "SWIFT";
	probe.executablePath = compilerPath;
	probe.versionText = versionText(compilerPath);
	probe.targetTriple = trimAscii(jsonStringValueAfterKey(targetInfo, "triple"));
	probe.includePaths = swiftImportPathsFromTargetInfo(targetInfo);
	probe.libraryPaths = swiftLibraryPathsFromTargetInfo(targetInfo);
	probe.runtimePaths = swiftRuntimePathsFromTargetInfo(targetInfo);
	return probe;
}

CompilerProbe probeLatexCompiler(const std::string &toolchain, const std::string &compilerPath) {
	CompilerProbe probe;

	probe.toolchain = toolchain;
	probe.executablePath = compilerPath;
	probe.versionText = versionText(compilerPath);
	return probe;
}

CompilerProbe probeBasicCompiler(const std::string &toolchain, const std::string &compilerPath) {
	CompilerProbe probe;

	probe.toolchain = toolchain;
	probe.executablePath = compilerPath;
	probe.versionText = versionText(compilerPath);
	if (toolchain == "FREEBASIC") probe.targetTriple = trimAscii(commandFirstLine(shellQuote(compilerPath) + " -print target 2>/dev/null"));
	return probe;
}

std::string compilerPostBuildMacroSpec() {
	return "compilersupport/MRCompilerMiddleware.mrmac^LatexMKPostBuild";
}

bool isCompilerMiddlewarePostBuildMacroSpec(const std::string &spec) {
	const std::string trimmed = trimAscii(spec);

	return trimmed == "compilersupport/MRCompilerMiddleware.mrmac^MRCompilerPostBuild" || trimmed == "compilersupport/MRCompilerMiddleware.mrmac^MRCompilerLatexmkPostBuild" || trimmed == compilerPostBuildMacroSpec();
}

bool isCompilerMiddlewarePreBuildMacroSpec(const std::string &spec) {
	return trimAscii(spec) == "compilersupport/MRCompilerMiddleware.mrmac^MRCompilerPreBuild";
}

void addDetectedCompilerProfileIdsForTool(std::vector<std::string> &ids, const std::string &toolchain, const char *suffixes[], std::size_t suffixCount) {
	if (toolchain.empty()) return;
	for (std::size_t index = 0; index < suffixCount; ++index) {
		std::string id = canonicalCompilerProfileId(toolchain + "_" + suffixes[index]);

		if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
	}
}

void addProfile(std::vector<MRCompilerProfile> &profiles, const CompilerProbe &probe, const std::string &suffix, const std::string &flags) {
	MRCompilerProfile profile;
	std::string idPrefix = probe.toolchain == "GCC" ? "GCC" : probe.toolchain;
	const char *displayName = probe.toolchain.c_str();
	struct CompilerDisplayName {
		const char *toolchain;
		const char *name;
	};
	static const CompilerDisplayName displayNames[] = {
		{"GCC", "g++"}, {"CLANG", "clang++"}, {"SWIFT", "swiftc"}, {"FREEBASIC", "fbc"}, {"QB64PE", "qb64pe"}, {"GAMBAS", "gbc3"},
	};

	for (const CompilerDisplayName &candidate : displayNames)
		if (probe.toolchain == candidate.toolchain) {
			displayName = candidate.name;
			break;
		}

	profile.id = idPrefix + "_" + upperAscii(suffix);
	profile.name = std::string(displayName) + " " + suffix;
	profile.toolchain = probe.toolchain;
	profile.executablePath = probe.executablePath;
	profile.versionText = probe.versionText;
	profile.targetTriple = probe.targetTriple;
	profile.buildFlags = flags;
	if (probe.toolchain == "LATEXMK") profile.postBuildMacro = compilerPostBuildMacroSpec();
	profile.includePaths = probe.includePaths;
	profile.libraryPaths = probe.libraryPaths;
	profile.runtimePaths = probe.runtimePaths;
	profiles.push_back(profile);
}

std::string normalizeToolchain(const std::string &value) {
	std::string upper = upperAscii(trimAscii(value));

	if (upper == "G++" || upper == "GCC" || upper == "GNU") return "GCC";
	if (upper == "CLANG++" || upper == "CLANG" || upper == "LLVM") return "CLANG";
	if (upper == "SWIFTC" || upper == "SWIFT") return "SWIFT";
	if (upper == "FBC" || upper == "FREEBASIC") return "FREEBASIC";
	if (upper == "QB64" || upper == "QB64PE") return "QB64PE";
	if (upper == "GBC3" || upper == "GAMBAS") return "GAMBAS";
	if (upper == "LATEXMK") return "LATEXMK";
	if (upper == "LATEX") return "LATEX";
	if (upper == "CUSTOM") return "CUSTOM";
	return upper;
}

} // namespace

std::string normalizeCompilerProfilePathList(const std::vector<std::string> &paths) {
	std::string out;

	for (const std::string &path : paths) {
		std::string normalized = normalizeConfiguredPathInput(path);
		if (normalized.empty()) continue;
		if (!out.empty()) out += ";";
		out += normalized;
	}
	return out;
}

std::vector<std::string> splitCompilerProfilePathList(const std::string &value) {
	std::vector<std::string> out;
	std::string current;

	for (char ch : value) {
		if (ch == ';') {
			current = normalizeConfiguredPathInput(current);
			if (!current.empty()) out.push_back(current);
			current.clear();
		} else
			current.push_back(ch);
	}
	current = normalizeConfiguredPathInput(current);
	if (!current.empty()) out.push_back(current);
	return out;
}

std::string canonicalCompilerProfileId(const std::string &value) {
	std::string out;
	std::string trimmed = trimAscii(value);

	for (char ch : trimmed) {
		if (std::isalnum(static_cast<unsigned char>(ch)) != 0) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
		else if (ch == '_' || ch == '-' || ch == ' ')
			out.push_back('_');
	}
	while (!out.empty() && out.front() == '_')
		out.erase(out.begin());
	while (!out.empty() && out.back() == '_')
		out.pop_back();
	return out;
}

std::string canonicalCompilerProfileName(const std::string &value) {
	return trimAscii(value);
}

bool normalizeCompilerProfileInPlace(MRCompilerProfile &profile, std::string *errorMessage) {
	profile.id = canonicalCompilerProfileId(profile.id);
	profile.name = canonicalCompilerProfileName(profile.name);
	profile.toolchain = normalizeToolchain(profile.toolchain);
	profile.executablePath = normalizeConfiguredPathInput(profile.executablePath);
	profile.versionText = trimAscii(profile.versionText);
	profile.targetTriple = trimAscii(profile.targetTriple);
	profile.buildFlags = trimAscii(profile.buildFlags);
	profile.preBuildCommand = trimAscii(profile.preBuildCommand);
	profile.buildSucceededCommand = trimAscii(profile.buildSucceededCommand);
	profile.buildFailedCommand = trimAscii(profile.buildFailedCommand);
	profile.preBuildMacro = trimAscii(profile.preBuildMacro);
	profile.postBuildMacro = trimAscii(profile.postBuildMacro);
	if (isCompilerMiddlewarePreBuildMacroSpec(profile.preBuildMacro)) profile.preBuildMacro.clear();
	if (isCompilerMiddlewarePostBuildMacroSpec(profile.postBuildMacro)) {
		if (profile.toolchain == "LATEXMK") profile.postBuildMacro = compilerPostBuildMacroSpec();
		else
			profile.postBuildMacro.clear();
	}
	profile.includePaths = splitCompilerProfilePathList(normalizeCompilerProfilePathList(profile.includePaths));
	profile.libraryPaths = splitCompilerProfilePathList(normalizeCompilerProfilePathList(profile.libraryPaths));
	profile.runtimePaths = splitCompilerProfilePathList(normalizeCompilerProfilePathList(profile.runtimePaths));
	profile.buildSuccessAudioUri = normalizeConfiguredPathInput(profile.buildSuccessAudioUri);
	profile.buildFailureAudioUri = normalizeConfiguredPathInput(profile.buildFailureAudioUri);

	if (profile.id.empty()) return setError(errorMessage, "Compiler profile id may not be empty.");
	if (profile.name.empty()) return setError(errorMessage, "Compiler profile name may not be empty.");
	if (profile.toolchain.empty()) return setError(errorMessage, "Compiler profile toolchain may not be empty.");
	if (profile.toolchain != "GCC" && profile.toolchain != "CLANG" && profile.toolchain != "SWIFT" && profile.toolchain != "FREEBASIC" && profile.toolchain != "QB64PE" && profile.toolchain != "GAMBAS" && profile.toolchain != "LATEXMK" && profile.toolchain != "LATEX" && profile.toolchain != "CUSTOM") return setError(errorMessage, "Compiler profile toolchain must be GCC, CLANG, SWIFT, FREEBASIC, QB64PE, GAMBAS, LATEXMK, LATEX or CUSTOM.");
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool validateCompilerProfiles(const std::vector<MRCompilerProfile> &profiles, std::string *errorMessage) {
	std::vector<std::string> ids;

	for (MRCompilerProfile profile : profiles) {
		if (!normalizeCompilerProfileInPlace(profile, errorMessage)) return false;
		if (std::find(ids.begin(), ids.end(), profile.id) != ids.end()) return setError(errorMessage, "Duplicate compiler profile id: " + profile.id);
		ids.push_back(profile.id);
	}
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool applyCompilerProfileDirectiveToVector(std::vector<MRCompilerProfile> &profiles, const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, bool *changed, std::string *errorMessage) {
	std::string op = upperAscii(trimAscii(operation));
	std::string id = canonicalCompilerProfileId(profileId);
	MRCompilerProfile *profile = nullptr;

	if (changed != nullptr) *changed = false;
	if (op.empty()) return setError(errorMessage, "MRCOMPILERPROFILE operation may not be empty.");
	if (id.empty()) return setError(errorMessage, "MRCOMPILERPROFILE profile id may not be empty.");
	for (MRCompilerProfile &candidate : profiles)
		if (candidate.id == id) {
			profile = &candidate;
			break;
		}
	if (op == "DEFINE") {
		if (profile != nullptr) return setError(errorMessage, "Duplicate compiler profile id: " + id);
		MRCompilerProfile created;
		created.id = id;
		created.name = canonicalCompilerProfileName(arg3);
		created.toolchain = arg4;
		if (!normalizeCompilerProfileInPlace(created, errorMessage)) return false;
		profiles.push_back(created);
		if (!validateCompilerProfiles(profiles, errorMessage)) {
			profiles.pop_back();
			return false;
		}
		if (changed != nullptr) *changed = true;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	if (profile == nullptr) return setError(errorMessage, "Unknown compiler profile id: " + id);
	if (op == "SET") {
		MRCompilerProfile previous = *profile;
		std::string key = upperAscii(trimAscii(arg3));

		if (key == "NAME")
			profile->name = arg4;
		else if (key == "TOOLCHAIN")
			profile->toolchain = arg4;
		else if (key == "EXECUTABLE")
			profile->executablePath = arg4;
		else if (key == "VERSION")
			profile->versionText = arg4;
		else if (key == "TARGET")
			profile->targetTriple = arg4;
		else if (key == "FLAGS")
			profile->buildFlags = arg4;
		else if (key == "PRE_BUILD_COMMAND")
			profile->preBuildCommand = arg4;
		else if (key == "BUILD_SUCCEEDED_COMMAND")
			profile->buildSucceededCommand = arg4;
		else if (key == "BUILD_FAILED_COMMAND")
			profile->buildFailedCommand = arg4;
		else if (key == "PRE_BUILD_MACRO")
			profile->preBuildMacro = arg4;
		else if (key == "POST_BUILD_MACRO")
			profile->postBuildMacro = arg4;
		else if (key == "INCLUDES")
			profile->includePaths = splitCompilerProfilePathList(arg4);
		else if (key == "LIBRARIES")
			profile->libraryPaths = splitCompilerProfilePathList(arg4);
		else if (key == "RUNTIME")
			profile->runtimePaths = splitCompilerProfilePathList(arg4);
		else if (key == "SUCCESS_AUDIO_URI")
			profile->buildSuccessAudioUri = arg4;
		else if (key == "FAILURE_AUDIO_URI")
			profile->buildFailureAudioUri = arg4;
		else
			return setError(errorMessage, "Unknown compiler profile setting key.");
		if (!normalizeCompilerProfileInPlace(*profile, errorMessage) || !validateCompilerProfiles(profiles, errorMessage)) {
			*profile = previous;
			return false;
		}
		if (changed != nullptr) *changed = previous != *profile;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	return setError(errorMessage, "MRCOMPILERPROFILE supports operations DEFINE and SET.");
}

std::vector<std::string> detectedCompilerExecutablePaths() {
	std::vector<std::string> paths;

	appendUniquePath(paths, executableFromPath("g++"));
	appendUniquePath(paths, executableFromPath("clang++"));
	appendUniquePath(paths, executableFromPath("swiftc"));
	appendUniquePath(paths, executableFromPath("fbc"));
	appendUniquePath(paths, executableFromPath("qb64pe"));
	appendUniquePath(paths, executableFromPath("gbc3"));
	appendUniquePath(paths, executableFromPath("latexmk"));
	appendUniquePath(paths, executableFromPath("pdflatex"));
	appendUniquePath(paths, executableFromPath("xelatex"));
	appendUniquePath(paths, executableFromPath("lualatex"));
	appendUniquePath(paths, executableFromPath("latex"));
	appendUniquePath(paths, executableFromPath("platex"));
	appendUniquePath(paths, executableFromPath("uplatex"));
	appendUniquePath(paths, executableFromPath("dvilualatex"));
	return paths;
}

std::vector<std::string> detectedCompilerProfileIds() {
	std::vector<std::string> ids;
	const char *buildFlavors[] = {"Debug", "Normal", "Speed", "Size"};
	const char *latexmkSuffixes[] = {"PDF"};
	const char *latexEngines[] = {"pdflatex", "xelatex", "lualatex", "latex", "platex", "uplatex", "dvilualatex"};

	if (!executableFromPath("g++").empty()) addDetectedCompilerProfileIdsForTool(ids, "GCC", buildFlavors, sizeof(buildFlavors) / sizeof(buildFlavors[0]));
	if (!executableFromPath("clang++").empty()) addDetectedCompilerProfileIdsForTool(ids, "CLANG", buildFlavors, sizeof(buildFlavors) / sizeof(buildFlavors[0]));
	if (!executableFromPath("swiftc").empty()) addDetectedCompilerProfileIdsForTool(ids, "SWIFT", buildFlavors, sizeof(buildFlavors) / sizeof(buildFlavors[0]));
	if (!executableFromPath("fbc").empty()) addDetectedCompilerProfileIdsForTool(ids, "FREEBASIC", buildFlavors, sizeof(buildFlavors) / sizeof(buildFlavors[0]));
	if (!executableFromPath("qb64pe").empty()) addDetectedCompilerProfileIdsForTool(ids, "QB64PE", buildFlavors, sizeof(buildFlavors) / sizeof(buildFlavors[0]));
	if (!executableFromPath("gbc3").empty()) addDetectedCompilerProfileIdsForTool(ids, "GAMBAS", buildFlavors, sizeof(buildFlavors) / sizeof(buildFlavors[0]));
	if (!executableFromPath("latexmk").empty()) addDetectedCompilerProfileIdsForTool(ids, "LATEXMK", latexmkSuffixes, sizeof(latexmkSuffixes) / sizeof(latexmkSuffixes[0]));
	for (const char *engine : latexEngines)
		if (!executableFromPath(engine).empty()) {
			const std::string id = canonicalCompilerProfileId(std::string("LATEX_") + engine);
			if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
		}
	return ids;
}

std::vector<MRCompilerProfile> detectedCompilerProfiles() {
	static std::vector<MRCompilerProfile> cachedProfiles;
	static std::string cachedKey;
	std::vector<MRCompilerProfile> profiles;
	const std::string key = processPathSearchKey() + "\n" + configuredShellExecutablePath();

	if (cachedKey == key) return cachedProfiles;
	std::string gcc = executableFromPath("g++");
	std::string clang = executableFromPath("clang++");
	std::string swift = executableFromPath("swiftc");
	std::string freeBasic = executableFromPath("fbc");
	std::string qb64pe = executableFromPath("qb64pe");
	std::string gambas = executableFromPath("gbc3");
	std::string latexmk = executableFromPath("latexmk");
	const char *latexEngines[] = {"pdflatex", "xelatex", "lualatex", "latex", "platex", "uplatex", "dvilualatex"};
	if (!gcc.empty()) {
		const CompilerProbe probe = probeCppCompiler("GCC", gcc);
		addProfile(profiles, probe, "Debug", "-std=c++20 -g -O0 -Wall -Wextra");
		addProfile(profiles, probe, "Normal", "-std=c++20 -O2 -Wall");
		addProfile(profiles, probe, "Speed", "-std=c++20 -O3 -march=native -Wall");
		addProfile(profiles, probe, "Size", "-std=c++20 -Os -Wall");
	}
	if (!clang.empty()) {
		const CompilerProbe probe = probeCppCompiler("CLANG", clang);
		addProfile(profiles, probe, "Debug", "-std=c++20 -g -O0 -Wall -Wextra");
		addProfile(profiles, probe, "Normal", "-std=c++20 -O2 -Wall");
		addProfile(profiles, probe, "Speed", "-std=c++20 -O3 -march=native -Wall");
		addProfile(profiles, probe, "Size", "-std=c++20 -Os -Wall");
	}
	if (!swift.empty()) {
		const CompilerProbe probe = probeSwiftCompiler(swift);
		addProfile(profiles, probe, "Debug", "-g -Onone");
		addProfile(profiles, probe, "Normal", "-O");
		addProfile(profiles, probe, "Speed", "-O -whole-module-optimization");
		addProfile(profiles, probe, "Size", "-Osize");
	}
	if (!freeBasic.empty()) {
		const CompilerProbe probe = probeBasicCompiler("FREEBASIC", freeBasic);
		addProfile(profiles, probe, "Debug", "-g -exx -O 0");
		addProfile(profiles, probe, "Normal", "-O 2");
		addProfile(profiles, probe, "Speed", "-O 3");
		addProfile(profiles, probe, "Size", "-O s -strip");
	}
	if (!qb64pe.empty()) {
		const CompilerProbe probe = probeBasicCompiler("QB64PE", qb64pe);
		addProfile(profiles, probe, "Debug", "-x -w");
		addProfile(profiles, probe, "Normal", "-x -q");
		addProfile(profiles, probe, "Speed", "-x -q");
		addProfile(profiles, probe, "Size", "-x -q");
	}
	if (!gambas.empty()) {
		const CompilerProbe probe = probeBasicCompiler("GAMBAS", gambas);
		addProfile(profiles, probe, "Debug", "-a -g -w");
		addProfile(profiles, probe, "Normal", "-a -w");
		addProfile(profiles, probe, "Speed", "-a -x -w");
		addProfile(profiles, probe, "Size", "-a -x -w");
	}
	if (!latexmk.empty()) {
		const CompilerProbe probe = probeLatexCompiler("LATEXMK", latexmk);
		addProfile(profiles, probe, "PDF", "-pdf -interaction=nonstopmode -file-line-error -synctex=1 -cd");
	}
	for (const char *engine : latexEngines) {
		const std::string path = executableFromPath(engine);
		if (path.empty()) continue;
		const CompilerProbe probe = probeLatexCompiler("LATEX", path);
		addProfile(profiles, probe, engine, "-interaction=nonstopmode -file-line-error -synctex=1");
	}
	cachedProfiles = profiles;
	cachedKey = key;
	return profiles;
}

bool autoConfigureCompilerProfileFromExecutable(MRCompilerProfile &profile, std::string *errorMessage) {
	std::string compilerPath = resolveCompilerExecutable(profile.executablePath);
	std::string compilerVersion;
	std::string toolchain;
	CompilerProbe probe;

	if (trimAscii(profile.executablePath).empty()) return setError(errorMessage, "need compiler executable for automatic setup");
	if (compilerPath.empty()) return setError(errorMessage, "Compiler executable not found.");
	compilerVersion = versionText(compilerPath);
	if (compilerVersion.empty()) return setError(errorMessage, "Unable to probe compiler executable.");
	toolchain = detectCompilerToolchain(compilerPath, compilerVersion);
	if (toolchain.empty()) return setError(errorMessage, "Automatic setup does not support this compiler executable.");
	if (toolchain == "SWIFT")
		probe = probeSwiftCompiler(compilerPath);
	else if (toolchain == "LATEXMK")
		probe = probeLatexCompiler("LATEXMK", compilerPath);
	else if (toolchain == "LATEX")
		probe = probeLatexCompiler("LATEX", compilerPath);
	else if (toolchain == "FREEBASIC" || toolchain == "QB64PE" || toolchain == "GAMBAS")
		probe = probeBasicCompiler(toolchain, compilerPath);
	else
		probe = probeCppCompiler(toolchain, compilerPath);

	profile.toolchain = probe.toolchain;
	profile.executablePath = probe.executablePath;
	profile.versionText = probe.versionText;
	profile.targetTriple = probe.targetTriple;
	profile.buildFlags = defaultBuildFlagsForProfile(probe.toolchain, profile);
	profile.includePaths = probe.includePaths;
	profile.libraryPaths = probe.libraryPaths;
	profile.runtimePaths = probe.runtimePaths;
	return normalizeCompilerProfileInPlace(profile, errorMessage);
}

const std::vector<MRCompilerProfile> &configuredCompilerProfiles() {
	recordSettingsRuntimeRead();
	return configuredCompilerProfilesValue();
}

bool setConfiguredCompilerProfiles(const std::vector<MRCompilerProfile> &profiles, std::string *errorMessage) {
	std::vector<MRCompilerProfile> normalized = profiles;
	const std::vector<MRCompilerProfile> previous = configuredCompilerProfilesValue();

	for (MRCompilerProfile &profile : normalized)
		if (!normalizeCompilerProfileInPlace(profile, errorMessage)) return false;
	if (!validateCompilerProfiles(normalized, errorMessage)) return false;
	configuredCompilerProfilesValue() = normalized;
	if (previous != normalized) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

bool compilerProfileIdExists(const std::string &profileId) {
	const std::string id = canonicalCompilerProfileId(profileId);

	if (id.empty()) return false;
	for (const MRCompilerProfile &profile : configuredCompilerProfilesValue())
		if (profile.id == id) return true;
	return false;
}

bool applyConfiguredCompilerProfileDirective(const std::string &operation, const std::string &profileId, const std::string &arg3, const std::string &arg4, std::string *errorMessage) {
	bool changed = false;

	if (!applyCompilerProfileDirectiveToVector(configuredCompilerProfilesValue(), operation, profileId, arg3, arg4, &changed, errorMessage)) return false;
	if (changed) markConfiguredSettingsDirty();
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}
