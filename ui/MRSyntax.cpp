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
constexpr std::uint16_t kSyntaxFlagPascalDirective = 0x0008;
constexpr std::uint16_t kSyntaxFlagPayloadLengthShift = 8;
constexpr std::uint16_t kSyntaxFlagPayloadLengthMask = 0xFF00;
constexpr std::uint32_t kSyntaxPayloadXmlComment = 1;
constexpr std::uint32_t kSyntaxPayloadXmlCdata = 2;
constexpr std::uint32_t kSyntaxPayloadXmlDirective = 3;
constexpr std::uint32_t kSyntaxPayloadXmlProcessing = 4;

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

const char *const kBashKeywords[] = {
	"alias", "break", "case", "continue", "coproc", "declare", "do", "done", "elif", "else", "esac", "eval", "exec", "export", "false", "fi", "for", "function", "if", "in",
	"local", "readonly", "return", "select", "set", "shopt", "source", "then", "time", "true", "typeset", "until", "while"
};

const char *const kBashBuiltins[] = {
	"bind", "builtin", "cd", "command", "compgen", "complete", "dirs", "disown", "echo", "enable", "fc", "getopts", "hash", "help", "history", "jobs", "kill", "let", "mapfile", "popd",
	"printf", "pushd", "pwd", "read", "readarray", "set", "shift", "test", "trap", "type", "ulimit", "umask", "unalias", "unset", "wait"
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

const char *const kFishKeywords[] = {
	"and", "begin", "break", "builtin", "case", "command", "continue", "else", "end", "for", "function", "if", "in", "not", "or", "return", "switch", "time", "while"
};

const char *const kFishBuiltins[] = {
	"abbr", "argparse", "bg", "bind", "builtin", "cd", "command", "complete", "contains", "count", "echo", "emit", "eval", "exec", "fg", "math", "path", "printf", "pwd", "read", "set", "set_color",
	"source", "status", "string", "test", "type", "ulimit", "wait"
};

const char *const kPerlKeywords[] = {
	"BEGIN",   "CHECK", "END",    "INIT",   "UNITCHECK", "and",    "cmp",    "continue", "default", "defined", "do",      "else",    "elsif", "eq",     "for",   "foreach",
	"ge",      "given", "goto",   "gt",     "if",        "last",   "le",     "local",    "lt",      "my",      "ne",      "next",    "no",    "our",    "package","redo",
	"require", "return","say",    "state",  "sub",       "undef",  "unless", "until",    "use",     "when",    "while",   "xor"
};

const char *const kSwiftKeywords[] = {
	"actor", "as", "associatedtype", "async", "await", "break", "case", "catch", "class", "continue", "default", "defer", "deinit", "do", "else", "enum", "extension", "fallthrough",
	"for", "func", "guard", "if", "import", "in", "init", "inout", "internal", "let", "mutating", "nonisolated", "open", "operator", "private", "protocol", "public", "repeat", "rethrows",
	"return", "static", "struct", "subscript", "switch", "throw", "throws", "try", "typealias", "var", "where", "while"
};

const char *const kSwiftConstants[] = {
	"false", "nil", "Self", "self", "super", "true"
};

const char *const kSwiftTypeKeywords[] = {
	"Any", "AnyObject", "Bool", "Character", "Double", "Float", "Int", "Int8", "Int16", "Int32", "Int64", "Never", "String", "UInt", "UInt8", "UInt16", "UInt32", "UInt64", "Void"
};

const char *const kRustKeywords[] = {
	"as",       "async",   "await",    "break",     "const",   "continue", "crate",   "dyn",     "else",   "enum",   "extern", "fn",      "for",    "if",      "impl",
	"in",       "let",     "loop",     "macro_rules","match",   "mod",      "move",    "mut",     "pub",    "ref",    "return", "self",    "Self",   "static",  "struct",
	"super",    "trait",   "type",     "union",     "unsafe",  "use",      "where",   "while",   "yield"
};

const char *const kRustConstants[] = {
	"Err", "false", "None", "Ok", "Some", "true"
};

const char *const kRustTypeKeywords[] = {
	"bool", "char", "f32", "f64", "i8", "i16", "i32", "i64", "i128", "isize", "str", "u8", "u16", "u32", "u64", "u128", "usize"
};

const char *const kGoKeywords[] = {
	"break", "case", "chan", "const", "continue", "default", "defer", "else", "fallthrough", "for", "func", "go", "goto", "if", "import", "interface", "map", "package", "range",
	"return", "select", "struct", "switch", "type", "var"
};

const char *const kGoConstants[] = {
	"false", "iota", "nil", "true"
};

const char *const kGoTypeKeywords[] = {
	"any", "bool", "byte", "comparable", "complex64", "complex128", "error", "float32", "float64", "int", "int8", "int16", "int32", "int64", "rune", "string", "uint", "uint8", "uint16",
	"uint32", "uint64", "uintptr"
};

const char *const kKotlinKeywords[] = {
	"as", "break", "by", "catch", "class", "companion", "constructor", "continue", "data", "do", "dynamic", "else", "enum", "expect", "external", "finally", "for", "fun", "if",
	"import", "in", "infix", "init", "inline", "interface", "internal", "is", "lateinit", "noinline", "object", "operator", "out", "override", "package", "private", "protected", "public",
	"reified", "return", "sealed", "suspend", "tailrec", "this", "throw", "try", "typealias", "val", "var", "vararg", "when", "where", "while"
};

const char *const kKotlinConstants[] = {
	"false", "null", "super", "true"
};

const char *const kKotlinTypeKeywords[] = {
	"Any", "Boolean", "Byte", "Char", "Double", "Float", "Int", "Long", "Nothing", "Short", "String", "Unit", "UByte", "UInt", "ULong", "UShort"
};

const char *const kCSharpKeywords[] = {
	"abstract", "as", "async", "await", "base", "break", "case", "catch", "checked", "class", "const", "continue", "default", "delegate", "do", "else", "enum", "event",
	"explicit", "extern", "finally", "fixed", "for", "foreach", "get", "global", "goto", "if", "implicit", "in", "init", "interface", "internal", "is", "lock", "namespace", "new",
	"operator", "out", "override", "params", "partial", "private", "protected", "public", "readonly", "record", "ref", "required", "return", "scoped", "sealed", "set", "sizeof",
	"stackalloc", "static", "struct", "switch", "this", "throw", "try", "typeof", "unchecked", "unsafe", "using", "virtual", "volatile", "when", "where", "while", "yield"
};

const char *const kCSharpConstants[] = {
	"false", "null", "true"
};

const char *const kCSharpTypeKeywords[] = {
	"bool", "byte", "char", "decimal", "double", "dynamic", "float", "int", "long", "nint", "nuint", "object", "sbyte", "short", "string", "uint", "ulong", "ushort", "var", "void"
};

const char *const kPascalKeywords[] = {
	"AND", "ARRAY", "ASM", "BEGIN", "CASE", "CLASS", "COMP", "CONST", "CONSTRUCTOR", "DESTRUCTOR", "DIV", "DO", "DOWNTO", "ELSE", "END", "EXCEPT", "EXIT", "EXTERNAL", "FILE", "FINALLY", "FOR",
	"FUNCTION", "GOTO", "IF", "IMPLEMENTATION", "INTERFACE", "LABEL", "MOD", "NOT", "OBJECT", "OF", "OR", "ORD", "PRIVATE", "PROCEDURE", "PROGRAM", "PROPERTY", "PROTECTED", "PUBLIC", "PUBLISHED",
	"RECORD", "REPEAT", "SHL", "SHR", "THEN", "TO", "TRY", "TYPE", "UNIT", "UNTIL", "USES", "VAR", "WHILE", "WITH", "XOR"
};

const char *const kPascalTypeKeywords[] = {
	"BOOLEAN", "BYTE", "CHAR", "DOUBLE", "EXTENDED", "INTEGER", "LONGINT", "REAL", "SHORTINT", "SINGLE", "STRING", "WORD"
};

const char *const kPascalConstants[] = {
	"FALSE", "NIL", "TRUE"
};

const char *const kSystemdSections[] = {
	"[UNIT]", "[SERVICE]", "[SOCKET]", "[TIMER]", "[MOUNT]", "[AUTOMOUNT]", "[TARGET]", "[PATH]", "[SLICE]", "[SCOPE]", "[SWAP]", "[DEVICE]", "[INSTALL]", "[LINK]", "[NETDEV]", "[MATCH]",
	"[NETWORK]", "[ADDRESS]", "[ROUTE]", "[DHCPV4]", "[DHCPV6]", "[BRIDGE]", "[VLAN]"
};

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

static bool isYamlIdentifierChar(char ch) {
	unsigned char value = static_cast<unsigned char>(ch);
	return std::isalnum(value) || ch == '_' || ch == '-';
}

static bool isXmlNameStartChar(char ch) {
	unsigned char value = static_cast<unsigned char>(ch);
	return std::isalpha(value) || ch == '_' || ch == ':';
}

static bool isXmlNameChar(char ch) {
	unsigned char value = static_cast<unsigned char>(ch);
	return std::isalnum(value) || ch == '_' || ch == ':' || ch == '-' || ch == '.';
}

static std::size_t skipWhitespaceView(std::string_view text, std::size_t pos = 0) {
	while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
		++pos;
	return pos;
}

static std::string upperCopyView(std::string_view value) {
	std::string result(value);
	for (char &ch : result)
		ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	return result;
}

static std::string_view trimWhitespaceView(std::string_view text) noexcept {
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
	return text;
}

static std::size_t findYamlCommentStart(std::string_view text) noexcept {
	bool inSingleQuote = false;
	bool inDoubleQuote = false;

	for (std::size_t i = 0; i < text.size(); ++i) {
		const char ch = text[i];
		if (inSingleQuote) {
			if (ch == '\'' && i + 1 < text.size() && text[i + 1] == '\'') {
				++i;
				continue;
			}
			if (ch == '\'') inSingleQuote = false;
			continue;
		}
		if (inDoubleQuote) {
			if (ch == '\\' && i + 1 < text.size()) {
				++i;
				continue;
			}
			if (ch == '"') inDoubleQuote = false;
			continue;
		}
		if (ch == '\'') {
			inSingleQuote = true;
			continue;
		}
		if (ch == '"') {
			inDoubleQuote = true;
			continue;
		}
		if (ch == '#' && (i == 0 || std::isspace(static_cast<unsigned char>(text[i - 1])) != 0)) return i;
	}
	return std::string_view::npos;
}

static std::size_t findYamlKeyDelimiter(std::string_view text) noexcept {
	bool inSingleQuote = false;
	bool inDoubleQuote = false;
	int flowDepth = 0;
	std::size_t i = skipWhitespaceView(text);

	if (i < text.size() && text[i] == '-') {
		const std::size_t next = i + 1;
		if (next < text.size() && std::isspace(static_cast<unsigned char>(text[next])) != 0) i = skipWhitespaceView(text, next + 1);
	}

	for (; i < text.size(); ++i) {
		const char ch = text[i];
		if (inSingleQuote) {
			if (ch == '\'' && i + 1 < text.size() && text[i + 1] == '\'') {
				++i;
				continue;
			}
			if (ch == '\'') inSingleQuote = false;
			continue;
		}
		if (inDoubleQuote) {
			if (ch == '\\' && i + 1 < text.size()) {
				++i;
				continue;
			}
			if (ch == '"') inDoubleQuote = false;
			continue;
		}
		if (ch == '\'') {
			inSingleQuote = true;
			continue;
		}
		if (ch == '"') {
			inDoubleQuote = true;
			continue;
		}
		if (ch == '[' || ch == '{') {
			++flowDepth;
			continue;
		}
		if ((ch == ']' || ch == '}') && flowDepth > 0) {
			--flowDepth;
			continue;
		}
		if (flowDepth > 0) continue;
		if (ch != ':') continue;
		if (i + 1 < text.size()) {
			const char next = text[i + 1];
			if (!(std::isspace(static_cast<unsigned char>(next)) != 0 || next == '#' || next == '|' || next == '>' || next == '[' || next == '{')) continue;
		}
		return i;
	}
	return std::string_view::npos;
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

static bool isPascalDelimiterChar(char ch) {
	switch (ch) {
		case '@':
		case '(':
		case ')':
		case '=':
		case '*':
		case '+':
		case '-':
		case '/':
		case '[':
		case ']':
		case '.':
		case ',':
		case '$':
		case '#':
		case '<':
		case '>':
		case ':':
		case ';':
			return true;
		default:
			return false;
	}
}

static std::size_t pascalDelimiterLength(std::string_view line, std::size_t pos) {
	if (pos + 1 >= line.size()) return 0;
	const char ch0 = line[pos];
	const char ch1 = line[pos + 1];

	if ((ch0 == ':' && ch1 == '=') || (ch0 == '<' && ch1 == '=') || (ch0 == '>' && ch1 == '=') || (ch0 == '<' && ch1 == '>') || (ch0 == '.' && ch1 == '.'))
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

static bool isSwiftTypeIntroducer(std::string_view word) {
	return word == "actor" || word == "associatedtype" || word == "class" || word == "enum" || word == "extension" || word == "protocol" || word == "struct" || word == "typealias";
}

static bool isSwiftCallableIntroducer(std::string_view word) {
	return word == "func" || word == "init" || word == "deinit" || word == "subscript";
}

static bool isRustTypeIntroducer(std::string_view word) {
	return word == "enum" || word == "impl" || word == "mod" || word == "struct" || word == "trait" || word == "type" || word == "union";
}

static bool isRustCallableIntroducer(std::string_view word) {
	return word == "fn" || word == "macro_rules";
}

static bool isGoTypeIntroducer(std::string_view word) {
	return word == "interface" || word == "struct" || word == "type";
}

static bool isGoCallableIntroducer(std::string_view word) {
	return word == "func";
}

static bool isKotlinTypeIntroducer(std::string_view word) {
	return word == "class" || word == "interface" || word == "object" || word == "typealias";
}

static bool isKotlinCallableIntroducer(std::string_view word) {
	return word == "fun" || word == "constructor";
}

static bool isCSharpTypeIntroducer(std::string_view word) {
	return word == "class" || word == "interface" || word == "record" || word == "struct" || word == "enum" || word == "namespace";
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

static bool findRustRawStringStart(std::string_view line, std::size_t pos, std::size_t &prefixEnd, std::size_t &hashCount) {
	std::size_t cursor = pos;

	if (cursor >= line.size()) return false;
	if (line.compare(cursor, 2, "br") == 0 || line.compare(cursor, 2, "rb") == 0) cursor += 2;
	else if (line[cursor] == 'r')
		++cursor;
	else
		return false;

	hashCount = 0;
	while (cursor < line.size() && line[cursor] == '#') {
		++hashCount;
		++cursor;
	}
	if (cursor >= line.size() || line[cursor] != '"') return false;
	prefixEnd = cursor + 1;
	return true;
}

static std::size_t findRustRawStringEnd(std::string_view line, std::size_t contentStart, std::size_t hashCount) {
	std::size_t cursor = contentStart;

	while (cursor < line.size()) {
		if (line[cursor] != '"') {
			++cursor;
			continue;
		}
		std::size_t end = cursor + 1;
		std::size_t matchedHashes = 0;
		while (matchedHashes < hashCount && end < line.size() && line[end] == '#') {
			++matchedHashes;
			++end;
		}
		if (matchedHashes == hashCount) return end;
		++cursor;
	}
	return std::string_view::npos;
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

static std::size_t consumeCSharpVerbatimStringLiteral(std::string_view line, std::size_t start) {
	std::size_t i = start + 2;

	while (i < line.size()) {
		if (line[i] == '"') {
			if (i + 1 < line.size() && line[i + 1] == '"') {
				i += 2;
				continue;
			}
			return i + 1;
		}
		++i;
	}
	return line.size();
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

static bool isFishCommandChar(char ch) {
	return isIdentifierChar(ch) || ch == '-';
}

static bool isFishCommentStart(std::string_view line, std::size_t pos) {
	if (pos >= line.size() || line[pos] != '#') return false;
	if (pos == 0) return true;
	const char previous = line[pos - 1];
	return std::isspace(static_cast<unsigned char>(previous)) != 0 || previous == ';' || previous == '(' || previous == ')' || previous == '|';
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

static std::size_t skipXmlName(std::string_view text, std::size_t pos) {
	if (pos >= text.size() || !isXmlNameStartChar(text[pos])) return pos;
	++pos;
	while (pos < text.size() && isXmlNameChar(text[pos]))
		++pos;
	return pos;
}

static std::size_t findXmlCommentEnd(std::string_view line, std::size_t start) {
	const std::size_t end = line.find("-->", start);
	return end == std::string_view::npos ? std::string_view::npos : end + 3;
}

static std::size_t findXmlCdataEnd(std::string_view line, std::size_t start) {
	const std::size_t end = line.find("]]>", start);
	return end == std::string_view::npos ? std::string_view::npos : end + 3;
}

static std::size_t findXmlMarkupEnd(std::string_view line, std::size_t start) {
	char quote = '\0';

	for (std::size_t i = start; i < line.size(); ++i) {
		const char ch = line[i];
		if (quote != '\0') {
			if (ch == quote) quote = '\0';
			continue;
		}
		if (ch == '"' || ch == '\'') {
			quote = ch;
			continue;
		}
		if (ch == '>') return i + 1;
	}
	return std::string_view::npos;
}

static std::size_t findXmlProcessingEnd(std::string_view line, std::size_t start) {
	char quote = '\0';

	for (std::size_t i = start; i < line.size(); ++i) {
		const char ch = line[i];
		if (quote != '\0') {
			if (ch == quote) quote = '\0';
			continue;
		}
		if (ch == '"' || ch == '\'') {
			quote = ch;
			continue;
		}
		if (ch == '?' && i + 1 < line.size() && line[i + 1] == '>') return i + 2;
	}
	return std::string_view::npos;
}

static void appendXmlTagRuns(std::vector<MRSyntaxTokenRun> &runs, std::string_view line, std::size_t start, std::size_t end) {
	if (start >= end || start >= line.size() || line[start] != '<') return;

	std::size_t i = start;
	appendRun(runs, i, i + 1, MRSyntaxToken::Delimiter);
	++i;

	if (i < end && (line[i] == '/' || line[i] == '?')) {
		appendRun(runs, i, i + 1, MRSyntaxToken::Delimiter);
		++i;
	}

	if (i < end && isXmlNameStartChar(line[i])) {
		const std::size_t nameEnd = std::min(skipXmlName(line, i), end);
		appendRun(runs, i, nameEnd, MRSyntaxToken::Keyword);
		i = nameEnd;
	}

	while (i < end) {
		if (line[i] == '"' || line[i] == '\'') {
			const std::size_t stringStart = i;
			const char quote = line[i++];
			while (i < end && line[i] != quote)
				++i;
			if (i < end) ++i;
			appendRun(runs, stringStart, i, MRSyntaxToken::String);
			continue;
		}
		if (i + 1 < end && line[i] == '/' && line[i + 1] == '>') {
			appendRun(runs, i, i + 2, MRSyntaxToken::Delimiter);
			i += 2;
			continue;
		}
		if (line[i] == '>' || line[i] == '/' || line[i] == '=' || line[i] == '?') {
			appendRun(runs, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}
		if (isXmlNameStartChar(line[i])) {
			const std::size_t nameEnd = std::min(skipXmlName(line, i), end);
			appendRun(runs, i, nameEnd, MRSyntaxToken::Key);
			i = nameEnd;
			continue;
		}
		++i;
	}
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

void tokenizeBash(MRSyntaxTokenMap &tokens, const std::string &line) {
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
			if (wordInList(word, kBashKeywords, sizeof(kBashKeywords) / sizeof(kBashKeywords[0]))) {
				paint(tokens, start, i, MRSyntaxToken::Keyword);
				expectCommand = word == "then" || word == "do" || word == "else" || word == "elif" || word == "if" || word == "for" || word == "while" || word == "until" || word == "case" || word == "select" || word == "function";
				continue;
			}
			if (expectCommand && wordInList(word, kBashBuiltins, sizeof(kBashBuiltins) / sizeof(kBashBuiltins[0]))) {
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

void tokenizeFish(MRSyntaxTokenMap &tokens, const std::string &line) {
	std::size_t trimmed = skipWhitespace(line);
	if (trimmed != std::string::npos && line.compare(trimmed, 2, "#!") == 0) {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Directive);
		return;
	}

	bool expectCommand = true;
	bool expectFunctionName = false;
	for (std::size_t i = 0; i < line.size();) {
		if (isFishCommentStart(line, i)) {
			paint(tokens, i, line.size(), MRSyntaxToken::Comment);
			break;
		}
		if (line[i] == '\'' || line[i] == '"') {
			const std::size_t start = i;
			const std::size_t end = consumeZshStringLiteral(line, i, line[i]);
			paint(tokens, start, end, MRSyntaxToken::String);
			i = end;
			expectCommand = false;
			expectFunctionName = false;
			continue;
		}
		if (line[i] == '$') {
			const std::size_t start = i;
			++i;
			if (i < line.size() && isIdentifierStart(line[i])) {
				++i;
				while (i < line.size() && isIdentifierChar(line[i]))
					++i;
				if (i < line.size() && line[i] == '[') {
					const std::size_t end = consumeBalancedRegion(line, i, "[", "]");
					i = end;
				}
				paint(tokens, start, i, MRSyntaxToken::Directive);
				expectCommand = false;
				expectFunctionName = false;
				continue;
			}
			paint(tokens, start, i, MRSyntaxToken::Delimiter);
			expectCommand = false;
			expectFunctionName = false;
			continue;
		}
		if (line[i] == '(') {
			const std::size_t end = consumeBalancedRegion(line, i, "(", ")");
			if (end > i) {
				paint(tokens, i, end, MRSyntaxToken::Directive);
				i = end;
				expectCommand = false;
				expectFunctionName = false;
				continue;
			}
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
				expectFunctionName = false;
				continue;
			}
		}
		if (isIdentifierStart(line[i])) {
			const std::size_t start = i++;
			while (i < line.size() && isFishCommandChar(line[i]))
				++i;
			const std::string_view word(line.data() + start, i - start);
			if (expectFunctionName) {
				paint(tokens, start, i, MRSyntaxToken::Key);
				expectCommand = false;
				expectFunctionName = false;
				continue;
			}
			if (wordInList(word, kFishKeywords, sizeof(kFishKeywords) / sizeof(kFishKeywords[0]))) {
				paint(tokens, start, i, MRSyntaxToken::Keyword);
				expectCommand = word == "and" || word == "or" || word == "begin" || word == "case" || word == "else" || word == "for" || word == "function" || word == "if" || word == "switch" || word == "while";
				expectFunctionName = word == "function";
				continue;
			}
			if (expectCommand && wordInList(word, kFishBuiltins, sizeof(kFishBuiltins) / sizeof(kFishBuiltins[0]))) {
				paint(tokens, start, i, MRSyntaxToken::Key);
				expectCommand = false;
				expectFunctionName = false;
				continue;
			}
			expectCommand = false;
			expectFunctionName = false;
			continue;
		}
		{
			const std::size_t delimiterLength = zshDelimiterLength(line, i);
			if (delimiterLength > 0) {
				paint(tokens, i, i + delimiterLength, MRSyntaxToken::Delimiter);
				expectCommand = true;
				expectFunctionName = false;
				i += delimiterLength;
				continue;
			}
		}
		if (isZshDelimiterChar(line[i])) {
			paint(tokens, i, i + 1, MRSyntaxToken::Delimiter);
			expectCommand = line[i] == ';' || line[i] == '|';
			expectFunctionName = false;
			++i;
			continue;
		}
		if (!std::isspace(static_cast<unsigned char>(line[i]))) {
			expectCommand = false;
			expectFunctionName = false;
		}
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

MRSyntaxLineResult MRBashSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
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
			tokenizeBash(suffixTokens, std::string(line.substr(end)));
			for (std::size_t i = 0; i < suffixTokens.size(); ++i)
				tokens[end + i] = suffixTokens[i];
		}
	} else
		tokenizeBash(tokens, std::string(line));

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

MRSyntaxLineResult MRFishSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	MRSyntaxTokenMap tokens(line.size(), MRSyntaxToken::Text);

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
			tokenizeFish(suffixTokens, std::string(line.substr(end)));
			for (std::size_t i = 0; i < suffixTokens.size(); ++i)
				tokens[end + i] = suffixTokens[i];
		}
	} else
		tokenizeFish(tokens, std::string(line));

	for (std::size_t i = 0; i < line.size();) {
		if (isFishCommentStart(line, i)) break;
		if (line[i] == '\'' || line[i] == '"') {
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

	result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
	return result;
}

static bool isSystemdSectionHeader(std::string_view trimmed) noexcept {
	if (trimmed.size() < 3 || trimmed.front() != '[' || trimmed.back() != ']') return false;
	for (std::size_t i = 0; i < sizeof(kSystemdSections) / sizeof(kSystemdSections[0]); ++i)
		if (trimmed == kSystemdSections[i]) return true;
	return false;
}

MRSyntaxLineResult MRSystemdSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = previousState;
	MRSyntaxTokenMap tokens(line.size(), MRSyntaxToken::Text);
	const std::string_view trimmed = trimWhitespaceView(line);

	if (trimmed.empty()) {
		result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
		return result;
	}
	if (trimmed.front() == '#' || trimmed.front() == ';') {
		paint(tokens, static_cast<std::size_t>(trimmed.data() - line.data()), line.size(), MRSyntaxToken::Comment);
		result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
		return result;
	}
	if (isSystemdSectionHeader(upperCopyView(trimmed))) {
		paint(tokens, static_cast<std::size_t>(trimmed.data() - line.data()), static_cast<std::size_t>(trimmed.data() - line.data()) + trimmed.size(), MRSyntaxToken::Section);
		result.tokenRuns = tmrBuildTokenRunsFromTokenMap(tokens);
		return result;
	}

	const std::size_t eq = trimmed.find('=');
	if (eq != std::string_view::npos && eq > 0) {
		const std::size_t keyStart = static_cast<std::size_t>(trimmed.data() - line.data());
		bool validKey = true;
		for (std::size_t i = 0; i < eq; ++i) {
			const char ch = trimmed[i];
			if (!(std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-')) {
				validKey = false;
				break;
			}
		}
		if (validKey) {
			paint(tokens, keyStart, keyStart + eq, MRSyntaxToken::Key);
			paint(tokens, keyStart + eq, keyStart + eq + 1, MRSyntaxToken::Delimiter);
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

MRSyntaxLineResult MRSwiftSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	std::size_t i = 0;
	std::uint32_t commentDepth = previousState.mode == MRSyntaxMode::BlockComment ? (previousState.payload == 0 ? 1U : previousState.payload) : 0;
	bool expectTypeName = false;
	bool expectCallableName = false;
	bool expectImportName = false;

	if (commentDepth > 0) {
		const std::size_t start = i;
		while (i < line.size()) {
			if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
				++commentDepth;
				i += 2;
				continue;
			}
			if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
				--commentDepth;
				i += 2;
				if (commentDepth == 0) break;
				continue;
			}
			++i;
		}
		appendRun(result.tokenRuns, start, i, MRSyntaxToken::Comment);
		if (commentDepth > 0) {
			result.stateOut.mode = MRSyntaxMode::BlockComment;
			result.stateOut.payload = commentDepth;
			return result;
		}
	}

	if (previousState.mode == MRSyntaxMode::QuotedString && (previousState.flags & kSyntaxFlagTripleQuoted) != 0) {
		const std::size_t end = findTripleQuotedStringEnd(line, 0, '"');
		if (end == std::string_view::npos) {
			appendRun(result.tokenRuns, 0, line.size(), MRSyntaxToken::String);
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.flags = kSyntaxFlagTripleQuoted;
			result.stateOut.payload = static_cast<std::uint32_t>('"');
			return result;
		}
		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		i = end;
	} else if (previousState.mode == MRSyntaxMode::QuotedString) {
		const char quote = static_cast<char>(previousState.payload);
		const std::size_t end = findStringContinuationEnd(line, 0, quote);
		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		if (end == line.size()) {
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.payload = previousState.payload;
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
			const std::size_t start = i;
			commentDepth = 1;
			i += 2;
			while (i < line.size()) {
				if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
					++commentDepth;
					i += 2;
					continue;
				}
				if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
					--commentDepth;
					i += 2;
					if (commentDepth == 0) break;
					continue;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, std::min(i, line.size()), MRSyntaxToken::Comment);
			if (commentDepth > 0) {
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				result.stateOut.payload = commentDepth;
				return result;
			}
			continue;
		}

		if (line[i] == '@') {
			const std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Directive);
			continue;
		}

		if (line[i] == '#') {
			const std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Directive);
			continue;
		}

		if (i + 2 < line.size() && line[i] == '"' && line[i + 1] == '"' && line[i + 2] == '"') {
			const std::size_t start = i;
			const std::size_t end = findTripleQuotedStringEnd(line, i + 3, '"');
			if (end == std::string_view::npos) {
				appendRun(result.tokenRuns, start, line.size(), MRSyntaxToken::String);
				result.stateOut.mode = MRSyntaxMode::QuotedString;
				result.stateOut.flags = kSyntaxFlagTripleQuoted;
				result.stateOut.payload = static_cast<std::uint32_t>('"');
				return result;
			}
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			i = end;
			continue;
		}

		if (line[i] == '"' || line[i] == '\'') {
			const std::size_t start = i;
			const std::size_t end = consumeCppStringLiteral(line, i, line[i]);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			i = end;
			continue;
		}

		if (isDecimalDigitChar(line[i]) || (line[i] == '.' && i + 1 < line.size() && isDecimalDigitChar(line[i + 1]))) {
			const std::size_t start = i;
			const std::size_t end = consumeCppNumber(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
			i = end;
			continue;
		}

		if (isIdentifierStart(line[i])) {
			const std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			const std::string_view word = line.substr(start, i - start);
			if (wordInList(word, kSwiftKeywords, sizeof(kSwiftKeywords) / sizeof(kSwiftKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
				expectTypeName = isSwiftTypeIntroducer(word);
				expectCallableName = isSwiftCallableIntroducer(word);
				expectImportName = word == "import";
				continue;
			}
			if (wordInList(word, kSwiftConstants, sizeof(kSwiftConstants) / sizeof(kSwiftConstants[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			if (wordInList(word, kSwiftTypeKeywords, sizeof(kSwiftTypeKeywords) / sizeof(kSwiftTypeKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			if (expectImportName || expectTypeName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				expectImportName = false;
				expectTypeName = false;
				continue;
			}
			if (expectCallableName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				expectCallableName = false;
				continue;
			}
			if (isUpperCaseIdentifier(word)) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			continue;
		}

		{
			const std::size_t delimiterLength = cppDelimiterLength(line, i);
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

MRSyntaxLineResult MRRustSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	std::size_t i = 0;
	std::uint32_t commentDepth = previousState.mode == MRSyntaxMode::BlockComment ? (previousState.payload == 0 ? 1U : previousState.payload) : 0;
	bool expectTypeName = false;
	bool expectCallableName = false;
	bool expectImportName = false;

	if (commentDepth > 0) {
		const std::size_t start = i;
		while (i < line.size()) {
			if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
				++commentDepth;
				i += 2;
				continue;
			}
			if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
				--commentDepth;
				i += 2;
				if (commentDepth == 0) break;
				continue;
			}
			++i;
		}
		appendRun(result.tokenRuns, start, i, MRSyntaxToken::Comment);
		if (commentDepth > 0) {
			result.stateOut.mode = MRSyntaxMode::BlockComment;
			result.stateOut.payload = commentDepth;
			return result;
		}
	}

	if (previousState.mode == MRSyntaxMode::RawString) {
		const std::size_t hashCount = payloadLength(previousState.flags);
		const std::size_t end = findRustRawStringEnd(line, 0, hashCount);
		if (end == std::string_view::npos) {
			appendRun(result.tokenRuns, 0, line.size(), MRSyntaxToken::String);
			result.stateOut.mode = MRSyntaxMode::RawString;
			result.stateOut.flags = previousState.flags;
			return result;
		}
		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		i = end;
	} else if (previousState.mode == MRSyntaxMode::QuotedString) {
		const char quote = static_cast<char>(previousState.payload);
		const std::size_t end = findStringContinuationEnd(line, 0, quote);
		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		if (end == line.size()) {
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.payload = previousState.payload;
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
			const std::size_t start = i;
			commentDepth = 1;
			i += 2;
			while (i < line.size()) {
				if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
					++commentDepth;
					i += 2;
					continue;
				}
				if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
					--commentDepth;
					i += 2;
					if (commentDepth == 0) break;
					continue;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, std::min(i, line.size()), MRSyntaxToken::Comment);
			if (commentDepth > 0) {
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				result.stateOut.payload = commentDepth;
				return result;
			}
			continue;
		}

		if (line[i] == '#' && i + 1 < line.size() && line[i + 1] == '[') {
			const std::size_t start = i;
			std::size_t depth = 0;
			while (i < line.size()) {
				if (line[i] == '[') {
					++depth;
					++i;
					continue;
				}
				if (line[i] == ']') {
					++i;
					if (depth > 0) --depth;
					if (depth == 0) break;
					continue;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, std::min(i, line.size()), MRSyntaxToken::Directive);
			continue;
		}

		{
			std::size_t prefixEnd = 0;
			std::size_t hashCount = 0;
			if (findRustRawStringStart(line, i, prefixEnd, hashCount)) {
				const std::size_t start = i;
				const std::size_t end = findRustRawStringEnd(line, prefixEnd, hashCount);
				if (end == std::string_view::npos) {
					appendRun(result.tokenRuns, start, line.size(), MRSyntaxToken::String);
					result.stateOut.mode = MRSyntaxMode::RawString;
					result.stateOut.flags = storePayloadLength(0, hashCount);
					return result;
				}
				appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
				i = end;
				continue;
			}
		}

		if (line[i] == '"' || line[i] == '\'') {
			const std::size_t start = i;
			const std::size_t end = consumeCppStringLiteral(line, i, line[i]);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			if (end == line.size() && (line.empty() || line.back() != line[start])) {
				result.stateOut.mode = MRSyntaxMode::QuotedString;
				result.stateOut.payload = static_cast<std::uint32_t>(line[start]);
				return result;
			}
			i = end;
			continue;
		}

		if (isDecimalDigitChar(line[i]) || (line[i] == '.' && i + 1 < line.size() && isDecimalDigitChar(line[i + 1]))) {
			const std::size_t start = i;
			const std::size_t end = consumeCppNumber(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
			i = end;
			continue;
		}

		if (isIdentifierStart(line[i])) {
			const std::size_t start = i++;
			while (i < line.size() && (isIdentifierChar(line[i]) || line[i] == '\'')) ++i;
			const std::string_view word = line.substr(start, i - start);
			if (wordInList(word, kRustKeywords, sizeof(kRustKeywords) / sizeof(kRustKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
				expectTypeName = isRustTypeIntroducer(word);
				expectCallableName = isRustCallableIntroducer(word);
				expectImportName = word == "use" || word == "crate" || word == "mod";
				continue;
			}
			if (wordInList(word, kRustConstants, sizeof(kRustConstants) / sizeof(kRustConstants[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			if (wordInList(word, kRustTypeKeywords, sizeof(kRustTypeKeywords) / sizeof(kRustTypeKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			if (expectImportName || expectTypeName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				expectImportName = false;
				expectTypeName = false;
				continue;
			}
			if (expectCallableName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				expectCallableName = false;
				continue;
			}
			if (i < line.size() && line[i] == '!') {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			if (hasScopeQualifierBefore(line, start) || hasScopeQualifierAfter(line, i) || isUpperCaseIdentifier(word)) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			continue;
		}

		{
			const std::size_t delimiterLength = cppDelimiterLength(line, i);
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

MRSyntaxLineResult MRYamlSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = previousState;

	const std::size_t commentStart = findYamlCommentStart(line);
	const std::string_view content = commentStart == std::string_view::npos ? line : line.substr(0, commentStart);
	const std::size_t keyDelimiter = findYamlKeyDelimiter(content);

	for (std::size_t i = 0; i < content.size();) {
		if (line[i] == '"' || line[i] == '\'') {
			const std::size_t start = i;
			const char quote = line[i++];

			while (i < content.size()) {
				if (quote == '\'' && line[i] == '\'' && i + 1 < content.size() && line[i + 1] == '\'') {
					i += 2;
					continue;
				}
				if (quote == '"' && line[i] == '\\' && i + 1 < content.size()) {
					i += 2;
					continue;
				}
				if (line[i] == quote) {
					++i;
					break;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, i, start < keyDelimiter ? MRSyntaxToken::Key : MRSyntaxToken::String);
			continue;
		}

		if (line[i] == '-' && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1])) != 0)) {
			const std::size_t next = i + 1;
			if (next < content.size() && std::isspace(static_cast<unsigned char>(line[next])) != 0) {
				appendRun(result.tokenRuns, i, next, MRSyntaxToken::Delimiter);
				i = next;
				continue;
			}
		}

		if (keyDelimiter != std::string_view::npos && i == keyDelimiter) {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		if ((line[i] == '|' || line[i] == '>') && i > keyDelimiter && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1])) != 0)) {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		if (line[i] == '-' || isDecimalDigitChar(line[i])) {
			const std::size_t start = i;
			const std::size_t end = consumeJsonNumber(content, i);
			if (end > start + (line[start] == '-' ? 1U : 0U)) {
				appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
				i = end;
				continue;
			}
		}

		if (isYamlIdentifierChar(line[i])) {
			const std::size_t start = i++;
			while (i < content.size() && isYamlIdentifierChar(line[i])) ++i;
			const std::string_view word = line.substr(start, i - start);
			const std::string upperWord = upperCopyView(word);

			if (keyDelimiter != std::string_view::npos && i <= keyDelimiter) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			if (upperWord == "TRUE" || upperWord == "FALSE" || upperWord == "NULL" || upperWord == "YES" || upperWord == "NO" || upperWord == "ON" || upperWord == "OFF" || upperWord == "~") {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
				continue;
			}
			continue;
		}

		if (line[i] == '[' || line[i] == ']' || line[i] == '{' || line[i] == '}' || line[i] == ',' || line[i] == '?' || line[i] == '&' || line[i] == '*' || line[i] == '!') {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		++i;
	}

	if (commentStart != std::string_view::npos) appendRun(result.tokenRuns, commentStart, line.size(), MRSyntaxToken::Comment);
	return result;
}

MRSyntaxLineResult MRXmlSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	std::size_t i = 0;

	if (previousState.mode == MRSyntaxMode::BlockComment) {
		std::size_t end = std::string_view::npos;
		MRSyntaxToken token = MRSyntaxToken::Comment;

		switch (previousState.payload) {
			case kSyntaxPayloadXmlComment:
				end = findXmlCommentEnd(line, 0);
				token = MRSyntaxToken::Comment;
				break;
			case kSyntaxPayloadXmlCdata:
				end = findXmlCdataEnd(line, 0);
				token = MRSyntaxToken::String;
				break;
			case kSyntaxPayloadXmlDirective:
				end = findXmlMarkupEnd(line, 0);
				token = MRSyntaxToken::Directive;
				break;
			case kSyntaxPayloadXmlProcessing:
				end = findXmlProcessingEnd(line, 0);
				token = MRSyntaxToken::Directive;
				break;
			default:
				break;
		}

		if (end == std::string_view::npos) {
			appendRun(result.tokenRuns, 0, line.size(), token);
			result.stateOut = previousState;
			return result;
		}
		appendRun(result.tokenRuns, 0, end, token);
		i = end;
	}

	if (previousState.mode == MRSyntaxMode::QuotedString) {
		const char quote = static_cast<char>(previousState.payload);
		const std::size_t start = i;
		while (i < line.size() && line[i] != quote)
			++i;
		if (i < line.size()) ++i;
		appendRun(result.tokenRuns, start, i, MRSyntaxToken::String);
		if (i >= line.size() && (line.empty() || line.back() != quote)) {
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.payload = previousState.payload;
			return result;
		}
	}

	while (i < line.size()) {
		if (i + 4 <= line.size() && line.substr(i, 4) == "<!--") {
			const std::size_t end = findXmlCommentEnd(line, i + 4);
			if (end == std::string_view::npos) {
				appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Comment);
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				result.stateOut.payload = kSyntaxPayloadXmlComment;
				return result;
			}
			appendRun(result.tokenRuns, i, end, MRSyntaxToken::Comment);
			i = end;
			continue;
		}
		if (i + 9 <= line.size() && line.substr(i, 9) == "<![CDATA[") {
			const std::size_t end = findXmlCdataEnd(line, i + 9);
			if (end == std::string_view::npos) {
				appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::String);
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				result.stateOut.payload = kSyntaxPayloadXmlCdata;
				return result;
			}
			appendRun(result.tokenRuns, i, end, MRSyntaxToken::String);
			i = end;
			continue;
		}
		if (i + 2 <= line.size() && line.substr(i, 2) == "<?") {
			const std::size_t end = findXmlProcessingEnd(line, i + 2);
			if (end == std::string_view::npos) {
				appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Directive);
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				result.stateOut.payload = kSyntaxPayloadXmlProcessing;
				return result;
			}
			appendRun(result.tokenRuns, i, end, MRSyntaxToken::Directive);
			i = end;
			continue;
		}
		if (i + 2 <= line.size() && line.substr(i, 2) == "<!" && (i + 4 > line.size() || line.substr(i, 4) != "<!--")) {
			const std::size_t end = findXmlMarkupEnd(line, i + 2);
			if (end == std::string_view::npos) {
				appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Directive);
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				result.stateOut.payload = kSyntaxPayloadXmlDirective;
				return result;
			}
			appendRun(result.tokenRuns, i, end, MRSyntaxToken::Directive);
			i = end;
			continue;
		}
		if (line[i] == '<') {
			const std::size_t end = findXmlMarkupEnd(line, i + 1);
			const std::size_t tokenEnd = end == std::string_view::npos ? line.size() : end;
			appendXmlTagRuns(result.tokenRuns, line, i, tokenEnd);
			if (end == std::string_view::npos) {
				char pendingQuote = '\0';
				for (std::size_t j = i + 1; j < line.size(); ++j) {
					if (pendingQuote != '\0') {
						if (line[j] == pendingQuote) pendingQuote = '\0';
						continue;
					}
					if (line[j] == '"' || line[j] == '\'') pendingQuote = line[j];
				}
				if (pendingQuote != '\0') {
					result.stateOut.mode = MRSyntaxMode::QuotedString;
					result.stateOut.payload = static_cast<std::uint32_t>(pendingQuote);
				}
				return result;
			}
			i = end;
			continue;
		}
		++i;
	}

	return result;
}

MRSyntaxLineResult MRPascalSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	std::size_t i = 0;

	if (previousState.mode == MRSyntaxMode::BlockComment) {
		const std::size_t start = 0;
		const MRSyntaxToken continuedToken = (previousState.flags & kSyntaxFlagPascalDirective) != 0 ? MRSyntaxToken::Directive : MRSyntaxToken::Comment;
		if (previousState.payload == static_cast<std::uint32_t>('{')) {
			while (i < line.size() && line[i] != '}')
				++i;
			if (i < line.size()) ++i;
		} else {
			while (i + 1 < line.size() && !(line[i] == '*' && line[i + 1] == ')'))
				++i;
			if (i + 1 < line.size()) i += 2;
			else
				i = line.size();
		}
		appendRun(result.tokenRuns, start, i, continuedToken);
		if (i >= line.size() && (previousState.payload == static_cast<std::uint32_t>('{') ? (line.empty() || line.back() != '}') : (line.size() < 2 || line.substr(line.size() - 2) != "*)"))) {
			result.stateOut.mode = MRSyntaxMode::BlockComment;
			result.stateOut.payload = previousState.payload;
			result.stateOut.flags = previousState.flags;
			return result;
		}
	}

	while (i < line.size()) {
		if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
			appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Comment);
			break;
		}

		if (line[i] == '{') {
			const std::size_t start = i++;
			const bool isDirective = i < line.size() && line[i] == '$';
			while (i < line.size() && line[i] != '}')
				++i;
			if (i < line.size()) ++i;
			appendRun(result.tokenRuns, start, i, isDirective ? MRSyntaxToken::Directive : MRSyntaxToken::Comment);
			if (i >= line.size() && (line.empty() || line.back() != '}')) {
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				result.stateOut.payload = static_cast<std::uint32_t>('{');
				result.stateOut.flags = isDirective ? kSyntaxFlagPascalDirective : 0;
				return result;
			}
			continue;
		}

		if (i + 1 < line.size() && line[i] == '(' && line[i + 1] == '*') {
			const std::size_t start = i;
			const bool isDirective = i + 2 < line.size() && line[i + 2] == '$';
			i += 2;
			while (i + 1 < line.size() && !(line[i] == '*' && line[i + 1] == ')'))
				++i;
			if (i + 1 < line.size()) i += 2;
			else
				i = line.size();
			appendRun(result.tokenRuns, start, i, isDirective ? MRSyntaxToken::Directive : MRSyntaxToken::Comment);
			if (i >= line.size() && (line.size() < 2 || line.substr(line.size() - 2) != "*)")) {
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				result.stateOut.payload = static_cast<std::uint32_t>('*');
				result.stateOut.flags = isDirective ? kSyntaxFlagPascalDirective : 0;
				return result;
			}
			continue;
		}

		if (line[i] == '\'') {
			const std::size_t start = i++;
			while (i < line.size()) {
				if (line[i] == '\'' && i + 1 < line.size() && line[i + 1] == '\'') {
					i += 2;
					continue;
				}
				if (line[i] == '\'') {
					++i;
					break;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::String);
			continue;
		}

		if (line[i] == '$' && i + 1 < line.size() && isHexDigitChar(line[i + 1])) {
			const std::size_t start = i++;
			while (i < line.size() && isHexDigitChar(line[i])) ++i;
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Number);
			continue;
		}

		if (isDecimalDigitChar(line[i]) || (line[i] == '.' && i + 1 < line.size() && isDecimalDigitChar(line[i + 1]))) {
			const std::size_t start = i;
			const std::size_t end = consumeCppNumber(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
			i = end;
			continue;
		}

		if (isIdentifierStart(line[i])) {
			const std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			const std::string_view word = line.substr(start, i - start);
			if (mrmacWordInList(word, kPascalKeywords, sizeof(kPascalKeywords) / sizeof(kPascalKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
				continue;
			}
			if (mrmacWordInList(word, kPascalTypeKeywords, sizeof(kPascalTypeKeywords) / sizeof(kPascalTypeKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			if (mrmacWordInList(word, kPascalConstants, sizeof(kPascalConstants) / sizeof(kPascalConstants[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			continue;
		}

		{
			const std::size_t delimiterLength = pascalDelimiterLength(line, i);
			if (delimiterLength > 0) {
				appendRun(result.tokenRuns, i, i + delimiterLength, MRSyntaxToken::Delimiter);
				i += delimiterLength;
				continue;
			}
		}

		if (isPascalDelimiterChar(line[i])) {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		++i;
	}

	return result;
}

MRSyntaxLineResult MRGoSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	std::size_t i = 0;
	bool inBlockComment = previousState.mode == MRSyntaxMode::BlockComment;
	bool expectTypeName = false;
	bool expectCallableName = false;
	bool expectImportName = false;

	if (inBlockComment) {
		const std::size_t start = i;
		while (i < line.size()) {
			if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
				i += 2;
				inBlockComment = false;
				break;
			}
			++i;
		}
		appendRun(result.tokenRuns, start, i, MRSyntaxToken::Comment);
		if (inBlockComment) {
			result.stateOut.mode = MRSyntaxMode::BlockComment;
			return result;
		}
	}

	if (previousState.mode == MRSyntaxMode::RawString) {
		const std::size_t end = line.find('`', 0);
		if (end == std::string_view::npos) {
			appendRun(result.tokenRuns, 0, line.size(), MRSyntaxToken::String);
			result.stateOut.mode = MRSyntaxMode::RawString;
			return result;
		}
		appendRun(result.tokenRuns, 0, end + 1, MRSyntaxToken::String);
		i = end + 1;
	} else if (previousState.mode == MRSyntaxMode::QuotedString) {
		const char quote = static_cast<char>(previousState.payload);
		const std::size_t end = findStringContinuationEnd(line, 0, quote);
		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		if (end == line.size()) {
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.payload = previousState.payload;
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
			const std::size_t start = i;
			i += 2;
			while (i < line.size()) {
				if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
					i += 2;
					break;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, std::min(i, line.size()), MRSyntaxToken::Comment);
			if (i >= line.size() && (line.size() < 2 || line.substr(line.size() - 2) != "*/")) {
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				return result;
			}
			continue;
		}

		if (line[i] == '`') {
			const std::size_t start = i;
			const std::size_t end = line.find('`', i + 1);
			if (end == std::string_view::npos) {
				appendRun(result.tokenRuns, start, line.size(), MRSyntaxToken::String);
				result.stateOut.mode = MRSyntaxMode::RawString;
				return result;
			}
			appendRun(result.tokenRuns, start, end + 1, MRSyntaxToken::String);
			i = end + 1;
			continue;
		}

		if (line[i] == '"' || line[i] == '\'') {
			const std::size_t start = i;
			const std::size_t end = consumeCppStringLiteral(line, i, line[i]);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			if (end == line.size() && (line.empty() || line.back() != line[start])) {
				result.stateOut.mode = MRSyntaxMode::QuotedString;
				result.stateOut.payload = static_cast<std::uint32_t>(line[start]);
				return result;
			}
			i = end;
			continue;
		}

		if (isDecimalDigitChar(line[i]) || (line[i] == '.' && i + 1 < line.size() && isDecimalDigitChar(line[i + 1]))) {
			const std::size_t start = i;
			const std::size_t end = consumeCppNumber(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
			i = end;
			continue;
		}

		if (isIdentifierStart(line[i])) {
			const std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			const std::string_view word = line.substr(start, i - start);
			if (wordInList(word, kGoKeywords, sizeof(kGoKeywords) / sizeof(kGoKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
				expectTypeName = isGoTypeIntroducer(word);
				expectCallableName = isGoCallableIntroducer(word);
				expectImportName = word == "import" || word == "package";
				continue;
			}
			if (wordInList(word, kGoConstants, sizeof(kGoConstants) / sizeof(kGoConstants[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			if (wordInList(word, kGoTypeKeywords, sizeof(kGoTypeKeywords) / sizeof(kGoTypeKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			if (expectImportName || expectTypeName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				expectImportName = false;
				expectTypeName = false;
				continue;
			}
			if (expectCallableName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				expectCallableName = false;
				continue;
			}
			if (isUpperCaseIdentifier(word)) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			continue;
		}

		{
			const std::size_t delimiterLength = cppDelimiterLength(line, i);
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

MRSyntaxLineResult MRKotlinSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	std::size_t i = 0;
	std::uint32_t commentDepth = previousState.mode == MRSyntaxMode::BlockComment ? (previousState.payload == 0 ? 1U : previousState.payload) : 0;
	bool expectTypeName = false;
	bool expectCallableName = false;
	bool expectImportName = false;

	if (commentDepth > 0) {
		const std::size_t start = i;
		while (i < line.size()) {
			if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
				++commentDepth;
				i += 2;
				continue;
			}
			if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
				--commentDepth;
				i += 2;
				if (commentDepth == 0) break;
				continue;
			}
			++i;
		}
		appendRun(result.tokenRuns, start, i, MRSyntaxToken::Comment);
		if (commentDepth > 0) {
			result.stateOut.mode = MRSyntaxMode::BlockComment;
			result.stateOut.payload = commentDepth;
			return result;
		}
	}

	if (previousState.mode == MRSyntaxMode::QuotedString && (previousState.flags & kSyntaxFlagTripleQuoted) != 0) {
		const std::size_t end = findTripleQuotedStringEnd(line, 0, '"');
		if (end == std::string_view::npos) {
			appendRun(result.tokenRuns, 0, line.size(), MRSyntaxToken::String);
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.flags = kSyntaxFlagTripleQuoted;
			result.stateOut.payload = static_cast<std::uint32_t>('"');
			return result;
		}
		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		i = end;
	}

	while (i < line.size()) {
		if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
			appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Comment);
			break;
		}

		if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
			const std::size_t start = i;
			commentDepth = 1;
			i += 2;
			while (i < line.size()) {
				if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
					++commentDepth;
					i += 2;
					continue;
				}
				if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
					--commentDepth;
					i += 2;
					if (commentDepth == 0) break;
					continue;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, std::min(i, line.size()), MRSyntaxToken::Comment);
			if (commentDepth > 0) {
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				result.stateOut.payload = commentDepth;
				return result;
			}
			continue;
		}

		if (line[i] == '@') {
			const std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			appendRun(result.tokenRuns, start, i, MRSyntaxToken::Directive);
			continue;
		}

		if (i + 2 < line.size() && line[i] == '"' && line[i + 1] == '"' && line[i + 2] == '"') {
			const std::size_t start = i;
			const std::size_t end = findTripleQuotedStringEnd(line, i + 3, '"');
			if (end == std::string_view::npos) {
				appendRun(result.tokenRuns, start, line.size(), MRSyntaxToken::String);
				result.stateOut.mode = MRSyntaxMode::QuotedString;
				result.stateOut.flags = kSyntaxFlagTripleQuoted;
				result.stateOut.payload = static_cast<std::uint32_t>('"');
				return result;
			}
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			i = end;
			continue;
		}

		if (line[i] == '"' || line[i] == '\'') {
			const std::size_t start = i;
			const std::size_t end = consumeCppStringLiteral(line, i, line[i]);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			i = end;
			continue;
		}

		if (isDecimalDigitChar(line[i]) || (line[i] == '.' && i + 1 < line.size() && isDecimalDigitChar(line[i + 1]))) {
			const std::size_t start = i;
			const std::size_t end = consumeCppNumber(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
			i = end;
			continue;
		}

		if (isIdentifierStart(line[i])) {
			const std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			const std::string_view word = line.substr(start, i - start);
			if (wordInList(word, kKotlinKeywords, sizeof(kKotlinKeywords) / sizeof(kKotlinKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
				expectTypeName = isKotlinTypeIntroducer(word);
				expectCallableName = isKotlinCallableIntroducer(word);
				expectImportName = word == "import" || word == "package";
				continue;
			}
			if (wordInList(word, kKotlinConstants, sizeof(kKotlinConstants) / sizeof(kKotlinConstants[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			if (wordInList(word, kKotlinTypeKeywords, sizeof(kKotlinTypeKeywords) / sizeof(kKotlinTypeKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			if (expectImportName || expectTypeName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				expectImportName = false;
				expectTypeName = false;
				continue;
			}
			if (expectCallableName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				expectCallableName = false;
				continue;
			}
			if (isUpperCaseIdentifier(word)) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			continue;
		}

		{
			const std::size_t delimiterLength = cppDelimiterLength(line, i);
			if (delimiterLength > 0) {
				appendRun(result.tokenRuns, i, i + delimiterLength, MRSyntaxToken::Delimiter);
				i += delimiterLength;
				continue;
			}
		}

		if (isCppDelimiterChar(line[i]) || line[i] == '$') {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		++i;
	}

	return result;
}

MRSyntaxLineResult MRCSharpSyntaxHighlighter::highlightLine(std::string_view line, MRSyntaxLineState previousState) {
	MRSyntaxLineResult result;
	result.stateOut = MRSyntaxLineState();
	std::size_t i = 0;
	bool inBlockComment = previousState.mode == MRSyntaxMode::BlockComment;
	bool expectTypeName = false;

	if (inBlockComment) {
		const std::size_t start = i;
		while (i < line.size()) {
			if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
				i += 2;
				inBlockComment = false;
				break;
			}
			++i;
		}
		appendRun(result.tokenRuns, start, i, MRSyntaxToken::Comment);
		if (inBlockComment) {
			result.stateOut.mode = MRSyntaxMode::BlockComment;
			return result;
		}
	}

	if (previousState.mode == MRSyntaxMode::RawString) {
		std::size_t end = 0;
		while (end < line.size()) {
			if (line[end] == '"') {
				if (end + 1 < line.size() && line[end + 1] == '"') {
					end += 2;
					continue;
				}
				++end;
				break;
			}
			++end;
		}
		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		if (end >= line.size() && (line.empty() || line.back() != '"')) {
			result.stateOut.mode = MRSyntaxMode::RawString;
			return result;
		}
		i = end;
	} else if (previousState.mode == MRSyntaxMode::QuotedString && (previousState.flags & kSyntaxFlagTripleQuoted) != 0) {
		const std::size_t end = findTripleQuotedStringEnd(line, 0, '"');
		if (end == std::string_view::npos) {
			appendRun(result.tokenRuns, 0, line.size(), MRSyntaxToken::String);
			result.stateOut.mode = MRSyntaxMode::QuotedString;
			result.stateOut.flags = kSyntaxFlagTripleQuoted;
			result.stateOut.payload = static_cast<std::uint32_t>('"');
			return result;
		}
		appendRun(result.tokenRuns, 0, end, MRSyntaxToken::String);
		i = end;
	}

	while (i < line.size()) {
		if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
			appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Comment);
			break;
		}

		if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
			const std::size_t start = i;
			i += 2;
			while (i < line.size()) {
				if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
					i += 2;
					break;
				}
				++i;
			}
			appendRun(result.tokenRuns, start, std::min(i, line.size()), MRSyntaxToken::Comment);
			if (i >= line.size() && (line.size() < 2 || line.substr(line.size() - 2) != "*/")) {
				result.stateOut.mode = MRSyntaxMode::BlockComment;
				return result;
			}
			continue;
		}

		if (line[i] == '#' && trimWhitespaceView(line.substr(0, i)).empty()) {
			appendRun(result.tokenRuns, i, line.size(), MRSyntaxToken::Directive);
			break;
		}

		if (i + 2 < line.size() && line[i] == '"' && line[i + 1] == '"' && line[i + 2] == '"') {
			const std::size_t start = i;
			const std::size_t end = findTripleQuotedStringEnd(line, i + 3, '"');
			if (end == std::string_view::npos) {
				appendRun(result.tokenRuns, start, line.size(), MRSyntaxToken::String);
				result.stateOut.mode = MRSyntaxMode::QuotedString;
				result.stateOut.flags = kSyntaxFlagTripleQuoted;
				result.stateOut.payload = static_cast<std::uint32_t>('"');
				return result;
			}
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			i = end;
			continue;
		}

		if (i + 1 < line.size() && line[i] == '@' && line[i + 1] == '"') {
			const std::size_t start = i;
			const std::size_t end = consumeCSharpVerbatimStringLiteral(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			if (end == line.size() && (line.empty() || line.back() != '"')) {
				result.stateOut.mode = MRSyntaxMode::RawString;
				return result;
			}
			i = end;
			continue;
		}

		if (i + 2 < line.size() && line[i] == '$' && line[i + 1] == '@' && line[i + 2] == '"') {
			const std::size_t start = i;
			const std::size_t end = consumeCSharpVerbatimStringLiteral(line, i + 1);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			if (end == line.size() && (line.empty() || line.back() != '"')) {
				result.stateOut.mode = MRSyntaxMode::RawString;
				return result;
			}
			i = end;
			continue;
		}

		if (i + 2 < line.size() && line[i] == '@' && line[i + 1] == '$' && line[i + 2] == '"') {
			const std::size_t start = i;
			const std::size_t end = consumeCSharpVerbatimStringLiteral(line, i + 1);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			if (end == line.size() && (line.empty() || line.back() != '"')) {
				result.stateOut.mode = MRSyntaxMode::RawString;
				return result;
			}
			i = end;
			continue;
		}

		if (i + 1 < line.size() && line[i] == '$' && line[i + 1] == '"') {
			const std::size_t start = i;
			const std::size_t end = consumeCppStringLiteral(line, i + 1, '"');
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			i = end;
			continue;
		}

		if (line[i] == '"' || line[i] == '\'') {
			const std::size_t start = i;
			const std::size_t end = consumeCppStringLiteral(line, i, line[i]);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::String);
			i = end;
			continue;
		}

		if (isDecimalDigitChar(line[i]) || (line[i] == '.' && i + 1 < line.size() && isDecimalDigitChar(line[i + 1]))) {
			const std::size_t start = i;
			const std::size_t end = consumeCppNumber(line, i);
			appendRun(result.tokenRuns, start, end, MRSyntaxToken::Number);
			i = end;
			continue;
		}

		if (isIdentifierStart(line[i])) {
			const std::size_t start = i++;
			while (i < line.size() && isIdentifierChar(line[i])) ++i;
			const std::string_view word = line.substr(start, i - start);
			if (wordInList(word, kCSharpKeywords, sizeof(kCSharpKeywords) / sizeof(kCSharpKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Keyword);
				expectTypeName = isCSharpTypeIntroducer(word);
				continue;
			}
			if (wordInList(word, kCSharpConstants, sizeof(kCSharpConstants) / sizeof(kCSharpConstants[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			if (wordInList(word, kCSharpTypeKeywords, sizeof(kCSharpTypeKeywords) / sizeof(kCSharpTypeKeywords[0]))) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				continue;
			}
			if (expectTypeName) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Type);
				expectTypeName = false;
				continue;
			}
			if (isUpperCaseIdentifier(word) || isFunctionLikeIdentifier(line, start, i)) {
				appendRun(result.tokenRuns, start, i, MRSyntaxToken::Key);
				continue;
			}
			continue;
		}

		{
			const std::size_t delimiterLength = cppDelimiterLength(line, i);
			if (delimiterLength > 0) {
				appendRun(result.tokenRuns, i, i + delimiterLength, MRSyntaxToken::Delimiter);
				i += delimiterLength;
				continue;
			}
		}

		if (isCppDelimiterChar(line[i]) || line[i] == '$' || line[i] == '@') {
			appendRun(result.tokenRuns, i, i + 1, MRSyntaxToken::Delimiter);
			++i;
			continue;
		}

		++i;
	}

	return result;
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
		case MRSyntaxLanguage::Yaml: {
			MRYamlSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Xml: {
			MRXmlSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Bash: {
			MRBashSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Zsh: {
			MRZshSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Fish: {
			MRFishSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Perl: {
			MRPerlSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Swift: {
			MRSwiftSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Rust: {
			MRRustSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Go: {
			MRGoSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Kotlin: {
			MRKotlinSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::CSharp: {
			MRCSharpSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Pascal: {
			MRPascalSyntaxHighlighter highlighter;
			return highlighter.highlightLine(line, previousState);
		}
		case MRSyntaxLanguage::Systemd: {
			MRSystemdSyntaxHighlighter highlighter;
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
