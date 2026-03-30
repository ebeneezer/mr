#include "TMRSyntax.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

std::string lowerCopy(const std::string &value) {
	std::string result = value;
	for (std::size_t i = 0; i < result.size(); ++i)
		result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
	return result;
}

std::string upperCopy(const std::string &value) {
	std::string result = value;
	for (std::size_t i = 0; i < result.size(); ++i)
		result[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[i])));
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

bool isWordStart(char ch) {
	unsigned char u = static_cast<unsigned char>(ch);
	return std::isalpha(u) || ch == '_';
}

bool isWordChar(char ch) {
	unsigned char u = static_cast<unsigned char>(ch);
	return std::isalnum(u) || ch == '_';
}

bool matchesWord(const std::string &word, const char *const *table, std::size_t count) {
	for (std::size_t i = 0; i < count; ++i)
		if (word == table[i])
			return true;
	return false;
}

void paint(TMRSyntaxTokenMap &tokens, std::size_t start, std::size_t end, TMRSyntaxToken token) {
	if (start > tokens.size())
		start = tokens.size();
	if (end > tokens.size())
		end = tokens.size();
	for (std::size_t i = start; i < end; ++i)
		tokens[i] = token;
}

std::size_t lineEndOf(const std::string &text, std::size_t lineStart) {
	while (lineStart < text.size() && text[lineStart] != '\r' && text[lineStart] != '\n')
		++lineStart;
	return lineStart;
}

void tokenizeString(TMRSyntaxTokenMap &tokens, const std::string &line, std::size_t &i, char quote,
                    bool doubledQuoteEscape = false) {
	std::size_t start = i++;
	while (i < line.size()) {
		if (!doubledQuoteEscape && line[i] == '\\' && i + 1 < line.size()) {
			i += 2;
			continue;
		}
		if (line[i] == quote) {
			if (doubledQuoteEscape && i + 1 < line.size() && line[i + 1] == quote) {
				i += 2;
				continue;
			}
			++i;
			break;
		}
		++i;
	}
	paint(tokens, start, i, TMRSyntaxToken::String);
}

void tokenizeCFamily(TMRSyntaxTokenMap &tokens, const std::string &line) {
	static const char *const keywords[] = {
	    "break",    "case",     "catch",   "class",    "const",    "continue", "default", "delete",
	    "do",       "else",     "enum",    "explicit", "extern",   "for",      "friend",  "goto",
	    "if",       "inline",   "namespace", "new",    "operator", "private",  "protected",
	    "public",   "return",   "sizeof",  "static",   "struct",   "switch",   "template", "throw",
	    "try",      "typedef",  "typename", "union",   "using",    "virtual",  "while",   "constexpr",
	    "noexcept", "override", "final",   "auto",     "decltype"
	};
	static const char *const types[] = {
	    "bool",       "char",      "double", "float",   "int",        "long",   "short",
	    "signed",     "size_t",    "ssize_t","std",     "string",     "unsigned","void",
	    "wchar_t",    "uint8_t",   "uint16_t","uint32_t","uint64_t",  "int8_t", "int16_t",
	    "int32_t",    "int64_t",   "FILE",   "Boolean", "TStringView","TColorAttr", "TAttrPair"
	};

	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') {
			paint(tokens, i, line.size(), TMRSyntaxToken::Comment);
			break;
		}
		if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '*') {
			std::size_t start = i;
			i += 2;
			while (i + 1 < line.size() && !(line[i] == '*' && line[i + 1] == '/'))
				++i;
			if (i + 1 < line.size())
				i += 2;
			paint(tokens, start, i, TMRSyntaxToken::Comment);
			continue;
		}
		if (line[i] == '"' || line[i] == '\'') {
			char quote = line[i];
			tokenizeString(tokens, line, i, quote);
			continue;
		}
		if ((line[i] == '#' && line.find_first_not_of(" \t") == i)) {
			paint(tokens, i, line.size(), TMRSyntaxToken::Directive);
			break;
		}
		if (std::isdigit(static_cast<unsigned char>(line[i]))) {
			std::size_t start = i++;
			while (i < line.size()) {
				char ch = line[i];
				if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '_' || ch == 'x' ||
				    ch == 'X')
					++i;
				else
					break;
			}
			paint(tokens, start, i, TMRSyntaxToken::Number);
			continue;
		}
		if (isWordStart(line[i])) {
			std::size_t start = i++;
			while (i < line.size() && isWordChar(line[i]))
				++i;
			std::string word = line.substr(start, i - start);
			if (matchesWord(word, keywords, sizeof(keywords) / sizeof(keywords[0])))
				paint(tokens, start, i, TMRSyntaxToken::Keyword);
			else if (matchesWord(word, types, sizeof(types) / sizeof(types[0])))
				paint(tokens, start, i, TMRSyntaxToken::Type);
			continue;
		}
		++i;
	}
}

void tokenizeMRMAC(TMRSyntaxTokenMap &tokens, const std::string &line) {
	static const char *const keywords[] = {
	    "SMACRO_FILE", "END_MACRO", "DEF_INT", "DEF_STR", "DEF_CHAR", "DEF_REAL", "IF",   "THEN",
	    "ELSE",        "END",       "WHILE",   "DO",      "TVCALL",   "CALL",     "RET",  "GOTO",
	    "TO",          "FROM",      "TRANS",   "DUMP",    "PERM",     "AND",      "OR",   "NOT",
	    "SHL",         "SHR",       "MOD"
	};

	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '{') {
			std::size_t start = i++;
			int depth = 1;
			while (i < line.size() && depth > 0) {
				if (line[i] == '{')
					++depth;
				else if (line[i] == '}')
					--depth;
				++i;
			}
			paint(tokens, start, i, TMRSyntaxToken::Comment);
			continue;
		}
		if (line[i] == '\'') {
			tokenizeString(tokens, line, i, '\'', true);
			continue;
		}
		if (line[i] == '<') {
			std::size_t start = i++;
			while (i < line.size() && line[i] != '>')
				++i;
			if (i < line.size())
				++i;
			paint(tokens, start, i, TMRSyntaxToken::Directive);
			continue;
		}
		if (line[i] == '$' && i + 1 < line.size() && std::isxdigit(static_cast<unsigned char>(line[i + 1]))) {
			std::size_t start = i;
			i += 2;
			while (i < line.size() && std::isxdigit(static_cast<unsigned char>(line[i])))
				++i;
			paint(tokens, start, i, TMRSyntaxToken::Number);
			continue;
		}
		if (line.compare(i, 6, "$MACRO") == 0 || line.compare(i, 6, "$macro") == 0) {
			paint(tokens, i, i + 6, TMRSyntaxToken::Directive);
			i += 6;
			continue;
		}
		if (std::isdigit(static_cast<unsigned char>(line[i]))) {
			std::size_t start = i++;
			while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
				++i;
			paint(tokens, start, i, TMRSyntaxToken::Number);
			continue;
		}
		if (isWordStart(line[i])) {
			std::size_t start = i++;
			while (i < line.size() && isWordChar(line[i]))
				++i;
			std::string word = upperCopy(line.substr(start, i - start));
			if (matchesWord(word, keywords, sizeof(keywords) / sizeof(keywords[0])))
				paint(tokens, start, i, TMRSyntaxToken::Keyword);
			continue;
		}
		++i;
	}
}

void tokenizeMake(TMRSyntaxTokenMap &tokens, const std::string &line) {
	std::size_t trimmed = line.find_first_not_of(" \t");
	if (trimmed != std::string::npos && line[trimmed] == '#') {
		paint(tokens, trimmed, line.size(), TMRSyntaxToken::Comment);
		return;
	}

	if (trimmed != std::string::npos && line[trimmed] != '\t') {
		std::size_t colon = line.find(':', trimmed);
		std::size_t eq = line.find('=', trimmed);
		if (colon != std::string::npos && (eq == std::string::npos || colon < eq))
			paint(tokens, trimmed, colon, TMRSyntaxToken::Key);
		else if (eq != std::string::npos)
			paint(tokens, trimmed, eq, TMRSyntaxToken::Directive);
	}

	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '#') {
			paint(tokens, i, line.size(), TMRSyntaxToken::Comment);
			break;
		}
		if (line[i] == '$' && i + 1 < line.size() && (line[i + 1] == '(' || line[i + 1] == '{')) {
			char closer = line[i + 1] == '(' ? ')' : '}';
			std::size_t start = i;
			i += 2;
			while (i < line.size() && line[i] != closer)
				++i;
			if (i < line.size())
				++i;
			paint(tokens, start, i, TMRSyntaxToken::Directive);
			continue;
		}
		++i;
	}
}

void tokenizeJson(TMRSyntaxTokenMap &tokens, const std::string &line) {
	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '"') {
			std::size_t start = i;
			tokenizeString(tokens, line, i, '"');
			std::size_t j = i;
			while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j])))
				++j;
			if (j < line.size() && line[j] == ':')
				paint(tokens, start, i, TMRSyntaxToken::Key);
			continue;
		}
		if (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '-') {
			std::size_t start = i++;
			while (i < line.size()) {
				char ch = line[i];
				if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == 'e' || ch == 'E' ||
				    ch == '+' || ch == '-')
					++i;
				else
					break;
			}
			paint(tokens, start, i, TMRSyntaxToken::Number);
			continue;
		}
		if (std::strncmp(line.c_str() + i, "true", 4) == 0 || std::strncmp(line.c_str() + i, "null", 4) == 0) {
			paint(tokens, i, i + 4, TMRSyntaxToken::Keyword);
			i += 4;
			continue;
		}
		if (std::strncmp(line.c_str() + i, "false", 5) == 0) {
			paint(tokens, i, i + 5, TMRSyntaxToken::Keyword);
			i += 5;
			continue;
		}
		++i;
	}
}

void tokenizeIni(TMRSyntaxTokenMap &tokens, const std::string &line) {
	std::size_t trimmed = line.find_first_not_of(" \t");
	if (trimmed == std::string::npos)
		return;
	if (line[trimmed] == ';' || line[trimmed] == '#') {
		paint(tokens, trimmed, line.size(), TMRSyntaxToken::Comment);
		return;
	}
	if (line[trimmed] == '[') {
		std::size_t end = line.find(']', trimmed + 1);
		if (end == std::string::npos)
			end = line.size();
		else
			++end;
		paint(tokens, trimmed, end, TMRSyntaxToken::Section);
		return;
	}

	std::size_t sep = line.find_first_of("=:", trimmed);
	if (sep != std::string::npos)
		paint(tokens, trimmed, sep, TMRSyntaxToken::Key);

	for (std::size_t i = sep == std::string::npos ? trimmed : sep + 1; i < line.size();) {
		if (line[i] == '"' || line[i] == '\'') {
			char quote = line[i];
			tokenizeString(tokens, line, i, quote);
			continue;
		}
		if (std::isdigit(static_cast<unsigned char>(line[i]))) {
			std::size_t start = i++;
			while (i < line.size() && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.'))
				++i;
			paint(tokens, start, i, TMRSyntaxToken::Number);
			continue;
		}
		++i;
	}
}

void tokenizeMarkdown(TMRSyntaxTokenMap &tokens, const std::string &line) {
	std::size_t trimmed = line.find_first_not_of(" \t");
	if (trimmed == std::string::npos)
		return;
	if (line.compare(trimmed, 3, "```") == 0) {
		paint(tokens, trimmed, line.size(), TMRSyntaxToken::Directive);
		return;
	}
	if (line[trimmed] == '#') {
		std::size_t i = trimmed;
		while (i < line.size() && line[i] == '#')
			++i;
		if (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
			paint(tokens, trimmed, line.size(), TMRSyntaxToken::Heading);
			return;
		}
	}
	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '`') {
			std::size_t start = i++;
			while (i < line.size() && line[i] != '`')
				++i;
			if (i < line.size())
				++i;
			paint(tokens, start, i, TMRSyntaxToken::String);
			continue;
		}
		++i;
	}
}

} // namespace

TMRSyntaxLanguage tmrDetectSyntaxLanguage(const std::string &path, const std::string &title) {
	std::string fileName = fileNamePart(!path.empty() ? path : title);
	std::string lowerName = lowerCopy(fileName);
	std::string ext = extensionPart(fileName);

	if (lowerName == "makefile" || lowerName == "gnumakefile" || ext == ".mk" || ext == ".mak")
		return TMRSyntaxLanguage::Make;
	if (ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".h" || ext == ".hh" ||
	    ext == ".hpp" || ext == ".hxx" || ext == ".l" || ext == ".y")
		return TMRSyntaxLanguage::CFamily;
	if (ext == ".mrmac" || ext == ".mr")
		return TMRSyntaxLanguage::MRMAC;
	if (ext == ".json")
		return TMRSyntaxLanguage::Json;
	if (ext == ".ini" || ext == ".cfg" || ext == ".conf")
		return TMRSyntaxLanguage::Ini;
	if (ext == ".md" || ext == ".markdown" || lowerName == "readme")
		return TMRSyntaxLanguage::Markdown;
	return TMRSyntaxLanguage::PlainText;
}

const char *tmrSyntaxLanguageName(TMRSyntaxLanguage language) noexcept {
	switch (language) {
		case TMRSyntaxLanguage::CFamily:
			return "C/C++";
		case TMRSyntaxLanguage::MRMAC:
			return "MRMAC";
		case TMRSyntaxLanguage::Make:
			return "Make";
		case TMRSyntaxLanguage::Json:
			return "JSON";
		case TMRSyntaxLanguage::Ini:
			return "INI";
		case TMRSyntaxLanguage::Markdown:
			return "Markdown";
		default:
			return "Plain Text";
	}
}

TMRSyntaxTokenMap tmrBuildTokenMapForLine(TMRSyntaxLanguage language, const std::string &text,
                                          std::size_t lineStart) {
	if (lineStart > text.size())
		lineStart = text.size();
	std::size_t lineEnd = lineEndOf(text, lineStart);
	return tmrBuildTokenMapForTextLine(language, text.substr(lineStart, lineEnd - lineStart));
}

TMRSyntaxTokenMap tmrBuildTokenMapForTextLine(TMRSyntaxLanguage language, const std::string &line) {
	TMRSyntaxTokenMap tokens(line.size(), TMRSyntaxToken::Text);

	switch (language) {
		case TMRSyntaxLanguage::CFamily:
			tokenizeCFamily(tokens, line);
			break;
		case TMRSyntaxLanguage::MRMAC:
			tokenizeMRMAC(tokens, line);
			break;
		case TMRSyntaxLanguage::Make:
			tokenizeMake(tokens, line);
			break;
		case TMRSyntaxLanguage::Json:
			tokenizeJson(tokens, line);
			break;
		case TMRSyntaxLanguage::Ini:
			tokenizeIni(tokens, line);
			break;
		case TMRSyntaxLanguage::Markdown:
			tokenizeMarkdown(tokens, line);
			break;
		default:
			break;
	}
	return tokens;
}
