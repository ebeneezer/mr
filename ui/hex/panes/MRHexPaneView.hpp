#ifndef MRHEXPANEVIEW_HPP
#define MRHEXPANEVIEW_HPP

#include "../MRHexPaneRole.hpp"
#include "../MRHexInspector.hpp"

#define Uses_TView
#define Uses_TEvent
#define Uses_TPalette
#include <tvision/tv.h>

#include <cstddef>
#include <string>
#include <vector>

class MRBentoHexEditor;

class MRHexPaneView final : public TView {
  public:
	MRHexPaneView(const TRect &bounds, MRBentoHexEditor &editor, MRHexPaneRole role) noexcept;
	void cancelPendingEdit() noexcept;
	[[nodiscard]] bool commitPendingEdit();
	[[nodiscard]] int verticalScrollBarMaximum() const;
	[[nodiscard]] int horizontalScrollBarMaximum() const;
	[[nodiscard]] int verticalScrollBarPageStep() const noexcept;
	[[nodiscard]] int horizontalScrollBarPageStep() const noexcept;
	[[nodiscard]] int verticalScrollBarValue() const noexcept;
	[[nodiscard]] int horizontalScrollBarValue() const noexcept;
	void setVerticalScrollBarValue(int value) noexcept;
	void setHorizontalScrollBarValue(int value) noexcept;
	void scrollByWheel(int wheel) noexcept;

	virtual void draw() override;
	virtual void handleEvent(TEvent &event) override;
	virtual TPalette &getPalette() const override;

  private:
	void cancelEdit() noexcept;
	bool commitEdit();
	void beginEdit(std::size_t offset, char character);
	[[nodiscard]] bool editContainsByte(std::size_t offset) const noexcept;
	[[nodiscard]] std::string editTextForByte(std::size_t offset) const;

	MRBentoHexEditor &mEditor;
	MRHexPaneRole mRole;
	bool mEditing;
	std::size_t mEditOffset;
	std::string mEditText;
	std::size_t mFirstRecord;
	std::size_t mFirstColumn;
	std::size_t mInspectorFirstLine;
	std::size_t mLastCursorProjectionRevision;
	bool mInputPaneWasActive;
	std::vector<MRHexInspectorLine> mInspectorLines;
};

#endif
