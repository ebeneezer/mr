#include "MRBentoBox.hpp"

#include "../MRFrame.hpp"

namespace {

class MRPaneFrame : public MRFrame {
  public:
	explicit MRPaneFrame(const TRect &bounds) noexcept : MRFrame(bounds) {
		eventMask = 0;
	}

	virtual void draw() override {
	}

	virtual void handleEvent(TEvent &) override {
	}
};

}

MRPaneEditWindow::MRPaneEditWindow(const TRect &bounds, const char *title, int number)
	: TWindowInit(&MRPaneEditWindow::initFrame), MREditWindow(bounds, title, number, mr::coprocessor::ExecutionOwnerKind::BentoPane), mPaneSpec() {
	flags = 0;
	state &= static_cast<ushort>(~sfShadow);
	options &= static_cast<ushort>(~(ofTileable | ofTopSelect));
	eventMask = 0;
	layoutPaneChrome();
}

MRPaneEditWindow::~MRPaneEditWindow() {
}

void MRPaneEditWindow::changeBounds(const TRect &bounds) {
	MREditWindow::changeBounds(bounds);
	layoutPaneChrome();
}

void MRPaneEditWindow::draw() {
	MREditWindow::draw();
}

void MRPaneEditWindow::handleEvent(TEvent &event) {
	MRFileEditor *committedEditor = event.what == evBroadcast && event.message.command == cmMrEditorDocumentCommitted
	                                    ? static_cast<MRFileEditor *>(event.message.infoPtr)
	                                    : nullptr;
	const bool relaySourceCommit = mPaneSpec.role == bprSplitEditor && mPaneSpec.bufferPolicy == bpbSharedSourceBuffer &&
	                               committedEditor != nullptr && committedEditor == getEditor();

	MREditWindow::handleEvent(event);
	if (relaySourceCommit)
		if (MRBentoBox *bento = dynamic_cast<MRBentoBox *>(owner)) bento->handleCommittedSourceEditor(committedEditor);
}

TColorAttr MRPaneEditWindow::mapColor(uchar index) {
	if (index == 4 || index == 5 || index == 14)
		if (MRBentoBox *bento = dynamic_cast<MRBentoBox *>(owner)) return bento->mapColor(index);
	return MREditWindow::mapColor(index);
}

Boolean MRPaneEditWindow::valid(ushort command) {
	if (command == cmClose) return True;
	return MREditWindow::valid(command);
}

void MRPaneEditWindow::cancelTransientInput() noexcept {
}

bool MRPaneEditWindow::completeTransientInput() noexcept {
	cancelTransientInput();
	return true;
}

bool MRPaneEditWindow::usesNativeEditorChrome() const noexcept {
	return true;
}

bool MRPaneEditWindow::ownsPaneWheelEvents() const noexcept {
	return false;
}

bool MRPaneEditWindow::projectsPaneContentLocally() const noexcept {
	return false;
}

void MRPaneEditWindow::setPaneSpec(const MRBentoPaneSpec &spec, const MRFileEditor *sourceEditor) noexcept {
	const bool sameSpec = mPaneSpec.role == spec.role && mPaneSpec.bufferPolicy == spec.bufferPolicy && mPaneSpec.readOnly == spec.readOnly && mPaneSpec.widgetMask == spec.widgetMask && mPaneSpec.suppressMiniMap == spec.suppressMiniMap && mPaneSpec.suppressWordWrap == spec.suppressWordWrap && mPaneSpec.scrollBarsAlwaysVisible == spec.scrollBarsAlwaysVisible && mPaneSpec.titleMenu == spec.titleMenu;
	const MRBentoPaneBufferPolicy oldBufferPolicy = mPaneSpec.bufferPolicy;
	mPaneSpec = spec;
	if (oldBufferPolicy == bpbSharedSourceBuffer && spec.bufferPolicy == bpbOwnBuffer && getEditor() != nullptr) getEditor()->detachContentStateCopy();
	if (usesNativeEditorChrome()) applyPanePolicy(spec.bufferPolicy == bpbSharedSourceBuffer ? sourceEditor : nullptr);
	else setReadOnly(spec.readOnly);
	if (sameSpec) return;
	layoutPaneChrome();
}

void MRPaneEditWindow::applyPanePolicy(const MRFileEditor *sourceEditor) noexcept {
	MRFileEditor *paneEditor = getEditor();

	if (paneEditor == nullptr) return;
	if (mPaneSpec.bufferPolicy == bpbSharedSourceBuffer && sourceEditor != nullptr) paneEditor->shareContentStateFrom(*sourceEditor);
	setReadOnly(mPaneSpec.readOnly);
	paneEditor->setCommunicationViewerMode(mPaneSpec.readOnly && mPaneSpec.role != bprDiffOriginal && mPaneSpec.role != bprDiffCompare, mPaneSpec.role != bprProblems);
	paneEditor->setMiniMapSuppressed(mPaneSpec.suppressMiniMap);
	paneEditor->setWordWrapSuppressed(mPaneSpec.suppressWordWrap);
	paneEditor->setScrollBarsAlwaysVisible(mPaneSpec.scrollBarsAlwaysVisible);
}

void MRPaneEditWindow::layoutPaneChrome() noexcept {
	if (!usesNativeEditorChrome()) return;
	if (frame != nullptr) frame->hide();
	if (editorIndicator() != nullptr) editorIndicator()->hide();
	if (MRFileEditor *paneEditor = getEditor()) {
		applyPanePolicy(nullptr);
		paneEditor->updateMetrics();
	}
}

TFrame *MRPaneEditWindow::initFrame(TRect bounds) {
	return new MRPaneFrame(bounds);
}
