#define Uses_TInputLine
#include <tvision/tv.h>

#include "MRHexPaneView.hpp"

#include "../MRBentoHexEditor.hpp"

#include "../../MRMessageLineController.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

struct NumericFormat {
	MRHexPaneRole role;
	int base;
	int maximumLength;
};

constexpr NumericFormat kNumericFormats[] = {
	{MRHexPaneRole::Hex, 16, 2},
	{MRHexPaneRole::Decimal, 10, 3},
	{MRHexPaneRole::Binary, 2, 8},
	{MRHexPaneRole::Octal, 8, 3},
};

const NumericFormat *numericFormat(MRHexPaneRole role) noexcept {
	for (const NumericFormat &format : kNumericFormats)
		if (format.role == role) return &format;
	return nullptr;
}

void hexEndPosition(std::size_t length, std::size_t recordLength, std::size_t &record, std::size_t &column) noexcept {
	const std::size_t normalizedRecordLength = std::max<std::size_t>(1, recordLength);

	record = length / normalizedRecordLength;
	column = length % normalizedRecordLength;
}

bool parseGotoOffset(const char *text, std::size_t &offset) {
	const char *first = text != nullptr ? text : "";
	const char *last = first + std::strlen(first);
	int base = 10;
	char *end = nullptr;
	unsigned long long value = 0;

	while (first != last && std::isspace(static_cast<unsigned char>(*first)) != 0) ++first;
	while (last != first && std::isspace(static_cast<unsigned char>(last[-1])) != 0) --last;
	if (last - first >= 2 && first[0] == '0' && (first[1] == 'x' || first[1] == 'X')) {
		base = 16;
		first += 2;
	} else if (last - first >= 2 && first[0] == '0' && (first[1] == 'o' || first[1] == 'O')) {
		base = 8;
		first += 2;
	}
	if (first == last) return false;
	std::string valueText(first, static_cast<std::size_t>(last - first));
	errno = 0;
	value = std::strtoull(valueText.c_str(), &end, base);
	if (errno == ERANGE || end == valueText.c_str() || *end != '\0' || value > std::numeric_limits<std::size_t>::max()) return false;
	offset = static_cast<std::size_t>(value);
	return true;
}

bool promptGotoOffset(std::size_t initialOffset, std::size_t &offset) {
	char text[32] = {0};

	std::snprintf(text, sizeof(text), "0x%zx", initialOffset);
	if (inputBox("HEX GOTO", "Offset (decimal, 0x, 0o)", text, static_cast<uchar>(sizeof(text) - 1)) == cmCancel) return false;
	if (parseGotoOffset(text, offset)) return true;
	messageBox(mfInformation | mfOKButton, "Offset must be decimal, 0x hexadecimal or 0o octal.");
	return false;
}

bool numericTextIsValid(const std::string &text, int base) noexcept {
	char *end = nullptr;
	const unsigned long value = std::strtoul(text.c_str(), &end, base);

	return !text.empty() && end != text.c_str() && *end == '\0' && value <= std::numeric_limits<unsigned char>::max();
}

bool acceptsNumericCharacter(char character, int base) noexcept {
	const unsigned char byte = static_cast<unsigned char>(character);
	const int value = std::isdigit(byte) != 0 ? character - '0' : (std::isalpha(byte) != 0 ? std::toupper(byte) - 'A' + 10 : base);

	return value >= 0 && value < base;
}

void writeInspectorLine(TDrawBuffer &buffer, int width, const char *label, const std::string &value, TAttrPair color) {
	const int labelWidth = std::min(18, std::max(0, width - 1));

	buffer.moveChar(0, ' ', color, static_cast<ushort>(std::max(0, width)));
	buffer.moveStr(0, label, color, static_cast<ushort>(labelWidth));
	if (width > labelWidth + 1) buffer.moveStr(static_cast<ushort>(labelWidth + 1), value.c_str(), color, static_cast<ushort>(width - labelWidth - 1));
}

bool projectionViewportMatches(const MRHexPaneProjectionKey &first, const MRHexPaneProjectionKey &second) noexcept {
	if (first.role != second.role || first.width != second.width || first.height != second.height || first.firstRecord != second.firstRecord) return false;
	if (first.role == MRHexPaneRole::Inspector) return true;
	return first.firstColumn == second.firstColumn && first.recordLength == second.recordLength && first.capacity == second.capacity &&
	       first.fieldWidth == second.fieldWidth;
}

bool projectedDataRowMatches(const MRHexPaneProjectionPayload &first, const MRHexPaneProjectionPayload &second, std::size_t row) noexcept {
	if (row >= first.rows.size() || row >= second.rows.size() || first.key.capacity < 0 || second.key.capacity != first.key.capacity) return false;
	if (std::strcmp(first.rows[row].offsetText, second.rows[row].offsetText) != 0) return false;
	const std::size_t capacity = static_cast<std::size_t>(first.key.capacity);
	if (capacity != 0 && row > std::numeric_limits<std::size_t>::max() / capacity) return false;
	const std::size_t firstCell = row * capacity;
	if (firstCell > first.cells.size() || capacity > first.cells.size() - firstCell || firstCell > second.cells.size() ||
	    capacity > second.cells.size() - firstCell)
		return false;
	for (std::size_t column = 0; column < capacity; ++column)
		if (std::memcmp(first.cells[firstCell + column].text, second.cells[firstCell + column].text,
		                sizeof(first.cells[firstCell + column].text)) != 0)
			return false;
	return true;
}

bool projectedInspectorRowMatches(const MRHexPaneProjectionPayload &first, const MRHexPaneProjectionPayload &second, std::size_t row) noexcept {
	if (row > std::numeric_limits<std::size_t>::max() - first.key.firstRecord || row > std::numeric_limits<std::size_t>::max() - second.key.firstRecord)
		return false;
	const std::size_t firstLine = first.key.firstRecord + row;
	const std::size_t secondLine = second.key.firstRecord + row;
	const bool firstValid = firstLine < first.inspectorLines.size();
	const bool secondValid = secondLine < second.inspectorLines.size();

	if (firstValid != secondValid) return false;
	if (!firstValid) return true;
	const char *firstLabel = first.inspectorLines[firstLine].label != nullptr ? first.inspectorLines[firstLine].label : "";
	const char *secondLabel = second.inspectorLines[secondLine].label != nullptr ? second.inspectorLines[secondLine].label : "";
	return std::strcmp(firstLabel, secondLabel) == 0 && first.inspectorLines[firstLine].value == second.inspectorLines[secondLine].value;
}

} // namespace

MRHexPaneView::MRHexPaneView(const TRect &bounds, MRBentoHexEditor &editor, MRHexPaneRole role, std::size_t executionOwnerLocalId) noexcept
	: TView(bounds), mEditor(editor), mRole(role), mExecutionOwnerLocalId(executionOwnerLocalId), mEditing(false), mEditOffset(0), mEditText(),
	  mInspectorFirstLine(0), mLastCursorProjectionRevision(0), mInputPaneWasActive(false), mProjectionGenerationCounter(1), mProjectionTaskId(0),
	  mDesiredProjectionValid(false), mActiveProjectionValid(false), mStringStatusPending(false),
	  mDesiredProjectionKey(), mActiveProjectionKey(), mProjection(), mRenderedProjectionGeneration(0), mRenderedCursorOffset(0),
	  mRenderedCursorValid(false) {
	options |= ofSelectable;
}

MRHexPaneView::~MRHexPaneView() {
	if (mProjectionTaskId != 0) static_cast<void>(mr::coprocessor::globalCoprocessor().cancelTask(mProjectionTaskId));
}

void MRHexPaneView::cancelPendingEdit() noexcept {
	const std::size_t editOffset = mEditOffset;
	const std::size_t editLength = std::max<std::size_t>(1, mEditText.size());
	const bool wasEditing = mEditing;

	cancelEdit();
	if (wasEditing) redrawEditRows(editOffset, editLength);
}

bool MRHexPaneView::commitPendingEdit() {
	if (!mEditing) return true;
	const std::size_t editOffset = mEditOffset;

	if (!commitEdit()) return false;
	mEditor.selectByte(editOffset);
	return true;
}

bool MRHexPaneView::normalizeProjectionViewport(const MRTextBufferModel::ReadSnapshot &snapshot) {
	const std::size_t length = snapshot.length();
	const std::size_t cursor = std::min(mEditor.byteCursor(), length);
	const std::size_t cursorProjectionRevision = mEditor.cursorProjectionRevision();
	const bool inputPaneActive = mEditor.inputPaneIsActive(mRole);
	const bool inputPaneBecameActive = inputPaneActive && !mInputPaneWasActive;

	if (mRole == MRHexPaneRole::Strings) {
		if (inputPaneBecameActive) mStringStatusPending = true;
		else if (!inputPaneActive)
			mStringStatusPending = false;
	}
	mInputPaneWasActive = inputPaneActive;
	if (mRole == MRHexPaneRole::Inspector) {
		const std::size_t visibleLines = static_cast<std::size_t>(std::max(1, size.y));
		const std::size_t maximumFirstLine = kMrHexInspectorLineCount > visibleLines ? kMrHexInspectorLineCount - visibleLines : 0;

		mInspectorFirstLine = std::min(mInspectorFirstLine, maximumFirstLine);
		return false;
	}

	MRHexPaneDisplayLayout layout = mrHexPaneDisplayLayout(mRole, length, size.x, size.y, mEditor.recordLength(), mEditor.dataFirstRecord(), mEditor.dataFirstColumn());
	const bool cursorProjectionChanged = mLastCursorProjectionRevision != cursorProjectionRevision;
	std::size_t firstRecord = layout.firstRecord;
	std::size_t firstColumn = layout.firstColumn;

	if (layout.capacity > 0 && inputPaneActive && cursorProjectionChanged) {
		std::size_t endRecord = 0;
		std::size_t endColumn = 0;

		hexEndPosition(length, layout.recordLength, endRecord, endColumn);
		const std::size_t cursorRecord = cursor == length ? endRecord : cursor / layout.recordLength;
		const std::size_t cursorColumn = cursor == length ? endColumn : cursor % layout.recordLength;
		const std::size_t visibleRows = static_cast<std::size_t>(std::max(1, size.y));
		const std::size_t visibleColumns = static_cast<std::size_t>(layout.capacity);

		if (cursorRecord < layout.firstRecord) firstRecord = cursorRecord;
		else if (cursorRecord - layout.firstRecord >= visibleRows)
			firstRecord = cursorRecord - visibleRows + 1;
		if (cursorColumn < layout.firstColumn) firstColumn = cursorColumn;
		else if (cursorColumn - layout.firstColumn >= visibleColumns)
			firstColumn = cursorColumn - visibleColumns + 1;
	}
	if (layout.capacity > 0) mLastCursorProjectionRevision = cursorProjectionRevision;
	return mEditor.setDataViewport(firstRecord, firstColumn);
}

MRHexPaneProjectionKey MRHexPaneView::projectionKey(const MRTextBufferModel::ReadSnapshot &snapshot) const {
	MRHexPaneProjectionKey key;
	const MRHexPaneDisplayLayout layout = mrHexPaneDisplayLayout(mRole, snapshot.length(), size.x, size.y, mEditor.recordLength(), mEditor.dataFirstRecord(), mEditor.dataFirstColumn());

	key.documentId = snapshot.documentId();
	key.documentVersion = snapshot.version();
	key.documentLength = snapshot.length();
	key.role = mRole;
	key.cursorProjectionRevision = mEditor.cursorProjectionRevision();
	key.cursorOffset = std::min(mEditor.byteCursor(), snapshot.length());
	key.littleEndian = mEditor.littleEndian();
	key.firstRecord = mRole == MRHexPaneRole::Inspector ? mInspectorFirstLine : layout.firstRecord;
	key.firstColumn = mRole == MRHexPaneRole::Inspector ? 0 : layout.firstColumn;
	key.recordLength = layout.recordLength;
	key.width = size.x;
	key.height = size.y;
	key.capacity = layout.capacity;
	key.fieldWidth = layout.fieldWidth;
	return key;
}

void MRHexPaneView::requestProjection() noexcept {
	try {
		const MRTextBufferModel::ReadSnapshot snapshot = mEditor.byteSnapshot();

		static_cast<void>(normalizeProjectionViewport(snapshot));
		const MRHexPaneProjectionKey key = projectionKey(snapshot);
		mDesiredProjectionKey = key;
		mDesiredProjectionValid = true;
		const bool projectionMatches = mProjection != nullptr && mProjection->key.computationMatches(key);

		if (mProjection != nullptr && !projectionMatches) {
			mRenderedCursorValid = false;
			hideCursor();
		}
		if (projectionMatches) {
			reportStringProjectionState();
			return;
		}
		if (mProjectionTaskId != 0) return;

		if (mProjectionGenerationCounter == 0) mProjectionGenerationCounter = 1;
		const std::uint64_t generation = mProjectionGenerationCounter++;
		std::size_t spanStart = 0;
		std::size_t spanEnd = 0;

		mrHexPaneProjectionByteSpan(key, spanStart, spanEnd);
		const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submitPacket(
		    mr::coprocessor::Lane::Compute, mr::coprocessor::TaskKind::HexPaneProjection, key.documentId, key.documentVersion,
		    mr::coprocessor::ExecutionOwnerKind::HexPane, mExecutionOwnerLocalId, generation, mr::coprocessor::WorkDirection::None,
		    static_cast<std::uint64_t>(spanStart), static_cast<std::uint64_t>(spanEnd), mrHexPaneProjectionTaskLabel(mRole),
		    [snapshot, key, generation](const mr::coprocessor::TaskInfo &info) {
			    mr::coprocessor::Result result;

			    result.task = info;
			    if (info.cancelRequested()) {
				    result.status = mr::coprocessor::TaskStatus::Cancelled;
				    return result;
			    }
			    result.payload = mrBuildHexPaneProjection(snapshot, key, generation, info.cancelFlag.get());
			    result.status = result.payload != nullptr ? mr::coprocessor::TaskStatus::Completed : mr::coprocessor::TaskStatus::Cancelled;
			    return result;
		    });
		if (taskId != 0) {
			mProjectionTaskId = taskId;
			mActiveProjectionKey = key;
			mActiveProjectionValid = true;
		}
	} catch (...) {
		mActiveProjectionValid = false;
		mProjectionTaskId = 0;
	}
}

bool MRHexPaneView::applyProjectionResult(const mr::coprocessor::Result &result) noexcept {
	if (mProjectionTaskId == 0 || result.task.id != mProjectionTaskId) return false;
	const MRHexPaneProjectionKey completedKey = mActiveProjectionKey;
	const bool completedKeyValid = mActiveProjectionValid;
	const bool newerProjectionRequested = completedKeyValid && mDesiredProjectionValid && !completedKey.computationMatches(mDesiredProjectionKey);
	const std::shared_ptr<const MRHexPaneProjectionPayload> previousProjection = mProjection;
	std::shared_ptr<const MRHexPaneProjectionPayload> adoptedProjection;
	bool adopted = false;

	mProjectionTaskId = 0;
	mActiveProjectionValid = false;
	try {
		const MRHexPaneProjectionPayload *payload = result.completed() ? dynamic_cast<const MRHexPaneProjectionPayload *>(result.payload.get()) : nullptr;
		const MRTextBufferModel::ReadSnapshot currentSnapshot = mEditor.byteSnapshot();
		const bool routeMatches = result.task.kind == mr::coprocessor::TaskKind::HexPaneProjection &&
		                          result.task.executionOwnerKind == mr::coprocessor::ExecutionOwnerKind::HexPane &&
		                          result.task.executionOwnerLocalId == mExecutionOwnerLocalId;

		if (routeMatches && payload != nullptr && completedKeyValid && payload->generation == result.task.generation && payload->key.exactlyMatches(completedKey) &&
		    mDesiredProjectionValid && payload->key.computationMatches(mDesiredProjectionKey) && currentSnapshot.documentId() == payload->key.documentId &&
		    currentSnapshot.version() == payload->key.documentVersion && currentSnapshot.length() == payload->key.documentLength) {
			adoptedProjection = std::dynamic_pointer_cast<const MRHexPaneProjectionPayload>(result.payload);
			if (adoptedProjection != nullptr) {
				mProjection = adoptedProjection;
				adopted = true;
			}
		}
	} catch (...) {
		adopted = false;
	}
	if (adopted) {
		reportStringProjectionState();
		const bool incremental = exposed() && previousProjection != nullptr &&
		                         mRenderedProjectionGeneration == previousProjection->generation &&
		                         projectionViewportMatches(previousProjection->key, adoptedProjection->key);

		if (!incremental) {
			if (exposed()) drawView();
			else {
				mRenderedProjectionGeneration = 0;
				mRenderedCursorValid = false;
				hideCursor();
			}
		} else if (mRole == MRHexPaneRole::Inspector) {
			const int visibleRows = std::max(0, std::min<int>(size.y, adoptedProjection->key.height));

			for (int row = 0; row < visibleRows; ++row)
				if (!projectedInspectorRowMatches(*previousProjection, *adoptedProjection, static_cast<std::size_t>(row)))
					drawInspectorRow(row, adoptedProjection->key, adoptedProjection.get());
			mRenderedProjectionGeneration = adoptedProjection->generation;
			mRenderedCursorValid = false;
			hideCursor();
		} else {
			const std::size_t currentCursor = std::min(mEditor.byteCursor(), adoptedProjection->key.documentLength);
			int previousCursorRow = -1;
			int currentCursorRow = -1;

			static_cast<void>(dataRowForOffset(previousProjection->key, mRenderedCursorOffset, previousCursorRow));
			static_cast<void>(dataRowForOffset(adoptedProjection->key, currentCursor, currentCursorRow));
			const int visibleRows = std::max(0, std::min<int>(size.y, adoptedProjection->key.height));
			for (int row = 0; row < visibleRows; ++row) {
				const bool cursorRow = row == previousCursorRow || row == currentCursorRow;

				if (cursorRow || !projectedDataRowMatches(*previousProjection, *adoptedProjection, static_cast<std::size_t>(row)))
					drawDataRow(row, adoptedProjection->key, adoptedProjection.get(), true, currentCursor);
			}
			mRenderedProjectionGeneration = adoptedProjection->generation;
			mRenderedCursorOffset = currentCursor;
			mRenderedCursorValid = true;
			projectNativeCursor(&adoptedProjection->key, adoptedProjection.get(), true, currentCursor);
		}
	} else if (newerProjectionRequested)
		requestProjection();
	return adopted;
}

void MRHexPaneView::refreshCursor(std::size_t previousOffset, std::size_t currentOffset, bool viewportChanged) noexcept {
	if (mRole == MRHexPaneRole::Inspector) {
		hideCursor();
		requestProjection();
		return;
	}
	if (mProjection == nullptr) {
		hideCursor();
		if (mProjectionTaskId == 0) requestProjection();
		return;
	}
	if (viewportChanged) {
		int previousRow = -1;

		if (exposed() && mRenderedProjectionGeneration == mProjection->generation && mRenderedCursorValid &&
			dataRowForOffset(mProjection->key, previousOffset, previousRow))
			drawDataRow(previousRow, mProjection->key, mProjection.get(), false, 0);
		mRenderedCursorValid = false;
		hideCursor();
		requestProjection();
		return;
	}

	bool projectionReady = false;
	try {
		const MRTextBufferModel::ReadSnapshot snapshot = mEditor.byteSnapshot();

		projectionReady = mDesiredProjectionValid && mProjection->key.computationMatches(mDesiredProjectionKey) &&
		                  snapshot.documentId() == mProjection->key.documentId && snapshot.version() == mProjection->key.documentVersion &&
		                  snapshot.length() == mProjection->key.documentLength;
	} catch (...) {
		projectionReady = false;
	}
	if (!projectionReady) {
		hideCursor();
		if (mProjectionTaskId == 0) requestProjection();
		return;
	}
	if (!exposed()) {
		mRenderedProjectionGeneration = 0;
		mRenderedCursorValid = false;
		hideCursor();
		return;
	}
	if (mRenderedProjectionGeneration != mProjection->generation) {
		drawView();
		return;
	}

	const std::size_t cursor = std::min(currentOffset, mProjection->key.documentLength);
	int previousRow = -1;
	int currentRow = -1;

	static_cast<void>(dataRowForOffset(mProjection->key, previousOffset, previousRow));
	static_cast<void>(dataRowForOffset(mProjection->key, cursor, currentRow));
	if (previousRow >= 0) drawDataRow(previousRow, mProjection->key, mProjection.get(), true, cursor);
	if (currentRow >= 0 && currentRow != previousRow) drawDataRow(currentRow, mProjection->key, mProjection.get(), true, cursor);
	mRenderedCursorOffset = cursor;
	mRenderedCursorValid = true;
	projectNativeCursor(&mProjection->key, mProjection.get(), true, cursor);
}

void MRHexPaneView::refreshFocus() noexcept {
	if (mRole == MRHexPaneRole::Inspector || mProjection == nullptr || mRenderedProjectionGeneration != mProjection->generation) {
		hideCursor();
		return;
	}

	bool projectionReady = false;
	try {
		const MRTextBufferModel::ReadSnapshot snapshot = mEditor.byteSnapshot();

		projectionReady = mDesiredProjectionValid && mProjection->key.computationMatches(mDesiredProjectionKey) &&
		                  snapshot.documentId() == mProjection->key.documentId && snapshot.version() == mProjection->key.documentVersion &&
		                  snapshot.length() == mProjection->key.documentLength;
	} catch (...) {
		projectionReady = false;
	}
	const std::size_t cursor = std::min(mEditor.byteCursor(), mProjection->key.documentLength);
	projectNativeCursor(&mProjection->key, mProjection.get(), projectionReady, cursor);
}

void MRHexPaneView::reportStringProjectionState() noexcept {
	if (mRole != MRHexPaneRole::Strings || !mStringStatusPending || !mEditor.inputPaneIsActive(mRole) || mProjection == nullptr ||
	    !mDesiredProjectionValid || !mProjection->key.computationMatches(mDesiredProjectionKey))
		return;
	mStringStatusPending = false;
	if (mProjection->hasVisibleString)
		mr::messageline::clearOwner(mr::messageline::Owner::HexEditor);
	else
		mr::messageline::postAutoTimed(mr::messageline::Owner::HexEditor, "No recognized C/ASCII/UTF-8 strings visible.", mr::messageline::Kind::Info,
		                               mr::messageline::kPriorityLow);
}

int MRHexPaneView::verticalScrollBarMaximum() const {
	if (mRole == MRHexPaneRole::Inspector) {
		const std::size_t visibleLines = static_cast<std::size_t>(std::max(1, size.y));
		const std::size_t maximumFirstLine = kMrHexInspectorLineCount > visibleLines ? kMrHexInspectorLineCount - visibleLines : 0;

		return mrHexPaneScrollBarMaximum(maximumFirstLine);
	}
	const MRTextBufferModel::ReadSnapshot snapshot = mEditor.byteSnapshot();
	const std::size_t recordLength = static_cast<std::size_t>(std::max(1, mEditor.recordLength()));

	return mrHexPaneScrollBarMaximum(mrHexPaneMaximumFirstRecord(snapshot.length(), recordLength, size.y));
}

int MRHexPaneView::horizontalScrollBarMaximum() const {
	if (mRole == MRHexPaneRole::Inspector) return 0;
	const std::size_t recordLength = static_cast<std::size_t>(std::max(1, mEditor.recordLength()));
	const int capacity = std::max(0, (size.x - kMrHexPaneOffsetWidth) / mrHexPaneFieldWidth(mRole));

	return mrHexPaneScrollBarMaximum(mrHexPaneMaximumFirstColumn(recordLength, capacity));
}

int MRHexPaneView::verticalScrollBarPageStep() const noexcept {
	return std::max(1, size.y - 1);
}

int MRHexPaneView::horizontalScrollBarPageStep() const noexcept {
	return std::max(1, (size.x - kMrHexPaneOffsetWidth) / mrHexPaneFieldWidth(mRole) - 1);
}

int MRHexPaneView::verticalScrollBarValue() const noexcept {
	return mrHexPaneScrollBarMaximum(mRole == MRHexPaneRole::Inspector ? mInspectorFirstLine : mEditor.dataFirstRecord());
}

int MRHexPaneView::horizontalScrollBarValue() const noexcept {
	return mrHexPaneScrollBarMaximum(mEditor.dataFirstColumn());
}

void MRHexPaneView::setVerticalScrollBarValue(int value) noexcept {
	const int maximum = verticalScrollBarMaximum();
	const int clamped = std::clamp(value, 0, maximum);

	const std::size_t next = static_cast<std::size_t>(clamped);

	if (mRole == MRHexPaneRole::Inspector) {
		if (mInspectorFirstLine == next) return;
		mInspectorFirstLine = next;
		requestProjection();
		return;
	}
	if (!mEditor.setDataViewport(next, mEditor.dataFirstColumn())) return;
	mEditor.refreshHexProjection();
}

void MRHexPaneView::setHorizontalScrollBarValue(int value) noexcept {
	const int maximum = horizontalScrollBarMaximum();

	const std::size_t next = static_cast<std::size_t>(std::clamp(value, 0, maximum));

	if (!mEditor.setDataViewport(mEditor.dataFirstRecord(), next)) return;
	mEditor.refreshHexProjection();
}

void MRHexPaneView::scrollByWheel(int wheel) noexcept {
	int direction = 0;

	switch (wheel) {
		case mwUp:
			direction = -1;
			break;
		case mwDown:
			direction = 1;
			break;
		default:
			return;
	}

	const int current = verticalScrollBarValue();
	const int maximum = verticalScrollBarMaximum();
	const int next = direction < 0 ? std::max(0, current - 3) : std::min(maximum, current + 3);

	setVerticalScrollBarValue(next);
}

void MRHexPaneView::changeBounds(const TRect &bounds) {
	mRenderedProjectionGeneration = 0;
	mRenderedCursorValid = false;
	TView::changeBounds(bounds);
}

bool MRHexPaneView::dataRowForOffset(const MRHexPaneProjectionKey &layout, std::size_t offset, int &row) const noexcept {
	row = -1;
	if (mRole == MRHexPaneRole::Inspector || layout.recordLength == 0 || offset > layout.documentLength) return false;
	const std::size_t endRecord = layout.documentLength / layout.recordLength;
	const std::size_t record = offset == layout.documentLength ? endRecord : offset / layout.recordLength;

	if (record < layout.firstRecord || record - layout.firstRecord >= static_cast<std::size_t>(std::max(0, std::min<int>(size.y, layout.height)))) return false;
	row = static_cast<int>(record - layout.firstRecord);
	return true;
}

void MRHexPaneView::drawDataRow(int row, const MRHexPaneProjectionKey &layout, const MRHexPaneProjectionPayload *projection, bool cursorValid,
	                            std::size_t cursorOffset) {
	const TAttrPair normal = getColor(0x0201);
	const TAttrPair highlighted = normal >> 8;
	const TAttrPair changed = getColor(0x0505);
	const TAttrPair lineNumbers = getColor(0x0606);
	const std::size_t length = layout.documentLength;
	const std::size_t cursor = std::min(cursorOffset, length);
	const std::size_t dataRecordCount = mrHexPaneDataRecordCount(length, layout.recordLength);
	std::size_t endRecord = 0;
	std::size_t endColumn = 0;

	hexEndPosition(length, layout.recordLength, endRecord, endColumn);
	const std::size_t rowIndex = static_cast<std::size_t>(row);
	const std::size_t record = rowIndex > std::numeric_limits<std::size_t>::max() - layout.firstRecord
	                               ? std::numeric_limits<std::size_t>::max()
	                               : layout.firstRecord + rowIndex;
	const bool dataRecord = record < dataRecordCount;
	const bool appendRecord = record == endRecord;
	const std::size_t recordStart = dataRecord && record <= std::numeric_limits<std::size_t>::max() / layout.recordLength
	                                      ? record * layout.recordLength
	                                      : (appendRecord ? length : std::numeric_limits<std::size_t>::max());
	TDrawBuffer buffer;

	buffer.moveChar(0, ' ', normal, size.x);
	if (projection != nullptr && rowIndex < projection->rows.size())
		buffer.moveStr(0, projection->rows[rowIndex].offsetText, lineNumbers, kMrHexPaneOffsetWidth - 1);
	for (int column = 0; column < layout.capacity; ++column) {
		const std::size_t columnIndex = static_cast<std::size_t>(column);
		const std::size_t recordColumn = columnIndex > std::numeric_limits<std::size_t>::max() - layout.firstColumn
		                                     ? std::numeric_limits<std::size_t>::max()
		                                     : layout.firstColumn + columnIndex;
		const bool fieldInRecord = recordColumn < layout.recordLength;
		const bool dataAddress = dataRecord && fieldInRecord && recordStart < length && recordColumn < length - recordStart;
		const bool appendAddress = appendRecord && recordColumn == endColumn;
		const bool addressValid = dataAddress || appendAddress;
		const std::size_t offset = dataAddress ? recordStart + recordColumn : length;
		const bool selectedByte = cursorValid && addressValid && offset == cursor;
		const bool editedByte = addressValid && editContainsByte(offset);
		const std::size_t cellIndex = rowIndex * static_cast<std::size_t>(layout.capacity) + columnIndex;
		const char *text = projection != nullptr && cellIndex < projection->cells.size() ? projection->cells[cellIndex].text : "";
		std::string editedText;

		if (editedByte) {
			editedText = editTextForByte(offset);
			text = editedText.c_str();
		}
		const bool highlightedByte = selectedByte || editedByte;
		const TAttrPair color = editedByte ? changed : (highlightedByte ? highlighted : normal);
		const int x = kMrHexPaneOffsetWidth + column * layout.fieldWidth;
		const int textWidth = layout.fieldWidth - (mRole == MRHexPaneRole::Strings ? 0 : 1);

		if (highlightedByte && textWidth > 0) buffer.moveChar(static_cast<ushort>(x), ' ', color[0], static_cast<ushort>(textWidth));
		if (text[0] != '\0') buffer.moveStr(static_cast<ushort>(x), text, color, static_cast<ushort>(textWidth));
	}
	writeLine(0, row, size.x, 1, buffer);
}

void MRHexPaneView::drawInspectorRow(int row, const MRHexPaneProjectionKey &layout, const MRHexPaneProjectionPayload *projection) {
	const TAttrPair normal = getColor(0x0201);
	TDrawBuffer buffer;
	const std::size_t rowIndex = static_cast<std::size_t>(std::max(0, row));
	const std::size_t lineIndex = rowIndex > std::numeric_limits<std::size_t>::max() - layout.firstRecord
	                                  ? std::numeric_limits<std::size_t>::max()
	                                  : layout.firstRecord + rowIndex;

	if (projection != nullptr && lineIndex < projection->inspectorLines.size())
		writeInspectorLine(buffer, size.x, projection->inspectorLines[lineIndex].label, projection->inspectorLines[lineIndex].value, normal);
	else
		buffer.moveChar(0, ' ', normal, size.x);
	writeLine(0, row, size.x, 1, buffer);
}

void MRHexPaneView::projectNativeCursor(const MRHexPaneProjectionKey *layout, const MRHexPaneProjectionPayload *projection, bool cursorValid,
	                                    std::size_t cursorOffset) noexcept {
	if (mRole != MRHexPaneRole::Inspector && cursorValid && projection != nullptr && layout != nullptr && layout->recordLength > 0 &&
	    layout->fieldWidth > 0 && mEditor.inputPaneIsActive(mRole)) {
		const std::size_t length = layout->documentLength;
		const std::size_t cursor = std::min(cursorOffset, length);
		std::size_t endRecord = 0;
		std::size_t endColumn = 0;

		hexEndPosition(length, layout->recordLength, endRecord, endColumn);
		const std::size_t cursorRecord = cursor == length ? endRecord : cursor / layout->recordLength;
		const std::size_t cursorColumn = cursor == length ? endColumn : cursor % layout->recordLength;
		const int currentCapacity = std::max(0, (size.x - kMrHexPaneOffsetWidth) / layout->fieldWidth);
		const int visibleCapacity = std::min(layout->capacity, currentCapacity);

		if (cursorRecord >= layout->firstRecord &&
		    cursorRecord - layout->firstRecord < static_cast<std::size_t>(std::max(0, std::min<int>(size.y, layout->height))) &&
		    cursorColumn >= layout->firstColumn && cursorColumn - layout->firstColumn < static_cast<std::size_t>(visibleCapacity)) {
			const int row = static_cast<int>(cursorRecord - layout->firstRecord);
			const int fieldX = kMrHexPaneOffsetWidth + static_cast<int>(cursorColumn - layout->firstColumn) * layout->fieldWidth;
			const int inputX = fieldX + (mRole == MRHexPaneRole::Strings ? 0 : layout->fieldWidth - 2);
			const std::size_t rowIndex = static_cast<std::size_t>(row);
			const std::size_t columnIndex = cursorColumn - layout->firstColumn;

			if (layout->capacity > 0 && rowIndex <= std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(layout->capacity)) {
				const std::size_t cellIndex = rowIndex * static_cast<std::size_t>(layout->capacity) + columnIndex;

				if (rowIndex < projection->rows.size() && cellIndex < projection->cells.size() && inputX >= 0 && inputX < size.x) {
					setCursor(inputX, row);
					showCursor();
					return;
				}
			}
		}
	}
	hideCursor();
}

void MRHexPaneView::redrawEditRows(std::size_t offset, std::size_t length) noexcept {
	if (mRole == MRHexPaneRole::Inspector || mProjection == nullptr || !exposed() ||
	    mRenderedProjectionGeneration != mProjection->generation)
		return;
	try {
		const MRTextBufferModel::ReadSnapshot snapshot = mEditor.byteSnapshot();

		if (!mDesiredProjectionValid || !mProjection->key.computationMatches(mDesiredProjectionKey) ||
		    snapshot.documentId() != mProjection->key.documentId || snapshot.version() != mProjection->key.documentVersion ||
		    snapshot.length() != mProjection->key.documentLength)
			return;
	} catch (...) {
		return;
	}

	const MRHexPaneProjectionKey &layout = mProjection->key;
	const std::size_t documentLength = layout.documentLength;
	const std::size_t firstOffset = std::min(offset, documentLength);
	const std::size_t spanLength = std::max<std::size_t>(1, length);
	const std::size_t lastOffset = spanLength - 1 > std::numeric_limits<std::size_t>::max() - firstOffset
	                                   ? documentLength
	                                   : std::min(documentLength, firstOffset + spanLength - 1);
	const std::size_t endRecord = documentLength / layout.recordLength;
	const std::size_t firstRecord = firstOffset == documentLength ? endRecord : firstOffset / layout.recordLength;
	const std::size_t lastRecord = lastOffset == documentLength ? endRecord : lastOffset / layout.recordLength;
	const std::size_t visibleRows = static_cast<std::size_t>(std::max(0, std::min<int>(size.y, layout.height)));
	const std::size_t visibleLastRecord = visibleRows == 0 || visibleRows - 1 > std::numeric_limits<std::size_t>::max() - layout.firstRecord
	                                          ? layout.firstRecord
	                                          : layout.firstRecord + visibleRows - 1;
	const std::size_t drawFirst = std::max(firstRecord, layout.firstRecord);
	const std::size_t drawLast = std::min(lastRecord, visibleLastRecord);
	const std::size_t cursor = std::min(mEditor.byteCursor(), documentLength);

	if (visibleRows != 0 && drawFirst <= drawLast)
		for (std::size_t record = drawFirst; record <= drawLast; ++record) {
			drawDataRow(static_cast<int>(record - layout.firstRecord), layout, mProjection.get(), true, cursor);
			if (record == std::numeric_limits<std::size_t>::max()) break;
		}
	mRenderedCursorOffset = cursor;
	mRenderedCursorValid = true;
	projectNativeCursor(&layout, mProjection.get(), true, cursor);
}

void MRHexPaneView::draw() {
	const TAttrPair normal = getColor(0x0201);
	const MRHexPaneProjectionPayload *projection = mProjection.get();
	const MRHexPaneProjectionKey *layout = projection != nullptr ? &projection->key : nullptr;
	bool projectionReady = false;
	if (projection != nullptr) {
		try {
			const MRTextBufferModel::ReadSnapshot snapshot = mEditor.byteSnapshot();

			projectionReady = mDesiredProjectionValid && projection->key.computationMatches(mDesiredProjectionKey) &&
			                  snapshot.documentId() == projection->key.documentId && snapshot.version() == projection->key.documentVersion &&
			                  snapshot.length() == projection->key.documentLength;
		} catch (...) {
			projectionReady = false;
		}
	}
	const std::size_t cursor = projectionReady && layout != nullptr
	                               ? std::min(mEditor.byteCursor(), layout->documentLength)
	                               : (layout != nullptr && mRenderedCursorValid
	                                      ? std::min(mRenderedCursorOffset, layout->documentLength)
	                                      : (layout != nullptr ? std::min(layout->cursorOffset, layout->documentLength) : 0));
	const bool cursorValid = projectionReady || (projection != nullptr && mRenderedProjectionGeneration == projection->generation && mRenderedCursorValid);

	for (int row = 0; row < size.y; ++row) {
		TDrawBuffer buffer;

		if (mRole == MRHexPaneRole::Inspector) {
			if (layout != nullptr && projection != nullptr && row < layout->height)
				drawInspectorRow(row, *layout, projection);
			else {
				buffer.moveChar(0, ' ', normal, size.x);
				writeLine(0, row, size.x, 1, buffer);
			}
			continue;
		}
		if (layout == nullptr || projection == nullptr || row >= layout->height || static_cast<std::size_t>(row) >= projection->rows.size()) {
			buffer.moveChar(0, ' ', normal, size.x);
			writeLine(0, row, size.x, 1, buffer);
			continue;
		}
		drawDataRow(row, *layout, projection, cursorValid, cursor);
	}
	mRenderedProjectionGeneration = projection != nullptr ? projection->generation : 0;
	if (mRole == MRHexPaneRole::Inspector || projection == nullptr) mRenderedCursorValid = false;
	else {
		mRenderedCursorOffset = cursor;
		mRenderedCursorValid = cursorValid;
	}
	projectNativeCursor(layout, projection, projectionReady && cursorValid, cursor);
}

TPalette &MRHexPaneView::getPalette() const {
	static TPalette palette("\x06\x07\x09\x0A\x0B\x0C", 6);

	return palette;
}

void MRHexPaneView::handleEvent(TEvent &event) {
	if (event.what == evKeyDown && event.keyDown.keyCode == kbCtrlE) {
		mEditor.toggleEndian();
		clearEvent(event);
		return;
	}
	if (event.what == evKeyDown && event.keyDown.keyCode == kbCtrlG) {
		std::size_t offset = 0;

		if (mEditing && !commitEdit()) {
			clearEvent(event);
			return;
		}
		if (promptGotoOffset(mEditor.byteCursor(), offset)) {
			mEditor.selectByte(offset);
		}
		clearEvent(event);
		return;
	}
	if (mRole == MRHexPaneRole::Inspector) {
		const std::size_t visibleLines = static_cast<std::size_t>(std::max(1, size.y));
		const std::size_t maximumFirstLine = kMrHexInspectorLineCount > visibleLines ? kMrHexInspectorLineCount - visibleLines : 0;

		if (event.what == evMouseWheel) {
			scrollByWheel(event.mouse.wheel);
			clearEvent(event);
			return;
		}
		if (event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0) {
			const TPoint local = makeLocal(event.mouse.where);
			const std::size_t lineIndex = local.y >= 0 && local.y < size.y ? mInspectorFirstLine + static_cast<std::size_t>(local.y) : kMrHexInspectorLineCount;

			if (lineIndex + 1 == kMrHexInspectorLineCount) mEditor.toggleEndian();
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
			const std::size_t previousFirstLine = mInspectorFirstLine;

			switch (event.keyDown.keyCode) {
				case kbUp:
					mInspectorFirstLine = mInspectorFirstLine == 0 ? 0 : mInspectorFirstLine - 1;
					break;
				case kbDown:
					mInspectorFirstLine = std::min(maximumFirstLine, mInspectorFirstLine + 1);
					break;
				case kbPgUp:
					mInspectorFirstLine = mInspectorFirstLine > visibleLines ? mInspectorFirstLine - visibleLines : 0;
					break;
				case kbPgDn:
					mInspectorFirstLine = std::min(maximumFirstLine, mInspectorFirstLine + visibleLines);
					break;
				case kbHome:
					mInspectorFirstLine = 0;
					break;
				case kbEnd:
					mInspectorFirstLine = maximumFirstLine;
					break;
				default:
					return;
			}
			if (mInspectorFirstLine != previousFirstLine) {
				requestProjection();
			}
			clearEvent(event);
		}
		return;
	}
	if (event.what == evMouseWheel) {
		scrollByWheel(event.mouse.wheel);
		clearEvent(event);
		return;
	}
	if (event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0) {
		const TPoint local = makeLocal(event.mouse.where);

		if (mEditing && !commitEdit()) {
			clearEvent(event);
			return;
		}
		const MRTextBufferModel::ReadSnapshot snapshot = mEditor.byteSnapshot();
		const MRHexPaneProjectionPayload *displayedProjection = mProjection.get();
		const std::size_t configuredRecordLength = static_cast<std::size_t>(std::max(1, mEditor.recordLength()));

		if (displayedProjection == nullptr || displayedProjection->key.role != mRole || displayedProjection->key.documentId != snapshot.documentId() ||
		    displayedProjection->key.documentVersion != snapshot.version() || displayedProjection->key.documentLength != snapshot.length() ||
		    displayedProjection->key.recordLength != configuredRecordLength) {
			requestProjection();
			clearEvent(event);
			return;
		}
		const MRHexPaneProjectionKey &layout = displayedProjection->key;
		const int currentCapacity = layout.fieldWidth > 0 ? std::max(0, (size.x - kMrHexPaneOffsetWidth) / layout.fieldWidth) : 0;
		const int visibleCapacity = std::min(layout.capacity, currentCapacity);
		if (local.y >= 0 && local.y < size.y && local.y < layout.height && local.x >= kMrHexPaneOffsetWidth && visibleCapacity > 0) {
			const int column = (local.x - kMrHexPaneOffsetWidth) / layout.fieldWidth;

			if (column >= 0 && column < visibleCapacity) {
				const std::size_t rowIndex = static_cast<std::size_t>(local.y);
				const std::size_t columnIndex = static_cast<std::size_t>(column);
				const std::size_t capacity = static_cast<std::size_t>(layout.capacity);

				if (rowIndex >= displayedProjection->rows.size() || rowIndex > (std::numeric_limits<std::size_t>::max() - columnIndex) / capacity ||
				    rowIndex * capacity + columnIndex >= displayedProjection->cells.size()) {
					clearEvent(event);
					return;
				}
				const std::size_t record = rowIndex > std::numeric_limits<std::size_t>::max() - layout.firstRecord
				                               ? std::numeric_limits<std::size_t>::max()
				                               : layout.firstRecord + rowIndex;
				const std::size_t recordColumn = columnIndex > std::numeric_limits<std::size_t>::max() - layout.firstColumn
				                                     ? std::numeric_limits<std::size_t>::max()
				                                     : layout.firstColumn + columnIndex;
				const std::size_t dataRecordCount = mrHexPaneDataRecordCount(snapshot.length(), layout.recordLength);
				std::size_t endRecord = 0;
				std::size_t endColumn = 0;

				hexEndPosition(snapshot.length(), layout.recordLength, endRecord, endColumn);
				const bool dataRecord = record < dataRecordCount && record <= std::numeric_limits<std::size_t>::max() / layout.recordLength;
				const std::size_t recordStart = dataRecord ? record * layout.recordLength : snapshot.length();
				const bool dataAddress = dataRecord && recordColumn < layout.recordLength && recordStart < snapshot.length() &&
				                         recordColumn < snapshot.length() - recordStart;
				const bool appendAddress = record == endRecord && recordColumn == endColumn;

				if (dataAddress || appendAddress) {
					const std::size_t selectedOffset = dataAddress ? recordStart + recordColumn : snapshot.length();
					const bool viewportChanged = mEditor.setDataViewport(layout.firstRecord, layout.firstColumn);
					const bool cursorUnchanged = mEditor.byteCursor() == selectedOffset;

					mEditor.selectByte(selectedOffset);
					if (viewportChanged && cursorUnchanged) mEditor.refreshHexProjection();
				}
			}
		}
		clearEvent(event);
		return;
	}
	if (event.what != evKeyDown) return;

	const ushort key = event.keyDown.keyCode;
	const char character = static_cast<char>(event.keyDown.charScan.charCode);
	const NumericFormat *format = numericFormat(mRole);
	if (key == kbEsc) {
		cancelPendingEdit();
		clearEvent(event);
		return;
	}
	if (key == kbIns) {
		if (mEditing) cancelPendingEdit();
		mEditor.toggleInsertMode();
		clearEvent(event);
		return;
	}
	if (key == kbEnter) {
		if (mEditing) static_cast<void>(commitEdit());
		else
			mEditor.moveByteCursor(1);
		clearEvent(event);
		return;
	}
	if (key == kbBack || key == kbCtrlH || key == kbCtrlBack) {
		if (mEditing && !mEditText.empty()) {
			const std::size_t previousLength = mEditText.size();

			mEditText.pop_back();
			redrawEditRows(mEditOffset, previousLength);
		}
		clearEvent(event);
		return;
	}
	if (key == kbLeft || key == kbRight || key == kbUp || key == kbDown || key == kbPgUp || key == kbPgDn || key == kbHome || key == kbEnd || key == kbCtrlHome || key == kbCtrlEnd) {
		const std::size_t navigationOrigin = mEditing ? mEditOffset : mEditor.byteCursor();

		if (mEditing && !commitEdit()) {
			clearEvent(event);
			return;
		}
		const std::size_t recordLength = static_cast<std::size_t>(std::max(1, mEditor.recordLength()));
		const std::size_t length = mEditor.byteSnapshot().length();
		const std::size_t cursor = std::min(navigationOrigin, length);
		const std::size_t dataRecordCount = mrHexPaneDataRecordCount(length, recordLength);
		std::size_t endRecord = 0;
		std::size_t endColumn = 0;

		hexEndPosition(length, recordLength, endRecord, endColumn);
		const std::size_t cursorRecord = cursor == length ? endRecord : cursor / recordLength;
		const std::size_t cursorColumn = cursor == length ? endColumn : cursor % recordLength;
		const std::size_t pageRecords = static_cast<std::size_t>(std::max(1, size.y - 1));

		switch (key) {
			case kbLeft:
				mEditor.selectByte(cursor == 0 ? 0 : cursor - 1);
				break;
			case kbRight:
				mEditor.selectByte(cursor >= length ? length : cursor + 1);
				break;
			case kbUp:
				mEditor.selectRecordColumn(cursorRecord == 0 ? 0 : cursorRecord - 1, cursorColumn);
				break;
			case kbDown:
				mEditor.selectRecordColumn(cursorRecord >= dataRecordCount ? dataRecordCount : cursorRecord + 1, cursorColumn);
				break;
			case kbPgUp:
				mEditor.selectRecordColumn(cursorRecord > pageRecords ? cursorRecord - pageRecords : 0, cursorColumn);
				break;
			case kbPgDn:
				mEditor.selectRecordColumn(pageRecords > dataRecordCount - std::min(cursorRecord, dataRecordCount)
				                               ? dataRecordCount
				                               : cursorRecord + pageRecords,
				                           cursorColumn);
				break;
			case kbHome:
				mEditor.selectRecordColumn(cursorRecord, 0);
				break;
			case kbEnd:
				mEditor.selectRecordColumn(cursorRecord, recordLength - 1);
				break;
			case kbCtrlHome:
				mEditor.selectByte(0);
				break;
			case kbCtrlEnd:
				mEditor.selectByte(length);
				break;
			default:
				break;
		}
		clearEvent(event);
		return;
	}
	if (character < ' ' || character > '~') return;
	const bool wasEditing = mEditing;
	const std::size_t previousEditLength = mEditText.size();
	if (format != nullptr) {
		const char normalized = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

		if (!acceptsNumericCharacter(normalized, format->base)) {
			clearEvent(event);
			return;
		}
		if (!mEditing) beginEdit(mEditor.byteCursor(), normalized);
		else if (mEditText.size() < static_cast<std::size_t>(format->maximumLength))
			mEditText.push_back(normalized);
	} else if (!mEditing)
		beginEdit(mEditor.byteCursor(), character);
	else if (mEditText.size() < static_cast<std::size_t>(std::max(1, mEditor.recordLength())))
		mEditText.push_back(character);
	if (mEditing && (!wasEditing || mEditText.size() != previousEditLength))
		redrawEditRows(mEditOffset, std::max(previousEditLength, mEditText.size()));
	clearEvent(event);
}

void MRHexPaneView::cancelEdit() noexcept {
	mEditing = false;
	mEditOffset = 0;
	mEditText.clear();
}

bool MRHexPaneView::commitEdit() {
	const NumericFormat *format = numericFormat(mRole);
	std::string bytes;
	std::size_t overwriteLength = 1;

	if (!mEditing) return true;
	if (format != nullptr) {
		if (!numericTextIsValid(mEditText, format->base)) return false;
		bytes.assign(1, static_cast<char>(std::strtoul(mEditText.c_str(), nullptr, format->base)));
	} else {
		if (mEditText.empty()) return false;
		bytes = mEditText;
		overwriteLength = mEditText.size();
	}
	if (!mEditor.replaceBytes(mEditOffset, bytes, overwriteLength)) return false;
	cancelEdit();
	mEditor.refreshHexProjection();
	return true;
}

void MRHexPaneView::beginEdit(std::size_t offset, char character) {
	mEditing = true;
	mEditOffset = offset;
	mEditText.assign(1, character);
}

bool MRHexPaneView::editContainsByte(std::size_t offset) const noexcept {
	if (!mEditing) return false;
	if (numericFormat(mRole) != nullptr) return offset == mEditOffset;
	return offset >= mEditOffset && offset - mEditOffset < mEditText.size();
}

std::string MRHexPaneView::editTextForByte(std::size_t offset) const {
	if (numericFormat(mRole) != nullptr || offset < mEditOffset || offset - mEditOffset >= mEditText.size()) return mEditText;
	return std::string(1, mEditText[offset - mEditOffset]);
}
