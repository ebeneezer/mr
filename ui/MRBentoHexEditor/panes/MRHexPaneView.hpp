#ifndef MRHEXPANEVIEW_HPP
#define MRHEXPANEVIEW_HPP

#include "MRHexPaneProjection.hpp"

#define Uses_TView
#define Uses_TEvent
#define Uses_TPalette
#include <tvision/tv.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class MRBentoHexEditor;

class MRHexPaneView final : public TView {
  public:
	MRHexPaneView(const TRect &bounds, MRBentoHexEditor &editor, MRHexPaneRole role, std::size_t executionOwnerLocalId) noexcept;
	virtual ~MRHexPaneView() override;
	void cancelPendingEdit() noexcept;
	[[nodiscard]] bool commitPendingEdit();
	void requestProjection() noexcept;
	[[nodiscard]] bool applyProjectionResult(const mr::coprocessor::Result &result) noexcept;
	void refreshCursor(std::size_t previousOffset, std::size_t currentOffset, bool viewportChanged) noexcept;
	void refreshFocus() noexcept;
	[[nodiscard]] int verticalScrollBarMaximum() const;
	[[nodiscard]] int horizontalScrollBarMaximum() const;
	[[nodiscard]] int verticalScrollBarPageStep() const noexcept;
	[[nodiscard]] int horizontalScrollBarPageStep() const noexcept;
	[[nodiscard]] int verticalScrollBarValue() const noexcept;
	[[nodiscard]] int horizontalScrollBarValue() const noexcept;
	void setVerticalScrollBarValue(int value) noexcept;
	void setHorizontalScrollBarValue(int value) noexcept;
	void scrollByWheel(int wheel) noexcept;

	virtual void changeBounds(const TRect &bounds) override;
	virtual void draw() override;
	virtual void handleEvent(TEvent &event) override;
	virtual TPalette &getPalette() const override;

  private:
	void cancelEdit() noexcept;
	bool commitEdit();
	void beginEdit(std::size_t offset, char character);
	[[nodiscard]] bool editContainsByte(std::size_t offset) const noexcept;
	[[nodiscard]] std::string editTextForByte(std::size_t offset) const;
	[[nodiscard]] bool normalizeProjectionViewport(const MRTextBufferModel::ReadSnapshot &snapshot);
	[[nodiscard]] MRHexPaneProjectionKey projectionKey(const MRTextBufferModel::ReadSnapshot &snapshot) const;
	void reportStringProjectionState() noexcept;
	[[nodiscard]] bool dataRowForOffset(const MRHexPaneProjectionKey &layout, std::size_t offset, int &row) const noexcept;
	void drawDataRow(int row, const MRHexPaneProjectionKey &layout, const MRHexPaneProjectionPayload *projection, bool cursorValid,
	                 std::size_t cursorOffset);
	void drawInspectorRow(int row, const MRHexPaneProjectionKey &layout, const MRHexPaneProjectionPayload *projection);
	void projectNativeCursor(const MRHexPaneProjectionKey *layout, const MRHexPaneProjectionPayload *projection, bool cursorValid,
	                         std::size_t cursorOffset) noexcept;
	void redrawEditRows(std::size_t offset, std::size_t length) noexcept;

	MRBentoHexEditor &mEditor;
	MRHexPaneRole mRole;
	std::size_t mExecutionOwnerLocalId;
	bool mEditing;
	std::size_t mEditOffset;
	std::string mEditText;
	std::size_t mInspectorFirstLine;
	std::size_t mLastCursorProjectionRevision;
	bool mInputPaneWasActive;
	std::uint64_t mProjectionGenerationCounter;
	std::uint64_t mProjectionTaskId;
	bool mDesiredProjectionValid;
	bool mActiveProjectionValid;
	bool mStringStatusPending;
	MRHexPaneProjectionKey mDesiredProjectionKey;
	MRHexPaneProjectionKey mActiveProjectionKey;
	std::shared_ptr<const MRHexPaneProjectionPayload> mProjection;
	std::uint64_t mRenderedProjectionGeneration;
	std::size_t mRenderedCursorOffset;
	bool mRenderedCursorValid;
};

#endif
