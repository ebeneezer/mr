#ifndef MRSYNTAX_HPP
#define MRSYNTAX_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class MRSyntaxLanguage : unsigned char {
	PlainText,
	C,
	Cpp,
	JavaScript,
	Python,
	Json,
	Yaml,
	Xml,
	Bash,
	Zsh,
	Fish,
	Perl,
	Swift,
	Rust,
	Go,
	Pascal,
	Systemd,
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
	Delimiter,
	Heading
};

using MRSyntaxTokenMap = std::vector<MRSyntaxToken>;

enum class MRSyntaxMode : std::uint16_t {
	Normal,
	BlockComment,
	DirectiveContinuation,
	HereDocument,
	RawString,
	QuotedString
};

struct MRSyntaxLineState {
	MRSyntaxMode mode;
	std::uint16_t flags;
	std::uint32_t payload;

	MRSyntaxLineState() noexcept : mode(MRSyntaxMode::Normal), flags(0), payload(0) {}
};

inline bool operator==(const MRSyntaxLineState &a, const MRSyntaxLineState &b) noexcept {
	return a.mode == b.mode && a.flags == b.flags && a.payload == b.payload;
}

inline bool operator!=(const MRSyntaxLineState &a, const MRSyntaxLineState &b) noexcept {
	return !(a == b);
}

struct MRSyntaxTokenRun {
	std::uint32_t column;
	std::uint32_t length;
	MRSyntaxToken token;

	MRSyntaxTokenRun() noexcept : column(0), length(0), token(MRSyntaxToken::Text) {}
	MRSyntaxTokenRun(std::uint32_t aColumn, std::uint32_t aLength, MRSyntaxToken aToken) noexcept : column(aColumn), length(aLength), token(aToken) {}
};

struct MRSyntaxLineResult {
	MRSyntaxLineState stateOut;
	std::vector<MRSyntaxTokenRun> tokenRuns;
};

struct MRSyntaxClassification {
	MRSyntaxLanguage language;
	std::uint16_t confidence;

	MRSyntaxClassification() noexcept : language(MRSyntaxLanguage::PlainText), confidence(0) {}
	MRSyntaxClassification(MRSyntaxLanguage aLanguage, std::uint16_t aConfidence) noexcept : language(aLanguage), confidence(aConfidence) {}
};

class MRSyntaxHighlighter {
  public:
	virtual ~MRSyntaxHighlighter() = default;
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) = 0;
};

class MRPlainTextHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRMakeSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRMarkdownSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRMRmacSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRCppSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRJavaScriptSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRPythonSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRJsonSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRYamlSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRXmlSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRBashSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRZshSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRFishSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRPerlSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRSwiftSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRRustSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRGoSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRPascalSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

class MRSystemdSyntaxHighlighter final : public MRSyntaxHighlighter {
  public:
	virtual MRSyntaxLineResult highlightLine(std::string_view line, MRSyntaxLineState previousState) override;
};

MRSyntaxLanguage tmrDetectSyntaxLanguage(const std::string &path, const std::string &title = std::string());
MRSyntaxClassification tmrClassifySyntaxLanguage(const std::string &path, const std::string &title, std::string_view text);
const char *tmrSyntaxLanguageName(MRSyntaxLanguage language) noexcept;
const char *tmrSyntaxLanguageMarker(MRSyntaxLanguage language) noexcept;
std::uint32_t tmrSyntaxLanguageMarkerRgb(MRSyntaxLanguage language) noexcept;
std::vector<MRSyntaxTokenRun> tmrBuildTokenRunsFromTokenMap(const MRSyntaxTokenMap &tokenMap);
MRSyntaxLineResult tmrHighlightTextLine(MRSyntaxLanguage language, std::string_view line, MRSyntaxLineState previousState = MRSyntaxLineState());
MRSyntaxTokenMap tmrBuildLegacyTokenMapForTextLine(MRSyntaxLanguage language, std::string_view line, MRSyntaxLineState previousState = MRSyntaxLineState());

#endif
