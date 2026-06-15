#include <iostream>
#include <string>
#include <unistd.h>

#include "../lsp/MRLspJsonRpc.hpp"

namespace {
struct PeerState {
	int didOpenCount = 0;
	int didChangeCount = 0;
	int didCloseCount = 0;
	std::string documentUri;
	int documentVersion = 0;
};

void skipWhitespace(const std::string &text, std::size_t &pos) noexcept {
	while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n'))
		++pos;
}

bool findKeyValueStart(const std::string &text, const std::string &key, std::size_t &valueStart) {
	const std::string quotedKey = "\"" + key + "\"";
	const std::size_t keyPos = text.find(quotedKey);

	if (keyPos == std::string::npos) return false;
	valueStart = keyPos + quotedKey.size();
	skipWhitespace(text, valueStart);
	if (valueStart >= text.size() || text[valueStart] != ':') return false;
	++valueStart;
	skipWhitespace(text, valueStart);
	return true;
}

bool extractStringValue(const std::string &text, const std::string &key, std::string &value) {
	std::size_t pos = 0;

	value.clear();
	if (!findKeyValueStart(text, key, pos) || pos >= text.size() || text[pos] != '"') return false;
	++pos;
	while (pos < text.size()) {
		const char ch = text[pos++];
		if (ch == '"') return true;
		if (ch == '\\') {
			if (pos >= text.size()) return false;
			value.push_back(text[pos++]);
		} else {
			value.push_back(ch);
		}
	}
	return false;
}

bool extractIntValue(const std::string &text, const std::string &key, int &value) {
	std::size_t pos = 0;
	bool negative = false;
	int parsed = 0;

	if (!findKeyValueStart(text, key, pos) || pos >= text.size()) return false;
	if (text[pos] == '-') {
		negative = true;
		++pos;
	}
	if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') return false;
	while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
		parsed = parsed * 10 + static_cast<int>(text[pos] - '0');
		++pos;
	}
	value = negative ? -parsed : parsed;
	return true;
}

std::string jsonString(const std::string &value) {
	std::string out = "\"";

	for (char ch : value) {
		if (ch == '"' || ch == '\\') {
			out.push_back('\\');
			out.push_back(ch);
		} else if (ch == '\n') {
			out += "\\n";
		} else {
			out.push_back(ch);
		}
	}
	out.push_back('"');
	return out;
}

std::string documentCountsResponse(const mr::lsp::JsonRpcEnvelope &envelope, const PeerState &state) {
	return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":{\"didOpen\":" + std::to_string(state.didOpenCount) + ",\"didChange\":" + std::to_string(state.didChangeCount) + ",\"didClose\":" + std::to_string(state.didCloseCount) + "}}";
}

std::string diagnosticsNotification(const std::string &uri, int version, const std::string &message) {
	return "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":" + jsonString(uri) + ",\"version\":" + std::to_string(version) +
	       ",\"diagnostics\":[{\"range\":{\"start\":{\"line\":0,\"character\":1},\"end\":{\"line\":0,\"character\":4}},\"severity\":2,\"message\":" + jsonString(message) + "}]}}";
}

std::string definitionResponse(const mr::lsp::JsonRpcEnvelope &envelope, const std::string &payload, const PeerState &state) {
	const std::string uri = state.documentUri.empty() ? "file:///tmp/mr.cpp" : state.documentUri;

	if (payload.find("\"character\":99") != std::string::npos) return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":null}";
	if (payload.find("\"character\":98") != std::string::npos) return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":[]}";
	return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":[{\"targetUri\":" + jsonString(uri) +
	       ",\"targetRange\":{\"start\":{\"line\":4,\"character\":0},\"end\":{\"line\":4,\"character\":12}},\"targetSelectionRange\":{\"start\":{\"line\":4,\"character\":2},\"end\":{\"line\":4,\"character\":9}}}]}";
}

std::string hoverResponse(const mr::lsp::JsonRpcEnvelope &envelope) {
	return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":{\"contents\":{\"kind\":\"markdown\",\"value\":\"**mr hover**\\n\\nDeterministic hover text.\"}}}";
}

std::string referencesResponse(const mr::lsp::JsonRpcEnvelope &envelope, const PeerState &state) {
	const std::string uri = state.documentUri.empty() ? "file:///tmp/mr.cpp" : state.documentUri;

	return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":[{\"uri\":" + jsonString(uri) +
	       ",\"range\":{\"start\":{\"line\":1,\"character\":2},\"end\":{\"line\":1,\"character\":5}}},{\"uri\":\"file:///tmp/other.cpp\",\"range\":{\"start\":{\"line\":7,\"character\":1},\"end\":{\"line\":7,\"character\":8}}}]}";
}

std::string completionResponse(const mr::lsp::JsonRpcEnvelope &envelope) {
	return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText +
	       ",\"result\":{\"isIncomplete\":false,\"items\":[{\"label\":\"main\",\"kind\":3,\"labelDetails\":{\"detail\":null},\"detail\":\"int\\u0020main()\",\"insertText\":\"ma\\u0069n\"},{\"label\":\"macroValue\",\"kind\":6,\"detail\":\"int\"}]}}";
}

void emitDiagnostics(const std::string &uri, int version, const std::string &message) {
	std::cout << mr::lsp::buildJsonRpcFrame(diagnosticsNotification(uri, version, message));
	std::cout.flush();
}

std::string responseFor(const mr::lsp::JsonRpcEnvelope &envelope) {
	if (envelope.kind != mr::lsp::JsonRpcMessageKind::Request) return {};
	if (envelope.method == "initialize") return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":{\"capabilities\":{}}}";
	if (envelope.method == "shutdown") return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":null}";
	if (envelope.method == "mr/echo") return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"result\":{\"ok\":true}}";
	return "{\"jsonrpc\":\"2.0\",\"id\":" + envelope.idText + ",\"error\":{\"code\":-32601,\"message\":\"method not found\"}}";
}

void recordNotification(const mr::lsp::JsonRpcEnvelope &envelope, const std::string &payload, PeerState &state) {
	if (envelope.kind != mr::lsp::JsonRpcMessageKind::Notification) return;
	if (envelope.method == "textDocument/didOpen") {
		++state.didOpenCount;
		static_cast<void>(extractStringValue(payload, "uri", state.documentUri));
		static_cast<void>(extractIntValue(payload, "version", state.documentVersion));
		if (!state.documentUri.empty()) emitDiagnostics(state.documentUri, state.documentVersion, "opened diagnostic");
	} else if (envelope.method == "textDocument/didChange") {
		++state.didChangeCount;
		static_cast<void>(extractStringValue(payload, "uri", state.documentUri));
		static_cast<void>(extractIntValue(payload, "version", state.documentVersion));
		if (!state.documentUri.empty()) {
			emitDiagnostics(state.documentUri, state.documentVersion - 1, "stale diagnostic");
			emitDiagnostics(state.documentUri, state.documentVersion, "changed diagnostic");
		}
	} else if (envelope.method == "textDocument/didClose") {
		++state.didCloseCount;
	}
}

bool handlePayload(const std::string &payload, PeerState &state) {
	const mr::lsp::JsonRpcEnvelope envelope = mr::lsp::parseJsonRpcEnvelope(payload);

	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Notification && envelope.method == "exit") return false;
	recordNotification(envelope, payload, state);
	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Request && envelope.method == "mr/documentCounts") {
		std::cout << mr::lsp::buildJsonRpcFrame(documentCountsResponse(envelope, state));
		std::cout.flush();
		return true;
	}
	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Request && envelope.method == "textDocument/definition") {
		std::cout << mr::lsp::buildJsonRpcFrame(definitionResponse(envelope, payload, state));
		std::cout.flush();
		return true;
	}
	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Request && envelope.method == "textDocument/hover") {
		std::cout << mr::lsp::buildJsonRpcFrame(hoverResponse(envelope));
		std::cout.flush();
		return true;
	}
	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Request && envelope.method == "textDocument/references") {
		std::cout << mr::lsp::buildJsonRpcFrame(referencesResponse(envelope, state));
		std::cout.flush();
		return true;
	}
	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Request && envelope.method == "textDocument/completion") {
		std::cout << mr::lsp::buildJsonRpcFrame(completionResponse(envelope));
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
		const std::string chunk(buffer, static_cast<std::size_t>(count));
		if (!framer.feed(chunk)) return 2;
		while (framer.hasMessage()) {
			if (!handlePayload(framer.popMessage().payload, state)) return 0;
		}
	}
	return framer.failed() ? 2 : 0;
}
