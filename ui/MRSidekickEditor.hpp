#ifndef MRSIDEKICKEDITOR_HPP
#define MRSIDEKICKEDITOR_HPP

#define Uses_TScroller
#include <tvision/tv.h>

#include <cstddef>
#include <string>
#include <vector>

class MREditWindow;
class MRSnippet;
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
	MRSidekickEditor(const TRect &bounds, int parentBufferId, std::string text, std::string title, bool readOnly = false, bool modalClose = false, MRSidekickPalette palette = MRSidekickPalette::Sidekick);
	~MRSidekickEditor() override;

	void draw() override;
	void handleEvent(TEvent &event) override;
	void insertInto(TGroup &group);

	[[nodiscard]] int parentBufferId() const noexcept;
	[[nodiscard]] bool isReadOnly() const noexcept;
	void updateReadOnlyText(std::string text, std::string title, const TRect &bounds);

  private:
	int mParentBufferId;
	std::string mTitle;
	std::vector<std::string> mLines;
	int mCursorRow;
	int mCursorCol;
	bool mReadOnly;
	bool mModalClose;
	MRSidekickPalette mPalette;
	TRect mOuterBounds;
	TScrollBar *mHorizontalScrollBar;
	TScrollBar *mVerticalScrollBar;
	bool mApiReferenceActive = false;
	std::size_t mApiReferenceAnchorOffset = 0;
	std::size_t mApiReferenceCursorOffset = 0;
	int mApiReferenceDeltaX = 0;
	int mApiReferenceDeltaY = 0;
	MRReadOnlySidekickPlacement mApiReferencePlacement = MRReadOnlySidekickPlacement::RightMargin;
	std::string mApiReferenceText;

	void setText(std::string text);
	[[nodiscard]] std::string text() const;
	void updateScrollBars(const TRect &bounds);
	void destroyScrollBars();
	void detachFromOwner();
	void ensureCursorVisible();
	void closeSidekick(ushort command = cmCancel);
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
	void moveLeft();
	void moveRight();
	void moveUp();
	void moveDown();
	void moveLineStart() noexcept;
	void moveLineEnd() noexcept;
	void moveWordLeft();
	void moveWordRight();
	void setCursorFromOffset(std::size_t offset);
	[[nodiscard]] std::size_t cursorOffset() const noexcept;
	void clampCursor() noexcept;

	friend class MRSnippet;
	friend void mrDropActiveSidekick();
	friend bool mrOpenReadOnlySidekickAt(MREditWindow *, const std::string &, const std::string &, int, int, int, MRReadOnlySidekickPlacement, std::size_t);
	friend bool mrDismissApiReferenceSidekickForParent(const MREditWindow *);
	friend void mrSyncApiReferenceSidekickForParent(MREditWindow *, bool);
};

bool mrOpenReadOnlySidekickAt(MREditWindow *parent, const std::string &text, const std::string &title, int anchorViewColumn, int anchorViewRow, int preferredViewColumn = 0, MRReadOnlySidekickPlacement placement = MRReadOnlySidekickPlacement::RightMargin, std::size_t apiReferenceAnchorOffset = std::string::npos);
bool mrDismissApiReferenceSidekickForParent(const MREditWindow *parent);
void mrSyncApiReferenceSidekickForParent(MREditWindow *parent, bool sourceScroll);
bool mrOpenSnippetSidekickAt(MREditWindow *parent, const std::string &text, const std::string &title, std::size_t replaceStart, std::size_t replaceEnd, const std::vector<MRSidekickSpan> &placeholders, int anchorViewColumn, int anchorViewRow, bool &committed);
bool mrHasReadOnlySidekickForParent(const MREditWindow *parent);
bool mrConsumeReadOnlySidekickDismissedForParent(const MREditWindow *parent);
bool mrMoveSnippetPlaceholderForParent(const MREditWindow *parent, int direction);
void mrDropSidekickForParent(const MREditWindow *parent);
void mrDropActiveSidekick();

#endif
