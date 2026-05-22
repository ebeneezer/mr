#ifndef MRSIDEKICKEDITOR_HPP
#define MRSIDEKICKEDITOR_HPP

#define Uses_TView
#include <tvision/tv.h>

#include <cstddef>
#include <string>
#include <vector>

class MREditWindow;

struct MRSidekickSpan {
	std::size_t start;
	std::size_t end;
};

class MRSidekickEditor : public TView {
  public:
	MRSidekickEditor(const TRect &bounds, int parentBufferId, std::size_t replaceStart, std::size_t replaceEnd, std::string text, std::string title, std::vector<MRSidekickSpan> placeholders);
	~MRSidekickEditor() override;

	void draw() override;
	void handleEvent(TEvent &event) override;

	[[nodiscard]] int parentBufferId() const noexcept;

  private:
	int mParentBufferId;
	std::size_t mReplaceStart;
	std::size_t mReplaceEnd;
	std::string mTitle;
	std::vector<std::string> mLines;
	std::vector<MRSidekickSpan> mPlaceholders;
	int mPlaceholderIndex;
	int mCursorRow;
	int mCursorCol;

	void setText(std::string text);
	[[nodiscard]] std::string text() const;
	void closeSidekick();
	void commitAndClose();
	void insertChar(char ch);
	void insertNewLine();
	void eraseBackward();
	void eraseForward();
	void moveLeft();
	void moveRight();
	void moveUp();
	void moveDown();
	void moveToPlaceholder(int direction);
	void setCursorFromOffset(std::size_t offset);
	[[nodiscard]] std::size_t cursorOffset() const noexcept;
	[[nodiscard]] bool offsetInPlaceholder(std::size_t offset) const noexcept;
	void adjustPlaceholdersAfterInsert(std::size_t offset, std::size_t length);
	void adjustPlaceholdersAfterErase(std::size_t offset, std::size_t length);
	void clampCursor() noexcept;
};

bool mrOpenSnippetSidekick(MREditWindow *parent, std::size_t replaceStart, std::size_t replaceEnd, const std::string &text, const std::string &title, const std::vector<MRSidekickSpan> &placeholders = std::vector<MRSidekickSpan>());
void mrDropSidekickForParent(const MREditWindow *parent);
void mrDropActiveSidekick();

#endif
