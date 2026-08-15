#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TScreen
#include <tvision/tv.h>

#include "MRUpdateInternal.hpp"

#include "MRCommandRouter.hpp"
#include "MRCommands.hpp"
#include "MRVersion.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRSidekickEditor.hpp"

#include <openssl/crypto.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace mr {
namespace update_internal {
namespace {

constexpr char kInstalledBinary[] = "/usr/local/bin/mr";
constexpr char kSudoExecutable[] = "/usr/bin/sudo";
constexpr char kProtocolMagic[] = "MRUPD01";
constexpr std::size_t kPasswordCapacity = 256;

TRect centeredUpdateChangedBounds() {
	constexpr short width = 76;
	constexpr short height = 22;
	const TRect desktop = TProgram::deskTop != nullptr ? TProgram::deskTop->getExtent() : TRect(0, 0, width, height);
	const short x = desktop.a.x + std::max<short>(0, (desktop.b.x - desktop.a.x - width) / 2);
	const short y = desktop.a.y + std::max<short>(0, (desktop.b.y - desktop.a.y - height) / 2);

	return TRect(x, y, x + width, y + height);
}

class PasswordInputLine final : public TInputLine {
  public:
	PasswordInputLine(const TRect &bounds, int limit) noexcept : TInputLine(bounds, limit) {
	}

	~PasswordInputLine() {
		if (data != nullptr && maxLen >= 0) OPENSSL_cleanse(data, static_cast<std::size_t>(maxLen) + 1);
	}

	void draw() override {
		TDrawBuffer buffer;
		const TColorAttr color = getColor((state & sfFocused) != 0 ? 2 : 1);
		const int length = data != nullptr ? static_cast<int>(::strnlen(data, static_cast<std::size_t>(maxLen))) : 0;

		buffer.moveChar(0, ' ', color, size.x);
		if (size.x > 2) buffer.moveChar(1, '*', color, std::min(length, size.x - 2));
		writeLine(0, 0, size.x, size.y, buffer);
		setCursor(static_cast<short>(std::min(length + 1, std::max(1, size.x - 1))), 0);
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evCommand && (event.message.command == cmCopy || event.message.command == cmCut)) {
			clearEvent(event);
			return;
		}
		TInputLine::handleEvent(event);
	}
};

class UpdateChangedDialog final : public TDialog {
  public:
	UpdateChangedDialog(const std::string &version, const std::string &changedText)
	    : TWindowInit(&TDialog::initFrame), TDialog(centeredUpdateChangedBounds(), ("Update to V" + version).c_str()) {
		flags = wfMove;
		MRSidekickEditor *view = new MRSidekickEditor(TRect(2, 2, 74, 18), 0, 0, 0, changedText, "Changed", std::vector<MRSidekickSpan>(), true, false, false);
		if (view != nullptr) insert(view);
		insert(new TButton(TRect(32, 19, 44, 21), "~R~estart", cmMrUpdateRestart, bfDefault));
	}

	void handleEvent(TEvent &event) override {
		if (event.what == evKeyDown && TKey(event.keyDown) == TKey(kbEsc)) {
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && event.message.command == cmMrUpdateRestart) {
			endModal(cmMrUpdateRestart);
			clearEvent(event);
			return;
		}
		if (event.what == evCommand && (event.message.command == cmCancel || event.message.command == cmClose)) {
			clearEvent(event);
			return;
		}
		TDialog::handleEvent(event);
	}
};

void secureClear(void *buffer, std::size_t size) noexcept {
	if (buffer != nullptr && size != 0) OPENSSL_cleanse(buffer, size);
}

bool writeAll(int fd, const void *buffer, std::size_t length) {
	const unsigned char *bytes = static_cast<const unsigned char *>(buffer);
	while (length != 0) {
		const ssize_t written = ::write(fd, bytes, length);
		if (written < 0 && errno == EINTR) continue;
		if (written <= 0) return false;
		bytes += written;
		length -= static_cast<std::size_t>(written);
	}
	return true;
}

bool blockSigpipe(sigset_t &previousMask, bool &alreadyPending) {
	sigset_t blocked;
	sigset_t pending;

	sigemptyset(&blocked);
	sigaddset(&blocked, SIGPIPE);
	alreadyPending = sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1;
	return pthread_sigmask(SIG_BLOCK, &blocked, &previousMask) == 0;
}

void restoreSigpipe(const sigset_t &previousMask, bool alreadyPending) {
	sigset_t pending;
	sigset_t blocked;

	if (!alreadyPending && sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1) {
		int received = 0;
		sigemptyset(&blocked);
		sigaddset(&blocked, SIGPIPE);
		static_cast<void>(sigwait(&blocked, &received));
	}
	static_cast<void>(pthread_sigmask(SIG_SETMASK, &previousMask, nullptr));
}

bool readAll(int fd, void *buffer, std::size_t length) {
	unsigned char *bytes = static_cast<unsigned char *>(buffer);
	while (length != 0) {
		const ssize_t received = ::read(fd, bytes, length);
		if (received < 0 && errno == EINTR) continue;
		if (received <= 0) return false;
		bytes += received;
		length -= static_cast<std::size_t>(received);
	}
	return true;
}

void encodeUint64(std::uint64_t value, unsigned char output[8]) {
	for (unsigned index = 0; index < 8; ++index) output[index] = static_cast<unsigned char>((value >> (index * 8)) & 0xffu);
}

std::uint64_t decodeUint64(const unsigned char input[8]) {
	std::uint64_t value = 0;
	for (unsigned index = 0; index < 8; ++index) value |= static_cast<std::uint64_t>(input[index]) << (index * 8);
	return value;
}

bool waitForSuccessfulExit(pid_t child) {
	int status = 0;
	while (::waitpid(child, &status, 0) < 0)
		if (errno != EINTR) return false;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void redirectToNull(int fd) {
	const int nullFd = ::open("/dev/null", fd == STDIN_FILENO ? O_RDONLY : O_WRONLY);
	if (nullFd >= 0) {
		::dup2(nullFd, fd);
		::close(nullFd);
	}
}

bool validateSudoWithoutPassword() {
	if (::geteuid() == 0) return true;
	if (::access(kSudoExecutable, X_OK) != 0) return false;
	const pid_t child = ::fork();
	if (child < 0) return false;
	if (child == 0) {
		redirectToNull(STDIN_FILENO);
		redirectToNull(STDOUT_FILENO);
		redirectToNull(STDERR_FILENO);
		char *const args[] = {const_cast<char *>(kSudoExecutable), const_cast<char *>("-n"), const_cast<char *>("-v"), nullptr};
		::execv(kSudoExecutable, args);
		::_exit(127);
	}
	return waitForSuccessfulExit(child);
}

bool promptSudoPassword(char password[kPasswordCapacity], std::size_t &passwordLength) {
	passwordLength = 0;
	if (TProgram::deskTop == nullptr) return false;
	TDialog *dialog = new TDialog(TRect(0, 0, 48, 7), "Superuser Password");
	if (dialog == nullptr) return false;
	dialog->options |= ofCentered;
	dialog->flags = wfMove;
	PasswordInputLine *field = new PasswordInputLine(TRect(2, 2, 46, 3), static_cast<int>(kPasswordCapacity - 1));
	dialog->insert(field);
	dialog->insert(new TButton(TRect(18, 4, 30, 6), "~U~pdate", cmOK, bfDefault));
	field->select();
	const ushort command = TProgram::deskTop->execView(dialog);
	if (command == cmOK && field->data != nullptr) {
		passwordLength = ::strnlen(field->data, kPasswordCapacity - 1);
		if (passwordLength != 0) std::memcpy(password, field->data, passwordLength);
		password[passwordLength] = '\0';
	}
	TObject::destroy(dialog);
	TScreen::flushScreen();
	return command == cmOK && passwordLength != 0;
}

bool validateSudoWithPassword(char password[kPasswordCapacity], std::size_t passwordLength) {
	int inputPipe[2] = {-1, -1};
	if (::pipe(inputPipe) != 0) {
		secureClear(password, kPasswordCapacity);
		return false;
	}
	const pid_t child = ::fork();
	if (child < 0) {
		::close(inputPipe[0]);
		::close(inputPipe[1]);
		secureClear(password, kPasswordCapacity);
		return false;
	}
	if (child == 0) {
		::close(inputPipe[1]);
		::dup2(inputPipe[0], STDIN_FILENO);
		::close(inputPipe[0]);
		redirectToNull(STDOUT_FILENO);
		redirectToNull(STDERR_FILENO);
		char *const args[] = {const_cast<char *>(kSudoExecutable), const_cast<char *>("-S"), const_cast<char *>("-p"), const_cast<char *>(""), const_cast<char *>("-v"), nullptr};
		::execv(kSudoExecutable, args);
		::_exit(127);
	}
	::close(inputPipe[0]);
	sigset_t previousMask;
	bool alreadyPending = false;
	const bool signalBlocked = blockSigpipe(previousMask, alreadyPending);
	const bool sent = signalBlocked && writeAll(inputPipe[1], password, passwordLength) && writeAll(inputPipe[1], "\n", 1);
	if (signalBlocked) restoreSigpipe(previousMask, alreadyPending);
	secureClear(password, kPasswordCapacity);
	::close(inputPipe[1]);
	return sent && waitForSuccessfulExit(child);
}

bool writeProtocol(int fd, const UpdatePackagePayload &package) {
	unsigned char length[8]{};

	if (!writeAll(fd, kProtocolMagic, sizeof(kProtocolMagic) - 1)) return false;
	encodeUint64(package.manifestBytes.size(), length);
	if (!writeAll(fd, length, sizeof(length))) return false;
	encodeUint64(package.signature.size(), length);
	if (!writeAll(fd, length, sizeof(length))) return false;
	for (const std::vector<unsigned char> &file : package.files) {
		encodeUint64(file.size(), length);
		if (!writeAll(fd, length, sizeof(length))) return false;
	}
	if (!writeAll(fd, package.manifestBytes.data(), package.manifestBytes.size())) return false;
	if (!writeAll(fd, package.signature.data(), package.signature.size())) return false;
	for (const std::vector<unsigned char> &file : package.files)
		if (!writeAll(fd, file.data(), file.size())) return false;
	return true;
}

bool installedBinaryIsTrusted() {
	struct stat status {};

	if (::lstat(kInstalledBinary, &status) != 0) return false;
	return S_ISREG(status.st_mode) && status.st_uid == 0 && (status.st_mode & 0022) == 0;
}

bool currentExecutableIsSystemBinary() {
	std::array<char, 4096> path{};
	const ssize_t count = ::readlink("/proc/self/exe", path.data(), path.size() - 1);

	if (count <= 0 || static_cast<std::size_t>(count) >= path.size()) return false;
	path[static_cast<std::size_t>(count)] = '\0';
	return std::strcmp(path.data(), kInstalledBinary) == 0 && installedBinaryIsTrusted();
}

bool applyPackageThroughSudo(const UpdatePackagePayload &package, std::string &error) {
	int inputPipe[2] = {-1, -1};
	int errorPipe[2] = {-1, -1};
	if (!installedBinaryIsTrusted()) {
		error = "Unable to find a trusted update helper at /usr/local/bin/mr.";
		return false;
	}
	if (::pipe(inputPipe) != 0 || ::pipe(errorPipe) != 0) {
		if (inputPipe[0] >= 0) {
			::close(inputPipe[0]);
			::close(inputPipe[1]);
		}
		error = "Unable to create the update channel.";
		return false;
	}
	const pid_t child = ::fork();
	if (child < 0) {
		::close(inputPipe[0]);
		::close(inputPipe[1]);
		::close(errorPipe[0]);
		::close(errorPipe[1]);
		error = "Unable to start the privileged update process.";
		return false;
	}
	if (child == 0) {
		::close(inputPipe[1]);
		::close(errorPipe[0]);
		::dup2(inputPipe[0], STDIN_FILENO);
		::dup2(errorPipe[1], STDERR_FILENO);
		redirectToNull(STDOUT_FILENO);
		::close(inputPipe[0]);
		::close(errorPipe[1]);
		if (::geteuid() == 0) {
			char *const args[] = {const_cast<char *>(kInstalledBinary), const_cast<char *>(kInternalApplyOption), nullptr};
			::execv(kInstalledBinary, args);
		} else {
			char *const args[] = {const_cast<char *>(kSudoExecutable), const_cast<char *>("-n"), const_cast<char *>("-p"), const_cast<char *>(""), const_cast<char *>(kInstalledBinary), const_cast<char *>(kInternalApplyOption), nullptr};
			::execv(kSudoExecutable, args);
		}
		::_exit(127);
	}
	::close(inputPipe[0]);
	::close(errorPipe[1]);
	sigset_t previousMask;
	bool alreadyPending = false;
	const bool signalBlocked = blockSigpipe(previousMask, alreadyPending);
	const bool sent = signalBlocked && writeProtocol(inputPipe[1], package);
	if (signalBlocked) restoreSigpipe(previousMask, alreadyPending);
	::close(inputPipe[1]);
	std::array<char, 2048> errorBuffer{};
	std::size_t errorLength = 0;
	while (errorLength + 1 < errorBuffer.size()) {
		const ssize_t count = ::read(errorPipe[0], errorBuffer.data() + errorLength, errorBuffer.size() - errorLength - 1);
		if (count < 0 && errno == EINTR) continue;
		if (count <= 0) break;
		errorLength += static_cast<std::size_t>(count);
	}
	::close(errorPipe[0]);
	errorBuffer[errorLength] = '\0';
	const bool exited = waitForSuccessfulExit(child);
	if (!sent || !exited) {
		error = errorLength != 0 ? std::string(errorBuffer.data(), errorLength) : "Privileged update installation failed.";
		while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) error.pop_back();
		return false;
	}
	return true;
}

bool atomicWriteUserMacro(const std::filesystem::path &target, const std::vector<unsigned char> &data, std::string &error) {
	std::error_code ec;
	std::filesystem::create_directories(target.parent_path(), ec);
	if (ec) {
		error = "Unable to create the user macro directory.";
		return false;
	}
	if (std::filesystem::exists(target, ec)) return !ec;
	const std::filesystem::path temporary = target.string() + ".mr-update-" + std::to_string(static_cast<long long>(::getpid()));
	const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
	if (fd < 0) {
		error = "Unable to create a user macro update file.";
		return false;
	}
	const bool written = writeAll(fd, data.data(), data.size()) && ::fsync(fd) == 0;
	::close(fd);
	if (!written || ::rename(temporary.c_str(), target.c_str()) != 0) {
		::unlink(temporary.c_str());
		error = "Unable to install a user macro update file.";
		return false;
	}
	return true;
}

std::string installUserMacros(const std::vector<UpdateMacroFile> &macros) {
	const char *xdg = std::getenv("XDG_CONFIG_HOME");
	const char *home = std::getenv("HOME");
	std::filesystem::path base;
	std::string error;

	if (xdg != nullptr && xdg[0] == '/') base = std::filesystem::path(xdg) / "mr/macros";
	else if (home != nullptr && home[0] == '/')
		base = std::filesystem::path(home) / ".config/mr/macros";
	else
		return "System files were updated, but the user macro directory could not be resolved.";
	for (const UpdateMacroFile &macro : macros)
		if (!atomicWriteUserMacro(base / macro.relativePath, macro.data, error)) return "System files were updated, but " + error;
	return std::string();
}

bool readProtocolVector(int fd, std::uint64_t length, std::size_t limit, std::vector<unsigned char> &bytes) {
	if (length == 0 || length > limit) return false;
	bytes.resize(static_cast<std::size_t>(length));
	return readAll(fd, bytes.data(), bytes.size());
}

bool readUpdateProtocol(int fd, UpdatePackagePayload &package, std::string &error) {
	char magic[sizeof(kProtocolMagic) - 1]{};
	std::array<std::uint64_t, 2 + kUpdateFileCount> lengths{};
	unsigned char encoded[8]{};
	if (!readAll(fd, magic, sizeof(magic)) || std::memcmp(magic, kProtocolMagic, sizeof(magic)) != 0) {
		error = "Invalid update protocol header.";
		return false;
	}
	for (std::uint64_t &length : lengths) {
		if (!readAll(fd, encoded, sizeof(encoded))) {
			error = "Incomplete update protocol header.";
			return false;
		}
		length = decodeUint64(encoded);
	}
	if (!readProtocolVector(fd, lengths[0], kManifestLimit, package.manifestBytes) || !readProtocolVector(fd, lengths[1], kSignatureLimit, package.signature)) {
		error = "Invalid update protocol metadata.";
		return false;
	}
	for (std::size_t index = 0; index < package.files.size(); ++index)
		if (!readProtocolVector(fd, lengths[index + 2], kUpdateTargets[index].sizeLimit, package.files[index])) {
			error = "Invalid update protocol file.";
			return false;
		}
	char trailing = 0;
	ssize_t trailingCount = 0;
	do {
		trailingCount = ::read(fd, &trailing, 1);
	} while (trailingCount < 0 && errno == EINTR);
	if (trailingCount != 0) {
		error = "Unexpected trailing update protocol data.";
		return false;
	}
	return true;
}

bool splitDirectoryPath(int rootFd, const char *path, int &directoryFd, std::string &error) {
	directoryFd = ::dup(rootFd);
	if (directoryFd < 0) return false;
	std::string_view remaining(path != nullptr ? path : "");
	while (!remaining.empty()) {
		const std::size_t slash = remaining.find('/');
		const std::string component(remaining.substr(0, slash));
		if (component.empty() || component == "." || component == "..") {
			error = "Invalid hardcoded update directory.";
			::close(directoryFd);
			return false;
		}
		if (::mkdirat(directoryFd, component.c_str(), 0755) != 0 && errno != EEXIST) {
			error = "Unable to create update directory.";
			::close(directoryFd);
			return false;
		}
		const int nextFd = ::openat(directoryFd, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		::close(directoryFd);
		if (nextFd < 0) {
			error = "Unsafe update directory path.";
			return false;
		}
		struct stat status {};
		if (::fstat(nextFd, &status) != 0 || status.st_uid != 0 || (status.st_mode & 0022) != 0) {
			error = "Update directory is not exclusively controlled by root.";
			::close(nextFd);
			return false;
		}
		directoryFd = nextFd;
		remaining = slash == std::string_view::npos ? std::string_view() : remaining.substr(slash + 1);
	}
	return true;
}

struct StagedTarget {
	int directoryFd = -1;
	std::string temporaryName;
};

void cleanupStaged(std::array<StagedTarget, kUpdateFileCount> &staged) {
	for (StagedTarget &target : staged) {
		if (target.directoryFd >= 0 && !target.temporaryName.empty()) ::unlinkat(target.directoryFd, target.temporaryName.c_str(), 0);
		if (target.directoryFd >= 0) ::close(target.directoryFd);
		target.directoryFd = -1;
	}
}

bool stageTargetFile(int rootFd, std::size_t index, const std::vector<unsigned char> &data, StagedTarget &staged, std::string &error) {
	const UpdateTarget &target = kUpdateTargets[index];
	int firstFd = -1;
	int secondFd = -1;
	if (!splitDirectoryPath(rootFd, target.directoryA, firstFd, error)) return false;
	if (!splitDirectoryPath(firstFd, target.directoryB, secondFd, error)) {
		::close(firstFd);
		return false;
	}
	::close(firstFd);
	if (!splitDirectoryPath(secondFd, target.directoryC, staged.directoryFd, error)) {
		::close(secondFd);
		return false;
	}
	::close(secondFd);
	staged.temporaryName = ".mr-update-" + std::to_string(static_cast<long long>(::getpid())) + "-" + std::to_string(index);
	const int fileFd = ::openat(staged.directoryFd, staged.temporaryName.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fileFd < 0) {
		error = "Unable to stage update file.";
		return false;
	}
	const bool written = writeAll(fileFd, data.data(), data.size()) && ::fchmod(fileFd, target.mode) == 0 && ::fchown(fileFd, 0, 0) == 0 && ::fsync(fileFd) == 0;
	::close(fileFd);
	if (!written) {
		error = "Unable to write staged update file.";
		return false;
	}
	return true;
}

bool installStagedTargets(std::array<StagedTarget, kUpdateFileCount> &staged, std::string &error) {
	const std::array<std::size_t, kUpdateFileCount> order = {1, 2, 3, 4, 5, 0};
	for (const std::size_t index : order) {
		StagedTarget &target = staged[index];
		if (::renameat(target.directoryFd, target.temporaryName.c_str(), target.directoryFd, kUpdateTargets[index].fileName) != 0 || ::fsync(target.directoryFd) != 0) {
			error = "Unable to atomically install update file.";
			return false;
		}
		target.temporaryName.clear();
	}
	return true;
}

} // namespace

const std::array<UpdateTarget, kUpdateFileCount> kUpdateTargets = {{
	{"bin/mr", "bin_mr_sha256", "usr", "local", "bin", "mr", 0755, 256 * 1024 * 1024},
	{"bin/mr.hlp", "help_sha256", "usr", "local", "bin", "mr.hlp", 0644, 64 * 1024 * 1024},
	{"share/doc/mr/mr-users-manual.pdf", "users_manual_sha256", "usr", "local/share", "doc/mr", "mr-users-manual.pdf", 0644, 256 * 1024 * 1024},
	{"share/doc/mr/mr-macro-reference.pdf", "macro_reference_sha256", "usr", "local/share", "doc/mr", "mr-macro-reference.pdf", 0644, 256 * 1024 * 1024},
	{"share/doc/mr/mr-technical-manual.pdf", "technical_manual_sha256", "usr", "local/share", "doc/mr", "mr-technical-manual.pdf", 0644, 256 * 1024 * 1024},
	{"share/licenses/mr/TVISION-COPYRIGHT", "license_sha256", "usr", "local/share", "licenses/mr", "TVISION-COPYRIGHT", 0644, 4 * 1024 * 1024},
}};

bool ensureUpdatePrivileges() {
	char password[kPasswordCapacity]{};
	std::size_t passwordLength = 0;

	if (validateSudoWithoutPassword()) return true;
	if (::access(kSudoExecutable, X_OK) != 0) {
		mr::messageline::postAutoTimed(mr::messageline::Owner::ApplicationUpdate, "sudo is required for the system update.", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
		return false;
	}
	if (!promptSudoPassword(password, passwordLength)) {
		secureClear(password, sizeof(password));
		mr::messageline::postAutoTimed(mr::messageline::Owner::ApplicationUpdate, "Update cancelled.", mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
		return false;
	}
	if (validateSudoWithPassword(password, passwordLength)) return true;
	mr::messageline::postAutoTimed(mr::messageline::Owner::ApplicationUpdate, "sudo authentication failed.", mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
	return false;
}

mr::coprocessor::Result applyUpdatePackage(const mr::coprocessor::TaskInfo &task, std::shared_ptr<const UpdatePackagePayload> package) {
	mr::coprocessor::Result result;
	std::string error;
	if (task.cancelRequested()) {
		result.status = mr::coprocessor::TaskStatus::Cancelled;
		return result;
	}
	if (package == nullptr || !applyPackageThroughSudo(*package, error)) {
		result.status = mr::coprocessor::TaskStatus::Failed;
		result.error = error.empty() ? "Unable to install update." : error;
		return result;
	}
	const std::string macroWarning = installUserMacros(package->macros);
	result.status = mr::coprocessor::TaskStatus::Completed;
	result.payload = std::make_shared<UpdateAppliedPayload>(package->manifest.version, package->manifest.changedText, macroWarning);
	return result;
}

bool showChangedAndRestart(const UpdateAppliedPayload &payload) {
	if (TProgram::deskTop == nullptr) return false;
	UpdateChangedDialog *dialog = new UpdateChangedDialog(payload.version, payload.changedText);
	if (dialog == nullptr) return false;
	const ushort result = TProgram::deskTop->execView(dialog);
	TObject::destroy(dialog);
	if (result != cmMrUpdateRestart) return false;
	return requestMRRestartWithDirtyGating();
}

bool runInternalUpdateApply(std::string &error) {
	UpdatePackagePayload package;
	std::array<StagedTarget, kUpdateFileCount> staged;
	if (::geteuid() != 0) {
		error = "Internal update mode requires root privileges.";
		return false;
	}
	if (!currentExecutableIsSystemBinary()) {
		error = "Internal update mode must run from /usr/local/bin/mr.";
		return false;
	}
	if (!readUpdateProtocol(STDIN_FILENO, package, error) || !verifyManifestSignature(package.manifestBytes, package.signature, error) || !parseManifest(package.manifestBytes, package.manifest, error)) return false;
	if (!versionIsNewer(package.manifest.version, mrDisplayVersion())) {
		error = "The signed update is not newer than the installed version.";
		return false;
	}
	for (std::size_t index = 0; index < package.files.size(); ++index)
		if (sha256Hex(package.files[index]) != package.manifest.fileHashes[index]) {
			error = "Privileged update file verification failed.";
			return false;
		}
	const int rootFd = ::open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (rootFd < 0) {
		error = "Unable to open the installation root.";
		return false;
	}
	bool success = true;
	for (std::size_t index = 0; index < package.files.size(); ++index)
		if (!stageTargetFile(rootFd, index, package.files[index], staged[index], error)) {
			success = false;
			break;
		}
	::close(rootFd);
	if (success) success = installStagedTargets(staged, error);
	cleanupStaged(staged);
	return success;
}

} // namespace update_internal
} // namespace mr
