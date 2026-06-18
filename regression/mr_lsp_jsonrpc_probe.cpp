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

bool expectKind(const std::string &payload, mr::lsp::JsonRpcMessageKind expected, const std::string &name, std::string &failureReason) {
	const mr::lsp::JsonRpcMessageKind actual = mr::lsp::classifyJsonRpcPayload(payload);

	return expect(actual == expected, name, failureReason);
}

bool expectEnvelope(const std::string &payload, mr::lsp::JsonRpcMessageKind expectedKind, mr::lsp::JsonRpcIdKind expectedIdKind, const std::string &expectedIdText, const std::string &expectedMethod, const std::string &name, std::string &failureReason) {
	const mr::lsp::JsonRpcEnvelope envelope = mr::lsp::parseJsonRpcEnvelope(payload);

	if (!expect(envelope.kind == expectedKind, name + ": kind", failureReason)) return false;
	if (!expect(envelope.idKind == expectedIdKind, name + ": id kind", failureReason)) return false;
	if (!expect(envelope.idText == expectedIdText, name + ": id text", failureReason)) return false;
	return expect(envelope.method == expectedMethod, name + ": method", failureReason);
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

	if (!expect(framer.feed(frame.substr(0, 7)), "fragmented first feed", failureReason)) return false;
	if (!expect(!framer.hasMessage(), "fragmented premature message", failureReason)) return false;
	if (!expect(framer.feed(frame.substr(7, 5)), "fragmented second feed", failureReason)) return false;
	if (!expect(!framer.hasMessage(), "fragmented second premature message", failureReason)) return false;
	if (!expect(framer.feed(frame.substr(12)), "fragmented final feed", failureReason)) return false;
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

bool testPayloadClassification(std::string &failureReason) {
	if (!expectKind("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"textDocument/definition\",\"params\":{}}", mr::lsp::JsonRpcMessageKind::Request, "classify request", failureReason)) return false;
	if (!expectKind("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}", mr::lsp::JsonRpcMessageKind::Notification, "classify notification", failureReason)) return false;
	if (!expectKind("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"uri\":\"file:///tmp/a.cpp\"}}", mr::lsp::JsonRpcMessageKind::Response, "classify response", failureReason)) return false;
	if (!expectKind("{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32601,\"message\":\"missing\"}}", mr::lsp::JsonRpcMessageKind::Response, "classify error response", failureReason)) return false;
	if (!expectKind("", mr::lsp::JsonRpcMessageKind::Unknown, "classify empty", failureReason)) return false;
	if (!expectKind("[]", mr::lsp::JsonRpcMessageKind::Unknown, "classify array", failureReason)) return false;
	if (!expectKind("{\"params\":{\"method\":\"nested\"}}", mr::lsp::JsonRpcMessageKind::Unknown, "classify nested method", failureReason)) return false;
	if (!expectKind("{\"message\":\"method should not count\"}", mr::lsp::JsonRpcMessageKind::Unknown, "classify string method", failureReason)) return false;
	if (!expectKind("{\"id\":1,\"method\":\"x\",\"result\":{}}", mr::lsp::JsonRpcMessageKind::Unknown, "classify ambiguous", failureReason)) return false;
	return expectKind("{\"id\":1,\"method\":\"x\"", mr::lsp::JsonRpcMessageKind::Unknown, "classify malformed", failureReason);
}

bool testEnvelopeExtraction(std::string &failureReason) {
	if (!expectEnvelope("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"textDocument/definition\",\"params\":{}}", mr::lsp::JsonRpcMessageKind::Request, mr::lsp::JsonRpcIdKind::Number, "1", "textDocument/definition", "envelope numeric request", failureReason)) return false;
	if (!expectEnvelope("{\"jsonrpc\":\"2.0\",\"id\":\"abc-1\",\"method\":\"workspace/symbol\"}", mr::lsp::JsonRpcMessageKind::Request, mr::lsp::JsonRpcIdKind::String, "\"abc-1\"", "workspace/symbol", "envelope string request", failureReason)) return false;
	if (!expectEnvelope("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}", mr::lsp::JsonRpcMessageKind::Notification, mr::lsp::JsonRpcIdKind::None, "", "initialized", "envelope notification", failureReason)) return false;
	if (!expectEnvelope("{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"uri\":\"file:///tmp/a.cpp\"}}", mr::lsp::JsonRpcMessageKind::Response, mr::lsp::JsonRpcIdKind::Number, "2", "", "envelope response", failureReason)) return false;
	if (!expectEnvelope("{\"jsonrpc\":\"2.0\",\"id\":\"r2\",\"error\":{\"code\":-32601,\"message\":\"missing\"}}", mr::lsp::JsonRpcMessageKind::Response, mr::lsp::JsonRpcIdKind::String, "\"r2\"", "", "envelope error response", failureReason)) return false;
	if (!expectEnvelope("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"shutdown\"}", mr::lsp::JsonRpcMessageKind::Unknown, mr::lsp::JsonRpcIdKind::Null, "null", "shutdown", "envelope null id", failureReason)) return false;
	if (!expectEnvelope("{\"params\":{\"id\":4,\"method\":\"nested\"}}", mr::lsp::JsonRpcMessageKind::Unknown, mr::lsp::JsonRpcIdKind::None, "", "", "envelope nested ignored", failureReason)) return false;
	if (!expectEnvelope("{\"message\":\"method should not count\",\"id\":3}", mr::lsp::JsonRpcMessageKind::Unknown, mr::lsp::JsonRpcIdKind::Number, "3", "", "envelope string ignored", failureReason)) return false;
	return expectEnvelope("{\"id\":true,\"method\":\"x\"}", mr::lsp::JsonRpcMessageKind::Unknown, mr::lsp::JsonRpcIdKind::Invalid, "", "", "envelope malformed id", failureReason);
}

bool testRequestTracker(std::string &failureReason) {
	mr::lsp::JsonRpcRequestTracker tracker;
	mr::lsp::JsonRpcPendingRequest completed;

	const mr::lsp::JsonRpcPendingRequest first = tracker.beginRequest("initialize");
	if (!expect(first.idText == "1", "tracker first id", failureReason)) return false;
	if (!expect(first.method == "initialize", "tracker first method", failureReason)) return false;
	if (!expect(tracker.pendingCount() == 1, "tracker first count", failureReason)) return false;
	if (!expect(tracker.hasPending(first.idText), "tracker first pending", failureReason)) return false;

	const mr::lsp::JsonRpcPendingRequest second = tracker.beginRequest("textDocument/definition");
	if (!expect(second.idText == "2", "tracker second id", failureReason)) return false;
	if (!expect(tracker.pendingCount() == 2, "tracker second count", failureReason)) return false;

	if (!expect(tracker.completeResponse(mr::lsp::parseJsonRpcEnvelope("{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":null}"), completed), "tracker complete second", failureReason)) return false;
	if (!expect(completed.idText == second.idText, "tracker completed second id", failureReason)) return false;
	if (!expect(completed.method == second.method, "tracker completed second method", failureReason)) return false;
	if (!expect(tracker.pendingCount() == 1, "tracker count after complete", failureReason)) return false;
	if (!expect(!tracker.completeResponse(mr::lsp::parseJsonRpcEnvelope("{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":null}"), completed), "tracker duplicate response", failureReason)) return false;
	if (!expect(!tracker.completeResponse(mr::lsp::parseJsonRpcEnvelope("{\"jsonrpc\":\"2.0\",\"id\":77,\"result\":null}"), completed), "tracker unknown response", failureReason)) return false;
	if (!expect(!tracker.completeResponse(mr::lsp::parseJsonRpcEnvelope("{\"jsonrpc\":\"2.0\",\"method\":\"window/logMessage\"}"), completed), "tracker notification response", failureReason)) return false;
	if (!expect(tracker.cancelRequest(first.idText), "tracker cancel first", failureReason)) return false;
	if (!expect(!tracker.hasPending(first.idText), "tracker first removed", failureReason)) return false;
	if (!expect(tracker.pendingCount() == 0, "tracker empty after cancel", failureReason)) return false;
	if (!expect(!tracker.cancelRequest(first.idText), "tracker duplicate cancel", failureReason)) return false;

	const mr::lsp::JsonRpcPendingRequest afterCancel = tracker.beginRequest("shutdown");
	if (!expect(afterCancel.idText == "3", "tracker monotone id", failureReason)) return false;
	tracker.clear();
	if (!expect(tracker.pendingCount() == 0, "tracker clear count", failureReason)) return false;
	const mr::lsp::JsonRpcPendingRequest afterClear = tracker.beginRequest("initialize");
	return expect(afterClear.idText == "1", "tracker clear resets id", failureReason);
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
	if (!testPayloadClassification(failureReason)) return false;
	if (!testEnvelopeExtraction(failureReason)) return false;
	if (!testRequestTracker(failureReason)) return false;
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
