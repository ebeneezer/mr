#include "MROutlineFoldProducer.hpp"

#include "../app/utils/MRStringUtils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

namespace {

struct MROutlineLeadDescriptor {
	MRSyntaxLanguage language;
	const char *upperPrefix;
	MROutlineKind kind;
	MROutlineConfidence confidence;
};

static const MROutlineLeadDescriptor kOutlineLeadDescriptors[] = {
	{MRSyntaxLanguage::Python, "DEF ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Python, "ASYNC DEF ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Python, "CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::JavaScript, "FUNCTION ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::JavaScript, "ASYNC FUNCTION ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::JavaScript, "EXPORT FUNCTION ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::JavaScript, "EXPORT DEFAULT FUNCTION ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::JavaScript, "EXPORT ASYNC FUNCTION ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::JavaScript, "EXPORT DEFAULT ASYNC FUNCTION ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::JavaScript, "CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::JavaScript, "EXPORT CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::JavaScript, "EXPORT DEFAULT CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Bash, "FUNCTION ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Zsh, "FUNCTION ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Fish, "FUNCTION ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Perl, "SUB ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Swift, "FUNC ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Swift, "INIT", mrokMethod, mrocStructural},
	{MRSyntaxLanguage::Swift, "DEINIT", mrokMethod, mrocStructural},
	{MRSyntaxLanguage::Swift, "CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Swift, "STRUCT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Swift, "ENUM ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Swift, "ACTOR ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Swift, "PROTOCOL ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Swift, "EXTENSION ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Rust, "FN ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Rust, "PUB FN ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Rust, "ASYNC FN ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Rust, "PUB ASYNC FN ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Rust, "STRUCT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Rust, "ENUM ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Rust, "TRAIT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Rust, "IMPL ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Go, "FUNC(", mrokMethod, mrocStructural},
	{MRSyntaxLanguage::Go, "FUNC ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Go, "TYPE ", mrokClass, mrocHeuristic},
	{MRSyntaxLanguage::Kotlin, "FUN ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Kotlin, "CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Kotlin, "DATA CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Kotlin, "SEALED CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Kotlin, "ENUM CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Kotlin, "INTERFACE ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Kotlin, "OBJECT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Kotlin, "COMPANION OBJECT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::CSharp, "CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::CSharp, "INTERFACE ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::CSharp, "RECORD ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::CSharp, "RECORD CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::CSharp, "RECORD STRUCT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::CSharp, "STRUCT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::CSharp, "ENUM ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::CSharp, "NAMESPACE ", mrokNamespace, mrocStructural},
	{MRSyntaxLanguage::Pascal, "PROCEDURE ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Pascal, "FUNCTION ", mrokFunction, mrocStructural},
	{MRSyntaxLanguage::Pascal, "CONSTRUCTOR ", mrokMethod, mrocStructural},
	{MRSyntaxLanguage::Pascal, "DESTRUCTOR ", mrokMethod, mrocStructural},
	{MRSyntaxLanguage::Pascal, "CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Pascal, "OBJECT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Pascal, "RECORD ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Cpp, "NAMESPACE ", mrokNamespace, mrocStructural},
	{MRSyntaxLanguage::Cpp, "CLASS ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Cpp, "STRUCT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Cpp, "UNION ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Cpp, "ENUM ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Cpp, "TYPEDEF STRUCT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Cpp, "TYPEDEF UNION ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::Cpp, "TYPEDEF ENUM ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::C, "STRUCT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::C, "UNION ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::C, "ENUM ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::C, "TYPEDEF STRUCT ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::C, "TYPEDEF UNION ", mrokClass, mrocStructural},
	{MRSyntaxLanguage::C, "TYPEDEF ENUM ", mrokClass, mrocStructural}
};

struct MROutlineAcceptedKey {
	std::size_t line = 0;
	MROutlineKind kind = mrokUnknown;
	std::string name;
};

bool outlineIsIndentWhitespace(char ch) noexcept {
	return ch == ' ' || ch == '\t';
}

std::string_view outlineTrimView(std::string_view text) noexcept {
	std::size_t start = 0;
	std::size_t end = text.size();

	while (start < end && outlineIsIndentWhitespace(text[start]))
		++start;
	while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
		--end;
	return text.substr(start, end - start);
}

std::size_t outlineLeadingIndentBytes(std::string_view text) noexcept {
	std::size_t index = 0;
	while (index < text.size() && outlineIsIndentWhitespace(text[index]))
		++index;
	return index;
}

bool outlineContainsUpperToken(std::string_view text, std::string_view token) noexcept {
	std::size_t pos = text.find(token);
	while (pos != std::string_view::npos) {
		const bool leftOk = pos == 0 || !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) != 0 || text[pos - 1] == '_');
		const std::size_t end = pos + token.size();
		const bool rightOk = end >= text.size() || !(std::isalnum(static_cast<unsigned char>(text[end])) != 0 || text[end] == '_');
		if (leftOk && rightOk) return true;
		pos = text.find(token, pos + token.size());
	}
	return false;
}

std::string_view outlineSkipLeadingClosersAndSpace(std::string_view trimmed) noexcept {
	std::size_t index = 0;
	while (index < trimmed.size() && (trimmed[index] == '}' || trimmed[index] == ']' || trimmed[index] == ')' || trimmed[index] == ' ' || trimmed[index] == '\t'))
		++index;
	return trimmed.substr(index);
}

bool outlineShellFunctionHeadLine(std::string_view trimmed, std::string_view upperLine) noexcept {
	if (upperLine.starts_with("FUNCTION ")) return true;
	const std::size_t paren = trimmed.find("()");
	if (paren == std::string_view::npos) return false;
	std::string_view rest = outlineTrimView(trimmed.substr(paren + 2));
	return rest.empty() || rest.front() == '{';
}

std::string_view outlineNormalizeRustStructuralLeadText(std::string_view text) noexcept {
	if (text.starts_with("PUB ")) text.remove_prefix(4);
	if (text.starts_with("ASYNC ")) text.remove_prefix(6);
	return text;
}

bool outlineParseXmlLeadingOpenTag(std::string_view trimmed, std::string_view &tagName) noexcept {
	if (trimmed.size() < 3 || trimmed.front() != '<') return false;
	if (trimmed.starts_with("</") || trimmed.starts_with("<!--") || trimmed.starts_with("<?") || trimmed.starts_with("<!")) return false;
	std::size_t index = 1;
	while (index < trimmed.size()) {
		const unsigned char ch = static_cast<unsigned char>(trimmed[index]);
		if (std::isalnum(ch) == 0 && trimmed[index] != '_' && trimmed[index] != '-' && trimmed[index] != ':') break;
		++index;
	}
	if (index <= 1) return false;
	tagName = trimmed.substr(1, index - 1);
	return true;
}

bool outlineLatexCommandNameChar(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalpha(uch) != 0;
}

std::size_t outlineLatexSkipBalanced(std::string_view text, std::size_t start, char open, char close) noexcept {
	int depth = 0;
	for (std::size_t index = start; index < text.size(); ++index) {
		if (text[index] == '\\' && index + 1 < text.size()) {
			++index;
			continue;
		}
		if (text[index] == open) ++depth;
		if (text[index] == close) {
			--depth;
			if (depth == 0) return index + 1;
		}
	}
	return std::string_view::npos;
}

std::string outlineLatexSectionTitle(std::string_view trimmed) {
	if (trimmed.empty() || trimmed.front() != '\\') return std::string();
	std::size_t index = 1;
	while (index < trimmed.size() && outlineLatexCommandNameChar(trimmed[index]))
		++index;
	while (index < trimmed.size() && (trimmed[index] == '*' || std::isspace(static_cast<unsigned char>(trimmed[index])) != 0))
		++index;
	while (index < trimmed.size() && trimmed[index] == '[') {
		const std::size_t next = outlineLatexSkipBalanced(trimmed, index, '[', ']');
		if (next == std::string_view::npos) return std::string();
		index = next;
		while (index < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[index])) != 0)
			++index;
	}
	if (index >= trimmed.size() || trimmed[index] != '{') return std::string();
	const std::size_t end = outlineLatexSkipBalanced(trimmed, index, '{', '}');
	if (end == std::string_view::npos || end <= index + 2) return std::string();
	return std::string(outlineTrimView(trimmed.substr(index + 1, end - index - 2)));
}

bool outlineKindIsFunctionLike(MROutlineKind kind) noexcept {
	switch (kind) {
		case mrokMethod:
		case mrokFunction:
		case mrokMacro:
		case mrokTarget:
			return true;
		default:
			return false;
	}
}

bool outlineNameChar(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '_' || ch == '$' || ch == '.' || ch == ':' || ch == '~';
}

std::string outlineReadNameAfter(std::string_view text, std::size_t start) {
	while (start < text.size() && (std::isspace(static_cast<unsigned char>(text[start])) != 0 || text[start] == '*' || text[start] == '&'))
		++start;
	const std::size_t begin = start;
	while (start < text.size() && outlineNameChar(text[start]))
		++start;
	if (start <= begin) return std::string();
	return std::string(text.substr(begin, start - begin));
}

std::string outlineReadNameBefore(std::string_view text, std::size_t end) {
	while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
		--end;
	std::size_t begin = end;
	while (begin > 0 && outlineNameChar(text[begin - 1]))
		--begin;
	if (begin >= end) return std::string();
	return std::string(text.substr(begin, end - begin));
}

std::string outlineNameAfterUpperToken(std::string_view text, std::string_view upperLine, const char *token) {
	const std::size_t tokenLength = std::strlen(token);
	const std::size_t tokenOffset = upperLine.find(token);
	if (tokenOffset == std::string_view::npos) return std::string();
	return outlineReadNameAfter(text, tokenOffset + tokenLength);
}

std::string outlineNameAfterUpperTokenAndGeneric(std::string_view text, std::string_view upperLine, const char *token) {
	const std::size_t tokenLength = std::strlen(token);
	std::size_t offset = upperLine.find(token);
	int genericDepth = 0;

	if (offset == std::string_view::npos) return std::string();
	offset += tokenLength;
	while (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset])) != 0)
		++offset;
	if (offset < text.size() && text[offset] == '<') {
		for (; offset < text.size(); ++offset) {
			if (text[offset] == '<') ++genericDepth;
			else if (text[offset] == '>') {
				--genericDepth;
				if (genericDepth == 0) {
					++offset;
					break;
				}
			}
		}
	}
	return outlineReadNameAfter(text, offset);
}

void trimTrailingOutlineNamePunctuation(std::string &name) {
	while (!name.empty() && (name.back() == ':' || name.back() == '{'))
		name.pop_back();
}

bool outlineLineStartsWithDescriptor(std::string_view upperLine, const char *prefix) noexcept {
	const std::size_t length = std::strlen(prefix);
	if (upperLine.size() < length) return false;
	if (upperLine.compare(0, length, prefix) != 0) return false;
	if (length == 0 || prefix[length - 1] == ' ') return true;
	if (!(std::isalnum(static_cast<unsigned char>(prefix[length - 1])) != 0 || prefix[length - 1] == '_')) return true;
	if (upperLine.size() == length) return true;
	const unsigned char next = static_cast<unsigned char>(upperLine[length]);
	return std::isalnum(next) == 0 && upperLine[length] != '_';
}

bool cLikeOutlineMethodCandidate(std::string_view trimmed) noexcept {
	const std::size_t openParen = trimmed.find('(');
	if (openParen == std::string_view::npos) return false;
	return trimmed.rfind("::", openParen) != std::string_view::npos;
}

bool cLikeOutlineFunctionCandidate(std::string_view trimmed) noexcept {
	static const char *kRejectedPrefixes[] = {"IF ", "FOR ", "WHILE ", "SWITCH ", "CATCH", "ELSE", "DO", "TRY", "RETURN ", "SIZEOF", "STATIC_ASSERT", "CLASS ", "STRUCT ", "ENUM ", "UNION ", "NAMESPACE "};
	const std::string_view normalizedTrimmed = outlineTrimView(outlineSkipLeadingClosersAndSpace(trimmed));
	const std::string normalizedUpperText = upperAscii(std::string(normalizedTrimmed));
	const std::string_view normalizedUpper(normalizedUpperText.data(), normalizedUpperText.size());
	const std::size_t openParen = normalizedTrimmed.find('(');
	const std::size_t closeParen = normalizedTrimmed.rfind(')');
	const std::size_t semicolon = normalizedTrimmed.find(';');

	if (openParen == std::string_view::npos || closeParen == std::string_view::npos || closeParen < openParen) return false;
	if (normalizedTrimmed.starts_with("(")) return false;
	if (!normalizedTrimmed.empty() && normalizedTrimmed.front() == '[') return false;
	if (normalizedTrimmed.find("= [", 0) < openParen || normalizedTrimmed.find("=[", 0) < openParen) return false;
	if (semicolon != std::string_view::npos && semicolon > closeParen) return false;
	for (const char *prefix : kRejectedPrefixes)
		if (outlineLineStartsWithDescriptor(normalizedUpper, prefix)) return false;
	return normalizedTrimmed.find('{', closeParen) != std::string_view::npos || normalizedTrimmed.find("=>", closeParen) != std::string_view::npos || normalizedTrimmed.ends_with(")");
}

bool rustOutlineFunctionLead(std::string_view upperLine) noexcept {
	const std::string_view normalizedUpper = outlineNormalizeRustStructuralLeadText(upperLine);
	const std::size_t fnPos = normalizedUpper.find("FN ");
	if (fnPos == std::string_view::npos) return false;
	if (fnPos > 0) {
		const unsigned char before = static_cast<unsigned char>(normalizedUpper[fnPos - 1]);
		if (std::isalnum(before) != 0 || normalizedUpper[fnPos - 1] == '_') return false;
	}
	return true;
}

bool javascriptClassFieldFunctionLead(std::string_view trimmed, std::string_view upperLine) noexcept {
	if (upperLine.starts_with("IF ") || upperLine.starts_with("FOR ") || upperLine.starts_with("WHILE ") || upperLine.starts_with("SWITCH ")) return false;
	return trimmed.find("=>") != std::string_view::npos || upperLine.find("FUNCTION") != std::string_view::npos;
}

bool parseJsonOutlineKey(std::string_view trimmed, std::string &name) {
	if (trimmed.size() < 4 || trimmed.front() != '"') return false;
	bool escaped = false;
	std::size_t closeQuote = std::string_view::npos;
	for (std::size_t i = 1; i < trimmed.size(); ++i) {
		if (escaped) {
			escaped = false;
			continue;
		}
		if (trimmed[i] == '\\') {
			escaped = true;
			continue;
		}
		if (trimmed[i] == '"') {
			closeQuote = i;
			break;
		}
	}
	if (closeQuote == std::string_view::npos) return false;
	std::string_view rest = outlineTrimView(trimmed.substr(closeQuote + 1));
	if (rest.empty() || rest.front() != ':') return false;
	rest = outlineTrimView(rest.substr(1));
	if (rest.empty() || (rest.front() != '{' && rest.front() != '[')) return false;
	name.assign(trimmed.substr(1, closeQuote - 1));
	return !name.empty();
}

bool parseYamlOutlineKey(std::string_view trimmed, std::string &name) {
	if (trimmed.empty() || trimmed.front() == '#' || trimmed.starts_with("- ")) return false;
	const std::size_t colon = trimmed.find(':');
	if (colon == std::string_view::npos || colon == 0) return false;
	std::string_view key = outlineTrimView(trimmed.substr(0, colon));
	if (key.empty() || key.find_first_of("{}[]") != std::string_view::npos) return false;
	if ((key.front() == '"' && key.back() == '"') || (key.front() == '\'' && key.back() == '\'')) key = key.substr(1, key.size() - 2);
	if (key.empty()) return false;
	name.assign(key);
	return true;
}

bool parseXmlOutlineTag(std::string_view trimmed, std::string &name) {
	std::string_view tagName;
	if (!outlineParseXmlLeadingOpenTag(trimmed, tagName)) return false;
	if (tagName.empty()) return false;
	name.assign(tagName);
	return true;
}

bool classifyTokenBasedOutlineLead(MRSyntaxLanguage language, std::string_view upperLine, MROutlineKind &kind, MROutlineConfidence &confidence) noexcept {
	switch (language) {
		case MRSyntaxLanguage::C:
			if (outlineContainsUpperToken(upperLine, "STRUCT") || outlineContainsUpperToken(upperLine, "UNION") || outlineContainsUpperToken(upperLine, "ENUM")) {
				kind = mrokClass;
				confidence = mrocStructural;
				return true;
			}
			break;
		case MRSyntaxLanguage::Cpp:
			if (outlineContainsUpperToken(upperLine, "NAMESPACE")) {
				kind = mrokNamespace;
				confidence = mrocStructural;
				return true;
			}
			if (outlineContainsUpperToken(upperLine, "CLASS") || outlineContainsUpperToken(upperLine, "STRUCT") || outlineContainsUpperToken(upperLine, "UNION") || outlineContainsUpperToken(upperLine, "ENUM")) {
				kind = mrokClass;
				confidence = mrocStructural;
				return true;
			}
			break;
		case MRSyntaxLanguage::Swift:
			if (outlineContainsUpperToken(upperLine, "FUNC")) {
				kind = mrokFunction;
				confidence = mrocStructural;
				return true;
			}
			if (outlineContainsUpperToken(upperLine, "CLASS") || outlineContainsUpperToken(upperLine, "STRUCT") || outlineContainsUpperToken(upperLine, "ENUM") || outlineContainsUpperToken(upperLine, "ACTOR") ||
			    outlineContainsUpperToken(upperLine, "PROTOCOL") || outlineContainsUpperToken(upperLine, "EXTENSION")) {
				kind = mrokClass;
				confidence = mrocStructural;
				return true;
			}
			break;
		case MRSyntaxLanguage::Rust:
			if (outlineContainsUpperToken(upperLine, "FN")) {
				kind = mrokFunction;
				confidence = mrocStructural;
				return true;
			}
			if (outlineContainsUpperToken(upperLine, "MOD")) {
				kind = mrokModule;
				confidence = mrocStructural;
				return true;
			}
			if (outlineContainsUpperToken(upperLine, "STRUCT") || outlineContainsUpperToken(upperLine, "ENUM") || outlineContainsUpperToken(upperLine, "TRAIT") || outlineContainsUpperToken(upperLine, "IMPL")) {
				kind = mrokClass;
				confidence = mrocStructural;
				return true;
			}
			break;
		case MRSyntaxLanguage::Kotlin:
			if (outlineContainsUpperToken(upperLine, "FUN")) {
				kind = mrokFunction;
				confidence = mrocStructural;
				return true;
			}
			if (outlineContainsUpperToken(upperLine, "CLASS") || outlineContainsUpperToken(upperLine, "INTERFACE") || outlineContainsUpperToken(upperLine, "OBJECT") || outlineContainsUpperToken(upperLine, "ENUM")) {
				kind = mrokClass;
				confidence = mrocStructural;
				return true;
			}
			break;
		case MRSyntaxLanguage::CSharp:
			if (upperLine.starts_with("WHERE ")) return false;
			if (outlineContainsUpperToken(upperLine, "NAMESPACE")) {
				kind = mrokNamespace;
				confidence = mrocStructural;
				return true;
			}
			if (outlineContainsUpperToken(upperLine, "CLASS") || outlineContainsUpperToken(upperLine, "INTERFACE") || outlineContainsUpperToken(upperLine, "RECORD") || outlineContainsUpperToken(upperLine, "STRUCT") ||
			    outlineContainsUpperToken(upperLine, "ENUM")) {
				kind = mrokClass;
				confidence = mrocStructural;
				return true;
			}
			break;
		default:
			break;
	}
	return false;
}

void classifyFoldOutlineNode(MRSyntaxLanguage language, const MRFoldSpan &span, std::string_view trimmed, MROutlineKind &kind, MROutlineConfidence &confidence) noexcept {
	const std::string upper = upperAscii(std::string(trimmed));
	const std::string_view upperLine(upper.data(), upper.size());

	kind = mrokBlock;
	confidence = mrocStructural;
	switch (span.sourceKind) {
		case MRFoldSourceKind::Section:
			kind = mrokSection;
			return;
		case MRFoldSourceKind::Macro:
			kind = mrokMacro;
			return;
		case MRFoldSourceKind::Target:
			kind = mrokTarget;
			return;
		case MRFoldSourceKind::Directive:
			kind = mrokModule;
			return;
		default:
			break;
	}

	for (const MROutlineLeadDescriptor &descriptor : kOutlineLeadDescriptors) {
		if (descriptor.language == language && outlineLineStartsWithDescriptor(upperLine, descriptor.upperPrefix)) {
			kind = descriptor.kind;
			confidence = descriptor.confidence;
			return;
		}
	}
	if ((language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::CSharp || language == MRSyntaxLanguage::JavaScript) && cLikeOutlineFunctionCandidate(trimmed)) {
		kind = cLikeOutlineMethodCandidate(trimmed) ? mrokMethod : mrokFunction;
		confidence = mrocHeuristic;
		return;
	}
	if (classifyTokenBasedOutlineLead(language, upperLine, kind, confidence)) return;
	if ((language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) && outlineShellFunctionHeadLine(trimmed, upperLine)) {
		kind = mrokFunction;
		confidence = mrocStructural;
		return;
	}
	if (language == MRSyntaxLanguage::Rust && rustOutlineFunctionLead(upperLine)) {
		kind = mrokFunction;
		confidence = mrocStructural;
		return;
	}
	if (language == MRSyntaxLanguage::Json) {
		std::string keyName;
		if (parseJsonOutlineKey(trimmed, keyName)) {
			kind = mrokSection;
			confidence = mrocStructural;
		}
		return;
	}
	if (language == MRSyntaxLanguage::Yaml) {
		std::string keyName;
		if (parseYamlOutlineKey(trimmed, keyName)) {
			kind = mrokSection;
			confidence = mrocHeuristic;
		}
		return;
	}
	if (language == MRSyntaxLanguage::Xml) {
		std::string tagName;
		if (parseXmlOutlineTag(trimmed, tagName)) {
			kind = mrokSection;
			confidence = mrocStructural;
		}
		return;
	}
	if (language == MRSyntaxLanguage::JavaScript && javascriptClassFieldFunctionLead(trimmed, upperLine) && cLikeOutlineFunctionCandidate(trimmed)) {
		kind = mrokFunction;
		confidence = mrocHeuristic;
	}
}

bool outlineKindVisibleInView(MROutlineView view, MROutlineKind kind) noexcept {
	if (view == mrovFunctions) return outlineKindIsFunctionLike(kind);
	return kind != mrokBlock;
}

bool cLikeTypedefTypeLead(std::string_view upperLine) noexcept {
	return outlineLineStartsWithDescriptor(upperLine, "TYPEDEF STRUCT ") || outlineLineStartsWithDescriptor(upperLine, "TYPEDEF UNION ") ||
	       outlineLineStartsWithDescriptor(upperLine, "TYPEDEF ENUM ");
}

std::string cLikeTypedefAliasName(std::string_view startUpperLine, std::string_view endTrimmed) {
	std::string_view afterBrace;
	std::size_t end = 0;
	std::size_t begin = 0;

	if (!cLikeTypedefTypeLead(startUpperLine)) return std::string();
	const std::size_t closeBrace = endTrimmed.find('}');
	if (closeBrace == std::string_view::npos) return std::string();
	afterBrace = outlineTrimView(endTrimmed.substr(closeBrace + 1));
	if (afterBrace.empty()) return std::string();
	end = afterBrace.find_first_of(",;[=");
	if (end == std::string_view::npos) end = afterBrace.size();
	while (end > 0 && std::isspace(static_cast<unsigned char>(afterBrace[end - 1])) != 0)
		--end;
	begin = end;
	while (begin > 0) {
		const unsigned char ch = static_cast<unsigned char>(afterBrace[begin - 1]);
		if (std::isalnum(ch) == 0 && afterBrace[begin - 1] != '_') break;
		--begin;
	}
	if (begin == end) return std::string();
	return std::string(afterBrace.substr(begin, end - begin));
}

std::string shellOutlineFunctionName(std::string_view trimmed, std::string_view upperLine) {
	if (upperLine.starts_with("FUNCTION ")) return outlineReadNameAfter(trimmed, 9);
	const std::size_t paren = trimmed.find("()");
	if (paren != std::string_view::npos) return outlineReadNameBefore(trimmed, paren);
	return std::string();
}

std::string javascriptOutlineFunctionName(std::string_view trimmed, std::string_view upperLine) {
	std::size_t offset = std::string_view::npos;
	if (upperLine.starts_with("EXPORT DEFAULT FUNCTION ")) offset = 24;
	else if (upperLine.starts_with("EXPORT ASYNC FUNCTION "))
		offset = 22;
	else if (upperLine.starts_with("EXPORT FUNCTION "))
		offset = 16;
	else if (upperLine.starts_with("ASYNC FUNCTION "))
		offset = 15;
	else if (upperLine.starts_with("FUNCTION "))
		offset = 9;
	if (offset != std::string_view::npos) {
		const std::string name = outlineReadNameAfter(trimmed, offset);
		if (!name.empty()) return name;
	}
	const std::size_t arrow = trimmed.find("=>");
	if (arrow != std::string_view::npos) {
		const std::size_t eq = trimmed.rfind('=', arrow);
		if (eq != std::string_view::npos) {
			const std::string name = outlineReadNameBefore(trimmed, eq);
			if (!name.empty()) return name;
		}
		const std::size_t colon = trimmed.rfind(':', arrow);
		if (colon != std::string_view::npos) {
			const std::string name = outlineReadNameBefore(trimmed, colon);
			if (!name.empty()) return name;
		}
	}
	const std::size_t openParen = trimmed.find('(');
	if (openParen != std::string_view::npos) return outlineReadNameBefore(trimmed, openParen);
	return std::string();
}

std::string cLikeOutlineFunctionName(std::string_view trimmed) {
	const std::size_t openParen = trimmed.find('(');
	if (openParen == std::string_view::npos) return std::string();
	const std::string_view prefix = trimmed.substr(0, openParen);
	const std::size_t operatorOffset = prefix.rfind("operator");
	if (operatorOffset != std::string_view::npos) return std::string(outlineTrimView(prefix.substr(operatorOffset)));
	return outlineReadNameBefore(trimmed, openParen);
}

std::string cLikeTypeKeyword(std::string_view upperLine) {
	if (upperLine.find("STRUCT") != std::string_view::npos) return "struct";
	if (upperLine.find("UNION") != std::string_view::npos) return "union";
	if (upperLine.find("ENUM") != std::string_view::npos) return "enum";
	if (upperLine.find("CLASS") != std::string_view::npos) return "class";
	return "type";
}

std::string outlineFunctionDisplayName(MRSyntaxLanguage language, std::string_view trimmed, std::string_view upperLine, MROutlineKind kind) {
	std::string name;
	const std::string_view normalizedTrimmed = outlineTrimView(outlineSkipLeadingClosersAndSpace(trimmed));
	const std::string normalizedUpperText = upperAscii(std::string(normalizedTrimmed));
	const std::string_view normalizedUpper(normalizedUpperText.data(), normalizedUpperText.size());

	if (language == MRSyntaxLanguage::Python) {
		if (normalizedUpper.starts_with("ASYNC DEF ")) return outlineReadNameAfter(normalizedTrimmed, 10);
		if (normalizedUpper.starts_with("DEF ")) return outlineReadNameAfter(normalizedTrimmed, 4);
	}
	if (language == MRSyntaxLanguage::JavaScript) return javascriptOutlineFunctionName(normalizedTrimmed, normalizedUpper);
	if (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) return shellOutlineFunctionName(normalizedTrimmed, normalizedUpper);
	if (language == MRSyntaxLanguage::Fish && normalizedUpper.starts_with("FUNCTION ")) return outlineReadNameAfter(normalizedTrimmed, 9);
	if (language == MRSyntaxLanguage::Perl && normalizedUpper.starts_with("SUB ")) return outlineReadNameAfter(normalizedTrimmed, 4);
	if (language == MRSyntaxLanguage::Swift) {
		name = outlineNameAfterUpperToken(normalizedTrimmed, normalizedUpper, "FUNC ");
		if (!name.empty()) return name;
		if (normalizedUpper.starts_with("INIT")) return "init";
		if (normalizedUpper.starts_with("DEINIT")) return "deinit";
	}
	if (language == MRSyntaxLanguage::Rust) return outlineNameAfterUpperToken(normalizedTrimmed, normalizedUpper, "FN ");
	if (language == MRSyntaxLanguage::Go) {
		if (normalizedUpper.starts_with("FUNC(") || normalizedUpper.starts_with("FUNC (")) {
			const std::size_t receiverEnd = normalizedTrimmed.find(')');
			if (receiverEnd != std::string_view::npos) return outlineReadNameAfter(normalizedTrimmed, receiverEnd + 1);
		}
		if (normalizedUpper.starts_with("FUNC ")) return outlineReadNameAfter(normalizedTrimmed, 5);
	}
	if (language == MRSyntaxLanguage::Kotlin) {
		name = outlineNameAfterUpperTokenAndGeneric(normalizedTrimmed, normalizedUpper, "FUN ");
		if (!name.empty()) return name;
	}
	if (language == MRSyntaxLanguage::Pascal) {
		static const char *kPascalTokens[] = {"PROCEDURE ", "FUNCTION ", "CONSTRUCTOR ", "DESTRUCTOR "};
		for (const char *token : kPascalTokens) {
			name = outlineNameAfterUpperToken(normalizedTrimmed, normalizedUpper, token);
			if (!name.empty()) return name;
		}
	}
	if (kind == mrokTarget && language == MRSyntaxLanguage::Make) {
		const std::size_t colon = normalizedTrimmed.find(':');
		if (colon != std::string_view::npos && colon > 0) return std::string(outlineTrimView(normalizedTrimmed.substr(0, colon)));
	}
	if (language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::CSharp || language == MRSyntaxLanguage::JavaScript) {
		name = cLikeOutlineFunctionName(normalizedTrimmed);
		if (!name.empty()) return name;
	}
	if (kind == mrokMacro && language == MRSyntaxLanguage::MRMAC) return outlineReadNameAfter(normalizedTrimmed, 7);
	return std::string();
}

std::string outlineTypeDisplayName(MRSyntaxLanguage language, std::string_view trimmed, std::string_view upperLine, std::string_view endTrimmed) {
	std::string name;

	if ((language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp) && cLikeTypedefTypeLead(upperLine)) {
		const std::string alias = cLikeTypedefAliasName(upperLine, endTrimmed);
		if (!alias.empty()) return cLikeTypeKeyword(upperLine) + " " + alias;
		return std::string();
	}
	static const char *kTypeTokens[] = {"EXPORT DEFAULT ASYNC CLASS ", "EXPORT DEFAULT CLASS ", "EXPORT CLASS ", "CLASS ", "STRUCT ", "UNION ", "ENUM ", "ACTOR ", "PROTOCOL ", "EXTENSION ",
	                                    "TRAIT ", "IMPL ", "TYPE ", "INTERFACE ", "OBJECT ", "COMPANION OBJECT ", "DATA CLASS ", "SEALED CLASS ", "ENUM CLASS ", "RECORD STRUCT ",
	                                    "RECORD CLASS ", "RECORD ", "NAMESPACE "};
	for (const char *token : kTypeTokens) {
		name = outlineNameAfterUpperToken(trimmed, upperLine, token);
		if (!name.empty()) {
			trimTrailingOutlineNamePunctuation(name);
			return name;
		}
	}
	if (language == MRSyntaxLanguage::Pascal) {
		name = outlineNameAfterUpperToken(trimmed, upperLine, "OBJECT ");
		if (!name.empty()) return name;
		name = outlineNameAfterUpperToken(trimmed, upperLine, "RECORD ");
		if (!name.empty()) return name;
	}
	return std::string();
}

std::string outlineModuleDisplayName(MRSyntaxLanguage language, std::string_view trimmed, std::string_view upperLine) {
	if (language == MRSyntaxLanguage::Rust) {
		const std::string name = outlineNameAfterUpperToken(trimmed, upperLine, "MOD ");
		if (!name.empty()) return name;
	}
	if (language == MRSyntaxLanguage::Latex && trimmed.starts_with("\\begin")) {
		std::size_t openBrace = trimmed.find('{');
		if (openBrace != std::string_view::npos) {
			std::size_t closeBrace = trimmed.find('}', openBrace + 1);
			if (closeBrace != std::string_view::npos && closeBrace > openBrace + 1) return std::string(outlineTrimView(trimmed.substr(openBrace + 1, closeBrace - openBrace - 1)));
		}
	}
	return std::string();
}

std::string outlineSectionDisplayName(MRSyntaxLanguage language, std::string_view trimmed) {
	std::string name;

	if (language == MRSyntaxLanguage::Json && parseJsonOutlineKey(trimmed, name)) return name;
	if (language == MRSyntaxLanguage::Yaml && parseYamlOutlineKey(trimmed, name)) return name;
	if (language == MRSyntaxLanguage::Xml && parseXmlOutlineTag(trimmed, name)) return name;
	if (language == MRSyntaxLanguage::Make) {
		const std::size_t colon = trimmed.find(':');
		if (colon != std::string_view::npos && colon > 0) return std::string(outlineTrimView(trimmed.substr(0, colon)));
	}
	if (language == MRSyntaxLanguage::Systemd) {
		std::string_view section = trimmed;
		while (!section.empty() && (section.front() == '[' || section.front() == ']'))
			section.remove_prefix(1);
		while (!section.empty() && section.back() == ']')
			section.remove_suffix(1);
		section = outlineTrimView(section);
		if (!section.empty()) return std::string(section);
	}
	if (language == MRSyntaxLanguage::Markdown) {
		std::string_view heading = trimmed;
		while (!heading.empty() && heading.front() == '#')
			heading.remove_prefix(1);
		heading = outlineTrimView(heading);
		if (!heading.empty()) return std::string(heading);
	}
	if (language == MRSyntaxLanguage::Latex) {
		if (trimmed == "\\appendix") return "Appendix";
		name = outlineLatexSectionTitle(trimmed);
		if (!name.empty()) return name;
	}
	return std::string();
}

std::string outlineDisplayName(MRSyntaxLanguage language, std::string_view trimmed, std::string_view endTrimmed, MROutlineKind kind) {
	static constexpr std::size_t kMaxOutlineNameBytes = 160;
	const std::string upper = upperAscii(std::string(trimmed));
	const std::string_view upperLine(upper.data(), upper.size());
	std::string_view name = trimmed;
	std::string ownedName;

	if (kind == mrokSection) {
		ownedName = outlineSectionDisplayName(language, trimmed);
		if (!ownedName.empty()) name = ownedName;
	}
	if (kind == mrokClass || kind == mrokNamespace) {
		ownedName = outlineTypeDisplayName(language, trimmed, upperLine, endTrimmed);
		if (!ownedName.empty())
			name = ownedName;
		else if ((language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp) && cLikeTypedefTypeLead(upperLine))
			name = std::string_view();
	}
	if (kind == mrokModule) {
		ownedName = outlineModuleDisplayName(language, trimmed, upperLine);
		if (!ownedName.empty()) name = ownedName;
	}
	if (kind == mrokFunction || kind == mrokMethod || kind == mrokMacro || kind == mrokTarget) {
		ownedName = outlineFunctionDisplayName(language, trimmed, upperLine, kind);
		if (!ownedName.empty()) name = ownedName;
	}
	if (name.size() > kMaxOutlineNameBytes) name = name.substr(0, kMaxOutlineNameBytes);
	if (name.empty()) return std::string("(unnamed)");
	return std::string(name);
}

void appendOutlineString(MROutlineSnapshot &snapshot, const std::string &text, std::uint32_t &offset, std::uint16_t &length) {
	offset = static_cast<std::uint32_t>(snapshot.textPool.size());
	length = static_cast<std::uint16_t>(std::min<std::size_t>(text.size(), std::numeric_limits<std::uint16_t>::max()));
	snapshot.textPool.append(text.data(), length);
}

bool acceptedKeyExists(const std::vector<MROutlineAcceptedKey> &keys, std::size_t line, MROutlineKind kind, const std::string &name) {
	for (const MROutlineAcceptedKey &key : keys)
		if (key.line == line && key.kind == kind && key.name == name) return true;
	return false;
}

const char *outlineKindName(MROutlineKind kind) noexcept {
	switch (kind) {
		case mrokModule:
			return "module";
		case mrokNamespace:
			return "namespace";
		case mrokClass:
			return "type";
		case mrokMethod:
			return "method";
		case mrokFunction:
			return "function";
		case mrokSection:
			return "section";
		case mrokMacro:
			return "macro";
		case mrokTarget:
			return "target";
		default:
			return "symbol";
	}
}

} // namespace

std::string mrBuildOutlineTrainingAsciiForFoldSpans(const std::vector<std::string> &lineTexts, const std::vector<MRFoldSpan> &spans, MRSyntaxLanguage language) {
	std::string output;
	std::size_t structureCount = 0;
	std::size_t functionsCount = 0;
	std::vector<MRFoldSpan> orderedSpans = spans;

	std::stable_sort(orderedSpans.begin(), orderedSpans.end(), [](const MRFoldSpan &lhs, const MRFoldSpan &rhs) {
		if (lhs.startLine != rhs.startLine) return lhs.startLine < rhs.startLine;
		return lhs.level < rhs.level;
	});
	auto appendSection = [&](const char *title, MROutlineView view, std::size_t &count) {
		std::string rows;
		std::vector<MROutlineAcceptedKey> acceptedKeys;
		for (const MRFoldSpan &span : orderedSpans) {
			if (span.startLine >= lineTexts.size()) continue;
			const std::string_view trimmed = outlineTrimView(lineTexts[span.startLine]);
			std::string_view endTrimmed;
			MROutlineKind kind = mrokBlock;
			MROutlineConfidence confidence = mrocStructural;
			classifyFoldOutlineNode(language, span, trimmed, kind, confidence);
			if (!outlineKindVisibleInView(view, kind)) continue;
			if (span.endLine < lineTexts.size()) endTrimmed = outlineTrimView(lineTexts[span.endLine]);
			const std::string name = outlineDisplayName(language, trimmed, endTrimmed, kind);
			if (name == "(unnamed)") continue;
			if (acceptedKeyExists(acceptedKeys, span.startLine, kind, name)) continue;
			acceptedKeys.push_back({span.startLine, kind, name});
			++count;
			char prefix[96];
			std::snprintf(prefix, sizeof(prefix), "%6zu  %-9s  %-10s  ", span.startLine + 1, outlineKindName(kind), confidence == mrocHeuristic ? "heuristic" : "structural");
			rows.append(prefix);
			rows.append(name);
			rows.push_back('\n');
		}
		output.append(title);
		output.append(" [");
		output.append(std::to_string(count));
		output.append("]\n");
		output.append(rows);
	};

	output.append("STRUCTURE/FUNCTION OUTLINE\n");
	appendSection("STRUCTURE", mrovStructure, structureCount);
	appendSection("FUNCTIONS", mrovFunctions, functionsCount);
	return output;
}

bool mrBuildFoldOutlineSnapshotFromFoldState(MRSyntaxLanguage language, std::size_t documentId, std::size_t version, std::size_t topLine, std::size_t bottomLine, bool complete,
                                             const std::vector<std::string> &lineTexts, const std::vector<MRFoldSpan> &spans, const MRTextBufferModel::ReadSnapshot &readSnapshot,
                                             const MROutlineRequest &request, MROutlineSnapshot &snapshot) {
	std::vector<std::uint32_t> levelLast;
	std::vector<std::uint32_t> lastChild;
	std::vector<MROutlineAcceptedKey> acceptedKeys;
	std::vector<MRFoldSpan> orderedSpans = spans;

	snapshot.documentId = documentId;
	snapshot.version = version;
	snapshot.topLine = topLine;
	snapshot.bottomLine = bottomLine;
	snapshot.complete = complete;
	snapshot.nodes.clear();
	snapshot.textPool.clear();
	if (bottomLine <= topLine) return false;
	if (!request.allowPartial && !complete) return false;

	std::stable_sort(orderedSpans.begin(), orderedSpans.end(), [](const MRFoldSpan &lhs, const MRFoldSpan &rhs) {
		if (lhs.startLine != rhs.startLine) return lhs.startLine < rhs.startLine;
		return lhs.level < rhs.level;
	});
	for (const MRFoldSpan &span : orderedSpans) {
		if (span.startLine < topLine || span.startLine >= bottomLine) continue;
		const std::size_t lineTextIndex = span.startLine - topLine;
		if (lineTextIndex >= lineTexts.size()) continue;

		const std::string &lineText = lineTexts[lineTextIndex];
		const std::string_view trimmed = outlineTrimView(lineText);
		std::string_view endTrimmed;
		MROutlineKind kind = mrokBlock;
		MROutlineConfidence confidence = mrocStructural;
		classifyFoldOutlineNode(language, span, trimmed, kind, confidence);
		if (!outlineKindVisibleInView(request.view, kind)) continue;
		if (span.endLine >= topLine && span.endLine < bottomLine) {
			const std::size_t endLineTextIndex = span.endLine - topLine;
			if (endLineTextIndex < lineTexts.size()) endTrimmed = outlineTrimView(lineTexts[endLineTextIndex]);
		}

		const std::string name = outlineDisplayName(language, trimmed, endTrimmed, kind);
		if (name == "(unnamed)") continue;
		if (acceptedKeyExists(acceptedKeys, span.startLine, kind, name)) continue;
		acceptedKeys.push_back({span.startLine, kind, name});
		const std::size_t startOffset = readSnapshot.lineStartByIndex(span.startLine);
		const std::size_t endOffset = readSnapshot.lineEnd(readSnapshot.lineStartByIndex(span.endLine));
		const std::size_t nameColumn = std::min(outlineLeadingIndentBytes(lineText), lineText.size());
		const std::size_t nameOffset = readSnapshot.clampOffset(startOffset + nameColumn);
		MROutlineNode node;

		node.kind = kind;
		node.source = mrosFold;
		node.confidence = confidence;
		node.range.start.line = span.startLine;
		node.range.start.column = 0;
		node.range.start.offset = startOffset;
		node.range.end.line = span.endLine;
		node.range.end.column = 0;
		node.range.end.offset = endOffset;
		node.selectionRange.start.line = span.startLine;
		node.selectionRange.start.column = nameColumn;
		node.selectionRange.start.offset = nameOffset;
		node.selectionRange.end = node.selectionRange.start;
		appendOutlineString(snapshot, name, node.nameOffset, node.nameLength);

		const std::uint32_t nodeIndex = static_cast<std::uint32_t>(snapshot.nodes.size());
		const std::size_t level = span.level;
		if (levelLast.size() <= level) levelLast.resize(level + 1, MROutlineNode::npos);
		for (std::size_t parentLevel = level; parentLevel > 0; --parentLevel) {
			const std::uint32_t candidateIndex = levelLast[parentLevel - 1];
			if (candidateIndex == MROutlineNode::npos) continue;
			if (candidateIndex >= snapshot.nodes.size()) continue;
			const MROutlineNode &candidate = snapshot.nodes[candidateIndex];
			if (candidate.range.end.line < span.startLine) continue;
			node.parent = candidateIndex;
			break;
		}
		if (node.kind == mrokFunction && node.parent != MROutlineNode::npos && snapshot.nodes[node.parent].kind == mrokClass) node.kind = mrokMethod;
		if (node.parent != MROutlineNode::npos) {
			MROutlineNode &parent = snapshot.nodes[node.parent];
			if (parent.firstChild == MROutlineNode::npos)
				parent.firstChild = nodeIndex;
			else if (lastChild[node.parent] != MROutlineNode::npos)
				snapshot.nodes[lastChild[node.parent]].nextSibling = nodeIndex;
			lastChild[node.parent] = nodeIndex;
		}
		for (std::size_t clearLevel = level; clearLevel < levelLast.size(); ++clearLevel)
			levelLast[clearLevel] = MROutlineNode::npos;
		levelLast[level] = nodeIndex;
		snapshot.nodes.push_back(node);
		lastChild.push_back(MROutlineNode::npos);
	}
	return true;
}
