#pragma once

#include <string_view>

struct MRSyntaxXmlNucleusEvidence {
	int score;
	int strongSignals;
	int structuralTags;
	bool decisive;

	MRSyntaxXmlNucleusEvidence() noexcept : score(0), strongSignals(0), structuralTags(0), decisive(false) {
	}
};

MRSyntaxXmlNucleusEvidence tmrAnalyzeSyntaxXmlNucleus(std::string_view text) noexcept;
