#include "MRBentoHexEditor.hpp"

#include "panes/MRHexPaneProjection.hpp"
#include "panes/MRHexPaneWindow.hpp"

#include "../MRFileEditor/MRFileEditor.hpp"
#include "../MRMessageLineController.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace {

constexpr int kMainColumnsNode = 0;
constexpr int kRemainingColumnsNode = 2;

struct HexPaneDescriptor {
	MRBentoPaneRole bentoRole;
	MRHexPaneRole hexRole;
	const char *title;
	bool readOnly;
};

constexpr HexPaneDescriptor kHexPaneDescriptors[] = {
	{static_cast<MRBentoPaneRole>(bprExtensionFirst + 0), MRHexPaneRole::Hex, "HEX", false},
	{static_cast<MRBentoPaneRole>(bprExtensionFirst + 1), MRHexPaneRole::Strings, "Strings", false},
	{static_cast<MRBentoPaneRole>(bprExtensionFirst + 2), MRHexPaneRole::Inspector, "Inspector", true},
	{static_cast<MRBentoPaneRole>(bprExtensionFirst + 3), MRHexPaneRole::Decimal, "Decimal", false},
	{static_cast<MRBentoPaneRole>(bprExtensionFirst + 4), MRHexPaneRole::Binary, "Binary", false},
	{static_cast<MRBentoPaneRole>(bprExtensionFirst + 5), MRHexPaneRole::Octal, "Octal", false},
};

constexpr MRHexPaneRole kHexInputPaneOrder[] = {
	MRHexPaneRole::Hex,
	MRHexPaneRole::Decimal,
	MRHexPaneRole::Strings,
	MRHexPaneRole::Binary,
	MRHexPaneRole::Octal,
};

const HexPaneDescriptor *hexPaneDescriptorForBentoRole(MRBentoPaneRole role) noexcept {
	for (const HexPaneDescriptor &descriptor : kHexPaneDescriptors)
		if (descriptor.bentoRole == role) return &descriptor;
	return nullptr;
}

const HexPaneDescriptor *hexPaneDescriptorForHexRole(MRHexPaneRole role) noexcept {
	for (const HexPaneDescriptor &descriptor : kHexPaneDescriptors)
		if (descriptor.hexRole == role) return &descriptor;
	return nullptr;
}

MRBentoWorkspaceLeaf hexWorkspaceLeaf(int id, MRBentoPaneRole role) {
	MRBentoWorkspaceLeaf leaf;

	leaf.id = id;
	leaf.role = role;
	leaf.visible = true;
	return leaf;
}

MRBentoWorkspaceNode hexWorkspacePaneNode(int leafId) {
	MRBentoWorkspaceNode node;

	node.kind = 0;
	node.orientation = 0;
	node.leafId = leafId;
	return node;
}

MRBentoWorkspaceNode hexWorkspaceSplitNode(int orientation, int firstChild, int secondChild) {
	MRBentoWorkspaceNode node;

	node.kind = 1;
	node.orientation = orientation;
	node.dividerPosition = 0;
	node.firstChild = firstChild;
	node.secondChild = secondChild;
	return node;
}

MRBentoWorkspaceSnapshot initialHexWorkspaceSnapshot() {
	MRBentoWorkspaceSnapshot snapshot;

	snapshot.mode = bbmToolWorkspace;
	snapshot.rootNode = 0;
	snapshot.activeLeafId = 0;
	snapshot.maximizedLeafId = -1;
	snapshot.nodes.push_back(hexWorkspaceSplitNode(1, 1, 2));
	snapshot.nodes.push_back(hexWorkspaceSplitNode(0, 3, 4));
	snapshot.nodes.push_back(hexWorkspaceSplitNode(1, 5, 6));
	snapshot.nodes.push_back(hexWorkspacePaneNode(0));
	snapshot.nodes.push_back(hexWorkspacePaneNode(3));
	snapshot.nodes.push_back(hexWorkspaceSplitNode(0, 7, 8));
	snapshot.nodes.push_back(hexWorkspaceSplitNode(0, 9, 10));
	snapshot.nodes.push_back(hexWorkspacePaneNode(1));
	snapshot.nodes.push_back(hexWorkspacePaneNode(4));
	snapshot.nodes.push_back(hexWorkspacePaneNode(2));
	snapshot.nodes.push_back(hexWorkspacePaneNode(5));
	for (std::size_t index = 0; index < sizeof(kHexPaneDescriptors) / sizeof(kHexPaneDescriptors[0]); ++index)
		snapshot.leaves.push_back(hexWorkspaceLeaf(static_cast<int>(index), kHexPaneDescriptors[index].bentoRole));
	return snapshot;
}

} // namespace

MRBentoHexEditor::MRBentoHexEditor(const TRect &bounds, const char *title, int number)
	: TWindowInit(&MRBentoBox::initFrame), MRBentoBox(bounds, title, number), mByteCursor(0), mCursorProjectionRevision(1), mViewportCursorProjectionRevision(0), mDataFirstRecord(0),
	  mDataFirstColumn(0), mLittleEndian(true), mApplyingHexEdit(false), mActiveRole(MRHexPaneRole::Hex) {
	if (MRFileEditor *editor = getEditor(); editor != nullptr) editor->setForceBinarySave(true);
	static_cast<void>(restoreWorkspaceSnapshot(initialHexWorkspaceSnapshot()));
	refreshHexProjection();
}

bool MRBentoHexEditor::matchesWorkspaceSnapshot(const MRBentoWorkspaceSnapshot &snapshot) noexcept {
	if (snapshot.leaves.size() != sizeof(kHexPaneDescriptors) / sizeof(kHexPaneDescriptors[0])) return false;
	for (std::size_t index = 0; index < snapshot.leaves.size(); ++index) {
		const MRBentoWorkspaceLeaf &leaf = snapshot.leaves[index];

		if (leaf.id != static_cast<int>(index) || leaf.role != kHexPaneDescriptors[index].bentoRole) return false;
	}
	return true;
}

void MRBentoHexEditor::synchronizePaneDocumentState() {
	const MRBentoWorkspaceSnapshot snapshot = workspaceSnapshot();

	if (!restoreWorkspaceSnapshot(snapshot)) return;
	synchronizeByteCursorFromDocument();
}

void MRBentoHexEditor::synchronizeByteCursorFromDocument() noexcept {
	const std::size_t nextCursor = getEditor() != nullptr ? std::min(getEditor()->cursorOffset(), byteSnapshot().length()) : 0;

	if (mByteCursor != nextCursor) {
		mByteCursor = nextCursor;
		noteByteCursorChanged();
	}
	mViewportCursorProjectionRevision = 0;
	static_cast<void>(setDataViewport(mDataFirstRecord, mDataFirstColumn));
	refreshHexProjection();
}

void MRBentoHexEditor::refreshAfterDocumentCommit() noexcept {
	const std::size_t length = byteSnapshot().length();
	const std::size_t nextCursor = std::min(mByteCursor, length);

	if (mByteCursor != nextCursor) {
		mByteCursor = nextCursor;
		noteByteCursorChanged();
	}
	static_cast<void>(setDataViewport(mDataFirstRecord, mDataFirstColumn));
	refreshHexProjection();
}

void MRBentoHexEditor::refreshHexProjection() noexcept {
	static_cast<void>(synchronizeDataViewportToCursor());
	for (const HexPaneDescriptor &descriptor : kHexPaneDescriptors) {
		MRHexPaneWindow *pane = dynamic_cast<MRHexPaneWindow *>(paneWindowForRole(descriptor.bentoRole));

		if (pane != nullptr) pane->requestHexProjection();
	}
}

void MRBentoHexEditor::handleEvent(TEvent &event) {
	if (event.what == evBroadcast && event.message.command == cmMrEditorDocumentCommitted) {
		if (!mApplyingHexEdit) refreshAfterDocumentCommit();
		clearEvent(event);
		return;
	}
	const bool nextInputPane = event.what == evKeyDown && TKey(event.keyDown.keyCode, event.keyDown.controlKeyState) == TKey(kbTab);
	const bool previousInputPane = event.what == evKeyDown && TKey(event.keyDown.keyCode, event.keyDown.controlKeyState) == TKey(kbShiftTab);
	if (nextInputPane || previousInputPane) {
		const MRHexPaneRole previousRole = mActiveRole;

		activateAdjacentInputPane(previousInputPane ? -1 : 1);
		if (mActiveRole != previousRole) refreshPaneChromeProjection();
		clearEvent(event);
		return;
	}
	MRBentoBox::handleEvent(event);
}

MRPaneEditWindow *MRBentoHexEditor::createPaneWindow(const TRect &bounds, const char *title, int number, const MRBentoPaneSpec &spec) {
	const HexPaneDescriptor *descriptor = hexPaneDescriptorForBentoRole(spec.role);

	if (descriptor == nullptr) return nullptr;
	return new MRHexPaneWindow(bounds, title, number, *this, descriptor->hexRole);
}

bool MRBentoHexEditor::primaryPaneUsesDedicatedWindow() const noexcept {
	return true;
}

bool MRBentoHexEditor::acceptsPaneRole(MRBentoPaneRole role) const noexcept {
	return hexPaneDescriptorForBentoRole(role) != nullptr;
}

const char *MRBentoHexEditor::titleForPaneRole(MRBentoPaneRole role) const noexcept {
	const HexPaneDescriptor *descriptor = hexPaneDescriptorForBentoRole(role);

	return descriptor != nullptr ? descriptor->title : MRBentoBox::titleForPaneRole(role);
}

MRBentoPaneSpec MRBentoHexEditor::paneSpecForRole(MRBentoPaneRole role) const noexcept {
	const HexPaneDescriptor *descriptor = hexPaneDescriptorForBentoRole(role);

	if (descriptor == nullptr) return MRBentoBox::paneSpecForRole(role);
	return MRBentoPaneSpec(role, bpbSharedSourceBuffer, descriptor->readOnly, true, true, false, nullptr);
}

bool MRBentoHexEditor::paneCloseActionEnabled() const noexcept {
	return false;
}

bool MRBentoHexEditor::paneMaximizeActionEnabled() const noexcept {
	return true;
}

bool MRBentoHexEditor::projectPaneDividerPosition(int nodeIndex, int position) noexcept {
	bool changed = setPaneDividerPositionForLayout(nodeIndex, position);

	if (nodeIndex != kMainColumnsNode) return changed;
	const TRect bounds = paneLayoutBounds();
	const int mainDivider = paneDividerPosition(kMainColumnsNode);
	const int remainingDivider = mainDivider + (bounds.b.x - mainDivider) / 2;

	return setPaneDividerPositionForLayout(kRemainingColumnsNode, remainingDivider) || changed;
}

void MRBentoHexEditor::paneLayoutChanged() noexcept {
	mViewportCursorProjectionRevision = 0;
	refreshHexProjection();
}

void MRBentoHexEditor::activePaneRoleChanged(MRBentoPaneRole role) noexcept {
	const HexPaneDescriptor *descriptor = hexPaneDescriptorForBentoRole(role);

	if (descriptor == nullptr) return;
	const bool roleChanged = mActiveRole != descriptor->hexRole;
	mActiveRole = descriptor->hexRole;
	if (descriptor->hexRole != MRHexPaneRole::Strings) mr::messageline::clearOwner(mr::messageline::Owner::HexEditor);
	if (!roleChanged) return;
	mViewportCursorProjectionRevision = 0;
	refreshHexProjection();
	refreshHexFocusProjection();
}

TColorAttr MRBentoHexEditor::mapColor(uchar index) {
	if (index == 1 || index == 13) return MREditWindow::mapColor(13);
	return MRBentoBox::mapColor(index);
}

MREditWindow *MRBentoHexEditor::editorCommandTarget() noexcept {
	return this;
}

const MREditWindow *MRBentoHexEditor::editorCommandTarget() const noexcept {
	return this;
}

MRTextBufferModel::ReadSnapshot MRBentoHexEditor::byteSnapshot() const {
	MRFileEditor *editor = getEditor();

	return editor != nullptr ? editor->readSnapshot() : MRTextBufferModel::ReadSnapshot();
}

std::size_t MRBentoHexEditor::byteCursor() const noexcept {
	return mByteCursor;
}

std::size_t MRBentoHexEditor::cursorProjectionRevision() const noexcept {
	return mCursorProjectionRevision;
}

bool MRBentoHexEditor::inputPaneIsActive(MRHexPaneRole role) const noexcept {
	return role != MRHexPaneRole::Inspector && role == mActiveRole;
}

std::size_t MRBentoHexEditor::dataFirstRecord() const noexcept {
	return mDataFirstRecord;
}

std::size_t MRBentoHexEditor::dataFirstColumn() const noexcept {
	return mDataFirstColumn;
}

int MRBentoHexEditor::recordLength() const {
	MREditSetupSettings settings = configuredEditSetupSettings();
	const char *path = currentFileName();

	if (path != nullptr && *path != '\0') static_cast<void>(effectiveEditSetupSettingsForPath(path, settings));
	return std::max(1, settings.binaryRecordLength);
}

bool MRBentoHexEditor::littleEndian() const noexcept {
	return mLittleEndian;
}

bool MRBentoHexEditor::setDataViewport(std::size_t firstRecord, std::size_t firstColumn) noexcept {
	const MRTextBufferModel::ReadSnapshot snapshot = byteSnapshot();
	const std::size_t normalizedRecordLength = static_cast<std::size_t>(std::max(1, recordLength()));
	const std::size_t nextFirstRecord = std::min(firstRecord, snapshot.length() / normalizedRecordLength);
	const std::size_t nextFirstColumn = std::min(firstColumn, normalizedRecordLength - 1);

	if (mDataFirstRecord == nextFirstRecord && mDataFirstColumn == nextFirstColumn) return false;
	mDataFirstRecord = nextFirstRecord;
	mDataFirstColumn = nextFirstColumn;
	return true;
}

void MRBentoHexEditor::toggleEndian() {
	mLittleEndian = !mLittleEndian;
	refreshHexProjection();
}

void MRBentoHexEditor::toggleInsertMode() {
	if (MRFileEditor *editor = getEditor(); editor != nullptr) editor->setInsertModeEnabled(!editor->insertModeEnabled());
}

void MRBentoHexEditor::selectByte(std::size_t offset) noexcept {
	const std::size_t length = byteSnapshot().length();
	const std::size_t nextCursor = std::min(offset, length);
	const std::size_t previousCursor = mByteCursor;

	if (mByteCursor == nextCursor) return;
	mByteCursor = nextCursor;
	noteByteCursorChanged();
	refreshHexCursorTransition(previousCursor);
}

void MRBentoHexEditor::selectRecordColumn(std::size_t record, std::size_t column) noexcept {
	const std::size_t length = byteSnapshot().length();
	const std::size_t normalizedRecordLength = static_cast<std::size_t>(std::max(1, recordLength()));
	const std::size_t lastSelectableRecord = length / normalizedRecordLength;

	if (record > lastSelectableRecord || record > std::numeric_limits<std::size_t>::max() / normalizedRecordLength) {
		selectByte(length);
		return;
	}
	const std::size_t recordStart = record * normalizedRecordLength;
	if (recordStart > length) {
		selectByte(length);
		return;
	}
	const std::size_t available = std::min(normalizedRecordLength, length - recordStart);
	selectByte(recordStart + std::min(column, available));
}

void MRBentoHexEditor::moveByteCursor(std::ptrdiff_t delta) noexcept {
	const std::size_t length = byteSnapshot().length();
	const std::size_t previousCursor = mByteCursor;

	if (delta < 0) {
		const std::size_t magnitude = static_cast<std::size_t>(-(delta + 1)) + 1;
		mByteCursor = magnitude > mByteCursor ? 0 : mByteCursor - magnitude;
	} else {
		const std::size_t magnitude = static_cast<std::size_t>(delta);
		mByteCursor = magnitude > length - mByteCursor ? length : mByteCursor + magnitude;
	}
	if (mByteCursor == previousCursor) return;
	noteByteCursorChanged();
	refreshHexCursorTransition(previousCursor);
}

bool MRBentoHexEditor::replaceBytes(std::size_t offset, const std::string &bytes, std::size_t overwriteLength) {
	MRFileEditor *editor = getEditor();
	const std::size_t length = byteSnapshot().length();
	bool replaced = false;

	if (editor == nullptr || isReadOnly() || bytes.empty()) return false;
	offset = std::min(offset, length);
	if (bytes.size() > std::numeric_limits<std::size_t>::max() - offset) return false;
	const std::size_t end = editor->insertModeEnabled() ? offset : (overwriteLength > length - offset ? length : offset + overwriteLength);
	mApplyingHexEdit = true;
	try {
		replaced = editor->replaceRangeAndSelect(offset, end, bytes.data(), bytes.size());
	} catch (...) {
		mApplyingHexEdit = false;
		throw;
	}
	mApplyingHexEdit = false;
	if (!replaced) return false;
	mByteCursor = offset + bytes.size();
	noteByteCursorChanged();
	return true;
}

void MRBentoHexEditor::activateAdjacentInputPane(int direction) noexcept {
	const int paneCount = static_cast<int>(sizeof(kHexInputPaneOrder) / sizeof(kHexInputPaneOrder[0]));
	int currentIndex = direction < 0 ? 0 : -1;

	for (int index = 0; index < paneCount; ++index)
		if (kHexInputPaneOrder[index] == mActiveRole) currentIndex = index;
	for (int step = 1; step <= paneCount; ++step) {
		const int candidateIndex = (currentIndex + paneCount + direction * step) % paneCount;
		const HexPaneDescriptor *descriptor = hexPaneDescriptorForHexRole(kHexInputPaneOrder[candidateIndex]);
		MRPaneEditWindow *pane = descriptor != nullptr ? paneWindowForRole(descriptor->bentoRole) : nullptr;

		if (pane != nullptr && activatePaneWindow(pane)) return;
	}
}

void MRBentoHexEditor::noteByteCursorChanged() noexcept {
	++mCursorProjectionRevision;
	if (mCursorProjectionRevision == 0) ++mCursorProjectionRevision;
}

void MRBentoHexEditor::refreshHexCursorTransition(std::size_t previousCursor) noexcept {
	const bool viewportChanged = synchronizeDataViewportToCursor();

	for (const HexPaneDescriptor &descriptor : kHexPaneDescriptors) {
		MRHexPaneWindow *pane = dynamic_cast<MRHexPaneWindow *>(paneWindowForRole(descriptor.bentoRole));

		if (pane == nullptr) continue;
		pane->refreshHexCursor(previousCursor, mByteCursor, viewportChanged);
	}
}

void MRBentoHexEditor::refreshHexFocusProjection() noexcept {
	for (const HexPaneDescriptor &descriptor : kHexPaneDescriptors) {
		MRHexPaneWindow *pane = dynamic_cast<MRHexPaneWindow *>(paneWindowForRole(descriptor.bentoRole));

		if (pane != nullptr) pane->refreshHexFocus();
	}
}

bool MRBentoHexEditor::synchronizeDataViewportToCursor() noexcept {
	if (mViewportCursorProjectionRevision == mCursorProjectionRevision) return false;
	mViewportCursorProjectionRevision = mCursorProjectionRevision;
	const MRTextBufferModel::ReadSnapshot snapshot = byteSnapshot();
	const std::size_t length = snapshot.length();
	const std::size_t normalizedRecordLength = static_cast<std::size_t>(std::max(1, recordLength()));
	const std::size_t cursor = std::min(mByteCursor, length);
	const std::size_t cursorRecord = cursor / normalizedRecordLength;
	const std::size_t cursorColumn = cursor % normalizedRecordLength;
	int activeRows = 0;
	int activeColumns = 0;

	for (const HexPaneDescriptor &descriptor : kHexPaneDescriptors) {
		if (descriptor.hexRole != mActiveRole || descriptor.hexRole == MRHexPaneRole::Inspector) continue;
		const MRHexPaneWindow *pane = dynamic_cast<const MRHexPaneWindow *>(paneWindowForRole(descriptor.bentoRole));

		if (pane != nullptr) {
			activeRows = pane->hexViewportRowCapacity();
			activeColumns = pane->hexViewportColumnCapacity();
		}
		break;
	}
	std::size_t firstRecord = mDataFirstRecord;
	std::size_t firstColumn = mDataFirstColumn;
	if (activeRows > 0) {
		const std::size_t visibleRows = static_cast<std::size_t>(activeRows);

		if (cursorRecord < firstRecord) firstRecord = cursorRecord;
		else if (cursorRecord - firstRecord >= visibleRows)
			firstRecord = cursorRecord - visibleRows + 1;
	}
	if (activeColumns > 0) {
		const std::size_t visibleColumns = static_cast<std::size_t>(activeColumns);

		if (cursorColumn < firstColumn) firstColumn = cursorColumn;
		else if (cursorColumn - firstColumn >= visibleColumns)
			firstColumn = cursorColumn - visibleColumns + 1;
	}
	return setDataViewport(firstRecord, firstColumn);
}
