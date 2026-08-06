#include "MRPrivilegedFileBroker.hpp"

#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <grp.h>
#include <limits>
#include <pwd.h>
#include <signal.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/xattr.h>
#endif

namespace {

constexpr int kClientSocketDescriptor = 198;
constexpr std::uint32_t kProtocolMagic = 0x4D524642u;
constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kMaximumPacketSize = 8192;
constexpr std::size_t kMaximumErrorLength = 2048;
constexpr const char *kBrokerEnvironmentMarker = "MR_PRIVILEGED_FILE_BROKER";

enum class BrokerOperation : std::uint16_t {
	AllowsPath = 1,
	OpenReadOnly = 2,
	BeginSave = 3,
	CommitSave = 4,
	AbortSave = 5
};

struct RequestHeader {
	std::uint32_t magic;
	std::uint16_t version;
	std::uint16_t operation;
	std::uint32_t flags;
	std::uint32_t pathLength;
};

struct ResponseHeader {
	std::uint32_t magic;
	std::uint16_t version;
	std::uint16_t reserved;
	std::int32_t status;
	std::uint32_t errorLength;
};

struct AuthorizedFile {
	std::string canonicalPath;
	std::string baseName;
	int directoryDescriptor;
	int sourceDescriptor;
	struct stat baseline;

	AuthorizedFile() noexcept : canonicalPath(), baseName(), directoryDescriptor(-1), sourceDescriptor(-1), baseline() {
		std::memset(&baseline, 0, sizeof(baseline));
	}

	AuthorizedFile(AuthorizedFile &&other) noexcept
	    : canonicalPath(std::move(other.canonicalPath)), baseName(std::move(other.baseName)), directoryDescriptor(other.directoryDescriptor), sourceDescriptor(other.sourceDescriptor), baseline(other.baseline) {
		other.directoryDescriptor = -1;
		other.sourceDescriptor = -1;
	}

	AuthorizedFile &operator=(AuthorizedFile &&other) noexcept {
		if (this == &other) return *this;
		if (directoryDescriptor >= 0) ::close(directoryDescriptor);
		if (sourceDescriptor >= 0) ::close(sourceDescriptor);
		canonicalPath = std::move(other.canonicalPath);
		baseName = std::move(other.baseName);
		directoryDescriptor = other.directoryDescriptor;
		sourceDescriptor = other.sourceDescriptor;
		baseline = other.baseline;
		other.directoryDescriptor = -1;
		other.sourceDescriptor = -1;
		return *this;
	}

	AuthorizedFile(const AuthorizedFile &) = delete;
	AuthorizedFile &operator=(const AuthorizedFile &) = delete;

	~AuthorizedFile() {
		if (directoryDescriptor >= 0) ::close(directoryDescriptor);
		if (sourceDescriptor >= 0) ::close(sourceDescriptor);
	}
};

struct SaveTransaction {
	AuthorizedFile *file;
	int descriptor;
	std::string temporaryName;
	bool backupEnabled;

	SaveTransaction() noexcept : file(nullptr), descriptor(-1), temporaryName(), backupEnabled(false) {
	}
};

bool parseUnsignedId(const char *text, unsigned long maximum, unsigned long &value) noexcept {
	char *end = nullptr;
	unsigned long parsed = 0;

	if (text == nullptr || *text == '\0' || *text == '-') return false;
	errno = 0;
	parsed = std::strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || parsed > maximum) return false;
	value = parsed;
	return true;
}

bool isRunMacroAssignment(std::string_view argument) noexcept {
	static constexpr std::string_view prefix = "--run-macro=";
	return argument.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> commandLineFileArguments(int argc, char **argv) {
	std::vector<std::string> paths;
	bool skipNext = false;

	for (int index = 1; argv != nullptr && index < argc; ++index) {
		const char *rawArgument = argv[index];
		std::string argument = rawArgument != nullptr ? rawArgument : "";

		if (skipNext) {
			skipNext = false;
			continue;
		}
		if (argument == "--run-macro" || argument == "-rm") {
			skipNext = true;
			continue;
		}
		if (argument == "--load-recursive" || argument == "-lr" || argument == "--exit-after-run-macro" || isRunMacroAssignment(argument)) continue;
		if (!argument.empty()) paths.push_back(std::move(argument));
	}
	return paths;
}

std::string expandInvokingUserHome(std::string path, const std::string &home) {
	if (path == "~") return home;
	if (path.size() >= 2 && path[0] == '~' && path[1] == '/') return home + path.substr(1);
	return path;
}

bool statFingerprintEqual(const struct stat &left, const struct stat &right) noexcept {
	return left.st_dev == right.st_dev && left.st_ino == right.st_ino && left.st_size == right.st_size && left.st_mtim.tv_sec == right.st_mtim.tv_sec &&
	       left.st_mtim.tv_nsec == right.st_mtim.tv_nsec && left.st_ctim.tv_sec == right.st_ctim.tv_sec && left.st_ctim.tv_nsec == right.st_ctim.tv_nsec;
}

bool authorizeFile(const std::string &rawPath, const std::string &home, std::vector<AuthorizedFile> &files) {
	std::array<char, PATH_MAX> resolved{};
	const std::string expandedPath = expandInvokingUserHome(rawPath, home);
	struct stat fileStatus {};
	std::filesystem::path canonicalPath;
	std::filesystem::path parentPath;
	std::string baseName;
	int directoryDescriptor = -1;
	int sourceDescriptor = -1;

	if (::realpath(expandedPath.c_str(), resolved.data()) == nullptr) return false;
	for (const AuthorizedFile &existing : files)
		if (existing.canonicalPath == resolved.data()) return true;

	canonicalPath = std::filesystem::path(resolved.data());
	parentPath = canonicalPath.parent_path();
	baseName = canonicalPath.filename().string();
	if (parentPath.empty() || baseName.empty()) return false;

	directoryDescriptor = ::open(parentPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (directoryDescriptor < 0) return false;
	sourceDescriptor = ::openat(directoryDescriptor, baseName.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (sourceDescriptor < 0) {
		::close(directoryDescriptor);
		return false;
	}
	if (::fstat(sourceDescriptor, &fileStatus) != 0 || !S_ISREG(fileStatus.st_mode)) {
		::close(sourceDescriptor);
		::close(directoryDescriptor);
		return false;
	}

	AuthorizedFile file;
	file.canonicalPath = canonicalPath.string();
	file.baseName = std::move(baseName);
	file.directoryDescriptor = directoryDescriptor;
	file.sourceDescriptor = sourceDescriptor;
	file.baseline = fileStatus;
	files.push_back(std::move(file));
	return true;
}

bool sendRequest(int socketDescriptor, BrokerOperation operation, std::string_view path, std::uint32_t flags) noexcept {
	RequestHeader header{kProtocolMagic, kProtocolVersion, static_cast<std::uint16_t>(operation), flags, static_cast<std::uint32_t>(path.size())};
	iovec vectors[2]{};
	msghdr message{};

	if (path.size() > kMaximumPacketSize - sizeof(header)) {
		errno = ENAMETOOLONG;
		return false;
	}
	vectors[0].iov_base = &header;
	vectors[0].iov_len = sizeof(header);
	vectors[1].iov_base = const_cast<char *>(path.data());
	vectors[1].iov_len = path.size();
	message.msg_iov = vectors;
	message.msg_iovlen = path.empty() ? 1 : 2;

	for (;;) {
		if (::sendmsg(socketDescriptor, &message, MSG_NOSIGNAL) >= 0) return true;
		if (errno != EINTR) return false;
	}
}

bool receiveResponse(int socketDescriptor, int &status, std::string &error, int &receivedDescriptor) noexcept {
	std::array<char, kMaximumPacketSize> packet{};
	std::array<char, CMSG_SPACE(sizeof(int))> control{};
	iovec vector{packet.data(), packet.size()};
	msghdr message{};
	ssize_t received = -1;

	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	message.msg_control = control.data();
	message.msg_controllen = control.size();
	for (;;) {
		received = ::recvmsg(socketDescriptor, &message, 0);
		if (received >= 0 || errno != EINTR) break;
	}
	if (received < static_cast<ssize_t>(sizeof(ResponseHeader)) || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
		error = received < 0 ? std::strerror(errno) : "Invalid privileged file broker response.";
		return false;
	}

	ResponseHeader header{};
	std::memcpy(&header, packet.data(), sizeof(header));
	if (header.magic != kProtocolMagic || header.version != kProtocolVersion || header.errorLength > kMaximumErrorLength ||
	    sizeof(header) + header.errorLength != static_cast<std::size_t>(received)) {
		error = "Invalid privileged file broker response.";
		return false;
	}
	status = header.status;
	error.assign(packet.data() + sizeof(header), header.errorLength);
	receivedDescriptor = -1;
	for (cmsghdr *controlMessage = CMSG_FIRSTHDR(&message); controlMessage != nullptr; controlMessage = CMSG_NXTHDR(&message, controlMessage)) {
		if (controlMessage->cmsg_level != SOL_SOCKET || controlMessage->cmsg_type != SCM_RIGHTS || controlMessage->cmsg_len < CMSG_LEN(sizeof(int))) continue;
		std::memcpy(&receivedDescriptor, CMSG_DATA(controlMessage), sizeof(receivedDescriptor));
		break;
	}
	return true;
}

bool brokerCall(BrokerOperation operation, std::string_view path, std::uint32_t flags, int &status, std::string &error, int &receivedDescriptor) noexcept {
	if (!mrPrivilegedFileBrokerAvailable()) {
		status = ENOTCONN;
		error = "Privileged file broker is not available.";
		return false;
	}
	if (!sendRequest(kClientSocketDescriptor, operation, path, flags)) {
		status = errno;
		error = std::strerror(errno);
		return false;
	}
	return receiveResponse(kClientSocketDescriptor, status, error, receivedDescriptor);
}

bool sendResponse(int socketDescriptor, int status, std::string_view error, int descriptor = -1) noexcept {
	if (error.size() > kMaximumErrorLength) error = error.substr(0, kMaximumErrorLength);
	ResponseHeader header{kProtocolMagic, kProtocolVersion, 0, status, static_cast<std::uint32_t>(error.size())};
	iovec vectors[2]{};
	std::array<char, CMSG_SPACE(sizeof(int))> control{};
	msghdr message{};

	vectors[0].iov_base = &header;
	vectors[0].iov_len = sizeof(header);
	vectors[1].iov_base = const_cast<char *>(error.data());
	vectors[1].iov_len = error.size();
	message.msg_iov = vectors;
	message.msg_iovlen = error.empty() ? 1 : 2;
	if (descriptor >= 0) {
		message.msg_control = control.data();
		message.msg_controllen = control.size();
		cmsghdr *controlMessage = CMSG_FIRSTHDR(&message);
		controlMessage->cmsg_level = SOL_SOCKET;
		controlMessage->cmsg_type = SCM_RIGHTS;
		controlMessage->cmsg_len = CMSG_LEN(sizeof(int));
		std::memcpy(CMSG_DATA(controlMessage), &descriptor, sizeof(descriptor));
		message.msg_controllen = control.size();
	}

	for (;;) {
		if (::sendmsg(socketDescriptor, &message, MSG_NOSIGNAL) >= 0) return true;
		if (errno != EINTR) return false;
	}
}

bool receiveRequest(int socketDescriptor, BrokerOperation &operation, std::uint32_t &flags, std::string &path, bool &valid) noexcept {
	std::array<char, kMaximumPacketSize> packet{};
	ssize_t received = -1;

	valid = false;
	for (;;) {
		received = ::recv(socketDescriptor, packet.data(), packet.size(), 0);
		if (received >= 0 || errno != EINTR) break;
	}
	if (received == 0) return false;
	if (received < static_cast<ssize_t>(sizeof(RequestHeader))) {
		static_cast<void>(sendResponse(socketDescriptor, EPROTO, "Invalid privileged file broker request."));
		return true;
	}
	RequestHeader header{};
	std::memcpy(&header, packet.data(), sizeof(header));
	if (header.magic != kProtocolMagic || header.version != kProtocolVersion || sizeof(header) + header.pathLength != static_cast<std::size_t>(received)) {
		static_cast<void>(sendResponse(socketDescriptor, EPROTO, "Invalid privileged file broker request."));
		return true;
	}
	operation = static_cast<BrokerOperation>(header.operation);
	flags = header.flags;
	path.assign(packet.data() + sizeof(header), header.pathLength);
	valid = true;
	return true;
}

AuthorizedFile *findAuthorizedFile(std::string_view requestedPath, std::vector<AuthorizedFile> &files) noexcept {
	std::array<char, PATH_MAX> resolved{};
	std::string path(requestedPath);

	if (path.empty()) return nullptr;
	for (AuthorizedFile &file : files)
		if (file.canonicalPath == path) return &file;
	if (::realpath(path.c_str(), resolved.data()) == nullptr) return nullptr;
	for (AuthorizedFile &file : files)
		if (file.canonicalPath == resolved.data()) return &file;
	return nullptr;
}

std::string backupBaseName(const AuthorizedFile &file) {
	std::filesystem::path backup(file.baseName);
	backup.replace_extension(".bak");
	std::string name = backup.filename().string();
	if (name == file.baseName) name += ".bak";
	return name;
}

void clearSaveTransaction(SaveTransaction &transaction, bool removeTemporaryFile) noexcept {
	if (transaction.descriptor >= 0) ::close(transaction.descriptor);
	if (removeTemporaryFile && transaction.file != nullptr && !transaction.temporaryName.empty())
		static_cast<void>(::unlinkat(transaction.file->directoryDescriptor, transaction.temporaryName.c_str(), 0));
	transaction.file = nullptr;
	transaction.descriptor = -1;
	transaction.temporaryName.clear();
	transaction.backupEnabled = false;
}

int createTemporaryFile(AuthorizedFile &file, std::string &temporaryName) noexcept {
	const long long processId = static_cast<long long>(::getpid());

	for (unsigned int attempt = 0; attempt < 100; ++attempt) {
		temporaryName = ".mr-save-" + std::to_string(processId) + "-" + std::to_string(attempt);
		int descriptor = ::openat(file.directoryDescriptor, temporaryName.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
		if (descriptor >= 0) return descriptor;
		if (errno != EEXIST) return -1;
	}
	errno = EEXIST;
	return -1;
}

#if defined(__linux__)
void copyExtendedAttributes(int sourceDescriptor, int targetDescriptor) noexcept {
	ssize_t listLength = ::flistxattr(sourceDescriptor, nullptr, 0);
	if (listLength <= 0) return;
	std::vector<char> names(static_cast<std::size_t>(listLength));
	listLength = ::flistxattr(sourceDescriptor, names.data(), names.size());
	if (listLength <= 0) return;

	std::size_t offset = 0;
	while (offset < static_cast<std::size_t>(listLength)) {
		const char *name = names.data() + offset;
		const std::size_t nameLength = std::strlen(name);
		if (nameLength == 0 || offset + nameLength >= static_cast<std::size_t>(listLength)) break;
		ssize_t valueLength = ::fgetxattr(sourceDescriptor, name, nullptr, 0);
		if (valueLength >= 0) {
			std::vector<char> value(static_cast<std::size_t>(valueLength));
			if (valueLength == 0 || ::fgetxattr(sourceDescriptor, name, value.data(), value.size()) == valueLength)
				static_cast<void>(::fsetxattr(targetDescriptor, name, value.data(), value.size(), 0));
		}
		offset += nameLength + 1;
	}
}
#else
void copyExtendedAttributes(int, int) noexcept {
}
#endif

bool beginSave(AuthorizedFile &file, bool backupEnabled, SaveTransaction &transaction, std::string &error) {
	struct stat current {};

	if (transaction.file != nullptr) {
		error = "A privileged save transaction is already active.";
		errno = EBUSY;
		return false;
	}
	if (::fstatat(file.directoryDescriptor, file.baseName.c_str(), &current, AT_SYMLINK_NOFOLLOW) != 0) {
		error = std::strerror(errno);
		return false;
	}
	if (!S_ISREG(current.st_mode) || !statFingerprintEqual(current, file.baseline)) {
		error = "The file changed after it was opened; privileged save was not committed.";
		errno = ESTALE;
		return false;
	}
	transaction.descriptor = createTemporaryFile(file, transaction.temporaryName);
	if (transaction.descriptor < 0) {
		error = std::strerror(errno);
		return false;
	}
	transaction.file = &file;
	transaction.backupEnabled = backupEnabled;
	return true;
}

bool commitSave(SaveTransaction &transaction, std::string &error) {
	AuthorizedFile &file = *transaction.file;
	const std::string backupName = transaction.backupEnabled ? backupBaseName(file) : std::string();
	const mode_t originalMode = file.baseline.st_mode & 07777;
	struct stat current {};
	bool backupMoved = false;

	if (::fsync(transaction.descriptor) != 0) {
		error = std::strerror(errno);
		return false;
	}
	if (::fstatat(file.directoryDescriptor, file.baseName.c_str(), &current, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(current.st_mode) ||
	    !statFingerprintEqual(current, file.baseline)) {
		error = "The file changed during the privileged save; the new content was not committed.";
		errno = ESTALE;
		return false;
	}
	if (::fchown(transaction.descriptor, file.baseline.st_uid, file.baseline.st_gid) != 0 || ::fchmod(transaction.descriptor, originalMode) != 0) {
		error = std::strerror(errno);
		return false;
	}
	copyExtendedAttributes(file.sourceDescriptor, transaction.descriptor);
	if (transaction.backupEnabled) {
		if (::unlinkat(file.directoryDescriptor, backupName.c_str(), 0) != 0 && errno != ENOENT) {
			error = std::strerror(errno);
			return false;
		}
		if (::renameat(file.directoryDescriptor, file.baseName.c_str(), file.directoryDescriptor, backupName.c_str()) != 0) {
			error = std::strerror(errno);
			return false;
		}
		backupMoved = true;
	}
	if (::renameat(file.directoryDescriptor, transaction.temporaryName.c_str(), file.directoryDescriptor, file.baseName.c_str()) != 0) {
		const int renameError = errno;
		if (backupMoved) static_cast<void>(::renameat(file.directoryDescriptor, backupName.c_str(), file.directoryDescriptor, file.baseName.c_str()));
		error = std::strerror(renameError);
		errno = renameError;
		return false;
	}
	transaction.temporaryName.clear();
	static_cast<void>(::fsync(file.directoryDescriptor));

	int replacementDescriptor = ::openat(file.directoryDescriptor, file.baseName.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	struct stat replacementStatus {};
	if (replacementDescriptor < 0 || ::fstat(replacementDescriptor, &replacementStatus) != 0 || !S_ISREG(replacementStatus.st_mode)) {
		if (replacementDescriptor >= 0) ::close(replacementDescriptor);
		error = "Saved file could not be reopened by the privileged file broker.";
		return false;
	}
	::close(file.sourceDescriptor);
	file.sourceDescriptor = replacementDescriptor;
	file.baseline = replacementStatus;
	return true;
}

int serveBroker(int socketDescriptor, std::vector<AuthorizedFile> &files) noexcept {
	SaveTransaction transaction;

	for (;;) {
		BrokerOperation operation = BrokerOperation::AllowsPath;
		std::uint32_t flags = 0;
		std::string path;
		bool valid = false;
		if (!receiveRequest(socketDescriptor, operation, flags, path, valid)) break;
		if (!valid) continue;

		AuthorizedFile *file = nullptr;
		if (operation == BrokerOperation::AllowsPath || operation == BrokerOperation::OpenReadOnly || operation == BrokerOperation::BeginSave)
			file = findAuthorizedFile(path, files);

		switch (operation) {
			case BrokerOperation::AllowsPath:
				if (!sendResponse(socketDescriptor, file != nullptr ? 0 : EACCES, file != nullptr ? "" : "Path was not authorized for privileged access.")) return 1;
				break;
			case BrokerOperation::OpenReadOnly: {
				if (file == nullptr) {
					if (!sendResponse(socketDescriptor, EACCES, "Path was not authorized for privileged access.")) return 1;
					break;
				}
				struct stat current {};
				if (::fstat(file->sourceDescriptor, &current) != 0 || !S_ISREG(current.st_mode)) {
					const int status = errno != 0 ? errno : EIO;
					if (!sendResponse(socketDescriptor, status, std::strerror(status))) return 1;
					break;
				}
				file->baseline = current;
				int duplicate = ::fcntl(file->sourceDescriptor, F_DUPFD_CLOEXEC, 0);
				if (duplicate < 0) {
					const int status = errno;
					if (!sendResponse(socketDescriptor, status, std::strerror(status))) return 1;
					break;
				}
				const bool sent = sendResponse(socketDescriptor, 0, "", duplicate);
				::close(duplicate);
				if (!sent) return 1;
				break;
			}
			case BrokerOperation::BeginSave: {
				std::string error;
				if (file == nullptr) {
					if (!sendResponse(socketDescriptor, EACCES, "Path was not authorized for privileged access.")) return 1;
					break;
				}
				if (!beginSave(*file, (flags & 1u) != 0, transaction, error)) {
					const int status = errno != 0 ? errno : EIO;
					if (!sendResponse(socketDescriptor, status, error)) return 1;
					break;
				}
				int duplicate = ::fcntl(transaction.descriptor, F_DUPFD_CLOEXEC, 0);
				if (duplicate < 0) {
					const int status = errno;
					clearSaveTransaction(transaction, true);
					if (!sendResponse(socketDescriptor, status, std::strerror(status))) return 1;
					break;
				}
				const bool sent = sendResponse(socketDescriptor, 0, "", duplicate);
				::close(duplicate);
				if (!sent) return 1;
				break;
			}
			case BrokerOperation::CommitSave: {
				std::string error;
				if (transaction.file == nullptr) {
					if (!sendResponse(socketDescriptor, EINVAL, "No privileged save transaction is active.")) return 1;
					break;
				}
				if (!commitSave(transaction, error)) {
					const int status = errno != 0 ? errno : EIO;
					clearSaveTransaction(transaction, true);
					if (!sendResponse(socketDescriptor, status, error)) return 1;
					break;
				}
				clearSaveTransaction(transaction, false);
				if (!sendResponse(socketDescriptor, 0, "")) return 1;
				break;
			}
			case BrokerOperation::AbortSave:
				clearSaveTransaction(transaction, true);
				if (!sendResponse(socketDescriptor, 0, "")) return 1;
				break;
			default:
				if (!sendResponse(socketDescriptor, EINVAL, "Unknown privileged file broker operation.")) return 1;
				break;
		}
	}
	clearSaveTransaction(transaction, true);
	return 0;
}

bool setEnvironmentValue(const char *name, const std::string &value, std::string &error) {
	if (::setenv(name, value.c_str(), 1) == 0) return true;
	error = "Could not set environment variable ";
	error += name;
	error += ": ";
	error += std::strerror(errno);
	return false;
}

bool sanitizeInvokingUserEnvironment(const passwd &account, std::string &error) {
	const std::string home = account.pw_dir != nullptr ? account.pw_dir : "";
	const std::string user = account.pw_name != nullptr ? account.pw_name : "";
	const std::string shell = account.pw_shell != nullptr ? account.pw_shell : "";

	if (home.empty() || user.empty()) {
		error = "The invoking sudo account has no usable home directory or user name.";
		return false;
	}
	if (!setEnvironmentValue("HOME", home, error) || !setEnvironmentValue("USER", user, error) || !setEnvironmentValue("LOGNAME", user, error)) return false;
	if (!shell.empty() && !setEnvironmentValue("SHELL", shell, error)) return false;
	const char *configDirectory = std::getenv("XDG_CONFIG_HOME");
	struct stat configStatus {};
	if (configDirectory != nullptr &&
	    (*configDirectory != '/' || ::stat(configDirectory, &configStatus) != 0 || !S_ISDIR(configStatus.st_mode) || configStatus.st_uid != account.pw_uid ||
	     ::access(configDirectory, R_OK | X_OK) != 0))
		::unsetenv("XDG_CONFIG_HOME");
	::unsetenv("XDG_CACHE_HOME");
	::unsetenv("XDG_DATA_HOME");
	::unsetenv("XDG_STATE_HOME");

	const char *runtimeDirectory = std::getenv("XDG_RUNTIME_DIR");
	struct stat runtimeStatus {};
	if (runtimeDirectory != nullptr && (::stat(runtimeDirectory, &runtimeStatus) != 0 || runtimeStatus.st_uid != account.pw_uid || !S_ISDIR(runtimeStatus.st_mode)))
		::unsetenv("XDG_RUNTIME_DIR");

	::unsetenv("SUDO_COMMAND");
	::unsetenv("SUDO_GID");
	::unsetenv("SUDO_UID");
	::unsetenv("SUDO_USER");
	::unsetenv("SUDO_PS1");
	return true;
}

bool dropPrivileges(const passwd &account, gid_t invokingGroup, std::string &error) {
	if (::initgroups(account.pw_name, invokingGroup) != 0) {
		error = "Could not initialize invoking user groups: ";
		error += std::strerror(errno);
		return false;
	}
	if (::setresgid(invokingGroup, invokingGroup, invokingGroup) != 0) {
		error = "Could not drop group privileges: ";
		error += std::strerror(errno);
		return false;
	}
	if (::setresuid(account.pw_uid, account.pw_uid, account.pw_uid) != 0) {
		error = "Could not drop user privileges: ";
		error += std::strerror(errno);
		return false;
	}
	if (::getuid() != account.pw_uid || ::geteuid() != account.pw_uid || ::getgid() != invokingGroup || ::getegid() != invokingGroup) {
		error = "Privilege drop verification failed.";
		return false;
	}
	return sanitizeInvokingUserEnvironment(account, error);
}

int childExitStatus(pid_t child) noexcept {
	int status = 0;
	for (;;) {
		if (::waitpid(child, &status, 0) >= 0) break;
		if (errno != EINTR) return 1;
	}
	if (WIFEXITED(status)) return WEXITSTATUS(status);
	if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
	return 1;
}

} // namespace

MRPrivilegedFileBrokerStartup mrStartPrivilegedFileBroker(int argc, char **argv, int &exitCode, std::string &error) {
	const char *sudoUidText = std::getenv("SUDO_UID");
	const char *sudoGidText = std::getenv("SUDO_GID");
	unsigned long invokingUidValue = 0;
	unsigned long invokingGidValue = 0;
	std::array<char, 16384> accountBuffer{};
	passwd accountStorage{};
	passwd *account = nullptr;
	std::vector<AuthorizedFile> files;
	int sockets[2]{-1, -1};
#if defined(__linux__)
	const pid_t supervisorProcess = ::getpid();
#endif
	pid_t child = -1;

	exitCode = 1;
	error.clear();
	if (::geteuid() != 0 || sudoUidText == nullptr) return MRPrivilegedFileBrokerStartup::RunApplication;
	if (!parseUnsignedId(sudoUidText, std::numeric_limits<uid_t>::max(), invokingUidValue) ||
	    !parseUnsignedId(sudoGidText, std::numeric_limits<gid_t>::max(), invokingGidValue)) {
		error = "Invalid SUDO_UID or SUDO_GID; refusing to start MR with elevated application privileges.";
		return MRPrivilegedFileBrokerStartup::Failed;
	}
	if (invokingUidValue == 0) return MRPrivilegedFileBrokerStartup::RunApplication;
	if (::getpwuid_r(static_cast<uid_t>(invokingUidValue), &accountStorage, accountBuffer.data(), accountBuffer.size(), &account) != 0 || account == nullptr ||
	    account->pw_name == nullptr || account->pw_dir == nullptr) {
		error = "Could not resolve the invoking sudo account.";
		return MRPrivilegedFileBrokerStartup::Failed;
	}

	for (const std::string &path : commandLineFileArguments(argc, argv))
		static_cast<void>(authorizeFile(path, account->pw_dir, files));

	if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) {
		error = "Could not create privileged file broker channel: ";
		error += std::strerror(errno);
		return MRPrivilegedFileBrokerStartup::Failed;
	}
	child = ::fork();
	if (child < 0) {
		error = "Could not start privileged file broker: ";
		error += std::strerror(errno);
		::close(sockets[0]);
		::close(sockets[1]);
		return MRPrivilegedFileBrokerStartup::Failed;
	}
	if (child > 0) {
		::close(sockets[1]);
		static_cast<void>(::signal(SIGINT, SIG_IGN));
		static_cast<void>(::signal(SIGQUIT, SIG_IGN));
		static_cast<void>(serveBroker(sockets[0], files));
		::close(sockets[0]);
		exitCode = childExitStatus(child);
		return MRPrivilegedFileBrokerStartup::ParentFinished;
	}

	::close(sockets[0]);
#if defined(__linux__)
	if (::prctl(PR_SET_PDEATHSIG, SIGHUP) != 0 || ::getppid() != supervisorProcess) {
		error = "Could not bind the editor process lifetime to the privileged file broker.";
		::close(sockets[1]);
		return MRPrivilegedFileBrokerStartup::Failed;
	}
#endif
	if (sockets[1] != kClientSocketDescriptor) {
		if (::dup2(sockets[1], kClientSocketDescriptor) < 0) {
			error = "Could not install privileged file broker channel: ";
			error += std::strerror(errno);
			::close(sockets[1]);
			return MRPrivilegedFileBrokerStartup::Failed;
		}
		::close(sockets[1]);
	}
	int descriptorFlags = ::fcntl(kClientSocketDescriptor, F_GETFD);
	if (descriptorFlags < 0 || ::fcntl(kClientSocketDescriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) != 0) {
		error = "Could not secure privileged file broker channel.";
		::close(kClientSocketDescriptor);
		return MRPrivilegedFileBrokerStartup::Failed;
	}
	if (!setEnvironmentValue(kBrokerEnvironmentMarker, "1", error) ||
	    !dropPrivileges(*account, static_cast<gid_t>(invokingGidValue), error)) {
		::close(kClientSocketDescriptor);
		return MRPrivilegedFileBrokerStartup::Failed;
	}
	return MRPrivilegedFileBrokerStartup::RunApplication;
}

bool mrPrivilegedFileBrokerAvailable() noexcept {
	const char *marker = std::getenv(kBrokerEnvironmentMarker);
	struct stat status {};
	return marker != nullptr && std::strcmp(marker, "1") == 0 && ::fstat(kClientSocketDescriptor, &status) == 0 && S_ISSOCK(status.st_mode);
}

bool mrPrivilegedFileBrokerAllowsPath(std::string_view path) noexcept {
	int status = EIO;
	int descriptor = -1;
	std::string error;

	if (path.empty() || !brokerCall(BrokerOperation::AllowsPath, path, 0, status, error, descriptor)) return false;
	if (descriptor >= 0) ::close(descriptor);
	return status == 0;
}

int mrPrivilegedFileBrokerOpenReadOnly(std::string_view path, std::string &error) {
	int status = EIO;
	int descriptor = -1;

	if (!brokerCall(BrokerOperation::OpenReadOnly, path, 0, status, error, descriptor)) return -1;
	if (status != 0 || descriptor < 0) {
		if (descriptor >= 0) ::close(descriptor);
		if (error.empty()) error = std::strerror(status != 0 ? status : EIO);
		errno = status != 0 ? status : EIO;
		return -1;
	}
	error.clear();
	return descriptor;
}

bool mrPrivilegedFileBrokerBeginSave(std::string_view path, bool backupEnabled, int &fileDescriptor, std::string &error) {
	int status = EIO;

	fileDescriptor = -1;
	if (!brokerCall(BrokerOperation::BeginSave, path, backupEnabled ? 1u : 0u, status, error, fileDescriptor)) return false;
	if (status != 0 || fileDescriptor < 0) {
		if (fileDescriptor >= 0) ::close(fileDescriptor);
		fileDescriptor = -1;
		if (error.empty()) error = std::strerror(status != 0 ? status : EIO);
		errno = status != 0 ? status : EIO;
		return false;
	}
	error.clear();
	return true;
}

bool mrPrivilegedFileBrokerCommitSave(std::string &error) {
	int status = EIO;
	int descriptor = -1;

	if (!brokerCall(BrokerOperation::CommitSave, "", 0, status, error, descriptor)) return false;
	if (descriptor >= 0) ::close(descriptor);
	if (status != 0) {
		if (error.empty()) error = std::strerror(status);
		errno = status;
		return false;
	}
	error.clear();
	return true;
}

void mrPrivilegedFileBrokerAbortSave() noexcept {
	int status = EIO;
	int descriptor = -1;
	std::string error;

	if (!mrPrivilegedFileBrokerAvailable()) return;
	static_cast<void>(brokerCall(BrokerOperation::AbortSave, "", 0, status, error, descriptor));
	if (descriptor >= 0) ::close(descriptor);
}
