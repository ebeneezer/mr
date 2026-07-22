#ifndef MRBENTOHEXEDITOR_HPP
#define MRBENTOHEXEDITOR_HPP

#include "../MRBentoBox/MRBentoBox.hpp"
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
	void refreshAfterDocumentCommit() noexcept;
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
	virtual void paneLayoutChanged() noexcept override;
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
	[[nodiscard]] std::size_t dataFirstRecord() const noexcept;
	[[nodiscard]] std::size_t dataFirstColumn() const noexcept;
	[[nodiscard]] int recordLength() const;
	[[nodiscard]] bool littleEndian() const noexcept;
	bool setDataViewport(std::size_t firstRecord, std::size_t firstColumn) noexcept;
	void toggleEndian();
	void toggleInsertMode();
	void selectByte(std::size_t offset) noexcept;
	void selectRecordColumn(std::size_t record, std::size_t column) noexcept;
	void moveByteCursor(std::ptrdiff_t delta) noexcept;
	bool replaceBytes(std::size_t offset, const std::string &bytes, std::size_t overwriteLength);
	void activateAdjacentInputPane(int direction) noexcept;
	void noteByteCursorChanged() noexcept;
	void refreshHexCursorTransition(std::size_t previousCursor) noexcept;
	void refreshHexFocusProjection() noexcept;
	bool synchronizeDataViewportToCursor() noexcept;

	std::size_t mByteCursor;
	std::size_t mCursorProjectionRevision;
	std::size_t mViewportCursorProjectionRevision;
	std::size_t mDataFirstRecord;
	std::size_t mDataFirstColumn;
	bool mLittleEndian;
	bool mApplyingHexEdit;
	MRHexPaneRole mActiveRole;
};

#endif
