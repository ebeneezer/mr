#include "MRFileIOUtils.hpp"
#include "../../config/settings/MRSettingsRuntime.hpp"
#include "MRStringUtils.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#elif defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
#include <emmintrin.h>
#endif

namespace {

constexpr unsigned char kCtrlZ = 0x1Au;
constexpr std::size_t kBinaryFileProbeSampleSize = 8192;

bool boundarySampleContainsNul(int fd, off_t offset, std::size_t length, std::array<char, kBinaryFileProbeSampleSize> &buffer) noexcept {
	ssize_t count = 0;

	if (length == 0) return false;
	for (;;) {
		count = ::pread(fd, buffer.data(), length, offset);
		if (count >= 0 || errno != EINTR) break;
	}
	return count > 0 && std::memchr(buffer.data(), '\0', static_cast<std::size_t>(count)) != nullptr;
}

bool readTextFileWithOptionalError(const std::string &path, std::string &out, std::string *outError) {
	std::ifstream file(path, std::ios::in | std::ios::binary);
	if (!file.is_open()) {
		if (outError != nullptr) *outError = "Could not open file: " + path;
		return false;
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	if (!file.good() && !file.eof()) {
		if (outError != nullptr) *outError = "Error while reading file: " + path;
		return false;
	}
	out = buffer.str();
	if (outError != nullptr) outError->clear();
	return true;
}

inline bool isTrimWhitespaceByte(unsigned char byte) noexcept {
	return byte == static_cast<unsigned char>(' ') || byte == static_cast<unsigned char>('\t') || byte == static_cast<unsigned char>('\v') || byte == static_cast<unsigned char>('\f');
}

std::size_t trimmedTrailingWhitespaceLengthScalar(std::string_view text) noexcept {
	std::size_t length = text.size();
	while (length > 0 && isTrimWhitespaceByte(static_cast<unsigned char>(text[length - 1])))
		--length;
	return length;
}

#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__))
std::size_t trimmedTrailingWhitespaceLengthAvx2(std::string_view text) noexcept {
	const __m256i wsSpace = _mm256_set1_epi8(' ');
	const __m256i wsTab = _mm256_set1_epi8('\t');
	const __m256i wsVTab = _mm256_set1_epi8('\v');
	const __m256i wsFormFeed = _mm256_set1_epi8('\f');
	std::size_t length = text.size();

	while (length >= 32) {
		const char *block = text.data() + length - 32;
		const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(block));
		const __m256i isSpace = _mm256_cmpeq_epi8(bytes, wsSpace);
		const __m256i isTab = _mm256_cmpeq_epi8(bytes, wsTab);
		const __m256i isVTab = _mm256_cmpeq_epi8(bytes, wsVTab);
		const __m256i isFormFeed = _mm256_cmpeq_epi8(bytes, wsFormFeed);
		const __m256i isWs = _mm256_or_si256(_mm256_or_si256(isSpace, isTab), _mm256_or_si256(isVTab, isFormFeed));
		const std::uint32_t mask = static_cast<std::uint32_t>(_mm256_movemask_epi8(isWs));

		if (mask == 0xFFFFFFFFu) {
			length -= 32;
			continue;
		}
		const std::uint32_t nonWsMask = ~mask;
		const int highestBit = 31 - __builtin_clz(nonWsMask);
		return length - 32 + static_cast<std::size_t>(highestBit + 1);
	}
	return trimmedTrailingWhitespaceLengthScalar(text.substr(0, length));
}
#endif

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
std::size_t trimmedTrailingWhitespaceLengthSse2(std::string_view text) noexcept {
	const __m128i wsSpace = _mm_set1_epi8(' ');
	const __m128i wsTab = _mm_set1_epi8('\t');
	const __m128i wsVTab = _mm_set1_epi8('\v');
	const __m128i wsFormFeed = _mm_set1_epi8('\f');
	std::size_t length = text.size();

	while (length >= 16) {
		const char *block = text.data() + length - 16;
		const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(block));
		const __m128i isSpace = _mm_cmpeq_epi8(bytes, wsSpace);
		const __m128i isTab = _mm_cmpeq_epi8(bytes, wsTab);
		const __m128i isVTab = _mm_cmpeq_epi8(bytes, wsVTab);
		const __m128i isFormFeed = _mm_cmpeq_epi8(bytes, wsFormFeed);
		const __m128i isWs = _mm_or_si128(_mm_or_si128(isSpace, isTab), _mm_or_si128(isVTab, isFormFeed));
		const std::uint32_t mask = static_cast<std::uint32_t>(_mm_movemask_epi8(isWs));

		if (mask == 0xFFFFu) {
			length -= 16;
			continue;
		}
		const std::uint32_t nonWsMask = (~mask) & 0xFFFFu;
		const int highestBit = 31 - __builtin_clz(nonWsMask);
		return length - 16 + static_cast<std::size_t>(highestBit + 1);
	}
	return trimmedTrailingWhitespaceLengthScalar(text.substr(0, length));
}
#endif

std::size_t trimmedTrailingWhitespaceLength(std::string_view text) noexcept {
#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__))
	return trimmedTrailingWhitespaceLengthAvx2(text);
#elif defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
	return trimmedTrailingWhitespaceLengthSse2(text);
#else
	return trimmedTrailingWhitespaceLengthScalar(text);
#endif
}

[[maybe_unused]] bool containsTabScalar(std::string_view text) noexcept {
	for (char ch : text)
		if (ch == '\t') return true;
	return false;
}

#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__))
bool containsTabAvx2(std::string_view text) noexcept {
	const __m256i tab = _mm256_set1_epi8('\t');
	std::size_t i = 0;
	const std::size_t size = text.size();

	for (; i + 32 <= size; i += 32) {
		const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(text.data() + i));
		if (_mm256_movemask_epi8(_mm256_cmpeq_epi8(bytes, tab)) != 0) return true;
	}
	for (; i < size; ++i)
		if (text[i] == '\t') return true;
	return false;
}
#endif

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
bool containsTabSse2(std::string_view text) noexcept {
	const __m128i tab = _mm_set1_epi8('\t');
	std::size_t i = 0;
	const std::size_t size = text.size();

	for (; i + 16 <= size; i += 16) {
		const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(text.data() + i));
		if (_mm_movemask_epi8(_mm_cmpeq_epi8(bytes, tab)) != 0) return true;
	}
	for (; i < size; ++i)
		if (text[i] == '\t') return true;
	return false;
}
#endif

bool containsTab(std::string_view text) noexcept {
#if defined(__AVX2__) && (defined(__x86_64__) || defined(__i386__))
	return containsTabAvx2(text);
#elif defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
	return containsTabSse2(text);
#else
	return containsTabScalar(text);
#endif
}

void appendOutputBytes(MRTextSaveStreamState &state, std::string &output, const char *data, std::size_t length) {
	if (data == nullptr || length == 0) return;
	output.append(data, length);
	state.emittedAnyByte = true;
	state.lastEmittedByte = static_cast<unsigned char>(data[length - 1]);
}

void appendOutputByte(MRTextSaveStreamState &state, std::string &output, char byte) {
	output.push_back(byte);
	state.emittedAnyByte = true;
	state.lastEmittedByte = static_cast<unsigned char>(byte);
}

void appendExpandedTabs(std::string_view line, int tabSize, std::string &expanded) {
	const std::size_t tabWidth = static_cast<std::size_t>(std::max(1, tabSize));
	std::size_t visualColumn = 0;

	expanded.clear();
	expanded.reserve(line.size());
	for (char ch : line) {
		if (ch == '\t') {
			std::size_t width = tabWidth - (visualColumn % tabWidth);
			if (width == 0) width = tabWidth;
			expanded.append(width, ' ');
			visualColumn += width;
			continue;
		}
		expanded.push_back(ch);
		++visualColumn;
	}
}

void appendLineEnding(MRTextSaveStreamState &state, std::string &output, const MRTextSaveOptions &options, bool eofLine) {
	if ((eofLine && options.eofCrLf) || options.legacyLineEndings) {
		appendOutputByte(state, output, '\r');
		appendOutputByte(state, output, '\n');
		return;
	}
	appendOutputByte(state, output, '\n');
}

void appendNormalizedLine(MRTextSaveStreamState &state, std::string &output, std::string_view line, const MRTextSaveOptions &options, bool eofLine) {
	std::string_view normalizedLine = line;

	if (!options.useTabs && containsTab(normalizedLine)) {
		appendExpandedTabs(normalizedLine, options.tabSize, state.expandedScratch);
		normalizedLine = state.expandedScratch;
	}
	if (options.truncateTrailingWhitespace) {
		const std::size_t trimmed = trimmedTrailingWhitespaceLength(normalizedLine);
		if (trimmed != 0) normalizedLine = normalizedLine.substr(0, trimmed);
	}
	appendOutputBytes(state, output, normalizedLine.data(), normalizedLine.size());
	appendLineEnding(state, output, options, eofLine);
}

void emitParsedLine(MRTextSaveStreamState &state, std::string &output, const MRTextSaveOptions &options, bool eofLine) {
	appendNormalizedLine(state, output, state.bufferedLine, options, eofLine);
}

void stageParsedLine(MRTextSaveStreamState &state, std::string &output, const MRTextSaveOptions &options) {
	if (state.hasBufferedLine) emitParsedLine(state, output, options, false);
	state.bufferedLine.swap(state.currentLine);
	state.currentLine.clear();
	state.hasBufferedLine = true;
}

void processNormalizedByte(unsigned char byte, MRTextSaveStreamState &state, std::string &output, const MRTextSaveOptions &options) {
	if (state.pendingCarriageReturn) {
		state.pendingCarriageReturn = false;
		stageParsedLine(state, output, options);
		if (byte == static_cast<unsigned char>('\n')) return;
	}
	if (byte == static_cast<unsigned char>('\r')) {
		state.pendingCarriageReturn = true;
		return;
	}
	if (byte == static_cast<unsigned char>('\n')) {
		stageParsedLine(state, output, options);
		return;
	}
	state.currentLine.push_back(static_cast<char>(byte));
}

} // namespace

bool readTextFile(const std::string &path, std::string &out) {
	return readTextFileWithOptionalError(path, out, nullptr);
}

bool readTextFile(const std::string &path, std::string &out, std::string &outError) {
	return readTextFileWithOptionalError(path, out, &outError);
}

bool readTextFileCancellable(const std::string &path, std::string &out, std::string &outError, const std::atomic_bool &cancelFlag, bool &cancelled) {
	constexpr std::size_t kReadChunkSize = 64 * 1024;
	std::array<char, kReadChunkSize> buffer{};
	struct stat st {};
	int fd = -1;

	out.clear();
	outError.clear();
	cancelled = false;
	if (cancelFlag.load(std::memory_order_acquire)) {
		cancelled = true;
		return false;
	}
	fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		outError = "Could not open file: " + path;
		return false;
	}
	if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) out.reserve(static_cast<std::size_t>(st.st_size));
	for (;;) {
		ssize_t count = 0;

		if (cancelFlag.load(std::memory_order_acquire)) {
			cancelled = true;
			out.clear();
			::close(fd);
			return false;
		}
		count = ::read(fd, buffer.data(), buffer.size());
		if (count > 0) {
			out.append(buffer.data(), static_cast<std::size_t>(count));
			continue;
		}
		if (count == 0) break;
		if (errno == EINTR) continue;
		out.clear();
		outError = "Error while reading file: " + path;
		::close(fd);
		return false;
	}
	::close(fd);
	return true;
}

bool writeTextFile(std::string_view path, std::string_view content) {
	std::ofstream file(std::string(path), std::ios::out | std::ios::trunc | std::ios::binary);
	if (!file.is_open()) return false;
	file << content;
	return file.good();
}

bool writeTextFile(const std::string &path, const std::string &content) {
	return writeTextFile(std::string_view(path), std::string_view(content));
}

bool fileContainsNulInBoundarySamples(const std::string &path) noexcept {
	std::array<char, kBinaryFileProbeSampleSize> buffer{};
	struct stat st {};
	int fd = -1;
	bool detected = false;

	if (path.empty()) return false;
	fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) return false;
	if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
		const std::size_t headLength = static_cast<std::size_t>(std::min<off_t>(st.st_size, static_cast<off_t>(kBinaryFileProbeSampleSize)));

		detected = boundarySampleContainsNul(fd, 0, headLength, buffer);
		if (!detected && st.st_size > static_cast<off_t>(kBinaryFileProbeSampleSize)) {
			const off_t tailOffset = std::max<off_t>(static_cast<off_t>(kBinaryFileProbeSampleSize), st.st_size - static_cast<off_t>(kBinaryFileProbeSampleSize));
			const std::size_t tailLength = static_cast<std::size_t>(st.st_size - tailOffset);

			detected = boundarySampleContainsNul(fd, tailOffset, tailLength, buffer);
		}
	}
	::close(fd);
	return detected;
}

MRTextSaveOptions textSaveOptionsFromEditSettings(const MREditSetupSettings &settings) {
	MRTextSaveOptions options;
	const std::string fileType = upperAscii(settings.fileType);

	options.binaryMode = fileType == "BINARY";
	options.legacyLineEndings = fileType == "LEGACY_TEXT";
	options.eofCtrlZ = !options.binaryMode && settings.eofCtrlZ;
	options.eofCrLf = !options.binaryMode && settings.eofCrLf;
	options.useTabs = settings.tabExpand;
	options.truncateTrailingWhitespace = settings.truncateSpaces;
	options.tabSize = std::max(1, settings.tabSize);
	return options;
}

MRTextSaveOptions effectiveTextSaveOptionsForPath(std::string_view path, std::size_t *outOptionsHash) {
	MREditSetupSettings settings = configuredEditSetupSettings();
	MREditSetupSettings effective;
	MRTextSaveOptions options;
	std::string normalizedPath = trimAscii(path);

	if (!normalizedPath.empty() && effectiveEditSetupSettingsForPath(normalizedPath, effective, nullptr)) settings = effective;
	options = textSaveOptionsFromEditSettings(settings);
	if (outOptionsHash != nullptr) *outOptionsHash = hashTextSaveOptions(options);
	return options;
}

std::size_t hashTextSaveOptions(const MRTextSaveOptions &options) {
	std::uint64_t hash = 1469598103934665603ull;
	auto appendByte = [&](std::uint8_t value) {
		hash ^= static_cast<std::uint64_t>(value);
		hash *= 1099511628211ull;
	};
	auto appendInt = [&](int value) {
		for (int shift = 0; shift < 32; shift += 8)
			appendByte(static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) >> shift) & 0xFFu));
	};

	appendByte(static_cast<std::uint8_t>(options.binaryMode ? 1 : 0));
	appendByte(static_cast<std::uint8_t>(options.legacyLineEndings ? 1 : 0));
	appendByte(static_cast<std::uint8_t>(options.eofCtrlZ ? 1 : 0));
	appendByte(static_cast<std::uint8_t>(options.eofCrLf ? 1 : 0));
	appendByte(static_cast<std::uint8_t>(options.useTabs ? 1 : 0));
	appendByte(static_cast<std::uint8_t>(options.truncateTrailingWhitespace ? 1 : 0));
	appendInt(options.tabSize);
	return static_cast<std::size_t>(hash);
}

void appendNormalizedTextSaveChunk(std::string_view chunk, const MRTextSaveOptions &options, MRTextSaveStreamState &state, std::string &output) {
	if (chunk.empty()) return;
	if (options.binaryMode) {
		appendOutputBytes(state, output, chunk.data(), chunk.size());
		return;
	}

	for (char ch : chunk) {
		const unsigned char byte = static_cast<unsigned char>(ch);
		if (state.deferredCtrlZ) {
			processNormalizedByte(kCtrlZ, state, output, options);
			state.deferredCtrlZ = false;
		}
		if (byte == kCtrlZ) {
			state.deferredCtrlZ = true;
			continue;
		}
		processNormalizedByte(byte, state, output, options);
	}
}

void finalizeNormalizedTextSaveStream(const MRTextSaveOptions &options, MRTextSaveStreamState &state, std::string &output) {
	if (options.binaryMode) return;
	if (state.pendingCarriageReturn) {
		state.pendingCarriageReturn = false;
		stageParsedLine(state, output, options);
	}
	if (state.deferredCtrlZ) state.deferredCtrlZ = false;
	if (!state.currentLine.empty()) stageParsedLine(state, output, options);
	if (!state.hasBufferedLine) state.hasBufferedLine = true;
	if (state.hasBufferedLine) emitParsedLine(state, output, options, true);
	if (options.eofCtrlZ && (!state.emittedAnyByte || state.lastEmittedByte != kCtrlZ)) appendOutputByte(state, output, static_cast<char>(kCtrlZ));
}

std::string normalizeTextForSave(std::string_view content, const MRTextSaveOptions &options) {
	if (options.binaryMode) return std::string(content);

	std::string output;
	MRTextSaveStreamState state;
	output.reserve(content.size() + 8);
	appendNormalizedTextSaveChunk(content, options, state, output);
	finalizeNormalizedTextSaveStream(options, state, output);
	return output;
}
