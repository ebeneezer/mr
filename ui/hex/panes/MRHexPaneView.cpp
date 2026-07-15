#define Uses_TInputLine
#include <tvision/tv.h>

#include "MRHexPaneView.hpp"

#include "../MRBentoHexEditor.hpp"
#include "../MRHexStrings.hpp"

#include "../../MRMessageLineController.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

constexpr int kOffsetWidth = 9;
constexpr std::size_t kInspectorLineCount = 26;

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

struct DisplayLayout {
	std::size_t firstRecord;
	std::size_t firstColumn;
	std::size_t recordLength;
	int capacity;
	int fieldWidth;
};

const NumericFormat *numericFormat(MRHexPaneRole role) noexcept {
	for (const NumericFormat &format : kNumericFormats)
		if (format.role == role) return &format;
	return nullptr;
}

int fieldWidth(MRHexPaneRole role) noexcept {
	switch (role) {
		case MRHexPaneRole::Hex:
			return 3;
		case MRHexPaneRole::Decimal:
		case MRHexPaneRole::Octal:
			return 4;
		case MRHexPaneRole::Binary:
			return 9;
		case MRHexPaneRole::Strings:
		case MRHexPaneRole::Inspector:
			return 1;
	}
	return 1;
}

std::size_t maximumFirstRecord(std::size_t length, std::size_t recordLength, int height) noexcept {
	const std::size_t visibleRecords = static_cast<std::size_t>(std::max(1, height));
	const std::size_t recordCount = length / recordLength + 1;

	return recordCount > visibleRecords ? recordCount - visibleRecords : 0;
}

std::size_t maximumFirstColumn(std::size_t recordLength, int capacity) noexcept {
	const std::size_t visibleColumns = static_cast<std::size_t>(std::max(0, capacity));

	return recordLength > visibleColumns ? recordLength - visibleColumns : 0;
}

int scrollBarMaximum(std::size_t maximum) noexcept {
	return maximum > static_cast<std::size_t>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max() : static_cast<int>(maximum);
}

DisplayLayout displayLayout(MRHexPaneRole role, std::size_t length, int width, int height, int configuredRecordLength, std::size_t firstRecord, std::size_t firstColumn) {
	const std::size_t recordLength = static_cast<std::size_t>(std::max(1, configuredRecordLength));
	const int widthPerField = fieldWidth(role);
	const int capacity = std::max(0, (width - kOffsetWidth) / widthPerField);
	const std::size_t maximumRecord = maximumFirstRecord(length, recordLength, height);
	const std::size_t maximumColumn = maximumFirstColumn(recordLength, capacity);

	return DisplayLayout{std::min(firstRecord, maximumRecord), std::min(firstColumn, maximumColumn), recordLength, capacity, widthPerField};
}

std::string byteText(MRHexPaneRole role, unsigned char value) {
	char text[16] = {0};

	switch (role) {
		case MRHexPaneRole::Hex:
			std::snprintf(text, sizeof(text), "%02X", static_cast<unsigned>(value));
			return text;
		case MRHexPaneRole::Decimal:
			std::snprintf(text, sizeof(text), "%03u", static_cast<unsigned>(value));
			return text;
		case MRHexPaneRole::Octal:
			std::snprintf(text, sizeof(text), "%03o", static_cast<unsigned>(value));
			return text;
		case MRHexPaneRole::Binary:
			for (int bit = 7; bit >= 0; --bit) text[7 - bit] = (value & (1u << bit)) != 0 ? '1' : '0';
			return text;
		case MRHexPaneRole::Strings:
			return std::string();
		case MRHexPaneRole::Inspector:
			return std::string();
	}
	return std::string();
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

} // namespace

MRHexPaneView::MRHexPaneView(const TRect &bounds, MRBentoHexEditor &editor, MRHexPaneRole role) noexcept
	: TView(bounds), mEditor(editor), mRole(role), mEditing(false), mEditOffset(0), mEditText(), mFirstRecord(0), mFirstColumn(0), mInspectorFirstLine(0), mLastCursorProjectionRevision(0), mInputPaneWasActive(false), mInspectorLines() {
	options |= ofSelectable;
}

void MRHexPaneView::cancelPendingEdit() noexcept {
	cancelEdit();
}

bool MRHexPaneView::commitPendingEdit() {
	return !mEditing || commitEdit();
}

int MRHexPaneView::verticalScrollBarMaximum() const {
	if (mRole == MRHexPaneRole::Inspector) {
		const std::size_t visibleLines = static_cast<std::size_t>(std::max(1, size.y));
		const std::size_t maximumFirstLine = kInspectorLineCount > visibleLines ? kInspectorLineCount - visibleLines : 0;

		return scrollBarMaximum(maximumFirstLine);
	}
	const MRTextBufferModel::ReadSnapshot snapshot = mEditor.byteSnapshot();
	const std::size_t recordLength = static_cast<std::size_t>(std::max(1, mEditor.recordLength()));

	return scrollBarMaximum(maximumFirstRecord(snapshot.length(), recordLength, size.y));
}

int MRHexPaneView::horizontalScrollBarMaximum() const {
	if (mRole == MRHexPaneRole::Inspector) return 0;
	const std::size_t recordLength = static_cast<std::size_t>(std::max(1, mEditor.recordLength()));
	const int capacity = std::max(0, (size.x - kOffsetWidth) / fieldWidth(mRole));

	return scrollBarMaximum(maximumFirstColumn(recordLength, capacity));
}

int MRHexPaneView::verticalScrollBarPageStep() const noexcept {
	return std::max(1, size.y - 1);
}

int MRHexPaneView::horizontalScrollBarPageStep() const noexcept {
	return std::max(1, (size.x - kOffsetWidth) / fieldWidth(mRole) - 1);
}

int MRHexPaneView::verticalScrollBarValue() const noexcept {
	return scrollBarMaximum(mRole == MRHexPaneRole::Inspector ? mInspectorFirstLine : mFirstRecord);
}

int MRHexPaneView::horizontalScrollBarValue() const noexcept {
	return scrollBarMaximum(mFirstColumn);
}

void MRHexPaneView::setVerticalScrollBarValue(int value) noexcept {
	const int maximum = verticalScrollBarMaximum();
	const int clamped = std::clamp(value, 0, maximum);

	if (mRole == MRHexPaneRole::Inspector)
		mInspectorFirstLine = static_cast<std::size_t>(clamped);
	else
		mFirstRecord = static_cast<std::size_t>(clamped);
}

void MRHexPaneView::setHorizontalScrollBarValue(int value) noexcept {
	const int maximum = horizontalScrollBarMaximum();

	mFirstColumn = static_cast<std::size_t>(std::clamp(value, 0, maximum));
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

void MRHexPaneView::draw() {
	const TAttrPair normal = getColor(0x0201);
	const TAttrPair highlighted = normal >> 8;
	const TAttrPair changed = getColor(0x0505);
	const TAttrPair lineNumbers = getColor(0x0606);
	const MRTextBufferModel::ReadSnapshot snapshot = mEditor.byteSnapshot();
	const std::size_t length = snapshot.length();
	const std::size_t cursor = std::min(mEditor.byteCursor(), length);
	const std::size_t cursorProjectionRevision = mEditor.cursorProjectionRevision();
	DisplayLayout layout = displayLayout(mRole, length, size.x, size.y, mEditor.recordLength(), mFirstRecord, mFirstColumn);
	const bool inputPaneActive = mEditor.inputPaneIsActive(mRole);
	const bool inputPaneBecameActive = inputPaneActive && !mInputPaneWasActive;
	const bool cursorProjectionChanged = mRole != MRHexPaneRole::Inspector && mLastCursorProjectionRevision != cursorProjectionRevision;
	bool hasVisibleString = false;

	mInputPaneWasActive = inputPaneActive;
	if (mRole != MRHexPaneRole::Inspector && layout.capacity > 0 && (inputPaneBecameActive || cursorProjectionChanged)) {
		const std::size_t cursorRecord = cursor / layout.recordLength;
		const std::size_t cursorColumn = cursor % layout.recordLength;
		const std::size_t visibleRows = static_cast<std::size_t>(std::max(1, size.y));
		const std::size_t visibleColumns = static_cast<std::size_t>(layout.capacity);

		if (cursorRecord < layout.firstRecord) mFirstRecord = cursorRecord;
		else if (cursorRecord >= layout.firstRecord + visibleRows)
			mFirstRecord = cursorRecord - visibleRows + 1;
		if (cursorColumn < layout.firstColumn) mFirstColumn = cursorColumn;
		else if (cursorColumn >= layout.firstColumn + visibleColumns)
			mFirstColumn = cursorColumn - visibleColumns + 1;
		layout = displayLayout(mRole, length, size.x, size.y, mEditor.recordLength(), mFirstRecord, mFirstColumn);
	}
	mFirstRecord = layout.firstRecord;
	mFirstColumn = layout.firstColumn;
	if (mRole != MRHexPaneRole::Inspector && layout.capacity > 0) mLastCursorProjectionRevision = cursorProjectionRevision;

	if (mRole == MRHexPaneRole::Inspector) {
		const std::size_t inspectedOffset = length == 0 ? 0 : std::min(cursor, length - 1);

		mrBuildHexInspectorLines(snapshot, inspectedOffset, mEditor.littleEndian(), mInspectorLines);
		const std::size_t visibleLines = static_cast<std::size_t>(std::max(1, size.y));
		const std::size_t maximumFirstLine = mInspectorLines.size() > visibleLines ? mInspectorLines.size() - visibleLines : 0;

		mInspectorFirstLine = std::min(mInspectorFirstLine, maximumFirstLine);
	}

	for (int row = 0; row < size.y; ++row) {
		TDrawBuffer buffer;

		if (mRole == MRHexPaneRole::Inspector) {
			const std::size_t lineIndex = mInspectorFirstLine + static_cast<std::size_t>(row);

			if (lineIndex < mInspectorLines.size()) writeInspectorLine(buffer, size.x, mInspectorLines[lineIndex].label, mInspectorLines[lineIndex].value, normal);
			else
				buffer.moveChar(0, ' ', normal, size.x);
			writeLine(0, row, size.x, 1, buffer);
			continue;
		}

		const std::size_t recordStart = (layout.firstRecord + static_cast<std::size_t>(row)) * layout.recordLength;
		char offsetText[16] = {0};

		buffer.moveChar(0, ' ', normal, size.x);
		std::snprintf(offsetText, sizeof(offsetText), "%08zx", recordStart + layout.firstColumn);
		buffer.moveStr(0, offsetText, lineNumbers, kOffsetWidth - 1);
		for (int column = 0; column < layout.capacity; ++column) {
			const std::size_t offset = recordStart + layout.firstColumn + static_cast<std::size_t>(column);
			const int x = kOffsetWidth + column * layout.fieldWidth;
			const bool selectedByte = mRole != MRHexPaneRole::Inspector && offset == cursor;
			const bool visibleByte = offset < length && offset < recordStart + layout.recordLength;
			const MRHexStringCell stringCell = mRole == MRHexPaneRole::Strings && visibleByte && !editContainsByte(offset) ? mrHexStringCellAt(snapshot, offset) : MRHexStringCell{MRHexStringSpanKind::Hidden, {'\0'}};
			const std::string text = editContainsByte(offset) ? editTextForByte(offset) : (mRole == MRHexPaneRole::Strings ? stringCell.text : (visibleByte ? byteText(mRole, static_cast<unsigned char>(snapshot.charAt(offset))) : std::string()));
			const bool editedByte = editContainsByte(offset);
			const bool highlightedByte = selectedByte || editedByte;
			TAttrPair color = editedByte ? changed : (highlightedByte ? highlighted : normal);
			const int textWidth = layout.fieldWidth - (mRole == MRHexPaneRole::Strings ? 0 : 1);

			if (mRole == MRHexPaneRole::Strings && stringCell.kind != MRHexStringSpanKind::Hidden) hasVisibleString = true;
			if (highlightedByte && textWidth > 0) buffer.moveChar(static_cast<ushort>(x), ' ', color[0], static_cast<ushort>(textWidth));
			if (!text.empty()) buffer.moveStr(static_cast<ushort>(x), text.c_str(), color, static_cast<ushort>(textWidth));
		}
		writeLine(0, row, size.x, 1, buffer);
	}
	if (mRole == MRHexPaneRole::Strings && inputPaneBecameActive) {
		if (hasVisibleString)
			mr::messageline::clearOwner(mr::messageline::Owner::HexEditor);
		else
			mr::messageline::postAutoTimed(mr::messageline::Owner::HexEditor, "No recognized C/ASCII/UTF-8 strings visible.", mr::messageline::Kind::Info, mr::messageline::kPriorityLow);
	}
	if (inputPaneActive && layout.capacity > 0 && layout.recordLength > 0) {
		const std::size_t cursorRecord = cursor / layout.recordLength;
		const std::size_t cursorColumn = cursor % layout.recordLength;

		if (cursorRecord >= layout.firstRecord && cursorRecord - layout.firstRecord < static_cast<std::size_t>(std::max(0, size.y)) && cursorColumn >= layout.firstColumn && cursorColumn - layout.firstColumn < static_cast<std::size_t>(layout.capacity)) {
			const int row = static_cast<int>(cursorRecord - layout.firstRecord);
			const int fieldX = kOffsetWidth + static_cast<int>(cursorColumn - layout.firstColumn) * layout.fieldWidth;
			const int inputX = fieldX + (mRole == MRHexPaneRole::Strings ? 0 : layout.fieldWidth - 2);

			if (inputX >= 0 && inputX < size.x) {
				setCursor(inputX, row);
				showCursor();
				return;
			}
		}
	}
	hideCursor();
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
		const std::size_t maximumFirstLine = kInspectorLineCount > visibleLines ? kInspectorLineCount - visibleLines : 0;

		if (event.what == evMouseWheel) {
			scrollByWheel(event.mouse.wheel);
			clearEvent(event);
			return;
		}
		if (event.what == evMouseDown && (event.mouse.buttons & mbLeftButton) != 0) {
			const TPoint local = makeLocal(event.mouse.where);
			const std::size_t lineIndex = local.y >= 0 && local.y < size.y ? mInspectorFirstLine + static_cast<std::size_t>(local.y) : mInspectorLines.size();

			if (lineIndex + 1 == mInspectorLines.size()) mEditor.toggleEndian();
			clearEvent(event);
			return;
		}
		if (event.what == evKeyDown) {
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
		const MRTextBufferModel::ReadSnapshot snapshot = mEditor.byteSnapshot();
		const DisplayLayout layout = displayLayout(mRole, snapshot.length(), size.x, size.y, mEditor.recordLength(), mFirstRecord, mFirstColumn);
		const TPoint local = makeLocal(event.mouse.where);

		if (mEditing && !commitEdit()) {
			clearEvent(event);
			return;
		}
		if (local.y >= 0 && local.y < size.y && local.x >= kOffsetWidth && layout.capacity > 0) {
			const int column = (local.x - kOffsetWidth) / layout.fieldWidth;
			const std::size_t offset = (layout.firstRecord + static_cast<std::size_t>(local.y)) * layout.recordLength + layout.firstColumn + static_cast<std::size_t>(column);

			if (column >= 0 && column < layout.capacity && offset < snapshot.length()) {
				mEditor.selectByte(offset);
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
		cancelEdit();
		clearEvent(event);
		return;
	}
	if (key == kbIns) {
		if (mEditing) cancelEdit();
		mEditor.toggleInsertMode();
		clearEvent(event);
		return;
	}
	if (key == kbEnter) {
		if (mEditing) static_cast<void>(commitEdit());
		clearEvent(event);
		return;
	}
	if (key == kbBack || key == kbCtrlH || key == kbCtrlBack) {
		if (mEditing && !mEditText.empty()) mEditText.pop_back();
		clearEvent(event);
		return;
	}
	if (key == kbLeft || key == kbRight || key == kbUp || key == kbDown || key == kbPgUp || key == kbPgDn || key == kbHome || key == kbEnd || key == kbCtrlHome || key == kbCtrlEnd) {
		if (mEditing && !commitEdit()) {
			clearEvent(event);
			return;
		}
		const std::size_t recordLength = static_cast<std::size_t>(std::max(1, mEditor.recordLength()));
		const std::size_t cursor = mEditor.byteCursor();
		const std::size_t length = mEditor.byteSnapshot().length();
		const std::ptrdiff_t page = static_cast<std::ptrdiff_t>(recordLength * static_cast<std::size_t>(std::max(1, size.y - 1)));

		switch (key) {
			case kbLeft:
				mEditor.moveByteCursor(-1);
				break;
			case kbRight:
				mEditor.moveByteCursor(1);
				break;
			case kbUp:
				mEditor.moveByteCursor(-static_cast<std::ptrdiff_t>(recordLength));
				break;
			case kbDown:
				mEditor.moveByteCursor(static_cast<std::ptrdiff_t>(recordLength));
				break;
			case kbPgUp:
				mEditor.moveByteCursor(-page);
				break;
			case kbPgDn:
				mEditor.moveByteCursor(page);
				break;
			case kbHome:
				mEditor.selectByte(cursor - cursor % recordLength);
				break;
			case kbEnd:
				mEditor.selectByte(std::min(length, cursor - cursor % recordLength + recordLength) - (length == 0 ? 0 : 1));
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
