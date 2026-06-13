#include "MRLspDocumentService.hpp"

#include "MRLspUri.hpp"

namespace mr::lsp {

LspDocumentService::LspDocumentService(LspLifecycle &lifecycleRef) noexcept : lifecycle(lifecycleRef), mirror() {
}

bool LspDocumentService::open(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) {
	LspDocumentMirror candidate = mirror;
	LspDocumentSnapshot documentSnapshot;
	LspDocumentNotification notification;

	if (!sourceToDocumentSnapshot(snapshot, documentSnapshot, errorMessage)) return false;
	if (!candidate.open(documentSnapshot, notification, errorMessage)) return false;
	if (!sendMirrorNotification(notification, errorMessage)) return false;
	mirror = candidate;
	return true;
}

bool LspDocumentService::change(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) {
	LspDocumentMirror candidate = mirror;
	LspDocumentSnapshot documentSnapshot;
	LspDocumentNotification notification;

	if (!sourceToDocumentSnapshot(snapshot, documentSnapshot, errorMessage)) return false;
	if (!candidate.change(documentSnapshot, notification, errorMessage)) return false;
	if (!sendMirrorNotification(notification, errorMessage)) return false;
	mirror = candidate;
	return true;
}

bool LspDocumentService::close(std::string &errorMessage) {
	LspDocumentMirror candidate = mirror;
	LspDocumentNotification notification;

	if (!candidate.close(notification, errorMessage)) return false;
	if (!sendMirrorNotification(notification, errorMessage)) return false;
	mirror = candidate;
	return true;
}

void LspDocumentService::clear() {
	mirror.clear();
}

bool LspDocumentService::isOpen() const noexcept {
	return mirror.isOpen();
}

const std::string &LspDocumentService::documentUri() const noexcept {
	return mirror.documentUri();
}

std::int64_t LspDocumentService::sentVersion() const noexcept {
	return mirror.sentVersion();
}

bool LspDocumentService::matchesSentVersion(std::int64_t version) const noexcept {
	return mirror.matchesSentVersion(version);
}

bool LspDocumentService::isStaleForSentVersion(std::int64_t version) const noexcept {
	return mirror.isStaleForSentVersion(version);
}

bool LspDocumentService::sourceToDocumentSnapshot(const LspDocumentSourceSnapshot &source, LspDocumentSnapshot &snapshot, std::string &errorMessage) const {
	if (!pathToFileUri(source.absolutePath, snapshot.uri, errorMessage)) return false;
	snapshot.languageId = source.languageId;
	snapshot.version = source.version;
	snapshot.text = source.text;
	return true;
}

bool LspDocumentService::sendMirrorNotification(const LspDocumentNotification &notification, std::string &errorMessage) {
	return lifecycle.sendInitializedPayload(notification.payload, errorMessage);
}

} // namespace mr::lsp
