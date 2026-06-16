#include "../lsp/MRLspJsonRpc.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

enum ShaperScenario {
	ShaperScenarioHappy,
	ShaperScenarioMalformed,
	ShaperScenarioUnexpected,
	ShaperScenarioCrashAfterInitialize,
	ShaperScenarioDelayed
};

struct ShaperState {
	ShaperScenario scenario = ShaperScenarioHappy;
	std::string uri = "file:///tmp/mr-lsp-shaper.c";
	std::string languageId = "plaintext";
	std::string text;
	int version = 0;
	int exitStatus = 0;
};

void delayIfRequested(const ShaperState &state) {
	if (state.scenario == ShaperScenarioDelayed) ::usleep(120000);
}

void skipWhitespace(const std::string &text, std::size_t &pos) {
	while (pos < text.size()) {
		const char ch = text[pos];
		if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') return;
		++pos;
	}
}

bool findKeyValueStart(const std::string &payload, const std::string &key, std::size_t &valueStart) {
	const std::string needle = "\"" + key + "\"";
	std::size_t keyPos = payload.find(needle);

	if (keyPos == std::string::npos) return false;
	keyPos += needle.size();
	skipWhitespace(payload, keyPos);
	if (keyPos >= payload.size() || payload[keyPos] != ':') return false;
	++keyPos;
	skipWhitespace(payload, keyPos);
	valueStart = keyPos;
	return true;
}

bool extractStringValue(const std::string &payload, const std::string &key, std::string &value) {
	std::size_t pos = 0;

	if (!findKeyValueStart(payload, key, pos)) return false;
	if (pos >= payload.size() || payload[pos] != '"') return false;
	++pos;
	value.clear();
	while (pos < payload.size()) {
		const char ch = payload[pos++];
		if (ch == '"') return true;
		if (ch == '\\') {
			if (pos >= payload.size()) return false;
			const char escaped = payload[pos++];
			if (escaped == 'n') value.push_back('\n');
			else if (escaped == 'r') value.push_back('\r');
			else if (escaped == 't') value.push_back('\t');
			else value.push_back(escaped);
			continue;
		}
		value.push_back(ch);
	}
	return false;
}

bool extractIntValue(const std::string &payload, const std::string &key, int &value) {
	std::size_t pos = 0;
	bool negative = false;
	int parsed = 0;

	if (!findKeyValueStart(payload, key, pos)) return false;
	if (pos < payload.size() && payload[pos] == '-') {
		negative = true;
		++pos;
	}
	if (pos >= payload.size() || payload[pos] < '0' || payload[pos] > '9') return false;
	while (pos < payload.size() && payload[pos] >= '0' && payload[pos] <= '9') {
		parsed = parsed * 10 + static_cast<int>(payload[pos] - '0');
		++pos;
	}
	value = negative ? -parsed : parsed;
	return true;
}

std::string jsonString(const std::string &text) {
	std::string value;

	value.push_back('"');
	for (std::size_t index = 0; index < text.size(); ++index) {
		const char ch = text[index];
		if (ch == '"' || ch == '\\') {
			value.push_back('\\');
			value.push_back(ch);
		} else if (ch == '\n') {
			value += "\\n";
		} else if (ch == '\r') {
			value += "\\r";
		} else if (ch == '\t') {
			value += "\\t";
		} else {
			value.push_back(ch);
		}
	}
	value.push_back('"');
	return value;
}

void sendPayload(const std::string &payload) {
	std::cout << mr::lsp::buildJsonRpcFrame(payload);
	std::cout.flush();
}

void sendResult(const std::string &idText, const std::string &resultJson) {
	sendPayload("{\"jsonrpc\":\"2.0\",\"id\":" + idText + ",\"result\":" + resultJson + "}");
}

void sendResponseError(const std::string &idText, const std::string &message) {
	sendPayload("{\"jsonrpc\":\"2.0\",\"id\":" + idText + ",\"error\":{\"code\":-32601,\"message\":" + jsonString(message) + "}}");
}

void sendMalformedHeader() {
	std::cout << "Content-Length: nope\r\n\r\n";
	std::cout.flush();
}

void sendUnexpectedResponse() {
	sendPayload("{\"jsonrpc\":\"2.0\",\"id\":999,\"result\":{\"source\":\"protocol-shaper\",\"unexpected\":true}}");
	sendPayload("{\"jsonrpc\":\"2.0\",\"method\":\"$/progress\",\"params\":{\"token\":\"shaper\",\"value\":{\"kind\":\"report\"}}}");
}

void sendDiagnostics(const std::string &uri, int version, const std::string &message) {
	std::string payload;

	payload = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":";
	payload += jsonString(uri);
	payload += ",\"version\":";
	payload += std::to_string(version);
	payload += ",\"diagnostics\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},";
	payload += "\"severity\":2,\"source\":\"mr-lsp-protocol-shaper\",\"message\":";
	payload += jsonString(message);
	payload += "}]}}";
	sendPayload(payload);
}

std::string initializeResult() {
	return "{\"capabilities\":{\"textDocumentSync\":1,\"definitionProvider\":true,\"referencesProvider\":true,"
	       "\"hoverProvider\":true,\"completionProvider\":{\"triggerCharacters\":[\".\",\"-\"]},"
	       "\"codeActionProvider\":true}}";
}

std::string locationResult(const std::string &uri) {
	return "{\"uri\":" + jsonString(uri) + ",\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":6}}}";
}

std::string referencesResult(const std::string &uri) {
	return "[" + locationResult(uri) + ",{\"uri\":" + jsonString(uri) + ",\"range\":{\"start\":{\"line\":1,\"character\":0},\"end\":{\"line\":1,\"character\":6}}}]";
}

std::string hoverResult() {
	return "{\"contents\":{\"kind\":\"plaintext\",\"value\":\"mr protocol shaper hover\"}}";
}

std::string completionResult() {
	return "{\"isIncomplete\":false,\"items\":["
	       "{\"label\":\"shaperCompletionOne\",\"kind\":3,\"detail\":\"protocol shaper item\",\"insertText\":\"shaperCompletionOne\"},"
	       "{\"label\":\"shaperFor\",\"kind\":14,\"detail\":\"protocol shaper snippet\",\"insertText\":\"for (${1:int i = 0}; ${2:i < count}; ${3:++i}) {\\n\\t$0\\n}\",\"insertTextFormat\":2},"
	       "{\"label\":\"shaperCompletionTwo\",\"kind\":6,\"detail\":\"protocol shaper fallback\"}]}";
}

std::string codeActionResult(const std::string &uri) {
	return "[{\"title\":\"protocol shaper quick fix\",\"kind\":\"quickfix\","
	       "\"edit\":{\"changes\":{" +
	       jsonString(uri) +
	       ":[{\"range\":{\"start\":{\"line\":0,\"character\":1},\"end\":{\"line\":0,\"character\":1}},\"newText\":\";\"}]}}},"
	       "{\"title\":\"protocol shaper command\",\"command\":{\"title\":\"protocol shaper command\",\"command\":\"mr.protocolShaper\"}}]";
}

bool handleMessage(ShaperState &state, const mr::lsp::JsonRpcMessage &message) {
	const mr::lsp::JsonRpcEnvelope envelope = mr::lsp::parseJsonRpcEnvelope(message.payload);

	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Request) {
		delayIfRequested(state);
		if (envelope.method == "initialize") {
			sendResult(envelope.idText, initializeResult());
			if (state.scenario == ShaperScenarioCrashAfterInitialize) {
				state.exitStatus = 23;
				return false;
			}
		} else if (envelope.method == "shutdown") {
			sendResult(envelope.idText, "null");
		} else if (envelope.method == "textDocument/definition") {
			sendResult(envelope.idText, locationResult(state.uri));
		} else if (envelope.method == "textDocument/references") {
			sendResult(envelope.idText, referencesResult(state.uri));
		} else if (envelope.method == "textDocument/hover") {
			sendResult(envelope.idText, hoverResult());
		} else if (envelope.method == "textDocument/completion") {
			sendResult(envelope.idText, completionResult());
		} else if (envelope.method == "textDocument/codeAction") {
			sendResult(envelope.idText, codeActionResult(state.uri));
		} else {
			sendResponseError(envelope.idText, "protocol shaper does not implement request: " + envelope.method);
		}
		return true;
	}

	if (envelope.kind == mr::lsp::JsonRpcMessageKind::Notification) {
		if (envelope.method == "initialized") {
			if (state.scenario == ShaperScenarioUnexpected) sendUnexpectedResponse();
		} else if (envelope.method == "textDocument/didOpen") {
			(void)extractStringValue(message.payload, "uri", state.uri);
			(void)extractStringValue(message.payload, "languageId", state.languageId);
			(void)extractStringValue(message.payload, "text", state.text);
			(void)extractIntValue(message.payload, "version", state.version);
			sendDiagnostics(state.uri, state.version, "protocol shaper opened document");
		} else if (envelope.method == "textDocument/didChange") {
			(void)extractIntValue(message.payload, "version", state.version);
			(void)extractStringValue(message.payload, "text", state.text);
			sendDiagnostics(state.uri, state.version - 1, "protocol shaper stale diagnostic");
			sendDiagnostics(state.uri, state.version, "protocol shaper changed document");
		} else if (envelope.method == "textDocument/didClose") {
			sendDiagnostics(state.uri, state.version, "protocol shaper closed document");
		} else if (envelope.method == "exit") {
			state.exitStatus = 0;
			return false;
		}
	}
	return true;
}

ShaperScenario parseScenario(int argc, char **argv) {
	for (int index = 1; index + 1 < argc; ++index) {
		const std::string arg = argv[index];
		const std::string value = argv[index + 1];

		if (arg != "--scenario") continue;
		if (value == "malformed") return ShaperScenarioMalformed;
		if (value == "unexpected") return ShaperScenarioUnexpected;
		if (value == "crash-after-initialize") return ShaperScenarioCrashAfterInitialize;
		if (value == "delayed") return ShaperScenarioDelayed;
		return ShaperScenarioHappy;
	}
	return ShaperScenarioHappy;
}

int runShaper(ShaperState &state) {
	mr::lsp::JsonRpcFramer framer;
	char buffer[4096];

	if (state.scenario == ShaperScenarioMalformed) {
		sendMalformedHeader();
		return 0;
	}

	for (;;) {
		const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
		if (count == 0) return state.exitStatus;
		if (count < 0) return 3;
		if (!framer.feed(std::string(buffer, static_cast<std::size_t>(count)))) return 4;
		while (framer.hasMessage()) {
			if (!handleMessage(state, framer.popMessage())) return state.exitStatus;
		}
	}
}

} // namespace

int main(int argc, char **argv) {
	ShaperState state;

	state.scenario = parseScenario(argc, argv);
	return runShaper(state);
}
