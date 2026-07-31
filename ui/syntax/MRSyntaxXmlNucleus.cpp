#include "MRSyntaxXmlNucleus.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace {
constexpr std::size_t kMaximumXmlNucleusDepth = 64;

bool isXmlWhitespace(char ch) noexcept {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool isXmlNameStart(char ch) noexcept {
	return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == ':';
}

bool isXmlNameCharacter(char ch) noexcept {
	return isXmlNameStart(ch) || std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '.';
}

class XmlNucleusScanner {
  public:
	explicit XmlNucleusScanner(std::string_view source) noexcept
	    : text(source), position(0), elementStack(), depth(0), startTags(0), endTags(0), matchedElements(0), selfClosingTags(0), attributes(0), namespaceAttributes(0),
	      xmlDeclarations(0), doctypes(0), comments(0), cdataSections(0), rootSeen(false), rootClosed(false), invalid(false), depthLimitReached(false) {
	}

	MRSyntaxXmlNucleusEvidence analyze() noexcept {
		skipByteOrderMark();
		while (position < text.size() && !invalid && !depthLimitReached) {
			if (text[position] != '<') {
				parseText();
				continue;
			}
			if (startsWith("<!--")) {
				parseComment();
				continue;
			}
			if (startsWith("<![CDATA[")) {
				parseCdata();
				continue;
			}
			if (startsWith("<?")) {
				parseProcessingInstruction();
				continue;
			}
			if (startsWith("<!DOCTYPE")) {
				parseDoctype();
				continue;
			}
			if (startsWith("</")) {
				parseEndTag();
				continue;
			}
			if (startsWith("<!")) {
				invalid = true;
				break;
			}
			parseStartTag();
		}
		return evidence();
	}

  private:
	std::string_view text;
	std::size_t position;
	std::array<std::string_view, kMaximumXmlNucleusDepth> elementStack;
	std::size_t depth;
	int startTags;
	int endTags;
	int matchedElements;
	int selfClosingTags;
	int attributes;
	int namespaceAttributes;
	int xmlDeclarations;
	int doctypes;
	int comments;
	int cdataSections;
	bool rootSeen;
	bool rootClosed;
	bool invalid;
	bool depthLimitReached;

	bool startsWith(std::string_view token) const noexcept {
		return position <= text.size() && token.size() <= text.size() - position && text.substr(position, token.size()) == token;
	}

	void skipByteOrderMark() noexcept {
		if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
			position = 3;
	}

	void skipWhitespace() noexcept {
		while (position < text.size() && isXmlWhitespace(text[position]))
			++position;
	}

	bool parseName(std::string_view &name) noexcept {
		if (position >= text.size() || !isXmlNameStart(text[position])) return false;
		const std::size_t start = position++;
		while (position < text.size() && isXmlNameCharacter(text[position]))
			++position;
		name = text.substr(start, position - start);
		return true;
	}

	bool parseQuotedValue() noexcept {
		if (position >= text.size() || (text[position] != '\'' && text[position] != '"')) return false;
		const char quote = text[position++];
		while (position < text.size()) {
			const char ch = text[position++];
			if (ch == quote) return true;
			if (ch == '<') return false;
		}
		return false;
	}

	void parseText() noexcept {
		const std::size_t nextTag = text.find('<', position);
		const std::size_t end = nextTag == std::string_view::npos ? text.size() : nextTag;
		if (depth == 0) {
			for (std::size_t i = position; i < end; ++i)
				if (!isXmlWhitespace(text[i])) {
					invalid = true;
					break;
				}
		}
		position = end;
	}

	void parseComment() noexcept {
		const std::size_t end = text.find("-->", position + 4);
		if (end == std::string_view::npos) {
			position = text.size();
			return;
		}
		++comments;
		position = end + 3;
	}

	void parseCdata() noexcept {
		if (depth == 0) {
			invalid = true;
			return;
		}
		const std::size_t end = text.find("]]>", position + 9);
		if (end == std::string_view::npos) {
			position = text.size();
			return;
		}
		++cdataSections;
		position = end + 3;
	}

	void parseProcessingInstruction() noexcept {
		position += 2;
		std::string_view target;
		if (!parseName(target)) {
			invalid = true;
			return;
		}
		char quote = '\0';
		while (position < text.size()) {
			const char ch = text[position];
			if (quote != '\0') {
				if (ch == quote) quote = '\0';
				++position;
				continue;
			}
			if (ch == '\'' || ch == '"') {
				quote = ch;
				++position;
				continue;
			}
			if (ch == '?' && position + 1 < text.size() && text[position + 1] == '>') {
				position += 2;
				if (!rootSeen && target == "xml") ++xmlDeclarations;
				return;
			}
			if (ch == '<') {
				invalid = true;
				return;
			}
			++position;
		}
	}

	void parseDoctype() noexcept {
		if (rootSeen) {
			invalid = true;
			return;
		}
		position += 9;
		int subsetDepth = 0;
		char quote = '\0';
		while (position < text.size()) {
			const char ch = text[position++];
			if (quote != '\0') {
				if (ch == quote) quote = '\0';
				continue;
			}
			if (ch == '\'' || ch == '"') {
				quote = ch;
				continue;
			}
			if (ch == '[') {
				++subsetDepth;
				continue;
			}
			if (ch == ']' && subsetDepth > 0) {
				--subsetDepth;
				continue;
			}
			if (ch == '>' && subsetDepth == 0) {
				++doctypes;
				return;
			}
		}
	}

	void parseStartTag() noexcept {
		++position;
		std::string_view name;
		if (!parseName(name)) {
			if (position >= text.size()) return;
			invalid = true;
			return;
		}
		bool selfClosing = false;
		bool tagClosed = false;
		int parsedAttributes = 0;
		int parsedNamespaceAttributes = 0;
		while (position < text.size()) {
			const std::size_t beforeWhitespace = position;
			skipWhitespace();
			const bool separated = position != beforeWhitespace;
			if (position >= text.size()) return;
			if (text[position] == '>') {
				++position;
				tagClosed = true;
				break;
			}
			if (text[position] == '/' && position + 1 < text.size() && text[position + 1] == '>') {
				position += 2;
				selfClosing = true;
				tagClosed = true;
				break;
			}
			if (!separated) {
				invalid = true;
				return;
			}
			std::string_view attributeName;
			if (!parseName(attributeName)) {
				invalid = true;
				return;
			}
			skipWhitespace();
			if (position >= text.size()) return;
			if (text[position] != '=') {
				invalid = true;
				return;
			}
			++position;
			skipWhitespace();
			if (!parseQuotedValue()) {
				if (position >= text.size()) return;
				invalid = true;
				return;
			}
			++parsedAttributes;
			if (attributeName == "xmlns" || (attributeName.size() > 6 && attributeName.substr(0, 6) == "xmlns:"))
				++parsedNamespaceAttributes;
		}
		if (!tagClosed) return;
		++startTags;
		attributes += parsedAttributes;
		namespaceAttributes += parsedNamespaceAttributes;
		if (depth == 0) {
			if (rootSeen || rootClosed) {
				invalid = true;
				return;
			}
			rootSeen = true;
		}
		if (selfClosing) {
			++selfClosingTags;
			++matchedElements;
			if (depth == 0) rootClosed = true;
			return;
		}
		if (depth >= elementStack.size()) {
			depthLimitReached = true;
			return;
		}
		elementStack[depth++] = name;
	}

	void parseEndTag() noexcept {
		position += 2;
		std::string_view name;
		if (!parseName(name)) {
			if (position >= text.size()) return;
			invalid = true;
			return;
		}
		skipWhitespace();
		if (position >= text.size()) return;
		if (text[position] != '>' || depth == 0 || elementStack[depth - 1] != name) {
			invalid = true;
			return;
		}
		++position;
		++endTags;
		++matchedElements;
		--depth;
		if (depth == 0) rootClosed = true;
	}

	MRSyntaxXmlNucleusEvidence evidence() const noexcept {
		MRSyntaxXmlNucleusEvidence result;
		if (invalid || !rootSeen) return result;

		result.structuralTags = startTags + endTags;
		result.score = std::min(startTags, 12) * 2 + std::min(matchedElements, 8) * 4 + std::min(attributes, 8) + xmlDeclarations * 10 + doctypes * 8 +
		               std::min(namespaceAttributes, 2) * 8 + std::min(comments, 2) * 2 + std::min(cdataSections, 2) * 4;
		result.strongSignals = std::min(4, matchedElements + xmlDeclarations * 2 + doctypes * 2 + namespaceAttributes * 2 + (rootClosed ? 2 : 0));
		result.decisive = (xmlDeclarations > 0 && startTags > 0) || (doctypes > 0 && startTags > 0) || (namespaceAttributes > 0 && startTags > 0) ||
		                  (rootClosed && matchedElements >= 2) || (startTags >= 3 && matchedElements >= 2) || (startTags >= 4 && selfClosingTags >= 2);
		return result;
	}
};
} // namespace

MRSyntaxXmlNucleusEvidence tmrAnalyzeSyntaxXmlNucleus(std::string_view text) noexcept {
	static constexpr std::size_t maximumSampleLength = 64 * 1024;
	if (text.size() > maximumSampleLength) text = text.substr(0, maximumSampleLength);
	XmlNucleusScanner scanner(text);
	return scanner.analyze();
}
