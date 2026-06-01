#ifndef MREXTERNALCOMMAND_HPP
#define MREXTERNALCOMMAND_HPP

#include <string>
#include <string_view>

#include "../../coprocessor/MRCoprocessor.hpp"

struct MRCompilerProfile;

[[nodiscard]] std::string shortenCommandTitle(std::string_view command);
[[nodiscard]] bool buildCompilerProfileCommandLine(const MRCompilerProfile &profile, const std::string &sourcePath, std::string &commandLine, std::string *errorMessage = nullptr);
[[nodiscard]] mr::coprocessor::Result runExternalCommandTask(const mr::coprocessor::TaskInfo &info, std::stop_token stopToken, std::size_t channelId, const std::string &command);

#endif
