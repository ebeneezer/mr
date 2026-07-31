#include "../../config/settings/MRSettingsRuntime.hpp"
#include "../../ui/MRFileEditor/MRFileEditor.hpp"
#include "../../ui/MRSyntax.hpp"
#include "../../ui/MRSyntaxBasic.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

struct EditSettingsGuard {
	MREditSetupSettings previous;
	bool restore = false;

	EditSettingsGuard() : previous(configuredEditSetupSettings()) {
	}

	~EditSettingsGuard() {
		if (restore) static_cast<void>(setConfiguredEditSetupSettings(previous, nullptr));
	}
};

struct UiIndentStyleGuard {
	MRUiIndentStyle previous;
	bool restore = false;

	UiIndentStyleGuard() : previous(configuredUiIndentStyle()) {
	}

	~UiIndentStyleGuard() {
		if (restore) static_cast<void>(setConfiguredUiIndentStyle(previous, nullptr));
	}
};

struct DedentSimulation {
	bool changed = false;
	int beforeColumn = 0;
	int afterColumn = 0;
};

struct EnterSimulation {
	bool changed = false;
	std::string previousLine;
	std::string currentLine;
};

struct StrictExpectation {
	bool applicable = false;
	bool passed = false;
	int expectedColumn = 0;
	std::string rule;
};

struct LanguageRun {
	MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;
	bool automatic = false;
};

struct TrainerOptions {
	std::vector<LanguageRun> languages;
	std::vector<std::string> indentStyles;
	std::vector<MRUiIndentStyle> uiIndentStyles;
	std::vector<bool> tabExpandValues;
	int tabSize = 4;
	std::string inputPath;
	std::string outputPath;
};

bool parseLanguageName(const std::string &name, MRSyntaxLanguage &language) noexcept {
	if (name == "plain" || name == "plaintext")
		language = MRSyntaxLanguage::PlainText;
	else if (name == "c")
		language = MRSyntaxLanguage::C;
	else if (name == "cpp" || name == "c++")
		language = MRSyntaxLanguage::Cpp;
	else if (name == "javascript" || name == "js" || name == "typescript" || name == "ts" || name == "tsx")
		language = MRSyntaxLanguage::JavaScript;
	else if (name == "json")
		language = MRSyntaxLanguage::Json;
	else if (name == "yaml" || name == "yml")
		language = MRSyntaxLanguage::Yaml;
	else if (name == "xml" || name == "xsd" || name == "xsl" || name == "xslt" || name == "svg")
		language = MRSyntaxLanguage::Xml;
	else if (name == "python" || name == "py")
		language = MRSyntaxLanguage::Python;
	else if (name == "markdown" || name == "md")
		language = MRSyntaxLanguage::Markdown;
	else if (name == "latex" || name == "tex" || name == "ltx")
		language = MRSyntaxLanguage::Latex;
	else if (name == "basic" || name == "bas" || name == "fb" || name == "qb" || name == "gambas")
		language = MRSyntaxLanguage::Basic;
	else if (name == "bash" || name == "sh")
		language = MRSyntaxLanguage::Bash;
	else if (name == "zsh")
		language = MRSyntaxLanguage::Zsh;
	else if (name == "fish")
		language = MRSyntaxLanguage::Fish;
	else if (name == "perl" || name == "pl")
		language = MRSyntaxLanguage::Perl;
	else if (name == "swift" || name == "sw")
		language = MRSyntaxLanguage::Swift;
	else if (name == "rust" || name == "rs")
		language = MRSyntaxLanguage::Rust;
	else if (name == "go")
		language = MRSyntaxLanguage::Go;
	else if (name == "kotlin" || name == "kt" || name == "kts")
		language = MRSyntaxLanguage::Kotlin;
	else if (name == "csharp" || name == "cs" || name == "c#")
		language = MRSyntaxLanguage::CSharp;
	else if (name == "pascal" || name == "pas")
		language = MRSyntaxLanguage::Pascal;
	else if (name == "systemd" || name == "sd")
		language = MRSyntaxLanguage::Systemd;
	else if (name == "make" || name == "mk")
		language = MRSyntaxLanguage::Make;
	else if (name == "mrmac" || name == "mm")
		language = MRSyntaxLanguage::MRMAC;
	else
		return false;
	return true;
}

const char *languageName(MRSyntaxLanguage language) noexcept {
	switch (language) {
		case MRSyntaxLanguage::PlainText:
			return "PlainText";
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
		case MRSyntaxLanguage::Yaml:
			return "YAML";
		case MRSyntaxLanguage::Xml:
			return "XML";
		case MRSyntaxLanguage::Bash:
			return "Bash";
		case MRSyntaxLanguage::Zsh:
			return "zsh";
		case MRSyntaxLanguage::Fish:
			return "fish";
		case MRSyntaxLanguage::Perl:
			return "Perl";
		case MRSyntaxLanguage::Swift:
			return "Swift";
		case MRSyntaxLanguage::Rust:
			return "Rust";
		case MRSyntaxLanguage::Go:
			return "Go";
		case MRSyntaxLanguage::Kotlin:
			return "Kotlin";
		case MRSyntaxLanguage::CSharp:
			return "C#";
		case MRSyntaxLanguage::Pascal:
			return "Pascal";
		case MRSyntaxLanguage::Systemd:
			return "systemd";
		case MRSyntaxLanguage::MRMAC:
			return "MRMAC";
		case MRSyntaxLanguage::Make:
			return "Make";
		case MRSyntaxLanguage::Markdown:
			return "Markdown";
		case MRSyntaxLanguage::Latex:
			return "LaTeX";
		case MRSyntaxLanguage::Basic:
			return "BASIC";
	}
	return "PlainText";
}

std::string languageSettingName(MRSyntaxLanguage language, bool automatic) {
	if (automatic) return "AUTO";
	switch (language) {
		case MRSyntaxLanguage::C:
			return "C";
		case MRSyntaxLanguage::Cpp:
			return "CPP";
		case MRSyntaxLanguage::JavaScript:
			return "JAVASCRIPT";
		case MRSyntaxLanguage::Python:
			return "PYTHON";
		case MRSyntaxLanguage::Json:
			return "JSON";
		case MRSyntaxLanguage::Yaml:
			return "YAML";
		case MRSyntaxLanguage::Xml:
			return "XML";
		case MRSyntaxLanguage::Bash:
			return "BASH";
		case MRSyntaxLanguage::Zsh:
			return "ZSH";
		case MRSyntaxLanguage::Fish:
			return "FISH";
		case MRSyntaxLanguage::Perl:
			return "PERL";
		case MRSyntaxLanguage::Swift:
			return "SWIFT";
		case MRSyntaxLanguage::Rust:
			return "RUST";
		case MRSyntaxLanguage::Go:
			return "GO";
		case MRSyntaxLanguage::Kotlin:
			return "KOTLIN";
		case MRSyntaxLanguage::CSharp:
			return "CSHARP";
		case MRSyntaxLanguage::Pascal:
			return "PASCAL";
		case MRSyntaxLanguage::Systemd:
			return "SYSTEMD";
		case MRSyntaxLanguage::Make:
			return "MAKE";
		case MRSyntaxLanguage::MRMAC:
			return "MRMAC";
		case MRSyntaxLanguage::Markdown:
			return "MARKDOWN";
		case MRSyntaxLanguage::Latex:
			return "LATEX";
		case MRSyntaxLanguage::Basic:
			return "BASIC";
		case MRSyntaxLanguage::PlainText:
		default:
			return "NONE";
	}
}

std::string toLowerAscii(std::string text) {
	for (char &ch : text)
		if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
	return text;
}

std::vector<std::string> splitCommaSeparated(const std::string &text) {
	std::vector<std::string> parts;
	std::size_t start = 0;
	while (start <= text.size()) {
		std::size_t comma = text.find(',', start);
		if (comma == std::string::npos) comma = text.size();
		if (comma > start) parts.push_back(text.substr(start, comma - start));
		start = comma + 1;
	}
	return parts;
}

std::vector<LanguageRun> allLanguageRuns() {
	return {
		{MRSyntaxLanguage::PlainText, true},
		{MRSyntaxLanguage::PlainText, false},
		{MRSyntaxLanguage::C, false},
		{MRSyntaxLanguage::Cpp, false},
		{MRSyntaxLanguage::JavaScript, false},
		{MRSyntaxLanguage::Json, false},
		{MRSyntaxLanguage::Yaml, false},
		{MRSyntaxLanguage::Xml, false},
		{MRSyntaxLanguage::Python, false},
		{MRSyntaxLanguage::Markdown, false},
		{MRSyntaxLanguage::Latex, false},
		{MRSyntaxLanguage::Basic, false},
		{MRSyntaxLanguage::Bash, false},
		{MRSyntaxLanguage::Zsh, false},
		{MRSyntaxLanguage::Fish, false},
		{MRSyntaxLanguage::Perl, false},
		{MRSyntaxLanguage::Swift, false},
		{MRSyntaxLanguage::Rust, false},
		{MRSyntaxLanguage::Go, false},
		{MRSyntaxLanguage::Kotlin, false},
		{MRSyntaxLanguage::CSharp, false},
		{MRSyntaxLanguage::Pascal, false},
		{MRSyntaxLanguage::Systemd, false},
		{MRSyntaxLanguage::Make, false},
		{MRSyntaxLanguage::MRMAC, false},
	};
}

void appendLanguageRun(std::vector<LanguageRun> &languages, LanguageRun candidate) {
	for (const LanguageRun &existing : languages)
		if (existing.language == candidate.language && existing.automatic == candidate.automatic) return;
	languages.push_back(candidate);
}

bool parseLanguageOption(const std::string &value, std::vector<LanguageRun> &languages) {
	const std::vector<std::string> tokens = splitCommaSeparated(toLowerAscii(value));
	for (const std::string &token : tokens) {
		if (token == "all") {
			for (const LanguageRun &candidate : allLanguageRuns())
				appendLanguageRun(languages, candidate);
			continue;
		}
		if (token == "auto") {
			appendLanguageRun(languages, {MRSyntaxLanguage::PlainText, true});
			continue;
		}
		MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;
		if (!parseLanguageName(token, language)) return false;
		appendLanguageRun(languages, {language, false});
	}
	return !languages.empty();
}

bool parseIndentStyleOption(const std::string &value, std::vector<std::string> &indentStyles) {
	const std::vector<std::string> tokens = splitCommaSeparated(toLowerAscii(value));
	for (const std::string &token : tokens) {
		if (token == "all") {
			indentStyles = {"OFF", "AUTOMATIC", "SMART"};
			return true;
		}
		if (token == "off")
			indentStyles.push_back("OFF");
		else if (token == "automatic" || token == "auto")
			indentStyles.push_back("AUTOMATIC");
		else if (token == "smart")
			indentStyles.push_back("SMART");
		else
			return false;
	}
	return !indentStyles.empty();
}

const char *uiIndentStyleName(MRUiIndentStyle style) noexcept {
	switch (style) {
		case MRUiIndentStyle::KandR4:
			return "K_AND_R4";
		case MRUiIndentStyle::Allman:
			return "ALLMAN";
		case MRUiIndentStyle::Gnome:
			return "GNOME";
		case MRUiIndentStyle::Whitesmiths:
			return "WHITESMITHS";
		case MRUiIndentStyle::Horstmann:
			return "HORSTMANN";
		case MRUiIndentStyle::KandR:
		default:
			return "K_AND_R";
	}
}

void appendUiIndentStyle(std::vector<MRUiIndentStyle> &styles, MRUiIndentStyle style) {
	for (MRUiIndentStyle existing : styles)
		if (existing == style) return;
	styles.push_back(style);
}

bool parseUiIndentStyleName(const std::string &token, MRUiIndentStyle &style) noexcept {
	if (token == "kandr" || token == "k_and_r" || token == "k&r")
		style = MRUiIndentStyle::KandR;
	else if (token == "kandr4" || token == "k_and_r4" || token == "k&r4")
		style = MRUiIndentStyle::KandR4;
	else if (token == "allman")
		style = MRUiIndentStyle::Allman;
	else if (token == "gnome")
		style = MRUiIndentStyle::Gnome;
	else if (token == "whitesmiths")
		style = MRUiIndentStyle::Whitesmiths;
	else if (token == "horstmann")
		style = MRUiIndentStyle::Horstmann;
	else
		return false;
	return true;
}

bool parseUiIndentStyleOption(const std::string &value, std::vector<MRUiIndentStyle> &styles) {
	const std::vector<std::string> tokens = splitCommaSeparated(toLowerAscii(value));
	for (const std::string &token : tokens) {
		if (token == "all") {
			styles = {MRUiIndentStyle::KandR, MRUiIndentStyle::KandR4, MRUiIndentStyle::Allman, MRUiIndentStyle::Gnome, MRUiIndentStyle::Whitesmiths, MRUiIndentStyle::Horstmann};
			return true;
		}
		MRUiIndentStyle style = MRUiIndentStyle::KandR;
		if (!parseUiIndentStyleName(token, style)) return false;
		appendUiIndentStyle(styles, style);
	}
	return !styles.empty();
}

bool parseBoolMatrixOption(const std::string &value, std::vector<bool> &values) {
	const std::vector<std::string> tokens = splitCommaSeparated(toLowerAscii(value));
	for (const std::string &token : tokens) {
		if (token == "all") {
			values = {false, true};
			return true;
		}
		if (token == "on" || token == "true" || token == "1")
			values.push_back(true);
		else if (token == "off" || token == "false" || token == "0")
			values.push_back(false);
		else
			return false;
	}
	return !values.empty();
}

std::string requestedLanguageName(const LanguageRun &language) {
	if (language.automatic) return "AUTO";
	return languageName(language.language);
}

std::string boolName(bool value) {
	return value ? "ON" : "OFF";
}

std::string escapeForReport(std::string_view text) {
	std::string out;
	for (char ch : text) {
		switch (ch) {
			case '\t':
				out += "\\t";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\n':
				out += "\\n";
				break;
			default:
				out.push_back(ch);
				break;
		}
	}
	return out;
}

std::string_view ltrimView(std::string_view text) {
	std::size_t index = 0;
	while (index < text.size() && (text[index] == ' ' || text[index] == '\t')) ++index;
	return text.substr(index);
}

std::string_view trimView(std::string_view text) {
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) text.remove_suffix(1);
	return text;
}

bool startsWithUpperKeyword(std::string_view upperText, std::string_view keyword) {
	return upperText.starts_with(keyword) && (upperText.size() == keyword.size() || upperText[keyword.size()] == ' ' || upperText[keyword.size()] == ':' || upperText[keyword.size()] == '{');
}

bool isCLikeCommentOrDirectiveLine(std::string_view trimmed) noexcept {
	return trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*") || trimmed.starts_with("*/") || trimmed.starts_with("#");
}

int braceIndentStepColumns(MRUiIndentStyle style) noexcept {
	switch (style) {
		case MRUiIndentStyle::KandR:
			return 2;
		case MRUiIndentStyle::KandR4:
			return 4;
		case MRUiIndentStyle::Allman:
			return 4;
		case MRUiIndentStyle::Gnome:
			return 2;
		case MRUiIndentStyle::Whitesmiths:
			return 4;
		case MRUiIndentStyle::Horstmann:
		default:
			return 4;
	}
}

bool braceLeadIndentsBodyOnNextLine(MRUiIndentStyle style) noexcept {
	return style == MRUiIndentStyle::KandR || style == MRUiIndentStyle::KandR4 || style == MRUiIndentStyle::Gnome || style == MRUiIndentStyle::Whitesmiths;
}

bool braceBodyAlignsWithStandaloneBrace(MRUiIndentStyle style) noexcept {
	return style == MRUiIndentStyle::Whitesmiths;
}

bool looksLikeFunctionHeaderLead(std::string_view trimmed) {
	const std::size_t openParen = trimmed.find('(');
	const std::size_t closeParen = trimmed.rfind(')');
	if (openParen == std::string_view::npos || closeParen == std::string_view::npos || closeParen < openParen) return false;
	if (trimmed.find(';') != std::string_view::npos) return false;
	if (trimmed.find('=') != std::string_view::npos && trimmed.find("==") == std::string_view::npos) return false;
	return true;
}

bool isBraceLanguage(MRSyntaxLanguage language) noexcept {
	return language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::JavaScript || language == MRSyntaxLanguage::Swift || language == MRSyntaxLanguage::Rust ||
	       language == MRSyntaxLanguage::Go || language == MRSyntaxLanguage::Kotlin || language == MRSyntaxLanguage::CSharp;
}

bool isRustStrictLead(std::string_view upperLine) {
	return startsWithUpperKeyword(upperLine, "IF") || startsWithUpperKeyword(upperLine, "ELSE") || startsWithUpperKeyword(upperLine, "FOR") || startsWithUpperKeyword(upperLine, "WHILE") ||
	       startsWithUpperKeyword(upperLine, "LOOP") || startsWithUpperKeyword(upperLine, "MATCH") || startsWithUpperKeyword(upperLine, "UNSAFE") || startsWithUpperKeyword(upperLine, "FN") ||
	       startsWithUpperKeyword(upperLine, "IMPL") || startsWithUpperKeyword(upperLine, "TRAIT") || startsWithUpperKeyword(upperLine, "STRUCT") || startsWithUpperKeyword(upperLine, "ENUM") ||
	       startsWithUpperKeyword(upperLine, "UNION") || startsWithUpperKeyword(upperLine, "MOD");
}

bool isGoStrictLead(std::string_view upperLine) {
	if (startsWithUpperKeyword(upperLine, "IF") || startsWithUpperKeyword(upperLine, "ELSE") || startsWithUpperKeyword(upperLine, "FOR") || startsWithUpperKeyword(upperLine, "SWITCH") ||
	    startsWithUpperKeyword(upperLine, "SELECT") || startsWithUpperKeyword(upperLine, "FUNC"))
		return true;
	return upperLine.starts_with("TYPE ") && (upperLine.find(" STRUCT") != std::string::npos || upperLine.find(" INTERFACE") != std::string::npos);
}

bool isKotlinStrictLead(std::string_view upperLine) {
	return startsWithUpperKeyword(upperLine, "IF") || startsWithUpperKeyword(upperLine, "ELSE") || startsWithUpperKeyword(upperLine, "FOR") || startsWithUpperKeyword(upperLine, "WHILE") ||
	       startsWithUpperKeyword(upperLine, "WHEN") || startsWithUpperKeyword(upperLine, "TRY") || startsWithUpperKeyword(upperLine, "CATCH") || startsWithUpperKeyword(upperLine, "FINALLY") ||
	       startsWithUpperKeyword(upperLine, "FUN") || startsWithUpperKeyword(upperLine, "CLASS") || startsWithUpperKeyword(upperLine, "INTERFACE") || startsWithUpperKeyword(upperLine, "OBJECT") ||
	       startsWithUpperKeyword(upperLine, "ENUM") || startsWithUpperKeyword(upperLine, "COMPANION OBJECT");
}

bool isCSharpStrictLead(std::string_view upperLine) {
	return startsWithUpperKeyword(upperLine, "IF") || startsWithUpperKeyword(upperLine, "ELSE") || startsWithUpperKeyword(upperLine, "FOR") || startsWithUpperKeyword(upperLine, "FOREACH") ||
	       startsWithUpperKeyword(upperLine, "WHILE") || startsWithUpperKeyword(upperLine, "DO") || startsWithUpperKeyword(upperLine, "SWITCH") || startsWithUpperKeyword(upperLine, "TRY") ||
	       startsWithUpperKeyword(upperLine, "CATCH") || startsWithUpperKeyword(upperLine, "FINALLY") || startsWithUpperKeyword(upperLine, "LOCK") || startsWithUpperKeyword(upperLine, "USING") ||
	       startsWithUpperKeyword(upperLine, "NAMESPACE") || startsWithUpperKeyword(upperLine, "CLASS") || startsWithUpperKeyword(upperLine, "STRUCT") || startsWithUpperKeyword(upperLine, "INTERFACE") ||
	       startsWithUpperKeyword(upperLine, "RECORD") || looksLikeFunctionHeaderLead(upperLine);
}

bool isPythonStrictLead(std::string_view upperLine) {
	return upperLine == "ELSE:" || upperLine == "TRY:" || upperLine == "FINALLY:" || upperLine.starts_with("IF ") || upperLine.starts_with("ELIF ") || upperLine.starts_with("FOR ") ||
	       upperLine.starts_with("WHILE ") || upperLine.starts_with("WITH ") || upperLine.starts_with("MATCH ") || upperLine.starts_with("CASE ") || upperLine.starts_with("EXCEPT ") ||
	       upperLine.starts_with("EXCEPT* ") || upperLine.starts_with("DEF ") || upperLine.starts_with("CLASS ") || upperLine.starts_with("ASYNC DEF ") || upperLine.starts_with("ASYNC FOR ") ||
	       upperLine.starts_with("ASYNC WITH ");
}

std::string_view skipLeadingClosersAndSpace(std::string_view text) {
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '}' || text.front() == ')' || text.front() == ']'))
		text.remove_prefix(1);
	return text;
}

bool isPerlPodStart(std::string_view trimmed) {
	return trimmed.starts_with("=") && trimmed != "=cut";
}

bool isPerlPodEnd(std::string_view trimmed) {
	return trimmed == "=cut";
}

bool isPerlCommentOrPodBoundary(std::string_view trimmed) {
	return trimmed.starts_with("#") || isPerlPodStart(trimmed) || isPerlPodEnd(trimmed);
}

bool isPerlStrictLead(std::string_view trimmed, std::string_view upperLine) {
	const std::string_view normalizedTrimmed = trimView(skipLeadingClosersAndSpace(trimmed));
	if (normalizedTrimmed.empty() || isPerlCommentOrPodBoundary(normalizedTrimmed)) return false;
	const std::size_t last = normalizedTrimmed.empty() ? std::string_view::npos : normalizedTrimmed.size() - 1;
	if (last == std::string_view::npos || normalizedTrimmed[last] != '{') return false;
	const std::string_view normalizedUpper = trimView(skipLeadingClosersAndSpace(upperLine));
	return startsWithUpperKeyword(normalizedUpper, "IF") || startsWithUpperKeyword(normalizedUpper, "UNLESS") || startsWithUpperKeyword(normalizedUpper, "ELSIF") || normalizedUpper == "ELSE {" ||
	       startsWithUpperKeyword(normalizedUpper, "FOR") || startsWithUpperKeyword(normalizedUpper, "FOREACH") || startsWithUpperKeyword(normalizedUpper, "WHILE") ||
	       startsWithUpperKeyword(normalizedUpper, "UNTIL") || startsWithUpperKeyword(normalizedUpper, "SUB") || startsWithUpperKeyword(normalizedUpper, "PACKAGE") ||
	       startsWithUpperKeyword(normalizedUpper, "GIVEN") || startsWithUpperKeyword(normalizedUpper, "WHEN") || normalizedUpper == "CONTINUE {" || startsWithUpperKeyword(normalizedUpper, "TRY") ||
	       startsWithUpperKeyword(normalizedUpper, "CATCH") || startsWithUpperKeyword(normalizedUpper, "FINALLY") || normalizedUpper == "BEGIN {" || normalizedUpper == "END {" ||
	       normalizedUpper == "INIT {" || normalizedUpper == "CHECK {" || normalizedUpper == "UNITCHECK {";
}

int nextSignificantPerlBodyColumn(MRFileEditor &editor, std::size_t lineIndex, int leadColumn) {
	bool inPod = false;
	const std::size_t lineCount = editor.bufferModel().lineCount();
	for (std::size_t probe = lineIndex + 1; probe < lineCount; ++probe) {
		const std::size_t lineStart = editor.bufferModel().lineStartByIndex(probe);
		const std::string lineText = editor.bufferModel().lineText(lineStart);
		const std::string_view trimmed = trimView(lineText);
		if (trimmed.empty()) continue;
		if (inPod) {
			if (isPerlPodEnd(trimmed)) inPod = false;
			continue;
		}
		if (isPerlPodStart(trimmed)) {
			inPod = true;
			continue;
		}
		if (trimmed.starts_with("#")) continue;

		const int nextColumn = editor.leadingIndentColumnForLine(lineStart);
		if (nextColumn > leadColumn) return nextColumn;
		return 0;
	}
	return 0;
}

bool isShellFunctionHeadLine(std::string_view trimmed, std::string_view upperLine) {
	const std::string_view normalized = trimView(trimmed);
	if (normalized.empty() || normalized.starts_with("#")) return false;
	std::string_view remainder = normalized;
	if (upperLine.starts_with("FUNCTION ")) {
		remainder.remove_prefix(std::string_view("function ").size());
		remainder = trimView(remainder);
	}
	const std::size_t openParen = remainder.find('(');
	const std::size_t closeParen = remainder.find(')');
	if (openParen == std::string_view::npos || closeParen == std::string_view::npos || closeParen < openParen) return false;
	std::size_t nameEnd = openParen;
	while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(remainder[nameEnd - 1])) != 0)
		--nameEnd;
	if (nameEnd == 0) return false;
	for (std::size_t i = 0; i < nameEnd; ++i) {
		const unsigned char ch = static_cast<unsigned char>(remainder[i]);
		if (!(std::isalnum(ch) != 0 || remainder[i] == '_')) return false;
	}
	for (std::size_t i = openParen + 1; i < closeParen; ++i)
		if (!std::isspace(static_cast<unsigned char>(remainder[i]))) return false;
	const std::string_view tail = trimView(remainder.substr(closeParen + 1));
	return tail.empty() || tail == "{";
}

bool isShellStrictLead(std::string_view trimmed, std::string_view upperLine) {
	if (trimmed.empty() || trimmed.starts_with("#")) return false;
	if (isShellFunctionHeadLine(trimmed, upperLine)) return true;
	return upperLine == "THEN" || upperLine.ends_with(" THEN") || upperLine == "DO" || upperLine.ends_with(" DO") || upperLine == "ELSE" || upperLine.starts_with("ELIF ") ||
	       (upperLine.starts_with("CASE ") && upperLine.ends_with(" IN")) || (upperLine.starts_with("SELECT ") && upperLine.ends_with(" DO")) || (upperLine.starts_with("UNTIL ") && upperLine.ends_with(" DO"));
}

int nextSignificantShellBodyColumn(MRFileEditor &editor, std::size_t lineIndex, MRSyntaxLanguage language, int leadColumn) {
	const std::size_t lineCount = editor.bufferModel().lineCount();
	for (std::size_t probe = lineIndex + 1; probe < lineCount; ++probe) {
		const std::size_t lineStart = editor.bufferModel().lineStartByIndex(probe);
		const std::string lineText = editor.bufferModel().lineText(lineStart);
		const std::string_view trimmed = trimView(lineText);
		if (trimmed.empty() || trimmed.starts_with("#")) continue;
		const int nextColumn = editor.leadingIndentColumnForLine(lineStart);
		if (nextColumn > leadColumn) return nextColumn;
		if (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) return 0;
	}
	return 0;
}

bool isFishStrictLead(std::string_view trimmed, std::string_view upperLine) {
	if (trimmed.empty() || trimmed.starts_with("#")) return false;
	return startsWithUpperKeyword(upperLine, "FUNCTION") || startsWithUpperKeyword(upperLine, "IF") || startsWithUpperKeyword(upperLine, "ELSE IF") || upperLine == "ELSE" ||
	       startsWithUpperKeyword(upperLine, "WHILE") || startsWithUpperKeyword(upperLine, "FOR") || startsWithUpperKeyword(upperLine, "SWITCH") || startsWithUpperKeyword(upperLine, "CASE") ||
	       upperLine == "BEGIN";
}

int nextSignificantFishBodyColumn(MRFileEditor &editor, std::size_t lineIndex, int leadColumn) {
	const std::size_t lineCount = editor.bufferModel().lineCount();
	for (std::size_t probe = lineIndex + 1; probe < lineCount; ++probe) {
		const std::size_t lineStart = editor.bufferModel().lineStartByIndex(probe);
		const std::string lineText = editor.bufferModel().lineText(lineStart);
		const std::string_view trimmed = trimView(lineText);
		if (trimmed.empty() || trimmed.starts_with("#")) continue;
		const int nextColumn = editor.leadingIndentColumnForLine(lineStart);
		if (nextColumn > leadColumn) return nextColumn;
		return 0;
	}
	return 0;
}

bool isBraceStructuralLead(std::string_view trimmed, std::string_view upperLine, MRSyntaxLanguage language) {
	if (trimmed.empty()) return false;
	if (language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp) {
		if (isCLikeCommentOrDirectiveLine(trimmed)) return false;
		if (startsWithUpperKeyword(upperLine, "IF") || startsWithUpperKeyword(upperLine, "ELSE") || startsWithUpperKeyword(upperLine, "FOR") || startsWithUpperKeyword(upperLine, "WHILE") ||
		    startsWithUpperKeyword(upperLine, "SWITCH") || startsWithUpperKeyword(upperLine, "DO") || startsWithUpperKeyword(upperLine, "TRY") || startsWithUpperKeyword(upperLine, "CATCH"))
			return true;
		if (language == MRSyntaxLanguage::Cpp &&
		    (startsWithUpperKeyword(upperLine, "CLASS") || startsWithUpperKeyword(upperLine, "NAMESPACE") || startsWithUpperKeyword(upperLine, "STRUCT")))
			return true;
		return looksLikeFunctionHeaderLead(trimmed);
	}
	if (language == MRSyntaxLanguage::JavaScript) {
		if (isCLikeCommentOrDirectiveLine(trimmed)) return false;
		if (startsWithUpperKeyword(upperLine, "IF") || startsWithUpperKeyword(upperLine, "ELSE") || startsWithUpperKeyword(upperLine, "FOR") || startsWithUpperKeyword(upperLine, "WHILE") ||
		    startsWithUpperKeyword(upperLine, "SWITCH") || startsWithUpperKeyword(upperLine, "DO") || startsWithUpperKeyword(upperLine, "TRY") || startsWithUpperKeyword(upperLine, "CATCH") ||
		    startsWithUpperKeyword(upperLine, "FINALLY") || startsWithUpperKeyword(upperLine, "CLASS") || startsWithUpperKeyword(upperLine, "FUNCTION") ||
		    startsWithUpperKeyword(upperLine, "ASYNC FUNCTION") || startsWithUpperKeyword(upperLine, "RETURN CLASS"))
			return true;
		return trimmed.find("=>") != std::string_view::npos || looksLikeFunctionHeaderLead(trimmed);
	}
	if (language == MRSyntaxLanguage::Swift) {
		if (isCLikeCommentOrDirectiveLine(trimmed)) return false;
		if (startsWithUpperKeyword(upperLine, "IF") || startsWithUpperKeyword(upperLine, "ELSE") || startsWithUpperKeyword(upperLine, "FOR") || startsWithUpperKeyword(upperLine, "WHILE") ||
		    startsWithUpperKeyword(upperLine, "SWITCH") || startsWithUpperKeyword(upperLine, "DO") || startsWithUpperKeyword(upperLine, "CATCH") || startsWithUpperKeyword(upperLine, "FUNC") ||
		    startsWithUpperKeyword(upperLine, "INIT") || startsWithUpperKeyword(upperLine, "DEINIT") || startsWithUpperKeyword(upperLine, "CLASS") || startsWithUpperKeyword(upperLine, "STRUCT") ||
		    startsWithUpperKeyword(upperLine, "ENUM") || startsWithUpperKeyword(upperLine, "EXTENSION") || startsWithUpperKeyword(upperLine, "PROTOCOL") || startsWithUpperKeyword(upperLine, "GET") ||
		    startsWithUpperKeyword(upperLine, "SET") || startsWithUpperKeyword(upperLine, "WILLSET") || startsWithUpperKeyword(upperLine, "DIDSET"))
			return true;
		return startsWithUpperKeyword(upperLine, "VAR") || startsWithUpperKeyword(upperLine, "LET") || looksLikeFunctionHeaderLead(trimmed);
	}
	if (language == MRSyntaxLanguage::Rust) {
		if (trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*") || trimmed.starts_with("*/") || trimmed.starts_with("#")) return false;
		if (trimmed.find('|') != std::string_view::npos && trimmed.find('{') != std::string_view::npos) return true;
		return isRustStrictLead(upperLine) || looksLikeFunctionHeaderLead(trimmed);
	}
	if (language == MRSyntaxLanguage::Go) {
		if (isCLikeCommentOrDirectiveLine(trimmed)) return false;
		return isGoStrictLead(upperLine) || looksLikeFunctionHeaderLead(trimmed);
	}
	if (language == MRSyntaxLanguage::Kotlin) {
		if (isCLikeCommentOrDirectiveLine(trimmed)) return false;
		return isKotlinStrictLead(upperLine) || looksLikeFunctionHeaderLead(trimmed);
	}
	if (language == MRSyntaxLanguage::CSharp) {
		if (isCLikeCommentOrDirectiveLine(trimmed) || trimmed.starts_with("#")) return false;
		return isCSharpStrictLead(upperLine);
	}
	return false;
}

StrictExpectation strictExpectationForLine(MRFileEditor &editor, std::size_t lineIndex, std::string_view lineText, int leadColumn, const MREditSetupSettings &settings, MRSyntaxLanguage language,
                                           MRUiIndentStyle uiIndentStyle, int smartColumn) {
	StrictExpectation result;
	if (settings.indentStyle != "SMART") return result;

	const std::string_view trimmed = trimView(lineText);
	if (trimmed.empty()) return result;

	const std::string upperLine = upperAscii(std::string(trimmed));
	const int braceTargetColumn = leadColumn + braceIndentStepColumns(uiIndentStyle);
	const std::size_t last = trimmed.empty() ? std::string_view::npos : trimmed.size() - 1;

	if (language == MRSyntaxLanguage::Json) {
		if (last != std::string_view::npos && (trimmed[last] == '{' || trimmed[last] == '[')) {
			result.applicable = true;
			result.rule = "json-container";
			result.expectedColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, leadColumn);
		}
	} else if (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) {
		if (isShellStrictLead(trimmed, upperLine)) {
			const int bodyColumn = nextSignificantShellBodyColumn(editor, lineIndex, language, leadColumn);
			if (bodyColumn > leadColumn) {
				result.applicable = true;
				result.rule = "shell-block";
				result.expectedColumn = bodyColumn;
			}
		}
	} else if (language == MRSyntaxLanguage::Fish) {
		if (isFishStrictLead(trimmed, upperLine)) {
			const int bodyColumn = nextSignificantFishBodyColumn(editor, lineIndex, leadColumn);
			if (bodyColumn > leadColumn) {
				result.applicable = true;
				result.rule = "fish-block";
				result.expectedColumn = bodyColumn;
			}
		}
	} else if (language == MRSyntaxLanguage::Python) {
		if (last != std::string_view::npos && trimmed[last] == ':' && isPythonStrictLead(upperLine)) {
			result.applicable = true;
			result.rule = "python-block";
			result.expectedColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, leadColumn);
		}
	} else if (language == MRSyntaxLanguage::Perl) {
		if (isPerlStrictLead(trimmed, upperLine)) {
			const int bodyColumn = nextSignificantPerlBodyColumn(editor, lineIndex, leadColumn);
			if (bodyColumn > leadColumn) {
				result.applicable = true;
				result.rule = "perl-block";
				result.expectedColumn = bodyColumn;
			}
		}
	} else if (isBraceLanguage(language)) {
		if (trimmed == "{") {
			result.applicable = true;
			result.rule = "standalone-brace";
			result.expectedColumn = braceBodyAlignsWithStandaloneBrace(uiIndentStyle) ? leadColumn : braceTargetColumn;
		} else if (last != std::string_view::npos && trimmed[last] == '{' && isBraceStructuralLead(trimmed, upperLine, language)) {
			result.applicable = true;
			result.rule = "brace-head";
			result.expectedColumn = braceTargetColumn;
		} else if (isBraceStructuralLead(trimmed, upperLine, language)) {
			result.applicable = true;
			result.rule = "lead-head";
			result.expectedColumn = braceLeadIndentsBodyOnNextLine(uiIndentStyle) ? braceTargetColumn : leadColumn;
		}
	}

	if (result.applicable) result.passed = smartColumn == result.expectedColumn;
	return result;
}

bool mayNeedSmartDedentProbe(std::string_view lineText, MRSyntaxLanguage language) {
	const std::string_view trimmed = trimView(lineText);
	if (trimmed.empty()) return false;
	if (trimmed.front() == '}' || trimmed.front() == ']' || trimmed.front() == ')') return true;

	const std::string upper = upperAscii(std::string(trimmed));
	if (language == MRSyntaxLanguage::Basic) {
		const MRBasicBlockDisposition disposition = mrBasicClassifyBlockLine(trimmed).disposition;
		return disposition == MRBasicBlockDisposition::Continue || disposition == MRBasicBlockDisposition::Close;
	}
	switch (language) {
		case MRSyntaxLanguage::Bash:
		case MRSyntaxLanguage::Zsh:
			return upper == "FI" || upper == "DONE" || upper == "ESAC" || startsWithUpperKeyword(upper, "ELSE") || startsWithUpperKeyword(upper, "ELIF");
		case MRSyntaxLanguage::Fish:
			return upper == "END" || startsWithUpperKeyword(upper, "ELSE") || startsWithUpperKeyword(upper, "CASE");
		case MRSyntaxLanguage::Python:
			return upper == "ELSE:" || upper == "FINALLY:" || upper == "EXCEPT:" || startsWithUpperKeyword(upper, "ELIF") || startsWithUpperKeyword(upper, "EXCEPT") || startsWithUpperKeyword(upper, "CASE");
		case MRSyntaxLanguage::Perl:
			return startsWithUpperKeyword(upper, "ELSIF") || upper == "ELSE" || upper == "ELSE {";
		case MRSyntaxLanguage::MRMAC:
			return upper == "ELSE" || upper == "END" || upper == "END;" || upper == "END_MACRO" || upper == "END_MACRO;";
		case MRSyntaxLanguage::C:
		case MRSyntaxLanguage::Cpp:
		case MRSyntaxLanguage::JavaScript:
			case MRSyntaxLanguage::Swift:
			case MRSyntaxLanguage::Rust:
			case MRSyntaxLanguage::Go:
			case MRSyntaxLanguage::Kotlin:
			case MRSyntaxLanguage::CSharp:
				return startsWithUpperKeyword(upper, "ELSE") || startsWithUpperKeyword(upper, "CATCH") || startsWithUpperKeyword(upper, "FINALLY");
		default:
			return false;
	}
}

bool containsTrailingSmartSplitToken(std::string_view lineText, MRSyntaxLanguage language) {
	const std::string_view trimmed = trimView(lineText);
	if (trimmed.empty()) return false;

	const std::size_t firstNonSpace = lineText.size() - ltrimView(lineText).size();
	if (firstNonSpace == lineText.size()) return false;

	for (char ch : {'}', ']', ')'}) {
		const std::size_t pos = lineText.find(ch);
		if (pos != std::string_view::npos && pos > firstNonSpace) return true;
	}

	const std::string upper = upperAscii(std::string(lineText));
	if (language == MRSyntaxLanguage::Basic) return mrBasicClassifyBlockLine(trimmed).disposition != MRBasicBlockDisposition::None;
	switch (language) {
		case MRSyntaxLanguage::Bash:
		case MRSyntaxLanguage::Zsh:
			return upper.find(" ELSE") != std::string::npos || upper.find(" ELIF") != std::string::npos || upper.find(" FI") != std::string::npos || upper.find(" DONE") != std::string::npos ||
			       upper.find(" ESAC") != std::string::npos;
		case MRSyntaxLanguage::Fish:
			return upper.find(" ELSE") != std::string::npos || upper.find(" CASE") != std::string::npos || upper.find(" END") != std::string::npos;
		case MRSyntaxLanguage::Python:
			return upper.find(" ELSE:") != std::string::npos || upper.find(" ELIF ") != std::string::npos || upper.find(" EXCEPT") != std::string::npos || upper.find(" FINALLY:") != std::string::npos ||
			       upper.find(" CASE ") != std::string::npos;
		case MRSyntaxLanguage::Perl:
			return upper.find(" ELSE") != std::string::npos || upper.find(" ELSIF") != std::string::npos;
		case MRSyntaxLanguage::MRMAC:
			return upper.find(" ELSE") != std::string::npos || upper.find(" END") != std::string::npos || upper.find(" END_MACRO") != std::string::npos;
		case MRSyntaxLanguage::C:
		case MRSyntaxLanguage::Cpp:
		case MRSyntaxLanguage::JavaScript:
			case MRSyntaxLanguage::Swift:
			case MRSyntaxLanguage::Rust:
			case MRSyntaxLanguage::Go:
			case MRSyntaxLanguage::Kotlin:
			case MRSyntaxLanguage::CSharp:
				return upper.find(" ELSE") != std::string::npos || upper.find(" CATCH") != std::string::npos || upper.find(" FINALLY") != std::string::npos;
		default:
			return false;
	}
}

int targetColumnForFill(std::string_view fill, const MREditSetupSettings &settings) {
	int column = 1;
	for (char ch : fill) {
		if (ch == '\t')
			column = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, column);
		else
			++column;
	}
	return column;
}

void resetEditorForSimulation(MRFileEditor &editor, const std::string &text, const std::string &inputPath, const std::string &codeLanguage) {
	editor.replaceBufferData(text.data(), static_cast<uint>(text.size()));
	editor.setPersistentFileName(inputPath);
	if (!codeLanguage.empty()) editor.bufferModel().setSyntaxContext(inputPath, inputPath, codeLanguage);
}

std::unique_ptr<MRFileEditor> buildEditor(const std::string &text, const std::string &inputPath, const std::string &codeLanguage) {
	TRect bounds(0, 0, 1, 1);
	auto editor = std::make_unique<MRFileEditor>(bounds, nullptr, nullptr, nullptr, TStringView());
	resetEditorForSimulation(*editor, text, inputPath, codeLanguage);
	return editor;
}

MRSyntaxLanguage resolveLanguageForRun(const std::string &text, const std::string &inputPath, const LanguageRun &run) {
	if (!run.automatic) return run.language;

	MRSyntaxClassification classification = tmrClassifySyntaxLanguage(inputPath, inputPath, text);
	if (classification.language != MRSyntaxLanguage::PlainText) return classification.language;
	return tmrDetectSyntaxLanguage(inputPath, inputPath);
}

std::string preferredIndentFillForCursor(MRFileEditor &editor, const MREditSetupSettings &settings) {
	const int targetColumn = resolvedEditFormatIndentColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, editor.displayedCursorColumn() + 1);
	return buildEditIndentFill(settings, 1, targetColumn, settings.tabExpand);
}

std::string modeIndentFillForCursor(MRFileEditor &editor, const MREditSetupSettings &settings) {
	if (settings.indentStyle == "AUTOMATIC") return editor.automaticIndentFillForCursor();
	if (settings.indentStyle == "SMART") return editor.smartIndentFillForCursor();
	return preferredIndentFillForCursor(editor, settings);
}

DedentSimulation simulateLiveDedent(MRFileEditor &editor, const std::string &text, const std::string &inputPath, const std::string &codeLanguage, std::size_t lineIndex, const MREditSetupSettings &settings) {
	DedentSimulation result;
	resetEditorForSimulation(editor, text, inputPath, codeLanguage);
	const std::size_t lineStart = editor.bufferModel().lineStartByIndex(lineIndex);
	const std::string originalLine = editor.bufferModel().lineText(lineStart);
	const std::string_view trimmed = ltrimView(originalLine);
	if (trimmed.empty()) return result;

	const int actualColumn = editor.leadingIndentColumnForLine(lineStart);
	int indentStep = 0;
	std::size_t currentLineStart = lineStart;
	while (currentLineStart > 0) {
		const std::size_t previousLineStart = editor.bufferModel().prevLine(currentLineStart);
		if (previousLineStart == currentLineStart) break;
		currentLineStart = previousLineStart;

		const std::string previousLineText = editor.bufferModel().lineText(previousLineStart);
		if (ltrimView(previousLineText).empty()) continue;

		const int previousColumn = editor.leadingIndentColumnForLine(previousLineStart);
		if (previousColumn < actualColumn) {
			indentStep = actualColumn - previousColumn;
			break;
		}
	}
	if (indentStep <= 0) {
		if (settings.tabExpand)
			indentStep = 4;
		else {
			const int nextColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, actualColumn);
			indentStep = std::max(1, nextColumn - actualColumn);
		}
	}
	const int deeperColumn = actualColumn + indentStep;
	if (deeperColumn <= actualColumn) return result;

	const std::string simulatedLine = buildEditIndentFill(settings, 1, deeperColumn, settings.tabExpand) + std::string(trimmed);
	editor.setCursorOffset(lineStart);
	if (!editor.replaceCurrentLineText(simulatedLine)) return result;

	const std::size_t currentStart = editor.bufferModel().lineStartByIndex(lineIndex);
	editor.setCursorOffset(editor.lineEndOffset(currentStart));
	result.beforeColumn = editor.leadingIndentColumnForLine(currentStart);
	editor.applyLiveSmartDedentAfterTextInput("x");
	result.afterColumn = editor.leadingIndentColumnForLine(currentStart);
	result.changed = result.afterColumn != result.beforeColumn;
	return result;
}

EnterSimulation simulateEnter(MRFileEditor &editor, const std::string &text, const std::string &inputPath, const std::string &codeLanguage, std::size_t lineIndex) {
	EnterSimulation result;
	resetEditorForSimulation(editor, text, inputPath, codeLanguage);
	const std::size_t originalLineStart = editor.bufferModel().lineStartByIndex(lineIndex);
	const std::string originalLine = editor.bufferModel().lineText(originalLineStart);
	const std::size_t lineEnd = editor.lineEndOffset(originalLineStart);

	editor.setCursorOffset(lineEnd);
	if (!editor.newLineWithPreferredIndent()) return result;

	const std::size_t updatedLineStart = editor.bufferModel().lineStartByIndex(lineIndex);
	const std::string updatedLine = editor.bufferModel().lineText(updatedLineStart);
	if (updatedLine == originalLine) return result;

	result.changed = true;
	result.previousLine = updatedLine;
	if (lineIndex + 1 < editor.bufferModel().lineCount()) {
		const std::size_t nextLineStart = editor.bufferModel().lineStartByIndex(lineIndex + 1);
		result.currentLine = editor.bufferModel().lineText(nextLineStart);
	}
	return result;
}

int usage(const char *argv0) {
	std::cerr << "Usage: " << argv0
	          << " [--language=<name|auto|all>[,...]] [--indent-style=<off|automatic|smart|all>[,...]] [--ui-indent-style=<kandr|kandr4|allman|gnome|whitesmiths|horstmann|all>[,...]] [--tab-expand=<on|off|all>[,...]] [--tab-size=<n>] <input> <output|->\n";
	return 2;
}

bool parseIntOption(const std::string &value, int &number) {
	if (value.empty()) return false;
	for (char ch : value)
		if (ch < '0' || ch > '9') return false;
	number = std::stoi(value);
	return number > 0;
}

void writeConfigurationReport(std::ostream &output, const std::string &text, const std::string &inputPath, const LanguageRun &requestedLanguage, MRUiIndentStyle uiIndentStyle,
                              const MREditSetupSettings &settings) {
	const MRSyntaxLanguage effectiveLanguage = resolveLanguageForRun(text, inputPath, requestedLanguage);
	auto editor = buildEditor(text, inputPath, settings.codeLanguage);
	const bool simulateDedent = settings.indentStyle == "SMART";
	const bool simulateEnterSplit = settings.indentStyle == "SMART";
	std::unique_ptr<MRFileEditor> dedentEditor;
	std::unique_ptr<MRFileEditor> enterEditor;
	std::size_t strictChecked = 0;
	std::size_t strictFailed = 0;

	if (simulateDedent) dedentEditor = buildEditor(text, inputPath, settings.codeLanguage);
	if (simulateEnterSplit) enterEditor = buildEditor(text, inputPath, settings.codeLanguage);

	output << "CONFIG requested_language=" << requestedLanguageName(requestedLanguage) << " effective_language=" << languageName(effectiveLanguage) << " code_language=" << settings.codeLanguage
	       << " indent_style=" << settings.indentStyle << " ui_indent_style=" << uiIndentStyleName(uiIndentStyle) << " tab_expand=" << boolName(settings.tabExpand) << " tab_size=" << settings.tabSize
	       << "\n";

	const std::size_t lineCount = editor->bufferModel().lineCount();
	for (std::size_t lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
		const std::size_t lineStart = editor->bufferModel().lineStartByIndex(lineIndex);
		const std::size_t lineEnd = editor->lineEndOffset(lineStart);
		const std::string lineText = editor->bufferModel().lineText(lineStart);
		editor->setCursorOffset(lineEnd);

		const std::string preferredFill = preferredIndentFillForCursor(*editor, settings);
		const std::string automaticFill = editor->automaticIndentFillForCursor();
		const std::string smartFill = editor->smartIndentFillForCursor();
		const std::string modeFill = modeIndentFillForCursor(*editor, settings);
		const int leadColumn = editor->leadingIndentColumnForLine(lineStart);
		const bool dedentCandidate = simulateDedent && leadColumn > 1 && mayNeedSmartDedentProbe(lineText, effectiveLanguage);
		const bool enterCandidate = simulateEnterSplit && containsTrailingSmartSplitToken(lineText, effectiveLanguage);
		const DedentSimulation dedent = dedentCandidate ? simulateLiveDedent(*dedentEditor, text, inputPath, settings.codeLanguage, lineIndex, settings) : DedentSimulation();
		const EnterSimulation enter = enterCandidate ? simulateEnter(*enterEditor, text, inputPath, settings.codeLanguage, lineIndex) : EnterSimulation();
		const int preferredColumn = targetColumnForFill(preferredFill, settings);
		const int automaticColumn = targetColumnForFill(automaticFill, settings);
		const int smartColumn = targetColumnForFill(smartFill, settings);
		const int modeColumn = targetColumnForFill(modeFill, settings);
		const StrictExpectation strict = strictExpectationForLine(*editor, lineIndex, lineText, leadColumn, settings, effectiveLanguage, uiIndentStyle, smartColumn);
		if (strict.applicable) {
			++strictChecked;
			if (!strict.passed) ++strictFailed;
		}

		output << std::setw(5) << (lineIndex + 1) << " lead=" << leadColumn << " preferred=" << preferredColumn << "(\"" << escapeForReport(preferredFill) << "\")"
		       << " auto=" << automaticColumn << "(\"" << escapeForReport(automaticFill) << "\")"
		       << " smart=" << smartColumn << "(\"" << escapeForReport(smartFill) << "\")"
		       << " mode=" << modeColumn << "(\"" << escapeForReport(modeFill) << "\")";
		if (dedent.changed)
			output << " dedent=" << dedent.beforeColumn << "->" << dedent.afterColumn;
		else
			output << " dedent=-";
		if (enter.changed)
			output << " enter=prev:'" << escapeForReport(enter.previousLine) << "' next:'" << escapeForReport(enter.currentLine) << "'";
		else
			output << " enter=-";
		if (strict.applicable)
			output << " strict=" << (strict.passed ? "PASS:" : "FAIL:") << strict.rule << ":" << strict.expectedColumn;
		else
			output << " strict=-";
		output << " text=" << escapeForReport(lineText) << "\n";
	}
	output << "SUMMARY strict_checked=" << strictChecked << " strict_failed=" << strictFailed << "\n";
	output << "END_CONFIG\n";
}

} // namespace

int main(int argc, char **argv) {
	TrainerOptions options;
	options.languages.push_back({MRSyntaxLanguage::PlainText, true});
	options.indentStyles.push_back("SMART");
	options.uiIndentStyles.push_back(configuredUiIndentStyle());
	options.tabExpandValues.push_back(configuredEditSetupSettings().tabExpand);
	options.tabSize = configuredEditSetupSettings().tabSize;

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg.starts_with("--language=")) {
			options.languages.clear();
			if (!parseLanguageOption(arg.substr(std::string("--language=").size()), options.languages)) return usage(argv[0]);
		} else if (arg.starts_with("--indent-style=")) {
			options.indentStyles.clear();
			if (!parseIndentStyleOption(arg.substr(std::string("--indent-style=").size()), options.indentStyles)) return usage(argv[0]);
		} else if (arg.starts_with("--ui-indent-style=")) {
			options.uiIndentStyles.clear();
			if (!parseUiIndentStyleOption(arg.substr(std::string("--ui-indent-style=").size()), options.uiIndentStyles)) return usage(argv[0]);
		} else if (arg.starts_with("--tab-expand=")) {
			options.tabExpandValues.clear();
			if (!parseBoolMatrixOption(arg.substr(std::string("--tab-expand=").size()), options.tabExpandValues)) return usage(argv[0]);
		} else if (arg.starts_with("--tab-size=")) {
			if (!parseIntOption(arg.substr(std::string("--tab-size=").size()), options.tabSize)) return usage(argv[0]);
		} else if (options.inputPath.empty())
			options.inputPath = arg;
		else if (options.outputPath.empty())
			options.outputPath = arg;
		else
			return usage(argv[0]);
	}
	if (options.inputPath.empty() || options.outputPath.empty()) return usage(argv[0]);

	std::ifstream input(options.inputPath, std::ios::binary);
	if (!input) {
		std::cerr << "cannot open input: " << options.inputPath << "\n";
		return 1;
	}
	const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

	std::ofstream outputFile;
	std::ostream *output = &std::cout;
	if (options.outputPath != "-") {
		outputFile.open(options.outputPath, std::ios::binary | std::ios::trunc);
		if (!outputFile) {
			std::cerr << "cannot open output: " << options.outputPath << "\n";
			return 1;
		}
		output = &outputFile;
	}

	EditSettingsGuard guard;
	UiIndentStyleGuard uiIndentStyleGuard;
	guard.restore = true;
	uiIndentStyleGuard.restore = true;
	std::vector<std::tuple<std::string, std::string, std::string, std::string, bool, int>> emittedConfigurations;
	for (const LanguageRun &language : options.languages)
		for (const std::string &indentStyle : options.indentStyles)
			for (MRUiIndentStyle uiIndentStyle : options.uiIndentStyles)
				for (bool tabExpand : options.tabExpandValues) {
					const MRSyntaxLanguage effectiveLanguage = resolveLanguageForRun(text, options.inputPath, language);
					MREditSetupSettings settings = guard.previous;
					settings.indentStyle = indentStyle;
					settings.tabExpand = tabExpand;
					settings.tabSize = options.tabSize;
					settings.codeLanguage = languageSettingName(effectiveLanguage, language.automatic);
					std::string errorText;
					if (!setConfiguredUiIndentStyle(uiIndentStyle, &errorText)) {
						std::cerr << "cannot configure ui indent style: " << errorText << "\n";
						return 1;
					}
					if (!setConfiguredEditSetupSettings(settings, &errorText)) {
						std::cerr << "cannot configure edit settings: " << errorText << "\n";
						return 1;
					}
					const MREditSetupSettings effectiveSettings = configuredEditSetupSettings();
					const auto configurationKey =
					    std::make_tuple(effectiveSettings.indentStyle, uiIndentStyleName(uiIndentStyle), effectiveSettings.codeLanguage, requestedLanguageName(language), effectiveSettings.tabExpand,
					                    effectiveSettings.tabSize);
					if (std::find(emittedConfigurations.begin(), emittedConfigurations.end(), configurationKey) != emittedConfigurations.end()) continue;
					emittedConfigurations.push_back(configurationKey);
					writeConfigurationReport(*output, text, options.inputPath, language, uiIndentStyle, effectiveSettings);
				}
	return 0;
}
