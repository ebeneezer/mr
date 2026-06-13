#include <iostream>
#include <string>

#include "../lsp/MRLspDocumentMirror.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

mr::lsp::LspDocumentSnapshot makeSnapshot(std::int64_t version, const std::string &text) {
	mr::lsp::LspDocumentSnapshot snapshot;

	snapshot.uri = "file:///tmp/mr.cpp";
	snapshot.languageId = "cpp";
	snapshot.version = version;
	snapshot.text = text;
	return snapshot;
}

bool testDidOpen(std::string &failureReason) {
	mr::lsp::LspDocumentMirror mirror;
	mr::lsp::LspDocumentNotification notification;
	std::string errorMessage;

	if (!expect(mirror.open(makeSnapshot(1, "int main() { return 0; }\n"), notification, errorMessage), "open failed: " + errorMessage, failureReason)) return false;
	if (!expect(mirror.isOpen(), "mirror open state", failureReason)) return false;
	if (!expect(mirror.documentUri() == "file:///tmp/mr.cpp", "mirror uri", failureReason)) return false;
	if (!expect(mirror.documentLanguageId() == "cpp", "mirror language id", failureReason)) return false;
	if (!expect(mirror.sentVersion() == 1, "mirror sent version", failureReason)) return false;
	if (!expect(notification.method == "textDocument/didOpen", "open method", failureReason)) return false;
	if (!expect(notification.version == 1, "open notification version", failureReason)) return false;
	if (!expect(notification.payload.find("\"method\":\"textDocument/didOpen\"") != std::string::npos, "open payload method", failureReason)) return false;
	if (!expect(notification.payload.find("\"languageId\":\"cpp\"") != std::string::npos, "open payload language", failureReason)) return false;
	return expect(notification.payload.find("int main() { return 0; }\\n") != std::string::npos, "open payload text", failureReason);
}

bool testDidChangeAndVersionGuards(std::string &failureReason) {
	mr::lsp::LspDocumentMirror mirror;
	mr::lsp::LspDocumentNotification notification;
	std::string errorMessage;

	if (!expect(mirror.open(makeSnapshot(1, "one"), notification, errorMessage), "change open failed", failureReason)) return false;
	if (!expect(mirror.change(makeSnapshot(2, "two \"quoted\""), notification, errorMessage), "change failed: " + errorMessage, failureReason)) return false;
	if (!expect(notification.method == "textDocument/didChange", "change method", failureReason)) return false;
	if (!expect(notification.version == 2, "change version", failureReason)) return false;
	if (!expect(mirror.matchesSentVersion(2), "matches sent version", failureReason)) return false;
	if (!expect(mirror.isStaleForSentVersion(1), "stale older version", failureReason)) return false;
	if (!expect(!mirror.isStaleForSentVersion(3), "future version stale", failureReason)) return false;
	if (!expect(notification.payload.find("two \\\"quoted\\\"") != std::string::npos, "change payload escaped text", failureReason)) return false;
	if (!expect(!mirror.change(makeSnapshot(2, "same version"), notification, errorMessage), "same version accepted", failureReason)) return false;
	if (!expect(mirror.sentVersion() == 2, "same version mutated mirror", failureReason)) return false;
	return expect(!errorMessage.empty(), "same version error", failureReason);
}

bool testIdentityGuards(std::string &failureReason) {
	mr::lsp::LspDocumentMirror mirror;
	mr::lsp::LspDocumentNotification notification;
	mr::lsp::LspDocumentSnapshot snapshot = makeSnapshot(2, "two");
	std::string errorMessage;

	if (!expect(mirror.open(makeSnapshot(1, "one"), notification, errorMessage), "identity open failed", failureReason)) return false;
	snapshot.uri = "file:///tmp/other.cpp";
	if (!expect(!mirror.change(snapshot, notification, errorMessage), "changed uri accepted", failureReason)) return false;
	if (!expect(mirror.sentVersion() == 1, "changed uri mutated mirror", failureReason)) return false;
	snapshot = makeSnapshot(2, "two");
	snapshot.languageId = "c";
	if (!expect(!mirror.change(snapshot, notification, errorMessage), "changed language accepted", failureReason)) return false;
	return expect(mirror.sentVersion() == 1, "changed language mutated mirror", failureReason);
}

bool testDidClose(std::string &failureReason) {
	mr::lsp::LspDocumentMirror mirror;
	mr::lsp::LspDocumentNotification notification;
	std::string errorMessage;

	if (!expect(!mirror.close(notification, errorMessage), "close before open accepted", failureReason)) return false;
	if (!expect(mirror.open(makeSnapshot(1, "one"), notification, errorMessage), "close open failed", failureReason)) return false;
	if (!expect(mirror.change(makeSnapshot(3, "three"), notification, errorMessage), "close change failed", failureReason)) return false;
	if (!expect(mirror.close(notification, errorMessage), "close failed: " + errorMessage, failureReason)) return false;
	if (!expect(notification.method == "textDocument/didClose", "close method", failureReason)) return false;
	if (!expect(notification.version == 3, "close version", failureReason)) return false;
	if (!expect(notification.payload.find("\"method\":\"textDocument/didClose\"") != std::string::npos, "close payload method", failureReason)) return false;
	return expect(!mirror.isOpen(), "mirror close state", failureReason);
}

bool testInvalidOpen(std::string &failureReason) {
	mr::lsp::LspDocumentMirror mirror;
	mr::lsp::LspDocumentNotification notification;
	mr::lsp::LspDocumentSnapshot snapshot = makeSnapshot(0, "zero");
	std::string errorMessage;

	if (!expect(!mirror.open(snapshot, notification, errorMessage), "zero version accepted", failureReason)) return false;
	if (!expect(!mirror.isOpen(), "invalid open state", failureReason)) return false;
	snapshot = makeSnapshot(1, "one");
	snapshot.uri.clear();
	if (!expect(!mirror.open(snapshot, notification, errorMessage), "empty uri accepted", failureReason)) return false;
	return expect(!mirror.isOpen(), "empty uri open state", failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testDidOpen(failureReason)) return false;
	if (!testDidChangeAndVersionGuards(failureReason)) return false;
	if (!testIdentityGuards(failureReason)) return false;
	if (!testDidClose(failureReason)) return false;
	if (!testInvalidOpen(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_document_mirror_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_document_mirror_probe passed\n";
	return 0;
}
