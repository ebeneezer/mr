#ifndef MRDROPLIST_HPP
#define MRDROPLIST_HPP

#define Uses_TRect
#define Uses_TEvent
#define Uses_TInputLine
#include <tvision/tv.h>

#include <string>
#include <vector>

class MRColumnListView;
class TGroup;
class TView;
class MRScrollableDialog;

class MRDropList {
  public:
	MRDropList() = default;
	~MRDropList();

	TView *createButton(TGroup &owner, const TRect &bounds, TInputLine *link, TView *relay, ushort command, bool triggerDownKey);
	void toggle(TGroup &owner, const TRect &anchor, const std::vector<std::string> &values, const std::string &currentValue, TView *relay, ushort acceptCommand, short maxVisibleRows = 0);
	void hide();
	[[nodiscard]] bool handleLinkedInputEvent(TEvent &event, TGroup &owner, const TRect &anchor, const std::vector<std::string> &values, TInputLine *link, TView *relay, ushort acceptCommand, short maxVisibleRows = 0);
	[[nodiscard]] bool handleOpenListEvent(TEvent &event, bool hideOnOutsideMouseDown = true);
	[[nodiscard]] bool visible() const noexcept;
	[[nodiscard]] bool acceptSelection(std::string &selectedValue);
	[[nodiscard]] bool selectedValue(std::string &selectedValue) const;
	[[nodiscard]] short selectedIndex() const;
	void focusIndex(short index);
	[[nodiscard]] bool containsPoint(TPoint where) const noexcept;
	[[nodiscard]] bool buttonContainsPoint(TPoint where) const noexcept;

  private:
	void show(TGroup &owner, const TRect &anchor, const std::vector<std::string> &values, const std::string &currentValue, TView *relay, ushort acceptCommand, short maxVisibleRows);
	void showValues(TGroup &owner, const TRect &anchor, const std::vector<std::string> &sourceValues, const std::vector<std::string> &visibleValues, const std::string &currentValue, TView *relay, ushort acceptCommand, short maxVisibleRows);
	[[nodiscard]] bool updateLinkedInputFromKey(TEvent &event, TInputLine *link, std::string &inputValue);
	[[nodiscard]] bool showFilteredValues(const std::string &inputValue, bool restoreLinkFocusOnNoMatch);
	[[nodiscard]] bool handleSpeedSearchText(const std::string &text);
	[[nodiscard]] short firstMatchingIndex(const std::string &prefix) const;
	void dispatchAcceptCommand();

	MRColumnListView *listView = nullptr;
	TGroup *listOwner = nullptr;
	MRScrollableDialog *scrollOwner = nullptr;
	TView *buttonView = nullptr;
	TInputLine *linkedInput = nullptr;
	TGroup *activeOwner = nullptr;
	TView *activeRelay = nullptr;
	TRect activeAnchor;
	ushort activeAcceptCommand = 0;
	short activeMaxVisibleRows = 0;
	int linkedInputCursor = -1;
	std::vector<std::string> sourceValues;
	std::vector<std::string> itemValues;
	std::string speedSearchPrefix;
};

class MRStringChoiceField : public TInputLine {
  public:
	MRStringChoiceField(const TRect &bounds, int maxLen) noexcept;

	void setChoices(const std::vector<std::string> &values);
	[[nodiscard]] const std::vector<std::string> &choices() const noexcept;
	void setValue(const std::string &text);
	[[nodiscard]] std::string value() const;

	TView *createDropListButton(TGroup &owner, const TRect &bounds, TView *relay, ushort command, bool triggerDownKey);
	void toggleDropList(TGroup &owner, const TRect &anchor, TView *relay, ushort acceptCommand, short maxVisibleRows = 0);
	void hideDropList();
	[[nodiscard]] bool handleDropListEvent(TEvent &event, bool hideOnOutsideMouseDown = true);
	[[nodiscard]] bool dropListVisible() const noexcept;
	[[nodiscard]] bool dropListContainsPoint(TPoint where) const noexcept;
	[[nodiscard]] bool acceptDropListSelection();

	void handleEvent(TEvent &event) override;

  private:
	[[nodiscard]] bool shouldAutocomplete(const TEvent &event) const;
	void autocompleteCurrentPrefix();
	void dispatchOpenCommand();

	std::vector<std::string> itemValues;
	MRDropList dropList;
	TView *commandRelay = nullptr;
	ushort openCommand = 0;
};

#endif
