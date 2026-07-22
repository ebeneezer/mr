#ifndef MREXTERNALCOMMAND_HPP
#define MREXTERNALCOMMAND_HPP

#include <string>
#include <string_view>

#include "../../coprocessor/MRCoprocessor.hpp"

struct MRCompilerProfile;

struct MRBuildHookContext {
	std::string sourcePath;
	std::string sourceDir;
	std::string sourceFile;
	std::string sourceStem;
	std::string outputPath;
	std::string pdfPath;
	std::string profileId;
	std::string profileName;
	std::string toolchain;
	std::string preBuildMacro;
	std::string postBuildMacro;
	int sourceBufferId;

	MRBuildHookContext() noexcept : sourcePath(), sourceDir(), sourceFile(), sourceStem(), outputPath(), pdfPath(), profileId(), profileName(), toolchain(), preBuildMacro(), postBuildMacro(), sourceBufferId(0) {
	}
};

[[nodiscard]] std::string shortenCommandTitle(std::string_view command);
[[nodiscard]] MRBuildHookContext buildCompilerProfileHookContext(const MRCompilerProfile &profile, const std::string &sourcePath, int sourceBufferId = 0);
void applyBuildHookContextGlobals(const MRBuildHookContext &context, int exitStatus, const std::string &statusText, const std::string &errorText);
bool runBuildHookMacro(const std::string &macroSpec, const MRBuildHookContext &context, int exitStatus, const std::string &statusText, const std::string &errorText, std::string *errorMessage = nullptr);
[[nodiscard]] bool buildCompilerProfileCommandLine(const MRCompilerProfile &profile, const std::string &sourcePath, std::string &commandLine, std::string *errorMessage = nullptr);
[[nodiscard]] mr::coprocessor::Result runExternalCommandTask(const mr::coprocessor::TaskInfo &info, std::size_t channelId, const std::string &command, const MRBuildHookContext &buildContext = MRBuildHookContext(), const std::string &successAudioUri = std::string(), const std::string &failureAudioUri = std::string());

#endif
