#include <iostream>
#include <string>
#include <unistd.h>

#include "../lsp/MRLspJsonRpc.hpp"

namespace {
std::string responseFor(const mr::lsp::JsonRpcEnvelope &envelope) {
	if (envelope.kind != mr::lsp::JsonRpcMessageKind::Request) return {};
	if (envelope.method == "initialize") return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":{\"capabilities\":{}}}";
	if (envelope.method == "shutdown") return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":null}";
	if (envelope.method == "mr/echo") return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":{\"ok\":true}}";
	return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"error\":{\"code\":-32601,\"message\":\"method not found\"}}";
}

bool handlePayload(const std::string &payload) {
	const mr::lsp::JsonRpcEnvelope envelope = mr::lsp::parseJsonRpcEnvelope(payload);

	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Notification && envelope.method == "exit") return false;
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
	char buffer[512];

	for (;;) {
		const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
		if (count <= 0) break;
		if (!framer.feed(std::string_view(buffer, static_cast<std::size_t>(count)))) return 2;
		while (framer.hasMessage()) {
			if (!handlePayload(framer.popMessage().payload)) return 0;
		}
	}
	return framer.failed() ? 2 : 0;
}
