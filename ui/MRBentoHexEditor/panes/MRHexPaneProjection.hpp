#ifndef MRHEXPANEPROJECTION_HPP
#define MRHEXPANEPROJECTION_HPP

#include "../../../coprocessor/MRCoprocessor.hpp"
#include "../../MRTextBufferModel.hpp"
#include "../MRHexInspector.hpp"
#include "../MRHexPaneRole.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

constexpr int kMrHexPaneOffsetWidth = 9;
constexpr std::size_t kMrHexInspectorLineCount = 26;

struct MRHexPaneDisplayLayout {
	std::size_t firstRecord;
	std::size_t firstColumn;
	std::size_t recordLength;
	int capacity;
	int fieldWidth;

	MRHexPaneDisplayLayout() noexcept : firstRecord(0), firstColumn(0), recordLength(1), capacity(0), fieldWidth(1) {
	}

	MRHexPaneDisplayLayout(std::size_t aFirstRecord, std::size_t aFirstColumn, std::size_t aRecordLength, int aCapacity, int aFieldWidth) noexcept
	    : firstRecord(aFirstRecord), firstColumn(aFirstColumn), recordLength(aRecordLength), capacity(aCapacity), fieldWidth(aFieldWidth) {
	}
};

struct MRHexPaneProjectionKey {
	std::size_t documentId;
	std::size_t documentVersion;
	std::size_t documentLength;
	MRHexPaneRole role;
	std::size_t cursorProjectionRevision;
	std::size_t cursorOffset;
	bool littleEndian;
	std::size_t firstRecord;
	std::size_t firstColumn;
	std::size_t recordLength;
	int width;
	int height;
	int capacity;
	int fieldWidth;

	MRHexPaneProjectionKey() noexcept;
	[[nodiscard]] bool exactlyMatches(const MRHexPaneProjectionKey &other) const noexcept;
	[[nodiscard]] bool computationMatches(const MRHexPaneProjectionKey &other) const noexcept;
	[[nodiscard]] bool drawGeometryMatches(const MRHexPaneProjectionKey &other) const noexcept;
};

struct MRHexPaneProjectedRow {
	char offsetText[24];

	MRHexPaneProjectedRow() noexcept;
};

struct MRHexPaneProjectedCell {
	char text[9];

	MRHexPaneProjectedCell() noexcept;
};

struct MRHexPaneProjectionPayload final : mr::coprocessor::Payload {
	std::uint64_t generation;
	MRHexPaneProjectionKey key;
	std::vector<MRHexPaneProjectedRow> rows;
	std::vector<MRHexPaneProjectedCell> cells;
	std::vector<MRHexInspectorLine> inspectorLines;
	bool hasVisibleString;

	MRHexPaneProjectionPayload() noexcept;
};

[[nodiscard]] int mrHexPaneFieldWidth(MRHexPaneRole role) noexcept;
[[nodiscard]] std::size_t mrHexPaneDataRecordCount(std::size_t length, std::size_t recordLength) noexcept;
[[nodiscard]] std::size_t mrHexPaneMaximumFirstRecord(std::size_t length, std::size_t recordLength, int height) noexcept;
[[nodiscard]] std::size_t mrHexPaneMaximumFirstColumn(std::size_t recordLength, int capacity) noexcept;
[[nodiscard]] int mrHexPaneScrollBarMaximum(std::size_t maximum) noexcept;
[[nodiscard]] MRHexPaneDisplayLayout mrHexPaneDisplayLayout(MRHexPaneRole role, std::size_t length, int width, int height, int configuredRecordLength,
                                                           std::size_t firstRecord, std::size_t firstColumn) noexcept;
[[nodiscard]] const char *mrHexPaneProjectionTaskLabel(MRHexPaneRole role) noexcept;
void mrHexPaneProjectionByteSpan(const MRHexPaneProjectionKey &key, std::size_t &startOffset, std::size_t &endOffset) noexcept;
[[nodiscard]] std::shared_ptr<const MRHexPaneProjectionPayload> mrBuildHexPaneProjection(const MRTextBufferModel::ReadSnapshot &snapshot,
                                                                                        const MRHexPaneProjectionKey &key, std::uint64_t generation,
                                                                                        const std::atomic_bool *cancelFlag);

#endif
