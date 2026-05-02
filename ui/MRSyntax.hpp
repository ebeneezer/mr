#ifndef MRSYNTAX_HPP
#define MRSYNTAX_HPP

#include <cstddef>
#include <string>
#include <vector>

enum class MRSyntaxLanguage : unsigned char {
	PlainText,
	MRMAC,
	Make,
	Markdown
};

enum class MRSyntaxToken : unsigned char {
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

using MRSyntaxTokenMap = std::vector<MRSyntaxToken>;

MRSyntaxLanguage tmrDetectSyntaxLanguage(const std::string &path, const std::string &title = std::string());
std::string tmrDetectTreeSitterLanguageName(const std::string &path, const std::string &title = std::string());
const char *tmrSyntaxLanguageName(MRSyntaxLanguage language) noexcept;
MRSyntaxTokenMap tmrBuildTokenMapForTextLine(MRSyntaxLanguage language, const std::string &line);
MRSyntaxTokenMap tmrBuildTokenMapForLine(MRSyntaxLanguage language, const std::string &text, std::size_t lineStart);

#endif
