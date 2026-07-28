#include "../MRSyntax.hpp"

#include <array>
#include <climits>
#include <cctype>
#include <string_view>

namespace {
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

bool hasSystemdUnitSuffix(std::string_view lowerName) noexcept {
	static constexpr std::string_view kSystemdSuffixes[] = {
		".service",   ".socket", ".timer", ".mount",  ".automount", ".target", ".path",   ".slice", ".scope",
		".swap",      ".device", ".link",  ".netdev", ".network",   ".service.in", ".socket.in", ".timer.in",
		".mount.in",  ".automount.in", ".target.in", ".path.in", ".slice.in", ".scope.in", ".swap.in",
		".device.in", ".link.in", ".netdev.in", ".network.in"
	};

	for (std::string_view suffix : kSystemdSuffixes)
		if (lowerName.ends_with(suffix)) return true;
	return false;
}

constexpr std::size_t kSyntaxLanguageCount = static_cast<std::size_t>(MRSyntaxLanguage::Basic) + 1;

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
		if (line.starts_with(prefix)) ++count;
	}
	return count;
}

bool isCLikeFunctionHeaderLine(std::string_view line) noexcept {
	static constexpr std::string_view kPrefixes[] = {
		"int ",      "void ",       "char ",         "short ",       "long ",      "float ",      "double ",      "signed ",
		"unsigned ", "static int ", "static void ",  "static char ", "static long ","static float ","static double ","const char "
	};

	line = trimWhitespaceView(line);
	if (line.empty()) return false;
	for (std::string_view prefix : kPrefixes) {
		if (!line.starts_with(prefix)) continue;
		const std::size_t lparen = line.find('(');
		const std::size_t rparen = line.find(')', lparen == std::string_view::npos ? 0 : lparen + 1);
		if (lparen == std::string_view::npos || rparen == std::string_view::npos || lparen == 0) return false;
		if (line.find('=', 0) != std::string_view::npos && line.find('=', 0) < lparen) return false;
		const char beforeParen = line[lparen - 1];
		if (!(std::isalnum(static_cast<unsigned char>(beforeParen)) || beforeParen == '_')) return false;
		return true;
	}
	return false;
}

int countCLikeFunctionHeaderLines(std::string_view text, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	while (pos < text.size() && count < maxCount)
		if (isCLikeFunctionHeaderLine(nextLineView(text, pos))) ++count;
	return count;
}

bool isPythonBlockHeader(std::string_view line) noexcept {
	static const std::array<std::string_view, 15> prefixes = {
		"def ", "class ", "if ", "elif ", "else:", "for ", "while ", "with ", "try:", "except", "finally:", "async def ", "match ", "case ", "except*"
	};

	line = trimWhitespaceView(line);
	if (line.empty() || line.back() != ':') return false;
	for (std::string_view prefix : prefixes)
		if (line.starts_with(prefix)) return true;
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

int countXmlTagLikeLines(std::string_view text, int maxCount = INT_MAX) noexcept {
	int count = 0;
	std::size_t pos = 0;

	while (pos < text.size() && count < maxCount) {
		const std::string_view line = trimWhitespaceView(nextLineView(text, pos));
		if (line.size() < 3 || line.front() != '<' || line.find('>') == std::string_view::npos) continue;
		const char second = line[1];
		if (second == '?' || second == '!' || second == '/' || std::isalpha(static_cast<unsigned char>(second)) != 0 || second == '_' || second == ':') ++count;
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
		if (line.starts_with("my $") || line.starts_with("my @") || line.starts_with("my %") || line.starts_with("our $") || line.starts_with("our @") || line.starts_with("our %")) ++count;
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
	if (line.starts_with("case ") || line.starts_with("default:")) return false;
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
		if (line.starts_with("#") || line.starts_with(">") || line.starts_with("```") || line.starts_with("~~~") || line.starts_with("- ") || line.starts_with("* ") || line.starts_with("+ ") || line.starts_with("1. ") || containsText(line, "](") || containsText(line, "![") || containsText(line, "| ---")) ++count;
	}
	return count;
}

} // namespace

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
	if (ext == ".yml" || ext == ".yaml") return MRSyntaxLanguage::Yaml;
	if (ext == ".xml" || ext == ".xsd" || ext == ".xsl" || ext == ".xslt" || ext == ".svg") return MRSyntaxLanguage::Xml;
	if (ext == ".sh" || ext == ".bash" || ext == ".ksh") return MRSyntaxLanguage::Bash;
	if (lowerName == ".bashrc" || lowerName == ".bash_profile" || lowerName == ".profile") return MRSyntaxLanguage::Bash;
	if (ext == ".zsh" || ext == ".zprofile" || ext == ".zshrc" || ext == ".zshenv" || ext == ".zlogin" || ext == ".zlogout") return MRSyntaxLanguage::Zsh;
	if (lowerName == ".zshrc" || lowerName == ".zprofile" || lowerName == ".zshenv" || lowerName == ".zlogin" || lowerName == ".zlogout") return MRSyntaxLanguage::Zsh;
	if (ext == ".fish" || lowerName == "config.fish") return MRSyntaxLanguage::Fish;
	if (ext == ".pl" || ext == ".pm" || ext == ".t" || ext == ".pod" || ext == ".cgi" || ext == ".psgi" || ext == ".perl") return MRSyntaxLanguage::Perl;
	if (ext == ".swift") return MRSyntaxLanguage::Swift;
	if (ext == ".rs") return MRSyntaxLanguage::Rust;
	if (ext == ".go") return MRSyntaxLanguage::Go;
	if (ext == ".kt" || ext == ".kts") return MRSyntaxLanguage::Kotlin;
	if (ext == ".cs" || ext == ".csx" || ext == ".cake") return MRSyntaxLanguage::CSharp;
	if (ext == ".pas" || ext == ".pp" || ext == ".lpr" || ext == ".dpr") return MRSyntaxLanguage::Pascal;
	if (ext == ".bas" || ext == ".bi" || ext == ".bm" || ext == ".qb" || ext == ".qbas" || ext == ".module" || ext == ".class") return MRSyntaxLanguage::Basic;
	if (ext == ".service" || ext == ".socket" || ext == ".timer" || ext == ".mount" || ext == ".automount" || ext == ".target" || ext == ".path" || ext == ".slice" || ext == ".scope" ||
	    ext == ".swap" || ext == ".device" || ext == ".link" || ext == ".netdev" || ext == ".network" || hasSystemdUnitSuffix(lowerName))
		return MRSyntaxLanguage::Systemd;
	if (ext == ".mrmac") return MRSyntaxLanguage::MRMAC;
	if (ext == ".md" || ext == ".markdown" || lowerName == "readme") return MRSyntaxLanguage::Markdown;
	if (ext == ".tex" || ext == ".ltx" || ext == ".sty" || ext == ".cls") return MRSyntaxLanguage::Latex;
	return MRSyntaxLanguage::PlainText;
}

MRSyntaxClassification tmrClassifySyntaxLanguage(const std::string &path, const std::string &title, std::string_view text) {
	std::array<int, kSyntaxLanguageCount> scores {};
	std::array<int, kSyntaxLanguageCount> strongSignals {};
	const std::string fileName = fileNamePart(!path.empty() ? path : title);
	const std::string lowerName = lowerCopy(fileName);
	const std::string ext = extensionPart(fileName);
	const bool forceCLanguageByExtension = ext == ".c";
	const bool forceCppLanguageByExtension =
	    ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".hh" || ext == ".hpp" || ext == ".hxx" || ext == ".ipp" || ext == ".tpp" || ext == ".inl";
	const bool forceYamlLanguageByExtension = ext == ".yml" || ext == ".yaml";
	const bool forceXmlLanguageByExtension = ext == ".xml" || ext == ".xsd" || ext == ".xsl" || ext == ".xslt" || ext == ".svg";
	const bool forceKotlinLanguageByExtension = ext == ".kt" || ext == ".kts";
	const bool forceCSharpLanguageByExtension = ext == ".cs" || ext == ".csx" || ext == ".cake";
	const bool forcePascalLanguageByExtension = ext == ".pas" || ext == ".pp" || ext == ".lpr" || ext == ".dpr";
	const bool forceBasicLanguageByExtension = ext == ".bas" || ext == ".bi" || ext == ".bm" || ext == ".qb" || ext == ".qbas" || ext == ".module" || ext == ".class";
	const bool forceLatexLanguageByExtension = ext == ".tex" || ext == ".ltx" || ext == ".sty" || ext == ".cls";
	const std::string_view sample = classificationSample(text);
	const std::string lowerSample = lowerCopyView(sample);
	const std::string_view lower = lowerSample;
	const std::string_view firstLine = firstLineView(sample);
	const std::string lowerFirstLine = lowerCopyView(firstLine);
	const std::string_view lowerShebang = lowerFirstLine;
	const MRSyntaxLanguage detectedByPath = tmrDetectSyntaxLanguage(path, title);

	if (forceCLanguageByExtension) return MRSyntaxClassification(MRSyntaxLanguage::C, 100);
	if (forceCppLanguageByExtension) return MRSyntaxClassification(MRSyntaxLanguage::Cpp, 100);
	if (forceYamlLanguageByExtension) return MRSyntaxClassification(MRSyntaxLanguage::Yaml, 100);
	if (forceXmlLanguageByExtension) return MRSyntaxClassification(MRSyntaxLanguage::Xml, 100);
	if (forceKotlinLanguageByExtension) return MRSyntaxClassification(MRSyntaxLanguage::Kotlin, 100);
	if (forceCSharpLanguageByExtension) return MRSyntaxClassification(MRSyntaxLanguage::CSharp, 100);
	if (forcePascalLanguageByExtension) return MRSyntaxClassification(MRSyntaxLanguage::Pascal, 100);
	if (forceBasicLanguageByExtension) return MRSyntaxClassification(MRSyntaxLanguage::Basic, 100);
	if (forceLatexLanguageByExtension) return MRSyntaxClassification(MRSyntaxLanguage::Latex, 100);

	const int includeLines = countLinePrefixMatches(lower, "#include", 8);
	const int cStdHeaderIncludes = countMatches(lower, "<assert.h>", 4) + countMatches(lower, "<ctype.h>", 4) + countMatches(lower, "<errno.h>", 4) + countMatches(lower, "<float.h>", 4) +
	                               countMatches(lower, "<limits.h>", 4) + countMatches(lower, "<math.h>", 4) + countMatches(lower, "<setjmp.h>", 4) + countMatches(lower, "<signal.h>", 4) +
	                               countMatches(lower, "<stdarg.h>", 4) + countMatches(lower, "<stdbool.h>", 4) + countMatches(lower, "<stddef.h>", 4) + countMatches(lower, "<stdint.h>", 4) +
	                               countMatches(lower, "<stdio.h>", 4) + countMatches(lower, "<stdlib.h>", 4) + countMatches(lower, "<string.h>", 4) + countMatches(lower, "<time.h>", 4);
	const int defineLines = countLinePrefixMatches(lower, "#define", 8);
	const int typedefLines = countLinePrefixMatches(lower, "typedef ", 8);
	const int namespaceLines = countLinePrefixMatches(lower, "namespace ", 8);
	const int templateLines = countLinePrefixMatches(lower, "template<", 8);
	const int cppClassLines = countLinePrefixMatches(lower, "class ", 8);
	const int mainFunctionMentions = countMatches(lower, "main(", 4);
	const int cFunctionHeaderLines = countCLikeFunctionHeaderLines(sample, 8);
	const int importLines = countLinePrefixMatches(lower, "import ", 8);
	const int exportLines = countLinePrefixMatches(lower, "export ", 8);
	const int functionLines = countLinePrefixMatches(lower, "function ", 8);
	const int constLines = countLinePrefixMatches(lower, "const ", 12);
	const int letLines = countLinePrefixMatches(lower, "let ", 12);
	const int pythonDefLines = countLinePrefixMatches(lower, "def ", 12);
	const int pythonClassLines = countLinePrefixMatches(lower, "class ", 8);
	const int pythonBlockHeaders = countPythonBlockHeaders(sample, 16);
	const int jsonKeyLines = countJsonKeyLikeLines(sample, 32);
	const int xmlTagLines = countXmlTagLikeLines(sample, 32);
	const int shellAssignmentLines = countShellAssignmentLines(sample, 16);
	const int fishFunctionLines = countLinePrefixMatches(lower, "function ", 12);
	const int fishSetLines = countLinePrefixMatches(lower, "set ", 16) + countLinePrefixMatches(lower, "set -", 16);
	const int fishSwitchLines = countLinePrefixMatches(lower, "switch ", 12) + countLinePrefixMatches(lower, "case ", 16);
	const int fishBlockLines =
	    countLinePrefixMatches(lower, "begin", 12) + countLinePrefixMatches(lower, "if ", 16) + countLinePrefixMatches(lower, "else if ", 16) + countLinePrefixMatches(lower, "else", 8) +
	    countLinePrefixMatches(lower, "for ", 12) + countLinePrefixMatches(lower, "while ", 12) + countLinePrefixMatches(lower, "end", 24);
	const int perlSigilDeclLines = countPerlSigilDeclLines(sample, 16);
	const int rustFunctionLines = countLinePrefixMatches(lower, "fn ", 12) + countLinePrefixMatches(lower, "pub fn ", 12) + countLinePrefixMatches(lower, "async fn ", 8) +
	                              countLinePrefixMatches(lower, "pub async fn ", 8);
	const int rustConcreteBlockLines = countLinePrefixMatches(lower, "impl ", 12);
	const int rustStructLines = countLinePrefixMatches(lower, "struct ", 8) + countLinePrefixMatches(lower, "pub struct ", 8) + countLinePrefixMatches(lower, "enum ", 8) +
	                            countLinePrefixMatches(lower, "pub enum ", 8) + countLinePrefixMatches(lower, "trait ", 8) + countLinePrefixMatches(lower, "pub trait ", 8);
	const int goFunctionLines = countLinePrefixMatches(lower, "func ", 12) + countLinePrefixMatches(lower, "func (", 12);
	const int goTypeLines = countLinePrefixMatches(lower, "type ", 12) + countMatches(lower, " struct {", 12) + countMatches(lower, " interface {", 12);
	const int goPackageLines = countLinePrefixMatches(lower, "package ", 4);
	const int goImportLines = countLinePrefixMatches(lower, "import ", 12);
	const int kotlinFunctionLines = countLinePrefixMatches(lower, "fun ", 12) + countLinePrefixMatches(lower, "suspend fun ", 8) + countMatches(lower, " fun ", 16);
	const int kotlinTypeLines = countLinePrefixMatches(lower, "class ", 8) + countLinePrefixMatches(lower, "data class ", 8) + countLinePrefixMatches(lower, "sealed class ", 8) +
	                            countLinePrefixMatches(lower, "enum class ", 8) + countLinePrefixMatches(lower, "interface ", 8) + countLinePrefixMatches(lower, "object ", 8);
	const int kotlinValueLines = countLinePrefixMatches(lower, "val ", 12) + countLinePrefixMatches(lower, "var ", 12);
	const int csharpTypeLines = countMatches(lower, " class ", 12) + countMatches(lower, " interface ", 12) + countMatches(lower, " struct ", 12) + countMatches(lower, " record ", 12) +
	                            countLinePrefixMatches(lower, "class ", 8) + countLinePrefixMatches(lower, "interface ", 8) + countLinePrefixMatches(lower, "struct ", 8) +
	                            countLinePrefixMatches(lower, "record ", 8);
	const int csharpNamespaceLines = countLinePrefixMatches(lower, "namespace ", 8) + countMatches(lower, "\nnamespace ", 8);
	const int csharpUsingLines = countLinePrefixMatches(lower, "using ", 12);
	const int systemdSectionLines = countLinePrefixMatches(lower, "[unit]", 8) + countLinePrefixMatches(lower, "[service]", 8) + countLinePrefixMatches(lower, "[socket]", 8) +
	                                countLinePrefixMatches(lower, "[timer]", 8) + countLinePrefixMatches(lower, "[path]", 8) + countLinePrefixMatches(lower, "[mount]", 8) +
	                                countLinePrefixMatches(lower, "[install]", 8) + countLinePrefixMatches(lower, "[network]", 8) + countLinePrefixMatches(lower, "[netdev]", 8) +
	                                countLinePrefixMatches(lower, "[match]", 8);
	const int systemdDirectiveLines = countLinePrefixMatches(lower, "description=", 12) + countLinePrefixMatches(lower, "execstart=", 12) + countLinePrefixMatches(lower, "wantedby=", 12) +
	                                  countLinePrefixMatches(lower, "after=", 12) + countLinePrefixMatches(lower, "requires=", 12);
	const int makeTargetLines = countMakeTargetLikeLines(sample, 16);
	const int makeRecipeLines = countRecipeTabLines(sample, 16);
	const int markdownStructureLines = countMarkdownStructureLines(sample, 24);
	const int latexDocumentLines = countMatches(lower, "\\documentclass", 4) + countMatches(lower, "\\begin{document}", 4) + countMatches(lower, "\\end{document}", 4);
	const int latexSectionLines = countMatches(lower, "\\section", 12) + countMatches(lower, "\\subsection", 12) + countMatches(lower, "\\chapter", 8) + countMatches(lower, "\\part", 8);
	const int latexEnvironmentLines = countMatches(lower, "\\begin{", 16) + countMatches(lower, "\\end{", 16);
	const int basicOptionLines = countLinePrefixMatches(lower, "option ", 8);
	const int basicProcedureLines = countLinePrefixMatches(lower, "sub ", 12) + countLinePrefixMatches(lower, "function ", 12) + countLinePrefixMatches(lower, "public sub ", 12) +
	                                countLinePrefixMatches(lower, "private sub ", 12);
	const int basicBlockLines = countLinePrefixMatches(lower, "end sub", 12) + countLinePrefixMatches(lower, "end function", 12) + countLinePrefixMatches(lower, "end if", 12) +
	                            countLinePrefixMatches(lower, "end select", 12) + countLinePrefixMatches(lower, "select case", 12);
	const int semicolonCount = countCharacter(sample, ';', 32);
	const int braceCount = countCharacter(sample, '{', 32) + countCharacter(sample, '}', 32);
	const int shellControlCount = countMatches(lower, "[[", 12) + countMatches(lower, "case ", 8) + countMatches(lower, "typeset ", 8) + countMatches(lower, "autoload ", 8) + countMatches(lower, "setopt ", 8);

	if (detectedByPath == MRSyntaxLanguage::Systemd && systemdSectionLines > 0) return MRSyntaxClassification(MRSyntaxLanguage::Systemd, 96);
	if (systemdSectionLines > 0 && systemdDirectiveLines > 0) return MRSyntaxClassification(MRSyntaxLanguage::Systemd, 94);

	if (lowerShebang.starts_with("#!")) {
		if (containsText(lowerShebang, "python")) addClassificationScore(scores, MRSyntaxLanguage::Python, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Python)] += 2;
		if (containsText(lowerShebang, "perl")) addClassificationScore(scores, MRSyntaxLanguage::Perl, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Perl)] += 2;
		if (containsText(lowerShebang, "zsh")) addClassificationScore(scores, MRSyntaxLanguage::Zsh, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Zsh)] += 2;
		if (containsText(lowerShebang, "bash") || containsText(lowerShebang, "/sh")) addClassificationScore(scores, MRSyntaxLanguage::Bash, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Bash)] += 2;
		if (containsText(lowerShebang, "fish")) addClassificationScore(scores, MRSyntaxLanguage::Fish, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Fish)] += 2;
		if (containsText(lowerShebang, "node")) addClassificationScore(scores, MRSyntaxLanguage::JavaScript, 12), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::JavaScript)] += 2;
		if (containsText(lowerShebang, "make")) addClassificationScore(scores, MRSyntaxLanguage::Make, 8), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Make)] += 1;
		if (containsText(lowerShebang, "rust")) addClassificationScore(scores, MRSyntaxLanguage::Rust, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Rust)] += 2;
		if (containsText(lowerShebang, "go")) addClassificationScore(scores, MRSyntaxLanguage::Go, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Go)] += 2;
		if (containsText(lowerShebang, "kotlin")) addClassificationScore(scores, MRSyntaxLanguage::Kotlin, 14), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Kotlin)] += 2;
	}

	if (detectedByPath != MRSyntaxLanguage::PlainText) {
		int pathBias = 4;
		if (ext == ".c" || ext == ".h" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".hh" || ext == ".hpp" || ext == ".hxx" || ext == ".ipp" || ext == ".tpp" || ext == ".inl" ||
		    ext == ".js" || ext == ".jsx" || ext == ".mjs" || ext == ".cjs" || ext == ".ts" || ext == ".tsx" || ext == ".json" || ext == ".jsonc" || ext == ".pl" || ext == ".pm" ||
		    ext == ".pod" || ext == ".swift" || ext == ".rs" || ext == ".go" || ext == ".kt" || ext == ".kts" || ext == ".cs" || ext == ".csx" || ext == ".cake" || ext == ".xml" ||
		    ext == ".xsd" || ext == ".xsl" || ext == ".xslt" || ext == ".svg" || ext == ".mrmac" || ext == ".service" || ext == ".socket" || ext == ".timer" || ext == ".mount" ||
		    ext == ".automount" || ext == ".target" || ext == ".path" || ext == ".slice" || ext == ".scope" || ext == ".swap" || ext == ".device" || ext == ".link" ||
		    ext == ".netdev" || ext == ".network" || ext == ".tex" || ext == ".ltx" || ext == ".sty" || ext == ".cls")
			pathBias = 14;
		if (detectedByPath == MRSyntaxLanguage::Systemd)
			pathBias = 24;
		else if (ext == ".py" || ext == ".pyw" || ext == ".zsh" || ext == ".sh" || ext == ".bash" || ext == ".ksh" || ext == ".fish" || ext == ".md" || ext == ".markdown" || ext == ".service" ||
		         ext == ".socket" || ext == ".timer" || ext == ".mount" || ext == ".automount" || ext == ".target" || ext == ".path" || ext == ".slice" || ext == ".scope" ||
		         ext == ".swap" || ext == ".device" || ext == ".link" || ext == ".netdev" || ext == ".network")
			pathBias = 10;
		addClassificationScore(scores, detectedByPath, pathBias);
		if (pathBias >= 10) ++strongSignals[syntaxLanguageIndex(detectedByPath)];
	}
	if (ext == ".pl" || ext == ".pm") addClassificationScore(scores, MRSyntaxLanguage::Perl, 6), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Perl)] += 1;
	if (lowerName == "makefile" || lowerName == "gnumakefile") addClassificationScore(scores, MRSyntaxLanguage::Make, 10), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Make)] += 2;
	if (lowerName == "readme" || lowerName.starts_with("readme.")) addClassificationScore(scores, MRSyntaxLanguage::Markdown, 6), strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Markdown)] += 1;

	addClassificationScore(scores, MRSyntaxLanguage::C, includeLines * 5);
	addClassificationScore(scores, MRSyntaxLanguage::C, cStdHeaderIncludes * 4);
	addClassificationScore(scores, MRSyntaxLanguage::C, defineLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::C, typedefLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::C, countMatches(lower, "struct ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::C, countMatches(lower, "enum ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::C, countMatches(lower, "->", 12));
	addClassificationScore(scores, MRSyntaxLanguage::C, cFunctionHeaderLines * 4);
	if (includeLines + typedefLines > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::C)] += std::min(3, includeLines + typedefLines);
	if (cStdHeaderIncludes > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::C)] += std::min(2, cStdHeaderIncludes);
	if (cFunctionHeaderLines > 0 && namespaceLines == 0 && templateLines == 0 && cppClassLines == 0 && countMatches(lower, "::", 16) == 0) {
		if (braceCount >= 2) addClassificationScore(scores, MRSyntaxLanguage::C, 4);
		if (defineLines > 0) addClassificationScore(scores, MRSyntaxLanguage::C, 3);
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::C)] += std::min(2, cFunctionHeaderLines);
	}
	if (includeLines > 0 && mainFunctionMentions > 0 && namespaceLines == 0 && templateLines == 0 && cppClassLines == 0 && countMatches(lower, "::", 16) == 0) {
		addClassificationScore(scores, MRSyntaxLanguage::C, 4 + mainFunctionMentions * 2);
		++strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::C)];
	}

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

	addClassificationScore(scores, MRSyntaxLanguage::Xml, countMatches(lower, "<?xml", 2) * 8);
	addClassificationScore(scores, MRSyntaxLanguage::Xml, countMatches(lower, "</", 16) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Xml, countMatches(lower, "<!--", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Xml, countMatches(lower, "/>", 16));
	addClassificationScore(scores, MRSyntaxLanguage::Xml, countMatches(lower, "=\"", 16) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Xml, countMatches(lower, "<![cdata[", 4) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Xml, countMatches(lower, "<!doctype", 4) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Xml, xmlTagLines * 2);
	if (containsText(lower, "<?xml") || containsText(lower, "<![cdata[") || containsText(lower, "<!doctype")) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Xml)] += 2;
	if (xmlTagLines >= 2) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Xml)] += std::min(3, xmlTagLines);

	addClassificationScore(scores, MRSyntaxLanguage::Bash, countMatches(lower, "[[", 12) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Bash, countMatches(lower, "${", 16));
	addClassificationScore(scores, MRSyntaxLanguage::Bash, countMatches(lower, "$(", 16));
	addClassificationScore(scores, MRSyntaxLanguage::Bash, countMatches(lower, "case ", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Bash, countMatches(lower, " in\n", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Bash, countMatches(lower, "declare ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Bash, countMatches(lower, "readonly ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Bash, countMatches(lower, "shopt ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Bash, countMatches(lower, "source ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Bash, countMatches(lower, "<<", 8));
	addClassificationScore(scores, MRSyntaxLanguage::Bash, shellAssignmentLines * 2);
	if (countMatches(lower, "shopt ", 8) + shellAssignmentLines > 0) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Bash)] += std::min(4, countMatches(lower, "shopt ", 8) + shellAssignmentLines);

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

	addClassificationScore(scores, MRSyntaxLanguage::Fish, fishFunctionLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Fish, fishSetLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Fish, fishSwitchLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Fish, fishBlockLines * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Fish, countMatches(lower, "string ", 12) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Fish, countMatches(lower, "contains --", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Fish, countMatches(lower, "argparse ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Fish, countMatches(lower, "math ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Fish, countMatches(lower, "path ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Fish, countMatches(lower, "count $", 8) * 2);
	if (fishFunctionLines + fishSetLines + fishSwitchLines + countMatches(lower, "argparse ", 8) > 0)
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Fish)] += std::min(4, fishFunctionLines + fishSetLines + fishSwitchLines + countMatches(lower, "argparse ", 8));

	addClassificationScore(scores, MRSyntaxLanguage::Systemd, countLinePrefixMatches(lower, "[unit]", 8) * 6);
	addClassificationScore(scores, MRSyntaxLanguage::Systemd, countLinePrefixMatches(lower, "[service]", 8) * 6);
	addClassificationScore(scores, MRSyntaxLanguage::Systemd, countLinePrefixMatches(lower, "[install]", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Systemd, countLinePrefixMatches(lower, "execstart=", 12) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Systemd, countLinePrefixMatches(lower, "wantedby=", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Systemd, countLinePrefixMatches(lower, "description=", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Systemd, countLinePrefixMatches(lower, "after=", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Systemd, countLinePrefixMatches(lower, "requires=", 8) * 2);
	if (systemdSectionLines + countLinePrefixMatches(lower, "execstart=", 12) > 0)
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Systemd)] += 3;

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

	addClassificationScore(scores, MRSyntaxLanguage::Swift, countLinePrefixMatches(lower, "import ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countLinePrefixMatches(lower, "func ", 12) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countLinePrefixMatches(lower, "guard ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countLinePrefixMatches(lower, "let ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countLinePrefixMatches(lower, "var ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countLinePrefixMatches(lower, "extension ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countLinePrefixMatches(lower, "protocol ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countLinePrefixMatches(lower, "struct ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countLinePrefixMatches(lower, "enum ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countMatches(lower, "@available", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countMatches(lower, "@mainactor", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Swift, countMatches(lower, "nil", 16));
	if (ext == ".swift") strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Swift)] += 2;
	if (countLinePrefixMatches(lower, "func ", 12) + countLinePrefixMatches(lower, "extension ", 8) + countLinePrefixMatches(lower, "protocol ", 8) > 0)
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Swift)] += 2;

	addClassificationScore(scores, MRSyntaxLanguage::Rust, rustFunctionLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Rust, rustConcreteBlockLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Rust, rustStructLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Rust, countLinePrefixMatches(lower, "use ", 12) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Rust, countLinePrefixMatches(lower, "pub use ", 12) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Rust, countLinePrefixMatches(lower, "mod ", 12) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Rust, countMatches(lower, "::", 16) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Rust, countMatches(lower, "match ", 12) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Rust, countMatches(lower, "let ", 16));
	addClassificationScore(scores, MRSyntaxLanguage::Rust, countMatches(lower, "mut ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::Rust, countMatches(lower, "#[", 12) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Rust, countMatches(lower, "->", 16));
	if (ext == ".rs") strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Rust)] += 2;
	if (rustFunctionLines + rustConcreteBlockLines + rustStructLines > 0)
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Rust)] += std::min(4, rustFunctionLines + rustConcreteBlockLines + rustStructLines);

	addClassificationScore(scores, MRSyntaxLanguage::Go, goPackageLines * 6);
	addClassificationScore(scores, MRSyntaxLanguage::Go, goImportLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Go, goFunctionLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Go, goTypeLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Go, countMatches(lower, ":=", 16) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Go, countMatches(lower, "defer ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Go, countMatches(lower, "go ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Go, countMatches(lower, "select ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Go, countMatches(lower, "chan ", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Go, countMatches(lower, "interface{}", 8) * 2);
	if (ext == ".go") strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Go)] += 2;
	if (goPackageLines + goImportLines + goFunctionLines + goTypeLines > 0)
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Go)] += std::min(4, goPackageLines + goImportLines + goFunctionLines + goTypeLines);

	addClassificationScore(scores, MRSyntaxLanguage::Kotlin, countLinePrefixMatches(lower, "package ", 8) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Kotlin, countLinePrefixMatches(lower, "import ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::Kotlin, kotlinFunctionLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Kotlin, kotlinTypeLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Kotlin, kotlinValueLines * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Kotlin, countMatches(lower, "data class ", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Kotlin, countMatches(lower, "companion object", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Kotlin, countMatches(lower, "when ", 12) * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Kotlin, countMatches(lower, "?:", 12) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Kotlin, countMatches(lower, "!!", 12) * 2);
	if (ext == ".kt" || ext == ".kts") strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Kotlin)] += 2;
	if (kotlinFunctionLines + kotlinTypeLines > 0)
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Kotlin)] += std::min(4, kotlinFunctionLines + kotlinTypeLines);

	addClassificationScore(scores, MRSyntaxLanguage::CSharp, csharpUsingLines * 2);
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, csharpNamespaceLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, csharpTypeLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, countMatches(lower, "public ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, countMatches(lower, "private ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, countMatches(lower, "protected ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, countMatches(lower, "internal ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, countMatches(lower, "async ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, countMatches(lower, "await ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, countMatches(lower, "string ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, countMatches(lower, "var ", 12));
	addClassificationScore(scores, MRSyntaxLanguage::CSharp, countMatches(lower, "=>", 12));
	if (ext == ".cs" || ext == ".csx" || ext == ".cake") strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::CSharp)] += 2;
	if (csharpTypeLines + csharpNamespaceLines > 0)
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::CSharp)] += std::min(4, csharpTypeLines + csharpNamespaceLines);

	addClassificationScore(scores, MRSyntaxLanguage::Basic, basicOptionLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Basic, basicProcedureLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Basic, basicBlockLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Basic, countLinePrefixMatches(lower, "rem ", 8) * 3);
	if (basicOptionLines + basicProcedureLines + basicBlockLines > 0)
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Basic)] += std::min(4, basicOptionLines + basicProcedureLines + basicBlockLines);

	addClassificationScore(scores, MRSyntaxLanguage::MRMAC, countMatches(lower, "$macro", 8) * 5);
	addClassificationScore(scores, MRSyntaxLanguage::MRMAC, countMatches(lower, "$macro_file", 8) * 5);
	addClassificationScore(scores, MRSyntaxLanguage::MRMAC, countMatches(lower, "def_tick", 8) * 5);
	addClassificationScore(scores, MRSyntaxLanguage::MRMAC, countMatches(lower, "def_int", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::MRMAC, countMatches(lower, "def_str", 8) * 4);
	if (containsText(lower, "$macro") || containsText(lower, "def_tick")) strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::MRMAC)] += 3;

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

	addClassificationScore(scores, MRSyntaxLanguage::Latex, latexDocumentLines * 8);
	addClassificationScore(scores, MRSyntaxLanguage::Latex, latexSectionLines * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Latex, latexEnvironmentLines * 3);
	addClassificationScore(scores, MRSyntaxLanguage::Latex, countMatches(lower, "\\usepackage", 8) * 4);
	addClassificationScore(scores, MRSyntaxLanguage::Latex, countMatches(lower, "\\label{", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Latex, countMatches(lower, "\\ref{", 8) * 2);
	addClassificationScore(scores, MRSyntaxLanguage::Latex, countMatches(lower, "\\cite", 8) * 2);
	if (latexDocumentLines + latexSectionLines + latexEnvironmentLines > 0)
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Latex)] += std::min(4, latexDocumentLines + latexSectionLines + latexEnvironmentLines);

	if (jsonKeyLines >= 3) {
		addClassificationScore(scores, MRSyntaxLanguage::Json, 6);
		++strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Json)];
		addClassificationScore(scores, MRSyntaxLanguage::JavaScript, -4);
		addClassificationScore(scores, MRSyntaxLanguage::Python, -4);
		addClassificationScore(scores, MRSyntaxLanguage::Zsh, -3);
		addClassificationScore(scores, MRSyntaxLanguage::Perl, -5);
	}
	if (xmlTagLines >= 3) {
		addClassificationScore(scores, MRSyntaxLanguage::Xml, 6);
		++strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Xml)];
		addClassificationScore(scores, MRSyntaxLanguage::Json, -4);
		addClassificationScore(scores, MRSyntaxLanguage::Markdown, -4);
		addClassificationScore(scores, MRSyntaxLanguage::Systemd, -3);
	}
	if (markdownStructureLines >= 3) {
		addClassificationScore(scores, MRSyntaxLanguage::Markdown, 4);
		++strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Markdown)];
		addClassificationScore(scores, MRSyntaxLanguage::C, -2);
		addClassificationScore(scores, MRSyntaxLanguage::Cpp, -2);
		addClassificationScore(scores, MRSyntaxLanguage::JavaScript, -2);
		addClassificationScore(scores, MRSyntaxLanguage::Json, -2);
	}
	if (systemdSectionLines >= 2) {
		addClassificationScore(scores, MRSyntaxLanguage::Systemd, 8);
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Systemd)] += 2;
		addClassificationScore(scores, MRSyntaxLanguage::Markdown, -8);
		addClassificationScore(scores, MRSyntaxLanguage::Json, -3);
		addClassificationScore(scores, MRSyntaxLanguage::Bash, -2);
		addClassificationScore(scores, MRSyntaxLanguage::Zsh, -2);
	}
	if (makeTargetLines >= 2 && makeRecipeLines >= 1) {
		addClassificationScore(scores, MRSyntaxLanguage::Make, 8);
		strongSignals[syntaxLanguageIndex(MRSyntaxLanguage::Make)] += 2;
		addClassificationScore(scores, MRSyntaxLanguage::Markdown, -3);
		addClassificationScore(scores, MRSyntaxLanguage::Json, -3);
		addClassificationScore(scores, MRSyntaxLanguage::Python, -2);
	}
	if (ext == ".c" || ext == ".h" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".hh" || ext == ".hpp" || ext == ".hxx")
		addClassificationScore(scores, MRSyntaxLanguage::Make, -10);
	if (ext == ".js" || ext == ".jsx" || ext == ".mjs" || ext == ".cjs" || ext == ".ts" || ext == ".tsx")
		addClassificationScore(scores, MRSyntaxLanguage::Make, -10);
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
	if (ext == ".swift") {
		addClassificationScore(scores, MRSyntaxLanguage::JavaScript, -8);
		addClassificationScore(scores, MRSyntaxLanguage::Cpp, -8);
		addClassificationScore(scores, MRSyntaxLanguage::Python, -6);
	}
	if (ext == ".rs") {
		addClassificationScore(scores, MRSyntaxLanguage::Cpp, -8);
		addClassificationScore(scores, MRSyntaxLanguage::JavaScript, -6);
		addClassificationScore(scores, MRSyntaxLanguage::Swift, -4);
	}
	if (ext == ".go") {
		addClassificationScore(scores, MRSyntaxLanguage::Cpp, -6);
		addClassificationScore(scores, MRSyntaxLanguage::JavaScript, -6);
		addClassificationScore(scores, MRSyntaxLanguage::Rust, -4);
		addClassificationScore(scores, MRSyntaxLanguage::Swift, -4);
	}
	if (ext == ".kt" || ext == ".kts") {
		addClassificationScore(scores, MRSyntaxLanguage::JavaScript, -6);
		addClassificationScore(scores, MRSyntaxLanguage::Swift, -4);
		addClassificationScore(scores, MRSyntaxLanguage::Go, -4);
		addClassificationScore(scores, MRSyntaxLanguage::CSharp, -4);
	}
	if (ext == ".cs" || ext == ".csx" || ext == ".cake") {
		addClassificationScore(scores, MRSyntaxLanguage::JavaScript, -6);
		addClassificationScore(scores, MRSyntaxLanguage::Cpp, -4);
		addClassificationScore(scores, MRSyntaxLanguage::Kotlin, -4);
		addClassificationScore(scores, MRSyntaxLanguage::Swift, -4);
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
