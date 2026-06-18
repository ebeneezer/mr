#ifndef MRLSPSESSION_HPP
#define MRLSPSESSION_HPP

#include "MRExternalProcess.hpp"
#include "MRLspJsonRpc.hpp"

#include <string>
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
	bool sendNotification(const std::string &method, const std::string &paramsJson, std::string &errorMessage);
	bool sendRequest(const std::string &method, const std::string &paramsJson, JsonRpcPendingRequest &request, std::string &errorMessage);
	bool sendRawPayload(const std::string &payload, std::string &errorMessage);
	bool poll(std::vector<LspInboundMessage> &messages, std::string &errorMessage);
	void requestStop();
	bool wait(int timeoutMs, int &exitStatus);
	[[nodiscard]] bool running() const noexcept;
	[[nodiscard]] std::size_t pendingRequestCount() const noexcept;
	void close();

private:
	ExternalProcessSession process;
	JsonRpcFramer framer;
	JsonRpcRequestTracker requestTracker;
};

[[nodiscard]] std::string buildJsonRpcNotification(const std::string &method, const std::string &paramsJson);
[[nodiscard]] std::string buildJsonRpcRequest(const JsonRpcPendingRequest &request, const std::string &paramsJson);

} // namespace mr::lsp

#endif
