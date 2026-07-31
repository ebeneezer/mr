#include "MRSettingsEditSetup.hpp"
#include "MRSettingsEditConstants.hpp"
#include "MRSettingsRuntime.hpp"

#include <algorithm>
#include <string>

namespace {

bool setError(std::string *errorMessage, const std::string &message) {
	if (errorMessage != nullptr) *errorMessage = message;
	return false;
}

} // namespace

namespace {

std::string resolvedEditFormatLineValue(const std::string &value, int tabSize, int leftMargin, int rightMargin, int &resolvedLeftMargin, int &resolvedRightMargin) {
	std::string normalized;

	if (normalizeEditFormatLine(value, tabSize, leftMargin, rightMargin, normalized, &resolvedLeftMargin, &resolvedRightMargin, nullptr)) return normalized;
	resolvedLeftMargin = clampEditFormatLeftMargin(leftMargin, rightMargin);
	resolvedRightMargin = clampEditFormatRightMargin(rightMargin);
	return defaultEditFormatLineForTabSize(tabSize, resolvedLeftMargin, resolvedRightMargin);
}

int nextNumericTabFillColumn(int column, int tabSize) noexcept {
	const int normalizedTabSize = clampEditFormatTabSize(tabSize);
	const int safeColumn = std::max(1, column);
	return ((safeColumn - 1) / normalizedTabSize + 1) * normalizedTabSize + 1;
}

} // namespace


int clampEditFormatTabSize(int tabSize) noexcept {
	return std::max(2, std::min(tabSize, 32));
}

int clampEditFormatRightMargin(int rightMargin) noexcept {
	return std::max(1, std::min(rightMargin, 999));
}

int clampEditFormatLeftMargin(int leftMargin, int rightMargin) noexcept {
	const int normalizedRightMargin = clampEditFormatRightMargin(rightMargin);
	if (normalizedRightMargin <= 1) return 1;
	return std::max(1, std::min(leftMargin, normalizedRightMargin - 1));
}

std::string defaultEditFormatLineForTabSize(int tabSize, int leftMargin, int rightMargin) {
	const int normalizedTabSize = clampEditFormatTabSize(tabSize);
	const int normalizedRightMargin = clampEditFormatRightMargin(rightMargin);
	const int normalizedLeftMargin = clampEditFormatLeftMargin(leftMargin, normalizedRightMargin);
	std::string out(static_cast<std::size_t>(normalizedRightMargin), '.');

	if (normalizedRightMargin <= 1) {
		out[0] = 'R';
		return out;
	}
	for (int col = normalizedTabSize; col <= normalizedRightMargin; col += normalizedTabSize)
		if (col > normalizedLeftMargin && col < normalizedRightMargin) out[static_cast<std::size_t>(col - 1)] = '|';
	out[static_cast<std::size_t>(normalizedLeftMargin - 1)] = 'L';
	out[static_cast<std::size_t>(normalizedRightMargin - 1)] = 'R';
	return out;
}

bool normalizeEditFormatLine(const std::string &value, int tabSize, int fallbackLeftMargin, int fallbackRightMargin, std::string &outValue, int *outLeftMargin, int *outRightMargin, std::string *errorMessage) {
	std::string out = value;
	const int normalizedFallbackRightMargin = clampEditFormatRightMargin(fallbackRightMargin);
	const int normalizedFallbackLeftMargin = clampEditFormatLeftMargin(fallbackLeftMargin, normalizedFallbackRightMargin);
	int lCount = 0;
	int rCount = 0;
	int lIndex = -1;
	int rIndex = -1;

	if (out.empty()) {
		outValue = defaultEditFormatLineForTabSize(tabSize, normalizedFallbackLeftMargin, normalizedFallbackRightMargin);
		if (outLeftMargin != nullptr) *outLeftMargin = normalizedFallbackLeftMargin;
		if (outRightMargin != nullptr) *outRightMargin = normalizedFallbackRightMargin;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	{
		bool legacy = true;
		for (char ch : out)
			if (ch != '!' && ch != '-') {
				legacy = false;
				break;
			}
		if (legacy) {
			outValue = defaultEditFormatLineForTabSize(tabSize, normalizedFallbackLeftMargin, normalizedFallbackRightMargin);
			if (outLeftMargin != nullptr) *outLeftMargin = normalizedFallbackLeftMargin;
			if (outRightMargin != nullptr) *outRightMargin = normalizedFallbackRightMargin;
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
	}
	for (char &ch : out)
		if (ch == ' ') ch = '.';
	for (std::size_t i = 0; i < out.size(); ++i) {
		char ch = out[i];
		if (ch != '.' && ch != '|' && ch != 'L' && ch != 'R') return setError(errorMessage, "FORMAT_LINE may only contain '.', ' ', '|', 'L' and 'R'.");
		if (ch == 'L') {
			++lCount;
			lIndex = static_cast<int>(i);
		}
		if (ch == 'R') {
			++rCount;
			rIndex = static_cast<int>(i);
		}
	}
	if (lCount > 1) return setError(errorMessage, "FORMAT_LINE must contain at most one 'L'.");
	if (rCount != 1) return setError(errorMessage, "FORMAT_LINE must contain exactly one 'R'.");
	if (lCount == 0) lIndex = 0;
	if (lIndex >= rIndex && rIndex > 0) return setError(errorMessage, "FORMAT_LINE must place 'L' before 'R'.");
	out.resize(static_cast<std::size_t>(rIndex + 1), '.');
	if (rIndex > 0) out[static_cast<std::size_t>(lIndex)] = 'L';
	out[static_cast<std::size_t>(rIndex)] = 'R';
	outValue = out;
	if (outLeftMargin != nullptr) *outLeftMargin = rIndex > 0 ? lIndex + 1 : 1;
	if (outRightMargin != nullptr) *outRightMargin = rIndex + 1;
	if (errorMessage != nullptr) errorMessage->clear();
	return true;
}

std::string synchronizeEditFormatLineMargins(const std::string &value, int leftMargin, int rightMargin, int tabSize) {
	std::string normalized;
	int oldLeftMargin = 1;
	int oldRightMargin = 1;
	const int normalizedRightMargin = clampEditFormatRightMargin(rightMargin);
	const int normalizedLeftMargin = clampEditFormatLeftMargin(leftMargin, normalizedRightMargin);
	std::string out;
	const int delta = normalizedLeftMargin - oldLeftMargin;

	if (!normalizeEditFormatLine(value, tabSize, normalizedLeftMargin, normalizedRightMargin, normalized, &oldLeftMargin, &oldRightMargin, nullptr)) return defaultEditFormatLineForTabSize(tabSize, normalizedLeftMargin, normalizedRightMargin);
	out = std::string(static_cast<std::size_t>(normalizedRightMargin), '.');
	if (normalizedRightMargin <= 1) {
		out[0] = 'R';
		return out;
	}
	out[static_cast<std::size_t>(normalizedLeftMargin - 1)] = 'L';
	out[static_cast<std::size_t>(normalizedRightMargin - 1)] = 'R';
	for (int i = 0; i < static_cast<int>(normalized.size()); ++i) {
		const char ch = normalized[static_cast<std::size_t>(i)];
		const int shifted = i + delta;
		const int column = shifted + 1;

		if (ch != '|') continue;
		if (column <= normalizedLeftMargin || column >= normalizedRightMargin) continue;
		if (shifted < 0 || shifted >= normalizedRightMargin) continue;
		out[static_cast<std::size_t>(shifted)] = '|';
	}
	return out;
}

bool editFormatLineAtColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column, char symbol, std::string &outValue, int *outLeftMargin, int *outRightMargin, std::string *errorMessage) {
	std::string normalized;
	std::string edited;
	int currentLeftMargin = leftMargin;
	int currentRightMargin = rightMargin;
	const char normalizedSymbol = symbol == ' ' ? '.' : symbol;
	const int safeColumn = std::max(1, std::min(column, 999));

	if (normalizedSymbol != '.' && normalizedSymbol != '|' && normalizedSymbol != 'L' && normalizedSymbol != 'R') return setError(errorMessage, "FORMAT_LINE editor accepts only '.', ' ', '|', 'L' and 'R'.");
	if (!normalizeEditFormatLine(value, tabSize, leftMargin, rightMargin, normalized, &currentLeftMargin, &currentRightMargin, errorMessage)) return false;
	edited = normalized;
	if (static_cast<int>(edited.size()) < safeColumn) edited.append(static_cast<std::size_t>(safeColumn - static_cast<int>(edited.size())), '.');
	if (normalizedSymbol == 'L') {
		if (safeColumn >= currentRightMargin) return setError(errorMessage, "FORMAT_LINE must place 'L' before 'R'.");
		for (char &ch : edited)
			if (ch == 'L') ch = '.';
		edited[static_cast<std::size_t>(safeColumn - 1)] = 'L';
	} else if (normalizedSymbol == 'R') {
		if (safeColumn <= currentLeftMargin) return setError(errorMessage, "FORMAT_LINE must place 'R' after 'L'.");
		for (char &ch : edited)
			if (ch == 'R') ch = '.';
		edited.resize(static_cast<std::size_t>(safeColumn), '.');
		edited[static_cast<std::size_t>(safeColumn - 1)] = 'R';
	} else if (normalizedSymbol == '|') {
		if (safeColumn <= currentLeftMargin || safeColumn >= currentRightMargin) {
			outValue = normalized;
			if (outLeftMargin != nullptr) *outLeftMargin = currentLeftMargin;
			if (outRightMargin != nullptr) *outRightMargin = currentRightMargin;
			if (errorMessage != nullptr) errorMessage->clear();
			return true;
		}
		edited[static_cast<std::size_t>(safeColumn - 1)] = '|';
	} else {
		if (safeColumn != currentLeftMargin && safeColumn != currentRightMargin && safeColumn <= static_cast<int>(edited.size())) edited[static_cast<std::size_t>(safeColumn - 1)] = '.';
	}
	return normalizeEditFormatLine(edited, tabSize, currentLeftMargin, currentRightMargin, outValue, outLeftMargin, outRightMargin, errorMessage);
}

bool translateEditFormatLine(const std::string &value, int tabSize, int leftMargin, int rightMargin, int deltaColumns, std::string &outValue, int *outLeftMargin, int *outRightMargin, std::string *errorMessage) {
	std::string normalized;
	int currentLeftMargin = leftMargin;
	int currentRightMargin = rightMargin;
	int clampedDelta = deltaColumns;
	std::string translated;

	if (!normalizeEditFormatLine(value, tabSize, leftMargin, rightMargin, normalized, &currentLeftMargin, &currentRightMargin, errorMessage)) return false;
	clampedDelta = std::max(1 - currentLeftMargin, std::min(deltaColumns, 999 - currentRightMargin));
	if (currentRightMargin <= 1) {
		outValue = "R";
		if (outLeftMargin != nullptr) *outLeftMargin = 1;
		if (outRightMargin != nullptr) *outRightMargin = 1;
		if (errorMessage != nullptr) errorMessage->clear();
		return true;
	}
	translated.assign(static_cast<std::size_t>(currentRightMargin + clampedDelta), '.');
	translated[static_cast<std::size_t>(currentLeftMargin + clampedDelta - 1)] = 'L';
	translated[static_cast<std::size_t>(currentRightMargin + clampedDelta - 1)] = 'R';
	for (int i = 0; i < static_cast<int>(normalized.size()); ++i) {
		const int shiftedColumn = i + clampedDelta + 1;
		if (normalized[static_cast<std::size_t>(i)] != '|') continue;
		if (shiftedColumn <= currentLeftMargin + clampedDelta || shiftedColumn >= currentRightMargin + clampedDelta) continue;
		translated[static_cast<std::size_t>(shiftedColumn - 1)] = '|';
	}
	return normalizeEditFormatLine(translated, tabSize, currentLeftMargin + clampedDelta, currentRightMargin + clampedDelta, outValue, outLeftMargin, outRightMargin, errorMessage);
}

int nextResolvedEditFormatTabStopColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column) {
	int resolvedLeftMargin = 1;
	int resolvedRightMargin = 1;
	const std::string normalized = resolvedEditFormatLineValue(value, tabSize, leftMargin, rightMargin, resolvedLeftMargin, resolvedRightMargin);
	const int safeColumn = std::max(1, column);

	for (int i = safeColumn; i < static_cast<int>(normalized.size()); ++i)
		if (normalized[static_cast<std::size_t>(i)] == '|') return i + 1;
	return safeColumn;
}

int prevResolvedEditFormatTabStopColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column) {
	int resolvedLeftMargin = 1;
	int resolvedRightMargin = 1;
	const std::string normalized = resolvedEditFormatLineValue(value, tabSize, leftMargin, rightMargin, resolvedLeftMargin, resolvedRightMargin);
	const int safeColumn = std::max(1, column);
	int i = std::min(std::max(0, safeColumn - 2), static_cast<int>(normalized.size()) - 1);

	for (; i >= 0; --i)
		if (normalized[static_cast<std::size_t>(i)] == '|') return i + 1;
	if (safeColumn > resolvedLeftMargin) return resolvedLeftMargin;
	return safeColumn;
}

int resolvedEditFormatTabDisplayColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int column) {
	const int safeColumn = std::max(1, column);
	const int resolvedTabStop = nextResolvedEditFormatTabStopColumn(value, tabSize, leftMargin, rightMargin, safeColumn);

	if (resolvedTabStop > safeColumn) return resolvedTabStop;
	return nextNumericTabFillColumn(safeColumn, tabSize);
}

int resolvedEditFormatIndentColumn(const std::string &value, int tabSize, int leftMargin, int rightMargin, int preferredColumn) {
	int resolvedLeftMargin = 1;
	int resolvedRightMargin = 1;
	const std::string normalized = resolvedEditFormatLineValue(value, tabSize, leftMargin, rightMargin, resolvedLeftMargin, resolvedRightMargin);
	const int safePreferredColumn = std::max(1, preferredColumn);
	int resolvedColumn = resolvedLeftMargin;

	if (safePreferredColumn <= resolvedLeftMargin) return resolvedLeftMargin;
	for (int i = 0; i < static_cast<int>(normalized.size()); ++i) {
		if (normalized[static_cast<std::size_t>(i)] != '|') continue;
		if (i + 1 > safePreferredColumn) break;
		resolvedColumn = i + 1;
	}
	return resolvedColumn;
}

std::string buildEditIndentFill(const MREditSetupSettings &settings, int startColumn, int targetColumn, bool preferTabs) {
	std::string out;
	int currentColumn = std::max(1, startColumn);
	const int safeTargetColumn = std::max(currentColumn, targetColumn);

	while (currentColumn < safeTargetColumn) {
		const int nextTabColumn = resolvedEditFormatTabDisplayColumn(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, currentColumn);
		if (preferTabs && nextTabColumn <= safeTargetColumn) {
			out.push_back('\t');
			currentColumn = nextTabColumn;
		} else {
			out.push_back(' ');
			++currentColumn;
		}
	}
	return out;
}
