#ifndef MREXTERNALPROCESS_HPP
#define MREXTERNALPROCESS_HPP

#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace mr::lsp {

struct ExternalProcessSpec {
	std::string executablePath;
	std::vector<std::string> arguments;
	std::string workingDirectory;
};

class ExternalProcessSession {
public:
	ExternalProcessSession() = default;
	ExternalProcessSession(const ExternalProcessSession &) = delete;
	ExternalProcessSession &operator=(const ExternalProcessSession &) = delete;
	~ExternalProcessSession();

	bool start(const ExternalProcessSpec &spec, std::string &errorMessage);
	bool writeStdin(std::string_view text, std::string &errorMessage);
	bool readAvailable(std::string &out, std::string &errorMessage);
	void requestStop();
	bool wait(int timeoutMs, int &exitStatus);
	[[nodiscard]] bool running() const noexcept;
	void close();

private:
	void closeParentPipes();
	void closeInputPipe();
	void closeOutputPipe();

	pid_t childPid = -1;
	int stdinFd = -1;
	int outputFd = -1;
	bool childRunning = false;
};

} // namespace mr::lsp

#endif
