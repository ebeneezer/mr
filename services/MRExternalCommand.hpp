#ifndef MREXTERNALCOMMAND_HPP
#define MREXTERNALCOMMAND_HPP

#include <string>

#include "../coprocessor/MRCoprocessor.hpp"

bool promptForCommandLine(std::string &commandLine);
std::string shortenCommandTitle(const std::string &command);
mr::coprocessor::Result runExternalCommandTask(const mr::coprocessor::TaskInfo &info,
                                               std::stop_token stopToken, std::size_t channelId,
                                               const std::string &command);

#endif
