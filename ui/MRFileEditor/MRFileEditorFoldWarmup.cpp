#include "MRFileEditor.hpp"
#include "../../outline/MROutlineFoldProducer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string_view>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif

namespace {

static constexpr std::size_t kLargeFileWarmupTraceBytes = static_cast<std::size_t>(8) * 1024 * 1024;

bool isIndentWhitespace(char ch) noexcept {
	return ch == ' ' || ch == '\t';
}

bool isStatefulSyntaxLanguage(MRSyntaxLanguage language) noexcept {
	return language == MRSyntaxLanguage::MRMAC || language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::JavaScript || language == MRSyntaxLanguage::Python ||
	       language == MRSyntaxLanguage::Markdown || language == MRSyntaxLanguage::Latex || language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh || language == MRSyntaxLanguage::Fish || language == MRSyntaxLanguage::Perl || language == MRSyntaxLanguage::Swift || language == MRSyntaxLanguage::Rust ||
	       language == MRSyntaxLanguage::Go || language == MRSyntaxLanguage::Kotlin || language == MRSyntaxLanguage::CSharp || language == MRSyntaxLanguage::Pascal || language == MRSyntaxLanguage::Xml;
}

static constexpr auto kLargeFileViewportWarmupDebounce = std::chrono::milliseconds(180);
static constexpr auto kLargeFileMiniMapEditDebounce = std::chrono::milliseconds(500);

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

enum : int {
	kPerlBlockNone = 0,
	kPerlBlockConditional = 1,
	kPerlBlockGeneric = 2,
};

enum : int {
	kShellBlockNone = 0,
	kShellBlockConditional = 1,
	kShellBlockLoop = 2,
	kShellBlockCase = 3,
};

enum : int {
	kFishBlockNone = 0,
	kFishBlockConditional = 1,
	kFishBlockLoop = 2,
	kFishBlockSwitch = 3,
	kFishBlockCase = 4,
	kFishBlockGeneric = 5,
};

enum : int {
	kPascalBlockNone = 0,
	kPascalBlockGeneric = 1,
	kPascalBlockConditional = 2,
	kPascalBlockRepeat = 3,
	kPascalBlockTry = 4,
};

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

bool markdownContinuationColumn(std::string_view line, int &targetColumn) noexcept {
	const std::size_t indent = leadingIndentBytes(line);
	const std::string_view trimmed = trimView(line);
	if (trimmed.empty()) return false;
	if (trimmed.front() == '>') {
		std::size_t marker = indent;
		while (marker < line.size() && line[marker] == '>')
			++marker;
		while (marker < line.size() && line[marker] == ' ')
			++marker;
		targetColumn = static_cast<int>(marker) + 1;
		return true;
	}
	if ((trimmed.front() == '-' || trimmed.front() == '*' || trimmed.front() == '+') && trimmed.size() > 1 && std::isspace(static_cast<unsigned char>(trimmed[1])) != 0) {
		std::size_t marker = indent + 2;
		if (trimmed.size() >= 5 && line.size() >= indent + 5 && line[indent + 2] == '[' && line[indent + 4] == ']') marker = indent + 6;
		targetColumn = static_cast<int>(marker) + 1;
		return true;
	}
	if (std::isdigit(static_cast<unsigned char>(trimmed.front())) != 0) {
		std::size_t marker = indent;
		while (marker < line.size() && std::isdigit(static_cast<unsigned char>(line[marker])) != 0)
			++marker;
		if (marker < line.size() && (line[marker] == '.' || line[marker] == ')')) {
			++marker;
			while (marker < line.size() && line[marker] == ' ')
				++marker;
			targetColumn = static_cast<int>(marker) + 1;
			return true;
		}
	}
	return false;
}

bool isMarkdownFenceLine(std::string_view trimmed) noexcept {
	if (trimmed.size() < 3) return false;
	const char marker = trimmed.front();
	if (marker != '`' && marker != '~') return false;
	std::size_t runLength = 0;
	while (runLength < trimmed.size() && trimmed[runLength] == marker)
		++runLength;
	return runLength >= 3;
}

bool isMarkdownSetextUnderline(std::string_view trimmed) noexcept {
	if (trimmed.size() < 3) return false;
	const char marker = trimmed.front();
	if (marker != '=' && marker != '-') return false;
	for (char ch : trimmed)
		if (ch != marker && ch != ' ' && ch != '\t') return false;
	return true;
}

bool isMakeTargetLine(std::string_view trimmed) noexcept {
	const std::size_t colon = trimmed.find(':');
	const std::size_t eq = trimmed.find('=');
	return colon != std::string_view::npos && colon > 0 && (eq == std::string_view::npos || colon < eq);
}

bool isMakeRecipeLine(std::string_view lineText) noexcept {
	return !lineText.empty() && lineText.front() == '\t';
}

bool isPreprocessorFoldStart(std::string_view trimmed) noexcept {
	return trimmed.starts_with("#if") || trimmed.starts_with("#ifdef") || trimmed.starts_with("#ifndef");
}

bool isPreprocessorFoldEnd(std::string_view trimmed) noexcept {
	return trimmed.starts_with("#endif");
}

bool isPreprocessorFoldSibling(std::string_view trimmed) noexcept {
	return trimmed.starts_with("#else") || trimmed.starts_with("#elif");
}

bool isIndentFoldLanguage(MRSyntaxLanguage language) noexcept {
	return language == MRSyntaxLanguage::Python || language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh || language == MRSyntaxLanguage::Perl || language == MRSyntaxLanguage::Make || language == MRSyntaxLanguage::MRMAC ||
	       language == MRSyntaxLanguage::Yaml || language == MRSyntaxLanguage::PlainText;
}

bool isShellSiblingLead(std::string_view upperLine) noexcept {
	return upperLine == "ELSE" || upperLine.starts_with("ELIF ");
}

bool isMRMACMacroStart(std::string_view upperLine) noexcept {
	return upperLine.starts_with("$MACRO ");
}

bool isMRMACMacroEnd(std::string_view upperLine) noexcept {
	return upperLine == "END_MACRO" || upperLine == "END_MACRO;";
}

bool isMRMACIfLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("IF ") && upperLine.ends_with(" THEN");
}

bool isMRMACElseLead(std::string_view upperLine) noexcept {
	return upperLine == "ELSE";
}

bool isMRMACWhileLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("WHILE ") && upperLine.ends_with(" DO");
}

bool isMRMACEndLead(std::string_view upperLine) noexcept {
	return upperLine == "END" || upperLine == "END;";
}

bool isMRMACCommentLine(std::string_view trimmed) noexcept {
	return !trimmed.empty() && trimmed.front() == ';';
}

bool isPascalIfLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("IF ") && upperLine.find(" THEN") != std::string_view::npos;
}

bool isPascalBeginLead(std::string_view upperLine) noexcept {
	return upperLine == "BEGIN" || upperLine == "BEGIN;";
}

bool isPascalRecordLead(std::string_view upperLine) noexcept {
	return upperLine == "RECORD" || upperLine == "RECORD;" || upperLine.ends_with(" RECORD");
}

bool isPascalCaseLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("CASE ") && upperLine.find(" OF") != std::string_view::npos;
}

bool isPascalRepeatLead(std::string_view upperLine) noexcept {
	return upperLine == "REPEAT" || upperLine == "REPEAT;";
}

bool isPascalElseLead(std::string_view upperLine) noexcept {
	return upperLine == "ELSE" || upperLine == "ELSE;";
}

bool isPascalClassLead(std::string_view upperLine) noexcept {
	return upperLine == "CLASS" || upperLine == "CLASS;" || upperLine.find("= CLASS") != std::string_view::npos;
}

bool isPascalObjectLead(std::string_view upperLine) noexcept {
	return upperLine == "OBJECT" || upperLine == "OBJECT;" || upperLine.find("= OBJECT") != std::string_view::npos;
}

bool isPascalTryLead(std::string_view upperLine) noexcept {
	return upperLine == "TRY" || upperLine == "TRY;";
}

bool isPascalExceptLead(std::string_view upperLine) noexcept {
	return upperLine == "EXCEPT" || upperLine == "EXCEPT;";
}

bool isPascalFinallyLead(std::string_view upperLine) noexcept {
	return upperLine == "FINALLY" || upperLine == "FINALLY;";
}

bool isPascalUntilLead(std::string_view upperLine) noexcept {
	return upperLine.starts_with("UNTIL ");
}

bool isPascalEndLead(std::string_view upperLine) noexcept {
	return upperLine == "END" || upperLine == "END;" || upperLine == "END.";
}

bool isPascalDoLead(std::string_view upperLine) noexcept {
	return upperLine.ends_with(" DO");
}

bool isPascalCommentLikeLine(std::string_view trimmed) noexcept {
	return trimmed.starts_with("//");
}

bool isXmlNameStartChar(char ch) noexcept {
	const unsigned char value = static_cast<unsigned char>(ch);
	return std::isalpha(value) != 0 || ch == '_' || ch == ':';
}

bool isXmlNameChar(char ch) noexcept {
	const unsigned char value = static_cast<unsigned char>(ch);
	return std::isalnum(value) != 0 || ch == '_' || ch == ':' || ch == '-' || ch == '.';
}

std::size_t skipXmlName(std::string_view text, std::size_t pos) noexcept {
	if (pos >= text.size() || !isXmlNameStartChar(text[pos])) return pos;
	++pos;
	while (pos < text.size() && isXmlNameChar(text[pos]))
		++pos;
	return pos;
}

bool parseXmlLeadingOpenTag(std::string_view trimmed, std::string_view &tagName) noexcept {
	const std::string_view normalized = trimView(trimmed);
	std::size_t pos = 0;
	char quote = '\0';
	std::size_t closePos = std::string_view::npos;
	std::size_t lastNonSpace = std::string_view::npos;

	tagName = {};
	if (normalized.size() < 3 || normalized.front() != '<') return false;
	if (normalized[1] == '/' || normalized[1] == '!' || normalized[1] == '?') return false;
	pos = skipXmlName(normalized, 1);
	if (pos <= 1) return false;
	tagName = normalized.substr(1, pos - 1);
	for (std::size_t i = pos; i < normalized.size(); ++i) {
		const char ch = normalized[i];
		if (quote != '\0') {
			if (ch == quote) quote = '\0';
			continue;
		}
		if (ch == '"' || ch == '\'') {
			quote = ch;
			continue;
		}
		if (ch == '>') {
			closePos = i;
			break;
		}
		if (std::isspace(static_cast<unsigned char>(ch)) == 0) lastNonSpace = i;
	}
	if (closePos == std::string_view::npos || quote != '\0') return false;
	if (lastNonSpace != std::string_view::npos && normalized[lastNonSpace] == '/') return false;
	return true;
}

bool parseXmlLeadingCloseTag(std::string_view trimmed, std::string_view &tagName) noexcept {
	const std::string_view normalized = trimView(trimmed);
	std::size_t pos = 0;

	tagName = {};
	if (normalized.size() < 4 || !normalized.starts_with("</")) return false;
	pos = skipXmlName(normalized, 2);
	if (pos <= 2) return false;
	tagName = normalized.substr(2, pos - 2);
	while (pos < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[pos])) != 0)
		++pos;
	return pos < normalized.size() && normalized[pos] == '>';
}

bool isXmlCommentLikeLine(std::string_view trimmed) noexcept {
	const std::string_view normalized = trimView(trimmed);
	return normalized.starts_with("<!--") || normalized.starts_with("<?") || normalized.starts_with("<!");
}

std::string_view stripMRMACTrailingComment(std::string_view text) noexcept {
	const std::size_t commentStart = text.find_first_of(';');
	if (commentStart == std::string_view::npos) return text;
	if (commentStart == 0 || !isIndentWhitespace(text[commentStart - 1])) return text;
	return trimView(text.substr(0, commentStart));
}

bool isSystemdSectionHeader(std::string_view trimmed) noexcept {
	if (trimmed.size() < 3 || trimmed.front() != '[' || trimmed.back() != ']') return false;
	for (char ch : trimmed) {
		if (std::isalnum(static_cast<unsigned char>(ch)) != 0) continue;
		if (ch == '[' || ch == ']' || ch == '-' || ch == '_') continue;
		return false;
	}
	return true;
}

bool isSystemdCommentLine(std::string_view trimmed) noexcept {
	return !trimmed.empty() && (trimmed.front() == '#' || trimmed.front() == ';');
}

char matchingCloserForOpenDelimiter(char ch) noexcept {
	switch (ch) {
		case '{':
			return '}';
		case '[':
			return ']';
		case '(':
			return ')';
		default:
			return 0;
	}
}

char matchingOpenDelimiterForCloser(char ch) noexcept {
	switch (ch) {
		case '}':
			return '{';
		case ']':
			return '[';
		case ')':
			return '(';
		default:
			return 0;
	}
}

enum class SmartDedentKind {
	None,
	Delimiter,
	ShellConditional,
	ShellLoop,
	ShellCase,
	FishConditional,
	FishCase,
	FishEnd,
	MRMACConditional,
	MRMACEnd,
	MRMACMacro,
	PythonConditional,
	PythonTry,
	PythonCase,
	PerlConditional,
	PascalConditional,
	PascalTry,
	PascalEnd,
	PascalRepeat,
	XmlTag,
	CLikeElse,
	CLikeCatch,
};

struct SmartDedentRequest {
	SmartDedentKind kind = SmartDedentKind::None;
	char closer = 0;
	std::string_view tagName;
};

SmartDedentRequest classifySmartDedentRequest(std::string_view trimmed, MRSyntaxLanguage language) noexcept {
	std::string_view normalizedTrimmed = trimView(trimmed);
	if (language == MRSyntaxLanguage::MRMAC) normalizedTrimmed = stripMRMACTrailingComment(normalizedTrimmed);
	const std::string upperLine = upperAscii(std::string(normalizedTrimmed));
	const std::string_view normalizedUpper = trimView(skipLeadingClosersAndSpace(upperLine));

	if (startsWithCloser(trimmed)) return {SmartDedentKind::Delimiter, trimmed.front()};
	switch (language) {
		case MRSyntaxLanguage::Bash:
		case MRSyntaxLanguage::Zsh:
			if (upperLine == "FI" || normalizedUpper == "ELSE" || normalizedUpper.starts_with("ELIF ")) return {SmartDedentKind::ShellConditional, 0};
			if (upperLine == "DONE") return {SmartDedentKind::ShellLoop, 0};
			if (upperLine == "ESAC") return {SmartDedentKind::ShellCase, 0};
			break;
		case MRSyntaxLanguage::Fish:
			if (isFishElseIfLead(upperLine) || isFishElseLead(upperLine)) return {SmartDedentKind::FishConditional, 0};
			if (isFishCaseLead(upperLine)) return {SmartDedentKind::FishCase, 0};
			if (isFishEndLead(upperLine)) return {SmartDedentKind::FishEnd, 0};
			break;
		case MRSyntaxLanguage::MRMAC:
			if (isMRMACElseLead(upperLine)) return {SmartDedentKind::MRMACConditional, 0};
			if (isMRMACEndLead(upperLine)) return {SmartDedentKind::MRMACEnd, 0};
			if (isMRMACMacroEnd(upperLine)) return {SmartDedentKind::MRMACMacro, 0};
			break;
		case MRSyntaxLanguage::Python:
			if (upperLine == "ELSE:" || upperLine.starts_with("ELIF ")) return {SmartDedentKind::PythonConditional, 0};
			if (upperLine == "FINALLY:" || upperLine == "EXCEPT:" || upperLine.starts_with("EXCEPT ") || upperLine.starts_with("EXCEPT* ")) return {SmartDedentKind::PythonTry, 0};
			if (upperLine.starts_with("CASE ")) return {SmartDedentKind::PythonCase, 0};
			break;
		case MRSyntaxLanguage::Perl:
			if (startsWithKeywordToken(normalizedUpper, "ELSIF") || normalizedUpper == "ELSE {" || normalizedUpper == "ELSE") return {SmartDedentKind::PerlConditional, 0};
			break;
		case MRSyntaxLanguage::Pascal:
			if (isPascalElseLead(normalizedUpper)) return {SmartDedentKind::PascalConditional, 0};
			if (isPascalExceptLead(normalizedUpper) || isPascalFinallyLead(normalizedUpper)) return {SmartDedentKind::PascalTry, 0};
			if (isPascalUntilLead(normalizedUpper)) return {SmartDedentKind::PascalRepeat, 0};
			if (isPascalEndLead(normalizedUpper)) return {SmartDedentKind::PascalEnd, 0};
			break;
		case MRSyntaxLanguage::Xml: {
			std::string_view tagName;
			if (parseXmlLeadingCloseTag(normalizedTrimmed, tagName)) return {SmartDedentKind::XmlTag, 0, tagName};
			break;
		}
		case MRSyntaxLanguage::C:
		case MRSyntaxLanguage::Cpp:
		case MRSyntaxLanguage::JavaScript:
			case MRSyntaxLanguage::Swift:
			case MRSyntaxLanguage::Rust:
			case MRSyntaxLanguage::Go:
			case MRSyntaxLanguage::Kotlin:
			case MRSyntaxLanguage::CSharp:
				if (normalizedUpper.starts_with("ELSE")) return {SmartDedentKind::CLikeElse, 0};
				if (normalizedUpper.starts_with("CATCH") || normalizedUpper.starts_with("FINALLY")) return {SmartDedentKind::CLikeCatch, 0};
			break;
		default:
			break;
	}
	return {};
}

bool isStandaloneSmartDedentLine(std::string_view trimmed, MRSyntaxLanguage language) noexcept {
	const SmartDedentRequest request = classifySmartDedentRequest(trimmed, language);
	if (request.kind == SmartDedentKind::None) return false;
	if (request.kind != SmartDedentKind::Delimiter) return true;

	const std::string_view remainder = trimView(skipLeadingClosersAndSpace(trimmed));
	if (remainder.empty()) return true;
	return classifySmartDedentRequest(remainder, language).kind != SmartDedentKind::None;
}

std::size_t trailingSmartDedentSplitOffset(std::string_view lineText, MRSyntaxLanguage language) noexcept {
	for (std::size_t tokenStart = 0; tokenStart < lineText.size(); ++tokenStart) {
		if (isIndentWhitespace(lineText[tokenStart])) continue;
		if (!isStandaloneSmartDedentLine(lineText.substr(tokenStart), language)) continue;
		if (trimView(lineText.substr(0, tokenStart)).empty()) continue;
		if (tokenStart > 0) {
			const unsigned char previous = static_cast<unsigned char>(lineText[tokenStart - 1]);
			if (std::isalnum(previous) != 0 || lineText[tokenStart - 1] == '_') continue;
		}
		std::size_t splitStart = tokenStart;
		while (splitStart > 0 && isIndentWhitespace(lineText[splitStart - 1]))
			--splitStart;
		return splitStart;
	}
	return std::string_view::npos;
}

bool isDedentSearchSkippableLine(std::string_view trimmed, MRSyntaxLanguage language) noexcept {
	if (trimView(trimmed).empty()) return true;
	switch (language) {
		case MRSyntaxLanguage::Bash:
		case MRSyntaxLanguage::Zsh:
		case MRSyntaxLanguage::Python:
		case MRSyntaxLanguage::Perl:
		case MRSyntaxLanguage::Fish:
			return trimmed.starts_with("#");
		case MRSyntaxLanguage::MRMAC:
			return isMRMACCommentLine(trimmed);
		case MRSyntaxLanguage::C:
		case MRSyntaxLanguage::Cpp:
		case MRSyntaxLanguage::JavaScript:
		case MRSyntaxLanguage::Json:
			return isCLikeCommentLikeLine(trimmed);
		case MRSyntaxLanguage::Swift:
			return isSwiftCommentLikeLine(trimmed);
		case MRSyntaxLanguage::Rust:
			return isRustCommentLikeLine(trimmed);
		case MRSyntaxLanguage::Go:
			return isGoCommentLikeLine(trimmed);
		case MRSyntaxLanguage::Kotlin:
			return isKotlinCommentLikeLine(trimmed);
		case MRSyntaxLanguage::CSharp:
			return isCSharpCommentLikeLine(trimmed) || trimmed.starts_with("#");
		case MRSyntaxLanguage::Pascal:
			return isPascalCommentLikeLine(trimmed);
		case MRSyntaxLanguage::Xml:
			return isXmlCommentLikeLine(trimmed);
		default:
			return false;
	}
}

bool matchesSmartDedentAnchor(std::string_view trimmed, std::string_view upperLine, MRSyntaxLanguage language, SmartDedentRequest request) noexcept {
	if (language == MRSyntaxLanguage::MRMAC) {
		trimmed = stripMRMACTrailingComment(trimmed);
		upperLine = stripMRMACTrailingComment(upperLine);
	}
	const std::string_view normalizedUpper = trimView(skipLeadingClosersAndSpace(upperLine));

	switch (request.kind) {
		case SmartDedentKind::Delimiter: {
			const char opener = matchingOpenDelimiterForCloser(request.closer);
			if (opener == 0) return false;
			const std::size_t last = lastSignificantByte(trimmed);
			return last != std::string_view::npos && trimmed[last] == opener;
		}
		case SmartDedentKind::ShellConditional:
			return (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) && shellIndentBlockKind(upperLine) == kShellBlockConditional;
		case SmartDedentKind::ShellLoop:
			return (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) && shellIndentBlockKind(upperLine) == kShellBlockLoop;
		case SmartDedentKind::ShellCase:
			return (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) && shellIndentBlockKind(upperLine) == kShellBlockCase;
		case SmartDedentKind::FishConditional:
			return language == MRSyntaxLanguage::Fish && (isFishIfLead(upperLine) || isFishElseIfLead(upperLine) || isFishElseLead(upperLine));
		case SmartDedentKind::FishCase:
			return language == MRSyntaxLanguage::Fish && (isFishCaseLead(upperLine) || isFishSwitchLead(upperLine));
		case SmartDedentKind::FishEnd:
			return language == MRSyntaxLanguage::Fish &&
			       (isFishFunctionLead(upperLine) || isFishIfLead(upperLine) || isFishElseIfLead(upperLine) || isFishElseLead(upperLine) || isFishWhileLead(upperLine) ||
			        isFishForLead(upperLine) || isFishSwitchLead(upperLine) || isFishBeginLead(upperLine));
		case SmartDedentKind::MRMACConditional:
			return language == MRSyntaxLanguage::MRMAC && (isMRMACIfLead(upperLine) || isMRMACElseLead(upperLine));
		case SmartDedentKind::MRMACEnd:
			return language == MRSyntaxLanguage::MRMAC && (isMRMACIfLead(upperLine) || isMRMACElseLead(upperLine) || isMRMACWhileLead(upperLine));
		case SmartDedentKind::MRMACMacro:
			return language == MRSyntaxLanguage::MRMAC && isMRMACMacroStart(upperLine);
		case SmartDedentKind::PythonConditional:
			return language == MRSyntaxLanguage::Python && (upperLine.starts_with("IF ") || upperLine.starts_with("ELIF ") || upperLine == "ELSE:");
		case SmartDedentKind::PythonTry:
			return language == MRSyntaxLanguage::Python &&
			       (upperLine == "TRY:" || upperLine == "FINALLY:" || upperLine == "EXCEPT:" || upperLine.starts_with("EXCEPT ") || upperLine.starts_with("EXCEPT* "));
		case SmartDedentKind::PythonCase:
			return language == MRSyntaxLanguage::Python && (upperLine.starts_with("MATCH ") || upperLine.starts_with("CASE "));
		case SmartDedentKind::PerlConditional:
			return language == MRSyntaxLanguage::Perl &&
			       (startsWithKeywordToken(normalizedUpper, "IF") || startsWithKeywordToken(normalizedUpper, "UNLESS") || startsWithKeywordToken(normalizedUpper, "ELSIF") || normalizedUpper == "ELSE {");
		case SmartDedentKind::PascalConditional:
			return language == MRSyntaxLanguage::Pascal && (isPascalIfLead(normalizedUpper) || isPascalElseLead(normalizedUpper));
		case SmartDedentKind::PascalTry:
			return language == MRSyntaxLanguage::Pascal && (isPascalTryLead(normalizedUpper) || isPascalExceptLead(normalizedUpper) || isPascalFinallyLead(normalizedUpper));
		case SmartDedentKind::PascalEnd:
			return language == MRSyntaxLanguage::Pascal &&
			       (isPascalBeginLead(normalizedUpper) || isPascalRecordLead(normalizedUpper) || isPascalCaseLead(normalizedUpper) || isPascalIfLead(normalizedUpper) || isPascalElseLead(normalizedUpper) ||
			        isPascalDoLead(normalizedUpper) || isPascalClassLead(normalizedUpper) || isPascalObjectLead(normalizedUpper) || isPascalTryLead(normalizedUpper) || isPascalExceptLead(normalizedUpper) ||
			        isPascalFinallyLead(normalizedUpper));
		case SmartDedentKind::PascalRepeat:
			return language == MRSyntaxLanguage::Pascal && isPascalRepeatLead(normalizedUpper);
		case SmartDedentKind::XmlTag: {
			std::string_view tagName;
			return language == MRSyntaxLanguage::Xml && parseXmlLeadingOpenTag(trimmed, tagName) && tagName == request.tagName;
		}
		case SmartDedentKind::CLikeElse:
			return normalizedUpper.starts_with("IF ") || normalizedUpper.starts_with("ELSE");
		case SmartDedentKind::CLikeCatch:
			return normalizedUpper.starts_with("TRY") || normalizedUpper.starts_with("CATCH") || normalizedUpper.starts_with("FINALLY");
		case SmartDedentKind::None:
			return false;
	}
	return false;
}

int markdownHeadingLevel(std::string_view trimmed, std::string_view nextTrimmed) noexcept {
	if (trimmed.starts_with("#")) {
		std::size_t level = 0;
		while (level < trimmed.size() && trimmed[level] == '#')
			++level;
		if (level > 0 && level <= 6 && (level == trimmed.size() || trimmed[level] == ' ' || trimmed[level] == '\t')) return static_cast<int>(level);
	}
	if (!nextTrimmed.empty() && isMarkdownSetextUnderline(nextTrimmed)) return nextTrimmed.front() == '=' ? 1 : 2;
	return 0;
}

int latexHeadingLevel(std::string_view trimmed) noexcept {
	struct LatexHeadingEntry {
		const char *prefix;
		int level;
	};
	static const LatexHeadingEntry entries[] = {
		{"\\part", 1},
		{"\\chapter", 2},
		{"\\section", 3},
		{"\\subsection", 4},
		{"\\subsubsection", 5},
		{"\\paragraph", 6},
		{"\\subparagraph", 7},
	};

	for (const LatexHeadingEntry &entry : entries) {
		const std::string_view prefix(entry.prefix);
		if (!trimmed.starts_with(prefix)) continue;
		if (trimmed.size() == prefix.size()) return entry.level;
		const char next = trimmed[prefix.size()];
		if (next == '{' || next == '*' || next == '[' || next == ' ' || next == '\t') return entry.level;
	}
	return 0;
}

std::string_view latexLineBeforeComment(std::string_view line) noexcept {
	for (std::size_t index = 0; index < line.size(); ++index) {
		if (line[index] != '%') continue;
		std::size_t slashCount = 0;
		std::size_t probe = index;
		while (probe > 0 && line[probe - 1] == '\\') {
			++slashCount;
			--probe;
		}
		if (slashCount % 2 == 0) return line.substr(0, index);
	}
	return line;
}

bool latexEnvironmentNameChar(char ch) noexcept {
	const unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '*' || ch == '_' || ch == '-' || ch == ':' || ch == '@';
}

bool parseLatexEnvironmentCommand(std::string_view line, std::string_view command, std::string_view &environmentName, std::size_t *commandEndOffset = nullptr) noexcept {
	line = trimView(latexLineBeforeComment(line));
	environmentName = std::string_view();
	if (!line.starts_with(command)) return false;
	std::size_t index = command.size();
	if (index < line.size() && line[index] != '{' && line[index] != ' ' && line[index] != '\t') return false;
	while (index < line.size() && isIndentWhitespace(line[index]))
		++index;
	if (index >= line.size() || line[index] != '{') return false;
	const std::size_t nameStart = index + 1;
	std::size_t nameEnd = nameStart;
	while (nameEnd < line.size() && latexEnvironmentNameChar(line[nameEnd]))
		++nameEnd;
	if (nameEnd == nameStart || nameEnd >= line.size() || line[nameEnd] != '}') return false;
	environmentName = line.substr(nameStart, nameEnd - nameStart);
	if (commandEndOffset != nullptr) *commandEndOffset = nameEnd + 1;
	return true;
}

bool latexLineContainsEnvironmentEnd(std::string_view line, std::string_view environmentName) noexcept {
	line = latexLineBeforeComment(line);
	for (std::size_t pos = line.find("\\end"); pos != std::string_view::npos; pos = line.find("\\end", pos + 1)) {
		std::string_view candidateName;
		if (parseLatexEnvironmentCommand(line.substr(pos), "\\end", candidateName) && candidateName == environmentName) return true;
	}
	return false;
}

bool parseLatexLeadingBeginEnvironment(std::string_view trimmed, std::string_view &environmentName) noexcept {
	std::size_t commandEndOffset = 0;
	if (!parseLatexEnvironmentCommand(trimmed, "\\begin", environmentName, &commandEndOffset)) return false;
	return !latexLineContainsEnvironmentEnd(trimmed.substr(commandEndOffset), environmentName);
}

bool parseLatexLeadingEndEnvironment(std::string_view trimmed, std::string_view &environmentName) noexcept {
	return parseLatexEnvironmentCommand(trimmed, "\\end", environmentName);
}

bool markdownFenceMarker(std::string_view trimmed, char &marker, std::size_t &runLength) noexcept {
	if (!isMarkdownFenceLine(trimmed)) return false;
	marker = trimmed.front();
	runLength = 0;
	while (runLength < trimmed.size() && trimmed[runLength] == marker)
		++runLength;
	return runLength >= 3;
}

bool isMarkdownFenceClose(std::string_view trimmed, char marker, std::size_t runLength) noexcept {
	if (trimmed.empty() || trimmed.front() != marker) return false;
	std::size_t currentRun = 0;
	while (currentRun < trimmed.size() && trimmed[currentRun] == marker)
		++currentRun;
	return currentRun >= runLength;
}

bool isMarkdownListLead(std::string_view line, std::string_view trimmed, std::string_view nextTrimmed, std::size_t currentIndent, std::size_t nextIndent) noexcept {
	if (trimmed.empty() || nextTrimmed.empty()) return false;
	if (nextIndent <= currentIndent) return false;
	if ((trimmed.front() == '-' || trimmed.front() == '*' || trimmed.front() == '+') && trimmed.size() > 1 && std::isspace(static_cast<unsigned char>(trimmed[1])) != 0) return true;
	if (std::isdigit(static_cast<unsigned char>(trimmed.front())) != 0) {
		std::size_t marker = currentIndent;
		while (marker < line.size() && std::isdigit(static_cast<unsigned char>(line[marker])) != 0)
			++marker;
		return marker < line.size() && (line[marker] == '.' || line[marker] == ')');
	}
	return false;
}

bool isMarkdownBlockQuoteLead(std::string_view trimmed, std::string_view nextTrimmed) noexcept {
	return !trimmed.empty() && !nextTrimmed.empty() && trimmed.front() == '>' && nextTrimmed.front() == '>';
}

bool isNonEmptyNonRecipeMakeLine(std::string_view lineText, std::string_view trimmed) noexcept {
	return !trimmed.empty() && !isMakeRecipeLine(lineText);
}

bool isMakeDirectiveFoldStart(std::string_view trimmed) noexcept {
	return trimmed.starts_with("ifeq") || trimmed.starts_with("ifneq") || trimmed.starts_with("ifdef") || trimmed.starts_with("ifndef") || trimmed.starts_with("define ");
}

bool isMakeDirectiveFoldEnd(std::string_view trimmed) noexcept {
	return trimmed == "endif" || trimmed == "endef";
}

bool isMakeDirectiveFoldSibling(std::string_view trimmed) noexcept {
	return trimmed == "else";
}

struct MRFoldOpenBlock {
	std::size_t startLine = 0;
	std::size_t indent = 0;
	unsigned short level = 0;
	MRFoldSourceKind sourceKind = MRFoldSourceKind::Generic;
	char closer = 0;
	char marker = 0;
	std::size_t markerLength = 0;
	int headingLevel = 0;
	int languageBlockKind = 0;
	bool siblingContinuation = false;
	std::size_t lastContentLine = std::numeric_limits<std::size_t>::max();
	std::string xmlTagName;
};

struct MRFoldScanOutput {
	std::vector<MRFoldSpan> spans;
	int visibleMaxLevel = 1;
};

struct MRFoldWarmupPayload final : mr::coprocessor::Payload {
	MRSyntaxLanguage language;
	std::size_t scanTopLine;
	std::size_t scanBottomLine;
	int visibleGutterColumns;
	std::vector<std::string> lineTexts;
	std::vector<MRFoldSpan> spans;

	MRFoldWarmupPayload() noexcept : language(MRSyntaxLanguage::PlainText), scanTopLine(0), scanBottomLine(0), visibleGutterColumns(1), lineTexts(), spans() {
	}

	MRFoldWarmupPayload(MRSyntaxLanguage aLanguage, std::size_t aScanTopLine, std::size_t aScanBottomLine, int aVisibleGutterColumns, std::vector<std::string> aLineTexts,
	                   std::vector<MRFoldSpan> aSpans)
	    : language(aLanguage), scanTopLine(aScanTopLine), scanBottomLine(aScanBottomLine), visibleGutterColumns(std::max(1, aVisibleGutterColumns)), lineTexts(std::move(aLineTexts)),
	      spans(std::move(aSpans)) {
	}
};

struct MRViewportScanChunk {
	std::size_t startLine = 0;
	std::size_t endLine = 0;
	std::vector<std::string> lineTexts;
};

void appendFoldScanLineTexts(std::vector<std::string> &target, const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t startLine, std::size_t endLine) {
	if (endLine <= startLine) return;
	std::size_t lineStart = snapshot.lineStartByIndex(startLine);

	for (std::size_t lineIndex = startLine; lineIndex < endLine; ++lineIndex) {
		target.push_back(snapshot.lineText(lineStart));
		if (lineStart >= snapshot.length()) break;
		const std::size_t nextLineStart = snapshot.nextLine(lineStart);
		if (nextLineStart <= lineStart) break;
		lineStart = nextLineStart;
	}
}

std::vector<MRViewportScanChunk> planViewportScanChunks(std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t focusTopLine, std::size_t focusBottomLine) {
	static constexpr std::size_t chunkLineCount = 256;
	std::vector<MRViewportScanChunk> chunks;

	if (scanBottomLine <= scanTopLine) return chunks;
	focusTopLine = std::clamp(focusTopLine, scanTopLine, scanBottomLine);
	focusBottomLine = std::clamp(focusBottomLine, focusTopLine, scanBottomLine);
	if (focusBottomLine <= focusTopLine) focusBottomLine = std::min(scanBottomLine, focusTopLine + chunkLineCount);
	chunks.push_back(MRViewportScanChunk{focusTopLine, focusBottomLine, {}});
	std::size_t aboveLine = focusTopLine;
	std::size_t belowLine = focusBottomLine;

	while (aboveLine > scanTopLine || belowLine < scanBottomLine) {
		if (aboveLine > scanTopLine) {
			const std::size_t chunkStartLine = aboveLine > chunkLineCount ? std::max(scanTopLine, aboveLine - chunkLineCount) : scanTopLine;
			chunks.push_back(MRViewportScanChunk{chunkStartLine, aboveLine, {}});
			aboveLine = chunkStartLine;
		}
		if (belowLine < scanBottomLine) {
			const std::size_t chunkEndLine = std::min(scanBottomLine, belowLine + chunkLineCount);
			chunks.push_back(MRViewportScanChunk{belowLine, chunkEndLine, {}});
			belowLine = chunkEndLine;
		}
	}
	std::sort(chunks.begin(), chunks.end(), [](const MRViewportScanChunk &lhs, const MRViewportScanChunk &rhs) { return lhs.startLine < rhs.startLine; });
	return chunks;
}

std::vector<std::string> buildViewportScanLineTextsFromChunks(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t focusTopLine,
                                                                std::size_t focusBottomLine) {
	std::vector<MRViewportScanChunk> chunks = planViewportScanChunks(scanTopLine, scanBottomLine, focusTopLine, focusBottomLine);
	std::vector<std::string> lineTexts;

	if (chunks.empty()) return lineTexts;
	if (chunks.size() == 1) {
		lineTexts.reserve(chunks.front().endLine - chunks.front().startLine);
		appendFoldScanLineTexts(lineTexts, snapshot, chunks.front().startLine, chunks.front().endLine);
		return lineTexts;
	}

	std::size_t reservedLines = 0;
	for (MRViewportScanChunk &chunk : chunks) {
		chunk.lineTexts.reserve(chunk.endLine > chunk.startLine ? chunk.endLine - chunk.startLine : 0);
		appendFoldScanLineTexts(chunk.lineTexts, snapshot, chunk.startLine, chunk.endLine);
		reservedLines += chunk.lineTexts.size();
	}
	lineTexts.reserve(reservedLines);
	for (MRViewportScanChunk &chunk : chunks)
		std::move(chunk.lineTexts.begin(), chunk.lineTexts.end(), std::back_inserter(lineTexts));
	return lineTexts;
}

std::vector<std::string> splitFoldTrainingLines(const std::string &text) {
	std::vector<std::string> lines;
	std::size_t lineStart = 0;

	while (lineStart <= text.size()) {
		std::size_t lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string::npos) lineEnd = text.size();
		std::string line = text.substr(lineStart, lineEnd - lineStart);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(std::move(line));
		if (lineEnd >= text.size()) break;
		lineStart = lineEnd + 1;
	}
	if (lines.empty()) lines.push_back(std::string());
	return lines;
}

MRFoldScanOutput computeFoldSpansForLineTexts(const std::vector<std::string> &lineTexts, std::size_t baseLineIndex, std::size_t topLine, std::size_t requestBottomLine, MRSyntaxLanguage language,
                                              const std::map<std::size_t, MRFoldSpan> &closedFoldSpans) {
	MRFoldScanOutput output;
	std::size_t currentLineIndex = baseLineIndex;

	if (lineTexts.empty()) return output;

	std::vector<MRFoldOpenBlock> openBlocks;
	std::string previousLineText;
	std::string previousUpperLine;
	std::string previousPreviousLineText;
	std::string previousPreviousUpperLine;
	enum : int {
		kLanguageBlockNone = 0,
		kMRMACIfBlock = 1,
		kMRMACWhileBlock = 2,
		kFishIfBlock = 3,
		kFishLoopBlock = 4,
		kFishSwitchBlock = 5,
		kFishCaseBlock = 6,
		kFishGenericBlock = 7,
		kXmlTagBlock = 8,
		kLatexEnvironmentBlock = 9,
	};
	auto appendVisibleSpan = [&](const MRFoldOpenBlock &block, std::size_t endLine) {
		if (endLine <= block.startLine) return;
		const bool spanOpen = closedFoldSpans.find(block.startLine) == closedFoldSpans.end();
		output.spans.push_back(MRFoldSpan(block.startLine, endLine, block.level, block.sourceKind, spanOpen, block.siblingContinuation));
		if (!(endLine < topLine || block.startLine >= requestBottomLine)) output.visibleMaxLevel = std::max(output.visibleMaxLevel, static_cast<int>(block.level) + 1);
	};
	auto openBlock = [&](MRFoldSourceKind sourceKind, std::size_t indent, char closer = 0, char marker = 0, std::size_t markerLength = 0, int headingLevel = 0,
	                     int languageBlockKind = kLanguageBlockNone, std::size_t startLine = std::numeric_limits<std::size_t>::max(),
	                     bool siblingContinuation = false, std::string_view xmlTagName = std::string_view()) {
		MRFoldOpenBlock block;
		unsigned short visibleLevel = 0;

		for ([[maybe_unused]] const MRFoldOpenBlock &existingBlock : openBlocks)
			++visibleLevel;
		block.startLine = startLine == std::numeric_limits<std::size_t>::max() ? currentLineIndex : startLine;
		block.indent = indent;
		block.level = visibleLevel;
		block.sourceKind = sourceKind;
		block.closer = closer;
		block.marker = marker;
		block.markerLength = markerLength;
		block.headingLevel = headingLevel;
		block.languageBlockKind = languageBlockKind;
		block.siblingContinuation = siblingContinuation;
		block.lastContentLine = block.startLine;
		block.xmlTagName.assign(xmlTagName.begin(), xmlTagName.end());
		openBlocks.push_back(block);
	};

	auto findRecentJavaScriptStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
		const std::size_t scanStart = localLineIndex > 4 ? localLineIndex - 4 : 0;
		for (std::size_t candidate = localLineIndex + 1; candidate-- > scanStart;) {
			const std::string_view candidateTrimmed = trimView(lineTexts[candidate]);
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (isJavaScriptStructuralLeadLine(candidateUpper)) return baseLineIndex + candidate;
			if (isJavaScriptArrowFunctionLeadLine(candidateTrimmed)) return baseLineIndex + candidate;
			if (candidate == 0) break;
		}
		return currentLineIndex;
	};

	auto findRecentCLikeStructuralLeadLine = [&](std::size_t localLineIndex, MRSyntaxLanguage currentLanguage) noexcept -> std::size_t {
		const std::size_t scanStart = localLineIndex > 80 ? localLineIndex - 80 : 0;
		for (std::size_t candidate = localLineIndex + 1; candidate-- > scanStart;) {
			const std::string_view candidateTrimmed = trimView(lineTexts[candidate]);
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (isCLikeStructuralLeadLine(candidateTrimmed, candidateUpper, currentLanguage)) {
				if (currentLanguage != MRSyntaxLanguage::Cpp) return baseLineIndex + candidate;
				std::size_t headCandidate = candidate;
				while (headCandidate > scanStart) {
					const std::size_t previousCandidate = headCandidate - 1;
					const std::string_view previousCandidateTrimmed = trimView(lineTexts[previousCandidate]);
					if (previousCandidateTrimmed.empty() || isCLikeCommentLikeLine(previousCandidateTrimmed) || previousCandidateTrimmed.front() == '#') break;
					const std::string previousCandidateUpper = upperAscii(std::string(previousCandidateTrimmed));
					if (!isCppTemplatePrefixLead(previousCandidateTrimmed, previousCandidateUpper)) break;
					headCandidate = previousCandidate;
				}
				return baseLineIndex + headCandidate;
			}
			if (candidate == 0) break;
		}
		return currentLineIndex;
	};

	auto findRecentCBraceStartLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
		const std::size_t structuralLeadLine = findRecentCLikeStructuralLeadLine(localLineIndex, MRSyntaxLanguage::C);
		if (structuralLeadLine != currentLineIndex) return structuralLeadLine;
		const std::size_t scanStart = localLineIndex > 80 ? localLineIndex - 80 : 0;
		for (std::size_t candidate = localLineIndex; candidate-- > scanStart;) {
			const std::string_view candidateTrimmed = trimView(lineTexts[candidate]);
			if (candidateTrimmed.empty() || isCLikeCommentLikeLine(candidateTrimmed)) {
				if (candidate == 0) break;
				continue;
			}
			if (candidateTrimmed.front() == '#') break;
			return baseLineIndex + candidate;
		}
		return currentLineIndex;
	};

	auto findRecentSwiftStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
		const std::size_t scanStart = localLineIndex > 4 ? localLineIndex - 4 : 0;
		for (std::size_t candidate = localLineIndex + 1; candidate-- > scanStart;) {
			const std::string_view candidateTrimmed = trimView(lineTexts[candidate]);
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (isSwiftStructuralLeadLine(candidateUpper)) return baseLineIndex + candidate;
			if (candidate == 0) break;
		}
		return currentLineIndex;
	};

	auto findRecentRustStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
		const std::size_t scanStart = localLineIndex > 6 ? localLineIndex - 6 : 0;
		for (std::size_t candidate = localLineIndex + 1; candidate-- > scanStart;) {
			const std::string_view candidateTrimmed = trimView(lineTexts[candidate]);
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (isRustStructuralLeadLine(candidateUpper)) return baseLineIndex + candidate;
			if (candidate == 0) break;
		}
		return currentLineIndex;
	};

		auto findRecentGoStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
			const std::size_t scanStart = localLineIndex > 6 ? localLineIndex - 6 : 0;
			for (std::size_t candidate = localLineIndex + 1; candidate-- > scanStart;) {
				const std::string_view candidateTrimmed = trimView(lineTexts[candidate]);
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (isGoStructuralLeadLine(candidateUpper)) return baseLineIndex + candidate;
			if (candidate == 0) break;
			}
			return currentLineIndex;
		};

		auto findRecentKotlinStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
			const std::size_t scanStart = localLineIndex > 6 ? localLineIndex - 6 : 0;
			for (std::size_t candidate = localLineIndex + 1; candidate-- > scanStart;) {
				const std::string_view candidateTrimmed = trimView(lineTexts[candidate]);
				const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
				if (isKotlinStructuralLeadLine(candidateUpper)) return baseLineIndex + candidate;
				if (candidate == 0) break;
			}
			return currentLineIndex;
		};

		auto findRecentCSharpStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
			const std::size_t scanStart = localLineIndex > 8 ? localLineIndex - 8 : 0;
			for (std::size_t candidate = localLineIndex + 1; candidate-- > scanStart;) {
				const std::string_view candidateTrimmed = trimView(lineTexts[candidate]);
				const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
				if (isCSharpStructuralLeadLine(candidateUpper)) return baseLineIndex + candidate;
				if (candidate == 0) break;
			}
			return currentLineIndex;
		};

		auto findRecentShellStructuralLeadLine = [&](std::size_t localLineIndex, std::string_view trimmed, std::string_view upperLine) noexcept -> std::size_t {
		const std::size_t scanStart = localLineIndex > 8 ? localLineIndex - 8 : 0;
		const bool braceLine = trimView(trimmed) == "{";
		const bool thenLead = upperLine == "THEN" || upperLine.ends_with(" THEN");
		const bool doLead = upperLine == "DO" || upperLine.ends_with(" DO");
		for (std::size_t candidate = localLineIndex + 1; candidate-- > scanStart;) {
			const std::string_view candidateTrimmed = trimView(lineTexts[candidate]);
			if (candidateTrimmed.empty() || candidateTrimmed.starts_with("#")) {
				if (candidate == 0) break;
				continue;
			}
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (braceLine && isShellFunctionHeadLine(candidateTrimmed, candidateUpper)) return baseLineIndex + candidate;
			if (thenLead && (candidateUpper.starts_with("IF ") || candidateUpper.starts_with("ELIF "))) return baseLineIndex + candidate;
			if (doLead && (candidateUpper.starts_with("FOR ") || candidateUpper.starts_with("WHILE ") || candidateUpper.starts_with("UNTIL ") || candidateUpper.starts_with("SELECT ")))
				return baseLineIndex + candidate;
			if (candidate == 0) break;
		}
		return currentLineIndex;
	};

	for (std::size_t localLineIndex = 0; localLineIndex < lineTexts.size(); ++localLineIndex) {
		const std::size_t lineIndex = baseLineIndex + localLineIndex;
		currentLineIndex = lineIndex;
		const std::string &lineText = lineTexts[localLineIndex];
		const std::string_view trimmed = trimView(lineText);
		const std::string_view previousTrimmed = trimView(previousLineText);
		const std::string_view previousPreviousTrimmed = trimView(previousPreviousLineText);
		const std::size_t currentIndent = leadingIndentBytes(lineText);
		const bool nonEmpty = !trimmed.empty();
		const std::string upperLine = upperAscii(std::string(trimmed));
		const std::string *nextLineTextPtr = localLineIndex + 1 < lineTexts.size() ? &lineTexts[localLineIndex + 1] : nullptr;
		const std::string_view nextTrimmed = nextLineTextPtr != nullptr ? trimView(*nextLineTextPtr) : std::string_view();
		const std::size_t nextIndent = nextLineTextPtr != nullptr ? leadingIndentBytes(*nextLineTextPtr) : 0;
		const std::size_t currentLast = lastSignificantByte(trimmed);
		const bool trailingBlockOpen = [&]() noexcept {
			return currentLast != std::string_view::npos && (trimmed[currentLast] == '{' || trimmed[currentLast] == '[' || trimmed[currentLast] == '(');
		}();
		const bool indentOpens = !nextTrimmed.empty() && nextIndent > currentIndent;
		const int headingLevel = language == MRSyntaxLanguage::Markdown ? markdownHeadingLevel(trimmed, nextTrimmed) :
		                         language == MRSyntaxLanguage::Latex ? latexHeadingLevel(trimmed) :
		                         (language == MRSyntaxLanguage::Systemd && isSystemdSectionHeader(trimmed) ? 1 : 0);
		const bool shellDedent = (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) && isShellDedentLead(trimmed, upperLine);
		const bool shellSiblingLead = (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) && isShellSiblingLead(upperLine);
		const bool fishConditionalLead = language == MRSyntaxLanguage::Fish && (isFishElseIfLead(upperLine) || isFishElseLead(upperLine));
		const bool fishCaseLead = language == MRSyntaxLanguage::Fish && isFishCaseLead(upperLine);
		const bool fishEndLead = language == MRSyntaxLanguage::Fish && isFishEndLead(upperLine);
		const int fishBlockKind = language == MRSyntaxLanguage::Fish ? fishIndentBlockKind(upperLine) : kFishBlockNone;
		const bool pascalElseLead = language == MRSyntaxLanguage::Pascal && isPascalElseLead(upperLine);
		const bool pascalExceptLead = language == MRSyntaxLanguage::Pascal && isPascalExceptLead(upperLine);
		const bool pascalFinallyLead = language == MRSyntaxLanguage::Pascal && isPascalFinallyLead(upperLine);
		const bool pascalEndLead = language == MRSyntaxLanguage::Pascal && isPascalEndLead(upperLine);
		const bool pascalUntilLead = language == MRSyntaxLanguage::Pascal && isPascalUntilLead(upperLine);
		const int pascalBlockKind = language == MRSyntaxLanguage::Pascal ? pascalIndentBlockKind(upperLine) : kPascalBlockNone;
		std::string_view xmlLeadingOpenTagName;
		const bool xmlLeadingOpenTag = language == MRSyntaxLanguage::Xml && parseXmlLeadingOpenTag(trimmed, xmlLeadingOpenTagName);
		std::string_view xmlLeadingCloseTagName;
		const bool xmlLeadingCloseTag = language == MRSyntaxLanguage::Xml && parseXmlLeadingCloseTag(trimmed, xmlLeadingCloseTagName);
		std::string_view latexBeginEnvironmentName;
		const bool latexBeginEnvironment = language == MRSyntaxLanguage::Latex && parseLatexLeadingBeginEnvironment(trimmed, latexBeginEnvironmentName);
		std::string_view latexEndEnvironmentName;
		const bool latexEndEnvironment = language == MRSyntaxLanguage::Latex && parseLatexLeadingEndEnvironment(trimmed, latexEndEnvironmentName);
		const bool pythonDedent = language == MRSyntaxLanguage::Python && isPythonDedentLead(upperLine);
		const bool perlSiblingLead = language == MRSyntaxLanguage::Perl && isPerlSiblingLead(upperLine);
		const bool perlSiblingAfterLeadingCloser = language == MRSyntaxLanguage::Perl && isPerlSiblingLead(upperAscii(std::string(skipLeadingClosersAndSpace(trimmed))));
		const bool perlPodStart = language == MRSyntaxLanguage::Perl && isPerlPodStart(upperLine);
		const bool perlPodEnd = language == MRSyntaxLanguage::Perl && isPerlPodEnd(upperLine);
		const bool javascriptSiblingAfterLeadingCloser =
		    language == MRSyntaxLanguage::JavaScript && isJavaScriptSiblingLead(upperAscii(std::string(skipLeadingClosersAndSpace(trimmed))));
			const bool cLikeSiblingAfterLeadingCloser =
			    (language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::Swift || language == MRSyntaxLanguage::Rust ||
			     language == MRSyntaxLanguage::Go || language == MRSyntaxLanguage::Kotlin || language == MRSyntaxLanguage::CSharp) &&
			    isCLikeSiblingLead(upperAscii(std::string(skipLeadingClosersAndSpace(trimmed))));
		const bool mrmacMacroStart = language == MRSyntaxLanguage::MRMAC && isMRMACMacroStart(upperLine);
		const bool mrmacMacroEnd = language == MRSyntaxLanguage::MRMAC && isMRMACMacroEnd(upperLine);
		const bool mrmacIfLead = language == MRSyntaxLanguage::MRMAC && isMRMACIfLead(upperLine);
		const bool mrmacElseLead = language == MRSyntaxLanguage::MRMAC && isMRMACElseLead(upperLine);
		const bool mrmacWhileLead = language == MRSyntaxLanguage::MRMAC && isMRMACWhileLead(upperLine);
		const bool mrmacEndLead = language == MRSyntaxLanguage::MRMAC && isMRMACEndLead(upperLine);
		const bool preprocessorSibling = isPreprocessorFoldSibling(trimmed);
		const bool makeDirectiveSibling = language == MRSyntaxLanguage::Make && isMakeDirectiveFoldSibling(trimmed);
		const std::size_t recentJavaScriptLeadLine = [&]() noexcept -> std::size_t {
			if (language != MRSyntaxLanguage::JavaScript || currentLast == std::string_view::npos || trimmed[currentLast] != '{') return currentLineIndex;
			const std::string_view beforeBrace = trimView(trimmed.substr(0, currentLast));
			if (trimmed != "{" && beforeBrace.find(')') == std::string_view::npos && beforeBrace.find(']') == std::string_view::npos) return currentLineIndex;
			return findRecentJavaScriptStructuralLeadLine(localLineIndex);
		}();
		const std::size_t recentCLikeLeadLine = [&]() noexcept -> std::size_t {
			if ((language != MRSyntaxLanguage::C && language != MRSyntaxLanguage::Cpp) || currentLast == std::string_view::npos || trimmed[currentLast] != '{') return currentLineIndex;
			return findRecentCLikeStructuralLeadLine(localLineIndex, language);
		}();
		const std::size_t recentCBraceStartLine = [&]() noexcept -> std::size_t {
			if (language != MRSyntaxLanguage::C || currentLast == std::string_view::npos || trimmed[currentLast] != '{') return currentLineIndex;
			if (trimmed == "{") return findRecentCBraceStartLine(localLineIndex);
			return currentLineIndex;
		}();
		const std::size_t recentSwiftLeadLine = [&]() noexcept -> std::size_t {
			if (language != MRSyntaxLanguage::Swift || currentLast == std::string_view::npos || trimmed[currentLast] != '{') return currentLineIndex;
			const std::string_view beforeBraceUpper = trimView(upperLine.substr(0, currentLast));
			const std::string_view normalizedBeforeBraceUpper = normalizeSwiftStructuralLeadText(beforeBraceUpper);
			if (trimmed != "{" && !normalizedBeforeBraceUpper.empty() && !isSwiftStructuralLeadLine(normalizedBeforeBraceUpper) &&
			    beforeBraceUpper.find(')') == std::string_view::npos && beforeBraceUpper.find(']') == std::string_view::npos)
				return currentLineIndex;
			return findRecentSwiftStructuralLeadLine(localLineIndex);
		}();
		const std::size_t recentRustLeadLine = [&]() noexcept -> std::size_t {
			if (language != MRSyntaxLanguage::Rust || currentLast == std::string_view::npos || trimmed[currentLast] != '{') return currentLineIndex;
			const std::string_view beforeBraceUpper = trimView(upperLine.substr(0, currentLast));
			const std::string_view normalizedBeforeBraceUpper = normalizeRustStructuralLeadText(beforeBraceUpper);
			if (trimmed != "{" && !normalizedBeforeBraceUpper.empty() && !isRustStructuralLeadLine(normalizedBeforeBraceUpper) && beforeBraceUpper.find(')') == std::string_view::npos &&
			    beforeBraceUpper.find(']') == std::string_view::npos && beforeBraceUpper.find('>') == std::string_view::npos)
				return currentLineIndex;
			return findRecentRustStructuralLeadLine(localLineIndex);
			}();
			const std::size_t recentGoLeadLine = [&]() noexcept -> std::size_t {
				if (language != MRSyntaxLanguage::Go || currentLast == std::string_view::npos || trimmed[currentLast] != '{') return currentLineIndex;
				const std::string_view beforeBraceUpper = trimView(upperLine.substr(0, currentLast));
				const std::string_view normalizedBeforeBraceUpper = normalizeGoStructuralLeadText(beforeBraceUpper);
				if (trimmed != "{" && !normalizedBeforeBraceUpper.empty() && !isGoStructuralLeadLine(normalizedBeforeBraceUpper) && beforeBraceUpper.find(')') == std::string_view::npos &&
				    beforeBraceUpper.find(']') == std::string_view::npos)
					return currentLineIndex;
				return findRecentGoStructuralLeadLine(localLineIndex);
			}();
			const std::size_t recentKotlinLeadLine = [&]() noexcept -> std::size_t {
				if (language != MRSyntaxLanguage::Kotlin || currentLast == std::string_view::npos || trimmed[currentLast] != '{') return currentLineIndex;
				const std::string_view beforeBraceUpper = trimView(upperLine.substr(0, currentLast));
				const std::string_view normalizedBeforeBraceUpper = normalizeKotlinStructuralLeadText(beforeBraceUpper);
				if (trimmed != "{" && !normalizedBeforeBraceUpper.empty() && !isKotlinStructuralLeadLine(normalizedBeforeBraceUpper) &&
				    beforeBraceUpper.find(')') == std::string_view::npos && beforeBraceUpper.find(']') == std::string_view::npos)
					return currentLineIndex;
				return findRecentKotlinStructuralLeadLine(localLineIndex);
			}();
			const std::size_t recentCSharpLeadLine = [&]() noexcept -> std::size_t {
				if (language != MRSyntaxLanguage::CSharp || currentLast == std::string_view::npos || trimmed[currentLast] != '{') return currentLineIndex;
				const std::string_view beforeBraceUpper = trimView(upperLine.substr(0, currentLast));
				const std::string_view normalizedBeforeBraceUpper = normalizeCSharpStructuralLeadText(beforeBraceUpper);
				if (trimmed != "{" && !normalizedBeforeBraceUpper.empty() && !isCSharpStructuralLeadLine(normalizedBeforeBraceUpper) &&
				    beforeBraceUpper.find(')') == std::string_view::npos && beforeBraceUpper.find(']') == std::string_view::npos)
					return currentLineIndex;
				return findRecentCSharpStructuralLeadLine(localLineIndex);
			}();
		const std::size_t recentShellLeadLine = [&]() noexcept -> std::size_t {
			if (language != MRSyntaxLanguage::Bash && language != MRSyntaxLanguage::Zsh) return currentLineIndex;
			if (trimmed == "{" || upperLine == "THEN" || upperLine.ends_with(" THEN") || upperLine == "DO" || upperLine.ends_with(" DO")) return findRecentShellStructuralLeadLine(localLineIndex, trimmed, upperLine);
			return currentLineIndex;
		}();
		bool closedFenceThisLine = false;
		bool mrmacMacroActive = false;
		bool openSiblingContinuation = false;

		if (language == MRSyntaxLanguage::MRMAC)
			for (const MRFoldOpenBlock &block : openBlocks)
				if (block.sourceKind == MRFoldSourceKind::Macro) {
					mrmacMacroActive = true;
					break;
				}

		if (language == MRSyntaxLanguage::Systemd && nonEmpty && !isSystemdSectionHeader(trimmed) && !isSystemdCommentLine(trimmed))
			for (MRFoldOpenBlock &block : openBlocks)
				if (block.sourceKind == MRFoldSourceKind::Section) {
					block.lastContentLine = lineIndex;
					break;
				}

		if (nonEmpty && lineIndex > baseLineIndex) {
			std::size_t closerIndex = 0;
			while (!openBlocks.empty() && closerIndex < trimmed.size() && openBlocks.back().sourceKind == MRFoldSourceKind::Delimiter && openBlocks.back().closer != 0 &&
			       trimmed[closerIndex] == openBlocks.back().closer) {
				appendVisibleSpan(openBlocks.back(), (perlSiblingAfterLeadingCloser || javascriptSiblingAfterLeadingCloser || cLikeSiblingAfterLeadingCloser) ? lineIndex - 1 : lineIndex);
				openBlocks.pop_back();
				++closerIndex;
			}
			if (perlSiblingAfterLeadingCloser || javascriptSiblingAfterLeadingCloser || cLikeSiblingAfterLeadingCloser) openSiblingContinuation = true;
		}
		if (nonEmpty && lineIndex > baseLineIndex) {
			const std::size_t splitOffset = trailingSmartDedentSplitOffset(lineText, language);
			if (splitOffset != std::string_view::npos) {
				const std::string_view trailingDedent = trimView(std::string_view(lineText).substr(splitOffset));
				std::size_t closerIndex = 0;

				while (!openBlocks.empty() && closerIndex < trailingDedent.size() && openBlocks.back().sourceKind == MRFoldSourceKind::Delimiter && openBlocks.back().closer != 0 &&
				       trailingDedent[closerIndex] == openBlocks.back().closer) {
					appendVisibleSpan(openBlocks.back(), lineIndex);
					openBlocks.pop_back();
					++closerIndex;
				}
			}
		}

		if (mrmacMacroStart && lineIndex > baseLineIndex) {
			while (!openBlocks.empty()) {
				appendVisibleSpan(openBlocks.back(), lineIndex - 1);
				openBlocks.pop_back();
			}
		}
		if (mrmacMacroEnd && lineIndex > baseLineIndex) {
			while (!openBlocks.empty() && openBlocks.back().sourceKind != MRFoldSourceKind::Macro) {
				appendVisibleSpan(openBlocks.back(), lineIndex - 1);
				openBlocks.pop_back();
			}
			if (!openBlocks.empty() && openBlocks.back().sourceKind == MRFoldSourceKind::Macro) {
				appendVisibleSpan(openBlocks.back(), lineIndex);
				openBlocks.pop_back();
			}
			mrmacMacroActive = false;
		}
		if (language == MRSyntaxLanguage::MRMAC && mrmacMacroActive && mrmacElseLead) {
			while (!openBlocks.empty()) {
				const MRFoldOpenBlock &block = openBlocks.back();
				if (block.sourceKind == MRFoldSourceKind::Macro) break;
				appendVisibleSpan(block, lineIndex - 1);
				const int closedKind = block.languageBlockKind;
				openBlocks.pop_back();
				if (closedKind == kMRMACIfBlock) break;
			}
			openSiblingContinuation = true;
		}
		if (language == MRSyntaxLanguage::MRMAC && mrmacMacroActive && mrmacEndLead) {
			while (!openBlocks.empty()) {
				const MRFoldOpenBlock &block = openBlocks.back();
				if (block.sourceKind == MRFoldSourceKind::Macro) break;
				appendVisibleSpan(block, lineIndex);
				openBlocks.pop_back();
				break;
			}
		}
		if (language == MRSyntaxLanguage::Fish && fishConditionalLead) {
			while (!openBlocks.empty()) {
				const MRFoldOpenBlock &block = openBlocks.back();
				appendVisibleSpan(block, lineIndex - 1);
				const int closedKind = block.languageBlockKind;
				openBlocks.pop_back();
				if (closedKind == kFishIfBlock) break;
			}
			openSiblingContinuation = true;
		}
		if (language == MRSyntaxLanguage::Fish && fishCaseLead) {
			bool closedFishCase = false;
			while (!openBlocks.empty()) {
				const MRFoldOpenBlock &block = openBlocks.back();
				if (block.languageBlockKind == kFishSwitchBlock) break;
				appendVisibleSpan(block, lineIndex - 1);
				const int closedKind = block.languageBlockKind;
				openBlocks.pop_back();
				if (closedKind == kFishCaseBlock) {
					closedFishCase = true;
					break;
				}
			}
			openSiblingContinuation = closedFishCase;
		}
		if (language == MRSyntaxLanguage::Fish && fishEndLead) {
			while (!openBlocks.empty() && openBlocks.back().languageBlockKind == kFishCaseBlock) {
				appendVisibleSpan(openBlocks.back(), lineIndex - 1);
				openBlocks.pop_back();
			}
			if (!openBlocks.empty()) {
				appendVisibleSpan(openBlocks.back(), lineIndex);
				openBlocks.pop_back();
			}
		}
		if (language == MRSyntaxLanguage::Pascal && pascalElseLead) {
			while (!openBlocks.empty()) {
				const MRFoldOpenBlock &block = openBlocks.back();
				appendVisibleSpan(block, lineIndex - 1);
				const int closedKind = block.languageBlockKind;
				openBlocks.pop_back();
				if (closedKind == kPascalBlockConditional) break;
			}
			openSiblingContinuation = true;
		}
		if (language == MRSyntaxLanguage::Pascal && (pascalExceptLead || pascalFinallyLead)) {
			while (!openBlocks.empty()) {
				const MRFoldOpenBlock &block = openBlocks.back();
				appendVisibleSpan(block, lineIndex - 1);
				const int closedKind = block.languageBlockKind;
				openBlocks.pop_back();
				if (closedKind == kPascalBlockTry) break;
			}
			openSiblingContinuation = true;
		}
		if (language == MRSyntaxLanguage::Xml && xmlLeadingCloseTag && !openBlocks.empty() && openBlocks.back().languageBlockKind == kXmlTagBlock &&
		    openBlocks.back().xmlTagName == xmlLeadingCloseTagName) {
			appendVisibleSpan(openBlocks.back(), lineIndex);
			openBlocks.pop_back();
		}
		if (language == MRSyntaxLanguage::Latex && latexEndEnvironment && !openBlocks.empty()) {
			std::size_t matchingIndex = std::numeric_limits<std::size_t>::max();
			for (std::size_t index = openBlocks.size(); index-- > 0;) {
				const MRFoldOpenBlock &block = openBlocks[index];
				if (block.languageBlockKind == kLatexEnvironmentBlock) {
					if (block.xmlTagName == latexEndEnvironmentName) matchingIndex = index;
					break;
				}
				if (block.sourceKind != MRFoldSourceKind::Section) break;
			}
			if (matchingIndex != std::numeric_limits<std::size_t>::max()) {
				while (openBlocks.size() - 1 > matchingIndex) {
					appendVisibleSpan(openBlocks.back(), lineIndex - 1);
					openBlocks.pop_back();
				}
				appendVisibleSpan(openBlocks.back(), lineIndex);
				openBlocks.pop_back();
			}
		}
		while (!openBlocks.empty()) {
			const MRFoldOpenBlock block = openBlocks.back();
			bool closeBlock = false;
			std::size_t endLine = lineIndex;

			switch (block.sourceKind) {
				case MRFoldSourceKind::Fence:
					if (nonEmpty && lineIndex > block.startLine && isMarkdownFenceClose(trimmed, block.marker, block.markerLength)) {
						closeBlock = true;
						closedFenceThisLine = true;
					}
					break;
				case MRFoldSourceKind::Directive:
					if (nonEmpty && lineIndex > block.startLine &&
					    ((language == MRSyntaxLanguage::Perl && (perlPodEnd || perlPodStart)) ||
					     (language != MRSyntaxLanguage::Make && language != MRSyntaxLanguage::Perl && (isPreprocessorFoldEnd(trimmed) || preprocessorSibling)) ||
					     (language == MRSyntaxLanguage::Make && (isMakeDirectiveFoldEnd(trimmed) || makeDirectiveSibling)))) {
						closeBlock = true;
						if (language == MRSyntaxLanguage::Perl && perlPodStart) endLine = lineIndex - 1;
						if (preprocessorSibling || makeDirectiveSibling) {
							endLine = lineIndex - 1;
							openSiblingContinuation = true;
						}
					}
					break;
				case MRFoldSourceKind::Macro:
					if (nonEmpty && lineIndex > block.startLine && mrmacMacroStart) {
						closeBlock = true;
						endLine = lineIndex - 1;
					}
					break;
				case MRFoldSourceKind::Delimiter:
					if (language == MRSyntaxLanguage::Perl && perlSiblingLead && block.languageBlockKind == kPerlBlockConditional) {
						closeBlock = true;
						endLine = lineIndex - 1;
					}
					break;
				case MRFoldSourceKind::Indent:
					if (!nonEmpty || lineIndex <= block.startLine) break;
					if ((language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) && shellDedent) {
						if (shellSiblingLead && block.languageBlockKind == kShellBlockConditional) {
							closeBlock = true;
							endLine = lineIndex - 1;
							openSiblingContinuation = true;
						} else if (upperLine == "FI" && block.languageBlockKind == kShellBlockConditional) {
							closeBlock = true;
						} else if (upperLine == "DONE" && block.languageBlockKind == kShellBlockLoop) {
							closeBlock = true;
						} else if (upperLine == "ESAC" && block.languageBlockKind == kShellBlockCase) {
							closeBlock = true;
						}
					} else if (language == MRSyntaxLanguage::Python && pythonDedent) {
						closeBlock = true;
						endLine = lineIndex - 1;
						if (upperLine == "ELSE:" || upperLine == "FINALLY:" || upperLine == "EXCEPT:" || upperLine.starts_with("ELIF ") || upperLine.starts_with("CASE ") ||
						    upperLine.starts_with("EXCEPT ")) {
							openSiblingContinuation = true;
						}
					} else if (language == MRSyntaxLanguage::Perl && perlSiblingLead) {
						closeBlock = true;
						endLine = lineIndex - 1;
						openSiblingContinuation = true;
					} else if (language == MRSyntaxLanguage::Pascal) {
						if (pascalElseLead && block.languageBlockKind == kPascalBlockConditional) {
							closeBlock = true;
							endLine = lineIndex - 1;
							openSiblingContinuation = true;
						} else if ((pascalExceptLead || pascalFinallyLead) && block.languageBlockKind == kPascalBlockTry) {
							closeBlock = true;
							endLine = lineIndex - 1;
							openSiblingContinuation = true;
						} else if (pascalUntilLead && block.languageBlockKind == kPascalBlockRepeat) {
							closeBlock = true;
						} else if (pascalEndLead && block.languageBlockKind != kPascalBlockRepeat) {
							closeBlock = true;
						}
					} else if (language != MRSyntaxLanguage::MRMAC && language != MRSyntaxLanguage::Xml && currentIndent <= block.indent) {
						closeBlock = true;
						endLine = lineIndex - 1;
					}
					break;
				case MRFoldSourceKind::Section:
					if (headingLevel > 0 && headingLevel <= block.headingLevel && lineIndex > block.startLine) {
						closeBlock = true;
						if (language == MRSyntaxLanguage::Systemd && block.lastContentLine != std::numeric_limits<std::size_t>::max() && block.lastContentLine > block.startLine)
							endLine = block.lastContentLine;
						else
							endLine = lineIndex - 1;
					}
					break;
				case MRFoldSourceKind::Target:
					if (lineIndex > block.startLine && isNonEmptyNonRecipeMakeLine(lineText, trimmed)) {
						closeBlock = true;
						endLine = lineIndex - 1;
					}
					break;
				case MRFoldSourceKind::Generic:
				default:
					break;
			}

			if (!closeBlock) break;
			appendVisibleSpan(block, endLine);
			openBlocks.pop_back();
		}

		switch (language) {
			case MRSyntaxLanguage::C:
				if (isCLikeBraceFoldCandidateLine(trimmed))
					openBlock(MRFoldSourceKind::Delimiter, currentIndent, '}', 0, 0, 0, kLanguageBlockNone, recentCBraceStartLine, openSiblingContinuation);
				if (isPreprocessorFoldStart(trimmed) || preprocessorSibling) openBlock(MRFoldSourceKind::Directive, currentIndent);
				break;
			case MRSyntaxLanguage::Cpp:
				if (isCLikeStructuralBraceLead(trimmed, upperLine, previousTrimmed, previousUpperLine, previousPreviousTrimmed, previousPreviousUpperLine, language) ||
				    (trimmed == "{" && recentCLikeLeadLine != currentLineIndex))
					openBlock(MRFoldSourceKind::Delimiter, currentIndent, '}', 0, 0, 0, kLanguageBlockNone, recentCLikeLeadLine, openSiblingContinuation);
				if (isPreprocessorFoldStart(trimmed) || preprocessorSibling) openBlock(MRFoldSourceKind::Directive, currentIndent);
				break;
			case MRSyntaxLanguage::JavaScript:
				if (isCLikeStructuralBraceLead(trimmed, upperLine, previousTrimmed, previousUpperLine, previousPreviousTrimmed, previousPreviousUpperLine, language) ||
				    recentJavaScriptLeadLine != currentLineIndex)
					openBlock(MRFoldSourceKind::Delimiter, currentIndent, '}', 0, 0, 0, kLanguageBlockNone, recentJavaScriptLeadLine, openSiblingContinuation);
				break;
			case MRSyntaxLanguage::Swift:
				if (isCLikeStructuralBraceLead(trimmed, upperLine, previousTrimmed, previousUpperLine, previousPreviousTrimmed, previousPreviousUpperLine, language))
					openBlock(MRFoldSourceKind::Delimiter, currentIndent, '}', 0, 0, 0, kLanguageBlockNone, recentSwiftLeadLine, openSiblingContinuation);
				break;
				case MRSyntaxLanguage::Rust:
					if (isCLikeStructuralBraceLead(trimmed, upperLine, previousTrimmed, previousUpperLine, previousPreviousTrimmed, previousPreviousUpperLine, language))
						openBlock(MRFoldSourceKind::Delimiter, currentIndent, '}', 0, 0, 0, kLanguageBlockNone, recentRustLeadLine, openSiblingContinuation);
					break;
				case MRSyntaxLanguage::Go:
					if (isCLikeStructuralBraceLead(trimmed, upperLine, previousTrimmed, previousUpperLine, previousPreviousTrimmed, previousPreviousUpperLine, language))
						openBlock(MRFoldSourceKind::Delimiter, currentIndent, '}', 0, 0, 0, kLanguageBlockNone, recentGoLeadLine, openSiblingContinuation);
					break;
				case MRSyntaxLanguage::Kotlin:
					if (isCLikeStructuralBraceLead(trimmed, upperLine, previousTrimmed, previousUpperLine, previousPreviousTrimmed, previousPreviousUpperLine, language))
						openBlock(MRFoldSourceKind::Delimiter, currentIndent, '}', 0, 0, 0, kLanguageBlockNone, recentKotlinLeadLine, openSiblingContinuation);
					break;
				case MRSyntaxLanguage::CSharp:
					if (isCLikeStructuralBraceLead(trimmed, upperLine, previousTrimmed, previousUpperLine, previousPreviousTrimmed, previousPreviousUpperLine, language))
						openBlock(MRFoldSourceKind::Delimiter, currentIndent, '}', 0, 0, 0, kLanguageBlockNone, recentCSharpLeadLine, openSiblingContinuation);
					break;
				case MRSyntaxLanguage::Systemd:
				if (headingLevel > 0) openBlock(MRFoldSourceKind::Section, currentIndent, 0, 0, 0, headingLevel);
				break;
			case MRSyntaxLanguage::Json: {
				const std::size_t last = lastSignificantByte(trimmed);
				if (last != std::string_view::npos && (trimmed[last] == '{' || trimmed[last] == '['))
					openBlock(MRFoldSourceKind::Delimiter, currentIndent, matchingCloserForOpenDelimiter(trimmed[last]));
				break;
			}
			case MRSyntaxLanguage::Python:
				if (!upperLine.empty() && upperLine.back() == ':' && isPythonIndentLead(upperLine)) openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kLanguageBlockNone,
				                                                                                                  std::numeric_limits<std::size_t>::max(), openSiblingContinuation);
				break;
			case MRSyntaxLanguage::Bash:
			case MRSyntaxLanguage::Zsh:
				if (trailingBlockOpen)
					openBlock(MRFoldSourceKind::Delimiter, currentIndent, matchingCloserForOpenDelimiter(trimmed[lastSignificantByte(trimmed)]), 0, 0, 0, kLanguageBlockNone,
					          recentShellLeadLine, openSiblingContinuation);
				else if (isShellIndentLead(trimmed, upperLine))
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, shellIndentBlockKind(upperLine), recentShellLeadLine, openSiblingContinuation);
				break;
			case MRSyntaxLanguage::Fish:
				if (fishBlockKind == kFishBlockConditional)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kFishIfBlock, std::numeric_limits<std::size_t>::max(), openSiblingContinuation);
				else if (fishBlockKind == kFishBlockLoop)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kFishLoopBlock);
				else if (fishBlockKind == kFishBlockSwitch)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kFishSwitchBlock);
				else if (fishBlockKind == kFishBlockCase)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kFishCaseBlock, std::numeric_limits<std::size_t>::max(), openSiblingContinuation);
				else if (fishBlockKind == kFishBlockGeneric)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kFishGenericBlock);
				break;
			case MRSyntaxLanguage::Perl:
				if (perlPodStart)
					openBlock(MRFoldSourceKind::Directive, currentIndent);
				else {
					const int perlBlockKind = perlStructuredBlockKind(trimmed, upperLine);
					if (perlBlockKind != kPerlBlockNone)
						openBlock(MRFoldSourceKind::Delimiter, currentIndent, '}', 0, 0, 0, perlBlockKind, std::numeric_limits<std::size_t>::max(), openSiblingContinuation);
				}
				break;
			case MRSyntaxLanguage::Pascal:
				if (pascalBlockKind == kPascalBlockConditional)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kPascalBlockConditional, std::numeric_limits<std::size_t>::max(), openSiblingContinuation);
				else if (pascalBlockKind == kPascalBlockGeneric)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kPascalBlockGeneric);
				else if (pascalBlockKind == kPascalBlockRepeat)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kPascalBlockRepeat);
				else if (pascalBlockKind == kPascalBlockTry)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kPascalBlockTry, std::numeric_limits<std::size_t>::max(), openSiblingContinuation);
				break;
			case MRSyntaxLanguage::Xml:
				if (xmlLeadingOpenTag)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kXmlTagBlock, std::numeric_limits<std::size_t>::max(), openSiblingContinuation, xmlLeadingOpenTagName);
				break;
			case MRSyntaxLanguage::Markdown: {
				char marker = 0;
				std::size_t markerLength = 0;
				if (!closedFenceThisLine && markdownFenceMarker(trimmed, marker, markerLength)) openBlock(MRFoldSourceKind::Fence, currentIndent, 0, marker, markerLength);
				else if (headingLevel > 0)
					openBlock(MRFoldSourceKind::Section, currentIndent, 0, 0, 0, headingLevel);
				else if (isMarkdownBlockQuoteLead(trimmed, nextTrimmed) || isMarkdownListLead(lineText, trimmed, nextTrimmed, currentIndent, nextIndent))
					openBlock(MRFoldSourceKind::Indent, currentIndent);
				break;
			}
			case MRSyntaxLanguage::Latex:
				if (headingLevel > 0) openBlock(MRFoldSourceKind::Section, currentIndent, 0, 0, 0, headingLevel);
				if (latexBeginEnvironment)
					openBlock(MRFoldSourceKind::Directive, currentIndent, 0, 0, 0, 0, kLatexEnvironmentBlock, std::numeric_limits<std::size_t>::max(), false, latexBeginEnvironmentName);
				break;
			case MRSyntaxLanguage::Make:
				if (!isMakeRecipeLine(lineText) && isMakeTargetLine(trimmed) && !nextTrimmed.empty() && nextLineTextPtr != nullptr && isMakeRecipeLine(*nextLineTextPtr))
					openBlock(MRFoldSourceKind::Target, currentIndent);
				else if (isMakeDirectiveFoldStart(trimmed))
					openBlock(MRFoldSourceKind::Directive, currentIndent);
				break;
			case MRSyntaxLanguage::MRMAC:
				if (mrmacMacroStart) openBlock(MRFoldSourceKind::Macro, currentIndent);
				else if (mrmacMacroActive && mrmacIfLead)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kMRMACIfBlock);
				else if (mrmacMacroActive && mrmacElseLead)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kMRMACIfBlock, std::numeric_limits<std::size_t>::max(), true);
				else if (mrmacMacroActive && mrmacWhileLead)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, kMRMACWhileBlock);
				break;
			case MRSyntaxLanguage::PlainText:
			default:
				if (isIndentFoldLanguage(language) && indentOpens) openBlock(MRFoldSourceKind::Indent, currentIndent);
				break;
		}

		previousPreviousLineText = previousLineText;
		previousPreviousUpperLine = previousUpperLine;
		previousLineText = lineText;
		previousUpperLine = upperLine;
	}

	const std::size_t finalLine = baseLineIndex + lineTexts.size() - 1;
	for (const MRFoldOpenBlock &block : openBlocks)
		if (language == MRSyntaxLanguage::Latex && block.languageBlockKind == kLatexEnvironmentBlock)
			continue;
		else if (language == MRSyntaxLanguage::Systemd && block.sourceKind == MRFoldSourceKind::Section && block.lastContentLine != std::numeric_limits<std::size_t>::max() && block.lastContentLine > block.startLine)
			appendVisibleSpan(block, block.lastContentLine);
		else
			appendVisibleSpan(block, finalLine);
	return output;
}

} // namespace

std::vector<std::string> mrBuildViewportScanLineTextsParallel(const mr::editor::ReadSnapshot &snapshot, std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t focusTopLine,
                                                              std::size_t focusBottomLine) {
	return buildViewportScanLineTextsFromChunks(snapshot, scanTopLine, scanBottomLine, focusTopLine, focusBottomLine);
}

std::string mrBuildFoldTrainingAscii(const std::string &text, MRSyntaxLanguage language) {
	const std::vector<std::string> lineTexts = splitFoldTrainingLines(text);
	const MRFoldScanOutput scan = computeFoldSpansForLineTexts(lineTexts, 0, 0, lineTexts.size(), language, std::map<std::size_t, MRFoldSpan>());
	std::string output;
	auto branchContinuesAtSameLevel = [&scan](const MRFoldSpan &span) noexcept {
		for (const MRFoldSpan &candidate : scan.spans)
			if (candidate.siblingContinuation && candidate.level == span.level && candidate.startLine == span.endLine + 1) return true;
		return false;
	};

	for (std::size_t lineIndex = 0; lineIndex < lineTexts.size(); ++lineIndex) {
		std::vector<std::string> gutter(static_cast<std::size_t>(std::max(1, scan.visibleMaxLevel)), " ");

		for (const MRFoldSpan &span : scan.spans) {
			if (span.level >= gutter.size()) continue;
			if (lineIndex == span.startLine) gutter[span.level] = span.siblingContinuation ? "\xE2\x94\x9C" : "\xE2\x95\xAD";
			else if (lineIndex == span.endLine)
				gutter[span.level] = branchContinuesAtSameLevel(span) ? "\xE2\x94\x82" : "\xE2\x95\xB0";
			else if (lineIndex > span.startLine && lineIndex < span.endLine)
				gutter[span.level] = "\xE2\x94\x82";
		}
		char lineNumber[32];
		std::snprintf(lineNumber, sizeof(lineNumber), "%6zu", lineIndex + 1);
		output.append(lineNumber);
		output.push_back(' ');
		for (const std::string &cell : gutter)
			output.append(cell);
		output.append(" | ");
		output.append(lineTexts[lineIndex]);
		output.push_back('\n');
	}
	return output;
}

std::string mrBuildOutlineTrainingAscii(const std::string &text, MRSyntaxLanguage language) {
	const std::vector<std::string> lineTexts = splitFoldTrainingLines(text);
	const MRFoldScanOutput scan = computeFoldSpansForLineTexts(lineTexts, 0, 0, lineTexts.size(), language, std::map<std::size_t, MRFoldSpan>());
	return mrBuildOutlineTrainingAsciiForFoldSpans(lineTexts, scan.spans, language);
}

bool MRFileEditor::buildFoldOutlineSnapshot(const MROutlineRequest &request, MROutlineSnapshot &snapshot) const {
	const MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();
	std::size_t exactLineCount = 0;
	bool complete = false;

	snapshot.documentId = mBufferModel.documentId();
	snapshot.version = mBufferModel.version();
	snapshot.topLine = 0;
	snapshot.bottomLine = 0;
	snapshot.complete = false;
	snapshot.nodes.clear();
	snapshot.textPool.clear();
	if (visibleState.documentId != snapshot.documentId || visibleState.version != snapshot.version || visibleState.bottomLine <= visibleState.topLine) return false;
	snapshot.topLine = visibleState.topLine;
	snapshot.bottomLine = visibleState.bottomLine;
	if (mBufferModel.exactLineCountKnown()) {
		exactLineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
		complete = snapshot.topLine == 0 && snapshot.bottomLine >= exactLineCount;
	}
	if (!request.allowPartial && !complete) return false;
	return mrBuildFoldOutlineSnapshotFromFoldState(visibleState.language, snapshot.documentId, snapshot.version, visibleState.topLine, visibleState.bottomLine, complete,
	                                              visibleState.lineTexts, visibleState.spans, mBufferModel.readSnapshot(), request, snapshot);
}

bool MRFileEditor::requestCompleteFoldOutlineWarmup() {
	static constexpr std::size_t kCompleteOutlineFoldLineBudget = 20000;
	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const MRSyntaxLanguage language = mBufferModel.language();
	const MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();
	std::size_t lineCount = 0;

	if (!foldingPipelineEnabled()) return false;
	if (useApproximateLargeFileMetrics()) return false;
	if (!mBufferModel.exactLineCountKnown()) return false;
	lineCount = std::max<std::size_t>(1, mBufferModel.lineCount());
	if (lineCount > kCompleteOutlineFoldLineBudget) return false;
	if (visibleState.documentId == documentId && visibleState.version == version && visibleState.language == language && visibleState.topLine == 0 && visibleState.bottomLine >= lineCount) return true;
	scheduleFoldWarmupIfNeeded(0, lineCount, 0, lineCount, language);
	return true;
}

bool MRFileEditor::applyFoldWarmup(const mr::coprocessor::Payload &payload, std::size_t expectedVersion, std::uint64_t expectedTaskId) {
	MRFoldingDerivedState::WarmupState &warmupState = mFoldState.warmupState();
	MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();
	if (expectedTaskId == 0 || warmupState.taskId != expectedTaskId) return false;
	if (mBufferModel.documentId() != warmupState.documentId || mBufferModel.version() != expectedVersion) return false;
	const MRFoldWarmupPayload *foldWarmup = dynamic_cast<const MRFoldWarmupPayload *>(&payload);
	if (foldWarmup == nullptr) return false;
	if (mBufferModel.language() != foldWarmup->language) return false;

	visibleState.spans = foldWarmup->spans;
	visibleState.documentId = mBufferModel.documentId();
	visibleState.version = expectedVersion;
	visibleState.topLine = foldWarmup->scanTopLine;
	visibleState.bottomLine = foldWarmup->scanBottomLine;
	visibleState.language = foldWarmup->language;
	visibleState.lineTexts = foldWarmup->lineTexts;
	visibleState.displayLevels.clear();
	visibleState.gutterColumns = std::max(1, foldWarmup->visibleGutterColumns);
	clearFoldWarmupTask(expectedTaskId);
	drawView();
	return true;
}

void MRFileEditor::clearFoldWarmupTask(std::uint64_t expectedTaskId) noexcept {
	MRFoldingDerivedState::WarmupState &warmupState = mFoldState.warmupState();
	if (expectedTaskId != 0 && warmupState.taskId != expectedTaskId) return;
	if (warmupState.taskId == 0) return;
	mFoldState.clearWarmupState();
	notifyWindowTaskStateChanged();
}

bool MRFileEditor::shouldTraceLargeFileWarmupDiagnostics() const noexcept {
	return mBufferModel.document().length() >= kLargeFileWarmupTraceBytes;
}

void MRFileEditor::traceLargeFileWarmup(std::string &slot, const char *stage, std::string detail) {
	if (!shouldTraceLargeFileWarmupDiagnostics()) return;
	std::string line = "Large-file warmup ";
	line += stage != nullptr ? stage : "unknown";
	line += ": doc=" + std::to_string(mBufferModel.documentId());
	line += " ver=" + std::to_string(mBufferModel.version());
	if (hasPersistentFileName()) line += " file='" + std::string(fileName) + "'";
	if (!detail.empty()) line += " " + detail;
	if (line == slot) return;
	slot = line;
	mrLogMessage(line);
}


void MRFileEditor::scheduleFoldWarmupIfNeeded(std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t topLine, std::size_t requestBottomLine, MRSyntaxLanguage language) {
	if (!foldingPipelineEnabled()) {
		if (mFoldState.warmupState().taskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mFoldState.warmupState().taskId));
			clearFoldWarmupTask(mFoldState.warmupState().taskId);
		}
		invalidateFoldCache();
		return;
	}
	MRFoldingDerivedState::WarmupState &warmupState = mFoldState.warmupState();
	MRFoldingDerivedState::VisibleState &visibleState = mFoldState.visibleState();
	if (scanBottomLine <= scanTopLine) {
		clearFoldWarmupTask();
		return;
	}

	const std::size_t docId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	if (warmupState.taskId != 0 && warmupState.documentId == docId && warmupState.version == version && warmupState.language == language && scanTopLine >= warmupState.topLine &&
	    scanBottomLine <= warmupState.bottomLine)
		return;

	MRTextBufferModel::ReadSnapshot snapshot = mBufferModel.readSnapshot();

	std::uint64_t previousTaskId = warmupState.taskId;
	if (previousTaskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(previousTaskId));
	warmupState.documentId = docId;
	warmupState.version = version;
	warmupState.topLine = scanTopLine;
	warmupState.bottomLine = scanBottomLine;
	warmupState.language = language;
	std::string foldTaskLabel = std::string(foldWarmupTaskLabel()) + " lines " + std::to_string(scanTopLine + 1) + "-" + std::to_string(scanBottomLine);
	warmupState.taskId = mr::coprocessor::globalCoprocessor().submit(
	    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::FoldWarmup, docId, version, foldTaskLabel,
	    [snapshot, language, scanTopLine, scanBottomLine, topLine, requestBottomLine, closedFoldSpans = mFoldState.closedFoldSpans()](const mr::coprocessor::TaskInfo &info,
	                                                                                                                                                                               std::stop_token stopToken) {
		    mr::coprocessor::Result result;
		    auto shouldStop = [&]() noexcept { return stopToken.stop_requested() || info.cancelRequested(); };
		    result.task = info;
		    if (shouldStop()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }

		    std::vector<std::string> lineTexts = mrBuildViewportScanLineTextsParallel(snapshot, scanTopLine, scanBottomLine, topLine, requestBottomLine);
		    if (shouldStop()) {
			    result.status = mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    }

		    const std::size_t actualBottomLine = scanTopLine + lineTexts.size();
		    MRFoldScanOutput scan = computeFoldSpansForLineTexts(lineTexts, scanTopLine, topLine, requestBottomLine, language, closedFoldSpans);
		    result.status = mr::coprocessor::TaskStatus::Completed;
		    result.payload = std::make_shared<MRFoldWarmupPayload>(language, scanTopLine, actualBottomLine, scan.visibleMaxLevel, std::move(lineTexts), std::move(scan.spans));
		    return result;
	    });
	if (shouldTraceLargeFileWarmupDiagnostics()) {
		std::string detail = "action=schedule task=" + std::to_string(warmupState.taskId) + " top=" + std::to_string(scanTopLine) + " bottom=" + std::to_string(scanBottomLine) + " cached=" +
		                     std::to_string(visibleState.topLine) + "/" + std::to_string(visibleState.bottomLine);
		traceLargeFileWarmup(mLastComputeWarmupTrace, "fold", std::move(detail));
	}
	if (warmupState.taskId != previousTaskId) notifyWindowTaskStateChanged();
}


#if defined(__clang__)
#pragma clang diagnostic pop
#endif
