#include <iostream>
#include <string>
#include <unistd.h>

#include "../lsp/MRLspJsonRpc.hpp"

namespace {
struct PeerState {
	int didOpenCount = 0;
	int didChangeCount = 0;
	int didCloseCount = 0;
};

std::string documentCountsResponse(const mr::lsp::JsonRpcEnvelope &envelope, const PeerState &state) {
	return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":{\"didOpen\":" + std::to_string(state.didOpenCount) + ",\"didChange\":" + std::to_string(state.didChangeCount) + ",\"didClose\":" + std::to_string(state.didCloseCount) + "}}";
}

std::string responseFor(const mr::lsp::JsonRpcEnvelope &envelope) {
	if (envelope.kind != mr::lsp::JsonRpcMessageKind::Request) return {};
	if (envelope.method == "initialize") return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":{\"capabilities\":{}}}";
	if (envelope.method == "shutdown") return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":null}";
	if (envelope.method == "mr/echo") return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":{\"ok\":true}}";
	return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"error\":{\"code\":-32601,\"message\":\"method not found\"}}";
}

void recordNotification(const mr::lsp::JsonRpcEnvelope &envelope, PeerState &state) noexcept {
	if (envelope.kind != mr::lsp::JsonRpcMessageKind::Notification) return;
	if (envelope.method == "textDocument/didOpen") ++state.didOpenCount;
	else if (envelope.method == "textDocument/didChange") ++state.didChangeCount;
	else if (envelope.method == "textDocument/didClose") ++state.didCloseCount;
}

bool handlePayload(const std::string &payload, PeerState &state) {
	const mr::lsp::JsonRpcEnvelope envelope = mr::lsp::parseJsonRpcEnvelope(payload);

	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Notification && envelope.method == "exit") return false;
	recordNotification(envelope, state);
	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Request && envelope.method == "mr/documentCounts") {
		std::cout << mr::lsp::buildJsonRpcFrame(documentCountsResponse(envelope, state));
		std::cout.flush();
		return true;
	}
	const std::string response = responseFor(envelope);
	if (!response.empty()) {
		std::cout << mr::lsp::buildJsonRpcFrame(response);
		std::cout.flush();
	}
	return true;
}
} // namespace

int main() {
	mr::lsp::JsonRpcFramer framer;
	PeerState state;
	char buffer[512];

	for (;;) {
		const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
		if (count <= 0) break;
		if (!framer.feed(std::string_view(buffer, static_cast<std::size_t>(count)))) return 2;
		while (framer.hasMessage()) {
			if (!handlePayload(framer.popMessage().payload, state)) return 0;
		}
	}
	return framer.failed() ? 2 : 0;
}
