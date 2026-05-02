#include "MRSyntax.hpp"

#include <cctype>

namespace {

std::string lowerCopy(const std::string &value) {
	std::string result = value;
	for (char &i : result)
		i = static_cast<char>(std::tolower(static_cast<unsigned char>(i)));
	return result;
}

std::string fileNamePart(const std::string &value) {
	std::size_t pos = value.find_last_of("/\\");
	return pos == std::string::npos ? value : value.substr(pos + 1);
}

std::string extensionPart(const std::string &value) {
	std::string name = fileNamePart(value);
	std::size_t pos = name.find_last_of('.');
	return pos == std::string::npos ? std::string() : lowerCopy(name.substr(pos));
}

void paint(MRSyntaxTokenMap &tokens, std::size_t start, std::size_t end, MRSyntaxToken token) {
	if (start > tokens.size()) start = tokens.size();
	if (end > tokens.size()) end = tokens.size();
	for (std::size_t i = start; i < end; ++i)
		tokens[i] = token;
}

std::size_t lineEndOf(const std::string &text, std::size_t lineStart) {
	while (lineStart < text.size() && text[lineStart] != '\r' && text[lineStart] != '\n')
		++lineStart;
	return lineStart;
}

// Optimization: skipWhitespace replaces find_first_not_of(" \t").
// Iterating manually avoids the overhead of std::string::find_first_not_of
// which performs poorly in hot paths like syntax highlighting.
static std::size_t skipWhitespace(const std::string &text, std::size_t pos = 0) {
	while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
		++pos;
	return pos == text.size() ? std::string::npos : pos;
}

void tokenizeMake(MRSyntaxTokenMap &tokens, const std::string &line) {
	std::size_t trimmed = skipWhitespace(line);
	if (trimmed != std::string::npos && line[trimmed] == '#') {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Comment);
		return;
	}

	if (trimmed != std::string::npos && line[trimmed] != '\t') {
		std::size_t colon = line.find(':', trimmed);
		std::size_t eq = line.find('=', trimmed);
		if (colon != std::string::npos && (eq == std::string::npos || colon < eq)) paint(tokens, trimmed, colon, MRSyntaxToken::Key);
		else if (eq != std::string::npos)
			paint(tokens, trimmed, eq, MRSyntaxToken::Directive);
	}

	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '#') {
			paint(tokens, i, line.size(), MRSyntaxToken::Comment);
			break;
		}
		if (line[i] == '$' && i + 1 < line.size() && (line[i + 1] == '(' || line[i + 1] == '{')) {
			char closer = line[i + 1] == '(' ? ')' : '}';
			std::size_t start = i;
			i += 2;
			while (i < line.size() && line[i] != closer)
				++i;
			if (i < line.size()) ++i;
			paint(tokens, start, i, MRSyntaxToken::Directive);
			continue;
		}
		++i;
	}
}


void tokenizeMarkdown(MRSyntaxTokenMap &tokens, const std::string &line) {
	std::size_t trimmed = skipWhitespace(line);
	if (trimmed == std::string::npos) return;
	if (line.compare(trimmed, 3, "```") == 0) {
		paint(tokens, trimmed, line.size(), MRSyntaxToken::Directive);
		return;
	}
	if (line[trimmed] == '#') {
		std::size_t i = trimmed;
		while (i < line.size() && line[i] == '#')
			++i;
		if (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
			paint(tokens, trimmed, line.size(), MRSyntaxToken::Heading);
			return;
		}
	}
	for (std::size_t i = 0; i < line.size();) {
		if (line[i] == '`') {
			std::size_t start = i++;
			while (i < line.size() && line[i] != '`')
				++i;
			if (i < line.size()) ++i;
			paint(tokens, start, i, MRSyntaxToken::String);
			continue;
		}
		++i;
	}
}

} // namespace

MRSyntaxLanguage tmrDetectSyntaxLanguage(const std::string &path, const std::string &title) {
	std::string fileName = fileNamePart(!path.empty() ? path : title);
	std::string lowerName = lowerCopy(fileName);
	std::string ext = extensionPart(fileName);

	if (lowerName == "makefile" || lowerName == "gnumakefile" || ext == ".mk" || ext == ".mak") return MRSyntaxLanguage::Make;
	if (ext == ".mrmac") return MRSyntaxLanguage::MRMAC;
	if (ext == ".md" || ext == ".markdown" || lowerName == "readme") return MRSyntaxLanguage::Markdown;
	return MRSyntaxLanguage::PlainText;
}

std::string tmrDetectTreeSitterLanguageName(const std::string &path, const std::string &title) {
	std::string fileName = fileNamePart(!path.empty() ? path : title);
	std::string lowerName = lowerCopy(fileName);
	std::string ext = extensionPart(fileName);

	if (ext == ".c") return "C";
	if (ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx" || ext == ".ipp" || ext == ".tpp" || ext == ".inl")
		return "CPP";
	if (ext == ".mrmac") return "MRMAC";
	if (ext == ".js" || ext == ".mjs" || ext == ".cjs") return "JAVASCRIPT";
	if (ext == ".py" || ext == ".pyw") return "PYTHON";
	if (ext == ".json") return "JSON";
	if (lowerName == "compile_commands.json") return "JSON";
	return std::string();
}

const char *tmrSyntaxLanguageName(MRSyntaxLanguage language) noexcept {
	switch (language) {
		case MRSyntaxLanguage::MRMAC:
			return "MRMAC";
		case MRSyntaxLanguage::Make:
			return "Make";
		case MRSyntaxLanguage::Markdown:
			return "Markdown";
		default:
			return "Plain Text";
	}
}

MRSyntaxTokenMap tmrBuildTokenMapForLine(MRSyntaxLanguage language, const std::string &text, std::size_t lineStart) {
	if (lineStart > text.size()) lineStart = text.size();
	std::size_t lineEnd = lineEndOf(text, lineStart);
	return tmrBuildTokenMapForTextLine(language, text.substr(lineStart, lineEnd - lineStart));
}

MRSyntaxTokenMap tmrBuildTokenMapForTextLine(MRSyntaxLanguage language, const std::string &line) {
	MRSyntaxTokenMap tokens(line.size(), MRSyntaxToken::Text);

	switch (language) {
		case MRSyntaxLanguage::Make:
			tokenizeMake(tokens, line);
			break;
		case MRSyntaxLanguage::Markdown:
			tokenizeMarkdown(tokens, line);
			break;
		default:
			break;
	}
	return tokens;
}
