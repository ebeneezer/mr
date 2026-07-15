#ifndef MRBENTOHEXEDITOR_HPP
#define MRBENTOHEXEDITOR_HPP

#include "../MRBentoBox.hpp"
#include "MRHexPaneRole.hpp"

#include <cstddef>
#include <string>

class MRHexPaneView;

class MRBentoHexEditor final : public MRBentoBox {
	friend class MRHexPaneView;

  public:
	MRBentoHexEditor(const TRect &bounds, const char *title, int number);
	[[nodiscard]] static bool matchesWorkspaceSnapshot(const MRBentoWorkspaceSnapshot &snapshot) noexcept;
	void synchronizePaneDocumentState();
	void synchronizeByteCursorFromDocument() noexcept;
	void refreshHexProjection() noexcept;

  protected:
	[[nodiscard]] virtual MRPaneEditWindow *createPaneWindow(const TRect &bounds, const char *title, int number, const MRBentoPaneSpec &spec) override;
	[[nodiscard]] virtual bool primaryPaneUsesDedicatedWindow() const noexcept override;
	[[nodiscard]] virtual bool acceptsPaneRole(MRBentoPaneRole role) const noexcept override;
	[[nodiscard]] virtual const char *titleForPaneRole(MRBentoPaneRole role) const noexcept override;
	[[nodiscard]] virtual MRBentoPaneSpec paneSpecForRole(MRBentoPaneRole role) const noexcept override;
	[[nodiscard]] virtual bool paneCloseActionEnabled() const noexcept override;
	[[nodiscard]] virtual bool paneMaximizeActionEnabled() const noexcept override;
	[[nodiscard]] virtual bool projectPaneDividerPosition(int nodeIndex, int position) noexcept override;
	virtual void handleEvent(TEvent &event) override;
	virtual TColorAttr mapColor(uchar index) override;
	virtual MREditWindow *editorCommandTarget() noexcept override;
	virtual const MREditWindow *editorCommandTarget() const noexcept override;
	virtual void activePaneRoleChanged(MRBentoPaneRole role) noexcept override;

  private:
	[[nodiscard]] MRTextBufferModel::ReadSnapshot byteSnapshot() const;
	[[nodiscard]] std::size_t byteCursor() const noexcept;
	[[nodiscard]] std::size_t cursorProjectionRevision() const noexcept;
	[[nodiscard]] bool inputPaneIsActive(MRHexPaneRole role) const noexcept;
	[[nodiscard]] int recordLength() const;
	[[nodiscard]] bool littleEndian() const noexcept;
	void toggleEndian();
	void toggleInsertMode();
	void selectByte(std::size_t offset) noexcept;
	void moveByteCursor(std::ptrdiff_t delta) noexcept;
	bool replaceBytes(std::size_t offset, const std::string &bytes, std::size_t overwriteLength);
	void activateNextInputPane() noexcept;
	void noteByteCursorChanged() noexcept;

	std::size_t mByteCursor;
	std::size_t mCursorProjectionRevision;
	bool mLittleEndian;
	MRHexPaneRole mActiveRole;
};

#endif
