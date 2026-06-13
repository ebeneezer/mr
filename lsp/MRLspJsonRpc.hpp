#ifndef MRLSPJSONRPC_HPP
#define MRLSPJSONRPC_HPP

#include <string>
#include <string_view>
#include <vector>

namespace mr::lsp {

struct JsonRpcMessage {
	std::string payload;
};

class JsonRpcFramer {
public:
	bool feed(std::string_view bytes);
	[[nodiscard]] bool hasMessage() const noexcept;
	[[nodiscard]] JsonRpcMessage popMessage();
	[[nodiscard]] bool failed() const noexcept;
	[[nodiscard]] const std::string &errorMessage() const noexcept;
	void clear();

private:
	bool processBuffer();
	bool fail(const std::string &message);

	std::string buffer;
	std::vector<JsonRpcMessage> messages;
	std::string error;
};

[[nodiscard]] std::string buildJsonRpcFrame(std::string_view json);

} // namespace mr::lsp

#endif
