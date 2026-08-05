#pragma once
#include <atomic>
#include <string>
#include <string_view>

struct MREditSetupSettings;

struct MRTextSaveOptions {
	bool binaryMode = false;
	bool legacyLineEndings = false;
	bool eofCtrlZ = false;
	bool eofCrLf = false;
	bool useTabs = true;
	bool truncateTrailingWhitespace = false;
	int tabSize = 8;
};

struct MRTextSaveStreamState {
	std::string currentLine;
	std::string bufferedLine;
	std::string expandedScratch;
	bool hasBufferedLine = false;
	bool pendingCarriageReturn = false;
	bool deferredCtrlZ = false;
	bool emittedAnyByte = false;
	unsigned char lastEmittedByte = 0;
};

bool readTextFile(const std::string &path, std::string &out);
bool readTextFile(const std::string &path, std::string &out, std::string &outError);
bool readTextFileCancellable(const std::string &path, std::string &out, std::string &outError, const std::atomic_bool &cancelFlag, bool &cancelled);
bool writeTextFile(std::string_view path, std::string_view content);
bool writeTextFile(const std::string &path, const std::string &content);
[[nodiscard]] bool fileContainsNulInBoundarySamples(const std::string &path) noexcept;
MRTextSaveOptions textSaveOptionsFromEditSettings(const MREditSetupSettings &settings);
MRTextSaveOptions effectiveTextSaveOptionsForPath(std::string_view path, std::size_t *outOptionsHash = nullptr);
std::size_t hashTextSaveOptions(const MRTextSaveOptions &options);
void appendNormalizedTextSaveChunk(std::string_view chunk, const MRTextSaveOptions &options, MRTextSaveStreamState &state, std::string &output);
void finalizeNormalizedTextSaveStream(const MRTextSaveOptions &options, MRTextSaveStreamState &state, std::string &output);
std::string normalizeTextForSave(std::string_view content, const MRTextSaveOptions &options);
