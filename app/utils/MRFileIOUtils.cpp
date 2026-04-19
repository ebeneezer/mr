#include "MRFileIOUtils.hpp"
#include "../../config/MRDialogPaths.hpp"
#include "MRStringUtils.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace {

bool readTextFileImpl(const std::string &path, std::string &out, std::string *outError) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        if (outError != nullptr)
            *outError = "Could not open file: " + path;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    if (!file.good() && !file.eof()) {
        if (outError != nullptr)
            *outError = "Error while reading file: " + path;
        return false;
    }
    out = buffer.str();
    if (outError != nullptr)
        outError->clear();
    return true;
}

} // namespace

bool readTextFile(const std::string &path, std::string &out) {
    return readTextFileImpl(path, out, nullptr);
}

bool readTextFile(const std::string &path, std::string &out, std::string &outError) {
    return readTextFileImpl(path, out, &outError);
}

bool writeTextFile(std::string_view path, std::string_view content) {
    std::ofstream file(std::string(path), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open()) return false;
    file << content;
    return file.good();
}

bool writeTextFile(const std::string &path, const std::string &content) { return writeTextFile(std::string_view(path), std::string_view(content)); }

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

    if (!normalizedPath.empty() &&
        effectiveEditSetupSettingsForPath(normalizedPath, effective, nullptr))
        settings = effective;
    options = textSaveOptionsFromEditSettings(settings);
    if (outOptionsHash != nullptr)
        *outOptionsHash = hashTextSaveOptions(options);
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

std::string normalizeTextForSave(std::string_view content, const MRTextSaveOptions &options) {
    if (options.binaryMode)
        return std::string(content);

    const bool stripTrailingCtrlZ =
        !content.empty() &&
        static_cast<unsigned char>(content.back()) == static_cast<unsigned char>(0x1A);
    const std::size_t contentLimit = stripTrailingCtrlZ ? content.size() - 1 : content.size();
    const int tabSize = std::max(1, options.tabSize);
    std::string output;
    std::string currentLine;
    std::string bufferedLine;
    bool hasBufferedLine = false;
    bool pendingCarriageReturn = false;

    auto normalizeLine = [&](const std::string &line) -> std::string {
        std::string normalized = line;
        if (!options.useTabs && normalized.find('\t') != std::string::npos) {
            std::string expanded;
            std::size_t visualColumn = 0;
            expanded.reserve(normalized.size());
            for (char ch : normalized) {
                if (ch == '\t') {
                    std::size_t width = static_cast<std::size_t>(tabSize) -
                                        (visualColumn % static_cast<std::size_t>(tabSize));
                    if (width == 0)
                        width = static_cast<std::size_t>(tabSize);
                    expanded.append(width, ' ');
                    visualColumn += width;
                } else {
                    expanded.push_back(ch);
                    ++visualColumn;
                }
            }
            normalized.swap(expanded);
        }
        if (options.truncateTrailingWhitespace) {
            while (!normalized.empty()) {
                unsigned char byte = static_cast<unsigned char>(normalized.back());
                if (std::isspace(static_cast<int>(byte)) == 0 || byte == '\n' || byte == '\r')
                    break;
                normalized.pop_back();
            }
        }
        return normalized;
    };

    auto emitLine = [&](const std::string &line, bool eofLine) {
        std::string normalized = normalizeLine(line);
        output.append(normalized);
        if ((eofLine && options.eofCrLf) || options.legacyLineEndings)
            output += "\r\n";
        else
            output.push_back('\n');
    };
    auto stageParsedLine = [&]() {
        if (hasBufferedLine)
            emitLine(bufferedLine, false);
        bufferedLine.swap(currentLine);
        currentLine.clear();
        hasBufferedLine = true;
    };

    for (std::size_t i = 0; i < contentLimit; ++i) {
        unsigned char byte = static_cast<unsigned char>(content[i]);
        if (pendingCarriageReturn) {
            pendingCarriageReturn = false;
            stageParsedLine();
            if (byte == '\n')
                continue;
        }
        if (byte == '\r') {
            pendingCarriageReturn = true;
            continue;
        }
        if (byte == '\n') {
            stageParsedLine();
            continue;
        }
        currentLine.push_back(static_cast<char>(byte));
    }
    if (pendingCarriageReturn)
        stageParsedLine();
    if (!currentLine.empty())
        stageParsedLine();
    if (!hasBufferedLine)
        hasBufferedLine = true;
    if (hasBufferedLine)
        emitLine(bufferedLine, true);
    if (options.eofCtrlZ &&
        (output.empty() ||
         static_cast<unsigned char>(output.back()) != static_cast<unsigned char>(0x1A)))
        output.push_back(static_cast<char>(0x1A));
    return output;
}
