#ifndef MRLSPSESSION_HPP
#define MRLSPSESSION_HPP

#include "MRExternalProcess.hpp"
#include "MRLspJsonRpc.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mr::lsp {

struct LspSessionSpec {
	ExternalProcessSpec process;
};

struct LspInboundMessage {
	JsonRpcEnvelope envelope;
	std::string payload;
	bool matchedPendingRequest = false;
	JsonRpcPendingRequest pendingRequest;
};

class LspSession {
public:
	LspSession() = default;
	LspSession(const LspSession &) = delete;
	LspSession &operator=(const LspSession &) = delete;
	~LspSession();

	bool start(const LspSessionSpec &spec, std::string &errorMessage);
	bool sendNotification(std::string_view method, std::string_view paramsJson, std::string &errorMessage);
	bool sendRequest(std::string_view method, std::string_view paramsJson, JsonRpcPendingRequest &request, std::string &errorMessage);
	bool poll(std::vector<LspInboundMessage> &messages, std::string &errorMessage);
	void requestStop();
	bool wait(int timeoutMs, int &exitStatus);
	[[nodiscard]] bool running() const noexcept;
	[[nodiscard]] std::size_t pendingRequestCount() const noexcept;
	void close();

private:
	bool sendPayload(std::string_view payload, std::string &errorMessage);

	ExternalProcessSession process;
	JsonRpcFramer framer;
	JsonRpcRequestTracker requestTracker;
};

[[nodiscard]] std::string buildJsonRpcNotification(std::string_view method, std::string_view paramsJson);
[[nodiscard]] std::string buildJsonRpcRequest(const JsonRpcPendingRequest &request, std::string_view paramsJson);

} // namespace mr::lsp

#endif
