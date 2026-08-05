#include "MRHexPaneProjection.hpp"

#include "../MRHexStrings.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

struct ProjectionRoleDescriptor {
	MRHexPaneRole role;
	int fieldWidth;
	const char *taskLabel;
};

constexpr ProjectionRoleDescriptor kProjectionRoleDescriptors[] = {
	{MRHexPaneRole::Hex, 3, "hex projection hexadecimal"},
	{MRHexPaneRole::Strings, 1, "hex projection strings"},
	{MRHexPaneRole::Inspector, 1, "hex projection inspector"},
	{MRHexPaneRole::Decimal, 4, "hex projection decimal"},
	{MRHexPaneRole::Binary, 9, "hex projection binary"},
	{MRHexPaneRole::Octal, 4, "hex projection octal"},
};

const ProjectionRoleDescriptor *projectionRoleDescriptor(MRHexPaneRole role) noexcept {
	for (const ProjectionRoleDescriptor &descriptor : kProjectionRoleDescriptors)
		if (descriptor.role == role) return &descriptor;
	return nullptr;
}

void formatByte(MRHexPaneRole role, unsigned char value, char *text, std::size_t textSize) noexcept {
	if (text == nullptr || textSize == 0) return;
	text[0] = '\0';
	switch (role) {
		case MRHexPaneRole::Hex:
			std::snprintf(text, textSize, "%02X", static_cast<unsigned>(value));
			break;
		case MRHexPaneRole::Decimal:
			std::snprintf(text, textSize, "%03u", static_cast<unsigned>(value));
			break;
		case MRHexPaneRole::Octal:
			std::snprintf(text, textSize, "%03o", static_cast<unsigned>(value));
			break;
		case MRHexPaneRole::Binary:
			if (textSize < 9) return;
			for (int bit = 7; bit >= 0; --bit) text[7 - bit] = (value & (1u << bit)) != 0 ? '1' : '0';
			text[8] = '\0';
			break;
		case MRHexPaneRole::Strings:
		case MRHexPaneRole::Inspector:
			break;
	}
}

std::size_t clampedRecordStart(const MRHexPaneProjectionKey &key, std::size_t row) noexcept {
	if (key.recordLength == 0 || row > std::numeric_limits<std::size_t>::max() - key.firstRecord) return std::numeric_limits<std::size_t>::max();
	const std::size_t record = key.firstRecord + row;

	if (record > std::numeric_limits<std::size_t>::max() / key.recordLength) return std::numeric_limits<std::size_t>::max();
	return record * key.recordLength;
}

} // namespace

MRHexPaneProjectionKey::MRHexPaneProjectionKey() noexcept
	: documentId(0), documentVersion(0), documentLength(0), role(MRHexPaneRole::Hex), cursorProjectionRevision(0), cursorOffset(0), littleEndian(true),
	  firstRecord(0), firstColumn(0), recordLength(1), width(0), height(0), capacity(0), fieldWidth(1) {
}

bool MRHexPaneProjectionKey::exactlyMatches(const MRHexPaneProjectionKey &other) const noexcept {
	return documentId == other.documentId && documentVersion == other.documentVersion && documentLength == other.documentLength && role == other.role &&
	       cursorProjectionRevision == other.cursorProjectionRevision && cursorOffset == other.cursorOffset && littleEndian == other.littleEndian &&
	       firstRecord == other.firstRecord && firstColumn == other.firstColumn && recordLength == other.recordLength && width == other.width && height == other.height &&
	       capacity == other.capacity && fieldWidth == other.fieldWidth;
}

bool MRHexPaneProjectionKey::computationMatches(const MRHexPaneProjectionKey &other) const noexcept {
	if (documentId != other.documentId || documentVersion != other.documentVersion || documentLength != other.documentLength || role != other.role) return false;
	if (role == MRHexPaneRole::Inspector)
		return cursorProjectionRevision == other.cursorProjectionRevision && cursorOffset == other.cursorOffset && littleEndian == other.littleEndian;
	return firstRecord == other.firstRecord && firstColumn == other.firstColumn && recordLength == other.recordLength && width == other.width && height == other.height &&
	       capacity == other.capacity && fieldWidth == other.fieldWidth;
}

MRHexPaneProjectedRow::MRHexPaneProjectedRow() noexcept : offsetText{0} {
}

MRHexPaneProjectedCell::MRHexPaneProjectedCell() noexcept : text{0} {
}

MRHexPaneProjectionPayload::MRHexPaneProjectionPayload() noexcept
	: generation(0), key(), rows(), cells(), inspectorLines(), hasVisibleString(false) {
}

int mrHexPaneFieldWidth(MRHexPaneRole role) noexcept {
	const ProjectionRoleDescriptor *descriptor = projectionRoleDescriptor(role);

	return descriptor != nullptr ? descriptor->fieldWidth : 1;
}

std::size_t mrHexPaneDataRecordCount(std::size_t length, std::size_t recordLength) noexcept {
	const std::size_t normalizedRecordLength = std::max<std::size_t>(1, recordLength);

	return length == 0 ? 0 : 1 + (length - 1) / normalizedRecordLength;
}

std::size_t mrHexPaneMaximumFirstRecord(std::size_t length, std::size_t recordLength, int) noexcept {
	return length / std::max<std::size_t>(1, recordLength);
}

std::size_t mrHexPaneMaximumFirstColumn(std::size_t recordLength, int) noexcept {
	return recordLength == 0 ? 0 : recordLength - 1;
}

int mrHexPaneScrollBarMaximum(std::size_t maximum) noexcept {
	return maximum > static_cast<std::size_t>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max() : static_cast<int>(maximum);
}

MRHexPaneDisplayLayout mrHexPaneDisplayLayout(MRHexPaneRole role, std::size_t length, int width, int height, int configuredRecordLength,
	                                           std::size_t firstRecord, std::size_t firstColumn) noexcept {
	const std::size_t recordLength = static_cast<std::size_t>(std::max(1, configuredRecordLength));
	const int widthPerField = mrHexPaneFieldWidth(role);
	const int capacity = role == MRHexPaneRole::Inspector ? 0 : std::max(0, (width - kMrHexPaneOffsetWidth) / widthPerField);
	const std::size_t maximumRecord = mrHexPaneMaximumFirstRecord(length, recordLength, height);
	const std::size_t maximumColumn = mrHexPaneMaximumFirstColumn(recordLength, capacity);

	return MRHexPaneDisplayLayout(std::min(firstRecord, maximumRecord), std::min(firstColumn, maximumColumn), recordLength, capacity, widthPerField);
}

const char *mrHexPaneProjectionTaskLabel(MRHexPaneRole role) noexcept {
	const ProjectionRoleDescriptor *descriptor = projectionRoleDescriptor(role);

	return descriptor != nullptr ? descriptor->taskLabel : "hex projection";
}

void mrHexPaneProjectionByteSpan(const MRHexPaneProjectionKey &key, std::size_t &startOffset, std::size_t &endOffset) noexcept {
	if (key.role == MRHexPaneRole::Inspector) {
		startOffset = key.documentLength == 0 ? 0 : std::min(key.cursorOffset, key.documentLength - 1);
		endOffset = std::min(key.documentLength, startOffset + std::min<std::size_t>(16, key.documentLength - startOffset));
		return;
	}
	if (key.recordLength == 0) {
		startOffset = 0;
		endOffset = 0;
		return;
	}

	const std::size_t dataRecordCount = mrHexPaneDataRecordCount(key.documentLength, key.recordLength);
	if (key.firstRecord >= dataRecordCount) {
		startOffset = key.documentLength;
		endOffset = key.documentLength;
		return;
	}
	const std::size_t firstRecordOffset = clampedRecordStart(key, 0);
	startOffset = firstRecordOffset > key.documentLength || key.firstColumn > key.documentLength - firstRecordOffset ? key.documentLength : firstRecordOffset + key.firstColumn;
	const std::size_t rowCount = static_cast<std::size_t>(std::max(0, key.height));
	if (rowCount == 0) {
		endOffset = startOffset;
		return;
	}
	if (rowCount >= dataRecordCount - key.firstRecord) {
		endOffset = key.documentLength;
	} else {
		const std::size_t endRecord = key.firstRecord + rowCount;

		endOffset = endRecord * key.recordLength;
	}
	if (endOffset < startOffset) endOffset = startOffset;
	if (key.role == MRHexPaneRole::Strings && endOffset > startOffset) {
		startOffset = startOffset > kMrHexMaximumStringProbe ? startOffset - kMrHexMaximumStringProbe : 0;
		endOffset += std::min(kMrHexMaximumStringProbe, key.documentLength - endOffset);
	}
}

std::shared_ptr<const MRHexPaneProjectionPayload> mrBuildHexPaneProjection(const MRTextBufferModel::ReadSnapshot &snapshot,
	                                                                       const MRHexPaneProjectionKey &key, std::uint64_t generation,
	                                                                       const std::atomic_bool *cancelFlag) {
	std::shared_ptr<MRHexPaneProjectionPayload> projection = std::make_shared<MRHexPaneProjectionPayload>();

	projection->generation = generation;
	projection->key = key;
	if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire)) return std::shared_ptr<const MRHexPaneProjectionPayload>();
	if (key.role == MRHexPaneRole::Inspector) {
		const std::size_t inspectedOffset = key.documentLength == 0 ? 0 : std::min(key.cursorOffset, key.documentLength - 1);

		mrBuildHexInspectorLines(snapshot, inspectedOffset, key.littleEndian, projection->inspectorLines);
		if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire)) return std::shared_ptr<const MRHexPaneProjectionPayload>();
		return projection;
	}

	const std::size_t rowCount = static_cast<std::size_t>(std::max(0, key.height));
	const std::size_t columnCount = static_cast<std::size_t>(std::max(0, key.capacity));
	const std::size_t dataRecordCount = mrHexPaneDataRecordCount(key.documentLength, key.recordLength);
	const std::size_t lastSelectableRecord = key.documentLength / key.recordLength;

	if (columnCount != 0 && rowCount > std::numeric_limits<std::size_t>::max() / columnCount)
		return std::shared_ptr<const MRHexPaneProjectionPayload>();
	projection->rows.resize(rowCount);
	projection->cells.resize(rowCount * columnCount);
	for (std::size_t row = 0; row < rowCount; ++row) {
		if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire)) return std::shared_ptr<const MRHexPaneProjectionPayload>();
		if (row > std::numeric_limits<std::size_t>::max() - key.firstRecord) continue;
		const std::size_t record = key.firstRecord + row;
		if (record > lastSelectableRecord) continue;
		if (record == dataRecordCount) {
			std::snprintf(projection->rows[row].offsetText, sizeof(projection->rows[row].offsetText), "%08zx", key.documentLength);
			continue;
		}
		if (record > dataRecordCount) continue;
		const std::size_t recordStart = clampedRecordStart(key, row);
		const std::size_t displayOffset = recordStart == std::numeric_limits<std::size_t>::max() || key.firstColumn > std::numeric_limits<std::size_t>::max() - recordStart
		                                      ? std::numeric_limits<std::size_t>::max()
		                                      : recordStart + key.firstColumn;

		std::snprintf(projection->rows[row].offsetText, sizeof(projection->rows[row].offsetText), "%08zx", displayOffset);
		for (std::size_t column = 0; column < columnCount; ++column) {
			if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_acquire)) return std::shared_ptr<const MRHexPaneProjectionPayload>();
			MRHexPaneProjectedCell &cell = projection->cells[row * columnCount + column];
			const std::size_t recordColumn = column > std::numeric_limits<std::size_t>::max() - key.firstColumn
			                                     ? std::numeric_limits<std::size_t>::max()
			                                     : key.firstColumn + column;
			const bool offsetValid = recordStart < key.documentLength && recordColumn < key.recordLength && recordColumn < key.documentLength - recordStart;
			const std::size_t offset = offsetValid ? recordStart + recordColumn : key.documentLength;

			if (!offsetValid || offset >= key.documentLength) continue;
			if (key.role == MRHexPaneRole::Strings) {
				const MRHexStringCell stringCell = mrHexStringCellAt(snapshot, offset);

				std::memcpy(cell.text, stringCell.text, sizeof(stringCell.text));
				if (stringCell.kind != MRHexStringSpanKind::Hidden) projection->hasVisibleString = true;
			} else
				formatByte(key.role, static_cast<unsigned char>(snapshot.charAt(offset)), cell.text, sizeof(cell.text));
		}
	}
	return projection;
}
