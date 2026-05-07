#include "MRSyntax.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cctype>
#include <cstring>
#include <string_view>

namespace {

constexpr std::uint16_t kSyntaxFlagCLanguage = 0x0001;
constexpr std::uint16_t kSyntaxFlagTripleQuoted = 0x0002;
constexpr std::uint16_t kSyntaxFlagHereDocStripTabs = 0x0004;
constexpr std::uint16_t kSyntaxFlagPayloadLengthShift = 8;
constexpr std::uint16_t kSyntaxFlagPayloadLengthMask = 0xFF00;

const char *const kMrmacKeywords[] = {
	"IF", "THEN", "ELSE", "END", "WHILE", "DO", "TVCALL", "CALL", "RET", "GOTO", "TO", "FROM", "TRANS", "DUMP", "PERM", "AND", "OR", "NOT", "SHL", "SHR", "MOD"
};

const char *const kMrmacTypeKeywords[] = {
	"DEF_INT", "DEF_STR", "DEF_CHAR", "DEF_REAL"
};

const char *const kMrmacDirectives[] = {
	"$MACRO", "SMACRO", "SMACRO_FILE", "$MACRO_FILE", "END_MACRO"
};

const char *const kCKeywords[] = {
	"auto", "break", "case", "const", "continue", "default", "do", "else", "enum", "extern", "for", "goto", "if", "inline", "register", "restrict", "return", "sizeof", "static", "struct", "switch", "typedef", "union",
	"volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local"
};

const char *const kCppKeywords[] = {
	"alignas", "alignof", "asm", "auto", "break", "case", "catch", "class", "const", "consteval", "constexpr", "constinit", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do", "dynamic_cast", "else", "enum",
	"explicit", "export", "extern", "final", "for", "friend", "goto", "if", "inline", "mutable", "namespace", "new", "noexcept", "operator", "override", "private", "protected", "public", "register", "reinterpret_cast", "requires", "return", "sizeof",
	"static", "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "try", "typedef", "typeid", "typename", "union", "using", "virtual", "volatile", "while"
};

const char *const kCTypeKeywords[] = {
	"char", "double", "float", "int", "long", "short", "signed", "unsigned", "void"
};

const char *const kCppTypeKeywords[] = {
	"bool", "char", "char8_t", "char16_t", "char32_t", "double", "float", "int", "long", "short", "signed", "unsigned", "void", "wchar_t"
};

const char *const kCppConstants[] = {
	"false", "nullptr", "NULL", "true"
};

const char *const kJavaScriptKeywords[] = {
	"as", "async", "await", "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "export", "extends", "finally", "for", "from",
	"function", "get", "if", "import", "in", "instanceof", "let", "new", "of", "return", "set", "static", "super", "switch", "this", "throw", "try", "typeof", "var", "void", "while",
	"with", "yield"
};

const char *const kJavaScriptConstants[] = {
	"false", "Infinity", "NaN", "null", "true", "undefined"
};

const char *const kPythonKeywords[] = {
	"and", "as", "assert", "async", "await", "break", "case", "class", "continue", "def", "del", "elif", "else", "except", "exec", "finally", "for", "from", "global", "if", "import",
	"in", "is", "lambda", "match", "nonlocal", "not", "or", "pass", "print", "raise", "return", "try", "while", "with", "yield"
};

const char *const kPythonConstants[] = {
	"False", "None", "True"
};

const char *const kZshKeywords[] = {
	"alias",     "autoload", "break",   "case",   "continue", "coproc",   "do",      "done",    "elif",   "else",   "esac",   "eval",    "exec",     "export",
	"false",     "fi",       "float",   "for",    "function", "if",       "in",      "integer", "local",  "readonly", "repeat", "return",  "select",   "setopt",
	"then",      "time",     "true",    "typeset","unalias",  "unset",    "unsetopt","until",   "while"
};

const char *const kZshBuiltins[] = {
	"bindkey", "builtin", "cd", "command", "compdef", "compinit", "dirs", "disown", "echo", "emulate", "enable", "fc", "getopts", "hash", "jobs", "kill", "let", "popd", "print", "printf",
	"pushd", "pwd", "read", "rehash", "set", "shift", "source", "test", "trap", "type", "typeset", "ulimit", "umask", "unalias", "unfunction", "unset", "wait", "whence", "where", "which", "zcompile",
	"zformat", "zle", "zmodload"
};

const char *const kPerlKeywords[] = {
	"BEGIN",   "CHECK", "END",    "INIT",   "UNITCHECK", "and",    "cmp",    "continue", "default", "defined", "do",      "else",    "elsif", "eq",     "for",   "foreach",
	"ge",      "given", "goto",   "gt",     "if",        "last",   "le",     "local",    "lt",      "my",      "ne",      "next",    "no",    "our",    "package","redo",
	"require", "return","say",    "state",  "sub",       "undef",  "unless", "until",    "use",     "when",    "while",   "xor"
};

std::string lowerCopy(const std::string &value) {
	std::string result = value;
	for (char &i : result)
		i = static_cast<char>(std::tolower(static_cast<unsigned char>(i)));
	return result;
}

std::string fileNamePart(const std::string &value) {
	std::size_t pos = value.find_last_of("/\\");
	return pos == std::string::npos ? value : value.substr(pos + 1);
}

std::string extensionPart(const std::string &value) {
	std::string name = fileNamePart(value);
	std::size_t pos = name.find_last_of('.');
	return pos == std::string::npos ? std::string() : lowerCopy(name.substr(pos));
}

constexpr std::size_t kSyntaxLanguageCount = static_cast<std::size_t>(MRSyntaxLanguage::Markdown) + 1;

std::size_t syntaxLanguageIndex(MRSyntaxLanguage language) noexcept {
	return static_cast<std::size_t>(language);
}

void addClassificationScore(std::array<int, kSyntaxLanguageCount> &scores, MRSyntaxLanguage language, int delta) noexcept {
	scores[syntaxLanguageIndex(language)] += delta;
}

bool containsText(std::string_view haystack, std::string_view needle) noexcept {
	return !needle.empty() && haystack.find(needle) != std::string_view::npos;
}

int countMatches(std::string_view haystack, std::string_view needle, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	if (needle.empty()) return 0;
	while (count < maxCount) {
		pos = haystack.find(needle, pos);
		if (pos == std::string_view::npos) break;
		++count;
		pos += needle.size();
	}
	return count;
}

bool startsWithText(std::string_view text, std::string_view prefix) noexcept {
	return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string_view firstLineView(std::string_view text) noexcept {
	const std::size_t end = text.find('\n');
	return end == std::string_view::npos ? text : text.substr(0, end);
}

std::string_view classificationSample(std::string_view text) noexcept {
	constexpr std::size_t kMaxSampleBytes = 64 * 1024;
	return text.size() <= kMaxSampleBytes ? text : text.substr(0, kMaxSampleBytes);
}

std::string lowerCopyView(std::string_view value) {
	std::string result(value);
	for (char &ch : result)
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	return result;
}

std::string_view trimWhitespaceView(std::string_view text) noexcept {
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
	return text;
}

std::string_view nextLineView(std::string_view text, std::size_t &pos) noexcept {
	const std::size_t start = pos;
	const std::size_t end = text.find('\n', pos);

	if (end == std::string_view::npos) {
		pos = text.size();
		std::string_view line = text.substr(start);
		if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
		return line;
	}
	pos = end + 1;
	std::string_view line = text.substr(start, end - start);
	if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
	return line;
}

int countCharacter(std::string_view text, char needle, int maxCount = INT_MAX) noexcept {
	int count = 0;

	for (char ch : text) {
		if (ch != needle) continue;
		++count;
		if (count >= maxCount) break;
	}
	return count;
}

int countLinePrefixMatches(std::string_view text, std::string_view prefix, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	while (pos < text.size() && count < maxCount) {
		const std::string_view line = trimWhitespaceView(nextLineView(text, pos));
		if (startsWithText(line, prefix)) ++count;
	}
	return count;
}

bool isPythonBlockHeader(std::string_view line) noexcept {
	static const std::array<std::string_view, 15> prefixes = {
		"def ", "class ", "if ", "elif ", "else:", "for ", "while ", "with ", "try:", "except", "finally:", "async def ", "match ", "case ", "except*"
	};

	line = trimWhitespaceView(line);
	if (line.empty() || line.back() != ':') return false;
	for (std::string_view prefix : prefixes)
		if (startsWithText(line, prefix)) return true;
	return false;
}

int countPythonBlockHeaders(std::string_view text, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	while (pos < text.size() && count < maxCount)
		if (isPythonBlockHeader(nextLineView(text, pos))) ++count;
	return count;
}

int countJsonKeyLikeLines(std::string_view text, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	while (pos < text.size() && count < maxCount) {
		std::string_view line = trimWhitespaceView(nextLineView(text, pos));
		if (line.size() < 4 || line.front() != '"') continue;
		const std::size_t quoteEnd = line.find('"', 1);
		if (quoteEnd == std::string_view::npos) continue;
		const std::size_t colon = line.find(':', quoteEnd + 1);
		if (colon == std::string_view::npos) continue;
		++count;
	}
	return count;
}

int countShellAssignmentLines(std::string_view text, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	while (pos < text.size() && count < maxCount) {
		std::string_view line = trimWhitespaceView(nextLineView(text, pos));
		std::size_t eq = line.find('=');
		if (eq == std::string_view::npos || eq == 0) continue;
		if (line.find(' ') != std::string_view::npos && line.find(' ') < eq) continue;
		if (line.find('\t') != std::string_view::npos && line.find('\t') < eq) continue;
		if (!(std::isalpha(static_cast<unsigned char>(line.front())) || line.front() == '_')) continue;
		bool valid = true;
		for (std::size_t i = 1; i < eq; ++i)
			if (!(std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) {
				valid = false;
				break;
			}
		if (valid) ++count;
	}
	return count;
}

int countPerlSigilDeclLines(std::string_view text, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	while (pos < text.size() && count < maxCount) {
		const std::string_view line = trimWhitespaceView(nextLineView(text, pos));
		if (startsWithText(line, "my $") || startsWithText(line, "my @") || startsWithText(line, "my %") || startsWithText(line, "our $") || startsWithText(line, "our @") || startsWithText(line, "our %")) ++count;
	}
	return count;
}

bool isMakeTargetLikeLine(std::string_view line) noexcept {
	std::size_t colon = 0;
	bool seenTargetChar = false;

	line = trimWhitespaceView(line);
	if (line.empty() || line.front() == '#' || line.front() == '\t') return false;
	colon = line.find(':');
	if (colon == std::string_view::npos || colon == 0) return false;
	if (line.find("://") != std::string_view::npos) return false;
	if (colon + 1 < line.size() && line[colon + 1] == '=') return false;
	if (startsWithText(line, "case ") || startsWithText(line, "default:")) return false;
	for (std::size_t i = 0; i < colon; ++i) {
		const char ch = line[i];
		if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.' || ch == '/' || ch == '%' || ch == '$' || ch == '(' || ch == ')' || ch == '{' || ch == '}' || ch == '*' || ch == '+' || ch == '?' || ch == ' ') {
			if (ch != ' ') seenTargetChar = true;
			continue;
		}
		return false;
	}
	return seenTargetChar;
}

int countMakeTargetLikeLines(std::string_view text, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	while (pos < text.size() && count < maxCount)
		if (isMakeTargetLikeLine(nextLineView(text, pos))) ++count;
	return count;
}

int countRecipeTabLines(std::string_view text, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	while (pos < text.size() && count < maxCount) {
		const std::string_view line = nextLineView(text, pos);
		if (!line.empty() && line.front() == '\t') ++count;
	}
	return count;
}

int countMarkdownStructureLines(std::string_view text, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	while (pos < text.size() && count < maxCount) {
		const std::string_view line = trimWhitespaceView(nextLineView(text, pos));
		if (line.empty()) continue;
		if (startsWithText(line, "#") || startsWithText(line, ">") || startsWithText(line, "```") || startsWithText(line, "~~~") || startsWithText(line, "- ") || startsWithText(line, "* ") || startsWithText(line, "+ ") || startsWithText(line, "1. ") || containsText(line, "](") || containsText(line, "![") || containsText(line, "| ---")) ++count;
	}
	return count;
}

void paint(MRSyntaxTokenMap &tokens, std::size_t start, std::size_t end, MRSyntaxToken token) {
	if (start > tokens.size()) start = tokens.size();
	if (end > tokens.size()) end = tokens.size();
	for (std::size_t i = start; i < end; ++i)
		tokens[i] = token;
}

// Optimization: skipWhitespace replaces find_first_not_of(" \t").
// Iterating manually avoids the overhead of std::string::find_first_not_of
// which performs poorly in hot paths like syntax highlighting.
static std::size_t skipWhitespace(const std::string &text, std::size_t pos = 0) {
	while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
		++pos;
	return pos == text.size() ? std::string::npos : pos;
}

static bool isIdentifierStart(char ch) {
	unsigned char value = static_cast<unsigned char>(ch);
	return std::isalpha(value) || ch == '_';
}

static bool isIdentifierChar(char ch) {
	unsigned char value = static_cast<unsigned char>(ch);
	return std::isalnum(value) || ch == '_';
}

static bool isHexDigitChar(char ch) {
	return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

static bool isDecimalDigitChar(char ch) {
	return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

static bool isRawStringDelimiterChar(char ch) {
	unsigned char value = static_cast<unsigned char>(ch);
	return std::isalnum(value) || ch == '_';
}

static std::size_t skipWhitespaceView(std::string_view text, std::size_t pos = 0) {
	while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
		++pos;
	return pos;
}

static bool isMrmacDelimiterChar(char ch) {
	switch (ch) {
		case '(':
		case ')':
		case '[':
		case ']':
		case '<':
		case '>':
		case '}':
		case ',':
		case ';':
		case ':':
		case '.':
		case '=':
		case '+':
		case '-':
		case '*':
		case '/':
		case '%':
		case '@':
		case '#':
		case '!':
		case '?':
		case '&':
		case '|':
			return true;
		default:
			return false;
	}
}

static std::size_t mrmacDelimiterLength(std::string_view line, std::size_t pos) {
	if (pos + 1 >= line.size()) return 0;
	if ((line[pos] == '<' && line[pos + 1] == '=') || (line[pos] == '>' && line[pos + 1] == '=') || (line[pos] == '<' && line[pos + 1] == '>') || (line[pos] == ':' && line[pos + 1] == '=') ||
		(line[pos] == '.' && line[pos + 1] == '.') || (line[pos] == '&' && line[pos + 1] == '&') || (line[pos] == '|' && line[pos + 1] == '|'))
		return 2;
	return 0;
}

static bool isCppDelimiterChar(char ch) {
	switch (ch) {
		case '(':
		case ')':
		case '[':
		case ']':
		case '{':
		case '}':
		case ',':
		case ';':
		case ':':
		case '.':
		case '#':
		case '+':
		case '-':
		case '*':
		case '/':
		case '%':
		case '=':
		case '!':
		case '<':
		case '>':
		case '&':
		case '|':
		case '^':
		case '~':
		case '?':
			return true;
		default:
			return false;
	}
}

static std::size_t cppDelimiterLength(std::string_view line, std::size_t pos) {
	if (pos + 1 >= line.size()) return 0;
	const char ch0 = line[pos];
	const char ch1 = line[pos + 1];

	if ((ch0 == ':' && ch1 == ':') || (ch0 == '+' && ch1 == '+') || (ch0 == '-' && ch1 == '-') || (ch0 == '-' && ch1 == '>') || (ch0 == '&' && ch1 == '&') || (ch0 == '|' && ch1 == '|') || (ch0 == '=' && ch1 == '=') ||
		(ch0 == '!' && ch1 == '=') || (ch0 == '<' && ch1 == '=') || (ch0 == '>' && ch1 == '=') || (ch0 == '<' && ch1 == '<') || (ch0 == '>' && ch1 == '>') || (ch0 == '+' && ch1 == '=') || (ch0 == '-' && ch1 == '=') ||
		(ch0 == '*' && ch1 == '=') || (ch0 == '/' && ch1 == '=') || (ch0 == '%' && ch1 == '=') || (ch0 == '&' && ch1 == '=') || (ch0 == '|' && ch1 == '=') || (ch0 == '^' && ch1 == '=') || (ch0 == ':' && ch1 == '>'))
		return 2;
	if (pos + 2 < line.size()) {
		const char ch2 = line[pos + 2];
		if ((ch0 == '.' && ch1 == '.' && ch2 == '.') || (ch0 == '<' && ch1 == '<' && ch2 == '=') || (ch0 == '>' && ch1 == '>' && ch2 == '=') || (ch0 == '-' && ch1 == '>' && ch2 == '*'))
			return 3;
	}
	return 0;
}

static bool equalsUpperAscii(std::string_view text, const char *keyword) {
	std::size_t i = 0;
	for (; keyword[i] != '\0'; ++i) {
		if (i >= text.size()) return false;
		unsigned char ch = static_cast<unsigned char>(text[i]);
		if (std::toupper(ch) != keyword[i]) return false;
	}
	return i == text.size();
}

static bool mrmacWordInList(std::string_view word, const char *const *items, std::size_t count) {
	for (std::size_t i = 0; i < count; ++i)
		if (equalsUpperAscii(word, items[i])) return true;
	return false;
}

static bool wordInList(std::string_view word, const char *const *items, std::size_t count) {
	for (std::size_t i = 0; i < count; ++i)
		if (word == items[i]) return true;
	return false;
}

static bool isUpperCaseIdentifier(std::string_view word) {
	if (word.empty()) return false;

	bool sawUpper = false;
	for (std::size_t i = 0; i < word.size(); ++i) {
		const unsigned char ch = static_cast<unsigned char>(word[i]);
		if (std::isalpha(ch) != 0) {
			if (!std::isupper(ch)) return false;
			sawUpper = true;
			continue;
		}
		if (std::isdigit(ch) != 0 || word[i] == '_') continue;
		return false;
	}
	return sawUpper;
}

static bool isKStyleConstant(std::string_view word) {
	if (word.size() < 2 || word[0] != 'k') return false;
	return std::isupper(static_cast<unsigned char>(word[1])) != 0;
}

static bool isDeclarationIntroducer(std::string_view word) {
	return word == "class" || word == "struct" || word == "enum" || word == "union" || word == "namespace";
}

static bool isAliasIntroducer(std::string_view word) {
	return word == "using" || word == "typename";
}

static std::size_t previousNonWhitespaceIndex(std::string_view line, std::size_t pos) {
	while (pos > 0) {
		--pos;
		if (line[pos] != ' ' && line[pos] != '\t') return pos;
	}
	return std::string_view::npos;
}

static bool hasScopeQualifierBefore(std::string_view line, std::size_t start) {
	const std::size_t last = previousNonWhitespaceIndex(line, start);
	if (last == std::string_view::npos || line[last] != ':') return false;
	const std::size_t first = previousNonWhitespaceIndex(line, last);
	return first != std::string_view::npos && line[first] == ':';
}

static bool hasScopeQualifierAfter(std::string_view line, std::size_t end) {
	std::size_t next = skipWhitespaceView(line, end);
	return next + 1 < line.size() && line[next] == ':' && line[next + 1] == ':';
}

static bool isFunctionLikeIdentifier(std::string_view line, std::size_t start, std::size_t end) {
	std::size_t next = skipWhitespaceView(line, end);
	if (next >= line.size() || line[next] != '(') return false;
	if (start == 0) return true;

	std::size_t prev = start;
	while (prev > 0) {
		--prev;
		const char ch = line[prev];
		if (ch == ' ' || ch == '\t') continue;
		return ch != '#';
	}
	return true;
}

static bool endsWithPreprocessorContinuation(std::string_view line) {
	while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.remove_suffix(1);
	return !line.empty() && line.back() == '\\';
}

static std::uint32_t packRawStringDelimiter(std::string_view delimiter) {
	std::uint32_t payload = 0;
	const std::size_t limit = std::min<std::size_t>(delimiter.size(), 4);

	for (std::size_t i = 0; i < limit; ++i)
		payload |= static_cast<std::uint32_t>(static_cast<unsigned char>(delimiter[i])) << (i * 8);
	return payload;
}

static std::string unpackRawStringDelimiter(std::uint32_t payload, std::uint16_t flags) {
	std::string delimiter;
	const std::size_t length = (flags >> 8) & 0x0F;

	for (std::size_t i = 0; i < length; ++i)
		delimiter.push_back(static_cast<char>((payload >> (i * 8)) & 0xFF));
	return delimiter;
}

static bool findRawStringStart(std::string_view line, std::size_t pos, std::size_t &prefixEnd, std::string_view &delimiter) {
	std::size_t start = pos;
	std::size_t quotePos = start;

	if (line.compare(pos, 3, "u8R") == 0) quotePos = pos + 3;
	else if (line.compare(pos, 2, "uR") == 0 || line.compare(pos, 2, "UR") == 0 || line.compare(pos, 2, "LR") == 0) quotePos = pos + 2;
	else if (line.compare(pos, 1, "R") == 0)
		quotePos = pos + 1;
	else
		return false;

	if (quotePos >= line.size() || line[quotePos] != '"') return false;
	std::size_t delimiterStart = quotePos + 1;
	std::size_t openParen = delimiterStart;
	while (openParen < line.size() && isRawStringDelimiterChar(line[openParen])) ++openParen;
	if (openParen >= line.size() || line[openParen] != '(') return false;

	delimiter = line.substr(delimiterStart, openParen - delimiterStart);
	prefixEnd = openParen + 1;
	return true;
}

static std::size_t findRawStringTerminator(std::string_view line, std::size_t contentStart, std::string_view delimiter) {
	std::size_t i = contentStart;

	while (i < line.size()) {
		if (line[i] != ')') {
			++i;
			continue;
		}
		if (i + 1 + delimiter.size() >= line.size()) {
			++i;
			continue;
		}
		if (line.substr(i + 1, delimiter.size()) == delimiter && line[i + 1 + delimiter.size()] == '"') return i + 2 + delimiter.size();
		++i;
	}
	return std::string_view::npos;
}

static std::size_t consumeCppStringLiteral(std::string_view line, std::size_t start, char quote) {
	std::size_t i = start + 1;

	while (i < line.size()) {
		if (line[i] == '\\') {
			i += (i + 1 < line.size()) ? 2 : 1;
			continue;
		}
		if (line[i] == quote) {
			++i;
			break;
		}
		++i;
	}
	return i;
}

static std::size_t findStringContinuationEnd(std::string_view line, std::size_t start, char quote) {
	std::size_t i = start;

	while (i < line.size()) {
		if (line[i] == '\\') {
			i += (i + 1 < line.size()) ? 2 : 1;
			continue;
		}
		if (line[i] == quote) return i + 1;
		++i;
	}
	return line.size();
}

static std::size_t consumeCppNumber(std::string_view line, std::size_t start) {
	std::size_t i = start;
	bool sawDigits = false;

	if (i + 1 < line.size() && line[i] == '0' && (line[i + 1] == 'x' || line[i + 1] == 'X')) {
		i += 2;
		while (i < line.size() && (isHexDigitChar(line[i]) || line[i] == '\'')) ++i;
		while (i < line.size() && std::isalpha(static_cast<unsigned char>(line[i])) != 0) ++i;
		return i;
	}

	while (i < line.size() && (isDecimalDigitChar(line[i]) || line[i] == '\'')) {
		sawDigits = true;
		++i;
	}
	if (i < line.size() && line[i] == '.') {
		++i;
		while (i < line.size() && (isDecimalDigitChar(line[i]) || line[i] == '\'')) {
			sawDigits = true;
			++i;
		}
	}
	if (sawDigits && i < line.size() && (line[i] == 'e' || line[i] == 'E' || line[i] == 'p' || line[i] == 'P')) {
		std::size_t exponent = i + 1;
		if (exponent < line.size() && (line[exponent] == '+' || line[exponent] == '-')) ++exponent;
		if (exponent < line.size() && isDecimalDigitChar(line[exponent])) {
			i = exponent + 1;
			while (i < line.size() && isDecimalDigitChar(line[i])) ++i;
		}
	}
	while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) != 0 || line[i] == '_')) ++i;
	return i;
}

static std::size_t consumeJsonStringLiteral(std::string_view line, std::size_t start) {
	std::size_t i = start + 1;

	while (i < line.size()) {
		if (line[i] == '\\') {
			i += (i + 1 < line.size()) ? 2 : 1;
			continue;
		}
		if (line[i] == '"') {
			++i;
			break;
		}
		++i;
	}
	return i;
}

static std::size_t consumeJsonNumber(std::string_view line, std::size_t start) {
	std::size_t i = start;

	if (i < line.size() && line[i] == '-') ++i;
	if (i < line.size() && line[i] == '0') ++i;
	else {
		while (i < line.size() && isDecimalDigitChar(line[i])) ++i;
	}
	if (i < line.size() && line[i] == '.') {
		++i;
		while (i < line.size() && isDecimalDigitChar(line[i])) ++i;
	}
	if (i < line.size() && (line[i] == 'e' || line[i] == 'E')) {
		std::size_t exponent = i + 1;
		if (exponent < line.size() && (line[exponent] == '+' || line[exponent] == '-')) ++exponent;
		if (exponent < line.size() && isDecimalDigitChar(line[exponent])) {
			i = exponent + 1;
			while (i < line.size() && isDecimalDigitChar(line[i])) ++i;
		}
	}
	return i;
}

static bool isPythonStringPrefixLetter(char ch) {
	switch (std::tolower(static_cast<unsigned char>(ch))) {
		case 'b':
		case 'f':
		case 'r':
		case 'u':
			return true;
		default:
			return false;
	}
}

static bool findPythonStringStart(std::string_view line, std::size_t pos, std::size_t &contentStart, char &quote, bool &tripleQuoted) {
	std::size_t i = pos;

	if (i >= line.size()) return false;
	if (line[i] == '\'' || line[i] == '"') {
		quote = line[i];
		tripleQuoted = i + 2 < line.size() && line[i + 1] == quote && line[i + 2] == quote;
		contentStart = i + (tripleQuoted ? 3 : 1);
		return true;
	}
	if (!isPythonStringPrefixLetter(line[i])) return false;
	if (pos > 0 && isIdentifierChar(line[pos - 1])) return false;

	while (i < line.size() && isPythonStringPrefixLetter(line[i]))
		++i;
	if (i >= line.size() || (line[i] != '\'' && line[i] != '"')) return false;

	quote = line[i];
	tripleQuoted = i + 2 < line.size() && line[i + 1] == quote && line[i + 2] == quote;
	contentStart = i + (tripleQuoted ? 3 : 1);
	return true;
}

static std::size_t findTripleQuotedStringEnd(std::string_view line, std::size_t contentStart, char quote) {
	for (std::size_t i = contentStart; i + 2 < line.size(); ++i)
		if (line[i] == quote && line[i + 1] == quote && line[i + 2] == quote) return i + 3;
	return std::string_view::npos;
}

static std::size_t consumePythonNumber(std::string_view line, std::size_t start) {
	std::size_t i = start;

	if (i + 1 < line.size() && line[i] == '0' && (line[i + 1] == 'x' || line[i + 1] == 'X' || line[i + 1] == 'b' || line[i + 1] == 'B' || line[i + 1] == 'o' || line[i + 1] == 'O')) {
		i += 2;
		while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) != 0 || line[i] == '_')) ++i;
		return i;
	}

	while (i < line.size() && (isDecimalDigitChar(line[i]) || line[i] == '_'))
		++i;
	if (i < line.size() && line[i] == '.') {
		++i;
		while (i < line.size() && (isDecimalDigitChar(line[i]) || line[i] == '_')) ++i;
	}
	if (i < line.size() && (line[i] == 'e' || line[i] == 'E')) {
		std::size_t exponent = i + 1;
		if (exponent < line.size() && (line[exponent] == '+' || line[exponent] == '-')) ++exponent;
		if (exponent < line.size() && (isDecimalDigitChar(line[exponent]) || line[exponent] == '_')) {
			i = exponent + 1;
			while (i < line.size() && (isDecimalDigitChar(line[i]) || line[i] == '_')) ++i;
		}
	}
	if (i < line.size() && (line[i] == 'j' || line[i] == 'J')) ++i;
	return i;
}

static bool isJsonDelimiterChar(char ch) {
	switch (ch) {
		case '{':
		case '}':
		case '[':
		case ']':
		case ':':
		case ',':
			return true;
		default:
			return false;
	}
}

static bool isZshDelimiterChar(char ch) {
	switch (ch) {
		case '(':
		case ')':
		case '[':
		case ']':
		case '{':
		case '}':
		case '|':
		case '&':
		case ';':
		case '<':
		case '>':
			return true;
		default:
			return false;
	}
}

static bool isMarkdownRuleLine(std::string_view line, std::size_t start) {
	char marker = line[start];
	if (marker != '-' && marker != '*' && marker != '_') return false;
	std::size_t count = 0;
	for (std::size_t i = start; i < line.size(); ++i) {
		if (line[i] == marker) {
			++count;
			continue;
		}
		if (line[i] != ' ' && line[i] != '\t') return false;
	}
	return count >= 3;
}

static bool isMarkdownOrderedListStart(std::string_view line, std::size_t start, std::size_t &markerEnd) {
	std::size_t i = start;
	if (i >= line.size() || !isDecimalDigitChar(line[i])) return false;
	while (i < line.size() && isDecimalDigitChar(line[i]))
		++i;
	if (i >= line.size() || line[i] != '.') return false;
	++i;
	if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i]))) return false;
	markerEnd = i;
	return true;
}

static std::size_t consumeMarkdownCodeSpan(std::string_view line, std::size_t start) {
	std::size_t ticks = 0;
	while (start + ticks < line.size() && line[start + ticks] == '`')
		++ticks;
	if (ticks == 0) return start;
	for (std::size_t i = start + ticks; i < line.size();) {
		if (line[i] != '`') {
			++i;
			continue;
		}
		std::size_t run = 0;
		while (i + run < line.size() && line[i + run] == '`')
			++run;
		if (run == ticks) return i + run;
		i += run;
	}
	return line.size();
}

static bool isMarkdownTableSeparatorLine(std::string_view line, std::size_t start) {
	bool sawDash = false;
	for (std::size_t i = start; i < line.size(); ++i) {
		const char ch = line[i];
		if (ch == '-') {
			sawDash = true;
			continue;
		}
		if (ch == '|' || ch == ':' || ch == ' ' || ch == '\t') continue;
		return false;
	}
	return sawDash;
}

static std::size_t consumeZshStringLiteral(std::string_view line, std::size_t start, char quote) {
	std::size_t i = start + 1;
	while (i < line.size()) {
		if (line[i] == '\\' && quote != '\'' && i + 1 < line.size()) {
			i += 2;
			continue;
		}
		if (line[i] == quote) return i + 1;
		++i;
	}
	return line.size();
}

static bool isMarkdownSetextHeadingLine(std::string_view line, std::size_t start, char &marker) {
	if (start >= line.size()) return false;
	marker = line[start];
	if (marker != '=' && marker != '-') return false;
	std::size_t count = 0;
	for (std::size_t i = start; i < line.size(); ++i) {
		if (line[i] == marker) {
			++count;
			continue;
		}
		if (line[i] != ' ' && line[i] != '\t') return false;
	}
	return count >= 3;
}

static std::size_t findMarkdownMarkerEnd(std::string_view line, std::size_t start, std::string_view marker) {
	for (std::size_t i = start; i + marker.size() <= line.size(); ++i) {
		if (line[i] == '\\') {
			++i;
			continue;
		}
		if (line.substr(i, marker.size()) == marker) return i;
	}
	return std::string_view::npos;
}

static bool isMarkdownAutoLink(std::string_view line, std::size_t start, std::size_t end) {
	if (start >= end || end > line.size()) return false;
	std::string_view body = line.substr(start, end - start);
	return body.find("://") != std::string_view::npos || body.starts_with("mailto:");
}

static bool parseMarkdownReferenceDefinition(std::string_view line, std::size_t start, std::size_t &labelStart, std::size_t &labelEnd, std::size_t &urlStart, std::size_t &urlEnd) {
	if (start >= line.size() || line[start] != '[') return false;
	labelStart = start + 1;
	labelEnd = line.find(']', labelStart);
	if (labelEnd == std::string_view::npos || labelEnd + 1 >= line.size() || line[labelEnd + 1] != ':') return false;
	urlStart = skipWhitespaceView(line, labelEnd + 2);
	if (urlStart >= line.size()) return false;
	urlEnd = line.size();
	while (urlEnd > urlStart && std::isspace(static_cast<unsigned char>(line[urlEnd - 1])))
		--urlEnd;
	return urlEnd > urlStart;
}

static bool findMarkdownFence(std::string_view line, std::size_t &trimmed, char &marker, std::size_t &runLength) {
	trimmed = skipWhitespaceView(line);
	if (trimmed >= line.size()) return false;
	marker = line[trimmed];
	if (marker != '`' && marker != '~') return false;
	runLength = 0;
	while (trimmed + runLength < line.size() && line[trimmed + runLength] == marker)
		++runLength;
	return runLength >= 3;
}

static std::uint16_t storePayloadLength(std::uint16_t flags, std::size_t length) {
	flags &= static_cast<std::uint16_t>(~kSyntaxFlagPayloadLengthMask);
	flags |= static_cast<std::uint16_t>((std::min<std::size_t>(length, 255U) << kSyntaxFlagPayloadLengthShift) & kSyntaxFlagPayloadLengthMask);
	return flags;
}

static std::size_t payloadLength(std::uint16_t flags) {
	return static_cast<std::size_t>((flags & kSyntaxFlagPayloadLengthMask) >> kSyntaxFlagPayloadLengthShift);
}

static std::uint32_t hashSyntaxPayload(std::string_view text) {
	std::uint32_t hash = 2166136261u;
	for (char ch : text) {
		hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(ch));
		hash *= 16777619u;
	}
	return hash;
}

static std::string_view trimRightAsciiWhitespace(std::string_view text) {
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
		text.remove_suffix(1);
	return text;
}

static bool parseHereDocumentMarker(std::string_view line, std::size_t start, std::size_t &tokenEnd, std::string_view &label) {
	start = skipWhitespaceView(line, start);
	if (start >= line.size()) return false;

	if (line[start] == '\'' || line[start] == '"') {
		const char quote = line[start];
		std::size_t end = start + 1;
		while (end < line.size() && line[end] != quote)
			++end;
		if (end >= line.size()) return false;
		label = line.substr(start + 1, end - start - 1);
		tokenEnd = end + 1;
		return !label.empty();
	}

	std::size_t end = start;
	while (end < line.size()) {
		const char ch = line[end];
		if (std::isspace(static_cast<unsigned char>(ch)) || ch == ';' || ch == '|' || ch == '&' || ch == '<' || ch == '>' || ch == ')' || ch == '(') break;
		++end;
	}
	if (end == start) return false;
	label = line.substr(start, end - start);
	tokenEnd = end;
	return true;
}

static bool lineMatchesHereDocumentEnd(std::string_view line, const MRSyntaxLineState &state) {
	std::string_view candidate = trimRightAsciiWhitespace(line);
	if ((state.flags & kSyntaxFlagHereDocStripTabs) != 0) {
		while (!candidate.empty() && candidate.front() == '\t')
			candidate.remove_prefix(1);
	}
	if (candidate.size() != payloadLength(state.flags)) return false;
	return hashSyntaxPayload(candidate) == state.payload;
}

static bool parseZshHereDocumentStart(std::string_view line, std::size_t &operatorStart, std::size_t &operatorEnd, std::string_view &label, bool &stripTabs) {
	for (std::size_t i = 0; i + 1 < line.size(); ++i) {
		if (line[i] == '\\' && i + 1 < line.size()) {
			++i;
			continue;
		}
		if (line[i] == '#') break;
		if (line[i] == '<' && line[i + 1] == '<') {
			operatorStart = i;
			operatorEnd = i + 2;
			stripTabs = false;
			if (operatorEnd < line.size() && line[operatorEnd] == '<') continue;
			if (operatorEnd < line.size() && line[operatorEnd] == '-') {
				stripTabs = true;
				++operatorEnd;
			}
			std::size_t tokenEnd = operatorEnd;
			return parseHereDocumentMarker(line, operatorEnd, tokenEnd, label);
		}
	}
	return false;
}

static bool parsePerlHereDocumentStart(std::string_view line, std::size_t &operatorStart, std::size_t &operatorEnd, std::string_view &label) {
	for (std::size_t i = 0; i + 1 < line.size(); ++i) {
		if (line[i] == '#') break;
		if (line[i] == '\'' || line[i] == '"' || line[i] == '`') {
			const std::size_t end = findStringContinuationEnd(line, i + 1, line[i]);
			if (end == line.size()) return false;
			i = end - 1;
			continue;
		}
		if (line[i] == '<' && line[i + 1] == '<') {
			operatorStart = i;
			operatorEnd = i + 2;
			std::size_t tokenEnd = operatorEnd;
			return parseHereDocumentMarker(line, operatorEnd, tokenEnd, label);
		}
	}
	return false;
}

static char pairedClosingDelimiter(char open) {
	switch (open) {
		case '(':
			return ')';
		case '[':
			return ']';
		case '{':
			return '}';
		case '<':
			return '>';
		default:
			return '\0';
	}
}

static bool isZshCommandBoundaryChar(char ch) {
	return ch == ';' || ch == '|' || ch == '&' || ch == '(' || ch == ')' || ch == '{' || ch == '}';
}

static bool isZshCommentStart(std::string_view line, std::size_t pos) {
	if (pos >= line.size() || line[pos] != '#') return false;
	if (pos == 0) return true;
	const char previous = line[pos - 1];
	return std::isspace(static_cast<unsigned char>(previous)) || isZshCommandBoundaryChar(previous);
}

static std::size_t zshDelimiterLength(std::string_view line, std::size_t pos) {
	if (pos + 1 >= line.size()) return 0;
	const char ch0 = line[pos];
	const char ch1 = line[pos + 1];

	if ((ch0 == '&' && ch1 == '&') || (ch0 == '|' && ch1 == '|') || (ch0 == '<' && ch1 == '<') || (ch0 == '>' && ch1 == '>') || (ch0 == '<' && ch1 == '&') || (ch0 == '>' && ch1 == '&') || (ch0 == '>' && ch1 == '|') ||
	    (ch0 == '[' && ch1 == '[') || (ch0 == ']' && ch1 == ']') || (ch0 == '(' && ch1 == '(') || (ch0 == ')' && ch1 == ')') || (ch0 == ';' && ch1 == ';'))
		return 2;
	if (pos + 2 < line.size()) {
		const char ch2 = line[pos + 2];
		if ((ch0 == '<' && ch1 == '<' && ch2 == '<') || (ch0 == ';' && ch1 == '&' && ch2 == ';') || (ch0 == ';' && ch1 == '|' && ch2 == ';')) return 3;
	}
	return 0;
}

static std::size_t consumeBalancedRegion(std::string_view line, std::size_t start, std::string_view open, std::string_view close) {
	if (start + open.size() > line.size() || line.substr(start, open.size()) != open) return start;
	std::size_t i = start + open.size();
	int depth = 1;
	while (i < line.size()) {
		if (line[i] == '\\' && i + 1 < line.size()) {
			i += 2;
			continue;
		}
		if (i + open.size() <= line.size() && line.substr(i, open.size()) == open) {
			++depth;
			i += open.size();
			continue;
		}
		if (i + close.size() <= line.size() && line.substr(i, close.size()) == close) {
			--depth;
			i += close.size();
			if (depth == 0) return i;
			continue;
		}
		++i;
	}
	return line.size();
}

static bool isZshAssignmentWord(std::string_view line, std::size_t start, std::size_t end) {
	if (start >= end || end > line.size()) return false;
	const std::size_t eq = line.find('=', start);
	if (eq == std::string_view::npos || eq >= end) return false;
	if (eq == start || !isIdentifierStart(line[start])) return false;
	for (std::size_t i = start + 1; i < eq; ++i)
		if (!isIdentifierChar(line[i])) return false;
	return true;
}

static bool isPerlDelimiterChar(char ch) {
	switch (ch) {
		case '(':
		case ')':
		case '[':
		case ']':
		case '{':
		case '}':
		case ',':
		case ';':
		case ':':
		case '=':
		case '+':
		case '-':
		case '*':
		case '/':
		case '%':
		case '&':
		case '|':
		case '<':
		case '>':
			return true;
		default:
			return false;
	}
}

static bool isPerlVariableSigil(char ch) {
	return ch == '$' || ch == '@' || ch == '%' || ch == '&' || ch == '*';
}

static bool isPerlPackageChar(char ch) {
	return isIdentifierChar(ch) || ch == ':';
}

static std::size_t consumePerlVariable(std::string_view line, std::size_t start) {
	std::size_t i = start + 1;
	if (i >= line.size()) return i;
	if (isDecimalDigitChar(line[i])) {
		while (i < line.size() && isDecimalDigitChar(line[i]))
			++i;
		return i;
	}
	if (isIdentifierStart(line[i])) {
		++i;
		while (i < line.size() && isIdentifierChar(line[i]))
			++i;
		return i;
	}
	if (std::strchr("_!@?$#*-.^&`'+=~|/,:;<>[](){}", line[i]) != nullptr) return i + 1;
	return i;
}

static std::size_t consumePerlDelimitedSegment(std::string_view line, std::size_t start) {
	if (start >= line.size()) return line.size();
	const char open = line[start];
	const char paired = pairedClosingDelimiter(open);
	const char close = paired != '\0' ? paired : open;
	std::size_t i = start + 1;
	int depth = paired != '\0' ? 1 : 0;

	while (i < line.size()) {
		if (line[i] == '\\' && i + 1 < line.size()) {
			i += 2;
			continue;
		}
		if (paired != '\0' && line[i] == open) {
			++depth;
			++i;
			continue;
		}
		if (line[i] == close) {
			++i;
			if (paired == '\0') return i;
			--depth;
			if (depth == 0) return i;
			continue;
		}
		++i;
	}
	return line.size();
}

static std::size_t consumePerlDelimitedLiteral(std::string_view line, std::size_t start, std::size_t prefixLength) {
	if (start + prefixLength >= line.size()) return line.size();
	std::size_t i = consumePerlDelimitedSegment(line, start + prefixLength);
	if (i == line.size()) return i;

	const std::string_view prefix = line.substr(start, prefixLength);
	if (prefix == "s" || prefix == "tr" || prefix == "y") i = consumePerlDelimitedSegment(line, i);
	while (i < line.size() && std::isalpha(static_cast<unsigned char>(line[i])) != 0)
		++i;
	return i;
}

static std::size_t perlLiteralPrefixLength(std::string_view line, std::size_t pos) {
	static const char *const prefixes[] = {"qq", "qw", "qx", "qr", "tr", "q", "m", "s", "y"};
	for (const char *prefix : prefixes) {
		std::string_view value(prefix);
		if (pos + value.size() >= line.size()) continue;
		if (line.substr(pos, value.size()) != value) continue;
		if (isIdentifierChar(line[pos + value.size()])) continue;
		return value.size();
	}
	return 0;
}

static bool isPerlPodDirectiveStart(std::string_view line, std::size_t trimmed) {
	if (trimmed >= line.size() || line[trimmed] != '=') return false;
	if (trimmed + 1 >= line.size() || !std::isalpha(static_cast<unsigned char>(line[trimmed + 1]))) return false;
	return true;
}

static void appendRun(std::vector<MRSyntaxTokenRun> &runs, std::size_t start, std::size_t end, MRSyntaxToken token) {
	if (end <= start) return;
	if (!runs.empty()) {
		MRSyntaxTokenRun &tail = runs.back();
		std::size_t tailEnd = static_cast<std::size_t>(tail.column) + static_cast<std::size_t>(tail.length);
		if (tail.token == token && tailEnd == start) {
			tail.length += static_cast<std::uint32_t>(end - start);
			return;
		}
	}
	runs.push_back(MRSyntaxTokenRun(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(end - start), token));
}

static MRSyntaxTokenMap tokenMapFromRuns(std::size_t length, const std::vector<MRSyntaxTokenRun> &runs) {
	MRSyntaxTokenMap tokens(length, MRSyntaxToken::Text);
	for (std::size_t i = 0; i < runs.size(); ++i) {
		std::size_t start = std::min<std::size_t>(runs[i].column, tokens.size());
		std::size_t end = std::min<std::size_t>(start + runs[i].length, tokens.size());
		for (std::size_t pos = start; pos < end; ++pos)
			tokens[pos] = runs[i].token;
	}
	return tokens;
}

void tokenizeMake(MRSyntaxTokenMap &tokens, const std::string &line) {
	std::size_t trimmed = skipWhitespace(line);
	if (trimmed != std::string::npos && line[trimmed] == '#') {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Comment);
		return;
	}

	if (trimmed != std::string::npos && line[trimmed] != '\t') {
		std::size_t colon = line.find(':', trimmed);
		std::size_t eq = line.find('=', trimmed);
		if (colon != std::string::npos && (eq == std::string::npos || colon < eq)) paint(tokens, trimmed, colon, MRSyntaxToken::Key);
		else if (eq != std::string::npos)
			paint(tokens, trimmed, eq, MRSyntaxToken::Directive);
	}

	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '#') {
			paint(tokens, i, line.size(), MRSyntaxToken::Comment);
			break;
		}
		if (line[i] == '$' && i + 1 < line.size() && (line[i + 1] == '(' || line[i + 1] == '{')) {
			char closer = line[i + 1] == '(' ? ')' : '}';
			std::size_t start = i;
			i += 2;
			while (i < line.size() && line[i] != closer)
				++i;
			if (i < line.size()) ++i;
			paint(tokens, start, i, MRSyntaxToken::Directive);
			continue;
		}
		++i;
	}
}


void tokenizeMarkdown(MRSyntaxTokenMap &tokens, const std::string &line) {
	std::size_t trimmed = skipWhitespace(line);
	if (trimmed == std::string::npos) return;
	if (line.compare(trimmed, 4, "<!--") == 0) {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Comment);
		return;
	}
	if (line.compare(trimmed, 3, "```") == 0 || line.compare(trimmed, 3, "~~~") == 0) {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Directive);
		return;
	}
	{
		char marker = '\0';
		if (isMarkdownSetextHeadingLine(line, trimmed, marker)) {
			paint(tokens, trimmed, line.size(), marker == '=' ? MRSyntaxToken::Heading : MRSyntaxToken::Section);
			return;
		}
	}
	if (isMarkdownTableSeparatorLine(line, trimmed)) {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Section);
		return;
	}
	if (isMarkdownRuleLine(line, trimmed)) {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Section);
		return;
	}
	{
		std::size_t labelStart = 0;
		std::size_t labelEnd = 0;
		std::size_t urlStart = 0;
		std::size_t urlEnd = 0;
		if (parseMarkdownReferenceDefinition(line, trimmed, labelStart, labelEnd, urlStart, urlEnd)) {
			paint(tokens, trimmed, trimmed + 1, MRSyntaxToken::Delimiter);
			paint(tokens, labelStart, labelEnd, MRSyntaxToken::Key);
			paint(tokens, labelEnd, labelEnd + 2, MRSyntaxToken::Delimiter);
			paint(tokens, urlStart, urlEnd, MRSyntaxToken::String);
			return;
		}
	}
	if (line[trimmed] == '#') {
		std::size_t i = trimmed;
		while (i < line.size() && line[i] == '#')
			++i;
		if (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
			paint(tokens, trimmed, line.size(), MRSyntaxToken::Heading);
			return;
		}
	}
	if (line[trimmed] == '>') {
		std::size_t i = trimmed + 1;
		while (i < line.size() && line[i] == '>')
			++i;
		paint(tokens, trimmed, i, MRSyntaxToken::Delimiter);
		if (i < line.size() && line[i] == ' ') ++i;
	} else if ((line[trimmed] == '-' || line[trimmed] == '*' || line[trimmed] == '+') && trimmed + 1 < line.size() && std::isspace(static_cast<unsigned char>(line[trimmed + 1]))) {
		paint(tokens, trimmed, trimmed + 1, MRSyntaxToken::Delimiter);
		if (trimmed + 5 <= line.size() && line[trimmed + 2] == '[' && line[trimmed + 4] == ']') {
			paint(tokens, trimmed + 2, trimmed + 5, MRSyntaxToken::Directive);
			if (line[trimmed + 3] == 'x' || line[trimmed + 3] == 'X') paint(tokens, trimmed + 3, trimmed + 4, MRSyntaxToken::Key);
		}
	} else {
		std::size_t orderedEnd = 0;
		if (isMarkdownOrderedListStart(line, trimmed, orderedEnd)) paint(tokens, trimmed, orderedEnd, MRSyntaxToken::Delimiter);
	}
	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '\\' && i + 1 < line.size()) {
			paint(tokens, i, i + 2, MRSyntaxToken::Delimiter);
			i += 2;
			continue;
		}
		if (i + 4 <= line.size() && line.compare(i, 4, "<!--") == 0) {
			std::size_t end = line.find("-->", i + 4);
			end = end == std::string::npos ? line.size() : end + 3;
			paint(tokens, i, end, MRSyntaxToken::Comment);
			i = end;
			continue;
		}
		if (line[i] == '`') {
			const std::size_t start = i;
			const std::size_t end = consumeMarkdownCodeSpan(line, i);
			paint(tokens, start, end, MRSyntaxToken::String);
			i = end;
			continue;
		}
		if (line[i] == '!' && i + 1 < line.size() && line[i + 1] == '[') {
			const std::size_t imageStart = i;
			std::size_t altEnd = line.find(']', i + 2);
			if (altEnd != std::string::npos && altEnd + 1 < line.size() && line[altEnd + 1] == '(') {
				std::size_t urlEnd = altEnd + 2;
				while (urlEnd < line.size() && line[urlEnd] != ')')
					++urlEnd;
				if (urlEnd < line.size()) {
					paint(tokens, imageStart, imageStart + 2, MRSyntaxToken::Delimiter);
					paint(tokens, imageStart + 2, altEnd, MRSyntaxToken::Key);
					paint(tokens, altEnd, altEnd + 2, MRSyntaxToken::Delimiter);
					paint(tokens, altEnd + 2, urlEnd, MRSyntaxToken::String);
					paint(tokens, urlEnd, urlEnd + 1, MRSyntaxToken::Delimiter);
					i = urlEnd + 1;
					continue;
				}
			}
			if (altEnd != std::string::npos && altEnd + 1 < line.size() && line[altEnd + 1] == '[') {
				std::size_t refEnd = line.find(']', altEnd + 2);
				if (refEnd != std::string::npos) {
					paint(tokens, imageStart, imageStart + 2, MRSyntaxToken::Delimiter);
					paint(tokens, imageStart + 2, altEnd, MRSyntaxToken::Key);
					paint(tokens, altEnd, altEnd + 2, MRSyntaxToken::Delimiter);
					paint(tokens, altEnd + 2, refEnd, MRSyntaxToken::Directive);
					paint(tokens, refEnd, refEnd + 1, MRSyntaxToken::Delimiter);
					i = refEnd + 1;
					continue;
				}
			}
		}
		if (line[i] == '[') {
			const std::size_t textStart = i;
			std::size_t textEnd = i + 1;
			while (textEnd < line.size() && line[textEnd] != ']')
				++textEnd;
			if (textEnd + 1 < line.size() && line[textEnd] == ']' && line[textEnd + 1] == '(') {
				std::size_t urlEnd = textEnd + 2;
				while (urlEnd < line.size() && line[urlEnd] != ')')
					++urlEnd;
				if (urlEnd < line.size()) {
					paint(tokens, textStart, textStart + 1, MRSyntaxToken::Delimiter);
					paint(tokens, textStart + 1, textEnd, MRSyntaxToken::Key);
					paint(tokens, textEnd, textEnd + 2, MRSyntaxToken::Delimiter);
					paint(tokens, textEnd + 2, urlEnd, MRSyntaxToken::String);
					paint(tokens, urlEnd, urlEnd + 1, MRSyntaxToken::Delimiter);
					i = urlEnd + 1;
					continue;
				}
			}
			if (textEnd < line.size() && textEnd + 1 < line.size() && line[textEnd + 1] == '[') {
				std::size_t refEnd = line.find(']', textEnd + 2);
				if (refEnd != std::string::npos) {
					paint(tokens, textStart, textStart + 1, MRSyntaxToken::Delimiter);
					paint(tokens, textStart + 1, textEnd, MRSyntaxToken::Key);
					paint(tokens, textEnd, textEnd + 2, MRSyntaxToken::Delimiter);
					if (refEnd > textEnd + 2) paint(tokens, textEnd + 2, refEnd, MRSyntaxToken::Directive);
					paint(tokens, refEnd, refEnd + 1, MRSyntaxToken::Delimiter);
					i = refEnd + 1;
					continue;
				}
			}
		}
		if (line[i] == '<') {
			std::size_t end = i + 1;
			while (end < line.size() && line[end] != '>')
				++end;
			if (end < line.size() && isMarkdownAutoLink(line, i + 1, end)) {
				paint(tokens, i, i + 1, MRSyntaxToken::Delimiter);
				paint(tokens, i + 1, end, MRSyntaxToken::String);
				paint(tokens, end, end + 1, MRSyntaxToken::Delimiter);
				i = end + 1;
				continue;
			}
		}
		if (line[i] == '|' || line[i] == ':') {
			paint(tokens, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}
		for (std::string_view marker : {std::string_view("***"), std::string_view("___"), std::string_view("**"), std::string_view("__"), std::string_view("~~"), std::string_view("*"), std::string_view("_")}) {
			if (i + marker.size() > line.size() || line.substr(i, marker.size()) != marker) continue;
			const std::size_t end = findMarkdownMarkerEnd(line, i + marker.size(), marker);
			if (end == std::string_view::npos || end == i + marker.size()) continue;
			paint(tokens, i, i + marker.size(), MRSyntaxToken::Delimiter);
			paint(tokens, i + marker.size(), end, MRSyntaxToken::String);
			paint(tokens, end, end + marker.size(), MRSyntaxToken::Delimiter);
			i = end + marker.size();
			goto markdown_continue;
		}
		++i;
markdown_continue:
		continue;
	}
}

void tokenizeZsh(MRSyntaxTokenMap &tokens, const std::string &line) {
	std::size_t trimmed = skipWhitespace(line);
	if (trimmed != std::string::npos && line.compare(trimmed, 2, "#!") == 0) {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Directive);
		return;
	}

	bool expectCommand = true;
	for (std::size_t i = 0; i < line.size();) {
		if (isZshCommentStart(line, i)) {
			paint(tokens, i, line.size(), MRSyntaxToken::Comment);
			break;
		}
		if (line[i] == '\'' || line[i] == '"' || line[i] == '`') {
			const std::size_t start = i;
			const std::size_t end = consumeZshStringLiteral(line, i, line[i]);
			paint(tokens, start, end, MRSyntaxToken::String);
			i = end;
			expectCommand = false;
			continue;
		}
		if (line[i] == '$') {
			const std::size_t start = i;
			if (i + 2 < line.size() && line[i + 1] == '(' && line[i + 2] == '(') {
				const std::size_t end = consumeBalancedRegion(line, i + 1, "((", "))");
				paint(tokens, start, end, MRSyntaxToken::Directive);
				i = end;
				expectCommand = false;
				continue;
			}
			if (i + 1 < line.size() && line[i + 1] == '(') {
				const std::size_t end = consumeBalancedRegion(line, i + 1, "(", ")");
				paint(tokens, start, end, MRSyntaxToken::Directive);
				i = end;
				expectCommand = false;
				continue;
			}
			++i;
			if (i < line.size() && line[i] == '{') {
				const std::size_t end = consumeBalancedRegion(line, i, "{", "}");
				paint(tokens, start, end, MRSyntaxToken::Directive);
				i = end;
				expectCommand = false;
				continue;
			}
			if (i < line.size() && (isIdentifierStart(line[i]) || isDecimalDigitChar(line[i]) || line[i] == '@' || line[i] == '*' || line[i] == '#' || line[i] == '?' || line[i] == '!' || line[i] == '$' || line[i] == '-')) {
				++i;
				while (i < line.size() && isIdentifierChar(line[i]))
					++i;
				paint(tokens, start, i, MRSyntaxToken::Directive);
				expectCommand = false;
				continue;
			}
			paint(tokens, start, i, MRSyntaxToken::Delimiter);
			expectCommand = false;
			continue;
		}
		if ((line[i] == '<' || line[i] == '>') && i + 1 < line.size() && line[i + 1] == '(') {
			const std::size_t end = consumeBalancedRegion(line, i + 1, "(", ")");
			paint(tokens, i, end, MRSyntaxToken::Directive);
			i = end;
			expectCommand = false;
			continue;
		}
		if (line[i] == '\\' && i + 1 < line.size()) {
			paint(tokens, i, i + 2, MRSyntaxToken::Delimiter);
			i += 2;
			expectCommand = false;
			continue;
		}
		if ((line[i] == '-' || isDecimalDigitChar(line[i])) && (i == 0 || !isIdentifierChar(line[i - 1]))) {
			std::size_t end = i + (line[i] == '-' ? 1 : 0);
			bool sawDigit = end > i;
			while (end < line.size() && isDecimalDigitChar(line[end])) {
				sawDigit = true;
				++end;
			}
			if (end < line.size() && line[end] == '.') {
				++end;
				while (end < line.size() && isDecimalDigitChar(line[end])) {
					sawDigit = true;
					++end;
				}
			}
			if (sawDigit) {
				paint(tokens, i, end, MRSyntaxToken::Number);
				i = end;
				expectCommand = false;
				continue;
			}
		}
		if (isIdentifierStart(line[i])) {
			const std::size_t wordStart = i;
			std::size_t wordEnd = i + 1;
			while (wordEnd < line.size() && (isIdentifierChar(line[wordEnd]) || line[wordEnd] == '-'))
				++wordEnd;
			if (isZshAssignmentWord(line, wordStart, wordEnd)) {
				const std::size_t eq = line.find('=', wordStart);
				paint(tokens, wordStart, eq, MRSyntaxToken::Key);
				paint(tokens, eq, eq + 1, MRSyntaxToken::Delimiter);
				i = eq + 1;
				expectCommand = true;
				continue;
			}
		}
		if (isIdentifierStart(line[i])) {
			const std::size_t start = i++;
			while (i < line.size() && (isIdentifierChar(line[i]) || line[i] == '-'))
				++i;
			const std::string_view word(line.data() + start, i - start);
			if (wordInList(word, kZshKeywords, sizeof(kZshKeywords) / sizeof(kZshKeywords[0]))) {
				paint(tokens, start, i, MRSyntaxToken::Keyword);
				expectCommand = word == "then" || word == "do" || word == "else" || word == "elif" || word == "if" || word == "for" || word == "while" || word == "until" || word == "case" || word == "select" || word == "function";
				continue;
			}
			if (expectCommand && wordInList(word, kZshBuiltins, sizeof(kZshBuiltins) / sizeof(kZshBuiltins[0]))) {
				paint(tokens, start, i, MRSyntaxToken::Key);
				expectCommand = false;
				continue;
			}
			if (i + 1 < line.size() && line[i] == '(' && line[i + 1] == ')') {
				paint(tokens, start, i, MRSyntaxToken::Key);
				expectCommand = false;
				continue;
			}
			expectCommand = false;
			continue;
		}
		{
			const std::size_t delimiterLength = zshDelimiterLength(line, i);
			if (delimiterLength > 0) {
				paint(tokens, i, i + delimiterLength, MRSyntaxToken::Delimiter);
				expectCommand = true;
				i += delimiterLength;
				continue;
			}
		}
		if (isZshDelimiterChar(line[i])) {
			paint(tokens, i, i + 1, MRSyntaxToken::Delimiter);
			expectCommand = true;
			++i;
			continue;
		}
		if (!std::isspace(static_cast<unsigned char>(line[i]))) expectCommand = false;
		++i;
	}
}

void tokenizePerl(MRSyntaxTokenMap &tokens, const std::string &line) {
	std::size_t trimmed = skipWhitespace(line);
	if (trimmed != std::string::npos && line.compare(trimmed, 2, "#!") == 0) {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Directive);
		return;
	}
	if (trimmed != std::string::npos && line[trimmed] == '=') {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Directive);
		return;
	}

	bool expectPackageName = false;
	bool expectSubName = false;
	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '#') {
			paint(tokens, i, line.size(), MRSyntaxToken::Comment);
			break;
		}
		if (line[i] == '\'' || line[i] == '"' || line[i] == '`') {
			const std::size_t end = consumeZshStringLiteral(line, i, line[i]);
			paint(tokens, i, end, MRSyntaxToken::String);
			i = end;
			continue;
		}
		if (const std::size_t prefixLength = perlLiteralPrefixLength(line, i); prefixLength > 0) {
			const std::size_t end = consumePerlDelimitedLiteral(line, i, prefixLength);
			paint(tokens, i, end, MRSyntaxToken::String);
			i = end;
			continue;
		}
		if (isPerlVariableSigil(line[i])) {
			const std::size_t end = consumePerlVariable(line, i);
			paint(tokens, i, end, MRSyntaxToken::Directive);
			i = end;
			continue;
		}
		if ((line[i] == '-' || isDecimalDigitChar(line[i])) && (i == 0 || !isIdentifierChar(line[i - 1]))) {
			std::size_t end = i + (line[i] == '-' ? 1 : 0);
			bool sawDigit = end > i;
			while (end < line.size() && isDecimalDigitChar(line[end])) {
				sawDigit = true;
				++end;
			}
			if (end < line.size() && line[end] == '.') {
				++end;
				while (end < line.size() && isDecimalDigitChar(line[end])) {
					sawDigit = true;
					++end;
				}
			}
			if (sawDigit) {
				paint(tokens, i, end, MRSyntaxToken::Number);
				i = end;
				continue;
			}
		}
		if (isIdentifierStart(line[i])) {
			const std::size_t start = i++;
			while (i < line.size() && isPerlPackageChar(line[i]))
				++i;
			const std::string_view word(line.data() + start, i - start);
			if (wordInList(word, kPerlKeywords, sizeof(kPerlKeywords) / sizeof(kPerlKeywords[0]))) {
				paint(tokens, start, i, MRSyntaxToken::Keyword);
				expectPackageName = word == "package" || word == "use" || word == "require" || word == "no";
				expectSubName = word == "sub";
				continue;
			}
			if (expectPackageName) {
				paint(tokens, start, i, MRSyntaxToken::Type);
				expectPackageName = false;
				continue;
			}
			if (expectSubName) {
				paint(tokens, start, i, MRSyntaxToken::Key);
				expectSubName = false;
				continue;
			}
			if (isUpperCaseIdentifier(word) || word.find("::") != std::string_view::npos) paint(tokens, start, i, MRSyntaxToken::Type);
			continue;
		}
		if (i + 1 < line.size() && ((line[i] == ':' && line[i + 1] == ':') || (line[i] == '=' && line[i + 1] == '>') || (line[i] == '-' && line[i + 1] == '>'))) {
			paint(tokens, i, i + 2, MRSyntaxToken::Delimiter);
			i += 2;
			continue;
		}
		if (isPerlDelimiterChar(line[i])) {
			paint(tokens, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}
		++i;
	}
}

} // namespace

MRSyntaxLineResult MRPlainTextHighlighter::highlightLine(std::string_view, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = previousState;
	return result;
}

MRSyntaxLineResult MRMakeSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = previousState;
	MRSyntaxTokenMap tokens(line.size(), MRSyntaxToken::Text);
	tokenizeMake(tokens, std::string(line));
	result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
	return result;
}

MRSyntaxLineResult MRMarkdownSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	MRSyntaxTokenMap tokens(line.size(), MRSyntaxToken::Text);

	if (previousState.mode == MRSyntaxMode::QuotedString) {
		const char fenceMarker = static_cast<char>(previousState.payload);
		const std::size_t requiredRunLength = payloadLength(previousState.flags);
		std::size_t trimmed = 0;
		char marker = '\0';
		std::size_t runLength = 0;

		if (findMarkdownFence(line, trimmed, marker, runLength) && marker == fenceMarker && runLength >= requiredRunLength) {
			paint(tokens, trimmed, line.size(), MRSyntaxToken::Directive);
			result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
			return result;
		}
		paint(tokens, 0, line.size(), MRSyntaxToken::String);
		result.stateOut.mode = MRSyntaxMode::QuotedString;
		result.stateOut.payload = previousState.payload;
		result.stateOut.flags = previousState.flags;
		result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
		return result;
	}

	tokenizeMarkdown(tokens, std::string(line));

	if (previousState.mode == MRSyntaxMode::BlockComment) {
		const std::size_t commentEnd = line.find("-->");
		if (commentEnd == std::string_view::npos) {
			paint(tokens, 0, line.size(), MRSyntaxToken::Comment);
			result.stateOut.mode = MRSyntaxMode::BlockComment;
			result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
			return result;
		}
		paint(tokens, 0, commentEnd + 3, MRSyntaxToken::Comment);
	}

	{
		std::size_t trimmed = 0;
		char marker = '\0';
		std::size_t runLength = 0;
		if (findMarkdownFence(line, trimmed, marker, runLength)) {
			paint(tokens, trimmed, trimmed + runLength, MRSyntaxToken::Directive);
			std::size_t languageStart = skipWhitespaceView(line, trimmed + runLength);
			if (languageStart < line.size()) {
				std::size_t languageEnd = languageStart;
				while (languageEnd < line.size() && !std::isspace(static_cast<unsigned char>(line[languageEnd])))
					++languageEnd;
				paint(tokens, languageStart, languageEnd, MRSyntaxToken::Key);
			}
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.payload = static_cast<std::uint32_t>(marker);
			result.stateOut.flags = storePayloadLength(0, runLength);
		}
	}

	const std::size_t commentStart = line.find("<!--");
	if (commentStart != std::string_view::npos && line.find("-->", commentStart + 4) == std::string_view::npos) {
		paint(tokens, commentStart, line.size(), MRSyntaxToken::Comment);
		result.stateOut.mode = MRSyntaxMode::BlockComment;
	}

	result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
	return result;
}

MRSyntaxLineResult MRZshSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	MRSyntaxTokenMap tokens(line.size(), MRSyntaxToken::Text);

	if (previousState.mode == MRSyntaxMode::HereDocument) {
		if (lineMatchesHereDocumentEnd(line, previousState)) {
			paint(tokens, 0, line.size(), MRSyntaxToken::Directive);
			result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
			return result;
		}
		paint(tokens, 0, line.size(), MRSyntaxToken::String);
		result.stateOut = previousState;
		result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
		return result;
	}

	if (previousState.mode == MRSyntaxMode::QuotedString) {
		const char quote = static_cast<char>(previousState.payload);
		const std::size_t end = findStringContinuationEnd(line, 0, quote);

		paint(tokens, 0, end, MRSyntaxToken::String);
		if (end == line.size()) {
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.payload = previousState.payload;
			result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
			return result;
		}
		if (end < line.size()) {
			MRSyntaxTokenMap suffixTokens(line.size() - end, MRSyntaxToken::Text);
			tokenizeZsh(suffixTokens, std::string(line.substr(end)));
			for (std::size_t i = 0; i < suffixTokens.size(); ++i)
				tokens[end + i] = suffixTokens[i];
		}
	} else
		tokenizeZsh(tokens, std::string(line));

	for (std::size_t i = 0; i < line.size();) {
		if (isZshCommentStart(line, i)) break;
		if (line[i] == '\'' || line[i] == '"' || line[i] == '`') {
			const std::size_t end = consumeZshStringLiteral(line, i, line[i]);
			if (end == line.size()) {
				paint(tokens, i, end, MRSyntaxToken::String);
				result.stateOut.mode = MRSyntaxMode::QuotedString;
				result.stateOut.payload = static_cast<std::uint32_t>(line[i]);
			}
			break;
		}
		if (line[i] == '\\' && i + 1 < line.size()) {
			i += 2;
			continue;
		}
		++i;
	}

	{
		std::size_t operatorStart = 0;
		std::size_t operatorEnd = 0;
		std::string_view label;
		bool stripTabs = false;
		if (parseZshHereDocumentStart(line, operatorStart, operatorEnd, label, stripTabs)) {
			paint(tokens, operatorStart, operatorEnd, MRSyntaxToken::Delimiter);
			paint(tokens, skipWhitespaceView(line, operatorEnd), std::min(line.size(), skipWhitespaceView(line, operatorEnd) + label.size()), MRSyntaxToken::Directive);
			result.stateOut.mode = MRSyntaxMode::HereDocument;
			result.stateOut.flags = storePayloadLength(stripTabs ? kSyntaxFlagHereDocStripTabs : 0, label.size());
			result.stateOut.payload = hashSyntaxPayload(label);
		}
	}

	result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
	return result;
}

MRSyntaxLineResult MRPerlSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	MRSyntaxTokenMap tokens(line.size(), MRSyntaxToken::Text);
	const std::size_t trimmed = skipWhitespaceView(line);

	if (previousState.mode == MRSyntaxMode::HereDocument) {
		if (lineMatchesHereDocumentEnd(line, previousState)) {
			paint(tokens, 0, line.size(), MRSyntaxToken::Directive);
			result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
			return result;
		}
		paint(tokens, 0, line.size(), MRSyntaxToken::String);
		result.stateOut = previousState;
		result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
		return result;
	}

	if (previousState.mode == MRSyntaxMode::DirectiveContinuation) {
		paint(tokens, 0, line.size(), MRSyntaxToken::Directive);
		if (trimmed != std::string_view::npos && trimmed < line.size() && line.compare(trimmed, 4, "=cut") != 0) {
			result.stateOut.mode = MRSyntaxMode::DirectiveContinuation;
			result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
			return result;
		}
		result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
		return result;
	}

	if (previousState.mode == MRSyntaxMode::QuotedString) {
		const char quote = static_cast<char>(previousState.payload);
		const std::size_t end = findStringContinuationEnd(line, 0, quote);

		paint(tokens, 0, end, MRSyntaxToken::String);
		if (end == line.size()) {
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.payload = previousState.payload;
			result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
			return result;
		}
		if (end < line.size()) {
			MRSyntaxTokenMap suffixTokens(line.size() - end, MRSyntaxToken::Text);
			tokenizePerl(suffixTokens, std::string(line.substr(end)));
			for (std::size_t i = 0; i < suffixTokens.size(); ++i)
				tokens[end + i] = suffixTokens[i];
		}
	} else
		tokenizePerl(tokens, std::string(line));

	if (isPerlPodDirectiveStart(line, trimmed)) {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Directive);
		if (line.compare(trimmed, 4, "=cut") != 0) result.stateOut.mode = MRSyntaxMode::DirectiveContinuation;
		result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
		return result;
	}

	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '#') break;
		if (line[i] == '\'' || line[i] == '"' || line[i] == '`') {
			const std::size_t end = consumeZshStringLiteral(line, i, line[i]);
			if (end == line.size()) {
				paint(tokens, i, end, MRSyntaxToken::String);
				result.stateOut.mode = MRSyntaxMode::QuotedString;
				result.stateOut.payload = static_cast<std::uint32_t>(line[i]);
			}
			break;
		}
		if (line[i] == '\\' && i + 1 < line.size()) {
			i += 2;
			continue;
		}
		++i;
	}

	{
		std::size_t operatorStart = 0;
		std::size_t operatorEnd = 0;
		std::string_view label;
		if (parsePerlHereDocumentStart(line, operatorStart, operatorEnd, label)) {
			paint(tokens, operatorStart, operatorEnd, MRSyntaxToken::Delimiter);
			paint(tokens, skipWhitespaceView(line, operatorEnd), std::min(line.size(), skipWhitespaceView(line, operatorEnd) + label.size()), MRSyntaxToken::Directive);
			result.stateOut.mode = MRSyntaxMode::HereDocument;
			result.stateOut.flags = storePayloadLength(0, label.size());
			result.stateOut.payload = hashSyntaxPayload(label);
		}
	}

	result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
	return result;
}

MRSyntaxLineResult MRMRmacSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	std::size_t i = 0;
	std::uint32_t commentDepth = 0;
	if (previousState.mode == MRSyntaxMode::BlockComment) commentDepth = previousState.payload == 0 ? 1U : previousState.payload;

	while (i < line.size()) {
		if (commentDepth > 0) {
			std::size_t start = i;
			while (i < line.size()) {
				if (line[i] == '{') {
					++commentDepth;
					++i;
					continue;
				}
				if (line[i] == '}') {
					if (commentDepth > 0) --commentDepth;
					++i;
					if (commentDepth == 0) break;
					continue;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Comment);
			continue;
		}

		if (line[i] == '{') {
			std::size_t start = i++;
			commentDepth = 1;
			while (i < line.size()) {
				if (line[i] == '{') {
					++commentDepth;
					++i;
					continue;
				}
				if (line[i] == '}') {
					--commentDepth;
					++i;
					if (commentDepth == 0) break;
					continue;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Comment);
			continue;
		}

		if (line[i] == '\'') {
			std::size_t start = i++;
			while (i < line.size()) {
				if (line[i] != '\'') {
					++i;
					continue;
				}
				if (i + 1 < line.size() && line[i + 1] == '\'') {
					i += 2;
					continue;
				}
				++i;
				break;
			}
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::String);
			continue;
		}

		if (line[i] == '<') {
			std::size_t start = i++;
			while (i < line.size() && line[i] != '>') ++i;
			if (i < line.size()) ++i;
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
			continue;
		}

		if (line[i] == '$') {
			std::size_t start = i;
			if (i + 1 < line.size() && isHexDigitChar(line[i + 1])) {
				i += 2;
				while (i < line.size() && isHexDigitChar(line[i])) ++i;
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Number);
				continue;
			}
			++i;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			if (i > start + 1) {
				std::string_view word = line.substr(start, i - start);
				if (mrmacWordInList(word, kMrmacDirectives, sizeof(kMrmacDirectives) / sizeof(kMrmacDirectives[0]))) appendRun(result.tokenRuns, start, i, MRSyntaxToken::Directive);
			}
			continue;
		}

		if (std::isdigit(static_cast<unsigned char>(line[i])) != 0) {
			std::size_t start = i++;
			while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])) != 0) ++i;
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Number);
			continue;
		}

		if (isIdentifierStart(line[i])) {
			std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			std::string_view word = line.substr(start, i - start);
			if (mrmacWordInList(word, kMrmacKeywords, sizeof(kMrmacKeywords) / sizeof(kMrmacKeywords[0]))) appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
			else if (mrmacWordInList(word, kMrmacTypeKeywords, sizeof(kMrmacTypeKeywords) / sizeof(kMrmacTypeKeywords[0]))) appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
			continue;
		}

		std::size_t delimiterLength = mrmacDelimiterLength(line, i);
		if (delimiterLength > 0) {
			appendRun(result.tokenRuns, i, i + delimiterLength, MRSyntaxToken::Delimiter);
			i += delimiterLength;
			continue;
		}

		if (isMrmacDelimiterChar(line[i])) {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		++i;
	}

	if (commentDepth > 0) {
		result.stateOut.mode = MRSyntaxMode::BlockComment;
		result.stateOut.payload = commentDepth;
	}
	return result;
}

MRSyntaxLineResult MRCppSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	result.stateOut.flags = static_cast<std::uint16_t>(previousState.flags & kSyntaxFlagCLanguage);
	std::size_t i = 0;
	std::size_t trimmed = 0;
	bool isPreprocessorContinuation = previousState.mode == MRSyntaxMode::DirectiveContinuation;
	bool blockCommentOpen = previousState.mode == MRSyntaxMode::BlockComment;
	bool expectingDeclaredName = false;
	bool expectingDefinedMacro = false;
	bool expectingPrimaryMacro = false;
	bool expectingIncludeTarget = false;
	std::string_view preprocessorDirective;
	const bool isC = (previousState.flags & kSyntaxFlagCLanguage) != 0;
	const char *const *keywords = isC ? kCKeywords : kCppKeywords;
	const std::size_t keywordCount = isC ? sizeof(kCKeywords) / sizeof(kCKeywords[0]) : sizeof(kCppKeywords) / sizeof(kCppKeywords[0]);
	const char *const *types = isC ? kCTypeKeywords : kCppTypeKeywords;
	const std::size_t typeCount = isC ? sizeof(kCTypeKeywords) / sizeof(kCTypeKeywords[0]) : sizeof(kCppTypeKeywords) / sizeof(kCppTypeKeywords[0]);

	if (blockCommentOpen) {
		while (i + 1 < line.size()) {
			if (line[i] == '*' && line[i + 1] == '/') {
				i += 2;
				appendRun(result.tokenRuns, 0, i, MRSyntaxToken::Comment);
				blockCommentOpen = false;
				break;
			}
			++i;
		}
		if (blockCommentOpen) {
			appendRun(result.tokenRuns, 0, line.size(), MRSyntaxToken::Comment);
			result.stateOut.mode = MRSyntaxMode::BlockComment;
			result.stateOut.flags = previousState.flags;
			return result;
		}
	}

	if (previousState.mode == MRSyntaxMode::RawString) {
		const std::string delimiter = unpackRawStringDelimiter(previousState.payload, previousState.flags);
		const std::size_t end = findRawStringTerminator(line, 0, delimiter);
		if (end == std::string_view::npos) {
			appendRun(result.tokenRuns, 0, line.size(), MRSyntaxToken::String);
			result.stateOut.mode = MRSyntaxMode::RawString;
			result.stateOut.flags = previousState.flags;
			result.stateOut.payload = previousState.payload;
			return result;
		}
		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		i = end;
	}

	trimmed = skipWhitespace(std::string(line), 0);
	if (i == 0 && trimmed != std::string::npos && trimmed < line.size() && line[trimmed] == '#') {
		std::size_t directiveStart = trimmed;
		std::size_t directiveWordStart = directiveStart + 1;
		std::size_t directiveWordEnd = directiveWordStart;
		while (directiveWordEnd < line.size() && isIdentifierChar(line[directiveWordEnd]))
			++directiveWordEnd;
		appendRun(result.tokenRuns, directiveStart, directiveWordEnd, MRSyntaxToken::Directive);
		preprocessorDirective = line.substr(directiveWordStart, directiveWordEnd - directiveWordStart);
		expectingPrimaryMacro = preprocessorDirective == "define" || preprocessorDirective == "undef" || preprocessorDirective == "ifdef" || preprocessorDirective == "ifndef";
		expectingIncludeTarget = preprocessorDirective == "include" || preprocessorDirective == "include_next" || preprocessorDirective == "import";
		i = directiveWordEnd;
	}

	while (i < line.size()) {
		if (expectingIncludeTarget) {
			std::size_t targetStart = skipWhitespaceView(line, i);
			if (targetStart >= line.size()) break;
			if (line[targetStart] == '<') {
				std::size_t targetEnd = targetStart + 1;
				while (targetEnd < line.size() && line[targetEnd] != '>') ++targetEnd;
				if (targetEnd < line.size()) ++targetEnd;
				appendRun(result.tokenRuns, targetStart, targetEnd, MRSyntaxToken::String);
				expectingIncludeTarget = false;
				i = targetEnd;
				continue;
			}
			if (line[targetStart] == '"') {
				std::size_t targetEnd = consumeCppStringLiteral(line, targetStart, '"');
				appendRun(result.tokenRuns, targetStart, targetEnd, MRSyntaxToken::String);
				expectingIncludeTarget = false;
				i = targetEnd;
				continue;
			}
			expectingIncludeTarget = false;
		}

		if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
			appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Comment);
			break;
		}

		if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
			std::size_t start = i;
			i += 2;
			while (i + 1 < line.size()) {
				if (line[i] == '*' && line[i + 1] == '/') {
					i += 2;
					blockCommentOpen = false;
					break;
				}
				++i;
			}
			if (i > line.size()) i = line.size();
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Comment);
			if (i >= line.size() && (line.size() < 2 || line[line.size() - 2] != '*' || line[line.size() - 1] != '/')) {
				blockCommentOpen = true;
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				result.stateOut.flags = previousState.flags;
				break;
			}
			continue;
		}

		if (line[i] == '"' || line[i] == '\'') {
			std::size_t start = i;
			std::size_t end = consumeCppStringLiteral(line, i, line[i]);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			i = end;
			continue;
		}

		if (!isC) {
			std::size_t prefixEnd = 0;
			std::string_view rawDelimiter;
			if (findRawStringStart(line, i, prefixEnd, rawDelimiter)) {
				const std::size_t start = i;
				const std::size_t end = findRawStringTerminator(line, prefixEnd, rawDelimiter);
				if (end == std::string_view::npos) {
					appendRun(result.tokenRuns, start, line.size(), MRSyntaxToken::String);
					if (rawDelimiter.size() <= 4) {
						result.stateOut.mode = MRSyntaxMode::RawString;
						result.stateOut.flags = static_cast<std::uint16_t>((result.stateOut.flags & kSyntaxFlagCLanguage) | (static_cast<std::uint16_t>(rawDelimiter.size()) << 8));
						result.stateOut.payload = packRawStringDelimiter(rawDelimiter);
					}
					return result;
				}
				appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
				i = end;
				continue;
			}
		}

		if (isDecimalDigitChar(line[i]) || (line[i] == '.' && i + 1 < line.size() && isDecimalDigitChar(line[i + 1]))) {
			std::size_t start = i;
			std::size_t end = consumeCppNumber(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
			i = end;
			continue;
		}

		if (isIdentifierStart(line[i])) {
			std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			std::string_view word = line.substr(start, i - start);
			if (expectingPrimaryMacro) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				expectingPrimaryMacro = false;
				continue;
			}
			if (expectingDefinedMacro) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				expectingDefinedMacro = false;
				continue;
			}
			if (wordInList(word, keywords, keywordCount)) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
				if (isDeclarationIntroducer(word) || isAliasIntroducer(word)) expectingDeclaredName = true;
				else if (expectingDeclaredName && (word == "class" || word == "struct")) expectingDeclaredName = true;
				continue;
			}
			if (wordInList(word, types, typeCount)) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			if (!isC && wordInList(word, kCppConstants, sizeof(kCppConstants) / sizeof(kCppConstants[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			if ((preprocessorDirective == "if" || preprocessorDirective == "elif") && word == "defined") {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
				expectingDefinedMacro = true;
				continue;
			}
			if (expectingDeclaredName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				expectingDeclaredName = false;
				continue;
			}
			if (hasScopeQualifierBefore(line, start) || hasScopeQualifierAfter(line, i)) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			if (isUpperCaseIdentifier(word) || isKStyleConstant(word)) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			if (isFunctionLikeIdentifier(line, start, i)) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			continue;
		}

		{
			std::size_t delimiterLength = cppDelimiterLength(line, i);
			if (delimiterLength > 0) {
				appendRun(result.tokenRuns, i, i + delimiterLength, MRSyntaxToken::Delimiter);
				i += delimiterLength;
				continue;
			}
		}

		if (isCppDelimiterChar(line[i])) {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		++i;
	}

	if ((trimmed != std::string::npos && trimmed < line.size() && line[trimmed] == '#') || isPreprocessorContinuation) {
		if (endsWithPreprocessorContinuation(line)) result.stateOut.mode = MRSyntaxMode::DirectiveContinuation;
	}

	return result;
}

MRSyntaxLineResult MRJavaScriptSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	std::size_t i = 0;
	bool blockCommentOpen = previousState.mode == MRSyntaxMode::BlockComment;

	if (blockCommentOpen) {
		while (i + 1 < line.size()) {
			if (line[i] == '*' && line[i + 1] == '/') {
				i += 2;
				appendRun(result.tokenRuns, 0, i, MRSyntaxToken::Comment);
				blockCommentOpen = false;
				break;
			}
			++i;
		}
		if (blockCommentOpen) {
			appendRun(result.tokenRuns, 0, line.size(), MRSyntaxToken::Comment);
			result.stateOut.mode = MRSyntaxMode::BlockComment;
			return result;
		}
	}

	if (previousState.mode == MRSyntaxMode::QuotedString && previousState.payload == static_cast<std::uint32_t>('`')) {
		const std::size_t end = findStringContinuationEnd(line, 0, '`');

		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		if (end == line.size()) {
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.payload = static_cast<std::uint32_t>('`');
			return result;
		}
		i = end;
	}

	while (i < line.size()) {
		if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
			appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Comment);
			break;
		}

		if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
			std::size_t start = i;
			i += 2;
			while (i + 1 < line.size()) {
				if (line[i] == '*' && line[i + 1] == '/') {
					i += 2;
					blockCommentOpen = false;
					break;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, std::min(i, line.size()), MRSyntaxToken::Comment);
			if (i >= line.size() && (line.size() < 2 || line[line.size() - 2] != '*' || line[line.size() - 1] != '/')) {
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				break;
			}
			continue;
		}

		if (line[i] == '"' || line[i] == '\'') {
			std::size_t start = i;
			std::size_t end = consumeCppStringLiteral(line, i, line[i]);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			i = end;
			continue;
		}

		if (line[i] == '`') {
			std::size_t start = i;
			std::size_t end = consumeCppStringLiteral(line, i, '`');
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			if (end == line.size() && (line.empty() || line.back() != '`')) {
				result.stateOut.mode = MRSyntaxMode::QuotedString;
				result.stateOut.payload = static_cast<std::uint32_t>('`');
				return result;
			}
			i = end;
			continue;
		}

		if (isDecimalDigitChar(line[i]) || (line[i] == '.' && i + 1 < line.size() && isDecimalDigitChar(line[i + 1]))) {
			std::size_t start = i;
			std::size_t end = consumeCppNumber(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
			i = end;
			continue;
		}

		if (isIdentifierStart(line[i])) {
			std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			std::string_view word = line.substr(start, i - start);
			if (wordInList(word, kJavaScriptKeywords, sizeof(kJavaScriptKeywords) / sizeof(kJavaScriptKeywords[0]))) appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
			else if (wordInList(word, kJavaScriptConstants, sizeof(kJavaScriptConstants) / sizeof(kJavaScriptConstants[0])))
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
			else if (isUpperCaseIdentifier(word) || isKStyleConstant(word))
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
			continue;
		}

		{
			std::size_t delimiterLength = cppDelimiterLength(line, i);
			if (delimiterLength > 0) {
				appendRun(result.tokenRuns, i, i + delimiterLength, MRSyntaxToken::Delimiter);
				i += delimiterLength;
				continue;
			}
		}

		if (isCppDelimiterChar(line[i])) {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		++i;
	}

	return result;
}

MRSyntaxLineResult MRPythonSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	std::size_t i = 0;
	bool expectClassName = false;
	bool expectFunctionName = false;

	if (previousState.mode == MRSyntaxMode::QuotedString && (previousState.flags & kSyntaxFlagTripleQuoted) != 0) {
		const char quote = static_cast<char>(previousState.payload);
		const std::size_t end = findTripleQuotedStringEnd(line, 0, quote);

		if (end == std::string_view::npos) {
			appendRun(result.tokenRuns, 0, line.size(), MRSyntaxToken::String);
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.flags = kSyntaxFlagTripleQuoted;
			result.stateOut.payload = previousState.payload;
			return result;
		}
		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		i = end;
	}

	while (i < line.size()) {
		if (line[i] == '#') {
			appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Comment);
			break;
		}

		{
			std::size_t contentStart = 0;
			char quote = '\0';
			bool tripleQuoted = false;

			if (findPythonStringStart(line, i, contentStart, quote, tripleQuoted)) {
				const std::size_t start = i;
				std::size_t end = contentStart;

				if (tripleQuoted) {
					end = findTripleQuotedStringEnd(line, contentStart, quote);
					if (end == std::string_view::npos) {
						appendRun(result.tokenRuns, start, line.size(), MRSyntaxToken::String);
						result.stateOut.mode = MRSyntaxMode::QuotedString;
						result.stateOut.flags = kSyntaxFlagTripleQuoted;
						result.stateOut.payload = static_cast<std::uint32_t>(quote);
						return result;
					}
				} else
					end = consumeCppStringLiteral(line, contentStart - 1, quote);

				appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
				i = end;
				continue;
			}
		}

		if (isDecimalDigitChar(line[i]) || (line[i] == '.' && i + 1 < line.size() && isDecimalDigitChar(line[i + 1]))) {
			std::size_t start = i;
			std::size_t end = consumePythonNumber(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
			i = end;
			continue;
		}

		if (line[i] == '@') {
			std::size_t start = i++;
			while (i < line.size() && (isIdentifierChar(line[i]) || line[i] == '.')) ++i;
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Directive);
			continue;
		}

		if (isIdentifierStart(line[i])) {
			std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			std::string_view word = line.substr(start, i - start);
			if (wordInList(word, kPythonKeywords, sizeof(kPythonKeywords) / sizeof(kPythonKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
				expectClassName = word == "class";
				expectFunctionName = word == "def";
				continue;
			}
			if (wordInList(word, kPythonConstants, sizeof(kPythonConstants) / sizeof(kPythonConstants[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			if (expectClassName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				expectClassName = false;
				continue;
			}
			if (expectFunctionName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				expectFunctionName = false;
				continue;
			}
			if (isUpperCaseIdentifier(word) || isKStyleConstant(word)) appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
			continue;
		}

		if (line[i] == ':' || line[i] == '=' || line[i] == '!' || line[i] == '-' || line[i] == '*' || line[i] == '/' || line[i] == '<' || line[i] == '>') {
			if (i + 1 < line.size()) {
				const char next = line[i + 1];
				if ((line[i] == ':' && next == '=') || (line[i] == '=' && next == '=') || (line[i] == '!' && next == '=') || (line[i] == '-' && next == '>') || (line[i] == '*' && next == '*') || (line[i] == '/' && next == '/') ||
					(line[i] == '<' && next == '=') || (line[i] == '>' && next == '=')) {
					appendRun(result.tokenRuns, i, i + 2, MRSyntaxToken::Delimiter);
					i += 2;
					continue;
				}
			}
		}

		if (isCppDelimiterChar(line[i]) || line[i] == '@') {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		++i;
	}

	return result;
}

MRSyntaxLineResult MRJsonSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = previousState;

	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') {
			appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Comment);
			break;
		}
		if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '*') {
			const std::size_t start = i;
			i += 2;
			while (i + 1 < line.size() && !(line[i] == '*' && line[i + 1] == '/'))
				++i;
			if (i + 1 < line.size()) i += 2;
			else
				i = line.size();
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Comment);
			continue;
		}
		if (line[i] == '"') {
			const std::size_t start = i;
			const std::size_t end = consumeJsonStringLiteral(line, i);
			std::size_t next = skipWhitespaceView(line, end);

			appendRun(result.tokenRuns, start, end, next < line.size() && line[next] == ':' ? MRSyntaxToken::Key : MRSyntaxToken::String);
			i = end;
			continue;
		}

		if (line[i] == '-' || isDecimalDigitChar(line[i])) {
			const std::size_t start = i;
			const std::size_t end = consumeJsonNumber(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
			i = end;
			continue;
		}

		if (isIdentifierStart(line[i])) {
			std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			std::string_view word = line.substr(start, i - start);
			if (word == "true" || word == "false" || word == "null") appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
			continue;
		}

		if (isJsonDelimiterChar(line[i])) {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		++i;
	}

	return result;
}

MRSyntaxLanguage tmrDetectSyntaxLanguage(const std::string &path, const std::string &title) {
	std::string fileName = fileNamePart(!path.empty() ? path : title);
	std::string lowerName = lowerCopy(fileName);
	std::string ext = extensionPart(fileName);

	if (lowerName == "makefile" || lowerName == "gnumakefile" || ext == ".mk" || ext == ".mak") return MRSyntaxLanguage::Make;
	if (ext == ".c" || ext == ".h") return MRSyntaxLanguage::C;
	if (ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".hh" || ext == ".hpp" || ext == ".hxx" || ext == ".ipp" || ext == ".tpp" || ext == ".inl") return MRSyntaxLanguage::Cpp;
	if (ext == ".js" || ext == ".jsx" || ext == ".mjs" || ext == ".cjs" || ext == ".ts" || ext == ".tsx") return MRSyntaxLanguage::JavaScript;
	if (ext == ".py" || ext == ".pyw") return MRSyntaxLanguage::Python;
	if (ext == ".json" || ext == ".jsonc") return MRSyntaxLanguage::Json;
	if (ext == ".zsh" || ext == ".sh" || ext == ".bash" || ext == ".ksh" || ext == ".zprofile" || ext == ".zshrc" || ext == ".zshenv" || ext == ".zlogin" || ext == ".zlogout") return MRSyntaxLanguage::Zsh;
	if (lowerName == ".zshrc" || lowerName == ".zprofile" || lowerName == ".zshenv" || lowerName == ".zlogin" || lowerName == ".zlogout" || lowerName == ".bashrc" || lowerName == ".bash_profile" || lowerName == ".profile")
		return MRSyntaxLanguage::Zsh;
	if (ext == ".pl" || ext == ".pm" || ext == ".t" || ext == ".pod" || ext == ".cgi" || ext == ".psgi" || ext == ".perl") return MRSyntaxLanguage::Perl;
	if (ext == ".mrmac") return MRSyntaxLanguage::MRMAC;
	if (ext == ".md" || ext == ".markdown" || lowerName == "readme") return MRSyntaxLanguage::Markdown;
	return MRSyntaxLanguage::PlainText;
}

MRSyntaxClassification tmrClassifySyntaxLanguage(const std::string &path, const std::string &title, std::string_view text) {
	std::array<int, kSyntaxLanguageCount> scores {};
	std::array<int, kSyntaxLanguageCount> strongSignals {};
	const std::string fileName = fileNamePart(!path.empty() ? path : title);
	const std::string lowerName = lowerCopy(fileName);
	const std::string ext = extensionPart(fileName);
	const std::string_view sample = classificationSample(text);
	const std::string lowerSample = lowerCopyView(sample);
	const std::string_view lower = lowerSample;
	const std::string_view firstLine = firstLineView(sample);
	const std::string lowerFirstLine = lowerCopyView(firstLine);
	const std::string_view lowerShebang = lowerFirstLine;
	const MRSyntaxLanguage detectedByPath = tmrDetectSyntaxLanguage(path, title);
	const int includeLines = countLinePrefixMatches(lower, "#include", 8);
	const int defineLines = countLinePrefixMatches(lower, "#define", 8);
	const int typedefLines = countLinePrefixMatches(lower, "typedef ", 8);
	const int namespaceLines = countLinePrefixMatches(lower, "namespace ", 8);
	const int templateLines = countLinePrefixMatches(lower, "template<", 8);
	const int cppClassLines = countLinePrefixMatches(lower, "class ", 8);
	const int importLines = countLinePrefixMatches(lower, "import ", 8);
	const int exportLines = countLinePrefixMatches(lower, "export ", 8);
	const int functionLines = countLinePrefixMatches(lower, "function ", 8);
	const int constLines = countLinePrefixMatches(lower, "const ", 12);
	const int letLines = countLinePrefixMatches(lower, "let ", 12);
	const int pythonDefLines = countLinePrefixMatches(lower, "def ", 12);
	const int pythonClassLines = countLinePrefixMatches(lower, "class ", 8);
	const int pythonBlockHeaders = countPythonBlockHeaders(sample, 16);
	const int jsonKeyLines = countJsonKeyLikeLines(sample, 32);
	const int shellAssignmentLines = countShellAssignmentLines(sample, 16);
	const int perlSigilDeclLines = countPerlSigilDeclLines(sample, 16);
	const int makeTargetLines = countMakeTargetLikeLines(sample, 16);
	const int makeRecipeLines = countRecipeTabLines(sample, 16);
	const int markdownStructureLines = countMarkdownStructureLines(sample, 24);
	const int semicolonCount = countCharacter(sample, ';', 32);
	const int braceCount = countCharacter(sample, '{', 32) + countCharacter(sample, '}', 32);
	const int shellControlCount = countMatches(lower, "[[", 12) + countMatches(lower, "case ", 8) + countMatches(lower, "typeset ", 8) + countMatches(lower, "autoload ", 8) + countMatches(lower, "setopt ", 8);

	if (startsWithText(lowerShebang, "#!")) {
		if (containsText(lowerShebang, "python")) addClassificationScore(scores, MRSyntaxLanguage::Python, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Python)] += 2;
		if (containsText(lowerShebang, "perl")) addClassificationScore(scores, MRSyntaxLanguage::Perl, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Perl)] += 2;
		if (containsText(lowerShebang, "zsh") || containsText(lowerShebang, "bash") || containsText(lowerShebang, "sh")) addClassificationScore(scores, MRSyntaxLanguage::Zsh, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Zsh)] += 2;
		if (containsText(lowerShebang, "node")) addClassificationScore(scores, MRSyntaxLanguage::JavaScript, 12), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::JavaScript)] += 2;
		if (containsText(lowerShebang, "make")) addClassificationScore(scores, MRSyntaxLanguage::Make, 8), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Make)] += 1;
	}

	if (detectedByPath != MRSyntaxLanguage::PlainText) addClassificationScore(scores, detectedByPath, 4);
	if (ext == ".pl" || ext == ".pm") addClassificationScore(scores, MRSyntaxLanguage::Perl, 6), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Perl)] += 1;
	if (lowerName == "makefile" || lowerName == "gnumakefile") addClassificationScore(scores, MRSyntaxLanguage::Make, 10), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Make)] += 2;
	if (lowerName == "readme" || startsWithText(lowerName, "readme.")) addClassificationScore(scores, MRSyntaxLanguage::Markdown, 6), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Markdown)] += 1;

	addClassificationScore(scores, MRSyntaxLanguage::C, includeLines * 5);
	addClassificationScore(scores, MRSyntaxLanguage::C, defineLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::C, typedefLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::C, countMatches(lower, "struct ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::C, countMatches(lower, "enum ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::C, countMatches(lower, "->", 12));
	if (includeLines + typedefLines > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::C)] += std::min(3, includeLines + typedefLines);

	addClassificationScore(scores, MRSyntaxLanguage::Cpp, namespaceLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Cpp, templateLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Cpp, countMatches(lower, "::", 16) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Cpp, countMatches(lower, "typename ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Cpp, countMatches(lower, "constexpr", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Cpp, countMatches(sample, "R\"", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Cpp, cppClassLines * 2);
	if (namespaceLines + templateLines + cppClassLines > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Cpp)] += std::min(3, namespaceLines + templateLines + cppClassLines);

	addClassificationScore(scores, MRSyntaxLanguage::JavaScript, importLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::JavaScript, countMatches(lower, " from ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::JavaScript, exportLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::JavaScript, constLines * 2);
	addClassificationScore(scores, MRSyntaxLanguage::JavaScript, letLines * 2);
	addClassificationScore(scores, MRSyntaxLanguage::JavaScript, countMatches(lower, "=>", 12) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::JavaScript, functionLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::JavaScript, countMatches(sample, "`", 24) > 1 ? 3 : 0);
	if (importLines + exportLines + functionLines + constLines + letLines > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::JavaScript)] += std::min(3, importLines + exportLines + functionLines + constLines + letLines);

	addClassificationScore(scores, MRSyntaxLanguage::Python, pythonDefLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Python, pythonClassLines * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Python, countMatches(lower, "elif ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Python, countMatches(lower, "except", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Python, countMatches(lower, "async def ", 6) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Python, countMatches(lower, "from ", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Python, (countMatches(sample, "\"\"\"", 6) + countMatches(sample, "'''", 6)) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Python, pythonBlockHeaders * 2);
	if (pythonDefLines + pythonClassLines + pythonBlockHeaders > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Python)] += std::min(4, pythonDefLines + pythonClassLines + pythonBlockHeaders);

	addClassificationScore(scores, MRSyntaxLanguage::Json, countMatches(lower, "\"", 64) >= 8 ? 2 : 0);
	addClassificationScore(scores, MRSyntaxLanguage::Json, countMatches(lower, "\":", 24) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Json, jsonKeyLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Json, countMatches(lower, "true", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Json, countMatches(lower, "false", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Json, countMatches(lower, "null", 8));
	if (containsText(lower, "{") && containsText(lower, "}") && containsText(lower, "[") && containsText(lower, "]")) addClassificationScore(scores, MRSyntaxLanguage::Json, 4);
	if (jsonKeyLines > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Json)] += std::min(4, jsonKeyLines);

	addClassificationScore(scores, MRSyntaxLanguage::Zsh, countMatches(lower, "[[", 12) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Zsh, countMatches(lower, "${", 16));
	addClassificationScore(scores, MRSyntaxLanguage::Zsh, countMatches(lower, "$(", 16));
	addClassificationScore(scores, MRSyntaxLanguage::Zsh, countMatches(lower, "case ", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Zsh, countMatches(lower, " in\n", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Zsh, countMatches(lower, "typeset ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Zsh, countMatches(lower, "autoload ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Zsh, countMatches(lower, "setopt ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Zsh, countMatches(lower, "<<", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Zsh, shellAssignmentLines * 2);
	if (shellControlCount + shellAssignmentLines > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Zsh)] += std::min(4, shellControlCount + shellAssignmentLines);

	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "my ", 12) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "our ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "sub ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "package ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "use ", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "=pod", 4) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "=cut", 4) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "qr/", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "tr/", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "y/", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(lower, "s/", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(sample, "$", 24) >= 3 ? 3 : 0);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(sample, "@", 24) >= 2 ? 2 : 0);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, countMatches(sample, "%", 24) >= 2 ? 2 : 0);
	addClassificationScore(scores, MRSyntaxLanguage::Perl, perlSigilDeclLines * 3);
	if (perlSigilDeclLines > 0 || containsText(lower, "=pod") || containsText(lower, "package ")) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Perl)] += std::min(4, perlSigilDeclLines + (containsText(lower, "=pod") ? 1 : 0) + (containsText(lower, "package ") ? 1 : 0));

	addClassificationScore(scores, MRSyntaxLanguage::MRMAC, countMatches(lower, "$macro", 8) * 5);
	addClassificationScore(scores, MRSyntaxLanguage::MRMAC, countMatches(lower, "$macro_file", 8) * 5);
	addClassificationScore(scores, MRSyntaxLanguage::MRMAC, countMatches(lower, "tvcall", 8) * 5);
	addClassificationScore(scores, MRSyntaxLanguage::MRMAC, countMatches(lower, "def_int", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::MRMAC, countMatches(lower, "def_str", 8) * 4);
	if (containsText(lower, "$macro") || containsText(lower, "tvcall")) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::MRMAC)] += 3;

	addClassificationScore(scores, MRSyntaxLanguage::Make, countMatches(lower, ".phony", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Make, makeRecipeLines * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Make, countMatches(lower, "$(", 16));
	addClassificationScore(scores, MRSyntaxLanguage::Make, makeTargetLines * 3);
	if (makeTargetLines >= 1 && makeRecipeLines >= 1) addClassificationScore(scores, MRSyntaxLanguage::Make, 6);
	if (makeTargetLines + makeRecipeLines > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Make)] += std::min(4, makeTargetLines + makeRecipeLines);

	addClassificationScore(scores, MRSyntaxLanguage::Markdown, countMatches(lower, "\n# ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Markdown, countMatches(lower, "\n##", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Markdown, countMatches(lower, "```", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Markdown, countMatches(lower, "\n> ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Markdown, countMatches(lower, "](", 12) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Markdown, countMatches(lower, "![", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Markdown, countMatches(lower, "\n- [", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Markdown, countMatches(lower, "\n|", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Markdown, markdownStructureLines * 2);
	if (markdownStructureLines > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Markdown)] += std::min(4, markdownStructureLines);

	if (jsonKeyLines >= 3) {
		addClassificationScore(scores, MRSyntaxLanguage::Json, 6);
		++strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Json)];
		addClassificationScore(scores, MRSyntaxLanguage::JavaScript, -4);
		addClassificationScore(scores, MRSyntaxLanguage::Python, -4);
		addClassificationScore(scores, MRSyntaxLanguage::Zsh, -3);
		addClassificationScore(scores, MRSyntaxLanguage::Perl, -5);
	}
	if (markdownStructureLines >= 3) {
		addClassificationScore(scores, MRSyntaxLanguage::Markdown, 4);
		++strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Markdown)];
		addClassificationScore(scores, MRSyntaxLanguage::C, -2);
		addClassificationScore(scores, MRSyntaxLanguage::Cpp, -2);
		addClassificationScore(scores, MRSyntaxLanguage::JavaScript, -2);
		addClassificationScore(scores, MRSyntaxLanguage::Json, -2);
	}
	if (makeTargetLines >= 2 && makeRecipeLines >= 1) {
		addClassificationScore(scores, MRSyntaxLanguage::Make, 8);
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Make)] += 2;
		addClassificationScore(scores, MRSyntaxLanguage::Markdown, -3);
		addClassificationScore(scores, MRSyntaxLanguage::Json, -3);
		addClassificationScore(scores, MRSyntaxLanguage::Python, -2);
	}
	if (perlSigilDeclLines >= 2) {
		addClassificationScore(scores, MRSyntaxLanguage::Perl, 4);
		++strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Perl)];
		addClassificationScore(scores, MRSyntaxLanguage::Zsh, -3);
		addClassificationScore(scores, MRSyntaxLanguage::JavaScript, -2);
		addClassificationScore(scores, MRSyntaxLanguage::Json, -3);
	}
	if (pythonBlockHeaders >= 2) {
		addClassificationScore(scores, MRSyntaxLanguage::Python, 4);
		++strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Python)];
		addClassificationScore(scores, MRSyntaxLanguage::Markdown, -2);
		addClassificationScore(scores, MRSyntaxLanguage::Json, -3);
	}
	if (shellControlCount >= 2) {
		addClassificationScore(scores, MRSyntaxLanguage::Zsh, 4);
		++strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Zsh)];
		addClassificationScore(scores, MRSyntaxLanguage::Perl, -2);
		addClassificationScore(scores, MRSyntaxLanguage::Json, -3);
	}
	if (semicolonCount >= 6 || braceCount >= 12) {
		addClassificationScore(scores, MRSyntaxLanguage::Markdown, -3);
		addClassificationScore(scores, MRSyntaxLanguage::Python, -2);
	}
	if (includeLines > 0 && namespaceLines == 0 && templateLines == 0) addClassificationScore(scores, MRSyntaxLanguage::Cpp, -1);
	if (namespaceLines > 0 || templateLines > 0 || countMatches(lower, "::", 16) >= 2) addClassificationScore(scores, MRSyntaxLanguage::C, -3);

	int bestScore = 0;
	int secondScore = 0;
	MRSyntaxLanguage bestLanguage = MRSyntaxLanguage::PlainText;

	for (std::size_t i = 0; i < scores.size(); ++i) {
		const int score = scores[i];
		if (score > bestScore) {
			secondScore = bestScore;
			bestScore = score;
			bestLanguage = static_cast<MRSyntaxLanguage>(i);
		} else if (score > secondScore)
			secondScore = score;
	}

	const int bestStrongSignals = strongSignals[syntaxLanguageIndex(bestLanguage)];
	if (bestScore < 8) return MRSyntaxClassification(MRSyntaxLanguage::PlainText, 0);
	if (bestStrongSignals == 0 && bestScore < 12) return MRSyntaxClassification(MRSyntaxLanguage::PlainText, 0);
	if (bestScore - secondScore < 3 && bestStrongSignals < 2) return MRSyntaxClassification(MRSyntaxLanguage::PlainText, 0);
	if (bestScore - secondScore < 5 && bestScore < 14) return MRSyntaxClassification(MRSyntaxLanguage::PlainText, 0);

	int confidence = bestScore * 5 + std::max(0, bestScore - secondScore) * 9 + bestStrongSignals * 8;
	if (confidence > 100) confidence = 100;
	return MRSyntaxClassification(bestLanguage, static_cast<std::uint16_t>(confidence));
}

const char *tmrSyntaxLanguageName(MRSyntaxLanguage language) noexcept {
	switch (language) {
		case MRSyntaxLanguage::C:
			return "C";
		case MRSyntaxLanguage::Cpp:
			return "C++";
		case MRSyntaxLanguage::JavaScript:
			return "JavaScript";
		case MRSyntaxLanguage::Python:
			return "Python";
		case MRSyntaxLanguage::Json:
			return "JSON";
		case MRSyntaxLanguage::Zsh:
			return "Zsh";
		case MRSyntaxLanguage::Perl:
			return "Perl";
		case MRSyntaxLanguage::MRMAC:
			return "MRMAC";
		case MRSyntaxLanguage::Make:
			return "Make";
		case MRSyntaxLanguage::Markdown:
			return "Markdown";
		default:
			return "Plain Text";
	}
}

const char *tmrSyntaxLanguageMarker(MRSyntaxLanguage language) noexcept {
	switch (language) {
		case MRSyntaxLanguage::C:
			return "C";
		case MRSyntaxLanguage::Cpp:
			return "C++";
		case MRSyntaxLanguage::JavaScript:
			return "JS";
		case MRSyntaxLanguage::Python:
			return "Py";
		case MRSyntaxLanguage::Json:
			return "Jn";
		case MRSyntaxLanguage::Zsh:
			return "Sh";
		case MRSyntaxLanguage::Perl:
			return "Pl";
		case MRSyntaxLanguage::MRMAC:
			return "MM";
		case MRSyntaxLanguage::Make:
			return "Mk";
		case MRSyntaxLanguage::Markdown:
			return "Md";
		default:
			return "";
	}
}

std::uint32_t tmrSyntaxLanguageMarkerRgb(MRSyntaxLanguage language) noexcept {
	switch (language) {
		case MRSyntaxLanguage::C:
			return 0x7DB7E8;
		case MRSyntaxLanguage::Cpp:
			return 0x5C9DED;
		case MRSyntaxLanguage::JavaScript:
			return 0xD9A400;
		case MRSyntaxLanguage::Python:
			return 0x4AA3D8;
		case MRSyntaxLanguage::Json:
			return 0x9FB3C8;
		case MRSyntaxLanguage::Zsh:
			return 0x6FBF73;
		case MRSyntaxLanguage::Perl:
			return 0xB084CC;
		case MRSyntaxLanguage::MRMAC:
			return 0xE58F65;
		case MRSyntaxLanguage::Make:
			return 0x8FA8B6;
		case MRSyntaxLanguage::Markdown:
			return 0xC0A060;
		default:
			return 0;
	}
}

MRSyntaxTokenMap tmrBuildLegacyTokenMapForTextLine(MRSyntaxLanguage language, std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result = tmrHighlightTextLine(language, line, previousState);
	return tokenMapFromRuns(line.size(), result.tokenRuns);
}

std::vector<MRSyntaxTokenRun> tmrBuildTokenRunsFromTokenMap(const MRSyntaxTokenMap &tokenMap) {
	std::vector<MRSyntaxTokenRun> runs;
	if (tokenMap.empty()) return runs;

	std::size_t start = 0;
	MRSyntaxToken current = tokenMap[0];
	for (std::size_t i = 1; i < tokenMap.size(); ++i) {
		if (tokenMap[i] == current) continue;
		runs.push_back(MRSyntaxTokenRun(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(i - start), current));
		start = i;
		current = tokenMap[i];
	}
	runs.push_back(MRSyntaxTokenRun(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(tokenMap.size() - start), current));
	return runs;
}

MRSyntaxLineResult tmrHighlightTextLine(MRSyntaxLanguage language, std::string_view line, MRSyntaxLineState previousState) {
	switch (language) {
		case MRSyntaxLanguage::PlainText: {
			MRPlainTextHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::C: {
			MRCppSyntaxHighlighter highlighter;
			MRSyntaxLineState state = previousState;
			state.flags = static_cast<std::uint16_t>((state.flags & ~kSyntaxFlagCLanguage) | kSyntaxFlagCLanguage);
			return highlighter.highlightLine(line, state);
		}
		case MRSyntaxLanguage::Cpp: {
			MRCppSyntaxHighlighter highlighter;
			MRSyntaxLineState state = previousState;
			state.flags = static_cast<std::uint16_t>(state.flags & ~kSyntaxFlagCLanguage);
			return highlighter.highlightLine(line, state);
		}
		case MRSyntaxLanguage::JavaScript: {
			MRJavaScriptSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Python: {
			MRPythonSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Json: {
			MRJsonSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Zsh: {
			MRZshSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Perl: {
			MRPerlSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Make: {
			MRMakeSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Markdown: {
			MRMarkdownSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::MRMAC: {
			MRMRmacSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		default:
			break;
	}

	MRSyntaxLineResult result;
	result.stateOut = previousState;
	return result;
}
