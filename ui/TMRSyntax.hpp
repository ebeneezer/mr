#ifndef TMRSYNTAX_HPP
#define TMRSYNTAX_HPP

#include <cstddef>
#include <string>
#include <vector>

enum class TMRSyntaxLanguage : unsigned char {
	PlainText,
	CFamily,
	MRMAC,
	Make,
	Json,
	Ini,
	Markdown
};

enum class TMRSyntaxToken : unsigned char {
	Text,
	Keyword,
	Type,
	Number,
	String,
	Comment,
	Directive,
	Section,
	Key,
	Heading
};

using TMRSyntaxTokenMap = std::vector<TMRSyntaxToken>;

TMRSyntaxLanguage tmrDetectSyntaxLanguage(const std::string &path, const std::string &title = std::string());
const char *tmrSyntaxLanguageName(TMRSyntaxLanguage language) noexcept;
TMRSyntaxTokenMap tmrBuildTokenMapForTextLine(TMRSyntaxLanguage language, const std::string &line);
TMRSyntaxTokenMap tmrBuildTokenMapForLine(TMRSyntaxLanguage language, const std::string &text,
                                          std::size_t lineStart);

#endif
