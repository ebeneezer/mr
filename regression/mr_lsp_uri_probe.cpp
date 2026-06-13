#include <iostream>
#include <string>

#include "../lsp/MRLspUri.hpp"

namespace {
bool expect(bool condition, const std::string &name, std::string &failureReason) {
	if (condition) return true;
	failureReason = name;
	return false;
}

bool expectPathToUri(const std::string &path, const std::string &expectedUri, std::string &failureReason) {
	std::string uri;
	std::string errorMessage;

	if (!expect(mr::lsp::pathToFileUri(path, uri, errorMessage), "pathToFileUri failed: " + errorMessage, failureReason)) return false;
	return expect(uri == expectedUri, "pathToFileUri mismatch: " + uri, failureReason);
}

bool expectUriToPath(const std::string &uri, const std::string &expectedPath, std::string &failureReason) {
	std::string path;
	std::string errorMessage;

	if (!expect(mr::lsp::fileUriToPath(uri, path, errorMessage), "fileUriToPath failed: " + errorMessage, failureReason)) return false;
	return expect(path == expectedPath, "fileUriToPath mismatch: " + path, failureReason);
}

bool testPathToFileUri(std::string &failureReason) {
	if (!expectPathToUri("/tmp/mr.cpp", "file:///tmp/mr.cpp", failureReason)) return false;
	if (!expectPathToUri("/tmp/mr file.cpp", "file:///tmp/mr%20file.cpp", failureReason)) return false;
	if (!expectPathToUri("/tmp/a#b%c?d.cpp", "file:///tmp/a%23b%25c%3Fd.cpp", failureReason)) return false;
	if (!expectPathToUri("/tmp/\xC3\xA4.cpp", "file:///tmp/%C3%A4.cpp", failureReason)) return false;
	return expectPathToUri("/tmp/control\x01.cpp", "file:///tmp/control%01.cpp", failureReason);
}

bool testFileUriToPath(std::string &failureReason) {
	if (!expectUriToPath("file:///tmp/mr.cpp", "/tmp/mr.cpp", failureReason)) return false;
	if (!expectUriToPath("file:///tmp/mr%20file.cpp", "/tmp/mr file.cpp", failureReason)) return false;
	if (!expectUriToPath("file:///tmp/a%23b%25c%3Fd.cpp", "/tmp/a#b%c?d.cpp", failureReason)) return false;
	if (!expectUriToPath("file:///tmp/%C3%A4.cpp", "/tmp/\xC3\xA4.cpp", failureReason)) return false;
	return expectUriToPath("file://localhost/tmp/mr.cpp", "/tmp/mr.cpp", failureReason);
}

bool testRoundTrip(std::string &failureReason) {
	const std::string original = "/home/idoc/mr/path with spaces/%hash#mark?.cpp";
	std::string uri;
	std::string path;
	std::string errorMessage;

	if (!expect(mr::lsp::pathToFileUri(original, uri, errorMessage), "roundtrip encode: " + errorMessage, failureReason)) return false;
	if (!expect(mr::lsp::fileUriToPath(uri, path, errorMessage), "roundtrip decode: " + errorMessage, failureReason)) return false;
	return expect(path == original, "roundtrip mismatch", failureReason);
}

bool testInvalidPath(std::string &failureReason) {
	std::string uri;
	std::string errorMessage;

	if (!expect(!mr::lsp::pathToFileUri("", uri, errorMessage), "empty path accepted", failureReason)) return false;
	if (!expect(!mr::lsp::pathToFileUri("relative.cpp", uri, errorMessage), "relative path accepted", failureReason)) return false;
	return expect(!errorMessage.empty(), "invalid path error text", failureReason);
}

bool testInvalidUri(std::string &failureReason) {
	std::string path;
	std::string errorMessage;

	if (!expect(!mr::lsp::fileUriToPath("http:///tmp/mr.cpp", path, errorMessage), "non-file uri accepted", failureReason)) return false;
	if (!expect(!mr::lsp::fileUriToPath("file://server/tmp/mr.cpp", path, errorMessage), "remote file uri accepted", failureReason)) return false;
	if (!expect(!mr::lsp::fileUriToPath("file://relative.cpp", path, errorMessage), "relative file uri accepted", failureReason)) return false;
	if (!expect(!mr::lsp::fileUriToPath("file:///tmp/%GG.cpp", path, errorMessage), "invalid escape accepted", failureReason)) return false;
	if (!expect(!mr::lsp::fileUriToPath("file:///tmp/%0", path, errorMessage), "incomplete escape accepted", failureReason)) return false;
	if (!expect(!mr::lsp::fileUriToPath("file:///tmp/%00.cpp", path, errorMessage), "nul escape accepted", failureReason)) return false;
	return expect(!mr::lsp::fileUriToPath("file:///tmp/mr.cpp#fragment", path, errorMessage), "fragment accepted", failureReason);
}

bool runProbe(std::string &failureReason) {
	if (!testPathToFileUri(failureReason)) return false;
	if (!testFileUriToPath(failureReason)) return false;
	if (!testRoundTrip(failureReason)) return false;
	if (!testInvalidPath(failureReason)) return false;
	if (!testInvalidUri(failureReason)) return false;
	return true;
}
} // namespace

int main() {
	std::string failureReason;

	if (!runProbe(failureReason)) {
		std::cerr << "mr_lsp_uri_probe failed: " << failureReason << "\n";
		return 1;
	}

	std::cout << "mr_lsp_uri_probe passed\n";
	return 0;
}
