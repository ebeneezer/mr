#include "MRFileEditor.hpp"
#include "../MREditWindow.hpp"
#include "../../app/MREditorApp.hpp"
#include "../../config/settings/MRSettingsStorage.hpp"

#include <chrono>
#include <ctime>
#include <sstream>

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
	       language == MRSyntaxLanguage::Go || language == MRSyntaxLanguage::Kotlin || language == MRSyntaxLanguage::CSharp || language == MRSyntaxLanguage::Pascal || language == MRSyntaxLanguage::Xml;
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

std::vector<std::string> buildViewportScanLineTextsParallelImpl(const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t focusTopLine,
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
		const int headingLevel = language == MRSyntaxLanguage::Markdown ? markdownHeadingLevel(trimmed, nextTrimmed) : (language == MRSyntaxLanguage::Systemd && isSystemdSectionHeader(trimmed) ? 1 : 0);
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
		if (language == MRSyntaxLanguage::Systemd && block.sourceKind == MRFoldSourceKind::Section && block.lastContentLine != std::numeric_limits<std::size_t>::max() && block.lastContentLine > block.startLine)
			appendVisibleSpan(block, block.lastContentLine);
		else
			appendVisibleSpan(block, finalLine);
	return output;
}

} // namespace

std::vector<std::string> mrBuildViewportScanLineTextsParallel(const mr::editor::ReadSnapshot &snapshot, std::size_t scanTopLine, std::size_t scanBottomLine, std::size_t focusTopLine,
                                                              std::size_t focusBottomLine) {
	return buildViewportScanLineTextsParallelImpl(snapshot, scanTopLine, scanBottomLine, focusTopLine, focusBottomLine);
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

MRFileEditor::LoadTiming::LoadTiming() noexcept : valid(false), bytes(0), lines(0), linesExact(false), mappedLoadMs(0.0), lineCountMs(0.0) {
}

MRFileEditor::DestructionProbe::~DestructionProbe() {
	if (!active) return;
	std::ostringstream line;
	line << "MRFileEditor destroy buffer_id=" << bufferId << " title='" << title << "' len=" << length << " add=" << addBufferLength << " pieces=" << pieceCount << " undo=" << undoDepth
	     << " redo=" << redoDepth << " took_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt).count() << ".";
	appendDirectProbeLog(line.str());
}

MRFileEditor::MRFileEditor(const TRect &bounds, TScrollBar *aHScrollBar, TScrollBar *aVScrollBar, TIndicator *aIndicator, TStringView aFileName) noexcept
    : TScroller(bounds, aHScrollBar, aVScrollBar), mIndicator(aIndicator), mReadOnly(false), mInsertMode(true), mAutoIndent(false), mSyntaxTitleHint(), mBufferModel(), mSelectionAnchor(0), mCursorVisualColumn(0), mIndicatorUpdateInProgress(false), mLineIndexWarmupTaskId(0), mLineIndexWarmupDocumentId(0), mLineIndexWarmupVersion(0), mSuppressLargeFileLineIndexWarmup(false), mSyntaxState(), mFoldState(), mMiniMapState(), mSaveNormalizationCache(), mSaveNormalizationWarmupTaskId(0), mSaveNormalizationWarmupDocumentId(0), mSaveNormalizationWarmupVersion(0),
      mSaveNormalizationWarmupOptionsHash(0), mSaveNormalizationWarmupSourceBytes(0), mSaveNormalizationWarmupStartedAt(std::chrono::steady_clock::time_point()), mSaveNormalizationThroughputBytesPerMicro(0.0), mSaveNormalizationThroughputSamples(0), mBlockOverlayActive(false), mBlockOverlayMode(0), mBlockOverlayAnchor(0), mBlockOverlayEnd(0), mBlockOverlayTrackingCursor(false), mPreferredIndentColumn(1), mLastLoadTiming(), mCachedCursorLineDocumentId(0), mCachedCursorLineVersion(0), mCachedCursorLineOffset(0), mCachedCursorLineIndexValue(0) {
	fileName[0] = EOS;
	options |= ofFirstClick;
	eventMask |= evMouse | evKeyboard | evCommand;
	if (!aFileName.empty()) setPersistentFileName(aFileName);
	syncFromEditorState(false);
}

MRFileEditor::~MRFileEditor() {
	mDestructionProbe.arm(static_cast<int>(mBufferModel.documentId()), persistentFileName(), mBufferModel.length(), mBufferModel.document().addBufferLength(), mBufferModel.document().pieceCount(),
	                     mBufferModel.undoStackDepth(), mBufferModel.redoStackDepth());
}

bool MRFileEditor::isReadOnly() const {
	return mReadOnly;
}

void MRFileEditor::setWindowEofMarkerColorOverride(bool enabled, TColorAttr color) {
	mCustomWindowEofMarkerColorOverrideValid = enabled;
	mCustomWindowEofMarkerColorOverride = color;
	drawView();
}

void MRFileEditor::setReadOnly(bool readOnly) {
	if (mReadOnly != readOnly) {
		mReadOnly = readOnly;
		if (mReadOnly) setDocumentModified(false);
		syncFromEditorState(false);
	}
}

const char *MRFileEditor::persistentFileName() const noexcept {
	return hasPersistentFileName() ? fileName : "";
}

std::size_t MRFileEditor::persistentFileNameCapacity() const noexcept {
	return sizeof(fileName);
}

bool MRFileEditor::hasPersistentFileName() const {
	return fileName[0] != EOS;
}

void MRFileEditor::setPersistentFileName(TStringView name) noexcept {
	strnzcpy(fileName, name, sizeof(fileName));
	refreshSyntaxContext();
	scheduleSaveNormalizationWarmupIfNeeded();
}

void MRFileEditor::clearPersistentFileName() noexcept {
	fileName[0] = EOS;
	refreshSyntaxContext();
	scheduleSaveNormalizationWarmupIfNeeded();
}

bool MRFileEditor::isDocumentModified() const noexcept {
	return mBufferModel.isModified();
}

void MRFileEditor::setDocumentModified(bool changed) {
	mBufferModel.setModified(changed);
	if (!changed) {
		mBufferModel.clearUndoRedo();
		clearDirtyRanges();
	}
	syncFromEditorState(false);
}

bool MRFileEditor::hasUndoHistory() const noexcept {
	return mBufferModel.undoStackDepth() > 0;
}

bool MRFileEditor::hasRedoHistory() const noexcept {
	return mBufferModel.redoStackDepth() > 0;
}

bool MRFileEditor::insertModeEnabled() const noexcept {
	return mInsertMode;
}

std::size_t MRFileEditor::originalBufferLength() const noexcept {
	return mBufferModel.document().originalLength();
}

std::size_t MRFileEditor::addBufferLength() const noexcept {
	return mBufferModel.document().addBufferLength();
}

std::size_t MRFileEditor::pieceCount() const noexcept {
	return mBufferModel.document().pieceCount();
}

bool MRFileEditor::hasMappedOriginalSource() const noexcept {
	return mBufferModel.document().hasMappedOriginal();
}

const std::string &MRFileEditor::mappedOriginalPath() const noexcept {
	return mBufferModel.document().mappedPath();
}

std::size_t MRFileEditor::estimatedLineCount() const noexcept {
	return mBufferModel.estimatedLineCount();
}

bool MRFileEditor::exactLineCountKnown() const noexcept {
	return mBufferModel.exactLineCountKnown();
}

std::size_t MRFileEditor::selectionLength() const noexcept {
	return mBufferModel.selection().range().length();
}

std::uint64_t MRFileEditor::pendingLineIndexWarmupTaskId() const noexcept {
	return mLineIndexWarmupTaskId;
}

std::uint64_t MRFileEditor::pendingSyntaxWarmupTaskId() const noexcept {
	return mSyntaxState.warmupState().taskId;
}

std::uint64_t MRFileEditor::pendingFoldWarmupTaskId() const noexcept {
	return mFoldState.warmupState().taskId;
}

std::uint64_t MRFileEditor::pendingMiniMapWarmupTaskId() const noexcept {
	return mMiniMapState.renderer().pendingWarmupTaskId();
}

std::uint64_t MRFileEditor::pendingSaveNormalizationWarmupTaskId() const noexcept {
	return mSaveNormalizationWarmupTaskId;
}

std::size_t MRFileEditor::syntaxWarmupTopLine() const noexcept {
	return mSyntaxState.warmupState().topLine;
}

std::size_t MRFileEditor::syntaxWarmupBottomLine() const noexcept {
	return mSyntaxState.warmupState().bottomLine;
}

std::size_t MRFileEditor::syntaxPrefetchTargetBottomLine() const noexcept {
	return mSyntaxState.prefetchState().targetBottomLine;
}

std::size_t MRFileEditor::syntaxPrefetchReachedBottomLine() const noexcept {
	return mSyntaxState.prefetchState().reachedBottomLine;
}

bool MRFileEditor::shouldReportMiniMapInitialRender() const noexcept {
	return mMiniMapState.shouldReportInitialRender(mBufferModel.documentId());
}

void MRFileEditor::markMiniMapInitialRenderReported() noexcept {
	mMiniMapState.markInitialRenderReported(mBufferModel.documentId());
}

const std::string &MRFileEditor::lastUiHotpathTrace() const noexcept {
	return mLastUiHotpathTrace;
}

bool MRFileEditor::lineIndexWarmupPending() const noexcept {
	return mLineIndexWarmupTaskId != 0;
}

bool MRFileEditor::syntaxWarmupPending() const noexcept {
	return mSyntaxState.warmupState().taskId != 0;
}

bool MRFileEditor::miniMapWarmupPending() const noexcept {
	return mMiniMapState.renderer().pendingWarmupTaskId() != 0;
}

bool MRFileEditor::saveNormalizationWarmupPending() const noexcept {
	return mSaveNormalizationWarmupTaskId != 0;
}

bool MRFileEditor::usesApproximateMetrics() const noexcept {
	return useApproximateLargeFileMetrics();
}

void MRFileEditor::setInsertModeEnabled(bool on) {
	if (mInsertMode == on) return;
	mInsertMode = on;
	syncFromEditorState(false);
	if (owner != nullptr) message(owner, evBroadcast, cmUpdateTitle, 0);
}

int MRFileEditor::preferredIndentColumn() const noexcept {
	return mPreferredIndentColumn;
}

void MRFileEditor::setPreferredIndentColumn(int column) noexcept {
	if (column < 1) column = 1;
	if (column > 999) column = 999;
	mPreferredIndentColumn = column;
}

bool MRFileEditor::freeCursorMovementEnabled() const noexcept {
	return configuredCursorBehaviour() == MRCursorBehaviour::FreeMovement;
}

int MRFileEditor::actualCursorVisualColumn(std::size_t offset) const noexcept {
	return charColumn(mBufferModel.lineStart(offset), offset);
}

int MRFileEditor::displayedCursorColumn() const noexcept {
	const int actualColumn = actualCursorVisualColumn(mBufferModel.cursor());
	if (!freeCursorMovementEnabled()) return actualColumn;
	return std::max(actualColumn, mCursorVisualColumn);
}

std::size_t MRFileEditor::cachedCursorLineIndex() const noexcept {
	const std::size_t documentId = mBufferModel.documentId();
	const std::size_t version = mBufferModel.version();
	const std::size_t cursor = mBufferModel.cursor();

	if (mCachedCursorLineDocumentId == documentId && mCachedCursorLineVersion == version && mCachedCursorLineOffset == cursor) return mCachedCursorLineIndexValue;

	mCachedCursorLineDocumentId = documentId;
	mCachedCursorLineVersion = version;
	mCachedCursorLineOffset = cursor;
	mCachedCursorLineIndexValue = mBufferModel.lineIndex(cursor);
	return mCachedCursorLineIndexValue;
}

void MRFileEditor::syncDisplayedCursorColumnFromCursor(bool preserveFreeColumn) noexcept {
	const int actualColumn = actualCursorVisualColumn(mBufferModel.cursor());

	if (!freeCursorMovementEnabled() || !preserveFreeColumn) {
		mCursorVisualColumn = actualColumn;
		return;
	}
	if (mCursorVisualColumn < actualColumn) mCursorVisualColumn = actualColumn;
}

std::size_t MRFileEditor::cursorOffset() const noexcept {
	return mBufferModel.cursor();
}

std::size_t MRFileEditor::bufferLength() const noexcept {
	return mBufferModel.length();
}

std::size_t MRFileEditor::selectionStartOffset() const noexcept {
	return mBufferModel.selectionStart();
}

std::size_t MRFileEditor::selectionEndOffset() const noexcept {
	return mBufferModel.selectionEnd();
}

bool MRFileEditor::hasTextSelection() const noexcept {
	return mBufferModel.hasSelection();
}

std::size_t MRFileEditor::lineStartOffset(std::size_t pos) const noexcept {
	return mBufferModel.lineStart(pos);
}

std::size_t MRFileEditor::lineEndOffset(std::size_t pos) const noexcept {
	return mBufferModel.lineEnd(pos);
}

std::size_t MRFileEditor::nextLineOffset(std::size_t pos) const noexcept {
	return mBufferModel.nextLine(pos);
}

std::size_t MRFileEditor::prevLineOffset(std::size_t pos) const noexcept {
	return mBufferModel.prevLine(pos);
}

std::size_t MRFileEditor::lineIndexOfOffset(std::size_t pos) const noexcept {
	return mBufferModel.lineIndex(pos);
}

std::size_t MRFileEditor::columnOfOffset(std::size_t pos) const noexcept {
	return mBufferModel.column(pos);
}

char MRFileEditor::charAtOffset(std::size_t pos) const noexcept {
	return mBufferModel.charAt(pos);
}

std::string MRFileEditor::lineTextAtOffset(std::size_t pos) const {
	return mBufferModel.lineText(pos);
}

int MRFileEditor::charColumn(std::size_t start, std::size_t pos) const noexcept {
	const auto startedAt = std::chrono::steady_clock::now();
	std::size_t lineStart = mBufferModel.lineStart(start);
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::size_t p = 0;
	std::size_t end = std::min(pos, mBufferModel.length()) - lineStart;
	int visual = 0;

	end = std::min(end, line.size());
	while (p < end) {
		std::size_t next = p;
		std::size_t width = 0;
		if (!nextDisplayChar(line, next, width, visual, settings)) break;
		if (next > end) break;
		visual += static_cast<int>(width);
		p = next;
	}
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowNavigationTraceThreshold) {
		std::ostringstream trace;
		trace << "Phase1 nav charColumn total_us=" << traceMicros(totalElapsed) << " start=" << start << " pos=" << pos << " line_start=" << lineStart << " end_bytes=" << end
		      << " line_bytes=" << line.size() << " len=" << mBufferModel.length() << " add=" << mBufferModel.document().addBufferLength() << " pieces=" << mBufferModel.document().pieceCount();
		appendDirectProbeLog(trace.str());
	}
	return visual;
}

void MRFileEditor::setCursorOffset(std::size_t pos, int) {
	moveCursor(std::min(pos, mBufferModel.length()), false, false);
}

bool MRFileEditor::scrollWindowByLines(int deltaRows) {
	const std::size_t cursorBefore = cursorOffset();
	const int rowBefore = currentViewRow();
	const int targetVisualColumn = displayedCursorColumn();
	const std::size_t target = lineMoveOffset(cursorBefore, deltaRows, targetVisualColumn);

	if (deltaRows == 0) return true;
	if (target == cursorBefore) return false;
	moveCursor(target, false, false, targetVisualColumn);
	if (const int rowDelta = currentViewRow() - rowBefore; rowDelta != 0) scrollTo(std::max(delta.x, 0), std::max(delta.y + rowDelta, 0));
	return true;
}

std::size_t MRFileEditor::offsetForGlobalPoint(TPoint where) noexcept {
	return mouseOffset(makeLocal(where));
}

int MRFileEditor::currentLineNumber() const noexcept {
	return static_cast<int>(cachedCursorLineIndex()) + 1;
}

int MRFileEditor::currentColumnNumber() const noexcept {
	return displayedCursorColumn() + 1;
}

int MRFileEditor::currentViewRow() const noexcept {
	return std::max(1, static_cast<int>(visibleLineForDocumentLine(cachedCursorLineIndex())) - delta.y + 1);
}

int MRFileEditor::currentViewColumn() const noexcept {
	return std::max(1, displayedCursorColumn() - delta.x + 1);
}

int MRFileEditor::visibleViewportRows() const noexcept {
	return std::max(1, visibleTextRows());
}

const MRTextBufferModel &MRFileEditor::bufferModel() const noexcept {
	return mBufferModel;
}

MRTextBufferModel &MRFileEditor::bufferModel() noexcept {
	return mBufferModel;
}

void MRFileEditor::shareContentStateFrom(const MRFileEditor &source) {
	mBufferModel.shareContentStateFrom(source.bufferModel());
	if (source.hasPersistentFileName()) setPersistentFileName(source.persistentFileName());
	else
		clearPersistentFileName();
	clearFindMarkerRanges();
	clearDirtyRanges();
	syncFromEditorState(false);
}

void MRFileEditor::detachContentStateCopy() {
	mBufferModel.detachContentStateCopy();
	clearFindMarkerRanges();
	clearDirtyRanges();
	syncFromEditorState(false);
}

std::string MRFileEditor::snapshotText() const {
	return mBufferModel.text();
}

MRTextBufferModel::ReadSnapshot MRFileEditor::readSnapshot() const {
	return mBufferModel.readSnapshot();
}

MRTextBufferModel::Document MRFileEditor::documentCopy() const {
	return mBufferModel.document();
}

std::size_t MRFileEditor::documentId() const noexcept {
	return mBufferModel.documentId();
}

std::size_t MRFileEditor::documentVersion() const noexcept {
	return mBufferModel.version();
}

MRFileEditor::LoadTiming MRFileEditor::lastLoadTiming() const noexcept {
	return mLastLoadTiming;
}

const char *MRFileEditor::syntaxLanguageName() const noexcept {
	return mBufferModel.languageName();
}

MRSyntaxLanguage MRFileEditor::syntaxLanguage() const noexcept {
	return mBufferModel.language();
}

bool MRFileEditor::syntaxLanguageAutomatic() const noexcept {
	return mBufferModel.languageAutomatic();
}

MRMiniMapRenderer::Palette MRFileEditor::resolveMiniMapPalette() {
	MRMiniMapRenderer::Palette palette;
	unsigned char configured = 0;
	const TColorAttr fallback = static_cast<TColorAttr>(getColor(0x0201));

	palette.normal = configuredColorSlotOverride(kMrPaletteMiniMapNormal, configured) ? static_cast<TColorAttr>(configured) : fallback;
	palette.viewport = configuredColorSlotOverride(kMrPaletteMiniMapViewport, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.changed = configuredColorSlotOverride(kMrPaletteMiniMapChanged, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.findMarker = configuredColorSlotOverride(kMrPaletteMiniMapFindMarker, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	palette.errorMarker = configuredColorSlotOverride(kMrPaletteMiniMapErrorMarker, configured) ? static_cast<TColorAttr>(configured) : palette.normal;
	return palette;
}

void MRFileEditor::refreshConfiguredVisualSettings() {
	syncDisplayedCursorColumnFromCursor(true);
	refreshSyntaxContext();
	invalidateFoldCache();
	syncIndicatorVisualSettings();
	updateMetrics();
	scheduleSyntaxWarmupIfNeeded();
	updateIndicator();
	drawView();
}

void MRFileEditor::setFindMarkerRanges(const std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
	std::vector<MRTextBufferModel::Range> normalized;
	const std::size_t length = mBufferModel.length();

	normalized.reserve(ranges.size());
	if (length != 0) {
		for (const auto &rangePair : ranges) {
			std::size_t start = std::min(rangePair.first, length);
			std::size_t end = std::min(rangePair.second, length);
			if (end < start) std::swap(start, end);
			if (end == start) {
				if (end < length) ++end;
				else if (start > 0)
					--start;
			}
			if (end > start) normalized.push_back(MRTextBufferModel::Range(start, end));
		}
	}
	normalizeRangeList(normalized);
	mFindMarkerRanges.swap(normalized);
	drawView();
}

void MRFileEditor::clearFindMarkerRanges() {
	if (mFindMarkerRanges.empty()) return;
	mFindMarkerRanges.clear();
	drawView();
}

void MRFileEditor::revealCursor(Boolean centerCursor) {
	ensureCursorVisible(centerCursor == True);
	updateIndicator();
	drawView();
}

void MRFileEditor::refreshViewState() {
	updateIndicator();
	drawView();
}

void MRFileEditor::update(uchar) {
	refreshViewState();
}

void MRFileEditor::syncFromEditorState(bool) {
	syncDisplayedCursorColumnFromCursor(true);
	refreshSyntaxContext();
	updateMetrics();
	syncIndicatorVisualSettings();
	updateIndicator();
}

void MRFileEditor::syncIndicatorVisualSettings() {
	if (auto *mrIndicator = dynamic_cast<MRIndicator *>(mIndicator)) {
		MREditSetupSettings settings = configuredEditSetupSettings();
		if (mWordWrapSuppressed) settings.wordWrap = false;
		mrIndicator->setInsertMode(mInsertMode);
		mrIndicator->setWordWrap(settings.wordWrap);
	}
}

void MRFileEditor::notifyWindowTaskStateChanged() {
	if (owner != nullptr) message(owner, evBroadcast, cmUpdateTitle, 0);
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

void MRFileEditor::clearLineIndexWarmupTask(std::uint64_t expectedTaskId) noexcept {
	if (expectedTaskId != 0 && mLineIndexWarmupTaskId != expectedTaskId) return;
	if (mLineIndexWarmupTaskId == 0) return;
	mLineIndexWarmupTaskId = 0;
	mLineIndexWarmupDocumentId = 0;
	mLineIndexWarmupVersion = 0;
	notifyWindowTaskStateChanged();
}

void MRFileEditor::clearSyntaxWarmupTask(std::uint64_t expectedTaskId) noexcept {
	MRSyntaxDerivedState::WarmupState &warmupState = mSyntaxState.warmupState();
	if (expectedTaskId != 0 && warmupState.taskId != expectedTaskId) return;
	if (warmupState.taskId == 0) return;
	warmupState = MRSyntaxDerivedState::WarmupState();
	notifyWindowTaskStateChanged();
}

void MRFileEditor::clearMiniMapWarmupTask(std::uint64_t expectedTaskId) noexcept {
	applyMiniMapSignals(mMiniMapState.renderer().clearWarmupTask(expectedTaskId));
}

void MRFileEditor::applyMiniMapSignals(const MRMiniMapRenderer::Signals &signals) {
	if (signals.notifyTaskStateChanged) notifyWindowTaskStateChanged();
	if (signals.redraw) drawView();
}

void MRFileEditor::clearSaveNormalizationWarmupTask(std::uint64_t expectedTaskId) noexcept {
	if (expectedTaskId != 0 && mSaveNormalizationWarmupTaskId != expectedTaskId) return;
	if (mSaveNormalizationWarmupTaskId == 0) return;
	mSaveNormalizationWarmupTaskId = 0;
	mSaveNormalizationWarmupDocumentId = 0;
	mSaveNormalizationWarmupVersion = 0;
	mSaveNormalizationWarmupOptionsHash = 0;
	mSaveNormalizationWarmupSourceBytes = 0;
	mSaveNormalizationWarmupStartedAt = std::chrono::steady_clock::time_point();
	notifyWindowTaskStateChanged();
}

void MRFileEditor::clearFoldWarmupTask(std::uint64_t expectedTaskId) noexcept {
	MRFoldingDerivedState::WarmupState &warmupState = mFoldState.warmupState();
	if (expectedTaskId != 0 && warmupState.taskId != expectedTaskId) return;
	if (warmupState.taskId == 0) return;
	mFoldState.clearWarmupState();
	notifyWindowTaskStateChanged();
}

void MRFileEditor::setSyntaxTitleHint(const std::string &title) {
	mSyntaxTitleHint = title;
	refreshSyntaxContext();
	updateMetrics();
	updateIndicator();
}

TPalette &MRFileEditor::getPalette() const {
	// 1..2: scroller text/selected text (window slots 6/7)
	// 3..5: editor-only highlight slots (window-local palette slots 9..11)
	// mapped to app palette extension 136..138.
	// 6: line number gutter (window-local slot 12, app slot 142).
	static TPalette palette("\x06\x07\x09\x0A\x0B\x0C", 6);
	return palette;
}

Boolean MRFileEditor::valid(ushort command) {
	if (command == cmValid || command == cmReleasedFocus) return True;
	if (mReadOnly || !mBufferModel.isModified()) return True;
	const auto startedAt = std::chrono::steady_clock::now();
	Boolean result = !canSaveInPlace() ? confirmSaveOrDiscardUntitled() : confirmSaveOrDiscardNamed();
	std::ostringstream trace;
	trace << "Phase1 discard editor valid total_us=" << traceMicros(std::chrono::steady_clock::now() - startedAt) << " result=" << (result == True ? 1 : 0) << " len=" << mBufferModel.length()
	      << " add=" << mBufferModel.document().addBufferLength() << " pieces=" << mBufferModel.document().pieceCount() << " modified=" << (mBufferModel.isModified() ? 1 : 0);
	appendDirectProbeLog(trace.str());
	return result;
}

bool MRFileEditor::isWordByte(char ch) noexcept {
	unsigned char uch = static_cast<unsigned char>(ch);
	return std::isalnum(uch) != 0 || ch == '_';
}

bool MRFileEditor::hasShiftModifier(ushort mods) noexcept {
	return (mods & (kbShift | kbCtrlShift | kbAltShift)) != 0;
}

int MRFileEditor::configuredTabSize() noexcept {
	int tabSize = configuredTabSizeSetting();
	if (tabSize < 1) tabSize = 1;
	if (tabSize > 32) tabSize = 32;
	return tabSize;
}

bool MRFileEditor::configuredDisplayTabs() noexcept {
	return configuredDisplayTabsSetting();
}

bool MRFileEditor::configuredFormatRuler() noexcept {
	return configuredEditSetupSettings().formatRuler;
}

int MRFileEditor::tabDisplayWidth(const MREditSetupSettings &settings, int visualColumn) noexcept {
	const int currentColumn = std::max(1, visualColumn + 1);
	const int targetColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
	return std::max(1, targetColumn - currentColumn);
}


int MRFileEditor::visibleTextRows() const noexcept {
	return std::max(0, size.y - (configuredFormatRuler() ? 1 : 0));
}

void MRFileEditor::syncScrollBarsToState() noexcept {
	bool show = mScrollBarsAlwaysVisible || (state & (sfActive | sfSelected)) != 0;
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
	if (window != nullptr && window->isMinimized()) show = false;
	if (hScrollBar != nullptr) {
		if (show) hScrollBar->show();
		else
			hScrollBar->hide();
	}
	if (vScrollBar != nullptr) {
		if (show) vScrollBar->show();
		else
			vScrollBar->hide();
	}
}

int MRFileEditor::decimalDigits(std::size_t value) noexcept {
	int digits = 1;
	while (value >= 10) {
		value /= 10;
		++digits;
	}
	return digits;
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


bool MRFileEditor::resolveSaveOptionsForPath(const char *path, MRTextSaveOptions &options, std::size_t *optionsHash) const {
	options = effectiveTextSaveOptionsForPath(path != nullptr ? path : "", optionsHash);
	return true;
}

void MRFileEditor::invalidateSaveNormalizationCache() noexcept {
	mSaveNormalizationCache.valid = false;
	mSaveNormalizationCache.documentId = 0;
	mSaveNormalizationCache.version = 0;
	mSaveNormalizationCache.optionsHash = 0;
	mSaveNormalizationCache.sourceBytes = 0;
}

void MRFileEditor::noteSaveNormalizationThroughput(std::size_t sourceBytes, double runMicros) noexcept {
	if (sourceBytes == 0 || runMicros <= 0.0) return;
	const double sampleBytesPerMicro = static_cast<double>(sourceBytes) / std::max(1.0, runMicros);
	if (mSaveNormalizationThroughputBytesPerMicro <= 0.0) mSaveNormalizationThroughputBytesPerMicro = sampleBytesPerMicro;
	else
		mSaveNormalizationThroughputBytesPerMicro = mSaveNormalizationThroughputBytesPerMicro * 0.75 + sampleBytesPerMicro * 0.25;
	++mSaveNormalizationThroughputSamples;
}

std::size_t MRFileEditor::nextCharOffset(std::size_t pos) noexcept {
	std::size_t len = mBufferModel.length();
	char bytes[4];
	std::size_t count = 0;

	if (pos >= len) return len;
	if (mBufferModel.charAt(pos) == '\r' && pos + 1 < len && mBufferModel.charAt(pos + 1) == '\n') return std::min(len, pos + 2);
	for (; count < sizeof(bytes) && pos + count < len; ++count)
		bytes[count] = mBufferModel.charAt(pos + count);
	std::size_t step = TText::next(TStringView(bytes, count));
	return std::min(len, pos + std::max<std::size_t>(step, 1));
}

std::size_t MRFileEditor::prevCharOffset(std::size_t pos) noexcept {
	char bytes[4];
	std::size_t start = 0;
	std::size_t count = 0;

	if (pos == 0) return 0;
	if (pos > 1 && mBufferModel.charAt(pos - 2) == '\r' && mBufferModel.charAt(pos - 1) == '\n') return pos - 2;
	start = pos > sizeof(bytes) ? pos - sizeof(bytes) : 0;
	count = pos - start;
	for (std::size_t i = 0; i < count; ++i)
		bytes[i] = mBufferModel.charAt(start + i);
	std::size_t step = TText::prev(TStringView(bytes, count), count);
	return pos - std::max<std::size_t>(step, 1);
}

std::size_t MRFileEditor::lineMoveOffset(std::size_t pos, int deltaLines, int targetVisualColumn) noexcept {
	const auto startedAt = std::chrono::steady_clock::now();
	const std::size_t clampedPos = std::min(pos, mBufferModel.length());
	const auto lineIndexStartedAt = std::chrono::steady_clock::now();
	const std::size_t currentDocumentLine = mBufferModel.lineIndex(clampedPos);
	const auto lineIndexElapsed = std::chrono::steady_clock::now() - lineIndexStartedAt;
	const auto visibleLineStartedAt = std::chrono::steady_clock::now();
	const std::size_t currentVisibleLine = visibleLineForDocumentLine(currentDocumentLine);
	const auto visibleLineElapsed = std::chrono::steady_clock::now() - visibleLineStartedAt;
	std::size_t targetVisibleLine = currentVisibleLine;
	std::size_t targetDocumentLine = currentDocumentLine;
	std::chrono::steady_clock::duration charColumnElapsed{};
	std::chrono::steady_clock::duration documentLineElapsed{};
	std::chrono::steady_clock::duration charPtrElapsed{};

	if (targetVisualColumn < 0) {
		const auto charColumnStartedAt = std::chrono::steady_clock::now();
		targetVisualColumn = charColumn(mBufferModel.lineStart(pos), clampedPos);
		charColumnElapsed = std::chrono::steady_clock::now() - charColumnStartedAt;
	}
	if (deltaLines < 0) targetVisibleLine = currentVisibleLine > static_cast<std::size_t>(-deltaLines) ? currentVisibleLine - static_cast<std::size_t>(-deltaLines) : 0;
	else
		targetVisibleLine = currentVisibleLine + static_cast<std::size_t>(deltaLines);
	{
		const auto documentLineStartedAt = std::chrono::steady_clock::now();
		targetDocumentLine = documentLineForVisibleLine(targetVisibleLine);
		documentLineElapsed = std::chrono::steady_clock::now() - documentLineStartedAt;
	}
	std::size_t targetOffset = 0;
	{
		const auto charPtrStartedAt = std::chrono::steady_clock::now();
		targetOffset = charPtrOffset(mBufferModel.lineStartByIndex(targetDocumentLine), targetVisualColumn);
		charPtrElapsed = std::chrono::steady_clock::now() - charPtrStartedAt;
	}
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowNavigationTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 nav lineMoveOffset total_us=" << traceMicros(totalElapsed) << " line_index_us=" << traceMicros(lineIndexElapsed) << " visible_map_us=" << traceMicros(visibleLineElapsed)
		     << " char_column_us=" << traceMicros(charColumnElapsed) << " document_map_us=" << traceMicros(documentLineElapsed) << " char_ptr_us=" << traceMicros(charPtrElapsed) << " pos=" << clampedPos
		     << " delta_lines=" << deltaLines << " target_visual=" << targetVisualColumn << " current_doc_line=" << currentDocumentLine << " current_visible_line=" << currentVisibleLine
		     << " target_doc_line=" << targetDocumentLine << " target_visible_line=" << targetVisibleLine << " len=" << mBufferModel.length() << " add=" << mBufferModel.document().addBufferLength()
		     << " pieces=" << mBufferModel.document().pieceCount() << " undo=" << mBufferModel.undoStackDepth() << " redo=" << mBufferModel.redoStackDepth();
		appendDirectProbeLog(line.str());
	}
	return targetOffset;
}

std::size_t MRFileEditor::tabStopMoveOffset(std::size_t pos, bool forward) noexcept {
	const std::size_t cursor = std::min(pos, mBufferModel.length());
	const std::size_t lineStart = lineStartOffset(cursor);
	const MREditSetupSettings settings = configuredEditSetupSettings();
	const int currentColumn = (freeCursorMovementEnabled() && cursor == mBufferModel.cursor() && !mBufferModel.hasSelection() ? displayedCursorColumn() : charColumn(lineStart, cursor)) + 1;
	const int targetColumn = forward ? nextResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn) : prevResolvedEditFormatTabStopColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);

	return charPtrOffset(lineStart, targetColumn - 1);
}

std::size_t MRFileEditor::prevWordOffset(std::size_t pos) noexcept {
	std::size_t p = std::min(pos, mBufferModel.length());

	while (p > 0 && !isWordByte(mBufferModel.charAt(p - 1)))
		--p;
	while (p > 0 && isWordByte(mBufferModel.charAt(p - 1)))
		--p;
	return p;
}

std::size_t MRFileEditor::nextWordOffset(std::size_t pos) noexcept {
	std::size_t p = std::min(pos, mBufferModel.length());
	std::size_t len = mBufferModel.length();

	while (p < len && isWordByte(mBufferModel.charAt(p)))
		++p;
	while (p < len && !isWordByte(mBufferModel.charAt(p)))
		++p;
	return p;
}

std::size_t MRFileEditor::charPtrOffset(std::size_t start, int pos) noexcept {
	const auto startedAt = std::chrono::steady_clock::now();
	std::size_t lineStart = mBufferModel.lineStart(start);
	std::string lineText = mBufferModel.lineText(lineStart);
	TStringView line(lineText.data(), lineText.size());
	const MREditSetupSettings settings = configuredEditSetupSettings();
	std::size_t p = 0;
	int visual = 0;
	int target = std::max(pos, 0);

	while (p < line.size()) {
		std::size_t next = p;
		std::size_t width = 0;
		if (!nextDisplayChar(line, next, width, visual, settings)) break;
		if (visual + static_cast<int>(width) > target) break;
		visual += static_cast<int>(width);
		p = next;
	}
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowNavigationTraceThreshold) {
		std::ostringstream trace;
		trace << "Phase1 nav charPtrOffset total_us=" << traceMicros(totalElapsed) << " start=" << start << " line_start=" << lineStart << " target_visual=" << pos << " line_bytes=" << line.size()
		      << " len=" << mBufferModel.length() << " add=" << mBufferModel.document().addBufferLength() << " pieces=" << mBufferModel.document().pieceCount();
		appendDirectProbeLog(trace.str());
	}
	return lineStart + p;
}

bool MRFileEditor::canSaveInPlace() const {
	std::string persistentName;

	if (mReadOnly || !hasPersistentFileName()) return false;
	persistentName = trimAscii(fileName);
	if (upperAscii(persistentName) == "?NO-FILE?") return false;
	if (looksLikeUri(persistentName)) return false;
	return true;
}

bool MRFileEditor::canSaveAs() const {
	return !mReadOnly;
}

bool MRFileEditor::loadMappedFile(TStringView path, std::string &error) {
	MRTextBufferModel::Document document;
	const auto mapStartedAt = std::chrono::steady_clock::now();

	mLastLoadTiming = LoadTiming();
	if (!document.loadMappedFile(path, error)) return false;
	const double mappedLoadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mapStartedAt).count();
	const std::size_t lines = document.estimatedLineCount();

	mLastLoadTiming.valid = true;
	mLastLoadTiming.bytes = document.length();
	mLastLoadTiming.lines = lines;
	mLastLoadTiming.linesExact = document.exactLineCountKnown();
	mLastLoadTiming.mappedLoadMs = mappedLoadMs;
	mLastLoadTiming.lineCountMs = 0.0;
	setPersistentFileName(path);
	if (!adoptCommittedDocument(document, 0, 0, 0, false)) {
		clearPersistentFileName();
		mLastLoadTiming = LoadTiming();
		error = "Unable to adopt mapped document.";
		return false;
	}
	scheduleLineIndexWarmupIfNeeded();
	return true;
}

Boolean MRFileEditor::saveInPlace() noexcept {
	if (!canSaveInPlace()) return False;
	Boolean ok = writeDocumentToPath(fileName) ? True : False;
	if (ok == True) setDocumentModified(false);
	return ok;
}

Boolean MRFileEditor::saveAsWithPrompt() noexcept {
	char saveName[MAXPATH];

	if (!canSaveAs()) return False;
	if (hasPersistentFileName()) strnzcpy(saveName, fileName, sizeof(saveName));
	else
		initRememberedLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName, sizeof(saveName), "*.*");
	if (TEditor::editorDialog(edSaveAs, saveName) == cmCancel) return False;
	fexpand(saveName);
	if (!samePath(saveName, fileName) && !confirmOverwriteForSaveAs(saveName)) return False;
	if (!writeDocumentToPath(saveName)) return False;
	rememberLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName);
	setPersistentFileName(saveName);
	if (owner != nullptr) message((TView *)owner, evBroadcast, cmUpdateTitle, 0);
	setDocumentModified(false);
	return True;
}

Boolean MRFileEditor::saveAsWithoutOverwritePrompt() noexcept {
	char saveName[MAXPATH];

	if (!canSaveAs()) return False;
	if (hasPersistentFileName()) strnzcpy(saveName, fileName, sizeof(saveName));
	else
		initRememberedLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName, sizeof(saveName), "*.*");
	if (TEditor::editorDialog(edSaveAs, saveName) == cmCancel) return False;
	fexpand(saveName);
	if (!writeDocumentToPath(saveName)) return False;
	rememberLoadDialogPath(MRDialogHistoryScope::EditorSaveAs, saveName);
	setPersistentFileName(saveName);
	if (owner != nullptr) message((TView *)owner, evBroadcast, cmUpdateTitle, 0);
	setDocumentModified(false);
	return True;
}

void MRFileEditor::pushUndoSnapshot() {
	MRTextBufferModel::CustomUndoRecord record;
	record.preSnapshot = mBufferModel.readSnapshot();
	record.preSnapshot.dropExactLineStartIndex();
	record.cursor = mBufferModel.cursor();
	record.modifiedState = mBufferModel.isModified();
	if (mBufferModel.hasSelection()) {
		record.selAnchor = mBufferModel.selection().range().start;
		record.selCursor = mBufferModel.selection().range().end;
	} else {
		record.selAnchor = 0;
		record.selCursor = 0;
	}
	if (owner != nullptr) {
		record.blockMode = mBlockOverlayMode;
		record.blockAnchor = mBlockOverlayAnchor;
		record.blockEnd = mBlockOverlayEnd;
		record.blockMarkingOn = mBlockOverlayActive;
	}
	mBufferModel.pushUndoSnapshot(std::move(record));
}

bool MRFileEditor::replaceBufferData(const char *data, uint length) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-buffer-data");

	if (data != nullptr && length != 0) text.assign(data, length);
	transaction.setText(text);
	return applyStagedTransaction(transaction, 0, 0, 0, false).applied();
}

bool MRFileEditor::replaceBufferText(const char *text) {
	uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
	return replaceBufferData(text, length);
}

bool MRFileEditor::appendBufferData(const char *data, uint length) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "append-buffer-data");
	std::size_t endPtr = mBufferModel.length();

	if (length == 0) return true;
	if (data != nullptr) text.assign(data, length);
	transaction.insert(endPtr, text);
	return applyStagedTransaction(transaction, endPtr + text.size(), endPtr + text.size(), endPtr + text.size(), false).applied();
}

bool MRFileEditor::appendBufferText(const char *text) {
	uint length = text != nullptr ? static_cast<uint>(std::strlen(text)) : 0;
	return appendBufferData(text, length);
}

void MRFileEditor::setCommunicationViewerMode(bool enabled, bool lineNumbers) {
	if (mCommunicationViewerMode == enabled && mCommunicationViewerLineNumbers == lineNumbers) return;
	mCommunicationViewerMode = enabled;
	mCommunicationViewerLineNumbers = lineNumbers;
	refreshSyntaxContext();
	refreshViewState();
}

void MRFileEditor::setCommunicationViewerOptions(bool lineNumbers) {
	setCommunicationViewerMode(true, lineNumbers);
}

void MRFileEditor::setMiniMapSuppressed(bool suppressed) noexcept {
	if (mMiniMapSuppressed == suppressed) return;
	mMiniMapSuppressed = suppressed;
	refreshViewState();
}

void MRFileEditor::setWordWrapSuppressed(bool suppressed) noexcept {
	if (mWordWrapSuppressed == suppressed) return;
	mWordWrapSuppressed = suppressed;
	syncIndicatorVisualSettings();
	refreshViewState();
}

void MRFileEditor::setScrollBarsAlwaysVisible(bool visible) noexcept {
	if (mScrollBarsAlwaysVisible == visible) return;
	mScrollBarsAlwaysVisible = visible;
	syncScrollBarsToState();
	refreshViewState();
}

bool MRFileEditor::appendLogViewerData(const char *data, uint length, const std::vector<std::pair<std::size_t, std::size_t>> *chunkFindRanges) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "append-log-viewer-data");
	const std::size_t oldCursor = mBufferModel.cursor();
	const std::size_t oldSelectionStart = mBufferModel.selectionStart();
	const std::size_t oldSelectionEnd = mBufferModel.selectionEnd();
	const std::size_t endPtr = mBufferModel.length();
	const int oldDeltaX = delta.x;
	const int oldDeltaY = delta.y;
	const int visibleRows = std::max(1, visibleTextRows());
	const int oldMaxY = std::max(0, static_cast<int>(std::max<std::size_t>(1, mBufferModel.lineCount())) - visibleRows);
	const bool follow = oldDeltaY >= oldMaxY;

	if (data == nullptr || length == 0) return true;
	text.assign(data, length);
	transaction.insert(endPtr, text);
	if (!applyStagedTransaction(transaction, follow ? endPtr + text.size() : oldCursor, follow ? endPtr + text.size() : oldSelectionStart, follow ? endPtr + text.size() : oldSelectionEnd, false).applied()) return false;
	if (chunkFindRanges != nullptr) {
		mFindMarkerRanges.clear();
		for (const auto &rangePair : *chunkFindRanges) {
			const std::size_t start = std::min(endPtr + rangePair.first, mBufferModel.length());
			const std::size_t end = std::min(endPtr + rangePair.second, mBufferModel.length());

			if (end > start) mFindMarkerRanges.push_back(MRTextBufferModel::Range(start, end));
		}
		normalizeRangeList(mFindMarkerRanges);
	}
	if (follow) {
		const int maxY = std::max(0, static_cast<int>(std::max<std::size_t>(1, mBufferModel.lineCount())) - visibleRows);
		scrollTo(oldDeltaX, maxY);
	} else
		scrollTo(oldDeltaX, oldDeltaY);
	drawView();
	return true;
}

bool MRFileEditor::prependLogViewerData(const char *data, uint length, const std::vector<std::pair<std::size_t, std::size_t>> *chunkFindRanges) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "prepend-log-viewer-data");
	const std::size_t oldCursor = mBufferModel.cursor();
	const std::size_t oldSelectionStart = mBufferModel.selectionStart();
	const std::size_t oldSelectionEnd = mBufferModel.selectionEnd();
	const int oldDeltaX = delta.x;
	const int oldDeltaY = delta.y;
	const bool follow = delta.y <= 0;
	int insertedLines = 0;

	if (data == nullptr || length == 0) return true;
	text.assign(data, length);
	for (char ch : text)
		if (ch == '\n') ++insertedLines;
	transaction.insert(0, text);
	if (!applyStagedTransaction(transaction, follow ? 0 : oldCursor + text.size(), follow ? 0 : oldSelectionStart + text.size(), follow ? 0 : oldSelectionEnd + text.size(), false).applied()) return false;
	if (chunkFindRanges != nullptr) {
		mFindMarkerRanges.clear();
		for (const auto &rangePair : *chunkFindRanges) {
			const std::size_t start = std::min(rangePair.first, mBufferModel.length());
			const std::size_t end = std::min(rangePair.second, mBufferModel.length());

			if (end > start) mFindMarkerRanges.push_back(MRTextBufferModel::Range(start, end));
		}
		normalizeRangeList(mFindMarkerRanges);
	}
	if (follow) scrollTo(oldDeltaX, 0);
	else
		scrollTo(oldDeltaX, std::max(0, oldDeltaY + insertedLines));
	drawView();
	return true;
}

bool MRFileEditor::formatParagraph(int rightMargin) {
	return formatParagraph(configuredEditSetupSettings().leftMargin, rightMargin);
}

std::string MRFileEditor::buildFormattedParagraphText(std::string_view paragraphText, int leftMargin, int rightMargin) const {
	return MRTextFormatting::formatParagraphText(paragraphText, leftMargin, rightMargin);
}

bool MRFileEditor::formatParagraph(int leftMargin, int rightMargin) {
	if (mReadOnly) return false;

	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	while (start > 0) {
		std::size_t prevLineStart = mBufferModel.lineStart(mBufferModel.prevLine(start));
		if (isBlankString(mBufferModel.lineText(prevLineStart))) break;
		start = prevLineStart;
	}
	while (end < mBufferModel.length()) {
		std::size_t nextLineStart = mBufferModel.nextLine(end);
		if (isBlankString(mBufferModel.lineText(end))) break;
		end = nextLineStart;
	}
	if (start == end) return true;

	std::string paragraphText;
	paragraphText.reserve(end - start);
	std::size_t current = start;
	while (current < end) {
		std::string chunk = mBufferModel.document().lineText(current);
		if (!paragraphText.empty()) paragraphText.push_back('\n');
		paragraphText += chunk;
		current = mBufferModel.document().nextLine(current);
	}
	std::string formattedText = buildFormattedParagraphText(paragraphText, leftMargin, rightMargin);
	if (formattedText.empty()) return true;

	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "format-paragraph");
	transaction.replace(MRTextBufferModel::Range(start, end), formattedText);
	return applyStagedTransaction(transaction, start, start, start, true).applied();
}

bool MRFileEditor::formatDocument(int leftMargin, int rightMargin) {
	std::string formattedText;
	const std::size_t length = mBufferModel.length();
	const std::size_t cursor = mBufferModel.cursor();
	std::size_t current = 0;

	if (mReadOnly) return false;
	while (current < length) {
		if (isBlankString(mBufferModel.lineText(current))) {
			formattedText.push_back('\n');
			current = mBufferModel.nextLine(current);
			continue;
		}
		std::string paragraphText;
		const std::size_t paragraphStart = current;
		std::size_t paragraphEnd = current;
		while (paragraphEnd < length && !isBlankString(mBufferModel.lineText(paragraphEnd))) {
			if (!paragraphText.empty()) paragraphText.push_back('\n');
			paragraphText += mBufferModel.document().lineText(paragraphEnd);
			paragraphEnd = mBufferModel.document().nextLine(paragraphEnd);
		}
		if (!formattedText.empty() && formattedText.back() != '\n') formattedText.push_back('\n');
		formattedText += buildFormattedParagraphText(paragraphText, leftMargin, rightMargin);
		current = paragraphEnd;
		if (current == paragraphStart) break;
	}
	return replaceWholeBuffer(formattedText, std::min(cursor, formattedText.size()));
}

bool MRFileEditor::justifyParagraph(int leftMargin, int rightMargin) {
	if (mReadOnly) return false;

	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	std::string paragraphText;
	while (start > 0) {
		std::size_t prevLineStart = mBufferModel.lineStart(mBufferModel.prevLine(start));
		if (isBlankString(mBufferModel.lineText(prevLineStart))) break;
		start = prevLineStart;
	}
	while (end < mBufferModel.length()) {
		std::size_t nextLineStart = mBufferModel.nextLine(end);
		if (isBlankString(mBufferModel.lineText(end))) break;
		end = nextLineStart;
	}
	if (start == end) return true;
	paragraphText.reserve(end - start);
	for (std::size_t current = start; current < end; current = mBufferModel.document().nextLine(current))
		if (std::string chunk = mBufferModel.document().lineText(current); true) {
			if (!paragraphText.empty()) paragraphText.push_back('\n');
			paragraphText += chunk;
		}
	std::string justifiedText = MRTextFormatting::justifyParagraphText(paragraphText, leftMargin, rightMargin);
	if (justifiedText.empty()) return true;
	return replaceRangeAndSelect(static_cast<uint>(start), static_cast<uint>(end), justifiedText.data(), static_cast<uint>(justifiedText.size()));
}

void MRFileEditor::setBlockOverlayState(int mode, std::size_t anchor, std::size_t end, bool active, bool trackCursor) {
	const std::size_t length = mBufferModel.length();

	if (!active || mode < 1 || mode > 3) {
		mBlockOverlayActive = false;
		mBlockOverlayMode = 0;
		mBlockOverlayAnchor = 0;
		mBlockOverlayEnd = 0;
		mBlockOverlayTrackingCursor = false;
		drawView();
		return;
	}
	mBlockOverlayActive = true;
	mBlockOverlayMode = mode;
	mBlockOverlayAnchor = std::min(anchor, length);
	mBlockOverlayEnd = std::min(end, length);
	mBlockOverlayTrackingCursor = trackCursor;
	drawView();
}

void MRFileEditor::setSelectionOffsets(std::size_t start, std::size_t end, Boolean) {
	start = std::min(start, mBufferModel.length());
	end = std::min(end, mBufferModel.length());
	mSelectionAnchor = start;
	mBufferModel.setSelection(start, end);
	syncFromEditorState(false);
}

bool MRFileEditor::replaceRangeAndSelect(uint start, uint end, const char *data, uint length) {
	std::string text;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "replace-range-select");
	MRTextBufferModel::Range range;

	if (mReadOnly) return false;
	if (end < start) std::swap(start, end);
	range = MRTextBufferModel::Range(start, end).clamped(mBufferModel.length());
	if (data != nullptr && length != 0) text.assign(data, length);
	transaction.replace(range, text);
	return applyStagedTransaction(transaction, range.start, range.start, range.start + text.size(), true).applied();
}

int MRFileEditor::paddingColumnsBeforeInsertAtCursor() const noexcept {
	const std::size_t cursor = mBufferModel.cursor();
	const std::size_t lineEnd = lineEndOffset(cursor);

	if (!freeCursorMovementEnabled() || mBufferModel.hasSelection() || cursor != lineEnd) return 0;
	return std::max(0, displayedCursorColumn() - actualCursorVisualColumn(cursor));
}

bool MRFileEditor::insertBufferText(const std::string &text) {
	std::string insertedText = text;
	std::size_t start = mBufferModel.cursor();
	std::size_t end = start;
	MRTextBufferModel::Range range;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "insert-buffer-text");

	if (mReadOnly) return false;
	if (!insertedText.empty()) {
		const int paddingColumns = paddingColumnsBeforeInsertAtCursor();
		if (paddingColumns > 0) insertedText.insert(0, static_cast<std::size_t>(paddingColumns), ' ');
	}
	if (mBufferModel.hasSelection()) {
		range = mBufferModel.selection().range();
		start = range.start;
		end = range.end;
	} else if (!mInsertMode) {
		std::size_t endSel = mBufferModel.cursor();
		for (std::string::size_type i = 0; i < insertedText.size() && endSel < lineEndOffset(start); ++i)
			endSel = nextCharOffset(endSel);
		end = endSel;
	}
	range = MRTextBufferModel::Range(start, end).clamped(mBufferModel.length());
	transaction.replace(range, insertedText);
	start = range.start + insertedText.size();
	return applyStagedTransaction(transaction, start, start, start, true).applied();
}


void MRFileEditor::effectiveFormatMargins(const MREditSetupSettings &settings, int &leftMargin, int &rightMargin) const noexcept {
	MRTextFormatting::effectiveMargins(settings, leftMargin, rightMargin);
}

bool MRFileEditor::persistVisibleEditSetupSettings(const MREditSetupSettings &settings, const std::string &errorPrefix) {
	MREditSetupSettings previousSettings = configuredEditSetupSettings();
	MRSettingsWriteReport writeReport;
	std::string errorText;

	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	if (!persistConfiguredSettingsSnapshot(&errorText, &writeReport)) {
		static_cast<void>(setConfiguredEditSetupSettings(previousSettings, nullptr));
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	return true;
}

bool MRFileEditor::previewVisibleEditSetupSettings(const MREditSetupSettings &settings, const std::string &errorPrefix) {
	std::string errorText;

	if (!setConfiguredEditSetupSettings(settings, &errorText)) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	refreshConfiguredVisualSettings();
	return true;
}

bool MRFileEditor::finalizeVisibleEditSetupPreview(const MREditSetupSettings &previousSettings, const std::string &errorPrefix) {
	MRSettingsWriteReport writeReport;
	std::string errorText;

	if (persistConfiguredSettingsSnapshot(&errorText, &writeReport)) return true;
	static_cast<void>(setConfiguredEditSetupSettings(previousSettings, nullptr));
	refreshConfiguredVisualSettings();
	mr::messageline::postAutoTimed(mr::messageline::Owner::DialogInteraction, errorPrefix + errorText, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
	return false;
}


bool MRFileEditor::wrapCurrentLineOnce(int leftMargin, int rightMargin) {
	const std::size_t cursor = cursorOffset();
	const std::size_t lineStart = lineStartOffset(cursor);
	const std::size_t lineEnd = lineEndOffset(cursor);
	const int safeLeftMargin = std::max(1, leftMargin);
	const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
	const int lineWidth = charColumn(lineStart, lineEnd);
	const std::string indent(static_cast<std::size_t>(safeLeftMargin - 1), ' ');
	const std::string replacement = "\n" + indent;
	std::size_t limitOffset = std::min(charPtrOffset(lineStart, safeRightMargin), lineEnd);
	std::size_t replaceStart = limitOffset;
	std::size_t replaceEnd = limitOffset;
	MRTextBufferModel::StagedTransaction transaction(mBufferModel.readSnapshot(), "live-word-wrap-line");
	std::size_t newCursor = cursor;

	if (lineWidth <= safeRightMargin) return false;
	for (std::size_t probe = limitOffset; probe > lineStart; probe = prevCharOffset(probe)) {
		const std::size_t candidate = prevCharOffset(probe);
		const char ch = charAtOffset(candidate);

		if (ch != ' ' && ch != '\t') continue;
		replaceStart = candidate;
		replaceEnd = probe;
		while (replaceStart > lineStart) {
			const std::size_t previous = prevCharOffset(replaceStart);
			const char previousChar = charAtOffset(previous);
			if (previousChar != ' ' && previousChar != '\t') break;
			replaceStart = previous;
		}
		while (replaceEnd < lineEnd) {
			const char nextChar = charAtOffset(replaceEnd);
			if (nextChar != ' ' && nextChar != '\t') break;
			replaceEnd = nextCharOffset(replaceEnd);
		}
		break;
	}
	transaction.replace(MRTextBufferModel::Range(replaceStart, replaceEnd), replacement);
	if (cursor <= replaceStart) newCursor = cursor;
	else if (cursor >= replaceEnd)
		newCursor = cursor - (replaceEnd - replaceStart) + replacement.size();
	else
		newCursor = replaceStart + replacement.size();
	return applyStagedTransaction(transaction, newCursor, newCursor, newCursor, true).applied();
}

void MRFileEditor::applyLiveWordWrapAfterTextInput() {
	MREditSetupSettings settings = configuredEditSetupSettings();
	int leftMargin = 1;
	int rightMargin = 78;

	if (mWordWrapSuppressed) settings.wordWrap = false;
	if (mReadOnly || !settings.wordWrap) return;
	effectiveFormatMargins(settings, leftMargin, rightMargin);
	for (int wraps = 0; wraps < 64; ++wraps)
		if (!wrapCurrentLineOnce(leftMargin, rightMargin)) break;
}

void MRFileEditor::ensureCursorVisible(bool centerCursor) {
	int visualColumn = displayedCursorColumn();
	int line = static_cast<int>(visibleLineForDocumentLine(cachedCursorLineIndex()));
	int targetX = delta.x;
	int targetY = delta.y;
	int viewportWidth = textViewportWidth();
	int textRows = std::max(1, visibleTextRows());

	if (visualColumn < targetX) targetX = visualColumn;
	else if (visualColumn >= targetX + viewportWidth)
		targetX = visualColumn - viewportWidth + 1;
	if (centerCursor) targetY = std::max(0, line - textRows / 2);
	else if (line < targetY)
		targetY = line;
	else if (line >= targetY + textRows)
		targetY = line - textRows + 1;
	if (targetX != delta.x || targetY != delta.y) scrollTo(targetX, targetY);
}

void MRFileEditor::moveCursor(std::size_t target, bool extendSelection, bool centerCursor, int requestedVisualColumn) {
	const auto startedAt = std::chrono::steady_clock::now();
	target = canonicalCursorOffset(std::min(target, mBufferModel.length()));
	if (extendSelection) {
		std::size_t anchor = mBufferModel.hasSelection() ? mBufferModel.selection().anchor : mBufferModel.cursor();
		mSelectionAnchor = anchor;
		mBufferModel.setCursorAndSelection(target, anchor, target);
	} else {
		if (configuredPersistentBlocksSetting() && mBufferModel.hasSelection()) mBufferModel.setCursor(target);
		else
			mBufferModel.setCursorAndSelection(target, target, target);
		mSelectionAnchor = target;
	}
	std::chrono::steady_clock::duration visualColumnElapsed{};
	std::chrono::steady_clock::duration updateMetricsElapsed{};
	std::chrono::steady_clock::duration ensureVisibleElapsed{};
	std::chrono::steady_clock::duration updateIndicatorElapsed{};
	std::chrono::steady_clock::duration drawViewElapsed{};
	{
		const auto visualColumnStartedAt = std::chrono::steady_clock::now();
		if (freeCursorMovementEnabled() && requestedVisualColumn >= 0) mCursorVisualColumn = std::max(actualCursorVisualColumn(target), requestedVisualColumn);
		else
			mCursorVisualColumn = actualCursorVisualColumn(target);
		visualColumnElapsed = std::chrono::steady_clock::now() - visualColumnStartedAt;
	}
	if (useApproximateLargeFileMetrics()) {
		const auto updateMetricsStartedAt = std::chrono::steady_clock::now();
		updateMetrics();
		updateMetricsElapsed = std::chrono::steady_clock::now() - updateMetricsStartedAt;
	}
	{
		const auto ensureVisibleStartedAt = std::chrono::steady_clock::now();
		ensureCursorVisible(centerCursor);
		ensureVisibleElapsed = std::chrono::steady_clock::now() - ensureVisibleStartedAt;
	}
	scheduleSyntaxWarmupIfNeeded();
	{
		const auto updateIndicatorStartedAt = std::chrono::steady_clock::now();
		updateIndicator();
		updateIndicatorElapsed = std::chrono::steady_clock::now() - updateIndicatorStartedAt;
	}
	{
		const auto drawViewStartedAt = std::chrono::steady_clock::now();
		drawView();
		drawViewElapsed = std::chrono::steady_clock::now() - drawViewStartedAt;
	}
	const auto totalElapsed = std::chrono::steady_clock::now() - startedAt;
	if (totalElapsed >= kSlowNavigationTraceThreshold) {
		std::ostringstream line;
		line << "Phase1 nav moveCursor total_us=" << traceMicros(totalElapsed) << " visual_column_us=" << traceMicros(visualColumnElapsed) << " update_metrics_us=" << traceMicros(updateMetricsElapsed)
		     << " ensure_visible_us=" << traceMicros(ensureVisibleElapsed) << " update_indicator_us=" << traceMicros(updateIndicatorElapsed) << " draw_view_us=" << traceMicros(drawViewElapsed)
		     << " target=" << target << " extend=" << (extendSelection ? 1 : 0) << " center=" << (centerCursor ? 1 : 0) << " requested_visual=" << requestedVisualColumn << " cursor_line=" << cachedCursorLineIndex()
		     << " delta_x=" << delta.x << " delta_y=" << delta.y << " len=" << mBufferModel.length() << " add=" << mBufferModel.document().addBufferLength() << " pieces=" << mBufferModel.document().pieceCount()
		     << " undo=" << mBufferModel.undoStackDepth() << " redo=" << mBufferModel.redoStackDepth();
		appendDirectProbeLog(line.str());
	}
}

bool MRFileEditor::isTextInputEvent(const TEvent &event) const {
	if (event.what != evKeyDown) return false;
	const ushort mods = event.keyDown.controlKeyState;
	const bool plainTab = event.keyDown.charScan.charCode == 9 && (mods & (kbShift | kbCtrlShift | kbAltShift | kbPaste)) == 0;
	const bool singleByteText = event.keyDown.charScan.charCode >= 32 && event.keyDown.charScan.charCode < 255;
	return (event.keyDown.controlKeyState & kbPaste) != 0 || plainTab || singleByteText;
}

void MRFileEditor::handleTextInput(TEvent &event) {
	if (mReadOnly) {
		clearEvent(event);
		return;
	}
	if ((event.keyDown.controlKeyState & kbPaste) != 0) {
		char buf[512];
		size_t length = 0;
		while (textEvent(event, TSpan<char>(buf, sizeof(buf)), length)) {
			const std::string insertedText(buf, length);
			if (insertBufferText(insertedText)) applyLiveSmartDedentAfterTextInput(insertedText);
		}
		applyLiveWordWrapAfterTextInput();
		clearEvent(event);
		return;
	}

	const ushort mods = event.keyDown.controlKeyState;
	const bool plainTab = event.keyDown.charScan.charCode == 9 && (mods & (kbShift | kbCtrlShift | kbAltShift | kbPaste)) == 0;
	std::string insertedText;

	if (plainTab)
		insertedText = tabKeyText();
	else if (event.keyDown.charScan.charCode >= 32 && event.keyDown.charScan.charCode < 255)
		insertedText.assign(1, static_cast<char>(event.keyDown.charScan.charCode));
	else
		insertedText.clear();
	if (insertedText.empty()) {
		clearEvent(event);
		return;
	}
	if (insertBufferText(insertedText)) applyLiveSmartDedentAfterTextInput(insertedText);
	applyLiveWordWrapAfterTextInput();
	clearEvent(event);
}

std::string MRFileEditor::tabKeyText() const {
	if (configuredTabExpandSetting()) return "\t";
	std::size_t insertPos = mBufferModel.cursor();
	const MREditSetupSettings settings = configuredEditSetupSettings();
	if (mBufferModel.hasSelection()) insertPos = mBufferModel.selection().range().start;
	int visualColumn = freeCursorMovementEnabled() && insertPos == mBufferModel.cursor() && !mBufferModel.hasSelection() ? displayedCursorColumn() : charColumn(mBufferModel.lineStart(insertPos), insertPos);
	return std::string(static_cast<std::size_t>(tabDisplayWidth(settings, visualColumn)), ' ');
}

void MRFileEditor::handleEvent(TEvent &event) {
	if (event.what == evKeyDown) {
		const ushort mods = event.keyDown.controlKeyState;
		const bool shiftTabPressed = event.keyDown.keyCode == kbShiftTab || ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && hasShiftModifier(mods));
		if (shiftTabPressed) {
			handleKeyDown(event);
			return;
		}
	}

	TScroller::handleEvent(event);

	if (event.what == evBroadcast) {
		if (event.message.command == cmScrollBarClicked && (event.message.infoPtr == hScrollBar || event.message.infoPtr == vScrollBar)) {
			select();
			clearEvent(event);
			return;
		}
		if (event.message.command == cmScrollBarChanged && (event.message.infoPtr == hScrollBar || event.message.infoPtr == vScrollBar)) {
			clearEvent(event);
			return;
		}
	}

	switch (event.what) {
		case evMouseDown:
			handleMouse(event);
			break;
		case evMouseWheel:
			if (vScrollBar != nullptr) vScrollBar->handleEvent(event);
			if (event.what != evNothing && hScrollBar != nullptr) hScrollBar->handleEvent(event);
			break;
		case evKeyDown:
			handleKeyDown(event);
			break;
		case evCommand:
			handleCommand(event);
			break;
		default:
			break;
	}
}

void MRFileEditor::scrollDraw() {
	int newDeltaX = hScrollBar != nullptr ? hScrollBar->value : 0;
	int newDeltaY = vScrollBar != nullptr ? vScrollBar->value : 0;

	if (newDeltaX != delta.x || newDeltaY != delta.y) {
		delta.x = newDeltaX;
		delta.y = newDeltaY;
		if (useApproximateLargeFileMetrics()) updateMetrics();
		scheduleSyntaxWarmupIfNeeded();
		drawView();
	} else {
		if (useApproximateLargeFileMetrics()) updateMetrics();
		updateIndicator();
	}
}

void MRFileEditor::setState(ushort aState, Boolean enable) {
	TScroller::setState(aState, enable);
	if ((aState & (sfActive | sfSelected)) != 0) syncScrollBarsToState();
	MREditWindow *window = dynamic_cast<MREditWindow *>(owner);
	if (window != nullptr && window->isMinimized()) return;
	if (aState == sfCursorVis || mIndicatorUpdateInProgress) return;
	updateIndicator();
}

void MRFileEditor::handleKeyDown(TEvent &event) {
	ushort key = ctrlToArrow(event.keyDown.keyCode);
	const ushort mods = event.keyDown.controlKeyState;
	bool extend = hasShiftModifier(mods);
	int coalescedPageCount = 1;
	const bool shiftTabPressed = event.keyDown.keyCode == kbShiftTab || ((event.keyDown.keyCode == kbTab || event.keyDown.keyCode == kbCtrlI) && hasShiftModifier(mods));

	if (shiftTabPressed) {
		const std::size_t target = tabStopMoveOffset(cursorOffset(), false);
		if (target != cursorOffset()) setPreferredIndentColumn(charColumn(lineStartOffset(target), target) + 1);
		moveCursor(target, false, false);
		clearEvent(event);
		return;
	}

	if (isTextInputEvent(event)) {
		handleTextInput(event);
		return;
	}

	switch (key) {
		case kbLeft:
			if (freeCursorMovementEnabled() && !extend && !mBufferModel.hasSelection() && displayedCursorColumn() > actualCursorVisualColumn(cursorOffset()))
				moveCursor(cursorOffset(), false, false, displayedCursorColumn() - 1);
			else
				moveCursor(prevCharOffset(cursorOffset()), extend, false);
			break;
		case kbRight:
			if (freeCursorMovementEnabled() && !extend && !mBufferModel.hasSelection() && cursorOffset() == lineEndOffset(cursorOffset()))
				moveCursor(cursorOffset(), false, false, displayedCursorColumn() + 1);
			else
				moveCursor(nextCharOffset(cursorOffset()), extend, false);
			break;
		case kbUp:
			moveCursor(lineMoveOffset(cursorOffset(), -1, displayedCursorColumn()), extend, false, displayedCursorColumn());
			break;
		case kbDown:
			moveCursor(lineMoveOffset(cursorOffset(), 1, displayedCursorColumn()), extend, false, displayedCursorColumn());
			break;
		case kbHome:
			moveCursor(mAutoIndent ? charPtrOffset(lineStartOffset(cursorOffset()), 0) : lineStartOffset(cursorOffset()), extend, false);
			break;
		case kbEnd:
			moveCursor(lineEndOffset(cursorOffset()), extend, false);
			break;
		case kbPgUp:
		{
			static constexpr int maxCoalescedPages = 8;

			if (TApplication *app = dynamic_cast<TApplication *>(TProgram::application); app != nullptr) {
				while (coalescedPageCount < maxCoalescedPages) {
					TEvent queuedEvent;
					std::memset(&queuedEvent, 0, sizeof(queuedEvent));
					static_cast<TView *>(app)->getEvent(queuedEvent, 0);
					if (queuedEvent.what == evNothing) break;
					if (queuedEvent.what == evKeyDown && ctrlToArrow(queuedEvent.keyDown.keyCode) == kbPgUp && queuedEvent.keyDown.controlKeyState == mods) {
						++coalescedPageCount;
						continue;
					}
					app->putEvent(queuedEvent);
					break;
				}
			}
			moveCursor(lineMoveOffset(cursorOffset(), -(std::max(2, visibleTextRows()) - 1) * coalescedPageCount, displayedCursorColumn()), extend, true, displayedCursorColumn());
			break;
		}
		case kbPgDn:
		{
			static constexpr int maxCoalescedPages = 8;

			if (TApplication *app = dynamic_cast<TApplication *>(TProgram::application); app != nullptr) {
				while (coalescedPageCount < maxCoalescedPages) {
					TEvent queuedEvent;
					std::memset(&queuedEvent, 0, sizeof(queuedEvent));
					static_cast<TView *>(app)->getEvent(queuedEvent, 0);
					if (queuedEvent.what == evNothing) break;
					if (queuedEvent.what == evKeyDown && ctrlToArrow(queuedEvent.keyDown.keyCode) == kbPgDn && queuedEvent.keyDown.controlKeyState == mods) {
						++coalescedPageCount;
						continue;
					}
					app->putEvent(queuedEvent);
					break;
				}
			}
			moveCursor(lineMoveOffset(cursorOffset(), (std::max(2, visibleTextRows()) - 1) * coalescedPageCount, displayedCursorColumn()), extend, true, displayedCursorColumn());
			break;
		}
		case kbCtrlHome:
			moveCursor(0, false, false);
			break;
		case kbCtrlEnd:
			moveCursor(bufferLength(), false, false);
			break;
		case kbCtrlLeft:
			moveCursor(prevWordOffset(cursorOffset()), extend, false);
			break;
		case kbCtrlRight:
			moveCursor(nextWordOffset(cursorOffset()), extend, false);
			break;
		case kbEnter:
			if (!mReadOnly) newLineWithPreferredIndent();
			clearEvent(event);
			return;
		case kbBack:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else if (cursorOffset() > 0) replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			}
			clearEvent(event);
			return;
		case kbDel:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else
					deleteCharsAtCursor(1);
			}
			clearEvent(event);
			return;
		case kbIns:
			setInsertModeEnabled(!insertModeEnabled());
			clearEvent(event);
			return;
		case kbShiftIns:
			requestSystemClipboardPaste();
			clearEvent(event);
			return;
		case kbCtrlIns:
			copySelection();
			clearEvent(event);
			return;
		case kbShiftDel:
			cutSelection();
			clearEvent(event);
			return;
		default:
			return;
	}
	clearEvent(event);
}

void MRFileEditor::handleCommand(TEvent &event) {
	switch (event.message.command) {
		case cmSave:
			saveInPlace();
			break;
		case cmSaveAs:
			saveAsWithPrompt();
			break;
		case cmCut:
			cutSelection();
			break;
		case cmCopy:
			copySelection();
			break;
		case cmPaste:
			requestSystemClipboardPaste();
			break;
		case cmMrEditUndo: {
			MRTextBufferModel::CustomUndoRecord record;
			const MRTextBufferModel::ReadSnapshot oldSnapshot = mBufferModel.readSnapshot();
			const std::size_t oldLength = mBufferModel.length();
			const std::size_t oldVersion = mBufferModel.version();
			if (mBufferModel.undo(&record)) {
				const bool modifiedState = mBufferModel.isModified();
				const std::size_t newLength = mBufferModel.length();
				std::size_t prefix = 0;
				while (prefix < oldLength && prefix < newLength && oldSnapshot.charAt(prefix) == mBufferModel.charAt(prefix))
					++prefix;
				std::size_t oldSuffix = oldLength;
				std::size_t newSuffix = newLength;
				while (oldSuffix > prefix && newSuffix > prefix && oldSnapshot.charAt(oldSuffix - 1) == mBufferModel.charAt(newSuffix - 1)) {
					--oldSuffix;
					--newSuffix;
				}
				const std::size_t touchedLength = std::max(oldSuffix - prefix, newSuffix - prefix);
				MRTextBufferModel::DocumentChangeSet changeSet;
				changeSet.changed = true;
				changeSet.oldLength = oldLength;
				changeSet.newLength = newLength;
				changeSet.oldVersion = oldVersion;
				changeSet.newVersion = mBufferModel.version();
				changeSet.touchedRange = MRTextBufferModel::Range(prefix, prefix + touchedLength);
				adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(), mBufferModel.selectionStart(), mBufferModel.selectionEnd(), modifiedState, &changeSet);
				if (owner != nullptr) setBlockOverlayState(record.blockMode, record.blockAnchor, record.blockEnd, record.blockMarkingOn, false);
			}
			break;
		}
		case cmMrEditRedo: {
			MRTextBufferModel::CustomUndoRecord record;
			const MRTextBufferModel::ReadSnapshot oldSnapshot = mBufferModel.readSnapshot();
			const std::size_t oldLength = mBufferModel.length();
			const std::size_t oldVersion = mBufferModel.version();
			if (mBufferModel.redo(&record)) {
				const bool modifiedState = mBufferModel.isModified();
				const std::size_t newLength = mBufferModel.length();
				std::size_t prefix = 0;
				while (prefix < oldLength && prefix < newLength && oldSnapshot.charAt(prefix) == mBufferModel.charAt(prefix))
					++prefix;
				std::size_t oldSuffix = oldLength;
				std::size_t newSuffix = newLength;
				while (oldSuffix > prefix && newSuffix > prefix && oldSnapshot.charAt(oldSuffix - 1) == mBufferModel.charAt(newSuffix - 1)) {
					--oldSuffix;
					--newSuffix;
				}
				const std::size_t touchedLength = std::max(oldSuffix - prefix, newSuffix - prefix);
				MRTextBufferModel::DocumentChangeSet changeSet;
				changeSet.changed = true;
				changeSet.oldLength = oldLength;
				changeSet.newLength = newLength;
				changeSet.oldVersion = oldVersion;
				changeSet.newVersion = mBufferModel.version();
				changeSet.touchedRange = MRTextBufferModel::Range(prefix, prefix + touchedLength);
				adoptCommittedDocument(mBufferModel.document(), mBufferModel.cursor(), mBufferModel.selectionStart(), mBufferModel.selectionEnd(), modifiedState, &changeSet);
				if (owner != nullptr) setBlockOverlayState(record.blockMode, record.blockAnchor, record.blockEnd, record.blockMarkingOn, false);
			}
			break;
		}
		case cmMrTextUpperCaseMenu:
			convertSelectionToUpperCase();
			break;
		case cmMrTextLowerCaseMenu:
			convertSelectionToLowerCase();
			break;
		case cmMrTextCenterLine:
			if (!mReadOnly) {
				MREditSetupSettings settings = configuredEditSetupSettings();
				centerCurrentLine(settings.leftMargin, settings.rightMargin > 0 ? settings.rightMargin : 78);
			}
			break;
		case cmMrTextReformatParagraph:
			if (!mReadOnly) {
				MREditSetupSettings settings = configuredEditSetupSettings();
				formatParagraph(settings.leftMargin, settings.rightMargin > 0 ? settings.rightMargin : 78);
			}
			break;
		case cmClear:
			if (!mReadOnly) replaceSelectionText(std::string());
			break;
		case cmCharLeft:
			if (freeCursorMovementEnabled() && !mBufferModel.hasSelection() && displayedCursorColumn() > actualCursorVisualColumn(cursorOffset()))
				moveCursor(cursorOffset(), false, false, displayedCursorColumn() - 1);
			else
				moveCursor(prevCharOffset(cursorOffset()), false, false);
			break;
		case cmCharRight:
			if (freeCursorMovementEnabled() && !mBufferModel.hasSelection() && cursorOffset() == lineEndOffset(cursorOffset()))
				moveCursor(cursorOffset(), false, false, displayedCursorColumn() + 1);
			else
				moveCursor(nextCharOffset(cursorOffset()), false, false);
			break;
		case cmWordLeft:
			moveCursor(prevWordOffset(cursorOffset()), false, false);
			break;
		case cmWordRight:
			moveCursor(nextWordOffset(cursorOffset()), false, false);
			break;
		case cmLineStart:
			moveCursor(lineStartOffset(cursorOffset()), false, false);
			break;
		case cmLineEnd:
			moveCursor(lineEndOffset(cursorOffset()), false, false);
			break;
		case cmLineUp:
			moveCursor(lineMoveOffset(cursorOffset(), -1, displayedCursorColumn()), false, false, displayedCursorColumn());
			break;
		case cmLineDown:
			moveCursor(lineMoveOffset(cursorOffset(), 1, displayedCursorColumn()), false, false, displayedCursorColumn());
			break;
		case cmPageUp:
			moveCursor(lineMoveOffset(cursorOffset(), -(std::max(2, visibleTextRows()) - 1), displayedCursorColumn()), false, true, displayedCursorColumn());
			break;
		case cmPageDown:
			moveCursor(lineMoveOffset(cursorOffset(), std::max(2, visibleTextRows()) - 1, displayedCursorColumn()), false, true, displayedCursorColumn());
			break;
		case cmTextStart:
			moveCursor(0, false, false);
			break;
		case cmTextEnd:
			moveCursor(bufferLength(), false, false);
			break;
		case cmNewLine:
			if (!mReadOnly) newLineWithPreferredIndent();
			break;
		case cmBackSpace:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else if (cursorOffset() > 0) replaceRangeAndSelect(static_cast<uint>(prevCharOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			}
			break;
		case cmDelChar:
			if (!mReadOnly) {
				if (mBufferModel.hasSelection()) replaceSelectionText(std::string());
				else
					deleteCharsAtCursor(1);
			}
			break;
		case cmDelWord:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(cursorOffset()), static_cast<uint>(nextWordOffset(cursorOffset())), "", 0);
			break;
		case cmDelWordLeft:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(prevWordOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			break;
		case cmDelStart:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(lineStartOffset(cursorOffset())), static_cast<uint>(cursorOffset()), "", 0);
			break;
		case cmDelEnd:
			if (!mReadOnly) replaceRangeAndSelect(static_cast<uint>(cursorOffset()), static_cast<uint>(lineEndOffset(cursorOffset())), "", 0);
			break;
		case cmDelLine:
			if (!mReadOnly) deleteCurrentLineText();
			break;
		case cmInsMode:
			setInsertModeEnabled(!insertModeEnabled());
			break;
		case cmSelectAll:
			mSelectionAnchor = 0;
			mBufferModel.setCursorAndSelection(mBufferModel.length(), 0, mBufferModel.length());
			revealCursor(True);
			break;
		default:
			return;
	}
	clearEvent(event);
}

void MRFileEditor::handleMouse(TEvent &event) {
	const TextViewportGeometry viewport = textViewportGeometry();
	const TPoint local = makeLocal(event.mouse.where);
	std::size_t foldLineIndex = 0;
	auto gutterSpanAtPoint = [this, &local, &viewport](std::size_t lineIndex) noexcept -> const MRFoldSpan * {
		const std::vector<unsigned short> &displayLevels = mFoldState.visibleState().displayLevels;
		const std::vector<MRFoldSpan> &visibleSpans = mFoldState.visibleState().spans;
		const int displayColumn = local.x - viewport.codeFoldingX;
		if (displayColumn < 0 || static_cast<std::size_t>(displayColumn) >= displayLevels.size()) return nullptr;
		const unsigned short level = displayLevels[static_cast<std::size_t>(displayColumn)];
		for (const MRFoldSpan &span : visibleSpans) {
			if (span.level != level) continue;
			if (!span.open) {
				if (span.startLine == lineIndex) return &span;
				continue;
			}
			if (lineIndex >= span.startLine && lineIndex <= span.endLine) return &span;
		}
		return nullptr;
	};
	auto toggleFoldColumnsFromPoint = [this, &local, &viewport]() -> bool {
		const std::vector<unsigned short> &displayLevels = mFoldState.visibleState().displayLevels;
		const std::vector<MRFoldSpan> &visibleSpans = mFoldState.visibleState().spans;
		std::map<std::size_t, MRFoldSpan> &closedFoldSpans = mFoldState.closedFoldSpans();
		const int displayColumn = local.x - viewport.codeFoldingX;
		if (displayColumn < 0 || static_cast<std::size_t>(displayColumn) >= displayLevels.size()) return false;
		const unsigned short level = displayLevels[static_cast<std::size_t>(displayColumn)];
		bool anyOpen = false;
		for (const MRFoldSpan &span : visibleSpans)
			if (span.level >= level && span.open) {
				anyOpen = true;
				break;
			}
		bool changed = false;
		std::size_t cursorLine = mBufferModel.lineIndex(mBufferModel.cursor());
		std::size_t foldCursorTarget = cursorLine;
		bool foldCursorTargetValid = false;

		for (const MRFoldSpan &span : visibleSpans) {
			if (anyOpen) {
				if (span.level < level) continue;
				if (!span.open) continue;
				closedFoldSpans[span.startLine] = MRFoldSpan(span.startLine, span.endLine, span.level, span.sourceKind, false, span.siblingContinuation);
				if (cursorLine > span.startLine && cursorLine <= span.endLine && (!foldCursorTargetValid || span.startLine < foldCursorTarget)) {
					foldCursorTarget = span.startLine;
					foldCursorTargetValid = true;
				}
			} else {
				if (span.level != level) continue;
				if (span.open) continue;
				closedFoldSpans.erase(span.startLine);
			}
			changed = true;
		}
		if (!changed) return false;
		mFoldState.rebuildEffectiveClosedFolds();
		if (foldCursorTargetValid) moveCursor(mBufferModel.lineStartByIndex(foldCursorTarget), false, false);
		if (mFoldState.warmupState().taskId != 0) {
			static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mFoldState.warmupState().taskId));
			clearFoldWarmupTask(mFoldState.warmupState().taskId);
		}
		invalidateFoldCache(true);
		updateMetrics();
		drawView();
		updateIndicator();
		return true;
	};

	if ((event.mouse.buttons & (mbLeftButton | mbRightButton)) == 0) return;
	if (dragFormatRulerAtLocalPoint(event, local)) {
		clearEvent(event);
		return;
	}
	if (foldingGutterHit(local, &foldLineIndex)) {
		if ((event.mouse.buttons & mbRightButton) != 0 && toggleFoldColumnsFromPoint()) {
			clearEvent(event);
			return;
		}
		if ((event.mouse.buttons & mbLeftButton) != 0) {
			const MRFoldSpan *clickedSpan = gutterSpanAtPoint(foldLineIndex);
			if (clickedSpan != nullptr && toggleFoldAtLine(clickedSpan->startLine)) {
				updateMetrics();
				drawView();
				updateIndicator();
				clearEvent(event);
				return;
			}
		}
	}

	select();
	int targetColumn = 0;
	std::size_t anchor = mouseOffset(local, &targetColumn);
	mSelectionAnchor = anchor;
	mBufferModel.setCursorAndSelection(anchor, anchor, anchor);
	if (freeCursorMovementEnabled()) mCursorVisualColumn = std::max(actualCursorVisualColumn(anchor), targetColumn);
	else
		mCursorVisualColumn = actualCursorVisualColumn(anchor);
	updateIndicator();
	drawView();

	while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseWheel)) {
		if (event.what == evMouseMove || event.what == evMouseAuto) {
			const TPoint mouse = makeLocal(event.mouse.where);
			int dx = delta.x;
			int dy = delta.y;
			if (mouse.x < viewport.textLeft) --dx;
			else if (mouse.x >= viewport.textRight)
				++dx;
			if (mouse.y < viewport.topInset) --dy;
			else if (mouse.y >= viewport.topInset + std::max(0, visibleTextRows()))
				++dy;
			if (dx != delta.x || dy != delta.y) scrollTo(std::max(dx, 0), std::max(dy, 0));
		} else if (event.what == evMouseWheel) {
			if (vScrollBar != nullptr) vScrollBar->handleEvent(event);
			if (event.what != evNothing && hScrollBar != nullptr) hScrollBar->handleEvent(event);
		}
		int dragColumn = 0;
		std::size_t target = mouseOffset(makeLocal(event.mouse.where), &dragColumn);
		mBufferModel.setCursorAndSelection(target, mSelectionAnchor, target);
		if (freeCursorMovementEnabled()) mCursorVisualColumn = std::max(actualCursorVisualColumn(target), dragColumn);
		else
			mCursorVisualColumn = actualCursorVisualColumn(target);
		updateIndicator();
		drawView();
	}
	clearEvent(event);
}

std::size_t MRFileEditor::mouseOffset(TPoint local, int *visualColumnOut) noexcept {
	TextViewportGeometry viewport = textViewportGeometry();
	const int textRows = std::max(1, visibleTextRows());
	int clampedY = std::max(0, std::min(local.y - viewport.topInset, textRows - 1));
	int row = clampedY + delta.y;
	int column = viewport.textColumnFromLocalX(local.x);
	std::size_t start = mBufferModel.lineStartByIndex(documentLineForVisibleLine(static_cast<std::size_t>(std::max(row, 0))));
	if (visualColumnOut != nullptr) *visualColumnOut = column;
	return canonicalCursorOffset(charPtrOffset(start, column));
}

std::size_t MRFileEditor::canonicalCursorOffset(std::size_t pos) const noexcept {
	pos = std::min(pos, mBufferModel.length());
	if (pos > 0 && pos < mBufferModel.length() && mBufferModel.charAt(pos) == '\n' && mBufferModel.charAt(pos - 1) == '\r') return pos - 1;
	return pos;
}

void MRFileEditor::copySelection() {
	if (!mBufferModel.hasSelection()) return;
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	const std::string text = mBufferModel.text().substr(range.start, range.length());
	TClipboard::setText(TStringView(text.data(), text.size()));
}

void MRFileEditor::cutSelection() {
	if (mReadOnly || !mBufferModel.hasSelection()) return;
	copySelection();
	replaceSelectionText(std::string());
}

void MRFileEditor::requestSystemClipboardPaste() {
	if (mReadOnly) return;
	TClipboard::requestText();
}

void MRFileEditor::replaceSelectionText(const std::string &text) {
	if (!mBufferModel.hasSelection()) {
		if (!text.empty()) insertBufferText(text);
		return;
	}
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	replaceRangeAndSelect(static_cast<uint>(range.start), static_cast<uint>(range.end), text.data(), static_cast<uint>(text.size()));
}

void MRFileEditor::convertSelectionToUpperCase() {
	if (mReadOnly || !mBufferModel.hasSelection()) return;
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	std::string text = mBufferModel.text().substr(range.start, range.length());
	for (char &c : text)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	replaceSelectionText(text);
	setSelectionOffsets(range.start, range.start + text.length());
}

void MRFileEditor::convertSelectionToLowerCase() {
	if (mReadOnly || !mBufferModel.hasSelection()) return;
	MRTextBufferModel::Range range = mBufferModel.selection().range();
	std::string text = mBufferModel.text().substr(range.start, range.length());
	for (char &c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	replaceSelectionText(text);
	setSelectionOffsets(range.start, range.start + text.length());
}

bool MRFileEditor::writeDocumentToPath(const char *targetPath) {
	char drive[MAXDRIVE];
	char dir[MAXDIR];
	char file[MAXFILE];
	char ext[MAXEXT];
	MRTextSaveOptions saveOptions;
	const std::size_t pieceCount = mBufferModel.document().pieceCount();

	resolveSaveOptionsForPath(targetPath, saveOptions);

	if (configuredBackupFilesSetting()) {
		fnsplit(targetPath, drive, dir, file, ext);
		char backupName[MAXPATH];
		fnmerge(backupName, drive, dir, file, ".bak");
		unlink(backupName);
		rename(targetPath, backupName);
	}

	std::ofstream out(targetPath, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out) {
		TEditor::editorDialog(edCreateError, targetPath);
		return false;
	}
	auto failWrite = [&]() -> bool {
		TEditor::editorDialog(edWriteError, targetPath);
		return false;
	};

	if (saveOptions.binaryMode) {
		for (std::size_t i = 0; i < pieceCount; ++i) {
			mr::editor::PieceChunkView chunk = mBufferModel.document().pieceChunk(i);
			writeChunk(out, chunk.data, chunk.length);
			if (!out) return failWrite();
		}
		return true;
	}
	const std::size_t sourceBytes = mBufferModel.document().length();
	const auto normalizeStartedAt = std::chrono::steady_clock::now();
	const std::size_t flushThresholdBytes = static_cast<std::size_t>(256) * 1024;
	MRTextSaveStreamState normalizeState;
	std::string outputBuffer;
	auto flushOutput = [&]() -> bool {
		if (outputBuffer.empty()) return true;
		writeChunk(out, outputBuffer.data(), outputBuffer.size());
		outputBuffer.clear();
		return static_cast<bool>(out);
	};

	outputBuffer.reserve(flushThresholdBytes + 1024);
	for (std::size_t i = 0; i < pieceCount; ++i) {
		mr::editor::PieceChunkView chunk = mBufferModel.document().pieceChunk(i);
		if (chunk.length == 0) continue;
		appendNormalizedTextSaveChunk(std::string_view(chunk.data, chunk.length), saveOptions, normalizeState, outputBuffer);
		if (outputBuffer.size() >= flushThresholdBytes && !flushOutput()) return failWrite();
	}
	finalizeNormalizedTextSaveStream(saveOptions, normalizeState, outputBuffer);
	if (!flushOutput()) return failWrite();

	noteSaveNormalizationThroughput(sourceBytes, static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - normalizeStartedAt).count()));
	if (!out) return failWrite();
	return true;
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



Boolean MRFileEditor::confirmSaveOrDiscardUntitled() {
	const char *detail = nullptr;
	std::string persistentName;

	if (hasPersistentFileName()) {
		persistentName = trimAscii(fileName);
		if (!persistentName.empty() && upperAscii(persistentName) != "?NO-FILE?") detail = persistentName.c_str();
	}
	const auto startedAt = std::chrono::steady_clock::now();
	appendDirectProbeLog("Phase1 discard untitled dialog begin");
	const mr::dialogs::UnsavedChangesChoice choice = mr::dialogs::showUnsavedChangesDialog("Save As", "Window has unsaved changes.", detail);
	{
		std::ostringstream trace;
		trace << "Phase1 discard untitled dialog end total_us=" << traceMicros(std::chrono::steady_clock::now() - startedAt) << " choice=" << static_cast<int>(choice);
		appendDirectProbeLog(trace.str());
	}
	switch (choice) {
		case mr::dialogs::UnsavedChangesChoice::Save:
			return saveAsWithPrompt();
		case mr::dialogs::UnsavedChangesChoice::Discard:
			appendDirectProbeLog("Phase1 discard untitled accepted");
			setDocumentModified(false);
			return True;
		default:
			return False;
	}
}

Boolean MRFileEditor::confirmSaveOrDiscardNamed() {
	const auto startedAt = std::chrono::steady_clock::now();
	appendDirectProbeLog("Phase1 discard named dialog begin");
	const mr::dialogs::UnsavedChangesChoice choice = mr::dialogs::showUnsavedChangesDialog("Save", "Save changes to:", fileName);
	{
		std::ostringstream trace;
		trace << "Phase1 discard named dialog end total_us=" << traceMicros(std::chrono::steady_clock::now() - startedAt) << " choice=" << static_cast<int>(choice);
		appendDirectProbeLog(trace.str());
	}
	switch (choice) {
		case mr::dialogs::UnsavedChangesChoice::Save:
			return saveInPlace();
		case mr::dialogs::UnsavedChangesChoice::Discard:
			appendDirectProbeLog("Phase1 discard named accepted");
			setDocumentModified(false);
			return True;
		default:
			return False;
	}
}


void MRFileEditor::clearDirtyRanges() noexcept {
	mDirtyRanges.clear();
}

void MRFileEditor::normalizePairRangeList(std::vector<std::pair<std::size_t, std::size_t>> &ranges) {
	std::sort(ranges.begin(), ranges.end(), [](const std::pair<std::size_t, std::size_t> &a, const std::pair<std::size_t, std::size_t> &b) { return a.first < b.first || (a.first == b.first && a.second < b.second); });
	std::vector<std::pair<std::size_t, std::size_t>> merged;
	for (const auto &item : ranges) {
		if (item.second <= item.first) continue;
		if (merged.empty() || item.first > merged.back().second) merged.push_back(item);
		else if (item.second > merged.back().second)
			merged.back().second = item.second;
	}
	ranges.swap(merged);
}

void MRFileEditor::normalizeRangeList(std::vector<MRTextBufferModel::Range> &ranges) {
	std::sort(ranges.begin(), ranges.end(), [](const MRTextBufferModel::Range &a, const MRTextBufferModel::Range &b) { return a.start < b.start || (a.start == b.start && a.end < b.end); });
	std::vector<MRTextBufferModel::Range> merged;
	for (const MRTextBufferModel::Range &item : ranges) {
		if (item.end <= item.start) continue;
		if (merged.empty() || item.start > merged.back().end) merged.push_back(item);
		else if (item.end > merged.back().end)
			merged.back().end = item.end;
	}
	ranges.swap(merged);
}

void MRFileEditor::normalizeDirtyRanges() {
	normalizeRangeList(mDirtyRanges);
}

void MRFileEditor::pushMappedDirtyRange(std::vector<MRTextBufferModel::Range> &mapped, std::size_t start, std::size_t end, std::size_t maxLength) {
	start = std::min(start, maxLength);
	end = std::min(end, maxLength);
	if (end <= start) return;
	mapped.push_back(MRTextBufferModel::Range(start, end));
}

void MRFileEditor::remapDirtyRangesForAppliedChange(const MRTextBufferModel::DocumentChangeSet &change) {
	const std::size_t oldLength = change.oldLength;
	const std::size_t newLength = change.newLength;
	const MRTextBufferModel::Range touched = change.touchedRange.normalized();
	const long long delta = static_cast<long long>(newLength) - static_cast<long long>(oldLength);
	const std::size_t touchedLength = touched.length();
	const std::size_t editStart = std::min(touched.start, oldLength);
	std::size_t replacedOldLength = touchedLength;

	if (mDirtyRanges.empty()) return;
	if (delta >= 0) {
		const std::size_t deltaUnsigned = static_cast<std::size_t>(delta);
		replacedOldLength = touchedLength > deltaUnsigned ? touchedLength - deltaUnsigned : 0;
	}
	if (replacedOldLength > oldLength - editStart) replacedOldLength = oldLength - editStart;
	const std::size_t oldEditEnd = editStart + replacedOldLength;

	std::vector<MRTextBufferModel::Range> mapped;
	mapped.reserve(mDirtyRanges.size() + 2);

	for (std::size_t i = 0; i < mDirtyRanges.size(); ++i) {
		MRTextBufferModel::Range range = mDirtyRanges[i].clamped(oldLength).normalized();

		if (range.end <= range.start) continue;
		if (range.end <= editStart) {
			pushMappedDirtyRange(mapped, range.start, range.end, newLength);
			continue;
		}
		if (range.start >= oldEditEnd) {
			const long long shiftedStart = static_cast<long long>(range.start) + delta;
			const long long shiftedEnd = static_cast<long long>(range.end) + delta;
			if (shiftedEnd <= 0) continue;
			pushMappedDirtyRange(mapped, static_cast<std::size_t>(std::max<long long>(0, shiftedStart)), static_cast<std::size_t>(std::max<long long>(0, shiftedEnd)), newLength);
			continue;
		}

		if (range.start < editStart) pushMappedDirtyRange(mapped, range.start, editStart, newLength);
		if (range.end > oldEditEnd) {
			const long long shiftedStart = static_cast<long long>(oldEditEnd) + delta;
			const long long shiftedEnd = static_cast<long long>(range.end) + delta;
			if (shiftedEnd > 0) pushMappedDirtyRange(mapped, static_cast<std::size_t>(std::max<long long>(0, shiftedStart)), static_cast<std::size_t>(std::max<long long>(0, shiftedEnd)), newLength);
		}
	}

	mDirtyRanges.swap(mapped);
	normalizeDirtyRanges();
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

void MRFileEditor::addDirtyRange(MRTextBufferModel::Range range) {
	if (mBufferModel.length() == 0) return;
	range = range.clamped(mBufferModel.length());
	range.normalize();
	if (range.empty()) {
		std::size_t point = std::min(range.start, mBufferModel.length() - 1);
		range = MRTextBufferModel::Range(point, point + 1);
	}
	mDirtyRanges.push_back(range);
	normalizeDirtyRanges();
}

bool MRFileEditor::isDirtyOffset(std::size_t pos) const noexcept {
	if (mDirtyRanges.empty() || mBufferModel.length() == 0) return false;
	if (pos >= mBufferModel.length()) return false;
	for (const MRTextBufferModel::Range &item : mDirtyRanges) {
		if (item.end <= pos) continue;
		if (item.start > pos) break;
		return pos < item.end;
	}
	return false;
}
