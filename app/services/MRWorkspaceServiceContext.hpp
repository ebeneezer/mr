#ifndef MRWORKSPACESERVICECONTEXT_HPP
#define MRWORKSPACESERVICECONTEXT_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace mr::services {

struct MRWorkspaceDocumentSnapshot {
	int bufferId = 0;
	std::size_t documentId = 0;
	std::size_t documentVersion = 0;
	std::string path;
	std::string languageName;
	bool mainFile = false;
};

struct MRWorkspaceMainFileState {
	bool hasMainFile = false;
	int bufferId = 0;
	std::string path;
};

struct MRServiceRootContext {
	bool hasRoot = false;
	std::string rootPath;
	std::string reason;
};

struct MRWorkspaceServiceSnapshot {
	std::vector<MRWorkspaceDocumentSnapshot> documents;
	MRWorkspaceMainFileState mainFile;
	MRServiceRootContext root;
};

class MRWorkspaceServiceContext {
public:
	void clearMainFile() noexcept;
	void setMainFileByBufferId(int bufferId) noexcept;
	void setMainFileByPath(const std::string &path);

	[[nodiscard]] MRWorkspaceMainFileState configuredMainFile() const;
	[[nodiscard]] MRWorkspaceServiceSnapshot buildSnapshot(const std::vector<MRWorkspaceDocumentSnapshot> &documents) const;

private:
	enum MainFileKind {
		mfkNone = 0,
		mfkBufferId,
		mfkPath
	};

	MainFileKind mainFileKind = mfkNone;
	int mainFileBufferId = 0;
	std::string mainFilePath;
};

[[nodiscard]] std::vector<MRWorkspaceDocumentSnapshot> collectCurrentWorkspaceDocuments();
[[nodiscard]] MRWorkspaceServiceSnapshot buildCurrentWorkspaceServiceSnapshot(const MRWorkspaceServiceContext &context);
[[nodiscard]] std::string normalizeWorkspaceServicePath(const std::string &path);

} // namespace mr::services

#endif
