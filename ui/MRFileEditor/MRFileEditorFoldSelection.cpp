#include "MRFileEditorFoldAnalysis.hpp"
#include "../MRSyntaxBasic.hpp"

namespace mr::editor::fold {

MRFoldSelectionLine classifyFoldSelectionLine(MRSyntaxLanguage language, std::string_view line, const std::vector<std::string> &recentLines,
    const std::vector<MRFoldOpenBlockState> &openBlocks, std::size_t lineIndex) {
	MRFoldSelectionLine result;
	result.startLine = lineIndex;
	line = trimView(line);
	if (line.empty()) return result;
	const std::string upper = upperAscii(std::string(line));
	std::string_view selector;
	bool arrowArms = false;
	bool colonArms = false;
	bool keywordArms = false;

	switch (language) {
		case MRSyntaxLanguage::C:
		case MRSyntaxLanguage::Cpp:
		case MRSyntaxLanguage::JavaScript:
		case MRSyntaxLanguage::Swift:
			selector = "SWITCH";
			colonArms = true;
			break;
		case MRSyntaxLanguage::CSharp:
			selector = "SWITCH";
			colonArms = true;
			arrowArms = true;
			break;
		case MRSyntaxLanguage::Go:
			selector = containsUpperToken(upper, "SELECT") ? "SELECT" : "SWITCH";
			colonArms = true;
			break;
		case MRSyntaxLanguage::Rust:
			selector = "MATCH";
			arrowArms = true;
			break;
		case MRSyntaxLanguage::Kotlin:
			selector = "WHEN";
			arrowArms = true;
			break;
		case MRSyntaxLanguage::Perl:
			selector = "GIVEN";
			keywordArms = startsWithKeywordToken(upper, "WHEN") || startsWithKeywordToken(upper, "DEFAULT");
			break;
		case MRSyntaxLanguage::Python:
			result.opensSelection = startsWithKeywordToken(upper, "MATCH") && line.back() == ':';
			keywordArms = startsWithKeywordToken(upper, "CASE");
			break;
		case MRSyntaxLanguage::Bash:
		case MRSyntaxLanguage::Zsh:
			result.opensSelection = startsWithKeywordToken(upper, "CASE") && upper.ends_with(" IN");
			result.closesSelection = upper == "ESAC" || upper == "ESAC;";
			break;
		case MRSyntaxLanguage::Fish:
			result.opensSelection = isFishSwitchLead(upper);
			result.closesSelection = isFishEndLead(upper);
			keywordArms = isFishCaseLead(upper);
			break;
		case MRSyntaxLanguage::Pascal:
			result.opensSelection = isPascalCaseLead(upper);
			result.closesSelection = isPascalEndLead(upper);
			keywordArms = startsWithKeywordToken(upper, "ELSE");
			break;
		case MRSyntaxLanguage::Basic: {
			const MRBasicBlockLine basic = mrBasicClassifyBlockLine(line);
			if (basic.kind == MRBasicBlockKind::Select) {
				result.opensSelection = basic.disposition == MRBasicBlockDisposition::Open;
				result.closesSelection = basic.disposition == MRBasicBlockDisposition::Close;
				keywordArms = basic.disposition == MRBasicBlockDisposition::Continue;
			}
			break;
		}
		default:
			return result;
	}

	// A selector header may end on a later line. Stop at the preceding statement
	// or block; the bounded history must never associate an unrelated brace.
	if (!selector.empty() && line.back() == '{') {
		int braces = 0;
		for (char ch : line) {
			if (ch == '{') ++braces;
			else if (ch == '}') --braces;
		}
		if (braces == 1) {
			for (std::size_t distance = 0; distance <= recentLines.size(); ++distance) {
				const std::string candidate = distance == 0 ? upper : upperAscii(std::string(trimView(recentLines[recentLines.size() - distance])));
				const std::string_view header = distance == 0 ? trimView(std::string_view(candidate).substr(0, candidate.size() - 1)) : trimView(candidate);
				if (header.find_first_of("{}") != std::string_view::npos) break;
				if (containsUpperToken(header, selector) || (language == MRSyntaxLanguage::Go && containsUpperToken(header, "SELECT"))) {
					result.opensSelection = true;
					result.closer = '}';
					result.startLine = lineIndex - distance;
					break;
				}
				if (header.find(';') != std::string_view::npos || header.find(':') != std::string_view::npos || header.find("=>") != std::string_view::npos || header.find("->") != std::string_view::npos) break;
			}
		}
	}
	if (result.opensSelection) return result;

	// Only the direct body of a selector can introduce a sibling arm.
	std::size_t parent = openBlocks.size();
	if (parent > 0 && openBlocks[parent - 1].languageBlockKind == kSelectionArm && openBlocks[parent - 1].closer == 0) --parent;
	if (parent == 0 || openBlocks[parent - 1].languageBlockKind != kSelectionBlock) {
		result.closesSelection = false;
		if (!openBlocks.empty() && openBlocks.back().languageBlockKind == kSelectionArm && openBlocks.back().closer == 'E' && isPascalEndLead(upper))
			result.closesArm = true;
		return result;
	}
	if (result.closesSelection) return result;
	result.opensArm = keywordArms;
	std::string continuedPattern;
	if (arrowArms) {
		const std::size_t arrow = line.find(language == MRSyntaxLanguage::Kotlin ? "->" : "=>");
		if (arrow != std::string_view::npos) {
			int unmatchedClosers = 0;
			for (char ch : line.substr(0, arrow)) {
				if (ch == ')' || ch == ']' || ch == '}') ++unmatchedClosers;
				else if (ch == '(' || ch == '[' || ch == '{') --unmatchedClosers;
			}
			bool patternAlternative = language == MRSyntaxLanguage::Rust && line.front() == '|';
			for (std::size_t distance = 1; distance <= recentLines.size(); ++distance) {
				if (lineIndex - distance <= openBlocks.back().startLine) break;
				const std::string_view previous = trimView(recentLines[recentLines.size() - distance]);
				if (previous.empty()) continue;
				const bool continuedAlternative = (language == MRSyntaxLanguage::Kotlin && previous.back() == ',' && previous.find("->") == std::string_view::npos) ||
				    (language == MRSyntaxLanguage::Rust && previous.back() == '|');
				if (unmatchedClosers <= 0 && !patternAlternative && !continuedAlternative) break;
				if (continuedPattern.empty()) continuedPattern.assign(line);
				continuedPattern.insert(0, "\n");
				continuedPattern.insert(0, previous);
				result.startLine = lineIndex - distance;
				for (char ch : previous) {
					if (ch == ')' || ch == ']' || ch == '}') ++unmatchedClosers;
					else if (ch == '(' || ch == '[' || ch == '{') --unmatchedClosers;
				}
				patternAlternative = language == MRSyntaxLanguage::Rust && previous.front() == '|';
			}
			if (unmatchedClosers > 0) return result;
			if (!continuedPattern.empty()) line = continuedPattern;
		}
	}

	int parentheses = 0;
	int brackets = 0;
	int braces = 0;
	int conditional = 0;
	std::size_t separator = std::string_view::npos;
	std::size_t separatorLength = 1;
	const bool caseLabel = startsWithKeywordToken(upper, "CASE") || startsWithKeywordToken(upper, "DEFAULT");
	for (std::size_t index = 0; index < line.size(); ++index) {
		const char ch = line[index];
		const char next = index + 1 < line.size() ? line[index + 1] : 0;
		if (ch == '(') ++parentheses;
		else if (ch == ')') {
			if (parentheses > 0) {
				--parentheses;
				if (parentheses == 0 && line.front() == '(' && (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh)) {
					separator = index;
					break;
				}
			}
			else if (language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) {
				separator = index;
				break;
			}
		} else if (ch == '[') ++brackets;
		else if (ch == ']') --brackets;
		else if (ch == '{') ++braces;
		else if (ch == '}') --braces;
		if (parentheses != 0 || brackets != 0 || braces != 0) continue;
		if (ch == '?' && next != '?' && next != '.' && language != MRSyntaxLanguage::Swift) ++conditional;
		if (ch == ':' && next != ':' && (index == 0 || line[index - 1] != ':')) {
			if (conditional > 0) --conditional;
			else if ((colonArms && caseLabel) || language == MRSyntaxLanguage::Pascal) {
				if (next != '=') separator = index;
				break;
			}
		}
		if (arrowArms && next == '>' && ch == (language == MRSyntaxLanguage::Kotlin ? '-' : '=')) {
			separator = index;
			separatorLength = 2;
			break;
		}
	}
	if (separator != std::string_view::npos) result.opensArm = true;
	// Colon labels can span lines; their header belongs to the arm as well.
	if (colonArms && caseLabel) result.opensArm = true;
	result.joinsLabels = colonArms && caseLabel;
	if (result.opensArm) {
		const std::string_view body = separator == std::string_view::npos ? line : trimView(line.substr(separator + separatorLength));
		result.hasBody = separator != std::string_view::npos && !body.empty();
		if (!body.empty() && body.back() == '{') result.closer = '}';
		if (language == MRSyntaxLanguage::Pascal && (startsWithKeywordToken(upperAscii(std::string(body)), "BEGIN") || upper == "ELSE BEGIN")) result.closer = 'E';
	}
	if ((language == MRSyntaxLanguage::Bash || language == MRSyntaxLanguage::Zsh) &&
	    (line.ends_with(";;") || line.ends_with(";&") || line.ends_with(";;&") || line.ends_with(";|")))
		result.closesArm = true;
	return result;
}

} // namespace mr::editor::fold
