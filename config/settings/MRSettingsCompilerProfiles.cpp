#include "../../app/utils/MRStringUtils.hpp"
#include "MRSettingsCompilerProfiles.hpp"
#include "MRSettingsRuntimeState.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
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

std::string commandFirstLine(const std::string &command) {
	std::array<char, 1024> buffer{};
	std::string line;
	FILE *pipe = ::popen(command.c_str(), "r");

	if (pipe == nullptr) return std::string();
	if (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) line = buffer.data();
	::pclose(pipe);
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
		line.pop_back();
	return line;
}

std::vector<std::string> commandLines(const std::string &command) {
	std::array<char, 2048> buffer{};
	std::vector<std::string> lines;
	FILE *pipe = ::popen(command.c_str(), "r");

	if (pipe == nullptr) return lines;
	while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
		std::string line = buffer.data();
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
			line.pop_back();
		lines.push_back(line);
	}
	::pclose(pipe);
	return lines;
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

std::string executableFromPath(const std::string &name) {
	std::string trimmed = trimAscii(name);

	if (trimmed.empty()) return std::string();
	return normalizeConfiguredPathInput(commandFirstLine("command -v " + shellQuote(trimmed) + " 2>/dev/null"));
}

std::string resolveCompilerExecutable(const std::string &value) {
	std::string trimmed = trimAscii(value);

	if (trimmed.empty()) return std::string();
	if (containsPathSeparator(trimmed)) return normalizeConfiguredPathInput(trimmed);
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
		if (line.starts_with("libraries: =")) return splitColonList(line.substr(std::strlen("libraries: =")));
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
	return trimAscii(commandFirstLine(shellQuote(compilerPath) + " --version 2>/dev/null"));
}

std::string detectCompilerToolchain(const std::string &compilerPath, const std::string &version) {
	std::string name = upperAscii(baseNameOfPath(compilerPath));
	std::string versionUpper = upperAscii(version);

	if (name.find("SWIFTC") != std::string::npos) return "SWIFT";
	if (name.find("LATEXMK") != std::string::npos || versionUpper.find("LATEXMK") != std::string::npos) return "LATEXMK";
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
	if (toolchain == "LATEXMK") return "-pdf -interaction=nonstopmode -file-line-error -synctex=1 -cd";
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

CompilerProbe probeLatexmkCompiler(const std::string &compilerPath) {
	CompilerProbe probe;

	probe.toolchain = "LATEXMK";
	probe.executablePath = compilerPath;
	probe.versionText = versionText(compilerPath);
	return probe;
}

void addProfile(std::vector<MRCompilerProfile> &profiles, const CompilerProbe &probe, const std::string &suffix, const std::string &flags) {
	MRCompilerProfile profile;
	std::string idPrefix = probe.toolchain == "GCC" ? "GCC" : probe.toolchain;

	profile.id = idPrefix + "_" + upperAscii(suffix);
	profile.name = (probe.toolchain == "GCC" ? "g++" : probe.toolchain == "CLANG" ? "clang++" : probe.toolchain == "SWIFT" ? "swiftc" : probe.toolchain) + std::string(" ") + suffix;
	profile.toolchain = probe.toolchain;
	profile.executablePath = probe.executablePath;
	profile.versionText = probe.versionText;
	profile.targetTriple = probe.targetTriple;
	profile.buildFlags = flags;
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
	if (upper == "LATEXMK" || upper == "LATEX") return "LATEXMK";
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
	profile.includePaths = splitCompilerProfilePathList(normalizeCompilerProfilePathList(profile.includePaths));
	profile.libraryPaths = splitCompilerProfilePathList(normalizeCompilerProfilePathList(profile.libraryPaths));
	profile.runtimePaths = splitCompilerProfilePathList(normalizeCompilerProfilePathList(profile.runtimePaths));
	profile.buildSuccessAudioUri = normalizeConfiguredPathInput(profile.buildSuccessAudioUri);
	profile.buildFailureAudioUri = normalizeConfiguredPathInput(profile.buildFailureAudioUri);
	profile.lspExecutablePath = normalizeConfiguredPathInput(profile.lspExecutablePath);
	profile.lspArguments = trimAscii(profile.lspArguments);
	profile.lspWorkingDirectory = normalizeConfiguredPathInput(profile.lspWorkingDirectory);
	profile.lspMiddlewarePath = normalizeConfiguredPathInput(profile.lspMiddlewarePath);

	if (profile.id.empty()) return setError(errorMessage, "Compiler profile id may not be empty.");
	if (profile.name.empty()) return setError(errorMessage, "Compiler profile name may not be empty.");
	if (profile.toolchain.empty()) return setError(errorMessage, "Compiler profile toolchain may not be empty.");
	if (profile.toolchain != "GCC" && profile.toolchain != "CLANG" && profile.toolchain != "SWIFT" && profile.toolchain != "LATEXMK" && profile.toolchain != "CUSTOM") return setError(errorMessage, "Compiler profile toolchain must be GCC, CLANG, SWIFT, LATEXMK or CUSTOM.");
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

std::vector<std::string> defaultCompilerExecutablePaths() {
	static std::vector<std::string> cachedPaths;
	static bool initialized = false;
	std::vector<std::string> paths;

	if (initialized) return cachedPaths;
	appendUniquePath(paths, executableFromPath("g++"));
	appendUniquePath(paths, executableFromPath("clang++"));
	appendUniquePath(paths, executableFromPath("swiftc"));
	appendUniquePath(paths, executableFromPath("latexmk"));
	cachedPaths = paths;
	initialized = true;
	return paths;
}

std::vector<MRCompilerProfile> defaultCompilerProfiles() {
	static std::vector<MRCompilerProfile> cachedProfiles;
	static bool initialized = false;
	std::vector<MRCompilerProfile> profiles;

	if (initialized) return cachedProfiles;
	std::string gcc = executableFromPath("g++");
	std::string clang = executableFromPath("clang++");
	std::string swift = executableFromPath("swiftc");
	std::string latexmk = executableFromPath("latexmk");
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
	if (!latexmk.empty()) {
		const CompilerProbe probe = probeLatexmkCompiler(latexmk);
		addProfile(profiles, probe, "PDF", "-pdf -interaction=nonstopmode -file-line-error -synctex=1 -cd");
	}
	cachedProfiles = profiles;
	initialized = true;
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
		probe = probeLatexmkCompiler(compilerPath);
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
	std::string op = upperAscii(trimAscii(operation));
	std::string id = canonicalCompilerProfileId(profileId);
	std::vector<MRCompilerProfile> profiles = configuredCompilerProfilesValue();
	MRCompilerProfile *profile = nullptr;

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
		created.toolchain = normalizeToolchain(arg4);
		profiles.push_back(created);
		return setConfiguredCompilerProfiles(profiles, errorMessage);
	}
	if (profile == nullptr) return setError(errorMessage, "Unknown compiler profile id: " + id);
	if (op == "SET") {
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
		else if (key == "LSP_EXECUTABLE")
			profile->lspExecutablePath = arg4;
		else if (key == "LSP_ARGUMENTS")
			profile->lspArguments = arg4;
		else if (key == "LSP_WORKING_DIRECTORY")
			profile->lspWorkingDirectory = arg4;
		else if (key == "LSP_MIDDLEWARE")
			profile->lspMiddlewarePath = arg4;
		else
			return setError(errorMessage, "Unknown compiler profile setting key.");
		return setConfiguredCompilerProfiles(profiles, errorMessage);
	}
	return setError(errorMessage, "MRCOMPILERPROFILE supports operations DEFINE and SET.");
}
