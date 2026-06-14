#ifndef MRLSPLIFECYCLE_HPP
#define MRLSPLIFECYCLE_HPP

#include "MRLspSession.hpp"

#include <string>
#include <vector>

namespace mr::lsp {

enum class LspLifecycleState {
	Stopped,
	Starting,
	Initialized,
	ShuttingDown,
	Shutdown,
	Failed
};

struct LspInitializeSpec {
	LspSessionSpec session;
	std::string initializeParamsJson = "{}";
};

class LspLifecycle {
public:
	LspLifecycle() = default;
	LspLifecycle(const LspLifecycle &) = delete;
	LspLifecycle &operator=(const LspLifecycle &) = delete;
	~LspLifecycle();

	bool start(const LspInitializeSpec &spec, std::string &errorMessage);
	bool poll(std::vector<LspInboundMessage> &messages, std::string &errorMessage);
	bool sendInitialized(std::string &errorMessage);
	bool sendInitializedPayload(const std::string &payload, std::string &errorMessage);
	bool shutdown(std::string &errorMessage);
	bool exit(std::string &errorMessage);
	void requestStop();
	bool wait(int timeoutMs, int &exitStatus);
	void close();

	[[nodiscard]] LspLifecycleState state() const noexcept;
	[[nodiscard]] bool running() const noexcept;

private:
	bool fail(std::string &errorMessage, const std::string &message);
	void applyInboundMessage(const LspInboundMessage &message);

	LspSession session;
	LspLifecycleState lifecycleState = LspLifecycleState::Stopped;
	std::string initializeRequestId;
	std::string shutdownRequestId;
};

[[nodiscard]] const char *lspLifecycleStateName(LspLifecycleState state) noexcept;

} // namespace mr::lsp

#endif
