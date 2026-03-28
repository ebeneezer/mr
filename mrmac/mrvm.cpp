#define Uses_MsgBox
#define Uses_TProgram
#define Uses_TDeskTop
#include <tvision/tv.h>

#include "mrvm.hpp"
#include "mrmac.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <glob.h>
#include <sys/stat.h>
#include <cerrno>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <ctime>
#include <map>
#include <unistd.h>

#include "../ui/mrtheme.hpp"
#include "../ui/TMREditWindow.hpp"

namespace
{
    using Value = VirtualMachine::Value;

    struct GlobalEntry
    {
        int type;
        Value value;
    };

    struct MacroRef
    {
        std::string fileKey;
        std::string displayName;
        std::size_t entryOffset;
        bool firstRunPending;
        bool transientAttr;
        bool dumpAttr;
        bool permAttr;

        MacroRef() : fileKey(), displayName(), entryOffset(0), firstRunPending(true), transientAttr(false), dumpAttr(false), permAttr(false) {}
    };

    struct LoadedMacroFile
    {
        std::string fileKey;
        std::string displayName;
        std::string resolvedPath;
        std::vector<unsigned char> bytecode;
        std::vector<std::string> macroNames;
    };

    struct MacroStackFrame
    {
        std::string macroName;
        bool firstRun;

        MacroStackFrame() : macroName(), firstRun(false) {}
        MacroStackFrame(const std::string &aName, bool aFirstRun) : macroName(aName), firstRun(aFirstRun) {}
    };

    struct RuntimeEnvironment
    {
        std::map<std::string, GlobalEntry> globals;
        std::vector<std::string> globalOrder;
        std::size_t globalEnumIndex;
        std::string parameterString;
        int returnInt;
        std::string returnStr;
        int errorLevel;
        std::map<std::string, LoadedMacroFile> loadedFiles;
        std::map<std::string, MacroRef> loadedMacros;
        std::vector<std::string> macroOrder;
        std::size_t macroEnumIndex;
        std::vector<MacroStackFrame> macroStack;
        std::vector<std::string> fileMatches;
        std::size_t fileMatchIndex;
        std::string lastFileName;
        std::map<const void *, std::vector<uint> > markStacks;
        std::string startupCommand;
        std::vector<std::string> processArgs;
        std::string executablePath;
        std::string executableDir;
        std::string shellPath;
        std::string shellVersion;
        bool ignoreCase;
        bool tabExpand;
        bool lastSearchValid;
        const void *lastSearchWindow;
        std::string lastSearchFileName;
        std::size_t lastSearchStart;
        std::size_t lastSearchEnd;
        std::size_t lastSearchCursor;

        RuntimeEnvironment() : globals(), globalOrder(), globalEnumIndex(0), parameterString(), returnInt(0), returnStr(), errorLevel(0), loadedFiles(), loadedMacros(), macroOrder(), macroEnumIndex(0), macroStack(), fileMatches(), fileMatchIndex(0), lastFileName(), markStacks(), startupCommand(), processArgs(), executablePath(), executableDir(), shellPath(), shellVersion(), ignoreCase(false), tabExpand(true), lastSearchValid(false), lastSearchWindow(NULL), lastSearchFileName(), lastSearchStart(0), lastSearchEnd(0), lastSearchCursor(0) {}
    };

    struct SplitTextBuffer
    {
        std::vector<std::string> lines;
        bool trailingNewline;

        SplitTextBuffer() : lines(), trailingNewline(false) {}
    };

    static RuntimeEnvironment g_runtimeEnv;

    static std::string valueAsString(const Value &value);
    static int valueAsInt(const Value &value);
    static void enforceStringLength(const std::string &s);
    static std::string trimAscii(const std::string &value);
    static std::string commandFirstLine(const std::string &command);
    static std::string detectExecutablePathFromProc();
    static std::string normalizeDirPath(const std::string &path);
    static std::string detectExecutableDir(const std::string &argv0);
    static std::string detectShellPath();
    static std::string detectShellVersion(const std::string &shellPath);
    static int detectCpuCode();
    static std::string getenvValue(const std::string &name);
    static std::string getEnvironmentValue(const std::string &entryName);
    static bool changeDirectoryPath(const std::string &path);
    static bool deleteFilePath(const std::string &path);
    static bool loadMacroFileIntoRegistry(const std::string &spec, std::string *loadedFileKey = NULL);
    static bool unloadMacroFromRegistry(const std::string &macroName);
    static bool parseRunMacroSpec(const std::string &spec, std::string &filePart, std::string &macroPart, std::string &paramPart);
    static bool ensureLoadedFileResident(const std::string &fileKey);
    static bool evictTransientFileImage(const std::string &fileKey);
    static std::string expandUserPath(const std::string &path);
    static bool fileExistsPath(const std::string &path);
    static int findFirstFileMatch(const std::string &pattern);
    static int findNextFileMatch();
    static TMREditWindow *currentEditWindow();
    static TFileEditor *currentEditor();
    static std::string snapshotEditorText(TFileEditor *editor);
    static std::size_t searchLimitForward(const std::string &text, std::size_t start, int numLines)
    {
        if (numLines <= 0)
            return text.size();
        std::size_t pos = start;
        int remaining = numLines;
        while (pos < text.size())
        {
            if (text[pos] == '\n')
            {
                --remaining;
                if (remaining == 0)
                    return pos;
            }
            ++pos;
        }
        return text.size();
    }

    static std::size_t searchLimitBackward(const std::string &text, std::size_t start, int numLines)
    {
        if (numLines <= 0)
            return 0;
        std::size_t pos = std::min(start, text.size());
        int remaining = numLines;
        while (pos > 0)
        {
            --pos;
            if (text[pos] == '\n')
            {
                --remaining;
                if (remaining == 0)
                    return pos + 1;
            }
        }
        return 0;
    }

    static bool searchEditorForward(TFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd);
    static bool searchEditorBackward(TFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd);
    static bool replaceLastSearch(TFileEditor *editor, const std::string &replacement);
    static Value currentEditorCharValue();
    static std::string currentEditorLineText(TFileEditor *editor);
    static std::string currentEditorWord(TFileEditor *editor, const std::string &delimiters);
    static int defaultTabWidth();
    static bool isVirtualChar(char c);
    static int nextTabStopColumn(int col);
    static int prevTabStopColumn(int col);
    static std::string makeIndentFill(int targetCol, bool preferTabs);
    static std::string crunchTabsString(const std::string &value);
    static std::string expandTabsString(const std::string &value, bool toVirtuals);
    static std::string tabsToSpacesString(const std::string &value);
    static int currentEditorIndentLevel();
    static bool setCurrentEditorIndentLevel(int level);
    static bool currentEditorInsertMode();
    static bool setCurrentEditorInsertMode(bool on);
    static bool insertEditorText(TFileEditor *editor, const std::string &text);
    static bool replaceEditorLine(TFileEditor *editor, const std::string &text);
    static bool deleteEditorChars(TFileEditor *editor, int count);
    static bool deleteEditorLine(TFileEditor *editor);
    static int currentEditorColumn(TFileEditor *editor);
    static int currentEditorLineNumber(TFileEditor *editor);
    static bool moveEditorLeft(TFileEditor *editor);
    static bool moveEditorRight(TFileEditor *editor);
    static bool moveEditorUp(TFileEditor *editor);
    static bool moveEditorDown(TFileEditor *editor);
    static bool moveEditorHome(TFileEditor *editor);
    static bool moveEditorEol(TFileEditor *editor);
    static bool moveEditorTof(TFileEditor *editor);
    static bool moveEditorEof(TFileEditor *editor);
    static bool moveEditorWordLeft(TFileEditor *editor);
    static bool moveEditorWordRight(TFileEditor *editor);
    static bool moveEditorFirstWord(TFileEditor *editor);
    static bool gotoEditorLine(TFileEditor *editor, int lineNum);
    static bool gotoEditorCol(TFileEditor *editor, int colNum);
    static bool currentEditorAtEof(TFileEditor *editor);
    static bool currentEditorAtEol(TFileEditor *editor);
    static int currentEditorRow(TFileEditor *editor);
    static bool markEditorPosition(TMREditWindow *win, TFileEditor *editor);
    static bool gotoEditorMark(TMREditWindow *win, TFileEditor *editor);
    static bool popEditorMark(TMREditWindow *win);
    static bool moveEditorPageUp(TFileEditor *editor);
    static bool moveEditorPageDown(TFileEditor *editor);
    static bool moveEditorNextPageBreak(TFileEditor *editor);
    static bool moveEditorLastPageBreak(TFileEditor *editor);
    static bool replaceEditorBuffer(TFileEditor *editor, const std::string &text, std::size_t cursorPos);
    static SplitTextBuffer splitBufferLines(const std::string &text);
    static std::string joinBufferLines(const SplitTextBuffer &buffer);
    static std::size_t bufferOffsetForLine(const SplitTextBuffer &buffer, int lineIndex);
    static std::size_t bufferOffsetForLineColumn(const SplitTextBuffer &buffer, int lineIndex, int colIndex);
    static int lineIndexForPtr(TFileEditor *editor, uint ptr);
    static bool currentBlockInfo(TMREditWindow *win, TFileEditor *editor, int &mode, uint &anchor, uint &end);
    static bool copyCurrentBlock(TMREditWindow *win, TFileEditor *editor);
    static bool moveCurrentBlock(TMREditWindow *win, TFileEditor *editor);
    static bool deleteCurrentBlock(TMREditWindow *win, TFileEditor *editor);

    static std::string upperKey(const std::string &value)
    {
        std::string out = value;
        for (std::string::size_type i = 0; i < out.size(); ++i)
            out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
        return out;
    }

    static Value makeInt(int value)
    {
        Value v;
        v.type = TYPE_INT;
        v.i = value;
        return v;
    }

    static Value makeReal(double value)
    {
        Value v;
        v.type = TYPE_REAL;
        v.r = value;
        return v;
    }

    static Value makeString(const std::string &value)
    {
        Value v;
        v.type = TYPE_STR;
        v.s = value;
        return v;
    }

    static Value makeChar(unsigned char value)
    {
        Value v;
        v.type = TYPE_CHAR;
        v.c = value;
        return v;
    }

    static std::string charToString(unsigned char c)
    {
        if (c == 0)
            return std::string();
        return std::string(1, static_cast<char>(c));
    }

    static bool isStringLike(const Value &value)
    {
        return value.type == TYPE_STR || value.type == TYPE_CHAR;
    }

    static bool isNumeric(const Value &value)
    {
        return value.type == TYPE_INT || value.type == TYPE_REAL;
    }

    static std::string valueAsString(const Value &value)
    {
        char buf[128];

        switch (value.type)
        {
        case TYPE_STR:
            return value.s;
        case TYPE_CHAR:
            return charToString(value.c);
        case TYPE_INT:
            std::snprintf(buf, sizeof(buf), "%d", value.i);
            return std::string(buf);
        case TYPE_REAL:
            std::snprintf(buf, sizeof(buf), "%.11g", value.r);
            return std::string(buf);
        default:
            return std::string();
        }
    }

    static std::string uppercaseAscii(std::string value)
    {
        for (std::string::size_type i = 0; i < value.size(); ++i)
            value[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[i])));
        return value;
    }

    static std::string removeSpaceAscii(const std::string &value)
    {
        std::string out;
        bool previousWasSpace = false;
        std::size_t i = 0;
        std::size_t end = value.size();

        while (i < end && value[i] == ' ')
            ++i;
        while (end > i && value[end - 1] == ' ')
            --end;

        for (; i < end; ++i)
        {
            char ch = value[i];
            if (ch == ' ')
            {
                if (!previousWasSpace)
                    out.push_back(' ');
                previousWasSpace = true;
            }
            else
            {
                out.push_back(ch);
                previousWasSpace = false;
            }
        }
        return out;
    }

    static std::size_t findLastPathSeparator(const std::string &value)
    {
        std::size_t slash = value.find_last_of("\\/");
        if (slash == std::string::npos)
            return std::string::npos;
        return slash;
    }

    static std::size_t baseNameStart(const std::string &value)
    {
        std::size_t sep = findLastPathSeparator(value);
        if (sep != std::string::npos)
            return sep + 1;
        if (value.size() >= 2 && value[1] == ':')
            return 2;
        return 0;
    }

    static std::string getExtensionPart(const std::string &value)
    {
        std::size_t baseStart = baseNameStart(value);
        std::size_t dot = value.find_last_of('.');
        if (dot == std::string::npos || dot < baseStart)
            return std::string();
        return value.substr(dot);
    }

    static std::string getPathPart(const std::string &value)
    {
        std::size_t sep = findLastPathSeparator(value);
        if (sep != std::string::npos)
            return value.substr(0, sep + 1);
        if (value.size() >= 2 && value[1] == ':')
            return value.substr(0, 2);
        return std::string();
    }

    static std::string truncateExtensionPart(const std::string &value)
    {
        std::size_t baseStart = baseNameStart(value);
        std::size_t dot = value.find_last_of('.');
        if (dot == std::string::npos || dot < baseStart)
            return value;
        return value.substr(0, dot);
    }

    static std::string truncatePathPart(const std::string &value)
    {
        return value.substr(baseNameStart(value));
    }

    static double valueAsReal(const Value &value)
    {
        if (value.type == TYPE_REAL)
            return value.r;
        if (value.type == TYPE_INT)
            return static_cast<double>(value.i);
        throw std::runtime_error("numeric value expected");
    }

    static int valueAsInt(const Value &value)
    {
        if (value.type == TYPE_INT)
            return value.i;
        throw std::runtime_error("integer value expected");
    }

    static int compareValues(const Value &a, const Value &b)
    {
        if (isStringLike(a) && isStringLike(b))
        {
            std::string as = valueAsString(a);
            std::string bs = valueAsString(b);
            if (as < bs)
                return -1;
            if (as > bs)
                return 1;
            return 0;
        }

        if (isNumeric(a) && isNumeric(b))
        {
            double av = valueAsReal(a);
            double bv = valueAsReal(b);
            if (av < bv)
                return -1;
            if (av > bv)
                return 1;
            return 0;
        }

        throw std::runtime_error("type mismatch");
    }

    static Value defaultValueForType(int type)
    {
        switch (type)
        {
        case TYPE_INT:
            return makeInt(0);
        case TYPE_REAL:
            return makeReal(0.0);
        case TYPE_CHAR:
            return makeChar(0);
        case TYPE_STR:
        default:
            return makeString("");
        }
    }

    static Value coerceForStore(const Value &value, int targetType)
    {
        switch (targetType)
        {
        case TYPE_INT:
            if (value.type == TYPE_INT)
                return value;
            throw std::runtime_error("type mismatch");

        case TYPE_REAL:
            if (value.type == TYPE_REAL)
                return value;
            if (value.type == TYPE_INT)
                return makeReal(static_cast<double>(value.i));
            throw std::runtime_error("type mismatch");

        case TYPE_STR:
            if (value.type == TYPE_STR)
                return value;
            if (value.type == TYPE_CHAR)
                return makeString(charToString(value.c));
            throw std::runtime_error("type mismatch");

        case TYPE_CHAR:
            if (value.type == TYPE_CHAR)
                return value;
            if (value.type == TYPE_STR)
            {
                if (value.s.empty())
                    return makeChar(0);
                return makeChar(static_cast<unsigned char>(value.s[0]));
            }
            throw std::runtime_error("type mismatch");

        default:
            throw std::runtime_error("unknown variable type");
        }
    }

    static void enforceStringLength(const std::string &s)
    {
        if (s.size() > 254)
            throw std::runtime_error("String length error.");
    }

    static int checkedStringIndex(int pos)
    {
        if (pos < 1 || pos > 254)
            throw std::runtime_error("Invalid string index on string copy operation.");
        return pos;
    }

    static int checkedInsertIndex(int pos)
    {
        if (pos < 0 || pos > 254)
            throw std::runtime_error("Invalid string index on string copy operation.");
        return pos;
    }

    static int findValErrorPosition(const std::string &text)
    {
        std::size_t i = 0;
        const std::size_t n = text.size();

        while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i == n)
            return 1;

        if (text[i] == '+' || text[i] == '-')
            ++i;

        {
            const std::size_t firstDigit = i;
            while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
                ++i;
            if (i == firstDigit)
                return static_cast<int>(firstDigit + 1);
        }

        while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i != n)
            return static_cast<int>(i + 1);
        return 0;
    }

    static int findRValErrorPosition(const std::string &text)
    {
        std::size_t i = 0;
        const std::size_t n = text.size();

        while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i == n)
            return 1;

        if (text[i] == '+' || text[i] == '-')
            ++i;

        {
            bool seenDigits = false;
            while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
            {
                seenDigits = true;
                ++i;
            }
            if (i < n && text[i] == '.')
            {
                ++i;
                while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
                {
                    seenDigits = true;
                    ++i;
                }
            }
            if (!seenDigits)
                return static_cast<int>(i + 1);
        }

        if (i < n && (text[i] == 'e' || text[i] == 'E'))
        {
            const std::size_t expPos = i;
            ++i;
            if (i < n && (text[i] == '+' || text[i] == '-'))
                ++i;
            {
                const std::size_t firstExpDigit = i;
                while (i < n && std::isdigit(static_cast<unsigned char>(text[i])))
                    ++i;
                if (i == firstExpDigit)
                    return static_cast<int>(expPos + 1);
            }
        }

        while (i < n && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i != n)
            return static_cast<int>(i + 1);
        return 0;
    }


    static std::string commandFirstLine(const std::string &command)
    {
        std::string line;
        FILE *pipe = ::popen(command.c_str(), "r");
        if (pipe == NULL)
            return std::string();
        char buffer[512];
        if (std::fgets(buffer, sizeof(buffer), pipe) != NULL)
            line = buffer;
        ::pclose(pipe);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        return line;
    }

    static std::string detectExecutablePathFromProc()
    {
        char buf[4096];
        ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0)
            return std::string();
        buf[n] = '\0';
        return std::string(buf);
    }

    static std::string normalizeDirPath(const std::string &path)
    {
        if (path.empty())
            return std::string("./");
        std::string out = path;
        if (out.back() != '/')
            out.push_back('/');
        return out;
    }

    static std::string detectExecutableDir(const std::string &argv0)
    {
        std::string path = detectExecutablePathFromProc();
        if (path.empty())
            path = argv0;
        if (path.empty())
        {
            char cwd[4096];
            if (::getcwd(cwd, sizeof(cwd)) != NULL)
                return normalizeDirPath(std::string(cwd));
            return std::string("./");
        }
        std::size_t sep = path.find_last_of("/");
        if (sep == std::string::npos)
        {
            char cwd[4096];
            if (::getcwd(cwd, sizeof(cwd)) != NULL)
                return normalizeDirPath(std::string(cwd));
            return std::string("./");
        }
        return normalizeDirPath(path.substr(0, sep));
    }

    static std::string detectShellPath()
    {
        const char *comspec = std::getenv("COMSPEC");
        if (comspec != NULL && *comspec != '\0')
            return std::string(comspec);
        const char *shell = std::getenv("SHELL");
        if (shell != NULL && *shell != '\0')
            return std::string(shell);
        return std::string("/bin/sh");
    }

    static std::string detectShellVersion(const std::string &shellPath)
    {
        if (shellPath.empty())
            return std::string();
        const char *bashVersion = std::getenv("BASH_VERSION");
        const char *zshVersion = std::getenv("ZSH_VERSION");
        const char *fishVersion = std::getenv("FISH_VERSION");
        std::string base = shellPath.substr(shellPath.find_last_of('/') == std::string::npos ? 0 : shellPath.find_last_of('/') + 1);
        if (base == "bash" && bashVersion != NULL && *bashVersion != '\0')
            return std::string("bash ") + bashVersion;
        if (base == "zsh" && zshVersion != NULL && *zshVersion != '\0')
            return std::string("zsh ") + zshVersion;
        if (base == "fish" && fishVersion != NULL && *fishVersion != '\0')
            return std::string("fish ") + fishVersion;
        std::string command = "'";
        for (std::string::size_type i = 0; i < shellPath.size(); ++i)
        {
            if (shellPath[i] == '\'')
                command += "'\\''";
            else
                command.push_back(shellPath[i]);
        }
        command += "' --version 2>/dev/null";
        std::string line = commandFirstLine(command);
        if (!line.empty())
            return line;
        return base;
    }

    static int detectCpuCode()
    {
    #if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        return 3;
    #elif defined(__aarch64__) || defined(__arm__) || defined(__riscv) || defined(__powerpc__) || defined(__ppc64__)
        return 3;
    #else
        return 3;
    #endif
    }

    static std::string getenvValue(const std::string &name)
    {
        const char *value = std::getenv(name.c_str());
        if (value == NULL)
            return std::string();
        return std::string(value);
    }

    static std::string getEnvironmentValue(const std::string &entryName)
    {
        std::string key = trimAscii(entryName);
        std::size_t pos = key.find('=');
        if (pos != std::string::npos)
            key = key.substr(0, pos);
        if (key.empty())
            return std::string();
        std::string direct = getenvValue(key);
        if (!direct.empty())
            return direct;
        std::string up = upperKey(key);
        if (up == "MR_PATH")
            return g_runtimeEnv.executableDir;
        if (up == "COMSPEC")
            return g_runtimeEnv.shellPath;
        if (up == "OS_VERSION")
            return g_runtimeEnv.shellVersion;
        return std::string();
    }

    static bool changeDirectoryPath(const std::string &path)
    {
        std::string expanded = expandUserPath(trimAscii(path));
        if (expanded.empty())
            return false;
        return ::chdir(expanded.c_str()) == 0;
    }

    static bool deleteFilePath(const std::string &path)
    {
        std::string expanded = expandUserPath(trimAscii(path));
        if (expanded.empty())
            return false;
        return std::remove(expanded.c_str()) == 0;
    }

    static std::string expandUserPath(const std::string &path)
    {
        if (path.size() >= 2 && path[0] == '~' && path[1] == '/')
        {
            const char *home = std::getenv("HOME");
            if (home != NULL && *home != '\0')
                return std::string(home) + path.substr(1);
        }
        return path;
    }

    static bool fileExistsPath(const std::string &path)
    {
        struct stat st;
        std::string expanded = expandUserPath(trimAscii(path));
        if (expanded.empty())
            return false;
        return ::stat(expanded.c_str(), &st) == 0;
    }

    static int findFirstFileMatch(const std::string &pattern)
    {
        glob_t g;
        std::string expanded = expandUserPath(trimAscii(pattern));
        int rc;

        g_runtimeEnv.fileMatches.clear();
        g_runtimeEnv.fileMatchIndex = 0;
        g_runtimeEnv.lastFileName.clear();

        std::memset(&g, 0, sizeof(g));
        rc = ::glob(expanded.c_str(), 0, NULL, &g);
        if (rc == 0)
        {
            for (std::size_t i = 0; i < g.gl_pathc; ++i)
                g_runtimeEnv.fileMatches.push_back(std::string(g.gl_pathv[i]));
            ::globfree(&g);
            if (!g_runtimeEnv.fileMatches.empty())
            {
                g_runtimeEnv.lastFileName = g_runtimeEnv.fileMatches[0];
                return 0;
            }
        }
        else
            ::globfree(&g);

        if (fileExistsPath(expanded))
        {
            g_runtimeEnv.fileMatches.push_back(expanded);
            g_runtimeEnv.lastFileName = expanded;
            return 0;
        }

        return 18;
    }

    static int findNextFileMatch()
    {
        if (g_runtimeEnv.fileMatches.empty())
            return 18;
        if (g_runtimeEnv.fileMatchIndex + 1 >= g_runtimeEnv.fileMatches.size())
            return 18;
        ++g_runtimeEnv.fileMatchIndex;
        g_runtimeEnv.lastFileName = g_runtimeEnv.fileMatches[g_runtimeEnv.fileMatchIndex];
        return 0;
    }

    static TMREditWindow *currentEditWindow()
    {
        if (TProgram::deskTop == 0 || TProgram::deskTop->current == 0)
            return NULL;
        return dynamic_cast<TMREditWindow *>(TProgram::deskTop->current);
    }

    static TFileEditor *currentEditor()
    {
        TMREditWindow *win = currentEditWindow();
        return win != NULL ? win->getEditor() : NULL;
    }

    static TMREditWindow *currentEditWindow();

    static Value loadCurrentFileState(const std::string &key)
    {
        TMREditWindow *win = currentEditWindow();
        if (key == "FIRST_SAVE")
            return makeInt(win != NULL && win->hasBeenSavedInSession() ? 1 : 0);
        if (key == "EOF_IN_MEM")
            return makeInt(win != NULL && win->eofInMemory() ? 1 : 0);
        if (key == "BUFFER_ID")
            return makeInt(win != NULL ? win->bufferId() : 0);
        if (key == "TMP_FILE")
            return makeInt(win != NULL && win->isTemporaryFile() ? 1 : 0);
        if (key == "TMP_FILE_NAME")
            return makeString(win != NULL ? win->temporaryFileName() : "");
        if (key == "FILE_CHANGED")
            return makeInt(win != NULL && win->isFileChanged() ? 1 : 0);
        if (key == "FILE_NAME")
            return makeString(win != NULL ? win->currentFileName() : "");
        return makeInt(0);
    }

    static std::string snapshotEditorText(TFileEditor *editor)
    {
        std::string out;
        if (editor == NULL)
            return out;
        out.reserve(editor->bufLen);
        for (uint i = 0; i < editor->bufLen; ++i)
            out.push_back(editor->bufChar(i));
        return out;
    }

    static bool searchEditorForward(TFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd)
    {
        std::string text;
        std::string haystack;
        std::string query;
        std::size_t startPos;
        std::size_t endPos;
        std::size_t found;

        matchStart = matchEnd = 0;
        if (editor == NULL || needle.empty())
            return false;

        text = snapshotEditorText(editor);
        startPos = std::min<std::size_t>(editor->curPtr, text.size());
        endPos = searchLimitForward(text, startPos, numLines);
        if (endPos < startPos)
            endPos = startPos;

        haystack = text.substr(startPos, endPos - startPos);
        query = needle;
        if (ignoreCase)
        {
            haystack = uppercaseAscii(haystack);
            query = uppercaseAscii(query);
        }

        found = haystack.find(query);
        if (found == std::string::npos)
            return false;

        matchStart = startPos + found;
        matchEnd = matchStart + needle.size();
        if (matchEnd > text.size())
            return false;
        return true;
    }

    static bool searchEditorBackward(TFileEditor *editor, const std::string &needle, int numLines, bool ignoreCase, std::size_t &matchStart, std::size_t &matchEnd)
    {
        std::string text;
        std::string haystack;
        std::string query;
        std::size_t startPos;
        std::size_t endPos;
        std::size_t found;

        matchStart = matchEnd = 0;
        if (editor == NULL || needle.empty())
            return false;

        text = snapshotEditorText(editor);
        endPos = std::min<std::size_t>(editor->curPtr, text.size());
        startPos = searchLimitBackward(text, endPos, numLines);
        if (endPos < startPos)
            endPos = startPos;

        haystack = text.substr(startPos, endPos - startPos + std::min<std::size_t>(needle.size(), text.size() - endPos));
        query = needle;
        if (ignoreCase)
        {
            haystack = uppercaseAscii(haystack);
            query = uppercaseAscii(query);
        }

        found = haystack.rfind(query, endPos - startPos);
        if (found == std::string::npos)
            return false;

        matchStart = startPos + found;
        matchEnd = matchStart + needle.size();
        if (matchEnd > text.size())
            return false;
        return true;
    }

    static bool replaceLastSearch(TFileEditor *editor, const std::string &replacement)
    {
        TMREditWindow *win = currentEditWindow();
        const char *fileName;
        if (editor == NULL || !g_runtimeEnv.lastSearchValid)
            return false;
        if (win == NULL || g_runtimeEnv.lastSearchWindow != win)
            return false;
        fileName = win->currentFileName();
        if (g_runtimeEnv.lastSearchFileName != std::string(fileName != NULL ? fileName : ""))
            return false;
        if (editor->curPtr != g_runtimeEnv.lastSearchCursor)
            return false;
        if (g_runtimeEnv.lastSearchEnd < g_runtimeEnv.lastSearchStart || g_runtimeEnv.lastSearchEnd > editor->bufLen)
            return false;

        editor->lock();
        editor->deleteRange(static_cast<uint>(g_runtimeEnv.lastSearchStart), static_cast<uint>(g_runtimeEnv.lastSearchEnd), False);
        editor->setCurPtr(static_cast<uint>(g_runtimeEnv.lastSearchStart), 0);
        if (!replacement.empty())
            editor->insertText(replacement.c_str(), static_cast<uint>(replacement.size()), False);
        editor->setCurPtr(static_cast<uint>(g_runtimeEnv.lastSearchStart), 0);
        editor->setSelect(static_cast<uint>(g_runtimeEnv.lastSearchStart), static_cast<uint>(g_runtimeEnv.lastSearchStart + replacement.size()), False);
        editor->trackCursor(True);
        editor->unlock();
        editor->doUpdate();

        g_runtimeEnv.lastSearchEnd = g_runtimeEnv.lastSearchStart + replacement.size();
        g_runtimeEnv.lastSearchCursor = g_runtimeEnv.lastSearchStart;
        g_runtimeEnv.lastSearchValid = false;
        return true;
    }

    static Value currentEditorCharValue()
    {
        TFileEditor *editor = currentEditor();
        uint lineEnd;
        if (editor == NULL)
            return makeChar(static_cast<char>(255));
        lineEnd = editor->lineEnd(editor->curPtr);
        if (editor->curPtr >= editor->bufLen || editor->curPtr >= lineEnd)
            return makeChar(static_cast<char>(255));
        return makeChar(editor->bufChar(editor->curPtr));
    }

    static int defaultTabWidth()
    {
        return 8;
    }

    static bool isVirtualChar(char c)
    {
        return static_cast<unsigned char>(c) == 255;
    }

    static int nextTabStopColumn(int col)
    {
        int width = defaultTabWidth();
        if (col < 1)
            col = 1;
        return ((col - 1) / width + 1) * width + 1;
    }

    static int prevTabStopColumn(int col)
    {
        int width = defaultTabWidth();
        if (col <= 1)
            return 1;
        return ((col - 2) / width) * width + 1;
    }

    static std::string makeIndentFill(int targetCol, bool preferTabs)
    {
        std::string out;
        int col = 1;
        if (targetCol < 1)
            targetCol = 1;
        while (col < targetCol)
        {
            int next = nextTabStopColumn(col);
            if (preferTabs && next <= targetCol)
            {
                out.push_back('	');
                col = next;
            }
            else
            {
                out.push_back(' ');
                ++col;
            }
        }
        return out;
    }

    static std::string crunchTabsString(const std::string &value)
    {
        std::string out;
        out.reserve(value.size());
        for (std::string::size_type i = 0; i < value.size(); ++i)
            if (!isVirtualChar(value[i]))
                out.push_back(value[i]);
        return out;
    }

    static std::string expandTabsString(const std::string &value, bool toVirtuals)
    {
        std::string out;
        int col = 1;
        out.reserve(value.size());
        for (std::string::size_type i = 0; i < value.size(); ++i)
        {
            unsigned char ch = static_cast<unsigned char>(value[i]);
            if (ch == '	')
            {
                int next = nextTabStopColumn(col);
                int width = next - col;
                if (toVirtuals)
                {
                    out.push_back('	');
                    for (int n = 1; n < width; ++n)
                        out.push_back(static_cast<char>(255));
                }
                else
                {
                    for (int n = 0; n < width; ++n)
                        out.push_back(' ');
                }
                col = next;
            }
            else
            {
                out.push_back(value[i]);
                if (ch == '\n' || ch == '\r')
                    col = 1;
                else
                    ++col;
            }
        }
        enforceStringLength(out);
        return out;
    }

    static std::string tabsToSpacesString(const std::string &value)
    {
        std::string out;
        int col = 1;
        out.reserve(value.size());
        for (std::string::size_type i = 0; i < value.size(); ++i)
        {
            unsigned char ch = static_cast<unsigned char>(value[i]);
            if (ch == '	')
            {
                int next = nextTabStopColumn(col);
                int width = next - col;
                for (int n = 0; n < width; ++n)
                    out.push_back(' ');
                col = next;
                while (i + 1 < value.size() && isVirtualChar(value[i + 1]))
                    ++i;
            }
            else if (isVirtualChar(value[i]))
            {
                out.push_back(' ');
                ++col;
            }
            else
            {
                out.push_back(value[i]);
                if (ch == '\n' || ch == '\r')
                    col = 1;
                else
                    ++col;
            }
        }
        enforceStringLength(out);
        return out;
    }

    static int currentEditorIndentLevel()
    {
        TMREditWindow *win = currentEditWindow();
        return win != NULL ? win->indentLevel() : 1;
    }

    static bool setCurrentEditorIndentLevel(int level)
    {
        TMREditWindow *win = currentEditWindow();
        if (win == NULL)
            return false;
        win->setIndentLevel(level);
        return true;
    }

    static bool currentEditorInsertMode()
    {
        TFileEditor *editor = currentEditor();
        return editor != NULL ? editor->overwrite == False : true;
    }

    static bool setCurrentEditorInsertMode(bool on)
    {
        TFileEditor *editor = currentEditor();
        if (editor == NULL)
            return false;
        editor->overwrite = on ? False : True;
        editor->drawView();
        return true;
    }

    static std::string currentEditorLineText(TFileEditor *editor)
    {
        std::string out;
        uint start;
        uint end;
        if (editor == NULL)
            return out;
        start = editor->lineStart(editor->curPtr);
        end = editor->lineEnd(editor->curPtr);
        out.reserve(end >= start ? end - start : 0);
        for (uint p = start; p < end; ++p)
            out.push_back(editor->bufChar(p));
        return out;
    }

    static std::string currentEditorWord(TFileEditor *editor, const std::string &delimiters)
    {
        std::string out;
        uint pos;
        uint end;
        if (editor == NULL)
            return out;
        pos = editor->curPtr;
        end = editor->lineEnd(pos);
        while (pos < end)
        {
            char c = editor->bufChar(pos);
            if (delimiters.find(c) != std::string::npos)
                break;
            out.push_back(c);
            pos = editor->nextChar(pos);
        }
        editor->setCurPtr(pos, 0);
        editor->trackCursor(True);
        editor->doUpdate();
        enforceStringLength(out);
        return out;
    }

    static bool insertEditorText(TFileEditor *editor, const std::string &text)
    {
        uint endSel;
        if (editor == NULL)
            return false;
        editor->lock();
        if (editor->overwrite == True && editor->hasSelection() == False)
        {
            endSel = editor->curPtr;
            for (std::string::size_type i = 0; i < text.size() && endSel < editor->lineEnd(editor->curPtr); ++i)
                endSel = editor->nextChar(endSel);
            if (endSel > editor->curPtr)
                editor->setSelect(editor->curPtr, endSel, False);
        }
        if (!text.empty())
            editor->insertText(text.c_str(), static_cast<uint>(text.size()), False);
        editor->trackCursor(True);
        editor->unlock();
        editor->doUpdate();
        return true;
    }

    static bool replaceEditorLine(TFileEditor *editor, const std::string &text)
    {
        uint start;
        uint end;
        if (editor == NULL)
            return false;
        start = editor->lineStart(editor->curPtr);
        end = editor->lineEnd(editor->curPtr);
        editor->lock();
        editor->deleteRange(start, end, False);
        editor->setCurPtr(start, 0);
        if (!text.empty())
            editor->insertText(text.c_str(), static_cast<uint>(text.size()), False);
        editor->setCurPtr(start, 0);
        editor->trackCursor(True);
        editor->unlock();
        editor->doUpdate();
        return true;
    }

    static bool deleteEditorChars(TFileEditor *editor, int count)
    {
        uint start;
        uint end;
        if (editor == NULL)
            return false;
        if (count <= 0)
            return true;
        start = editor->curPtr;
        end = start;
        for (int i = 0; i < count && end < editor->bufLen; ++i)
            end = editor->nextChar(end);
        if (end <= start)
            return true;
        editor->lock();
        editor->deleteRange(start, end, False);
        editor->setCurPtr(start, 0);
        editor->trackCursor(True);
        editor->unlock();
        editor->doUpdate();
        return true;
    }

    static bool deleteEditorLine(TFileEditor *editor)
    {
        uint start;
        uint end;
        if (editor == NULL)
            return false;
        start = editor->lineStart(editor->curPtr);
        end = editor->nextLine(editor->curPtr);
        if (end < start)
            end = start;
        if (end > editor->bufLen)
            end = editor->bufLen;
        editor->lock();
        editor->deleteRange(start, end, False);
        if (start > editor->bufLen)
            start = editor->bufLen;
        editor->setCurPtr(start, 0);
        editor->trackCursor(True);
        editor->unlock();
        editor->doUpdate();
        return true;
    }

    static int currentEditorColumn(TFileEditor *editor)
    {
        uint lineStart;
        if (editor == NULL)
            return 1;
        lineStart = editor->lineStart(editor->curPtr);
        return editor->charPos(lineStart, editor->curPtr) + 1;
    }

    static int currentEditorLineNumber(TFileEditor *editor)
    {
        if (editor == NULL)
            return 1;
        return editor->curPos.y + 1;
    }

    static bool setEditorCursor(TFileEditor *editor, uint target)
    {
        TMREditWindow *win;
        if (editor == NULL)
            return false;
        if (target > editor->bufLen)
            target = editor->bufLen;
        editor->setCurPtr(target, 0);
        win = currentEditWindow();
        if (win != NULL && win->isBlockMarking())
            win->refreshBlockVisual();
        else
        {
            editor->trackCursor(True);
            editor->doUpdate();
        }
        return true;
    }

    static bool moveEditorLeft(TFileEditor *editor)
    {
        uint start;
        uint target;
        if (editor == NULL)
            return false;
        start = editor->lineStart(editor->curPtr);
        if (editor->curPtr > start)
            target = editor->prevChar(editor->curPtr);
        else if (start > 0)
            target = editor->lineEnd(editor->prevLine(start));
        else
            target = 0;
        return setEditorCursor(editor, target);
    }

    static bool moveEditorRight(TFileEditor *editor)
    {
        uint lineEnd;
        uint target;
        if (editor == NULL)
            return false;
        lineEnd = editor->lineEnd(editor->curPtr);
        if (editor->curPtr < lineEnd)
            target = editor->nextChar(editor->curPtr);
        else
            target = editor->curPtr;
        return setEditorCursor(editor, target);
    }

    static bool moveEditorUp(TFileEditor *editor)
    {
        if (editor == NULL)
            return false;
        return setEditorCursor(editor, editor->lineMove(editor->curPtr, -1));
    }

    static bool moveEditorDown(TFileEditor *editor)
    {
        if (editor == NULL)
            return false;
        return setEditorCursor(editor, editor->lineMove(editor->curPtr, 1));
    }

    static bool moveEditorHome(TFileEditor *editor)
    {
        uint start;
        if (editor == NULL)
            return false;
        start = editor->lineStart(editor->curPtr);
        return setEditorCursor(editor, editor->charPtr(start, currentEditorIndentLevel() - 1));
    }

    static bool moveEditorEol(TFileEditor *editor)
    {
        if (editor == NULL)
            return false;
        return setEditorCursor(editor, editor->lineEnd(editor->curPtr));
    }

    static bool moveEditorTof(TFileEditor *editor)
    {
        if (editor == NULL)
            return false;
        return setEditorCursor(editor, 0);
    }

    static bool moveEditorEof(TFileEditor *editor)
    {
        if (editor == NULL)
            return false;
        return setEditorCursor(editor, editor->bufLen);
    }

    static bool moveEditorWordLeft(TFileEditor *editor)
    {
        if (editor == NULL)
            return false;
        return setEditorCursor(editor, editor->prevWord(editor->curPtr));
    }

    static bool moveEditorWordRight(TFileEditor *editor)
    {
        if (editor == NULL)
            return false;
        return setEditorCursor(editor, editor->nextWord(editor->curPtr));
    }

    static bool moveEditorFirstWord(TFileEditor *editor)
    {
        uint pos;
        uint end;
        if (editor == NULL)
            return false;
        pos = editor->lineStart(editor->curPtr);
        end = editor->lineEnd(editor->curPtr);
        while (pos < end)
        {
            char c = editor->bufChar(pos);
            if (c != ' ' && c != '	')
                break;
            pos = editor->nextChar(pos);
        }
        return setEditorCursor(editor, pos);
    }

    static bool gotoEditorLine(TFileEditor *editor, int lineNum)
    {
        uint pos = 0;
        if (editor == NULL || lineNum < 1 || lineNum > 32767)
            return false;
        for (int i = 1; i < lineNum && pos < editor->bufLen; ++i)
            pos = editor->nextLine(pos);
        return setEditorCursor(editor, pos);
    }

    static bool gotoEditorCol(TFileEditor *editor, int colNum)
    {
        uint start;
        if (editor == NULL || colNum < 1 || colNum > 254)
            return false;
        start = editor->lineStart(editor->curPtr);
        return setEditorCursor(editor, editor->charPtr(start, colNum - 1));
    }

    static bool currentEditorAtEof(TFileEditor *editor)
    {
        return editor == NULL || editor->curPtr >= editor->bufLen;
    }

    static bool currentEditorAtEol(TFileEditor *editor)
    {
        uint lineEnd;
        if (editor == NULL)
            return true;
        lineEnd = editor->lineEnd(editor->curPtr);
        return editor->curPtr >= lineEnd;
    }

    static int currentEditorRow(TFileEditor *editor)
    {
        if (editor == NULL)
            return 1;
        return std::max(1, editor->curPos.y - editor->delta.y + 1);
    }

    static bool markEditorPosition(TMREditWindow *win, TFileEditor *editor)
    {
        if (win == NULL || editor == NULL)
            return false;
        g_runtimeEnv.markStacks[win].push_back(editor->curPtr);
        return true;
    }

    static bool gotoEditorMark(TMREditWindow *win, TFileEditor *editor)
    {
        std::map<const void *, std::vector<uint> >::iterator it;
        uint pos;
        if (win == NULL || editor == NULL)
            return false;
        it = g_runtimeEnv.markStacks.find(win);
        if (it == g_runtimeEnv.markStacks.end() || it->second.empty())
            return false;
        pos = it->second.back();
        it->second.pop_back();
        return setEditorCursor(editor, pos);
    }

    static bool popEditorMark(TMREditWindow *win)
    {
        std::map<const void *, std::vector<uint> >::iterator it;
        if (win == NULL)
            return false;
        it = g_runtimeEnv.markStacks.find(win);
        if (it == g_runtimeEnv.markStacks.end() || it->second.empty())
            return false;
        it->second.pop_back();
        return true;
    }

    static bool moveEditorPageUp(TFileEditor *editor)
    {
        int pageLines;
        if (editor == NULL)
            return false;
        pageLines = std::max(1, editor->size.y - 1);
        return setEditorCursor(editor, editor->lineMove(editor->curPtr, -pageLines));
    }

    static bool moveEditorPageDown(TFileEditor *editor)
    {
        int pageLines;
        if (editor == NULL)
            return false;
        pageLines = std::max(1, editor->size.y - 1);
        return setEditorCursor(editor, editor->lineMove(editor->curPtr, pageLines));
    }

    static bool moveEditorNextPageBreak(TFileEditor *editor)
    {
        std::string text;
        std::string::size_type pos;
        if (editor == NULL)
            return false;
        text = snapshotEditorText(editor);
        pos = text.find('\f', std::min<std::size_t>(editor->curPtr, text.size()));
        if (pos == std::string::npos)
            return false;
        return setEditorCursor(editor, editor->nextLine(static_cast<uint>(pos)));
    }

    static bool moveEditorLastPageBreak(TFileEditor *editor)
    {
        std::string text;
        std::string::size_type pos;
        std::size_t start;
        if (editor == NULL)
            return false;
        text = snapshotEditorText(editor);
        start = std::min<std::size_t>(editor->curPtr, text.size());
        if (start == 0)
            return false;
        pos = text.rfind('\f', start - 1);
        if (pos == std::string::npos)
            return false;
        return setEditorCursor(editor, editor->nextLine(static_cast<uint>(pos)));
    }

    static bool replaceEditorBuffer(TFileEditor *editor, const std::string &text, std::size_t cursorPos)
    {
        if (editor == NULL)
            return false;
        if (cursorPos > text.size())
            cursorPos = text.size();
        editor->lock();
        editor->deleteRange(0, editor->bufLen, False);
        editor->setCurPtr(0, 0);
        if (!text.empty())
            editor->insertText(text.c_str(), static_cast<uint>(text.size()), False);
        if (cursorPos > editor->bufLen)
            cursorPos = editor->bufLen;
        editor->setSelect(static_cast<uint>(cursorPos), static_cast<uint>(cursorPos), False);
        editor->setCurPtr(static_cast<uint>(cursorPos), 0);
        editor->trackCursor(True);
        editor->unlock();
        editor->doUpdate();
        return true;
    }
    static SplitTextBuffer splitBufferLines(const std::string &text)
    {
        SplitTextBuffer out;
        std::string current;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            char c = text[i];
            if (c == '\r' || c == '\n')
            {
                out.lines.push_back(current);
                current.clear();
                out.trailingNewline = true;
                if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                    ++i;
            }
            else
            {
                current.push_back(c);
                out.trailingNewline = false;
            }
        }
        if (!current.empty() || !out.trailingNewline || out.lines.empty())
            out.lines.push_back(current);
        return out;
    }

    static std::string joinBufferLines(const SplitTextBuffer &buffer)
    {
        std::string out;
        for (std::size_t i = 0; i < buffer.lines.size(); ++i)
        {
            if (i > 0)
                out.push_back('\n');
            out += buffer.lines[i];
        }
        if (buffer.trailingNewline && !buffer.lines.empty())
            out.push_back('\n');
        return out;
    }

    static std::size_t bufferOffsetForLine(const SplitTextBuffer &buffer, int lineIndex)
    {
        std::size_t offset = 0;
        int limit = std::max(0, std::min(lineIndex, static_cast<int>(buffer.lines.size())));
        for (int i = 0; i < limit; ++i)
        {
            offset += buffer.lines[static_cast<std::size_t>(i)].size();
            if (static_cast<std::size_t>(i + 1) < buffer.lines.size() || buffer.trailingNewline)
                ++offset;
        }
        return offset;
    }

    static std::size_t bufferOffsetForLineColumn(const SplitTextBuffer &buffer, int lineIndex, int colIndex)
    {
        std::size_t offset;
        std::size_t col;
        if (buffer.lines.empty())
            return 0;
        lineIndex = std::max(0, std::min(lineIndex, static_cast<int>(buffer.lines.size()) - 1));
        col = static_cast<std::size_t>(std::max(0, colIndex));
        offset = bufferOffsetForLine(buffer, lineIndex);
        if (col > buffer.lines[static_cast<std::size_t>(lineIndex)].size())
            col = buffer.lines[static_cast<std::size_t>(lineIndex)].size();
        return offset + col;
    }

    static int lineIndexForPtr(TFileEditor *editor, uint ptr)
    {
        uint pos = 0;
        int line = 0;
        if (editor == NULL)
            return 0;
        if (ptr > editor->bufLen)
            ptr = editor->bufLen;
        while (pos < ptr && pos < editor->bufLen)
        {
            uint next = editor->nextLine(pos);
            if (next <= pos || next > ptr)
                break;
            pos = next;
            ++line;
        }
        return line;
    }

    static bool currentBlockInfo(TMREditWindow *win, TFileEditor *editor, int &mode, uint &anchor, uint &end)
    {
        if (win == NULL || editor == NULL || !win->hasBlock())
            return false;
        mode = win->blockStatus();
        anchor = win->blockAnchorPtr();
        end = win->blockEffectiveEndPtr();
        if (anchor > editor->bufLen)
            anchor = editor->bufLen;
        if (end > editor->bufLen)
            end = editor->bufLen;
        return mode != 0;
    }

    static bool copyCurrentBlock(TMREditWindow *win, TFileEditor *editor)
    {
        int mode;
        uint anchor;
        uint end;
        std::string text;
        if (!currentBlockInfo(win, editor, mode, anchor, end))
            return false;
        text = snapshotEditorText(editor);
        if (mode == TMREditWindow::bmStream)
        {
            std::size_t start = std::min<std::size_t>(anchor, end);
            std::size_t finish = std::max<std::size_t>(anchor, end);
            std::size_t dest = std::min<std::size_t>(editor->curPtr, text.size());
            std::string blockText = text.substr(start, finish - start);
            text.insert(dest, blockText);
            if (!replaceEditorBuffer(editor, text, dest + blockText.size()))
                return false;
            win->clearBlock();
            return true;
        }
        else if (mode == TMREditWindow::bmLine)
        {
            SplitTextBuffer buf = splitBufferLines(text);
            int line1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int line2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int destLine = lineIndexForPtr(editor, editor->curPtr);
            std::vector<std::string> blockLines;
            if (buf.lines.empty())
                return false;
            line1 = std::max(0, std::min(line1, static_cast<int>(buf.lines.size()) - 1));
            line2 = std::max(line1, std::min(line2, static_cast<int>(buf.lines.size()) - 1));
            destLine = std::max(0, std::min(destLine, static_cast<int>(buf.lines.size())));
            blockLines.assign(buf.lines.begin() + line1, buf.lines.begin() + line2 + 1);
            buf.lines.insert(buf.lines.begin() + destLine, blockLines.begin(), blockLines.end());
            if (!replaceEditorBuffer(editor, joinBufferLines(buf), bufferOffsetForLine(buf, destLine)))
                return false;
            win->clearBlock();
            return true;
        }
        else if (mode == TMREditWindow::bmColumn)
        {
            SplitTextBuffer buf = splitBufferLines(text);
            int row1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int row2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int col1 = std::min(win->blockCol1(), win->blockCol2());
            int col2 = std::max(win->blockCol1(), win->blockCol2());
            int width = std::max(1, col2 - col1);
            int destRow = lineIndexForPtr(editor, editor->curPtr);
            int destCol = std::max(0, currentEditorColumn(editor) - 1);
            std::vector<std::string> slices;
            if (buf.lines.empty())
                return false;
            row1 = std::max(0, std::min(row1, static_cast<int>(buf.lines.size()) - 1));
            row2 = std::max(row1, std::min(row2, static_cast<int>(buf.lines.size()) - 1));
            for (int row = row1; row <= row2; ++row)
            {
                const std::string &line = buf.lines[static_cast<std::size_t>(row)];
                std::string slice(static_cast<std::size_t>(width), ' ');
                std::size_t startCol = static_cast<std::size_t>(std::max(0, col1 - 1));
                if (startCol < line.size())
                {
                    std::size_t avail = std::min<std::size_t>(static_cast<std::size_t>(width), line.size() - startCol);
                    slice.replace(0, avail, line.substr(startCol, avail));
                }
                slices.push_back(slice);
            }
            while (static_cast<int>(buf.lines.size()) < destRow + static_cast<int>(slices.size()))
                buf.lines.push_back(std::string());
            for (std::size_t i = 0; i < slices.size(); ++i)
            {
                std::string &line = buf.lines[static_cast<std::size_t>(destRow) + i];
                if (line.size() < static_cast<std::size_t>(destCol))
                    line.append(static_cast<std::size_t>(destCol) - line.size(), ' ');
                line.insert(static_cast<std::size_t>(destCol), slices[i]);
            }
            if (!replaceEditorBuffer(editor, joinBufferLines(buf), bufferOffsetForLineColumn(buf, destRow, destCol)))
                return false;
            win->clearBlock();
            return true;
        }
        return false;
    }

    static bool moveCurrentBlock(TMREditWindow *win, TFileEditor *editor)
    {
        int mode;
        uint anchor;
        uint end;
        std::string text;
        if (!currentBlockInfo(win, editor, mode, anchor, end))
            return false;
        text = snapshotEditorText(editor);
        if (mode == TMREditWindow::bmStream)
        {
            std::size_t start = std::min<std::size_t>(anchor, end);
            std::size_t finish = std::max<std::size_t>(anchor, end);
            std::size_t dest = std::min<std::size_t>(editor->curPtr, text.size());
            std::string blockText = text.substr(start, finish - start);
            if (dest >= start && dest <= finish)
                return true;
            text.erase(start, finish - start);
            if (dest > finish)
                dest -= (finish - start);
            text.insert(dest, blockText);
            if (!replaceEditorBuffer(editor, text, dest + blockText.size()))
                return false;
            win->clearBlock();
            return true;
        }
        else if (mode == TMREditWindow::bmLine)
        {
            SplitTextBuffer buf = splitBufferLines(text);
            int line1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int line2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int destLine = lineIndexForPtr(editor, editor->curPtr);
            int count;
            std::vector<std::string> blockLines;
            if (buf.lines.empty())
                return false;
            line1 = std::max(0, std::min(line1, static_cast<int>(buf.lines.size()) - 1));
            line2 = std::max(line1, std::min(line2, static_cast<int>(buf.lines.size()) - 1));
            count = line2 - line1 + 1;
            if (destLine >= line1 && destLine <= line2 + 1)
                return true;
            blockLines.assign(buf.lines.begin() + line1, buf.lines.begin() + line2 + 1);
            buf.lines.erase(buf.lines.begin() + line1, buf.lines.begin() + line2 + 1);
            if (buf.lines.empty())
                buf.lines.push_back(std::string());
            if (destLine > line2)
                destLine -= count;
            destLine = std::max(0, std::min(destLine, static_cast<int>(buf.lines.size())));
            buf.lines.insert(buf.lines.begin() + destLine, blockLines.begin(), blockLines.end());
            if (!replaceEditorBuffer(editor, joinBufferLines(buf), bufferOffsetForLine(buf, destLine)))
                return false;
            win->clearBlock();
            return true;
        }
        else if (mode == TMREditWindow::bmColumn)
        {
            SplitTextBuffer buf = splitBufferLines(text);
            int row1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int row2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int col1 = std::min(win->blockCol1(), win->blockCol2());
            int col2 = std::max(win->blockCol1(), win->blockCol2());
            int width = std::max(1, col2 - col1);
            int destRow = lineIndexForPtr(editor, editor->curPtr);
            int destCol = std::max(0, currentEditorColumn(editor) - 1);
            std::vector<std::string> slices;
            int height;
            if (buf.lines.empty())
                return false;
            row1 = std::max(0, std::min(row1, static_cast<int>(buf.lines.size()) - 1));
            row2 = std::max(row1, std::min(row2, static_cast<int>(buf.lines.size()) - 1));
            height = row2 - row1 + 1;
            if (destRow >= row1 && destRow <= row2 && destCol >= col1 - 1 && destCol <= col1 - 1 + width)
                return true;
            for (int row = row1; row <= row2; ++row)
            {
                std::string &line = buf.lines[static_cast<std::size_t>(row)];
                std::size_t startCol = static_cast<std::size_t>(std::max(0, col1 - 1));
                std::string slice(static_cast<std::size_t>(width), ' ');
                if (startCol < line.size())
                {
                    std::size_t avail = std::min<std::size_t>(static_cast<std::size_t>(width), line.size() - startCol);
                    slice.replace(0, avail, line.substr(startCol, avail));
                    line.erase(startCol, avail);
                }
                slices.push_back(slice);
            }
            if (!(destRow + height - 1 < row1 || destRow > row2) && destCol > col1 - 1)
                destCol = std::max(0, destCol - width);
            while (static_cast<int>(buf.lines.size()) < destRow + static_cast<int>(slices.size()))
                buf.lines.push_back(std::string());
            for (std::size_t i = 0; i < slices.size(); ++i)
            {
                std::string &line = buf.lines[static_cast<std::size_t>(destRow) + i];
                if (line.size() < static_cast<std::size_t>(destCol))
                    line.append(static_cast<std::size_t>(destCol) - line.size(), ' ');
                line.insert(static_cast<std::size_t>(destCol), slices[i]);
            }
            if (!replaceEditorBuffer(editor, joinBufferLines(buf), bufferOffsetForLineColumn(buf, destRow, destCol)))
                return false;
            win->clearBlock();
            return true;
        }
        return false;
    }

    static bool deleteCurrentBlock(TMREditWindow *win, TFileEditor *editor)
    {
        int mode;
        uint anchor;
        uint end;
        std::string text;
        if (!currentBlockInfo(win, editor, mode, anchor, end))
            return false;
        text = snapshotEditorText(editor);
        if (mode == TMREditWindow::bmStream)
        {
            std::size_t start = std::min<std::size_t>(anchor, end);
            std::size_t finish = std::max<std::size_t>(anchor, end);
            text.erase(start, finish - start);
            if (!replaceEditorBuffer(editor, text, start))
                return false;
            win->clearBlock();
            return true;
        }
        else if (mode == TMREditWindow::bmLine)
        {
            SplitTextBuffer buf = splitBufferLines(text);
            int line1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int line2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            if (buf.lines.empty())
                return false;
            line1 = std::max(0, std::min(line1, static_cast<int>(buf.lines.size()) - 1));
            line2 = std::max(line1, std::min(line2, static_cast<int>(buf.lines.size()) - 1));
            buf.lines.erase(buf.lines.begin() + line1, buf.lines.begin() + line2 + 1);
            if (buf.lines.empty())
            {
                buf.lines.push_back(std::string());
                buf.trailingNewline = false;
            }
            if (!replaceEditorBuffer(editor, joinBufferLines(buf), bufferOffsetForLine(buf, std::min(line1, static_cast<int>(buf.lines.size()) - 1))))
                return false;
            win->clearBlock();
            return true;
        }
        else if (mode == TMREditWindow::bmColumn)
        {
            SplitTextBuffer buf = splitBufferLines(text);
            int row1 = std::min(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int row2 = std::max(lineIndexForPtr(editor, anchor), lineIndexForPtr(editor, end));
            int col1 = std::min(win->blockCol1(), win->blockCol2());
            int col2 = std::max(win->blockCol1(), win->blockCol2());
            int width = std::max(1, col2 - col1);
            if (buf.lines.empty())
                return false;
            row1 = std::max(0, std::min(row1, static_cast<int>(buf.lines.size()) - 1));
            row2 = std::max(row1, std::min(row2, static_cast<int>(buf.lines.size()) - 1));
            for (int row = row1; row <= row2; ++row)
            {
                std::string &line = buf.lines[static_cast<std::size_t>(row)];
                std::size_t startCol = static_cast<std::size_t>(std::max(0, col1 - 1));
                if (startCol < line.size())
                    line.erase(startCol, std::min<std::size_t>(static_cast<std::size_t>(width), line.size() - startCol));
            }
            if (!replaceEditorBuffer(editor, joinBufferLines(buf), bufferOffsetForLineColumn(buf, row1, std::max(0, col1 - 1))))
                return false;
            win->clearBlock();
            return true;
        }
        return false;
    }

    static bool moveEditorTabRight(TFileEditor *editor)
    {
        int col;
        int targetCol;
        uint lineStart;
        if (editor == NULL)
            return false;
        col = currentEditorColumn(editor);
        targetCol = nextTabStopColumn(col);
        if (editor->overwrite == False)
        {
            if (g_runtimeEnv.tabExpand)
                return insertEditorText(editor, std::string(1, '	'));
            return insertEditorText(editor, std::string(static_cast<std::size_t>(targetCol - col), ' '));
        }
        lineStart = editor->lineStart(editor->curPtr);
        return setEditorCursor(editor, editor->charPtr(lineStart, targetCol - 1));
    }

    static bool moveEditorTabLeft(TFileEditor *editor)
    {
        uint lineStart;
        int targetCol;
        if (editor == NULL)
            return false;
        lineStart = editor->lineStart(editor->curPtr);
        targetCol = prevTabStopColumn(currentEditorColumn(editor));
        return setEditorCursor(editor, editor->charPtr(lineStart, targetCol - 1));
    }

    static bool indentEditor(TFileEditor *editor)
    {
        if (!moveEditorTabRight(editor))
            return false;
        return setCurrentEditorIndentLevel(currentEditorColumn(editor));
    }

    static bool undentEditor(TFileEditor *editor)
    {
        if (!moveEditorTabLeft(editor))
            return false;
        return setCurrentEditorIndentLevel(currentEditorColumn(editor));
    }

    static bool carriageReturnEditor(TFileEditor *editor)
    {
        int indentLevel;
        std::string fill;
        if (editor == NULL)
            return false;
        indentLevel = currentEditorIndentLevel();
        editor->lock();
        editor->newLine();
        fill = makeIndentFill(indentLevel, g_runtimeEnv.tabExpand);
        if (!fill.empty())
            editor->insertText(fill.c_str(), static_cast<uint>(fill.size()), False);
        editor->trackCursor(True);
        editor->unlock();
        editor->doUpdate();
        return true;
    }

    static std::string formatCurrentDate()
    {
        char buf[32];
        std::time_t now = std::time(NULL);
        std::tm *tmv = std::localtime(&now);
        if (tmv == NULL)
            return std::string();
        std::strftime(buf, sizeof(buf), "%m/%d/%y", tmv);
        return std::string(buf);
    }

    static std::string formatCurrentTime()
    {
        char buf[32];
        std::time_t now = std::time(NULL);
        std::tm *tmv = std::localtime(&now);
        if (tmv == NULL)
            return std::string();
        std::strftime(buf, sizeof(buf), "%I:%M:%S%p", tmv);
        std::string out(buf);
        for (std::string::size_type i = 0; i < out.size(); ++i)
            out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
        return out;
    }

    static Value loadSpecialVariable(const std::string &name, bool &handled)
    {
        std::string key = upperKey(name);
        handled = true;
        if (key == "RETURN_INT")
            return makeInt(g_runtimeEnv.returnInt);
        if (key == "RETURN_STR")
            return makeString(g_runtimeEnv.returnStr);
        if (key == "ERROR_LEVEL")
            return makeInt(g_runtimeEnv.errorLevel);
        if (key == "IGNORE_CASE")
            return makeInt(g_runtimeEnv.ignoreCase ? 1 : 0);
        if (key == "TAB_EXPAND")
            return makeInt(g_runtimeEnv.tabExpand ? 1 : 0);
        if (key == "INSERT_MODE")
            return makeInt(currentEditorInsertMode() ? 1 : 0);
        if (key == "INDENT_LEVEL")
            return makeInt(currentEditorIndentLevel());
        if (key == "MPARM_STR")
            return makeString(g_runtimeEnv.parameterString);
        if (key == "DATE")
            return makeString(formatCurrentDate());
        if (key == "TIME")
            return makeString(formatCurrentTime());
        if (key == "COMSPEC")
            return makeString(g_runtimeEnv.shellPath);
        if (key == "MR_PATH")
            return makeString(g_runtimeEnv.executableDir);
        if (key == "OS_VERSION")
            return makeString(g_runtimeEnv.shellVersion);
        if (key == "PARAM_COUNT")
            return makeInt(static_cast<int>(g_runtimeEnv.processArgs.size()));
        if (key == "CPU")
            return makeInt(detectCpuCode());
        if (key == "C_COL")
            return makeInt(currentEditorColumn(currentEditor()));
        if (key == "C_LINE")
            return makeInt(currentEditorLineNumber(currentEditor()));
        if (key == "C_ROW")
            return makeInt(currentEditorRow(currentEditor()));
        if (key == "AT_EOF")
            return makeInt(currentEditorAtEof(currentEditor()) ? 1 : 0);
        if (key == "AT_EOL")
            return makeInt(currentEditorAtEol(currentEditor()) ? 1 : 0);
        if (key == "BLOCK_STAT")
        {
            TMREditWindow *win = currentEditWindow();
            return makeInt(win != NULL ? win->blockStatus() : 0);
        }
        if (key == "BLOCK_LINE1")
        {
            TMREditWindow *win = currentEditWindow();
            return makeInt(win != NULL ? win->blockLine1() : 0);
        }
        if (key == "BLOCK_LINE2")
        {
            TMREditWindow *win = currentEditWindow();
            return makeInt(win != NULL ? win->blockLine2() : 0);
        }
        if (key == "BLOCK_COL1")
        {
            TMREditWindow *win = currentEditWindow();
            return makeInt(win != NULL ? win->blockCol1() : 0);
        }
        if (key == "BLOCK_COL2")
        {
            TMREditWindow *win = currentEditWindow();
            return makeInt(win != NULL ? win->blockCol2() : 0);
        }
        if (key == "MARKING")
        {
            TMREditWindow *win = currentEditWindow();
            return makeInt(win != NULL && win->isBlockMarking() ? 1 : 0);
        }
        if (key == "LAST_FILE_NAME")
            return makeString(g_runtimeEnv.lastFileName);
        if (key == "GET_LINE")
            return makeString(currentEditorLineText(currentEditor()));
        if (key == "CUR_CHAR")
            return currentEditorCharValue();
        if (key == "FIRST_SAVE" || key == "EOF_IN_MEM" || key == "BUFFER_ID" ||
            key == "TMP_FILE" || key == "TMP_FILE_NAME" || key == "FILE_CHANGED" ||
            key == "FILE_NAME")
            return loadCurrentFileState(key);
        if (key == "FIRST_RUN")
        {
            if (!g_runtimeEnv.macroStack.empty())
                return makeInt(g_runtimeEnv.macroStack.back().firstRun ? 1 : 0);
            return makeInt(0);
        }
        if (key == "FIRST_MACRO")
        {
            g_runtimeEnv.macroEnumIndex = 0;
            while (g_runtimeEnv.macroEnumIndex < g_runtimeEnv.macroOrder.size())
            {
                const std::string &macroKey = g_runtimeEnv.macroOrder[g_runtimeEnv.macroEnumIndex++];
                std::map<std::string, MacroRef>::const_iterator it = g_runtimeEnv.loadedMacros.find(macroKey);
                if (it != g_runtimeEnv.loadedMacros.end())
                    return makeString(it->second.displayName);
            }
            return makeString("");
        }
        if (key == "NEXT_MACRO")
        {
            while (g_runtimeEnv.macroEnumIndex < g_runtimeEnv.macroOrder.size())
            {
                const std::string &macroKey = g_runtimeEnv.macroOrder[g_runtimeEnv.macroEnumIndex++];
                std::map<std::string, MacroRef>::const_iterator it = g_runtimeEnv.loadedMacros.find(macroKey);
                if (it != g_runtimeEnv.loadedMacros.end())
                    return makeString(it->second.displayName);
            }
            return makeString("");
        }
        handled = false;
        return makeInt(0);
    }

    static bool storeSpecialVariable(const std::string &name, const Value &value)
    {
        std::string key = upperKey(name);
        if (key == "RETURN_INT")
        {
            g_runtimeEnv.returnInt = valueAsInt(value);
            return true;
        }
        if (key == "RETURN_STR")
        {
            g_runtimeEnv.returnStr = valueAsString(value);
            enforceStringLength(g_runtimeEnv.returnStr);
            return true;
        }
        if (key == "ERROR_LEVEL")
        {
            g_runtimeEnv.errorLevel = valueAsInt(value);
            return true;
        }
        if (key == "IGNORE_CASE")
        {
            g_runtimeEnv.ignoreCase = valueAsInt(value) != 0;
            return true;
        }
        if (key == "TAB_EXPAND")
        {
            g_runtimeEnv.tabExpand = valueAsInt(value) != 0;
            return true;
        }
        if (key == "INSERT_MODE")
            return setCurrentEditorInsertMode(valueAsInt(value) != 0);
        if (key == "INDENT_LEVEL")
            return setCurrentEditorIndentLevel(valueAsInt(value));
        if (key == "MPARM_STR")
        {
            g_runtimeEnv.parameterString = valueAsString(value);
            enforceStringLength(g_runtimeEnv.parameterString);
            return true;
        }
        if (key == "FILE_CHANGED")
        {
            TMREditWindow *win = currentEditWindow();
            if (win != NULL)
                win->setFileChanged(valueAsInt(value) != 0);
            return true;
        }
        if (key == "FILE_NAME")
        {
            TMREditWindow *win = currentEditWindow();
            if (win != NULL)
                win->setCurrentFileName(valueAsString(value).c_str());
            return true;
        }
        if (key == "FIRST_RUN" || key == "FIRST_MACRO" || key == "NEXT_MACRO" || key == "LAST_FILE_NAME" ||
            key == "GET_LINE" || key == "CUR_CHAR" || key == "C_COL" || key == "C_LINE" ||
            key == "C_ROW" || key == "AT_EOF" || key == "AT_EOL" ||
            key == "BLOCK_STAT" || key == "BLOCK_LINE1" || key == "BLOCK_LINE2" ||
            key == "BLOCK_COL1" || key == "BLOCK_COL2" || key == "MARKING" ||
            key == "FIRST_SAVE" || key == "EOF_IN_MEM" || key == "BUFFER_ID" ||
            key == "TMP_FILE" || key == "TMP_FILE_NAME" || key == "COMSPEC" ||
            key == "MR_PATH" || key == "OS_VERSION" || key == "PARAM_COUNT" || key == "CPU")
            throw std::runtime_error("Attempt to assign to read-only system variable.");
        return false;
    }

    static std::string parseNamedValue(const std::string &needle, const std::string &source)
    {
        std::size_t pos = source.find(needle);
        if (pos == std::string::npos)
            return std::string();
        pos += needle.size();
        std::size_t end = source.find_first_of(" \t\r\n", pos);
        if (end == std::string::npos)
            end = source.size();
        return source.substr(pos, end - pos);
    }

    static void setGlobalValue(const std::string &name, int type, const Value &value)
    {
        std::string key = upperKey(name);
        if (g_runtimeEnv.globals.find(key) == g_runtimeEnv.globals.end())
            g_runtimeEnv.globalOrder.push_back(key);
        GlobalEntry entry;
        entry.type = type;
        entry.value = value;
        g_runtimeEnv.globals[key] = entry;
    }

    static bool fileExists(const std::string &path)
    {
        std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
        return in.good();
    }

    static std::string trimAscii(const std::string &value)
    {
        std::size_t start = 0;
        std::size_t end = value.size();
        while (start < end && std::isspace(static_cast<unsigned char>(value[start])))
            ++start;
        while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
            --end;
        return value.substr(start, end - start);
    }

    static std::string stripMrmacExtension(const std::string &value)
    {
        std::string upper = upperKey(value);
        if (upper.size() >= 6 && upper.substr(upper.size() - 6) == ".MRMAC")
            return value.substr(0, value.size() - 6);
        return value;
    }

    static std::string makeFileKey(const std::string &value)
    {
        return upperKey(stripMrmacExtension(trimAscii(value)));
    }

    static bool readTextFile(const std::string &path, std::string &outContent)
    {
        std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
        std::ostringstream buffer;
        if (!in)
            return false;
        buffer << in.rdbuf();
        if (!in.good() && !in.eof())
            return false;
        outContent = buffer.str();
        return true;
    }

    static std::string resolveMacroFilePath(const std::string &spec)
    {
        std::string trimmed = trimAscii(spec);
        if (trimmed.empty())
            return std::string();
        if (fileExists(trimmed))
            return trimmed;
        if (upperKey(trimmed).size() < 6 || upperKey(trimmed).substr(upperKey(trimmed).size() - 6) != ".MRMAC")
        {
            std::string withExt = trimmed + ".mrmac";
            if (fileExists(withExt))
                return withExt;
        }
        return trimmed;
    }

    static bool macroIsRunning(const std::string &macroKey)
    {
        for (std::size_t i = 0; i < g_runtimeEnv.macroStack.size(); ++i)
            if (upperKey(g_runtimeEnv.macroStack[i].macroName) == macroKey)
                return true;
        return false;
    }

    static bool removeMacroFromRegistryByKey(const std::string &macroKey)
    {
        std::map<std::string, MacroRef>::iterator it = g_runtimeEnv.loadedMacros.find(macroKey);
        if (it == g_runtimeEnv.loadedMacros.end())
            return false;

        const std::string fileKey = it->second.fileKey;
        g_runtimeEnv.loadedMacros.erase(it);
        g_runtimeEnv.macroOrder.erase(std::remove(g_runtimeEnv.macroOrder.begin(), g_runtimeEnv.macroOrder.end(), macroKey), g_runtimeEnv.macroOrder.end());

        std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
        if (fit != g_runtimeEnv.loadedFiles.end())
        {
            fit->second.macroNames.erase(std::remove(fit->second.macroNames.begin(), fit->second.macroNames.end(), macroKey), fit->second.macroNames.end());
            if (fit->second.macroNames.empty())
                g_runtimeEnv.loadedFiles.erase(fit);
        }
        return true;
    }

    static bool parseRunMacroSpec(const std::string &spec, std::string &filePart, std::string &macroPart, std::string &paramPart)
    {
        std::string trimmed = trimAscii(spec);
        std::size_t spacePos;
        std::string head;
        std::size_t caretPos;

        filePart.clear();
        macroPart.clear();
        paramPart.clear();

        if (trimmed.empty())
            return false;

        spacePos = trimmed.find_first_of(" \t\r\n");
        if (spacePos == std::string::npos)
            head = trimmed;
        else
        {
            head = trimmed.substr(0, spacePos);
            paramPart = trimAscii(trimmed.substr(spacePos + 1));
        }

        caretPos = head.find('^');
        if (caretPos == std::string::npos)
            macroPart = head;
        else
        {
            filePart = head.substr(0, caretPos);
            macroPart = head.substr(caretPos + 1);
        }
        return !macroPart.empty();
    }

    static bool fileContainsOnlyTransientMacros(const LoadedMacroFile &file)
    {
        if (file.macroNames.empty())
            return false;
        for (std::size_t i = 0; i < file.macroNames.size(); ++i)
        {
            std::map<std::string, MacroRef>::const_iterator mit = g_runtimeEnv.loadedMacros.find(file.macroNames[i]);
            if (mit == g_runtimeEnv.loadedMacros.end() || !mit->second.transientAttr)
                return false;
        }
        return true;
    }

    static bool refreshLoadedFileBytecode(const std::string &fileKey)
    {
        std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
        std::string source;
        unsigned char *compiled = NULL;
        size_t compiledSize = 0;
        int macroCount;

        if (fit == g_runtimeEnv.loadedFiles.end())
            return false;
        if (fit->second.resolvedPath.empty() || !readTextFile(fit->second.resolvedPath, source))
        {
            g_runtimeEnv.errorLevel = 5001;
            return false;
        }

        compiled = compile_macro_code(source.c_str(), &compiledSize);
        if (compiled == NULL)
        {
            g_runtimeEnv.errorLevel = 5005;
            return false;
        }

        fit->second.bytecode.assign(compiled, compiled + compiledSize);
        std::free(compiled);

        macroCount = get_compiled_macro_count();
        for (int i = 0; i < macroCount; ++i)
        {
            const char *macroNameText = get_compiled_macro_name(i);
            int entry = get_compiled_macro_entry(i);
            int flags = get_compiled_macro_flags(i);
            std::string displayName = macroNameText != NULL ? macroNameText : std::string();
            std::string macroKey = upperKey(displayName);
            std::map<std::string, MacroRef>::iterator mit = g_runtimeEnv.loadedMacros.find(macroKey);

            if (displayName.empty() || entry < 0)
                continue;
            if (mit == g_runtimeEnv.loadedMacros.end() || mit->second.fileKey != fileKey)
                continue;

            mit->second.displayName = displayName;
            mit->second.entryOffset = static_cast<std::size_t>(entry);
            mit->second.transientAttr = (flags & MACRO_ATTR_TRANS) != 0;
            mit->second.dumpAttr = (flags & MACRO_ATTR_DUMP) != 0;
            mit->second.permAttr = (flags & MACRO_ATTR_PERM) != 0;
        }

        g_runtimeEnv.errorLevel = 0;
        return true;
    }

    static bool ensureLoadedFileResident(const std::string &fileKey)
    {
        std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
        if (fit == g_runtimeEnv.loadedFiles.end())
            return false;
        if (!fit->second.bytecode.empty())
            return true;
        return refreshLoadedFileBytecode(fileKey);
    }

    static bool evictTransientFileImage(const std::string &fileKey)
    {
        std::map<std::string, LoadedMacroFile>::iterator fit = g_runtimeEnv.loadedFiles.find(fileKey);
        if (fit == g_runtimeEnv.loadedFiles.end())
            return false;
        if (!fileContainsOnlyTransientMacros(fit->second))
            return false;
        for (std::size_t i = 0; i < fit->second.macroNames.size(); ++i)
            if (macroIsRunning(fit->second.macroNames[i]))
                return false;
        fit->second.bytecode.clear();
        fit->second.bytecode.shrink_to_fit();
        return true;
    }

    static bool loadMacroFileIntoRegistry(const std::string &spec, std::string *loadedFileKey)
    {
        std::string resolvedPath = resolveMacroFilePath(spec);
        std::string fileKey = makeFileKey(spec);
        std::string source;
        LoadedMacroFile newFile;
        unsigned char *compiled = NULL;
        size_t compiledSize = 0;
        int macroCount;

        if (loadedFileKey != NULL)
            loadedFileKey->clear();

        if (resolvedPath.empty() || !readTextFile(resolvedPath, source))
        {
            g_runtimeEnv.errorLevel = 5001;
            return false;
        }

        std::map<std::string, LoadedMacroFile>::iterator existingFile = g_runtimeEnv.loadedFiles.find(fileKey);
        if (existingFile != g_runtimeEnv.loadedFiles.end())
        {
            for (std::size_t i = 0; i < existingFile->second.macroNames.size(); ++i)
                if (macroIsRunning(existingFile->second.macroNames[i]))
                {
                    g_runtimeEnv.errorLevel = 5006;
                    return false;
                }
        }

        compiled = compile_macro_code(source.c_str(), &compiledSize);
        if (compiled == NULL)
        {
            g_runtimeEnv.errorLevel = 5005;
            return false;
        }

        macroCount = get_compiled_macro_count();
        for (int i = 0; i < macroCount; ++i)
        {
            const char *macroNameText = get_compiled_macro_name(i);
            std::string displayName = macroNameText != NULL ? macroNameText : std::string();
            std::string macroKey = upperKey(displayName);
            std::map<std::string, MacroRef>::iterator mit;

            if (displayName.empty())
                continue;

            mit = g_runtimeEnv.loadedMacros.find(macroKey);
            if (mit != g_runtimeEnv.loadedMacros.end())
            {
                if (macroIsRunning(macroKey) || mit->second.permAttr)
                {
                    std::free(compiled);
                    g_runtimeEnv.errorLevel = 5006;
                    return false;
                }
            }
        }

        newFile.fileKey = fileKey;
        newFile.displayName = trimAscii(spec);
        newFile.resolvedPath = resolvedPath;
        newFile.bytecode.assign(compiled, compiled + compiledSize);
        std::free(compiled);

        if (existingFile != g_runtimeEnv.loadedFiles.end())
        {
            std::vector<std::string> oldNames = existingFile->second.macroNames;
            for (std::size_t i = 0; i < oldNames.size(); ++i)
                removeMacroFromRegistryByKey(oldNames[i]);
        }

        for (int i = 0; i < macroCount; ++i)
        {
            const char *macroNameText = get_compiled_macro_name(i);
            int entry = get_compiled_macro_entry(i);
            int flags = get_compiled_macro_flags(i);
            std::string displayName = macroNameText != NULL ? macroNameText : std::string();
            std::string macroKey = upperKey(displayName);
            MacroRef ref;

            if (displayName.empty() || entry < 0)
                continue;
            removeMacroFromRegistryByKey(macroKey);

            ref.fileKey = fileKey;
            ref.displayName = displayName;
            ref.entryOffset = static_cast<std::size_t>(entry);
            ref.firstRunPending = true;
            ref.transientAttr = (flags & MACRO_ATTR_TRANS) != 0;
            ref.dumpAttr = (flags & MACRO_ATTR_DUMP) != 0;
            ref.permAttr = (flags & MACRO_ATTR_PERM) != 0;
            g_runtimeEnv.loadedMacros[macroKey] = ref;
            g_runtimeEnv.macroOrder.push_back(macroKey);
            newFile.macroNames.push_back(macroKey);
        }

        g_runtimeEnv.loadedFiles[fileKey] = newFile;
        g_runtimeEnv.errorLevel = 0;
        if (loadedFileKey != NULL)
            *loadedFileKey = fileKey;
        return true;
    }

    static bool unloadMacroFromRegistry(const std::string &macroName)
    {
        std::string macroKey = upperKey(trimAscii(macroName));
        if (macroKey.empty())
            return false;
        if (macroIsRunning(macroKey))
        {
            g_runtimeEnv.errorLevel = 5006;
            return false;
        }
        if (!removeMacroFromRegistryByKey(macroKey))
            return false;
        g_runtimeEnv.errorLevel = 0;
        return true;
    }

    static Value applyIntrinsic(const std::string &name, const std::vector<Value> &args)
    {
        if (name == "STR")
        {
            if (args.size() != 1 || args[0].type != TYPE_INT)
                throw std::runtime_error("STR expects one integer argument.");
            return makeString(valueAsString(args[0]));
        }
        if (name == "CHAR")
        {
            if (args.size() != 1 || args[0].type != TYPE_INT)
                throw std::runtime_error("CHAR expects one integer argument.");
            return makeChar(static_cast<unsigned char>(args[0].i & 0xFF));
        }
        if (name == "ASCII")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("ASCII expects one string argument.");
            std::string s = valueAsString(args[0]);
            return makeInt(s.empty() ? 0 : static_cast<unsigned char>(s[0]));
        }
        if (name == "CAPS")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("CAPS expects one string argument.");
            return makeString(uppercaseAscii(valueAsString(args[0])));
        }
        if (name == "COPY")
        {
            std::string s;
            int pos;
            int count;
            std::size_t start;
            if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT)
                throw std::runtime_error("COPY expects (string, int, int).");
            s = valueAsString(args[0]);
            pos = checkedStringIndex(args[1].i);
            count = args[2].i;
            if (count < 0)
                throw std::runtime_error("Invalid string index on string copy operation.");
            if (static_cast<std::size_t>(pos) > s.size())
                return makeString("");
            start = static_cast<std::size_t>(pos - 1);
            return makeString(s.substr(start, static_cast<std::size_t>(count)));
        }
        if (name == "LENGTH")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("LENGTH expects one string argument.");
            return makeInt(static_cast<int>(valueAsString(args[0]).size()));
        }
        if (name == "POS")
        {
            std::string needle;
            std::string haystack;
            std::size_t pos;
            if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1]))
                throw std::runtime_error("POS expects (substring, string).");
            needle = valueAsString(args[0]);
            haystack = valueAsString(args[1]);
            if (needle.empty())
                return makeInt(1);
            pos = haystack.find(needle);
            return makeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
        }
        if (name == "XPOS")
        {
            std::string needle;
            std::string haystack;
            int startPos;
            std::size_t pos;
            if (args.size() != 3 || !isStringLike(args[0]) || !isStringLike(args[1]) || args[2].type != TYPE_INT)
                throw std::runtime_error("XPOS expects (substring, string, int).");
            needle = valueAsString(args[0]);
            haystack = valueAsString(args[1]);
            startPos = checkedStringIndex(args[2].i);
            if (needle.empty())
                return makeInt(startPos <= static_cast<int>(haystack.size()) + 1 ? startPos : 0);
            if (static_cast<std::size_t>(startPos) > haystack.size())
                return makeInt(0);
            pos = haystack.find(needle, static_cast<std::size_t>(startPos - 1));
            return makeInt(pos == std::string::npos ? 0 : static_cast<int>(pos + 1));
        }
        if (name == "STR_DEL")
        {
            std::string s;
            int pos;
            int count;
            std::size_t start;
            if (args.size() != 3 || !isStringLike(args[0]) || args[1].type != TYPE_INT || args[2].type != TYPE_INT)
                throw std::runtime_error("STR_DEL expects (string, int, int).");
            s = valueAsString(args[0]);
            pos = checkedStringIndex(args[1].i);
            count = args[2].i;
            if (count < 0)
                throw std::runtime_error("Invalid string index on string copy operation.");
            if (static_cast<std::size_t>(pos) > s.size())
                return makeString(s);
            start = static_cast<std::size_t>(pos - 1);
            s.erase(start, static_cast<std::size_t>(count));
            return makeString(s);
        }
        if (name == "STR_INS")
        {
            std::string target;
            std::string dest;
            int location;
            std::size_t insertPos;
            if (args.size() != 3 || !isStringLike(args[0]) || !isStringLike(args[1]) || args[2].type != TYPE_INT)
                throw std::runtime_error("STR_INS expects (string, string, int).");
            target = valueAsString(args[0]);
            dest = valueAsString(args[1]);
            location = checkedInsertIndex(args[2].i);
            insertPos = static_cast<std::size_t>(location);
            if (insertPos > dest.size())
                insertPos = dest.size();
            dest.insert(insertPos, target);
            enforceStringLength(dest);
            return makeString(dest);
        }
        if (name == "REAL_I")
        {
            if (args.size() != 1 || args[0].type != TYPE_INT)
                throw std::runtime_error("REAL_I expects one integer argument.");
            return makeReal(static_cast<double>(args[0].i));
        }
        if (name == "INT_R")
        {
            if (args.size() != 1 || args[0].type != TYPE_REAL)
                throw std::runtime_error("INT_R expects one real argument.");
            if (args[0].r < static_cast<double>(std::numeric_limits<int>::min()) ||
                args[0].r > static_cast<double>(std::numeric_limits<int>::max()))
                throw std::runtime_error("Real to Integer conversion out of range.");
            return makeInt(static_cast<int>(args[0].r));
        }
        if (name == "RSTR")
        {
            char fmt[32];
            char buf[256];
            int width;
            int precision;

            if (args.size() != 3 || args[0].type != TYPE_REAL || args[1].type != TYPE_INT || args[2].type != TYPE_INT)
                throw std::runtime_error("RSTR expects (real, int, int).");

            width = args[1].i;
            precision = args[2].i;
            if (width < 0)
                width = 0;
            if (precision < 0)
                precision = 0;
            if (precision > 20)
                precision = 20;

            std::snprintf(fmt, sizeof(fmt), "%%%d.%df", width, precision);
            std::snprintf(buf, sizeof(buf), fmt, args[0].r);
            enforceStringLength(buf);
            return makeString(buf);
        }
        if (name == "REMOVE_SPACE")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("REMOVE_SPACE expects one string argument.");
            return makeString(removeSpaceAscii(valueAsString(args[0])));
        }
        if (name == "GET_EXTENSION")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("GET_EXTENSION expects one string argument.");
            return makeString(getExtensionPart(valueAsString(args[0])));
        }
        if (name == "GET_PATH")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("GET_PATH expects one string argument.");
            return makeString(getPathPart(valueAsString(args[0])));
        }
        if (name == "TRUNCATE_EXTENSION")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("TRUNCATE_EXTENSION expects one string argument.");
            return makeString(truncateExtensionPart(valueAsString(args[0])));
        }
        if (name == "TRUNCATE_PATH")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("TRUNCATE_PATH expects one string argument.");
            return makeString(truncatePathPart(valueAsString(args[0])));
        }
        if (name == "FILE_EXISTS")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("FILE_EXISTS expects one string argument.");
            return makeInt(fileExistsPath(valueAsString(args[0])) ? 1 : 0);
        }
        if (name == "FIRST_FILE")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("FIRST_FILE expects one string argument.");
            return makeInt(findFirstFileMatch(valueAsString(args[0])));
        }
        if (name == "NEXT_FILE")
        {
            if (!args.empty())
                throw std::runtime_error("NEXT_FILE expects no arguments.");
            return makeInt(findNextFileMatch());
        }
        if (name == "SEARCH_FWD")
        {
            TFileEditor *editor;
            std::size_t matchStart = 0;
            std::size_t matchEnd = 0;
            TMREditWindow *win;
            if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT)
                throw std::runtime_error("SEARCH_FWD expects (string, int).");
            if (valueAsString(args[0]).empty())
            {
                g_runtimeEnv.errorLevel = 1010;
                return makeInt(0);
            }
            editor = currentEditor();
            if (editor == NULL)
                return makeInt(0);
            if (!searchEditorForward(editor, valueAsString(args[0]), valueAsInt(args[1]), g_runtimeEnv.ignoreCase, matchStart, matchEnd))
            {
                g_runtimeEnv.lastSearchValid = false;
                g_runtimeEnv.errorLevel = 0;
                return makeInt(0);
            }
            editor->setCurPtr(static_cast<uint>(matchStart), 0);
            editor->setSelect(static_cast<uint>(matchStart), static_cast<uint>(matchEnd), False);
            editor->trackCursor(True);
            editor->doUpdate();
            win = currentEditWindow();
            g_runtimeEnv.lastSearchValid = true;
            g_runtimeEnv.lastSearchWindow = win;
            g_runtimeEnv.lastSearchFileName = win != NULL ? std::string(win->currentFileName()) : std::string();
            g_runtimeEnv.lastSearchStart = matchStart;
            g_runtimeEnv.lastSearchEnd = matchEnd;
            g_runtimeEnv.lastSearchCursor = matchStart;
            g_runtimeEnv.errorLevel = 0;
            return makeInt(1);
        }
        if (name == "SEARCH_BWD")
        {
            TFileEditor *editor;
            std::size_t matchStart = 0;
            std::size_t matchEnd = 0;
            TMREditWindow *win;
            if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT)
                throw std::runtime_error("SEARCH_BWD expects (string, int).");
            if (valueAsString(args[0]).empty())
            {
                g_runtimeEnv.errorLevel = 1010;
                return makeInt(0);
            }
            editor = currentEditor();
            if (editor == NULL)
                return makeInt(0);
            if (!searchEditorBackward(editor, valueAsString(args[0]), valueAsInt(args[1]), g_runtimeEnv.ignoreCase, matchStart, matchEnd))
            {
                g_runtimeEnv.lastSearchValid = false;
                g_runtimeEnv.errorLevel = 0;
                return makeInt(0);
            }
            editor->setCurPtr(static_cast<uint>(matchStart), 0);
            editor->setSelect(static_cast<uint>(matchStart), static_cast<uint>(matchEnd), False);
            editor->trackCursor(True);
            editor->doUpdate();
            win = currentEditWindow();
            g_runtimeEnv.lastSearchValid = true;
            g_runtimeEnv.lastSearchWindow = win;
            g_runtimeEnv.lastSearchFileName = win != NULL ? std::string(win->currentFileName()) : std::string();
            g_runtimeEnv.lastSearchStart = matchStart;
            g_runtimeEnv.lastSearchEnd = matchEnd;
            g_runtimeEnv.lastSearchCursor = matchStart;
            g_runtimeEnv.errorLevel = 0;
            return makeInt(1);
        }
        if (name == "GET_ENVIRONMENT")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("GET_ENVIRONMENT expects one string argument.");
            return makeString(getEnvironmentValue(valueAsString(args[0])));
        }
        if (name == "GET_WORD")
        {
            TFileEditor *editor;
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("GET_WORD expects one string argument.");
            editor = currentEditor();
            if (editor == NULL)
                return makeString("");
            return makeString(currentEditorWord(editor, valueAsString(args[0])));
        }
        if (name == "PARAM_STR")
        {
            int index;
            if (args.size() != 1 || args[0].type != TYPE_INT)
                throw std::runtime_error("PARAM_STR expects one integer argument.");
            index = valueAsInt(args[0]);
            if (index == 0)
                return makeString(g_runtimeEnv.startupCommand);
            if (index < 0 || static_cast<std::size_t>(index) > g_runtimeEnv.processArgs.size())
                return makeString("");
            return makeString(g_runtimeEnv.processArgs[static_cast<std::size_t>(index - 1)]);
        }
        if (name == "GLOBAL_STR")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("GLOBAL_STR expects one string argument.");
            std::string key = upperKey(valueAsString(args[0]));
            std::map<std::string, GlobalEntry>::const_iterator it = g_runtimeEnv.globals.find(key);
            if (it == g_runtimeEnv.globals.end() || it->second.type != TYPE_STR)
                return makeString("");
            return makeString(valueAsString(it->second.value));
        }
        if (name == "GLOBAL_INT")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("GLOBAL_INT expects one string argument.");
            std::string key = upperKey(valueAsString(args[0]));
            std::map<std::string, GlobalEntry>::const_iterator it = g_runtimeEnv.globals.find(key);
            if (it == g_runtimeEnv.globals.end() || it->second.type != TYPE_INT)
                return makeInt(0);
            return makeInt(valueAsInt(it->second.value));
        }
        if (name == "PARSE_STR")
        {
            if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1]))
                throw std::runtime_error("PARSE_STR expects (string, string).");
            return makeString(parseNamedValue(valueAsString(args[0]), valueAsString(args[1])));
        }
        if (name == "PARSE_INT")
        {
            std::string parsed;
            int errorPos;
            if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1]))
                throw std::runtime_error("PARSE_INT expects (string, string).");
            parsed = parseNamedValue(valueAsString(args[0]), valueAsString(args[1]));
            if (parsed.empty())
                return makeInt(0);
            errorPos = findValErrorPosition(parsed);
            if (errorPos != 0)
                return makeInt(0);
            return makeInt(static_cast<int>(std::strtol(parsed.c_str(), NULL, 10)));
        }
        if (name == "INQ_MACRO")
        {
            if (args.size() != 1 || !isStringLike(args[0]))
                throw std::runtime_error("INQ_MACRO expects one string argument.");
            return makeInt(g_runtimeEnv.loadedMacros.find(upperKey(valueAsString(args[0]))) != g_runtimeEnv.loadedMacros.end() ? 1 : 0);
        }

        throw std::runtime_error("Unknown intrinsic: " + name);
    }
}

void mrvmSetProcessContext(int argc, char **argv)
{
    g_runtimeEnv.startupCommand.clear();
    g_runtimeEnv.processArgs.clear();
    if (argc > 0 && argv != NULL && argv[0] != NULL)
        g_runtimeEnv.startupCommand = argv[0];
    for (int i = 1; argv != NULL && i < argc; ++i)
        g_runtimeEnv.processArgs.push_back(argv[i] != NULL ? std::string(argv[i]) : std::string());
    g_runtimeEnv.executablePath = detectExecutablePathFromProc();
    if (g_runtimeEnv.executablePath.empty() && !g_runtimeEnv.startupCommand.empty())
        g_runtimeEnv.executablePath = g_runtimeEnv.startupCommand;
    g_runtimeEnv.executableDir = detectExecutableDir(g_runtimeEnv.startupCommand);
    g_runtimeEnv.shellPath = detectShellPath();
    g_runtimeEnv.shellVersion = detectShellVersion(g_runtimeEnv.shellPath);
}

VirtualMachine::Value::Value() : type(TYPE_INT), i(0), r(0.0), s(), c(0) {}

VirtualMachine::VirtualMachine()
{
}

void VirtualMachine::push(const Value &value)
{
    stack.push_back(value);
}

VirtualMachine::Value VirtualMachine::pop()
{
    if (!stack.empty())
    {
        Value value = stack.back();
        stack.pop_back();
        return value;
    }

    log.push_back("VM Error: Stack underflow.");
    return makeInt(0);
}


void VirtualMachine::execute(const unsigned char *bytecode, size_t length)
{
    executeAt(bytecode, length, 0, std::string(), std::string(), true, false);
}

void VirtualMachine::executeAt(const unsigned char *bytecode, size_t length, size_t entryOffset,
                               const std::string &parameterString, const std::string &macroName,
                               bool resetState, bool firstRun)
{
    size_t ip = entryOffset;
    std::vector<size_t> call_stack;
    std::string savedParameterString = g_runtimeEnv.parameterString;
    bool pushedMacroFrame = false;

    variables.clear();
    stack.clear();
    if (resetState)
    {
        log.clear();
        g_runtimeEnv.globalEnumIndex = 0;
        g_runtimeEnv.macroEnumIndex = 0;
        g_runtimeEnv.parameterString.clear();
        g_runtimeEnv.returnInt = 0;
        g_runtimeEnv.returnStr.clear();
        g_runtimeEnv.errorLevel = 0;
    }

    if (!macroName.empty())
    {
        g_runtimeEnv.macroStack.push_back(MacroStackFrame(macroName, firstRun));
        pushedMacroFrame = true;
    }
    g_runtimeEnv.parameterString = parameterString;

    auto readInt = [&](int &value) {
        std::memcpy(&value, &bytecode[ip], sizeof(int));
        ip += sizeof(int);
    };

    auto readDouble = [&](double &value) {
        std::memcpy(&value, &bytecode[ip], sizeof(double));
        ip += sizeof(double);
    };

    auto readCString = [&](std::string &value) {
        const char *textp = reinterpret_cast<const char *>(&bytecode[ip]);
        value = textp;
        ip += value.size() + 1;
    };

    auto popArgs = [&](unsigned char count) {
        std::vector<Value> args;
        args.reserve(count);
        for (unsigned char i = 0; i < count; ++i)
            args.push_back(pop());
        std::reverse(args.begin(), args.end());
        return args;
    };

    try
    {
        while (ip < length)
        {
            unsigned char opcode = bytecode[ip++];

            if (opcode == OP_PUSH_I)
            {
                int val;
                readInt(val);
                push(makeInt(val));
                log.push_back("Push integer: " + std::to_string(val));
            }
            else if (opcode == OP_PUSH_R)
            {
                double val;
                readDouble(val);
                push(makeReal(val));
                log.push_back("Push real: " + valueAsString(makeReal(val)));
            }
            else if (opcode == OP_PUSH_S)
            {
                std::string str;
                readCString(str);
                enforceStringLength(str);
                push(makeString(str));
                log.push_back("Push string: " + str);
            }
            else if (opcode == OP_DEF_VAR)
            {
                std::string varName;
                int varType = static_cast<int>(bytecode[ip++]);
                readCString(varName);
                variables[varName] = defaultValueForType(varType);
                log.push_back("Define variable: " + varName);
            }
            else if (opcode == OP_LOAD_VAR)
            {
                std::string varName;
                bool handled = false;
                readCString(varName);

                Value special = loadSpecialVariable(varName, handled);
                if (handled)
                    push(special);
                else
                {
                    std::map<std::string, Value>::const_iterator it = variables.find(varName);
                    if (it == variables.end())
                        variables[varName] = makeInt(0);
                    push(variables[varName]);
                }
                log.push_back("Load variable: " + varName);
            }
            else if (opcode == OP_STORE_VAR)
            {
                std::string varName;
                int targetType = static_cast<int>(bytecode[ip++]);
                readCString(varName);
                Value value = coerceForStore(pop(), targetType);
                if (value.type == TYPE_STR)
                    enforceStringLength(value.s);
                if (!storeSpecialVariable(varName, value))
                    variables[varName] = value;
                log.push_back("Store variable: " + varName);
            }
            else if (opcode == OP_GOTO)
            {
                int target;
                readInt(target);
                if (target < 0 || static_cast<size_t>(target) >= length)
                    throw std::runtime_error("Invalid jump target in GOTO.");
                ip = static_cast<size_t>(target);
            }
            else if (opcode == OP_CALL)
            {
                int target;
                readInt(target);
                if (target < 0 || static_cast<size_t>(target) >= length)
                    throw std::runtime_error("Invalid jump target in CALL.");
                call_stack.push_back(ip);
                ip = static_cast<size_t>(target);
            }
            else if (opcode == OP_RET)
            {
                if (call_stack.empty())
                    throw std::runtime_error("RET without matching CALL.");
                ip = call_stack.back();
                call_stack.pop_back();
            }
            else if (opcode == OP_JZ)
            {
                int target;
                Value cond;
                readInt(target);
                cond = pop();
                if (cond.type != TYPE_INT)
                    throw std::runtime_error("IF/WHILE expression must be integer.");
                if (target < 0 || static_cast<size_t>(target) >= length)
                    throw std::runtime_error("Invalid jump target in JZ.");
                if (cond.i == 0)
                    ip = static_cast<size_t>(target);
            }
            else if (opcode == OP_ADD)
            {
                Value b = pop();
                Value a = pop();
                if (isStringLike(a) && isStringLike(b))
                {
                    std::string s = valueAsString(a) + valueAsString(b);
                    enforceStringLength(s);
                    push(makeString(s));
                }
                else if (isNumeric(a) && isNumeric(b))
                {
                    if (a.type == TYPE_REAL || b.type == TYPE_REAL)
                        push(makeReal(valueAsReal(a) + valueAsReal(b)));
                    else
                        push(makeInt(a.i + b.i));
                }
                else
                    throw std::runtime_error("Type mismatch or syntax error.");
            }
            else if (opcode == OP_SUB)
            {
                Value b = pop();
                Value a = pop();
                if (!isNumeric(a) || !isNumeric(b))
                    throw std::runtime_error("Type mismatch or syntax error.");
                if (a.type == TYPE_REAL || b.type == TYPE_REAL)
                    push(makeReal(valueAsReal(a) - valueAsReal(b)));
                else
                    push(makeInt(a.i - b.i));
            }
            else if (opcode == OP_MUL)
            {
                Value b = pop();
                Value a = pop();
                if (!isNumeric(a) || !isNumeric(b))
                    throw std::runtime_error("Type mismatch or syntax error.");
                if (a.type == TYPE_REAL || b.type == TYPE_REAL)
                    push(makeReal(valueAsReal(a) * valueAsReal(b)));
                else
                    push(makeInt(a.i * b.i));
            }
            else if (opcode == OP_DIV)
            {
                Value b = pop();
                Value a = pop();
                if (!isNumeric(a) || !isNumeric(b))
                    throw std::runtime_error("Type mismatch or syntax error.");
                if ((b.type == TYPE_REAL && b.r == 0.0) || (b.type == TYPE_INT && b.i == 0))
                    throw std::runtime_error("Division by zero.");
                if (a.type == TYPE_REAL || b.type == TYPE_REAL)
                    push(makeReal(valueAsReal(a) / valueAsReal(b)));
                else
                    push(makeInt(a.i / b.i));
            }
            else if (opcode == OP_MOD)
            {
                Value b = pop();
                Value a = pop();
                if (a.type != TYPE_INT || b.type != TYPE_INT)
                    throw std::runtime_error("Type mismatch or syntax error.");
                if (b.i == 0)
                    throw std::runtime_error("Modulo by zero.");
                push(makeInt(a.i % b.i));
            }
            else if (opcode == OP_NEG)
            {
                Value a = pop();
                if (!isNumeric(a))
                    throw std::runtime_error("Type mismatch or syntax error.");
                if (a.type == TYPE_REAL)
                    push(makeReal(-a.r));
                else
                    push(makeInt(-a.i));
            }
            else if (opcode == OP_CMP_EQ || opcode == OP_CMP_NE || opcode == OP_CMP_LT ||
                     opcode == OP_CMP_GT || opcode == OP_CMP_LE || opcode == OP_CMP_GE)
            {
                Value b = pop();
                Value a = pop();
                int cmp = compareValues(a, b);
                int result = 0;
                switch (opcode)
                {
                case OP_CMP_EQ: result = (cmp == 0); break;
                case OP_CMP_NE: result = (cmp != 0); break;
                case OP_CMP_LT: result = (cmp < 0); break;
                case OP_CMP_GT: result = (cmp > 0); break;
                case OP_CMP_LE: result = (cmp <= 0); break;
                case OP_CMP_GE: result = (cmp >= 0); break;
                }
                push(makeInt(result));
            }
            else if (opcode == OP_AND)
            {
                Value b = pop();
                Value a = pop();
                push(makeInt((valueAsInt(a) != 0 && valueAsInt(b) != 0) ? 1 : 0));
            }
            else if (opcode == OP_OR)
            {
                Value b = pop();
                Value a = pop();
                push(makeInt((valueAsInt(a) != 0 || valueAsInt(b) != 0) ? 1 : 0));
            }
            else if (opcode == OP_NOT)
            {
                Value a = pop();
                push(makeInt(valueAsInt(a) == 0 ? 1 : 0));
            }
            else if (opcode == OP_SHL)
            {
                Value b = pop();
                Value a = pop();
                push(makeInt(valueAsInt(a) << valueAsInt(b)));
            }
            else if (opcode == OP_SHR)
            {
                Value b = pop();
                Value a = pop();
                push(makeInt(valueAsInt(a) >> valueAsInt(b)));
            }
            else if (opcode == OP_INTRINSIC)
            {
                std::string name;
                readCString(name);
                unsigned char argc = bytecode[ip++];
                std::vector<Value> args = popArgs(argc);
                push(applyIntrinsic(name, args));
            }
            else if (opcode == OP_VAL || opcode == OP_RVAL)
            {
                std::string varName;
                Value source;
                int resultCode = 0;
                readCString(varName);
                source = pop();
                if (!isStringLike(source))
                    throw std::runtime_error("Type mismatch or syntax error.");

                std::string textValue = valueAsString(source);
                if (opcode == OP_VAL)
                {
                    int errorPos = findValErrorPosition(textValue);
                    if (errorPos == 0)
                    {
                        long long parsed = std::strtoll(textValue.c_str(), NULL, 10);
                        if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) ||
                            parsed > static_cast<long long>(std::numeric_limits<int>::max()))
                            throw std::runtime_error("Real to Integer conversion out of range.");
                        variables[varName] = makeInt(static_cast<int>(parsed));
                    }
                    else
                        resultCode = errorPos;
                }
                else
                {
                    int errorPos = findRValErrorPosition(textValue);
                    if (errorPos == 0)
                    {
                        char *endPtr = NULL;
                        double parsed = std::strtod(textValue.c_str(), &endPtr);
                        (void) endPtr;
                        variables[varName] = makeReal(parsed);
                    }
                    else
                        resultCode = errorPos;
                }
                push(makeInt(resultCode));
            }
            else if (opcode == OP_FIRST_GLOBAL || opcode == OP_NEXT_GLOBAL)
            {
                std::string targetVar;
                readCString(targetVar);
                if (opcode == OP_FIRST_GLOBAL)
                    g_runtimeEnv.globalEnumIndex = 0;

                while (g_runtimeEnv.globalEnumIndex < g_runtimeEnv.globalOrder.size())
                {
                    const std::string &key = g_runtimeEnv.globalOrder[g_runtimeEnv.globalEnumIndex++];
                    std::map<std::string, GlobalEntry>::const_iterator it = g_runtimeEnv.globals.find(key);
                    if (it == g_runtimeEnv.globals.end())
                        continue;
                    variables[targetVar] = makeInt(it->second.type == TYPE_INT ? 1 : 0);
                    push(makeString(key));
                    goto handled_global_enum;
                }
                variables[targetVar] = makeInt(0);
                push(makeString(""));
            handled_global_enum:
                ;
            }
            else if (opcode == OP_PROC_VAR)
            {
                std::string name;
                std::string varName;
                std::map<std::string, Value>::iterator it;
                readCString(name);
                readCString(varName);
                it = variables.find(varName);
                if (it == variables.end())
                    throw std::runtime_error("Variable expected.");
                if (it->second.type != TYPE_STR)
                    throw std::runtime_error("Type mismatch or syntax error.");
                if (name == "CRUNCH_TABS")
                    it->second = makeString(crunchTabsString(valueAsString(it->second)));
                else if (name == "EXPAND_TABS")
                    it->second = makeString(expandTabsString(valueAsString(it->second), g_runtimeEnv.tabExpand));
                else if (name == "TABS_TO_SPACES")
                    it->second = makeString(tabsToSpacesString(valueAsString(it->second)));
                else
                    throw std::runtime_error("Unknown variable procedure.");
            }
            else if (opcode == OP_PROC)
            {
                std::string name;
                readCString(name);
                unsigned char argc = bytecode[ip++];
                std::vector<Value> args = popArgs(argc);
                if (name == "SET_GLOBAL_STR")
                {
                    if (args.size() != 2 || !isStringLike(args[0]) || !isStringLike(args[1]))
                        throw std::runtime_error("SET_GLOBAL_STR expects (string, string).");
                    setGlobalValue(valueAsString(args[0]), TYPE_STR, makeString(valueAsString(args[1])));
                }
                else if (name == "SET_GLOBAL_INT")
                {
                    if (args.size() != 2 || !isStringLike(args[0]) || args[1].type != TYPE_INT)
                        throw std::runtime_error("SET_GLOBAL_INT expects (string, int).");
                    setGlobalValue(valueAsString(args[0]), TYPE_INT, makeInt(args[1].i));
                }
                else if (name == "LOAD_MACRO_FILE")
                {
                    if (args.size() != 1 || !isStringLike(args[0]))
                        throw std::runtime_error("LOAD_MACRO_FILE expects one string argument.");
                    loadMacroFileIntoRegistry(valueAsString(args[0]), NULL);
                }
                else if (name == "UNLOAD_MACRO")
                {
                    if (args.size() != 1 || !isStringLike(args[0]))
                        throw std::runtime_error("UNLOAD_MACRO expects one string argument.");
                    unloadMacroFromRegistry(valueAsString(args[0]));
                }
                else if (name == "CHANGE_DIR")
                {
                    if (args.size() != 1 || !isStringLike(args[0]))
                        throw std::runtime_error("CHANGE_DIR expects one string argument.");
                    if (changeDirectoryPath(valueAsString(args[0])))
                        g_runtimeEnv.errorLevel = 0;
                    else
                        g_runtimeEnv.errorLevel = errno != 0 ? errno : 1;
                }
                else if (name == "DEL_FILE")
                {
                    if (args.size() != 1 || !isStringLike(args[0]))
                        throw std::runtime_error("DEL_FILE expects one string argument.");
                    if (deleteFilePath(valueAsString(args[0])))
                        g_runtimeEnv.errorLevel = 0;
                    else
                        g_runtimeEnv.errorLevel = errno != 0 ? errno : 1;
                }
                else if (name == "LOAD_FILE")
                {
                    TMREditWindow *win;
                    std::string path;
                    if (args.size() != 1 || !isStringLike(args[0]))
                        throw std::runtime_error("LOAD_FILE expects one string argument.");
                    path = expandUserPath(valueAsString(args[0]));
                    win = currentEditWindow();
                    if (win == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    if (!fileExistsPath(path))
                    {
                        g_runtimeEnv.errorLevel = 3002;
                        continue;
                    }
                    if (!win->loadFromFile(path.c_str()))
                    {
                        g_runtimeEnv.errorLevel = 3002;
                        continue;
                    }
                    g_runtimeEnv.lastFileName = win->currentFileName();
                    g_runtimeEnv.errorLevel = 0;
                }
                else if (name == "SAVE_FILE")
                {
                    TMREditWindow *win = currentEditWindow();
                    if (!args.empty())
                        throw std::runtime_error("SAVE_FILE expects no arguments.");
                    if (win == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    if (!win->saveCurrentFile())
                    {
                        g_runtimeEnv.errorLevel = 2002;
                        continue;
                    }
                    g_runtimeEnv.lastFileName = win->currentFileName();
                    g_runtimeEnv.errorLevel = 0;
                }
                else if (name == "SET_INDENT_LEVEL")
                {
                    if (!args.empty())
                        throw std::runtime_error("SET_INDENT_LEVEL expects no arguments.");
                    g_runtimeEnv.errorLevel = setCurrentEditorIndentLevel(currentEditorColumn(currentEditor())) ? 0 : 1001;
                }
                else if (name == "REPLACE")
                {
                    TFileEditor *editor;
                    if (args.size() != 1 || !isStringLike(args[0]))
                        throw std::runtime_error("REPLACE expects one string argument.");
                    editor = currentEditor();
                    if (editor == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    replaceLastSearch(editor, valueAsString(args[0]));
                    g_runtimeEnv.errorLevel = 0;
                }
                else if (name == "TEXT")
                {
                    TFileEditor *editor;
                    if (args.size() != 1 || !isStringLike(args[0]))
                        throw std::runtime_error("TEXT expects one string argument.");
                    editor = currentEditor();
                    if (editor == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    insertEditorText(editor, valueAsString(args[0]));
                    g_runtimeEnv.errorLevel = 0;
                }
                else if (name == "PUT_LINE")
                {
                    TFileEditor *editor;
                    if (args.size() != 1 || !isStringLike(args[0]))
                        throw std::runtime_error("PUT_LINE expects one string argument.");
                    editor = currentEditor();
                    if (editor == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    replaceEditorLine(editor, valueAsString(args[0]));
                    g_runtimeEnv.errorLevel = 0;
                }
                else if (name == "CR")
                {
                    TFileEditor *editor = currentEditor();
                    if (!args.empty())
                        throw std::runtime_error("CR expects no arguments.");
                    if (editor == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    carriageReturnEditor(editor);
                    g_runtimeEnv.errorLevel = 0;
                }
                else if (name == "DEL_CHAR")
                {
                    TFileEditor *editor = currentEditor();
                    if (!args.empty())
                        throw std::runtime_error("DEL_CHAR expects no arguments.");
                    if (editor == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    deleteEditorChars(editor, 1);
                    g_runtimeEnv.errorLevel = 0;
                }
                else if (name == "DEL_CHARS")
                {
                    TFileEditor *editor = currentEditor();
                    if (args.size() != 1 || args[0].type != TYPE_INT)
                        throw std::runtime_error("DEL_CHARS expects one integer argument.");
                    if (editor == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    deleteEditorChars(editor, valueAsInt(args[0]));
                    g_runtimeEnv.errorLevel = 0;
                }
                else if (name == "DEL_LINE")
                {
                    TFileEditor *editor = currentEditor();
                    if (!args.empty())
                        throw std::runtime_error("DEL_LINE expects no arguments.");
                    if (editor == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    deleteEditorLine(editor);
                    g_runtimeEnv.errorLevel = 0;
                }
                else if (name == "LEFT" || name == "RIGHT" || name == "UP" || name == "DOWN" ||
                         name == "HOME" || name == "EOL" || name == "TOF" || name == "EOF" ||
                         name == "WORD_LEFT" || name == "WORD_RIGHT" || name == "FIRST_WORD" ||
                         name == "MARK_POS" || name == "GOTO_MARK" || name == "POP_MARK" ||
                         name == "PAGE_UP" || name == "PAGE_DOWN" ||
                         name == "NEXT_PAGE_BREAK" || name == "LAST_PAGE_BREAK" ||
                         name == "TAB_RIGHT" || name == "TAB_LEFT" || name == "INDENT" || name == "UNDENT" ||
                         name == "BLOCK_BEGIN" || name == "COL_BLOCK_BEGIN" || name == "STR_BLOCK_BEGIN" ||
                         name == "BLOCK_END" || name == "BLOCK_OFF" || name == "COPY_BLOCK" ||
                         name == "MOVE_BLOCK" || name == "DELETE_BLOCK")
                {
                    TFileEditor *editor = currentEditor();
                    bool ok = false;
                    if (!args.empty())
                        throw std::runtime_error((name + " expects no arguments.").c_str());
                    if (editor == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    if (name == "LEFT")
                        ok = moveEditorLeft(editor);
                    else if (name == "RIGHT")
                        ok = moveEditorRight(editor);
                    else if (name == "UP")
                        ok = moveEditorUp(editor);
                    else if (name == "DOWN")
                        ok = moveEditorDown(editor);
                    else if (name == "HOME")
                        ok = moveEditorHome(editor);
                    else if (name == "EOL")
                        ok = moveEditorEol(editor);
                    else if (name == "TOF")
                        ok = moveEditorTof(editor);
                    else if (name == "EOF")
                        ok = moveEditorEof(editor);
                    else if (name == "WORD_LEFT")
                        ok = moveEditorWordLeft(editor);
                    else if (name == "WORD_RIGHT")
                        ok = moveEditorWordRight(editor);
                    else if (name == "FIRST_WORD")
                        ok = moveEditorFirstWord(editor);
                    else if (name == "MARK_POS")
                        ok = markEditorPosition(currentEditWindow(), editor);
                    else if (name == "GOTO_MARK")
                        ok = gotoEditorMark(currentEditWindow(), editor);
                    else if (name == "POP_MARK")
                        ok = popEditorMark(currentEditWindow());
                    else if (name == "PAGE_UP")
                        ok = moveEditorPageUp(editor);
                    else if (name == "PAGE_DOWN")
                        ok = moveEditorPageDown(editor);
                    else if (name == "NEXT_PAGE_BREAK")
                        ok = moveEditorNextPageBreak(editor);
                    else if (name == "LAST_PAGE_BREAK")
                        ok = moveEditorLastPageBreak(editor);
                    else if (name == "TAB_RIGHT")
                        ok = moveEditorTabRight(editor);
                    else if (name == "TAB_LEFT")
                        ok = moveEditorTabLeft(editor);
                    else if (name == "INDENT")
                        ok = indentEditor(editor);
                    else if (name == "UNDENT")
                        ok = undentEditor(editor);
                    else if (name == "BLOCK_BEGIN")
                    {
                        currentEditWindow()->beginLineBlock();
                        ok = true;
                    }
                    else if (name == "COL_BLOCK_BEGIN")
                    {
                        currentEditWindow()->beginColumnBlock();
                        ok = true;
                    }
                    else if (name == "STR_BLOCK_BEGIN")
                    {
                        currentEditWindow()->beginStreamBlock();
                        ok = true;
                    }
                    else if (name == "BLOCK_END")
                    {
                        currentEditWindow()->endBlock();
                        ok = true;
                    }
                    else if (name == "BLOCK_OFF")
                    {
                        currentEditWindow()->clearBlock();
                        ok = true;
                    }
                    else if (name == "COPY_BLOCK")
                        ok = copyCurrentBlock(currentEditWindow(), editor);
                    else if (name == "MOVE_BLOCK")
                        ok = moveCurrentBlock(currentEditWindow(), editor);
                    else if (name == "DELETE_BLOCK")
                        ok = deleteCurrentBlock(currentEditWindow(), editor);
                    g_runtimeEnv.errorLevel = ok ? 0 : 1001;
                }
                else if (name == "GOTO_LINE")
                {
                    TFileEditor *editor = currentEditor();
                    if (args.size() != 1 || args[0].type != TYPE_INT)
                        throw std::runtime_error("GOTO_LINE expects one integer argument.");
                    if (editor == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    g_runtimeEnv.errorLevel = gotoEditorLine(editor, valueAsInt(args[0])) ? 0 : 1010;
                }
                else if (name == "GOTO_COL")
                {
                    TFileEditor *editor = currentEditor();
                    if (args.size() != 1 || args[0].type != TYPE_INT)
                        throw std::runtime_error("GOTO_COL expects one integer argument.");
                    if (editor == NULL)
                    {
                        g_runtimeEnv.errorLevel = 1001;
                        continue;
                    }
                    g_runtimeEnv.errorLevel = gotoEditorCol(editor, valueAsInt(args[0])) ? 0 : 1010;
                }
                else if (name == "RUN_MACRO")
                {
                    std::string spec;
                    std::string filePart;
                    std::string macroPart;
                    std::string paramPart;
                    std::string targetFileKey;
                    std::string macroKey;
                    std::map<std::string, MacroRef>::iterator mit;
                    std::map<std::string, LoadedMacroFile>::iterator fit;
                    VirtualMachine childVm;

                    if (args.size() != 1 || !isStringLike(args[0]))
                        throw std::runtime_error("RUN_MACRO expects one string argument.");

                    spec = valueAsString(args[0]);
                    if (!parseRunMacroSpec(spec, filePart, macroPart, paramPart))
                    {
                        g_runtimeEnv.errorLevel = 5001;
                        continue;
                    }

                    macroKey = upperKey(macroPart);
                    if (!filePart.empty())
                        targetFileKey = makeFileKey(filePart);

                    mit = g_runtimeEnv.loadedMacros.find(macroKey);
                    if (mit == g_runtimeEnv.loadedMacros.end() || (!targetFileKey.empty() && mit->second.fileKey != targetFileKey))
                    {
                        if (!filePart.empty())
                        {
                            if (!loadMacroFileIntoRegistry(filePart, &targetFileKey))
                                continue;
                        }
                        else
                        {
                            if (!loadMacroFileIntoRegistry(macroPart, &targetFileKey))
                                continue;
                        }
                        mit = g_runtimeEnv.loadedMacros.find(macroKey);
                    }

                    if (mit == g_runtimeEnv.loadedMacros.end() || (!targetFileKey.empty() && mit->second.fileKey != targetFileKey))
                    {
                        g_runtimeEnv.errorLevel = 5001;
                        continue;
                    }

                    fit = g_runtimeEnv.loadedFiles.find(mit->second.fileKey);
                    if (fit == g_runtimeEnv.loadedFiles.end())
                    {
                        g_runtimeEnv.errorLevel = 5001;
                        continue;
                    }
                    if (!ensureLoadedFileResident(mit->second.fileKey))
                        continue;
                    fit = g_runtimeEnv.loadedFiles.find(mit->second.fileKey);
                    if (fit == g_runtimeEnv.loadedFiles.end() || fit->second.bytecode.empty())
                    {
                        g_runtimeEnv.errorLevel = 5001;
                        continue;
                    }

                    {
                        bool childFirstRun = mit->second.firstRunPending;
                        bool childDump = mit->second.dumpAttr;
                        bool childTransient = mit->second.transientAttr;
                        std::string childMacroKey = macroKey;
                        std::string childFileKey = mit->second.fileKey;
                        mit->second.firstRunPending = false;
                        childVm.executeAt(fit->second.bytecode.data(), fit->second.bytecode.size(), mit->second.entryOffset,
                                          paramPart, mit->second.displayName, false, childFirstRun);
                        log.insert(log.end(), childVm.log.begin(), childVm.log.end());
                        if (childDump)
                            unloadMacroFromRegistry(childMacroKey);
                        else if (childTransient)
                            evictTransientFileImage(childFileKey);
                    }
                    g_runtimeEnv.errorLevel = 0;
                }
                else
                {
                    throw std::runtime_error("Unknown procedure: " + name);
                }
            }
            else if (opcode == OP_TVCALL)
            {
                std::string funcName;
                readCString(funcName);
                unsigned char argc = bytecode[ip++];
                std::vector<Value> args = popArgs(argc);

                log.push_back("TVCALL: " + funcName + " (" + std::to_string(argc) + " params)");

                if (funcName == "MessageBox")
                {
                    if (args.empty())
                        messageBox(mfInformation | mfOKButton, "%s", "");
                    else
                    {
                        std::string text;
                        for (size_t i = 0; i < args.size(); ++i)
                        {
                            if (i != 0)
                                text += ' ';
                            text += valueAsString(args[i]);
                        }
                        messageBox(mfInformation | mfOKButton, "%s", text.c_str());
                    }
                }
                else if (funcName == "RegisterTheme")
                {
                    if (args.size() == 17 && isStringLike(args[16]))
                    {
                        MRTheme newTheme;
                        std::string targetClass = valueAsString(args[16]);
                        for (int i = 0; i < 16; ++i)
                        {
                            if (args[i].type != TYPE_INT)
                                throw std::runtime_error("RegisterTheme palette values must be integers.");
                            newTheme.paletteData[i] = static_cast<char>(args[i].i & 0xFF);
                        }
                        newTheme.paletteData[16] = '\0';
                        MRThemeRegistry::instance().registerTheme(targetClass, newTheme);
                    }
                }
            }
            else if (opcode == OP_HALT)
            {
                log.push_back("Program end reached.");
                break;
            }
            else
            {
                char hexOp[10];
                std::snprintf(hexOp, sizeof(hexOp), "0x%02X", opcode);
                throw std::runtime_error(std::string("Unknown opcode ") + hexOp);
            }
        }
    }
    catch (const std::exception &ex)
    {
        log.push_back(std::string("VM Error: ") + ex.what());
    }

    g_runtimeEnv.parameterString = savedParameterString;
    if (pushedMacroFrame)
        g_runtimeEnv.macroStack.pop_back();
}
