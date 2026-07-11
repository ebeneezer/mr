#include "MRSyntax.hpp"

#include <cctype>
#include <string_view>

namespace {

const char *const kBasicKeywords[] = {
	"ABS", "AND", "AS", "ASC", "BEEP", "BYREF", "BYVAL", "CALL", "CASE", "CLASS", "CONST", "CONTINUE", "DATA", "DECLARE", "DEF", "DIM", "DO", "EACH", "ELSE", "ELSEIF", "END", "ENDIF", "ENUM", "ERASE",
	"ERROR", "EXIT", "FOR", "FUNCTION", "GOSUB", "GOTO", "IF", "IMP", "IN", "INPUT", "IS", "LET", "LINE", "LOCATE", "LOOP", "MOD", "NEXT", "NOT", "ON", "OPEN", "OPTION", "OR", "PRINT",
	"PRIVATE", "PROPERTY", "PUBLIC", "RANDOMIZE", "READ", "REDIM", "REM", "RESTORE", "RETURN", "SELECT", "SHARED", "STATIC", "STEP", "STOP", "STRUCT", "SUB", "THEN", "TO", "TRY", "TYPE", "UNTIL", "USING",
	"WEND", "WHILE", "WITH", "XOR"
};

const char *const kBasicTypeKeywords[] = {
	"ANY", "BOOLEAN", "BYTE", "DOUBLE", "INTEGER", "LONG", "LONGINT", "LONGPTR", "OBJECT", "POINTER", "SHORT", "SINGLE", "STRING", "UBYTE", "UINTEGER", "ULONG", "ULONGINT", "USHORT",
	"VARIANT", "WSTRING", "_BIT", "_BYTE", "_FLOAT", "_INTEGER", "_MEM", "_OFFSET", "_UNSIGNED"
};

const char *const kBasicConstants[] = {
	"FALSE", "NULL", "NOTHING", "PI", "TRUE"
};

bool basicIdentifierStart(char ch) noexcept {
	const unsigned char value = static_cast<unsigned char>(ch);
	return std::isalpha(value) != 0 || ch == '_';
}

bool basicIdentifierChar(char ch) noexcept {
	const unsigned char value = static_cast<unsigned char>(ch);
	return std::isalnum(value) != 0 || ch == '_';
}

bool basicWordEquals(std::string_view word, const char *candidate) noexcept {
	std::size_t index = 0;

	for (; index < word.size() && candidate[index] != '\0'; ++index)
		if (std::toupper(static_cast<unsigned char>(word[index])) != static_cast<unsigned char>(candidate[index])) return false;
	return index == word.size() && candidate[index] == '\0';
}

bool basicWordInList(std::string_view word, const char *const words[], std::size_t wordCount) noexcept {
	for (std::size_t index = 0; index < wordCount; ++index)
		if (basicWordEquals(word, words[index])) return true;
	return false;
}

void appendBasicRun(std::vector<MRSyntaxTokenRun> &runs, std::size_t start, std::size_t end, MRSyntaxToken token) {
	if (end <= start) return;
	runs.push_back(MRSyntaxTokenRun(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(end - start), token));
}

bool basicStatementStart(std::string_view line, std::size_t column) noexcept {
	while (column > 0) {
		--column;
		if (line[column] == ':') return true;
		if (line[column] != ' ' && line[column] != '\t') return false;
	}
	return true;
}

bool basicDirectiveStart(std::string_view line, std::size_t column) noexcept {
	while (column > 0) {
		--column;
		if (line[column] != ' ' && line[column] != '\t') return false;
	}
	return true;
}

std::size_t consumeBasicNumber(std::string_view line, std::size_t start) noexcept {
	std::size_t index = start;

	if (index + 2 <= line.size() && line[index] == '&' && (line[index + 1] == 'H' || line[index + 1] == 'h' || line[index + 1] == 'O' || line[index + 1] == 'o' || line[index + 1] == 'B' || line[index + 1] == 'b')) {
		index += 2;
		while (index < line.size() && std::isxdigit(static_cast<unsigned char>(line[index])) != 0)
			++index;
		return index;
	}
	while (index < line.size() && std::isdigit(static_cast<unsigned char>(line[index])) != 0)
		++index;
	if (index < line.size() && line[index] == '.') {
		++index;
		while (index < line.size() && std::isdigit(static_cast<unsigned char>(line[index])) != 0)
			++index;
	}
	if (index < line.size() && (line[index] == 'E' || line[index] == 'e')) {
		const std::size_t exponent = index++;
		if (index < line.size() && (line[index] == '+' || line[index] == '-')) ++index;
		const std::size_t digits = index;
		while (index < line.size() && std::isdigit(static_cast<unsigned char>(line[index])) != 0)
			++index;
		if (digits == index) index = exponent;
	}
	while (index < line.size() && (line[index] == '!' || line[index] == '#' || line[index] == '%' || line[index] == '&'))
		++index;
	return index;
}

bool basicDelimiter(char ch) noexcept {
	switch (ch) {
		case '(':
		case ')':
		case '[':
		case ']':
		case '{':
		case '}':
		case ',':
		case '.':
		case ':':
		case ';':
		case '=':
		case '+':
		case '-':
		case '*':
		case '/':
		case '\\':
		case '^':
		case '<':
		case '>':
		case '&':
		case '|':
		case '!':
		case '?':
			return true;
		default:
			return false;
	}
}

} // namespace

MRSyntaxLineResult MRBasicSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	bool expectTypeName = false;
	bool expectCallableName = false;
	std::size_t index = 0;

	static_cast<void>(previousState);
	result.stateOut = MRSyntaxLineState();
	while (index < line.size()) {
		if (line[index] == '\'') {
			appendBasicRun(result.tokenRuns, index, line.size(), MRSyntaxToken::Comment);
			break;
		}
		if ((line[index] == '#' || line[index] == '$') && basicDirectiveStart(line, index)) {
			appendBasicRun(result.tokenRuns, index, line.size(), MRSyntaxToken::Directive);
			break;
		}
		if (line[index] == '"') {
			const std::size_t start = index++;
			while (index < line.size()) {
				if (line[index] != '"') {
					++index;
					continue;
				}
				++index;
				if (index < line.size() && line[index] == '"') {
					++index;
					continue;
				}
				break;
			}
			appendBasicRun(result.tokenRuns, start, index, MRSyntaxToken::String);
			continue;
		}
		if (std::isdigit(static_cast<unsigned char>(line[index])) != 0 || (line[index] == '&' && index + 1 < line.size())) {
			const std::size_t start = index;
			const std::size_t end = consumeBasicNumber(line, start);
			if (end > start) {
				appendBasicRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
				index = end;
				continue;
			}
		}
		if (basicIdentifierStart(line[index])) {
			const std::size_t start = index++;
			while (index < line.size() && basicIdentifierChar(line[index]))
				++index;
			if (index < line.size() && (line[index] == '$' || line[index] == '%' || line[index] == '&' || line[index] == '!' || line[index] == '#')) ++index;
			const std::string_view word = line.substr(start, index - start);
			if (basicWordEquals(word, "REM") && basicStatementStart(line, start)) {
				appendBasicRun(result.tokenRuns, start, line.size(), MRSyntaxToken::Comment);
				break;
			}
			if (basicWordInList(word, kBasicKeywords, sizeof(kBasicKeywords) / sizeof(kBasicKeywords[0]))) {
				appendBasicRun(result.tokenRuns, start, index, MRSyntaxToken::Keyword);
				expectTypeName = basicWordEquals(word, "TYPE") || basicWordEquals(word, "ENUM");
				expectCallableName = basicWordEquals(word, "SUB") || basicWordEquals(word, "FUNCTION") || basicWordEquals(word, "PROPERTY") || basicWordEquals(word, "CONSTRUCTOR") || basicWordEquals(word, "DESTRUCTOR");
				continue;
			}
			if (basicWordInList(word, kBasicTypeKeywords, sizeof(kBasicTypeKeywords) / sizeof(kBasicTypeKeywords[0]))) {
				appendBasicRun(result.tokenRuns, start, index, MRSyntaxToken::Type);
				continue;
			}
			if (basicWordInList(word, kBasicConstants, sizeof(kBasicConstants) / sizeof(kBasicConstants[0]))) {
				appendBasicRun(result.tokenRuns, start, index, MRSyntaxToken::Key);
				continue;
			}
			if (expectTypeName) {
				appendBasicRun(result.tokenRuns, start, index, MRSyntaxToken::Type);
				expectTypeName = false;
				continue;
			}
			if (expectCallableName) {
				appendBasicRun(result.tokenRuns, start, index, MRSyntaxToken::Key);
				expectCallableName = false;
			}
			continue;
		}
		if (basicDelimiter(line[index])) appendBasicRun(result.tokenRuns, index, index + 1, MRSyntaxToken::Delimiter);
		++index;
	}
	return result;
}
