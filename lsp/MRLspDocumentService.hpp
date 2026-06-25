#ifndef MRLSPDOCUMENTSERVICE_HPP
#define MRLSPDOCUMENTSERVICE_HPP

#include "MRLspDocumentMirror.hpp"
#include "MRLspLifecycle.hpp"

#include <cstdint>
#include <map>
#include <string>

namespace mr::lsp {

struct LspDocumentSourceSnapshot {
	std::string absolutePath;
	std::string languageId;
	std::int64_t version = 0;
	std::string text;
};

class LspDocumentService {
public:
	explicit LspDocumentService(LspLifecycle &lifecycle) noexcept;

	bool open(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage);
	bool change(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage);
	bool activate(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage);
	bool close(std::string &errorMessage);
	bool close(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage);
	void clear();

	[[nodiscard]] bool isOpen() const noexcept;
	[[nodiscard]] bool hasOpenDocument(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) const;
	[[nodiscard]] bool hasOpenDocumentUri(const std::string &uri) const noexcept;
	[[nodiscard]] const std::string &documentUri() const noexcept;
	[[nodiscard]] std::int64_t sentVersion() const noexcept;
	[[nodiscard]] bool matchesSentVersion(std::int64_t version) const noexcept;
	[[nodiscard]] bool matchesSentVersion(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) const;
	[[nodiscard]] bool matchesSentVersion(const std::string &uri, std::int64_t version) const noexcept;
	[[nodiscard]] bool isStaleForSentVersion(std::int64_t version) const noexcept;
	[[nodiscard]] bool isStaleForSentVersion(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) const;
	[[nodiscard]] bool isStaleForSentVersion(const std::string &uri, std::int64_t version) const noexcept;

private:
	bool sourceToDocumentSnapshot(const LspDocumentSourceSnapshot &source, LspDocumentSnapshot &snapshot, std::string &errorMessage) const;
	bool sendMirrorNotification(const LspDocumentNotification &notification, std::string &errorMessage);

	LspLifecycle &lifecycle;
	std::map<std::string, LspDocumentMirror> mirrors;
	std::string activeUri;
};

} // namespace mr::lsp

#endif
