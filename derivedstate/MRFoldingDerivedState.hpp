#ifndef MRFOLDINGDERIVEDSTATE_HPP
#define MRFOLDINGDERIVEDSTATE_HPP

#include "../ui/MRSyntax.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

enum class MRFoldSourceKind : unsigned char {
	Delimiter,
	Indent,
	Fence,
	Section,
	Directive,
	Macro,
	Target,
	Generic
};

struct MRFoldSpan {
	std::size_t startLine;
	std::size_t endLine;
	unsigned short level;
	MRFoldSourceKind sourceKind;
	bool open;
	bool siblingContinuation;

	MRFoldSpan() noexcept : startLine(0), endLine(0), level(0), sourceKind(MRFoldSourceKind::Generic), open(true), siblingContinuation(false) {
	}

	MRFoldSpan(std::size_t aStartLine, std::size_t aEndLine, unsigned short aLevel, MRFoldSourceKind aSourceKind, bool aOpen = true, bool aSiblingContinuation = false) noexcept
	    : startLine(aStartLine), endLine(aEndLine), level(aLevel), sourceKind(aSourceKind), open(aOpen), siblingContinuation(aSiblingContinuation) {
	}
};

struct MRFoldGutterBranch {
	std::size_t line;
	unsigned short level;

	MRFoldGutterBranch() noexcept : line(0), level(0) {
	}

	MRFoldGutterBranch(std::size_t aLine, unsigned short aLevel) noexcept : line(aLine), level(aLevel) {
	}
};

struct MRFoldOpenBlockState {
	std::size_t startLine;
	std::size_t indent;
	unsigned short level;
	MRFoldSourceKind sourceKind;
	char closer;
	char marker;
	std::size_t markerLength;
	int headingLevel;
	int languageBlockKind;
	bool siblingContinuation;
	std::size_t lastContentLine;
	std::string xmlTagName;

	MRFoldOpenBlockState() noexcept;
	bool operator==(const MRFoldOpenBlockState &other) const noexcept;
};

struct MRFoldAnalysisState {
	std::vector<MRFoldOpenBlockState> openBlocks;
	MRSyntaxLineState syntaxState;
	std::string previousLineText;
	std::string previousUpperLine;
	std::string previousPreviousLineText;
	std::string previousPreviousUpperLine;
	std::vector<std::string> recentLineTexts;

	bool operator==(const MRFoldAnalysisState &other) const noexcept;
	bool operator!=(const MRFoldAnalysisState &other) const noexcept;
};

struct MRFoldClosedProjection {
	std::vector<MRFoldSpan> spans;
	std::vector<std::size_t> hiddenLinePrefix;

	MRFoldClosedProjection() noexcept;
	void finalize();
	std::size_t hiddenLineCount() const noexcept;
	std::size_t hiddenLineCountBefore(std::size_t documentLine) const noexcept;
	std::size_t hiddenLineCountInRange(std::size_t startLine, std::size_t endLine) const noexcept;
	const MRFoldSpan *spanStartingAt(std::size_t documentLine) const noexcept;
	const MRFoldSpan *spanEndingAt(std::size_t documentLine) const noexcept;
	const MRFoldSpan *spanContaining(std::size_t documentLine) const noexcept;
};

class MRFoldingDerivedState {
  public:
	struct VisibleState {
		std::uint64_t revision = 0;
		std::size_t documentId = 0;
		std::size_t version = 0;
		std::size_t topLine = 0;
		std::size_t bottomLine = 0;
		MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;
		int gutterColumns = 1;
		std::vector<MRFoldSpan> spans;
		std::vector<MRFoldGutterBranch> branches;
		std::vector<unsigned short> displayLevels;
		std::vector<std::string> lineTexts;
	};

	MRFoldingDerivedState() noexcept;

	VisibleState &visibleState() noexcept;
	const VisibleState &visibleState() const noexcept;

	std::map<std::size_t, MRFoldSpan> &closedFoldSpans() noexcept;
	const std::map<std::size_t, MRFoldSpan> &closedFoldSpans() const noexcept;

	std::vector<MRFoldSpan> &effectiveClosedFoldSpans() noexcept;
	const std::vector<MRFoldSpan> &effectiveClosedFoldSpans() const noexcept;

	void clearVisibleState(bool preserveProjection) noexcept;
	void clearClosedFolds() noexcept;
	void rebuildEffectiveClosedFolds() noexcept;
	int visibleGutterColumns() const noexcept;

	void beginDocumentFoldLevel(unsigned short level, std::shared_ptr<const MRFoldClosedProjection> preview);
	void adoptDocumentFoldLevelProjection(unsigned short level, std::size_t canonicalEndLine, bool complete, std::shared_ptr<const MRFoldClosedProjection> projection);
	bool refreshDocumentFoldLevelViewportProjection();
	void clearDocumentFoldLevel() noexcept;
	bool documentFoldLevelActive() const noexcept;
	unsigned short documentFoldLevel() const noexcept;
	bool documentFoldLevelContains(std::size_t startLine) const noexcept;
	bool documentFoldLevelCloses(std::size_t startLine) const noexcept;
	bool toggleDocumentFoldLevelSpan(std::size_t startLine);
	void refreshVisibleFoldOpenStates() noexcept;

	bool hasEffectiveClosedFolds() const noexcept;
	std::size_t foldedLineCount(std::size_t totalLines) const noexcept;
	std::size_t documentLineForVisibleLine(std::size_t visibleLine, std::size_t totalLines) const noexcept;
	std::size_t visibleLineForDocumentLine(std::size_t documentLine) const noexcept;
	const MRFoldSpan *effectiveClosedFoldStartingAt(std::size_t documentLine) const noexcept;
	const MRFoldSpan *effectiveClosedFoldEndingAt(std::size_t documentLine) const noexcept;
	const MRFoldSpan *effectiveClosedFoldContaining(std::size_t documentLine) const noexcept;

  private:
	bool documentProjectionSpanOpen(std::size_t startLine) const noexcept;
	const MRFoldSpan *documentCanonicalProjectionSpanStartingAt(std::size_t documentLine) const noexcept;
	const MRFoldSpan *documentCanonicalProjectionSpanEndingAt(std::size_t documentLine) const noexcept;
	const MRFoldSpan *documentCanonicalProjectionSpanContaining(std::size_t documentLine) const noexcept;
	std::size_t documentCanonicalProjectionHiddenBefore(std::size_t documentLine) const noexcept;
	std::size_t documentCanonicalProjectionHiddenInRange(std::size_t startLine, std::size_t endLine) const noexcept;
	std::size_t documentCanonicalProjectionHiddenLineCount() const noexcept;
	const MRFoldSpan *documentBaseProjectionSpanStartingAt(std::size_t documentLine) const noexcept;
	const MRFoldSpan *documentProjectionSpanStartingAt(std::size_t documentLine) const noexcept;
	const MRFoldSpan *documentProjectionSpanEndingAt(std::size_t documentLine) const noexcept;
	const MRFoldSpan *documentProjectionSpanContaining(std::size_t documentLine) const noexcept;
	std::size_t documentProjectionHiddenBefore(std::size_t documentLine) const noexcept;
	std::size_t documentProjectionHiddenInRange(std::size_t startLine, std::size_t endLine) const noexcept;
	std::size_t effectiveHiddenBefore(std::size_t documentLine) const noexcept;
	bool documentProjectionCovers(const MRFoldSpan &span) const noexcept;

	VisibleState mVisible;
	std::map<std::size_t, MRFoldSpan> mClosedFoldSpans;
	std::vector<MRFoldSpan> mEffectiveClosedFoldSpans;
	bool mDocumentFoldLevelActive;
	unsigned short mDocumentFoldLevel;
	std::size_t mDocumentFoldCanonicalEndLine;
	std::vector<std::shared_ptr<const MRFoldClosedProjection>> mDocumentFoldCanonicalProjections;
	std::vector<std::size_t> mDocumentFoldCanonicalLastStartLines;
	std::vector<std::size_t> mDocumentFoldCanonicalLastEndLines;
	std::vector<std::size_t> mDocumentFoldCanonicalHiddenPrefix;
	std::shared_ptr<const MRFoldClosedProjection> mDocumentFoldViewportProjection;
	std::shared_ptr<const MRFoldClosedProjection> mDocumentFoldDescendantProjection;
	std::vector<MRFoldSpan> mDocumentFoldOpenSpans;
};

#endif
