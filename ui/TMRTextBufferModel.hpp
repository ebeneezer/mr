#ifndef TMRTEXTBUFFERMODEL_HPP
#define TMRTEXTBUFFERMODEL_HPP

#include <algorithm>
#include <cstddef>
#include <string>

#include "TMRSyntax.hpp"

class TMRTextBufferModel {
  public:
	TMRTextBufferModel() noexcept
	    : text_(), cursor_(0), selectionStart_(0), selectionEnd_(0), modified_(false),
	      language_(TMRSyntaxLanguage::PlainText), syntaxPathHint_(), syntaxTitleHint_() {
	}

	void setText(const char *data, std::size_t length) {
		if (data == nullptr || length == 0)
			text_.clear();
		else
			text_.assign(data, length);
		clampState();
	}

	void setText(const std::string &text) {
		text_ = text;
		clampState();
	}

	const std::string &text() const noexcept {
		return text_;
	}

	std::size_t length() const noexcept {
		return text_.size();
	}

	bool isEmpty() const noexcept {
		return text_.empty();
	}

	char charAt(std::size_t pos) const noexcept {
		return pos < text_.size() ? text_[pos] : '\0';
	}

	std::size_t lineCount() const noexcept {
		std::size_t lines = 1;
		for (std::size_t i = 0; i < text_.size(); ++i)
			if (text_[i] == '\n')
				++lines;
		return lines;
	}

	std::size_t cursor() const noexcept {
		return cursor_;
	}

	void setCursor(std::size_t pos) noexcept {
		cursor_ = clampOffset(pos);
	}

	void setSelection(std::size_t start, std::size_t end) noexcept {
		selectionStart_ = clampOffset(start);
		selectionEnd_ = clampOffset(end);
	}

	void setCursorAndSelection(std::size_t cursor, std::size_t start, std::size_t end) noexcept {
		cursor_ = clampOffset(cursor);
		setSelection(start, end);
	}

	bool hasSelection() const noexcept {
		return selectionStart_ != selectionEnd_;
	}

	std::size_t selectionStart() const noexcept {
		return selectionStart_;
	}

	std::size_t selectionEnd() const noexcept {
		return selectionEnd_;
	}

	bool isModified() const noexcept {
		return modified_;
	}

	void setModified(bool changed) noexcept {
		modified_ = changed;
	}

	void setSyntaxContext(const std::string &path, const std::string &title = std::string()) {
		syntaxPathHint_ = path;
		syntaxTitleHint_ = title;
		language_ = tmrDetectSyntaxLanguage(syntaxPathHint_, syntaxTitleHint_);
	}

	TMRSyntaxLanguage language() const noexcept {
		return language_;
	}

	const char *languageName() const noexcept {
		return tmrSyntaxLanguageName(language_);
	}

	TMRSyntaxTokenMap tokenMapForLine(std::size_t pos) const {
		return tmrBuildTokenMapForLine(language_, text_, lineStart(pos));
	}

	std::size_t lineStart(std::size_t pos) const noexcept {
		pos = clampOffset(pos);
		while (pos > 0 && text_[pos - 1] != '\n')
			--pos;
		return pos;
	}

	std::size_t lineEnd(std::size_t pos) const noexcept {
		pos = clampOffset(pos);
		while (pos < text_.size() && text_[pos] != '\n')
			++pos;
		return pos;
	}

	std::size_t nextLine(std::size_t pos) const noexcept {
		pos = lineEnd(pos);
		if (pos < text_.size() && text_[pos] == '\n')
			++pos;
		return pos;
	}

	std::size_t prevLine(std::size_t pos) const noexcept {
		pos = lineStart(pos);
		if (pos == 0)
			return 0;
		return lineStart(pos - 1);
	}

	std::size_t lineIndex(std::size_t pos) const noexcept {
		std::size_t line = 0;
		pos = clampOffset(pos);
		for (std::size_t i = 0; i < pos; ++i)
			if (text_[i] == '\n')
				++line;
		return line;
	}

	std::size_t column(std::size_t pos) const noexcept {
		pos = clampOffset(pos);
		return pos - lineStart(pos);
	}

	std::string lineText(std::size_t pos) const {
		std::size_t start = lineStart(pos);
		std::size_t end = lineEnd(pos);
		return text_.substr(start, end - start);
	}

  private:
	std::size_t clampOffset(std::size_t pos) const noexcept {
		return std::min(pos, text_.size());
	}

	void clampState() noexcept {
		cursor_ = clampOffset(cursor_);
		selectionStart_ = clampOffset(selectionStart_);
		selectionEnd_ = clampOffset(selectionEnd_);
	}

	std::string text_;
	std::size_t cursor_;
	std::size_t selectionStart_;
	std::size_t selectionEnd_;
	bool modified_;
	TMRSyntaxLanguage language_;
	std::string syntaxPathHint_;
	std::string syntaxTitleHint_;
};

#endif
