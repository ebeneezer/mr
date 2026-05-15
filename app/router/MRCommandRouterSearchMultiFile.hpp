#ifndef MRCOMMANDROUTERSEARCHMULTIFILE_HPP
#define MRCOMMANDROUTERSEARCHMULTIFILE_HPP

#include <string>

[[nodiscard]] bool hasPreviousMultiFileSearchResults();
[[nodiscard]] bool handleSearchMultiFileSearchImpl(const std::string &patternSeed);
[[nodiscard]] bool handleSearchListFilesFromLastSearchImpl();
[[nodiscard]] bool handleSearchMultiFileSearchReplaceImpl(const std::string &patternSeed, const std::string &replacementSeed);
[[nodiscard]] bool handleSearchResultsNextMultiFileImpl();

#endif
