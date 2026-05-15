#ifndef MRCOMMANDROUTERSEARCH_HPP
#define MRCOMMANDROUTERSEARCH_HPP

#include <tvision/tv.h>

[[nodiscard]] bool handleSearchFindText();
[[nodiscard]] bool handleSearchRepeatPrevious();
[[nodiscard]] bool handleSearchReplace();
[[nodiscard]] bool handleSearchMultiFileSearch();
[[nodiscard]] bool handleSearchListFilesFromLastSearch();
[[nodiscard]] bool handleSearchMultiFileSearchReplace();
[[nodiscard]] bool handleSearchResultsNext();
void clearTransientSelectionIfPending(const TEvent &event);

#endif
