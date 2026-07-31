#ifndef MRCOPROCESSORDEFERREDPLAYBACK_HPP
#define MRCOPROCESSORDEFERREDPLAYBACK_HPP

#include "../mrmac/MRVM.hpp"

#include <cstddef>
#include <string>
#include <vector>

void queueDeferredMacroUiPlayback(std::size_t documentId, const std::string &displayName, const std::vector<MRMacroDeferredUiCommand> &commands);

#endif
