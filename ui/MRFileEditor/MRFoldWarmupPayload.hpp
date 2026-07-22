#pragma once

#include "MRFileEditor.hpp"

#include <algorithm>
#include <utility>

struct MRFoldWarmupPayload final : mr::coprocessor::Payload {
	std::uint64_t generation;
	mr::coprocessor::WorkDirection direction;
	MRSyntaxLanguage language;
	std::size_t startLine;
	std::size_t endLine;
	int visibleGutterColumns;
	MRFoldAnalysisState stateIn;
	MRFoldAnalysisState stateOut;
	std::vector<std::string> lineTexts;
	std::vector<MRFoldSpan> spans;
	std::vector<MRFoldGutterBranch> branches;

	MRFoldWarmupPayload() noexcept
	    : generation(0), direction(mr::coprocessor::WorkDirection::None), language(MRSyntaxLanguage::PlainText), startLine(0), endLine(0), visibleGutterColumns(1), stateIn(), stateOut(),
	      lineTexts(), spans(), branches() {
	}

	MRFoldWarmupPayload(std::uint64_t aGeneration, mr::coprocessor::WorkDirection aDirection, MRSyntaxLanguage aLanguage, std::size_t aStartLine, std::size_t aEndLine,
	                    int aVisibleGutterColumns, MRFoldAnalysisState aStateIn, MRFoldAnalysisState aStateOut, std::vector<std::string> aLineTexts, std::vector<MRFoldSpan> aSpans,
	                    std::vector<MRFoldGutterBranch> aBranches)
	    : generation(aGeneration), direction(aDirection), language(aLanguage), startLine(aStartLine), endLine(aEndLine), visibleGutterColumns(std::max(1, aVisibleGutterColumns)),
	      stateIn(std::move(aStateIn)), stateOut(std::move(aStateOut)), lineTexts(std::move(aLineTexts)), spans(std::move(aSpans)), branches(std::move(aBranches)) {
	}
};
