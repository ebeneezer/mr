#ifndef MRCOLUMNLISTVIEW_HPP
#define MRCOLUMNLISTVIEW_HPP

#define Uses_TListBox
#define Uses_TScrollBar
#include <tvision/tv.h>

#include <string>
#include <vector>

class MRColumnListView : public TListBox {
  public:
	using Row = std::vector<std::string>;

	MRColumnListView(const TRect &bounds, TScrollBar *scrollBar, TView *relay = nullptr,
	                 ushort selectionCommand = 0, ushort activationCommand = 0) noexcept;

	void setRows(const std::vector<Row> &rows, short selection = 0);
	[[nodiscard]] short selectedIndex() const;
	[[nodiscard]] const std::vector<Row> &rows() const noexcept {
		return rowValues;
	}

	void focusItemNum(short item) override;
	void selectItem(short item) override;

 private:
	void dispatchSelectionChanged();
	void dispatchActivation();
	[[nodiscard]] std::string buildDisplayRow(const Row &row,
	                                          const std::vector<std::size_t> &widths) const;

	std::vector<Row> rowValues;
	TView *relayTarget = nullptr;
	ushort relayCommand = 0;
	ushort activationCommand = 0;
};

#endif
