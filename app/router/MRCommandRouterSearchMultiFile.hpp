#ifndef MRCOMMANDROUTERSEARCHMULTIFILE_HPP
#define MRCOMMANDROUTERSEARCHMULTIFILE_HPP

#include <string>

[[nodiscard]] bool hasPreviousMultiFileSearchResults();
[[nodiscard]] bool handleMultiFileSearchDialog(const std::string &patternSeed);
[[nodiscard]] bool handleLastMultiFileSearchListDialog();
[[nodiscard]] bool handleMultiFileSearchReplaceDialog(const std::string &patternSeed, const std::string &replacementSeed);
[[nodiscard]] bool handleNextMultiFileSearchResult();

#endif
