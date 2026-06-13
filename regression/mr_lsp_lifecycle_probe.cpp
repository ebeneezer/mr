#include <iostream>
#include <poll.h>
#include <string>
#include <vector>

#include "../lsp/MRLspLifecycle.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

bool pollUntilState(mr::lsp::LspLifecycle &lifecycle, mr::lsp::LspLifecycleState expectedState, std::string &failureReason) {
	std::string errorMessage;
	std::vector<mr::lsp::LspInboundMessage> messages;

	for (int i = 0; i < 50; ++i) {
		if (!lifecycle.poll(messages, errorMessage)) {
			failureReason = "poll failed: " + errorMessage;
			return false;
		}
		if (lifecycle.state() == expectedState) return true;
		::poll(nullptr, 0, 20);
	}
	failureReason = std::string("expected lifecycle state not observed: ") + mr::lsp::lspLifecycleStateName(expectedState);
	return false;
}

bool testLifecycleHappyPath(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspInitializeSpec spec;
	std::string errorMessage;
	int exitStatus = -1;

	spec.session.process.executablePath = "./regression/mr_lsp_session_peer";
	spec.initializeParamsJson = "{\"processId\":null,\"rootUri\":null,\"capabilities\":{}}";
	if (!expect(lifecycle.start(spec, errorMessage), "lifecycle start: " + errorMessage, failureReason)) return false;
	if (!expect(lifecycle.state() == mr::lsp::LspLifecycleState::Starting, "lifecycle starting", failureReason)) return false;
	if (!pollUntilState(lifecycle, mr::lsp::LspLifecycleState::Initialized, failureReason)) return false;
	if (!expect(lifecycle.sendInitialized(errorMessage), "initialized send: " + errorMessage, failureReason)) return false;
	if (!expect(lifecycle.shutdown(errorMessage), "shutdown send: " + errorMessage, failureReason)) return false;
	if (!expect(lifecycle.state() == mr::lsp::LspLifecycleState::ShuttingDown, "lifecycle shutting down", failureReason)) return false;
	if (!pollUntilState(lifecycle, mr::lsp::LspLifecycleState::Shutdown, failureReason)) return false;
	if (!expect(lifecycle.exit(errorMessage), "exit send: " + errorMessage, failureReason)) return false;
	if (!expect(lifecycle.wait(1000, exitStatus), "lifecycle wait", failureReason)) return false;
	return expect(exitStatus == 0, "lifecycle exit status", failureReason);
}

bool testLifecycleGuards(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	std::string errorMessage;

	if (!expect(!lifecycle.sendInitialized(errorMessage), "initialized before start accepted", failureReason)) return false;
	if (!expect(lifecycle.state() == mr::lsp::LspLifecycleState::Failed, "guard failure state", failureReason)) return false;
	lifecycle.close();
	if (!expect(!lifecycle.shutdown(errorMessage), "shutdown before start accepted", failureReason)) return false;
	if (!expect(lifecycle.state() == mr::lsp::LspLifecycleState::Failed, "shutdown guard failure state", failureReason)) return false;
	return true;
}

bool testLifecycleInvalidPath(std::string &failureReason) {
	mr::lsp::LspLifecycle lifecycle;
	mr::lsp::LspInitializeSpec spec;
	std::string errorMessage;

	spec.session.process.executablePath = "./regression/not-an-lsp-peer";
	if (!expect(!lifecycle.start(spec, errorMessage), "invalid path accepted", failureReason)) return false;
	return expect(lifecycle.state() == mr::lsp::LspLifecycleState::Failed, "invalid path failed state", failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testLifecycleHappyPath(failureReason)) return false;
	if (!testLifecycleGuards(failureReason)) return false;
	if (!testLifecycleInvalidPath(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_lifecycle_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_lifecycle_probe passed\n";
	return 0;
}
