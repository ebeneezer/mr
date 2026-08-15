#ifndef MRSIDEKICKEDITOR_HPP
#define MRSIDEKICKEDITOR_HPP

#define Uses_TScroller
#include <tvision/tv.h>

#include <cstddef>
#include <string>
#include <vector>

class MREditWindow;
class TGroup;
class TScrollBar;

struct MRSidekickSpan {
	std::size_t start;
	std::size_t end;
};

enum class MRReadOnlySidekickPlacement : unsigned char {
	UnderCode,
	RightMargin
};

enum class MRSidekickPalette : unsigned char {
	Sidekick,
	Owner
};

class MRSidekickEditor : public TScroller {
  public:
	MRSidekickEditor(const TRect &bounds, int parentBufferId, std::size_t replaceStart, std::size_t replaceEnd, std::string text, std::string title, std::vector<MRSidekickSpan> placeholders, bool readOnly = false, bool modalClose = false, bool snippetSidekick = false, MRSidekickPalette palette = MRSidekickPalette::Sidekick);
	~MRSidekickEditor() override;

	void draw() override;
	void handleEvent(TEvent &event) override;
	void insertInto(TGroup &group);

	[[nodiscard]] int parentBufferId() const noexcept;
	[[nodiscard]] bool isReadOnly() const noexcept;
	[[nodiscard]] bool isSnippetSidekick() const noexcept;
	bool moveSnippetPlaceholder(int direction);
	void updateReadOnlyText(std::string text, std::string title, const TRect &bounds);

  private:
	int mParentBufferId;
	std::size_t mReplaceStart;
	std::size_t mReplaceEnd;
	std::string mTitle;
	std::vector<std::string> mLines;
	std::vector<MRSidekickSpan> mPlaceholders;
	std::vector<unsigned char> mPlaceholderTouched;
	int mPlaceholderIndex;
	bool mPlaceholderEndEdge;
	int mCursorRow;
	int mCursorCol;
	bool mReadOnly;
	bool mModalClose;
	bool mSnippetSidekick;
	MRSidekickPalette mPalette;
	TRect mOuterBounds;
	TScrollBar *mHorizontalScrollBar;
	TScrollBar *mVerticalScrollBar;

	void setText(std::string text);
	[[nodiscard]] std::string text() const;
	void updateScrollBars(const TRect &bounds);
	void destroyScrollBars();
	void detachFromOwner();
	void ensureCursorVisible();
	void closeSidekick(ushort command = cmCancel);
	void commitAndClose();
	void insertChar(char ch);
	void insertTextAtCursor(const std::string &value);
	void insertNewLine();
	void eraseBackward();
	void eraseForward();
	void eraseWordBackward();
	void eraseWordForward();
	void eraseToLineStart();
	void eraseToLineEnd();
	void eraseLine();
	bool replaceActivePlaceholder(const std::string &replacement);
	bool handleRuntimeKeymap(TEvent &event);
	bool handleSnippetSidekickAction(const std::string &actionId);
	bool loadBlockFromFileIntoSnippetSidekick();
	void moveLeft();
	void moveRight();
	void moveUp();
	void moveDown();
	void moveLineStart() noexcept;
	void moveLineEnd() noexcept;
	void moveWordLeft();
	void moveWordRight();
	void moveToPlaceholder(int direction);
	void setCursorFromActivePlaceholder();
	void setCursorFromOffset(std::size_t offset);
	[[nodiscard]] std::size_t cursorOffset() const noexcept;
	void adjustPlaceholdersAfterInsert(std::size_t offset, std::size_t length);
	void adjustPlaceholdersAfterErase(std::size_t offset, std::size_t length);
	void resizeSnippetSidekickForContent();
	void clampCursor() noexcept;

	friend void mrDropActiveSidekick();
};

bool mrOpenReadOnlySidekickAt(MREditWindow *parent, const std::string &text, const std::string &title, int anchorViewColumn, int anchorViewRow, int preferredViewColumn = 0, MRReadOnlySidekickPlacement placement = MRReadOnlySidekickPlacement::RightMargin);
bool mrOpenSnippetSidekickAt(MREditWindow *parent, const std::string &text, const std::string &title, std::size_t replaceStart, std::size_t replaceEnd, const std::vector<MRSidekickSpan> &placeholders, int anchorViewColumn, int anchorViewRow, bool &committed);
bool mrHasReadOnlySidekickForParent(const MREditWindow *parent);
bool mrConsumeReadOnlySidekickDismissedForParent(const MREditWindow *parent);
bool mrMoveSnippetPlaceholderForParent(const MREditWindow *parent, int direction);
void mrDropSidekickForParent(const MREditWindow *parent);
void mrDropActiveSidekick();

#endif
