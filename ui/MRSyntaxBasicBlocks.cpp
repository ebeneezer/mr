#include "MRSyntaxBasic.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace {

struct BasicBlockPhrase {
	const char *text;
	MRBasicBlockKind kind;
};

constexpr BasicBlockPhrase kBasicClosingPhrases[] = {
	{"END IF", MRBasicBlockKind::Conditional},
	{"ENDIF", MRBasicBlockKind::Conditional},
	{"END SELECT", MRBasicBlockKind::Select},
	{"NEXT", MRBasicBlockKind::Loop},
	{"LOOP", MRBasicBlockKind::Loop},
	{"WEND", MRBasicBlockKind::Loop},
	{"END WHILE", MRBasicBlockKind::Loop},
	{"END SUB", MRBasicBlockKind::Procedure},
	{"END FUNCTION", MRBasicBlockKind::Procedure},
	{"END PROPERTY", MRBasicBlockKind::Procedure},
	{"END CONSTRUCTOR", MRBasicBlockKind::Procedure},
	{"END DESTRUCTOR", MRBasicBlockKind::Procedure},
	{"END TYPE", MRBasicBlockKind::Type},
	{"END ENUM", MRBasicBlockKind::Type},
	{"END CLASS", MRBasicBlockKind::Type},
	{"END STRUCT", MRBasicBlockKind::Type},
	{"END WITH", MRBasicBlockKind::With},
	{"END TRY", MRBasicBlockKind::Try},
	{"END", MRBasicBlockKind::Procedure},
};

constexpr BasicBlockPhrase kBasicContinuationPhrases[] = {
	{"ELSEIF", MRBasicBlockKind::Conditional},
	{"ELSE IF", MRBasicBlockKind::Conditional},
	{"ELSE", MRBasicBlockKind::Conditional},
	{"CASE", MRBasicBlockKind::Select},
	{"CATCH", MRBasicBlockKind::Try},
	{"FINALLY", MRBasicBlockKind::Try},
};

constexpr BasicBlockPhrase kBasicOpeningPhrases[] = {
	{"SELECT CASE", MRBasicBlockKind::Select},
	{"FOR", MRBasicBlockKind::Loop},
	{"DO", MRBasicBlockKind::Loop},
	{"WHILE", MRBasicBlockKind::Loop},
	{"SUB", MRBasicBlockKind::Procedure},
	{"FUNCTION", MRBasicBlockKind::Procedure},
	{"PROPERTY", MRBasicBlockKind::Procedure},
	{"CONSTRUCTOR", MRBasicBlockKind::Procedure},
	{"DESTRUCTOR", MRBasicBlockKind::Procedure},
	{"TYPE", MRBasicBlockKind::Type},
	{"ENUM", MRBasicBlockKind::Type},
	{"CLASS", MRBasicBlockKind::Type},
	{"STRUCT", MRBasicBlockKind::Type},
	{"WITH", MRBasicBlockKind::With},
	{"TRY", MRBasicBlockKind::Try},
};

bool basicWhitespace(char ch) noexcept {
	return ch == ' ' || ch == '\t' || ch == '\r';
}

std::string_view trimBasicLine(std::string_view line) noexcept {
	while (!line.empty() && basicWhitespace(line.front()))
		line.remove_prefix(1);
	while (!line.empty() && basicWhitespace(line.back()))
		line.remove_suffix(1);
	return line;
}

bool basicPhraseAtStart(std::string_view line, std::string_view phrase) noexcept {
	if (line.size() < phrase.size() || line.substr(0, phrase.size()) != phrase) return false;
	if (line.size() == phrase.size()) return true;
	const unsigned char next = static_cast<unsigned char>(line[phrase.size()]);
	return std::isspace(next) != 0 || next == ':' || next == '(';
}

std::string basicUpperLineWithoutComment(std::string_view line) {
	std::string out;
	char quote = '\0';

	for (std::size_t index = 0; index < line.size(); ++index) {
		const char ch = line[index];
		if (quote != '\0') {
			out.push_back(ch);
			if (ch == quote) {
				if (index + 1 < line.size() && line[index + 1] == quote) {
					out.push_back(line[++index]);
					continue;
				}
				quote = '\0';
			}
			continue;
		}
		if (ch == '\'') break;
		if (ch == '"') {
			quote = ch;
			out.push_back(ch);
			continue;
		}
		out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
	}

	return out;
}

std::string_view stripBasicLineNumber(std::string_view line) noexcept {
	std::size_t index = 0;
	while (index < line.size() && std::isdigit(static_cast<unsigned char>(line[index])) != 0)
		++index;
	if (index == 0 || index == line.size() || !basicWhitespace(line[index])) return line;
	return trimBasicLine(line.substr(index));
}

std::string_view stripBasicVisibilityPrefixes(std::string_view line) noexcept {
	static constexpr std::string_view kVisibilityPrefixes[] = {"PUBLIC", "PRIVATE", "PROTECTED", "STATIC", "SHARED", "EXPORT"};
	bool stripped = true;

	while (stripped) {
		stripped = false;
		for (std::string_view prefix : kVisibilityPrefixes) {
			if (!basicPhraseAtStart(line, prefix)) continue;
			line = trimBasicLine(line.substr(prefix.size()));
			stripped = true;
			break;
		}
	}
	return line;
}

MRBasicBlockLine basicBlockLineForPhrases(std::string_view line, const BasicBlockPhrase phrases[], std::size_t phraseCount, MRBasicBlockDisposition disposition) noexcept {
	for (std::size_t index = 0; index < phraseCount; ++index)
		if (basicPhraseAtStart(line, phrases[index].text)) return {phrases[index].kind, disposition};
	return {MRBasicBlockKind::None, MRBasicBlockDisposition::None};
}

bool basicIfOpensBlock(std::string_view line) noexcept {
	const std::size_t thenPos = line.find(" THEN");

	if (!basicPhraseAtStart(line, "IF") || thenPos == std::string_view::npos) return false;
	return trimBasicLine(line.substr(thenPos + 5)).empty();
}

} // namespace

bool mrBasicLineIsComment(std::string_view line) noexcept {
	line = trimBasicLine(line);
	return !line.empty() && (line.front() == '\'' || basicPhraseAtStart(line, "REM"));
}

MRBasicBlockLine mrBasicClassifyBlockLine(std::string_view line) noexcept {
	std::string upper = basicUpperLineWithoutComment(line);
	std::string_view normalized = stripBasicLineNumber(trimBasicLine(upper));
	MRBasicBlockLine result;

	if (normalized.empty() || mrBasicLineIsComment(normalized)) return {MRBasicBlockKind::None, MRBasicBlockDisposition::None};
	result = basicBlockLineForPhrases(normalized, kBasicClosingPhrases, sizeof(kBasicClosingPhrases) / sizeof(kBasicClosingPhrases[0]), MRBasicBlockDisposition::Close);
	if (result.disposition != MRBasicBlockDisposition::None) return result;
	result = basicBlockLineForPhrases(normalized, kBasicContinuationPhrases, sizeof(kBasicContinuationPhrases) / sizeof(kBasicContinuationPhrases[0]), MRBasicBlockDisposition::Continue);
	if (result.disposition != MRBasicBlockDisposition::None) return result;
	if (basicIfOpensBlock(normalized)) return {MRBasicBlockKind::Conditional, MRBasicBlockDisposition::Open};
	normalized = stripBasicVisibilityPrefixes(normalized);
	return basicBlockLineForPhrases(normalized, kBasicOpeningPhrases, sizeof(kBasicOpeningPhrases) / sizeof(kBasicOpeningPhrases[0]), MRBasicBlockDisposition::Open);
}
