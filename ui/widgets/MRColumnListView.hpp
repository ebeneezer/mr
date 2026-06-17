#ifndef MRCOLUMNLISTVIEW_HPP
#define MRCOLUMNLISTVIEW_HPP

#define Uses_TListBox
#define Uses_TScrollBar
#define Uses_TEvent
#include <tvision/tv.h>

#include <string>
#include <vector>

class MRColumnListView : public TListBox {
  public:
	using Row = std::vector<std::string>;

	MRColumnListView(const TRect &bounds, TScrollBar *scrollBar, TView *relay = nullptr, ushort selectionCommand = 0, ushort activationCommand = 0, bool dropListColors = false) noexcept;
	MRColumnListView(const TRect &bounds, TScrollBar *verticalScrollBar, TScrollBar *horizontalScrollBar, TView *relay, ushort selectionCommand, ushort activationCommand, bool dropListColors = false) noexcept;

	void setRows(const std::vector<Row> &rows, short selection = 0);
	void setActivateOnSingleClick(bool enabled) noexcept;
	void setContextMenuColors(bool enabled) noexcept;
	[[nodiscard]] short selectedIndex() const;
	[[nodiscard]] bool handleWheel(TEvent &event);
	[[nodiscard]] const std::vector<Row> &rows() const noexcept {
		return rowValues;
	}

	void focusItemNum(short item) override;
	void selectItem(short item) override;
	void handleEvent(TEvent &event) override;
	TColorAttr mapColor(uchar index) override;
	void changeBounds(const TRect &bounds) override;

  private:
	void dispatchSelectionChanged();
	void dispatchActivation();
	[[nodiscard]] std::string buildDisplayRow(const Row &row, const std::vector<std::size_t> &widths) const;
	void configureHorizontalScrollBar(std::size_t displayWidth);

	std::vector<Row> rowValues;
	TScrollBar *optionalHorizontalScrollBar = nullptr;
	std::size_t maxDisplayRowWidth = 0;
	TView *relayTarget = nullptr;
	ushort relayCommand = 0;
	ushort activationCommand = 0;
	bool useDropListColors = false;
	bool useContextMenuColors = false;
	bool activateOnSingleClick = false;
};

#endif
