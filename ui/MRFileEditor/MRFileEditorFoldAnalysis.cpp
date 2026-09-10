#include "MRFileEditorFoldAnalysis.hpp"
#include "../MRSyntaxBasic.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <string_view>

namespace mr::editor::fold {

using MRFoldOpenBlock = MRFoldOpenBlockState;

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

MRFoldScanOutput computeFoldSpansForLineTexts(const std::vector<std::string> &lineTexts, std::size_t processedLineCount, std::size_t baseLineIndex, std::size_t topLine,
	                                              std::size_t requestBottomLine, MRSyntaxLanguage language, const std::map<std::size_t, MRFoldSpan> &closedFoldSpans,
	                                              const MRFoldAnalysisState &inputState, bool finalizeAtDocumentEnd) {
	MRFoldScanOutput output;
	std::size_t currentLineIndex = baseLineIndex;
	processedLineCount = std::min(processedLineCount, lineTexts.size());

	if (processedLineCount == 0) {
		output.stateOut = inputState;
		return output;
	}

	std::vector<MRFoldOpenBlock> openBlocks = inputState.openBlocks;
	std::string previousLineText = inputState.previousLineText;
	std::string previousUpperLine = inputState.previousUpperLine;
	std::string previousPreviousLineText = inputState.previousPreviousLineText;
	std::string previousPreviousUpperLine = inputState.previousPreviousUpperLine;
	const std::vector<std::string> inputRecentLineTexts = inputState.recentLineTexts;
	std::vector<std::string> recentLineTexts = inputRecentLineTexts;
	MRSyntaxLineState syntaxState = inputState.syntaxState;
	std::string structuralLineText;
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

	auto recentText = [&](std::size_t localLineIndex, std::size_t distance) noexcept -> const std::string * {
		if (distance <= localLineIndex) return &lineTexts[localLineIndex - distance];
		const std::size_t historyDistance = distance - localLineIndex;
		if (historyDistance == 0 || historyDistance > inputRecentLineTexts.size()) return nullptr;
		return &inputRecentLineTexts[inputRecentLineTexts.size() - historyDistance];
	};
	auto recentLineIndex = [&](std::size_t distance) noexcept -> std::size_t {
		return distance <= currentLineIndex ? currentLineIndex - distance : currentLineIndex;
	};
	auto rememberRecentLine = [&](const std::string &lineText) {
		static constexpr std::size_t kFoldRecentLineLimit = 80;
		if (recentLineTexts.size() == kFoldRecentLineLimit) recentLineTexts.erase(recentLineTexts.begin());
		recentLineTexts.push_back(lineText);
	};

	auto findRecentJavaScriptStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
		for (std::size_t distance = 0; distance <= 4; ++distance) {
			const std::string *candidateText = recentText(localLineIndex, distance);
			if (candidateText == nullptr) break;
			const std::string_view candidateTrimmed = trimView(*candidateText);
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (isJavaScriptStructuralLeadLine(candidateUpper)) return recentLineIndex(distance);
			if (isJavaScriptArrowFunctionLeadLine(candidateTrimmed)) return recentLineIndex(distance);
		}
		return currentLineIndex;
	};

	auto findRecentCLikeStructuralLeadLine = [&](std::size_t localLineIndex, MRSyntaxLanguage currentLanguage) noexcept -> std::size_t {
		for (std::size_t distance = 0; distance <= 80; ++distance) {
			const std::string *candidateText = recentText(localLineIndex, distance);
			if (candidateText == nullptr) break;
			const std::string_view candidateTrimmed = trimView(*candidateText);
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (isCLikeStructuralLeadLine(candidateTrimmed, candidateUpper, currentLanguage)) {
				if (currentLanguage != MRSyntaxLanguage::Cpp) return recentLineIndex(distance);
				std::size_t headDistance = distance;
				while (headDistance < 80) {
					const std::string *previousCandidateText = recentText(localLineIndex, headDistance + 1);
					if (previousCandidateText == nullptr) break;
					const std::string_view previousCandidateTrimmed = trimView(*previousCandidateText);
					if (previousCandidateTrimmed.empty() || isCLikeCommentLikeLine(previousCandidateTrimmed) || previousCandidateTrimmed.front() == '#') break;
					const std::string previousCandidateUpper = upperAscii(std::string(previousCandidateTrimmed));
					if (!isCppTemplatePrefixLead(previousCandidateTrimmed, previousCandidateUpper)) break;
					++headDistance;
				}
				return recentLineIndex(headDistance);
			}
		}
		return currentLineIndex;
	};

	auto findRecentCBraceStartLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
		const std::size_t structuralLeadLine = findRecentCLikeStructuralLeadLine(localLineIndex, MRSyntaxLanguage::C);
		if (structuralLeadLine != currentLineIndex) return structuralLeadLine;
		for (std::size_t distance = 1; distance <= 80; ++distance) {
			const std::string *candidateText = recentText(localLineIndex, distance);
			if (candidateText == nullptr) break;
			const std::string_view candidateTrimmed = trimView(*candidateText);
			if (candidateTrimmed.empty() || isCLikeCommentLikeLine(candidateTrimmed)) {
				continue;
			}
			if (candidateTrimmed.front() == '#') break;
			return recentLineIndex(distance);
		}
		return currentLineIndex;
	};

	auto findRecentSwiftStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
		for (std::size_t distance = 0; distance <= 4; ++distance) {
			const std::string *candidateText = recentText(localLineIndex, distance);
			if (candidateText == nullptr) break;
			const std::string_view candidateTrimmed = trimView(*candidateText);
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (isSwiftStructuralLeadLine(candidateUpper)) return recentLineIndex(distance);
		}
		return currentLineIndex;
	};

	auto findRecentRustStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
		for (std::size_t distance = 0; distance <= 6; ++distance) {
			const std::string *candidateText = recentText(localLineIndex, distance);
			if (candidateText == nullptr) break;
			const std::string_view candidateTrimmed = trimView(*candidateText);
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (isRustStructuralLeadLine(candidateUpper)) return recentLineIndex(distance);
		}
		return currentLineIndex;
	};

		auto findRecentGoStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
			for (std::size_t distance = 0; distance <= 6; ++distance) {
				const std::string *candidateText = recentText(localLineIndex, distance);
				if (candidateText == nullptr) break;
				const std::string_view candidateTrimmed = trimView(*candidateText);
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (isGoStructuralLeadLine(candidateUpper)) return recentLineIndex(distance);
			}
			return currentLineIndex;
		};

		auto findRecentKotlinStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
			for (std::size_t distance = 0; distance <= 6; ++distance) {
				const std::string *candidateText = recentText(localLineIndex, distance);
				if (candidateText == nullptr) break;
				const std::string_view candidateTrimmed = trimView(*candidateText);
				const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
				if (isKotlinStructuralLeadLine(candidateUpper)) return recentLineIndex(distance);
			}
			return currentLineIndex;
		};

		auto findRecentCSharpStructuralLeadLine = [&](std::size_t localLineIndex) noexcept -> std::size_t {
			for (std::size_t distance = 0; distance <= 8; ++distance) {
				const std::string *candidateText = recentText(localLineIndex, distance);
				if (candidateText == nullptr) break;
				const std::string_view candidateTrimmed = trimView(*candidateText);
				const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
				if (isCSharpStructuralLeadLine(candidateUpper)) return recentLineIndex(distance);
			}
			return currentLineIndex;
		};

		auto findRecentShellStructuralLeadLine = [&](std::size_t localLineIndex, std::string_view trimmed, std::string_view upperLine) noexcept -> std::size_t {
		const bool braceLine = trimView(trimmed) == "{";
		const bool thenLead = upperLine == "THEN" || upperLine.ends_with(" THEN");
		const bool doLead = upperLine == "DO" || upperLine.ends_with(" DO");
		for (std::size_t distance = 0; distance <= 8; ++distance) {
			const std::string *candidateText = recentText(localLineIndex, distance);
			if (candidateText == nullptr) break;
			const std::string_view candidateTrimmed = trimView(*candidateText);
			if (candidateTrimmed.empty() || candidateTrimmed.starts_with("#")) {
				continue;
			}
			const std::string candidateUpper = upperAscii(std::string(candidateTrimmed));
			if (braceLine && isShellFunctionHeadLine(candidateTrimmed, candidateUpper)) return recentLineIndex(distance);
			if (thenLead && (candidateUpper.starts_with("IF ") || candidateUpper.starts_with("ELIF "))) return recentLineIndex(distance);
			if (doLead && (candidateUpper.starts_with("FOR ") || candidateUpper.starts_with("WHILE ") || candidateUpper.starts_with("UNTIL ") || candidateUpper.starts_with("SELECT ")))
				return recentLineIndex(distance);
		}
		return currentLineIndex;
	};

	for (std::size_t localLineIndex = 0; localLineIndex < processedLineCount; ++localLineIndex) {
		const std::size_t lineIndex = baseLineIndex + localLineIndex;
		currentLineIndex = lineIndex;
		const std::string &lineText = lineTexts[localLineIndex];
		std::string_view structuralLine = lineText;
		if (language == MRSyntaxLanguage::Zsh || language == MRSyntaxLanguage::C || language == MRSyntaxLanguage::Cpp) {
			MRSyntaxLineResult syntaxLine = tmrHighlightTextLine(language, lineText, syntaxState);

			structuralLineText.assign(lineText);
			for (const MRSyntaxTokenRun &run : syntaxLine.tokenRuns) {
				const bool masksStructure = run.token == MRSyntaxToken::String || run.token == MRSyntaxToken::Comment ||
				                            (language == MRSyntaxLanguage::Zsh && run.token == MRSyntaxToken::Directive);
				if (!masksStructure) continue;
				const std::size_t start = std::min<std::size_t>(run.column, structuralLineText.size());
				const std::size_t end = std::min<std::size_t>(start + run.length, structuralLineText.size());

				std::fill(structuralLineText.begin() + static_cast<std::ptrdiff_t>(start), structuralLineText.begin() + static_cast<std::ptrdiff_t>(end), ' ');
			}
			syntaxState = syntaxLine.stateOut;
			structuralLine = structuralLineText;
		}
		const std::string_view trimmed = trimView(structuralLine);
		const std::string_view previousTrimmed = trimView(previousLineText);
		const std::string_view previousPreviousTrimmed = trimView(previousPreviousLineText);
		const std::size_t currentIndent = leadingIndentBytes(lineText);
		const bool nonEmpty = !trimmed.empty();
		const std::string upperLine = upperAscii(std::string(trimmed));
		if (language == MRSyntaxLanguage::Latex && !openBlocks.empty() && openBlocks.back().languageBlockKind == kLatexEnvironmentBlock &&
		    isLatexRawTextEnvironment(openBlocks.back().xmlTagName)) {
			if (latexLineContainsEnvironmentEnd(lineText, openBlocks.back().xmlTagName)) {
				appendVisibleSpan(openBlocks.back(), lineIndex);
				openBlocks.pop_back();
			}
			previousPreviousLineText = previousLineText;
			previousPreviousUpperLine = previousUpperLine;
			previousLineText = lineText;
			previousUpperLine = upperLine;
			rememberRecentLine(lineText);
			continue;
		}
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
		const MRBasicBlockLine basicLine = language == MRSyntaxLanguage::Basic ? mrBasicClassifyBlockLine(trimmed) : MRBasicBlockLine {MRBasicBlockKind::None, MRBasicBlockDisposition::None};
		std::string_view xmlLeadingOpenTagName;
		const bool xmlLeadingOpenTag = language == MRSyntaxLanguage::Xml && parseXmlLeadingOpenTag(trimmed, xmlLeadingOpenTagName);
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

		if (nonEmpty && lineIndex > 0) {
			std::size_t closerIndex = 0;
			while (!openBlocks.empty() && closerIndex < trimmed.size() && openBlocks.back().sourceKind == MRFoldSourceKind::Delimiter && openBlocks.back().closer != 0 &&
			       trimmed[closerIndex] == openBlocks.back().closer) {
				appendVisibleSpan(openBlocks.back(), (perlSiblingAfterLeadingCloser || javascriptSiblingAfterLeadingCloser || cLikeSiblingAfterLeadingCloser) ? lineIndex - 1 : lineIndex);
				openBlocks.pop_back();
				++closerIndex;
			}
			if (perlSiblingAfterLeadingCloser || javascriptSiblingAfterLeadingCloser || cLikeSiblingAfterLeadingCloser) openSiblingContinuation = true;
		}
		if (language != MRSyntaxLanguage::Xml && nonEmpty && lineIndex > 0) {
			const std::size_t splitOffset = trailingSmartDedentSplitOffset(structuralLine, language);
			if (splitOffset != std::string_view::npos) {
				const std::string_view trailingDedent = trimView(structuralLine.substr(splitOffset));
				std::size_t closerIndex = 0;

				while (!openBlocks.empty() && closerIndex < trailingDedent.size() && openBlocks.back().sourceKind == MRFoldSourceKind::Delimiter && openBlocks.back().closer != 0 &&
				       trailingDedent[closerIndex] == openBlocks.back().closer) {
					appendVisibleSpan(openBlocks.back(), lineIndex);
					openBlocks.pop_back();
					++closerIndex;
				}
			}
		}

		if (mrmacMacroStart && lineIndex > 0) {
			while (!openBlocks.empty()) {
				appendVisibleSpan(openBlocks.back(), lineIndex - 1);
				openBlocks.pop_back();
			}
		}
		if (mrmacMacroEnd && lineIndex > 0) {
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
		if (language == MRSyntaxLanguage::Basic && basicLine.disposition == MRBasicBlockDisposition::Continue) {
			bool selectParentOpen = false;

			if (basicLine.kind == MRBasicBlockKind::Select)
				for (std::size_t index = openBlocks.size(); index-- > 0;)
					if (openBlocks[index].languageBlockKind == static_cast<int>(MRBasicBlockKind::Select)) {
						selectParentOpen = true;
						break;
					}
			if (selectParentOpen) {
				while (!openBlocks.empty()) {
					const MRFoldOpenBlock &block = openBlocks.back();

					if (block.languageBlockKind == static_cast<int>(MRBasicBlockKind::Select)) break;
					appendVisibleSpan(block, lineIndex - 1);
					openBlocks.pop_back();
				}
				if (!openBlocks.empty()) output.branches.push_back(MRFoldGutterBranch(lineIndex, openBlocks.back().level));
			} else {
				while (!openBlocks.empty()) {
					const MRFoldOpenBlock &block = openBlocks.back();
					appendVisibleSpan(block, lineIndex - 1);
					const int closedKind = block.languageBlockKind;
					openBlocks.pop_back();
					if (closedKind == static_cast<int>(basicLine.kind)) break;
				}
				openSiblingContinuation = true;
			}
		}
		if (language == MRSyntaxLanguage::Basic && basicLine.disposition == MRBasicBlockDisposition::Close) {
			while (!openBlocks.empty()) {
				const MRFoldOpenBlock &block = openBlocks.back();
				appendVisibleSpan(block, block.languageBlockKind == static_cast<int>(basicLine.kind) ? lineIndex : lineIndex - 1);
				const int closedKind = block.languageBlockKind;
				openBlocks.pop_back();
				if (closedKind == static_cast<int>(basicLine.kind)) break;
			}
		}
		while (language == MRSyntaxLanguage::Xml && !openBlocks.empty() && openBlocks.back().languageBlockKind == kXmlTagBlock &&
		       xmlLineContainsMatchingCloseTag(trimmed, 0, openBlocks.back().xmlTagName)) {
			appendVisibleSpan(openBlocks.back(), lineIndex);
			openBlocks.pop_back();
		}
		if (language == MRSyntaxLanguage::Latex && latexEndEnvironment && !openBlocks.empty()) {
			std::size_t matchingIndex = std::numeric_limits<std::size_t>::max();
			const bool recoverToDocumentEnd = latexEndEnvironmentName == "document";
			for (std::size_t index = openBlocks.size(); index-- > 0;) {
				const MRFoldOpenBlock &block = openBlocks[index];
				if (block.languageBlockKind == kLatexEnvironmentBlock) {
					if (block.xmlTagName == latexEndEnvironmentName) {
						matchingIndex = index;
						break;
					}
					if (!recoverToDocumentEnd) break;
					continue;
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
					} else if (language != MRSyntaxLanguage::MRMAC && language != MRSyntaxLanguage::Xml && language != MRSyntaxLanguage::Basic && currentIndent <= block.indent) {
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
			case MRSyntaxLanguage::Basic:
				if (basicLine.disposition == MRBasicBlockDisposition::Open)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0, static_cast<int>(basicLine.kind), std::numeric_limits<std::size_t>::max(), openSiblingContinuation);
				else if (basicLine.disposition == MRBasicBlockDisposition::Continue && basicLine.kind != MRBasicBlockKind::Select)
					openBlock(MRFoldSourceKind::Indent, currentIndent, 0, 0, 0, 0,
					          static_cast<int>(basicLine.kind), std::numeric_limits<std::size_t>::max(), openSiblingContinuation);
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
		previousLineText.assign(structuralLine.begin(), structuralLine.end());
		previousUpperLine = upperLine;
		rememberRecentLine(previousLineText);
	}

	output.stateOut.openBlocks = openBlocks;
	output.stateOut.syntaxState = syntaxState;
	output.stateOut.previousLineText = previousLineText;
	output.stateOut.previousUpperLine = previousUpperLine;
	output.stateOut.previousPreviousLineText = previousPreviousLineText;
	output.stateOut.previousPreviousUpperLine = previousPreviousUpperLine;
	output.stateOut.recentLineTexts = recentLineTexts;
	if (finalizeAtDocumentEnd) {
		const std::size_t finalLine = baseLineIndex + processedLineCount - 1;
		for (const MRFoldOpenBlock &block : openBlocks)
			if (language == MRSyntaxLanguage::Xml && block.languageBlockKind == kXmlTagBlock)
				continue;
			else if (language == MRSyntaxLanguage::Latex && block.languageBlockKind == kLatexEnvironmentBlock)
				continue;
			else if (language == MRSyntaxLanguage::Systemd && block.sourceKind == MRFoldSourceKind::Section && block.lastContentLine != std::numeric_limits<std::size_t>::max() && block.lastContentLine > block.startLine)
				appendVisibleSpan(block, block.lastContentLine);
			else
				appendVisibleSpan(block, finalLine);
	}
	return output;
}
} // namespace mr::editor::fold
