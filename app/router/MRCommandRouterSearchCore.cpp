#define Uses_TProgram
#define Uses_TDeskTop
#include <tvision/tv.h>

#include "MRCommandRouterSearchCore.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "../../mrmac/MRVM.hpp"
#include "../../ui/MREditWindow.hpp"

namespace {

std::string escapeRegexLiteral(std::string_view value) {
	static constexpr const char *kMetaChars = R"(\.^$|()[]{}*+?-)";
	std::string escaped;

	escaped.reserve(value.size() * 2);
	for (char ch : value) {
		if (std::strchr(kMetaChars, ch) != nullptr) escaped.push_back('\\');
		escaped.push_back(ch);
	}
	return escaped;
}

} // namespace

std::string buildSearchPatternExpression(const std::string &pattern, MRSearchTextType type) {
	if (type == MRSearchTextType::Pcre) return pattern;

	const std::string literal = escapeRegexLiteral(pattern);
	if (type == MRSearchTextType::Word) return std::string("\\b") + literal + "\\b";
	return literal;
}

SearchPreviewParts previewForMatch(const std::string &text, std::size_t start, std::size_t end) {
	auto sanitizeLine = [](std::string value) {
		for (char &ch : value) {
			unsigned char uch = static_cast<unsigned char>(ch);
			if (ch == '\t' || ch == '\r' || ch == '\n' || uch < 32 || uch >= 127) ch = ' ';
		}
		return value;
	};
	const std::size_t safeStart = std::min(start, text.size());
	const std::size_t safeEnd = std::min(std::max(end, safeStart), text.size());
	std::size_t highlightStart = safeStart;
	std::size_t highlightEnd = safeEnd;

	if (highlightEnd <= highlightStart && !text.empty()) {
		if (highlightStart < text.size()) highlightEnd = highlightStart + 1;
		else {
			highlightStart = text.size() - 1;
			highlightEnd = text.size();
		}
	}

	const std::size_t lineStart = text.rfind('\n', highlightStart == 0 ? 0 : highlightStart - 1);
	const std::size_t left = (lineStart == std::string::npos) ? 0 : lineStart + 1;
	const std::size_t lineEnd = text.find('\n', highlightStart);
	const std::size_t right = (lineEnd == std::string::npos) ? text.size() : lineEnd;
	const std::size_t windowLeft = highlightStart > 24 ? highlightStart - 24 : left;
	const std::size_t windowRight = std::min(right, highlightEnd + 24);
	std::string leftText = text.substr(windowLeft, highlightStart - windowLeft);
	std::string matchText = text.substr(highlightStart, highlightEnd - highlightStart);
	std::string rightText = text.substr(highlightEnd, windowRight - highlightEnd);
	SearchPreviewParts parts;

	parts.matchOffset = leftText.size();
	parts.matchLength = matchText.size();
	parts.matchLineOffset = highlightStart >= left ? highlightStart - left : 0;
	parts.matchLineLength = highlightEnd - highlightStart;
	parts.text = leftText + matchText + rightText;
	parts.text = sanitizeLine(parts.text);
	parts.matchLine = sanitizeLine(text.substr(left, right - left));
	if (left > 0) {
		const std::size_t prevEnd = left - 1;
		const std::size_t prevStartBreak = text.rfind('\n', prevEnd == 0 ? 0 : prevEnd - 1);
		const std::size_t prevStart = prevStartBreak == std::string::npos ? 0 : prevStartBreak + 1;
		parts.previousLine = sanitizeLine(text.substr(prevStart, prevEnd - prevStart));
	}
	if (right < text.size()) {
		const std::size_t nextStart = right + 1;
		const std::size_t nextEndBreak = text.find('\n', nextStart);
		const std::size_t nextEnd = nextEndBreak == std::string::npos ? text.size() : nextEndBreak;
		parts.nextLine = sanitizeLine(text.substr(nextStart, nextEnd - nextStart));
	}
	return parts;
}

std::size_t centeredPreviewLeft(const std::string &line, std::size_t matchOffset, std::size_t matchLength, std::size_t width) {
	if (line.empty() || width == 0 || line.size() <= width) return 0;
	const std::size_t safeOffset = std::min(matchOffset, line.size());
	const std::size_t safeLength = std::min(matchLength, line.size() - safeOffset);
	const std::size_t center = safeOffset + safeLength / 2;
	const std::size_t maxLeft = line.size() - width;
	std::size_t left = center > width / 2 ? center - width / 2 : 0;

	if (safeLength > 0) {
		const std::size_t minLeft = (safeOffset + safeLength > width) ? safeOffset + safeLength - width : 0;
		const std::size_t maxLeftForVisibleStart = safeOffset;
		left = std::max(left, minLeft);
		left = std::min(left, maxLeftForVisibleStart);
	}
	return std::min(left, maxLeft);
}

void lineColumnForOffset(const std::string &text, std::size_t offset, std::size_t &line, std::size_t &column) {
	const std::size_t safe = std::min(offset, text.size());
	std::size_t currentLine = 1;
	std::size_t lastLineStart = 0;

	for (std::size_t i = 0; i < safe; ++i)
		if (text[i] == '\n') {
			++currentLine;
			lastLineStart = i + 1;
		}
	line = currentLine;
	column = safe - lastLineStart + 1;
}

bool collectRegexMatches(const std::string &text, pcre2_code *code, std::vector<SearchMatchEntry> &outMatches) {
	pcre2_match_data *matchData = nullptr;
	std::size_t seek = 0;

	outMatches.clear();
	matchData = pcre2_match_data_create_from_pattern(code, nullptr);
	if (matchData == nullptr) return false;
	while (seek <= text.size()) {
		int rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(text.data()), static_cast<PCRE2_SIZE>(text.size()), static_cast<PCRE2_SIZE>(seek), 0, matchData, nullptr);
		PCRE2_SIZE *ovector = nullptr;
		std::size_t start = 0;
		std::size_t end = 0;
		SearchMatchEntry entry;

		if (rc < 0) break;
		ovector = pcre2_get_ovector_pointer(matchData);
		start = static_cast<std::size_t>(ovector[0]);
		end = static_cast<std::size_t>(ovector[1]);
		if (end < start || end > text.size()) break;
		entry.start = start;
		entry.end = end;
		lineColumnForOffset(text, start, entry.line, entry.column);
		{
			SearchPreviewParts preview = previewForMatch(text, start, end);
			entry.preview = preview.text;
			entry.previewMatchOffset = preview.matchOffset;
			entry.previewMatchLength = preview.matchLength;
		}
		outMatches.push_back(entry);
		if (end > seek) seek = end;
		else
			++seek;
	}
	pcre2_match_data_free(matchData);
	return true;
}

bool compileSearchRegex(const std::string &patternExpression, bool ignoreCase, pcre2_code **outCode, std::string &errorText) {
	int errorCode = 0;
	int jitCode = 0;
	PCRE2_SIZE errorOffset = 0;
	uint32_t options = PCRE2_UTF | PCRE2_UCP;
	char errorBuffer[256];
	int messageLength = 0;

	*outCode = nullptr;
	if (ignoreCase) options |= PCRE2_CASELESS;
	*outCode = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(patternExpression.c_str()), static_cast<PCRE2_SIZE>(patternExpression.size()), options, &errorCode, &errorOffset, nullptr);
	if (*outCode != nullptr) {
		jitCode = pcre2_jit_compile(*outCode, PCRE2_JIT_COMPLETE);
		if (jitCode < 0 && jitCode != PCRE2_ERROR_JIT_BADOPTION && jitCode != PCRE2_ERROR_NOMEMORY) {
			errorText = "Regex JIT compile error: " + std::to_string(jitCode);
			pcre2_code_free(*outCode);
			*outCode = nullptr;
			return false;
		}
		errorText.clear();
		return true;
	}
	std::memset(errorBuffer, 0, sizeof(errorBuffer));
	messageLength = static_cast<int>(pcre2_get_error_message(errorCode, reinterpret_cast<PCRE2_UCHAR *>(errorBuffer), sizeof(errorBuffer)));
	if (messageLength < 0) errorText = "Regex compile error.";
	else
		errorText = std::string(errorBuffer, static_cast<std::size_t>(messageLength));
	errorText += " (offset ";
	errorText += std::to_string(static_cast<unsigned long long>(errorOffset));
	errorText += ")";
	return false;
}

bool findRegexForward(const std::string &text, pcre2_code *code, std::size_t startOffset, std::size_t &matchStart, std::size_t &matchEnd) {
	pcre2_match_data *matchData = nullptr;
	PCRE2_SIZE *ovector = nullptr;
	int rc = 0;
	const std::size_t safeStart = std::min(startOffset, text.size());

	matchData = pcre2_match_data_create_from_pattern(code, nullptr);
	if (matchData == nullptr) return false;
	rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(text.data()), static_cast<PCRE2_SIZE>(text.size()), static_cast<PCRE2_SIZE>(safeStart), 0, matchData, nullptr);
	if (rc < 0) {
		pcre2_match_data_free(matchData);
		return false;
	}
	ovector = pcre2_get_ovector_pointer(matchData);
	matchStart = static_cast<std::size_t>(ovector[0]);
	matchEnd = static_cast<std::size_t>(ovector[1]);
	pcre2_match_data_free(matchData);
	return matchEnd >= matchStart;
}

bool findRegexForwardInRange(const std::string &text, pcre2_code *code, std::size_t startOffset, std::size_t rangeStart, std::size_t rangeEnd, std::size_t &matchStart, std::size_t &matchEnd) {
	const std::size_t safeStart = std::min(std::max(startOffset, rangeStart), text.size());
	const std::size_t safeEnd = std::min(rangeEnd, text.size());
	std::size_t seek = safeStart;

	if (safeStart >= safeEnd) return false;
	while (seek < safeEnd) {
		std::size_t nextStart = 0;
		std::size_t nextEnd = 0;
		if (!findRegexForward(text, code, seek, nextStart, nextEnd)) return false;
		if (nextStart >= safeEnd) return false;
		if (nextStart >= rangeStart && nextEnd <= safeEnd) {
			matchStart = nextStart;
			matchEnd = nextEnd;
			return true;
		}
		if (nextEnd > seek) seek = nextEnd;
		else
			++seek;
	}
	return false;
}

bool findLastRegexBeforeLimit(const std::string &text, pcre2_code *code, std::size_t limitOffset, std::size_t rangeStart, std::size_t rangeEnd, std::size_t &matchStart, std::size_t &matchEnd) {
	const std::size_t boundedStart = std::min(rangeStart, text.size());
	const std::size_t boundedEnd = std::min(rangeEnd, text.size());
	const std::size_t limit = std::min(std::max(limitOffset, boundedStart), boundedEnd);
	std::size_t candidateStart = 0;
	std::size_t candidateEnd = 0;
	std::size_t seek = boundedStart;
	bool found = false;

	if (boundedStart >= boundedEnd) return false;
	while (seek < boundedEnd) {
		std::size_t nextStart = 0;
		std::size_t nextEnd = 0;

		if (!findRegexForwardInRange(text, code, seek, boundedStart, boundedEnd, nextStart, nextEnd)) break;
		if (nextStart >= limit) break;
		candidateStart = nextStart;
		candidateEnd = nextEnd;
		found = true;
		if (nextEnd > seek) seek = nextEnd;
		else
			++seek;
	}
	if (!found) return false;
	matchStart = candidateStart;
	matchEnd = candidateEnd;
	return true;
}

bool findRegexWithWrap(const std::string &text, pcre2_code *code, std::size_t startOffset, MRSearchDirection direction, std::size_t rangeStart, std::size_t rangeEnd, bool allowWrap, std::size_t &matchStart, std::size_t &matchEnd, bool &wrapped) {
	const std::size_t safeRangeStart = std::min(rangeStart, text.size());
	const std::size_t safeRangeEnd = std::min(rangeEnd, text.size());
	const std::size_t safeStart = std::min(std::max(startOffset, safeRangeStart), safeRangeEnd);

	wrapped = false;
	if (direction == MRSearchDirection::Forward) {
		if (findRegexForwardInRange(text, code, safeStart, safeRangeStart, safeRangeEnd, matchStart, matchEnd)) return true;
		if (!allowWrap) return false;
		if (safeStart <= safeRangeStart) return false;
		if (findRegexForwardInRange(text, code, safeRangeStart, safeRangeStart, safeRangeEnd, matchStart, matchEnd)) {
			wrapped = true;
			return true;
		}
		return false;
	}
	if (findLastRegexBeforeLimit(text, code, safeStart, safeRangeStart, safeRangeEnd, matchStart, matchEnd)) return true;
	if (!allowWrap) return false;
	if (safeStart >= safeRangeEnd) return false;
	if (findLastRegexBeforeLimit(text, code, safeRangeEnd, safeRangeStart, safeRangeEnd, matchStart, matchEnd)) {
		wrapped = true;
		return true;
	}
	return false;
}

void syncVmLastSearch(MREditWindow *win, bool valid, std::size_t start, std::size_t end, std::size_t cursor) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::string fileName;
	if (win == nullptr) return;
	if (editor != nullptr && editor->hasPersistentFileName()) fileName = editor->persistentFileName();
	mrvmUiReplaceWindowLastSearch(win, fileName, valid, start, end, cursor);
}

void clearSearchSelection(MREditWindow *win) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	const std::size_t cursor = editor != nullptr ? editor->cursorOffset() : 0;

	if (editor == nullptr) return;
	editor->setSelectionOffsets(cursor, cursor);
	syncVmLastSearch(win, false, 0, 0, cursor);
	editor->clearFindMarkerRanges();
	editor->refreshViewState();
}

void updateMiniMapFindMarkers(MREditWindow *win, const std::string &pattern, const MRSearchDialogOptions &options) {
	MRFileEditor *editor = win != nullptr ? win->getEditor() : nullptr;
	std::vector<std::pair<std::size_t, std::size_t>> ranges;
	pcre2_code *code = nullptr;
	std::string regexError;
	std::string text;
	std::vector<SearchMatchEntry> matches;
	std::size_t rangeStart = 0;
	std::size_t rangeEnd = 0;

	if (editor == nullptr) return;
	if (pattern.empty()) {
		editor->clearFindMarkerRanges();
		return;
	}
	if (!compileSearchRegex(buildSearchPatternExpression(pattern, options.textType), !options.caseSensitive, &code, regexError)) {
		editor->clearFindMarkerRanges();
		return;
	}
	text = editor->snapshotText();
	rangeStart = 0;
	rangeEnd = text.size();
	if (options.restrictToMarkedBlock) {
		rangeStart = std::min(editor->selectionStartOffset(), editor->selectionEndOffset());
		rangeEnd = std::max(editor->selectionStartOffset(), editor->selectionEndOffset());
		if (rangeStart >= rangeEnd) {
			pcre2_code_free(code);
			editor->clearFindMarkerRanges();
			return;
		}
	}
	static_cast<void>(collectRegexMatches(text, code, matches));
	pcre2_code_free(code);
	for (const SearchMatchEntry &match : matches) {
		if (match.start < rangeStart || match.end > rangeEnd) continue;
		std::size_t start = std::min(match.start, text.size());
		std::size_t end = std::min(std::max(match.end, start), text.size());
		if (end == start) {
			if (end < text.size()) ++end;
			else if (start > 0)
				--start;
		}
		if (end > start) ranges.push_back(std::make_pair(start, end));
	}
	editor->setFindMarkerRanges(ranges);
}
