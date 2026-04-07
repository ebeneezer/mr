#ifndef MREXTERNALCOMMAND_HPP
#define MREXTERNALCOMMAND_HPP

#include <string>
#include <string_view>

#include "../../coprocessor/MRCoprocessor.hpp"

[[nodiscard]] bool promptForCommandLine(std::string &commandLine);
[[nodiscard]] std::string shortenCommandTitle(std::string_view command);
[[nodiscard]] mr::coprocessor::Result runExternalCommandTask(const mr::coprocessor::TaskInfo &info,
                                                             std::stop_token stopToken,
                                                             std::size_t channelId,
                                                             const std::string &command);

#endif
