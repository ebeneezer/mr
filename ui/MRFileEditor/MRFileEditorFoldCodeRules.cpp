#include "MRFileEditorFoldAnalysis.hpp"
#include "../MRSyntaxBasic.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>

namespace mr::editor::fold {

bool isIndentWhitespace(char ch) noexcept {
	return ch == ' ' || ch == '\t';
}

bool isStatefulSyntaxLanguage(MRSyntaxLanguage language) noexcept {
	return language == MRSyntaxLanguage::MRMAC || language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::JavaScript || language == MRSyntaxLanguage::Python ||
	       language == MRSyntaxLanguage::Markdown || language == MRSyntaxLanguage::Latex || language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh || language == MRSyntaxLanguage::Fish || language == MRSyntaxLanguage::Perl || language == MRSyntaxLanguage::Swift || language == MRSyntaxLanguage::Rust ||
	       language == MRSyntaxLanguage::Go || language == MRSyntaxLanguage::Kotlin || language == MRSyntaxLanguage::CSharp || language == MRSyntaxLanguage::Pascal || language == MRSyntaxLanguage::Basic || language == MRSyntaxLanguage::Xml;
}

std::string_view trimView(std::string_view text) noexcept {
	std::size_t start = 0;
	std::size_t end = text.size();

	while (start < end && isIndentWhitespace(text[start]))
		++start;
	while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
		--end;
	return text.substr(start, end - start);
}

std::size_t lastSignificantByte(std::string_view text) noexcept {
	std::size_t end = text.size();

	while (end > 0) {
		const char ch = text[end - 1];
		if (ch != ' ' && ch != '\t' && ch != '\r') return end - 1;
		--end;
	}
	return std::string_view::npos;
}

bool containsUpperToken(std::string_view text, std::string_view token) noexcept {
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

std::string_view skipLeadingClosersAndSpace(std::string_view trimmed) noexcept {
	std::size_t index = 0;
	while (index < trimmed.size() && (trimmed[index] == '}' || trimmed[index] == ']' || trimmed[index] == ')' || trimmed[index] == ' ' || trimmed[index] == '\t'))
		++index;
	return trimmed.substr(index);
}

bool startsWithCloser(std::string_view trimmed) noexcept {
	return !trimmed.empty() && (trimmed.front() == '}' || trimmed.front() == ']' || trimmed.front() == ')');
}

std::size_t leadingIndentBytes(std::string_view text) noexcept {
	std::size_t index = 0;
	while (index < text.size() && isIndentWhitespace(text[index]))
		++index;
	return index;
}

bool isPythonIndentLead(std::string_view upperLine) noexcept {
	return upperLine == "ELSE:" || upperLine == "TRY:" || upperLine == "FINALLY:" || upperLine.starts_with("IF ") || upperLine.starts_with("ELIF ") || upperLine.starts_with("FOR ") || upperLine.starts_with("WHILE ") ||
	       upperLine.starts_with("WITH ") || upperLine.starts_with("MATCH ") || upperLine.starts_with("CASE ") || upperLine.starts_with("EXCEPT ") || upperLine.starts_with("DEF ") || upperLine.starts_with("CLASS ") ||
	       upperLine.starts_with("EXCEPT* ") || upperLine.starts_with("ASYNC DEF ") || upperLine.starts_with("ASYNC FOR ") || upperLine.starts_with("ASYNC WITH ");
}

bool isPythonDedentLead(std::string_view upperLine) noexcept {
	return upperLine == "ELSE:" || upperLine == "FINALLY:" || upperLine == "EXCEPT:" || upperLine.starts_with("ELIF ") || upperLine.starts_with("CASE ") || upperLine.starts_with("EXCEPT ") ||
	       upperLine.starts_with("EXCEPT* ");
}

bool isShellIndentLead(std::string_view trimmed, std::string_view upperLine) noexcept {
	const std::size_t last = lastSignificantByte(trimmed);
	if (last != std::string_view::npos && trimmed[last] == '{') return true;
	return upperLine == "THEN" || upperLine.ends_with(" THEN") || upperLine == "DO" || upperLine.ends_with(" DO") || upperLine == "ELSE" || upperLine.starts_with("ELIF ") ||
	       (upperLine.starts_with("CASE ") && upperLine.ends_with(" IN")) || (upperLine.starts_with("SELECT ") && upperLine.ends_with(" DO")) || (upperLine.starts_with("UNTIL ") && upperLine.ends_with(" DO"));
}

bool isShellDedentLead(std::string_view trimmed, std::string_view upperLine) noexcept {
	return startsWithCloser(trimmed) || upperLine == "FI" || upperLine == "DONE" || upperLine == "ESAC" || upperLine == "ELSE" || upperLine.starts_with("ELIF ");
}

bool isShellFunctionHeadLine(std::string_view trimmed, std::string_view upperLine) noexcept {
	const std::string_view normalized = trimView(trimmed);
	if (normalized.empty()) return false;
	if (upperLine.starts_with("FUNCTION ")) return true;
	const std::size_t openParen = normalized.find('(');
	const std::size_t closeParen = normalized.find(')');
	if (openParen == std::string_view::npos || closeParen == std::string_view::npos || closeParen < openParen) return false;
	if (normalized.find('{') != std::string_view::npos) return true;
	if (closeParen != normalized.size() - 1) return false;
	std::size_t nameEnd = openParen;
	while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(normalized[nameEnd - 1])) != 0)
		--nameEnd;
	if (nameEnd == 0) return false;
	for (std::size_t i = 0; i < nameEnd; ++i) {
		const unsigned char ch = static_cast<unsigned char>(normalized[i]);
		if (!(std::isalnum(ch) != 0 || normalized[i] == '_')) return false;
	}
	for (std::size_t i = openParen + 1; i < closeParen; ++i)
		if (!std::isspace(static_cast<unsigned char>(normalized[i]))) return false;
	return true;
}

bool isFishFunctionLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("FUNCTION ");
}

bool isFishIfLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("IF ");
}

bool isFishElseIfLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("ELSE IF ");
}

bool isFishElseLead(std::string_view upperLine) noexcept {
	return upperLine == "ELSE";
}

bool isFishWhileLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("WHILE ");
}

bool isFishForLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("FOR ") && (upperLine.find(" IN ") != std::string_view::npos || upperLine.ends_with(" IN"));
}

bool isFishSwitchLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("SWITCH ");
}

bool isFishCaseLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("CASE ");
}

bool isFishBeginLead(std::string_view upperLine) noexcept {
	return upperLine == "BEGIN";
}

bool isFishEndLead(std::string_view upperLine) noexcept {
	return upperLine == "END";
}

bool startsWithKeywordToken(std::string_view upperLine, std::string_view keyword) noexcept {
	if (!upperLine.starts_with(keyword)) return false;
	if (upperLine.size() == keyword.size()) return true;
	const unsigned char next = static_cast<unsigned char>(upperLine[keyword.size()]);
	return std::isspace(next) != 0 || next == '(' || next == '{' || next == ':';
}

bool isPascalIfLead(std::string_view upperLine) noexcept;
bool isPascalBeginLead(std::string_view upperLine) noexcept;
bool isPascalRecordLead(std::string_view upperLine) noexcept;
bool isPascalCaseLead(std::string_view upperLine) noexcept;
bool isPascalRepeatLead(std::string_view upperLine) noexcept;
bool isPascalElseLead(std::string_view upperLine) noexcept;
bool isPascalClassLead(std::string_view upperLine) noexcept;
bool isPascalObjectLead(std::string_view upperLine) noexcept;
bool isPascalTryLead(std::string_view upperLine) noexcept;
bool isPascalExceptLead(std::string_view upperLine) noexcept;
bool isPascalFinallyLead(std::string_view upperLine) noexcept;
bool isPascalUntilLead(std::string_view upperLine) noexcept;
bool isPascalEndLead(std::string_view upperLine) noexcept;
bool isPascalDoLead(std::string_view upperLine) noexcept;

int shellIndentBlockKind(std::string_view upperLine) noexcept {
	if (upperLine == "THEN" || upperLine.ends_with(" THEN") || upperLine == "ELSE" || upperLine.starts_with("ELIF ")) return kShellBlockConditional;
	if (upperLine == "DO" || upperLine.ends_with(" DO") || upperLine.starts_with("SELECT ") || upperLine.starts_with("UNTIL ")) return kShellBlockLoop;
	if (upperLine.starts_with("CASE ") && upperLine.ends_with(" IN")) return kShellBlockCase;
	return kShellBlockNone;
}

int fishIndentBlockKind(std::string_view upperLine) noexcept {
	if (isFishIfLead(upperLine) || isFishElseIfLead(upperLine) || isFishElseLead(upperLine)) return kFishBlockConditional;
	if (isFishWhileLead(upperLine) || isFishForLead(upperLine)) return kFishBlockLoop;
	if (isFishSwitchLead(upperLine)) return kFishBlockSwitch;
	if (isFishCaseLead(upperLine)) return kFishBlockCase;
	if (isFishFunctionLead(upperLine) || isFishBeginLead(upperLine)) return kFishBlockGeneric;
	return kFishBlockNone;
}

int pascalIndentBlockKind(std::string_view upperLine) noexcept {
	if (isPascalIfLead(upperLine) || isPascalElseLead(upperLine)) return kPascalBlockConditional;
	if (isPascalBeginLead(upperLine) || isPascalRecordLead(upperLine) || isPascalCaseLead(upperLine) || isPascalDoLead(upperLine) || isPascalClassLead(upperLine) || isPascalObjectLead(upperLine))
		return kPascalBlockGeneric;
	if (isPascalRepeatLead(upperLine)) return kPascalBlockRepeat;
	if (isPascalTryLead(upperLine) || isPascalExceptLead(upperLine) || isPascalFinallyLead(upperLine)) return kPascalBlockTry;
	return kPascalBlockNone;
}

int perlStructuredBlockKind(std::string_view trimmed, std::string_view upperLine) noexcept {
	const std::size_t last = lastSignificantByte(trimmed);
	if (last == std::string_view::npos || trimmed[last] != '{') return kPerlBlockNone;
	const std::string_view normalizedUpper = trimView(skipLeadingClosersAndSpace(upperLine));
	if (startsWithKeywordToken(normalizedUpper, "IF") || startsWithKeywordToken(normalizedUpper, "UNLESS") || startsWithKeywordToken(normalizedUpper, "ELSIF") || normalizedUpper == "ELSE {")
		return kPerlBlockConditional;
	if (startsWithKeywordToken(normalizedUpper, "SUB") || startsWithKeywordToken(normalizedUpper, "FOR") || startsWithKeywordToken(normalizedUpper, "FOREACH") ||
	    startsWithKeywordToken(normalizedUpper, "WHILE") || startsWithKeywordToken(normalizedUpper, "UNTIL") || startsWithKeywordToken(normalizedUpper, "GIVEN") ||
	    startsWithKeywordToken(normalizedUpper, "WHEN") || normalizedUpper == "CONTINUE {" || startsWithKeywordToken(normalizedUpper, "TRY") || startsWithKeywordToken(normalizedUpper, "CATCH") ||
	    startsWithKeywordToken(normalizedUpper, "FINALLY") || normalizedUpper == "BEGIN {" || normalizedUpper == "END {" || normalizedUpper == "INIT {" || normalizedUpper == "CHECK {" ||
	    normalizedUpper == "UNITCHECK {" || startsWithKeywordToken(normalizedUpper, "PACKAGE"))
		return kPerlBlockGeneric;
	return kPerlBlockNone;
}

bool isPerlStructuredBlockLead(std::string_view trimmed, std::string_view upperLine) noexcept {
	return perlStructuredBlockKind(trimmed, upperLine) != kPerlBlockNone;
}

bool isPerlSiblingLead(std::string_view upperLine) noexcept {
	const std::string_view normalizedUpper = trimView(skipLeadingClosersAndSpace(upperLine));
	return startsWithKeywordToken(normalizedUpper, "ELSIF") || normalizedUpper == "ELSE {" || normalizedUpper == "ELSE";
}

bool isJavaScriptSiblingLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("ELSE") || upperLine.starts_with("CATCH") || upperLine.starts_with("FINALLY");
}

bool isCLikeSiblingLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("ELSE") || upperLine.starts_with("CATCH");
}

bool isJavaScriptStructuralLeadLine(std::string_view upperLine) noexcept {
	return upperLine == "DO" || upperLine.starts_with("ELSE") || upperLine.starts_with("TRY") || upperLine.starts_with("CATCH") || upperLine.starts_with("FINALLY") ||
	       upperLine.starts_with("IF ") || upperLine.starts_with("FOR ") || upperLine.starts_with("WHILE ") || upperLine.starts_with("SWITCH ") || upperLine.starts_with("CLASS ") ||
	       upperLine.starts_with("FUNCTION ") || upperLine.starts_with("ASYNC FUNCTION ") || upperLine.starts_with("EXPORT FUNCTION ") || upperLine.starts_with("EXPORT DEFAULT FUNCTION ") ||
	       upperLine.starts_with("EXPORT ASYNC FUNCTION ") || upperLine.starts_with("EXPORT DEFAULT ASYNC FUNCTION ") || upperLine.starts_with("EXPORT CLASS ") ||
	       upperLine.starts_with("EXPORT DEFAULT CLASS ");
}

bool isJavaScriptArrowFunctionLeadLine(std::string_view trimmed) noexcept {
	const std::size_t arrow = trimmed.find("=>");
	if (arrow == std::string_view::npos) return false;
	const std::string_view afterArrow = trimView(trimmed.substr(arrow + 2));
	return afterArrow.empty() || afterArrow.starts_with("{");
}

bool isCLikeCommentLikeLine(std::string_view trimmed) noexcept {
	return trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*") || trimmed.starts_with("*/");
}

bool isCppLambdaLeadLine(std::string_view trimmed) noexcept {
	const std::string_view normalizedTrimmed = trimView(skipLeadingClosersAndSpace(trimmed));
	const std::size_t last = lastSignificantByte(normalizedTrimmed);
	if (last == std::string_view::npos || normalizedTrimmed[last] != '{') return false;
	const std::string_view beforeBrace = trimView(normalizedTrimmed.substr(0, last));
	const std::size_t captureOpen = beforeBrace.find('[');
	const std::size_t captureClose = beforeBrace.rfind(']');
	if (captureOpen == std::string_view::npos || captureClose == std::string_view::npos || captureClose < captureOpen) return false;
	const std::string_view afterCapture = trimView(beforeBrace.substr(captureClose + 1));
	if (afterCapture.empty()) return true;
	const std::string afterCaptureUpper = upperAscii(std::string(afterCapture));
	return afterCapture.front() == '(' || afterCapture.front() == '<' || startsWithKeywordToken(afterCaptureUpper, "MUTABLE") || startsWithKeywordToken(afterCaptureUpper, "NOEXCEPT") ||
	       startsWithKeywordToken(afterCaptureUpper, "REQUIRES") || afterCapture.starts_with("->");
}

bool isRustCommentLikeLine(std::string_view trimmed) noexcept;
bool isRustStructuralLeadLine(std::string_view upperLine) noexcept;
bool isGoStructuralLeadLine(std::string_view upperLine) noexcept;
bool isKotlinStructuralLeadLine(std::string_view upperLine) noexcept;
bool isCSharpStructuralLeadLine(std::string_view upperLine) noexcept;

bool isCLikeStructuralLeadLine(std::string_view trimmed, std::string_view upperLine, MRSyntaxLanguage language) noexcept {
	const std::string_view normalizedTrimmed = trimView(skipLeadingClosersAndSpace(trimmed));
	const std::string_view normalizedUpper = trimView(skipLeadingClosersAndSpace(upperLine));
	if (normalizedTrimmed.empty() || isCLikeCommentLikeLine(normalizedTrimmed) || normalizedTrimmed.front() == '#') return false;
	if (language == MRSyntaxLanguage::Cpp && isCppLambdaLeadLine(normalizedTrimmed)) return true;
	const std::size_t normalizedLast = lastSignificantByte(normalizedTrimmed);
	const bool cppRequiresBraceLead = language == MRSyntaxLanguage::Cpp && normalizedLast != std::string_view::npos && normalizedTrimmed[normalizedLast] == '{' &&
	                                  containsUpperToken(normalizedUpper, "REQUIRES");
	if (normalizedUpper == "DO" || normalizedUpper.starts_with("ELSE") || normalizedUpper.starts_with("SWITCH ") || normalizedUpper.starts_with("IF ") ||
	    normalizedUpper.starts_with("FOR ") || normalizedUpper.starts_with("WHILE ") || normalizedUpper.starts_with("TRY") || normalizedUpper.starts_with("CATCH"))
		return true;
	if (containsUpperToken(normalizedUpper, "STRUCT") || containsUpperToken(normalizedUpper, "UNION") || containsUpperToken(normalizedUpper, "ENUM") ||
	    containsUpperToken(normalizedUpper, "EXTERN"))
		return true;
	if (language == MRSyntaxLanguage::Cpp && (containsUpperToken(normalizedUpper, "CLASS") || containsUpperToken(normalizedUpper, "NAMESPACE")))
		return true;
	if (language == MRSyntaxLanguage::Rust && isRustStructuralLeadLine(normalizedUpper)) return true;
	if (language == MRSyntaxLanguage::Go && isGoStructuralLeadLine(normalizedUpper)) return true;
	if (language == MRSyntaxLanguage::Kotlin && isKotlinStructuralLeadLine(normalizedUpper)) return true;
	if (language == MRSyntaxLanguage::CSharp && isCSharpStructuralLeadLine(normalizedUpper)) return true;
	const std::size_t openParen = normalizedTrimmed.find('(');
	if (openParen == std::string_view::npos) return false;
	if (normalizedTrimmed.find(';') != std::string_view::npos && !cppRequiresBraceLead) return false;
	if (normalizedTrimmed.find('=') != std::string_view::npos && normalizedTrimmed.find("==") == std::string_view::npos) return false;
	return true;
}

bool isCppTemplatePrefixLead(std::string_view trimmed, std::string_view upperLine) noexcept {
	const std::string_view normalizedTrimmed = trimView(skipLeadingClosersAndSpace(trimmed));
	const std::string_view normalizedUpper = trimView(skipLeadingClosersAndSpace(upperLine));
	if (normalizedTrimmed.find(';') != std::string_view::npos) return false;
	return normalizedUpper == "TEMPLATE" || normalizedUpper.starts_with("TEMPLATE ") || normalizedUpper.starts_with("EXPORT TEMPLATE ") || normalizedUpper.starts_with("REQUIRES ");
}

bool isCLikeBraceFoldCandidateLine(std::string_view trimmed) noexcept {
	const std::string_view normalizedTrimmed = trimView(trimmed);
	if (normalizedTrimmed.empty() || isCLikeCommentLikeLine(normalizedTrimmed) || normalizedTrimmed.front() == '#') return false;
	const std::size_t last = lastSignificantByte(normalizedTrimmed);
	return last != std::string_view::npos && normalizedTrimmed[last] == '{';
}

bool isSwiftCommentLikeLine(std::string_view trimmed) noexcept {
	return trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*") || trimmed.starts_with("*/");
}

bool isSwiftLabelIdentifier(std::string_view text) noexcept {
	if (text.empty()) return false;
	const unsigned char first = static_cast<unsigned char>(text.front());
	if (!(std::isalpha(first) != 0 || text.front() == '_')) return false;
	for (std::size_t i = 1; i < text.size(); ++i) {
		const unsigned char ch = static_cast<unsigned char>(text[i]);
		if (!(std::isalnum(ch) != 0 || text[i] == '_')) return false;
	}
	return true;
}

std::string_view skipSwiftLeadingLabels(std::string_view text) noexcept {
	text = trimView(text);
	while (!text.empty()) {
		const std::size_t colon = text.find(':');
		if (colon == std::string_view::npos || colon == 0) break;
		const std::string_view candidate = trimView(text.substr(0, colon));
		if (!isSwiftLabelIdentifier(candidate)) break;
		const std::string_view rest = trimView(text.substr(colon + 1));
		if (rest.empty()) break;
		text = rest;
	}
	return text;
}

std::string_view normalizeSwiftStructuralLeadText(std::string_view text) noexcept {
	return skipSwiftLeadingLabels(trimView(skipLeadingClosersAndSpace(text)));
}

bool isSwiftStructuralLeadLine(std::string_view upperLine) noexcept {
	const std::string_view normalizedUpper = normalizeSwiftStructuralLeadText(upperLine);
	if (normalizedUpper.empty() || isSwiftCommentLikeLine(normalizedUpper)) return false;
	if (normalizedUpper == "DO" || normalizedUpper == "ELSE" || normalizedUpper.starts_with("ELSE ") || normalizedUpper.starts_with("CATCH") || normalizedUpper.starts_with("DEFER") ||
	    normalizedUpper.starts_with("DO ") || normalizedUpper.starts_with("IF ") || normalizedUpper.starts_with("GUARD ") || normalizedUpper.starts_with("FOR ") ||
	    normalizedUpper.starts_with("WHILE ") || normalizedUpper.starts_with("SWITCH ") || normalizedUpper.starts_with("REPEAT"))
		return true;
	return containsUpperToken(normalizedUpper, "FUNC") || containsUpperToken(normalizedUpper, "INIT") || containsUpperToken(normalizedUpper, "DEINIT") ||
	       containsUpperToken(normalizedUpper, "SUBSCRIPT") || containsUpperToken(normalizedUpper, "STRUCT") || containsUpperToken(normalizedUpper, "CLASS") ||
	       containsUpperToken(normalizedUpper, "ACTOR") || containsUpperToken(normalizedUpper, "ENUM") || containsUpperToken(normalizedUpper, "PROTOCOL") ||
	       containsUpperToken(normalizedUpper, "EXTENSION");
}

bool isSwiftAccessorLeadLine(std::string_view upperLine) noexcept {
	const std::string_view normalizedUpper = normalizeSwiftStructuralLeadText(upperLine);
	if (normalizedUpper.empty() || isSwiftCommentLikeLine(normalizedUpper)) return false;
	return normalizedUpper == "GET" || normalizedUpper.starts_with("GET ") || normalizedUpper == "SET" || normalizedUpper.starts_with("SET ") || normalizedUpper == "WILLSET" ||
	       normalizedUpper.starts_with("WILLSET ") || normalizedUpper == "DIDSET" || normalizedUpper.starts_with("DIDSET ");
}

bool isSwiftPropertyBlockLeadLine(std::string_view trimmedLine, std::string_view upperLine) noexcept {
	const std::string_view normalizedUpper = normalizeSwiftStructuralLeadText(upperLine);
	const std::string_view normalizedTrimmed = normalizeSwiftStructuralLeadText(trimmedLine);
	if (normalizedUpper.empty() || normalizedTrimmed.empty() || isSwiftCommentLikeLine(normalizedUpper)) return false;
	if (!containsUpperToken(normalizedUpper, "VAR") && !containsUpperToken(normalizedUpper, "LET")) return false;
	return normalizedTrimmed.find('{') != std::string_view::npos && (normalizedTrimmed.find(':') != std::string_view::npos || normalizedTrimmed.find('=') != std::string_view::npos);
}

bool isRustCommentLikeLine(std::string_view trimmed) noexcept {
	return trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*") || trimmed.starts_with("*/");
}

bool isGoCommentLikeLine(std::string_view trimmed) noexcept {
	return trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*") || trimmed.starts_with("*/");
}

bool isKotlinCommentLikeLine(std::string_view trimmed) noexcept {
	return trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*") || trimmed.starts_with("*/");
}

bool isCSharpCommentLikeLine(std::string_view trimmed) noexcept {
	return trimmed.starts_with("//") || trimmed.starts_with("/*") || trimmed.starts_with("*") || trimmed.starts_with("*/");
}

std::string_view skipRustLeadingLabels(std::string_view text) noexcept {
	text = trimView(skipLeadingClosersAndSpace(text));
	while (!text.empty() && text.front() == '\'') {
		const std::size_t colon = text.find(':');
		if (colon == std::string_view::npos || colon <= 1) break;
		const std::string_view candidate = trimView(text.substr(1, colon - 1));
		if (!isSwiftLabelIdentifier(candidate)) break;
		const std::string_view rest = trimView(text.substr(colon + 1));
		if (rest.empty()) break;
		text = rest;
	}
	return text;
}

std::string_view normalizeRustStructuralLeadText(std::string_view text) noexcept {
	return skipRustLeadingLabels(trimView(skipLeadingClosersAndSpace(text)));
}

bool isRustStructuralLeadLine(std::string_view upperLine) noexcept {
	const std::string_view normalizedUpper = normalizeRustStructuralLeadText(upperLine);
	if (normalizedUpper.empty() || isRustCommentLikeLine(normalizedUpper)) return false;
	if (normalizedUpper == "ELSE" || normalizedUpper.starts_with("ELSE ") || normalizedUpper.starts_with("IF ") || normalizedUpper.starts_with("FOR ") || normalizedUpper.starts_with("WHILE ") ||
	    normalizedUpper.starts_with("LOOP") || normalizedUpper.starts_with("MATCH ") || normalizedUpper.starts_with("UNSAFE"))
		return true;
	if (normalizedUpper.starts_with("MACRO_RULES!")) return true;
	return containsUpperToken(normalizedUpper, "FN") || containsUpperToken(normalizedUpper, "IMPL") || containsUpperToken(normalizedUpper, "TRAIT") || containsUpperToken(normalizedUpper, "STRUCT") ||
	       containsUpperToken(normalizedUpper, "ENUM") || containsUpperToken(normalizedUpper, "UNION") || containsUpperToken(normalizedUpper, "MOD") || containsUpperToken(normalizedUpper, "MACRO");
}

std::string_view normalizeGoStructuralLeadText(std::string_view text) noexcept {
	return skipSwiftLeadingLabels(trimView(skipLeadingClosersAndSpace(text)));
}

bool isGoStructuralLeadLine(std::string_view upperLine) noexcept {
	const std::string_view normalizedUpper = normalizeGoStructuralLeadText(upperLine);
	if (normalizedUpper.empty() || isGoCommentLikeLine(normalizedUpper)) return false;
	if (normalizedUpper == "ELSE" || normalizedUpper.starts_with("ELSE ") || normalizedUpper.starts_with("IF ") || normalizedUpper == "FOR" || normalizedUpper.starts_with("FOR ") ||
	    normalizedUpper == "SWITCH" || normalizedUpper.starts_with("SWITCH ") || normalizedUpper == "SELECT" || normalizedUpper.starts_with("SELECT "))
		return true;
	if (normalizedUpper.starts_with("FUNC ") || normalizedUpper.starts_with("FUNC(") || normalizedUpper.starts_with("GO FUNC(") || normalizedUpper.starts_with("DEFER FUNC(")) return true;
	if (normalizedUpper.starts_with("TYPE ") && (containsUpperToken(normalizedUpper, "STRUCT") || containsUpperToken(normalizedUpper, "INTERFACE"))) return true;
	return false;
}

std::string_view normalizeKotlinStructuralLeadText(std::string_view text) noexcept {
	return trimView(skipLeadingClosersAndSpace(text));
}

bool isKotlinStructuralLeadLine(std::string_view upperLine) noexcept {
	const std::string_view normalizedUpper = normalizeKotlinStructuralLeadText(upperLine);
	if (normalizedUpper.empty() || isKotlinCommentLikeLine(normalizedUpper)) return false;
	if (normalizedUpper == "ELSE" || normalizedUpper.starts_with("ELSE ") || normalizedUpper.starts_with("IF ") || normalizedUpper.starts_with("FOR ") || normalizedUpper.starts_with("WHILE ") ||
	    normalizedUpper.starts_with("WHEN ") || normalizedUpper.starts_with("TRY") || normalizedUpper.starts_with("CATCH") || normalizedUpper.starts_with("FINALLY") || normalizedUpper.starts_with("DO "))
		return true;
	return containsUpperToken(normalizedUpper, "FUN") || containsUpperToken(normalizedUpper, "CLASS") || containsUpperToken(normalizedUpper, "INTERFACE") ||
	       containsUpperToken(normalizedUpper, "OBJECT") || containsUpperToken(normalizedUpper, "ENUM") || containsUpperToken(normalizedUpper, "COMPANION");
}

std::string_view normalizeCSharpStructuralLeadText(std::string_view text) noexcept {
	return trimView(skipLeadingClosersAndSpace(text));
}

bool isCSharpStructuralLeadLine(std::string_view upperLine) noexcept {
	const std::string_view normalizedUpper = normalizeCSharpStructuralLeadText(upperLine);
	if (normalizedUpper.empty() || isCSharpCommentLikeLine(normalizedUpper) || normalizedUpper.starts_with("#")) return false;
	if (normalizedUpper == "ELSE" || normalizedUpper.starts_with("ELSE ") || normalizedUpper.starts_with("IF ") || normalizedUpper.starts_with("FOR ") || normalizedUpper.starts_with("FOREACH ") ||
	    normalizedUpper.starts_with("WHILE ") || normalizedUpper.starts_with("DO") || normalizedUpper.starts_with("SWITCH ") || normalizedUpper.starts_with("TRY") || normalizedUpper.starts_with("CATCH") ||
	    normalizedUpper.starts_with("FINALLY") || normalizedUpper.starts_with("LOCK ") || normalizedUpper.starts_with("USING ") || normalizedUpper.starts_with("NAMESPACE "))
		return true;
	return containsUpperToken(normalizedUpper, "CLASS") || containsUpperToken(normalizedUpper, "STRUCT") || containsUpperToken(normalizedUpper, "INTERFACE") ||
	       containsUpperToken(normalizedUpper, "ENUM") || containsUpperToken(normalizedUpper, "RECORD");
}

bool isPerlPodStart(std::string_view trimmed) noexcept {
	return trimmed.starts_with("=POD") || trimmed.starts_with("=HEAD") || trimmed.starts_with("=BEGIN") || trimmed.starts_with("=FOR") || trimmed.starts_with("=OVER");
}

bool isPerlPodEnd(std::string_view trimmed) noexcept {
	return trimmed.starts_with("=CUT");
}

bool isCLikeStructuralBraceLead(std::string_view trimmed, std::string_view upperLine, std::string_view previousTrimmed, std::string_view previousUpperLine, std::string_view previousPreviousTrimmed,
                                std::string_view previousPreviousUpperLine, MRSyntaxLanguage language) noexcept {
	const std::size_t last = lastSignificantByte(trimmed);
	if (last == std::string_view::npos || trimmed[last] != '{') return false;
	if (language == MRSyntaxLanguage::Swift && isSwiftCommentLikeLine(trimmed)) return false;
	if (language == MRSyntaxLanguage::Rust && isRustCommentLikeLine(trimmed)) return false;
	if (language == MRSyntaxLanguage::Go && isGoCommentLikeLine(trimmed)) return false;
	if (language == MRSyntaxLanguage::Kotlin && isKotlinCommentLikeLine(trimmed)) return false;
	if (language == MRSyntaxLanguage::CSharp && isCSharpCommentLikeLine(trimmed)) return false;
	const std::string_view beforeBrace = trimView(trimmed.substr(0, last));
	const std::string_view beforeBraceUpper = trimView(upperLine.substr(0, last));
	const std::string_view normalizedBeforeBrace = trimView(skipLeadingClosersAndSpace(beforeBrace));
	const std::string_view normalizedBeforeBraceUpper = language == MRSyntaxLanguage::Rust ? normalizeRustStructuralLeadText(beforeBraceUpper)
	                                                                                        : trimView(skipLeadingClosersAndSpace(beforeBraceUpper));
	if (!normalizedBeforeBrace.empty()) {
		if (normalizedBeforeBrace.back() == ')') return true;
		if ((language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp) && normalizedBeforeBrace.find(')') != std::string_view::npos) return true;
	} else if (trimmed == "{") {
		const std::size_t previousLast = lastSignificantByte(previousTrimmed);
		if (previousLast != std::string_view::npos) {
			if (previousTrimmed[previousLast] == ')') return true;
			if (language == MRSyntaxLanguage::JavaScript && previousTrimmed[previousLast] == '=') return true;
		}
	}
	if (language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp) {
		const std::string_view structuralUpper = normalizedBeforeBraceUpper.empty() ? previousUpperLine : normalizedBeforeBraceUpper;
		if (language == MRSyntaxLanguage::Cpp && beforeBrace.find(')') != std::string_view::npos && containsUpperToken(beforeBraceUpper, "REQUIRES")) return true;
		return structuralUpper == "DO" || structuralUpper.starts_with("ELSE") || structuralUpper.starts_with("SWITCH ") || structuralUpper.starts_with("IF ") ||
		       structuralUpper.starts_with("FOR ") || structuralUpper.starts_with("WHILE ") || containsUpperToken(structuralUpper, "STRUCT") || containsUpperToken(structuralUpper, "UNION") ||
		       containsUpperToken(structuralUpper, "ENUM") || containsUpperToken(structuralUpper, "CLASS") || containsUpperToken(structuralUpper, "NAMESPACE") ||
		       structuralUpper.starts_with("TRY") || structuralUpper.starts_with("CATCH") || containsUpperToken(structuralUpper, "EXTERN");
	}
	if (language == MRSyntaxLanguage::JavaScript) {
		const std::string_view structuralUpper = normalizedBeforeBraceUpper.empty() ? previousUpperLine : normalizedBeforeBraceUpper;
		const std::string_view structuralTrimmed = normalizedBeforeBrace.empty() ? previousTrimmed : normalizedBeforeBrace;
		if (isJavaScriptStructuralLeadLine(structuralUpper) || isJavaScriptArrowFunctionLeadLine(structuralTrimmed)) return true;
		if (beforeBrace.find(')') != std::string_view::npos || beforeBrace.find(']') != std::string_view::npos) {
			if (isJavaScriptStructuralLeadLine(previousUpperLine) || isJavaScriptStructuralLeadLine(previousPreviousUpperLine)) return true;
			if (isJavaScriptArrowFunctionLeadLine(previousTrimmed) || isJavaScriptArrowFunctionLeadLine(previousPreviousTrimmed)) return true;
		}
		if (trimmed == "{") {
			if (isJavaScriptStructuralLeadLine(previousPreviousUpperLine)) return true;
			if (isJavaScriptArrowFunctionLeadLine(previousTrimmed) || isJavaScriptArrowFunctionLeadLine(previousPreviousTrimmed)) return true;
		} else if (normalizedBeforeBraceUpper.empty()) {
			if (isJavaScriptStructuralLeadLine(previousPreviousUpperLine)) return true;
		}
		return false;
	}
	if (language == MRSyntaxLanguage::Swift) {
		const std::string_view structuralUpper = normalizedBeforeBraceUpper.empty() ? previousUpperLine : normalizedBeforeBraceUpper;
		if (isSwiftStructuralLeadLine(structuralUpper)) return true;
		if (beforeBrace.find(')') != std::string_view::npos || beforeBrace.find(']') != std::string_view::npos) {
			if (isSwiftStructuralLeadLine(previousUpperLine) || isSwiftStructuralLeadLine(previousPreviousUpperLine)) return true;
		}
		if (trimmed == "{") {
			if (isSwiftStructuralLeadLine(previousUpperLine) || isSwiftStructuralLeadLine(previousPreviousUpperLine)) return true;
		} else if (normalizedBeforeBraceUpper.empty()) {
			if (isSwiftStructuralLeadLine(previousPreviousUpperLine)) return true;
		}
		return false;
	}
	if (language == MRSyntaxLanguage::Rust) {
		const std::string_view structuralUpper = normalizedBeforeBraceUpper.empty() ? previousUpperLine : normalizedBeforeBraceUpper;
		if (isRustStructuralLeadLine(structuralUpper)) return true;
		if (beforeBrace.find(')') != std::string_view::npos || beforeBrace.find(']') != std::string_view::npos || beforeBrace.find('>') != std::string_view::npos) {
			if (isRustStructuralLeadLine(previousUpperLine) || isRustStructuralLeadLine(previousPreviousUpperLine)) return true;
		}
		if (trimmed == "{") {
			if (isRustStructuralLeadLine(previousUpperLine) || isRustStructuralLeadLine(previousPreviousUpperLine)) return true;
		} else if (normalizedBeforeBraceUpper.empty()) {
			if (isRustStructuralLeadLine(previousPreviousUpperLine)) return true;
		}
		return false;
	}
		if (language == MRSyntaxLanguage::Go) {
			const std::string_view structuralUpper = normalizedBeforeBraceUpper.empty() ? previousUpperLine : normalizedBeforeBraceUpper;
			if (isGoStructuralLeadLine(structuralUpper)) return true;
		if (beforeBrace.find(')') != std::string_view::npos || beforeBrace.find(']') != std::string_view::npos) {
			if (isGoStructuralLeadLine(previousUpperLine) || isGoStructuralLeadLine(previousPreviousUpperLine)) return true;
		}
		if (trimmed == "{") {
			if (isGoStructuralLeadLine(previousUpperLine) || isGoStructuralLeadLine(previousPreviousUpperLine)) return true;
		} else if (normalizedBeforeBraceUpper.empty()) {
			if (isGoStructuralLeadLine(previousPreviousUpperLine)) return true;
		}
			return false;
		}
		if (language == MRSyntaxLanguage::Kotlin) {
			const std::string_view structuralUpper = normalizedBeforeBraceUpper.empty() ? previousUpperLine : normalizedBeforeBraceUpper;
			if (isKotlinStructuralLeadLine(structuralUpper)) return true;
			if (beforeBrace.find(')') != std::string_view::npos || beforeBrace.find(']') != std::string_view::npos) {
				if (isKotlinStructuralLeadLine(previousUpperLine) || isKotlinStructuralLeadLine(previousPreviousUpperLine)) return true;
			}
			if (trimmed == "{") {
				if (isKotlinStructuralLeadLine(previousUpperLine) || isKotlinStructuralLeadLine(previousPreviousUpperLine)) return true;
			} else if (normalizedBeforeBraceUpper.empty()) {
				if (isKotlinStructuralLeadLine(previousPreviousUpperLine)) return true;
			}
			return false;
		}
		if (language == MRSyntaxLanguage::CSharp) {
			const std::string_view structuralUpper = normalizedBeforeBraceUpper.empty() ? previousUpperLine : normalizedBeforeBraceUpper;
			if (isCSharpStructuralLeadLine(structuralUpper)) return true;
			if (beforeBrace.find(')') != std::string_view::npos || beforeBrace.find(']') != std::string_view::npos) {
				if (isCSharpStructuralLeadLine(previousUpperLine) || isCSharpStructuralLeadLine(previousPreviousUpperLine)) return true;
			}
			if (trimmed == "{") {
				if (isCSharpStructuralLeadLine(previousUpperLine) || isCSharpStructuralLeadLine(previousPreviousUpperLine)) return true;
			} else if (normalizedBeforeBraceUpper.empty()) {
				if (isCSharpStructuralLeadLine(previousPreviousUpperLine)) return true;
			}
			return false;
		}
	return false;
}
} // namespace mr::editor::fold
