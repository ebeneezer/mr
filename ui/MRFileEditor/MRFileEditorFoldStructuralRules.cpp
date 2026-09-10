#include "MRFileEditorFoldAnalysis.hpp"
#include "../MRSyntaxBasic.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>

namespace mr::editor::fold {

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

bool xmlLineContainsMatchingCloseTag(std::string_view text, std::size_t pos, std::string_view tagName) noexcept {
	while (pos < text.size()) {
		const std::size_t closeStart = text.find("</", pos);
		if (closeStart == std::string_view::npos) return false;
		const std::size_t nameStart = closeStart + 2;
		std::size_t nameEnd = skipXmlName(text, nameStart);

		if (nameEnd > nameStart && text.substr(nameStart, nameEnd - nameStart) == tagName) {
			while (nameEnd < text.size() && std::isspace(static_cast<unsigned char>(text[nameEnd])) != 0)
				++nameEnd;
			if (nameEnd < text.size() && text[nameEnd] == '>') return true;
		}
		pos = nameStart;
	}
	return false;
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
	if (xmlLineContainsMatchingCloseTag(normalized, closePos + 1, tagName)) return false;
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
	std::size_t braceDepth = 0;
	std::size_t bracketDepth = 0;
	std::size_t parenthesisDepth = 0;

	for (std::size_t tokenStart = 0; tokenStart < lineText.size(); ++tokenStart) {
		const char ch = lineText[tokenStart];
		if (ch == '{') {
			++braceDepth;
			continue;
		}
		if (ch == '[') {
			++bracketDepth;
			continue;
		}
		if (ch == '(') {
			++parenthesisDepth;
			continue;
		}
		if (ch == '}' && braceDepth > 0) {
			--braceDepth;
			continue;
		}
		if (ch == ']' && bracketDepth > 0) {
			--bracketDepth;
			continue;
		}
		if (ch == ')' && parenthesisDepth > 0) {
			--parenthesisDepth;
			continue;
		}
		if (isIndentWhitespace(ch)) continue;
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
		{"\\appendix", 1},
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

bool isLatexRawTextEnvironment(std::string_view environmentName) noexcept {
	return environmentName == "verbatim" || environmentName == "verbatim*" || environmentName == "lstlisting" || environmentName == "minted";
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
} // namespace mr::editor::fold
