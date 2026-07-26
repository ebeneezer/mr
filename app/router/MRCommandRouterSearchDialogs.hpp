#ifndef MRCOMMANDROUTERSEARCHDIALOGS_HPP
#define MRCOMMANDROUTERSEARCHDIALOGS_HPP

#include <string>

struct SearchPreviewParts;

enum class PromptReplaceDecision : unsigned char {
	Replace = 0,
	Skip = 1,
	ReplaceAll = 2,
	Cancel = 3
};

enum class PromptSearchDecision : unsigned char {
	Next = 0,
	Stop = 1,
	Cancel = 2
};

std::string searchSeedFromCurrentSelection();
PromptReplaceDecision promptReplaceDecisionDialog(const std::string &title, const SearchPreviewParts &preview, const std::string &replacement);
PromptSearchDecision promptSearchDecisionDialog(const SearchPreviewParts &preview);

#endif
