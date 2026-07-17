#ifndef MRHEXPANEWINDOW_HPP
#define MRHEXPANEWINDOW_HPP

#include "../../MRBentoBox/MRBentoBox.hpp"
#include "../MRHexPaneRole.hpp"

class MRBentoHexEditor;
class MRHexPaneView;

class MRHexPaneWindow final : public MRPaneEditWindow {
  public:
	MRHexPaneWindow(const TRect &bounds, const char *title, int number, MRBentoHexEditor &editor, MRHexPaneRole role);

  protected:
	virtual void changeBounds(const TRect &bounds) override;
	virtual void draw() override;
	virtual void handleEvent(TEvent &event) override;
	virtual void cancelTransientInput() noexcept override;
	[[nodiscard]] virtual bool completeTransientInput() noexcept override;
	[[nodiscard]] virtual bool usesNativeEditorChrome() const noexcept override;
	[[nodiscard]] virtual bool ownsPaneWheelEvents() const noexcept override;

  private:
	void layoutHexScrollBars() noexcept;
	void synchronizeHexScrollBars() noexcept;
	void drawHexScrollBars() noexcept;
	[[nodiscard]] bool handlesHexScrollBar(const TEvent &event) const noexcept;
	void acceptHexScrollBarChange(TScrollBar *scrollBar) noexcept;

	MRHexPaneView *mHexView;
	MRHexPaneRole mRole;
	bool mSynchronizingScrollBars;
};

#endif
