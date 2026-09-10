#include "../MRSyntax.hpp"

#include <algorithm>

namespace {
MRSyntaxTokenMap tokenMapFromRuns(std::size_t length, const std::vector<MRSyntaxTokenRun> &runs) {
	MRSyntaxTokenMap tokens(length, MRSyntaxToken::Text);

	for (const MRSyntaxTokenRun &run : runs) {
		const std::size_t start = std::min<std::size_t>(run.column, tokens.size());
		const std::size_t end = std::min<std::size_t>(start + run.length, tokens.size());
		for (std::size_t pos = start; pos < end; ++pos)
			tokens[pos] = run.token;
	}
	return tokens;
}
} // namespace

MRSyntaxTokenMap tmrBuildLegacyTokenMapForTextLine(MRSyntaxLanguage language, std::string_view line, MRSyntaxLineState previousState) {
	const MRSyntaxLineResult result = tmrHighlightTextLine(language, line, previousState);
	return tokenMapFromRuns(line.size(), result.tokenRuns);
}

std::vector<MRSyntaxTokenRun> tmrBuildTokenRunsFromTokenMap(const MRSyntaxTokenMap &tokenMap) {
	std::vector<MRSyntaxTokenRun> runs;
	if (tokenMap.empty()) return runs;

	std::size_t start = 0;
	MRSyntaxToken current = tokenMap[0];
	for (std::size_t i = 1; i < tokenMap.size(); ++i) {
		if (tokenMap[i] == current) continue;
		runs.push_back(MRSyntaxTokenRun(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(i - start), current));
		start = i;
		current = tokenMap[i];
	}
	runs.push_back(MRSyntaxTokenRun(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(tokenMap.size() - start), current));
	return runs;
}
