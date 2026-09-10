#pragma once

#include "MRFoldWarmupPayload.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mr::editor::fold {

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

struct MRFoldScanOutput {
	std::vector<MRFoldSpan> spans;
	std::vector<MRFoldGutterBranch> branches;
	MRFoldAnalysisState stateOut;
	int visibleMaxLevel = 1;
};

bool isIndentWhitespace(char ch) noexcept;
bool isStatefulSyntaxLanguage(MRSyntaxLanguage language) noexcept;
std::string_view trimView(std::string_view text) noexcept;
std::size_t lastSignificantByte(std::string_view text) noexcept;
bool containsUpperToken(std::string_view text, std::string_view token) noexcept;
std::string_view skipLeadingClosersAndSpace(std::string_view trimmed) noexcept;
bool startsWithCloser(std::string_view trimmed) noexcept;
std::size_t leadingIndentBytes(std::string_view text) noexcept;
bool isPythonIndentLead(std::string_view upperLine) noexcept;
bool isPythonDedentLead(std::string_view upperLine) noexcept;
bool isShellIndentLead(std::string_view trimmed, std::string_view upperLine) noexcept;
bool isShellDedentLead(std::string_view trimmed, std::string_view upperLine) noexcept;
bool isShellFunctionHeadLine(std::string_view trimmed, std::string_view upperLine) noexcept;
bool isFishFunctionLead(std::string_view upperLine) noexcept;
bool isFishIfLead(std::string_view upperLine) noexcept;
bool isFishElseIfLead(std::string_view upperLine) noexcept;
bool isFishElseLead(std::string_view upperLine) noexcept;
bool isFishWhileLead(std::string_view upperLine) noexcept;
bool isFishForLead(std::string_view upperLine) noexcept;
bool isFishSwitchLead(std::string_view upperLine) noexcept;
bool isFishCaseLead(std::string_view upperLine) noexcept;
bool isFishBeginLead(std::string_view upperLine) noexcept;
bool isFishEndLead(std::string_view upperLine) noexcept;
bool startsWithKeywordToken(std::string_view upperLine, std::string_view keyword) noexcept;
int shellIndentBlockKind(std::string_view upperLine) noexcept;
int fishIndentBlockKind(std::string_view upperLine) noexcept;
int pascalIndentBlockKind(std::string_view upperLine) noexcept;
int perlStructuredBlockKind(std::string_view trimmed, std::string_view upperLine) noexcept;
bool isPerlStructuredBlockLead(std::string_view trimmed, std::string_view upperLine) noexcept;
bool isPerlSiblingLead(std::string_view upperLine) noexcept;
bool isJavaScriptSiblingLead(std::string_view upperLine) noexcept;
bool isCLikeSiblingLead(std::string_view upperLine) noexcept;
bool isJavaScriptStructuralLeadLine(std::string_view upperLine) noexcept;
bool isJavaScriptArrowFunctionLeadLine(std::string_view trimmed) noexcept;
bool isCLikeCommentLikeLine(std::string_view trimmed) noexcept;
bool isCppLambdaLeadLine(std::string_view trimmed) noexcept;
bool isCLikeStructuralLeadLine(std::string_view trimmed, std::string_view upperLine, MRSyntaxLanguage language) noexcept;
bool isCppTemplatePrefixLead(std::string_view trimmed, std::string_view upperLine) noexcept;
bool isCLikeBraceFoldCandidateLine(std::string_view trimmed) noexcept;
bool isSwiftCommentLikeLine(std::string_view trimmed) noexcept;
bool isSwiftLabelIdentifier(std::string_view text) noexcept;
std::string_view skipSwiftLeadingLabels(std::string_view text) noexcept;
std::string_view normalizeSwiftStructuralLeadText(std::string_view text) noexcept;
bool isSwiftStructuralLeadLine(std::string_view upperLine) noexcept;
bool isSwiftAccessorLeadLine(std::string_view upperLine) noexcept;
bool isSwiftPropertyBlockLeadLine(std::string_view trimmedLine, std::string_view upperLine) noexcept;
bool isRustCommentLikeLine(std::string_view trimmed) noexcept;
bool isGoCommentLikeLine(std::string_view trimmed) noexcept;
bool isKotlinCommentLikeLine(std::string_view trimmed) noexcept;
bool isCSharpCommentLikeLine(std::string_view trimmed) noexcept;
std::string_view skipRustLeadingLabels(std::string_view text) noexcept;
std::string_view normalizeRustStructuralLeadText(std::string_view text) noexcept;
bool isRustStructuralLeadLine(std::string_view upperLine) noexcept;
std::string_view normalizeGoStructuralLeadText(std::string_view text) noexcept;
bool isGoStructuralLeadLine(std::string_view upperLine) noexcept;
std::string_view normalizeKotlinStructuralLeadText(std::string_view text) noexcept;
bool isKotlinStructuralLeadLine(std::string_view upperLine) noexcept;
std::string_view normalizeCSharpStructuralLeadText(std::string_view text) noexcept;
bool isCSharpStructuralLeadLine(std::string_view upperLine) noexcept;
bool isPerlPodStart(std::string_view trimmed) noexcept;
bool isPerlPodEnd(std::string_view trimmed) noexcept;
bool isCLikeStructuralBraceLead(std::string_view trimmed, std::string_view upperLine, std::string_view previousTrimmed, std::string_view previousUpperLine,
								std::string_view previousPreviousTrimmed, std::string_view previousPreviousUpperLine, MRSyntaxLanguage language) noexcept;

bool markdownContinuationColumn(std::string_view line, int &targetColumn) noexcept;
bool isMarkdownFenceLine(std::string_view trimmed) noexcept;
bool isMarkdownSetextUnderline(std::string_view trimmed) noexcept;
bool isMakeTargetLine(std::string_view trimmed) noexcept;
bool isMakeRecipeLine(std::string_view lineText) noexcept;
bool isPreprocessorFoldStart(std::string_view trimmed) noexcept;
bool isPreprocessorFoldEnd(std::string_view trimmed) noexcept;
bool isPreprocessorFoldSibling(std::string_view trimmed) noexcept;
bool isIndentFoldLanguage(MRSyntaxLanguage language) noexcept;
bool isShellSiblingLead(std::string_view upperLine) noexcept;
bool isMRMACMacroStart(std::string_view upperLine) noexcept;
bool isMRMACMacroEnd(std::string_view upperLine) noexcept;
bool isMRMACIfLead(std::string_view upperLine) noexcept;
bool isMRMACElseLead(std::string_view upperLine) noexcept;
bool isMRMACWhileLead(std::string_view upperLine) noexcept;
bool isMRMACEndLead(std::string_view upperLine) noexcept;
bool isMRMACCommentLine(std::string_view trimmed) noexcept;
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
bool isPascalCommentLikeLine(std::string_view trimmed) noexcept;
bool xmlLineContainsMatchingCloseTag(std::string_view text, std::size_t pos, std::string_view tagName) noexcept;
bool parseXmlLeadingOpenTag(std::string_view trimmed, std::string_view &tagName) noexcept;
bool parseXmlLeadingCloseTag(std::string_view trimmed, std::string_view &tagName) noexcept;
bool isSystemdSectionHeader(std::string_view trimmed) noexcept;
bool isSystemdCommentLine(std::string_view trimmed) noexcept;
char matchingCloserForOpenDelimiter(char ch) noexcept;
char matchingOpenDelimiterForCloser(char ch) noexcept;
std::size_t trailingSmartDedentSplitOffset(std::string_view lineText, MRSyntaxLanguage language) noexcept;
int markdownHeadingLevel(std::string_view trimmed, std::string_view nextTrimmed) noexcept;
int latexHeadingLevel(std::string_view trimmed) noexcept;
bool latexLineContainsEnvironmentEnd(std::string_view line, std::string_view environmentName) noexcept;
bool isLatexRawTextEnvironment(std::string_view environmentName) noexcept;
bool parseLatexLeadingBeginEnvironment(std::string_view trimmed, std::string_view &environmentName) noexcept;
bool parseLatexLeadingEndEnvironment(std::string_view trimmed, std::string_view &environmentName) noexcept;
bool markdownFenceMarker(std::string_view trimmed, char &marker, std::size_t &runLength) noexcept;
bool isMarkdownFenceClose(std::string_view trimmed, char marker, std::size_t runLength) noexcept;
bool isMarkdownListLead(std::string_view line, std::string_view trimmed, std::string_view nextTrimmed, std::size_t currentIndent, std::size_t nextIndent) noexcept;
bool isMarkdownBlockQuoteLead(std::string_view trimmed, std::string_view nextTrimmed) noexcept;
bool isNonEmptyNonRecipeMakeLine(std::string_view lineText, std::string_view trimmed) noexcept;
bool isMakeDirectiveFoldStart(std::string_view trimmed) noexcept;
bool isMakeDirectiveFoldEnd(std::string_view trimmed) noexcept;
bool isMakeDirectiveFoldSibling(std::string_view trimmed) noexcept;

void appendFoldScanLineTexts(std::vector<std::string> &target, const MRTextBufferModel::ReadSnapshot &snapshot, std::size_t startLine, std::size_t endLine);
std::vector<std::string> splitFoldTrainingLines(const std::string &text);
MRFoldScanOutput computeFoldSpansForLineTexts(const std::vector<std::string> &lineTexts, std::size_t processedLineCount, std::size_t baseLineIndex, std::size_t topLine,
											 std::size_t requestBottomLine, MRSyntaxLanguage language, const std::map<std::size_t, MRFoldSpan> &closedFoldSpans,
											 const MRFoldAnalysisState &inputState, bool finalizeAtDocumentEnd);

} // namespace mr::editor::fold
