#ifndef MRLSPDOCUMENTMIRROR_HPP
#define MRLSPDOCUMENTMIRROR_HPP

#include <cstdint>
#include <string>

namespace mr::lsp {

struct LspDocumentSnapshot {
	std::string uri;
	std::string languageId;
	std::int64_t version = 0;
	std::string text;
};

struct LspDocumentNotification {
	std::string method;
	std::string payload;
	std::string uri;
	std::int64_t version = 0;
};

class LspDocumentMirror {
public:
	bool open(const LspDocumentSnapshot &snapshot, LspDocumentNotification &notification, std::string &errorMessage);
	bool change(const LspDocumentSnapshot &snapshot, LspDocumentNotification &notification, std::string &errorMessage);
	bool close(LspDocumentNotification &notification, std::string &errorMessage);
	void clear();

	[[nodiscard]] bool isOpen() const noexcept;
	[[nodiscard]] const std::string &documentUri() const noexcept;
	[[nodiscard]] const std::string &documentLanguageId() const noexcept;
	[[nodiscard]] std::int64_t sentVersion() const noexcept;
	[[nodiscard]] bool matchesSentVersion(std::int64_t version) const noexcept;
	[[nodiscard]] bool isStaleForSentVersion(std::int64_t version) const noexcept;

private:
	bool validateSnapshotIdentity(const LspDocumentSnapshot &snapshot, std::string &errorMessage) const;
	bool validateOpenSnapshot(const LspDocumentSnapshot &snapshot, std::string &errorMessage) const;
	bool validateChangeSnapshot(const LspDocumentSnapshot &snapshot, std::string &errorMessage) const;

	std::string uri;
	std::string languageId;
	std::int64_t lastSentVersion = 0;
	bool opened = false;
	bool hasSentVersion = false;
};

[[nodiscard]] std::string buildDidOpenPayload(const LspDocumentSnapshot &snapshot);
[[nodiscard]] std::string buildDidChangePayload(const LspDocumentSnapshot &snapshot);
[[nodiscard]] std::string buildDidClosePayload(const std::string &uri);

} // namespace mr::lsp

#endif
