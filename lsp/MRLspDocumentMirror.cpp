#include "MRLspDocumentMirror.hpp"

#include <iomanip>
#include <sstream>

namespace mr::lsp {
namespace {
bool setError(std::string &errorMessage, const std::string &message) {
	errorMessage = message;
	return false;
}

void appendJsonString(std::ostringstream &out, const std::string &text) {
	out << '"';
	for (std::size_t index = 0; index < text.size(); ++index) {
		const unsigned char ch = static_cast<unsigned char>(text[index]);
		if (ch == '"' || ch == '\\') {
			out << '\\' << static_cast<char>(ch);
		} else if (ch == '\n') {
			out << "\\n";
		} else if (ch == '\r') {
			out << "\\r";
		} else if (ch == '\t') {
			out << "\\t";
		} else if (ch < 0x20) {
			out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec;
		} else {
			out << static_cast<char>(ch);
		}
	}
	out << '"';
}

LspDocumentNotification makeNotification(const std::string &method, const std::string &payload, const std::string &uri, std::int64_t version) {
	LspDocumentNotification notification;

	notification.method = method;
	notification.payload = payload;
	notification.uri = uri;
	notification.version = version;
	return notification;
}
} // namespace

bool LspDocumentMirror::open(const LspDocumentSnapshot &snapshot, LspDocumentNotification &notification, std::string &errorMessage) {
	if (opened) return setError(errorMessage, "LSP document mirror is already open.");
	if (!validateOpenSnapshot(snapshot, errorMessage)) return false;
	uri = snapshot.uri;
	languageId = snapshot.languageId;
	lastSentVersion = snapshot.version;
	opened = true;
	hasSentVersion = true;
	notification = makeNotification("textDocument/didOpen", buildDidOpenPayload(snapshot), uri, lastSentVersion);
	errorMessage.clear();
	return true;
}

bool LspDocumentMirror::change(const LspDocumentSnapshot &snapshot, LspDocumentNotification &notification, std::string &errorMessage) {
	if (!opened) return setError(errorMessage, "LSP document mirror is not open.");
	if (!validateChangeSnapshot(snapshot, errorMessage)) return false;
	lastSentVersion = snapshot.version;
	hasSentVersion = true;
	notification = makeNotification("textDocument/didChange", buildDidChangePayload(snapshot), uri, lastSentVersion);
	errorMessage.clear();
	return true;
}

bool LspDocumentMirror::close(LspDocumentNotification &notification, std::string &errorMessage) {
	if (!opened) return setError(errorMessage, "LSP document mirror is not open.");
	notification = makeNotification("textDocument/didClose", buildDidClosePayload(uri), uri, lastSentVersion);
	clear();
	errorMessage.clear();
	return true;
}

void LspDocumentMirror::clear() {
	uri.clear();
	languageId.clear();
	lastSentVersion = 0;
	opened = false;
	hasSentVersion = false;
}

bool LspDocumentMirror::isOpen() const noexcept {
	return opened;
}

const std::string &LspDocumentMirror::documentUri() const noexcept {
	return uri;
}

const std::string &LspDocumentMirror::documentLanguageId() const noexcept {
	return languageId;
}

std::int64_t LspDocumentMirror::sentVersion() const noexcept {
	return lastSentVersion;
}

bool LspDocumentMirror::matchesSentVersion(std::int64_t version) const noexcept {
	return hasSentVersion && version == lastSentVersion;
}

bool LspDocumentMirror::isStaleForSentVersion(std::int64_t version) const noexcept {
	return hasSentVersion && version < lastSentVersion;
}

bool LspDocumentMirror::validateSnapshotIdentity(const LspDocumentSnapshot &snapshot, std::string &errorMessage) const {
	if (snapshot.uri.empty()) return setError(errorMessage, "LSP document URI is empty.");
	if (snapshot.languageId.empty()) return setError(errorMessage, "LSP document language id is empty.");
	return true;
}

bool LspDocumentMirror::validateOpenSnapshot(const LspDocumentSnapshot &snapshot, std::string &errorMessage) const {
	if (!validateSnapshotIdentity(snapshot, errorMessage)) return false;
	if (snapshot.version <= 0) return setError(errorMessage, "LSP document version must be positive.");
	return true;
}

bool LspDocumentMirror::validateChangeSnapshot(const LspDocumentSnapshot &snapshot, std::string &errorMessage) const {
	if (!validateSnapshotIdentity(snapshot, errorMessage)) return false;
	if (snapshot.uri != uri) return setError(errorMessage, "LSP document URI changed.");
	if (snapshot.languageId != languageId) return setError(errorMessage, "LSP document language id changed.");
	if (snapshot.version <= lastSentVersion) return setError(errorMessage, "LSP document version did not advance.");
	return true;
}

std::string buildDidOpenPayload(const LspDocumentSnapshot &snapshot) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":";
	appendJsonString(out, snapshot.uri);
	out << ",\"languageId\":";
	appendJsonString(out, snapshot.languageId);
	out << ",\"version\":" << snapshot.version << ",\"text\":";
	appendJsonString(out, snapshot.text);
	out << "}}}";
	return out.str();
}

std::string buildDidChangePayload(const LspDocumentSnapshot &snapshot) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":{\"uri\":";
	appendJsonString(out, snapshot.uri);
	out << ",\"version\":" << snapshot.version << "},\"contentChanges\":[{\"text\":";
	appendJsonString(out, snapshot.text);
	out << "}]}}";
	return out.str();
}

std::string buildDidClosePayload(const std::string &uri) {
	std::ostringstream out;

	out << "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":";
	appendJsonString(out, uri);
	out << "}}}";
	return out.str();
}

} // namespace mr::lsp
