#ifndef MRCOLUMNLISTVIEW_HPP
#define MRCOLUMNLISTVIEW_HPP

#define Uses_TListBox
#define Uses_TScrollBar
#define Uses_TEvent
#define Uses_TDrawBuffer
#include <tvision/tv.h>

#include <string>
#include <vector>

class MRColumnListView : public TListBox {
  public:
	using Row = std::vector<std::string>;
	enum class RowStyle : unsigned char {
		Normal,
		OutlineHeader,
		OutlineLevel0,
		OutlineLevel1,
		OutlineLevel2,
		OutlineLevel3,
		OutlineLevel4,
		OutlineLevel5,
		OutlineLevel6,
		OutlineLevel7,
		OutlineLevel8,
		OutlineLevel9
	};

	MRColumnListView(const TRect &bounds, TScrollBar *scrollBar, TView *relay = nullptr, ushort selectionCommand = 0, ushort activationCommand = 0, bool dropListColors = false) noexcept;
	MRColumnListView(const TRect &bounds, TScrollBar *verticalScrollBar, TScrollBar *horizontalScrollBar, TView *relay, ushort selectionCommand, ushort activationCommand, bool dropListColors = false) noexcept;

	void setRows(const std::vector<Row> &rows, short selection = 0);
	void setRowStyles(const std::vector<RowStyle> &styles);
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
	void draw() override;

  private:
	void dispatchSelectionChanged();
	void dispatchActivation();
	[[nodiscard]] std::string buildDisplayRow(const Row &row, const std::vector<std::size_t> &widths) const;
	void configureHorizontalScrollBar(std::size_t displayWidth);
	TColorAttr colorForRow(short row);

	std::vector<Row> rowValues;
	std::vector<std::string> displayRowValues;
	std::vector<RowStyle> rowStyles;
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
