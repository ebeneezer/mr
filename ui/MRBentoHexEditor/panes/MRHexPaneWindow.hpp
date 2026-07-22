#ifndef MRHEXPANEWINDOW_HPP
#define MRHEXPANEWINDOW_HPP

#include "../../MRBentoBox/MRBentoBox.hpp"
#include "../MRHexPaneRole.hpp"

class MRBentoHexEditor;
class MRHexPaneView;

class MRHexPaneWindow final : public MRPaneEditWindow {
  public:
	MRHexPaneWindow(const TRect &bounds, const char *title, int number, MRBentoHexEditor &editor, MRHexPaneRole role);
	void requestHexProjection() noexcept;
	[[nodiscard]] int hexViewportRowCapacity() const noexcept;
	[[nodiscard]] int hexViewportColumnCapacity() const noexcept;
	[[nodiscard]] bool applyHexProjectionResult(const mr::coprocessor::Result &result) noexcept;
	void refreshHexCursor(std::size_t previousOffset, std::size_t currentOffset, bool viewportChanged) noexcept;
	void refreshHexFocus() noexcept;

  protected:
	virtual void changeBounds(const TRect &bounds) override;
	virtual void draw() override;
	virtual void handleEvent(TEvent &event) override;
	virtual void cancelTransientInput() noexcept override;
	[[nodiscard]] virtual bool completeTransientInput() noexcept override;
	[[nodiscard]] virtual bool usesNativeEditorChrome() const noexcept override;
	[[nodiscard]] virtual bool ownsPaneWheelEvents() const noexcept override;
	[[nodiscard]] virtual bool projectsPaneContentLocally() const noexcept override;

  private:
	void layoutHexScrollBars() noexcept;
	void synchronizeHexScrollBars() noexcept;
	void drawHexScrollBars() noexcept;
	[[nodiscard]] bool handlesHexScrollBar(const TEvent &event) const noexcept;
	void acceptHexScrollBarChange(TScrollBar *scrollBar) noexcept;

	MRBentoHexEditor &mEditor;
	MRHexPaneView *mHexView;
	MRHexPaneRole mRole;
	bool mSynchronizingScrollBars;
};

#endif
