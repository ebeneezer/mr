#ifndef MRFOLDINGDERIVEDSTATE_HPP
#define MRFOLDINGDERIVEDSTATE_HPP

#include "MRDerivedStateBase.hpp"

#include "../ui/MRSyntax.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
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

class MRFoldingDerivedState : public MRDerivedStateBase {
  public:
	struct WarmupState {
		std::uint64_t taskId = 0;
		std::size_t documentId = 0;
		std::size_t version = 0;
		std::size_t topLine = 0;
		std::size_t bottomLine = 0;
		MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;
	};

	struct VisibleState {
		std::size_t documentId = 0;
		std::size_t version = 0;
		std::size_t topLine = 0;
		std::size_t bottomLine = 0;
		MRSyntaxLanguage language = MRSyntaxLanguage::PlainText;
		int gutterColumns = 1;
		std::vector<MRFoldSpan> spans;
		std::vector<unsigned short> displayLevels;
		std::vector<std::string> lineTexts;
	};

	MRFoldingDerivedState() noexcept;

	WarmupState &warmupState() noexcept;
	const WarmupState &warmupState() const noexcept;

	VisibleState &visibleState() noexcept;
	const VisibleState &visibleState() const noexcept;

	std::map<std::size_t, MRFoldSpan> &closedFoldSpans() noexcept;
	const std::map<std::size_t, MRFoldSpan> &closedFoldSpans() const noexcept;

	std::vector<MRFoldSpan> &effectiveClosedFoldSpans() noexcept;
	const std::vector<MRFoldSpan> &effectiveClosedFoldSpans() const noexcept;

	void clearWarmupState() noexcept;
	void clearVisibleState(bool preserveProjection) noexcept;
	void clearClosedFolds() noexcept;
	void rebuildEffectiveClosedFolds() noexcept;
	int visibleGutterColumns() const noexcept;

  private:
	WarmupState mWarmup;
	VisibleState mVisible;
	std::map<std::size_t, MRFoldSpan> mClosedFoldSpans;
	std::vector<MRFoldSpan> mEffectiveClosedFoldSpans;
};

#endif
