#ifndef MRCOMMANDROUTERSEARCHSTATE_HPP
#define MRCOMMANDROUTERSEARCHSTATE_HPP

#include "../../config/settings/MRSettingsRuntime.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace mr::search_runtime {

struct SearchUiState {
	bool hasPrevious = false;
	bool hasAcceptedPattern = false;
	std::string pattern;
	std::string acceptedPattern;
	std::string replacement;
	std::size_t lastStart = 0;
	std::size_t lastEnd = 0;
	MRSearchDialogOptions acceptedOptions;
	MRSearchDialogOptions lastOptions;
};

enum class SearchResultsContextKind : unsigned char {
	None = 0,
	SingleFile = 1,
	MultiFile = 2
};

struct SearchResultsContext {
	SearchResultsContextKind kind = SearchResultsContextKind::None;
	int bufferId = 0;
};

struct PendingTransientSelectionClear {
	bool active = false;
	std::string normalizedPath;
	std::size_t start = 0;
	std::size_t end = 0;
};

[[nodiscard]] SearchUiState searchUiState();
void storeSearchUiState(const SearchUiState &value);
[[nodiscard]] SearchResultsContext searchResultsContext();
void storeSearchResultsContext(const SearchResultsContext &value);
[[nodiscard]] PendingTransientSelectionClear pendingTransientSelectionClear();
void storePendingTransientSelectionClear(const PendingTransientSelectionClear &value);
[[nodiscard]] std::string normalizedSearchPath(const std::filesystem::path &path);

} // namespace mr::search_runtime

#endif
