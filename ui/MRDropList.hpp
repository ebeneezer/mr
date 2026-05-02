#ifndef MRDROPLIST_HPP
#define MRDROPLIST_HPP

#define Uses_TRect
#include <tvision/tv.h>

#include <string>
#include <vector>

class MRColumnListView;
class TGroup;
class TInputLine;
class TView;

class MRDropList {
  public:
	MRDropList() = default;
	~MRDropList();

	TView *createButton(TGroup &owner, const TRect &bounds, TInputLine *link, TView *relay, ushort command, bool triggerDownKey);
	void toggle(TGroup &owner, const TRect &anchor, const std::vector<std::string> &values, const std::string &currentValue, TView *relay, ushort acceptCommand, short maxVisibleRows = 0);
	void hide();
	[[nodiscard]] bool visible() const noexcept;
	[[nodiscard]] bool acceptSelection(std::string &selectedValue);
	[[nodiscard]] bool containsPoint(TPoint where) const noexcept;
	[[nodiscard]] bool buttonContainsPoint(TPoint where) const noexcept;

  private:
	void show(TGroup &owner, const TRect &anchor, const std::vector<std::string> &values, const std::string &currentValue, TView *relay, ushort acceptCommand, short maxVisibleRows);

	MRColumnListView *listView = nullptr;
	TGroup *listOwner = nullptr;
	TView *buttonView = nullptr;
	std::vector<std::string> itemValues;
};

#endif
