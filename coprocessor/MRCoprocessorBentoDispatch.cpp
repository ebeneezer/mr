#define Uses_TGroup
#include <tvision/tv.h>

#include "MRCoprocessorBentoDispatch.hpp"

#include <string>

#include "MRPerformance.hpp"
#include "../app/commands/MRWindowCommands.hpp"
#include "../ui/MRBentoBox/MRBentoBox.hpp"
#include "../ui/MREditWindow.hpp"
#include "../ui/MRWindowSupport.hpp"

namespace {
const char *projectionAction(mr::coprocessor::TaskKind kind) noexcept {
	switch (kind) {
		case mr::coprocessor::TaskKind::BentoDiagnosticsProjection:
			return "Bento diagnostics projection";
		case mr::coprocessor::TaskKind::BentoOutlineProjection:
			return "Bento outline projection";
		default:
			return "Bento projection";
	}
}

mr::performance::Outcome projectionOutcome(const mr::coprocessor::Result &result) noexcept {
	switch (result.status) {
		case mr::coprocessor::TaskStatus::Failed:
			return mr::performance::Outcome::Failed;
		case mr::coprocessor::TaskStatus::Cancelled:
			return mr::performance::Outcome::Cancelled;
		case mr::coprocessor::TaskStatus::Completed:
		default:
			return mr::performance::Outcome::Completed;
	}
}

MRBentoBox *projectionOwner(MREditWindow *targetPane) noexcept {
	if (targetPane == nullptr) return nullptr;
	MRBentoBox *box = dynamic_cast<MRBentoBox *>(targetPane);
	if (box != nullptr) return box;
	return dynamic_cast<MRBentoBox *>(targetPane->owner);
}
} // namespace

void mrDispatchBentoProjectionResult(const mr::coprocessor::Result &result) {
	MREditWindow *targetPane = nullptr;
	MRBentoBox *box = nullptr;
	const MRBentoDiagnosticsProjectionPayload *diagnosticsPayload = nullptr;
	const MRBentoOutlineProjectionPayload *outlinePayload = nullptr;
	bool adopted = false;
	std::size_t bytes = 0;

	if (result.task.executionOwnerKind == mr::coprocessor::ExecutionOwnerKind::BentoPane) {
		targetPane = findEditWindowByBufferId(static_cast<int>(result.task.executionOwnerLocalId));
		box = projectionOwner(targetPane);
	}
	if (box != nullptr) adopted = box->applyBentoProjectionResult(result);
	if (result.completed()) {
		diagnosticsPayload = dynamic_cast<const MRBentoDiagnosticsProjectionPayload *>(result.payload.get());
		outlinePayload = dynamic_cast<const MRBentoOutlineProjectionPayload *>(result.payload.get());
		if (diagnosticsPayload != nullptr && diagnosticsPayload->projectionText != nullptr)
			bytes = diagnosticsPayload->projectionText->size();
		else if (outlinePayload != nullptr && outlinePayload->projectionText != nullptr)
			bytes = outlinePayload->projectionText->size();
	}

	mr::performance::recordBackgroundEvent(result.task.lane, projectionOutcome(result), result.timing, projectionAction(result.task.kind),
	                                       result.task.executionOwnerLocalId, result.task.documentId, bytes,
	                                       result.task.label, adopted);
	mr::coprocessor::globalCoprocessor().noteResultAdoption(result, adopted);
	if (result.failed()) {
		const std::string message = std::string(projectionAction(result.task.kind)) + " failed: " + result.error;
		mrLogMessage(message);
	}
}
