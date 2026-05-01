#include "MRTextFormatting.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace {

std::vector<std::string> splitWords(std::string_view text) {
	std::vector<std::string> words;
	std::string currentWord;

	for (std::size_t i = 0; i <= text.length(); ++i) {
		if (i == text.length() || std::isspace(static_cast<unsigned char>(text[i])) != 0) {
			if (!currentWord.empty()) {
				words.push_back(currentWord);
				currentWord.clear();
			}
			continue;
		}
		currentWord.push_back(text[i]);
	}
	return words;
}

} // namespace

MRTextFormatting::NormalizedFormatLine MRTextFormatting::normalizedFormatLine(const MREditSetupSettings &settings) {
	NormalizedFormatLine normalized;

	normalized.leftMargin = settings.leftMargin;
	normalized.rightMargin = settings.rightMargin;
	if (!normalizeEditFormatLine(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, normalized.line, &normalized.leftMargin, &normalized.rightMargin, nullptr)) normalized.line = defaultEditFormatLineForTabSize(settings.tabSize, settings.leftMargin, settings.rightMargin);
	return normalized;
}

void MRTextFormatting::effectiveMargins(const MREditSetupSettings &settings, int &leftMargin, int &rightMargin) noexcept {
	std::string normalized;

	leftMargin = settings.leftMargin;
	rightMargin = settings.rightMargin;
	if (normalizeEditFormatLine(settings.formatLine, settings.tabSize, settings.leftMargin, settings.rightMargin, normalized, &leftMargin, &rightMargin, nullptr)) return;
	leftMargin = std::max(1, settings.leftMargin);
	rightMargin = std::max(leftMargin, settings.rightMargin > 0 ? settings.rightMargin : 78);
}

std::string MRTextFormatting::formatParagraphText(std::string_view paragraphText, int leftMargin, int rightMargin) {
	const int safeLeftMargin = std::max(1, leftMargin);
	const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
	const std::size_t indentWidth = static_cast<std::size_t>(safeLeftMargin - 1);
	const std::size_t contentWidth = static_cast<std::size_t>(std::max(1, safeRightMargin - safeLeftMargin + 1));
	const std::vector<std::string> words = splitWords(paragraphText);
	std::string formattedText;
	std::string currentLine;
	std::size_t currentLineLength = 0;

	if (words.empty()) return std::string();
	for (const std::string &word : words) {
		const std::size_t projectedLength = currentLine.empty() ? word.size() : currentLineLength + 1 + word.size();
		if (!currentLine.empty() && projectedLength > contentWidth) {
			if (!formattedText.empty()) formattedText.push_back('\n');
			formattedText.append(indentWidth, ' ');
			formattedText += currentLine;
			currentLine = word;
			currentLineLength = word.size();
			continue;
		}
		if (!currentLine.empty()) {
			currentLine.push_back(' ');
			++currentLineLength;
		}
		currentLine += word;
		currentLineLength += word.size();
	}
	if (!currentLine.empty()) {
		if (!formattedText.empty()) formattedText.push_back('\n');
		formattedText.append(indentWidth, ' ');
		formattedText += currentLine;
	}
	return formattedText;
}

std::string MRTextFormatting::justifyParagraphText(std::string_view paragraphText, int leftMargin, int rightMargin) {
	const int safeLeftMargin = std::max(1, leftMargin);
	const int safeRightMargin = std::max(safeLeftMargin, rightMargin);
	const std::size_t indentWidth = static_cast<std::size_t>(safeLeftMargin - 1);
	const std::size_t contentWidth = static_cast<std::size_t>(std::max(1, safeRightMargin - safeLeftMargin + 1));
	const std::vector<std::string> words = splitWords(paragraphText);
	std::vector<std::vector<std::string>> lines;
	std::vector<std::string> currentLineWords;
	std::size_t currentLineLength = 0;
	std::string justifiedText;

	if (words.empty()) return std::string();
	for (const std::string &word : words) {
		const std::size_t projectedLength = currentLineWords.empty() ? word.size() : currentLineLength + 1 + word.size();
		if (!currentLineWords.empty() && projectedLength > contentWidth) {
			lines.push_back(currentLineWords);
			currentLineWords.clear();
			currentLineLength = 0;
		}
		if (!currentLineWords.empty()) ++currentLineLength;
		currentLineWords.push_back(word);
		currentLineLength += word.size();
	}
	if (!currentLineWords.empty()) lines.push_back(currentLineWords);
	for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
		const bool lastLine = lineIndex + 1 == lines.size();
		std::size_t wordsLength = 0;
		std::string line(indentWidth, ' ');

		for (const std::string &word : lines[lineIndex])
			wordsLength += word.size();
		if (lastLine || lines[lineIndex].size() == 1) {
			for (std::size_t i = 0; i < lines[lineIndex].size(); ++i) {
				if (i != 0) line.push_back(' ');
				line += lines[lineIndex][i];
			}
		} else {
			const std::size_t gapCount = lines[lineIndex].size() - 1;
			const std::size_t totalSpaces = contentWidth > wordsLength ? contentWidth - wordsLength : gapCount;
			const std::size_t baseGap = totalSpaces / gapCount;
			const std::size_t extraGap = totalSpaces % gapCount;

			for (std::size_t i = 0; i < lines[lineIndex].size(); ++i) {
				if (i != 0) line.append(baseGap + (i <= extraGap ? 1 : 0), ' ');
				line += lines[lineIndex][i];
			}
		}
		if (!justifiedText.empty()) justifiedText.push_back('\n');
		justifiedText += line;
	}
	return justifiedText;
}
