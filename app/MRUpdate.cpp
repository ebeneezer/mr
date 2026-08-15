#include "MRUpdate.hpp"
#include "MRUpdateInternal.hpp"

#include "MRVersion.hpp"
#include "../coprocessor/MRCoprocessor.hpp"
#include "../mrmac/MRVM.hpp"
#include "../mrmac/mrmac.h"
#include "../mrmac/vm/MRVMHash.hpp"
#include "../mrmac/vm/MRVMProcessRuntime.hpp"
#include "../mrmac/vm/MRVMRuntimeKv.hpp"
#include "../mrmac/vm/MRVMValue.hpp"
#include "../ui/MRMenuBar.hpp"
#include "../ui/MRMessageLineController.hpp"
#include "../ui/MRWindowSupport.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

MRVMRuntimeKv &mrvmRuntimeKv() noexcept;
std::recursive_mutex &mrvmExecutionMutex() noexcept;
std::vector<std::string> mrvmProcessArguments();

namespace {

constexpr char kLatestManifestUrl[] = "https://github.com/ebeneezer/mr/releases/latest/download/mr-linux-x86_64-v3.update";
constexpr char kLatestSignatureUrl[] = "https://github.com/ebeneezer/mr/releases/latest/download/mr-linux-x86_64-v3.update.sig";
constexpr char kReleaseBaseUrl[] = "https://github.com/ebeneezer/mr/releases/download/v";
constexpr char kPlatform[] = "linux-x86_64-v3";
constexpr char kManifestHeader[] = "MR-UPDATE-MANIFEST 1\n";
constexpr std::size_t kArchiveLimit = 768 * 1024 * 1024;
constexpr std::size_t kMacroCountLimit = 256;
constexpr std::size_t kMacroFileLimit = 4 * 1024 * 1024;
constexpr std::size_t kUpdateCheckOwner = 0x4D525501;
constexpr std::size_t kUpdatePackageOwner = 0x4D525502;
constexpr std::size_t kUpdateApplyOwner = 0x4D525503;

constexpr std::array<unsigned char, 32> kUpdatePublicKey = {
	0xc5, 0x37, 0x5d, 0xda, 0x8e, 0x55, 0xfa, 0xfb, 0x86, 0x71, 0xdf, 0x3e, 0xab, 0x7d, 0x37, 0xf0,
	0xe5, 0x41, 0x6f, 0x8d, 0xa2, 0x7c, 0x89, 0x61, 0xfa, 0x62, 0xda, 0x02, 0x53, 0x69, 0xff, 0xda};

using mr::update_internal::UpdateInstallPayload;
using mr::update_internal::UpdateMacroFile;
using mr::update_internal::UpdateManifest;
using mr::update_internal::UpdatePackagePayload;
using mr::update_internal::kManifestLimit;
using mr::update_internal::kSignatureLimit;
using mr::update_internal::kUpdateFileCount;
using mr::update_internal::kUpdateTargets;

struct SemanticVersion {
	unsigned major = 0;
	unsigned minor = 0;
	unsigned patch = 0;
};

class UpdateCheckPayload final : public mr::coprocessor::Payload {
  public:
	explicit UpdateCheckPayload(UpdateManifest value, bool newer) : manifest(std::move(value)), updateAvailable(newer) {
	}

	UpdateManifest manifest;
	bool updateAvailable;
};

struct DownloadBuffer {
	std::vector<unsigned char> *bytes;
	std::size_t limit;
	bool exceeded;
};

bool parseUnsignedPart(std::string_view text, std::size_t &position, unsigned &value) {
	unsigned parsed = 0;
	std::size_t digits = 0;

	while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
		if (parsed > 1000000u) return false;
		parsed = parsed * 10u + static_cast<unsigned>(text[position] - '0');
		++position;
		++digits;
	}
	if (digits == 0) return false;
	value = parsed;
	return true;
}

bool parseSemanticVersion(std::string_view text, SemanticVersion &version) {
	std::size_t position = 0;

	if (!parseUnsignedPart(text, position, version.major) || position >= text.size() || text[position++] != '.') return false;
	if (!parseUnsignedPart(text, position, version.minor) || position >= text.size() || text[position++] != '.') return false;
	if (!parseUnsignedPart(text, position, version.patch)) return false;
	return position == text.size();
}

bool versionIsNewerLocal(std::string_view candidate, std::string_view current) {
	SemanticVersion candidateVersion;
	SemanticVersion currentVersion;

	if (!parseSemanticVersion(candidate, candidateVersion) || !parseSemanticVersion(current, currentVersion)) return false;
	if (candidateVersion.major != currentVersion.major) return candidateVersion.major > currentVersion.major;
	if (candidateVersion.minor != currentVersion.minor) return candidateVersion.minor > currentVersion.minor;
	return candidateVersion.patch > currentVersion.patch;
}

bool validHexHash(std::string_view hash) {
	if (hash.size() != 64) return false;
	for (const char ch : hash)
		if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
	return true;
}

bool validArchiveName(std::string_view name) {
	if (name.size() < 5 || name.size() > 180 || name.substr(name.size() - 4) != ".zip") return false;
	for (const char ch : name)
		if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_')) return false;
	return name.find("..") == std::string_view::npos;
}

bool takeManifestLine(std::string_view text, std::size_t &position, std::string_view &line) {
	const std::size_t end = text.find('\n', position);

	if (end == std::string_view::npos) return false;
	line = text.substr(position, end - position);
	position = end + 1;
	return true;
}

bool takeManifestField(std::string_view text, std::size_t &position, std::string_view key, std::string &value) {
	std::string_view line;

	if (!takeManifestLine(text, position, line) || line.size() <= key.size() + 1 || line.substr(0, key.size()) != key || line[key.size()] != '=') return false;
	value.assign(line.substr(key.size() + 1));
	return true;
}

bool parseManifestLocal(const std::vector<unsigned char> &bytes, UpdateManifest &manifest, std::string &error) {
	const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	std::size_t position = 0;
	std::string platform;
	std::string_view line;

	if (bytes.empty() || bytes.size() > kManifestLimit || text.substr(0, std::strlen(kManifestHeader)) != kManifestHeader) {
		error = "Invalid update manifest header.";
		return false;
	}
	position = std::strlen(kManifestHeader);
	if (!takeManifestField(text, position, "version", manifest.version) || !takeManifestField(text, position, "build", manifest.build) ||
	    !takeManifestField(text, position, "platform", platform) || !takeManifestField(text, position, "archive", manifest.archiveName) ||
	    !takeManifestField(text, position, "archive_sha256", manifest.archiveHash)) {
		error = "Incomplete update manifest.";
		return false;
	}
	for (std::size_t index = 0; index < kUpdateTargets.size(); ++index)
		if (!takeManifestField(text, position, kUpdateTargets[index].hashField, manifest.fileHashes[index])) {
			error = "Incomplete update file hash list.";
			return false;
		}
	if (!takeManifestLine(text, position, line) || !line.empty()) {
		error = "Invalid update manifest separator.";
		return false;
	}
	manifest.changedText.assign(text.substr(position));
	SemanticVersion parsedVersion;
	if (!parseSemanticVersion(manifest.version, parsedVersion) || platform != kPlatform || !validArchiveName(manifest.archiveName) || !validHexHash(manifest.archiveHash) || manifest.changedText.empty()) {
		error = "Invalid update manifest values.";
		return false;
	}
	if (manifest.build.empty() || !std::all_of(manifest.build.begin(), manifest.build.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) {
		error = "Invalid update build number.";
		return false;
	}
	for (const std::string &hash : manifest.fileHashes)
		if (!validHexHash(hash)) {
			error = "Invalid update file hash.";
			return false;
		}
	return true;
}

bool verifyManifestSignatureLocal(const std::vector<unsigned char> &manifest, const std::vector<unsigned char> &signature, std::string &error) {
	EVP_PKEY *key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, kUpdatePublicKey.data(), kUpdatePublicKey.size());
	EVP_MD_CTX *context = key != nullptr ? EVP_MD_CTX_new() : nullptr;
	bool verified = false;

	if (context != nullptr && signature.size() == 64 && EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) == 1)
		verified = EVP_DigestVerify(context, signature.data(), signature.size(), manifest.data(), manifest.size()) == 1;
	EVP_MD_CTX_free(context);
	EVP_PKEY_free(key);
	if (!verified) error = "Update manifest signature verification failed.";
	return verified;
}

std::string sha256HexLocal(const std::vector<unsigned char> &bytes) {
	std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
	unsigned digestLength = 0;
	char hex[65]{};

	if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &digestLength, EVP_sha256(), nullptr) != 1 || digestLength != 32) return std::string();
	for (std::size_t index = 0; index < digestLength; ++index) std::snprintf(hex + index * 2, 3, "%02x", digest[index]);
	return std::string(hex, 64);
}

std::size_t curlWrite(void *contents, std::size_t size, std::size_t count, void *userData) {
	DownloadBuffer *target = static_cast<DownloadBuffer *>(userData);
	const std::size_t length = size != 0 && count <= static_cast<std::size_t>(-1) / size ? size * count : 0;

	if (target == nullptr || target->bytes == nullptr || length == 0) return 0;
	if (target->bytes->size() > target->limit || length > target->limit - target->bytes->size()) {
		target->exceeded = true;
		return 0;
	}
	const unsigned char *source = static_cast<const unsigned char *>(contents);
	target->bytes->insert(target->bytes->end(), source, source + length);
	return length;
}

bool downloadHttps(CURL *curl, const std::string &url, std::size_t limit, long timeoutSeconds, std::vector<unsigned char> &bytes, std::string &error) {
	DownloadBuffer target{&bytes, limit, false};
	char curlError[CURL_ERROR_SIZE]{};

	bytes.clear();
	curl_easy_reset(curl);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "MR-editor-update/1");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &target);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	const CURLcode result = curl_easy_perform(curl);
	long responseCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
	if (result != CURLE_OK || target.exceeded || responseCode < 200 || responseCode >= 300) {
		if (target.exceeded) error = "Update download exceeded its size limit.";
		else if (curlError[0] != '\0') error = curlError;
		else
			error = "Update download failed with HTTP status " + std::to_string(responseCode) + ".";
		bytes.clear();
		return false;
	}
	return true;
}

bool downloadSignedManifest(CURL *curl, UpdateManifest &manifest, std::vector<unsigned char> &manifestBytes, std::vector<unsigned char> &signature, std::string &error) {
	if (!downloadHttps(curl, kLatestManifestUrl, kManifestLimit, 30, manifestBytes, error)) return false;
	if (!downloadHttps(curl, kLatestSignatureUrl, kSignatureLimit, 30, signature, error)) return false;
	if (!verifyManifestSignatureLocal(manifestBytes, signature, error)) return false;
	return parseManifestLocal(manifestBytes, manifest, error);
}

bool readArchiveEntry(struct archive *reader, std::size_t limit, std::vector<unsigned char> &data, std::string &error) {
	std::array<unsigned char, 64 * 1024> buffer{};
	la_ssize_t count = 0;

	data.clear();
	while ((count = archive_read_data(reader, buffer.data(), buffer.size())) > 0) {
		const std::size_t length = static_cast<std::size_t>(count);
		if (data.size() > limit || length > limit - data.size()) {
			error = "Update archive entry exceeded its size limit.";
			data.clear();
			return false;
		}
		data.insert(data.end(), buffer.begin(), buffer.begin() + count);
	}
	if (count < 0) {
		error = archive_error_string(reader) != nullptr ? archive_error_string(reader) : "Unable to read update archive entry.";
		data.clear();
		return false;
	}
	return true;
}

bool validMacroRelativePath(std::string_view path) {
	if (path.empty() || path.size() > 512 || path.substr(path.size() > 6 ? path.size() - 6 : 0) != ".mrmac") return false;
	std::size_t position = 0;
	while (position < path.size()) {
		const std::size_t end = path.find('/', position);
		const std::string_view part = path.substr(position, end == std::string_view::npos ? path.size() - position : end - position);
		if (part.empty() || part == "." || part == "..") return false;
		position = end == std::string_view::npos ? path.size() : end + 1;
	}
	return true;
}

bool extractPackage(const UpdateManifest &manifest, const std::vector<unsigned char> &archiveBytes, UpdatePackagePayload &package, std::string &error) {
	struct archive *reader = archive_read_new();
	struct archive_entry *entry = nullptr;
	std::array<bool, kUpdateFileCount> found{};
	const std::string root = manifest.archiveName.substr(0, manifest.archiveName.size() - 4) + "/";
	const std::string macroPrefix = root + "share/mr/macros/";
	bool success = reader != nullptr;

	if (!success) error = "Unable to initialize update archive reader.";
	if (success && archive_read_support_filter_none(reader) != ARCHIVE_OK) success = false;
	if (success && archive_read_support_format_zip(reader) != ARCHIVE_OK) success = false;
	if (success && archive_read_open_memory(reader, archiveBytes.data(), archiveBytes.size()) != ARCHIVE_OK) success = false;
	if (!success && error.empty()) error = archive_error_string(reader) != nullptr ? archive_error_string(reader) : "Unable to open update archive.";
	while (success && archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
		const char *rawPath = archive_entry_pathname(entry);
		const std::string path = rawPath != nullptr ? rawPath : "";
		bool handled = false;

		if (archive_entry_filetype(entry) != AE_IFREG) {
			archive_read_data_skip(reader);
			continue;
		}
		for (std::size_t index = 0; index < kUpdateTargets.size(); ++index) {
			if (path != root + kUpdateTargets[index].archivePath) continue;
			if (found[index]) {
				error = "Duplicate file in update archive.";
				success = false;
				break;
			}
			found[index] = true;
			handled = true;
			if (!readArchiveEntry(reader, kUpdateTargets[index].sizeLimit, package.files[index], error)) success = false;
			break;
		}
		if (!success) break;
		if (!handled && path.rfind(macroPrefix, 0) == 0) {
			const std::string relative = path.substr(macroPrefix.size());
			if (!validMacroRelativePath(relative) || package.macros.size() >= kMacroCountLimit) {
				error = "Invalid macro path in update archive.";
				success = false;
				break;
			}
			UpdateMacroFile macro;
			macro.relativePath = relative;
			if (!readArchiveEntry(reader, kMacroFileLimit, macro.data, error)) {
				success = false;
				break;
			}
			package.macros.push_back(std::move(macro));
			handled = true;
		}
		if (!handled) archive_read_data_skip(reader);
	}
	if (success)
		for (const bool present : found)
			if (!present) {
				error = "Required file missing from update archive.";
				success = false;
				break;
			}
	if (reader != nullptr) archive_read_free(reader);
	if (!success) return false;
	for (std::size_t index = 0; index < package.files.size(); ++index)
		if (sha256HexLocal(package.files[index]) != manifest.fileHashes[index]) {
			error = "Update file hash verification failed.";
			return false;
		}
	return true;
}

VirtualMachine::Value updateStateRoot(MRVMRuntimeKv &runtimeKv) {
	VirtualMachine::Value applicationUi = runtimeKv.ensureRoot("APPLICATIONUI");
	return runtimeKv.ensureChild(applicationUi, "update");
}

void storeUpdateInt(const char *key, int value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, updateStateRoot(runtimeKv), key, mrvmMakeInt(value));
}

void storeUpdateString(const char *key, const std::string &value) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	mrvmHashWriteValue(store, store, updateStateRoot(runtimeKv), key, mrvmMakeString(value));
}

bool findUpdateState(MRVMRuntimeKv &runtimeKv, VirtualMachine::Value &updateRoot) {
	VirtualMachine::Value applicationUi;
	return runtimeKv.findRoot("APPLICATIONUI", applicationUi) && runtimeKv.findChild(applicationUi, "update", updateRoot);
}

int readUpdateInt(const char *key) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value root;
	if (!findUpdateState(runtimeKv, root) || !mrvmHashContainsValue(store, store, root, key)) return 0;
	const VirtualMachine::Value value = mrvmHashReadValue(store, store, root, key);
	return value.type == TYPE_INT ? value.i : 0;
}

std::string readUpdateString(const char *key) {
	std::lock_guard<std::recursive_mutex> lock(mrvmExecutionMutex());
	MRVMRuntimeKv &runtimeKv = mrvmRuntimeKv();
	MRVMHashStore &store = runtimeKv.globalStore();
	VirtualMachine::Value root;
	if (!findUpdateState(runtimeKv, root) || !mrvmHashContainsValue(store, store, root, key)) return std::string();
	const VirtualMachine::Value value = mrvmHashReadValue(store, store, root, key);
	return value.type == TYPE_STR ? value.s : std::string();
}

mr::coprocessor::Result failedResult(const std::string &error) {
	mr::coprocessor::Result result;
	result.status = mr::coprocessor::TaskStatus::Failed;
	result.error = error;
	return result;
}

mr::coprocessor::Result runUpdateCheck(const mr::coprocessor::TaskInfo &task) {
	mr::coprocessor::Result result;
	std::vector<unsigned char> manifestBytes;
	std::vector<unsigned char> signature;
	UpdateManifest manifest;
	std::string error;

	if (task.cancelRequested()) {
		result.status = mr::coprocessor::TaskStatus::Cancelled;
		return result;
	}
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return failedResult("Unable to initialize HTTPS update check.");
	CURL *curl = curl_easy_init();
	const bool downloaded = curl != nullptr && downloadSignedManifest(curl, manifest, manifestBytes, signature, error);
	if (curl != nullptr) curl_easy_cleanup(curl);
	curl_global_cleanup();
	if (!downloaded) return failedResult(error.empty() ? "Unable to check for updates." : error);
	const bool newer = versionIsNewerLocal(manifest.version, mrDisplayVersion());
	result.status = mr::coprocessor::TaskStatus::Completed;
	result.payload = std::make_shared<UpdateCheckPayload>(std::move(manifest), newer);
	return result;
}

mr::coprocessor::Result downloadUpdatePackage(const mr::coprocessor::TaskInfo &task, const std::string &wantedVersion) {
	mr::coprocessor::Result result;
	std::shared_ptr<UpdatePackagePayload> package = std::make_shared<UpdatePackagePayload>();
	std::vector<unsigned char> archiveBytes;
	std::string error;

	if (task.cancelRequested()) {
		result.status = mr::coprocessor::TaskStatus::Cancelled;
		return result;
	}
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return failedResult("Unable to initialize HTTPS update download.");
	CURL *curl = curl_easy_init();
	bool success = curl != nullptr && downloadSignedManifest(curl, package->manifest, package->manifestBytes, package->signature, error);
	if (success && package->manifest.version != wantedVersion) {
		error = "The available release changed during download; please retry.";
		success = false;
	}
	if (success) {
		const std::string archiveUrl = std::string(kReleaseBaseUrl) + package->manifest.version + "/" + package->manifest.archiveName;
		success = downloadHttps(curl, archiveUrl, kArchiveLimit, 300, archiveBytes, error);
	}
	if (curl != nullptr) curl_easy_cleanup(curl);
	curl_global_cleanup();
	if (!success) return failedResult(error.empty() ? "Unable to download update package." : error);
	if (sha256HexLocal(archiveBytes) != package->manifest.archiveHash) return failedResult("Update archive hash verification failed.");
	if (!extractPackage(package->manifest, archiveBytes, *package, error)) return failedResult(error);
	result.status = mr::coprocessor::TaskStatus::Completed;
	result.payload = std::move(package);
	return result;
}

void postUpdateError(const std::string &error) {
	mr::messageline::postAutoTimed(mr::messageline::Owner::ApplicationUpdate, error.empty() ? "Update failed." : error, mr::messageline::Kind::Error, mr::messageline::kPriorityHigh);
}

} // namespace

namespace mr {
namespace update_internal {

bool versionIsNewer(std::string_view candidate, std::string_view current) {
	return versionIsNewerLocal(candidate, current);
}

bool parseManifest(const std::vector<unsigned char> &bytes, UpdateManifest &manifest, std::string &error) {
	return parseManifestLocal(bytes, manifest, error);
}

bool verifyManifestSignature(const std::vector<unsigned char> &manifest, const std::vector<unsigned char> &signature, std::string &error) {
	return verifyManifestSignatureLocal(manifest, signature, error);
}

std::string sha256Hex(const std::vector<unsigned char> &bytes) {
	return sha256HexLocal(bytes);
}

} // namespace update_internal
} // namespace mr

MRUpdateInternalStartup mrStartInternalUpdateApply(int argc, char **argv, int &exitCode, std::string &error) {
	bool requested = false;
	for (int index = 1; argv != nullptr && index < argc; ++index)
		if (argv[index] != nullptr && std::strcmp(argv[index], mr::update_internal::kInternalApplyOption) == 0) requested = true;
	if (!requested) return MRUpdateInternalStartup::RunApplication;
	if (argc != 3 || argv[2] == nullptr || argv[2][0] != '/') {
		error = "Internal update mode requires an absolute protocol file path.";
		exitCode = 2;
		return MRUpdateInternalStartup::Failed;
	}
	if (!mr::update_internal::runInternalUpdateApply(argv[2], error)) {
		exitCode = 1;
		return MRUpdateInternalStartup::Failed;
	}
	exitCode = 0;
	return MRUpdateInternalStartup::ParentFinished;
}

void mrStartAutomaticUpdateCheck() {
	storeUpdateInt("available", 0);
	storeUpdateInt("busy", 1);
	const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::Custom, 0, 0, mr::coprocessor::ExecutionOwnerKind::Worker, kUpdateCheckOwner, "application update check", runUpdateCheck);
	if (taskId == 0) storeUpdateInt("busy", 0);
}

bool mrAdoptUpdateCoprocessorResult(const mr::coprocessor::Result &result) {
	if (result.task.executionOwnerKind != mr::coprocessor::ExecutionOwnerKind::Worker) return false;
	if (result.task.executionOwnerLocalId == kUpdateCheckOwner) {
		storeUpdateInt("busy", 0);
		const UpdateCheckPayload *payload = dynamic_cast<const UpdateCheckPayload *>(result.payload.get());
		if (result.completed() && payload != nullptr && payload->updateAvailable) {
			storeUpdateInt("available", 1);
			storeUpdateString("version", payload->manifest.version);
			mr::messageline::postTimed(mr::messageline::Owner::ApplicationUpdate, "Update V" + payload->manifest.version + " available — see Help.", mr::messageline::Kind::Warning, std::chrono::seconds(7), mr::messageline::kPriorityHigh);
		}
		mr::coprocessor::globalCoprocessor().noteResultAdoption(result, true);
		return true;
	}
	if (result.task.executionOwnerLocalId == kUpdatePackageOwner) {
		storeUpdateInt("busy", 0);
		const UpdatePackagePayload *rawPayload = dynamic_cast<const UpdatePackagePayload *>(result.payload.get());
		if (!result.completed() || rawPayload == nullptr) {
			postUpdateError(result.error);
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, true);
			return true;
		}
		std::shared_ptr<const UpdatePackagePayload> payload = std::dynamic_pointer_cast<const UpdatePackagePayload>(result.payload);
		std::shared_ptr<mr::update_internal::UpdateAuthorization> authorization = mr::update_internal::ensureUpdatePrivileges();
		if (authorization == nullptr) {
			mr::coprocessor::globalCoprocessor().noteResultAdoption(result, true);
			return true;
		}
		storeUpdateInt("busy", 1);
		const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::Custom, 0, 0, mr::coprocessor::ExecutionOwnerKind::Worker, kUpdateApplyOwner, "application update install",
		                                                                            [payload, authorization](const mr::coprocessor::TaskInfo &task) { return mr::update_internal::applyUpdatePackage(task, payload, authorization); });
		if (taskId == 0) {
			storeUpdateInt("busy", 0);
			postUpdateError("Unable to start the update installation worker.");
		}
		mr::coprocessor::globalCoprocessor().noteResultAdoption(result, true);
		return true;
	}
	if (result.task.executionOwnerLocalId == kUpdateApplyOwner) {
		storeUpdateInt("busy", 0);
		const UpdateInstallPayload *payload = dynamic_cast<const UpdateInstallPayload *>(result.payload.get());
		if (payload != nullptr && !payload->diagnostic.empty()) mrLogMessage("Application update privilege process:\n" + payload->diagnostic);
		if (!result.completed() || payload == nullptr) postUpdateError(result.error);
		else {
			storeUpdateInt("available", 0);
			mr::messageline::clearOwner(mr::messageline::Owner::ApplicationUpdate);
			if (!payload->warning.empty()) mr::messageline::postTimed(mr::messageline::Owner::ApplicationUpdate, payload->warning, mr::messageline::Kind::Warning, std::chrono::seconds(7), mr::messageline::kPriorityHigh);
			static_cast<void>(mr::update_internal::showChangedAndRestart(*payload));
		}
		mr::coprocessor::globalCoprocessor().noteResultAdoption(result, true);
		return true;
	}
	return false;
}

bool mrHandleUpdateCommand() {
	if (!mrUpdateAvailable() || readUpdateInt("busy") != 0) return true;
	const std::string version = mrUpdateAvailableVersion();
	if (version.empty()) return true;
	storeUpdateInt("busy", 1);
	mr::messageline::postSticky(mr::messageline::Owner::ApplicationUpdate, "Updating to V" + version + " - one moment please.", mr::messageline::Kind::Warning, mr::messageline::kPriorityHigh);
	const std::uint64_t taskId = mr::coprocessor::globalCoprocessor().submit(mr::coprocessor::Lane::Io, mr::coprocessor::TaskKind::Custom, 0, 0, mr::coprocessor::ExecutionOwnerKind::Worker, kUpdatePackageOwner, "application update download",
	                                                                        [version](const mr::coprocessor::TaskInfo &task) { return downloadUpdatePackage(task, version); });
	if (taskId == 0) {
		storeUpdateInt("busy", 0);
		postUpdateError("Unable to start the update download worker.");
	}
	return true;
}

bool mrUpdateAvailable() {
	return readUpdateInt("available") != 0;
}

std::string mrUpdateAvailableVersion() {
	return readUpdateString("version");
}

void mrRefreshUpdateMenu(MRMenuBar *menuBar) {
	const bool available = mrUpdateAvailable();
	if (menuBar != nullptr) menuBar->setUpdateMenuState(mrUpdateAvailableVersion(), available, available && readUpdateInt("busy") == 0);
}

bool mrUpdateForcesWorkspaceRestore() {
	for (const std::string &argument : mrvmProcessArguments())
		if (argument == mr::update_internal::kReloadWorkspaceOption) return true;
	return false;
}
