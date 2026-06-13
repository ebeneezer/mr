#include <iostream>
#include <string>
#include <poll.h>

#include "../lsp/MRExternalProcess.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

bool readUntilContains(mr::lsp::ExternalProcessSession &session, const std::string &expected, std::string &collected, std::string &failureReason) {
	std::string errorMessage;

	for (int i = 0; i < 50; ++i) {
		std::string chunk;
		if (!session.readAvailable(chunk, errorMessage)) {
			failureReason = "read failed: " + errorMessage;
			return false;
		}
		collected += chunk;
		if (collected.find(expected) != std::string::npos) return true;
		::poll(nullptr, 0, 20);
	}
	failureReason = "expected process output not observed";
	return false;
}

bool testCatEcho(std::string &failureReason) {
	mr::lsp::ExternalProcessSession session;
	mr::lsp::ExternalProcessSpec spec;
	std::string errorMessage;
	std::string output;
	int exitStatus = -1;

	spec.executablePath = "/bin/cat";
	if (!expect(session.start(spec, errorMessage), "cat start: " + errorMessage, failureReason)) return false;
	if (!expect(session.running(), "cat running", failureReason)) return false;
	if (!expect(session.writeStdin("mr external process probe\n", errorMessage), "cat write: " + errorMessage, failureReason)) return false;
	if (!readUntilContains(session, "mr external process probe\n", output, failureReason)) return false;
	session.requestStop();
	if (!expect(session.wait(1000, exitStatus), "cat wait", failureReason)) return false;
	return expect(!session.running(), "cat stopped", failureReason);
}

bool testExitStatus(std::string &failureReason) {
	mr::lsp::ExternalProcessSession session;
	mr::lsp::ExternalProcessSpec spec;
	std::string errorMessage;
	int exitStatus = -1;

	spec.executablePath = "/bin/true";
	if (!expect(session.start(spec, errorMessage), "true start: " + errorMessage, failureReason)) return false;
	if (!expect(session.wait(1000, exitStatus), "true wait", failureReason)) return false;
	return expect(exitStatus == 0, "true exit status", failureReason);
}

bool testInvalidPath(std::string &failureReason) {
	mr::lsp::ExternalProcessSession session;
	mr::lsp::ExternalProcessSpec spec;
	std::string errorMessage;

	spec.executablePath = "/definitely/not/mr-lsp-process";
	if (!expect(!session.start(spec, errorMessage), "invalid path accepted", failureReason)) return false;
	return expect(!errorMessage.empty(), "invalid path error text", failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testCatEcho(failureReason)) return false;
	if (!testExitStatus(failureReason)) return false;
	if (!testInvalidPath(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_external_process_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_external_process_probe passed\n";
	return 0;
}
