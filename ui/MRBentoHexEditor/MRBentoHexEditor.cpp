#include "MRBentoHexEditor.hpp"

#include "panes/MRHexPaneWindow.hpp"

#include "../MRFileEditor/MRFileEditor.hpp"
#include "../MRMessageLineController.hpp"

#include <algorithm>
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

const HexPaneDescriptor *hexPaneDescriptorForBentoRole(MRBentoPaneRole role) noexcept {
	for (const HexPaneDescriptor &descriptor : kHexPaneDescriptors)
		if (descriptor.bentoRole == role) return &descriptor;
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
	: TWindowInit(&MRBentoBox::initFrame), MRBentoBox(bounds, title, number), mByteCursor(0), mCursorProjectionRevision(1), mLittleEndian(true), mActiveRole(MRHexPaneRole::Hex) {
	if (MRFileEditor *editor = getEditor(); editor != nullptr) editor->setForceBinarySave(true);
	static_cast<void>(restoreWorkspaceSnapshot(initialHexWorkspaceSnapshot()));
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
	refreshHexProjection();
}

void MRBentoHexEditor::synchronizeByteCursorFromDocument() noexcept {
	mByteCursor = getEditor() != nullptr ? std::min(getEditor()->cursorOffset(), byteSnapshot().length()) : 0;
	noteByteCursorChanged();
}

void MRBentoHexEditor::refreshHexProjection() noexcept {
	refreshPaneContentProjection();
}

void MRBentoHexEditor::handleEvent(TEvent &event) {
	if (event.what == evKeyDown && event.keyDown.keyCode == kbTab) {
		activateNextInputPane();
		refreshHexProjection();
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

void MRBentoHexEditor::activePaneRoleChanged(MRBentoPaneRole role) noexcept {
	const HexPaneDescriptor *descriptor = hexPaneDescriptorForBentoRole(role);

	if (descriptor == nullptr) return;
	mActiveRole = descriptor->hexRole;
	if (descriptor->hexRole != MRHexPaneRole::Strings) mr::messageline::clearOwner(mr::messageline::Owner::HexEditor);
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

int MRBentoHexEditor::recordLength() const {
	MREditSetupSettings settings = configuredEditSetupSettings();
	const char *path = currentFileName();

	if (path != nullptr && *path != '\0') static_cast<void>(effectiveEditSetupSettingsForPath(path, settings));
	return std::max(1, settings.binaryRecordLength);
}

bool MRBentoHexEditor::littleEndian() const noexcept {
	return mLittleEndian;
}

void MRBentoHexEditor::toggleEndian() {
	mLittleEndian = !mLittleEndian;
}

void MRBentoHexEditor::toggleInsertMode() {
	if (MRFileEditor *editor = getEditor(); editor != nullptr) editor->setInsertModeEnabled(!editor->insertModeEnabled());
}

void MRBentoHexEditor::selectByte(std::size_t offset) noexcept {
	const std::size_t length = byteSnapshot().length();

	mByteCursor = std::min(offset, length);
	noteByteCursorChanged();
	refreshHexProjection();
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
	refreshHexProjection();
}

bool MRBentoHexEditor::replaceBytes(std::size_t offset, const std::string &bytes, std::size_t overwriteLength) {
	MRFileEditor *editor = getEditor();
	const std::size_t length = byteSnapshot().length();

	if (editor == nullptr || isReadOnly() || bytes.empty()) return false;
	offset = std::min(offset, length);
	const std::size_t end = editor->insertModeEnabled() ? offset : std::min(length, offset + overwriteLength);
	if (!editor->replaceRangeAndSelect(static_cast<uint>(offset), static_cast<uint>(end), bytes.data(), static_cast<uint>(bytes.size()))) return false;
	mByteCursor = offset + bytes.size();
	noteByteCursorChanged();
	return true;
}

void MRBentoHexEditor::activateNextInputPane() noexcept {
	for (int attempt = 0; attempt < 6; ++attempt) {
		const MRHexPaneRole previousRole = mActiveRole;

		toggleActivePane();
		if (mActiveRole == previousRole) return;
		if (mActiveRole != MRHexPaneRole::Inspector) return;
	}
}

void MRBentoHexEditor::noteByteCursorChanged() noexcept {
	++mCursorProjectionRevision;
}
