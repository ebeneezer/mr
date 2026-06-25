#include "MRLspDocumentService.hpp"

#include "MRLspUri.hpp"

namespace mr::lsp {

LspDocumentService::LspDocumentService(LspLifecycle &lifecycleRef) noexcept : lifecycle(lifecycleRef), mirrors(), activeUri() {
}

bool LspDocumentService::open(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) {
	LspDocumentSnapshot documentSnapshot;
	LspDocumentNotification notification;
	LspDocumentMirror mirror;

	if (!sourceToDocumentSnapshot(snapshot, documentSnapshot, errorMessage)) return false;
	if (mirrors.find(documentSnapshot.uri) != mirrors.end()) {
		errorMessage = "LSP document service document is already open.";
		return false;
	}
	if (!mirror.open(documentSnapshot, notification, errorMessage)) return false;
	if (!sendMirrorNotification(notification, errorMessage)) return false;
	mirrors[documentSnapshot.uri] = mirror;
	activeUri = documentSnapshot.uri;
	return true;
}

bool LspDocumentService::change(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) {
	LspDocumentSnapshot documentSnapshot;
	LspDocumentNotification notification;
	std::map<std::string, LspDocumentMirror>::iterator found;
	LspDocumentMirror candidate;

	if (!sourceToDocumentSnapshot(snapshot, documentSnapshot, errorMessage)) return false;
	found = mirrors.find(documentSnapshot.uri);
	if (found == mirrors.end()) {
		errorMessage = "LSP document service document is not open.";
		return false;
	}
	candidate = found->second;
	if (!candidate.change(documentSnapshot, notification, errorMessage)) return false;
	if (!sendMirrorNotification(notification, errorMessage)) return false;
	found->second = candidate;
	activeUri = documentSnapshot.uri;
	return true;
}

bool LspDocumentService::activate(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) {
	LspDocumentSnapshot documentSnapshot;

	if (!sourceToDocumentSnapshot(snapshot, documentSnapshot, errorMessage)) return false;
	if (mirrors.find(documentSnapshot.uri) == mirrors.end()) {
		errorMessage = "LSP document service document is not open.";
		return false;
	}
	activeUri = documentSnapshot.uri;
	errorMessage.clear();
	return true;
}

bool LspDocumentService::close(std::string &errorMessage) {
	LspDocumentNotification notification;
	std::map<std::string, LspDocumentMirror>::iterator found;
	LspDocumentMirror candidate;

	found = mirrors.find(activeUri);
	if (found == mirrors.end()) {
		errorMessage = "LSP document service has no active open document.";
		return false;
	}
	candidate = found->second;
	if (!candidate.close(notification, errorMessage)) return false;
	if (!sendMirrorNotification(notification, errorMessage)) return false;
	mirrors.erase(found);
	activeUri.clear();
	if (!mirrors.empty()) activeUri = mirrors.begin()->first;
	return true;
}

bool LspDocumentService::close(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) {
	LspDocumentSnapshot documentSnapshot;
	LspDocumentNotification notification;
	std::map<std::string, LspDocumentMirror>::iterator found;
	LspDocumentMirror candidate;

	if (!sourceToDocumentSnapshot(snapshot, documentSnapshot, errorMessage)) return false;
	found = mirrors.find(documentSnapshot.uri);
	if (found == mirrors.end()) {
		errorMessage = "LSP document service document is not open.";
		return false;
	}
	candidate = found->second;
	if (!candidate.close(notification, errorMessage)) return false;
	if (!sendMirrorNotification(notification, errorMessage)) return false;
	mirrors.erase(found);
	if (activeUri == documentSnapshot.uri) {
		activeUri.clear();
		if (!mirrors.empty()) activeUri = mirrors.begin()->first;
	}
	return true;
}

void LspDocumentService::clear() {
	mirrors.clear();
	activeUri.clear();
}

bool LspDocumentService::isOpen() const noexcept {
	return !activeUri.empty() && mirrors.find(activeUri) != mirrors.end();
}

bool LspDocumentService::hasOpenDocument(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) const {
	LspDocumentSnapshot documentSnapshot;

	if (!sourceToDocumentSnapshot(snapshot, documentSnapshot, errorMessage)) return false;
	errorMessage.clear();
	return mirrors.find(documentSnapshot.uri) != mirrors.end();
}

bool LspDocumentService::hasOpenDocumentUri(const std::string &uri) const noexcept {
	return mirrors.find(uri) != mirrors.end();
}

const std::string &LspDocumentService::documentUri() const noexcept {
	return activeUri;
}

std::int64_t LspDocumentService::sentVersion() const noexcept {
	const std::map<std::string, LspDocumentMirror>::const_iterator found = mirrors.find(activeUri);

	if (found == mirrors.end()) return 0;
	return found->second.sentVersion();
}

bool LspDocumentService::matchesSentVersion(std::int64_t version) const noexcept {
	const std::map<std::string, LspDocumentMirror>::const_iterator found = mirrors.find(activeUri);

	return found != mirrors.end() && found->second.matchesSentVersion(version);
}

bool LspDocumentService::matchesSentVersion(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) const {
	LspDocumentSnapshot documentSnapshot;
	std::map<std::string, LspDocumentMirror>::const_iterator found;

	if (!sourceToDocumentSnapshot(snapshot, documentSnapshot, errorMessage)) return false;
	found = mirrors.find(documentSnapshot.uri);
	errorMessage.clear();
	return found != mirrors.end() && found->second.matchesSentVersion(documentSnapshot.version);
}

bool LspDocumentService::matchesSentVersion(const std::string &uri, std::int64_t version) const noexcept {
	const std::map<std::string, LspDocumentMirror>::const_iterator found = mirrors.find(uri);

	return found != mirrors.end() && found->second.matchesSentVersion(version);
}

bool LspDocumentService::isStaleForSentVersion(std::int64_t version) const noexcept {
	const std::map<std::string, LspDocumentMirror>::const_iterator found = mirrors.find(activeUri);

	return found != mirrors.end() && found->second.isStaleForSentVersion(version);
}

bool LspDocumentService::isStaleForSentVersion(const LspDocumentSourceSnapshot &snapshot, std::string &errorMessage) const {
	LspDocumentSnapshot documentSnapshot;
	std::map<std::string, LspDocumentMirror>::const_iterator found;

	if (!sourceToDocumentSnapshot(snapshot, documentSnapshot, errorMessage)) return false;
	found = mirrors.find(documentSnapshot.uri);
	errorMessage.clear();
	return found != mirrors.end() && found->second.isStaleForSentVersion(documentSnapshot.version);
}

bool LspDocumentService::isStaleForSentVersion(const std::string &uri, std::int64_t version) const noexcept {
	const std::map<std::string, LspDocumentMirror>::const_iterator found = mirrors.find(uri);

	return found != mirrors.end() && found->second.isStaleForSentVersion(version);
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
