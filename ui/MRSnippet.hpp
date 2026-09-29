#ifndef MRSNIPPET_HPP
#define MRSNIPPET_HPP

#include "MRSidekickEditor.hpp"

#include <cstddef>
#include <string>
#include <vector>

class TFrame;

class MRSnippet final {
  public:
	static MRSnippet &instance() noexcept;

	bool open(MREditWindow *parent, const std::string &text, const std::string &title, std::size_t replaceStart, std::size_t replaceEnd,
	          const std::vector<MRSidekickSpan> &placeholders, int anchorViewColumn, int anchorViewRow, bool &committed);
	bool handleEditorKey(MREditWindow *parent, const TEvent &event);
	bool moveForParent(const MREditWindow *parent, int direction);

  private:
	friend class MRSidekickEditor;
	class Dialog;
	class HelpButton;
	class HintGuard;
	struct DrawColors {
		TColorAttr text;
		TColorAttr selected;
		TColorAttr placeholder;
	};
	enum class Action : unsigned char {
		CursorLeft, CursorRight, CursorUp, CursorDown, CursorHome, CursorEnd, CursorWordLeft, CursorWordRight,
		DeleteBackwardChar, DeleteForwardChar, DeleteBackwardWord, DeleteForwardWord, DeleteBackwardToHome,
		DeleteToEndOfLine, DeleteLine, LoadBlockFromFile, PlaceholderNext, PlaceholderPrevious
	};

	MRSnippet() = default;
	MRSnippet(const MRSnippet &) = delete;
	MRSnippet &operator=(const MRSnippet &) = delete;

	bool isEditor(const MRSidekickEditor *editor) const noexcept;
	bool movePlaceholder(MRSidekickEditor &editor, int direction);
	bool replacePlaceholder(MRSidekickEditor &editor, const std::string &replacement);
	void adjustAfterInsert(MRSidekickEditor &editor, std::size_t offset, std::size_t length);
	void adjustAfterErase(MRSidekickEditor &editor, std::size_t offset, std::size_t length);
	void resizeForContent(MRSidekickEditor &editor);
	bool handleRuntimeKeymap(MRSidekickEditor &editor, TEvent &event);
	void commit(MRSidekickEditor &editor);
	DrawColors drawColors(const MRSidekickEditor &editor, TColorAttr baseTextColor) const noexcept;
	TColorAttr placeholderColor(std::size_t offset, const DrawColors &colors) const noexcept;
	TColorAttr dialogColor(uchar index) const noexcept;
	std::size_t wordLeftOffset(const std::string &value, std::size_t offset) const noexcept;
	std::size_t wordRightOffset(const std::string &value, std::size_t offset) const noexcept;
	void detach(const MRSidekickEditor *editor) noexcept;

	void attach(MRSidekickEditor *editor, const std::vector<MRSidekickSpan> &placeholders, std::size_t replaceStart, std::size_t replaceEnd);
	void selectPlaceholder(MRSidekickEditor &editor, int direction);
	void setCursorFromActivePlaceholder(MRSidekickEditor &editor);
	bool handleAction(MRSidekickEditor &editor, const std::string &actionId);
	bool actionFromId(const std::string &actionId, Action &action) const noexcept;
	bool loadBlockFromFile(MRSidekickEditor &editor);
	TRect boundsFor(MREditWindow *parent, const std::string &text, std::size_t replaceStart, int anchorViewColumn, int anchorViewRow) const;
	static TFrame *frame(TRect bounds);
	static bool wordByte(char ch) noexcept;

	MRSidekickEditor *activeEditor = nullptr;
	std::vector<MRSidekickSpan> spans;
	std::vector<unsigned char> touched;
	int activeIndex = -1;
	bool endEdge = false;
	std::size_t replaceStart = 0;
	std::size_t replaceEnd = 0;
};

#endif
