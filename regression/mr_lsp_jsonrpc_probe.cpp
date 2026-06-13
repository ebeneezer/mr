#include <iostream>
#include <string>

#include "../lsp/MRLspJsonRpc.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

bool expectPayload(mr::lsp::JsonRpcFramer &framer, const std::string &payload, const std::string &name, std::string &failureReason) {
	if (!expect(framer.hasMessage(), name + ": missing message", failureReason)) return false;
	const mr::lsp::JsonRpcMessage message = framer.popMessage();
	if (!expect(message.payload == payload, name + ": payload mismatch", failureReason)) return false;
	return true;
}

bool testCompleteFrame(std::string &failureReason) {
	mr::lsp::JsonRpcFramer framer;
	const std::string payload = "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\"}";

	if (!expect(framer.feed(mr::lsp::buildJsonRpcFrame(payload)), "complete frame feed", failureReason)) return false;
	if (!expectPayload(framer, payload, "complete frame", failureReason)) return false;
	return expect(!framer.hasMessage(), "complete frame extra message", failureReason);
}

bool testFragmentedFrame(std::string &failureReason) {
	mr::lsp::JsonRpcFramer framer;
	const std::string frame = mr::lsp::buildJsonRpcFrame("{\"fragmented\":true}");

	if (!expect(framer.feed(std::string_view(frame).substr(0, 7)), "fragmented first feed", failureReason)) return false;
	if (!expect(!framer.hasMessage(), "fragmented premature message", failureReason)) return false;
	if (!expect(framer.feed(std::string_view(frame).substr(7, 5)), "fragmented second feed", failureReason)) return false;
	if (!expect(!framer.hasMessage(), "fragmented second premature message", failureReason)) return false;
	if (!expect(framer.feed(std::string_view(frame).substr(12)), "fragmented final feed", failureReason)) return false;
	return expectPayload(framer, "{\"fragmented\":true}", "fragmented final", failureReason);
}

bool testMultipleFrames(std::string &failureReason) {
	mr::lsp::JsonRpcFramer framer;
	const std::string first = "{\"id\":1}";
	const std::string second = "{\"id\":2}";
	const std::string frames = mr::lsp::buildJsonRpcFrame(first) + mr::lsp::buildJsonRpcFrame(second);

	if (!expect(framer.feed(frames), "multiple frame feed", failureReason)) return false;
	if (!expectPayload(framer, first, "multiple first", failureReason)) return false;
	if (!expectPayload(framer, second, "multiple second", failureReason)) return false;
	return expect(!framer.hasMessage(), "multiple extra message", failureReason);
}

bool testHeaderWhitespaceAndExtraHeader(std::string &failureReason) {
	mr::lsp::JsonRpcFramer framer;
	const std::string payload = "{\"ok\":true}";
	const std::string frame = "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\nContent-Length:   " + std::to_string(payload.size()) + "  \r\n\r\n" + payload;

	if (!expect(framer.feed(frame), "extra header feed", failureReason)) return false;
	return expectPayload(framer, payload, "extra header", failureReason);
}

bool testEmbeddedNewlines(std::string &failureReason) {
	mr::lsp::JsonRpcFramer framer;
	const std::string payload = "{\r\n  \"line\":\"a\\nb\"\r\n}";

	if (!expect(framer.feed(mr::lsp::buildJsonRpcFrame(payload)), "embedded newline feed", failureReason)) return false;
	return expectPayload(framer, payload, "embedded newline", failureReason);
}

bool testMalformedMissingColon(std::string &failureReason) {
	mr::lsp::JsonRpcFramer framer;

	if (!expect(!framer.feed("Content-Length 3\r\n\r\nabc"), "missing colon accepted", failureReason)) return false;
	return expect(framer.failed(), "missing colon not failed", failureReason);
}

bool testMalformedMissingLength(std::string &failureReason) {
	mr::lsp::JsonRpcFramer framer;

	if (!expect(!framer.feed("Content-Type: application/json\r\n\r\n{}"), "missing length accepted", failureReason)) return false;
	return expect(framer.failed(), "missing length not failed", failureReason);
}

bool testMalformedInvalidLength(std::string &failureReason) {
	mr::lsp::JsonRpcFramer framer;

	if (!expect(!framer.feed("Content-Length: 12x\r\n\r\n{}"), "invalid length accepted", failureReason)) return false;
	return expect(framer.failed(), "invalid length not failed", failureReason);
}

bool testDuplicateLength(std::string &failureReason) {
	mr::lsp::JsonRpcFramer framer;

	if (!expect(!framer.feed("Content-Length: 1\r\nContent-Length: 1\r\n\r\na"), "duplicate length accepted", failureReason)) return false;
	return expect(framer.failed(), "duplicate length not failed", failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testCompleteFrame(failureReason)) return false;
	if (!testFragmentedFrame(failureReason)) return false;
	if (!testMultipleFrames(failureReason)) return false;
	if (!testHeaderWhitespaceAndExtraHeader(failureReason)) return false;
	if (!testEmbeddedNewlines(failureReason)) return false;
	if (!testMalformedMissingColon(failureReason)) return false;
	if (!testMalformedMissingLength(failureReason)) return false;
	if (!testMalformedInvalidLength(failureReason)) return false;
	if (!testDuplicateLength(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_jsonrpc_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_jsonrpc_probe passed\n";
	return 0;
}
