#ifndef MRCOMMANDROUTERSEARCHMULTIFILECOLLECT_HPP
#define MRCOMMANDROUTERSEARCHMULTIFILECOLLECT_HPP

#include <filesystem>
#include <string>

#include "MRCommandRouterSearchMultiFileSession.hpp"

std::string normalizedSearchPath(const std::filesystem::path &path);
MultiFileCollectOutcome collectMultiFileSession(MultiFileSearchSession &session, const MRMultiSearchDialogOptions &options, const std::string &pattern, const std::string &replacement, bool replaceMode, bool keepFilesOpen, std::string &errorText);

#endif
