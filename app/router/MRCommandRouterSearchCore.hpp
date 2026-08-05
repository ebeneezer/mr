#ifndef MRCOMMANDROUTERSEARCHCORE_HPP
#define MRCOMMANDROUTERSEARCHCORE_HPP

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "../../dialogs/setup/MRSetupCommon.hpp"

class MREditWindow;

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

struct SearchMatchEntry {
	std::size_t start = 0;
	std::size_t end = 0;
	std::size_t line = 1;
	std::size_t column = 1;
	std::string preview;
	std::size_t previewMatchOffset = 0;
	std::size_t previewMatchLength = 0;
};

struct SearchPreviewParts {
	std::string text;
	std::size_t matchOffset = 0;
	std::size_t matchLength = 0;
	std::size_t matchLineOffset = 0;
	std::size_t matchLineLength = 0;
	std::string previousLine;
	std::string matchLine;
	std::string nextLine;
};

enum class RegexCollectOutcome : unsigned char {
	Success,
	Cancelled,
	Error
};

std::string buildSearchPatternExpression(const std::string &pattern, MRSearchTextType type);
SearchPreviewParts previewForMatch(const std::string &text, std::size_t start, std::size_t end);
std::size_t centeredPreviewLeft(const std::string &line, std::size_t matchOffset, std::size_t matchLength, std::size_t width);
bool collectRegexMatches(const std::string &text, pcre2_code *code, std::vector<SearchMatchEntry> &outMatches);
RegexCollectOutcome collectRegexMatchesCancellable(const std::string &text, pcre2_code *code, std::vector<SearchMatchEntry> &outMatches, const std::atomic_bool &cancelFlag);
bool compileSearchRegex(const std::string &patternExpression, bool ignoreCase, pcre2_code **outCode, std::string &errorText);
bool compileSearchRegex(const std::string &patternExpression, bool ignoreCase, pcre2_code **outCode, std::string &errorText, bool automaticCallouts);
bool findRegexForward(const std::string &text, pcre2_code *code, std::size_t startOffset, std::size_t &matchStart, std::size_t &matchEnd);
bool findRegexForwardInRange(const std::string &text, pcre2_code *code, std::size_t startOffset, std::size_t rangeStart, std::size_t rangeEnd, std::size_t &matchStart, std::size_t &matchEnd);
bool findLastRegexBeforeLimit(const std::string &text, pcre2_code *code, std::size_t limitOffset, std::size_t rangeStart, std::size_t rangeEnd, std::size_t &matchStart, std::size_t &matchEnd);
bool findRegexWithWrap(const std::string &text, pcre2_code *code, std::size_t startOffset, MRSearchDirection direction, std::size_t rangeStart, std::size_t rangeEnd, bool allowWrap, std::size_t &matchStart, std::size_t &matchEnd, bool &wrapped);
void syncVmLastSearch(MREditWindow *win, bool valid, std::size_t start, std::size_t end, std::size_t cursor);
void clearSearchSelection(MREditWindow *win);
void updateMiniMapFindMarkers(MREditWindow *win, const std::string &pattern, const MRSearchDialogOptions &options);

#endif
