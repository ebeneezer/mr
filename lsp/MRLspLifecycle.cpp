#include "MRLspLifecycle.hpp"

namespace mr::lsp {

LspLifecycle::~LspLifecycle() {
	close();
}

bool LspLifecycle::start(const LspInitializeSpec &spec, std::string &errorMessage) {
	JsonRpcPendingRequest request;

	if (lifecycleState != LspLifecycleState::Stopped && lifecycleState != LspLifecycleState::Shutdown)
		return fail(errorMessage, "LSP lifecycle is not stopped.");
	initializeRequestId.clear();
	shutdownRequestId.clear();
	if (!session.start(spec.session, errorMessage)) {
		lifecycleState = LspLifecycleState::Failed;
		return false;
	}
	if (!session.sendRequest("initialize", spec.initializeParamsJson, request, errorMessage)) {
		session.close();
		lifecycleState = LspLifecycleState::Failed;
		return false;
	}
	initializeRequestId = request.idText;
	lifecycleState = LspLifecycleState::Starting;
	errorMessage.clear();
	return true;
}

bool LspLifecycle::poll(std::vector<LspInboundMessage> &messages, std::string &errorMessage) {
	if (!session.poll(messages, errorMessage)) {
		lifecycleState = LspLifecycleState::Failed;
		return false;
	}
	for (const LspInboundMessage &message : messages)
		applyInboundMessage(message);
	errorMessage.clear();
	return true;
}

bool LspLifecycle::sendInitialized(std::string &errorMessage) {
	if (lifecycleState != LspLifecycleState::Initialized)
		return fail(errorMessage, "LSP lifecycle is not initialized.");
	return session.sendNotification("initialized", "{}", errorMessage);
}

bool LspLifecycle::sendInitializedPayload(std::string_view payload, std::string &errorMessage) {
	if (lifecycleState != LspLifecycleState::Initialized)
		return fail(errorMessage, "LSP lifecycle is not initialized.");
	return session.sendRawPayload(payload, errorMessage);
}

bool LspLifecycle::shutdown(std::string &errorMessage) {
	JsonRpcPendingRequest request;

	if (lifecycleState != LspLifecycleState::Initialized)
		return fail(errorMessage, "LSP lifecycle is not initialized.");
	if (!session.sendRequest("shutdown", "null", request, errorMessage)) {
		lifecycleState = LspLifecycleState::Failed;
		return false;
	}
	shutdownRequestId = request.idText;
	lifecycleState = LspLifecycleState::ShuttingDown;
	return true;
}

bool LspLifecycle::exit(std::string &errorMessage) {
	if (lifecycleState != LspLifecycleState::Shutdown)
		return fail(errorMessage, "LSP lifecycle is not shutdown.");
	return session.sendNotification("exit", "null", errorMessage);
}

void LspLifecycle::requestStop() {
	session.requestStop();
}

bool LspLifecycle::wait(int timeoutMs, int &exitStatus) {
	return session.wait(timeoutMs, exitStatus);
}

void LspLifecycle::close() {
	session.close();
	lifecycleState = LspLifecycleState::Stopped;
	initializeRequestId.clear();
	shutdownRequestId.clear();
}

LspLifecycleState LspLifecycle::state() const noexcept {
	return lifecycleState;
}

bool LspLifecycle::running() const noexcept {
	return session.running();
}

bool LspLifecycle::fail(std::string &errorMessage, const std::string &message) {
	errorMessage = message;
	lifecycleState = LspLifecycleState::Failed;
	return false;
}

void LspLifecycle::applyInboundMessage(const LspInboundMessage &message) {
	if (!message.matchedPendingRequest) return;
	if (lifecycleState == LspLifecycleState::Starting && message.pendingRequest.idText == initializeRequestId && message.pendingRequest.method == "initialize") {
		lifecycleState = LspLifecycleState::Initialized;
		return;
	}
	if (lifecycleState == LspLifecycleState::ShuttingDown && message.pendingRequest.idText == shutdownRequestId && message.pendingRequest.method == "shutdown")
		lifecycleState = LspLifecycleState::Shutdown;
}

const char *lspLifecycleStateName(LspLifecycleState state) noexcept {
	switch (state) {
	case LspLifecycleState::Stopped:
		return "Stopped";
	case LspLifecycleState::Starting:
		return "Starting";
	case LspLifecycleState::Initialized:
		return "Initialized";
	case LspLifecycleState::ShuttingDown:
		return "ShuttingDown";
	case LspLifecycleState::Shutdown:
		return "Shutdown";
	case LspLifecycleState::Failed:
		return "Failed";
	}
	return "Unknown";
}

} // namespace mr::lsp
