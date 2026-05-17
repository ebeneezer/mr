#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../../app/MREditorApp.hpp"

#include <chrono>
#include <ctime>
#include <future>
#include <sstream>
#include <thread>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif

namespace {

static constexpr std::size_t kLargeFileWarmupTraceBytes = static_cast<std::size_t>(8) * 1024 * 1024;
static constexpr auto kSlowNavigationTraceThreshold = std::chrono::microseconds(2000);

bool isIndentWhitespace(char ch) noexcept {
	return ch == ' ' || ch == '\t';
}

bool isStatefulSyntaxLanguage(MRSyntaxLanguage language) noexcept {
	return language == MRSyntaxLanguage::MRMAC || language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp || language == MRSyntaxLanguage::JavaScript || language == MRSyntaxLanguage::Python ||
	       language == MRSyntaxLanguage::Markdown || language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh || language == MRSyntaxLanguage::Fish || language == MRSyntaxLanguage::Perl || language == MRSyntaxLanguage::Swift || language == MRSyntaxLanguage::Rust ||
	       language == MRSyntaxLanguage::Go || language == MRSyntaxLanguage::Pascal || language == MRSyntaxLanguage::Xml;
}

static constexpr auto kLargeFileViewportWarmupDebounce = std::chrono::milliseconds(180);
static constexpr auto kLargeFileMiniMapEditDebounce = std::chrono::milliseconds(500);

std::string directProbeTimestamp() {
	std::array<char, 32> buffer{};
	const std::time_t now = std::time(nullptr);
	const std::tm *tmNow = std::localtime(&now);

	if (tmNow == nullptr) return std::string("--:--:--");
	if (std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", tmNow) == 0) return std::string("--:--:--");
	return std::string(buffer.data());
}

void appendDirectProbeLog(std::string_view message) {
	std::ofstream out("misc/mr.log", std::ios::out | std::ios::app | std::ios::binary);

	if (!out) return;
	out << "[" << directProbeTimestamp() << "] " << message << '\n';
	out.flush();
}

bool quitTailTraceActive() noexcept {
	const auto *app = dynamic_cast<const MREditorApp *>(TProgram::application);
	return app != nullptr && app->quitPrepared();
}

template <class Duration> long long traceMicros(Duration duration) {
	return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
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

bool isYamlIndentLead(std::string_view trimmed) noexcept {
	const std::string_view normalized = trimView(trimmed);
	if (normalized.empty() || normalized.starts_with("#")) return false;
	if (normalized == "-") return true;
	if ((normalized.front() == '-' || normalized.front() == '?') && normalized.size() > 1 && std::isspace(static_cast<unsigned char>(normalized[1])) != 0) {
		const std::size_t last = lastSignificantByte(normalized);
		return last != std::string_view::npos && (normalized[last] == ':' || normalized[last] == '|' || normalized[last] == '>');
	}
	const std::size_t last = lastSignificantByte(normalized);
	return last != std::string_view::npos && (normalized[last] == ':' || normalized[last] == '|' || normalized[last] == '>');
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

bool isSystemdDirectiveLine(std::string_view trimmed) noexcept {
	const std::string_view normalized = trimView(trimmed);
	if (normalized.empty() || isSystemdCommentLine(normalized) || isSystemdSectionHeader(normalized)) return false;
	const std::size_t eq = normalized.find('=');
	if (eq == std::string_view::npos || eq == 0) return false;
	for (std::size_t i = 0; i < eq; ++i) {
		const char ch = normalized[i];
		if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-') continue;
		return false;
	}
	return true;
}

bool isSystemdContinuationLead(std::string_view trimmed) noexcept {
	const std::string_view normalized = trimView(trimmed);
	if (!isSystemdDirectiveLine(normalized)) return false;
	const std::size_t last = lastSignificantByte(normalized);
	return last != std::string_view::npos && normalized[last] == '\\';
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
		case MRSyntaxLanguage::Pascal:
			return isPascalCommentLikeLine(trimmed);
		case MRSyntaxLanguage::Systemd:
			return isSystemdCommentLine(trimmed);
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

}

std::string MRFileEditor::preferredIndentFill() const {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const int targetColumn = resolvedEditFormatIndentColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, mPreferredIndentColumn);

	return buildEditIndentFill(settings, 1, targetColumn, configuredTabExpandSetting());
}
bool MRFileEditor::applyCurrentLineLeadingIndent(int targetColumn) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const std::size_t cursor = cursorOffset();
	const std::size_t lineStart = lineStartOffset(cursor);
	const std::string lineText = mBufferModel.lineText(lineStart);
	std::size_t indentBytes = 0;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "live-smart-dedent");
	std::string replacement;
	std::size_t newCursor = cursor;

	while (indentBytes < lineText.size() && (lineText[indentBytes] == ' ' || lineText[indentBytes] == '\t'))
		++indentBytes;
	replacement = buildEditIndentFill(settings, 1, std::max(1, targetColumn), configuredTabExpandSetting());
	if (indentBytes == replacement.size() && lineText.compare(0, indentBytes, replacement) == 0) return false;
	transaction.replace(MRTextBufferModel::Range(lineStart, lineStart + indentBytes), replacement);
	if (cursor <= lineStart + indentBytes) newCursor = lineStart + replacement.size();
	else
		newCursor = cursor - indentBytes + replacement.size();
	return applyStagedTransaction(transaction, newCursor, newCursor, newCursor, true).applied();
}

bool MRFileEditor::replaceCurrentLineText(const std::string &text) {
	std::size_t start = mBufferModel.lineStart(mBufferModel.cursor());
	std::size_t end = mBufferModel.lineEnd(mBufferModel.cursor());
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-current-line");

	if (mReadOnly) return false;
	transaction.replace(MRTextBufferModel::Range(start, end), text);
	return applyStagedTransaction(transaction, start, start, start, true).applied();
}

bool MRFileEditor::centerCurrentLine(int leftMargin, int rightMargin) {
	std::string text;
	std::string trimmed;
	const int safeLeftMargin = std::max(1, leftMargin);
	const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
	int contentWidth = 0;
	int padWidth = 0;

	if (mReadOnly) return false;
	text = lineTextAtOffset(cursorOffset());
	trimmed = trimAscii(text);
	if (trimmed.empty()) return replaceCurrentLineText(std::string());
	contentWidth = displayWidthForText(trimmed, configuredEditSetupSettings());
	padWidth = std::max(safeLeftMargin - 1, ((safeRightMargin - contentWidth) / 2));
	return replaceCurrentLineText(std::string(static_cast<std::size_t>(padWidth), ' ') + trimmed);
}

bool MRFileEditor::copyCharFromLineAbove() {
	const std::size_t cursor = cursorOffset();
	const std::size_t currentLineStart = lineStartOffset(cursor);
	const std::size_t previousLineStart = prevLineOffset(currentLineStart);
	const std::size_t previousLineEnd = lineEndOffset(previousLineStart);
	const int targetColumn = charColumn(currentLineStart, cursor);
	const std::size_t sourceOffset = charPtrOffset(previousLineStart, targetColumn);
	char ch = '\0';

	if (mReadOnly || currentLineStart == previousLineStart || sourceOffset >= previousLineEnd) return false;
	ch = charAtOffset(sourceOffset);
	if (ch == '\0' || ch == '\r' || ch == '\n') return false;
	return insertBufferText(std::string(1, ch));
}

bool MRFileEditor::deleteCharsAtCursor(int count) {
	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "delete-chars-at-cursor");

	if (mReadOnly) return false;
	if (count <= 0) return true;
	for (int i = 0; i < count && end < mBufferModel.length(); ++i)
		end = nextCharOffset(end);
	if (end <= start) return true;
	transaction.erase(MRTextBufferModel::Range(start, end));
	return applyStagedTransaction(transaction, start, start, start, true).applied();
}

bool MRFileEditor::deleteCurrentLineText() {
	std::size_t start = mBufferModel.lineStart(mBufferModel.cursor());
	std::size_t end = mBufferModel.nextLine(mBufferModel.cursor());
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "delete-current-line");

	if (mReadOnly) return false;
	transaction.erase(MRTextBufferModel::Range(start, end));
	return applyStagedTransaction(transaction, start, start, start, true).applied();
}

bool MRFileEditor::replaceWholeBuffer(const std::string &text, std::size_t cursorPos) {
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-whole-buffer");

	if (mReadOnly) return false;
	transaction.setText(text);
	cursorPos = std::min(cursorPos, text.size());
	return applyStagedTransaction(transaction, cursorPos, cursorPos, cursorPos, true).applied();
}

bool MRFileEditor::syncAfterCommittedDocument(std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet) {
	const MRTextBufferModel::Document &document = mBufferModel.document();
	const bool suppressLineIndexWarmup = changeSet != nullptr && changeSet->changed && useApproximateLargeFileMetrics();
	const bool preserveStaleMiniMapDuringEdit = changeSet != nullptr && changeSet->changed && useApproximateLargeFileMetrics();
	const bool pieceTableOnly = pieceTableOnlyPhaseActive();
	const bool miniMapEnabled = miniMapPipelineEnabled();

	cursorPos = std::min(cursorPos, document.length());
	selStart = std::min(selStart, document.length());
	selEnd = std::min(selEnd, document.length());
	if (selEnd < selStart) std::swap(selStart, selEnd);

	invalidateSaveNormalizationCache();
	resetSyntaxWarmupState(changeSet == nullptr || !changeSet->changed);
	if (changeSet != nullptr && changeSet->changed) invalidateSyntaxCacheFromLineStart(mBufferModel.lineStart(changeSet->touchedRange.start));
	if (mFoldState.warmupState().taskId != 0) {
		static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mFoldState.warmupState().taskId));
		clearFoldWarmupTask(mFoldState.warmupState().taskId);
	}
	if (changeSet != nullptr && changeSet->changed) {
		mFoldState.clearClosedFolds();
	}
	invalidateFoldCache(changeSet != nullptr && changeSet->changed);
	if (!miniMapEnabled) {
		const std::uint64_t cancelledMiniMapTaskId = mMiniMapState.renderer().pendingWarmupTaskId();
		mMiniMapState.clearLastEditAt();
		mMiniMapState.clearOverlayCache();
		if (cancelledMiniMapTaskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledMiniMapTaskId));
			applyMiniMapSignals(mMiniMapState.renderer().clearWarmupTask(cancelledMiniMapTaskId));
		}
	} else if (preserveStaleMiniMapDuringEdit) {
		const std::uint64_t cancelledMiniMapTaskId = mMiniMapState.renderer().pendingWarmupTaskId();

		mMiniMapState.setLastEditAt(std::chrono::steady_clock::now());
		if (cancelledMiniMapTaskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledMiniMapTaskId));
			applyMiniMapSignals(mMiniMapState.renderer().clearWarmupTask(cancelledMiniMapTaskId));
		}
	} else {
		mMiniMapState.clearLastEditAt();
		mMiniMapState.clearOverlayCache();
	}
	if (miniMapEnabled) applyMiniMapSignals(mMiniMapState.renderer().invalidate(false, mBufferModel.documentId()));
	refreshSyntaxContext();
	cursorPos = canonicalCursorOffset(cursorPos);
	selStart = canonicalCursorOffset(selStart);
	selEnd = canonicalCursorOffset(selEnd);
	mBufferModel.setCursorAndSelection(cursorPos, selStart, selEnd);
	syncDisplayedCursorColumnFromCursor(false);
	mBufferModel.setModified(modifiedState);
	if (changeSet == nullptr || changeSet->changed) mFindMarkerRanges.clear();
	if (!modifiedState) clearDirtyRanges();
	else if (changeSet != nullptr && changeSet->changed) {
		remapDirtyRangesForAppliedChange(*changeSet);
		addDirtyRange(changeSet->touchedRange);
	}
	mSelectionAnchor = selStart;
	if (pieceTableOnly) {
		const std::uint64_t cancelledTaskId = mLineIndexWarmupTaskId;
		mSuppressLargeFileLineIndexWarmup = true;
		if (cancelledTaskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
			clearLineIndexWarmupTask(cancelledTaskId);
		}
	} else if (suppressLineIndexWarmup) {
		const std::uint64_t cancelledTaskId = mLineIndexWarmupTaskId;
		mSuppressLargeFileLineIndexWarmup = true;
		if (cancelledTaskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(cancelledTaskId));
			clearLineIndexWarmupTask(cancelledTaskId);
		}
	} else
		mSuppressLargeFileLineIndexWarmup = false;
	updateMetrics();
	if (!pieceTableOnly && !mSuppressLargeFileLineIndexWarmup) scheduleLineIndexWarmupIfNeeded();
	if (syntaxPipelineEnabled()) scheduleSyntaxWarmupIfNeeded();
	scheduleSaveNormalizationWarmupIfNeeded();
	ensureCursorVisible(false);
	updateIndicator();
	drawView();
	return true;
}

bool MRFileEditor::adoptCommittedDocument(const MRTextBufferModel::Document &document, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState, const MRTextBufferModel::DocumentChangeSet *changeSet) {
	mBufferModel.document() = document;
	return syncAfterCommittedDocument(cursorPos, selStart, selEnd, modifiedState, changeSet);
}

MRTextBufferModel::CommitResult MRFileEditor::applyStagedTransaction(const MRTextBufferModel::StagedTransaction &transaction, std::size_t cursorPos, std::size_t selStart, std::size_t selEnd, bool modifiedState) {
	pushUndoSnapshot();
	MRTextBufferModel::CommitResult result = mBufferModel.tryApplyStagedTransaction(transaction);

	if (result.applied()) syncAfterCommittedDocument(cursorPos, selStart, selEnd, modifiedState, &result.change);
	else
		mBufferModel.popUndoSnapshot();
	return result;
}

bool MRFileEditor::newLineWithIndent(const std::string &fill) {
	const bool applied = insertBufferText(std::string("\n") + fill);
	if (applied) applyLiveSmartDedentAfterTextInput("");
	return applied;
}

int MRFileEditor::leadingIndentColumnForLine(std::size_t lineStart) const noexcept {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	std::size_t index = 0;
	int visualColumn = 0;

	while (index < line.size()) {
		if (line[index] != ' ' && line[index] != '\t') break;
		std::size_t next = index;
		std::size_t width = 0;
		if (!nextDisplayChar(line, next, width, visualColumn, settings)) break;
		visualColumn += static_cast<int>(width);
		index = next;
	}
	return visualColumn + 1;
}

int MRFileEditor::inferredShellIndentStepColumns(std::size_t lineStart, const MREditSetupSettings &settings) const noexcept {
	const int baseColumn = leadingIndentColumnForLine(lineStart);
	const std::string currentLineText = mBufferModel.lineText(lineStart);
	const std::size_t currentIndentLength = leadingIndentBytes(currentLineText);
	const std::string_view currentIndent(currentLineText.data(), currentIndentLength);
	std::size_t currentLineStart = lineStart;

	while (currentLineStart > 0) {
		const std::size_t previousLineStart = prevLineOffset(currentLineStart);
		if (previousLineStart == currentLineStart) break;
		currentLineStart = previousLineStart;

		const std::string previousLineText = mBufferModel.lineText(previousLineStart);
		if (trimView(previousLineText).empty()) continue;

		const int previousColumn = leadingIndentColumnForLine(previousLineStart);
		if (previousColumn < baseColumn) {
			const std::size_t previousIndentLength = leadingIndentBytes(previousLineText);
			const std::string_view previousIndent(previousLineText.data(), previousIndentLength);

			if (currentIndent.starts_with(previousIndent)) {
				const std::string_view stepFill = currentIndent.substr(previousIndent.size());
				if (!stepFill.empty()) {
					TStringView fill(stepFill.data(), stepFill.size());
					std::size_t index = 0;
					int visualColumn = std::max(0, previousColumn - 1);

					while (index < fill.size()) {
						std::size_t next = index;
						std::size_t width = 0;
						if (!nextDisplayChar(fill, next, width, visualColumn, settings)) break;
						visualColumn += static_cast<int>(width);
						index = next;
					}
					const int stepColumns = visualColumn - std::max(0, previousColumn - 1);
					if (stepColumns > 0) return stepColumns;
				}
			}
			return std::max(1, baseColumn - previousColumn);
		}
	}

	if (!settings.tabExpand) {
		const int nextColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
		return std::max(1, nextColumn - baseColumn);
	}
	return 4;
}

std::string MRFileEditor::automaticIndentFillForCursor() const {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const std::size_t lineStart = lineStartOffset(cursorOffset());
	const int targetColumn = leadingIndentColumnForLine(lineStart);

	return buildEditIndentFill(settings, 1, targetColumn, configuredTabExpandSetting());
}

std::string MRFileEditor::smartIndentFillForCursor() {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const MRUiIndentStyle uiIndentStyle = configuredUiIndentStyle();
	const std::size_t cursor = cursorOffset();
	const std::size_t lineStart = lineStartOffset(cursor);
	const int baseColumn = leadingIndentColumnForLine(lineStart);
	int targetColumn = baseColumn;
	std::string lineText = mBufferModel.lineText(lineStart);
	std::size_t cursorInLine = cursor > lineStart ? std::min(cursor - lineStart, lineText.size()) : 0;
	std::string_view beforeCursor(lineText.data(), cursorInLine);
	std::string_view trimmedBeforeCursor = trimView(beforeCursor);
	std::string previousLineText;
	std::string previousPreviousLineText;
	std::string_view previousTrimmed;
	std::string previousUpperLine;
	std::string previousPreviousUpperLine;
	const MRSyntaxLanguage language = mBufferModel.language();
	const bool smartEnabled = upperAscii(settings.indentStyle) == "SMART";
	const bool neutralAutoScratchIndent = mBufferModel.languageAutomatic() && !hasPersistentFileName();
	const auto braceIndentStepColumns = [&]() noexcept {
		switch (uiIndentStyle) {
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
	};
	const auto braceIndentedNextLine = [&]() noexcept {
		return uiIndentStyle == MRUiIndentStyle::KandR || uiIndentStyle == MRUiIndentStyle::KandR4 || uiIndentStyle == MRUiIndentStyle::Gnome || uiIndentStyle == MRUiIndentStyle::Whitesmiths;
	};
	const auto bodyAlignsWithBraceLine = [&]() noexcept { return uiIndentStyle == MRUiIndentStyle::Whitesmiths; };
	const auto braceIndentedColumn = [&](int column) noexcept { return column + braceIndentStepColumns(); };

	if (lineStart > 0) {
		const std::size_t previousLineStart = lineStartOffset(lineStart - 1);
		previousLineText = mBufferModel.lineText(previousLineStart);
		previousTrimmed = trimView(previousLineText);
		previousUpperLine = upperAscii(std::string(previousTrimmed));
		if (previousLineStart > 0) {
			const std::size_t previousPreviousLineStart = lineStartOffset(previousLineStart - 1);
			previousPreviousLineText = mBufferModel.lineText(previousPreviousLineStart);
			previousPreviousUpperLine = upperAscii(std::string(trimView(previousPreviousLineText)));
		}
	}

	if (!smartEnabled) return buildEditIndentFill(settings, 1, targetColumn, configuredTabExpandSetting());
	if (neutralAutoScratchIndent) return automaticIndentFillForCursor();
	if (language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp) {
		const std::size_t last = lastSignificantByte(beforeCursor);
		if (trimmedBeforeCursor == "{")
			targetColumn = bodyAlignsWithBraceLine() ? baseColumn : braceIndentedColumn(baseColumn);
		else if (last != std::string_view::npos && beforeCursor[last] == '{' &&
		         isCLikeStructuralBraceLead(trimmedBeforeCursor, upperAscii(std::string(trimmedBeforeCursor)), previousTrimmed, previousUpperLine, trimView(previousPreviousLineText),
		                               previousPreviousUpperLine, language))
			targetColumn = braceIndentedColumn(baseColumn);
		else if (isCLikeStructuralLeadLine(trimmedBeforeCursor, upperAscii(std::string(trimmedBeforeCursor)), language))
			targetColumn = braceIndentedNextLine() ? braceIndentedColumn(baseColumn) : baseColumn;
	} else if (language == MRSyntaxLanguage::JavaScript || language == MRSyntaxLanguage::Json) {
		const std::size_t last = lastSignificantByte(beforeCursor);
		if (language == MRSyntaxLanguage::JavaScript && trimmedBeforeCursor == "{")
			targetColumn = bodyAlignsWithBraceLine() ? baseColumn : braceIndentedColumn(baseColumn);
		else if (language == MRSyntaxLanguage::JavaScript && last != std::string_view::npos && beforeCursor[last] == '{' &&
		         isCLikeStructuralBraceLead(trimmedBeforeCursor, upperAscii(std::string(trimmedBeforeCursor)), previousTrimmed, previousUpperLine, trimView(previousPreviousLineText),
		                               previousPreviousUpperLine, language))
			targetColumn = braceIndentedColumn(baseColumn);
		else if (language == MRSyntaxLanguage::JavaScript &&
		         (isCLikeStructuralLeadLine(trimmedBeforeCursor, upperAscii(std::string(trimmedBeforeCursor)), language) || isJavaScriptArrowFunctionLeadLine(trimmedBeforeCursor)))
			targetColumn = braceIndentedNextLine() ? braceIndentedColumn(baseColumn) : baseColumn;
		else if (language == MRSyntaxLanguage::Json && last != std::string_view::npos && (beforeCursor[last] == '{' || beforeCursor[last] == '['))
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Swift) {
		const std::string upperLine = upperAscii(std::string(trimmedBeforeCursor));
		const std::size_t last = lastSignificantByte(beforeCursor);
		const auto isSwiftCurrentLead = [&]() noexcept {
			return isSwiftStructuralLeadLine(upperLine) || isSwiftAccessorLeadLine(upperLine) || isSwiftPropertyBlockLeadLine(trimmedBeforeCursor, upperLine);
		};
		const auto hasRecentSwiftStructuralLead = [&]() {
			std::size_t probeLineStart = lineStart;
			int scannedNonEmptyLines = 0;
			while (probeLineStart > 0 && scannedNonEmptyLines < 12) {
				const std::size_t previousLineStart = lineStartOffset(probeLineStart - 1);
				if (previousLineStart == probeLineStart) break;
				probeLineStart = previousLineStart;

				const std::string candidateLine = mBufferModel.lineText(previousLineStart);
				const std::string_view candidateTrimmed = trimView(candidateLine);
				if (candidateTrimmed.empty() || isSwiftCommentLikeLine(candidateTrimmed)) continue;

				++scannedNonEmptyLines;
				const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
				if (isSwiftStructuralLeadLine(candidateUpper) || isSwiftAccessorLeadLine(candidateUpper) || isSwiftPropertyBlockLeadLine(candidateTrimmed, candidateUpper)) return true;
				if (lastSignificantByte(candidateTrimmed) != std::string_view::npos && candidateTrimmed.back() == '{') return false;
			}
			return false;
		};
		if (trimmedBeforeCursor == "{")
			targetColumn = bodyAlignsWithBraceLine() ? baseColumn : braceIndentedColumn(baseColumn);
		else if (last != std::string_view::npos && beforeCursor[last] == '{' &&
		         (isCLikeStructuralBraceLead(trimmedBeforeCursor, upperLine, previousTrimmed, previousUpperLine, trimView(previousPreviousLineText), previousPreviousUpperLine, language) ||
		          hasRecentSwiftStructuralLead()))
			targetColumn = braceIndentedColumn(baseColumn);
		else if (isSwiftCurrentLead())
			targetColumn = braceIndentedNextLine() ? braceIndentedColumn(baseColumn) : baseColumn;
	} else if (language == MRSyntaxLanguage::Rust || language == MRSyntaxLanguage::Go) {
		const std::string upperLine = upperAscii(std::string(trimmedBeforeCursor));
		const std::size_t last = lastSignificantByte(beforeCursor);
		if (trimmedBeforeCursor == "{")
			targetColumn = bodyAlignsWithBraceLine() ? baseColumn : braceIndentedColumn(baseColumn);
		else if (last != std::string_view::npos && beforeCursor[last] == '{' &&
		         isCLikeStructuralBraceLead(trimmedBeforeCursor, upperLine, previousTrimmed, previousUpperLine, trimView(previousPreviousLineText),
		                               previousPreviousUpperLine, language))
			targetColumn = braceIndentedColumn(baseColumn);
		else if ((language == MRSyntaxLanguage::Rust && isRustStructuralLeadLine(upperLine)) || (language == MRSyntaxLanguage::Go && isGoStructuralLeadLine(upperLine)))
			targetColumn = braceIndentedNextLine() ? braceIndentedColumn(baseColumn) : baseColumn;
	} else if (language == MRSyntaxLanguage::Python) {
		const std::string upperLine = upperAscii(std::string(trimmedBeforeCursor));
		if (!upperLine.empty() && upperLine.back() == ':' && isPythonIndentLead(upperLine))
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Bash) {
		const std::string upperLine = upperAscii(std::string(trimmedBeforeCursor));
		if (isShellIndentLead(trimmedBeforeCursor, upperLine)) {
			std::size_t nextLineStart = lineStart;
			while (nextLineStart < mBufferModel.length()) {
				const std::size_t candidateLineStart = nextLineOffset(nextLineStart);
				if (candidateLineStart <= nextLineStart) break;
				nextLineStart = candidateLineStart;

				const std::string candidateLineText = mBufferModel.lineText(candidateLineStart);
				const std::string_view candidateTrimmed = trimView(candidateLineText);
				if (candidateTrimmed.empty()) continue;

				const int candidateColumn = leadingIndentColumnForLine(candidateLineStart);
				if (candidateColumn > baseColumn) {
					targetColumn = candidateColumn;
					break;
				}
				if (candidateColumn <= baseColumn) break;
			}
			if (targetColumn == baseColumn) targetColumn = baseColumn + inferredShellIndentStepColumns(lineStart, settings);
		}
	} else if (language == MRSyntaxLanguage::Zsh) {
		const std::string upperLine = upperAscii(std::string(trimmedBeforeCursor));
		if (isShellIndentLead(trimmedBeforeCursor, upperLine)) {
			std::size_t nextLineStart = lineStart;
			while (nextLineStart < mBufferModel.length()) {
				const std::size_t candidateLineStart = nextLineOffset(nextLineStart);
				if (candidateLineStart <= nextLineStart) break;
				nextLineStart = candidateLineStart;

				const std::string candidateLineText = mBufferModel.lineText(candidateLineStart);
				const std::string_view candidateTrimmed = trimView(candidateLineText);
				if (candidateTrimmed.empty() || candidateTrimmed.starts_with("#")) continue;

				const int candidateColumn = leadingIndentColumnForLine(candidateLineStart);
				if (candidateColumn > baseColumn) {
					targetColumn = candidateColumn;
					break;
				}
				break;
			}
			if (targetColumn == baseColumn)
				targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
		}
	} else if (language == MRSyntaxLanguage::Fish) {
		const std::string upperLine = upperAscii(std::string(trimmedBeforeCursor));
		if (fishIndentBlockKind(upperLine) != kFishBlockNone) {
			std::size_t nextLineStart = lineStart;
			while (nextLineStart < mBufferModel.length()) {
				const std::size_t candidateLineStart = nextLineOffset(nextLineStart);
				if (candidateLineStart <= nextLineStart) break;
				nextLineStart = candidateLineStart;

				const std::string candidateLineText = mBufferModel.lineText(candidateLineStart);
				const std::string_view candidateTrimmed = trimView(candidateLineText);
				if (candidateTrimmed.empty() || candidateTrimmed.starts_with("#")) continue;

				const int candidateColumn = leadingIndentColumnForLine(candidateLineStart);
				if (candidateColumn > baseColumn) {
					targetColumn = candidateColumn;
					break;
				}
				break;
			}
			if (targetColumn == baseColumn) targetColumn = baseColumn + inferredShellIndentStepColumns(lineStart, settings);
		}
	} else if (language == MRSyntaxLanguage::Perl) {
		const std::string upperLine = upperAscii(std::string(trimmedBeforeCursor));
		if (isPerlStructuredBlockLead(trimmedBeforeCursor, upperLine)) {
			bool inPod = false;
			std::size_t nextLineStart = lineStart;
			while (nextLineStart < mBufferModel.length()) {
				const std::size_t candidateLineStart = nextLineOffset(nextLineStart);
				if (candidateLineStart <= nextLineStart) break;
				nextLineStart = candidateLineStart;

				const std::string candidateLineText = mBufferModel.lineText(candidateLineStart);
				const std::string_view candidateTrimmed = trimView(candidateLineText);
				if (candidateTrimmed.empty()) continue;
				if (inPod) {
					if (isPerlPodEnd(candidateTrimmed)) inPod = false;
					continue;
				}
				if (isPerlPodStart(candidateTrimmed)) {
					inPod = true;
					continue;
				}
				if (candidateTrimmed.starts_with("#")) continue;

				const int candidateColumn = leadingIndentColumnForLine(candidateLineStart);
				if (candidateColumn > baseColumn) {
					targetColumn = candidateColumn;
					break;
				}
				break;
			}
			if (targetColumn == baseColumn)
				targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
		}
	} else if (language == MRSyntaxLanguage::MRMAC) {
		const std::string upperLine = upperAscii(std::string(stripMRMACTrailingComment(trimmedBeforeCursor)));
		if (isMRMACMacroStart(upperLine) || isMRMACIfLead(upperLine) || isMRMACElseLead(upperLine) || isMRMACWhileLead(upperLine))
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Yaml) {
		if (isYamlIndentLead(trimmedBeforeCursor))
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Xml) {
		std::string_view tagName;
		if (parseXmlLeadingOpenTag(trimmedBeforeCursor, tagName))
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Systemd) {
		if (isSystemdContinuationLead(trimmedBeforeCursor)) {
			if (baseColumn > 1)
				targetColumn = baseColumn;
			else
				targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
		}
	} else if (language == MRSyntaxLanguage::Pascal) {
		const std::string upperLine = upperAscii(std::string(trimmedBeforeCursor));
		if (pascalIndentBlockKind(upperLine) != kPascalBlockNone)
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Make) {
		if (isMakeTargetLine(trimmedBeforeCursor) || isMakeDirectiveFoldStart(trimmedBeforeCursor))
			targetColumn = nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	} else if (language == MRSyntaxLanguage::Markdown) {
		int markdownColumn = 0;
		if (markdownContinuationColumn(beforeCursor, markdownColumn)) targetColumn = std::max(targetColumn, markdownColumn);
	}
	return buildEditIndentFill(settings, 1, targetColumn, configuredTabExpandSetting());
}

void MRFileEditor::applyLiveSmartDedentAfterTextInput(const std::string &insertedText) {
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const MRSyntaxLanguage language = mBufferModel.language();
	const bool smartEnabled = upperAscii(settings.indentStyle) == "SMART";
	const bool neutralAutoScratchIndent = mBufferModel.languageAutomatic() && !hasPersistentFileName();

	if (!smartEnabled) return;
	if (neutralAutoScratchIndent) return;
	if (insertedText.find('\n') != std::string::npos || insertedText.find('\r') != std::string::npos) return;
	if (language != MRSyntaxLanguage::C && language != MRSyntaxLanguage::Cpp && language != MRSyntaxLanguage::JavaScript && language != MRSyntaxLanguage::Json && language != MRSyntaxLanguage::Python &&
	    language != MRSyntaxLanguage::Bash && language != MRSyntaxLanguage::Zsh && language != MRSyntaxLanguage::Fish && language != MRSyntaxLanguage::Perl && language != MRSyntaxLanguage::MRMAC && language != MRSyntaxLanguage::Swift &&
	    language != MRSyntaxLanguage::Rust && language != MRSyntaxLanguage::Go && language != MRSyntaxLanguage::Pascal && language != MRSyntaxLanguage::Systemd &&
	    language != MRSyntaxLanguage::Xml)
		return;

	const std::size_t lineStart = lineStartOffset(cursorOffset());
	const int baseColumn = leadingIndentColumnForLine(lineStart);
	if (baseColumn <= 1) return;

	const std::string lineText = mBufferModel.lineText(lineStart);
	const std::string_view trimmedLine = trimView(lineText);
	if (language == MRSyntaxLanguage::Systemd && isSystemdSectionHeader(trimmedLine)) {
		applyCurrentLineLeadingIndent(1);
		return;
	}
	const SmartDedentRequest request = classifySmartDedentRequest(trimmedLine, language);

	if (request.kind == SmartDedentKind::None) return;

	int targetColumn = 0;
	std::size_t previousLineStart = lineStart;
	while (previousLineStart > 0) {
		const std::size_t candidateLineStart = prevLineOffset(previousLineStart);
		if (candidateLineStart == previousLineStart) break;
		previousLineStart = candidateLineStart;

		const std::string candidateLineText = mBufferModel.lineText(candidateLineStart);
		const std::string_view candidateTrimmed = trimView(candidateLineText);
		if (isDedentSearchSkippableLine(candidateTrimmed, language)) continue;

		const int candidateColumn = leadingIndentColumnForLine(candidateLineStart);
		if (candidateColumn >= baseColumn) continue;

		const std::string candidateUpperLine = upperAscii(std::string(candidateTrimmed));
		if (!matchesSmartDedentAnchor(candidateTrimmed, candidateUpperLine, language, request)) continue;
		if (request.kind == SmartDedentKind::FishCase && isFishSwitchLead(candidateUpperLine))
			targetColumn = candidateColumn + inferredShellIndentStepColumns(lineStart, settings);
		else
			targetColumn = candidateColumn;
		break;
	}

	if (targetColumn <= 0) {
		if (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Fish)
			targetColumn = std::max(1, baseColumn - inferredShellIndentStepColumns(lineStart, settings));
		else
			targetColumn = prevResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, baseColumn);
	}

	applyCurrentLineLeadingIndent(targetColumn);
}

bool MRFileEditor::newLineWithPreferredIndent() {
	const std::string indentStyle = upperAscii(configuredEditSetupSettings().indentStyle);
	const bool neutralAutoScratchIndent = mBufferModel.languageAutomatic() && !hasPersistentFileName();
	if (indentStyle == "SMART") {
		const MRSyntaxLanguage language = mBufferModel.language();
		const std::size_t cursor = cursorOffset();
		const std::size_t lineStart = lineStartOffset(cursor);
		const std::size_t lineEnd = lineEndOffset(lineStart);
		if (cursor == lineEnd) {
			const std::string lineText = mBufferModel.lineText(lineStart);
			const std::size_t splitOffset = trailingSmartDedentSplitOffset(lineText, language);
			if (splitOffset != std::string::npos) {
				setCursorOffset(lineStart + splitOffset);
				return newLineWithIndent(smartIndentFillForCursor());
			}
		}
	}
	if (indentStyle == "AUTOMATIC") return newLineWithIndent(automaticIndentFillForCursor());
	if (indentStyle == "SMART" && neutralAutoScratchIndent) return newLineWithIndent(automaticIndentFillForCursor());
	if (indentStyle == "SMART") return newLineWithIndent(smartIndentFillForCursor());
	return newLineWithIndent(preferredIndentFill());
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
