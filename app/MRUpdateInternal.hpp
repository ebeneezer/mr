#ifndef MRUPDATEINTERNAL_HPP
#define MRUPDATEINTERNAL_HPP

#include "../coprocessor/MRCoprocessor.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace mr {
namespace update_internal {

constexpr char kInternalApplyOption[] = "--internal-apply-update";
constexpr char kReloadWorkspaceOption[] = "--internal-reload-workspace-after-update";
constexpr std::size_t kManifestLimit = 128 * 1024;
constexpr std::size_t kSignatureLimit = 1024;
constexpr std::size_t kUpdateFileCount = 6;

struct UpdateTarget {
	const char *archivePath;
	const char *hashField;
	const char *directoryA;
	const char *directoryB;
	const char *directoryC;
	const char *fileName;
	mode_t mode;
	std::size_t sizeLimit;
};

extern const std::array<UpdateTarget, kUpdateFileCount> kUpdateTargets;

struct UpdateManifest {
	std::string version;
	std::string build;
	std::string archiveName;
	std::string archiveHash;
	std::array<std::string, kUpdateFileCount> fileHashes;
	std::string changedText;
};

struct UpdateMacroFile {
	std::string relativePath;
	std::vector<unsigned char> data;
};

class UpdatePackagePayload final : public mr::coprocessor::Payload {
  public:
	UpdateManifest manifest;
	std::vector<unsigned char> manifestBytes;
	std::vector<unsigned char> signature;
	std::array<std::vector<unsigned char>, kUpdateFileCount> files;
	std::vector<UpdateMacroFile> macros;
};

class UpdateAppliedPayload final : public mr::coprocessor::Payload {
  public:
	UpdateAppliedPayload(std::string value, std::string changedValue, std::string warningValue)
	    : version(std::move(value)), changedText(std::move(changedValue)), warning(std::move(warningValue)) {
	}

	std::string version;
	std::string changedText;
	std::string warning;
};

class UpdateAuthorization;

bool versionIsNewer(std::string_view candidate, std::string_view current);
bool parseManifest(const std::vector<unsigned char> &bytes, UpdateManifest &manifest, std::string &error);
bool verifyManifestSignature(const std::vector<unsigned char> &manifest, const std::vector<unsigned char> &signature, std::string &error);
std::string sha256Hex(const std::vector<unsigned char> &bytes);

std::shared_ptr<UpdateAuthorization> ensureUpdatePrivileges();
mr::coprocessor::Result applyUpdatePackage(const mr::coprocessor::TaskInfo &task, std::shared_ptr<const UpdatePackagePayload> package, std::shared_ptr<UpdateAuthorization> authorization);
bool showChangedAndRestart(const UpdateAppliedPayload &payload);
bool runInternalUpdateApply(const char *protocolPath, std::string &error);

} // namespace update_internal
} // namespace mr

#endif
